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
 * @file	isp-wt61p807.h
 * @brief	header for isp-wt61p807 driver
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/10/18
 *
 */

#ifndef __MICOM_ISP_H
#define __MICOM_ISP_H

#include <linux/types.h>
#include <linux/mfd/sdp_micom.h>

#define DRIVER_NAME	"isp-wt61p807"
#define DEV_NAME	"micom-isp"

enum {
	FALSE,
	TRUE
};

#define NO_CHECK_ACK		0xFF

/* no of retries */
#define ACK_RETRY_MAX_ISP	5
#define KEY_ISP_READ_PACKET_SIZE 12

/* contains device information */
struct wt61p807_isp_data {
	struct cdev *isp_dev;
	struct class *isp_class;
	struct device *isp_device;
	int micom_isp_major;
	int ref_count;
};

#endif
