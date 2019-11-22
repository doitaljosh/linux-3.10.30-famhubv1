/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/completion.h>
#include <linux/sched.h>

#include <mach/map.h>
#include <mach/regs-serial.h>

#define portaddr(port, reg) ((port)->regs + (reg))
#define portaddrl(port, reg) ((unsigned long *)((port)->regs + (reg)))

#define rd_regb(port, reg) (__raw_readb(portaddr(port, reg)))
#define rd_regl(port, reg) (__raw_readl(portaddr(port, reg)))

#define wr_regb(port, reg, val) __raw_writeb(val, portaddr(port, reg))
#define wr_regl(port, reg, val) __raw_writel(val, portaddr(port, reg))

#define SDP_MICOM_FIFO_SIZE	16
#if defined(CONFIG_ARCH_SDP1106) || defined(CONFIG_ARCH_SDP1202) || defined(CONFIG_ARCH_SDP1207)
#define SDP_MICOM_BAUDRATE	9600
#else
#define SDP_MICOM_BAUDRATE	115200
#endif

#define SDP_MICOM_ACK_RETRY	5
#define SDP_MICOM_BUF_SIZE	64

#define SDP_MICOM_CMD_LEN	9
#define SDP_MICOM_CMD_PREFIX	0xFF
#define SDP_MICOM_CMD_CEC_PREFIX	0xFE
#define SDP_MICOM_CMD_ISP_PREFIX	0x0F

#define SDP_MICOM_DATA_LEN	(SDP_MICOM_CMD_LEN - 4)

#define SDP_ULCON_DLAB			(1 << 7)

#define SDP_ADDR_BASE		0xFE000000

#ifndef __ASSEMBLY__
#define SDP_ADDR(x)		((void __iomem __force *)SDP_ADDR_BASE + (x))
#else
#define SDP_ADDR(x)		(SDP_ADDR_BASE + (x))
#endif

#define SDP_VA_UART		SDP_ADDR(0x90A00)

/* TODO: add devices: IR, WDT.. etc. */
static struct mfd_cell sdp_micom_devs[] = {
	{ .name = "rc-wt61p807" },
	{ .name	= "leds-wt61p807" },
	{ .name	= "rtc-wt61p807" },
	{ .name	= "ontimer-wt61p807" },
	{ .name	= "system-wt61p807" },
	{ .name = "cec-wt61p807" },
	{ .name = "msg-wt61p807" },
	{ .name	= "bsensor-wt61p807" },
	{ .name	= "isp-wt61p807" },
	{ .name	= "ar-wt61p807" },
	{ .name = "panel-wt61p807" },
};

struct sdp_micom_port {
	struct device		*dev;

	void __iomem		*regs;
	unsigned int		irq;

	struct clk		*clk;
	struct clk		*baudclk;

	char			ack;
	struct completion	complete;
	char			ack_data[SDP_MICOM_DATA_LEN];

	char			*buf;
	unsigned int		count;
	unsigned int		pos;

	int			long_isp_resp;
	enum block_cmd		block_flag;
	int			panel_key_lock;
	int			ar_mode;

	struct sdp_micom_cb	*cb[SDP_MICOM_DEV_NUM];

	unsigned int		ulcon;
	unsigned int		ucon;
	unsigned int		ubrdiv;
};

static struct sdp_micom_port *micom_port;

enum {
	MICOM_CACHED_MSG_MICOM_VER=0,  //CTM_MICOM_VERSION(cmd:0x20,ack:0x30)
	MICOM_CACHED_MSG_PRODUCT_INFO, //CTM_GET_TV_SIDE_PRODUCT_INFO(cmd:0xC6,ack:0xC6)
	MICOM_CACHED_MSG_MAX
};
static struct sdp_micom_msg micom_cached_msg_data_list[MICOM_CACHED_MSG_MAX];

static DEFINE_MUTEX(micom_lock);
static DEFINE_MUTEX(micom_cmd_lock);
static DEFINE_MUTEX(micom_complete_lock);

static void sdp_micom_handle_cmd(struct sdp_micom_port *port, struct sdp_micom_msg *msg, int id);

static void sdp_micom_resetport(struct sdp_micom_port *port)
{
	unsigned long ucon = rd_regl(port, SDP_UCON);
	unsigned long ufcon = SDP_UFCON_FIFOMODE | SDP_UFCON_RXTRIG12;

	wr_regl(port, SDP_UCON, ucon | SDP_UCON_DEFAULT);

	/* reset both fifos */
	wr_regl(port, SDP_UFCON, ufcon | SDP_UFCON_RESETBOTH);
	wr_regl(port, SDP_UFCON, ufcon);

	/* some delay is required after fifo reset */
	udelay(1);
}

static int sdp_micom_txrdy(struct sdp_micom_port *port)
{
	unsigned long ufstat;

	ufstat = rd_regl(port, SDP_UFSTAT);
	return (ufstat & SDP_UFSTAT_TXFIFO_FULL) ? 0 : 1;
}

static void sdp_micom_putchar(struct sdp_micom_port *port, char ch)
{
	while (!sdp_micom_txrdy(port))
		barrier();

	wr_regb(port, SDP_UTXH, ch);
}

static unsigned int sdp_micom_write_direct(struct sdp_micom_port *port,
			const char *s, unsigned int count)
{
	unsigned int i = 0;

	for (i = 0; i < count; i++)
		sdp_micom_putchar(port, s[i]);

	return i;
}

static unsigned int sdp_micom_write(struct sdp_micom_port *port,
			const char *s, unsigned int count)
{
	unsigned int i=0;

	mutex_lock(&micom_lock);

	for (i = 0; i < count; i++)
		sdp_micom_putchar(port, s[i]);

	mutex_unlock(&micom_lock);
	return i;
}

static char sdp_micom_checksum_cal(const char *buf,
		enum sdp_micom_data_type msg_type,
		int msg_len)
{
	char checksum = 0x00;
	int index = 0;

	if (buf == NULL)
		goto out;

	if (msg_type == MICOM_NORMAL_DATA) {
		/* last byte is check sum */
		for (index = 0; index < msg_len; index++)
			checksum += buf[index];
	} else if (msg_type == MICOM_CEC_DATA) {
		for (index = 0; index < msg_len; index++)
			checksum ^= buf[index];
	}

out:
	return checksum;
}

static int sdp_micom_send_cmd_no_lock(char cmd, char *data, int len)
{
	struct sdp_micom_port *port = micom_port;
	char buf[SDP_MICOM_CMD_LEN] = {0};
	int i = 0, rtn = 0;
	unsigned int sentByte = 0;

	if (port->block_flag == NOBLOCK) {
		buf[0] = SDP_MICOM_CMD_PREFIX;
		buf[1] = SDP_MICOM_CMD_PREFIX;
		buf[2] = cmd;

		if (data) {
			if (len > SDP_MICOM_DATA_LEN)
				len = SDP_MICOM_DATA_LEN;
			for (i = 0; i < len; i++)
				buf[3 + i] = data[i];
		}

		/* checksum */
		for (i = 2; i < 8; i++)
			buf[8] += buf[i];

		sentByte = sdp_micom_write(port, buf, SDP_MICOM_CMD_LEN);
		if (sentByte != SDP_MICOM_CMD_LEN) {
			dev_err(port->dev, "sdp-micom: Failed to write all of data(send_cmd).(cmd:0x%02x,len:%d,sent:%d)\n", cmd, SDP_MICOM_CMD_LEN, sentByte);
			rtn = -1;
		} else
			rtn = 0;
	}
	return rtn;
}

int sdp_micom_send_cmd(char cmd, char *data, int len)
{
	struct sdp_micom_port *port = micom_port;
	char buf[SDP_MICOM_CMD_LEN] = {0};
	int i = 0, rtn = 0;
	unsigned int sentByte = 0;

	if (port->block_flag == NOBLOCK) {
		buf[0] = SDP_MICOM_CMD_PREFIX;
		buf[1] = SDP_MICOM_CMD_PREFIX;
		buf[2] = cmd;

		if (data) {
			if (len > SDP_MICOM_DATA_LEN)
				len = SDP_MICOM_DATA_LEN;
			for (i = 0; i < len; i++)
				buf[3 + i] = data[i];
		}

		/* checksum */
		for (i = 2; i < 8; i++)
			buf[8] += buf[i];

		mutex_lock(&micom_cmd_lock);

		sentByte = sdp_micom_write(port, buf, SDP_MICOM_CMD_LEN);
		if (sentByte != SDP_MICOM_CMD_LEN) {
			dev_err(port->dev, "sdp-micom: Failed to write all of data(send_cmd).(cmd:0x%02x,len:%d,sent:%d)\n", cmd, SDP_MICOM_CMD_LEN, sentByte);
			rtn = -1;
		} else
			rtn = 0;

		mutex_unlock(&micom_cmd_lock);
	}
	return rtn;
}
EXPORT_SYMBOL(sdp_micom_send_cmd);

void sdp_micom_send_cmd_sync(char cmd, char ack, char *data, int len)
{
	sdp_micom_send_cmd_ack( cmd, ack, data, len);
}
EXPORT_SYMBOL(sdp_micom_send_cmd_sync);

int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len)
{
	struct sdp_micom_port *port = micom_port;
	int ret = 0, sdpRtn = 0, retry = SDP_MICOM_ACK_RETRY;

	if (port->block_flag == NOBLOCK) {
		mutex_lock(&micom_cmd_lock);

		while(retry--) {
			mutex_lock(&micom_complete_lock);
			init_completion(&port->complete);
			port->ack = ack;
			mutex_unlock(&micom_complete_lock);

			if (port->ack == 0x30 && 0 != micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER].length) {
				//printk("[MICOM][%s():%d]MICOM_CACHED_MSG_MICOM_VER send cached data\n", __FUNCTION__, __LINE__);
				sdp_micom_handle_cmd(port, &(micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER]), SDP_MICOM_DEV_MSG);
			} else if (port->ack == 0xC6 && 0 != micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO].length) {
				//printk("[MICOM][%s():%d]MICOM_CACHED_MSG_PRODUCT_INFO send cached data\n", __FUNCTION__, __LINE__);
				sdp_micom_handle_cmd(port, &(micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO]), SDP_MICOM_DEV_MSG);
			} else
				ret = sdp_micom_send_cmd_no_lock(cmd, data, len);

			if (!ret) {
				ret = wait_for_completion_interruptible_timeout(&port->complete,
						msecs_to_jiffies(400));

				port->ack = 0;

				if (-ERESTARTSYS == ret) {
					dev_err(port->dev, "sdp-micom: Interrupt is happened by another cause(ex>freeze for suspend)(%d)(cmd:0x%x)\n", ret, cmd);
					sdpRtn = -EINTR;
					break;
				}
				else if (!ret) {
					if (!retry) {
						dev_err(port->dev, "sdp-micom: Timeout error - command 0x%x\n", cmd);
						sdpRtn = -1;
					}
				} else {
					/* get ack */
					sdpRtn = 0;
					break;
				}
			}
			else {
				port->ack = 0;
				dev_err(port->dev, "sdp-micom: Failed to send Command(0x%02x)\n", cmd);
				sdpRtn = -1;
			}
		}

		mutex_unlock(&micom_cmd_lock);
	}
	return sdpRtn;
}
EXPORT_SYMBOL(sdp_micom_send_cmd_ack);

void sdp_micom_send_cmd_sync_data(char cmd, char ack, char *data,
						int len, char *ack_data)
{
	sdp_micom_send_cmd_ack_data( cmd, ack, data, len, ack_data);
}
EXPORT_SYMBOL(sdp_micom_send_cmd_sync_data);

int sdp_micom_send_cmd_ack_data(char cmd, char ack, char *data,
						int len, char *ack_data)
{
	struct sdp_micom_port *port = micom_port;
	int rtn = 0;

	if (port->block_flag == NOBLOCK) {
		rtn = sdp_micom_send_cmd_ack(cmd, ack, data, len);
		if (!rtn)
			memcpy(ack_data, port->ack_data, SDP_MICOM_DATA_LEN);
	}
	return rtn;
}
EXPORT_SYMBOL(sdp_micom_send_cmd_ack_data);

int sdp_micom_send_msg(struct sdp_micom_msg *msg)
{
	struct sdp_micom_port *port = micom_port;
	char buf[KEY_CEC_SIZE
		+ KEY_PACKET_HEADER_SIZE
		+ KEY_PACKET_CHECKSUM_SIZE];
	int total_len = 0, buf_checksum = 0;
	unsigned int sentByte = 0;
	int rtn = 0;

	if ((!msg) || (msg->length <= 0)) {
		dev_err(port->dev, "sdp-micom: empty message, aborting\n");
		return -1;
	}

	dev_dbg(port->dev, "sdp-micom: Packet received\n");

	if (msg->msg_type == MICOM_NORMAL_DATA) {
		if (msg->msg[0] == 0x20 && 0 != micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER].length) {
			//printk("[MICOM][%s():%d]MICOM_CACHED_MSG_MICOM_VER send cached data\n", __FUNCTION__, __LINE__);
			sdp_micom_handle_cmd(port, &(micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER]), SDP_MICOM_DEV_MSG);
			return 0;
		} else if (msg->msg[0] == 0xC6 && 0 != micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO].length) {
			//printk("[MICOM][%s():%d]MICOM_CACHED_MSG_PRODUCT_INFO send cached data\n", __FUNCTION__, __LINE__);
			sdp_micom_handle_cmd(port, &(micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO]), SDP_MICOM_DEV_MSG);
			return 0;
		}
	}

	/* Fill packet headers */
	buf[0] = SDP_MICOM_CMD_PREFIX;

	switch (msg->msg_type) {
	case MICOM_NORMAL_DATA:
		buf[1] = SDP_MICOM_CMD_PREFIX;
		total_len = KEY_PACKET_SIZE;
		break;
	case MICOM_CEC_DATA:
		buf[1] = SDP_MICOM_CMD_CEC_PREFIX;
		total_len = KEY_PACKET_HEADER_SIZE + msg->length +
			KEY_PACKET_CHECKSUM_SIZE;
		break;
	case MICOM_ISP_DATA:
		total_len = msg->length;
		break;
	default:
		dev_err(port->dev, "sdp-micom: datatype 0x%X not supported\n",
				 msg->msg_type);
		return -1;
	}

	/* ISP data has different header length and no checksum is required,
	 * hence different treatment to ISP data
	 **/
	if ((msg->msg_type == MICOM_ISP_DATA) && (port->block_flag == BLOCK)) {
		/* Overwriting buf again as the complete ISP packet is sent by
		 * user
		 **/
		memcpy(&buf[0], msg->msg, msg->length);

		/* write the packet onto micom uart line */
		sentByte = sdp_micom_write(port, buf, total_len);
	} else if (port->block_flag == NOBLOCK) {
		/* copy the buffer data and calculate its checksum */
		memcpy(&buf[KEY_PACKET_HEADER_SIZE], msg->msg, msg->length);
		buf_checksum = sdp_micom_checksum_cal(msg->msg, msg->msg_type,
								msg->length);

		/* fill the checksum at last byte */
		buf[total_len - 1] = buf_checksum;

		/* write the packet onto micom uart line */
		sentByte = sdp_micom_write(port, buf, total_len);
	}
	if( sentByte != total_len ) {
		dev_err(port->dev, "sdp-micom: Failed to write all of data(send_msg)(cmd:0x%02x,sent:%d,len:%d)\n", buf[2], sentByte, total_len);
		rtn = -1;
	}
	else {
		rtn = 0;
	}

	dev_dbg(port->dev, "sdp-micom: Packet written on micom uart\n");
	return rtn;
}
EXPORT_SYMBOL(sdp_micom_send_msg);

void sdp_micom_msg_lock(void)
{
	mutex_lock(&micom_cmd_lock);
}
EXPORT_SYMBOL(sdp_micom_msg_lock);

void sdp_micom_msg_unlock(void)
{
	mutex_unlock(&micom_cmd_lock);
}
EXPORT_SYMBOL(sdp_micom_msg_unlock);

int sdp_micom_set_baudrate(int baud)
{
	struct sdp_micom_port *port = micom_port;
	struct clk *clk = ERR_PTR(-EINVAL);
	unsigned int quot;
	unsigned long rate;
	unsigned long dlab_flag = 0;
	int ret = 0;

	clk = devm_clk_get(port->dev, "apb_pclk");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(port->dev, "failed to get apb_pclk: %d\n", ret);
		return ret;
	}

	rate = clk_get_rate(clk);
	quot = (rate / (baud / 10 * 16) - 5) / 10;

	dlab_flag = rd_regl(port, SDP_ULCON);

	dev_dbg(port->dev, "SDP_ULCON: %lu\n", dlab_flag);
	dlab_flag |= SDP_ULCON_DLAB;
	wr_regl(port, SDP_ULCON, dlab_flag);
	usleep_range(10000, 15000);

	dev_dbg(port->dev, "SDP_ULCON: %lu\n",
			(unsigned long)rd_regl(port, SDP_ULCON));

	wr_regl(port, SDP_UBRDIV, quot);

	dlab_flag &= ~SDP_ULCON_DLAB;
	wr_regl(port, SDP_ULCON, dlab_flag);
	dev_dbg(port->dev, "SDP_ULCON: %lu\n",
			(unsigned long)rd_regl(port, SDP_ULCON));

	sdp_micom_resetport(port);
	port->count = 0;

	return ret;
}
EXPORT_SYMBOL(sdp_micom_set_baudrate);

int sdp_micom_get_baudrate(int *baud)
{
	struct sdp_micom_port *port = micom_port;
	struct clk *clk = ERR_PTR(-EINVAL);
	unsigned int quot;
	unsigned long rate;
	int ret = 0;

	clk = devm_clk_get(port->dev, "apb_pclk");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(port->dev, "failed to get apb_pclk: %d\n", ret);
		return ret;
	}

	rate = clk_get_rate(clk);

	quot = rd_regl(port, SDP_UBRDIV);

	/* Calculating as per sdp_serial_get_options() inside sdp.c */
	*baud = rate / (16 * (quot + 1));

	return ret;
}
EXPORT_SYMBOL(sdp_micom_get_baudrate);

void sdp_micom_send_block_msg(enum block_cmd cmd)
{
	struct sdp_micom_port *port = micom_port;
	switch (cmd) {
	case BLOCK:
		/* fall through */
	case NOBLOCK:
		port->block_flag = cmd;
		break;
	default:
		dev_warn(port->dev, "invalid block message 0x%02X\n", cmd);
		break;
	}
}
EXPORT_SYMBOL(sdp_micom_send_block_msg);

void sdp_micom_set_isp_response(int isp_resp)
{
	struct sdp_micom_port *port = micom_port;

	port->long_isp_resp = isp_resp;
}
EXPORT_SYMBOL(sdp_micom_set_isp_response);

int sdp_micom_register_cb(struct sdp_micom_cb *cb)
{
	struct sdp_micom_port *port = micom_port;

	if (!cb)
		return -EINVAL;

	if ((cb->id >= SDP_MICOM_DEV_NUM) || (cb->id < 0))
		return -EINVAL;

	port->cb[cb->id] = cb;

	dev_info(port->dev, "callback registered, device%d %s\n",
			cb->id, cb->name);

	return 0;
}
EXPORT_SYMBOL(sdp_micom_register_cb);

void sdp_micom_panel_key_lock(int lock)
{
	struct sdp_micom_port *port = micom_port;
	port->panel_key_lock = lock;
}
EXPORT_SYMBOL(sdp_micom_panel_key_lock);

void sdp_micom_set_ar_mode(int flag)
{
	struct sdp_micom_port *port = micom_port;
	port->ar_mode = flag;
}
EXPORT_SYMBOL(sdp_micom_set_ar_mode);

static void sdp_micom_cb(struct sdp_micom_port *port,
			struct sdp_micom_msg *msg, int id)
{
	struct sdp_micom_cb *cb = port->cb[id];

	/* print for debug */
	dev_dbg(port->dev, "sending msg to cb[%d]\n", id);

	if (!cb || !(cb->cb) || cb->id != id) {
		dev_err(port->dev, "device%d did not registered completely\n",
				id);
		return;
	}

	cb->cb(msg, cb->dev_id);
}

static void sdp_micom_handle_ir_cmd(struct sdp_micom_port *port,
					struct sdp_micom_msg *msg)
{
	struct sdp_micom_msg tmp_msg;

	switch(msg->msg[1]) {
	case DISCRET_POWER_OFF:
	case DISCRET_POWER_ON:
	case DISCRET_VIDEO1:
	case DISCRET_VIDEO2:
	case DISCRET_VIDEO3:
	case DISCRET_S_VIDEO1:
	case DISCRET_S_VIDEO2:
	case DISCRET_S_VIDEO3:
	case DISCRET_COMPONENT1:
	case DISCRET_COMPONENT2:
	case DISCRET_COMPONENT3:
	case DISCRET_HDMI1:
	case DISCRET_HDMI2:
	case DISCRET_HDMI3:
	case DISCRET_PC:
	case DISCRET_DVI1:
	case DISCRET_DVI2:
	case DISCRET_ZOOM1:
	case DISCRET_ZOOM2:
	case DISCRET_PANORAMA:
	case DISCRET_4_3:
	case DISCRET_16_9:
		tmp_msg.msg_type = msg->msg_type;
		tmp_msg.length = msg->length;
		tmp_msg.msg[0] = SDP_MICOM_ACK_AUTO_REMOCON;
		tmp_msg.msg[1] = 0xDC;
		tmp_msg.msg[2] = msg->msg[1];
		tmp_msg.msg[3] = (msg->msg[0] == SDP_MICOM_EVT_KEYPRESS) ? 1 : 0;
		tmp_msg.msg[4] = 0;
		tmp_msg.msg[5] = 0;
		sdp_micom_cb(port, &tmp_msg, SDP_MICOM_DEV_AR);
		break;
	default:
		sdp_micom_cb(port, msg, SDP_MICOM_DEV_IR);
		break;
	}
}

static void sdp_micom_handle_cmd(struct sdp_micom_port *port,
					struct sdp_micom_msg *msg, int id)
{
	static int pre_key = SDP_MICOM_DEV_IR;
	static int pre_rc = SDP_MICOM_DEV_IR;
	struct sdp_micom_msg tmp_msg;

	if (msg == NULL) {
		dev_err(port->dev, "invalid micom msg. aborting\n");
		return;
	}

	if ((id == SDP_MICOM_DEV_ISP) && (port->block_flag == BLOCK)) {
		dev_dbg(port->dev, "received ISP data from micom.\n");
		sdp_micom_cb(port, msg, SDP_MICOM_DEV_ISP);
	} else if ((id == SDP_MICOM_DEV_CEC) && (port->block_flag == NOBLOCK)) {
		dev_dbg(port->dev, "received CEC data from micom.\n");
		sdp_micom_cb(port, msg, SDP_MICOM_DEV_CEC);
	} else if (port->block_flag == NOBLOCK) {

		//Save Static Micom Msg Data for performance
		switch( msg->msg[0] ) {
		case 0x30://CTM_MICOM_VERSION(cmd:0x20,ack:0x30)
			if( 0 == micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER].length ) {
				//printk("[MICOM]Save Cache (MICOM_CACHED_MSG_MICOM_VER)\n", __FUNCTION__, __LINE__ );
				memcpy( &(micom_cached_msg_data_list[MICOM_CACHED_MSG_MICOM_VER]), msg, sizeof(struct sdp_micom_msg));
			}
			break;
		case 0xC6://CTM_GET_TV_SIDE_PRODUCT_INFO(cmd:0xC6,ack:0xC6)
			if( 0 == micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO].length ) {
				//printk("[MICOM]Save Cache (MICOM_CACHED_MSG_PRODUCT_INFO)\n", __FUNCTION__, __LINE__ );
				memcpy( &(micom_cached_msg_data_list[MICOM_CACHED_MSG_PRODUCT_INFO]), msg, sizeof(struct sdp_micom_msg));
			}
			break;
		default:
			/* Do Nothing */
			break;
		}

		switch (msg->msg[0]) {
		case SDP_MICOM_EVT_KEYPRESS:
		/* fall through */
		case SDP_MICOM_EVT_PANEL_KEYPRESS:
		/* fall through */
		case SDP_MICOM_EVT_KEYRELEASE:
			/* TEMP : turn off directly for demo */
			/* if (msg->msg[1] == 0x02)
				sdp_micom_send_cmd(SDP_MICOM_CMD_POWEROFF,
								NULL, 0);*/
			/* send destroy AR env message */
			if (port->ar_mode || (pre_key == SDP_MICOM_DEV_AR)) {
				if ((msg->msg[0] == SDP_MICOM_EVT_KEYPRESS) ||
					((port->panel_key_lock == 0) &&
					(msg->msg[0] == SDP_MICOM_EVT_KEYPRESS))) {
					tmp_msg.msg_type = msg->msg_type;
					tmp_msg.length = msg->length;
					tmp_msg.msg[0] = SDP_MICOM_ACK_AUTO_REMOCON;
					tmp_msg.msg[1] = 0x1F;
					tmp_msg.msg[2] = 0xFF;
					tmp_msg.msg[3] = 0;
					tmp_msg.msg[4] = 0;
					tmp_msg.msg[5] = 0;
					sdp_micom_cb(port, &tmp_msg, SDP_MICOM_DEV_AR);
					pre_key = SDP_MICOM_DEV_IR;
					port->ar_mode = 0;
				}
			}

			if (msg->msg[0] == SDP_MICOM_EVT_KEYPRESS)
				pre_rc = SDP_MICOM_DEV_IR;
			else if (msg->msg[0] == SDP_MICOM_EVT_PANEL_KEYPRESS)
				pre_rc = SDP_MICOM_DEV_PANEL;

			/* generic key press/release events */
			if (pre_rc == SDP_MICOM_DEV_IR)
				sdp_micom_handle_ir_cmd(port, msg);
			else {
				/* autoremocon's request */
				if (port->panel_key_lock == 0)
					sdp_micom_cb(port, msg, SDP_MICOM_DEV_PANEL);
				else {
					tmp_msg.msg_type = msg->msg_type;
					tmp_msg.length = msg->length;
					tmp_msg.msg[2] = msg->msg[0];
					tmp_msg.msg[3] = msg->msg[1];
					tmp_msg.msg[0] = SDP_MICOM_ACK_AUTO_REMOCON;
					tmp_msg.msg[1] = 0x1E;
					tmp_msg.msg[4] = 0;
					tmp_msg.msg[5] = 0;
					sdp_micom_cb(port, &tmp_msg, SDP_MICOM_DEV_AR);
				}
			}
			break;
		case SDP_MICOM_ACK_VERSION:
			dev_info(port->dev, "TV Side MICOM version %04d(%d), JP Side MICOM version %04d(%d)\n",
					((msg->msg[2] << 8) + msg->msg[1])&0x7FFF, !!(((msg->msg[2] << 8) + msg->msg[1])&0x8000),
					((msg->msg[4] << 8) + msg->msg[3])&0x7FFF, !!(((msg->msg[4] << 8) + msg->msg[3])&0x8000));
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_MSG);
			break;
		case SDP_MICOM_ACK_GET_TIME:
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_RTC);
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_MSG);
			break;
		case SDP_MICOM_ACK_POWERON_MODE:
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_SYSTEM);
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_MSG);
			break;
		case SDP_MICOM_BRIGHT_SENSOR_VALUE:
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_BS);
			break;
		case SDP_MICOM_ACK_AUTO_REMOCON:
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_AR);
			/* set current mode to AR mode */
			pre_key = SDP_MICOM_DEV_AR;
			break;
		case SDP_MICOM_ACK_BATTERYPACK_STATUS:
			sdp_micom_cb( port, msg, SDP_MICOM_DEV_BATTERY);
			break;
		case SDP_MICOM_ACK_POWEROFF:
			/* fall through */
		case SDP_MICOM_ACK_ENABLE_ONTIMER:
			/* fall through */
		case SDP_MICOM_ACK_DISABLE_ONTIMER:
			/* fall through */
		case SDP_MICOM_ACK_SET_TIME:
			/* fall through */
		case SDP_MICOM_ACK_RESTART:
			/* fall through */
		case SDP_MICOM_ACK_LED_ON:
			/* fall through */
		case SDP_MICOM_ACK_LED_OFF:
			/* fall through */
		case SDP_MICOM_ACK_SW_PVCC:
			/* fall through */
		case SDP_MICOM_ACK_SW_INVERTER:
			sdp_micom_cb(port, msg, SDP_MICOM_DEV_MSG);
			break;
		default:
			/* these events will be handled by
				normal msg callback */
			dev_dbg(port->dev, "cmd 0x%x\n", msg->msg[0]);
			/* Special event has been notified.
			* now notifying normal events
			*/
			sdp_micom_cb(port, msg, id);
		}

		mutex_lock(&micom_complete_lock);
		if (port->ack && (msg->msg[0] == port->ack)) {
			memcpy(port->ack_data, &msg->msg[1], SDP_MICOM_DATA_LEN);
			complete(&port->complete);
			port->ack = 0;
		}
		mutex_unlock(&micom_complete_lock);
	}
}

static void sdp_micom_work(struct sdp_micom_port *port)
{

	struct sdp_micom_msg msg;	/* forming full micom msg in here */
	char checksum = 0x00;
	int devid = -1;
	unsigned int cmd_len = KEY_PACKET_HEADER_SIZE + 1;
	int len;

	while (port->pos < port->count) {
		len = port->count - port->pos;

		if (len < cmd_len) /* need more data */
			goto out;

/*		if (port->buf[port->pos] != 0xFF) {
			dev_info(port->dev, "received data:");
			for (i = 0; i < len; i++)
				dev_info(port->dev, "data[%d]: 0x%02X",
							i, port->buf[i]);
		}
*/

		/* Since the ISP packet differs from normal and cec packets with
		 * reference to header size and checksum check, isp packets will
		 * be handled separately */
		/* if packet is normal or cec */
		if (port->buf[port->pos] == SDP_MICOM_CMD_PREFIX) {
			switch (port->buf[port->pos + 1]) {
			case SDP_MICOM_CMD_PREFIX:
				/* packet contains normal buffer */
				devid = SDP_MICOM_DEV_MSG;

				/* header size is 2 bytes and checksum is stored
				 * in 1 byte. */
				/* refer to TDcSamMicomInterface.cpp */
				cmd_len = KEY_PACKET_HEADER_SIZE
						+ KEY_PACKET_DATA_SIZE
						+ KEY_PACKET_CHECKSUM_SIZE;

				if (len < cmd_len) /* need more data */
					goto out;

				/* filling out the micom normal msg */
				msg.msg_type = MICOM_NORMAL_DATA;
				msg.length = KEY_PACKET_DATA_SIZE;
				memcpy(&(msg.msg),
					&(port->buf[port->pos
						+ KEY_PACKET_HEADER_SIZE]),
					(size_t)msg.length);

				/* calculate packet checksum */
				checksum = sdp_micom_checksum_cal(
						&(port->buf[port->pos
						+ KEY_PACKET_HEADER_SIZE]),
						msg.msg_type,
						msg.length);
				break;

			case SDP_MICOM_CMD_CEC_PREFIX:
				/* packet contains cec buffer */
				devid = SDP_MICOM_DEV_CEC;

				/* header size is 2 bytes and checksum is stored
				 * in 1 byte. */
				/* refer to TDcSamMicomInterface.cpp */
				cmd_len = KEY_PACKET_HEADER_SIZE + 1
					+ (port->buf[port->pos
					+ KEY_PACKET_HEADER_SIZE] & 0x1f)
					+ KEY_PACKET_CHECKSUM_SIZE;
				if (len < cmd_len)
					goto out;

				/* filling out the micom cec msg */
				msg.msg_type = MICOM_CEC_DATA;
				msg.length = 1 + (port->buf[port->pos
					+ KEY_PACKET_HEADER_SIZE] & 0x1f);
				memcpy(&(msg.msg),
					&(port->buf[port->pos
						+ KEY_PACKET_HEADER_SIZE]),
					(size_t)msg.length);

				/* calculate packet checksum */
				checksum = sdp_micom_checksum_cal(
						&(port->buf[port->pos
						+ KEY_PACKET_HEADER_SIZE]),
						msg.msg_type,
						msg.length);
				break;

			default:
				/* unhandled data print out for debugging and
				 * update the size */
				dev_dbg(port->dev, "prefix 0x%x\n",
						port->buf[port->pos + 1]);
				goto out_reset;
			}

			/* Checksum verification */
			if (checksum != port->buf[port->pos + cmd_len - 1]) {
				dev_err(port->dev,
					"cs err. cal[%02X]!=mic[%02X]\n",
					checksum,
					port->buf[port->pos + cmd_len - 1]);
				goto out_reset;
			}
		} else if (port->buf[port->pos] == SDP_MICOM_CMD_ISP_PREFIX) {
			/* if packet is isp [0xFF][0x80][ACK], just copy and
			 * send the packet for further processing */
			devid = SDP_MICOM_DEV_ISP;

			/* is long isp response expected? */
			cmd_len = (port->long_isp_resp == 0) ?
					(KEY_PACKET_DATA_SIZE + 1) :
					(KEY_ISP_READ_PACKET_SIZE);

			if (len < cmd_len)
				goto out;

			/* filling out the micom isp msg */
			msg.msg_type = MICOM_ISP_DATA;
			/* leave the isp header [0x0F] and transfer rest
			 * of the message */
			msg.length = cmd_len - 1;
			memcpy(&(msg.msg), &(port->buf[port->pos + 1]),
					(size_t)msg.length);
		} else {
			goto out_reset;
		}

		sdp_micom_handle_cmd(port, &msg, devid);

		port->pos += cmd_len;
	}

out_reset:
	port->count = 0;
	port->pos = 0;
	port->long_isp_resp = 0;

out:
	return;
}

static int sdp_micom_rx_fifocnt(struct sdp_micom_port *port,
				unsigned long ufstat)
{
	if (ufstat & SDP_UFSTAT_RXFIFO_FULL)
		return SDP_MICOM_FIFO_SIZE;

	return ufstat & SDP_UFSTAT_RXFIFO_MASK;
}

static void sdp_micom_rx_chars(struct sdp_micom_port *port)
{
	unsigned int ufstat;
	int max_count = SDP_MICOM_BUF_SIZE - port->count;

	while (max_count-- > 0) {
		ufstat = rd_regl(port, SDP_UFSTAT);

		if (sdp_micom_rx_fifocnt(port, ufstat) == 0)
			break;

		port->buf[port->count] = rd_regb(port, SDP_URXH);
		port->count++;
	}
}

static irqreturn_t sdp_micom_handle_irq(int irq, void *id)
{
	struct sdp_micom_port *port = id;
	unsigned int pend = rd_regl(port, SDP_UTRSTAT);

	if (pend & SDP_UTRSTAT_RXI) {
		sdp_micom_rx_chars(port);
		dev_dbg(port->dev, "before : pos[%d], count[%d]\n",
					port->pos, port->count);
		sdp_micom_work(port);
		dev_dbg(port->dev, "after  : pos[%d], count[%d]\n",
					port->pos, port->count);
	}

	wr_regl(port, SDP_UTRSTAT, pend);

	return IRQ_HANDLED;
}

static void __init sdp_micom_setup(struct sdp_micom_port *port)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	unsigned int quot;
	unsigned int ulcon;
	unsigned long rate;

	clk = devm_clk_get(port->dev, "apb_pclk");
	if (IS_ERR(clk))
		return;

	rate = clk_get_rate(clk);
	quot = (rate / (SDP_MICOM_BAUDRATE / 10 * 16) - 5) / 10;

	if (port->baudclk != clk) {
		if (!IS_ERR(port->baudclk)) {
			clk_disable_unprepare(port->baudclk);
			port->baudclk = ERR_PTR(-EINVAL);
		}

		clk_prepare_enable(clk);

		port->baudclk = clk;
	}

	ulcon = SDP_ULCON_CS8 | SDP_ULCON_PNONE;

	wr_regl(port, SDP_ULCON, ulcon);
	wr_regl(port, SDP_UBRDIV, quot);
}

static int __init sdp_micom_init_port(struct sdp_micom_port *port,
				    struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	if (pdev == NULL)
		return -ENODEV;

	/* setup info for port */
	port->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(port->dev, "failed to find memory resource for uart\n");
		return -EINVAL;
	}

	port->regs = SDP_VA_UART + (res->start & 0xff);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(port->dev, "failed to find irq resource for uart\n");
		return -EINVAL;
	}

	port->irq = ret;

	port->clk = devm_clk_get(port->dev, "rstn_uart");
	if (IS_ERR(port->clk))
		dev_err(port->dev, "failed to find uart clock\n");
	else
		clk_prepare_enable(port->clk);

	port->buf = devm_kzalloc(&pdev->dev, SDP_MICOM_BUF_SIZE, GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->baudclk = ERR_PTR(-EINVAL);

	sdp_micom_resetport(port);

	/* set baudrate and options for uart port */
	sdp_micom_setup(port);

	ret = devm_request_threaded_irq(port->dev, port->irq, NULL,
			sdp_micom_handle_irq, IRQF_ONESHOT,
			dev_name(port->dev), port);
	if (ret) {
		dev_err(port->dev, "cannot get irq %d\n", port->irq);
		return ret;
	}

	/* clear RX IRQ flag */
	wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI);

	return 0;
}

void sdp_micom_poweroff(void)
{
	struct sdp_micom_port *port = micom_port;
	char buf[SDP_MICOM_CMD_LEN] = {0,};

	printk(KERN_ERR "\n\n>>> micom power off (requested:%s) <<<\n\n", current->comm);

	do
	{
		buf[0] = SDP_MICOM_CMD_PREFIX;
		buf[1] = SDP_MICOM_CMD_PREFIX;
		buf[2] = SDP_MICOM_CMD_POWEROFF;
		buf[8] = SDP_MICOM_CMD_POWEROFF;	//checksum
		sdp_micom_write_direct(port, buf, SDP_MICOM_CMD_LEN);
		printk("finish to try to send power off cmd.\n");
		usleep_range(200*1000, 250*1000);
	}while(1);
}

void sdp_micom_restart(char str, const char *cmd)
{
	struct sdp_micom_port *port = micom_port;
	char buf[SDP_MICOM_CMD_LEN] = {0,};

	printk(KERN_ERR "\n\n>>> micom restart (requested:%s) <<<\n\n", current->comm);

	do
	{
		buf[0] = SDP_MICOM_CMD_PREFIX;
		buf[1] = SDP_MICOM_CMD_PREFIX;
		buf[2] = SDP_MICOM_CMD_RESTART;
		buf[8] = SDP_MICOM_CMD_RESTART;		//checksum
		sdp_micom_write_direct(port, buf, SDP_MICOM_CMD_LEN);
		printk("finish to try to send restart cmd.\n");
		usleep_range(200*1000, 250*1000);
	}while(1);
}

static int sdp_micom_probe(struct platform_device *pdev)
{
	struct sdp_micom_port *port;
	int ret;

	port = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	platform_set_drvdata(pdev, port);
	micom_port = port;

	ret = sdp_micom_init_port(port, pdev);
	if (ret < 0)
		return ret;

	ret = mfd_add_devices(port->dev, -1, sdp_micom_devs,
			      ARRAY_SIZE(sdp_micom_devs), NULL, 0, NULL);

	if (ret < 0) {
		dev_err(port->dev, "failed to add devices (%d)\n", ret);
		mfd_remove_devices(port->dev);
	}

	port->block_flag = NOBLOCK;
	port->long_isp_resp = 0;

	dev_info(port->dev, "MICOM multi function device, irq %d\n", port->irq);

	/* get micom version */
	init_completion(&port->complete);           //SDP_MICOM_CMD_VERSION is sync message.
	                                            //But for performance, it doesn't use sync func in probe.
	                                            //So it needs completion initialize itself.
	sdp_micom_send_cmd(SDP_MICOM_CMD_VERSION, NULL, 0);

	pm_power_off = sdp_micom_poweroff;
	arm_pm_restart = sdp_micom_restart;

	//Clear cached msg buffer
	memset( micom_cached_msg_data_list, 0x00, sizeof(struct sdp_micom_msg) * MICOM_CACHED_MSG_MAX);

	return 0;
}

static int sdp_micom_remove(struct platform_device *pdev)
{
	struct sdp_micom_port *port = platform_get_drvdata(pdev);

	clk_disable_unprepare(port->clk);
	devm_clk_put(port->dev, port->clk);

	mfd_remove_devices(port->dev);

	return 0;
}

static int sdp_micom_suspend(struct device *dev)
{
	struct sdp_micom_port *port = micom_port;

	unsigned long start_time = jiffies;

	printk(KERN_INFO "@suspend_in/micom\n");

	if (!port)
	{
		dev_err(port->dev, "Failed Micom Uart Suspend. Wrong Param(NULL port)\n");
		return 0;
	}

	mutex_lock(&micom_lock);
	dev_dbg(port->dev, "Micom Uart Suspend Start...!!\n");

	port->ulcon = rd_regl(port, SDP_ULCON);
	port->ucon = rd_regl(port, SDP_UCON);
	port->ubrdiv = rd_regl(port, SDP_UBRDIV);

	printk(KERN_INFO "@suspend_time/micom/%d/msec\n",
		jiffies_to_msecs(jiffies - start_time));

	return 0;
}

static int sdp_micom_resume(struct device *dev)
{
	struct sdp_micom_port *port = micom_port;

	unsigned long start_time = jiffies;

	printk(KERN_INFO "@resume_in/micom\n");

	if (!port)
	{
		dev_err(port->dev, "Failed Micom Messagebox Resume. Wrong Param(NULL port)\n");
		mutex_unlock(&micom_lock);
		return 0;
	}

	wr_regl(port, SDP_ULCON, port->ulcon);
	wr_regl(port, SDP_UCON, port->ucon);
	wr_regl(port, SDP_UBRDIV, port->ubrdiv);

	sdp_micom_resetport(port);
	wr_regl(port, SDP_UTRSTAT, SDP_UTRSTAT_RXI | SDP_UTRSTAT_TXI | SDP_UTRSTAT_ERRI);	//Clear Flags
	port->count = 0;

	dev_dbg(port->dev, "Micom Uart Suspend Finish...!!\n");
	mutex_unlock(&micom_lock);

	printk(KERN_INFO "@resume_time/micom/%d/msec\n",
		jiffies_to_msecs(jiffies - start_time));

	return 0;
}

static const struct dev_pm_ops sdp_micom_pm_ops = {
	.suspend = sdp_micom_suspend,
	.resume = sdp_micom_resume,
};

static const struct of_device_id sdp_micom_dt_match[] = {
	{ .compatible = "samsung,sdp1304-uart-micom" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_micom_dt_match);

static struct platform_driver sdp_micom_driver = {
	.probe		= sdp_micom_probe,
	.remove		= sdp_micom_remove,
	.driver		= {
		.name	= "sdp-uart-micom",
		.owner	= THIS_MODULE,
		.pm	= &sdp_micom_pm_ops,
		.of_match_table	= sdp_micom_dt_match,
	},
};

static int __init sdp_micom_init(void)
{
	return platform_driver_register(&sdp_micom_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(sdp_micom_init);

static void __exit sdp_micom_exit(void)
{
	platform_driver_unregister(&sdp_micom_driver);
}
module_exit(sdp_micom_exit);

MODULE_DESCRIPTION("Samsung SDP MICOM driver");
MODULE_LICENSE("GPL");
