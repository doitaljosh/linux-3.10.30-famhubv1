/* drivers/cpufreq/sdp1406-cpufreq.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1406 Hawk-P little cluster - CPU frequency scaling support
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/power/sdp_asv.h>
#include <linux/platform_data/sdp-cpufreq.h>
#include <linux/of.h>

#include <asm/cacheflush.h>

#include <mach/map.h>

#define FREQ_LOCK_TIME		50
#define CPUFREQ_LEVEL_END	L13

#ifdef MAX_CPU_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_CPU_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif

extern bool sdp_cpufreq_print_on;

static DEFINE_SPINLOCK(freq_lock);

static struct clk *finclk;

static unsigned int max_support_idx;
static unsigned int min_support_idx = L10;
static unsigned int max_real_idx = L0;
static unsigned int min_real_idx = L12;	/* for thermal throttle */

static bool is_fhd;

static struct cpufreq_frequency_table sdp1406_freq_table[] = {
	{ L0, 1300*1000},
	{ L1, 1200*1000},	
	{ L2, 1100*1000},	
	{ L3, 1000*1000},
	{ L4,  900*1000},
	{ L5,  800*1000},
	{ L6,  700*1000},
	{ L7,  600*1000},
	{ L8,  500*1000},
	{ L9,  400*1000},
	{L10,  300*1000},
	{L11,  200*1000},
	{L12,  100*1000},
	{0, CPUFREQ_TABLE_END},
};

static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x717200, /* 1300 L0  */
	0x624A01, /* 1200 L1  */
	0x416601, /* 1100 L2  */
	0x828B01, /* 1000 L3  */
	0x412501, /*  900 L4  */
	0xA28B01, /*  800 L5  */
	0x40E401, /*  700 L6  */
	0x624A02, /*  600 L7  */
	0x828B02, /*  500 L8  */
	0xA28B02, /*  400 L9  */
	0x624A03, /*  300 L10 */
	0xA28B03, /*  200 L11 */
	0xA28B04, /*  100 L12 */
};

/* voltage table (uV scale) */
static unsigned int sdp1406_volt_table[CPUFREQ_LEVEL_END];

static const unsigned int sdp1406_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*  ASV0,    ASV1,    ASV2,    ASV3,	ASV4,	 ASV5,	  ASV6,   ASV7,     ASV8,    ASV9 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1300 L0  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1200 L1  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1100 L2  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /* 1000 L3  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  900 L4  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  800 L5  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  700 L6  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  600 L7  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  500 L8  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  400 L9  */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  300 L10 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  200 L11 */
	{1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000, 1100000,}, /*  100 L11 */
};

static const unsigned int sdp1406fhd_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*  ASV0,    ASV1,    ASV2,    ASV3,	ASV4,	 ASV5,	  ASV6,   ASV7,     ASV8,    ASV9 */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /* 1300 L0  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /* 1200 L1  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /* 1100 L2  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /* 1000 L3  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  900 L4  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  800 L5  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  700 L6  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  600 L7  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  500 L8  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  400 L9  */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  300 L10 */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  200 L11 */
	{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000,}, /*  100 L11 */
};

#define PHY_REG_BASE			(0x00100000)
#define REG_SDP1406_PLL_CPU_PMS		(SFR_VA + 0x005C0000 - PHY_REG_BASE)
#define REG_SDP1406_PWM_CLK_CON		(SFR_VA + 0x00790008 - PHY_REG_BASE)
#define USE_DVFS_CLOCKS			(1 << 16)
#define SEL_DVFSHALF			12
#define SEL_ARM_VS_DVFS			(1 << 8)
#define REG_SDP1406_PLL_CPU_CTRL	(SFR_VA + 0x005C0038 - PHY_REG_BASE)
#define PLL_CPU_LOCK_EN			(1 << 18)
#define REG_SDP1406_PLL_RESET_N_CTRL	(0x005C008C) /* must use lock, physical address */
#define PLL_CPU_RESET_N			(1 << 10)
#define REG_SDP1406_PLL_CPU_LOCK	(SFR_VA + 0x005C0080 - PHY_REG_BASE)
#define CPU_PLL_LOCK			(1 << 0)
#define CPUFREQ_TEMP_FREQ		(250000);

static unsigned int sdp1406_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int pms;
	unsigned int infreq;

	if (finclk)
		infreq = clk_get_rate(finclk);
	else
		infreq = 24576000UL;

	pms = readl((void *)REG_SDP1406_PLL_CPU_PMS);

	ret = (infreq >> (pms & 0x7)) / ((pms >> 20) & 0x3F);
	ret *= ((pms >> 8) & 0x3FF); 

	/* convert to 10MHz scale */
	ret = ((ret + 5000000) / 10000000) * 10000000;

	return ret;
}

static void set_volt_table(int result)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu clock */
	freq = sdp1406_get_speed(0);
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1406_freq_table[i].frequency*1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;

	pr_info("DVFS: cur CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp1406_freq_table[i].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp1406_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: %s CPU voltage table is setted with asv group %d\n", 
		is_fhd ? "FHD" : "UHD", result);

	if (result < CPUFREQ_ASV_COUNT)
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			if (is_fhd)
				sdp1406_volt_table[i] = sdp1406fhd_asv_voltage[i][result];
			else
				sdp1406_volt_table[i] = sdp1406_asv_voltage[i][result];
		}
	else
		pr_err("%s: asv table index error. %d\n", __func__, result);
}

static void update_volt_table(int result)
{
	int i;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			if (is_fhd)
				sdp1406_volt_table[i] = sdp1406fhd_asv_voltage[i][result];
			else
				sdp1406_volt_table[i] = sdp1406_asv_voltage[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

static void sdp1406_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	u32 val;
	u8 tmp_div;
	
	/* calculate temp clock */
	if (old_index > new_index) {
		if (sdp1406_freq_table[new_index].frequency > 1000000)
			tmp_div = 1;
		else if (sdp1406_freq_table[new_index].frequency > 500000)
			tmp_div = 2;
		else
			tmp_div = 3;
	} else {
		/*
		 * when frequency down case,
		 * temp freq selection algorithm is
		 * 3 - MSB(old freq / 250MHz)
		 */
		tmp_div = (int)sdp1406_freq_table[old_index].frequency / CPUFREQ_TEMP_FREQ;
		tmp_div = 3 - (fls((int)tmp_div) - 1);
		if (tmp_div > 3 || tmp_div < 0)
			tmp_div = 3;
	}

	/* set the mux to selected ams clock(sel_ams_half) */
	val = readl((void *)REG_SDP1406_PWM_CLK_CON) & (~(0x3 << SEL_DVFSHALF));
	val |= (u32)tmp_div << SEL_DVFSHALF; /* AMS clk div */
	writel(val, (void *)REG_SDP1406_PWM_CLK_CON);

	/* change CPU clock source to Temp clock(sel_arm_ams) */
	val = readl((void *)REG_SDP1406_PWM_CLK_CON);
	val &= ~SEL_ARM_VS_DVFS;
	writel(val, (void *)REG_SDP1406_PWM_CLK_CON);

	/* PWD off */
	sdp_set_clockgating(REG_SDP1406_PLL_RESET_N_CTRL, PLL_CPU_RESET_N, 0);
	/* change CPU pll value */
	writel(clkdiv_cpu[new_index], (void *)(REG_SDP1406_PLL_CPU_PMS));
	/* PWD on */
	sdp_set_clockgating(REG_SDP1406_PLL_RESET_N_CTRL, PLL_CPU_RESET_N, PLL_CPU_RESET_N);

	/* wait PLL lock */
	while (!(readl((void *)REG_SDP1406_PLL_CPU_LOCK) & CPU_PLL_LOCK));

	/* change CPU clock source to ARM PLL(sel_arm_ams) */
	val = readl((void *)REG_SDP1406_PWM_CLK_CON);
	val |= SEL_ARM_VS_DVFS;
	writel(val, (void *)REG_SDP1406_PWM_CLK_CON);	
}

static void sdp1406_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
	if (sdp_cpufreq_print_on)
	printk("@$%u\n", sdp1406_freq_table[new_index].frequency/10000);

	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp1406_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

int sdp1406_cpufreq_init(struct sdp_dvfs_info *info)
{
	u32 val;
	
	info->cur_group = 0;
	
	/* select temp clock to ams clock(sel_ftest_ams) */
	val = readl((void *)REG_SDP1406_PWM_CLK_CON) | USE_DVFS_CLOCKS;
	writel(val, (void *)REG_SDP1406_PWM_CLK_CON);

	/* pll lock enable(pll_cpu_locken) */
	val = readl((void *)REG_SDP1406_PLL_CPU_CTRL) | PLL_CPU_LOCK_EN;
	writel(val, (void *)REG_SDP1406_PLL_CPU_CTRL);

	/* get fin clock */
	finclk = clk_get(NULL, "fin_pll");
	if (!finclk)
		pr_err("finclk get fail\n");

	if (of_machine_is_compatible("samsung,sdp1406fhd"))
		is_fhd = true;
	else
		is_fhd = false;
	
	/* set default AVS off table */	
	set_volt_table(info->cur_group);

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->volt_table = sdp1406_volt_table;
	info->freq_table = sdp1406_freq_table;
	info->set_freq = sdp1406_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1406_get_speed;
	
	return 0;	
}
EXPORT_SYMBOL(sdp1406_cpufreq_init);
