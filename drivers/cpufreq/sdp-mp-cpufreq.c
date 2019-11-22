/*
 * sdp-mp-cpufreq.c - SDP MP SoCs cpufreq
 * 
 * Copyright (C) 2014 Samsung Electronics
 * Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/platform_data/sdp-cpufreq.h>

#ifdef CONFIG_SDP_AVS
#include <linux/power/sdp_asv.h>
#endif

#ifdef CONFIG_SDP_THERMAL
#include <mach/sdp_thermal.h>
#endif

#include <mach/soc.h>

static struct sdp_dvfs_info *sdp_info[CL_END];

static struct regulator *cpu_regulator[CL_END];
static struct cpufreq_freqs freqs[CL_END];

static DEFINE_MUTEX(cpufreq_lock);
static DEFINE_MUTEX(cpufreq_on_lock);

static struct cpumask cluster_cpus[CL_END];

static bool sdp_cpufreq_on = false;
static bool sdp_cpufreq_fixed[CL_END] = {false, false};
static bool sdp_cpufreq_init_done = false;
bool sdp_cpufreq_print_on = false;

static unsigned int apply_limit_level[CL_END];
static unsigned int sdp_cpufreq_limit_id[CL_END];
static unsigned int sdp_cpufreq_limit_level[CL_END][DVFS_LOCK_ID_END];

static unsigned int apply_lock_level[CL_END];
static unsigned int sdp_cpufreq_lock_id[CL_END];
static unsigned int sdp_cpufreq_lock_level[CL_END][DVFS_LOCK_ID_END];

static void init_cpumask_cluster_set(void)
{
	int i;

	for_each_cpu(i, cpu_possible_mask) {
		if (i >= 4)
			cpumask_set_cpu((u32)i, &cluster_cpus[CL_LITTLE]);
		else
			cpumask_set_cpu((u32)i, &cluster_cpus[CL_BIG]);
	}
}

static inline unsigned int cluster_to_cpu(enum cluster_type cluster)
{
	unsigned int cpu;
	unsigned int i;
	unsigned int cl_start, cl_end;
	
	if (cluster == CL_LITTLE) {
		cl_start = 4;
		cl_end = 8;
	} else {
		cl_start = 0;
		cl_end = 3;
	}

	for (i = cl_start; i <= cl_end; i++)
		if (!!cpu_online(i)) {
			cpu = i;
			break;
		}
	if (i > cl_end)
		cpu = cl_start;

	//printk("%s:cpu%u selected\n", __func__, cpu);

	return cpu;
}

static inline enum cluster_type cpu_to_cluster(unsigned int cpu)
{
	/* TODO : find to get current cluster type */
	if (cpu < 4)
		return CL_BIG;
	else
		return CL_LITTLE;
}

static int sdp_verify_speed(struct cpufreq_policy *policy)
{
	enum cluster_type clust = cpu_to_cluster(policy->cpu);
	
	return cpufreq_frequency_table_verify(policy,
					sdp_info[clust]->freq_table);
}

static unsigned int sdp_getspeed(unsigned int cpu)
{
	enum cluster_type clust = cpu_to_cluster(cpu);
	
	if (sdp_info[clust]->get_speed)
		return sdp_info[clust]->get_speed(cpu) / 1000;
	else
		return 0;
}

static int sdp_cpufreq_get_index(enum cluster_type clust,
				unsigned int freq)
{
	struct cpufreq_frequency_table *freq_table;
	int index;

	freq_table = sdp_info[clust]->freq_table;

	for (index = 0;
		freq_table[index].frequency != (u32)CPUFREQ_TABLE_END; index++)
		if (freq_table[index].frequency == freq)
			break;

	if (freq_table[index].frequency == (u32)CPUFREQ_TABLE_END)
		return -EINVAL;

	return index;	
}

static int sdp_cpufreq_set_voltage(enum cluster_type clust, int uV)
{
	return regulator_set_voltage(cpu_regulator[clust], uV, uV + 10000);
}

static int sdp_cpufreq_scale(struct cpufreq_policy *policy,
				unsigned int target_freq)
{
	unsigned int *volt_table;
	enum cluster_type clust;
	unsigned int arm_volt;
	int new_index, old_index;
	int ret = 0;

	if (!policy)
		return -EINVAL;

	clust = cpu_to_cluster(policy->cpu);
	volt_table = sdp_info[clust]->volt_table;

	freqs[clust].old = policy->cur;
	freqs[clust].new = target_freq;

	if (freqs[clust].new == freqs[clust].old)
		goto out;

	/*
	 * The policy max have been changed so that we cannot get proper
	 * old_index with cpufreq_frequency_table_target(). Thus, ignore
	 * policy and get the index from the raw freqeuncy table.
	 */
	old_index = sdp_cpufreq_get_index(clust, freqs[clust].old);
	if (old_index < 0) {
		ret = old_index;
		goto out;
	}

	new_index = sdp_cpufreq_get_index(clust, freqs[clust].new);
	if (new_index < 0) {
		ret = new_index;
		goto out;
	}

	/*
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	arm_volt = volt_table[new_index];

	cpufreq_notify_transition(policy, &freqs[clust], CPUFREQ_PRECHANGE);

	/* When the new frequency is higher than current frequency */
	if (freqs[clust].new > freqs[clust].old && cpu_regulator[clust]) {
		/* Firstly, voltage up to increase frequency */
		ret = sdp_cpufreq_set_voltage(clust, (int)arm_volt);
		if (ret) {
			pr_err("%s: failed to set cl%d voltage to %d\n",
				__func__, clust, arm_volt);
			goto out;
		}
	}

	sdp_info[clust]->set_freq(policy->cpu, (u32)old_index, (u32)new_index, 0);

	cpufreq_notify_transition(policy, &freqs[clust], CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if (freqs[clust].new < freqs[clust].old && cpu_regulator[clust]) {
		/* down the voltage after frequency change */
		ret = sdp_cpufreq_set_voltage(clust, (int)arm_volt);
		if (ret) {
			pr_err("%s: cl%d failed to set cpu voltage to %d\n",
				__func__, clust, arm_volt);
			goto out;
		}
	}

out:

	return ret;
}

static int sdp_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	struct cpufreq_frequency_table *freq_table;
	enum cluster_type clust;
	unsigned int index;
	unsigned int new_freq;
	int ret = 0;

	if (!policy)
		return -EINVAL;

	clust = cpu_to_cluster(policy->cpu);

	freq_table = sdp_info[clust]->freq_table;
	
	mutex_lock(&cpufreq_lock);

	/* check on/off */
	if (!sdp_cpufreq_on)
		goto out;

	/* check frequency fix */
	if (sdp_cpufreq_fixed[clust])
		goto out;

	/* 
	 * check max support
	 *if (target_freq > freq_table[sdp_info[clust]->max_support_idx].frequency)
	 *	target_freq = freq_table[sdp_info[clust]->max_support_idx].frequency;
	 */

	/*
	 * check min support
	 *if (target_freq < freq_table[sdp_info[clust]->min_support_idx].frequency)
	 *	target_freq = freq_table[sdp_info[clust]->min_support_idx].frequency;
	 */

	if (cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index)) {
		ret = -EINVAL;
		goto out;
	}

	/* check frequency lock */
	if (index > apply_lock_level[clust])
		index = apply_lock_level[clust];

	/* check frequency limit */
	if (index < apply_limit_level[clust])
		index = apply_limit_level[clust];

	new_freq = freq_table[index].frequency;

	ret = sdp_cpufreq_scale(policy, new_freq);

out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

static int sdp_cpufreq_freq_fix(enum cluster_type clust,
				unsigned int freq, bool on)
{
	struct cpufreq_policy *policy;
	int ret = 0;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	mutex_lock(&cpufreq_lock);
	
	/* frequency fix */
	if (on) {
		if (sdp_cpufreq_fixed[clust]) {
			pr_info("cl%d CPUFreq is already fixed\n", clust);
			ret = -EPERM;
			goto out;
		}

		/* convert to 10MHz scale */
		freq = (freq / 10000) * 10000;

		policy = cpufreq_cpu_get(cluster_to_cpu(clust));
		WARN_ON(!policy);
		
		ret = sdp_cpufreq_scale(policy, freq);
		if (policy)
			cpufreq_cpu_put(policy);
		
		if (ret < 0) {
			pr_err("cl%d cpufreq scaling is failed\n", clust);
			goto out;
		}

		sdp_cpufreq_fixed[clust] = true;
	/* frequency unfix */
	} else {
		sdp_cpufreq_fixed[clust] = false;
	}

out:
	mutex_unlock(&cpufreq_lock);
	return ret;
}

/* This function limits frequnecy uppper level */
int sdp_cpufreq_limit(enum cluster_type clust, unsigned int nId,
			enum cpufreq_level_index cpufreq_level)
{
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_new;
	struct cpufreq_policy *policy;
	int ret = 0, i;
	
	if (!sdp_info[clust] || !sdp_cpufreq_init_done)
		return -EPERM;

	if (sdp_cpufreq_fixed[clust]) {
		pr_info("can't set limitation. "
			"CPUFreq cl%d is already fixed\n", clust);
		return -EPERM;
	}

	if (cpufreq_level < sdp_info[clust]->max_support_idx ||
		cpufreq_level > sdp_info[clust]->min_real_idx) {
		pr_err("%s: cl%d invalid cpufreq_level(%d:%d)\n",
			__func__, clust, nId, cpufreq_level);
		return -EINVAL;
	}

	mutex_lock(&cpufreq_lock);

	freq_table = sdp_info[clust]->freq_table;
	
	sdp_cpufreq_limit_id[clust] |= (1U << nId);
	sdp_cpufreq_limit_level[clust][nId] = cpufreq_level;

	/* find lowest level and store for the next sdp_target */
	apply_limit_level[clust] = sdp_info[clust]->max_support_idx;
	for (i = 0; i < DVFS_LOCK_ID_END; i++)
		if (sdp_cpufreq_limit_level[clust][i] > apply_limit_level[clust])
			apply_limit_level[clust] = sdp_cpufreq_limit_level[clust][i];

	/* update cpu frequency */
	freq_new = freq_table[apply_limit_level[clust]].frequency;

	policy = cpufreq_cpu_get(cluster_to_cpu(clust));
	WARN_ON(!policy);
	
	ret = sdp_cpufreq_scale(policy, freq_new);

	if (policy)
		cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);

	return ret;
}
EXPORT_SYMBOL(sdp_cpufreq_limit);

/* This function frees upper limit */
void sdp_cpufreq_limit_free(enum cluster_type clust, unsigned int nId)
{
	unsigned int i;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_policy *policy;

	if (clust < 0 || clust >= CL_END) {
		pr_err("%s: cluster number is not available(%d)\n",
			__func__, clust);
		return;
	}

	if (!sdp_cpufreq_init_done)
		return;

	freq_table = sdp_info[clust]->freq_table;

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_limit_id[clust] &= ~(1U << nId);
	sdp_cpufreq_limit_level[clust][nId] = sdp_info[clust]->max_support_idx;
	apply_limit_level[clust] = sdp_info[clust]->max_support_idx;

	/* find lowest frequency */
	if (sdp_cpufreq_limit_id[clust]) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (sdp_cpufreq_limit_level[clust][i] > apply_limit_level[clust])
				apply_limit_level[clust] = sdp_cpufreq_limit_level[clust][i];
		}
	}

	policy = cpufreq_cpu_get(cluster_to_cpu(clust));
	WARN_ON(!policy);
	
	sdp_cpufreq_scale(policy, freq_table[apply_limit_level[clust]].frequency);

	if (policy)
		cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);
}
EXPORT_SYMBOL(sdp_cpufreq_limit_free);

/* This function locks frequency lower level */
int sdp_cpufreq_lock(enum cluster_type clust, 
			unsigned int nId,
			enum cpufreq_level_index cpufreq_level)
{
	int ret = 0, i;
	unsigned int freq_new;
	struct cpufreq_policy *policy;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	if (clust < 0 || clust >= CL_END)
		return -EINVAL;

	if (!sdp_info[clust])
		return -EPERM;

	if (sdp_cpufreq_fixed[clust] && (nId != DVFS_LOCK_ID_TMU)) {
		pr_info("can't set freq lock. "
			"cl%d CPUFreq is already fixed\n", clust);
		return -EPERM;
	}

	if (cpufreq_level < sdp_info[clust]->max_real_idx ||
		cpufreq_level > sdp_info[clust]->min_support_idx) {
		pr_warn("%s: cl%d invalid cpufreq_level(%d:%d)\n",
			__func__, clust, nId, cpufreq_level);
		return -EINVAL;
	}

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_lock_id[clust] |= (1U << nId);
	sdp_cpufreq_lock_level[clust][nId] = cpufreq_level;

	/* find highest level and store for the next sdp_target */
	apply_lock_level[clust] = sdp_info[clust]->min_support_idx;
	for (i = 0; i < DVFS_LOCK_ID_END; i++)
		if (sdp_cpufreq_lock_level[clust][i] < apply_lock_level[clust])
			apply_lock_level[clust] = sdp_cpufreq_lock_level[clust][i];

	/* update cpu frequency */
	freq_new = sdp_info[clust]->freq_table[apply_lock_level[clust]].frequency;

	policy = cpufreq_cpu_get(cluster_to_cpu(clust));
	WARN_ON(!policy);
	
	ret = sdp_cpufreq_scale(policy, freq_new);

	if (policy)
		cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);

	return ret;
}
EXPORT_SYMBOL(sdp_cpufreq_lock);

/* This function frees locked frequency lower level */
void sdp_cpufreq_lock_free(enum cluster_type clust, unsigned int nId)
{
	unsigned int i;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_policy *policy;

	if (clust < 0 || clust >= CL_END) {
		pr_err("%s: cluster%d is not available\n", __func__, clust);
		return;
	}

	if (!sdp_cpufreq_init_done)
		return;

	freq_table = sdp_info[clust]->freq_table;

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_lock_id[clust] &= ~(1U << nId);
	sdp_cpufreq_lock_level[clust][nId] = sdp_info[clust]->min_support_idx;
	apply_lock_level[clust] = sdp_info[clust]->min_support_idx;

	/* find highest level */
	if (sdp_cpufreq_lock_id[clust]) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (sdp_cpufreq_lock_level[clust][i] < apply_lock_level[clust])
				apply_lock_level[clust] = sdp_cpufreq_lock_level[clust][i];
		}
	}

	policy = cpufreq_cpu_get(cluster_to_cpu(clust));
	WARN_ON(!policy);
	
	sdp_cpufreq_scale(policy, freq_table[apply_lock_level[clust]].frequency);

	if (policy)
		cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);
}
EXPORT_SYMBOL(sdp_cpufreq_lock_free);

int sdp_cpufreq_get_level(enum cluster_type clust, 
			unsigned int freq, unsigned int *level)
{
	struct cpufreq_frequency_table *table;
	unsigned int i;
	unsigned int cpu;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	if (clust < 0 || clust >= CL_END)
		return -EINVAL;

	cpu = cluster_to_cpu(clust);

	table = cpufreq_frequency_get_table(cpu);
	if (!table) {
		pr_err("%s: Failed to get cluster%d the cpufreq table\n",
			__func__, clust);
		return -EINVAL;
	}

	for (i = sdp_info[clust]->max_real_idx;
		(table[i].frequency != (u32)CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == freq) {
			*level = i;
			return 0;
		}
	}

	pr_err("%s: cluster%d %uKHz is an unsupported cpufreq\n",
		__func__, clust, freq);

	return -EINVAL;
}
EXPORT_SYMBOL(sdp_cpufreq_get_level);

/*******************/
/* sysfs interface */
/*******************/
/* cpufreq on/off */
static ssize_t sdp_cpufreq_on_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", sdp_cpufreq_on);

	return ret;
}

static ssize_t sdp_cpufreq_on_store(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int on;
	struct cpufreq_policy *policy;
	int ret;
	int i, timeout = 10;
	int j;	
	
	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	printk(KERN_DEBUG "cpufreq on = %u\n", on);

	mutex_lock(&cpufreq_on_lock);

	if (on == 1) { /* ON */
		if (sdp_cpufreq_on) {
			printk(KERN_INFO "cpufreq already ON\n");
			goto out;
		}

		sdp_cpufreq_on = true;
	} else if (on == 0) { /* OFF */
		if (!sdp_cpufreq_on) {
			printk(KERN_INFO "cpufreq already OFF\n");
			goto out;
		}

		/* set each cluster to max frequency */
		for (i = 0; i < CL_END; i++) {
			policy = cpufreq_cpu_get(cluster_to_cpu(i));
			if (!policy)
				continue;
			
			/* set frequency to max */
			for (j = 0; j < timeout; i++) {
				if (!sdp_target(policy, policy->max, CPUFREQ_RELATION_H))
					break;

				printk(KERN_WARNING "retry frequnecy setting.\n");
				msleep(10);
			}
			if (j == timeout)
				printk(KERN_WARNING "frequnecy set time out!!\n");

			if (policy)
				cpufreq_cpu_put(policy);
		}
		
		sdp_cpufreq_on = false;
	} else {
		printk(KERN_ERR "%s: ERROR - input 0 or 1\n", __func__);
	}

out:
	mutex_unlock(&cpufreq_on_lock);
	return (ssize_t)count;
}
static struct global_attr cpufreq_on = __ATTR(cpufreq_on, 0644, sdp_cpufreq_on_show, sdp_cpufreq_on_store);

/* cpufreq fix */
static ssize_t cpufreq_freqfix_show(enum cluster_type clust,
				struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	ssize_t ret;
	struct cpufreq_policy *policy;
	int cur;

	if (sdp_cpufreq_fixed[clust]) {
		mutex_lock(&cpufreq_lock);
		
		policy = cpufreq_cpu_get(cluster_to_cpu(clust));
		if (policy)
			cur = (int)policy->cur;
		else 
			cur = 0;
		
		ret = snprintf(buf, PAGE_SIZE, "%d\n", cur);

		if (policy)
			cpufreq_cpu_put(policy);

		mutex_unlock(&cpufreq_lock);
	} else {
		ret = snprintf(buf, PAGE_SIZE, "0\n");
	}

	return ret;
}

static ssize_t cpufreq_freqfix_store(enum cluster_type clust,
				struct kobject *a,
				struct attribute *b,
			 	const char *buf,
			 	size_t count)
{
	unsigned int freq;
	int ret;

	if (!sdp_cpufreq_on) {
		printk(KERN_ERR "%s : cpufreq_on must be turned on.\n", __func__);
		return -EPERM;
	}

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	/* must unfix before frequency fix */
	sdp_cpufreq_freq_fix(clust, freq, false);

	if (freq > 0) {
		printk(KERN_DEBUG "cl%d freq=%u, cpufreq_fix\n", clust, freq);
		sdp_cpufreq_freq_fix(clust, freq, true);
	} else {
		printk(KERN_DEBUG "cl%d freq=%u, cpufreq unfix\n", clust, freq);
	}
	
	return (ssize_t)count;
}

static ssize_t sdp_cpufreq_big_freqfix_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqfix_show(CL_BIG, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_big_freqfix_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
{
	return cpufreq_freqfix_store(CL_BIG, a, b, buf, count);
}

static struct global_attr freq_big = __ATTR(freq_big, 0644,
					sdp_cpufreq_big_freqfix_show,
					sdp_cpufreq_big_freqfix_store);

static ssize_t sdp_cpufreq_lt_freqfix_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqfix_show(CL_LITTLE, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_lt_freqfix_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
{
	return cpufreq_freqfix_store(CL_LITTLE, a, b, buf, count);
}

static struct global_attr freq_lt = __ATTR(freq_lt, 0644,
					sdp_cpufreq_lt_freqfix_show,
					sdp_cpufreq_lt_freqfix_store);

static struct attribute *dbs_attributes[] = {
	&freq_big.attr,
	&freq_lt.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "freqfix",
};

/* frequency limitation */
static ssize_t cpufreq_freqlimit_show(enum cluster_type clust,
					struct kobject *kobj,
					struct attribute *attr,
					char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (sdp_cpufreq_limit_id[clust] & (1 << DVFS_LOCK_ID_USER)) {
		freq = sdp_info[clust]->freq_table[sdp_cpufreq_limit_level[clust][DVFS_LOCK_ID_USER]].frequency;
		ret = snprintf(buf, PAGE_SIZE, "%d\n", freq);
	} else {
		ret = snprintf(buf, PAGE_SIZE, "0\n");
	}

	return ret;
}

static ssize_t cpufreq_freqlimit_store(enum cluster_type clust,
					struct kobject *a,
					struct attribute *b,
					const char *buf,
					size_t count)
{
	unsigned int freq;
	unsigned int level;
	int ret;

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (freq != 0) {
		ret = sdp_cpufreq_get_level(clust, freq, &level);
		if (ret < 0)
			goto out;
	}

#if 0
	if (sdp_cpufreq_limit_id[clust] & (1 << DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlimit\n", freq);
		sdp_cpufreq_limit_free(clust, DVFS_LOCK_ID_USER);
	}
#endif

	if (freq > 0) {
		printk(KERN_DEBUG "cl%d freq=%u, freq limit\n", clust, freq);
		sdp_cpufreq_limit(clust, DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "cl%d freq=%u, freq unlimit\n", clust, freq);
		sdp_cpufreq_limit_free(clust, DVFS_LOCK_ID_USER);
	}
	
out:
	return (ssize_t)count;
}

static ssize_t sdp_cpufreq_big_freqlimit_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqlimit_show(CL_BIG, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_big_freqlimit_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return cpufreq_freqlimit_store(CL_BIG, a, b, buf, count);
}
static struct global_attr freq_limit_big = __ATTR(freq_limit_big, 0644, 
					sdp_cpufreq_big_freqlimit_show, 
					sdp_cpufreq_big_freqlimit_store);

static ssize_t sdp_cpufreq_lt_freqlimit_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqlimit_show(CL_LITTLE, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_lt_freqlimit_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return cpufreq_freqlimit_store(CL_LITTLE, a, b, buf, count);
}
static struct global_attr freq_limit_lt = __ATTR(freq_limit_lt, 0644, 
					sdp_cpufreq_lt_freqlimit_show, 
					sdp_cpufreq_lt_freqlimit_store);

/* frequency lock */
static ssize_t cpufreq_freqlock_show(enum cluster_type clust,
				struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (sdp_cpufreq_lock_id[clust] & (1 << DVFS_LOCK_ID_USER)) {
		freq = sdp_info[clust]->freq_table[sdp_cpufreq_lock_level[clust][DVFS_LOCK_ID_USER]].frequency;
		ret = snprintf(buf, PAGE_SIZE, "%d\n", freq);
	} else {
		ret = snprintf(buf, PAGE_SIZE, "0\n");
	}

	return ret;
}

static ssize_t cpufreq_freqlock_store(enum cluster_type clust,
					struct kobject *a,
					struct attribute *b,
			 		const char *buf,
			 		size_t count)
{
	unsigned int freq;
	int ret;
	unsigned int level;

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (freq != 0) {
		ret = sdp_cpufreq_get_level(clust, freq, &level);
		if (ret < 0)
			goto out;
	}

#if 0
	if (sdp_cpufreq_lock_id[clust] & (1 << DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(clust, DVFS_LOCK_ID_USER);
	}
#endif

	if (freq > 0) {
		printk(KERN_DEBUG "cl%d freq=%u, freq lock\n", clust, freq);
		sdp_cpufreq_lock(clust, DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(clust, DVFS_LOCK_ID_USER);
	}

out:	
	return (ssize_t)count;
}

static ssize_t sdp_cpufreq_big_freqlock_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqlock_show(CL_BIG, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_big_freqlock_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
{
	return cpufreq_freqlock_store(CL_BIG, a, b, buf, count);
}

static struct global_attr freq_lock_big = __ATTR(freq_lock_big, 0644,
						sdp_cpufreq_big_freqlock_show,
						sdp_cpufreq_big_freqlock_store);

static ssize_t sdp_cpufreq_lt_freqlock_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_freqlock_show(CL_LITTLE, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_lt_freqlock_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
{
	return cpufreq_freqlock_store(CL_LITTLE, a, b, buf, count);
}

static struct global_attr freq_lock_lt = __ATTR(freq_lock_lt, 0644,
						sdp_cpufreq_lt_freqlock_show,
						sdp_cpufreq_lt_freqlock_store);

/* DVFS voltage table update from Filesystem */
static ssize_t cpufreq_voltupdate_show(enum cluster_type clust,
					struct kobject *kobj,
					struct attribute *attr,
					char *buf)
{
	ssize_t ret = 0;
	int i;
	unsigned int *volt_table = sdp_info[clust]->volt_table;

	for (i = (int)sdp_info[clust]->max_real_idx;
		i <= (int)sdp_info[clust]->min_real_idx; i++) {
		printk(KERN_INFO "%s grp%d, [%4dMHz] %7uuV\n", 
				(clust == CL_BIG) ? "BIG" : "LITTLE",
				sdp_info[clust]->cur_group, 
				sdp_info[clust]->freq_table[i].frequency / 1000,
				volt_table[i]);
	}

	return ret;
}

#ifdef MAX_CPU_ASV_GROUP
static unsigned int g_volt_table[LMAX][MAX_CPU_ASV_GROUP];
#else
static unsigned int g_volt_table[LMAX][10];
#endif
static ssize_t cpufreq_voltupdate_store(enum cluster_type clust, struct kobject *a,
					struct attribute *b, const char *buf,
					size_t count)
{
	int i, j;
	size_t size, read_cnt = 0;
	char atoi_buf[15];
	char *sbuf;
	char temp;
	int line_cnt = 0, char_cnt;
	bool started = 0, loop = 1;

	/* store to memory */
	memset(g_volt_table, 0, sizeof(g_volt_table));
	
	size = count;

	i = 0;
	j = 0;
	char_cnt = 0;
	while (size > read_cnt && loop) {
		/* get 1 byte */
		temp = buf[read_cnt++];
		
		/* find 'S' */
		if (started == 0 && temp == 'S') {
			/* find '\n' */
			while (size > read_cnt) {
				temp = buf[read_cnt++];
				if (temp == '\n') {
					started = 1;
					break;
				}
			}
			continue;
		}

		if (started == 0)
			continue;

		/* check volt table line count */
		if (i > (int)(sdp_info[clust]->min_real_idx - sdp_info[clust]->max_real_idx + 1)) {
			printk(KERN_ERR "cpufreq ERR: volt table line count is more than %d, i = %d\n",
					sdp_info[clust]->min_real_idx - sdp_info[clust]->max_real_idx + 1, i);
			goto out;
		}

		/* check volt table column count */
#ifdef MAX_CPU_ASV_GROUP
		if (j > MAX_CPU_ASV_GROUP) {
			printk(KERN_ERR "cpufreq ERR: volt table column count "
				"is more than %d, j = %d\n", MAX_CPU_ASV_GROUP, j);
			goto out;
		}
#else
		if (j > 10) {
			printk(KERN_ERR "cpufreq ERR: volt table column count "
				"is more than 10, j = %d\n", j);
			goto out;
		}
#endif		

		/* parsing */
		switch (temp) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			atoi_buf[char_cnt] = temp;
			char_cnt++;
			break;

		case ',':
			atoi_buf[char_cnt++] = 0;
#ifdef MAX_CPU_ASV_GROUP
			if (j >= MAX_CPU_ASV_GROUP)
				break;
#else
			if (j >= 10)
				break;
#endif
			sbuf = atoi_buf;
			g_volt_table[i][j] = (unsigned int)simple_strtoul(sbuf, (char **)&sbuf, 0);
			//printk("g_volt_table[%d][%d]=%u\n", i, j, g_volt_table[i][j]);
			j++;
			char_cnt = 0;
			break;

		case '\n':
			//printk("meet LF\n");
			i++;
			j = 0;
			break;
		
		case 'E':
			loop = 0;
			line_cnt = i;
			//printk("meet END, line_cnt = %d\n", line_cnt);
			break;

		default:
			break;
		}
	}

	/* check line count */
	if (line_cnt != (int)(sdp_info[clust]->min_real_idx - sdp_info[clust]->max_real_idx + 1)) {
		printk(KERN_ERR "cpufreq ERR: volt table line count is not %d\n",
			sdp_info[clust]->min_real_idx - sdp_info[clust]->max_real_idx + 1);
	
		goto out;
	}

	/* change current volt table */
	printk(KERN_INFO "> DVFS volt table change\n");
	mutex_lock(&cpufreq_lock);
	for (i = (int)sdp_info[clust]->max_real_idx, j = 0;
		i <= (int)sdp_info[clust]->min_real_idx;
		i++, j++) {
		printk(KERN_INFO "group%d, [%4dMHz] %7uuV -> %7uuV\n", 
			sdp_info[clust]->cur_group, sdp_info[clust]->freq_table[i].frequency/1000,
			sdp_info[clust]->volt_table[i], g_volt_table[j][sdp_info[clust]->cur_group]);
		sdp_info[clust]->volt_table[i] = g_volt_table[j][sdp_info[clust]->cur_group];
	}
	mutex_unlock(&cpufreq_lock);
	printk(KERN_INFO "> DONE\n");
	
out:
	return (ssize_t)count;
}

static ssize_t sdp_cpufreq_big_voltupdate_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_voltupdate_show(CL_BIG, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_big_voltupdate_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return cpufreq_voltupdate_store(CL_BIG, a, b, buf, count);
}

static struct global_attr volt_update_big = __ATTR(volt_update_big, 0644,
						sdp_cpufreq_big_voltupdate_show,
						sdp_cpufreq_big_voltupdate_store);

static ssize_t sdp_cpufreq_lt_voltupdate_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return cpufreq_voltupdate_show(CL_LITTLE, kobj, attr, buf);
}

static ssize_t sdp_cpufreq_lt_voltupdate_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return cpufreq_voltupdate_store(CL_LITTLE, a, b, buf, count);
}

static struct global_attr volt_update_lt = __ATTR(volt_update_lt, 0644,
						sdp_cpufreq_lt_voltupdate_show,
						sdp_cpufreq_lt_voltupdate_store);

/* adjustable voltage by user, percentage */
static unsigned int g_volt_table_org[CL_END][LMAX];
static int voltadjust_ratio = 100;
static const int voltadjust_ratio_min = 90;
static const int voltadjust_ratio_max = 110;

static ssize_t sdp_cpufreq_voltadjust_show(enum cluster_type clust, struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", voltadjust_ratio);
}
static ssize_t sdp_cpufreq_voltadjust_store(enum cluster_type clust, struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int new_ratio = -1;
	unsigned int i;
	struct sdp_dvfs_info *info = sdp_info[clust];

	sscanf(buf, "%d", &new_ratio);
	if (new_ratio < voltadjust_ratio_min || new_ratio > voltadjust_ratio_max)
		return -EINVAL;

	mutex_lock(&cpufreq_lock);
	
	pr_info ("sdp_cpufreq: cl%d (group=%d) voltage adjustment: %d.\n",
			clust, info->cur_group, new_ratio);
	voltadjust_ratio = new_ratio;

	for (i = info->max_real_idx; i <= info->min_real_idx; i++) {
		info->volt_table[i] = (g_volt_table_org[clust][i] * (unsigned int)new_ratio + 50) / 100;
	}
	mutex_unlock(&cpufreq_lock);

	for (i = info->max_real_idx; i <= info->min_real_idx; i++) {
		pr_info ("[%4dMhz] %7uuV --> %7uuV\n", info->freq_table[i].frequency / 1000,
				g_volt_table_org[clust][i], info->volt_table[i]);
	}
	return (ssize_t)count;
}
static ssize_t sdp_cpufreq_big_voltadjust_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return sdp_cpufreq_voltadjust_show(CL_BIG, kobj, attr, buf);
}
static ssize_t sdp_cpufreq_big_voltadjust_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return sdp_cpufreq_voltadjust_store(CL_BIG, a, b, buf, count);
}

static ssize_t sdp_cpufreq_lt_voltadjust_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return sdp_cpufreq_voltadjust_show(CL_LITTLE, kobj, attr, buf);
}
static ssize_t sdp_cpufreq_lt_voltadjust_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	return sdp_cpufreq_voltadjust_store(CL_LITTLE, a, b, buf, count);
}

static struct global_attr volt_adjust_big = __ATTR(volt_adjust_big, 0644,
						sdp_cpufreq_big_voltadjust_show,
						sdp_cpufreq_big_voltadjust_store);
static struct global_attr volt_adjust_lt = __ATTR(volt_adjust_lt, 0644,
						sdp_cpufreq_lt_voltadjust_show,
						sdp_cpufreq_lt_voltadjust_store);

/* debug print on/off */
static ssize_t sdp_cpufreq_print_on_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", sdp_cpufreq_print_on);

	return ret;
}

static ssize_t sdp_cpufreq_print_on_store(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int on;
	int ret;
	
	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	printk(KERN_DEBUG "cpufreq print on = %u\n", on);

	if (on == 1)		/* ON */
		sdp_cpufreq_print_on = true;
	else if (on == 0)	/* OFF */
		sdp_cpufreq_print_on = false;
	else
		printk(KERN_ERR "%s: ERROR - input 0 or 1\n", __func__);

	return (ssize_t)count;
}
static struct global_attr print_on = 
	__ATTR(print_on, 0644, sdp_cpufreq_print_on_show, sdp_cpufreq_print_on_store);

static int sdp_cpufreq_dev_register(void)
{
	int err;

	err = sysfs_create_group(cpufreq_global_kobject, &dbs_attr_group);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &cpufreq_on.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &freq_limit_big.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &freq_limit_lt.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &freq_lock_big.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &freq_lock_lt.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &volt_update_big.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &volt_update_lt.attr);
	if (err < 0)
		goto out;
	
	err = sysfs_create_file(cpufreq_global_kobject, &volt_adjust_big.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &volt_adjust_lt.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &print_on.attr);

out:
	return err;
}
/****************/
/* sysfs end    */
/****************/

#ifdef CONFIG_PM
static int sdp_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int sdp_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

static int sdp_cpufreq_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	static bool dvfs_on;
	
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&cpufreq_on_lock);
		
		dvfs_on = sdp_cpufreq_on;
		/* disable cpufreq */
		sdp_cpufreq_on = false;

		mutex_unlock(&cpufreq_on_lock);
		break;

	case PM_POST_SUSPEND:
		mutex_lock(&cpufreq_on_lock);

		/* restore on/off status */
		sdp_cpufreq_on = dvfs_on;

		mutex_unlock(&cpufreq_on_lock);
		break;
		
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block sdp_cpufreq_pm_nb = {
	.notifier_call = sdp_cpufreq_pm_notifier,
};

/* cpufreq_lock should be held */
static void sdp_cpufreq_update_volt_table(enum cluster_type clust, int asv_group)
{
	unsigned int i;

	pr_info("sdp_cpufreq: cl%d cpu voltage table arranged. group=%d\n",
			clust, asv_group);

	sdp_info[clust]->update_volt_table(asv_group);

	/* back-up original voltage table for restoring after user's adjustment */
	for (i = sdp_info[clust]->max_real_idx; i <= sdp_info[clust]->min_real_idx; i++) {
		g_volt_table_org[clust][i] = sdp_info[clust]->volt_table[i];
	}
}

#ifdef CONFIG_SDP_AVS
static int sdp_cpufreq_asv_notifier(struct notifier_block *notifier,
			  	     unsigned long event, void *v)
{
	struct sdp_asv_info *info = (struct sdp_asv_info *)v;
	unsigned int volt;
	unsigned int cur_freq_idx;
	struct cpufreq_policy *policy;
	int ret;
	int i;

	if (!info) {
		pr_err("sdp_cpufre: error - asv_info is NULL\n");
		return NOTIFY_DONE;
	}

	mutex_lock(&cpufreq_lock);

	switch (event) {
	case SDP_ASV_NOTIFY_AVS_ON:
		for (i = 0; i < CL_END; i++) {
			sdp_info[i]->cur_group = info->cpu[i].result;
			sdp_cpufreq_update_volt_table(i, asv_info->cpu[i].result);
		}
		break;
	case SDP_ASV_NOTIFY_AVS_OFF:
		for (i = 0; i < CL_END; i++) {
			sdp_info[i]->cur_group = 0;
			sdp_cpufreq_update_volt_table(i, 0);
		}
		break;
	default:
		break;
	}

	/* apply cpu AVS voltage */
	for (i = 0; i < CL_END; i++) {
		if (cpu_regulator[i] &&
			(event == SDP_ASV_NOTIFY_AVS_ON ||
			event == SDP_ASV_NOTIFY_AVS_OFF)) {
			policy = cpufreq_cpu_get(cluster_to_cpu(i));
			if (!policy) {
				pr_err("%s: policy is NULL\n", __func__);
				continue;
			}
			
			ret = sdp_cpufreq_get_level(i, policy->cur, &cur_freq_idx);
			cpufreq_cpu_put(policy);
			
			if (ret) {
				pr_err("CPUFREQ: error - cl%d failed to get level\n", i);
				
				goto out;
			}

			volt = sdp_info[i]->volt_table[cur_freq_idx];
			
			pr_info("set cl%d cpu voltage to %uuV\n", i, volt);
			
			ret = sdp_cpufreq_set_voltage(i, (int)volt);
			if (ret)
				pr_err("%s: failed to set cl%d cpu voltage to %d\n",
					__func__, i, volt);
		}
	}

out:
	mutex_unlock(&cpufreq_lock);
	
	return NOTIFY_OK;
}

static struct notifier_block sdp_cpufreq_asv_nb = {
	.notifier_call = sdp_cpufreq_asv_notifier,
};
#endif

#ifdef CONFIG_SDP_THERMAL
static int sdp_cpufreq_tmu_notifier(struct notifier_block *notifier,
			  	     unsigned long event, void *v)
{
	struct throttle_params *param = NULL;
	unsigned int level;
	int ret;
	int i;

	if (v) {
		param = (struct throttle_params *)v;
		for (i = 0; i < CL_END; i++)
			pr_info("%s: cl%d freq=%dMHz\n", __func__, i,
				param->cpu_limit_freq[i] / 1000);
	}

	switch (event) {
	case SDP_TMU_FREQ_LIMIT:
		if (!param)
			break;

		for (i = 0; i < CL_END;  i++) {
			ret = sdp_cpufreq_get_level(i, param->cpu_limit_freq[i], &level);
			if (ret < 0) {
				pr_info("DVFS: cl%d can't find level using %dMHz\n",
					i, param->cpu_limit_freq[i] / 1000);
				break;
			}
			pr_info("DVFS: cl%d freq limit %dMHz\n",
				i, param->cpu_limit_freq[i] / 1000);
			sdp_cpufreq_limit(i, DVFS_LOCK_ID_TMU, level);
		}
		break;
		
	case SDP_TMU_FREQ_LIMIT_FREE:
		pr_info("DVFS: freq limit free\n");
		for (i = 0; i < CL_END; i++)
			sdp_cpufreq_limit_free(i, DVFS_LOCK_ID_TMU);
		break;
		
	default:
		break;
	}
	
	return NOTIFY_OK;
}

static struct notifier_block sdp_cpufreq_tmu_nb = {
	.notifier_call = sdp_cpufreq_tmu_notifier,
};
#endif


static int sdp_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	enum cluster_type clust;;
	
	policy->cur = policy->min = policy->max = sdp_getspeed(policy->cpu);

	clust = cpu_to_cluster(policy->cpu);
	cpufreq_frequency_table_get_attr(sdp_info[clust]->freq_table, policy->cpu);

	/* set the transition latency value (nano second) */
	policy->cpuinfo.transition_latency = 100000;

	if (cpumask_test_cpu(policy->cpu, &cluster_cpus[CL_BIG])) {
		cpumask_copy(policy->cpus, &cluster_cpus[CL_BIG]);
		cpumask_copy(policy->related_cpus, &cluster_cpus[CL_BIG]);
	} else {
		cpumask_copy(policy->cpus, &cluster_cpus[CL_LITTLE]);
		cpumask_copy(policy->related_cpus, &cluster_cpus[CL_LITTLE]);
	}

	return cpufreq_frequency_table_cpuinfo(policy, sdp_info[clust]->freq_table);
}

static int sdp_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *sdp_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver sdp_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= sdp_verify_speed,
	.target		= sdp_target,
	.get		= sdp_getspeed,
	.init		= sdp_cpufreq_cpu_init,
	.exit		= sdp_cpufreq_cpu_exit,
	.name		= "sdp_cpufreq",
	.attr		= sdp_cpufreq_attr,
#ifdef CONFIG_PM
	.suspend	= sdp_cpufreq_suspend,
	.resume		= sdp_cpufreq_resume,
#endif
};

#if defined(CONFIG_ARM_SDP1404_CPUFREQ)
extern int sdp1404_big_cpufreq_init(struct sdp_dvfs_info *info);
extern int sdp1404_lt_cpufreq_init(struct sdp_dvfs_info *info);
#else
#warning "select SDP SoC type for cpufreq."
#endif

static int __init sdp_cpufreq_init(void)
{
	int ret = -EINVAL;
	int i;

	for (i = 0; i < CL_END; i++) {
		sdp_info[i] = kzalloc(sizeof(struct sdp_dvfs_info), GFP_KERNEL);
		if (!sdp_info[i])
			return -ENOMEM;
	}
	
#if defined(CONFIG_ARM_SDP1404_CPUFREQ)
	ret = sdp1404_big_cpufreq_init(sdp_info[CL_BIG]);
	if (ret) {
		pr_err("cpufreq big init fail\n");
		return ret;
	}

	ret = sdp1404_lt_cpufreq_init(sdp_info[CL_LITTLE]);
	if (ret) {
		pr_err("cpufreq little init fail\n");
		return ret;
	}
#endif
	/* set initial voltage table to default group 0 */
	sdp_cpufreq_update_volt_table(CL_BIG, 0);
	sdp_cpufreq_update_volt_table(CL_LITTLE, 0);

	init_cpumask_cluster_set();

	if (sdp_info[CL_BIG]->set_freq == NULL ||
		sdp_info[CL_LITTLE]->set_freq == NULL) {
		pr_err("%s: No set_freq function (ERR)\n", __func__);
		goto err_vdd_arm;
	}

	sdp_cpufreq_init_done = true;

	/* freq limit and lock value intialization */
	for (i = 0; i < DVFS_LOCK_ID_END; i++) {
		sdp_cpufreq_limit_level[CL_BIG][i] = sdp_info[CL_BIG]->max_support_idx;
		sdp_cpufreq_lock_level[CL_BIG][i] = sdp_info[CL_BIG]->min_support_idx;

		sdp_cpufreq_limit_level[CL_LITTLE][i] = sdp_info[CL_LITTLE]->max_support_idx;
		sdp_cpufreq_lock_level[CL_LITTLE][i] = sdp_info[CL_LITTLE]->min_support_idx;
	}
	
	apply_limit_level[CL_BIG] = sdp_info[CL_BIG]->max_support_idx;
	apply_lock_level[CL_BIG] = sdp_info[CL_BIG]->min_support_idx;
	apply_limit_level[CL_LITTLE] = sdp_info[CL_LITTLE]->max_support_idx;
	apply_lock_level[CL_LITTLE] = sdp_info[CL_LITTLE]->min_support_idx;

	/* get cpu regulator */
	cpu_regulator[CL_BIG] = regulator_get(NULL, "CPU_BIG_PW");
	if (IS_ERR(cpu_regulator[CL_BIG])) {
		pr_err("%s: failed to get big cpu regulator\n", __func__);
		cpu_regulator[CL_BIG] = NULL;
	}

	cpu_regulator[CL_LITTLE] = regulator_get(NULL, "CPU_LT_PW");
	if (IS_ERR(cpu_regulator[CL_LITTLE])) {
		pr_err("%s: failed to get LITTLE cpu regulator\n", __func__);
		cpu_regulator[CL_LITTLE] = NULL;
	}

	register_pm_notifier(&sdp_cpufreq_pm_nb);

#ifdef CONFIG_SDP_AVS
	register_sdp_asv_notifier(&sdp_cpufreq_asv_nb);
#endif

#ifdef CONFIG_SDP_THERMAL
	register_sdp_tmu_notifier(&sdp_cpufreq_tmu_nb);
#endif

	if (cpufreq_register_driver(&sdp_driver)) {
		pr_err("%s: failed to register cpufreq driver\n", __func__);
		goto err_cpufreq;
	}

	/* sysfs register*/
	ret = sdp_cpufreq_dev_register();
	if (ret < 0) {
		pr_err("%s: failed to register sysfs device\n", __func__);
		goto err_cpufreq;
	}

	return 0;

err_cpufreq:
#ifdef CONFIG_SDP_THERMAL
	unregister_sdp_tmu_notifier(&sdp_cpufreq_tmu_nb);
#endif

#ifdef CONFIG_SDP_AVS
	unregister_sdp_asv_notifier(&sdp_cpufreq_asv_nb);
#endif

	unregister_pm_notifier(&sdp_cpufreq_pm_nb);

	regulator_put(cpu_regulator[CL_BIG]);
	regulator_put(cpu_regulator[CL_LITTLE]);
err_vdd_arm:
	kfree(sdp_info[CL_BIG]);
	kfree(sdp_info[CL_LITTLE]);
	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
late_initcall(sdp_cpufreq_init);

