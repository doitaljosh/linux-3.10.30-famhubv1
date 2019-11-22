#include <linux/regulator/consumer.h>

/******************/
/* SDP1406 asv    */
/******************/
#define CHIPID_BASE	(SFR_VA + 0x00180000 - 0x00100000)

/* Hawk-M select the group only using TMCB */
struct asv_judge_table sdp1406_ids_table[MAX_CPU_ASV_GROUP] = {
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

#if 0//def CONFIG_SDP_MESSAGEBOX
struct avs_state {
	u32 tmcb_ids;
	u32 volt;
};

struct avs_state sdp1406_cpugpu_state[4] = {
	/* TMCB, VOLT */
	{  0, 1152600},
	{ 63, 1101280},
	{ 63, 1053081},
	{ 63, 1023734},
};

struct avs_state sdp1406fhd_cpugpu_state[4] = {
	/* TMCB, VOLT */
	{  0, 1000000},
	{ 63,  950000},
	{ 63,  900000},
	{ 63,  850000},
};

struct avs_state sdp1406_core_state[4] = {
	/* TMCB, VOLT */
	{  0, 1153333},
	{ 63, 1104439},
	{ 63, 1051002},
	{ 63, 1025297},
};

struct avs_state sdp1406fhd_core_state[4] = {
	/* TMCB, VOLT */
	{  0, 1000000},
	{ 63,  950000},
	{ 63,  900000},
	{ 63,  850000},
};
#endif /* CONFIG_SDP_MESSAGEBOX */

static int sdp1406_get_cpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read cpu ids */
	info->cpu[0].ids = ((readl((void *)(CHIPID_BASE + 0xC)) >> 16) & 0x1FF) * 2;

	/* read cpu tmcb */
	info->cpu[0].tmcb = (readl((void *)(CHIPID_BASE + 0x14)) >> 10) & 0x3F;
	
	return 0;
}

static int sdp1406_get_gpu_ids_tmcb(struct sdp_asv_info *info)
{
	/* read gpu ids */
	info->gpu.ids = ((readl((void *)(CHIPID_BASE + 0xC)) >> 25) |
			((readl((void *)(CHIPID_BASE + 0x10)) & 0x3) << 7)) * 2;

	/* read gpu tmcb */
	info->gpu.tmcb = (readl((void *)(CHIPID_BASE + 0x14)) >> 16) & 0x3F;
	
	return 0;
}

static int sdp1406_get_core_ids_tmcb(struct sdp_asv_info *info)
{
	/* read ids */
	info->core.ids = ((readl((void *)(CHIPID_BASE + 0x10)) >> 2) & 0x1FF) * 2;

	/* read tmcb */
	info->core.tmcb = (readl((void *)(CHIPID_BASE + 0x10)) >> 11) & 0x3F;
	
	return 0;
}

#if 0//def CONFIG_SDP_MESSAGEBOX
extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);

#define CORE_AVS_ADDR	0x7A
#define CPUGPU_AVS_ADDR	0x7B

static int send_micom_avs_state(u32 cmd_addr, int state)
{
	int ret, retry = 3;
	char buff[12] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	if (cmd_addr != CORE_AVS_ADDR &&
		cmd_addr != CPUGPU_AVS_ADDR) {
		pr_err("AVS: cmd addr is not valid %x\n", cmd_addr);
		return -EINVAL;
	}

	/* set command */
	buff[2] = cmd_addr;

	/* gpio high/low */
	buff[3] = state & 0x1;
	buff[4] = (state >> 1) & 0x1;
	
	/* calc checksum */
	buff[8] = buff[2] + buff[3] + buff[4];	
		
	/* send message */
	pr_debug("send micom cmd : 0xff 0xff 0x%02x 0x%02x 0x%02x\n",
			buff[2], buff[3], buff[4]);
	do {
		ret = sdp_messagebox_write(buff, 9);
		if (ret == 9)
			break;
		pr_debug("retry send message %d\n", retry);
	} while (retry--);
	
	if (!retry)
		pr_debug("micom comm failed\n");

	pr_info("AVS: send %s AVS gpio state %d\n",
		(cmd_addr==CPUGPU_AVS_ADDR)?"CPU/GPU":"CORE",
		state);

	return 0;
}

static u32 get_cpugpu_avs_state(struct sdp_asv_info *info)
{
	u32 state;
	int i;
	bool is_fhd;

	if (of_machine_is_compatible("samsung,sdp1406fhd"))
		is_fhd = true;
	else
		is_fhd = false;

	for (i = 0; i < 4; i++) {
		if (is_fhd) {
			if (info->cpu[0].tmcb <= sdp1406fhd_cpugpu_state[i].tmcb_ids)
				break;
		} else {
			if (info->cpu[0].tmcb <= sdp1406_cpugpu_state[i].tmcb_ids)
				break;
		}	
	}

	state = i;

	if (i == 4) {
		pr_debug("can't find cpu/gpu avs state. return state 0\n");
		state = 0;
	}
	
	return state;
}

static u32 get_core_avs_state(struct sdp_asv_info *info)
{
	u32 state;
	int i;
	bool is_fhd;

	if (of_machine_is_compatible("samsung,sdp1406fhd"))
		is_fhd = true;
	else
		is_fhd = false;

	for (i = 0; i < 4; i++) {
		if (is_fhd) {
			if (info->core.tmcb <= sdp1406fhd_core_state[i].tmcb_ids)
				break;
		} else {
			if (info->core.tmcb <= sdp1406_core_state[i].tmcb_ids)
				break;
		}	
	}

	state = i;

	if (i == 4) {
		pr_debug("can't find core avs state. return state 0\n");
		state = 0;
	}
	
	return state;
}
#endif /* CONFIG_SDP_MESSAGEBOX */

static int sdp1406_store_result(struct sdp_asv_info *info)
{
	int i;
#if 0//def CONFIG_SDP_MESSAGEBOX
	u32 state;
#endif
	
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

	/* show all ids, tmcb */
	pr_info("AVS: cpu - tmcb: %d, ids: %dmA\n", info->cpu[0].tmcb, info->cpu[0].ids);
	pr_info("AVS: gpu - tmcb: %d, ids: %dmA\n", info->gpu.tmcb, info->gpu.ids);
	pr_info("AVS: core - tmcb: %d, ids: %dmA\n", info->core.tmcb, info->core.ids);

#if 0//def CONFIG_SDP_MESSAGEBOX
	/* send avs result to micom */
	state = get_cpugpu_avs_state(info);
	send_micom_avs_state(CPUGPU_AVS_ADDR, state);
	
	state = get_core_avs_state(info);
	send_micom_avs_state(CORE_AVS_ADDR, state);
#endif

	return 0;
}

static int sdp1406_suspend(struct sdp_asv_info *info)
{
	return 0;
}

static int sdp1406_resume(struct sdp_asv_info *info)
{
	return 0;
}

static int sdp1406_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	
	info->cpu_table[0] = sdp1406_ids_table;

	info->get_cpu_ids_tmcb = sdp1406_get_cpu_ids_tmcb;
	info->get_gpu_ids_tmcb = sdp1406_get_gpu_ids_tmcb;
	info->get_core_ids_tmcb = sdp1406_get_core_ids_tmcb;
	
	info->store_result = sdp1406_store_result;

	info->suspend = sdp1406_suspend;
	info->resume = sdp1406_resume;

	/* enable Hawk-M chip id register */
	writel(0x1F, (void*)(CHIPID_BASE + 0x4));
	while (timeout) {
		if (readl((void*)CHIPID_BASE) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		pr_err("AVS: hawk-m chip id enable failed!\n");
		return -EIO;
	}
	
	return 0;
}
