/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct sdp_uart_port {
	unsigned int			pm_level;
	unsigned long			baudclk_rate;

	struct clk			*clk;
	struct clk			*baudclk;
	struct uart_port		port;
	int	debug_port;
	int debug_active;
	int *pdebug_active;
	unsigned int 	ulcon;
	unsigned int 	ucon;
	unsigned int 	ubrdiv;
	int irq_affinity;
	struct cpumask irq_affinity_mask;
};


#define portaddr(port, reg) ((port)->membase + (reg))
#define portaddrl(port, reg) ((unsigned long *)((port)->membase + (reg)))

#define rd_regb(port, reg) (__raw_readb(portaddr(port, reg)))
#define rd_regl(port, reg) (__raw_readl(portaddr(port, reg)))

#define wr_regb(port, reg, val) __raw_writeb(val, portaddr(port, reg))
#define wr_regl(port, reg, val) __raw_writel(val, portaddr(port, reg))

#ifdef CONFIG_SERIAL_SDP_DEBUG
extern void printascii(const char *);

static void dbg(const char *fmt, ...)
{
	va_list va;
	char buff[256];

	va_start(va, fmt);
	vsprintf(buff, fmt, va);
	va_end(va);

	printascii(buff);
}

#else
#define dbg(x...) do { } while (0)
#endif
