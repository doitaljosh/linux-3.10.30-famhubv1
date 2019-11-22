/* sdp1404_tmu.c
 *
 * Copyright (c) 2013-2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * SDP1404 - Thermal Management support
 *
 */
 
#include <linux/io.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <mach/map.h>
#include <mach/sdp_thermal.h>

#define MAX_SENSOR_CNT	3

/* Register define */
/* 0x11250C70 base */
#define SDP_TSC_PAD_CTRL_28	0x00
#define SDP_TSC_PAD_CTRL_29	0x04
#define SDP_TSC_PAD_CTRL_35	0x1C
#define SDP_TSC_PAD_CTRL_36	0x20
#define SDP_TSC_PAD_CTRL_37	0x24
#define TSC_TEM_T_EN		(1 << 0)

#define AP_BASE		(SFR_VA + 0x10080000 - SFR0_BASE)

static int diff_val[3];
static int rev;

extern int sdp_get_revision_id(void);

static int get_fused_value(struct sdp_tmu_info * info)
{
	int timeout = 200;

	/* prepare to read */
	writel(0x1F, (void*)(AP_BASE + 0x4));
	while (timeout--) {
		if (readl((void*)AP_BASE) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("TMU: efuse read fail!\n");
		goto out_diff;
	}

	rev = sdp_get_revision_id();

	if (rev) {
		diff_val[0] = (int)((readl((void *)(AP_BASE + 0x10)) >> 17) & 0xFF);
		diff_val[1] = (int)((readl((void *)(AP_BASE + 0x10)) >> 25) |
				((readl((void *)(AP_BASE + 0x14)) & 0x1) << 7));
		diff_val[2] = (int)((readl((void *)(AP_BASE + 0x14)) >> 1) & 0xFF);
	} else {
		diff_val[0] = diff_val[1] = diff_val[2] = TSC_DEGREE_25;
	}
	printk(KERN_INFO "TMU: diff val - 0: %d(%d'C), 1: %d(%d'C), 2: %d(%d'C)\n", 
				diff_val[0], TSC_DEGREE_25 - diff_val[0],
				diff_val[1], TSC_DEGREE_25 - diff_val[1],
				diff_val[2], TSC_DEGREE_25 - diff_val[2]);

	diff_val[0] = TSC_DEGREE_25 - diff_val[0];
	diff_val[1] = TSC_DEGREE_25 - diff_val[1];
	diff_val[2] = TSC_DEGREE_25 - diff_val[2];

out_diff:
	return 0;
}

static int sdp1404_enable_tmu(struct sdp_tmu_info * info)
{
	u32 val;

	if (!info->tmu_base)
		return -EINVAL;
	
	/* Enable temperature sensor */
	val = readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_28)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_28));
	
	val = readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_35)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_35));

	val = readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_36)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_36));
		
	return 0;
}

#define TMU_MAX_TEMP_DIFF	(125)
static u32 sdp1404_get_temp(struct sdp_tmu_info *info)
{
	int temp[3] = {0, };
	static int prev_temp[3] = {TSC_DEGREE_25, TSC_DEGREE_25, TSC_DEGREE_25}; /* defualt temp is 46'C */
	static int print_delay = PRINT_RATE;
	int i;

	if (info->sensor_id >= MAX_SENSOR_CNT) {
		printk(KERN_ERR "TMU ERROR - sensor id is not avaiable. %d\n", info->sensor_id);
		return 0;
	}

	/* get temperature from TSC register */
	temp[0] = (readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_29)) >> 12) & 0xFF;
	temp[1] = readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_37)) & 0xFF;
	temp[2] = (readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_37)) >> 16) & 0xFF;
	
	for (i = 0; i < 3; i++) {
		/* calibration */
		temp[i] = temp[i] + diff_val[i] - (TSC_DEGREE_25 - 25);

		/* check boundary */
		if (temp[i] < 0)
			temp[i] = 0;

		/* sanity check */
		if (abs(temp[i] - prev_temp[i]) > TMU_MAX_TEMP_DIFF) {
			printk(KERN_INFO "TMU: warning - temp is insane, %d'C(force set to %d'C)\n", 
				temp[i], prev_temp[i]);
			temp[i] = prev_temp[i];
			continue;
		}

		prev_temp[i] = temp[i];
	}

	/* Temperature is printed every PRINT_RATE. */ 
	if (info->print_on || info->user_print_on) {
		print_delay -= SAMPLING_RATE;
		if (print_delay <= 0) {
			printk(KERN_INFO "\033[1;7;33mT^%d'C,%d'C,%d'C\033[0m\n",
					temp[0], temp[1], temp[2]);
			print_delay = PRINT_RATE;
		}
	}
	
	return (unsigned int)temp[info->sensor_id];
}

#if defined(CONFIG_PLAT_TIZEN)
#define HOTPLUG_START_CPU 1
#else
#define HOTPLUG_START_CPU 2
#endif

#define BIG_CPU_COUNT	4

static int sdp1404_cpu_hotplug_down(struct sdp_tmu_info * info)
{
#if defined(CONFIG_HOTPLUG_CPU)
	struct device * dev;
	u32 cpu;
	ssize_t ret;

	/* cpu down */
	for (cpu = HOTPLUG_START_CPU; cpu < BIG_CPU_COUNT; cpu++) {
		if (!cpu_online(cpu))
			continue;
		
		dev = get_cpu_device(cpu);
		ret = cpu_down(cpu);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
	}
#endif
	
	return 0;
}

static int sdp1404_cpu_hotplug_up(struct sdp_tmu_info * info)
{
#if defined(CONFIG_HOTPLUG_CPU)
	u32 cpu;
	struct device * dev;
	ssize_t ret;

	/* find offline cpu and power up */
	for (cpu = HOTPLUG_START_CPU; cpu < BIG_CPU_COUNT; cpu++) {
		if (cpu_online(cpu))
			continue;
		
		dev = get_cpu_device(cpu);
		ret = cpu_up(cpu);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
	}
#endif

	return 0;
}

/* get temperature for sysfs */
static u32 get_temp(struct sdp_tmu_info * info, int id)
{
	u32 val;
	int temp;

	/* get temperature from TSC register */
	if (id == 0)
		val = (readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_29)) >> 12) & 0xFF;
	else if (id == 1)
		val = readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_37)) & 0xFF;
	else if (id == 2)
		val = (readl((void *)((u32)info->tmu_base + SDP_TSC_PAD_CTRL_37)) >> 16) & 0xFF;
	else
		return 0;
	
	temp = (int)val + diff_val[id] - (TSC_DEGREE_25 - 25);
	if (temp < 0)
		temp = 0;

	return (unsigned int)temp;
}

/* sysfs */
static ssize_t show_temp0(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	u32 temp;

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	temp = get_temp(info, 0);

	return snprintf(buf, 5, "%u\n", temp);
}

static ssize_t show_temp1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	u32 temp;

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	temp = get_temp(info, 1);

	return snprintf(buf, 5, "%u\n", temp);
}

static ssize_t show_temp2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	u32 temp;

	if (!dev)
		return -ENODEV;
	
	if (info == NULL)
		return -1;

	temp = get_temp(info, 2);

	return snprintf(buf, 5, "%u\n", temp);
}

static DEVICE_ATTR(temp0, 0444, show_temp0, NULL);
static DEVICE_ATTR(temp1, 0444, show_temp1, NULL);
static DEVICE_ATTR(temp2, 0444, show_temp2, NULL);

/* init */
int sdp1404_tmu_init(struct sdp_tmu_info * info)
{
	int ret;
	struct device *dev = info->dev;

	/* read efuse value */
	get_fused_value(info);

	info->enable_tmu = sdp1404_enable_tmu;
	info->get_temp = sdp1404_get_temp;
	info->cpu_hotplug_down = sdp1404_cpu_hotplug_down;
	info->cpu_hotplug_up = sdp1404_cpu_hotplug_up;

	/* sysfs interface */
	ret = device_create_file(dev, &dev_attr_temp0);
	if (ret != 0)
		pr_err("Failed to create temp0 file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_temp1);
	if (ret != 0)
		pr_err("Failed to create temp1 file: %d\n", ret);

	ret = device_create_file(dev, &dev_attr_temp2);
	if (ret != 0)
		pr_err("Failed to create temp2 file: %d\n", ret);


	return 0;
}

