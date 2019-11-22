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
 * @file	bsensor-wt61p807.c
 * @brief	Driver for Micom Brightness Sensor
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/09/02
 *
 */

/* internal Release1 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>

#include "bsensor-wt61p807.h"

/* static mutexes for brightness sensor driver */
DEFINE_MUTEX(bs_dev_lock);

spinlock_t bs_spin_lock;

/* global structures for wt61p807_bs_data and wt61p807_bs_cdev as those must
 * be accessible from other functions ie.open, release etc.
 */
struct wt61p807_bs_data m_bs_dev;
static struct cdev wt61p807_bs_cdev;
static struct device *micom_dev;

/* wait bs queue */
wait_queue_head_t bs_queue;

/* micom bs data */
struct sdp_micom_msg micom_bs_data;

/* micom bs file operations */
static int micom_bs_open(struct inode *inode, struct file *filp);
static unsigned int micom_bs_poll(struct file *file, poll_table *wait);
static ssize_t micom_bs_read(struct file *file, char __user *buf, size_t count,
				loff_t *f_pos);
static int micom_bs_release(struct inode *inode, struct file *filp);

/* file operations for micom bs device */
const struct file_operations wt61p807_bs_fops = {
	.owner = THIS_MODULE,
	.open = micom_bs_open,
	.poll = micom_bs_poll,
	.read = micom_bs_read,
	.release = micom_bs_release,
};

/*
 *
 *   @fn	static int micom_bs_open(struct inode *inode, \
 *				struct file *filp);
 *   @brief	opens micom bs device and returns file descriptor
 *   @details	opens micom bs device and sets m_bs_dev_p->is_open to TRUE.
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns	file descriptor if device is opened successfully
 *		else returns -EBUSY.
 */
static int micom_bs_open(struct inode *inode, struct file *filp)
{

	struct wt61p807_bs_data *wt61p807_bs = &m_bs_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&bs_dev_lock);

	wt61p807_bs->ref_count++;
	dev_dbg(micom_dev, "reference count : %d\n", wt61p807_bs->ref_count);

	/* Release lock*/
	mutex_unlock(&bs_dev_lock);

	return 0;
}

/*
 *
 *   @fn	static unsigned int micom_bs_poll(struct file *file, \
 *					poll_table *wait)
 *   @brief	polling function for micom bs data.
 *   @details	determines if the micom bs data is ready to be read or no.
 *
 *   @param	file	file pointer
 *		wait	pointer to poll table
 *
 *   @return	returns mask
 */
static unsigned int micom_bs_poll(struct file *file, poll_table *wait)
{
	unsigned mask = 0;

	poll_wait(file, &bs_queue, wait);

	mask |= POLLIN | POLLRDNORM;

	return mask;
}

/*
 *
 *   @fn	static ssize_t micom_bs_read(struct file *file, \
 *					char __user *buf, size_t count,
 *					loff_t *f_pos)
 *   @brief	read function for micom bs data.
 *   @details	reads micom bs data which is 4 bytes long.
 *
 *   @param	file	file pointer
 *		buf	user buffer pointer
 *		count	requested data bytes count
 *		f_pos	f_pos pointer
 *
 *   @return	returns number of bytes read (KEY_BSENSOR_DATA_SIZE)
 *			-EAGAIN: if data is not ready and driver is opened
 *				 with O_NONBLOCK
 *			-EINVAL: if user buffer is smaller than bsensor data
 *			-EFAULT: if failed to copy to userspace
 */
static ssize_t micom_bs_read(struct file *file, char __user *buf, size_t count,
				loff_t *f_pos)
{
	int ret = 0;
	unsigned long flags;

	if (count < KEY_BSENSOR_DATA_SIZE) {
		dev_dbg(micom_dev, "minimum buffer size is %d",
				KEY_PACKET_DATA_SIZE);
		ret = -EINVAL;
		goto out;
	}

	if (file->f_flags & O_NONBLOCK) {
		ret = -EAGAIN;
		goto out;
	}

	interruptible_sleep_on(&bs_queue);

	spin_lock_irqsave(&bs_spin_lock, flags);
	ret = copy_to_user(buf, &micom_bs_data.msg[1],
				(size_t)KEY_BSENSOR_DATA_SIZE);

	if (ret) {
		/* don't increment the queue head if read is unsuccessful. */
		ret = -EFAULT;
		goto out_unlock;
	} else {
		ret = KEY_BSENSOR_DATA_SIZE;
	}

out_unlock:
	spin_unlock_irqrestore(&bs_spin_lock, flags);
out:
	return ret;
}
/*
 *
 *   @fn	static void wt61p807_bs_cb(struct sdp_micom_msg *msg, \
 *				void *dev_id);
 *   @brief	handles callbacks addressed to micom bs device
 *   @details	everytime an irq is generated from micom device, a relevant
 *		registered callback is called from micom core. This callbacks
 *		represents irq handler for micom bs messages.
 *
 *   @param	msg	pointer to micom message structure
 *		dev_id	micom bs device
 *
 *   @return	returns nothing
 */
static void wt61p807_bs_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	int i;
	unsigned long flags;

	/* debug printing */
	for (i = 0; i < msg->length; i++)
		dev_dbg(micom_dev, "bs msg [%d]: %02X", i, msg->msg[i]);

	spin_lock_irqsave(&bs_spin_lock, flags);

	memcpy(micom_bs_data.msg, msg->msg, msg->length);

	spin_unlock_irqrestore(&bs_spin_lock, flags);

	/* Wake any reading process */
	wake_up_interruptible(&bs_queue);

	/* just for debugging, will be removed later. */
	dev_dbg(micom_dev, "bs irq processed\n");
}


/*
 *
 *   @fn	static int micom_bs_release(struct inode *inode, \
 *                              struct file *filp);
 *   @brief	closes micom bs device and returns status
 *   @details
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns zero if device is closed
 */
static int micom_bs_release(struct inode *inode, struct file *filp)
{

	struct wt61p807_bs_data *wt61p807_bs = &m_bs_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&bs_dev_lock);

	wt61p807_bs->ref_count--;
	dev_dbg(micom_dev, "reference count : %d\n", wt61p807_bs->ref_count);

	/* Release lock*/
	mutex_unlock(&bs_dev_lock);

	return 0;
}


/*
 *
 *   @fn	static int __init wt61p807_bs_probe( \
 *                              struct platform_device *pdev);
 *   @brief	probe for micom bs device
 *   @details	it is a character device which uses micom-core for registering
 *		the micom bs device.
 *
 *   @param	pdev	pointer to platform device

 *   @return	returns status of device probing
 */
static int wt61p807_bs_probe(struct platform_device *pdev)
{

	struct wt61p807_bs_data *wt61p807_bs;
	struct sdp_micom_cb *micom_cb;
	dev_t devid = 0;
	int ret = -1;

	/* allocate char device region */
	ret = alloc_chrdev_region(&devid, 0, 1, DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed with %d\n",
			ret);
		goto chrdev_alloc_fail;
	}

	/* initialize associated cdev and attach the file_operations */
	cdev_init(&wt61p807_bs_cdev, &wt61p807_bs_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_bs_cdev, devid, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_bs = &m_bs_dev;

	wt61p807_bs->bs_dev = &wt61p807_bs_cdev;
	wt61p807_bs->micom_bs_major = MAJOR(devid);
	wt61p807_bs->ref_count = 0;

	wt61p807_bs->bs_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(wt61p807_bs->bs_class)) {
		dev_err(&pdev->dev, "failed to create sys class\n");
	} else {
		wt61p807_bs->bs_device = device_create(
						wt61p807_bs->bs_class,
						NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_bs->bs_device)) {
			dev_err(&pdev->dev, "failed to create sys device\n");
			class_destroy(wt61p807_bs->bs_class);
		}
	}

	platform_set_drvdata(pdev, wt61p807_bs);

	micom_dev = &(pdev->dev);

	init_waitqueue_head(&bs_queue);

	/* dynamic initialization of mutex for device */
	mutex_init(&bs_dev_lock);
	spin_lock_init(&bs_spin_lock);

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);

	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto alloc_fail;
	}

	/* id differs from devid as id is used by micom core driver for keeping
	 * track of platform driver registered with micom core.
	 */
	micom_cb->id		= SDP_MICOM_DEV_BS;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_bs_cb;
	micom_cb->dev_id	= wt61p807_bs;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		goto cb_fail;
	}

	return ret;

	/* cleaning up due to failure. */
cb_fail:
alloc_fail:
cdev_add_fail:
	unregister_chrdev_region(devid, 1);
chrdev_alloc_fail:
	return ret;
}

/*
 *
 *   @fn	static int wt61p807_bs_remove(struct platform_device *pdev);
 *   @brief	remove micom bs device
 *   @details	it uses micom-core for deregistering the micom bs
 *		device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns	zero
 */

static int wt61p807_bs_remove(struct platform_device *pdev)
{
	struct wt61p807_bs_data *wt61p807_bs;

	wt61p807_bs = platform_get_drvdata(pdev);

	mutex_destroy(&bs_dev_lock);

	/* destroy micom bs sysfs device and class */
	if (wt61p807_bs->bs_device != NULL) {
		device_destroy(wt61p807_bs->bs_class,
			MKDEV(wt61p807_bs->micom_bs_major, 0));
	}
	if (wt61p807_bs->bs_class != NULL)
		class_destroy(wt61p807_bs->bs_class);

	unregister_chrdev_region(MKDEV(m_bs_dev.micom_bs_major, 0), 1);
	return 0;
}

/* micom bs driver for probing and removal of device */
static struct platform_driver wt61p807_bs_driver = {
	.probe  = wt61p807_bs_probe,
	.remove = wt61p807_bs_remove,
	.driver = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(wt61p807_bs_driver);

MODULE_DESCRIPTION("Micom driver interface for echo brightness sensor data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
