/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_SDP_MAP_H
#define __ASM_ARCH_SDP_MAP_H

/* common macros */
#define VA_SFR(blk)		(VA_SFR_BASE(PA_##blk##_BASE) + SFR_OFFSET(PA_##blk##_BASE))

#define VA_SFR0_BASE		(SFR_VA)
#define VA_SFR_BASE(pa)		(VA_SFR0_BASE)

#if 0
/* for 2 banks structures */
#define VA_SFR1_BASE		(VA_SFR0_BASE + SFR0_SIZE)
#define VA_SFR_BASE(pa)		(SFR_BANK(pa) ? VA_SFR1_BASE : VA_SFR0_BASE)
#endif

/* for compatibility */
#define PA_IO_BASE0		SFR0_BASE
#define DIFF_IO_BASE0		(VA_SFR0_BASE - SFR0_BASE)

/* machine-dependent macros */
#define SFR_NR_BANKS		(1)
#define SFR_VA			(0xfe000000)
#define SFR0_BASE		(0x10000000)
#define SFR0_SIZE		(0x01000000)
#define SFR_OFFSET(pa)		(pa & 0x00ffffff)
#define SFR_BANK(x)		(0)

#endif /* __ASM_ARCH_SDP_MAP_H */
