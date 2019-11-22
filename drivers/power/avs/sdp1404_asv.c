#include <linux/regulator/consumer.h>

#define BIG_CLUSTER	0
#define LITTLE_CLUSTER	1
#define MAX_CLUSTER	2

#define AP_BASE		(SFR_VA + 0x10080000 - SFR0_BASE)

/******************/
/* SDP1404 asv    */
/******************/
/* Hawk-P select the group only using TMCB */
struct asv_judge_table sdp1404_big_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{ 1023, 32},	/* Reserved Group (typical default) */
	{ 1023, 35},
	{ 1023, 43},
	{ 1023, 47},
	{ 1023, 50},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},	/* Reserved Group (MAX) */
};

struct asv_judge_table sdp1404_lt_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{ 1023, 63},	/* Reserved Group (typical default) */
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},
	{ 1023, 63},		
	{ 1023, 63},	/* Reserved Group (MAX) */
};

struct asv_dual_volt_table sdp1404_us_table[MAX_US_ASV_GROUP] = {
	/* tmcb,   volt1,   volt2 */
	{ 13, 1000000, 1000000},
	{ 18,  980000,  980000},
	{ 26,  960000,  960000},
	{ 30,  940000,  940000},
	{ 63,  920000,  920000},
	{ 63,  920000,  920000},
	{ 63,  920000,  920000},
	{ 63,  920000,  920000},
	{ 63,  920000,  920000},
	{ 63,  920000,  920000},
};

struct asv_volt_table sdp1404_core_table[MAX_CORE_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{ 0, 32, 1180000},
	{ 0, 36, 1140000},
	{ 0, 39, 1120000},
	{ 0, 41, 1110000},
	{ 0, 46, 1080000},
	{ 0, 63, 1060000},
	{ 0, 63, 1060000},
	{ 0, 63, 1060000},
	{ 0, 63, 1060000},
	{ 0, 63, 1060000},
};

static int sdp1404_get_cpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read cpu ids */
	info->cpu[0].ids = ((readl((void *)(AP_BASE + 0xC)) >> 16) & 0x1FF) * 2;
	info->cpu[1].ids = info->cpu[0].ids;

	/* read cpu tmcb */
	info->cpu[0].tmcb = (readl((void *)(AP_BASE + 0x14)) >> 10) & 0x3F;
	info->cpu[1].tmcb = info->cpu[0].tmcb;
	
	return 0;
}

static int sdp1404_get_gpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read gpu ids */
	info->gpu.ids = ((readl((void *)(AP_BASE + 0xC)) >> 25) |
			((readl((void *)(AP_BASE + 0x10)) & 0x3) << 7)) * 2;

	/* read gpu tmcb */
	info->gpu.tmcb = (readl((void *)(AP_BASE + 0x14)) >> 16) & 0x3F;
	
	return 0;
}

static int sdp1404_get_core_ids_tmcb(struct sdp_asv_info *info)
{
	/* read ids */
	info->core.ids = ((readl((void *)(AP_BASE + 0x10)) >> 2) & 0x1FF) * 2;

	/* read tmcb */
	info->core.tmcb = (readl((void *)(AP_BASE + 0x10)) >> 11) & 0x3F;

	return 0;
}

static int sdp1404_get_us_ids_tmcb(struct sdp_asv_info *info)
{
	void __iomem *base;
	int timeout;

	base = ioremap(0x1ad80000, 0x20);
	if (base == NULL) {
		pr_err("AVS ERROR - hawk-us ioremap fail\n");
		return -1;
	}

	/* enable US chip id */
	timeout = 100;
	writel(0x1F, (void*)((u32)base + 0x4));
	while (timeout) {
		if (readl(base) == 0)
			break;
	}
	if (!timeout) {
		pr_err("AVS ERROR - fail to enable US chip id\n");
		iounmap(base);
		return -1;
	}

	/* read us ids */
	info->us.ids = (readl((void*)((u32)base + 0x8)) >> 16) & 0x3FF;
	
	/* read us tmcb */
	info->us.tmcb = (readl((void*)((u32)base + 0x8)) >> 26) & 0x3F;

	iounmap(base);
	
	return 0;
}

static int sdp1404_apply_us_avs(struct sdp_asv_info *info)
{
	int i, ret;
	int volt;
	
	if (!info->us.regulator) {
		pr_err("AVS: hawk-us regualtor is NULL\n");
		return -EPERM;
	}

	if (!info->us_table) {
		pr_err("AVS: hawk-us table is NULL\n");
		return -EPERM;
	}
	
	/* find US group */
	for (i = 0; i < MAX_US_ASV_GROUP; i++) {
		if (info->us.tmcb <= info->us_table[i].ids_tmcb)
			break;
	}
	if (i == MAX_US_ASV_GROUP) {
		pr_warn("AVS: can't find us group. force set to group 0\n");
		i = 0;
	}
		
	info->us.result = i;
	pr_info("AVS: US group %d selected by tmcb\n", info->us.result);

	/* apply US voltage */
	volt = info->us_table[info->us.result].volt1;
		
	pr_info("AVS: set US voltage to %duV\n", volt);
	ret = regulator_set_voltage(info->us.regulator, volt, volt);
	if (ret < 0) {
		pr_err("AVS ERROR - fail to set US voltage\n");
		return -EIO;
	}

	return 0;
}

#if 0
static int sdp1404_apply_core_avs(struct sdp_asv_info *info)
{
	int i, ret;
	int volt;
	
	if (!info->core.regulator) {
		pr_err("AVS: core regualtor is NULL\n");
		return -EPERM;
	}

	if (!info->core_table) {
		pr_err("AVS: core table is NULL\n");
		return -EPERM;
	}

	/* find CORE group */
	for (i = 0; i < MAX_CORE_ASV_GROUP; i++) {
		if (info->core.tmcb <= info->core_table[i].tmcb)
			break;
	}
	if (i == MAX_CORE_ASV_GROUP) {
		pr_warn("AVS: can't find core group. force set to group 0\n");
		i = 0;
	}

	info->core.result = i;
	pr_info("AVS: CORE group %d selected by tmcb\n", info->core.result);

	/* apply CORE voltage */
	volt = info->core_table[info->core.result].volt;

	pr_info("AVS: set CORE voltage to %duV\n", volt);
	ret = regulator_set_voltage(info->core.regulator, volt, volt);
	if (ret < 0) {
		pr_err("AVS ERROR - fail to set CORE voltage\n");
		return -EIO;
	}

	return 0;
}
#endif

static int sdp1404_store_result(struct sdp_asv_info *info)
{
	int i, j;
	
	/* find CPU group */
	for (i = 0; i < MAX_CLUSTER; i++) {
		for (j = 0; j < MAX_CPU_ASV_GROUP; j++) {
			if (info->cpu[i].tmcb <= info->cpu_table[i][j].tmcb_limit) {
				info->cpu[i].result = j;
				pr_info("AVS: %s CPU group %d selected.\n",
					(i == BIG_CLUSTER) ? "big": "little", j);
				break;
			}
		}
		if (info->cpu[i].result < DEFAULT_ASV_GROUP ||
			info->cpu[i].result >= MAX_CPU_ASV_GROUP) {
			info->cpu[i].result = DEFAULT_ASV_GROUP;
		}
	}
	
	sdp1404_apply_us_avs(info);
	//sdp1404_apply_core_avs(info);

	/* show all ids, tmcb */
	pr_info("AVS: cpu - tmcb: %d, ids: %dmA\n", info->cpu[BIG_CLUSTER].tmcb, info->cpu[BIG_CLUSTER].ids);
	//pr_info("AVS: lt  cpu - tmcb: %d, ids: %dmA\n", info->cpu[LITTLE_CLUSTER].tmcb, info->cpu[LITTLE_CLUSTER].ids);
	pr_info("AVS: gpu - tmcb: %d, ids: %dmA\n", info->gpu.tmcb, info->gpu.ids);
	pr_info("AVS: total - tmcb: %d, ids: %dmA\n", info->core.tmcb, info->core.ids);
	pr_info("AVS: us  - tmcb: %d, ids: %dmA\n", info->us.tmcb, info->us.ids);
		
	return 0;
}

static int sdp1404_suspend(struct sdp_asv_info *info)
{
	int volt;

	if (info->us.regulator) {
		volt = info->us_table[info->us.result].volt1;
		
		regulator_set_voltage(info->us.regulator, volt + 10000, volt + 10000);
	}
	
	return 0;
}

static int sdp1404_resume(struct sdp_asv_info *info)
{
	/* apply US voltage */
	sdp1404_apply_us_avs(info);
	
	return 0;
}

static int sdp1404_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	enum sdp_board bd_type;
	
	info->cpu_table[BIG_CLUSTER] = sdp1404_big_table;
	info->cpu_table[LITTLE_CLUSTER] = sdp1404_lt_table;
	info->core_table = sdp1404_core_table;

	info->get_cpu_ids_tmcb = sdp1404_get_cpu_ids_tmcb;
	info->get_gpu_ids_tmcb = sdp1404_get_gpu_ids_tmcb;
	info->get_core_ids_tmcb = sdp1404_get_core_ids_tmcb;
	
	info->store_result = sdp1404_store_result;

	info->suspend = sdp1404_suspend;
	info->resume = sdp1404_resume;

	/* only for Main TV includes Hawk-US */
	bd_type = get_sdp_board_type();
	if (bd_type != SDP_BOARD_AV &&
		bd_type != SDP_BOARD_SBB) {
		info->us_table = sdp1404_us_table;
		info->get_us_ids_tmcb = sdp1404_get_us_ids_tmcb;
		info->us.regulator = regulator_get(NULL, "US_PW");
		if (IS_ERR(info->us.regulator)) {
			pr_err("AVS: failed to get US regulator\n");
			info->us.regulator = NULL;
		}
	}

#if 0
	/* get CORE regulator */
	info->core.regulator = regulator_get(NULL, "CORE_PW");
	if (IS_ERR(info->core.regulator)) {
		pr_err("AVS: failed to get CORE regulator\n");
		info->core.regulator = NULL;
	}
#endif

	/* enable hawk-p chip id register */
	writel(0x1F, (void*)(AP_BASE + 0x4));
	while (timeout) {
		if (readl((void*)AP_BASE) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		pr_err("AVS: hawk-p chip id enable failed!\n");
		return -EIO;
	}
		
	return 0;
}
