/* linux/arch/arm/mach-sdp/sdp_soc/sdp1307/sdp1307_cpufreq.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1307 - CPU frequency scaling support
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

#include <asm/cacheflush.h>

#include <mach/map.h>

#define CPUFREQ_LEVEL_END	L12

#ifdef MAX_CPU_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_CPU_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif

static DEFINE_SPINLOCK(freq_lock);

static unsigned int max_support_idx;
static unsigned int min_support_idx = L10;
static unsigned int max_real_idx = L2;
static unsigned int min_real_idx = L11;

static struct clk *cpu_clk;
static struct clk *in_clk;
static u32 in_freq;

static struct cpufreq_frequency_table sdp1307_freq_table[] = {
	{ L0, 1200*1000},
	{ L1, 1100*1000},
	{ L2, 1000*1000},
	{ L3,  900*1000},
	{ L4,  800*1000},
	{ L5,  700*1000},
	{ L6,  600*1000},
	{ L7,  500*1000},	
	{ L8,  400*1000},	
	{ L9,  300*1000},	
	{L10,  200*1000},	
	{L11,  100*1000},
	{0, CPUFREQ_TABLE_END},
};

/*
@===========================================================================
@PLL PMS Table
@===========================================================================
@CPU	
@---------------------------------------------------------------------------
@FIN FOUT 		Value		
@---------------------------------------------------------------------------
@24 1200  		0x3115
@24 1100(1104)  0x2d15	
@24 1000(984)	0x2815
@24  900(888)  	0x2415
@24  800(792)  	0x2015
@24  700(696)  	0x1c15
@24  600  		0x1815
@24  500(504)	0x1415
@24  400(408)	0x1015
@24  300(288)	0x0b15
@24  200(192)	0x0715
@24  100(96)	0x0315
@===========================================================================
*/
static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x3115, /* 1200 L0 */
	0x2d15, /* 1100 L1 */
	0x2815, /* 1000 L2 */
	0x2415, /*  900 L3 */
	0x2015, /*  800 L4 */
	0x1c15, /*  700 L5 */
	0x1815, /*  600 L6 */
	0x1415, /*  500 L7 */
	0x1015, /*  400 L8 */
	0x0b15, /*  300 L9 */
	0x0715, /*  200 L10 */
	0x0315, /*  100 L11 , only for emergency situation */
};

/* voltage table */
/* uV scale */
static unsigned int sdp1307_volt_table[CPUFREQ_LEVEL_END];

/* voltage table */
static const unsigned int sdp1307_asv_voltage[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000}, /* L0 */
	{ 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000, 1280000}, /* L1 */
	{ 1280000, 1280000, 1260000, 1230000, 1180000, 1160000, 1160000, 1160000, 1160000, 1160000}, /* L2 */
	{ 1280000, 1200000, 1180000, 1170000, 1120000, 1090000, 1090000, 1090000, 1090000, 1090000}, /* L3 */
	{ 1280000, 1160000, 1140000, 1130000, 1070000, 1050000, 1050000, 1050000, 1050000, 1050000}, /* L4 */
	{ 1280000, 1100000, 1090000, 1080000, 1010000, 1000000, 1000000, 1000000, 1000000, 1000000}, /* L5 */
	{ 1280000, 1050000, 1050000, 1030000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000}, /* L6 */
	{ 1280000, 1050000, 1030000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L7 */
	{ 1280000, 1050000, 1030000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L8 */
	{ 1280000, 1050000, 1030000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L9 */
	{ 1280000, 1050000, 1030000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L10 */
	{ 1280000, 1050000, 1030000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L11 */
};

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

extern ktime_t ktime_get(void);
static inline void cpufreq_udelay(u32 us)
{
	ktime_t stime = ktime_get();

	while ((ktime_us_delta(ktime_get(), stime)) < us);
}

#define SDP1307_REG_CLK_CTRL	(void*)(VA_SFR0_BASE + 0xB70030)
#define MUX_ARM			(1 << 8)
#define DVFS_PLL_SEL		(12)
#define DVFS_MUX_SEL		(16)
#define SDP1307_REG_CPU_PLL	(void*)(VA_SFR0_BASE + 0x90804)
#define SDP1307_REG_PLL_LOCK	(void*)(VA_SFR0_BASE + 0x9085C)
#define CPU_PLL_LOCK		(1 << 1)
#define SDP1307_PLL_PWD		(0x18090860)
#define CPU_PLL_PWD		(1 << 1)
static void sdp1307_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	u32 val;
	u32 div = 3;

	if (old_index > new_index && new_index <= L2) {
		/* frequency up case (xMHz -> max freq) */
		div = 1; /* 492MHz(984/2) temp freq */
	} else {
		/* frequency down case */
		div = 3; /* 123MHz(984/8) temp freq */
	}
		
	/* select temp clock source */
	val = readl(SDP1307_REG_CLK_CTRL) | (div << DVFS_PLL_SEL);
	writel(val, SDP1307_REG_CLK_CTRL);
	
	/* change CPU clock source to Temp clock (ARM pll -> DSP PLL / 8) */
	val = readl(SDP1307_REG_CLK_CTRL) | (1 << DVFS_MUX_SEL);
	writel(val, SDP1307_REG_CLK_CTRL);

	val = readl(SDP1307_REG_CLK_CTRL) & ~MUX_ARM;
	writel(val, SDP1307_REG_CLK_CTRL);

	/* change CPU pll value */
	sdp_set_clockgating(SDP1307_PLL_PWD, CPU_PLL_PWD, 0);
	writel(clkdiv_cpu[new_index], SDP1307_REG_CPU_PLL);
	cpufreq_udelay(1);
	sdp_set_clockgating(SDP1307_PLL_PWD, CPU_PLL_PWD, CPU_PLL_PWD);
	
	/* wait PLL lock */
	while ((readl(SDP1307_REG_PLL_LOCK) & CPU_PLL_LOCK) == 0);

	/* change CPU clock source to PLL clock */
	val = readl(SDP1307_REG_CLK_CTRL) | MUX_ARM;
	writel(val, SDP1307_REG_CLK_CTRL);
}

static void sdp1307_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
#if defined(CONFIG_ARM_SDP1307_CPUFREQ_DEBUG)
	printk("@$%u\n", sdp1307_freq_table[new_index].frequency/10000);
#endif

	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp1307_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

static unsigned int sdp1307_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int val;

	/* get idivfb */
	val = readl((void *)SDP1307_REG_CPU_PLL) >> 8 & 0xFF;
	
	ret = (in_freq * ((val + 1) * 2)) / 2;
	
	/* convert to 100MHz scale */
	ret = ((ret + 50000000) / 100000000) * 100000000;

	return ret;
}

static void set_volt_table(int result)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu's clock */
	freq = sdp1307_get_speed(0) / 1000000;
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1307_freq_table[i].frequency/1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;
	else
		max_support_idx = L2;
	
	pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
			freq, sdp1307_freq_table[max_support_idx].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp1307_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1307_volt_table[i] = 
				sdp1307_asv_voltage[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

static void update_volt_table(int result)
{
	int i;

	pr_info("DVFS: VDD_ARM Voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			sdp1307_volt_table[i] = 
				sdp1307_asv_voltage[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

int sdp1307_cpufreq_init(struct sdp_dvfs_info *info)
{
	info->cur_group = 0;

	in_clk = clk_get(NULL, "fin_pll");
	if (IS_ERR(in_clk)) {
		printk(KERN_ERR "%s = fin_pll clock get fail", __func__);
		return PTR_ERR(in_clk);
	}
	
	in_freq = clk_get_rate(in_clk);
	printk(KERN_INFO "DVFS: input freq = %d\n", in_freq);
	
	cpu_clk = clk_get(NULL, "arm_clk");
	if (IS_ERR(cpu_clk)) {
		/* error */
		printk(KERN_ERR "%s - arm clock get fail", __func__);
		return PTR_ERR(cpu_clk);
	}

	/* set default AVS off table */	
	set_volt_table(info->cur_group);

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = sdp1307_volt_table;
	info->freq_table = sdp1307_freq_table;
	info->set_freq = sdp1307_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1307_get_speed;

	return 0;	
}
EXPORT_SYMBOL(sdp1307_cpufreq_init);

