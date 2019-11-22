#include <linux/regulator/consumer.h>

/******************/
/* SDP1307 asv    */
/******************/
struct asv_judge_table sdp1307_ids_table[MAX_CPU_ASV_GROUP] = {
	/* IDS, TMCB */
	{    0,  0},	/* Reserved Group (typical fixed) */
	{  100, 63},	/* Reserved Group (typical default) */
	{  300, 63},
	{  350, 63},
	{  500, 63},
	{  700, 63},
	{ 1890, 63},
	{ 1890, 63},
	{ 1890, 63},
	{ 1890, 63},	/* Reserved Group (MAX) */
};

struct asv_volt_table sdp1307_core_table[MAX_CORE_ASV_GROUP] = {
	/* ids, tmcb, volt */
	{   0, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
	{ 510, 0, 1100000},
};

static int sdp1307_get_cpu_ids(struct sdp_asv_info *info)
{
	u32 val;
	u32 rev_id = (readl(info->base + 0x8) >> 23) & 0x1;
	
	/* cpu ids */
	if (rev_id) {
		val = (readl(info->base + 0x10) >> 8) & 0xFF;
		info->cpu[0].ids = val * 3;
	} else {
		val = (readl(info->base + 0x10) >> 10) & 0x3F;
		info->cpu[0].ids = val * 30;
	}
	printk(KERN_DEBUG "cpu ids = %u\n", info->cpu[0].ids);

	return 0;
}

static int sdp1307_get_gpu_ids(struct sdp_asv_info *info)
{
	u32 val;
	u32 rev_id = (readl(info->base + 0x8) >> 23) & 0x1;
	
	/* gpu ids */
	if (rev_id) {
		val = readl(info->base + 0x10) & 0xFF;
		info->gpu.ids = val;
	} else {
		val = (readl(info->base + 0x10) >> 10) & 0x3F;
		info->gpu.ids = val * 30;
	}
	printk(KERN_DEBUG "gpu ids = %u\n", info->gpu.ids);

	return 0;
}

static int sdp1307_get_core_ids(struct sdp_asv_info *info)
{
	u32 rev_id = (readl(info->base + 0x8) >> 23) & 0x1;
	
	/* core ids */
	if (rev_id)
		info->core.ids = ((readl(info->base + 0x8) >> 24) & 0xFF) * 2;
	else 
		info->core.ids = ((readl(info->base + 0x8) >> 16) & 0xFF) * 2;

	printk(KERN_DEBUG "core ids = %u\n", info->core.ids);

	return 0;
}

static unsigned int sdp1307_get_total_ids(struct sdp_asv_info *info)
{
	unsigned int ids;
	
	/* total ids (cpu + gpu + core) */
	ids = ((readl(info->base + 0x8) >> 16) & 0x3F) * 30;

	return ids;
}

static int sdp1307_store_result(struct sdp_asv_info *info)
{
	int i;
	u32 rev_id = (readl(info->base + 0x8) >> 23) & 0x1;

	/* get core ids */
	sdp1307_get_core_ids(info);

	/* find CPU group */
	for (i = 0; i < MAX_CPU_ASV_GROUP; i++) {
		if (info->cpu[0].ids <= info->cpu_table[0][i].ids_limit) {
			/* es1 */
			if (!rev_id) {
				info->cpu[0].result = 0;
				printk(KERN_INFO "AVS: es1 is not grouped\n");
				break;
			}

			/* es2 */
			info->cpu[0].result = i;
			printk(KERN_INFO "AVS: CPU group %d selected.\n", i);
			break;
		}
	}
	if (info->cpu[0].result < DEFAULT_ASV_GROUP ||
		info->cpu[0].result >= MAX_CPU_ASV_GROUP) {
		info->cpu[0].result = DEFAULT_ASV_GROUP;
	}

	/* find CORE group */
	for (i = 0; i < MAX_CORE_ASV_GROUP; i++) {
		if (info->core.ids <= info->core_table[i].ids) {
			info->core.result = i;
			printk(KERN_INFO "AVS: CORE gourp %d selected\n", info->core.result);
			break;
		}
	}

	printk(KERN_INFO "AVS: cpu - ids: %umA\n", info->cpu[0].ids);
	printk(KERN_INFO "AVS: gpu - ids: %umA\n", info->gpu.ids);
	printk(KERN_INFO "AVS: core - ids: %umA\n", info->core.ids);

	if (rev_id)
		printk(KERN_INFO "AVS: total - ids: %umA\n", sdp1307_get_total_ids(info));
	
	return 0;
}

#define SDP1307_CHIPID_BASE	0x18080000
static int sdp1307_asv_init(struct sdp_asv_info *info)
{
	int timeout = 200;
	unsigned int rev_id;
	
	info->cpu_table[0] = sdp1307_ids_table;
	info->core_table = sdp1307_core_table;
	
	info->get_cpu_ids_tmcb = sdp1307_get_cpu_ids;
	info->get_gpu_ids_tmcb = sdp1307_get_gpu_ids;
	info->get_core_ids_tmcb = sdp1307_get_core_ids;
	
	info->store_result = sdp1307_store_result;

	/* enable chip id register */
	info->base = ioremap(SDP1307_CHIPID_BASE, 0x10);
	if (info->base == NULL) {
		printk(KERN_ERR "ASV Error: can't map chip id address\n");
		return -1;
	}

	writel(0xFF01, info->base + 0x4);
	while (timeout) {
		if (readl(info->base) == 0)
			break;
		msleep(1);
	}
	if (timeout == 0) {
		printk(KERN_ERR "AVS: chip id enable failed!\n");
		return -EIO;
	}

	/* read revision id */
	rev_id = (readl(info->base + 0x8) >> 23) & 0x1;
	if (rev_id)
		printk(KERN_INFO "AVS: golf-v es2, rev num = %d\n", rev_id);
		
	return 0;
}
