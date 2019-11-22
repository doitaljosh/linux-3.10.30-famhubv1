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
 * @file	ar-wt61p807.c
 * @brief	Driver for communication with wt61p807 micom chip for Auto
 *		Remocon msg.
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2014/02/12
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/completion.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/micom-msg.h>

#include "ar-wt61p807.h"

/* static mutexes for device and ar_data */
DEFINE_MUTEX(ar_dev_lock);
DEFINE_MUTEX(ar_read_lock);
DEFINE_MUTEX(ar_write_lock);

/* structure for dynamic completion event structure */
struct completion micom_ar_event;

/* global structures for wt61p807_ar_data and wt61p807_ar_cdev as those must
 * be accessible from other functions ie.open, release etc.
 */
struct wt61p807_ar_data m_ar_dev;
static struct cdev wt61p807_ar_cdev;
static struct device *micom_dev;

/* wait queue for micom auto remocon data */
struct sdp_micom_msg micom_ar_data_queue[MICOM_AR_QUEUE_SIZE];
struct sdp_micom_msg *micom_ar_event_head = micom_ar_data_queue;
struct sdp_micom_msg *micom_ar_event_tail = micom_ar_data_queue;

/* list of micom auto remocon file operations prototypes. */
static int micom_ar_open(struct inode *inode, struct file *filp);
static int micom_ar_release(struct inode *inode, struct file *filp);
static ssize_t micom_ar_write(struct file *fp, const char __user *buf,
				size_t count, loff_t *pos);
static ssize_t micom_ar_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos);
static long micom_ar_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg);

/* micom callback for auto remocon msg interrupts */
static void wt61p807_ar_cb(struct sdp_micom_msg *msg, void *dev_id);

/* file operations for micom auto remocon device */
const struct file_operations wt61p807_ar_fops = {
	.owner = THIS_MODULE,
	.write = micom_ar_write,
	.open = micom_ar_open,
	.read = micom_ar_read,
	.unlocked_ioctl = micom_ar_ioctl,
	.release = micom_ar_release,
};

/*
 *
 *   @fn	static int micom_ar_open(struct inode *inode, \
 *				struct file *filp);
 *   @brief	opens micom auto remocon device and returns file descriptor
 *   @details	opens micom auto remocon device and increments reference counter.
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns file descriptor if device is opened successfully
 *		else -EBUSY.
 */
static int micom_ar_open(struct inode *inode, struct file *filp)
{

	struct wt61p807_ar_data *wt61p807_ar = &m_ar_dev;
	int ret = 0;

	/* acquire lock before setting is_open.*/
	mutex_lock(&ar_dev_lock);

	wt61p807_ar->ref_cnt++;

	dev_dbg(micom_dev, "Auto Remocon device is opened. ref_cnt[%d]",
							wt61p807_ar->ref_cnt);

	/* Release lock*/
	mutex_unlock(&ar_dev_lock);

	return ret;
}

/*
 *
 *   @fn	static int micom_ar_release(struct inode *inode, \
 *				struct file *filp);
 *   @brief	closes micom auto remocon device and returns status
 *   @details
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns zero if device is closed
 */
static int micom_ar_release(struct inode *inode, struct file *filp)
{

	int ret = 0;
	struct wt61p807_ar_data *wt61p807_ar = &m_ar_dev;

	/* acquire lock before restoring is_open.*/
	mutex_lock(&ar_dev_lock);

	wt61p807_ar->ref_cnt--;

	dev_dbg(micom_dev, "Auto Remocon device is closed. ref_cnt[%d]",
							wt61p807_ar->ref_cnt);

	/* Release lock*/
	mutex_unlock(&ar_dev_lock);

	return ret;
}

/*
 *
 *   @fn	static void wt61p807_ar_cb(struct sdp_micom_msg *msg, \
 *						void *dev_id);
 *   @brief	handles callbacks addressed to micom auto remocon device
 *   @details	everytime an irq is generated from micom device, a relevant
 *		registered callback is called from micom core. This callbacks
 *		represents irq handler for micom auto remocon messages.
 *
 *   @param	msg	pointer to micom message structure
 *		dev_id	micom auto remocon device id
 *
 *   @return	returns nothing
 */
static void wt61p807_ar_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	int i;
	/* debug printing */
	for (i = 0; i < msg->length; i++)
		dev_dbg(micom_dev, "ar msg [%d]: %02X", i, msg->msg[i]);

	micom_ar_event_tail->msg_type = msg->msg_type;
	micom_ar_event_tail->length = msg->length;
	memcpy(micom_ar_event_tail->msg, msg->msg, msg->length);

	/* check if tail reached at last element of queue. */
	if (micom_ar_event_tail ==
		&(micom_ar_data_queue[MICOM_AR_QUEUE_SIZE - 1])) {
		/* wrap up the tail */
		micom_ar_event_tail = micom_ar_data_queue;
	} else {
		/* increment the tail pointer. */
		micom_ar_event_tail++;
	}

	/* Wake any reading process */
	complete(&micom_ar_event);

	/* just for debugging, will be removed later. */
	dev_dbg(micom_dev, "ar irq processed\n");
}

/*
 *
 *   @fn	ssize_t micom_ar_read(struct file *fp, char __user *buf, \
 *						size_t count, loff_t *pos);
 *   @brief	read call for micom auto remocon device
 *   @details	read waits for auto remocon data to be arrived and then reads
 *		from micom auto remocon queue. The wait is interruptible.
 *
 *   @param	fp	file pointer
 *		buf	pointer to user buffer
 *		count	buffer size count
 *		pos	position
 *
 *   @return	returns number of bytes read
 *		In failure:
 *			-EINVAL: if invalid buffer is passed from user.
 *			-EAGAIN: if wait_for_completion_interruptible_timeout
 *				 fails.
			-ERANGE: if user buffer size is less than ar body size.
 *			-EFAULT: if copy to user fails.
 */
ssize_t micom_ar_read(struct file *fp, char __user *buf,
			size_t count, loff_t *pos)
{
	int ret = 0;

	if ((buf == NULL) || (count <= 0)) {
		dev_err(micom_dev, "invalid buffer type\n");
		ret = -EINVAL;
		goto out;
	}

	/* acquire lock before accessing micom auto remocon event queue. */
	mutex_lock(&ar_read_lock);

	/* TODO: does it need some timeout or retries? */
	/* wait for completion event */
	if ( wait_for_completion_interruptible(&micom_ar_event) == -ERESTARTSYS ) {
		/* interrupt is happened by another cause(ex>freeze for suspend). */
		ret = -EINTR;
		goto out_unlock;
	}

	/* micom_ar_event_head->length is the auto remocon body size */
	if (count < (micom_ar_event_head->length)) {
		dev_err(micom_dev, "insufficient buffer provided\n");
		ret = -ERANGE;
		goto out_unlock;
	} else {
		ret = copy_to_user(buf, (void *)&(micom_ar_event_head->msg),
			micom_ar_event_head->length);
		if (ret < 0)
			goto out_unlock;
		else
			ret = micom_ar_event_head->length;
	}

	/* check if head reached at last element of queue. */
	if (micom_ar_event_head ==
		&(micom_ar_data_queue[MICOM_AR_QUEUE_SIZE - 1])) {
		/* wrap up the head */
		micom_ar_event_head = micom_ar_data_queue;
	} else {
		/* increment the head pointer. */
		micom_ar_event_head++;
	}

out_unlock:
	/* release the lock */
	mutex_unlock(&ar_read_lock);
out:
	return ret;
}

/*
 *
 *   @fn	ssize_t micom_ar_write(struct file *fp, char __user *buf, \
 *						size_t count, loff_t *pos);
 *   @brief	write call for micom auto remocon device
 *   @details	write validates the user buffer and send it to micom core
 *		which in turn writes it to micom uart line.
 *
 *   @param	fp	file pointer
 *		buf	pointer to user buffer
 *		count	buffer size count
 *		pos	position
 *
 *   @return	returns number of bytes written
 *		In failure:
 *			-EINVAL: if invalid buffer is passed from user.
			-ERANGE: when ar data overflows.
 *			-EFAULT: if copy to user fails.
 */
static ssize_t micom_ar_write(struct file *fp, const char __user *buf,
			size_t count, loff_t *pos)
{

	struct sdp_micom_msg msg;
	int ret = 0;

	if ((buf == NULL) || (count <= 0)) {
		dev_err(micom_dev, "invalid ar data provided\n");
		ret = -EINVAL;
		goto out;
	}

	/* micom auto remocon message overflow check */
	/* TODO: Check the actual Auto Remocon data size */
	if (count > KEY_PACKET_DATA_SIZE) {
		dev_err(micom_dev, "ar message overflow\n");
		ret = -ERANGE;
		goto out;
	}

	ret = copy_from_user((void *)&msg.msg, (void *)buf, count);
	if (ret < 0) {
		dev_err(micom_dev, "failed to copy from user\n");
		goto out;
	}

	mutex_lock(&ar_write_lock);

	dev_dbg(micom_dev, "writing to micom\n");
	/* prepare rest of auto remocon message and send to micom if
	 * successfully copied from userspace.
	 */
	msg.msg_type = MICOM_NORMAL_DATA;
	/* TODO: Check the actual Auto Remocon data size */
	msg.length = KEY_PACKET_DATA_SIZE;

	sdp_micom_send_msg(&msg);

	/* TODO: Check the actual Auto Remocon data size */
	ret = KEY_PACKET_DATA_SIZE;

	dev_dbg(micom_dev, "writing to micom done\n");

	mutex_unlock(&ar_write_lock);
out:
	return ret;
}

static long micom_ar_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	long ret = -EINVAL;

	switch(cmd) {
	case MICOM_AR_IOCTL_PANEL_LOCK:
		sdp_micom_panel_key_lock((int)arg);
		ret = 0;
		break;
	case MICOM_AR_IOCTL_MANUAL_ON_OFF:
		sdp_micom_set_ar_mode((int)arg);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

/*
 *
 *   @fn	static char *wt61p807_ar_devnode(struct device *dev, \
 *							umode_t *mode)
 *   @brief	provide access permission of /dev/micom-ar to 666
 *   @param	dev	pointer to device structure
 *		mode	pointer to device permissions
 *   @return	NULL.
 */
static char *wt61p807_ar_devnode(struct device *dev, umode_t *mode)
{
	*mode = 0666; /* rw-rw-rw- */
	return NULL;
}


/*
 *
 *   @fn	static int __init wt61p807_ar_probe( \
 *				struct platform_device *pdev);
 *   @brief	probe for micom auto remocon device
 *   @details	it is a character device which uses micom-core for registering
 *		the micom auto remocon device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns status of device probing
 */
static int __init wt61p807_ar_probe(struct platform_device *pdev)
{

	struct wt61p807_ar_data *wt61p807_ar;
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
	cdev_init(&wt61p807_ar_cdev, &wt61p807_ar_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_ar_cdev, devid, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_ar = &m_ar_dev;

	wt61p807_ar->ar_dev = &wt61p807_ar_cdev;
	wt61p807_ar->micom_ar_major = MAJOR(devid);
	wt61p807_ar->ref_cnt = 0;

	wt61p807_ar->ar_class = class_create(THIS_MODULE, DRIVER_NAME);

	wt61p807_ar->ar_class->devnode = wt61p807_ar_devnode;

	if (IS_ERR(wt61p807_ar->ar_class)) {
		dev_err(&pdev->dev, "failed to create sys class\n");
	} else {
		wt61p807_ar->ar_device = device_create(
						wt61p807_ar->ar_class,
						NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_ar->ar_device)) {
			dev_err(&pdev->dev, "failed to create sys device\n");
			class_destroy(wt61p807_ar->ar_class);
		}
	}

	platform_set_drvdata(pdev, wt61p807_ar);
	
	/* dynamic initialization of acknowledge completion */
	init_completion(&micom_ar_event);
	micom_dev = &(pdev->dev);

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
	micom_cb->id		= SDP_MICOM_DEV_AR;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_ar_cb;
	micom_cb->dev_id	= wt61p807_ar;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		goto cb_fail;
	}

	/* dynamic initialization of mutex for device */
	mutex_init(&ar_dev_lock);
	mutex_init(&ar_read_lock);
	mutex_init(&ar_write_lock);

	return ret;

	/* cleaning up due to failure. */
cb_fail:
alloc_fail:
cdev_add_fail:
	unregister_chrdev_region(devid, 1);
chrdev_alloc_fail:
	return ret;
}

/* cleaning up */
/*
 *
 *   @fn	static int wt61p807_ar_remove(struct platform_device *pdev);
 *   @brief	remove micom auto remocon device
 *   @details	it uses micom-core for deregistering the micom auto remocon
 *		device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns zero
 */
static int wt61p807_ar_remove(struct platform_device *pdev)
{
	struct wt61p807_ar_data *wt61p807_ar;

	wt61p807_ar = platform_get_drvdata(pdev);

	mutex_destroy(&ar_dev_lock);
	mutex_destroy(&ar_read_lock);
	mutex_destroy(&ar_write_lock);

	/* destroy micom auto remocon sysfs device and class */
	if (wt61p807_ar->ar_device != NULL) {
		device_destroy(wt61p807_ar->ar_class,
				MKDEV(wt61p807_ar->micom_ar_major, 0));
	}
	if (wt61p807_ar->ar_class != NULL)
		class_destroy(wt61p807_ar->ar_class);

	unregister_chrdev_region(MKDEV(m_ar_dev.micom_ar_major, 0), 1);
	return 0;
}

/* micom auto remocon driver for probe and removal of device */
static struct platform_driver wt61p807_ar_driver = {
	.probe	= wt61p807_ar_probe,
	.remove = wt61p807_ar_remove,
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(wt61p807_ar_driver);

MODULE_DESCRIPTION("Micom driver interface for Auto Remocon data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
