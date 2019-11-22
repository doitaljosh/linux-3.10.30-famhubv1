/*
 * sdp_thermal.c - SDP SoCs cpufreq
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
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <mach/soc.h>
#include <mach/sdp_thermal.h>

static struct sdp_tmu_info tmu_info;
static bool throt_on;
static struct workqueue_struct *tmu_monitor_wq;

/* lock */
static DEFINE_MUTEX(tmu_lock);

/* for log file */
#define LONGNAME_SIZE	255
#define LOG_BUF_LEN	255

static char tmu_log_file_path[LONGNAME_SIZE]; /* log file path comming from dts */

static char state_name[TMU_STATE_CNT][10] = {
	"Cold", "1st", "2nd", "3rd",
};

static BLOCKING_NOTIFIER_HEAD(tmu_chain_head);

int register_sdp_tmu_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&tmu_chain_head, nb);
}
EXPORT_SYMBOL(register_sdp_tmu_notifier);

int unregister_sdp_tmu_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&tmu_chain_head, nb);
}
EXPORT_SYMBOL(unregister_sdp_tmu_notifier);

static int sdp_tmu_notifier_call_chain(unsigned long val, struct throttle_params *param)
{
	int ret = blocking_notifier_call_chain(&tmu_chain_head, val, (void *)param);

	return notifier_to_errno(ret);
}

/* for log file */
static int tmu_1st_throttle_count = 0;
static int tmu_2nd_throttle_count = 0;
static int tmu_3rd_throttle_count = 0;

/* write log to filesystem */
static void write_log(unsigned int throttle)
{
	char tmu_longname[LONGNAME_SIZE];
	static char tmu_logbuf[LOG_BUF_LEN];
	size_t len;
	struct file *fp;
	
	if (throttle == TMU_1ST)
		tmu_1st_throttle_count++;
	else if (throttle == TMU_2ND)
		tmu_2nd_throttle_count++;
	else if (throttle == TMU_3RD)
		tmu_3rd_throttle_count++;
	else
		printk(KERN_INFO "TMU: %s - throttle level is not valid. %u\n",
				__func__, throttle);

	printk(KERN_INFO "TMU: %s - 1st = %d, 2nd = %d, 3rd = %d\n", __func__,
			tmu_1st_throttle_count, tmu_2nd_throttle_count, tmu_3rd_throttle_count);

	snprintf(tmu_longname, LONGNAME_SIZE, tmu_log_file_path);
	printk("tmu_longname : %s\n", tmu_longname);

	fp = filp_open(tmu_longname, O_CREAT|O_WRONLY|O_TRUNC|O_LARGEFILE, 0644);
	if (IS_ERR(fp)) {
		printk(KERN_ERR "TMU: error in opening tmu log file.\n");
		return;
	}

	snprintf(tmu_logbuf, LOG_BUF_LEN,
		"1st throttle count = %d\n"
		"2nd throttle count = %d\n"
		"3rd throttle count = %d\n",
		tmu_1st_throttle_count, tmu_2nd_throttle_count, tmu_3rd_throttle_count);
	len = (size_t)strlen(tmu_logbuf);
	
	fp->f_op->write(fp, tmu_logbuf, len, &fp->f_pos);

	filp_close(fp, NULL);	
}


/* sysfs */
static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);
	u32 temperature;

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	mutex_lock(&tmu_lock);

	temperature = info->get_temp(info);

	mutex_unlock(&tmu_lock);

	return snprintf(buf, 5, "%u\n", temperature);
}

static ssize_t show_tmu_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (!dev)
		return -ENODEV;

	if (info == NULL)
		return -1;

	return snprintf(buf, 3, "%d\n", info->tmu_state);
}

static ssize_t show_throttle_on(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 3, "%d\n", throt_on);
}

static ssize_t store_throttle_on(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	u32 on;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (on == 1) {
		if (throt_on) {
			printk(KERN_ERR "TMU: throttle already ON.\n");
			goto out;
		}
		throt_on = true;
		pr_info("TMU: throttle ON\n");
	} else if (on == 0) {
		if (!throt_on) {
			printk(KERN_ERR "TMU: throttle already OFF\n");
			goto out;
		}
		throt_on = false;
		pr_info("TMU: throttle OFF\n");
	} else {
		pr_err("TMU: Invalid value!! %d\n", on);
		return -EINVAL;
	}

out:
	return (ssize_t)count;
}

static ssize_t tmu_show_print_temp(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (info == NULL)
		return -1;

	ret = snprintf(buf, 3, "%d\n", info->user_print_on);

	return ret;
}

static ssize_t tmu_store_print_temp(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	unsigned int on;
	struct sdp_tmu_info *info = dev_get_drvdata(dev);

	if (info == NULL)
		goto err;

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) {
		printk(KERN_ERR "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if (on)
		info->user_print_on = true;
	else
		info->user_print_on = false;

err:
	return (ssize_t)count;
}

static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);
static DEVICE_ATTR(tmu_state, 0444, show_tmu_state, NULL);
static DEVICE_ATTR(throttle_on, 0644, show_throttle_on, store_throttle_on);
static DEVICE_ATTR(print_temp, 0644, tmu_show_print_temp, tmu_store_print_temp);

/* limit cpu and gpu frequency */
static void sdp_tmu_limit(struct sdp_tmu_info* info, u32 throttle)
{
	if (!throt_on) {
		pr_info("TMU: thermal throttle is OFF now.");
		return;
	}

	if (throttle < TMU_1ST || throttle > TMU_3RD) {
		pr_info("TMU: bad throttle state %d\n", throttle);
		return;
	}

	pr_info("TMU: state%d limit\n", throttle);

	info->print_on = true;

	/* send frequency limit notification. CPU and GPU limit */
	sdp_tmu_notifier_call_chain(SDP_TMU_FREQ_LIMIT,	&info->throttle[throttle]);
}

/* free cpu and gpu frequency limitation */
static void sdp_tmu_limit_free(struct sdp_tmu_info* info)
{
	/* free limit */
	info->print_on = false;

	/* cpu up */
	if ((info->handled_flag & THROTTLE_HOTPLUG_FLAG) &&
		info->cpu_hotplug_up) {
		pr_info("TMU: cpu hotplug up\n");
		info->cpu_hotplug_up(info);
	}

	/* cpu and gpu limit free*/
	sdp_tmu_notifier_call_chain(SDP_TMU_FREQ_LIMIT_FREE, NULL);
}

static void sdp_tmu_handle_cold_state(struct sdp_tmu_info *info, u32 cur_temp)
{
	/* 1. cold state. dvfs off */
	if (cur_temp <= info->throttle[TMU_COLD].start_temp &&
		!(info->handled_flag & THROTTLE_COLD_FLAG)) {
		/* if limit is set, free */
		if (info->handled_flag &
			(THROTTLE_1ST_FLAG | THROTTLE_2ND_FLAG | THROTTLE_3RD_FLAG)) {
			/* send limit free notification */
			sdp_tmu_limit_free(info);
			
			info->handled_flag = 0;
			pr_info("TMU: cold state. all limit free.\n");
		}

		/* send avs off notification */
		sdp_tmu_notifier_call_chain(SDP_TMU_AVS_OFF, NULL);

		info->handled_flag |= THROTTLE_COLD_FLAG;

		/* store current state */
		info->tmu_prev_state = info->tmu_state;
	/* 2. change to NORMAL */
	} else if (cur_temp >= info->throttle[TMU_COLD].stop_temp) {
		/* send avs on notification */
		sdp_tmu_notifier_call_chain(SDP_TMU_AVS_ON, NULL);

		info->tmu_state = TMU_STATUS_NORMAL;
		pr_info("TMU: change state: cold -> normal. %d'C\n", cur_temp);
	/* 3. polling */
	} else {
		if (!(info->handled_flag & THROTTLE_COLD_FLAG))
			pr_info("TMU: polling. cold state is not activated yet. %d'C\n", cur_temp);
	}
}

static void sdp_tmu_handle_normal_state(struct sdp_tmu_info *info, u32 cur_temp)
{
	/* 1. change to COLD THROTTLE */
	if (cur_temp <= info->throttle[TMU_COLD].start_temp) {
		info->tmu_state = TMU_STATUS_COLD;
		pr_info("TMU: change state: normal -> cold state. %d'C\n", cur_temp);
	/* 2. change to 1ST THROTTLE */
	} else if (cur_temp >= info->throttle[TMU_1ST].start_temp) {
		info->tmu_state = TMU_STATUS_1ST;
		pr_info("TMU: change state: normal -> 1st throttle. %d'C\n", cur_temp);
	/* 3. all limit free or dvfs/avs on */
	} else if (cur_temp > info->throttle[TMU_COLD].start_temp &&
		cur_temp <= info->throttle[TMU_1ST].start_temp &&
		info->handled_flag) {
		if (info->handled_flag &
			(THROTTLE_1ST_FLAG | THROTTLE_2ND_FLAG | THROTTLE_3RD_FLAG)) {
			/* send limit free notification */
			sdp_tmu_limit_free(info);
		}

		info->handled_flag = 0;

		/* store current state */
		info->tmu_prev_state = info->tmu_state;
		
		pr_info("TMU: current status is NORMAL. %d'C\n", cur_temp);
		pr_debug("TMU: info->handled_flag = %d. %d'C\n", info->handled_flag, cur_temp);
	} else {
		pr_debug("TMU: NORMAL polling. %d'C\n", cur_temp);
	}
}

static void sdp_tmu_handle_throttle(struct sdp_tmu_info *info, int cur_state, u32 cur_temp)
{
	u32 throt_idx;
	
	switch (cur_state) {
	case TMU_STATUS_1ST:
		throt_idx = TMU_1ST;
		break;
	case TMU_STATUS_2ND:
		throt_idx = TMU_2ND;
		break;
	case TMU_STATUS_3RD:
		throt_idx = TMU_3RD;
		break;
	default:
		pr_err("TMU: cur_state is wrong %d\n", cur_state);
		return;
	}
	
	/* 1. change to previous state */
	if (cur_temp <= info->throttle[throt_idx].stop_temp) {
		pr_info("TMU: change state: %s throttle -> %s. %d'C\n",
			info->throttle[throt_idx].name,
			(throt_idx == TMU_1ST) ? "normal" : info->throttle[throt_idx - 1].name,
			cur_temp);
		info->tmu_state--;
	/* 2. change to next state */
	} else if (throt_idx < TMU_3RD &&
		cur_temp >= info->throttle[throt_idx + 1].start_temp) {
		pr_info("TMU: change state: %s throttle -> %s throttle. %d'C\n",
			info->throttle[throt_idx].name, info->throttle[throt_idx + 1].name,
			cur_temp);
		info->tmu_state++;
	/* 3. THROTTLE - cpu, gpu limitation */
	} else if ((cur_temp >= info->throttle[throt_idx].start_temp) &&
			!(info->handled_flag & (1U << throt_idx))) {
		/* cpu up */
		if ((info->handled_flag & THROTTLE_HOTPLUG_FLAG) &&
			info->cpu_hotplug_up) {
			pr_info("TMU: cpu hotplug up\n");
			info->cpu_hotplug_up(info);
		}

		/* clear other handled flag */
		if (info->handled_flag & ~(1U << throt_idx))
			info->handled_flag = 0;

		/* frequency limit */
		sdp_tmu_limit(info, throt_idx);
		
		/* write tmu log to filesystem */
		if (info->tmu_prev_state < info->tmu_state)
			write_log(throt_idx);

		info->handled_flag |= (1U << throt_idx);
		pr_debug("info->handled_flag = %d\n", info->handled_flag);
		
		/* store current state */
		info->tmu_prev_state = info->tmu_state;
	} else {
		pr_debug("TMU: %s THROTTLE polling. %d'C\n",
			info->throttle[throt_idx].name, cur_temp);

		/* cpu hoplug down when temp is over hotplug temp */
		if (cur_state == TMU_STATUS_3RD &&
			throt_on && cur_temp >= info->cpu_hotplug_temp) {
			/* cpu down */
			if (!(info->handled_flag & THROTTLE_HOTPLUG_FLAG) &&
				info->cpu_hotplug_down) {
				pr_info("TMU: cpu hotplug down\n");
				info->cpu_hotplug_down(info);
				info->handled_flag |= THROTTLE_HOTPLUG_FLAG;
			}
		}
	}
}

static void sdp_handler_tmu_state(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct sdp_tmu_info *info = 
		container_of(delayed_work, struct sdp_tmu_info, polling);
	u32 cur_temp;

	mutex_lock(&tmu_lock);

	cur_temp = info->get_temp(info);

	switch (info->tmu_state) {
	case TMU_STATUS_INIT:
		info->tmu_prev_state = info->tmu_state = TMU_STATUS_COLD;
		
		pr_info("TMU: init state is %d(%d'C)\n", info->tmu_state, cur_temp);
		break;
	case TMU_STATUS_COLD:
		sdp_tmu_handle_cold_state(info, cur_temp);
		break;
	case TMU_STATUS_NORMAL:
		sdp_tmu_handle_normal_state(info, cur_temp);
		break;
	case TMU_STATUS_1ST:
	case TMU_STATUS_2ND:
	case TMU_STATUS_3RD:
		sdp_tmu_handle_throttle(info, info->tmu_state, cur_temp);
		break;
	default:
		pr_warn("TMU: Bug - checked tmu_state. %d\n", info->tmu_state);
		info->tmu_state = TMU_STATUS_INIT;
		
		break;
	};

	/* reschedule the next work */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
				info->sampling_rate);

	mutex_unlock(&tmu_lock);
}

static void get_tmu_info_from_of(struct sdp_tmu_info *info, struct device_node* np)
{
	const char * path;

	of_property_read_u32(np, "sensor_id", &info->sensor_id);
	
	of_property_read_u32(np, "start_cold_throttle", &info->throttle[TMU_COLD].start_temp);
	of_property_read_u32(np, "stop_cold_throttle", &info->throttle[TMU_COLD].stop_temp);
	of_property_read_u32(np, "start_1st_throttle", &info->throttle[TMU_1ST].start_temp);
	of_property_read_u32(np, "stop_1st_throttle", &info->throttle[TMU_1ST].stop_temp);
	of_property_read_u32(np, "start_2nd_throttle", &info->throttle[TMU_2ND].start_temp);
	of_property_read_u32(np, "stop_2nd_throttle", &info->throttle[TMU_2ND].stop_temp);
	of_property_read_u32(np, "start_3rd_throttle", &info->throttle[TMU_3RD].start_temp);
	of_property_read_u32(np, "stop_3rd_throttle", &info->throttle[TMU_3RD].stop_temp);
	of_property_read_u32(np, "start_3rd_hotplug", &info->cpu_hotplug_temp);
	if (!info->cpu_hotplug_temp)
		info->cpu_hotplug_temp = info->throttle[TMU_3RD].start_temp + 5;

	of_property_read_u32(np, "cpu_limit_1st_throttle", &info->throttle[TMU_1ST].cpu_limit_freq[0]);
	of_property_read_u32(np, "cpu_limit_2nd_throttle", &info->throttle[TMU_2ND].cpu_limit_freq[0]);
	of_property_read_u32(np, "cpu_limit_3rd_throttle", &info->throttle[TMU_3RD].cpu_limit_freq[0]);

	of_property_read_u32(np, "cpu_lt_limit_1st_throttle", &info->throttle[TMU_1ST].cpu_limit_freq[1]);
	of_property_read_u32(np, "cpu_lt_limit_2nd_throttle", &info->throttle[TMU_2ND].cpu_limit_freq[1]);
	of_property_read_u32(np, "cpu_lt_limit_3rd_throttle", &info->throttle[TMU_3RD].cpu_limit_freq[1]);

	of_property_read_u32(np, "gpu_limit_1st_throttle", &info->throttle[TMU_1ST].gpu_limit_freq);
	of_property_read_u32(np, "gpu_limit_2nd_throttle", &info->throttle[TMU_2ND].gpu_limit_freq);
	of_property_read_u32(np, "gpu_limit_3rd_throttle", &info->throttle[TMU_3RD].gpu_limit_freq);

	of_property_read_string(np, "log_file_path", &path);
	if (strlen(path) < LONGNAME_SIZE)
		strncpy(tmu_log_file_path, path, strlen(path));
}

static int sdp_tmu_init(struct sdp_tmu_info *info)
{
	int i;

	throt_on = true;

	info->throttle[TMU_COLD].name = state_name[TMU_COLD];
	info->throttle[TMU_1ST].name = state_name[TMU_1ST];
	info->throttle[TMU_2ND].name = state_name[TMU_2ND];
	info->throttle[TMU_3RD].name = state_name[TMU_3RD];
	
	pr_info("TMU: sensor id %d\n", info->sensor_id);

	for (i = TMU_COLD; i < TMU_STATE_CNT; i++)
		pr_info("TMU: [%s] start=%d'C, stop=%d'C, "
			"cpu=%dMHz, cpu_lt=%dMHz, gpu=%dMHz\n",
			info->throttle[i].name,
			info->throttle[i].start_temp, info->throttle[i].stop_temp,
			info->throttle[i].cpu_limit_freq[0] / 1000,
			info->throttle[i].cpu_limit_freq[1] / 1000,
			info->throttle[i].gpu_limit_freq / 1000);
	pr_info("TMU: [hotplug] start=%d'C\n", info->cpu_hotplug_temp);

	if (soc_is_sdp1304())
		sdp1304_tmu_init(info);
	else if (soc_is_sdp1404())
		sdp1404_tmu_init(info);
	else if (soc_is_sdp1406())
		sdp1406_tmu_init(info);
	else
		pr_err("erorr: must set soc type for tmu init\n");

	info->sampling_rate = usecs_to_jiffies(SAMPLING_RATE);
	
	return 0;
}

static int sdp_tmu_init_sysfs(struct sdp_tmu_info *info)
{
	int ret;
	
	/* sysfs interface */
	ret = device_create_file(info->dev, &dev_attr_temperature);
	if (ret != 0) {
		pr_err("Failed to create temperatue file: %d\n", ret);
		goto err_sysfs_file1;
	}

	ret = device_create_file(info->dev, &dev_attr_tmu_state);
	if (ret != 0) {
		pr_err("Failed to create tmu_state file: %d\n", ret);
		goto err_sysfs_file2;
	}

	ret = device_create_file(info->dev, &dev_attr_throttle_on);
	if (ret != 0) {
		pr_err("Failed to create throttle on file: %d\n", ret);
		goto err_sysfs_file3;
	}

	ret = device_create_file(info->dev, &dev_attr_print_temp);
	if (ret != 0) {
		pr_err("Failed to create print_temp\n");
		goto err;
	}

	return 0;

err:
	device_remove_file(info->dev, &dev_attr_throttle_on);

err_sysfs_file3:
	device_remove_file(info->dev, &dev_attr_tmu_state);

err_sysfs_file2:
	device_remove_file(info->dev, &dev_attr_temperature);

err_sysfs_file1:
	return ret;
}

static int sdp_tmu_pm_notifier(struct notifier_block *notifier,
			       unsigned long pm_event, void *v)
{
	struct sdp_tmu_info *info = &tmu_info;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pr_info("TMU: PM_SUSPEND_PREPARE\n");
		
		cancel_delayed_work(&info->polling);
		
		break;
	case PM_POST_SUSPEND:
		pr_info("TMU: PM_POST_SUSPEND\n");

		mutex_lock(&tmu_lock);

		info->tmu_prev_state = info->tmu_state = TMU_STATUS_INIT;

		mutex_unlock(&tmu_lock);

		/* reinit TSC */
		info->enable_tmu(info);
		
		/* wakeup */
		queue_delayed_work_on(0, tmu_monitor_wq, &info->polling, 0);
		
		break;
	default:
		break;
	}
	
	return NOTIFY_OK;
}

static struct notifier_block sdp_tmu_nb = {
	.notifier_call = sdp_tmu_pm_notifier,
};

/* probe */
static int sdp_tmu_probe(struct platform_device *pdev)
{
	struct sdp_tmu_info *info = &tmu_info;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int ret;

	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->tmu_state = TMU_STATUS_INIT;

	/* get platform data from device tree */
	get_tmu_info_from_of(info, np);

	/* initialization */
	ret = sdp_tmu_init(info);
	if (ret < 0) {
		pr_err("TMU: ERROR - failed to init\n");
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("TMU: ERROR - failed to get memory region resource\n");
		ret = -ENODEV;
		goto err;
	}

	info->tmu_base = devm_ioremap(&pdev->dev, res->start, (unsigned long)resource_size(res));
	if (!(info->tmu_base)) {
		pr_err("TMU: ERROR - failed ioremap()\n");
		ret = -ENOMEM;
		goto err;
	}

	tmu_monitor_wq = create_freezable_workqueue(dev_name(&pdev->dev));
	if (!tmu_monitor_wq) {
		pr_info("Creation of tmu_monitor_wq failed\n");
		ret = -ENOMEM;
		goto err;
	}

	/* To support periodic temprature monitoring */
	INIT_DELAYED_WORK(&info->polling, sdp_handler_tmu_state);

	sdp_tmu_init_sysfs(info);

	if (info->enable_tmu) {
		ret = info->enable_tmu(info);
		if (ret) {
			pr_err("error: enable_tmu fail\n");
			goto err;
		}
	} else {
		pr_err("error: enable_tmu is NULL\n");
		goto err;
	}

	/* initialize tmu_state */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
				info->sampling_rate);

	register_pm_notifier(&sdp_tmu_nb);

err:
	return 0;
}

static int sdp_tmu_remove(struct platform_device *pdev)
{
	struct sdp_tmu_info *info = platform_get_drvdata(pdev);

	if (info)
		cancel_delayed_work(&info->polling);

	destroy_workqueue(tmu_monitor_wq);

	device_remove_file(&pdev->dev, &dev_attr_temperature);
	device_remove_file(&pdev->dev, &dev_attr_tmu_state);
	device_remove_file(&pdev->dev, &dev_attr_throttle_on);

	pr_info("%s is removed\n", dev_name(&pdev->dev));
	
	return 0;
}

static struct of_device_id sdp_tmu_match[] = {
	{ .compatible = "samsung,sdp-thermal", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sdp_tmu_match);

static struct platform_driver sdp_tmu_driver = {
	.probe		= sdp_tmu_probe,
	.remove		= sdp_tmu_remove,
	.driver		= {
		.name   = "sdp-thermal",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_tmu_match),
	},
};

static int __init sdp_tmu_driver_init(void)
{
	return platform_driver_register(&sdp_tmu_driver);
}

static void __exit sdp_tmu_driver_exit(void)
{
	platform_driver_unregister(&sdp_tmu_driver);
}
late_initcall_sync(sdp_tmu_driver_init);
module_exit(sdp_tmu_driver_exit);
