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
 * @file	bsensor-wt61p807.h
 * @brief	header for bsensor-wt61p807 driver
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/09/02
 *
 */

/* internal Release1 */

#ifndef __MICOM_BSENSOR_H
#define __MICOM_BSENSOR_H

#define DRIVER_NAME		"bsensor-wt61p807"
#define DEV_NAME		"micom-bsensor"

/* micom bs queue size */
#define MICOM_BS_QUEUE_SIZE	5

enum {
	FALSE,
	TRUE,
};

/* contains device information */
struct wt61p807_bs_data {
	struct cdev *bs_dev;
	struct class *bs_class;
	struct device *bs_device;
	int micom_bs_major;
	int ref_count;
};

#endif
