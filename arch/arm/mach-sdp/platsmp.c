/*
 *  linux/arch/arm/mach-ccep/platsmp.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Cloned from linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/localtimer.h>
#include <asm/unified.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include <asm/mcpm.h>

#include <mach/sdp_smp.h>
#include <mach/soc.h>

#ifndef VA_SCU_BASE
#define VA_SCU_BASE 0
#endif

#ifdef CONFIG_HAVE_ARM_SCU
static void __iomem *sdp_scu_base = (void __iomem *)VA_SCU_BASE;
static void __iomem *scu_base_addr(void)
{
	return sdp_scu_base;
}

static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static inline int get_core_count(void)
{
	void __iomem *scu_base = scu_base_addr();
	if (scu_base)
		return (int) scu_get_core_count(scu_base);
	return nr_cpu_ids;
}
#endif

static DEFINE_SPINLOCK(boot_lock);

static void __cpuinit sdp_smp_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);

	set_cpu_online(cpu, true);
}

#ifdef CONFIG_OF
static const char *sdp_dt_cortex_a9_match[] __initconst = {
        "arm,cortex-a9-scu",
        NULL
};

static int __init sdp_dt_find_scu(unsigned long node,
		                const char *uname, int depth, void *data)
{
	if(of_flat_dt_match(node, sdp_dt_cortex_a9_match)) {
		phys_addr_t *val = (phys_addr_t *)of_get_flat_dt_prop(node, "reg", NULL);
		phys_addr_t phys_addr;

		if (WARN_ON(!val))
			return -EINVAL;

		if (sizeof(phys_addr_t) != sizeof(__be32))
			phys_addr = be64_to_cpup((__be64 *)val);
		else
			phys_addr = be32_to_cpup((__be32 *)val);
		sdp_scu_base = ioremap((unsigned long) phys_addr, SZ_256);
		
		if (WARN_ON(!sdp_scu_base))
		       return -EFAULT;
	}
	return 0;
}
#endif

static struct sdp_power_ops *sdp_power_ops;

void __init sdp_platsmp_init(struct sdp_power_ops *ops)
{
	if (ops)
		sdp_power_ops = ops;

#ifdef CONFIG_HAVE_ARM_SCU
#ifdef CONFIG_OF
	if (initial_boot_params)
		WARN_ON(of_scan_flat_dt(sdp_dt_find_scu, NULL));
#endif
#endif
}

void __init sdp_set_power_ops(struct sdp_power_ops *ops)
{
	sdp_power_ops = ops;
}

static int __cpuinit sdp_install_warp(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->install_warp)
		return sdp_power_ops->install_warp(cpu);
	else
		return 0;
}

static int __cpuinit sdp_powerup_cpu(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->powerup_cpu)
		return sdp_power_ops->powerup_cpu(cpu);
	else
		return 0;
}

int sdp_powerdown_cpu(unsigned int cpu)
{
	if (sdp_power_ops && sdp_power_ops->powerdown_cpu)
		return sdp_power_ops->powerdown_cpu(cpu);
	else
		return 0;
}

static int __cpuinit sdp_smp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	printk("start boot_secondary....\r\n");

	sdp_install_warp(cpu);
	sdp_powerup_cpu(cpu);
	
	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/* enable cpu clock on cpu1 */

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */

	write_pen_release(cpu_logical_map((int) cpu));

	/*
	 * This is a later addition to the booting protocol: the
	 * bootMonitor now puts secondary cores into WFI, so
	 * poke_milo() no longer gets the cores moving; we need
	 * to send a soft interrupt to wake the secondary core.
	 * Use smp_cross_call() for this, since there's little
	 * point duplicating the code here
	 */

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		
		arch_send_wakeup_ipi_mask(cpumask_of(cpu));	

		smp_rmb();
		if (pen_release == -1)
		{
			printk("pen release ok!!!!!\n");
			break;
		}

		udelay(10);
	}


	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init sdp_smp_init_cpus(void)
{
	int i, ncores;

	ncores = get_core_count();

	/* sanity check */
	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible((u32) i, true);

	//set_smp_cross_call(gic_raise_softirq);
}

void sdp_scu_enable(void)
{
#ifdef CONFIG_HAVE_ARM_SCU
	if(scu_base_addr())
		scu_enable(scu_base_addr());
#endif
}

static void __init sdp_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int i;

	for(i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	sdp_scu_enable();	
}

struct smp_operations sdp_smp_ops __initdata = {
	.smp_init_cpus		= sdp_smp_init_cpus,
	.smp_prepare_cpus	= sdp_smp_prepare_cpus,
	.smp_secondary_init	= sdp_smp_secondary_init,
	.smp_boot_secondary	= sdp_smp_boot_secondary,
#if defined(CONFIG_HOTPLUG_CPU)
	.cpu_kill		= sdp_cpu_kill,
	.cpu_die		= sdp_cpu_die,
	.cpu_disable		= sdp_cpu_disable,
#endif
};

bool __init sdp_smp_init_ops(void)
{
#if defined(CONFIG_MCPM)
	/*
	 * The best way to detect a multi-cluster configuration at the moment
	 * is to look for the presence of a CCI in the system.
	 * Override the default vexpress_smp_ops if so.
	 */
		mcpm_smp_set_ops();
		return true;
#else
	return false;
#endif
}

