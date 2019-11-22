/* linux/arch/arm/mach-sdp/sdp_soc/sdp1304/sdp1304_cpufreq.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP1304 - CPU frequency scaling support
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

#define FREQ_LOCK_TIME		50
#define CPUFREQ_LEVEL_END	L20

#ifdef MAX_CPU_ASV_GROUP
#define CPUFREQ_ASV_COUNT	MAX_CPU_ASV_GROUP
#else
#define CPUFREQ_ASV_COUNT	10
#endif

#define CPUFREQ_EMA_COUNT	4

#define CPUFREQ_BOARD_MAINTV	0
#define CPUFREQ_BOARD_SBB		1
#define CPUFREQ_BOARD_NGM		2
#define CPUFREQ_BOARD_JACK		3

static int board_table_sel = CPUFREQ_BOARD_MAINTV;

static DEFINE_SPINLOCK(freq_lock);

static unsigned int max_support_idx;
static unsigned int min_support_idx = L18;
static unsigned int max_real_idx = L3; /* overclock frequnecy 1700MHz */
static unsigned int min_real_idx = L19;	/* for thermal throttle */

static struct cpufreq_frequency_table sdp1304_freq_table[] = {
	{ L0, 2000*1000},
	{ L1, 1900*1000},
	{ L2, 1800*1000},
	{ L3, 1700*1000},
	{ L4, 1600*1000},
	{ L5, 1500*1000},
	{ L6, 1400*1000},
	{ L7, 1300*1000},
	{ L8, 1200*1000},	
	{ L9, 1100*1000},	
	{L10, 1000*1000},
	{L11,  900*1000},
	{L12,  800*1000},
	{L13,  700*1000},
	{L14,  600*1000},
	{L15,  500*1000},
	{L16,  400*1000},
	{L17,  300*1000},
	{L18,  200*1000},
	{L19,  100*1000},
	{0, CPUFREQ_TABLE_END},
};

/*
@===========================================================================
@PLL PMS Table
@===========================================================================
@CPU
@---------------------------------------------------------------------------
@FIN FOUT Value		
@---------------------------------------------------------------------------
@24  100  0x30C804	
@24  200  0x30C803	
@24  300  0x20C803	
@24  400  0x30C802
@24  500  0x30FA02
@24  600  0x20C802
@24  700  0x30AF01
@24  800  0x30C801
@24  900  0x209601
@24 1000  0x30FA01
@24 1100  0x311301
@24 1200  0x20C801
@24 1302  0x40D900 *
@24 1400  0x30AF00
@24 1500  0x207D00
@24 1600  0x30C800
@24 1704  0x208E00 *
@24 1800  0x209600
@24 1896  0x209E00 *
@24 2000  0x30FA00
@===========================================================================
*/
static unsigned int clkdiv_cpu[CPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x30FA00, /* 2000 L0 */
	0x209E00, /* 1900 L1 */
	0x209600, /* 1800 L2 */
	0x208E00, /* 1700 L3 */
	0x30C800, /* 1600 L4 */
	0x207D00, /* 1500 L5 */
	0x30AF00, /* 1400 L6 */
	0x40D900, /* 1300 L7 */
	0x20C801, /* 1200 L8 */
	0x311301, /* 1100 L9 */
	0x30FA01, /* 1000 L10 */
	0x209601, /*  900 L11 */
	0x30C801, /*  800 L12 */
	0x30AF01, /*  700 L13 */
	0x20C802, /*  600 L14 */
	0x30FA02, /*  500 L15 */
	0x30C802, /*  400 L16 */
	0x20C803, /*  300 L17 */
	0x30C803, /*  200 L18 */
	0x30C804, /*  100 L19 */
};

/* voltage table (uV scale) */
static unsigned int sdp1304_volt_table[CPUFREQ_LEVEL_END];

/* MainTV - voltage table */
static const unsigned int sdp1304_asv_voltage_maintv[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1270000, 1270000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L0  2000 */
	{ 1270000, 1270000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L1  1900 */
	{ 1270000, 1270000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L2  1800 */
	{ 1270000, 1270000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L3  1700 */
	{ 1270000, 1270000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L4  1600 */
	{ 1270000, 1270000, 1240000, 1220000, 1190000, 1120000, 1100000, 1090000, 1090000, 1090000}, /* L5  1500 */
	{ 1270000, 1220000, 1220000, 1220000, 1160000, 1080000, 1070000, 1060000, 1060000, 1060000}, /* L6  1400 */
	{ 1270000, 1220000, 1220000, 1180000, 1160000, 1050000, 1050000, 1030000, 1030000, 1030000}, /* L7  1300 */
	{ 1270000, 1130000, 1090000, 1090000, 1090000, 1030000, 1030000,  980000,  980000,  980000}, /* L8  1200 */
	{ 1270000, 1090000, 1090000, 1060000, 1060000,  980000,  980000,  980000,  980000,  980000}, /* L9  1100 */
	{ 1270000, 1050000, 1050000, 1030000, 1030000,  970000,  950000,  950000,  950000,  950000}, /* L10 1000 */
	{ 1270000, 1030000, 1030000, 1000000, 1000000,  930000,  920000,  900000,  900000,  900000}, /* L11  900 */
	{ 1270000, 1000000, 1000000,  970000,  960000,  900000,  900000,  900000,  900000,  900000}, /* L12  800 */
	{ 1270000,  950000,  950000,  950000,  930000,  900000,  900000,  900000,  900000,  900000}, /* L13  700 */
	{ 1270000,  950000,  920000,  920000,  910000,  900000,  900000,  900000,  900000,  900000}, /* L14  600 */
	{ 1270000,  950000,  920000,  910000,  910000,  900000,  900000,  900000,  900000,  900000}, /* L15  500 */
	{ 1270000,  950000,  920000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L16  400 */
	{ 1270000,  950000,  900000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L17  300 */
	{ 1270000,  950000,  900000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L18  200 */
	{ 1270000,  950000,  900000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L19  100 */
};

/* SBB, Jackpack and NGM?? - voltage table */
static const unsigned int sdp1304_asv_voltage_sbb[CPUFREQ_LEVEL_END][CPUFREQ_ASV_COUNT] = {
	/*   ASV0,    ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5,	   ASV6,    ASV7,    ASV8,    ASV9 */
	{ 1280000, 1280000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L0  2000 */
	{ 1280000, 1280000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L1  1900 */
	{ 1280000, 1280000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000, 1250000}, /* L2  1800 */
	{ 1280000, 1280000, 1250000, 1230000, 1200000, 1140000, 1120000, 1110000, 1110000, 1110000}, /* L3  1700 */
	{ 1280000, 1220000, 1190000, 1170000, 1140000, 1080000, 1060000, 1050000, 1050000, 1050000}, /* L4  1600 */
	{ 1280000, 1170000, 1140000, 1120000, 1090000, 1030000, 1010000, 1000000, 1000000, 1000000}, /* L5  1500 */
	{ 1280000, 1130000, 1120000, 1120000, 1070000,  990000,  980000,  970000,  970000,  970000}, /* L6  1400 */
	{ 1280000, 1120000, 1120000, 1080000, 1060000,  980000,  980000,  930000,  930000,  930000}, /* L7  1300 */
	{ 1280000, 1070000, 1050000, 1040000, 1040000,  980000,  980000,  930000,  930000,  930000}, /* L8  1200 */
	{ 1280000, 1040000, 1040000, 1010000, 1010000,  930000,  930000,  930000,  930000,  930000}, /* L9  1100 */
	{ 1280000, 1000000, 1000000,  980000,  980000,  920000,  900000,  900000,  900000,  900000}, /* L10 1000 */
	{ 1280000,  980000,  980000,  950000,  950000,  900000,  900000,  900000,  900000,  900000}, /* L11  900 */
	{ 1280000,  950000,  950000,  950000,  930000,  900000,  900000,  900000,  900000,  900000}, /* L12  800 */
	{ 1280000,  950000,  950000,  950000,  930000,  900000,  900000,  900000,  900000,  900000}, /* L13  700 */
	{ 1280000,  950000,  920000,  920000,  910000,  900000,  900000,  900000,  900000,  900000}, /* L14  600 */
	{ 1280000,  950000,  920000,  910000,  910000,  900000,  900000,  900000,  900000,  900000}, /* L15  500 */
	{ 1280000,  950000,  920000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L16  400 */
	{ 1280000,  950000,  920000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L17  300 */
	{ 1280000,  950000,  920000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L18  200 */
	{ 1280000,  950000,  920000,  900000,  900000,  900000,  900000,  900000,  900000,  900000}, /* L19  100 */
};

#ifdef CPUFREQ_USE_EMA
/* ema table */
static const struct cpufreq_ema_table sdp1304_ema_table[CPUFREQ_EMA_COUNT] = {
	{ 950000, 0x118888, 0x124},	/* ~ 0.94V */
	{1060000, 0x114444, 0x003},	/* 0.95 ~ 1.05 */
	{1200000, 0x112222, 0x001},	/* 1.06 ~ 1.19 */
	{1870000, 0x111111, 0x000},	/* 1.20V ~ */
};
#endif

#define INPUT_FREQ	(24000000UL)
#define GET_P_VALUE(x)	((x >> 20) & 0x3F)
#define GET_M_VALUE(x)	((x >> 8) & 0x3FF)
#define GET_S_VALUE(x)	(x & 0x7)
#define SDP1304_PWM_CLK_CON	(SFR_VA + 0xF70030)
#define SEL_ARM_VS_AMS		(1 << 8)	/* clock change (0: AMS clock, 1: ARM clock) */
#define SEL_AMSHALF		(12) 	/* AMS clock (0: 800MHz, 1: 400MHz, 2:200MHz, 3: 100MHz) */
#define SEL_FTEST_AMS		(1 << 16)	/* select AMS clock for temp clock (0: 24MHz, 1: AMS) */
#define SDP1304_CPU_PLL		(SFR_VA + 0xB00800)
#define SDP1304_PLL_PWDOWN	(0x10B00890) /* pll power down register (physical address) */
#define CPU_PLL				(1 << 0)
#define CPUFREQ_TEMP_FREQ	(100000)
static unsigned int sdp1304_get_speed(unsigned int cpu)
{
	unsigned int ret;
	unsigned int pms;

	pms = readl((void *)SDP1304_CPU_PLL);

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
	freq = sdp1304_get_speed(0);
	for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
		if ((sdp1304_freq_table[i].frequency*1000) == freq)
			break;
	}

	if (i < CPUFREQ_LEVEL_END)
		max_support_idx = i;
	else
		max_support_idx = L5;

	pr_info("DVFS: current CPU clk = %dMHz, max support freq is %dMHz",
				freq/1000000, sdp1304_freq_table[i].frequency/1000);
	
	for (i = L0; i < max_real_idx; i++)
		sdp1304_freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;

	pr_info("DVFS: CPU voltage table is setted with asv group %d\n", result);

	if (result < CPUFREQ_ASV_COUNT) { 
		for (i = L0; i < CPUFREQ_LEVEL_END; i++) {
			if (board_table_sel >= CPUFREQ_BOARD_SBB &&
					board_table_sel <= CPUFREQ_BOARD_JACK)
				sdp1304_volt_table[i] = sdp1304_asv_voltage_sbb[i][result];
			else
				sdp1304_volt_table[i] = sdp1304_asv_voltage_maintv[i][result];
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
			if (board_table_sel >= CPUFREQ_BOARD_SBB &&
					board_table_sel <= CPUFREQ_BOARD_JACK)
				sdp1304_volt_table[i] = sdp1304_asv_voltage_sbb[i][result];
			else
				sdp1304_volt_table[i] = sdp1304_asv_voltage_maintv[i][result];
		}
	} else {
		pr_err("%s: asv table index error. %d\n", __func__, result);
	}
}

#ifdef CPUFREQ_USE_EMA
static void __iomem * ema_base;
static int cur_ema_idx;

static void init_ema(void)
{
	int i;
	u32 val;
	
	ema_base = ioremap(0x10F70060, 0x10);
	
	if (ema_base == NULL) {
		pr_err("DVFS ERROR - ema addres ioremap fail\n");
		return;
	}

	/* find current ema index */
	val = readl(ema_base) & 0xFFFFFF;
	for (i = 0; i < CPUFREQ_EMA_COUNT; i++) {
		if (val == sdp1304_ema_table[i].cpuema)
			break;
	}
	/* fail to find */
	if (i == CPUFREQ_EMA_COUNT) {
		pr_err("DVFS ERROR - can't find current ema index (0x%x)\n", val);
		cur_ema_idx = 2;	/* set to reset value */
		return;
	}

	cur_ema_idx = i;
	pr_info("DVFS: current ema index = %d\n", cur_ema_idx);	
}

static void set_ema(unsigned int volt)
{
	int i;

	if (!ema_base)
		return;

	for (i = 0; i < CPUFREQ_EMA_COUNT; i++) {
		if (volt < sdp1304_ema_table[i].volt)
			break;
	}
	/* fail to find */
	if (i == CPUFREQ_EMA_COUNT) {
		pr_err("DVFS ERROR - fail to find ema index\n");
		return;
	}

	/* no need to change ema */
	if (cur_ema_idx == i)
		return;

	writel(sdp1304_ema_table[i].cpuema, ema_base);
	writel(sdp1304_ema_table[i].l2ema, ema_base + 0x4);
	
	cur_ema_idx = i;
}
#endif /* CPUFREQ_USE_EMA */

extern ktime_t ktime_get(void);
static inline void cpufreq_udelay(u32 us)
{
	ktime_t stime = ktime_get();

	while ((ktime_us_delta(ktime_get(), stime)) < us);
}

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);
static void sdp1304_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	unsigned int val;
	int tmp_div;

	/* freq up direction */
	if (old_index > new_index) {
		/*
		 * when frequency up case (xHz -> max freq)
		 * temp frequency need to be 800MHz
		 * 800MHz clk div value = 0
		 */
		if (new_index <= L5) /* target freq is higher than or equl to 1.5GHz */
			tmp_div = 0; /* 800MHz temp freq */
		else
			tmp_div = 1; /* 400MHz temp freq */
	/* freq down direction */
	} else {
		/*
		 * when frequency down case,
		 * temp freq selection algorithm is
		 * 3 - MSB(old freq / 100MHz)
		 */
		tmp_div = (int)sdp1304_freq_table[old_index].frequency / CPUFREQ_TEMP_FREQ;
		tmp_div = 3 - (fls((int)tmp_div) - 1);
		if (tmp_div > 3 || tmp_div < 0)
			tmp_div = 3;
	}
	
	/* select temp clock source (AMS clock) */
	val = readl((void *)SDP1304_PWM_CLK_CON) & (~(0x3 << SEL_AMSHALF));
	val |= SEL_FTEST_AMS | ((u32)tmp_div << SEL_AMSHALF); /* AMS clk div */
	writel(val, (void *)SDP1304_PWM_CLK_CON);

	/* change CPU clock source to Temp clock (ARM PLL -> AMS PLL) */
	val = readl((void *)SDP1304_PWM_CLK_CON);
	val &= ~SEL_ARM_VS_AMS;
	writel(val, (void *)SDP1304_PWM_CLK_CON);

	/* PWD off */
	sdp_set_clockgating(SDP1304_PLL_PWDOWN, CPU_PLL, 0);
	/* change CPU pll value */
	writel(clkdiv_cpu[new_index], (void *)SDP1304_CPU_PLL);
	/* PWD on */
	sdp_set_clockgating(SDP1304_PLL_PWDOWN, CPU_PLL, 1);
	/* wait PLL lock (over 25us) */
	cpufreq_udelay(FREQ_LOCK_TIME);

	/* change CPU clock source to ARM PLL */
	val = readl((void *)SDP1304_PWM_CLK_CON);
	val |= SEL_ARM_VS_AMS;
	writel(val, (void *)SDP1304_PWM_CLK_CON);
}

static void sdp1304_set_frequency(unsigned int cpu, unsigned int old_index,
				unsigned int new_index, unsigned int mux)
{
	unsigned long flags;
	
	/* Change the system clock divider values */
#if defined(CONFIG_ARM_SDP1304_CPUFREQ_DEBUG)
	printk("@$%u\n", sdp1304_freq_table[new_index].frequency/10000);
#endif

	spin_lock_irqsave(&freq_lock, flags);

#ifdef CPUFREQ_USE_EMA
	/* set EMA (higher volt case), volt change -> EMA -> freq change */
	if (sdp1304_volt_table[new_index] > sdp1304_volt_table[old_index])
		set_ema(sdp1304_volt_table[new_index]);
#endif

	/* change cpu frequnecy */
	sdp1304_set_clkdiv(old_index, new_index);

#ifdef CPUFREQ_USE_EMA
	/* set EMA (lower volt case), freq change -> EMA -> volt change */
	if (sdp1304_volt_table[new_index] < sdp1304_volt_table[old_index])
		set_ema(sdp1304_volt_table[new_index]);
#endif

	spin_unlock_irqrestore(&freq_lock, flags);
}

static __init int sdp1304_cpufreq_board_maintv(char *buf)
{
	printk(KERN_INFO "DVFS: MainTV table selected\n");
	board_table_sel = CPUFREQ_BOARD_MAINTV;
	
	return 0;
}
early_param("maintv", sdp1304_cpufreq_board_maintv);

static __init int sdp1304_cpufreq_board_sbb(char *buf)
{
	printk(KERN_INFO "DVFS: SBB table selected\n");
	board_table_sel = CPUFREQ_BOARD_SBB;
	
	return 0;
}
early_param("sbb", sdp1304_cpufreq_board_sbb);

static __init int sdp1304_cpufreq_board_ngm(char *buf)
{
	printk(KERN_INFO "DVFS: NGM table selected\n");
	board_table_sel = CPUFREQ_BOARD_NGM;
	
	return 0;
}
early_param("ngm", sdp1304_cpufreq_board_ngm);

static __init int sdp1304_cpufreq_board_jack(char *buf)
{
	printk(KERN_INFO "DVFS: JackpackTV table selected\n");
	board_table_sel = CPUFREQ_BOARD_JACK;
	
	return 0;
}
early_param("jackpack", sdp1304_cpufreq_board_jack);

int sdp1304_cpufreq_init(struct sdp_dvfs_info *info)
{
	info->cur_group = 0;
	
	/* set default AVS off table */	
	set_volt_table(info->cur_group);

	info->max_real_idx = max_real_idx;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->min_real_idx = min_real_idx;
	info->volt_table = sdp1304_volt_table;
	info->freq_table = sdp1304_freq_table;
	info->set_freq = sdp1304_set_frequency;
	info->update_volt_table = update_volt_table;
	info->get_speed = sdp1304_get_speed;
	
#ifdef CPUFREQ_USE_EMA
	init_ema();
#endif

	return 0;	
}
EXPORT_SYMBOL(sdp1304_cpufreq_init);

