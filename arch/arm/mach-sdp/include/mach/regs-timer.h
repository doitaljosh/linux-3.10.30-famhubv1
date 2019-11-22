/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGS_TIMER_H
#define __ASM_ARCH_REGS_TIMER_H

#define SDP_TIMER_REG(x)		(x)
#define SDP_TIMER_CH(reg, ch)		(SDP_TIMER_REG(reg) + (ch) * 0x10)

#define SDP_TMCON(x)			SDP_TIMER_CH(0x00, x)
#define SDP_TMDATA(x)			SDP_TIMER_CH(0x04, x)
#define SDP_TMCNT(x)			SDP_TIMER_CH(0x08, x)
#define SDP_TMCONE(x)			SDP_TIMER_CH(0x0C, x)
#define SDP_TMDATA64L(x)		SDP_TIMER_CH(0x80, x)
#define SDP_TMDATA64H(x)		SDP_TIMER_CH(0x84, x)
#define SDP_TMCNT64L(x)			SDP_TIMER_CH(0x88, x)
#define SDP_TMCNT64H(x)			SDP_TIMER_CH(0x8C, x)
#define SDP_TMSTAT				(0x40)
/* TMCON */
#define SDP_TMCON_64BIT_DOWN		(0 << 5)
#define SDP_TMCON_64BIT_UP		(1 << 5)
#define SDP_TMCON_64BIT			(0 << 4)
#define SDP_TMCON_16BIT			(1 << 4)
#define SDP_TMCON_MUX4			(0 << 2)
#define SDP_TMCON_MUX8			(1 << 2)
#define SDP_TMCON_MUX16			(2 << 2)
#define SDP_TMCON_MUX32			(3 << 2)
#define SDP_TMCON_IRQ_DISABLE		(0 << 1)
#define SDP_TMCON_IRQ_ENABLE		(1 << 1)
#define SDP_TMCON_STOP			(0 << 0)
#define SDP_TMCON_RUN			(1 << 0)

/* TMDATA */
#define SDP_TMDATA_PRESCALING(x)	((x) << 16)

/* TMCONE */
#define SDP_TMCONE_IRQMASK		(1 << 4)


#endif /* __ASM_ARCH_REGS_TIMER_H */

