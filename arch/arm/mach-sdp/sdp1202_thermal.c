/* sdp1202_thermal.c
 *
 * Copyright (c) 2013-2015 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * SDP1202 - Thermal Management support
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

/* Register define */
#define SDP1202_TSC_CONTROL0	0x0
#define SDP1202_TSC_CONTROL1	0x4
#define SDP1202_TSC_CONTROL2	0x8
#define SDP1202_TSC_CONTROL3	0xC
#define SDP1202_TSC_CONTROL4	0x10
#define TSC_TEM_T_EN		(1 << 0)
#define SDP1202_TSC_CONTROL5	0x14
#define TSC_TEM_T_TS_8BIT_TS	12

static int diff_val;

static int get_fused_value(struct sdp_tmu_info * info)
{
	u32 base = SFR_VA + 0x80000;
	int timeout = 200;
	
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

	/* read efuse */
	diff_val = readl((void*)(base + 0x10)) & 0xFF;
	printk(KERN_INFO "TMU: diff val - 0: %d(%d'C)\n", 
				diff_val, TSC_DEGREE_25 - diff_val);

	diff_val = TSC_DEGREE_25 - diff_val;
	
out_diff:
	return 0;
}

static int sdp1202_enable_tmu(struct sdp_tmu_info * info)
{
	u32 val;

	/* read efuse value */
	get_fused_value(info);
	
	/* Temperature sensor enable */
	val = readl((void *)((u32)info->tmu_base + SDP1202_TSC_CONTROL4)) | TSC_TEM_T_EN;
	writel(val, (void *)((u32)info->tmu_base + SDP1202_TSC_CONTROL4));

	mdelay(1);
	
	return 0;
}

#define TMU_MAX_TEMP_DIFF	(125)
static u32 sdp1202_get_temp(struct sdp_tmu_info *info)
{
	int temp = 0;
	static int prev_temp = TSC_DEGREE_25; /* defualt temp is 46'C */
	static int print_delay = PRINT_RATE;

	/* get temperature from TSC register */
	temp = (readl((void *)((u32)info->tmu_base + SDP1202_TSC_CONTROL5)) >> TSC_TEM_T_TS_8BIT_TS) & 0xFF;

	/* calibration */
	temp = temp + diff_val - (TSC_DEGREE_25 - 25);

	/* check boundary */
	if (temp < 0)
		temp = 0;

	/* sanity check */
	if (abs(temp - prev_temp) > TMU_MAX_TEMP_DIFF) {
		printk(KERN_INFO "TMU: warning - temp is insane, %d'C(force set to %d'C)\n", 
			temp, prev_temp);
		temp = prev_temp;
	}

	prev_temp = temp;

	/* Temperature is printed every PRINT_RATE. */ 
	if (info->print_on || info->user_print_on) {
		print_delay -= SAMPLING_RATE;
		if (print_delay <= 0) {
			printk(KERN_INFO "\033[1;7;33mT^%d'C\n", temp);
			print_delay = PRINT_RATE;
		}
	}
	
	return (unsigned int)temp;
}

/* init */
int sdp1202_tmu_init(struct sdp_tmu_info * info)
{
	info->enable_tmu = sdp1202_enable_tmu;
	info->get_temp = sdp1202_get_temp;

	return 0;
}
