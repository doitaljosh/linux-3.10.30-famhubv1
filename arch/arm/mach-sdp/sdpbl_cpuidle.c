/*
 * big.LITTLE CPU idle driver.
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/arm-cci.h>
#include <linux/bitmap.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tick.h>
#include <linux/vexpress.h>
#include <asm/mcpm.h>
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <linux/of.h>

static int sdpbl_cpuidle_simple_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	ktime_t time_start, time_end;
	s64 diff;

	time_start = ktime_get();

	cpu_do_idle();

	time_end = ktime_get();

	local_irq_enable();

	diff = ktime_to_us(ktime_sub(time_end, time_start));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;

	return index;
}

static int sdpbl_enter_powerdown(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx);

static struct cpuidle_state sdpbl_cpuidle_set[] __initdata = {
	[0] = {
		.enter                  = sdpbl_cpuidle_simple_enter,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= INT_MAX,
		.flags                  = CPUIDLE_FLAG_TIME_VALID,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	},
	[1] = {
		.enter			= sdpbl_enter_powerdown,
		.exit_latency		= 2000,
		.target_residency	= 200000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

struct cpuidle_driver sdpbl_idle_driver = {
	.name = "sdpbl_idle",
	.owner = THIS_MODULE,
	.safe_state_index = 0
};

static DEFINE_PER_CPU(struct cpuidle_device, sdpbl_idle_dev);

static int notrace sdpbl_powerdown_finisher(unsigned long arg)
{
	unsigned int mpidr = read_cpuid_mpidr();
	unsigned int cluster = (mpidr >> 8) & 0xf;
	unsigned int cpu = mpidr & 0xf;

	mcpm_set_entry_vector(cpu, cluster, cpu_resume);
	mcpm_cpu_suspend(0);  /* 0 should be replaced with better value here */
	return 1;
}

/*
 * sdpbl_enter_powerdown - Programs CPU to enter the specified state
 * @dev: cpuidle device
 * @drv: The target state to be programmed
 * @idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int sdpbl_enter_powerdown(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	struct timespec ts_preidle, ts_postidle, ts_idle;
	int ret;
	unsigned int cpu;

	cpu = smp_processor_id();

	/* Used to keep track of the total time in idle */
	getnstimeofday(&ts_preidle);

	BUG_ON(!irqs_disabled());

	cpu_pm_enter();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	ret = cpu_suspend((unsigned long) dev, sdpbl_powerdown_finisher);
	if (ret)
		BUG();

	mcpm_cpu_powered_up();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

	cpu_pm_exit();

	getnstimeofday(&ts_postidle);
	
	local_irq_enable();

#if 0	/* this should be done by gic's CPU_PM_EXIT notification */
	/* enable GIC */
	writel_relaxed(1, (void *)0xfef82000);
#endif
	
	ts_idle = timespec_sub(ts_postidle, ts_preidle);

	dev->last_residency = ts_idle.tv_nsec / NSEC_PER_USEC +
					ts_idle.tv_sec * USEC_PER_SEC;

	return idx;
}

/*
 * sdpbl_idle_init
 *
 * Registers the bl specific cpuidle driver with the cpuidle
 * framework with the valid set of states.
 */
static int __init sdpbl_idle_init(void)
{
	struct cpuidle_device *dev;
	int i, cpu_id;
	struct cpuidle_driver *drv = &sdpbl_idle_driver;

	if (!of_find_compatible_node(NULL, NULL, "samsung,sdp1404")) {
		pr_info("%s: No compatible node found\n", __func__);
		return -ENODEV;
	}

	drv->state_count = (sizeof(sdpbl_cpuidle_set) /
				       sizeof(struct cpuidle_state));

	for (i = 0; i < drv->state_count; i++) {
		memcpy(&drv->states[i], &sdpbl_cpuidle_set[i],
				sizeof(struct cpuidle_state));
	}

	cpuidle_register_driver(drv);

	for_each_cpu(cpu_id, cpu_online_mask) {
		pr_err("CPUidle for CPU%d registered\n", cpu_id);
		dev = &per_cpu(sdpbl_idle_dev, cpu_id);
		dev->cpu = (u32) cpu_id;

		dev->state_count = drv->state_count;
		/* default disable for state1 */
		dev->states_usage[1].disable = 1;

		if (cpuidle_register_device(dev)) {
			printk(KERN_ERR "%s: Cpuidle register device failed\n",
			       __func__);
			return -EIO;
		}
	}

	return 0;
}

device_initcall(sdpbl_idle_init);
