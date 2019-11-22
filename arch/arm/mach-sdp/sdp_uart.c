/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if defined(CONFIG_SERIAL_SDP_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/of.h>

#include <asm/irq.h>

#include <mach/map.h>
#include <mach/regs-serial.h>

#include "sdp_uart.h"

#define SDP_SERIAL_NAME		"ttyS"
#define SDP_SERIAL_MAJOR	204
#define SDP_SERIAL_MINOR	64

#define SDP_SERIALD_NAME	"ttySD"
#define SDP_SERIALD_MAJOR	SDP_SERIAL_MAJOR
#define SDP_SERIALD_MINOR	(74)

/* flag to ignore all characters coming in */
#define RXSTAT_DUMMY_READ (0x10000000)

#define SDP_SERIAL_DEBUG

#define CONFIG_SERIAL_SDP_UARTS	5


static inline struct sdp_uart_port *to_ourport(struct uart_port *port)
{
	return container_of(port, struct sdp_uart_port, port);
}

static inline const char *sdp_serial_portname(struct uart_port *port)
{
	return to_platform_device(port->dev)->name;
}

static unsigned int sdp_serial_txempty_nofifo(struct uart_port *port)
{
	return rd_regl(port, SDP_UTRSTAT) & SDP_UTRSTAT_TXE;
}

static void sdp_serial_stop_tx(struct uart_port *port)
{
}

static void sdp_serial_tx_chars(struct sdp_uart_port *ourport);

static void sdp_serial_start_tx(struct uart_port *port)
{
	struct sdp_uart_port *ourport = to_ourport(port);
	unsigned int ucon;

	if((!ourport->debug_port) && *ourport->pdebug_active)	{	//If debug port is activated, user port is ignored.
		struct circ_buf *xmit = &port->state->xmit;
		do {
			xmit->tail = (xmit->tail + 1) & ((int) UART_XMIT_SIZE - 1);
			port->icount.tx++;
			if (uart_circ_empty(xmit)) break;
		} while (1);
		return;
	}

	ucon = rd_regl(port, SDP_UCON);
	ucon |= SDP_UCON_TXIRQ2MODE;
	wr_regl(port, SDP_UCON, ucon);

	sdp_serial_tx_chars(ourport);
}

static void sdp_serial_stop_rx(struct uart_port *port)
{
}

static void sdp_serial_enable_ms(struct uart_port *port)
{
}

static int sdp_serial_rx_fifocnt(struct sdp_uart_port *ourport,
				     unsigned long ufstat)
{
	if (ufstat & SDP_UFSTAT_RXFIFO_FULL)
		return (int) ourport->port.fifosize;

	return ufstat & SDP_UFSTAT_RXFIFO_MASK;
}

static void sdp_serial_rx_chars(struct sdp_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	unsigned int ch, flag, ufstat, uerstat;
	int max_count = 64;

	while (max_count-- > 0) {
		ufstat = rd_regl(port, SDP_UFSTAT);

		if (sdp_serial_rx_fifocnt(ourport, ufstat) == 0)
			break;

		uerstat = rd_regl(port, SDP_UERSTAT);
		ch = rd_regb(port, SDP_URXH);

		/* insert the character into the buffer */

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(uerstat & SDP_UERSTAT_ANY)) {
			/* check for break */
			if (uerstat & SDP_UERSTAT_BREAK) {
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			}

			if (uerstat & SDP_UERSTAT_FRAME)
				port->icount.frame++;
			if (uerstat & SDP_UERSTAT_OVERRUN)
				port->icount.overrun++;

			uerstat &= port->read_status_mask;

			if (uerstat & SDP_UERSTAT_BREAK)
				flag = TTY_BREAK;
			else if (uerstat & SDP_UERSTAT_PARITY)
				flag = TTY_PARITY;
			else if (uerstat & (SDP_UERSTAT_FRAME |
					    SDP_UERSTAT_OVERRUN))
				flag = TTY_FRAME;
		}

#ifdef SUPPORT_SYSRQ
		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;
#endif

		uart_insert_char(port, uerstat, SDP_UERSTAT_OVERRUN,
				 ch, flag);

 ignore_char:
		continue;
	}
	tty_flip_buffer_push(&port->state->port);
}

static void sdp_serial_tx_chars(struct sdp_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	struct circ_buf *xmit = &port->state->xmit;
	int count = 256;

	if (port->x_char) {
		wr_regb(port, SDP_UTXH, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	/*
	 * if there isn't anything more to transmit, or the uart is now
	 * stopped, disable the uart and exit
	 */

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sdp_serial_stop_tx(port);
		return;
	}

	/* try and drain the buffer... */

	while (!uart_circ_empty(xmit) && count-- > 0) {
		if (rd_regl(port, SDP_UFSTAT) & SDP_UFSTAT_TXFIFO_FULL)
			break;

		wr_regb(port, SDP_UTXH, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS) {
		spin_unlock(&port->lock);
		uart_write_wakeup(port);
		spin_lock(&port->lock);
	}

	if (uart_circ_empty(xmit))
		sdp_serial_stop_tx(port);
}

static irqreturn_t sdp_serial_handle_irq(int irq, void *id)
{
	struct sdp_uart_port *ourport = id;
	struct uart_port *port = &ourport->port;
	unsigned int pend = rd_regl(port, SDP_UTRSTAT);
	unsigned long flags;

	if((!ourport->debug_port) && *ourport->pdebug_active)	//If debug port is activated, user port is ignored.
		return IRQ_HANDLED;

	spin_lock_irqsave(&port->lock, flags);

	if (pend & SDP_UTRSTAT_RXI) {
		sdp_serial_rx_chars(ourport);
		wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI);
	}

	if (pend & SDP_UTRSTAT_TXI) {
		sdp_serial_tx_chars(ourport);
		wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_TXI);
	}

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static unsigned int sdp_serial_tx_empty(struct uart_port *port)
{
	unsigned long utrstat = rd_regl(port, SDP_UTRSTAT);
	unsigned long ufcon = rd_regl(port, SDP_UFCON);

	if (ufcon & SDP_UFCON_FIFOMODE)
		return !!(utrstat & SDP_UTRSTAT_TXFE);

	return sdp_serial_txempty_nofifo(port);
}

/* no modem control lines */
static unsigned int sdp_serial_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR;
}

static void sdp_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void sdp_serial_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int ucon;

	spin_lock_irqsave(&port->lock, flags);

	ucon = rd_regl(port, SDP_UCON);

	if (break_state)
		ucon |= SDP_UCON_SBREAK;
	else
		ucon &= (u32) ~SDP_UCON_SBREAK;

	wr_regl(port, SDP_UCON, ucon);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sdp_serial_shutdown(struct uart_port *port)
{
	struct sdp_uart_port *ourport = to_ourport(port);

	free_irq(port->irq, ourport);

	if(ourport->debug_port)
	{
		while((rd_regl(port, SDP_UTRSTAT) & (SDP_UTRSTAT_TXFE | SDP_UTRSTAT_TXE))
			!= (SDP_UTRSTAT_TXFE | SDP_UTRSTAT_TXE));	//wait to empty of tx,rx buffer
		wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI | SDP_UTRSTAT_TXI | SDP_UTRSTAT_ERRI);	//Clear Flags
		wr_regl(port, SDP_ULCON, ourport->ulcon);
		wr_regl(port, SDP_UCON, ourport->ucon);
		wr_regl(port, SDP_UBRDIV, ourport->ubrdiv);
		ourport->debug_active = false;
	}
}

static int sdp_serial_startup(struct uart_port *port)
{
	struct sdp_uart_port *ourport = to_ourport(port);
	int ret;

	if(ourport->debug_port)	//Clear RX,TX buffer if debug port.
	{
		printk("Debug port startup \n");
		while((rd_regl(port, SDP_UTRSTAT) & (SDP_UTRSTAT_TXFE | SDP_UTRSTAT_TXE))
			!= (SDP_UTRSTAT_TXFE | SDP_UTRSTAT_TXE));	//wait to empty of tx,rx buffer
		wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI | SDP_UTRSTAT_TXI | SDP_UTRSTAT_ERRI);	//Clear Flags
		ourport->ulcon = rd_regl(port, SDP_ULCON);
		ourport->ucon = rd_regl(port, SDP_UCON);
		ourport->ubrdiv = rd_regl(port, SDP_UBRDIV);
		ourport->debug_active = true;
	}

	ret = request_irq(port->irq, sdp_serial_handle_irq, IRQF_SHARED,
			  sdp_serial_portname(port), ourport);
	if (ret) {
		dev_err(port->dev, "cannot get irq %d\n", port->irq);
		return ret;
	}

	if(!cpumask_empty(&ourport->irq_affinity_mask)) {
		irq_set_affinity(port->irq, &ourport->irq_affinity_mask);
	} else if(cpu_online((u32) ourport->irq_affinity)) {
		irq_set_affinity(port->irq, cpumask_of((u32) ourport->irq_affinity));
	}

	wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI);

	return ret;
}

static void sdp_serial_pm(struct uart_port *port, unsigned int level,
			      unsigned int old)
{
}

static void sdp_serial_set_termios(struct uart_port *port,
				       struct ktermios *termios,
				       struct ktermios *old)
{
	struct sdp_uart_port *ourport = to_ourport(port);
	struct clk *clk = ERR_PTR(-EINVAL);
	unsigned long flags;
	unsigned int baud, quot;
	unsigned int ulcon;
	unsigned long rate;

	/*
	 * We don't support modem control lines.
	 */
	termios->c_cflag &= (u32) ~(HUPCL | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/*
	 * Ask the core to calculate the divisor for us.
	 */

	baud = uart_get_baud_rate(port, termios, old, 0, 115200 * 8);

	clk = clk_get(port->dev, "apb_pclk");
	if (IS_ERR(clk))
		return;

	rate = clk_get_rate(clk);
	quot = (rate / (baud / 10 * 16) - 5) / 10;

	/* check to see if we need  to change clock source */

	if (ourport->baudclk != clk) {
		if (!IS_ERR(ourport->baudclk)) {
			clk_disable_unprepare(ourport->baudclk);
			ourport->baudclk = ERR_PTR(-EINVAL);
		}

		clk_prepare_enable(clk);

		ourport->baudclk = clk;
		ourport->baudclk_rate = clk ? clk_get_rate(clk) : 0;
	}

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		ulcon = SDP_ULCON_CS5;
		break;
	case CS6:
		ulcon = SDP_ULCON_CS6;
		break;
	case CS7:
		ulcon = SDP_ULCON_CS7;
		break;
	case CS8:
	default:
		ulcon = SDP_ULCON_CS8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		ulcon |= SDP_ULCON_STOPB;

	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD)
			ulcon |= SDP_ULCON_PODD;
		else
			ulcon |= SDP_ULCON_PEVEN;
	} else {
		ulcon |= SDP_ULCON_PNONE;
	}

	spin_lock_irqsave(&port->lock, flags);

	wr_regl(port, SDP_ULCON, ulcon);
	wr_regl(port, SDP_UBRDIV, quot);

	/* Update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* Which character status flags are we interested in? */
	port->read_status_mask = SDP_UERSTAT_OVERRUN;
	if (termios->c_iflag & INPCK) {
		port->read_status_mask |=
			(SDP_UERSTAT_FRAME | SDP_UERSTAT_PARITY);
	}

	/* Which character status flags should we ignore? */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SDP_UERSTAT_OVERRUN;
	if (termios->c_iflag & IGNBRK && termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SDP_UERSTAT_FRAME;

	/* Ignore all characters if CREAD is not set. */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= RXSTAT_DUMMY_READ;

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *sdp_serial_type(struct uart_port *port)
{
	return "SDP";
}

#define MAP_SIZE (0x40)

static void sdp_serial_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, MAP_SIZE);
}

static int sdp_serial_request_port(struct uart_port *port)
{
	const char *name = sdp_serial_portname(port);
	return request_mem_region(port->mapbase, MAP_SIZE, name) ? 0 : -EBUSY;
}

static void sdp_serial_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_SDP;
}

/* verify the new serial_struct (for TIOCSSERIAL) */
static int sdp_serial_verify_port(struct uart_port *port,
					struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops sdp_serial_ops = {
	.pm		= sdp_serial_pm,
	.tx_empty	= sdp_serial_tx_empty,
	.get_mctrl	= sdp_serial_get_mctrl,
	.set_mctrl	= sdp_serial_set_mctrl,
	.stop_tx	= sdp_serial_stop_tx,
	.start_tx	= sdp_serial_start_tx,
	.stop_rx	= sdp_serial_stop_rx,
	.enable_ms	= sdp_serial_enable_ms,
	.break_ctl	= sdp_serial_break_ctl,
	.startup	= sdp_serial_startup,
	.shutdown	= sdp_serial_shutdown,
	.set_termios	= sdp_serial_set_termios,
	.type		= sdp_serial_type,
	.release_port	= sdp_serial_release_port,
	.request_port	= sdp_serial_request_port,
	.config_port	= sdp_serial_config_port,
	.verify_port	= sdp_serial_verify_port,
};

static struct sdp_uart_port sdp_serial_ports[CONFIG_SERIAL_SDP_UARTS] = {
	[0] = {
		.port = {
			.lock		= __SPIN_LOCK_UNLOCKED(
						sdp_serial_ports[0].port.lock),
			.iotype		= UPIO_MEM,
			.uartclk	= 0,
			.fifosize	= 16,
			.ops		= &sdp_serial_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 0,
		}
	},
	[1] = {
		.port = {
			.lock		= __SPIN_LOCK_UNLOCKED(
						sdp_serial_ports[1].port.lock),
			.iotype		= UPIO_MEM,
			.uartclk	= 0,
			.fifosize	= 16,
			.ops		= &sdp_serial_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 1,
		}
	},
	[2] = {
		.port = {
			.lock		= __SPIN_LOCK_UNLOCKED(
						sdp_serial_ports[2].port.lock),
			.iotype		= UPIO_MEM,
			.uartclk	= 0,
			.fifosize	= 16,
			.ops		= &sdp_serial_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 2,
		}
	},
	[3] = {
		.port = {
			.lock		= __SPIN_LOCK_UNLOCKED(
						sdp_serial_ports[3].port.lock),
			.iotype		= UPIO_MEM,
			.uartclk	= 0,
			.fifosize	= 16,
			.ops		= &sdp_serial_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 3,
		}
	},
	[4] = {
		.port = {
			.lock		= __SPIN_LOCK_UNLOCKED(
						sdp_serial_ports[4].port.lock),
			.iotype		= UPIO_MEM,
			.uartclk	= 0,
			.fifosize	= 16,
			.ops		= &sdp_serial_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 4,
		}
	},
};

static struct sdp_uart_port sdp_serial_ports_debug[CONFIG_SERIAL_SDP_UARTS];

static struct uart_port *cons_uart;

static int sdp_serial_console_txrdy(struct uart_port *port)
{
	unsigned long ufstat;

	ufstat = rd_regl(port, SDP_UFSTAT);
	return (ufstat & SDP_UFSTAT_TXFIFO_FULL) ? 0 : 1;
}

static void sdp_serial_resetport(struct uart_port *port);

static void sdp_serial_console_putchar(struct uart_port *port, int ch)
{
	int ntimeout = 100000;
	
	while (!sdp_serial_console_txrdy(port))	{
		if(ntimeout-- < 0)	{
			sdp_serial_resetport(port);
			break;		
		}
	}

	wr_regb(cons_uart, SDP_UTXH, (u8) ch);
}

static void sdp_serial_console_write(struct console *co,
			const char *s, unsigned int count)
{
	struct sdp_uart_port *ourport = &sdp_serial_ports[co->index];
	struct uart_port *port = &ourport->port;
	unsigned long flags;
	
	if(*ourport->pdebug_active)	//If debug port is activated, console is ignored.
		return;

	spin_lock_irqsave(&port->lock, flags);
	
	uart_console_write(cons_uart, s, count, sdp_serial_console_putchar);

	spin_unlock_irqrestore(&port->lock, flags);	
}

static void __init sdp_serial_get_options(struct uart_port *port, int *baud,
			   int *parity, int *bits)
{
	unsigned int ulcon;
	unsigned int ucon;
	unsigned int ubrdiv;
	struct clk *clk;
	unsigned long rate;

	ulcon  = rd_regl(port, SDP_ULCON);
	ucon   = rd_regl(port, SDP_UCON);
	ubrdiv = rd_regl(port, SDP_UBRDIV);

	if ((ucon & 0xf) == 0)
		return;

	/* consider the serial port configured if the tx/rx mode set */

	switch (ulcon & SDP_ULCON_CSMASK) {
	case SDP_ULCON_CS5:
		*bits = 5;
		break;
	case SDP_ULCON_CS6:
		*bits = 6;
		break;
	case SDP_ULCON_CS7:
		*bits = 7;
		break;
	default:
	case SDP_ULCON_CS8:
		*bits = 8;
		break;
	}

	switch (ulcon & SDP_ULCON_PMASK) {
	case SDP_ULCON_PEVEN:
		*parity = 'e';
		break;

	case SDP_ULCON_PODD:
		*parity = 'o';
		break;

	case SDP_ULCON_PNONE:
	default:
		*parity = 'n';
	}

	/* now calculate the baud rate */
	clk = clk_get(port->dev, "apb_pclk");
	if (!IS_ERR(clk))
		rate = clk_get_rate(clk);
	else
		rate = 200000000;

	*baud = (int) rate / (16 * ((int) ubrdiv + 1));
}

static int __init sdp_serial_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 119200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/* is this a valid port */

	if (co->index == -1 || co->index >= CONFIG_SERIAL_SDP_UARTS)
		co->index = 0;

	port = &sdp_serial_ports[co->index].port;

	/* is the port configured? */

	if (port->mapbase == 0x0)
		return -ENODEV;

	cons_uart = port;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		sdp_serial_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver sdp_serial_uart;

static struct uart_driver sdp_serial_uart_debug = {
	.owner                  = THIS_MODULE,
	.dev_name               = SDP_SERIALD_NAME,
	.nr                     = CONFIG_SERIAL_SDP_UARTS,
	.cons                   = NULL,
	.driver_name            = SDP_SERIALD_NAME,
	.major                  = SDP_SERIALD_MAJOR,
	.minor                  = SDP_SERIALD_MINOR,
};


static struct console sdp_serial_console = {
	.name		= SDP_SERIAL_NAME,
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.write		= sdp_serial_console_write,
	.setup		= sdp_serial_console_setup,
	.data		= &sdp_serial_uart,
};

#ifdef CONFIG_SERIAL_SDP_CONSOLE

static int __init sdp_serial_console_init(void)
{
	register_console(&sdp_serial_console);
	return 0;
}
console_initcall(sdp_serial_console_init);

#define SDP_SERIAL_CONSOLE	&sdp_serial_console
#else
#define SDP_SERIAL_CONSOLE	NULL
#endif

static struct uart_driver sdp_serial_uart = {
	.owner		= THIS_MODULE,
	.driver_name	= "sdp-uart",
	.nr		= CONFIG_SERIAL_SDP_UARTS,
	.cons		= SDP_SERIAL_CONSOLE,
	.dev_name	= SDP_SERIAL_NAME,
	.major		= SDP_SERIAL_MAJOR,
	.minor		= SDP_SERIAL_MINOR,
};

static void sdp_serial_resetport(struct uart_port *port)
{
	unsigned long ucon = rd_regl(port, SDP_UCON);

	wr_regl(port, SDP_UCON, ucon | SDP_UCON_DEFAULT);

	/* reset both fifos */
	wr_regl(port, SDP_UFCON, SDP_UFCON_DEFAULT | SDP_UFCON_RESETBOTH);
	wr_regl(port, SDP_UFCON, SDP_UFCON_DEFAULT);

	/* some delay is required after fifo reset */
	udelay(1);
}

static int sdp_serial_init_port(struct sdp_uart_port *ourport,
				    struct platform_device *platdev)
{
	struct uart_port *port = &ourport->port;
	struct resource *res;
	struct device *dev;
	int ret;

	if (platdev == NULL)
		return -ENODEV;

	dev = &platdev->dev;

	if (port->mapbase != 0)
		return 0;

	/* setup info for port */
	port->dev = dev;
	port->uartclk = 1;

	/* sort our the physical and virtual addresses for each UART */

	res = platform_get_resource(platdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "failed to find memory resource for uart\n");
		return -EINVAL;
	}

	port->mapbase = res->start;
	port->membase = devm_request_and_ioremap(dev, res);
	if (!port->membase) {
		dev_err(dev, "ioremap failed\n");
		return -ENODEV;
	}

		
	ret = platform_get_irq(platdev, 0);
	if (ret < 0) {
		dev_err(dev, "failed to find irq resource for uart\n");
		return -EINVAL;
	}

	port->irq = (u32) ret;

	ourport->clk = clk_get(port->dev, "apb_pclk");
	if (IS_ERR(ourport->clk))
		dev_err(port->dev, "failed to find uart clock\n");
	else
		clk_prepare_enable(ourport->clk);

	of_property_read_u32(dev->of_node, "irq-affinity", &ourport->irq_affinity);
	of_property_read_u32_array(dev->of_node, "irq-affinity-mask", (u32 *)cpumask_bits(&ourport->irq_affinity_mask), BITS_TO_LONGS(NR_CPUS));
	
	/* reset the fifos (and setup the uart) */
	sdp_serial_resetport(port);

	return 0;
}

static int probe_index;

static int sdp_serial_probe(struct platform_device *pdev)
{
	struct sdp_uart_port *ourport, *dbgport;
	int ret;

	ourport = &sdp_serial_ports[probe_index];
	dbgport = &sdp_serial_ports_debug[probe_index];
	ourport->pdebug_active = &dbgport->debug_active;
	ourport->baudclk = ERR_PTR(-EINVAL);

	ret = sdp_serial_init_port(ourport, pdev);
	if (ret < 0)
		goto probe_err;

	memcpy(dbgport, ourport, sizeof(struct sdp_uart_port));
	ourport->debug_port = false;
	dbgport->debug_port = true;
	dbgport->debug_active = false;

	uart_add_one_port(&sdp_serial_uart, &ourport->port);
	platform_set_drvdata(pdev, &ourport->port);

	uart_add_one_port(&sdp_serial_uart_debug, &dbgport->port);
//	platform_set_drvdata(pdev, &dbgport->port);

	probe_index++;

	/* TODO */
	/*
	ret = device_create_file(&pdev->dev, &dev_attr_clock_source);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to add clock source attr.\n");

	ret = sdp_serial_cpufreq_register(ourport);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to add cpufreq notifier\n");
	*/

	return 0;

 probe_err:
	return ret;
}

static int sdp_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port =
		(struct uart_port *)dev_get_drvdata(&pdev->dev);

	if (!port)
		return 0;

	/* TODO */
	/*
	sdp_serial_cpufreq_deregister(to_ourport(port));
	device_remove_file(&pdev->dev, &dev_attr_clock_source);
	*/
	uart_remove_one_port(&sdp_serial_uart, port);

	return 0;
}

/* UART power management code */
#ifdef CONFIG_PM_SLEEP
static int sdp_serial_suspend(struct device *dev)
{
	struct uart_port *port =
		(struct uart_port *)dev_get_drvdata(dev);
	struct sdp_uart_port *ourport = to_ourport(port);

	if (!port)
		return 0;

	ourport->ulcon = rd_regl(port, SDP_ULCON);
	ourport->ucon = rd_regl(port, SDP_UCON);
	ourport->ubrdiv = rd_regl(port, SDP_UBRDIV);
	uart_suspend_port(&sdp_serial_uart, port);

	return 0;
}

static int sdp_serial_resume(struct device *dev)
{
	struct uart_port *port =
		(struct uart_port *)dev_get_drvdata(dev);
	struct sdp_uart_port *ourport = to_ourport(port);

	if (!port)
		return 0;

	wr_regl(port, SDP_ULCON, ourport->ulcon);
	wr_regl(port, SDP_UCON, ourport->ucon);
	wr_regl(port, SDP_UBRDIV, ourport->ubrdiv);

	/* TODO */
	/*clk_prepare_enable(ourport->clk);*/
	sdp_serial_resetport(port);
	wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI | SDP_UTRSTAT_TXI | SDP_UTRSTAT_ERRI);	//Clear Flags
	/*clk_disable_unprepare(ourport->clk);*/
	uart_resume_port(&sdp_serial_uart, port);
	
	return 0;
}

static const struct dev_pm_ops sdp_serial_pm_ops = {
	.suspend_late = sdp_serial_suspend,
	.resume_early = sdp_serial_resume,
};
#define SERIAL_SDP_PM_OPS	(&sdp_serial_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define SERIAL_SDP_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_device_id sdp_serial_driver_ids[] = {
	{ .name = "sdp-uart" },
	{ },
};
MODULE_DEVICE_TABLE(platform, sdp_serial_driver_ids);

static const struct of_device_id sdp_serial_dt_match[] = {
	{ .compatible = "samsung,sdp-uart" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_serial_dt_match);

static struct platform_driver sdp_serial_driver = {
	.probe		= sdp_serial_probe,
	.remove		= sdp_serial_remove,
	.id_table	= sdp_serial_driver_ids,
	.driver		= {
		.name	= "sdp-uart",
		.owner	= THIS_MODULE,
		.pm	= SERIAL_SDP_PM_OPS,
#ifdef CONFIG_OF
		.of_match_table	= sdp_serial_dt_match,
#endif
	},
};

static int __init sdp_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&sdp_serial_uart);
	if (ret < 0)
		return ret;

#ifdef SDP_SERIAL_DEBUG
	ret = uart_register_driver(&sdp_serial_uart_debug);
	if (ret < 0)	{
		uart_unregister_driver(&sdp_serial_uart);
		return ret;
	}
#endif

	ret = platform_driver_register(&sdp_serial_driver);
	if (ret < 0)
	{
		uart_unregister_driver(&sdp_serial_uart);
		uart_unregister_driver(&sdp_serial_uart_debug);
	}

	return ret;
}

static void __exit sdp_serial_exit(void)
{
	platform_driver_unregister(&sdp_serial_driver);
	uart_unregister_driver(&sdp_serial_uart);
}

module_init(sdp_serial_init);
module_exit(sdp_serial_exit);

MODULE_DESCRIPTION("Samsung SDP SoC Serial port driver");
MODULE_LICENSE("GPL");
