#include <linux/regulator/consumer.h>

#include "sdp1202_asv_mptrain.c"

struct asv_judge_table sdp1202_cpu_table[MAX_CPU_ASV_GROUP];

/* fox-ap es0 ids table */
struct asv_judge_table sdp1202_cpu_table_es0[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{   25,  0},	/* Reserved Group (typical default) */
	{   37,  0},
	{   49,  0},
	{   59,  0},
	{   69,  0},
	{   87,  0},
	{   88,  0},
	{ 1023, 63},
	{ 1023, 63},	/* Reserved Group (MAX) */
};
/* fox-ap es1 ids table */
struct asv_judge_table sdp1202_cpu_table_es1[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{   69,  0},	/* Reserved Group (typical default) */
	{   87,  0},
	{  127,  0},
	{  143,  0},
	{  144,  0},
	{ 1023,  0},
	{ 1023,  0},
	{ 1023, 63},
	{ 1023, 63},	/* Reserved Group (MAX) */
};

/* MP asv table */
static struct asv_volt_table sdp1202_mp_table[MAX_MP_ASV_GROUP];

/* mp es0 ids table */
static struct asv_volt_table sdp1202_mp_table_es0[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{   0, 0, 1180000},
	{  13, 0, 1180000},
	{  20, 0, 1170000},
	{  34, 0, 1100000},
	{  69, 0, 1090000},
	{  83, 0, 1070000},
	{  98, 0, 1060000},
	{1023, 0, 1000000},
	{1023, 0, 1000000},
	{1023, 0, 1000000},
};
/* mp es1 ids table */
static struct asv_volt_table sdp1202_mp_table_es1[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{   0, 0, 1100000},
	{  30, 0, 1150000},
	{  52, 0, 1100000},
	{  60, 0, 1090000},
	{  67, 0, 1080000},
	{  85, 0, 1070000},
	{ 110, 0, 1050000},
	{ 141, 0,  990000},
	{1023, 0,  970000},
	{1023, 0,  970000},
};

static u64 ap_pkg_id;
static u64 mp_pkg_id;
static int mp_rev_id;

static int sdp1202_get_cpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read cpu ids */
	info->cpu[0].ids = (ap_pkg_id >> 16) & 0x1F;
	info->cpu[0].ids |= (u32)((ap_pkg_id >> 41) & 0xF) << 5;
	info->cpu[0].ids *= 2;

	/* read cpu tmcb */
	info->cpu[0].tmcb = (ap_pkg_id >> 26) & 0x3F;

	return 0;
}

static int sdp1202_get_gpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read gpu ids */
	info->gpu.ids = (ap_pkg_id >> 21) & 0x1F;
	info->gpu.ids |= (u32)((ap_pkg_id >> 45) & 0x7) << 5;
	info->gpu.ids |= (u32)((ap_pkg_id >> 40) & 0x1) << 8;
	info->gpu.ids *= 2;

	return 0;
}

static int sdp1202_get_mp_ids_tmcb(struct sdp_asv_info *info)
{
	/* read ids */
	info->mp.ids = (u32)(mp_pkg_id >> 48)&0x3FF;

	/* read tmcb */
	info->mp.tmcb = (u32)(mp_pkg_id >> 58)&0x3F;	

	return 0;
}

static void sdp1202_apply_mp_avs(struct sdp_asv_info *info)
{
	if (info->mp.regulator == NULL) {
		pr_warn("AVS: mp0 regulator is NULL\n");
		return;
	}

	regulator_set_voltage(info->mp.regulator, sdp1202_mp_table[info->mp.result].volt,
		sdp1202_mp_table[info->mp.result].volt + 10000);

	udelay(200);

	/* mp ddr training */
	if (sdp1201_asv_mp_training(0) < 0)
		pr_err("AVS: error - MP0 ddr training failed\n");
}

static int sdp1202_store_result(struct sdp_asv_info *info)
{
	int i;
	
	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (info->cpu[0].ids <= info->cpu_table[0][i].ids_limit) {
			info->cpu[0].result = i;
			pr_info("AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (info->cpu[0].result < DEFAULT_ASV_GROUP ||
		info->cpu[0].result >= MAX_CPU_ASV_GROUP) {
		info->cpu[0].result = DEFAULT_ASV_GROUP;
	}

	/* find MP group */
	for (i = 0; i < MAX_MP_ASV_GROUP; i++) {
		if (info->mp.ids <= sdp1202_mp_table[i].ids) {
			info->mp.result = i;
			pr_info("AVS: MP group %d selected.\n", i);
			break;
		}
	}
	if (info->mp.result < DEFAULT_ASV_GROUP ||
		info->mp.result >= MAX_MP_ASV_GROUP) {
		info->mp.result = DEFAULT_ASV_GROUP;
	}

	sdp1202_apply_mp_avs(info);
	
	/* show all ids, tmcb */
	pr_info("AVS: cpu - tmcb: %d, ids: %dmA\n", info->cpu[0].tmcb, info->cpu[0].ids);
	pr_info("AVS: gpu - tmcb: %d, ids: %dmA\n", info->gpu.tmcb, info->gpu.ids);
	pr_info("AVS: mp0 - tmcb: %d, ids: %dmA\n", info->mp.tmcb, info->mp.ids);

	return 0;
}

static void sdp1202_asv_copy_table(void)
{
	int i;
	
	/* ap table */
	if (sdp_get_revision_id()) {
		for (i = 0; i < MAX_CPU_ASV_GROUP; i++)
			sdp1202_cpu_table[i] = sdp1202_cpu_table_es0[i];
	} else {
		for (i = 0; i < MAX_CPU_ASV_GROUP; i++)
			sdp1202_cpu_table[i] = sdp1202_cpu_table_es1[i];
	}
	
	/* mp table */
	if (mp_rev_id == 0) {
		for (i = 0; i < MAX_MP_ASV_GROUP; i++)
			sdp1202_mp_table[i] = sdp1202_mp_table_es0[i];
	} else {
		for (i = 0; i < MAX_MP_ASV_GROUP; i++)
			sdp1202_mp_table[i] = sdp1202_mp_table_es1[i];
	}
}

static int sdp1202_asv_init(struct sdp_asv_info *info)
{
	u32 ap_base = 0xFE000000 + 0x80000;
	void __iomem * mp_base;
	int timeout = 500;

	info->cpu_table[0] = sdp1202_cpu_table;

	info->mp_table = sdp1202_mp_table;

	info->get_cpu_ids_tmcb = sdp1202_get_cpu_ids_tmcb;
	info->get_gpu_ids_tmcb = sdp1202_get_gpu_ids_tmcb;
	info->get_mp_ids_tmcb = sdp1202_get_mp_ids_tmcb;
	
	info->store_result = sdp1202_store_result;

	/* prepare to read ap efuse*/
	writel(0x1F, (void *)(ap_base + 0x4));
	while (timeout--) {
		if (readl((void *)ap_base) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("fail to read ap efuse\n");
		return -1;
	}

	/* get ap package id [32:79] */
	ap_pkg_id = readl((void *)(ap_base + 0x8));
	ap_pkg_id |= ((u64)readl((void *)(ap_base + 0x10))) << 32;

	/* get mp regulator */
	info->mp.regulator = regulator_get(NULL, "MP0_PW");
	if (IS_ERR(info->mp.regulator)) {
		pr_err("AVS: error - failed to get mp0 regulator\n");
		info->mp.regulator = NULL;
	}

	/* get mp efuse value */
	mp_base = ioremap(0x18080000, 0x14);
	if (mp_base == NULL) {
		pr_err("fail to map mp0 address\n");
		return -1;
	}
	
	/* prepare to read mp efuse*/
	timeout = 500;
	writel(0x1F, (void *)((u32)mp_base + 0x4));
	while (timeout--) {
		if (readl((void *)mp_base) == 0)
			break;
		msleep(1);
	}
	if (!timeout) {
		pr_warn("mp0 efuse read fail!\n");
		iounmap(mp_base);
		return -1;
	}

	/* get mp package id */
	mp_pkg_id = readl((void *)((u32)mp_base + 0xC));
	mp_pkg_id = ((u64)readl((void *)((u32)mp_base + 0x8))) << 32;
	mp_rev_id = (u32)(mp_pkg_id >> 42)&0x3;

	iounmap(mp_base);

	/* copy AP and MP ids table */
	sdp1202_asv_copy_table();

	return 0;
}
