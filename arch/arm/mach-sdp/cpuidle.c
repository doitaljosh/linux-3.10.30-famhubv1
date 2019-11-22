/* linux/arch/arm/mach-sdp/cpuidle.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/smp.h>

#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/unified.h>
#include <asm/cpuidle.h>
#include <mach/sdp_smp.h>

#include "common.h"

static int sdp_enter_idle(struct cpuidle_device *dev,
			  struct cpuidle_driver *drv,
			  int index);

static int sdp_enter_lowpower(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index);

static struct cpuidle_state sdp_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= sdp_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 10,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "WFI",
		.desc			= "ARM WFI",
	},
	[1] = {
		.enter			= sdp_enter_lowpower,
		.exit_latency		= 5000,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, sdp_cpuidle_device);

static struct cpuidle_driver sdp_idle_driver = {
	.name		= "sdp_idle",
	.owner		= THIS_MODULE,
};

static int sdp_enter_idle(struct cpuidle_device *dev,
			  struct cpuidle_driver *drv,
			  int index)
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

static int sdp_enter_lowpower(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index)
{
	sdp_enter_idle(dev, drv, index);

	return index;
}

static int __init sdp_init_cpuidle(void)
{
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &sdp_idle_driver;
	int i;
	int cpu_id;
	int ret;

	/* setup cpuidle driver */
	drv->state_count = ARRAY_SIZE(sdp_cpuidle_set);

	for (i = 0; i < drv->state_count; i++)
		memcpy(&drv->states[i], &sdp_cpuidle_set[i],
			sizeof(struct cpuidle_state));
	drv->safe_state_index = 0;
	ret = cpuidle_register_driver(&sdp_idle_driver);
	if (ret < 0)
		printk(KERN_ERR "error : cpuidle driver register fail\n");

	for_each_cpu(cpu_id, cpu_online_mask) {
		dev = &per_cpu(sdp_cpuidle_device, cpu_id);
		dev->cpu = cpu_id;

		dev->state_count = ARRAY_SIZE(sdp_cpuidle_set);

		if (cpuidle_register_device(dev)) {
			printk(KERN_ERR "CPUidle register device failed\n");
			return -EIO;
		}
	}
	
	return 0;
}
device_initcall(sdp_init_cpuidle);