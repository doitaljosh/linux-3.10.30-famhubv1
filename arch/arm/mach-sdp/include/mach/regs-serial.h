/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGS_SERIAL_H
#define __ASM_ARCH_REGS_SERIAL_H

#define SDP_UART_BASE(x)		(0x10090A00 + (x) * 0x40)

#define SDP_ULCON			0x00
#define SDP_UCON			0x04
#define SDP_UFCON			0x08
#define SDP_UMCON			0x0C
#define SDP_UTRSTAT			0x10
#define SDP_UERSTAT			0x14
#define SDP_UFSTAT			0x18
#define SDP_UMSTAT			0x1C
#define SDP_UTXH			0x20
#define SDP_URXH			0x24
#define SDP_UBRDIV			0x28

#define SDP_ULCON_PNONE			(0 << 3)
#define SDP_ULCON_PEVEN			(5 << 3)
#define SDP_ULCON_PODD			(4 << 3)
#define SDP_ULCON_PMASK			(7 << 3)

#define SDP_ULCON_STOPB			(1 << 2)

#define SDP_ULCON_CS5			(0 << 0)
#define SDP_ULCON_CS6			(1 << 0)
#define SDP_ULCON_CS7			(2 << 0)
#define SDP_ULCON_CS8			(3 << 0)
#define SDP_ULCON_CSMASK		(3 << 0)

#define SDP_UCON_TXIRQ2MODE		(1 << 13)
#define SDP_UCON_RXIRQ2MODE		(1 << 12)
#define SDP_UCON_RXFIFO_TOI		(1 << 7)
#define SDP_UCON_LOOPBACK		(1 << 5)
#define SDP_UCON_SBREAK			(1 << 4)
#define SDP_UCON_TXIRQMODE		(1 << 2)
#define SDP_UCON_RXIRQMODE		(1 << 0)

#define SDP_UCON_DEFAULT		(SDP_UCON_RXIRQMODE | \
						SDP_UCON_TXIRQMODE | \
						SDP_UCON_RXFIFO_TOI | \
						SDP_UCON_RXIRQ2MODE)

#define SDP_UFCON_TXTRIG0		(0 << 6)
#define SDP_UFCON_TXTRIG4		(1 << 6)
#define SDP_UFCON_TXTRIG8		(2 << 6)
#define SDP_UFCON_TXTRIG12		(3 << 6)

#define SDP_UFCON_RXTRIG4		(0 << 4)
#define SDP_UFCON_RXTRIG8		(1 << 4)
#define SDP_UFCON_RXTRIG12		(2 << 4)
#define SDP_UFCON_RXTRIG16		(3 << 4)

#define SDP_UFCON_RESETBOTH		(3 << 1)
#define SDP_UFCON_RESETTX		(1 << 2)
#define SDP_UFCON_RESETRX		(1 << 1)
#define SDP_UFCON_FIFOMODE		(1 << 0)

#define SDP_UFCON_DEFAULT		(SDP_UFCON_FIFOMODE | \
						SDP_UFCON_RXTRIG4 | \
						SDP_UFCON_TXTRIG0)

#define SDP_UTRSTAT_ERRI		(1 << 6)
#define SDP_UTRSTAT_TXI			(1 << 5)
#define SDP_UTRSTAT_RXI			(1 << 4)
#define SDP_UTRSTAT_TXE			(1 << 2)
#define SDP_UTRSTAT_TXFE		(1 << 1)
#define SDP_UTRSTAT_RXDR		(1 << 0)

#define SDP_UERSTAT_BREAK		(1 << 3)
#define SDP_UERSTAT_FRAME		(1 << 2)
#define SDP_UERSTAT_PARITY		(1 << 1)
#define SDP_UERSTAT_OVERRUN		(1 << 0)

#define SDP_UERSTAT_ANY			(SDP_UERSTAT_OVERRUN | \
						SDP_UERSTAT_FRAME | \
						SDP_UERSTAT_BREAK)

#define SDP_UFSTAT_TXFIFO_MASK		(0xF << 4)
#define SDP_UFSTAT_TXFIFO_SHIFT		4
#define SDP_UFSTAT_TXFIFO_MAX		15
#define SDP_UFSTAT_RXFIFO_MASK		(0xF << 0)
#define SDP_UFSTAT_TXFIFO_FULL		(1 << 9)
#define SDP_UFSTAT_RXFIFO_FULL		(1 << 8)

#endif /* __ASM_ARCH_REGS_SERIAL_H */
