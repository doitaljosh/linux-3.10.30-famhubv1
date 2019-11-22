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
 * @file	cec-wt61p807.c
 * @brief	Driver for communication with wt61p807 micom chip for cec msg
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/08/05
 *
 */

/* internal Release1 */

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

#include "cec-wt61p807.h"

/* static mutexes for device and cec_data */
DEFINE_MUTEX(cec_dev_lock);
DEFINE_MUTEX(cec_read_lock);
DEFINE_MUTEX(cec_write_lock);

/* structure for dynamic completion event structure */
struct completion micom_cec_event;

/* global structures for wt61p807_cec_data and wt61p807_cec_cdev as those must
 * be accessible from other functions ie.open, release etc.
 */
struct wt61p807_cec_data m_cec_dev;
static struct cdev wt61p807_cec_cdev;
static struct device *micom_dev;

/* wait queue for micom cec data */
struct sdp_micom_msg micom_cec_data_queue[MICOM_CEC_QUEUE_SIZE];
struct sdp_micom_msg *micom_cec_event_head = micom_cec_data_queue;
struct sdp_micom_msg *micom_cec_event_tail = micom_cec_data_queue;

/* list of micom cec file operations prototypes. */
static int micom_cec_open(struct inode *inode, struct file *filp);
static int micom_cec_release(struct inode *inode, struct file *filp);
static ssize_t micom_cec_write(struct file *fp, const char __user *buf,
				size_t count, loff_t *pos);
static ssize_t micom_cec_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos);

/* micom callback for cec msg interrupts */
static void wt61p807_cec_cb(struct sdp_micom_msg *msg, void *dev_id);

/* file operations for micom cec device */
const struct file_operations wt61p807_cec_fops = {
	.owner = THIS_MODULE,
	.write = micom_cec_write,
	.open = micom_cec_open,
	.read = micom_cec_read,
	.release = micom_cec_release,
};

/*
 *
 *   @fn	static int micom_cec_open(struct inode *inode, \
 *				struct file *filp);
 *   @brief	opens micom cec device and returns file descriptor
 *   @details	opens micom cec device and sets m_cec_dev_p->is_open to TRUE.
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns file descriptor if device is opened successfully
 *		else -EBUSY.
 */
static int micom_cec_open(struct inode *inode, struct file *filp)
{

	struct wt61p807_cec_data *wt61p807_cec = &m_cec_dev;
	int ret = 0;

	/* acquire lock before setting is_open.*/
	mutex_lock(&cec_dev_lock);

	wt61p807_cec->ref_cnt++;

	dev_dbg(micom_dev,
		"CEC device is opened. ref_cnt[%d]", wt61p807_cec->ref_cnt);

	/* Release lock*/
	mutex_unlock(&cec_dev_lock);

	return ret;
}

/*
 *
 *   @fn	static int micom_cec_release(struct inode *inode, \
 *				struct file *filp);
 *   @brief	closes micom cec device and returns status
 *   @details
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns zero if device is closed
 */
static int micom_cec_release(struct inode *inode, struct file *filp)
{

	int ret = 0;
	struct wt61p807_cec_data *wt61p807_cec = &m_cec_dev;

	/* acquire lock before restoring is_open.*/
	mutex_lock(&cec_dev_lock);

	wt61p807_cec->ref_cnt--;

	dev_dbg(micom_dev,
		"CEC device is closed. ref_cnt[%d]", wt61p807_cec->ref_cnt);

	/* Release lock*/
	mutex_unlock(&cec_dev_lock);

	return ret;
}

/*
 *
 *   @fn	static void wt61p807_cec_cb(struct sdp_micom_msg *msg, \
 *						void *dev_id);
 *   @brief	handles callbacks addressed to micom cec device
 *   @details	everytime an irq is generated from micom device, a relevant
 *		registered callback is called from micom core. This callbacks
 *		represents irq handler for micom cec messages.
 *
 *   @param	msg	pointer to micom message structure
 *		dev_id	micom cec device
 *
 *   @return	returns nothing
 */
static void wt61p807_cec_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	int i;
	/* debug printing */
	for (i = 0; i < msg->length; i++)
		dev_dbg(micom_dev, "cec msg [%d]: %02X", i, msg->msg[i]);

	micom_cec_event_tail->msg_type = msg->msg_type;
	micom_cec_event_tail->length = msg->length;
	memcpy(micom_cec_event_tail->msg, msg->msg, msg->length);

	/* check if tail reached at last element of queue. */
	if (micom_cec_event_tail ==
		&(micom_cec_data_queue[MICOM_CEC_QUEUE_SIZE - 1])) {
		/* wrap up the tail */
		micom_cec_event_tail = micom_cec_data_queue;
	} else {
		/* increment the tail pointer. */
		micom_cec_event_tail++;
	}

	/* Wake any reading process */
	complete(&micom_cec_event);

	/* just for debugging, will be removed later. */
	dev_dbg(micom_dev, "cec irq processed\n");
}

/*
 *
 *   @fn	ssize_t micom_cec_read(struct file *fp, char __user *buf, \
 *						size_t count, loff_t *pos);
 *   @brief	read call for micom cec device
 *   @details	read waits for a cec data to be arrived and then read from
 *		micom cec queue. The timeout given as of now is 1000 msecs.
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
			-ERANGE: if user buffer size is less than cec body size.
 *			-EFAULT: if copy to user fails.
 */
ssize_t micom_cec_read(struct file *fp, char __user *buf,
			size_t count, loff_t *pos)
{
	int ret = 0;

	if ((buf == NULL) || (count <= 0)) {
		dev_err(micom_dev, "invalid buffer type\n");
		ret = -EINVAL;
		goto out;
	}

	/* acquire lock before accessing micom cec event queue. */
	mutex_lock(&cec_read_lock);

	/* TODO: does it needs some timeout or retries? */
	/* wait for completion event */
	if (wait_for_completion_interruptible(&micom_cec_event) == -ERESTARTSYS ) {
		/* interrupt is happened by another cause(ex>freeze for suspend). */
		ret = -EINTR;
		goto out_unlock;
	}

	/* micom_cec_event_head->length is the cec body size */
	if (count < (micom_cec_event_head->length)) {
		dev_err(micom_dev, "insufficient buffer provided\n");
		ret = -ERANGE;
		goto out_unlock;
	} else {
		ret = copy_to_user(buf,	(void *)&(micom_cec_event_head->msg),
			micom_cec_event_head->length);
		if (ret < 0)
			goto out_unlock;
		else
			ret = micom_cec_event_head->length;
	}

	/* check if head reached at last element of queue. */
	if (micom_cec_event_head ==
		&(micom_cec_data_queue[MICOM_CEC_QUEUE_SIZE - 1])) {
		/* wrap up the head */
		micom_cec_event_head = micom_cec_data_queue;
	} else {
		/* increment the head pointer. */
		micom_cec_event_head++;
	}

out_unlock:
	/* release the lock */
	mutex_unlock(&cec_read_lock);
out:
	return ret;
}

/* write cec data to micom device.
 * it actually send message to micom core driver which in turn writes it to uart
 * connected with micom */
/*
 *
 *   @fn	ssize_t micom_cec_write(struct file *fp, char __user *buf, \
 *						size_t count, loff_t *pos);
 *   @brief	write call for micom cec device
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
			-ERANGE: when cec data overflows.
 *			-EFAULT: if copy to user fails.
 */
static ssize_t micom_cec_write(struct file *fp, const char __user *buf,
			size_t count, loff_t *pos)
{

	struct sdp_micom_msg msg;
	int ret = 0;

	if ((buf == NULL) || (count <= 0)) {
		dev_err(micom_dev, "invalid cec data provided\n");
		ret = -EINVAL;
		goto out;
	}

	/* micom cec message overflow check */
	if (count > 18) {
		dev_err(micom_dev, "cec message overflow\n");
		ret = -ERANGE;
		goto out;
	}

	ret = copy_from_user((void *)&msg.msg, (void *)buf, count);
	if (ret < 0) {
		dev_err(micom_dev, "failed to copy from user\n");
		goto out;
	}

	mutex_lock(&cec_write_lock);

	dev_dbg(micom_dev, "writing to micom\n");
	/* prepare rest of cec message and send to micom if successfully copied
	 * from userspace.
	 */
	msg.msg_type = MICOM_CEC_DATA;
	msg.length = count;

	sdp_micom_send_msg(&msg);

	ret = count;

	dev_dbg(micom_dev, "writing to micom done\n");

	mutex_unlock(&cec_write_lock);
out:
	return ret;
}


static char *wt61p807_cec_devnode(struct device *dev, umode_t *mode)
{
	*mode = 0666; // rw-rw-rw-
	return NULL;
}


/*
 *
 *   @fn	static int __init wt61p807_cec_probe( \
 *				struct platform_device *pdev);
 *   @brief	probe for micom cec device
 *   @details	it is a character device which uses micom-core for registering
 *		the micom cec device.
 *
 *   @param	pdev	pointer to platform device

 *   @return	returns status of device probing
 */
static int wt61p807_cec_probe(struct platform_device *pdev)
{

	struct wt61p807_cec_data *wt61p807_cec;
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
	cdev_init(&wt61p807_cec_cdev, &wt61p807_cec_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_cec_cdev, devid, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_cec = &m_cec_dev;

	wt61p807_cec->cec_dev = &wt61p807_cec_cdev;
	wt61p807_cec->micom_cec_major = MAJOR(devid);
	wt61p807_cec->ref_cnt = 0;

	wt61p807_cec->cec_class = class_create(THIS_MODULE, DRIVER_NAME);

	wt61p807_cec->cec_class->devnode = wt61p807_cec_devnode;

	if (IS_ERR(wt61p807_cec->cec_class)) {
		dev_err(&pdev->dev, "failed to create sys class\n");
	} else {
		wt61p807_cec->cec_device = device_create(
						wt61p807_cec->cec_class,
						NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_cec->cec_device)) {
			dev_err(&pdev->dev, "failed to create sys device\n");
			class_destroy(wt61p807_cec->cec_class);
		}
	}

	platform_set_drvdata(pdev, wt61p807_cec);

	/* dynamic initialization of acknowledge completion */
	init_completion(&micom_cec_event);
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
	micom_cb->id		= SDP_MICOM_DEV_CEC;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_cec_cb;
	micom_cb->dev_id	= wt61p807_cec;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		goto cb_fail;
	}

	/* dynamic initialization of mutex for device */
	mutex_init(&cec_dev_lock);
	mutex_init(&cec_read_lock);
	mutex_init(&cec_write_lock);

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
 *   @fn	static int wt61p807_cec_remove(struct platform_device *pdev);
 *   @brief	remove micom cec device
 *   @details	it uses micom-core for deregistering the micom cec
 *		device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns zero
 */
static int wt61p807_cec_remove(struct platform_device *pdev)
{
	struct wt61p807_cec_data *wt61p807_cec;

	wt61p807_cec = platform_get_drvdata(pdev);

	mutex_destroy(&cec_dev_lock);
	mutex_destroy(&cec_read_lock);
	mutex_destroy(&cec_write_lock);

	/* destroy micom cec sysfs device and class */
	if (wt61p807_cec->cec_device != NULL) {
		device_destroy(wt61p807_cec->cec_class,
				MKDEV(wt61p807_cec->micom_cec_major, 0));
	}
	if (wt61p807_cec->cec_class != NULL)
		class_destroy(wt61p807_cec->cec_class);

	unregister_chrdev_region(MKDEV(m_cec_dev.micom_cec_major, 0), 1);
	return 0;
}

/* micom cec driver for probe and removal of device */
static struct platform_driver wt61p807_cec_driver = {
	.probe	= wt61p807_cec_probe,
	.remove = wt61p807_cec_remove,
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(wt61p807_cec_driver);

MODULE_DESCRIPTION("Micom driver interface for CEC data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
