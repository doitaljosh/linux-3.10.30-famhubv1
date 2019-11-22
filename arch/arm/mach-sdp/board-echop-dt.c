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
#include <linux/delay.h>

#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include "common.h"

#include <mach/map.h>

#undef SFR0_BASE
#define SFR0_BASE		(0x30000000)
#undef SFR0_SIZE
#define SFR0_SIZE		(0x01000000)

//#undef SFR_NR_BANKS
//#define SFR_NR_BANKS	2

//#define SFR1_BASE		(0x30b00000)
//#define SFR1_SIZE		(0x00800000)
//#define VA_SFR1_BASE	(0xFE800000)

static struct map_desc sdp1106_io_desc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)VA_SFR0_BASE,
		.pfn		= __phys_to_pfn(SFR0_BASE),
		.length		= SFR0_SIZE,
		.type		= MT_DEVICE,
	},
#if 0
#if SFR_NR_BANKS == 2
	[1] = {
		.virtual	= VA_SFR1_BASE,
		.pfn		= __phys_to_pfn(SFR1_BASE),
		.length		= SFR1_SIZE,
		.type		= MT_DEVICE,
	},
#endif
#endif
};

static char const *sdp1106_dt_compat[] __initdata = {
	"samsung,sdp1106",
	NULL
};


#if 1
#define ASM_LDR_PC(offset)	(0xe59ff000 + (offset - 0x8))
static __cpuinit int flamengo_install_warp(unsigned int cpu)
{
	void __iomem *base;
	u32 instr_addr = 0x3C;

	base = ioremap(0x0, 512);

	writel_relaxed(ASM_LDR_PC(instr_addr), base);
	writel_relaxed(virt_to_phys(sdp_secondary_startup), base + instr_addr);
	dmb();

	iounmap(base);
	
	mdelay(10);

	return 0;
}
#endif


struct sdp_power_ops sdp1106_power_ops = {
	.powerup_cpu	= NULL,
	.powerdown_cpu	= NULL,
	.install_warp	= flamengo_install_warp,
};

static void __init sdp1106_init_early(void)
{
	/* XXX: NS kernel cannot change aux con.  */
	l2x0_of_init(0, 0xfdffffff);
}

#if !defined(CONFIG_SPARSEMEM)
extern phys_addr_t sdp_sys_mem0_size;
#endif

static void __init flamengo_map_io(void)
{
	iotable_init(sdp1106_io_desc, ARRAY_SIZE(sdp1106_io_desc));
	sdp_platsmp_init(&sdp1106_power_ops);

#if !defined(CONFIG_SPARSEMEM)
	sdp_sys_mem0_size = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_sys_mem0_size += meminfo.bank[1].size;
#endif
}

static void __init sdp1106_init_time(void)
{
#ifdef CONFIG_OF
	of_clk_init(NULL);
	clocksource_of_init();
#endif
}

DT_MACHINE_START(sdp1106_DT, "Samsung sdp1106(Flattened Device Tree)")
	/* Maintainer: */
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= flamengo_map_io,
	.init_machine	= sdp_dt_init_machine,
	.init_early	= sdp1106_init_early,
	.init_time = sdp1106_init_time,
	.dt_compat	= sdp1106_dt_compat,
	.restart	= sdp_restart,
MACHINE_END
