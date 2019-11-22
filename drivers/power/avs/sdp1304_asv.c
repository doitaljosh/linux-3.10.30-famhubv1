#include <linux/regulator/consumer.h>

/******************/
/* SDP1304 asv    */
/******************/
/* Golf-AP select the group only using TMCB */
struct asv_judge_table sdp1304_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{    0, 16},	/* Reserved Group (typical default) */
	{ 1023, 21},
	{ 1023, 24},
	{ 1023, 32},
	{ 1023, 37},
	{ 1023, 44},
	{ 1023, 63},
	{ 1023, 63},		
	{ 1023, 63},	/* Reserved Group (MAX) */
};

struct asv_volt_table sdp1304_mp_table[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{    0,  0, 1200000},
	{ 1023, 20, 1200000},
	{ 1023, 26, 1170000},
	{ 1023, 31, 1140000},
	{ 1023, 47, 1110000},
	{ 1023, 54, 1040000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
	{ 1023, 63,  960000},
};

struct asv_volt_table sdp1304_mp_evk_table[MAX_MP_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{    0,  0, 1180000},
	{ 1023, 20, 1180000},
	{ 1023, 26, 1150000},
	{ 1023, 31, 1120000},
	{ 1023, 47, 1090000},
	{ 1023, 54, 1020000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
	{ 1023, 63,  940000},
};

struct asv_dual_volt_table sdp1304_us_table[MAX_US_ASV_GROUP] = {
	/* ids, volt1, volt2 */
	{  150, 1100000, 1200000},
	{  260, 1050000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
	{ 1023, 1030000, 1130000},
};

static bool us_altvolt_on = false;

static int sdp1304_get_cpu_ids_tmcb(struct sdp_asv_info *asv_info)
{
	/* read cpu ids [56:48] */
	asv_info->cpu[0].ids = (readl((void*)(SFR_VA + 0x80008)) >> 16) & 0x1FF;	
	asv_info->cpu[0].ids *= 2; /* conver to 1mA scale */

	/* read cpu tmcb [63:58] */
	asv_info->cpu[0].tmcb = readl((void*)(SFR_VA + 0x80008)) >> 26;
	
	return 0;
}

static int sdp1304_get_gpu_ids_tmcb(struct sdp_asv_info *asv_info)
{
	/* read gpu ids */
	asv_info->gpu.ids = readl((void*)(SFR_VA + 0x80010)) & 0xFF; /* [71:64] */
	asv_info->gpu.ids *= 2; /* conver to 1mA scale */

	/* read gpu tmcb [63:58] */
	asv_info->gpu.tmcb = readl((void*)(SFR_VA + 0x80008)) >> 26;
	
	return 0;
}

static int sdp1304_get_mp_ids_tmcb(struct sdp_asv_info *asv_info)
{
	int timeout;
	void __iomem *mp_base;

	/* ioremap */
	mp_base = ioremap(0x18080000, 0x10);
	if (mp_base == NULL) {
		pr_err("AVS ERROR - MP0 ioremap fail\n");
		return -EIO;
	}
		
	/* enable MP chip id */
	timeout = 200;
	writel(0x1F, mp_base + 0x4);
	while (timeout) {
		if (readl(mp_base) == 0)
			break;
	}
	if (!timeout) {
		pr_err("AVS ERROR - fail to enable MP0 chip id\n");
		iounmap(mp_base);
		return -EIO;
	}

	/* read mp ids */
	asv_info->mp.ids = (readl(mp_base + 0x8) >> 16) & 0x3FF;
	
	/* read mp tmcb */
	asv_info->mp.tmcb = (readl(mp_base + 0x8) >> 26) & 0x3F;

	iounmap(mp_base);

	return 0;
}

static int sdp1304_get_us_ids_tmcb(struct sdp_asv_info *info)
{
	void __iomem *base;
	int timeout;

	base = ioremap(0x1d580000, 0x10);
	if (base == NULL) {
		pr_err("AVS ERROR - golf-us ioremap fail\n");
		return -1;
	}

	/* enable US chip id */
	timeout = 200;
	writel(0x1F, base + 0x4);
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
	info->us.ids = (readl(base + 0x8) >> 16) & 0x3FF;
	
	/* read us tmcb */
	info->us.tmcb = (readl(base + 0x8) >> 26) & 0x3F;

	pr_info("AVS: us - tmcb: %u, ids: %umA\n", info->us.tmcb, info->us.ids);
	
	iounmap(base);
	
	return 0;
}

static int sdp1304_apply_mp_avs(struct sdp_asv_info *info)
{
	int i, ret;

	if (!info->mp.regulator) {
		pr_err("AVS: MP reuglator is NULL\n");
		return -EPERM;
	}
	
	/* find MP group */
	for (i = 0; i < MAX_MP_ASV_GROUP; i++) {
		if (info->mp.tmcb <= info->mp_table[i].tmcb)
			break;
	}
	info->mp.result = i;
	pr_info("AVS: MP0 gourp %d selected\n", info->mp.result);
	
	/* apply MP voltage */
	pr_info("AVS: set MP0 voltage to %duV\n", info->mp_table[info->mp.result].volt);
	ret = regulator_set_voltage(info->mp.regulator,
					info->mp_table[info->mp.result].volt,
					info->mp_table[info->mp.result].volt);
	if (ret < 0) {
		pr_err("AVS: ERROR - failed to set MP0 voltage\n");
		return -EIO;
	}

	return 0;
}

static int sdp1304_apply_us_avs(struct sdp_asv_info *info)
{
	int i, ret;
	int volt;
	
	if (!info->us.regulator) {
		pr_err("AVS: US regualtor is NULL\n");
		return -EPERM;
	}
	
	/* find US group */
	for (i = 0; i < MAX_US_ASV_GROUP; i++) {
		if (info->us.ids <= info->us_table[i].ids_tmcb)
			break;
	}
	info->us.result = i;
	pr_info("AVS: US gourp %d selected by using ids\n", info->us.result);

	/* apply US voltage */
	if (us_altvolt_on)
		volt = info->us_table[info->us.result].volt2;
	else
		volt = info->us_table[info->us.result].volt1;
		
	pr_info("AVS: set US voltage to %duV\n", volt);
	ret = regulator_set_voltage(info->us.regulator, volt, volt);
	if (ret < 0) {
		pr_err("AVS ERROR - fail to set US voltage\n");
		return -EIO;
	}

	return 0;
}

static int sdp1304_store_result(struct sdp_asv_info *info)
{
	int i;
	
	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (info->cpu[0].tmcb <= info->cpu_table[0][i].tmcb_limit) {
			info->cpu[0].result = i;
			pr_info("AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (info->cpu[0].result < DEFAULT_ASV_GROUP ||
		info->cpu[0].result >= MAX_CPU_ASV_GROUP) {
		info->cpu[0].result = DEFAULT_ASV_GROUP;
	}

	/* apply ABB at cpu ids < 70mA */
	if (info->cpu[0].ids <= 70) {
		/* ABB x0.8 */
		pr_info("AVS: apply ABB x0.8\n");
		writel(0x9, (void *)(SFR_VA + 0xB00E3C));
	}

	sdp1304_apply_mp_avs(info);
	
	sdp1304_apply_us_avs(info);

	/* show all ids, tmcb */
	pr_info("AVS: cpu - tmcb: %d, ids: %dmA\n", info->cpu[0].tmcb, info->cpu[0].ids);
	pr_info("AVS: gpu - tmcb: %d, ids: %dmA\n", info->gpu.tmcb, info->gpu.ids);
	pr_info("AVS: mp0 - tmcb: %d, ids: %dmA\n", info->mp.tmcb, info->mp.ids);
	if (of_machine_is_compatible("samsung,golf-us"))
		pr_info("ASV: us - tmcb: %d, ids: %d\n", info->us.tmcb, info->us.ids);
		
	return 0;
}

static int sdp1304_suspend(struct sdp_asv_info *info)
{
	int volt;

	if (info->mp.regulator)
		regulator_set_voltage(info->mp.regulator,
						info->mp_table[info->mp.result].volt + 10000,
						info->mp_table[info->mp.result].volt + 10000);

	if (info->us.regulator) {
		if (us_altvolt_on)
			volt = info->us_table[info->us.result].volt2;
		else
			volt = info->us_table[info->us.result].volt1;
		
		regulator_set_voltage(info->us.regulator, volt + 10000, volt + 10000);
	}
	
	return 0;
}

static int sdp1304_resume(struct sdp_asv_info *info)
{
	/* apply MP voltage */
	sdp1304_apply_mp_avs(info);
	
	/* apply US voltage */
	sdp1304_apply_us_avs(info);
	
	return 0;
}

static int sdp1304_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->cpu_table[0] = sdp1304_ids_table;

	if (get_sdp_board_type() == SDP_BOARD_SBB)
		info->mp_table = sdp1304_mp_evk_table;
	else
		info->mp_table = sdp1304_mp_table;
	
	info->get_cpu_ids_tmcb = sdp1304_get_cpu_ids_tmcb;
	info->get_gpu_ids_tmcb = sdp1304_get_gpu_ids_tmcb;
	info->get_mp_ids_tmcb = sdp1304_get_mp_ids_tmcb;
	
	info->store_result = sdp1304_store_result;

	info->suspend = sdp1304_suspend;
	info->resume = sdp1304_resume;

	if (of_machine_is_compatible("samsung,golf-us")) {
		info->us_table = sdp1304_us_table;
		info->get_us_ids_tmcb = sdp1304_get_us_ids_tmcb;
		info->us.regulator = regulator_get(NULL, "US_PW");
		if (IS_ERR(info->us.regulator))
			pr_err("AVS: ERROR - failed to get US regulator\n");
	}

	/* get MP0 regulator */
	info->mp.regulator = regulator_get(NULL, "MP0_PW");
	if (IS_ERR(info->mp.regulator))
		pr_err("AVS: ERROR - failed to get MP0 regualtor\n");

	/* enable AP chip id register */
	writel(0x1F, (void*)(SFR_VA + 0x80004));
	while (timeout) {
		if (readl((void*)(SFR_VA + 0x80000)) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		pr_err("AVS: AP chip id enable failed!\n");
		return -EIO;
	}
		
	return 0;
}

int sdp1304_us_set_altvolt(bool on)
{
	int volt;
	int ret;
	
	/* set to alternative voltage */
	if (on == true)
		volt = asv_info->us_table[asv_info->us.result].volt2;
	else /* set to original voltage */
		volt = asv_info->us_table[asv_info->us.result].volt1;

	if (asv_info->us.regulator == NULL) {
		pr_err("AVS: us_regulator is NULL\n");
		return -1;
	}

	us_altvolt_on = on;

	/* set voltage */
	pr_info("AVS: set US voltage to %duV\n", volt);
	ret = regulator_set_voltage(asv_info->us.regulator, volt, volt);
	if (ret < 0) {
		pr_err("AVS ERROR - fail to set US voltage\n");
		return -EIO;
	}
	
	return 0;
}
EXPORT_SYMBOL(sdp1304_us_set_altvolt);

