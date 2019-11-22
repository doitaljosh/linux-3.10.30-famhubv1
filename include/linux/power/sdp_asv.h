/* include/linux/power/sdp_asv.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * SDP - Adoptive Support Voltage Header file
 *
 */

#ifndef __POWER_SDP_ASV_H
#define __POWER_SDP_ASV_H

//#define LOOP_CNT			10
#define MAX_CPU_ASV_GROUP	10	/* must be same as CPUFREQ_ASV_COUNT in cpufreq code */
#define MAX_MP_ASV_GROUP	10
#define MAX_CORE_ASV_GROUP	10
#define MAX_US_ASV_GROUP	10

#define DEFAULT_ASV_GROUP	1

/* AVS events */
#define SDP_ASV_NOTIFY_AVS_ON	0x0001 /* avs on */
#define SDP_ASV_NOTIFY_AVS_OFF	0x0002 /* avs off */

struct asv_judge_table {
	unsigned int ids_limit; /* IDS value to decide group of target */
	unsigned int tmcb_limit; /* TMCB value to decide group of target */
};

struct asv_volt_table {
	unsigned int ids; /* ids value */
	unsigned int tmcb; /* tmcb value */
	int volt; /* micro volt */
};

struct asv_dual_volt_table {
	unsigned int ids_tmcb; /* ids or tmcb */
	int volt1;		/* original voltage */
	int volt2;		/* alternative voltage */
};

struct asv_block_info {
	unsigned int ids;
	unsigned int tmcb;
	struct regulator *regulator;
	int result;
};

struct sdp_asv_info {
	void __iomem * base;
	bool is_avs_on;

	struct asv_block_info cpu[2];
	struct asv_block_info gpu;
	struct asv_block_info core;
	struct asv_block_info mp;
	struct asv_block_info us;

	struct asv_judge_table *cpu_table[2];	/* cpu ids table */
	struct asv_volt_table *core_table;
	struct asv_volt_table *mp_table;	/* mp ids, tmcb voltage table */
	struct asv_dual_volt_table *us_table;
	
	int (*get_cpu_ids_tmcb)(struct sdp_asv_info *asv_info);
	int (*get_gpu_ids_tmcb)(struct sdp_asv_info *asv_info);
	int (*get_core_ids_tmcb)(struct sdp_asv_info *asv_info);
	int (*get_mp_ids_tmcb)(struct sdp_asv_info *asv_info);
	int (*get_us_ids_tmcb)(struct sdp_asv_info *asv_info);
	int (*store_result)(struct sdp_asv_info *asv_info);
	int (*suspend)(struct sdp_asv_info *asv_info);
	int (*resume)(struct sdp_asv_info *asv_info);
	int (*avs_on)(struct sdp_asv_info *asv_info, bool on);
};

extern struct sdp_asv_info *asv_info;

struct sdp_asv_info * get_sdp_asv_info(void);

int register_sdp_asv_notifier(struct notifier_block *nb);
int unregister_sdp_asv_notifier(struct notifier_block *nb);

#endif /* __POWER_SDP_ASV_H */
