/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/sdp_smp.h>
#include <mach/map.h>
#include "common.h"

static struct map_desc sdp1302_io_desc[] __initdata = {
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

static char const *sdp1302_dt_compat[] __initdata = {
	"samsung,sdp1302",
	NULL
};

struct sdp_power_ops sdp1302_power_ops = {
	.powerup_cpu	= NULL,
	.powerdown_cpu	= NULL,
	.install_warp	= NULL,
};

static void __init sdp1302_init_early(void)
{
	/* XXX: NS kernel cannot change aux con.  */
	l2x0_of_init(0, 0xfdffffff);
}

#if !defined(CONFIG_SPARSEMEM)
extern phys_addr_t sdp_sys_mem0_size;
#endif

static void __init golfs_map_io(void)
{
	iotable_init(sdp1302_io_desc, ARRAY_SIZE(sdp1302_io_desc));
	sdp_platsmp_init(&sdp1302_power_ops);

#if !defined(CONFIG_SPARSEMEM)
	sdp_sys_mem0_size = meminfo.bank[0].size;
	if(meminfo.nr_banks > 1)
		sdp_sys_mem0_size += meminfo.bank[1].size;
#endif
}

DT_MACHINE_START(SDP1302_DT, "Samsung SDP1302(Flattened Device Tree)")
	/* Maintainer: */
	.smp		= smp_ops(sdp_smp_ops),
	.map_io		= golfs_map_io,
	.init_machine	= sdp_dt_init_machine,
	.init_early	= sdp1302_init_early,
	.init_time	= sdp_init_time,
	.dt_compat	= sdp1302_dt_compat,
	.restart	= sdp_restart,
MACHINE_END

