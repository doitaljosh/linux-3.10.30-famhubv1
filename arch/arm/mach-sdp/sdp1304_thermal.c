/* sdp1304_tmu.c
 *
 * Copyright (c) 2013-2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * SDP1304 - Thermal Management support
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
#define SDP_TSC_CONTROL0	0x0
#define SDP_TSC_CONTROL1	0x4
#define SDP_TSC_CONTROL2	0x8
#define SDP_TSC_CONTROL3	0xC
#define SDP_TSC_CONTROL4	0x10
#define SDP_TSC_CONTROL5	0x14
#define SDP_TSC_CONTROL6	0x18
#define TSC_TEM_T_EN		(1 << 0)
#define SDP_TSC_CONTROL7	0x1C
#define TSC_TEM_T_TS_8BIT_TS	12
#define SDP_TSC_CONTROL8	0x20
#define SDP_TSC_CONTROL9	0x24

static int diff_val[3];

static int get_fused_value(struct sdp_tmu_info * info)
{
	u32 base = SFR_VA + 0x80000;
	int timeout = 200;
	void __iomem * base2;
	
	/* prepare to read */
	writel(0x1F, (void*)(base + 0x4));
	while (timeout--) {
		if (readl((void*)base) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("TMU: efuse read fail!\n");
		goto out_diff;
	}

	base2 = ioremap(0x10b00c94, 0x4);
	if (base2 == NULL) {
		printk(KERN_ERR "TMU ERROR - efuse register address ioremap fail\n");
		goto out_diff;
	}

	/* read efuse */
	diff_val[0] = (readl((void*)(base + 0x10)) >> 8) & 0xFF;
	diff_val[1] = readl((void*)base2) & 0xFF;
	diff_val[2] = (readl((void*)base2) >> 8) & 0xFF;
	printk(KERN_INFO "TMU: diff val - 0: %d(%d'C), 1: %d(%d'C), 2: %d(%d'C)\n", 
				diff_val[0], TSC_DEGREE_25 - diff_val[0],
				diff_val[1], TSC_DEGREE_25 - diff_val[1],
				diff_val[2], TSC_DEGREE_25 - diff_val[2]);

	diff_val[0] = TSC_DEGREE_25 - diff_val[0];
	diff_val[1] = TSC_DEGREE_25 - diff_val[1];
	diff_val[2] = TSC_DEGREE_25 - diff_val[2];

	iounmap(base2);
	
out_diff:
	
	return 0;
}

static int sdp1304_enable_tmu(struct sdp_tmu_info * info)
{
	u32 val;

	/* read efuse value */
	get_fused_value(info);
	
	/* Temperature sensor enable */
	val = readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL4)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_CONTROL4));

	val = readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL5)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_CONTROL5));

	val = readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL6)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP_TSC_CONTROL6));

	mdelay(1);
	
	return 0;
}

#define TMU_MAX_TEMP_DIFF	(125)
static u32 sdp1304_get_temp(struct sdp_tmu_info *info)
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
	temp[0] = (readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL7)) >> TSC_TEM_T_TS_8BIT_TS) & 0xFF;
	temp[1] = readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL9)) & 0xFF;
	temp[2] = (readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL9)) >> 16) & 0xFF;

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

static int sdp1304_cpu_hotplug_down(struct sdp_tmu_info * info)
{
#if 0//def CONFIG_HOTPLUG_CPU	
	struct device * dev;
	u32 cpu;
	ssize_t ret;

	/* cpu down (cpu1 ~ cpu3) */
	for (cpu = 1; cpu < NR_CPUS; cpu++) {
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

static int sdp1304_cpu_hotplug_up(struct sdp_tmu_info * info)
{
#if 0//def CONFIG_HOTPLUG_CPU
	u32 cpu;
	struct device * dev;
	ssize_t ret;

	/* find offline cpu and power up */
	for (cpu = 1; cpu < NR_CPUS; cpu++) {
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
		val = (readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL7)) >> TSC_TEM_T_TS_8BIT_TS) & 0xFF;
	else if (id == 1)
		val = readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL9)) & 0xFF;
	else if (id == 2)
		val = (readl((void *)((u32)info->tmu_base + SDP_TSC_CONTROL9)) >> 16) & 0xFF;
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
int sdp1304_tmu_init(struct sdp_tmu_info * info)
{
	int ret;
	struct device *dev = info->dev;

	info->enable_tmu = sdp1304_enable_tmu;
	info->get_temp = sdp1304_get_temp;
	info->cpu_hotplug_down = sdp1304_cpu_hotplug_down;
	info->cpu_hotplug_up = sdp1304_cpu_hotplug_up;

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

