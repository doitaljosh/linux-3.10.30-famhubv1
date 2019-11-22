/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of_i2c.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <asm/irq.h>
#include <linux/semaphore.h>
#include <mach/regs-iic.h>
#include <mach/soc.h>

#define SDP_IDLE_TIMEOUT	5000
// #define SDP_FRC

/* i2c controller state */
enum sdp_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct sdp_platform_i2c {
	int		bus_num;
	unsigned int	flags;
	unsigned int	slave_addr;
	unsigned int	i2c_sck; 	//hsguy.son (add)
	unsigned int	i2c_sda; 	//hsguy.son (add)
	unsigned int	i2c_channel_num; 	//hsguy.son (add)
	unsigned int	hdmi_disable; 	//hsguy.son (add)
	unsigned long	frequency;
	unsigned int	sda_delay;
	unsigned int	irq_reg;
};

struct sdp_i2c {
	wait_queue_head_t	wait;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	unsigned int		tx_setup;
	unsigned int		irq;

	enum sdp_i2c_state	state;
	unsigned long		clkrate;

	void __iomem		*regs;
	void __iomem		*irq_reg;
	struct clk		*clk;
	struct device		*dev;
	struct i2c_adapter	adap;

	struct sdp_platform_i2c	*pdata;

};

static spinlock_t		lock_pend;
static spinlock_t 		lock_int;

static void __iomem * vbase;
static void __iomem * REG_GPIO_CTRL_P16; 	//hsguy.son (add)
static void __iomem * REG_GPIO_CTRL_P17; 	//hsguy.son (add)
static void __iomem * REG_HDMI_DISABLE; 	//hsguy.son (add)

static bool is_suspend;

static const struct of_device_id sdp_i2c_dt_match[] = {
	{ .compatible = "samsung,sdp-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_i2c_dt_match);

//hsguy.son (add)
static inline void writel_convert(int val, int mask, unsigned int *addr)
{
	unsigned int tmp;

	tmp = readl(addr);

	if(val == 1)
	{
		writel(tmp | (1 << mask), addr);
	}
	else
	{
		writel(tmp & (~(1 << mask)), addr);
	}
}
//hsguy.son (add_end)

static inline void sdp_i2c_master_complete(struct sdp_i2c *i2c, int ret)
{
	dev_dbg(i2c->dev, "master_complete %d\n", ret);
	
	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx++;
	i2c->msg_num = 0;

	if (ret)
		i2c->msg_idx = (u32) ret;

	wake_up(&i2c->wait);
}

static inline void sdp_i2c_clear_pending(struct sdp_i2c *i2c)
{
	unsigned long tmp;
	unsigned long flags;
	int nr=0;
#if  defined(CONFIG_ARCH_SDP)
		udelay((u32) i2c->adap.byte_delay);
#endif
	spin_lock_irqsave(&lock_pend,flags);

	if(i2c->adap.nr>7)
	{
		nr=	i2c->adap.nr;
		nr-=8;
	}else nr 	= i2c->adap.nr;

	tmp = readl(i2c->irq_reg + SDP_IICIRQ_STAT);
	tmp &= (1 << nr);
	writel(tmp, i2c->irq_reg + SDP_IICIRQ_STAT);
	readl(i2c->irq_reg + SDP_IICIRQ_STAT);

	spin_unlock_irqrestore(&lock_pend,flags);
}

static inline void sdp_i2c_disable_ack(struct sdp_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + SDP_IICCON);
	writel(tmp & ~SDP_IICCON_ACKEN, i2c->regs + SDP_IICCON);
}

static inline void sdp_i2c_enable_ack(struct sdp_i2c *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + SDP_IICCON);
	writel(tmp | SDP_IICCON_ACKEN, i2c->regs + SDP_IICCON);
}

static inline void sdp_i2c_disable_irq(struct sdp_i2c *i2c)
{
	unsigned long tmp;
	unsigned long flag=0;
	int nr=0;

	spin_lock_irqsave(&lock_int,flag);

	if(i2c->adap.nr>7)
	{
		nr=	i2c->adap.nr;
		nr-=8;
	}else nr 	= i2c->adap.nr;

	tmp = readl(i2c->irq_reg + SDP_IICIRQ_EN);
	tmp |= (1 <<nr);
	writel(tmp, i2c->irq_reg + SDP_IICIRQ_EN);

	spin_unlock_irqrestore(&lock_int,flag);

	tmp = readl(i2c->regs + SDP_IICCON);
	writel(tmp & ~SDP_IICCON_IRQEN, i2c->regs + SDP_IICCON);
	
}

static inline void sdp_i2c_enable_irq(struct sdp_i2c *i2c)
{
	unsigned long tmp;
	unsigned long flag=0;
	int nr=0;
	
	spin_lock_irqsave(&lock_int,flag);

	if(i2c->adap.nr>7)
	{
		nr=	i2c->adap.nr;
		nr-=8;
	}else nr 	= i2c->adap.nr;
	
	tmp = readl(i2c->irq_reg + SDP_IICIRQ_EN);
	tmp &= ~(1 << nr);
	writel(tmp, i2c->irq_reg + SDP_IICIRQ_EN);
	
	spin_unlock_irqrestore(&lock_int,flag);

	tmp = readl(i2c->regs + SDP_IICCON);
	writel(tmp | SDP_IICCON_IRQEN, i2c->regs + SDP_IICCON);
}

static void sdp_i2c_message_start(struct sdp_i2c *i2c, struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;
	unsigned long stat;
	unsigned long iiccon;

	stat = SDP_IICSTAT_TXRXEN;

	if (msg->flags & I2C_M_RD) {
		stat |= SDP_IICSTAT_MASTER_RX;
		addr |= 1;
	} else
		stat |= SDP_IICSTAT_MASTER_TX;

	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	sdp_i2c_enable_ack(i2c);

	iiccon = readl(i2c->regs + SDP_IICCON);
	writel(stat, i2c->regs + SDP_IICSTAT);

	dev_dbg(i2c->dev, "START: %08lx to IICSTAT, %02x to DS\n", stat, addr);
	writeb((u8) addr, (void*)((u32)i2c->regs + SDP_IICDS));

	/* delay here to ensure the data byte has gotten onto the bus
	 * before the transaction is started */

	ndelay(i2c->tx_setup);

	dev_dbg(i2c->dev, "iiccon, %08lx\n", iiccon);
	writel(iiccon, i2c->regs + SDP_IICCON);

	stat |= SDP_IICSTAT_START;
	writel(stat, i2c->regs + SDP_IICSTAT);
}

static inline void sdp_i2c_stop(struct sdp_i2c *i2c, int ret)
{
	unsigned long iicstat = readl(i2c->regs + SDP_IICSTAT);

	dev_dbg(i2c->dev, "STOP\n");

	/* stop the transfer */
	iicstat &= ~SDP_IICSTAT_START;

	writel(iicstat, i2c->regs + SDP_IICSTAT);

	i2c->state = STATE_STOP;

	sdp_i2c_master_complete(i2c, ret);
	sdp_i2c_disable_irq(i2c);
}

static inline int is_lastmsg(struct sdp_i2c *i2c)
{
	return i2c->msg_idx >= (i2c->msg_num - 1);
}

static inline int is_msglast(struct sdp_i2c *i2c)
{
	return i2c->msg_ptr == ((u32) i2c->msg->len-1);
}

static inline int is_msgend(struct sdp_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

static int sdp_irq_nextbyte(struct sdp_i2c *i2c, unsigned long iicstat)
{
	unsigned char byte;
	int ret = 0;

	switch (i2c->state) {
	case STATE_IDLE:
		dev_err(i2c->dev, "%s: called in STATE_IDLE\n", __func__);
		goto out;

	case STATE_STOP:
		dev_err(i2c->dev, "%s: called in STATE_STOP\n", __func__);
		sdp_i2c_disable_irq(i2c);
		goto out_ack;

	case STATE_START:
		/*
		 * last thing we did was send a start condition on the
		 * bus, or started a new i2c message
		 */

		if (iicstat & SDP_IICSTAT_LASTBIT &&
			!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			dev_dbg(i2c->dev, "ack was not received\n");
			sdp_i2c_stop(i2c, -ENXIO);
			goto out_ack;
		}

		if (i2c->msg->flags & I2C_M_RD)
			i2c->state = STATE_READ;
		else
			i2c->state = STATE_WRITE;

		/*
		 * terminate the transfer if there is nothing to do
		 * as this is used by the i2c probe to find devices.
		 */

		if (is_lastmsg(i2c) && i2c->msg->len == 0) {
			sdp_i2c_stop(i2c, 0);
			goto out_ack;
		}

		if (i2c->state == STATE_READ)
			goto prepare_read;

		/* fall through to the write state, as we will need to
		 * send a byte as well */

	case STATE_WRITE:
		/*
		 * we are writing data to the device... check for the
		 * end of the message, and if so, work out what to do
		 */

		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			if (iicstat & SDP_IICSTAT_LASTBIT) {
				dev_dbg(i2c->dev, "WRITE: No Ack\n");

				sdp_i2c_stop(i2c, -ECONNREFUSED);
				goto out_ack;
			}
		}

 retry_write:

		if (!is_msgend(i2c)) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
			writeb(byte, i2c->regs + SDP_IICDS);

			/* delay after writing the byte to allow the
			 * data setup time on the bus, as writing the
			 * data to the register causes the first bit
			 * to appear on SDA, and SCL will change as
			 * soon as the interrupt is acknowledged */

			ndelay(i2c->tx_setup);

		} else if (!is_lastmsg(i2c)) {
			/* we need to go to the next i2c message */

			dev_dbg(i2c->dev, "WRITE: Next Message\n");

			i2c->msg_ptr = 0;
			i2c->msg_idx++;
			i2c->msg++;

			/* check to see if we need to do another message */
			if (i2c->msg->flags & I2C_M_NOSTART) {

				if (i2c->msg->flags & I2C_M_RD) {
					/* cannot do this, the controller
					 * forces us to send a new START
					 * when we change direction */

					sdp_i2c_stop(i2c, -EINVAL);
				}

				goto retry_write;
			} else {
				/* send the new start */
				sdp_i2c_message_start(i2c, i2c->msg);
				i2c->state = STATE_START;
			}

		} else {
			/* send stop */
			sdp_i2c_stop(i2c, 0);
		}
		break;

	case STATE_READ:
		/*
		 * we have a byte of data in the data register, do
		 * something with it, and then work out whether we are
		 * going to do any more read/write
		 */

		byte = readb(i2c->regs + SDP_IICDS);
		i2c->msg->buf[i2c->msg_ptr++] = byte;

 prepare_read:
		if (is_msglast(i2c)) {
			/* last byte of buffer */

			if (is_lastmsg(i2c))
				sdp_i2c_disable_ack(i2c);

		} else if (is_msgend(i2c)) {
			/*
			 * ok, we've read the entire buffer, see if there
			 * is anything else we need to do
			 */

			if (is_lastmsg(i2c)) {
				/* last message, send stop and complete */
				dev_dbg(i2c->dev, "READ: Send Stop\n");

				sdp_i2c_stop(i2c, 0);
			} else {
				/* go to the next transfer */
				dev_dbg(i2c->dev, "READ: Next Transfer\n");

				i2c->msg_ptr = 0;
				i2c->msg_idx++;
				i2c->msg++;
			}
		}

		break;
		default:
		break;
	}

	/* acknowlegde the IRQ and get back on with the work */

 out_ack:
	sdp_i2c_clear_pending(i2c);

 out:
	return ret;
}

static irqreturn_t sdp_i2c_irq(int irqno, void *dev_id)
{
	struct sdp_i2c *i2c = dev_id;
	unsigned long status;

	status = readl(i2c->regs + SDP_IICSTAT);

	if (status & SDP_IICSTAT_ARBITR) {
		/* deal with arbitration loss */
		dev_err(i2c->dev, "deal with arbitration loss\n");
	}

	if (i2c->state == STATE_IDLE) {
		dev_dbg(i2c->dev, "IRQ: error i2c->state == IDLE\n");

		sdp_i2c_clear_pending(i2c);
		goto out;
	}

	sdp_irq_nextbyte(i2c, status);

 out:
	return IRQ_HANDLED;
}

static int sdp_i2c_set_master(struct sdp_i2c *i2c)
{
	unsigned long iicstat;
	unsigned long iiccone;
	int timeout = 400;

	while (timeout-- > 0) {
		iicstat = readl(i2c->regs + SDP_IICSTAT);

		if (!(iicstat & SDP_IICSTAT_BUSBUSY)) {
			return 0;
		} else {
			sdp_i2c_disable_ack(i2c);
			sdp_i2c_clear_pending(i2c);

			iiccone = readl(i2c->regs + SDP_IICCONE);
			iiccone &= ~SDP_IICCONE_STOP_DETECT;
			writel(iiccone, i2c->regs + SDP_IICCONE);
		}

		udelay(1000);
	}

	return -ETIMEDOUT;
}

static void sdp_i2c_wait_idle(struct sdp_i2c *i2c)
{
	unsigned long iicstat;
	ktime_t start, now;
	unsigned long delay;
	int spins;

	/* ensure the stop has been through the bus */

	dev_dbg(i2c->dev, "waiting for bus idle\n");

	start = now = ktime_get();

	/*
	 * Most of the time, the bus is already idle within a few usec of the
	 * end of a transaction.  However, really slow i2c devices can stretch
	 * the clock, delaying STOP generation.
	 *
	 * On slower SoCs this typically happens within a very small number of
	 * instructions so busy wait briefly to avoid scheduling overhead.
	 */
	spins = 3;
	iicstat = readl(i2c->regs + SDP_IICSTAT);

	while ((iicstat & SDP_IICSTAT_START) && --spins) {
		cpu_relax();
		iicstat = readl(i2c->regs + SDP_IICSTAT);
	}

	/*
	 * If we do get an appreciable delay as a compromise between idle
	 * detection latency for the normal, fast case, and system load in the
	 * slow device case, use an exponential back off in the polling loop,
	 * up to 1/10th of the total timeout, then continue to poll at a
	 * constant rate up to the timeout.
	 */
	delay = 1;

	while ((iicstat & SDP_IICSTAT_START) &&
			ktime_us_delta(now, start) < SDP_IDLE_TIMEOUT) {
		usleep_range(delay, 2 * delay);

		if (delay < SDP_IDLE_TIMEOUT / 10)
			delay <<= 1;

		now = ktime_get();
		iicstat = readl(i2c->regs + SDP_IICSTAT);
	}

	if (iicstat & SDP_IICSTAT_START)
		dev_warn(i2c->dev, "timeout waiting for bus idle\n");
}
static int sdp_i2c_check_bus(struct sdp_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long iicstat;
	int retry = 10,ret=0,nr=0;
	unsigned long delay = jiffies + HZ/5;		// 200mS delay
	unsigned long delay2 = jiffies + 2;	

	sdp_i2c_disable_irq(i2c);

	iicstat=readl(i2c->regs + SDP_IICSTAT)&SDP_IICSTAT_BUSBUSY;

	if(iicstat) //busy
	{	
		while(time_before(jiffies, delay2))//4ms
		{
			if (readl(i2c->regs + SDP_IICSTAT)&(1<<5)) cond_resched(); 			
			else goto __ready_out;
		}
	}
	while((readl(i2c->regs + SDP_IICSTAT)&SDP_IICSTAT_BUSBUSY)&& retry)
	{	
		writel( readl(i2c->regs + SDP_IICCON)| SDP_IICCON_IRQEN, i2c->regs + SDP_IICCON);// status ready
		iicstat=readl(i2c->regs + SDP_IICSTAT)&0xF0;
		if(iicstat==0xF0)
		{
			writel(0xD0,i2c->regs + SDP_IICSTAT);
			sdp_i2c_clear_pending(i2c);
		}
		else if(iicstat==0xB0)
		{	
			sdp_i2c_disable_ack(i2c);
			sdp_i2c_clear_pending(i2c);
			delay = jiffies + HZ/100;	// 10mS delay

			while(time_before(jiffies, delay))
			{ 	
				if(i2c->adap.nr>7)
				{
					nr= i2c->adap.nr;
					nr-=8;
				}else nr	= i2c->adap.nr;

				if(readl(i2c->irq_reg + SDP_IICIRQ_STAT)&(1 <<nr))break;
			}	
			
			writel(0x90,i2c->regs + SDP_IICSTAT);
			sdp_i2c_clear_pending(i2c);
		}else if(iicstat==0x30)
		{	
			dev_err(i2c->dev, "I2C master busy!!!\n");
			writel(0xf0,i2c->regs + SDP_IICSTAT);
				delay = jiffies + HZ/100;  
				
				while(time_before(jiffies, delay))
				{ 	
					if(i2c->adap.nr>7)
					{
						nr= i2c->adap.nr;
						nr-=8;
					}else nr	= i2c->adap.nr;

					if(readl(i2c->irq_reg + SDP_IICIRQ_STAT)&(1 <<nr))break;
				}
				writel(0xD0,i2c->regs + SDP_IICSTAT);
				sdp_i2c_clear_pending(i2c);
		}
		else goto __ready_out;

		iicstat=readl(i2c->irq_reg + SDP_IICCONE);
		iicstat&=(~SDP_IICCONE_STOP_DETECT);
		writel(iicstat,i2c->irq_reg + SDP_IICCONE);
		retry--;
		
	}

	iicstat=readl(i2c->regs + SDP_IICSTAT)&SDP_IICSTAT_BUSBUSY;
	if(iicstat) dev_err(i2c->dev, "[i2c fail] port %d recovery is failed, check bus line %x\n", i2c->adap.nr,readl((void*)((u32)i2c->regs + SDP_IICSTAT)));
	
	
__ready_out:
	return ret;
}

#ifdef SDP_FRC
/* only HawkP/M */
static int sdp_frc_gpio_con(const int busnr, const int addr, const int level)
{
	static void __iomem *gpio_base;
	int val = 0;
	u32 hawkp_gpio = 0x11250DD8;//P16.2
	u32 hawkm_gpio = 0x005C1160;//P6.6
		
	if(soc_is_sdp1404() && (busnr == 8) && (addr == 0xC2 || addr == 0xC4 || addr == 0xC0 \
		|| addr == 0x28 ||addr == 0xEC || addr == 0xC8 || addr == 0xF8 || addr == 0xB4 \
		|| addr == 0xB6 || addr == 0xA0 || addr == 0x9E|| addr == 0xF0 || addr == 0xDE || addr == 0x66|| addr == 0x40 || addr == 0xA2))
	{
		gpio_base = ioremap(hawkp_gpio,0x10);
		val = readl((void*)((u32)gpio_base+0x4));
		if(level == 0)writel(val&~(1<<2),(void*)((u32)gpio_base+0x4));//low
		else if(level == 1)writel(val|(1<<2),(void*)((u32)gpio_base+0x4)); //High
		else 
			{
			pr_err("invaild i2c gpio  (level %d)\n", level);
			iounmap(gpio_base);
			return -1;
		}
		val = readl(gpio_base);
		writel(val|(0x3<<8),(void*)((u32)gpio_base)); //out
		iounmap(gpio_base);

		if(addr == 0xEC || addr == 0xF0) mdelay(5);
	}
	else if((soc_is_sdp1406fhd() || soc_is_sdp1406uhd()) && (busnr == 8) && (addr == 0xC2 || addr == 0xC4 || addr == 0xC0 \
		|| addr == 0x28 ||addr == 0xEC || addr == 0xC8 || addr == 0xF8 || addr == 0xB4 \
		|| addr == 0xB6 || addr == 0xA0 || addr == 0x9E || addr == 0xF0|| addr == 0x66|| addr == 0x40 || addr == 0xA2))
	{
		gpio_base = ioremap(hawkm_gpio,0x10);
		val = readl((void*)((u32)gpio_base+0x4));
		if(level == 0)writel(val&~(1<<6),(void*)((u32)gpio_base+0x4));//low
		else if(level == 1)writel(val|(1<<6),(void*)((u32)gpio_base+0x4)); //High
		else 
		{
			pr_err("invaild i2c gpio  (level %d)\n", level);
			iounmap(gpio_base);
			return -1;
		}
		val = readl(gpio_base);
		writel(val|(0x3<<24),(void*)((u32)gpio_base)); //out	
		iounmap(gpio_base);

		if(addr == 0xEC || addr == 0xF0) mdelay(5);
	}

	return 0;
}
#endif

/* only HawkP/M */
static int sdp_i2c_doxfer(struct sdp_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	int ret;
	int i,waittime,datasize = 0;
#ifdef SDP_FRC	
	int frc_busnr, frc_addr;
#endif

	ret = sdp_i2c_set_master(i2c);
	if (ret != 0) {
		dev_err(i2c->dev, "cannot get bus (error %d)\n", ret);
		ret = -EAGAIN;
		goto out;
	}
	
	i2c->msg     = msgs;
	i2c->msg_num = (u32) num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;
	i2c->state   = STATE_START;

	for(i=0;i<num;i++)
	{
		datasize += msgs[i].len;
	}
	waittime = datasize << 1;
	waittime += HZ;

#ifdef SDP_FRC	
	frc_busnr = i2c->adap.nr;
	frc_addr = msgs->addr << 1;
	sdp_frc_gpio_con(frc_busnr, frc_addr, 1);
#endif
	sdp_i2c_enable_irq(i2c);
	sdp_i2c_message_start(i2c, msgs);

	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0,waittime);

	ret = i2c->msg_idx;

	/* having these next two as dev_err() makes life very
	 * noisy when doing an i2cdetect */

	if (timeout == 0) {
		dev_err(i2c->dev, "[i2c fail] CH:%d Device ID:0x%x timeout\n",i2c->adap.nr,(msgs->addr<<1));
		sdp_i2c_disable_irq(i2c);
		ret = -ETIMEDOUT;
	} else if (ret != num)
		dev_err(i2c->dev, "[i2c fail] CH:%d Device ID:0x%x incomplete xfer (%d)\n",i2c->adap.nr,(msgs->addr<<1), ret);

#ifdef SDP_FRC
	sdp_frc_gpio_con(frc_busnr, frc_addr, 0);
#endif
	sdp_i2c_wait_idle(i2c);

 out:
	return ret;
}

static int sdp_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct sdp_i2c *i2c = (struct sdp_i2c *)adap->algo_data;
	int retry;
	int ret;

	if (is_suspend) {
		dev_err(i2c->dev, "not permitted in suspend\n");
		return -EPERM;
	}

	sdp_i2c_check_bus(i2c,msgs,num);

	if(i2c->adap.byte_delay > 300000) //0.3s
	{
		dev_info(i2c->dev, "too big byte delay (%d)us,reset 0us\n", i2c->adap.byte_delay);
		i2c->adap.byte_delay = 0;
	}
	
	for (retry = 0; retry < adap->retries; retry++) {
		ret = sdp_i2c_doxfer(i2c, msgs, num);

		if (ret != -EAGAIN)
			return ret;

		udelay(100);
	}

	return -EREMOTEIO;
}

static u32 sdp_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_NOSTART |
		I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm sdp_i2c_algorithm = {
	.master_xfer		= sdp_i2c_xfer,
	.functionality		= sdp_i2c_func,
};

static int sdp_i2c_calcdivisor(unsigned long clkin, unsigned int wanted,
				   unsigned int *div1, unsigned int *divs)
{
	unsigned int calc_div1;
	unsigned int  prescaler;
	
	if (((clkin>>4) / wanted )> 63)
		calc_div1 = 256;
	else
		calc_div1 = 16;	
	
	prescaler=((clkin/calc_div1)/wanted);

	*div1 = calc_div1;		//div select
	*divs = prescaler;

	return wanted;
}

static int sdp_i2c_clockrate(struct sdp_i2c *i2c, unsigned int *got)
{
	struct sdp_platform_i2c *pdata = i2c->pdata;
	unsigned long clkin = clk_get_rate(i2c->clk);
	unsigned int divs, div1;
	unsigned int reg; //for sdp1302 GolfS
	unsigned long target_frequency;
	u32 iiccon;
	u32 iiccone;
	int freq;

	i2c->clkrate = clkin;
	clkin /= 1000;		/* clkin now in KHz */

	dev_dbg(i2c->dev, "pdata desired frequency %lu\n", pdata->frequency);
	dev_dbg(i2c->dev, "clkin %lu\n", clkin);

	target_frequency = pdata->frequency ? pdata->frequency : 100000;
	target_frequency /= 1000; /* Target frequency now in KHz */

	freq = sdp_i2c_calcdivisor(clkin, target_frequency, &div1, &divs);

	if (freq > (int) target_frequency) {
		dev_err(i2c->dev,
			"Unable to achieve desired frequency %luKHz."
			" Lowest achievable %dKHz\n", target_frequency, freq);
		return -EINVAL;
	}
	
	*got = (u32) freq;

	iiccon = readl(i2c->regs + SDP_IICCON);
	iiccon &= ~(SDP_IICCON_SCALE_MASK | SDP_IICCON_TXDIV_256);
	iiccon |= SDP_IICCON_SCALE(divs);

	if (div1 == 256)
		iiccon |= SDP_IICCON_TXDIV_256;

	writel(iiccon, i2c->regs + SDP_IICCON);

	if(soc_is_sdp1302())//GolfS
	{
		reg=(u32)i2c->regs & 0x0ff;
		if(reg/0x20 == 4) // GolfS ch4 eeprom stretch off
		{
			iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay)|
					SDP_IICCONE_FILTER_OFF;
		}else{
			iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay) |
					SDP_IICCONE_FILTER_ON |
					SDP_IICCONE_FILTER_MAJORITY |
					SDP_IICCONE_BUFFER(3);
		}

	}else if(soc_is_sdp1304())//GolfAP
	{
		reg=(u32)i2c->regs & 0x0ff;
		if(reg/0x20 == 0) // GolfAP ch1 eeprom stretch off
		{
			iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay)|
					SDP_IICCONE_FILTER_OFF;
		}else if(reg/0x20 == 7) // GolfAP ch7 scl skew max
		{
			iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay) |
					(15<<4) | 
					SDP_IICCONE_FILTER_ON |
					SDP_IICCONE_FILTER_MAJORITY |
					SDP_IICCONE_BUFFER(3);
		}
		else{
			iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay) |
					SDP_IICCONE_FILTER_ON |
					SDP_IICCONE_FILTER_MAJORITY |
					SDP_IICCONE_BUFFER(3);
		}

	}
#if defined(CONFIG_ARCH_SDP1106) // for EchoP
	else{
	iiccone = iiccone & 0xF;
	reg=(u32)i2c->regs & 0x0ff;
	reg = reg/0x20;
	switch (reg)
	{
	case (4): 		// 1.233
		iiccone |= (1 << 9);	// filter enable
		iiccone |= (1 << 16);	// filter mode 
		iiccone |= SDP_IICCONE_BUFFER(3);	// use buffer
		break;
	case (2):			//Tuner 
		iiccone |= (1 << 9);	// filter enable
		iiccone |= (1 << 17);	// filter mode 
		iiccone |= SDP_IICCONE_BUFFER(3);	// use buffer
	default:
		break;
		}
	
	}
#else //for HawkP
	else{
		iiccone = SDP_IICCONE_SDA_DELAY(pdata->sda_delay) |
					SDP_IICCONE_FILTER_ON |
					SDP_IICCONE_FILTER_MAJORITY | SDP_IICCONE_SDAFILTER_MAJORITY |
					SDP_IICCONE_BUFFER(7);
	}
#endif

	writel(iiccone, i2c->regs + SDP_IICCONE);

	return 0;
}

static int sdp_i2c_bus_init(struct sdp_i2c *i2c)
{
	struct sdp_platform_i2c *pdata;
	unsigned int freq;

	/* get the plafrom data */
	pdata = i2c->pdata;

	/* status reset */
	writel(0, i2c->regs + SDP_IICSTAT);

	/* write slave address */
	writeb((u8)pdata->slave_addr, (void *)((u32)i2c->regs + SDP_IICADD));

	dev_info(i2c->dev, "slave address 0x%02x\n", pdata->slave_addr);

	writel(SDP_IICCON_IRQEN , i2c->regs + SDP_IICCON);

	/* we need to work out the divisors for the clock... */

	if (sdp_i2c_clockrate(i2c, &freq) != 0) {
		writel(0, i2c->regs + SDP_IICCON);
		dev_err(i2c->dev, "cannot meet bus frequency required\n");
		return -EINVAL;
	}

	dev_info(i2c->dev, "bus frequency set to %d KHz\n", freq);

	return 0;
}

static int sdp_i2c_get_irq_reg(struct sdp_i2c *i2c)
{
	struct sdp_platform_i2c *pdata;
	pdata = i2c->pdata;

	i2c->irq_reg = ioremap(pdata->irq_reg, 8);

	if (!i2c->irq_reg) {
		dev_err(i2c->dev, "Can't get interrupts status register\n");
		return -ENXIO;
	}

	return 0;
}
static struct sdp_i2c_mux
{
	u32 reg;
	u32 value;	
}  sdp_i2c_mux_;

static void sdp_i2c_parse_dt(struct device_node *np, struct sdp_i2c *i2c)
{
	struct sdp_platform_i2c *pdata = i2c->pdata;
	int tmp;

	if (!np)
		return;

	pdata->bus_num = -1;

	of_property_read_u32(np, "samsung,i2c-sda-delay", &pdata->sda_delay);
	of_property_read_u32(np, "samsung,i2c-slave-addr", &pdata->slave_addr);
	of_property_read_u32(np, "samsung,i2c-max-bus-freq",
					(u32 *)&pdata->frequency);
	of_property_read_u32(np, "samsung,i2c-irq-status-reg",
					(u32 *)&pdata->irq_reg);

	if(of_property_read_u32_array(np, "samsung,i2c-pad-enable", &sdp_i2c_mux_.reg,2)==0)
	{	
		vbase=ioremap(sdp_i2c_mux_.reg,0x100);
		tmp=readl((void *)vbase);
		tmp=tmp|(sdp_i2c_mux_.value);
		writel(tmp,(void *)vbase);
		iounmap(vbase);
	}else
		{
		sdp_i2c_mux_.reg=0;
		sdp_i2c_mux_.value=0;
	}

	if(soc_is_sdp1304())	//i2c0_clk,i2c0_data gpio default
	{
		vbase=ioremap(0x10B00CD4,0x10);
		tmp=readl((void *)vbase);
		tmp=tmp&~(0xff);
		writel(tmp,(void *)vbase);
		iounmap(vbase);
	}
	//hsguy.son (add)
	else
	{
		if(of_get_named_gpio(np, "samsung,scl-gpio", 0) > 0)
		{
			pdata->i2c_sck = of_get_named_gpio(np, "samsung,scl-gpio", 0);
			gpio_set_value(pdata->i2c_sck, 1);
			msleep(50);
		}
		if(of_get_named_gpio(np, "samsung,sda-gpio", 0) > 0)
		{
			pdata->i2c_sda = of_get_named_gpio(np, "samsung,sda-gpio", 0);
			gpio_set_value(pdata->i2c_sda, 1);
			msleep(50);
		}
		if(of_property_read_u32(np, "samsung,hdmi-disable", &pdata->hdmi_disable)==0)
		{
			if(pdata->hdmi_disable)
			{
				REG_HDMI_DISABLE = ioremap(0x005C1040, sizeof(u32));
				writel_convert(0, 1, REG_HDMI_DISABLE);
			}
		}
		if(of_property_read_u32(np, "samsung,i2c-channel-number", &pdata->i2c_channel_num)==0)
		{
			REG_GPIO_CTRL_P16 = ioremap(0x005C11D8, sizeof(u32));
			REG_GPIO_CTRL_P17 = ioremap(0x005C11E4, sizeof(u32));
			switch(pdata->i2c_channel_num)
			{
			case 0:
				writel_convert(0, 4, REG_GPIO_CTRL_P16);
				writel_convert(0, 5, REG_GPIO_CTRL_P16);
				writel_convert(0, 8, REG_GPIO_CTRL_P16);
				writel_convert(0, 9, REG_GPIO_CTRL_P16);
				break;
			case 1:
				writel_convert(0, 12, REG_GPIO_CTRL_P16);
				writel_convert(0, 13, REG_GPIO_CTRL_P16);
				writel_convert(0, 16, REG_GPIO_CTRL_P16);
				writel_convert(0, 17, REG_GPIO_CTRL_P16);
				break;
			case 2:
				writel_convert(0, 20, REG_GPIO_CTRL_P16);
				writel_convert(0, 21, REG_GPIO_CTRL_P16);
				writel_convert(0, 24, REG_GPIO_CTRL_P16);
				writel_convert(0, 25, REG_GPIO_CTRL_P16);
				break;
			case 3:
				writel_convert(0, 28, REG_GPIO_CTRL_P16);
				writel_convert(0, 29, REG_GPIO_CTRL_P16);
				writel_convert(0, 0, REG_GPIO_CTRL_P17);
				writel_convert(0, 1, REG_GPIO_CTRL_P17);
				break;
			default:
				break;
			}
		}

	}
	//hsguy.son (add_end)

	
}

//hsguy.son (add)
static int sdp_i2c_of_do_initregs(struct device *dev)
{
	int psize;
	const u32 *initregs;
	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -1;
	}

	/* Get "initregs" property */
	initregs = of_get_property(dev->of_node, "initregs", &psize);

	if (initregs != NULL) {
		int onesize;
		int i = 0;

		psize /= 4;/* each cell size 4byte */
		onesize = 3;
		for (i = 0; psize >= onesize; psize -= onesize, initregs += onesize, i++) {
			u32 addr, mask, val;
			u8 * __iomem iomem;

			addr = be32_to_cpu(initregs[0]);
			mask = be32_to_cpu(initregs[1]);
			val = be32_to_cpu(initregs[2]);

			iomem = ioremap(addr, sizeof(u32));
			if(iomem) {
				writel( (readl(iomem)&~mask) | (val&mask), iomem );
				dev_printk(KERN_DEBUG, dev,
					"of initreg addr 0x%08x, mask 0x%08x, val 0x%08x\n",
					addr, mask, val);
				iounmap(iomem);
			} else {
				return -ENOMEM;
			}
		}
	}
	return 0;
}
//hsguy.son (add_end)

static void sdp_i2c_cpu_affinity(struct device_node *np, struct sdp_i2c *i2c)
{
//	struct sdp_platform_i2c *pdata = i2c->pdata;
	u32 cpu_aff;

	if (!np)
		return;

	if(of_property_read_u32(np, "samsung,i2c-cpu-affinity", &cpu_aff)==0)
	{
		if(num_online_cpus() > 1) 
		{ 	
			irq_set_affinity(i2c->irq, cpumask_of(cpu_aff));
		}
	}
	else
	{
		dev_info(i2c->dev, "i2c cpu affinity get fail\n");
	}

}

static int sdp_i2c_probe(struct platform_device *pdev)
{
	struct sdp_i2c *i2c;
	struct sdp_platform_i2c *pdata = NULL;
	struct resource *res;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "device tree node not found\n");
		return -ENXIO;
	}

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct sdp_i2c), GFP_KERNEL);
	if (!i2c) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}

	i2c->pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!i2c->pdata) {
		ret = -ENOMEM;
		goto err_noclk;
	}

	sdp_i2c_parse_dt(pdev->dev.of_node, i2c);
	sdp_i2c_of_do_initregs(&pdev->dev); 	//hsguy.son (add)

	spin_lock_init(&lock_pend);
	spin_lock_init(&lock_int);
	
	strlcpy(i2c->adap.name, "sdp-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.algo    = &sdp_i2c_algorithm;
	i2c->adap.retries = 2;
	i2c->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->tx_setup     = 50;

	init_waitqueue_head(&i2c->wait);

	/* find the clock and enable it */

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "rstn_i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	clk_prepare_enable(i2c->clk);

	/* map the registers */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}

	i2c->regs = devm_request_and_ioremap(&pdev->dev, res);

	if (i2c->regs == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_clk;
	}

	dev_dbg(&pdev->dev, "registers %p (%p)\n",
		i2c->regs, res);

	/* setup info block for the i2c core */

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	/* Get the interrupt pending status register */

	ret = sdp_i2c_get_irq_reg(i2c);
	if (ret != 0)
		goto err_clk;

	/* initialise the i2c controller */

	ret = sdp_i2c_bus_init(i2c);
	if (ret != 0)
		goto err_clk;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto err_clk;
	}

	i2c->irq = (u32) ret;
	
	ret = request_irq(i2c->irq, sdp_i2c_irq, 0,
		dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
		goto err_clk;
	}
	sdp_i2c_cpu_affinity(pdev->dev.of_node, i2c);

	i2c->adap.nr = i2c->pdata->bus_num;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_irq;
	}

	of_i2c_register_devices(&i2c->adap);
	platform_set_drvdata(pdev, i2c);

	dev_info(&pdev->dev, "%s: SDP I2C adapter\n", dev_name(&i2c->adap.dev));
	return 0;

 err_irq:
	free_irq(i2c->irq, i2c);

 err_clk:
	clk_disable_unprepare(i2c->clk);
	clk_put(i2c->clk);

 err_noclk:
	return ret;
}

static int sdp_i2c_remove(struct platform_device *pdev)
{
	struct sdp_i2c *i2c = platform_get_drvdata(pdev);
	if(i2c == NULL)
			return 0;
	
	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq, i2c);

	clk_disable_unprepare(i2c->clk);
	clk_put(i2c->clk);

	return 0;
}

#ifdef CONFIG_PM
static int sdp_i2c_suspend(struct device *dev)
{
//	pr_info("[%s:%d]\n",__func__,__LINE__);
	is_suspend = true;

	return 0;
}
static int sdp_i2c_resume(struct device *dev)
{
	//struct sdp_i2c *i2c = platform_get_drvdata(pdev);
	struct sdp_i2c *i2c =dev_get_drvdata(dev);
	int ret;
//	pr_info("[%s:%d]\n",__func__,__LINE__);

	ret = sdp_i2c_bus_init(i2c);
	if (ret != 0)		dev_err(dev, "failed to init i2c bus\n");

	is_suspend = false;

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops sdp_i2c_pm_ops = {
	.suspend_late = sdp_i2c_suspend,
	.resume_early = sdp_i2c_resume,
	//.suspend = sdp_i2c_suspend,
	//.resume = sdp_i2c_resume,
};
#define SDP_I2C_PM_OPS (&sdp_i2c_pm_ops)
#else /* !CONFIG_PM */
#define SDP_I2C_PM_OPS NULL
#endif /* !CONFIG_PM */

static struct platform_driver sdp_i2c_driver = {
	.probe		= sdp_i2c_probe,
	.remove		= sdp_i2c_remove,
	.driver = {
		.name	= "sdp-i2c",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_i2c_dt_match),
		#ifdef CONFIG_PM
		.pm = SDP_I2C_PM_OPS,
		#endif 
	},
};

static int __init sdp_i2c_init(void)
{
	return platform_driver_register(&sdp_i2c_driver);
}
subsys_initcall(sdp_i2c_init);

static void __exit sdp_i2c_exit(void)
{
	platform_driver_unregister(&sdp_i2c_driver);
}
module_exit(sdp_i2c_exit);

MODULE_DESCRIPTION("Samsung SDP SoCs I2C Bus driver");
MODULE_LICENSE("GPL v2");
