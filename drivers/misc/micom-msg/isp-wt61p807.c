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
 * @file	isp-wt61p807.c
 * @brief	Driver for Micom ISP Data
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/10/18
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/micom-msg.h>
#include "isp-wt61p807.h"

/* static mutexes for micom isp driver */
DEFINE_MUTEX(isp_dev_lock);

/* structure for dynamic completion event structure */
struct completion micom_isp_ack_comp;

/* global ack for expected acknowledgement */
static char ack;

/* global structures for wt61p807_isp_data and wt61p807_isp_cdev as those must
 * be accessible from other functions ie.open, release etc.
 */
struct wt61p807_isp_data m_isp_dev;
static struct cdev wt61p807_isp_cdev;
static struct device *micom_dev;
static struct sdp_micom_msg micom_ack_isp;

/* micom isp file operations */
static int micom_isp_open(struct inode *inode, struct file *filp);
static int micom_isp_release(struct inode *inode, struct file *filp);
static long micom_isp_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg);

/* Micom callback */
static void wt61p807_isp_cb(struct sdp_micom_msg *msg, void *dev_id);

/* other functions */
static void micom_prepare_n_send_isp(struct sdp_micom_usr_isp *micom_user_isp);
static bool micom_check_n_copy_ack(struct sdp_micom_usr_isp *micom_user_isp);

/* file operations for micom isp device */
const struct file_operations wt61p807_isp_fops = {
	.owner = THIS_MODULE,
	.open = micom_isp_open,
	.unlocked_ioctl = micom_isp_ioctl,
	.release = micom_isp_release,
};

/*
 *
 *   @fn	static int micom_isp_open(struct inode *inode, \
 *				struct file *filp);
 *   @brief	opens micom isp device and returns file descriptor
 *   @details	opens micom isp device and increments m_isp_dev_p->ref_count by
 *		one.
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns	file descriptor if device is opened successfully
 */
static int micom_isp_open(struct inode *inode, struct file *filp)
{
	struct wt61p807_isp_data *wt61p807_isp = &m_isp_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&isp_dev_lock);

	wt61p807_isp->ref_count++;
	dev_dbg(micom_dev, "reference count : %d\n", wt61p807_isp->ref_count);

	/* Release lock*/
	mutex_unlock(&isp_dev_lock);

	return 0;
}

/*
 *
 *   @fn	static void micom_prepare_n_send_isp(
				struct sdp_micom_usr_isp *micom_user_isp);
 *   @brief	prepares the sdp_micom_msg and sends it to micom core.
 *   @details
 *
 *   @param	micom_user_isp	pointer to sdp_micom_usr_isp

 *   @return	returns nothing
 */
static void micom_prepare_n_send_isp(struct sdp_micom_usr_isp *micom_user_isp)
{
	struct sdp_micom_msg isp;

	if (!micom_user_isp) {
		dev_err(micom_dev,
			"Not Able to send isp packet as null pointer passed\n");
		return;
	}

	/* set acknowledgement */
	ack = micom_user_isp->ack;

	/* prepare sdp_micom_msg */
	isp.msg_type = MICOM_ISP_DATA;
	isp.length = micom_user_isp->input_data_size;

	if (isp.length <= KEY_ISP_READ_PACKET_SIZE) {
		memcpy(isp.msg, micom_user_isp->input_data,
				 micom_user_isp->input_data_size);

		/* send command to micom core for further processing */
		sdp_micom_send_msg(&isp);
	} else {
		dev_err(micom_dev,
			"Unable to send isp packet. incorrect length\n");
	}
}

/*
 *
 *   @fn	static bool micom_check_n_copy_ack(
 *				struct sdp_micom_usr_isp *micom_user_isp);
 *   @brief	check the for completion event and if it is correct one, copy
 *		isp message from acknowledgement event buffer to
 *		sdp_micom_usr_isp.
 *   @details	everytime wait_for_completion_timeout() completes, this
 *		function is called to check the ack event. If the ack event
 *		occured with correct ack command, copy the micom data from
 *		ack event buffer and return status of ack event.
 *
 *   @param	micom_user_isp	pointer to sdp_micom_usr_isp.

 *   @return	returns status of ack event.
 */
static bool micom_check_n_copy_ack(struct sdp_micom_usr_isp *micom_user_isp)
{
	bool flag_ack = FALSE;
	struct sdp_micom_msg *ack_msg;

	ack_msg = &micom_ack_isp;

	/* check if the received acknowledgement matches with ack
	 * provided by user, else ignore ack event. */
	if (ack_msg->msg[0] == micom_user_isp->ack) {
		/* set flag_ack to TRUE */
		flag_ack = TRUE;
		memcpy(micom_user_isp->output_data,
			&(ack_msg->msg[1]), KEY_ISP_READ_DATA_SIZE);
	}
	return flag_ack;
}

/*   @fn	static long micom_isp_ioctl(struct file *filp, \
 *				unsigned int cmd, unsigned long arg);
 *   @brief	handles IOCTLs addressed to micom isp device and returns status
 *   @details	valid IOCTLs:
 *			MICOM_MSG_IOCTL_SEND_MSG: Used to send messages
 *			containing normal data to micom device. It expects
 *			acknowledgement	from the device.
 *			MICOM_MSG_IOCTL_SEND_MSG_NO_ACK: Used to send messages
 *			containing normal buffer data without expecting any
 *			acknowledgement from micom isp device.
 *
 *   @param	filp	pointer to device node file
 *		cmd	IOCTL command.
 *		arg	argument to ioctl command (struct sdp_micom_usr_isp).
 *
 *   @return	returns status of IOCTL
 *			-EINVAL: if null arg is passed from user.
 *			-EFAULT: if copy_from_user() fails to copy
 *			-ERANGE: if micom command sent from user exceeds the
 *			defined max value (0xFF)
 *			zero:	if suceess
 */
static long micom_isp_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{

	struct sdp_micom_usr_isp micom_isp_local;
	struct sdp_micom_usr_isp *micom_user_isp = &micom_isp_local;

	/* maximum no. of retries are 10. refer TDcSamMicomInterface.cpp */
	short int retry = 1;
	long status = 0;

	/* acquire lock */
	mutex_lock(&isp_dev_lock);

	/* arg validity check */
	if ((arg == (unsigned long)NULL)) {
		dev_err(micom_dev, "null argument provided\n");
		status = -EINVAL;
		goto null_arg_fail;
	} else {
		/* copy the arg from user space to kernel space */
		status = copy_from_user(micom_user_isp, (void *)arg,
				sizeof(struct sdp_micom_usr_isp));
		if (status != 0) {
			dev_err(micom_dev, "copy from user failed\n");
			status = -EFAULT;
			goto copy_from_user_fail;
		}
	}


	dev_dbg(micom_dev, "0x%02X ioctl received\n", cmd);

	/* determine the IOCTL sent */
	switch (cmd) {
	case MICOM_ISP_IOCTL_SEND_ARRAY_DATA:
		/* send array isp data to micom and expect acknowledgement from
		 * micom */

		while (retry <= ACK_RETRY_MAX_ISP) {
			if (micom_user_isp->is_long_resp) {
				dev_dbg(micom_dev,
					"setting isp response: %d\n",
					micom_user_isp->is_long_resp);
				sdp_micom_set_isp_response(
					micom_user_isp->is_long_resp);
			}
			/* dynamic initialization of acknowledge \
			 * completion.
			 */
			init_completion(&micom_isp_ack_comp);
			micom_prepare_n_send_isp(micom_user_isp);

			/* wait for ack if NO_CHECK_ACK is not sent as ack */
			if (micom_user_isp->ack != NO_CHECK_ACK) {
				/* wait for acknowledgement for 10 msecs*/
				/* refer to TDcSamMicomInterface.cpp */
				/*dev_dbg(micom_dev,
				"waiting for micom to respond...attempt %d\n",
				retry);*/
				retry++;
				/* wait for completion event with 100 msecs
				 * timeout
				 */
				/* TODO: Do we need only 10 msecs? */
				if (!wait_for_completion_timeout(
						&micom_isp_ack_comp,
						msecs_to_jiffies(1000))) {
					/* If this was the last retry, set the
					 * status as EAGAIN. */
					if (retry > ACK_RETRY_MAX_ISP) {
						dev_warn(micom_dev,
						"ack[%X] timed out\n", ack);
						status = -EAGAIN;
						break;
					} else {
						continue;
					}
				} else if (micom_check_n_copy_ack(
					micom_user_isp)) {
					/* check for ack event and write device
					 * acknowledgement data from
					 * micom_user_msg to local buffer only
					 * if ack cmd matches with the ack
					 * provided by user.
					 */
					status = copy_to_user((void *)arg,
						micom_user_isp, sizeof(struct
						sdp_micom_usr_isp));

					if (status) {
						dev_err(micom_dev,
						"copy to user failed\n");
						status = -EFAULT;
						goto copy_to_user_fail;
					}
					break;
				}
			} else {
				/* If ack is not expected, break out of loop */
				break;
			}
		}
		break;
	case MICOM_ISP_IOCTL_SET_BAUDRATE:
		/* Set micom baudrate */
		switch (micom_user_isp->baud_rate) {
		case 110:
			/* fall through */
		case 300:
			/* fall through */
		case 600:
			/* fall through */
		case 1200:
			/* fall through */
		case 2400:
			/* fall through */
		case 4800:
			/* fall through */
		case 9600:
			/* fall through */
		case 14400:
			/* fall through */
		case 19200:
			/* fall through */
		case 38400:
			/* fall through */
		case 57600:
			/* fall through */
		case 115200:
			/* fall through */
		case 230400:
			/* fall through */
		case 460800:
			/* fall through */
		case 921600:
			status = sdp_micom_set_baudrate(
						micom_user_isp->baud_rate);

			if (status)
				dev_err(micom_dev,
					"sdp_micom_set_baudrate failed\n");
			else
				dev_info(micom_dev, "baudrate set\n");

			break;
		default:
			dev_err(micom_dev, "invalid baud_rate received %d\n",
						micom_user_isp->baud_rate);
			status = -EINVAL;
			break;
		}
		break;
	case MICOM_ISP_IOCTL_GET_BAUDRATE:
		/* get micom baudrate */
		status = sdp_micom_get_baudrate(&(micom_user_isp->baud_rate));

		if (status)
			dev_err(micom_dev,
				"sdp_micom_get_baudrate failed\n");
		else
			status = copy_to_user((void *)arg, micom_user_isp,
					sizeof(struct sdp_micom_usr_isp));

			if (status) {
				dev_err(micom_dev, "copy to user failed\n");
				status = -EFAULT;
			}

		break;
	case MICOM_ISP_IOCTL_SET_BLOCK:
		switch (micom_user_isp->block_flag) {
		case BLOCK:
			/* fall through */
		case NOBLOCK:
			sdp_micom_send_block_msg(micom_user_isp->block_flag);
			break;
		default:
			dev_err(micom_dev, "invalid block msg received\n");
			status = -EINVAL;
			break;
		}
		break;
	default:
		dev_err(micom_dev, "invalid ioctl received 0x%08X\n",
				cmd);
		status = -EINVAL;
	}

null_arg_fail:
copy_from_user_fail:
copy_to_user_fail:
	/* Release lock*/
	mutex_unlock(&isp_dev_lock);
	return status;
}

/*
 *
 *   @fn	static int micom_isp_release(struct inode *inode, \
 *                              struct file *filp);
 *   @brief	closes micom isp device and returns status
 *   @details
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns zero if device is closed
 */
static int micom_isp_release(struct inode *inode, struct file *filp)
{

	struct wt61p807_isp_data *wt61p807_isp = &m_isp_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&isp_dev_lock);

	wt61p807_isp->ref_count--;
	dev_dbg(micom_dev, "reference count : %d\n", wt61p807_isp->ref_count);

	/* Release lock*/
	mutex_unlock(&isp_dev_lock);

	return 0;
}

/*
 *
 *   @fn	static void wt61p807_isp_cb(struct sdp_micom_msg *msg, \
 *				void *dev_id);
 *   @brief	handles callbacks addressed to micom isp device
 *   @details	everytime an irq is generated from micom device, a relevant
 *		registered callback is called from micom core. This callbacks
 *		represents irq handler for micom isp messages.
 *
 *   @param	msg	pointer to micom message structure
 *		dev_id	micom isp device
 *
 *   @return	returns nothing
 */
static void wt61p807_isp_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	int i;
	struct sdp_micom_msg *ack_isp = &micom_ack_isp;

	/* debug printing */
	for (i = 0; i < msg->length; i++)
		dev_dbg(micom_dev, "isp msg [%d]: %02X\n", i, msg->msg[i]);

	/* ignoring all other unwanted ack */
	if (ack == msg->msg[1]) {
		dev_dbg(micom_dev, "received acknowledgement 0x%02X\n",
				msg->msg[1]);
		memcpy(ack_isp->msg, &(msg->msg[1]), msg->length - 1);

		/* wake up notification to thread waiting for
		 * acknowledgement event */
		complete(&micom_isp_ack_comp);
	} else {
		dev_info(micom_dev, "received ack: 0x%02X expected: 0x%02X\n",
							msg->msg[1], ack);
	}

	/* just for debugging, will be removed later. */
	dev_dbg(micom_dev, "isp irq processed\n");
}

/*
 *
 *   @fn	static int __init wt61p807_isp_probe( \
 *                              struct platform_device *pdev);
 *   @brief	probe function for micom isp
 *   @details	it is a character device which uses micom-core for registering
 *		micom isp driver.
 *
 *   @param	pdev	pointer to platform device

 *   @return	returns status of probing
 */
static int wt61p807_isp_probe(struct platform_device *pdev)
{

	struct wt61p807_isp_data *wt61p807_isp;
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
	cdev_init(&wt61p807_isp_cdev, &wt61p807_isp_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_isp_cdev, devid, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_isp = &m_isp_dev;

	wt61p807_isp->isp_dev = &wt61p807_isp_cdev;
	wt61p807_isp->micom_isp_major = MAJOR(devid);
	wt61p807_isp->ref_count = 0;

	wt61p807_isp->isp_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(wt61p807_isp->isp_class)) {
		dev_err(&pdev->dev, "failed to create sys class\n");
	} else {
		wt61p807_isp->isp_device = device_create(
						wt61p807_isp->isp_class,
						NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_isp->isp_device)) {
			dev_err(&pdev->dev, "failed to create sys device\n");
			class_destroy(wt61p807_isp->isp_class);
		}
	}

	platform_set_drvdata(pdev, wt61p807_isp);
	
	/* intialization for completion event */
	init_completion(&micom_isp_ack_comp);
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
	micom_cb->id		= SDP_MICOM_DEV_ISP;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_isp_cb;
	micom_cb->dev_id	= wt61p807_isp;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		goto cb_fail;
	}

	/* dynamic initialization of mutex for device */
	mutex_init(&isp_dev_lock);

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
 *   @fn	static int wt61p807_isp_remove(struct platform_device *pdev);
 *   @brief	remove micom isp device
 *   @details	it uses micom-core for deregistering the micom isp
 *		device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns	zero
 */

static int wt61p807_isp_remove(struct platform_device *pdev)
{
	struct wt61p807_isp_data *wt61p807_isp;

	wt61p807_isp = platform_get_drvdata(pdev);

	mutex_destroy(&isp_dev_lock);

	/* destroy micom isp sysfs device and class */
	if (wt61p807_isp->isp_device != NULL) {
		device_destroy(wt61p807_isp->isp_class,
			MKDEV(wt61p807_isp->micom_isp_major, 0));
	}
	if (wt61p807_isp->isp_class != NULL)
		class_destroy(wt61p807_isp->isp_class);

	unregister_chrdev_region(MKDEV(m_isp_dev.micom_isp_major, 0), 1);
	return 0;
}

/* micom isp driver for probing and removal of device */
static struct platform_driver wt61p807_isp_driver = {
	.probe  = wt61p807_isp_probe,
	.remove = wt61p807_isp_remove,
	.driver = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(wt61p807_isp_driver);

MODULE_DESCRIPTION("Micom driver interface for ISP data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
