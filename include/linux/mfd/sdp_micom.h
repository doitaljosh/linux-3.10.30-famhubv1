/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_SDP_MICOM_H
#define __LINUX_MFD_SDP_MICOM_H

#include <uapi/linux/micom-msg.h>

/* to MICOM */
#define SDP_MICOM_CMD_POWERON_MODE		0X10
#define SDP_MICOM_CMD_POWEROFF			0x12
#define SDP_MICOM_CMD_ENABLE_ONTIMER		0x19
#define SDP_MICOM_CMD_DISABLE_ONTIMER		0x1A
#define SDP_MICOM_CMD_RESTART			0x1D
#define SDP_MICOM_CMD_VERSION			0x20
#define SDP_MICOM_CMD_BOOT_REASON		0x63
#define SDP_MICOM_CMD_LED_ON			0x29
#define SDP_MICOM_CMD_LED_OFF			0x2A
#define SDP_MICOM_CMD_GET_TIME			0x60
#define SDP_MICOM_CMD_SET_TIME			0x61
#define SDP_MICOM_CMD_SW_PVCC			0xD0
#define SDP_MICOM_CMD_SW_INVERTER		0xD1
#define SDP_MICOM_CMD_I_AM_OK           0x96
#define SDP_MICOM_CMD_GET_PROXIMITY_SENSOR	0x77
#define SDP_MICOM_CMD_SEND_WATCHDOG_MSG	0x99

/* from MICOM */
/* events*/
#define SDP_MICOM_EVT_KEYPRESS			0x10
#define SDP_MICOM_EVT_PANEL_KEYPRESS		0x11
#define SDP_MICOM_EVT_KEYRELEASE		0x1E
#define SDP_MICOM_BRIGHT_SENSOR_VALUE		0xDA

/* acks */
#define SDP_MICOM_ACK_AUTO_REMOCON		0x12
#define SDP_MICOM_ACK_POWERON_MODE		0x20
#define SDP_MICOM_ACK_POWEROFF			0x23
#define SDP_MICOM_ACK_BOOT_REASON		0x64
#define SDP_MICOM_ACK_ENABLE_ONTIMER		0x2A
#define SDP_MICOM_ACK_DISABLE_ONTIMER		0x2B
#define SDP_MICOM_ACK_RESTART			0x2E
#define SDP_MICOM_ACK_VERSION			0x30
#define SDP_MICOM_ACK_LED_ON			0x39
#define SDP_MICOM_ACK_LED_OFF			0x3A
#define SDP_MICOM_ACK_GET_TIME			0x50
#define SDP_MICOM_ACK_SET_TIME			0x51
#define SDP_MICOM_ACK_JACK_ID			0xA0
#define SDP_MICOM_ACK_SW_PVCC			0xD0
#define SDP_MICOM_ACK_SW_INVERTER		0xD1
#define SDP_MICOM_ACK_BATTERYPACK_STATUS	0xEE
#define SDP_MICOM_ACK_ARE_YOU_ALIVE     0x96
#define SDP_MICOM_ACK_GET_PROXIMITY_SENSOR	0x78
#define SDP_MICOM_ACK_WATCHDOG_MSG	0x99

/* discrete key */
#define DISCRET_POWER_OFF               0x98
#define DISCRET_POWER_ON                0x99
#define DISCRET_VIDEO1                  0x84
#define DISCRET_VIDEO2                  0xEB
#define DISCRET_VIDEO3                  0xEC
#define DISCRET_S_VIDEO1                0x85
#define DISCRET_S_VIDEO2                0xED
#define DISCRET_S_VIDEO3                0xFB
#define DISCRET_COMPONENT1              0x86
#define DISCRET_COMPONENT2              0x88
#define DISCRET_COMPONENT3              0xE8
#define DISCRET_HDMI1                   0xE9
#define DISCRET_HDMI2                   0xBE
#define DISCRET_HDMI3                   0xC2
#define DISCRET_PC                      0x69
#define DISCRET_DVI1                    0x8A
#define DISCRET_DVI2                    0xEA
#define DISCRET_ZOOM1                   0x53
#define DISCRET_ZOOM2                   0xE1
#define DISCRET_PANORAMA                0xE2
#define DISCRET_4_3                     0xE3
#define DISCRET_16_9                    0xE4

/* sdp micom packet size maximum
 * Assumption: Maximum packets handled
 * are 20 bytes long. This will be changed
 * when Hotel data and clone data will be
 * handled by relevant drivers.
 */
#define SDP_MICOM_PACKET_MAX_SIZE	20

enum sdp_micom_dev_id {
	SDP_MICOM_DEV_IR,
	SDP_MICOM_DEV_RTC,
	SDP_MICOM_DEV_SYSTEM,
	SDP_MICOM_DEV_MSG,
	SDP_MICOM_DEV_CEC,
	SDP_MICOM_DEV_BS,
	SDP_MICOM_DEV_ISP,
	SDP_MICOM_DEV_AR,
	SDP_MICOM_DEV_PANEL,
	SDP_MICOM_DEV_BATTERY,
	SDP_MICOM_DEV_SENSOR,
	SDP_MICOM_DEV_NUM,
};

enum sdp_micom_data_type {
	MICOM_NORMAL_DATA,
	MICOM_CEC_DATA,
	MICOM_CLONE_DATA,
	MICOM_HOTEL_DATA,
	MICOM_DUALTV_DATA,
	MICOM_NORMAL_ACK,
	MICOM_ISP_DATA,
};

enum {
	KEY_PACKET_CHECKSUM_SIZE	= 1,
	KEY_PACKET_HEADER_SIZE		= 2,
	KEY_BSENSOR_DATA_SIZE		= 4,
	KEY_PACKET_DATA_SIZE		= 6,
	KEY_ISP_READ_DATA_SIZE		= 8,
	KEY_PACKET_SIZE			= 9,
	KEY_CEC_SIZE			= 20,
	KEY_CLONE_SIZE			= 65,
	KEY_BUFFER_SIZE			= 512,
};

struct sdp_micom_msg {
	enum sdp_micom_data_type msg_type;
	unsigned short length;
	char msg[SDP_MICOM_PACKET_MAX_SIZE];
};

struct sdp_micom_cb {
	int id;
	char *name;
	void (*cb)(struct sdp_micom_msg *msg, void *dev_id);
	void *dev_id;
};

#ifdef CONFIG_MFD_SDP_MICOM
extern int sdp_micom_send_cmd(char cmd, char *data, int len);
extern void sdp_micom_send_cmd_sync(char cmd, char ack, char *data, int len);
extern int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len);
extern void sdp_micom_send_cmd_sync_data(char cmd, char ack, char *data,
						int len, char *ack_data);
extern int sdp_micom_send_cmd_ack_data(char cmd, char ack, char *data,
						int len, char *ack_data);
extern int sdp_micom_send_msg(struct sdp_micom_msg *msg);
extern void sdp_micom_msg_lock(void);
extern void sdp_micom_msg_unlock(void);
extern int sdp_micom_register_cb(struct sdp_micom_cb *cb);
extern int sdp_micom_set_baudrate(int baud_rate);
extern int sdp_micom_get_baudrate(int *baud_rate);
extern void sdp_micom_send_block_msg(enum block_cmd cmd);
extern void sdp_micom_set_isp_response(int isp_resp);
extern void sdp_micom_panel_key_lock(int lock);
extern void sdp_micom_set_ar_mode(int flag);
#else
static inline int sdp_micom_send_cmd(char cmd, char *data, int len) {}
static inline void sdp_micom_send_cmd_sync(char cmd, char ack, char *data, int len) {}
static inline int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len) {}
static inline void sdp_micom_send_cmd_sync_data(char cmd, char ack, char *data,
						int len, char *ack_data) {}
static inline int sdp_micom_send_cmd_ack_data(char cmd, char ack, char *data,
						int len, char *ack_data) {}
static inline int sdp_micom_send_msg(struct sdp_micom_msg *msg) {}
static inline void sdp_micom_msg_lock(void) {}
static inline void sdp_micom_msg_unlock(void) {}
extern int sdp_micom_register_cb(struct sdp_micom_cb *cb) {}
static int sdp_micom_set_baudrate(int baud_rate) {}
static int sdp_micom_get_baudrate(int *baud_rate) {}
static void sdp_micom_send_block_msg(enum block_cmd cmd) {}
static void sdp_micom_set_isp_response(int isp_resp) {}
static void sdp_micom_panel_key_lock(int lock) {}
static void sdp_micom_set_ar_mode(int flag) {}
#endif

#endif
