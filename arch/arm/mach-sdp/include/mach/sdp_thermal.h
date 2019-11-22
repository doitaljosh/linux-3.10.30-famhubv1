/* sdp_thermal.h
 *
 * Copyright 2013-2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for sdp tmu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SDP_THERMAL_H
#define __SDP_THERMAL_H


#define TMU_COLD	0
#define TMU_1ST		1
#define TMU_2ND		2
#define TMU_3RD		3

#define TMU_STATE_CNT	4

/* times */
#define SAMPLING_RATE		(2 * 1000 * 1000) /* u sec */
#define PANIC_TIME_SEC		(10 * 60 * 1000 * 1000 / SAMPLING_RATE) /* 10 minutes */
#define PRINT_RATE		(6 * 1000 * 1000) /* 6 sec */

#define TSC_DEGREE_25		46	/* 25'C is 46 */

/* flags that throttling or trippint is treated */
#define THROTTLE_COLD_FLAG	(0x1 << TMU_COLD)
#define THROTTLE_1ST_FLAG	(0x1 << TMU_1ST)
#define THROTTLE_2ND_FLAG	(0x1 << TMU_2ND)
#define THROTTLE_3RD_FLAG	(0x1 << TMU_3RD)
#define THROTTLE_HOTPLUG_FLAG	(0x1 << 4)

/* for notification */
#define SDP_TMU_FREQ_LIMIT		0x0001
#define SDP_TMU_FREQ_LIMIT_FREE		0x0002
#define SDP_TMU_AVS_ON			0x0003
#define SDP_TMU_AVS_OFF			0x0004

enum tmu_state_t {
	TMU_STATUS_INIT,
	TMU_STATUS_COLD,
	TMU_STATUS_NORMAL,
	TMU_STATUS_1ST,
	TMU_STATUS_2ND,
	TMU_STATUS_3RD,
};

struct throttle_params {
	char *name;
	u32 start_temp;
	u32 stop_temp;
	u32 cpu_limit_freq[2];
	u32 gpu_limit_freq;
};

struct sdp_tmu_info {
	struct device   *dev;
	void __iomem    *tmu_base;
	struct resource *ioarea;

	int tmu_state;
	int tmu_prev_state;

	u32 sensor_id;

	int print_on;
	int user_print_on;
	u32 handled_flag;

	struct throttle_params throttle[TMU_STATE_CNT];
	u32 cpu_hotplug_temp;

	struct delayed_work polling;

	u32 sampling_rate;
		
	int (*enable_tmu)(struct sdp_tmu_info *info);
	u32 (*get_temp)(struct sdp_tmu_info *info);
	int (*cpu_hotplug_down)(struct sdp_tmu_info *info);
	int (*cpu_hotplug_up)(struct sdp_tmu_info *info);
};

int register_sdp_tmu_notifier(struct notifier_block *nb);
int unregister_sdp_tmu_notifier(struct notifier_block *nb);

extern int sdp1304_tmu_init(struct sdp_tmu_info *info);
extern int sdp1404_tmu_init(struct sdp_tmu_info *info);
extern int sdp1406_tmu_init(struct sdp_tmu_info *info);

#endif /* __SDP_THERMAL_H */

