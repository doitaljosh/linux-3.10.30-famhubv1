/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGS_IIC_H
#define __ASM_ARCH_REGS_IIC_H

#define SDP_IICCON			0x00
#define SDP_IICSTAT			0x04
#define SDP_IICADD			0x08
#define SDP_IICDS			0x0C
#define SDP_IICCONE			0x10

#define SDP_IICIRQ_STAT			0x00
#define SDP_IICIRQ_EN			0x04

#define SDP_IICCON_ACKEN		(1 << 8)
#define SDP_IICCON_TXDIV_16		(0 << 7)
#define SDP_IICCON_TXDIV_256		(1 << 7)
#define SDP_IICCON_IRQEN		(1 << 6)
#define SDP_IICCON_SCALE_MASK		(0x3f)
#define SDP_IICCON_SCALE(x)		((x) & SDP_IICCON_SCALE_MASK)

#define SDP_IICSTAT_MASTER_TX		(3 << 6)
#define SDP_IICSTAT_MASTER_RX		(2 << 6)
#define SDP_IICSTAT_SLAVE_TX		(1 << 6)
#define SDP_IICSTAT_SLAVE_RX		(0 << 6)
#define SDP_IICSTAT_START		(1 << 5)
#define SDP_IICSTAT_BUSBUSY		(1 << 5)
#define SDP_IICSTAT_TXRXEN		(1 << 4)
#define SDP_IICSTAT_ARBITR		(1 << 3)
#define SDP_IICSTAT_ASSLAVE		(1 << 2)
#define SDP_IICSTAT_ADDR0		(1 << 1)
#define SDP_IICSTAT_LASTBIT		(1 << 0)

#define SDP_IICCONE_BUFFER_MASK		(0xff)
#define SDP_IICCONE_BUFFER(x)		(((x) & SDP_IICCONE_BUFFER_MASK) << 24)
#define SDP_IICCONE_FILTER_PRIORITY	(2 << 16)
#define SDP_IICCONE_FILTER_MAJORITY	(1 << 16)
#define SDP_IICCONE_FILTER_SAMPLING	(0 << 16)

#define SDP_IICCONE_SDAFILTER_PRIORITY	(2 << 14)
#define SDP_IICCONE_SDAFILTER_MAJORITY	(1 << 14)
#define SDP_IICCONE_SDAFILTER_SAMPLING	(0 << 14)

#define SDP_IICCONE_STOP_DETECT		(1 << 10)
#define SDP_IICCONE_FILTER_ON		(2 << 8)
#define SDP_IICCONE_FILTER_OFF		(1 << 8)
#define SDP_IICCONE_SDA_DELAY_MASK	(0xf)
#define SDP_IICCONE_SDA_DELAY(x)	((x) & SDP_IICCONE_SDA_DELAY_MASK)

#endif
