/*
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * Copyright (c) 2013 Samsung R&D Institute India-Delhi.
 * Author: Abhishek Jaiswal <abhishek1.j@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * @file	msg-wt61p807.h
 * @brief	header for msg-wt61p807 driver
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/07/30
 *
 */

/* internal Release1 */

#ifndef __MICOM_MSG_H
#define __MICOM_MSG_H

#include<asm-generic/ioctl.h>

#define KEY_PACKET_PARAM_SIZE		5
#define KEY_ISP_READ_PACKET_SIZE	12

enum block_cmd {
	BLOCK,
	NOBLOCK,
};

struct sdp_micom_usr_msg {
	char cmd;
	char cmd_ack;
	char input_param[KEY_PACKET_PARAM_SIZE];
	char output_param[KEY_PACKET_PARAM_SIZE];
};

struct sdp_micom_usr_isp {
	/* baud_rate and isp data will be used in different ioctls */
	int baud_rate;
	enum block_cmd block_flag;
	/* for some isp commands submicom response is 12 bytes long (including
	 * 1 byte header) */
	int is_long_resp;
	char ack;
	int input_data_size;
	char input_data[KEY_ISP_READ_PACKET_SIZE];
	char output_data[KEY_ISP_READ_PACKET_SIZE];
};

/* MSG IOCTL base */
#define MICOM_MSG_IOCTL_IOCBASE		0xB3
/* IOCTL list for micom msg driver */
#define MICOM_MSG_IOCTL_SEND_MSG_NO_ACK	_IOWR(MICOM_MSG_IOCTL_IOCBASE, 0x00, \
						struct sdp_micom_usr_msg)
#define MICOM_MSG_IOCTL_SEND_MSG	_IOWR(MICOM_MSG_IOCTL_IOCBASE, 0x01, \
						struct sdp_micom_usr_msg)
/* ISP IOCTL base */
#define MICOM_ISP_IOCTL_IOCBASE		0xB4
/* IOCTL list for micom isp driver */
#define MICOM_ISP_IOCTL_SEND_ARRAY_DATA	_IOWR(MICOM_ISP_IOCTL_IOCBASE, 0x00, \
						struct sdp_micom_usr_isp)
#define MICOM_ISP_IOCTL_SET_BAUDRATE	_IOWR(MICOM_ISP_IOCTL_IOCBASE, 0x01, \
						struct sdp_micom_usr_isp)
#define MICOM_ISP_IOCTL_GET_BAUDRATE	_IOWR(MICOM_ISP_IOCTL_IOCBASE, 0x02, \
						struct sdp_micom_usr_isp)
#define MICOM_ISP_IOCTL_SET_BLOCK	_IOWR(MICOM_ISP_IOCTL_IOCBASE, 0x03, \
						struct sdp_micom_usr_isp)

/* AR IOCTL base */
#define MICOM_AR_IOCTL_IOCBASE		0xB5
#define MICOM_AR_IOCTL_PANEL_LOCK	_IOWR(MICOM_AR_IOCTL_IOCBASE, 0x00, \
						int)
#define MICOM_AR_IOCTL_MANUAL_ON_OFF	_IOWR(MICOM_AR_IOCTL_IOCBASE, 0x01, \
						int)
#endif
