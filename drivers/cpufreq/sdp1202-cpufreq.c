/* linux/arch/arm/mach-sdp/sdp_soc/sdp1202/sdp1202_cpufreq.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1202 - CPU frequency scaling support
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

/* #define CPUFREQ_USE_EMA */
#define FREQ_LOCK_TIME		50
#define CPUFREQ_LEVEL_END	L20

#ifdef MAX_CPU_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_CPU_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif

#define CPUFREQ_EMA_COUNT	4

static DEFINE_SPINLOCK(freq_lock);

static unsigned int max_support_idx;
static unsigned int min_support_idx = L12;
static unsigned int max_real_idx = L0;
static unsigned int min_real_idx = L13;	/* for thermal throttle */

static struct cpufreq_frequency_table sdp1202_freq_table[] = {
	{ L0, 1350*1000},
	{ L1, 1302*1000},
	{ L2, 1200*1000},
	{ L3, 1100*1000},
	{ L4, 1000*1000},
	{ L5, 900*1000},
	{ L6, 800*1000},
	{ L7, 700*1000},
	{ L8, 600*1000},
	{ L9, 500*1000},	
	{L10, 400*1000},	
	{L11, 300*1000},	
	{L12, 200*1000},	
	{L13, 100*1000},
	{0, CPUFREQ_TABLE_END},
};

/*
@===========================================================================
@PLL PMS Table
@===========================================================================
@CPU,AMS,GPU,DSP		@DDR
@---------------------------------------------------------------------------
@FIN FOUT Value			@FIN FOUT Value
@---------------------------------------------------------------------------
@24   50  0x30C805
@24  100  0x30C804		@24  100  0x30C804
@24  200  0x30C803		@24  200  0x30C803
@24  300  0x20C803		@24  300  0x206402
@24  400  0x30C802		@24  400  0x30C802
@24  500  0x30FA02		@24  500  0x20A702
@24  600  0x20C802		@24  600  0x206401
@24  700  0x30AF01		@24  700  0x207501
@24  800  0x30C801		@24  800  0x30C801
@24  900  0x209601		@24  900  0x209601
@24 1000  0x30FA01		@24 1000  0x20A701
@24 1100  0x311301		@24 1100  0x311301
@24 1200  0x20C801		@24 1200  0x206400
@24 1302  0x40D900		@24 1300  0x30A300
@24 1350  0x40E100
@24 1400  0x30AF00		@24 1400  0x207500
@24 1500  0x207D00		@24 1500  0x207D00
@24 1600  0x30C800		@24 1600  0x30C800
@24 1704  0x208E00		@24 1700  0x208E00
@24 1800  0x209600		@24 1800  0x209600
@24 1896  0x209E00		@24 1900  0x30EE00
@24 2000  0x30FA00		@24 2000  0x20A700
@===========================================================================
*/
static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x40E100, /* 1350 L0 */
	0x40D900, /* 1300 L1 */
	0x20C801, /* 1200 L2 */
	0x311301, /* 1100 L3 */
	0x30FA01, /* 1000 L4 */
	0x209601, /*  900 L5 */
	0x30C801, /*  800 L6 */
	0x30AF01, /*  700 L7 */
	0x20C802, /*  600 L8 */
	0x30FA02, /*  500 L9 */
	0x30C802, /*  400 L10 */
	0x20C803, /*  300 L11 */
	0x30C803, /*  200 L12 */
	0x30C804, /*  100 L13, only for emergency situation */
};

#if 0
static struct cpufreq_timerdiv_table sdp1202_timerdiv_table[CPUFREQ_LEVEL_END] = {
	{256, 8}, /* L0 */
	{256, 8}, /* L1 */
	{256, 8}, /* L2 */
	{256, 8}, /* L3 */
	{256, 8}, /* L4 */
	{256, 8}, /* L5 */
	{256, 8}, /* L6 */
	{256, 8}, /* L7 */
	{256, 8}, /* L8 */
	{256, 8}, /* L9 */
	{256, 8}, /* L10 */
	{256, 8}, /* L11 */
	{256, 8}, /* L12 */
	{256, 8}, /* L13 */		
};
#endif

/* voltage table */
/* uV scale */
static unsigned int sdp1202_volt_table[CPUFREQ_LEVEL_END];

/* es0 voltage table */
static const unsigned int sdp1202_asv_voltage_es0[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L0 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L1 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L2 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L3 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L4 */
	{ 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000, 1170000}, /* L5 */
	{ 1170000, 1170000, 1150000, 1120000, 1110000, 1080000, 1070000, 1060000, 1060000, 1060000}, /* L6 */
	{ 1170000, 1130000, 1130000, 1080000, 1060000, 1040000, 1040000, 1020000, 1020000, 1020000}, /* L7 */
	{ 1170000, 1070000, 1060000, 1060000, 1010000, 1000000, 1000000,  980000,  980000,  980000}, /* L8 */
	{ 1170000, 1020000, 1010000, 1010000,  970000,  970000,  970000,  940000,  940000,  940000}, /* L9 */
	{ 1170000,  960000,  960000,  960000,  900000,  890000,  890000,  880000,  880000,  880000}, /* L10 */
	{ 1170000,  920000,  900000,  880000,  860000,  860000,  850000,  850000,  850000,  850000}, /* L11 */
	{ 1170000,  890000,  880000,  870000,  850000,  850000,  850000,  850000,  850000,  850000}, /* L12 */
	{ 1170000,  890000,  880000,  870000,  850000,  850000,  850000,  850000,  850000,  850000}, /* L13 */		
};
/* es1 voltage table */
static const unsigned int sdp1202_asv_voltage_es1[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1130000, 1100000, 1090000, 1070000, 1070000, 1060000, 1060000, 1060000, 1060000, 1060000}, /* L0 */
	{ 1130000, 1090000, 1080000, 1060000, 1060000, 1060000, 1060000, 1060000, 1060000, 1060000}, /* L1 */
	{ 1130000, 1050000, 1040000, 1020000, 1010000, 1010000, 1010000, 1010000, 1010000, 1010000}, /* L2 */
	{ 1130000, 1020000, 1000000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L3 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L4 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L5 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L6 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L7 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L8 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L9 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L10 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L11 */
	{ 1130000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000,  980000}, /* L12 */
	{ 1130000,  980000,  980000,  980000,  980000,  950000,  950000,  950000,  950000,  950000}, /* L13 */		
};

#define INPUT_FREQ	(24000000UL)
#define GET_P_VALUE(x)	((x >> 20) & 0x3F)
#define GET_M_VALUE(x)	((x >> 8) & 0x3FF)
#define GET_S_VALUE(x)	(x & 0x7)
#define SDP1202_CPU_PLL		(SFR_VA + 0x90800)
static unsigned int sdp1202_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int pms;

	pms = readl((void *)SDP1202_CPU_PLL);

	ret = ((u32) INPUT_FREQ >> (GET_S_VALUE(pms))) / GET_P_VALUE(pms);
	ret *= GET_M_VALUE(pms); 
	
	/* convert to 10MHz scale */
	ret = ((ret + 5000000) / 10000000) * 10000000;

	return ret;
}

static void set_volt_table(int result)
{
	unsigned int i;
	unsigned int freq;

	/* get current cpu clock */
	freq = sdp1202_get_speed(0);
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1202_freq_table[i].frequency*1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;
	else
		max_support_idx = L7;

	pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp1202_freq_table[i].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp1202_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			/* ES1 only */
			sdp1202_volt_table[i] = sdp1202_asv_voltage_es1[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

static void update_volt_table(int result)
{
	int i;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
			/* ES1 only */
			sdp1202_volt_table[i] = sdp1202_asv_voltage_es1[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

#if 0
#define CPU_EMA_CTRL_REG	(0x60)
#define CPUxEMA_MASK		(0xFFFF)
#define CPU0EMA_SHIFT		(0)
#define CPU1EMA_SHIFT		(4)
#define CPU2EMA_SHIFT		(8)
#define CPU3EMA_SHIFT		(12)
#define L2_EMA_CTRL_REG		(0x64)
#define L2EMA_MASK			(0xF)
#define L2EMA_SHIFT			(0)
static void sdp1202_set_ema(unsigned int index)
{
#if defined(CPUFREQ_USE_EMA)	
	int i;
	unsigned int val;
	unsigned int reg_base = VA_CORE_POWER_BASE; /* 0x10b70000 */

	/* revision check, ES0 is not needed */
	if (!sdp_revision_id)
		return;
	
	/* find ema value */
	for (i = 0; i < CPUFREQ_EMA_COUNT; i++) {
		if (sdp1202_volt_table[index] <= sdp1202_ema_table[i].volt) {
			//printk("%s - input volt = %duV, ema table %d selected\n", 
			//		__func__, sdp1202_volt_table[index], i);
			break;
		}
	}
	if (i == CPUFREQ_EMA_COUNT) {
		printk(KERN_WARNING "WARN: %s - %d uV is not in EMA table.\n",
							__func__, sdp1202_volt_table[index]);
		return;
	}

	/* CPU EMA */
	val = readl(reg_base + CPU_EMA_CTRL_REG);

	val &= ~CPUxEMA_MASK;
	
	/* cpu0~3ema */
	val |= sdp1202_ema_table[i].cpuema << CPU0EMA_SHIFT;
	val |= sdp1202_ema_table[i].cpuema << CPU1EMA_SHIFT;
	val |= sdp1202_ema_table[i].cpuema << CPU2EMA_SHIFT;
	val |= sdp1202_ema_table[i].cpuema << CPU3EMA_SHIFT;

	writel(val, reg_base + CPU_EMA_CTRL_REG);

	/* L2 EMA */
	val = readl(reg_base + L2_EMA_CTRL_REG);
	val &= ~(L2EMA_MASK << L2EMA_SHIFT);
	val |= sdp1202_ema_table[i].l2ema << L2EMA_SHIFT;
	writel(val, reg_base + L2_EMA_CTRL_REG);	
#else
	return;
#endif
}
#endif

extern ktime_t ktime_get(void);
static inline void cpufreq_udelay(u32 us)
{
	ktime_t stime = ktime_get();

	while ((ktime_us_delta(ktime_get(), stime)) < us);
}

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

#define SDP1202_PWM_CLK_CON	0x430
#define SEL_ARM_VS_AMS		8
#define SEL_ARM_VS_AMSHALF	12
#define USE_24MHZ_VS_AMSCLOCKS	16
#define VA_CORE_POWER_BASE	(SFR_VA + 0xB70000)
#define PA_PMU_BASE		(0x10090800)
#define VA_PMU_BASE		(SFR_VA + 0x90800)
static void sdp1202_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&freq_lock, flags);

	/* select temp clock source (AMS 500MHz) */
	val = readl((void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	//val &= ~(1 << SEL_ARM_VS_AMSHALF | 1 << USE_24MHZ_VS_AMSCLOCKS); // 24MHz
	val = (1 << SEL_ARM_VS_AMSHALF) | (1 << USE_24MHZ_VS_AMSCLOCKS); // AMS 500MHz
	//val = (val & ~(1 << SEL_ARM_VS_AMSHALF)) | (1 << USE_24MHZ_VS_AMSCLOCKS); // AMS 1GHz
	writel(val, (void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	//printk("select temp clock : 0x%X = 0x%08X\n", VA_CORE_MISC_BASE + SDP1202_PWM_CLK_CON, val);

	/* change CPU clock source to Temp clock (AMS 500MHz) */
	val = readl((void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	val &= ~(1U << SEL_ARM_VS_AMS);
	writel(val, (void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	//printk("change CPU clock source to Temp clock : 0x%X=0x%08X\n",
	//			VA_CORE_MISC_BASE + SDP1202_PWM_CLK_CON, val);

	/* PWD off */
	sdp_set_clockgating(PA_PMU_BASE + 0x90, 0x1, 0);
	/* change CPU pll value */
	writel(clkdiv_cpu[new_index], (void *)VA_PMU_BASE);
	/* PWD on */
	sdp_set_clockgating(PA_PMU_BASE + 0x90, 0x1, 1);

	/* wait PLL lock (over 25us) */
	cpufreq_udelay(FREQ_LOCK_TIME);

	/* change CPU clock source to PLL clock */
	val = readl((void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	val |= 1 << SEL_ARM_VS_AMS;
	writel(val, (void *)(VA_CORE_POWER_BASE + SDP1202_PWM_CLK_CON));
	//printk("change CPU clk source to PLL clk : 0x%X=0x%08X\n",
	//			VA_CORE_MISC_BASE + SDP1202_PWM_CLK_CON, val);

	spin_unlock_irqrestore(&freq_lock, flags);
}

static void sdp1202_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
#if defined(CONFIG_ARM_SDP1202_CPUFREQ_DEBUG)
	printk("@$%u\n", sdp1202_freq_table[new_index].frequency/10000);
#endif

	spin_lock_irqsave(&freq_lock, flags);

	/* change cpu frequnecy */
	sdp1202_set_clkdiv(old_index, new_index);

	spin_unlock_irqrestore(&freq_lock, flags);
}

int sdp1202_cpufreq_init(struct sdp_dvfs_info *info)
{
	info->cur_group = 0;
	
	/* set default AVS off table */	
	set_volt_table(info->cur_group);

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->volt_table = sdp1202_volt_table;
	info->freq_table = sdp1202_freq_table;
	info->set_freq = sdp1202_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1202_get_speed;
	
	return 0;	
}
EXPORT_SYMBOL(sdp1202_cpufreq_init);
