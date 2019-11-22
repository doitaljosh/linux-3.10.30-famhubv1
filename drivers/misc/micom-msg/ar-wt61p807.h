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
 * @file	ar-wt61p807.h
 * @briefi	Header for ar-wt61p807 driver
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2014/02/12
 *
 */

#ifndef __MICOM_AR_H
#define __MICOM_AR_H

#include <linux/mfd/sdp_micom.h>

#define DRIVER_NAME		"ar-wt61p807"
#define DEV_NAME		"micom-ar"
/* micom auto remocon queue size */
#define MICOM_AR_QUEUE_SIZE	3

enum {
	FALSE,
	TRUE
};

/* contains device information */
struct wt61p807_ar_data {
	struct cdev *ar_dev;
	struct class *ar_class;
	struct device *ar_device;
	int micom_ar_major;
	int ref_cnt;
};

#endif
