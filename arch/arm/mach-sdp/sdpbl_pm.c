/*
 * arch/arm/mach-vexpress/sdpbl_pm.c - SDPBL power management support
 *
 * Created by:	Nicolas Pitre, October 2012
 * Copyright:	(C) 2012  Linaro Limited
 *
 * Some portions of this file were originally written by Achin Gupta
 * Copyright:   (C) 2012  ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/psci.h>
#include <asm/io.h>

#include <mach/map.h>
#include <mach/soc.h>
#include <mach/sdpbl.h>
#include <mach/sdp_smp.h>
#include <linux/of.h>

#include <linux/arm-cci.h>

//#define pr_debug	pr_err

//static u32 sdpbl_clustercpu2cpu[SDPBL_MAX_CLUSTERS][SDPBL_MAX_CPUS];
/*
 * We can't use regular spinlocks. In the switcher case, it is possible
 * for an outbound CPU to call power_down() after its inbound counterpart
 * is already live using the same logical CPU number which trips lockdep
 * debugging.
 */
static arch_spinlock_t sdpbl_pm_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static int sdpbl_pm_use_count[SDPBL_MAX_CPUS][SDPBL_MAX_CLUSTERS];

static struct sdp_mcpm_ops *sdp_mcpm_ops;

extern void sdpbl_resume(void);

struct sdpbl_cluster_info_t
{
	u32 ncpus;
	u32 firstcpu;
	void __iomem *resumereg;
};

static struct sdpbl_info_t
{
	u32 nclusters;
	struct sdpbl_cluster_info_t cluster[SDPBL_MAX_CLUSTERS];
	
} sdpbl_info;

void __init sdp_set_mcpm_ops(struct sdp_mcpm_ops *ops)
{
	sdp_mcpm_ops = ops;
}

static int get_cluster_info(void)
{
	struct device_node *clustern, *clusters, *cores;
	u32 n = 0, ncpu = 0;
	u32 resumereg;

	clusters = of_find_node_by_path("/clusters");
	if (!clusters) {
		pr_warn("Missing clusters node, bailing out\n");
		return -1;
	}

	sdpbl_info.nclusters = (u32) of_get_child_count(clusters);
	

	for_each_child_of_node(clusters, clustern) {
		struct sdpbl_cluster_info_t *cluster_info = &sdpbl_info.cluster[n];
		
		if(of_property_read_u32(clustern, "startreg", &resumereg))	{
			pr_warn("Cannot get startreg in DT\n");
		}
		else
		{
			cluster_info->resumereg = ioremap(resumereg, 0x100);
			if(cluster_info->resumereg == NULL)	{
				pr_err("startreg ioremap failed!!!\n");
				return -1;
			}
		}
		
		cores = of_get_child_by_name(clustern, "cores");
		if(!cores)	{
			pr_warn("Missing cores node, bailing out\n");
			return -1;
		}
		cluster_info->ncpus = (u32) of_get_child_count(cores);
		cluster_info->firstcpu = ncpu;
		ncpu += cluster_info->ncpus;
		n++;
	}
	return -1;
}

static u32 sdpbl_get_nb_clusters(void)
{
	if(sdpbl_info.nclusters == 0)
		return SDPBL_MAX_CLUSTERS;
	else
		return sdpbl_info.nclusters;
}

static u32 sdpbl_get_nb_cpus(unsigned int cluster)
{
	struct sdpbl_cluster_info_t *cluster_info = &sdpbl_info.cluster[cluster];

	if(cluster_info->ncpus == 0)
		return SDPBL_MAX_CPUS;
	else
		return cluster_info->ncpus;
}

static void sdp_set_cpu_wakeup_irq(unsigned int cluster, unsigned int cpu)
{
	unsigned int cpuid;
	int giccpuid;
	struct sdpbl_cluster_info_t *cluster_info = &sdpbl_info.cluster[cluster];

	cpuid = cluster_info->firstcpu + cpu;
	giccpuid = gic_get_cpu_id(cpuid);
	if(giccpuid >= 0)
		cpuid = (unsigned int) giccpuid;
	
	gic_send_sgi(cpuid, 0);		//wake up cpu
	return;
}


static int sdpbl_pm_power_up(unsigned int cpu, unsigned int cluster)
{
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= sdpbl_get_nb_clusters() ||
	    cpu >= sdpbl_get_nb_cpus(cluster))
		return -EINVAL;

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&sdpbl_pm_lock);

/*
	//If previous status cluster down, cluster power on
	if (!sdpbl_pm_use_count[0][cluster] &&
	    !sdpbl_pm_use_count[1][cluster] &&
	    !sdpbl_pm_use_count[2][cluster] &&
	    !sdpbl_pm_use_count[3][cluster])
		sdp_cluster_powerdown_enable(cluster, 0);
*/

	sdpbl_pm_use_count[cpu][cluster]++;
	if (sdpbl_pm_use_count[cpu][cluster] == 1) {
		sdp_mcpm_ops->write_resume_reg(cluster, cpu,
					      (u32) virt_to_phys(mcpm_entry_point));
		sdp_mcpm_ops->powerup(cluster, cpu);
		sdp_set_cpu_wakeup_irq(cluster, cpu);
	} else if (sdpbl_pm_use_count[cpu][cluster] != 2) {
		/*
		 * The only possible values are:
		 * 0 = CPU down
		 * 1 = CPU (still) up
		 * 2 = CPU requested to be up before it had a chance
		 *     to actually make itself down.
		 * Any other value is a bug.
		 */
		BUG();
	}

	arch_spin_unlock(&sdpbl_pm_lock);
	local_irq_enable();

	return 0;
}

static void sdpbl_pm_down(u64 residency)
{
	unsigned int mpidr, cpu, cluster;
	bool last_man = false, skip_wfi = false;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= sdpbl_get_nb_clusters() ||
	       cpu >= sdpbl_get_nb_cpus(cluster));

	__mcpm_cpu_going_down(cpu, cluster);

	arch_spin_lock(&sdpbl_pm_lock);
	BUG_ON(__mcpm_cluster_state(cluster) != CLUSTER_UP);
	sdpbl_pm_use_count[cpu][cluster]--;
	if (sdpbl_pm_use_count[cpu][cluster] == 0) {
		if (!sdpbl_pm_use_count[0][cluster] &&
		    !sdpbl_pm_use_count[1][cluster] &&
		    !sdpbl_pm_use_count[2][cluster] &&
		    !sdpbl_pm_use_count[3][cluster] &&
		    (!residency || residency > 5000)) {
			//sdp_cluster_powerdown_enable(cluster, 1);
			last_man = true;
		}
		/* sdp_mcpm_ops->powerdown(cluster, cpu); --> moved */
	} else if (sdpbl_pm_use_count[cpu][cluster] == 1) {
		/*
		 * A power_up request went ahead of us.
		 * Even if we do not want to shut this CPU down,
		 * the caller expects a certain state as if the WFI
		 * was aborted.  So let's continue with cache cleaning.
		 */
		skip_wfi = true;
	} else
		BUG();

	/*
	 * If the CPU is committed to power down, make sure
	 * the power controller will be in charge of waking it
	 * up upon IRQ, ie IRQ lines are cut from GIC CPU IF
	 * to the CPU by disabling the GIC CPU IF to prevent wfi
	 * from completing execution behind power controller back
	 */
	if (!skip_wfi)
	gic_cpu_if_down();

	if (last_man && __mcpm_outbound_enter_critical(cpu, cluster)) {
		arch_spin_unlock(&sdpbl_pm_lock);

		v7_exit_coherency_flush(all);

		cci_disable_port_by_cpu(mpidr);

		/*
		 * Ensure that both C & I bits are disabled in the SCTLR
		 * before disabling ACE snoops. This ensures that no
		 * coherency traffic will originate from this cpu after
		 * ACE snoops are turned off.
		 */
		cpu_proc_fin();

		__mcpm_outbound_leave_critical(cluster, CLUSTER_DOWN);
	} else {
		/*
		 * If last man then undo any setup done previously.
		 */
		if (last_man) {
			//sdp_spc_powerdown_enable(cluster, 0);
			//sdp_spc_set_global_wakeup_intr(0);
		}

		arch_spin_unlock(&sdpbl_pm_lock);

		v7_exit_coherency_flush(all);
	}

	__mcpm_cpu_down(cpu, cluster);

	/* Now we are prepared for power-down, do it: */
	
	if (!skip_wfi) {
		sdp_mcpm_ops->powerdown(cluster, cpu);
		dsb();
		wfi();
	}

	/* Not dead at this point?  Let our caller cope. */
}

static void sdpbl_pm_power_down(void)
{
	sdpbl_pm_down(0);
}

static void sdpbl_pm_suspend(u64 residency)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	sdp_mcpm_ops->write_resume_reg(cluster, cpu,
				      (u32) virt_to_phys(sdpbl_resume));

	sdpbl_pm_down(residency);
}

static void sdpbl_pm_powered_up(void)
{
	unsigned int mpidr, cpu, cluster;
	unsigned long flags;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= SDPBL_MAX_CLUSTERS ||
	       cpu >= sdpbl_get_nb_cpus(cluster));

	local_irq_save(flags);
	arch_spin_lock(&sdpbl_pm_lock);

	//If all core power-off in own cluster,
	if (!sdpbl_pm_use_count[0][cluster] &&
	    !sdpbl_pm_use_count[1][cluster] &&
	    !sdpbl_pm_use_count[2][cluster] &&
	    !sdpbl_pm_use_count[3][cluster]) {
//		sdp_spc_powerdown_enable(cluster, 0);
//		sdp_spc_set_global_wakeup_intr(0);
	}

	//if 
	if (!sdpbl_pm_use_count[cpu][cluster])
		sdpbl_pm_use_count[cpu][cluster] = 1;

//	sdp_write_resume_reg(cluster, cpu, 0);

	arch_spin_unlock(&sdpbl_pm_lock);
	local_irq_restore(flags);
}

static const struct mcpm_platform_ops sdpbl_pm_power_ops = {
	.power_up	= sdpbl_pm_power_up,
	.power_down	= sdpbl_pm_power_down,
	.suspend	= sdpbl_pm_suspend,
	.powered_up	= sdpbl_pm_powered_up,
};

static void __init sdpbl_pm_usage_count_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= sdpbl_get_nb_clusters() ||
	       cpu >= sdpbl_get_nb_cpus(cluster));

	sdpbl_pm_use_count[cpu][cluster] = 1;
}

extern void sdpbl_pm_power_up_setup(unsigned int affinity_level);

static int __init sdpbl_pm_init(void)
{
	int ret;

#if 0
	ret = psci_probe();
	if (!ret) {
		pr_debug("psci found. Aborting native init\n");
		return -ENODEV;
	}
#endif
	sdpbl_pm_usage_count_init();

	get_cluster_info();

	ret = mcpm_platform_register(&sdpbl_pm_power_ops);
	if (!ret)
		ret = mcpm_sync_init(sdpbl_pm_power_up_setup);
	if (!ret)
		pr_info("SDPBL power management initialized\n");
	return ret;
}

early_initcall(sdpbl_pm_init);
