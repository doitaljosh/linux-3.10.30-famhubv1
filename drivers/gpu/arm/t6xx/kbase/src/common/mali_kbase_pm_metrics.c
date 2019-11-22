/*
 *
 * (C) COPYRIGHT 2011-2013 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

/* HISTORY */
/* [RQ130627-00062] GolfP DVFS Update reina */

/**
 * @file mali_kbase_pm_metrics.c
 * Metrics for power management
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>

#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/err.h>

/* Thermal Throttle : reina */ 
#include <linux/platform_data/sdp-cpufreq.h>
#include <mach/sdp_thermal.h> 
#include <linux/power/sdp_asv.h>



/* When VSync is being hit aim for utilisation between 70-90% */
#define KBASE_PM_VSYNC_MIN_UTILISATION          70
#define KBASE_PM_VSYNC_MAX_UTILISATION          90
/* Otherwise aim for 10-40% */
#define KBASE_PM_NO_VSYNC_MIN_UTILISATION       45
#define KBASE_PM_NO_VSYNC_MAX_UTILISATION       75
#ifndef CONFIG_MALI_T6XX_DVFS
/* Frequency that DVFS clock frequency decisions should be made */
#define KBASE_PM_DVFS_FREQUENCY                 30 /*default 500 : eastson */
#endif

#define GPU_SAMPLING_RATE		(20 * 1000) /* 20 msec = (20 * 1000 * 1000) u sec  */
#define CPU_FREQ_DOWN_LOCK_DELAY	10

static struct workqueue_struct  *dvfs_workqueue;
unsigned int sampling;
const char dvfs_dev_name[] = "dvfs_dev";

typedef struct {
	struct delayed_work my_work;
	struct hrtimer *timer;
} my_work_t;

my_work_t *dvfs_work;

#define CPU_LOCK
/* DVFS : reina */
#define USE_TEMP_SCALER_PATH
#define DVFS_REGISTER_ADDR		0x10b00000
#define DVFS_IOREMAP_SIZE		0x1000
#define PA_PMU_BASE				0x10b00800

#define MALI_DVFS_KEEP_STAY_CNT  10 

/* W0000145965 - using spinlock for power down */
#define CLEAR_BIT(REG_ADDR, BIT)  	do {\
										u32 val;\
										val = __raw_readl(REG_ADDR);\
										val &= ~(0x1 << (BIT));\
										__raw_writel(val, REG_ADDR);\
									} while(0)
#define SET_BIT(REG_ADDR, BIT)  	do {\
										u32 val;\
										val = __raw_readl(REG_ADDR);\
										val |= (0x1 << (BIT));\
										__raw_writel(val, REG_ADDR);\
									} while(0)

#define DVFS_Print	if(g_bDVFS_Print == 2) printk
#define DVFS_DBG	if(g_bDVFS_Print == 1) printk


extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

static unsigned int * g_pu32VirAddr_Reg_DVFS;
static unsigned int * g_pu32VirAddr_Reg_DVFS_Path_Ctl;
static unsigned int * g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS;
static unsigned int * g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff;
static unsigned int * g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio;
static unsigned int * g_pu32VirAddr_Reg_DVFS_GPU_EMA;
static unsigned int * g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS;


/* For Voltage*/
static struct regulator *gpu_regulator;
static DEFINE_MUTEX(wq_lock);
static DEFINE_MUTEX(GPU_freq_lock);

static kbase_device *gdev;

unsigned int max_support_idx = Lev7;
unsigned int min_support_idx = Lev2;
unsigned int max_real_idx = Lev6;	/* for thermal throttle */
unsigned int cpu_lock_threshold = Lev4;	/* for thermal throttle */
unsigned int cpu_unlock_threshold = Lev5;	/* for thermal throttle */

unsigned int gpu_asv_tmcb=0;
unsigned int gpu_result_of_asv;
unsigned int gpu_asv_stored_result;
unsigned int g_u32TMU_MAXLevel;

bool g_bASV_OnOFF = MALI_FALSE; /* asv status change flag */  /* W0000116302 : [GPU] AVS always on */
static bool sg_bASV_OnOFF = MALI_TRUE; /* real asv status */
kbase_pm_dvfs_status g_DVFS_OnOFF = DVFS_OFF;

bool g_bDVFS_Print = MALI_FALSE; 
bool g_bThermal_limit= MALI_TRUE;

unsigned int g_u32DVFS_CurLevel= MAX_GPU_FREQ_LEVEL; 
unsigned int g_u32DVFS_FixLevel= MAX_GPU_FREQ_LEVEL; 
unsigned int g_u32DVFS_Manual_Level=  MAX_GPU_FREQ_LEVEL;
static int 	 g_u32DVFS_ema_idx;

gpufreq_info g_FreqDbg[50]={{0,},};
unsigned int g_u32dbgcnt = 0;

/* voltage table (uV scale) */
unsigned int GolfP_Volt_table[GPUFREQ_LEVEL_END] = {0,};

/* asv table    */
struct asv_judge_table GolfP_asv_table[MAX_GPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{ 0, 0},
	{ 0, 19},
	{ 0, 23},
	{ 0, 32},
	{ 0, 37},
	{ 0, 43},
	{ 0, 63},	/* Reserved Group (MAX) */
};

static const unsigned int GolfP_ASV_voltage[GPUFREQ_LEVEL_END][GPUFREQ_ASV_COUNT] = {
	/*   ASV0(default),     ASV1,    ASV2,    ASV3,	 ASV4,	  ASV5 , ASV6 */
	{ 1300000, 1300000, 1280000, 1220000,  1160000, 1140000, 1100000}, /* Lev0 */
	{ 1300000, 1190000, 1190000, 1090000,  1060000, 1040000, 1010000}, /* Lev1 */
	{ 1300000, 1100000, 1100000, 1020000,   990000,  960000,  940000}, /* Lev2 */
	{ 1300000, 1030000, 1030000,  950000,   890000,  890000,  890000}, /* Lev3 */
	{ 1300000,  950000, 950000,   900000,   890000,  890000,  890000}, /* Lev4 */
	{ 1300000,  890000,	890000,   890000,   890000,  890000,  890000}, /* Lev5 */
	{ 1300000,  890000, 890000,   890000,   890000,  890000,  890000}, /* Lev6 */
	{ 1300000,  890000,	890000,   890000,   890000,	 890000,  890000}, /* Lev7*/

};

gpufreq_frequency_table GolfP_freq_table[GPUFREQ_LEVEL_END] = {
	{Lev0,  750*1000,  80, 100},
	{Lev1,  600*1000,  70, 90},
	{Lev2,  500*1000,  60, 80},
	{Lev3,  400*1000,  50, 70},
	{Lev4,  300*1000,  45, 65},
	{Lev5,  200*1000,  35, 55},
	{Lev6,  100*1000,  10, 45},
	{Lev7, 	50*1000,   0, 100},	
};


gputemp_frequency_table GolfP_temp_freq[GPUFREQ_TEMP_LEVEL] = {
	{Lev0,  125,  7},
	{Lev1,  333,  2},
	{Lev2,  500,  1},
};


static unsigned int clkdiv_gpu[GPUFREQ_LEVEL_END] = {
	/* PMS value */
	0x207D00, /* 750 L0 */
	0x20C801, /* 600 L1 */
	0x30FA01, /* 500 L2 */
	0x30C801, /* 400 L3 */
	0x20C802, /* 300 L4 */
	0x30C802, /* 200 L5 */
	0x30C803, /* 100 L6 */	
	0x30C803, /* 100  L7 emergency */
};

/* W0000135754 :  for emergency Thermal test */
bool g_Thermal_limit_50 = MALI_FALSE; 

static unsigned int DVFS_EMA_Table[GPUFREQ_EMA_COUNT][14] = 
{						/*	0x10B00D78,	0x10B00D7C,	0x10B00D80,  0x10B00D84,  0x10B00D88,	0x10B00D8C,	0x10B00D90,	0x10B00D94,	0x10B00D98,	0x10B00D9C,	0x10B00DA0,	0x10B00DA4,	0x10B00DA8,	0x10B00DAC  */
	/* 1.20V ~ */		{	0x22202220,	0x22022210,	0x02222202, 0x22220222, 0x02022222,	0x22022222,	0x22222202,	0x22020222,	0x02220222,	0x22222222,	0x22220202,	0x22022202,	0x02222222,	0x00002202 },
	/* 1.06 ~ 1.19 */	{	0x11101110,	0x11011100,	0x01111101, 0x11110111, 0x01011111,	0x11011111,	0x11111101,	0x11010111,	0x01110111,	0x11111111,	0x11110101,	0x11011101,	0x01111111,	0x00001101 },
	/* 0.95 ~ 1.05 */	{	0x11101110,	0x11011100,	0x01111101, 0x11110111, 0x01011111,	0x11011111,	0x11111101,	0x11010111,	0x01110111,	0x11111111,	0x11110101,	0x11011101,	0x01111111,	0x00001101 },
	/* 0.95v */			{	0x44404440,	0x44044400,	0x04444404, 0x44440444, 0x04044444,	0x44044444,	0x44444404,	0x44040444,	0x04440444,	0x44444444,	0x44440404,	0x44044404,	0x04444444,	0x00004404 }
};


extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

/* W0000085638 : Thermal Throttle  */
static int gpu_freq_limit(int freq)
{
	static unsigned int DVFS_level_index;
	
	if(g_bThermal_limit == MALI_TRUE)
	{
		DVFS_Print("\e[41m @@@[%s]@@@ Emergency Thermal Throttle limit is Enabled	freq[%d] Lev[%d]!!! \e[0m\n\n\n", __func__, freq, g_u32DVFS_Manual_Level);
		
		for(DVFS_level_index = min_support_idx ; DVFS_level_index <= max_real_idx; DVFS_level_index++)
		{			
			if(GolfP_freq_table[DVFS_level_index].frequency <= (unsigned int)freq)
			{
				g_u32TMU_MAXLevel = DVFS_level_index;	
				
				if(g_u32DVFS_CurLevel <= g_u32TMU_MAXLevel)
				{
					g_u32DVFS_Manual_Level = g_u32TMU_MAXLevel;
				}
	
				if( DVFS_level_index == MIN_GPU_FREQ_LEVEL)
					g_Thermal_limit_50 = MALI_TRUE;

				return 0;
			}
		}
	}
	else
	{
		DVFS_Print("\e[41m @@@[%s]@@@  Now Thermal Throttle control is OFF \e[0m\n\n\n", __func__);
	}

	return 0;
}

/* W0000085638 : Thermal Throttle  */
static int gpu_freq_limit_free(void)
{
	if(g_bThermal_limit == MALI_TRUE)
	{
		g_u32TMU_MAXLevel = min_support_idx;

		if(g_u32DVFS_CurLevel <= g_u32TMU_MAXLevel)
		{
			g_u32DVFS_Manual_Level = g_u32TMU_MAXLevel;
		}
		g_Thermal_limit_50 = MALI_FALSE;

		DVFS_Print("@@@[%s]@@@ Emergency Thermal Throttle limit is Disabled!!! \n\n\n",__func__);
	}
	
	else
	{
		DVFS_Print("\e[41m @@@[%s]@@@  Now Thermal Throttle control is OFF \e[0m\n\n\n", __func__);	
	}
	return 0;
}


static int gpufreq_asv_notifier(struct notifier_block *notifier,unsigned long pm_event, void *v)
{
	struct sdp_asv_info *asv_info = (struct sdp_asv_info *)v;

	if (!asv_info) {
		DVFS_Print("error - asv_info is NULL\n");
		return NOTIFY_DONE;
	}

	mutex_lock(&GPU_freq_lock);

	switch (pm_event) {
	case SDP_ASV_NOTIFY_AVS_ON:		
		g_bASV_OnOFF = MALI_TRUE;
		DVFS_Print("\e[41m @@@[%s]@@@ AVS ON!!!!!\n", __func__);
		break;

	case SDP_ASV_NOTIFY_AVS_OFF:		
		g_bASV_OnOFF = MALI_FALSE;
		DVFS_Print("\e[41m @@@[%s]@@@ AVS OFF!!!!!\n", __func__);
		break;

	default:
		break;
	}
	
	mutex_unlock(&GPU_freq_lock);

	return NOTIFY_OK;
}
static int gpufreq_tmu_notifier(struct notifier_block *notifier,	unsigned long pm_event, void *v)
{
	struct throttle_params *param = NULL;

	if (v)
	  param = (struct throttle_params *)v;
	
	mutex_lock(&GPU_freq_lock);

	switch (pm_event) {
	case SDP_TMU_FREQ_LIMIT:
		if (param)
			gpu_freq_limit((int)(param->cpu_limit_freq));

		break;

	case SDP_TMU_FREQ_LIMIT_FREE:
		gpu_freq_limit_free();
		break;

	default:
		break;
	}

	mutex_unlock(&GPU_freq_lock);
		
	return NOTIFY_OK;

}


static struct notifier_block gpufreq_tmu_nb = {
	.notifier_call = gpufreq_tmu_notifier,
};

static struct notifier_block gpufreq_asv_nb = {
	.notifier_call = gpufreq_asv_notifier,
};


static void dvfs_set_ema(unsigned int volt)
{
	static unsigned int ema_level=0;
	static int s_index=0;

#if 0
	if(volt > 1200000) { ema_level = 0;} /* 1.20V ~ */
	else if(volt > 1060000) { ema_level = 1;} 	/* 1.06 ~ 1.19 */
	else if(volt > 950000) { ema_level = 2;} 	/* 0.95 ~ 1.05 */
	else { ema_level = 3; } 	/* ~ 0.94V */
#else
	ema_level = 1; // ema level fix : 1.1V 
#endif
	/* no need to change ema */
	if (g_u32DVFS_ema_idx == ema_level)
	{
		DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] changing value is same with current value EMA:[%d] volt:[%d] \e[0m\n", __func__,g_u32DVFS_ema_idx, volt); 
		return;
	}
	for(s_index=0; s_index<14; s_index++)
	{
		*(g_pu32VirAddr_Reg_DVFS_GPU_EMA + s_index) = DVFS_EMA_Table[ema_level][s_index];
	}
	
	g_u32DVFS_ema_idx = ema_level;
	DVFS_Print(KERN_INFO"\e[0;35m ::DBG_DVFS:: [%s] current value EMA:[%d] volt:[%d] \e[0m\n", __func__,g_u32DVFS_ema_idx, volt); 
}

static void dvfs_freq_dbg(unsigned int freq, unsigned int pll_val)
{
	g_u32dbgcnt++;

	if(g_u32dbgcnt==50)
		g_u32dbgcnt = 0;

	g_FreqDbg[g_u32dbgcnt].frequency = freq;
	g_FreqDbg[g_u32dbgcnt].pll_val = pll_val;
}

void kbasep_dvfs_freq_print(void)
{
	int i =0;
	for(i=0; i<50;i++)
		DVFS_DBG("\e[0;45m ::DBG_DVFS[%d] ---> [%d]:[%d]Mhz pll[%x]\e[0m\n", g_u32dbgcnt, i, g_FreqDbg[i].frequency, g_FreqDbg[i].pll_val);
}
KBASE_EXPORT_TEST_API(kbasep_dvfs_freq_print)

static void dvfs_init_clkPath(void)
{
	static unsigned int temp_ScaleRatio;

	mutex_lock(&GPU_freq_lock);

	/* < GPU PLL Lock > */
	/* 1) choose Main path */
	SET_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 8);
	
	/* 2) Scaler off */
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff, 16);

	/* 3) Scale Ratio setting as 62.5MHz */
	temp_ScaleRatio = (* g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio) & 0xFFFFFFF0; /* Clear [3:0] */
	temp_ScaleRatio |= GolfP_temp_freq[Lev1].ratio; 

	*g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio = temp_ScaleRatio;
	
	/* 4) Scaler on */
	SET_BIT(g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff, 16);
	
	/* 5)  choose Temporal Clock path   */
#ifdef USE_TEMP_SCALER_PATH	
	SET_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 9);	
#else
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 9);	
#endif

	g_u32DVFS_CurLevel = min_support_idx;
	mutex_unlock(&GPU_freq_lock);
}

static void dvfs_set_tempPath(unsigned int old_index, unsigned int new_index)
{
	static unsigned int temp_ScaleRatio;
	unsigned int ratio=0;
	unsigned int level=0;
	unsigned int avg_freq;

	avg_freq = (unsigned int)((GolfP_freq_table[old_index].frequency +  GolfP_freq_table[new_index].frequency)/2000);


	if(avg_freq < GolfP_temp_freq[0].frequency)
	{ 
		ratio = GolfP_temp_freq[0].ratio;				
		DVFS_DBG("\e[44m @@@<%d> avg[%d] inx[%d] tmp[%x]@@@\e[0m\n\n",__LINE__,avg_freq ,GolfP_temp_freq[0].frequency, ratio );	
	}

	else if(avg_freq > GolfP_temp_freq[2].frequency)
	{
		ratio = GolfP_temp_freq[2].ratio;				
		DVFS_DBG("\e[44m @@@<%d> avg[%d] inx[%d] tmp[%x]@@@\e[0m\n\n",__LINE__,avg_freq ,GolfP_temp_freq[2].frequency, ratio );	
	}

	else
	{
		for(level= 1 ; level < GPUFREQ_TEMP_LEVEL; level++)
		{	
			if((GolfP_temp_freq[level-1].frequency <= avg_freq) && (avg_freq < GolfP_temp_freq[level].frequency ))
			{
				ratio = GolfP_temp_freq[level-1].ratio;	
				DVFS_DBG("\e[44m @@@<%d> avg[%d] inx[%d] tmp[%x]@@@\e[0m\n\n",__LINE__,avg_freq , GolfP_temp_freq[level-1].frequency, ratio ); 
				break;
			}
		}
	}

	/* 1) Scaler off */
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff, 16);

	/* 2) Scale Ratio setting as 62.5MHz */
	temp_ScaleRatio = (* g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio) & 0xFFFFFFF0; /* Clear [3:0] */
	temp_ScaleRatio |= ratio; 

	*g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio = temp_ScaleRatio;
	
	/* 3) Scaler on */
	SET_BIT(g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff, 16);

}

static void dvfs_set_clkdiv(unsigned int old_index, unsigned int new_index)
{
	/* < GPU PLL Lock > */
	/* 1) choose Temporal Clock path*/

#ifdef USE_TEMP_SCALER_PATH								// 62.5 MHz 
	dvfs_set_tempPath(old_index, new_index);
	
	SET_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 9);	
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 8);
#else 													// 24MHz
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 9);	
	CLEAR_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 8);
#endif

	/* 2) PLL2 PWD off */
	sdp_set_clockgating(PA_PMU_BASE + 0x90, 1<<2, 0);

	/* 3) PLL2 PMS setting */
	__raw_writel(clkdiv_gpu[new_index], g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS);

	/* 4) PLL2 PWD on */
	sdp_set_clockgating(PA_PMU_BASE + 0x90, 1<<2, 1<<2);

	DVFS_DBG("\e[41m @@@TMP Ratio[%x]@@@\e[0m\n\n",__raw_readl(g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio));	

	dvfs_freq_dbg(__raw_readl(g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio), __raw_readl(g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS));
	
	/* 5) For wait 40 usec */
	udelay(40);
	
	/* 6) choose Main path */
	SET_BIT(g_pu32VirAddr_Reg_DVFS_Path_Ctl, 8);

}


void dvfs_set_max_frequency(u32 max_freq)
{
	u32 level; 
	u32 current_freq;

	mutex_lock(&GPU_freq_lock);

	current_freq = GolfP_freq_table[min_support_idx].frequency; 
	
	DVFS_DBG("\e[0;42m ::DBG_DVFS:: [%s] DVFS: current Max Freq = %dMhz changed Max Freq = %dMhz  \e[0m\n",__func__, current_freq/1000, max_freq/1000);

	
	if( max_freq > GolfP_freq_table[0].frequency)
	{
		DVFS_Print("\e[0;32m [%s]: max_freq[%d] should be lower then [%d] \e[0m\n" , __func__, max_freq, GolfP_freq_table[0].frequency);
		goto	out_set;
	
}

	if( max_freq < GolfP_freq_table[max_real_idx].frequency)
	{
		DVFS_Print("\e[0;32m [%s]: max_freq[%d] should be higher then min_freq[%d] \e[0m\n" , __func__, max_freq, GolfP_freq_table[max_real_idx].frequency);
		goto	out_set;

	}

	for(level= 1 ; level < GPUFREQ_LEVEL_END; level++)
	{
		if(GolfP_freq_table[level-1].frequency == max_freq)
		{
			min_support_idx = level-1;				
			g_u32TMU_MAXLevel = min_support_idx;
			break;
		}
                            
	 	else if( ((unsigned int)max_freq < GolfP_freq_table[level-1].frequency) && (GolfP_freq_table[level].frequency < (unsigned int)max_freq))
		{
			min_support_idx = level-1;					
			g_u32TMU_MAXLevel = min_support_idx;
		}
	}

out_set:
	mutex_unlock(&GPU_freq_lock);
}


void dvfs_set_min_frequency(u32 min_freq)
{
	u32 level; 
	u32 current_freq;

	mutex_lock(&GPU_freq_lock);

	current_freq = GolfP_freq_table[max_real_idx].frequency; 
	
	DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] DVFS: current Min Freq = %d changed Min Freq = %d  \e[0m\n",__func__, current_freq, min_freq);

	if(min_freq < GolfP_freq_table[MIN_GPU_FREQ_LEVEL].frequency)
	{
		DVFS_Print("\e[0;32m [%s]: min_freq[%d] should be higher then [%d] \e[0m\n" , __func__, min_freq, GolfP_freq_table[MIN_GPU_FREQ_LEVEL].frequency);
		goto out_set;
	
	}

	if( min_freq > GolfP_freq_table[min_support_idx].frequency)
	{
		DVFS_Print("\e[0;32m [%s]: max_freq[%d] should be lower then min_freq[%d] \e[0m\n" , __func__, min_freq, GolfP_freq_table[min_support_idx].frequency);
		goto out_set;

	}

	for(level= 1 ; level < GPUFREQ_LEVEL_END; level++)
	{
		if(GolfP_freq_table[level-1].frequency == min_freq)
		{
			max_real_idx = level-1;	
			break;
		}
                            
	 	else if( ((unsigned int)min_freq < GolfP_freq_table[level-1].frequency) && (GolfP_freq_table[level].frequency < (unsigned int)min_freq))
		{
			max_real_idx = level-1;				
		}
	}
	
out_set:
	mutex_unlock(&GPU_freq_lock);
}


static void dvfs_set_volt_table(void)
{
	unsigned int i;
	
	DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] DVFS: GPU voltage table is setted with asv group %d \e[0m\n",__func__, gpu_result_of_asv);

	if (gpu_result_of_asv < GPUFREQ_ASV_COUNT) 
	{ 
		for (i = 0; i < GPUFREQ_LEVEL_END; i++) {
			GolfP_Volt_table[i] = GolfP_ASV_voltage[i][gpu_result_of_asv];
		}
	}
	else
	{
		DVFS_Print(KERN_ERR"\e[0;35m ::DBG_DVFS:: %s: asv table index error. %d \e[0m\n", __func__, gpu_result_of_asv);
	}
}

static void dvfs_update_volt_table(void)
{
	int i;

	DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] Voltage table is setted with asv group %d \e[0m\n",__func__, gpu_result_of_asv);

	if (gpu_result_of_asv < GPUFREQ_ASV_COUNT) { 
		for (i = 0; i < GPUFREQ_LEVEL_END; i++) {
			GolfP_Volt_table[i] = GolfP_ASV_voltage[i][gpu_result_of_asv];
		}
	} 

	else {
			DVFS_Print(KERN_ERR"\e[0;35m ::DBG_DVFS:: %s: asv table index error. %d \e[0m\n", __func__, gpu_result_of_asv);
	}
}

static void dvfs_set_volt_level(unsigned int update_level)
{
	int ret = 0;

	DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] Set Voltage level[%d], minuV[%d] , maxuV[%d] \e[0m\n", __func__,update_level, GolfP_Volt_table[update_level], GolfP_Volt_table[update_level] + 10000);

	if (gpu_regulator) 
	{
		ret = regulator_set_voltage(gpu_regulator, GolfP_Volt_table[update_level], GolfP_Volt_table[update_level] + 10000);
		if (ret < 0) {
			ret = -EIO;
			DVFS_Print("\e[0;35m ::DBG_DVFS:: [%s] regulator_set_voltage fail \e[0m\n", __func__);
		}
	}

	else
	{
		ret = -EIO;		
		DVFS_Print("\e[0;35m ::DBG_DVFS:: [%s] regulator_get fail \e[0m\n", __func__);
	}

	return;
}

static void dvfs_set_frequency(kbase_pm_dvfs_action action, unsigned int old_index, unsigned int new_index)
{	
	mutex_lock(&GPU_freq_lock);

	if(action == KBASE_PM_DVFS_CLOCK_UP)
	{
		/* set EMA (higher volt case), volt change -> EMA -> freq change */
		dvfs_set_volt_level(new_index);
		udelay(200);
		dvfs_set_ema(GolfP_Volt_table[new_index]);

#ifdef CPU_LOCK
		if(old_index == cpu_lock_threshold && new_index ==(cpu_lock_threshold-1))
		{
			unsigned int freq = 1200000; /* 100MHz is lock value */
			unsigned int level; 
			sdp_cpufreq_get_level(freq, &level);		// freq? level? ??
			sdp_cpufreq_lock(DVFS_LOCK_ID_GPU, level);	// cpufreq lock? ??
			DVFS_DBG(KERN_INFO"\e[0;42m [%s] cpu_lock----old: %dMHz new:%dMHz\e[0m\n",__func__,(GolfP_freq_table[old_index].frequency)/1000 ,(GolfP_freq_table[g_u32DVFS_CurLevel].frequency)/1000); 
		}	
#endif

	}

	/* < GPU PLL Lock > */
	dvfs_set_clkdiv(old_index, new_index);

	if(action == KBASE_PM_DVFS_CLOCK_DOWN)
	{
		/* set EMA (lower volt case), freq change -> EMA -> volt change */
		dvfs_set_ema(GolfP_Volt_table[new_index]);
		dvfs_set_volt_level(new_index);
		udelay(200);

#ifdef CPU_LOCK
		if(new_index >= cpu_unlock_threshold)
		{
			DVFS_DBG(KERN_INFO"\e[0;44m [%s] cpu_unlock----------> @@%dMHz@@\e[0m\n",__func__,(GolfP_freq_table[g_u32DVFS_CurLevel].frequency)/1000); 
			sdp_cpufreq_lock_free(DVFS_LOCK_ID_GPU);
		}
#endif
	}
	
	mutex_unlock(&GPU_freq_lock);
	DVFS_DBG("\e[41m @@@%dMhz@@@  asv LEV[%d] \e[0m\n\n",(GolfP_freq_table[g_u32DVFS_CurLevel].frequency)/1000, gpu_asv_stored_result);	
	dvfs_freq_dbg((GolfP_freq_table[g_u32DVFS_CurLevel].frequency)/1000, __raw_readl(g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS));

}

void dvfs_control_level(kbase_pm_dvfs_action action) 
{
	static int down_count = 0;
	unsigned int old_index = g_u32DVFS_CurLevel;

	if(g_DVFS_OnOFF == DVFS_FIX)
	{
		g_u32DVFS_CurLevel = g_u32DVFS_FixLevel;
		return; 
	}


	if(action == KBASE_PM_DVFS_NOP)
	{
		return;
	}
	
	if(action == KBASE_PM_DVFS_CLOCK_DOWN)
	{
		if(g_Thermal_limit_50 == MALI_TRUE)
		{
			g_u32DVFS_CurLevel = MIN_GPU_FREQ_LEVEL;
		}
		
		else if(g_u32DVFS_CurLevel < max_real_idx)  /* if not Min freq. - level 3 */
		{
			g_u32DVFS_CurLevel++;
		}
		else
		{
			if (down_count < CPU_FREQ_DOWN_LOCK_DELAY) 
			{
				down_count++;
			} 
			else if (down_count == CPU_FREQ_DOWN_LOCK_DELAY) 
			{
				down_count++;
			}
			
			return;
		}
	}
	
	else /*if(b_DVFS_ctrl == KBASE_PM_DVFS_CLOCK_UP) */
	{
		if(g_u32DVFS_CurLevel != min_support_idx)
		{
			g_u32DVFS_CurLevel--;
		}
		else
		{
			down_count = 0;
			return;
		}

	}

	dvfs_set_frequency(action, old_index, g_u32DVFS_CurLevel);
}
KBASE_EXPORT_TEST_API(dvfs_control_level)

/* Shift used for kbasep_pm_metrics_data.time_busy/idle - units of (1 << 8) ns
   This gives a maximum period between samples of 2^(32+8)/100 ns = slightly under 11s.
   Exceeding this will cause overflow */
#define KBASE_PM_TIME_SHIFT			8

static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
	unsigned long flags;
	kbase_pm_dvfs_action action;
	kbasep_pm_metrics_data *metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, kbasep_pm_metrics_data, timer);
	action = kbase_pm_get_dvfs_action(metrics->kbdev);

	spin_lock_irqsave(&metrics->lock, flags);

	if (metrics->timer_active)
		hrtimer_start(timer,
					  HR_TIMER_DELAY_MSEC(metrics->kbdev->pm.platform_dvfs_frequency),
					  HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&metrics->lock, flags);

	return HRTIMER_NORESTART;
}



static void dvfs_function(struct hrtimer *timer)
{
	kbase_pm_dvfs_action action;
	kbasep_pm_metrics_data * metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, kbasep_pm_metrics_data, timer );
	action = kbase_pm_get_dvfs_action(metrics->kbdev);
	
	/* ASV */
	if(sg_bASV_OnOFF != g_bASV_OnOFF)
	{
		sg_bASV_OnOFF = g_bASV_OnOFF;
		
		/* ASV setting */
		if(sg_bASV_OnOFF == MALI_TRUE)
		{
			gpu_result_of_asv = gpu_asv_stored_result; /* decide asv level */
		}
		else
		{
			gpu_result_of_asv = 0; /* set asv level as 0 */
		}

		/* for update voltage table */
		dvfs_update_volt_table();
		/* for setting voltage */
		dvfs_set_frequency(KBASE_PM_DVFS_CLOCK_UP, min_support_idx , g_u32DVFS_CurLevel);
		DVFS_Print(KERN_INFO "\e[0;41m ::DBG_DVFS:: [%s] ASV_control[%d]  Set Voltage level[%d], minuV[%d] , maxuV[%d] \e[0m\n",__func__, g_bASV_OnOFF,g_u32DVFS_CurLevel, GolfP_Volt_table[g_u32DVFS_CurLevel], GolfP_Volt_table[g_u32DVFS_CurLevel] + 10000);
	}


	/* if cur gpu freq is higher than TMU_Max_freq */
	while(g_u32DVFS_CurLevel < g_u32TMU_MAXLevel) 
	{
		dvfs_control_level(KBASE_PM_DVFS_CLOCK_DOWN);
	}

	/* DVFS */
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		/* W0000085638 : Thermal Throttle : eastson */
		if((action == KBASE_PM_DVFS_CLOCK_UP) && (g_u32DVFS_CurLevel <= g_u32TMU_MAXLevel))
		{
			return; /*do nothing */
		}
		else
		{
			dvfs_control_level(action);
		}	
	}
	
	else /*if(g_DVFS_OnOFF == DVFS_OFF) */
	{
		/* W0000085638 : Thermal Throttle : eastson */
		if(g_u32DVFS_Manual_Level < g_u32TMU_MAXLevel) /* if Manual freq is higher than TMU_Max_freq */
		{
			g_u32DVFS_Manual_Level = g_u32TMU_MAXLevel;
		}
		
		while(g_u32DVFS_Manual_Level > g_u32DVFS_CurLevel)
		{
			dvfs_control_level(KBASE_PM_DVFS_CLOCK_DOWN);
		}

		while(g_u32DVFS_Manual_Level < g_u32DVFS_CurLevel)
		{
			dvfs_control_level(KBASE_PM_DVFS_CLOCK_UP);
		}
	}

	DVFS_Print(KERN_INFO"\e[0;44m ::DBG_DVFS:: [%s] DVFS LEV[%d] [%d]KHz asv LEV[%d] \e[0m\n",__func__, g_u32DVFS_CurLevel, (GolfP_freq_table[g_u32DVFS_CurLevel].frequency), gpu_result_of_asv); 
}

static void dvfs_wq_handler(struct work_struct *work)
{
	my_work_t *my_work = (my_work_t *)work;
	mutex_lock(&wq_lock);
	
	/*dvfs_function(my_work->kbdev); */
	dvfs_function(my_work->timer);
	queue_delayed_work_on(0, dvfs_workqueue, (struct delayed_work * ) dvfs_work, sampling);
	mutex_unlock(&wq_lock);
}

static int dvfs_asv_store_result(unsigned int gpu_tmcb)
{
	unsigned int i;

	/* find AP group */
	for (i = 0; i < GPUFREQ_ASV_COUNT; i++) {
		if (gpu_tmcb <= GolfP_asv_table[i].tmcb_limit) {
			gpu_asv_stored_result = i;
			DVFS_Print(KERN_INFO "\e[0;35m ::DBG_DVFS:: [%s] ASV Group %d selected \e[0m\n",__func__, i);
			break;
		}
	}

	/* If ASV result value is lower than default value Fix with default value.*/
	if ((gpu_asv_stored_result < DEFAULT_GPU_ASV_GROUP) || (gpu_asv_stored_result >= MAX_GPU_ASV_GROUP))
	{
		gpu_asv_stored_result = DEFAULT_GPU_ASV_GROUP;
	}

	DVFS_Print("\e[0;42m ::DBG_DVFS:: GolfP ASV : GPU tmcb : %d, RESULT : group %d \e[0m\n",gpu_tmcb, gpu_asv_stored_result);
	gpu_result_of_asv = gpu_asv_stored_result;

	return 0;
}



mali_error kbasep_pm_metrics_init(kbase_device *kbdev) // [RQ130627-00062] 
{
	struct sdp_asv_info * info;

	// step 1. ASV setting 		
	gpu_result_of_asv = 0;
	gpu_asv_stored_result = 0;

	info = get_sdp_asv_info();

	if (info == NULL) 
	{
		DVFS_Print("\e[0;35m::DBG_DVFS:: ASV: Fail to get ASV Info \e[0m\n");
		return MALI_ERROR_MCLP_INVALID_KERNEL_ARGS;
	}

	gpu_asv_tmcb = info->gpu.tmcb;	
	dvfs_asv_store_result(gpu_asv_tmcb);
	g_bASV_OnOFF =  info->is_avs_on;
	
	// step 2. tmu info setting		
	register_sdp_tmu_notifier(&gpufreq_tmu_nb);
	register_sdp_asv_notifier(&gpufreq_asv_nb);
	
	g_u32TMU_MAXLevel = min_support_idx;
	
	//step 3.  DVFS register setting. 
	g_pu32VirAddr_Reg_DVFS = (unsigned int *)ioremap(DVFS_REGISTER_ADDR, DVFS_IOREMAP_SIZE);
	if(g_pu32VirAddr_Reg_DVFS == (unsigned int *)NULL)
	{
		DVFS_Print("[%s] ioremap fail \n", __func__);
	}

	g_pu32VirAddr_Reg_DVFS_Path_Ctl = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x928);
	g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x808);
	g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x900);
	g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x90C);
	
	/* For Changing Voltage*/
	gpu_regulator = regulator_get(NULL, "GPU_PW");
	if (IS_ERR(gpu_regulator))
	{			
		DVFS_Print("\e[0;32m [%s]: regulator_get fail \e[0m\n" , __func__);
		gpu_regulator = NULL;
	}

	//step 4. Work_Queue Create
	gdev = kbdev;
	kbdev->pm.metrics.kbdev = kbdev;
	kbdev->pm.metrics.vsync_hit = 0;
	kbdev->pm.metrics.utilisation = 0;

	kbdev->pm.metrics.time_period_start = ktime_get();
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.gpu_active = MALI_TRUE;
	kbdev->pm.metrics.timer_active = MALI_TRUE;

	spin_lock_init(&kbdev->pm.metrics.lock);

	hrtimer_init(&kbdev->pm.metrics.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbdev->pm.metrics.timer.function = dvfs_callback;

	hrtimer_start(&kbdev->pm.metrics.timer, HR_TIMER_DELAY_MSEC(kbdev->pm.platform_dvfs_frequency), HRTIMER_MODE_REL);

	kbase_pm_register_vsync_callback(kbdev);



	// step 5. EMA / Voltage setting Initialize
	g_pu32VirAddr_Reg_DVFS_GPU_EMA = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0xD78);
	dvfs_set_volt_table();
	dvfs_set_ema(GolfP_Volt_table[min_support_idx]);
	dvfs_init_clkPath();
	dvfs_set_clkdiv(0, g_u32DVFS_CurLevel);
	
	sampling= usecs_to_jiffies(GPU_SAMPLING_RATE);
	
	dvfs_workqueue = create_freezable_workqueue(dvfs_dev_name);
	if (!dvfs_workqueue) 
	{
		DVFS_Print("[%s] Creation of dvfs_workqueue failed\n", __func__);
	}

	dvfs_work = (my_work_t *) kmalloc(sizeof(my_work_t), GFP_KERNEL);
	if(dvfs_work)
	{
		/* dvfs_work->kbdev = kbdev;  */
		dvfs_work->timer = &(kbdev->pm.metrics.timer);
		INIT_DELAYED_WORK( (struct delayed_work *) dvfs_work, dvfs_wq_handler);

		queue_delayed_work_on(0, dvfs_workqueue, (struct delayed_work * ) dvfs_work, sampling);
	}
	
	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init)

mali_error kbasep_pm_metrics_resume(kbase_device *kbdev) // [RQ130627-00062] 
{
	struct sdp_asv_info * info;

	// step 1. ASV setting 		
	gpu_result_of_asv = 0;
	gpu_asv_stored_result = 0;

	info = get_sdp_asv_info();

	if (info == NULL) 
	{
		DVFS_Print("\e[0;35m::DBG_DVFS:: ASV: Fail to get ASV Info \e[0m\n");
		return MALI_ERROR_MCLP_INVALID_KERNEL_ARGS;
	}

	gpu_asv_tmcb = info->gpu.tmcb;
	dvfs_asv_store_result(gpu_asv_tmcb);
	g_bASV_OnOFF = DVFS_ON;

	
	// step 2. tmu info setting
	g_u32TMU_MAXLevel = min_support_idx;	

	//step 3.  DVFS register setting. 
	g_pu32VirAddr_Reg_DVFS = (unsigned int *)ioremap(DVFS_REGISTER_ADDR, DVFS_IOREMAP_SIZE);
	if(g_pu32VirAddr_Reg_DVFS == (unsigned int *)NULL)
	{
		DVFS_Print("[%s] ioremap fail \n", __func__);
	}

	g_pu32VirAddr_Reg_DVFS_Path_Ctl = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x928);
	g_pu32VirAddr_Reg_DVFS_GPU_PLL_PMS = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x808);
	g_pu32VirAddr_Reg_DVFS_GPU_Scaler_OnOff = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x900);
	g_pu32VirAddr_Reg_DVFS_GPU_ScalingRatio = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0x90C);

	// step 5. EMA / Voltage setting Initialize
	g_pu32VirAddr_Reg_DVFS_GPU_EMA = (unsigned int *)((unsigned int)g_pu32VirAddr_Reg_DVFS + 0xD78);
	dvfs_set_volt_table();
	dvfs_set_ema(GolfP_Volt_table[min_support_idx]);
	dvfs_init_clkPath();
	dvfs_set_clkdiv(0, g_u32DVFS_CurLevel);
	
	return MALI_ERROR_NONE;

}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_resume)


void kbasep_pm_metrics_term(kbase_device *kbdev)
{
	unsigned long flags;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbdev->pm.metrics.timer_active = MALI_FALSE;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	hrtimer_cancel(&kbdev->pm.metrics.timer);

	kbase_pm_unregister_vsync_callback(kbdev);
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term)

void kbasep_pm_record_gpu_idle(kbase_device *kbdev)
{
	unsigned long flags;
	ktime_t now = ktime_get();
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	KBASE_DEBUG_ASSERT(kbdev->pm.metrics.gpu_active == MALI_TRUE);

	kbdev->pm.metrics.gpu_active = MALI_FALSE;

	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	kbdev->pm.metrics.time_busy += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.metrics.time_period_start = now;

	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_idle)

void kbasep_pm_record_gpu_active(kbase_device *kbdev)
{
	unsigned long flags;
	ktime_t now = ktime_get();
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	KBASE_DEBUG_ASSERT(kbdev->pm.metrics.gpu_active == MALI_FALSE);

	kbdev->pm.metrics.gpu_active = MALI_TRUE;

	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.metrics.time_period_start = now;

	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_active)

void kbase_pm_report_vsync(void)
{
	unsigned long flags;
	struct kbase_device *kbdev;
	kbdev = gdev;
	
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbdev->pm.metrics.vsync_hit = 0;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_report_vsync)
EXPORT_SYMBOL(kbase_pm_report_vsync);
/*caller needs to hold kbdev->pm.metrics.lock before calling this function*/
int kbase_pm_get_dvfs_utilisation(kbase_device *kbdev)
{
	int utilisation = 0;
	ktime_t now = ktime_get();
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	if (kbdev->pm.metrics.gpu_active) {
		kbdev->pm.metrics.time_busy += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
		kbdev->pm.metrics.time_period_start = now;
	} else {
		kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
		kbdev->pm.metrics.time_period_start = now;
	}

	if (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy == 0) {
		/* No data - so we return NOP */
		utilisation = -1;
		goto out;
	}

	utilisation = (100 * kbdev->pm.metrics.time_busy) / (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy);


 out:
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;

	return utilisation;
}

kbase_pm_dvfs_action kbase_pm_get_dvfs_action(kbase_device *kbdev)
{
	unsigned long flags;
	int utilisation;
	kbase_pm_dvfs_action action;
	static int keep_cnt = 0;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	utilisation = kbase_pm_get_dvfs_utilisation(kbdev);

	if (utilisation < 0) {
		action = KBASE_PM_DVFS_NOP;
		utilisation = 0;		
		goto out;
	}

	if (utilisation < GolfP_freq_table[g_u32DVFS_CurLevel].min_threshold)
	{
		keep_cnt++;

		if(keep_cnt > MALI_DVFS_KEEP_STAY_CNT)
		{
			action = KBASE_PM_DVFS_CLOCK_DOWN;
			keep_cnt = 0;
		}
		else
		{
			action = KBASE_PM_DVFS_NOP;
		}
	}
	
	else if ( utilisation > GolfP_freq_table[g_u32DVFS_CurLevel].max_threshold )
	{
		keep_cnt = 0;
		action = KBASE_PM_DVFS_CLOCK_UP;
	}
	
	else
	{
		keep_cnt=0;
		action = KBASE_PM_DVFS_NOP;
	}

	kbdev->pm.metrics.utilisation = utilisation;
 	//DVFS_Print(KERN_INFO "\e[0;41m ::DBG_DVFS:: [%s] [%d]KHz  utilization : %d, action : %d \e[0m\n",__func__,GolfP_freq_table[g_u32DVFS_CurLevel].frequency, utilisation, action);
 out:
#ifdef CONFIG_MALI_T6XX_DVFS
	kbase_platform_dvfs_event(kbdev, utilisation);
#endif				/*CONFIG_MALI_T6XX_DVFS */
	kbdev->pm.metrics.vsync_hit = 0;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	return action;
}
KBASE_EXPORT_TEST_API(kbase_pm_get_dvfs_action)

mali_bool kbase_pm_metrics_is_active(kbase_device *kbdev)
{
	mali_bool isactive;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	isactive = (kbdev->pm.metrics.timer_active == MALI_TRUE);
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	return isactive;
}
KBASE_EXPORT_TEST_API(kbase_pm_metrics_is_active)
