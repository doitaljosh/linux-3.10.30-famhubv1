/*
 * sdp_cpufreq.c - SDP SoCs cpufreq
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
#include <linux/platform_data/sdp-cpufreq.h>

#ifdef CONFIG_SDP_AVS
#include <linux/power/sdp_asv.h>
#endif

#ifdef CONFIG_SDP_THERMAL
#include <mach/sdp_thermal.h>
#endif

#include <mach/soc.h>

static struct sdp_dvfs_info *sdp_info;

static struct regulator *cpu_regulator;
static struct cpufreq_freqs freqs;

static DEFINE_MUTEX(cpufreq_lock);
static DEFINE_MUTEX(cpufreq_on_lock);

static bool sdp_cpufreq_on = false;
static bool sdp_cpufreq_fixed = false;
static bool sdp_cpufreq_init_done = false;
bool sdp_cpufreq_print_on = false;

static unsigned int apply_limit_level;
static unsigned int sdp_cpufreq_limit_id;
static unsigned int sdp_cpufreq_limit_level[DVFS_LOCK_ID_END];

static unsigned int apply_lock_level;
static unsigned int sdp_cpufreq_lock_id;
static unsigned int sdp_cpufreq_lock_level[DVFS_LOCK_ID_END];

static inline unsigned int get_online_cpu(void)
{
	unsigned int cpu;
	int i;

	for (i = 0; i <= 4; i++)
		if (!!cpu_online(i)) {
			cpu = i;
			break;
		}
	if (i > 4)
		cpu = 0;

	//printk("%s:cpu%u selected\n", __func__, cpu);

	return cpu;
}

static int sdp_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
						sdp_info->freq_table);
}

static unsigned int sdp_getspeed(unsigned int cpu)
{
	if (sdp_info->get_speed)
		return sdp_info->get_speed(cpu) / 1000;
	else
		return (unsigned int)(clk_get_rate(sdp_info->cpu_clk) / 1000);
}

static int sdp_cpufreq_get_index(unsigned int freq)
{
	struct cpufreq_frequency_table *freq_table = sdp_info->freq_table;
	int index;

	for (index = 0;
		freq_table[index].frequency != CPUFREQ_TABLE_END; index++)
		if (freq_table[index].frequency == freq)
			break;

	if (freq_table[index].frequency == CPUFREQ_TABLE_END)
		return -EINVAL;

	return index;	
}

static int sdp_cpufreq_scale(struct cpufreq_policy *policy,
				unsigned int target_freq)
{
	unsigned int *volt_table = sdp_info->volt_table;
	unsigned int arm_volt;
	unsigned int new_index, old_index;
	int ret = 0;

	if (!policy)
		return -EINVAL;
	
	freqs.old = policy->cur;
	freqs.new = target_freq;

	if (freqs.new == freqs.old)
				goto out;

	/*
	 * The policy max have been changed so that we cannot get proper
	 * old_index with cpufreq_frequency_table_target(). Thus, ignore
	 * policy and get the index from the raw freqeuncy table.
	 */
	old_index = sdp_cpufreq_get_index(freqs.old);
	if (old_index < 0) {
		ret = old_index;
		printk("%s: error old_index=%d\n", __FUNCTION__, old_index);
		goto out;
	}

	new_index = sdp_cpufreq_get_index(target_freq);
	if (new_index < 0) {
		ret = new_index;
		printk("%s: error new_index=%d\n", __FUNCTION__, new_index);
		goto out;
	}

	/*
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	arm_volt = volt_table[new_index];

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	/* When the new frequency is higher than current frequency */
	if (freqs.new > freqs.old && cpu_regulator) {
		/* Firstly, voltage up to increase frequency */
		ret = regulator_set_voltage(cpu_regulator, arm_volt, arm_volt);
		if (ret) {
			pr_err("%s: failed to set cpu voltage to %d\n",
				__func__, arm_volt);
			goto out;
		}
	}

	sdp_info->set_freq(policy->cpu, old_index, new_index, 0);

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if (freqs.new < freqs.old && cpu_regulator) {
		/* down the voltage after frequency change */
		ret = regulator_set_voltage(cpu_regulator, arm_volt, arm_volt);
		if (ret) {
			pr_err("%s: failed to set cpu voltage to %d\n",
				__func__, arm_volt);
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
	unsigned int index;
	unsigned int new_freq;
	int ret = 0;

	if (!policy)
		return -EINVAL;

	freq_table = sdp_info->freq_table;

	mutex_lock(&cpufreq_lock);

	/* check on/off */
	if (!sdp_cpufreq_on)
		goto out;

	/* check frequency fix */
	if (sdp_cpufreq_fixed)
		goto out;

	ret = cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index);
	if (ret < 0) {
		printk("%s: find table target error %d\n", __func__, ret);
		ret = -EINVAL;
		goto out;
	}

	/* check frequency lock */
	if (index > apply_lock_level)
		index = apply_lock_level;

	/* check frequency limit */
	if (index < apply_limit_level)
		index = apply_limit_level;

	new_freq = freq_table[index].frequency;

	ret = sdp_cpufreq_scale(policy, new_freq);

out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

static int sdp_cpufreq_freq_fix(unsigned int freq, bool on)
{
	struct cpufreq_policy *policy;
	int ret = 0;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	mutex_lock(&cpufreq_lock);
	
	/* frequency fix */
	if (on) {
		if (sdp_cpufreq_fixed) {
			pr_info("CPUFreq is already fixed\n");
			ret = -EPERM;
			goto out;
		}

		/* convert to 10MHz scale */
		freq = (freq / 10000) * 10000;

		policy = cpufreq_cpu_get(get_online_cpu());
		if (!policy) {
			ret = -EPERM;
			goto out;
		}

		ret = sdp_cpufreq_scale(policy, freq);
			if (ret < 0) {
				pr_err("cpufreq scaling is failed\n");
				cpufreq_cpu_put(policy);
				goto out;
			}

		sdp_cpufreq_fixed = true;

		cpufreq_cpu_put(policy);
	/* frequency unfix */
	} else {
		sdp_cpufreq_fixed = false;
	}

out:
	mutex_unlock(&cpufreq_lock);
	return ret;
}

/* This function limits frequnecy uppper level */
int sdp_cpufreq_limit(unsigned int nId,	enum cpufreq_level_index cpufreq_level)
{
	struct cpufreq_frequency_table *freq_table;
	unsigned int *volt_table;
	unsigned int freq_new;
	struct cpufreq_policy *policy;
	int ret = 0, i;

	if (!sdp_info || !sdp_cpufreq_init_done)
		return -EPERM;

	if (sdp_cpufreq_fixed) {
		pr_info("CPUFreq is already fixed\n");
		return -EPERM;
	}

	if (cpufreq_level < sdp_info->max_support_idx ||
		cpufreq_level > sdp_info->min_real_idx) {
		pr_err("%s: invalid cpufreq_level(%d:%d)\n", __func__, nId,
				cpufreq_level);
		return -EINVAL;
	}

	mutex_lock(&cpufreq_lock);

	volt_table = sdp_info->volt_table;
	freq_table = sdp_info->freq_table;
	
	sdp_cpufreq_limit_id |= (1U << nId);
	sdp_cpufreq_limit_level[nId] = cpufreq_level;

	/* find lowest level and store for the next sdp_target */
	apply_limit_level = sdp_info->max_support_idx;
	for (i = 0; i < DVFS_LOCK_ID_END; i++)
		if (sdp_cpufreq_limit_level[i] > apply_limit_level)
			apply_limit_level = sdp_cpufreq_limit_level[i];

	/* update cpu frequency */
	freq_new = freq_table[apply_limit_level].frequency;

	policy = cpufreq_cpu_get(get_online_cpu());
	if (!policy) {
		mutex_unlock(&cpufreq_lock);	
		return -EPERM;
	}

	ret = sdp_cpufreq_scale(policy, freq_new);

	if (policy)
	cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);

	return ret;
}
EXPORT_SYMBOL(sdp_cpufreq_limit);

/* This function frees upper limit */
void sdp_cpufreq_limit_free(unsigned int nId)
{
	unsigned int i;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_policy *policy;

	if (!sdp_cpufreq_init_done)
		return;

	freq_table = sdp_info->freq_table;

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_limit_id &= ~(1U << nId);
	sdp_cpufreq_limit_level[nId] = sdp_info->max_support_idx;
	apply_limit_level = sdp_info->max_support_idx;

	/* find lowest frequency */
	if (sdp_cpufreq_limit_id) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (sdp_cpufreq_limit_level[i] > apply_limit_level)
				apply_limit_level = sdp_cpufreq_limit_level[i];
		}
	}
	
	policy = cpufreq_cpu_get(get_online_cpu());
	if (!policy) {
		pr_err("%s: policy is no available\n", __func__);
		goto out;
	}
	
	sdp_cpufreq_scale(policy, freq_table[apply_limit_level].frequency);

	cpufreq_cpu_put(policy);

out:	
	mutex_unlock(&cpufreq_lock);
}
EXPORT_SYMBOL(sdp_cpufreq_limit_free);

/* This function locks frequency lower level */
int sdp_cpufreq_lock(unsigned int nId, enum cpufreq_level_index cpufreq_level)
{
	int ret = 0, i;
	unsigned int freq_new;
	struct cpufreq_policy *policy;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	if (!sdp_info)
		return -EPERM;

	if (sdp_cpufreq_fixed && (nId != DVFS_LOCK_ID_TMU)) {
		pr_info("CPUFreq is already fixed\n");
		return -EPERM;
	}

	if (cpufreq_level < sdp_info->max_real_idx ||
		cpufreq_level > sdp_info->min_support_idx) {
		pr_warn("%s: invalid cpufreq_level(%d:%d)\n", __func__, nId,
				cpufreq_level);
		return -EINVAL;
	}

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_lock_id |= (1U << nId);
	sdp_cpufreq_lock_level[nId] = cpufreq_level;

	/* find highest level and store for the next sdp_target */
	apply_lock_level = sdp_info->min_support_idx;
	for (i = 0; i < DVFS_LOCK_ID_END; i++)
		if (sdp_cpufreq_lock_level[i] < apply_lock_level)
			apply_lock_level = sdp_cpufreq_lock_level[i];

	/* update cpu frequency */
	freq_new = sdp_info->freq_table[apply_lock_level].frequency;

	policy = cpufreq_cpu_get(get_online_cpu());
	if (!policy) {
		mutex_unlock(&cpufreq_lock);
		return -EPERM;
	}
	
	ret = sdp_cpufreq_scale(policy, freq_new);

	cpufreq_cpu_put(policy);

	mutex_unlock(&cpufreq_lock);

	return ret;
}
EXPORT_SYMBOL(sdp_cpufreq_lock);

/* This function frees locked frequency lower level */
void sdp_cpufreq_lock_free(unsigned int nId)
{
	unsigned int i;
	struct cpufreq_frequency_table *freq_table;
	struct cpufreq_policy *policy;

	if (!sdp_cpufreq_init_done)
		return;

	freq_table = sdp_info->freq_table;

	mutex_lock(&cpufreq_lock);

	sdp_cpufreq_lock_id &= ~(1U << nId);
	sdp_cpufreq_lock_level[nId] = sdp_info->min_support_idx;
	apply_lock_level = sdp_info->min_support_idx;

	/* find highest level */
	if (sdp_cpufreq_lock_id) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (sdp_cpufreq_lock_level[i] < apply_lock_level)
				apply_lock_level = sdp_cpufreq_lock_level[i];
		}
	}

	policy = cpufreq_cpu_get(get_online_cpu());
	if (!policy) {
		mutex_unlock(&cpufreq_lock);
		return;
	}
		
	sdp_cpufreq_scale(policy, freq_table[apply_lock_level].frequency);

	cpufreq_cpu_put(policy);
	
	mutex_unlock(&cpufreq_lock);
}
EXPORT_SYMBOL(sdp_cpufreq_lock_free);

int sdp_cpufreq_get_level(unsigned int freq, unsigned int *level)
{
	struct cpufreq_frequency_table *table;
	unsigned int i;

	if (!sdp_cpufreq_init_done)
		return -EPERM;

	table = cpufreq_frequency_get_table(get_online_cpu());
	if (!table) {
		pr_err("%s: Failed to get the cpufreq table\n", __func__);
		return -EINVAL;
	}

	for (i = sdp_info->max_real_idx;
		(table[i].frequency != (u32)CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == freq) {
			*level = i;
			return 0;
		}
	}

	pr_err("%s: %u KHz is an unsupported cpufreq\n", __func__, freq);

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

	ret = sprintf(buf, "%d\n", sdp_cpufreq_on);

	return ret;
}

static ssize_t sdp_cpufreq_on_store(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int on;
	struct cpufreq_policy *policy;
	int ret;
	int i, timeout = 10;
	
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

		policy = cpufreq_cpu_get(get_online_cpu());
		if (!policy) {
			printk(KERN_ERR "%s - policy is NULL\n", __func__);
			goto out;
		}
		
		/* frequency to max */
		for (i = 0; i < timeout; i++) {
			if (!sdp_target(policy, policy->max, CPUFREQ_RELATION_H))
				break;

			printk(KERN_WARNING "retry frequnecy setting.\n");
			msleep(10);
		}
		if (i == timeout)
			printk(KERN_WARNING "frequnecy set time out!!\n");
		
		sdp_cpufreq_on = false;

		cpufreq_cpu_put(policy);
	} else {
		printk(KERN_ERR "%s: ERROR - input 0 or 1\n", __func__);
	}

out:
	mutex_unlock(&cpufreq_on_lock);
	return (ssize_t)count;
}
static struct global_attr cpufreq_on = __ATTR(cpufreq_on, 0644, sdp_cpufreq_on_show, sdp_cpufreq_on_store);

/* cpufreq fix */
static ssize_t sdp_cpufreq_freqfix_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct cpufreq_policy *policy;
	ssize_t ret;
	int cur;

	if (sdp_cpufreq_fixed) {
		mutex_lock(&cpufreq_lock);
		
		policy = cpufreq_cpu_get(get_online_cpu());
		if (policy)
			cur = policy->cur;
		else
			cur = 0;
		
		ret = sprintf(buf, "%d\n", cur);

		if (policy)
			cpufreq_cpu_put(policy);

		mutex_unlock(&cpufreq_lock);
	} else {
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t sdp_cpufreq_freqfix_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
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
	sdp_cpufreq_freq_fix(freq, false);

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, cpufreq_fix\n", freq);
		sdp_cpufreq_freq_fix(freq, true);
	} else {
		printk(KERN_DEBUG "freq=%u, cpufreq unfix\n", freq);
	}
	
	return (ssize_t)count;
}
static struct global_attr frequency = __ATTR(frequency, 0644, sdp_cpufreq_freqfix_show, sdp_cpufreq_freqfix_store);

static struct attribute *dbs_attributes[] = {
	&frequency.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "freqfix",
};

/* frequency limitation */
static ssize_t sdp_cpufreq_freqlimit_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (sdp_cpufreq_limit_id & (1<<DVFS_LOCK_ID_USER)) {
		freq = sdp_info->freq_table[sdp_cpufreq_limit_level[DVFS_LOCK_ID_USER]].frequency;
		ret = sprintf(buf, "%d\n", freq);
	} else {
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t sdp_cpufreq_freqlimit_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
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
		ret = sdp_cpufreq_get_level(freq, &level);
		if (ret < 0)
			goto out;
	}

#if 0
	if (sdp_cpufreq_limit_id & (1 << DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlimit\n", freq);
		sdp_cpufreq_limit_free(DVFS_LOCK_ID_USER);
	}
#endif

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, freq limit\n", freq);
		sdp_cpufreq_limit(DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "freq=%u, freq unlimit\n", freq);
		sdp_cpufreq_limit_free(DVFS_LOCK_ID_USER);
	}
	
out:
	return (ssize_t)count;
}
static struct global_attr freq_limit = __ATTR(freq_limit, 0644, 
					sdp_cpufreq_freqlimit_show, 
					sdp_cpufreq_freqlimit_store);

/* frequency lock */
static ssize_t sdp_cpufreq_freqlock_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	ssize_t ret;
	unsigned int freq;

	if (sdp_cpufreq_lock_id & (1 << DVFS_LOCK_ID_USER)) {
		freq = sdp_info->freq_table[sdp_cpufreq_lock_level[DVFS_LOCK_ID_USER]].frequency;
		ret = sprintf(buf, "%d\n", freq);
	} else {
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t sdp_cpufreq_freqlock_store(struct kobject *a, struct attribute *b,
			 		const char *buf, size_t count)
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
		ret = sdp_cpufreq_get_level(freq, &level);
		if (ret < 0)
			goto out;
	}

#if 0
	if (sdp_cpufreq_lock_id & (1 << DVFS_LOCK_ID_USER)) {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(DVFS_LOCK_ID_USER);
	}
#endif

	if (freq > 0) {
		printk(KERN_DEBUG "freq=%u, freq lock\n", freq);
		sdp_cpufreq_lock(DVFS_LOCK_ID_USER, level);
	} else {
		printk(KERN_DEBUG "freq=%u, freq unlock\n", freq);
		sdp_cpufreq_lock_free(DVFS_LOCK_ID_USER);
	}

out:	
	return (ssize_t)count;
}
static struct global_attr freq_lock = __ATTR(freq_lock, 0644, sdp_cpufreq_freqlock_show,
						sdp_cpufreq_freqlock_store);

/* DVFS voltage table update from Filesystem */
static ssize_t sdp_cpufreq_voltupdate_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i;
	unsigned int *volt_table = sdp_info->volt_table;

	for (i = (int)sdp_info->max_real_idx; i <= (int)sdp_info->min_real_idx; i++)
		printk(KERN_INFO "group%d, [%4dMHz] %7uuV\n", 
				sdp_info->cur_group, 
				sdp_info->freq_table[i].frequency / 1000,
				volt_table[i]);

	return ret;
}

#ifdef MAX_CPU_ASV_GROUP
	unsigned int g_volt_table[LMAX][MAX_CPU_ASV_GROUP];
#else
	unsigned int g_volt_table[LMAX][10];
#endif
static ssize_t sdp_cpufreq_voltupdate_store(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int i, j;
	size_t size, read_cnt = 0;
	char atoi_buf[15];
	char temp;
	int line_cnt = 0, char_cnt;
	bool started = 0, loop = 1;

	/* store to memory */
	memset(g_volt_table, 0, sizeof(g_volt_table));
	
	size = count;//filp->f_dentry->d_inode->i_size;

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
		if (i > (sdp_info->min_real_idx - sdp_info->max_real_idx + 1)) {
			printk(KERN_ERR "cpufreq ERR: volt table line count is more than %d, i = %d\n",
					sdp_info->min_real_idx - sdp_info->max_real_idx + 1, i);
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
				g_volt_table[i][j] = (unsigned int)simple_strtoul(atoi_buf, (char **)&atoi_buf, 0);
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
	if (line_cnt != (sdp_info->min_real_idx - sdp_info->max_real_idx + 1)) {
		printk(KERN_ERR "cpufreq ERR: volt table line count is not %d\n",
			sdp_info->min_real_idx - sdp_info->max_real_idx + 1);
	
		goto out;
	}

	/* change current volt table */
	printk(KERN_INFO "> DVFS volt table change\n");
	for (i = sdp_info->max_real_idx, j = 0; i <= sdp_info->min_real_idx; i++, j++) {
		printk(KERN_INFO "group%d, [%4dMHz] %7uuV -> %7uuV\n", 
			sdp_info->cur_group, sdp_info->freq_table[i].frequency/1000,
			sdp_info->volt_table[i], g_volt_table[j][sdp_info->cur_group]);
		sdp_info->volt_table[i] = g_volt_table[j][sdp_info->cur_group];
	}
	printk(KERN_INFO "> DONE\n");
	
out:
	return (ssize_t)count;
}
static struct global_attr volt_update = __ATTR(volt_update, 0644,
						sdp_cpufreq_voltupdate_show,
						sdp_cpufreq_voltupdate_store);

/* debug print on/off */
static ssize_t sdp_cpufreq_print_on_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t ret;

	ret = sprintf(buf, "%d\n", sdp_cpufreq_print_on);

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

	err = sysfs_create_file(cpufreq_global_kobject, &freq_limit.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &freq_lock.attr);
	if (err < 0)
		goto out;

	err = sysfs_create_file(cpufreq_global_kobject, &volt_update.attr);
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

#ifdef CONFIG_SDP_AVS
static int sdp_cpufreq_asv_notifier(struct notifier_block *notifier,
			  	     unsigned long event, void *v)
{
	struct sdp_asv_info *asv_info = (struct sdp_asv_info *)v;
	unsigned int volt;
	unsigned int cur_freq_idx;
	struct cpufreq_policy *policy;
	int ret;

	if (!asv_info) {
		pr_err("CPUFREQ: error - asv_info is NULL\n");
		return NOTIFY_DONE;
	}

	pr_info("%s: cpu group=%d\n", __func__, asv_info->cpu[0].result);

	mutex_lock(&cpufreq_lock);

	switch (event) {
	case SDP_ASV_NOTIFY_AVS_ON:
		sdp_info->cur_group = asv_info->cpu[0].result;
		sdp_info->update_volt_table(asv_info->cpu[0].result);
		break;
	case SDP_ASV_NOTIFY_AVS_OFF:
		sdp_info->cur_group = 0;
		sdp_info->update_volt_table(0);
		break;
	default:
		break;
	}

	/* apply cpu AVS voltage */
	if (event == SDP_ASV_NOTIFY_AVS_ON ||
		event == SDP_ASV_NOTIFY_AVS_OFF) {
		if (cpu_regulator) {
			policy = cpufreq_cpu_get(get_online_cpu());
			if (!policy) {
				pr_err("CPUFREQ: error - policy is NULL\n");
				goto out;
			}

			ret = sdp_cpufreq_get_level(policy->cur, &cur_freq_idx);
			cpufreq_cpu_put(policy);
			if (ret) {
				pr_err("CPUFREQ: error - failed to get level\n");
			
				goto out;
			}

			volt = sdp_info->volt_table[cur_freq_idx];
			
			pr_info("set cpu voltage to %uuV\n", volt);
 			ret = regulator_set_voltage(cpu_regulator, volt, volt);
			if (ret)
				pr_err("%s: failed to set cpu voltage to %d\n",
					__func__, volt);
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

	if (v) {
		param = (struct throttle_params *)v;
		pr_info("%s: freq=%dMHz\n", __func__, param->cpu_limit_freq[0] / 1000);
	}

	switch (event) {
	case SDP_TMU_FREQ_LIMIT:
		if (!param)
			break;
		
		ret = sdp_cpufreq_get_level(param->cpu_limit_freq[0], &level);
		if (ret < 0) {
			pr_info("DVFS: can't find level using %dMHz\n",
				param->cpu_limit_freq[0] / 1000);
			break;
		}
		pr_info("DVFS: freq limit %dMHz\n", param->cpu_limit_freq[0] / 1000);
		sdp_cpufreq_limit(DVFS_LOCK_ID_TMU, level);
		break;
		
	case SDP_TMU_FREQ_LIMIT_FREE:
		pr_info("DVFS: freq limit free\n");
		sdp_cpufreq_limit_free(DVFS_LOCK_ID_TMU);
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
	policy->cur = policy->min = policy->max = sdp_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(sdp_info->freq_table, policy->cpu);

	/* set the transition latency value (nano second) */
	policy->cpuinfo.transition_latency = 100000;

	cpumask_setall(policy->cpus);

	return cpufreq_frequency_table_cpuinfo(policy, sdp_info->freq_table);
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

#if defined(CONFIG_ARM_SDP1304_CPUFREQ)
extern int sdp1304_cpufreq_init(struct sdp_dvfs_info *info);
#elif defined(CONFIG_ARM_SDP1307_CPUFREQ)
extern int sdp1307_cpufreq_init(struct sdp_dvfs_info *info);
#elif defined(CONFIG_ARM_SDP1406_CPUFREQ)
extern int sdp1406_cpufreq_init(struct sdp_dvfs_info *info);
#endif

static int __init sdp_cpufreq_init(void)
{
	int ret = -EINVAL;
	int i;

	sdp_info = kzalloc(sizeof(struct sdp_dvfs_info), GFP_KERNEL);
	if (!sdp_info)
		return -ENOMEM;
	
#if defined(CONFIG_ARM_SDP1304_CPUFREQ)
	ret = sdp1304_cpufreq_init(sdp_info);
#elif defined(CONFIG_ARM_SDP1307_CPUFREQ)
	ret = sdp1307_cpufreq_init(sdp_info);
#elif defined(CONFIG_ARM_SDP1406_CPUFREQ)
	ret = sdp1406_cpufreq_init(sdp_info);
#endif
	if (ret)
		goto err_vdd_arm;

	if (sdp_info->set_freq == NULL) {
		pr_err("%s: No set_freq function (ERR)\n", __func__);
		goto err_vdd_arm;
	}

	sdp_cpufreq_init_done = true;

	/* freq limit and lock value intialization */
	for (i = 0; i < DVFS_LOCK_ID_END; i++) {
		sdp_cpufreq_limit_level[i] = sdp_info->max_support_idx;
		sdp_cpufreq_lock_level[i] = sdp_info->min_support_idx;
	}
	
	apply_limit_level = sdp_info->max_support_idx;
	apply_lock_level = sdp_info->min_support_idx;

	/* get cpu regulator */
	cpu_regulator = regulator_get(NULL, "CPU_PW");
	if (IS_ERR(cpu_regulator)) {
		pr_err("%s: failed to get cpu regulator\n", __func__);
		cpu_regulator = NULL;
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

	regulator_put(cpu_regulator);
err_vdd_arm:
	kfree(sdp_info);
	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
late_initcall(sdp_cpufreq_init);

