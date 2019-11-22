/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <linux/export.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include "common.h"
#include <mach/soc.h>

#include <mach/map.h>

#undef SFR0_BASE
#define SFR0_BASE		(0x10000000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x01000000)

#define SDP1404_MISC_BASE	0x10F90000
#define SDP1404_MISC2_BASE	0x10F98000
#define SDP1404_MISC_POWER_CTL	0x10
#define SDP1404_MISC_BOOTUP	0x3E0

static struct map_desc sdp1404_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
#if SFR_NR_BANKS == 2
	[1] = {
		.virtual	= VA_SFR1_BASE,
		.pfn		= __phys_to_pfn(SFR1_BASE),
		.length		= SFR1_SIZE,
		.type		= MT_DEVICE,
	},
#endif
};

static char const *sdp1404_dt_compat[] __initdata = {
	"samsung,sdp1404",
	NULL
};

struct sdp_power_ops sdp1404_power_ops = {
	.powerup_cpu	= NULL,
	.powerdown_cpu	= NULL,
	.install_warp	= NULL,
};

#if !defined(CONFIG_SPARSEMEM)
extern phys_addr_t sdp_sys_mem0_size;
#endif

static void __init sdp1404_reserve(void)
{
	/* EHCI workaround */
	const phys_addr_t reserve_addr = (0xffffffff - (PAGE_SIZE * 2) + 1);
	/* Hawk-P prevent error direction address */
	const phys_addr_t reserve_addr2 = 0x117000000ULL;
	
	if (pfn_valid(reserve_addr >> PAGE_SHIFT)) {
		pr_info("sdp1404: reserve last 2 pages on 32bit boundary.\n");
		memblock_reserve(reserve_addr, PAGE_SIZE * 2);
	}

	if (pfn_valid(reserve_addr2 >> PAGE_SHIFT)) {
		pr_info("sdp1404: reserve 1 pages for prevent error response.\n");
		memblock_reserve(reserve_addr2, PAGE_SIZE);
	}
#if 0
	if(get_sdp_board_type() == SDP_BOARD_AV)
	{
		pr_info("sdp1404: reserve 1GB memory.\n");
		memblock_reserve(0xC0000000, 0x40000000);
	}
#endif
}

#ifdef CONFIG_MCPM
static void __iomem *pwrbase[2];

static int __cpuinit hawkp_mcpm_write_resume_reg(unsigned int cluster, unsigned int cpu, unsigned int value)
{
	void __iomem *base = pwrbase[0];
	cpu = cluster * 4 + cpu;
	writel_relaxed(value, (void *) ((u32) base + SDP1404_MISC_BOOTUP + cpu * 4));
	dmb();	
	udelay(1);	
	return 0;
}

static int __cpuinit hawkp_mcpm_powerup(unsigned int cluster, unsigned int cpu)
{
	void __iomem *base = pwrbase[cluster];
	u32 mask, val;

	mask = 0x1;
	val = readl((void *)((u32) base + SDP1404_MISC_POWER_CTL + cpu*4)) & ~mask;
	val |= mask;
	writel(val, (void *)((u32) base + SDP1404_MISC_POWER_CTL + cpu*4));

	return 0;
}

static int __cpuinit hawkp_mcpm_powerdown(unsigned int cluster, unsigned int cpu)
{
	void __iomem *base = pwrbase[cluster];
	u32 mask, val;

	mask = 0x10;
	val = readl((void *)((u32) base + SDP1404_MISC_POWER_CTL + cpu*4)) & ~mask;
	val |= mask;
	writel(val, (void *)((u32) base + SDP1404_MISC_POWER_CTL + cpu*4));

	return 0;
}

struct sdp_mcpm_ops sdp1404_mcpm_ops = {
	.write_resume_reg	= hawkp_mcpm_write_resume_reg,
	.powerup	= hawkp_mcpm_powerup,
	.powerdown = hawkp_mcpm_powerdown,
};

#endif

static void __init hawkp_map_io(void)
{
	iotable_init(sdp1404_io_desc, ARRAY_SIZE(sdp1404_io_desc));
	sdp_platsmp_init(&sdp1404_power_ops);
#if defined(CONFIG_MCPM)
	sdp_set_mcpm_ops(&sdp1404_mcpm_ops);
	pwrbase[0] = ioremap(SDP1404_MISC_BASE, 0x400);
	pwrbase[1] = ioremap(SDP1404_MISC2_BASE, 0x400);
	BUG_ON(!pwrbase[0] || !pwrbase[1]);
#endif

#if !defined(CONFIG_SPARSEMEM)
	sdp_sys_mem0_size = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_sys_mem0_size += meminfo.bank[1].size;
#endif
}



static void __init sdp1404_init_time(void)
{
#ifdef CONFIG_OF
	of_clk_init(NULL);
	clocksource_of_init();
#endif
}

DT_MACHINE_START(SDP1404_DT, "Samsung SDP1404(Flattened Device Tree)")
	/* Maintainer: */
	.init_irq	= sdp_init_irq,
	.smp		= smp_ops(sdp_smp_ops),
#ifdef CONFIG_MCPM
	.smp_init	= sdp_smp_init_ops,
#endif
	.map_io		= hawkp_map_io,
	.init_machine	= sdp_dt_init_machine,
	.init_time = sdp1404_init_time,
	.dt_compat	= sdp1404_dt_compat,
	.restart	= sdp_restart,
	.reserve	= sdp1404_reserve,
MACHINE_END
