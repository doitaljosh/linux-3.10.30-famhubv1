/* drivers/cpufreq/sdp-cpufreq.h
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SDP - CPUFreq support
 *
 */

/* CPU frequency level index for using cpufreq lock API
 * This should be same with cpufreq_frequency_table
*/

#include <linux/regulator/consumer.h>

#ifndef __SDP_CPUFREQ_H
#define __SDP_CPUFREQ_H

enum cpufreq_level_index {
	L0, L1,	L2, L3, L4,
	L5, L6,	L7, L8,	L9,
	L10, L11, L12, L13, L14,
	L15, L16, L17, L18, L19,
	L20, L21, L22, L23, L24,
	LMAX
};

/* ID_END must be less than 32 */
enum cpufreq_lock_ID {
	DVFS_LOCK_ID_PM,	/* PM */
	DVFS_LOCK_ID_TMU,	/* TMU */
	DVFS_LOCK_ID_USER,	/* USER */
	DVFS_LOCK_ID_GPU,	/* GPU */
	DVFS_LOCK_ID_MFC,	/* MFC */
	DVFS_LOCK_ID_USB2,	/* USB2 host */
	DVFS_LOCK_ID_USB3,	/* USB3 host */
	DVFS_LOCK_ID_USBOTG,	/* USB otg */
	DVFS_LOCK_ID_CAM,	/* CAMERA */
	DVFS_LOCK_ID_G2D,	/* 2D GFX */
	DVFS_LOCK_ID_MIC,	/* MIC */
	DVFS_LOCK_ID_ROTATION,	/* UI ROTATION */
	
	DVFS_LOCK_ID_END,
};

enum cluster_type {
	CL_BIG,
	CL_LITTLE,
	CL_END,	
};

struct sdp_dvfs_info {
	unsigned int	max_real_idx;
	unsigned int	max_support_idx;
	unsigned int	min_support_idx;
	unsigned int	min_real_idx;

	bool		mcs_support;	/* multi core scaling support */

	int		cur_group;

	struct clk	*cpu_clk;

	unsigned int	*volt_table;
	struct cpufreq_frequency_table	*freq_table;
	
	void (*set_freq)(unsigned int cpu, unsigned int old_index,
			unsigned int new_index, unsigned int mux);
	void (*update_volt_table)(int result);
	unsigned int (*get_speed)(unsigned int);
};

#if defined(CONFIG_ARM_SDP_MP_CPUFREQ)
int sdp_cpufreq_lock(enum cluster_type clust,
			unsigned int nId,
			enum cpufreq_level_index cpufreq_level);
void sdp_cpufreq_lock_free(enum cluster_type clust, unsigned int nId);
int sdp_cpufreq_get_level(enum cluster_type cluster,
			unsigned int freq, unsigned int *level);
#else
int sdp_cpufreq_lock(unsigned int nId, enum cpufreq_level_index cpufreq_level);
void sdp_cpufreq_lock_free(unsigned int nId);
int sdp_cpufreq_get_level(unsigned int freq, unsigned int *level);
#endif

#endif
