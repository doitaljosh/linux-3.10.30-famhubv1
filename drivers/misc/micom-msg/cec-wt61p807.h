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
 * @file	cec-wt61p807.h
 * @briefi	header for cec-wt61p807 driver
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/08/05
 *
 */

/* internal Release1 */

#ifndef __MICOM_CEC_H
#define __MICOM_CEC_H

#include <linux/mfd/sdp_micom.h>

#define DRIVER_NAME		"cec-wt61p807"
#define DEV_NAME		"micom-cec"
/* micom cec queue size */
#define MICOM_CEC_QUEUE_SIZE	3
/* micom cec packet length = total length - 2 (header) - 1 (checksum) */
#define MICOM_CEC_PACKET_LEN(x)		\
			(x - KEY_PACKET_HEADER_SIZE - KEY_PACKET_CHECKSUM_SIZE)

enum {
	FALSE,
	TRUE
};

/* contains device information */
struct wt61p807_cec_data {
	struct cdev *cec_dev;
	struct class *cec_class;
	struct device *cec_device;
	int micom_cec_major;
	int ref_cnt;
};

#endif
