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
 * @file	msg-wt61p807.c
 * @brief	Driver for communication with wt61p807 micom chip for normal msg
 * @author	Abhishek Jaiswal <abhishek1.j@samsung.com>
 * @date	2013/07/30
 *
 */

/* internal Release1 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mfd/sdp_micom.h>
#include <asm/io.h>

#include <linux/micom-msg.h>

#define DRIVER_NAME			"msg-wt61p807"
#define DEV_NAME			"micom-msg"

#define micom_watchdog_enable	0

/* no of retries */
#define ACK_RETRY_MAX			5

enum {
	FALSE,
	TRUE
};

/* static mutexes for micom msg device */
static DEFINE_MUTEX(dev_msg_lock);
static DEFINE_MUTEX(dev_msg_complete_lock);

/* structure for dynamic completion event structure */
struct completion micom_msg_ack;

/* global micom normal buffer for ack events */
struct sdp_micom_msg micom_ack_msg;
/* global ack for expected acknowledgement */
char ack;
/* global structure pointer for device */
static struct device *micom_dev;

/* list of micom msg file operations prototypes. */
static int micom_msg_open(struct inode *inode, struct file *filp);
static int micom_msg_release(struct inode *inode, struct file *filp);
static long micom_msg_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg);
static ssize_t show_jack_ident(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t show_scart_lv_1(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t show_scart_lv_2(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t show_jack_ident_ready(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t show_micom_version(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t show_boot_reason(struct device *dev,
			struct device_attribute *attr, char *buf);
/* micom callback for normal msg interrupts */
static void wt61p807_msg_cb(struct sdp_micom_msg *msg, void *data);

/* micom misc functions */
static void micom_prepare_n_send_msg(struct sdp_micom_usr_msg *micom_user_msg);
static bool micom_check_n_copy_ack(struct sdp_micom_usr_msg *micom_user_msg);

/* file operations for micom msg device */
const struct file_operations wt61p807_msg_fops = {
	.owner		= THIS_MODULE,
	.open		= micom_msg_open,
	.unlocked_ioctl	= micom_msg_ioctl,
	.release	= micom_msg_release,
};

struct wt61p807_msg_data {
	struct cdev *msg_dev;
	struct class *msg_class;
	struct device *msg_device;
	int micom_msg_major;
	int ref_count;

	int jack_ident;
	int jack_ident_ready;
	int scart_lv_1;
	int scart_lv_2;
	int micom_version;
	int boot_reason;
};

/* micom msg device specific data */
struct wt61p807_msg_data m_msg_dev;

/* micom msg cdev */
struct cdev wt61p807_msg_cdev;

static DEVICE_ATTR(jack_ident, S_IRUGO, show_jack_ident, NULL);
static DEVICE_ATTR(scart_lv_1, S_IRUGO, show_scart_lv_1, NULL);
static DEVICE_ATTR(scart_lv_2, S_IRUGO, show_scart_lv_2, NULL);
static DEVICE_ATTR(jack_ident_ready, S_IRUGO,
						show_jack_ident_ready, NULL);
static DEVICE_ATTR(micom_version, S_IRUGO, show_micom_version, NULL);
static DEVICE_ATTR(boot_reason, S_IRUGO, show_boot_reason, NULL);

#define SDP_MICOM_CMD_ALIVE_MSG	0x18
#define SDP_MICOM_ACK_ALIVE_MSG	0x29

static struct task_struct *thd_alive;
static struct task_struct *thd_alive_scaler;

static unsigned char skip_for_suspend_mode=0;

#if micom_watchdog_enable 
static int stop_thd_alive;
#endif 

static unsigned char send_i_am_ok=0;

#if defined(CONFIG_ARCH_SDP1304)
static unsigned int *addr_scaler_blk;

#define SDP_SCALER_FLAG_ADDR	0x189500AC
#define SDP_SCALER_FLAG_SIZE	8
#endif

/*
 *
 *   @fn	static int micom_msg_open(struct inode *inode, \
 *				struct file *filp);
 *   @brief	opens micom msg device and returns file descriptor
 *   @details	opens micom msg device and increments m_msg_dev_p->ref_count.
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns file descriptor if device is opened successfully
 */
static int micom_msg_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct wt61p807_msg_data *m_msg_dev_p = &m_msg_dev;

	/* acquire lock before opening device.*/
	mutex_lock(&dev_msg_lock);

	m_msg_dev_p->ref_count++;

	dev_dbg(micom_dev,
		"MSG device is opened. ref_count[%d]\n",
		m_msg_dev_p->ref_count);
	/* Release lock */
	mutex_unlock(&dev_msg_lock);

	return ret;
}

/*
 *
 *   @fn	static int micom_msg_release(struct inode *inode, \
 *				struct file *filp);
 *   @brief	closes micom msg device and returns status
 *   @details
 *
 *   @param	inode	pointer to device node's inode structure
 *		filp	pointer to device node file
 *
 *   @return	returns zero if device is closed
 */
static int micom_msg_release(struct inode *inode, struct file *filp)
{

	int ret = 0;
	struct wt61p807_msg_data *m_msg_dev_p = &m_msg_dev;

	/* acquire lock before closing device.*/
	mutex_lock(&dev_msg_lock);

	m_msg_dev_p->ref_count--;

	dev_dbg(micom_dev,
		"MSG device is closed. ref_count[%d]\n",
		m_msg_dev_p->ref_count);
	/* Release lock*/
	mutex_unlock(&dev_msg_lock);

	return ret;
}

/*
 *
 *   @fn	static long micom_msg_ioctl(struct file *filp, \
 *				unsigned int cmd, unsigned long arg);
 *   @brief	handles IOCTLs addressed to micom msg device and returns status
 *   @details	valid IOCTLs:
 *			MICOM_MSG_IOCTL_SEND_MSG: Used to send messages
 *			containing normal data to micom device. It expects
 *			acknowledgement from the device.
 *			MICOM_MSG_IOCTL_SEND_MSG_NO_ACK: Used to send messages
 *			containing normal buffer data without expecting any
 *			acknowledgement from micom msg device.
 *
 *   @param	filp	pointer to device node file
 *		cmd	IOCTL command.
 *		arg	argument to ioctl command (struct sdp_micom_usr_msg).
 *
 *   @return	returns status of IOCTL
 *			-EINVAL: if null arg is passed from user.
 *			-EFAULT: if copy_from_user() fails to copy
 *			-ERANGE: if micom command sent from user exceeds the
 *			defined max value (0xFF)
 *			zero:	if suceess
 */
static long micom_msg_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{

	struct sdp_micom_usr_msg micom_msg_local;
	struct sdp_micom_usr_msg *micom_user_msg = &micom_msg_local;
	struct sdp_micom_msg *ack_msg;

	/* maximum no. of retries are 3. refer TDcSamMicomInterface.cpp */
	short int retry = 0;
	long status = 0;

	/* acquire lock */
	mutex_lock(&dev_msg_lock);

	ack_msg = &micom_ack_msg;
	/* arg validity check */
	if ((arg == (unsigned long)NULL)) {
		dev_err(micom_dev, "null argument provided\n");
		status = -EINVAL;
		goto null_arg_fail;
	} else {
		/* copy the arg from user space to kernel space */
		status = copy_from_user(micom_user_msg, (void *)arg,
				sizeof(struct sdp_micom_usr_msg));
		if (status != 0) {
			dev_err(micom_dev, "copy from user failed\n");
			status = -EFAULT;
			goto copy_from_user_fail;
		}
	}


	dev_dbg(micom_dev, "0x%X ioctl received\n", cmd);

	/* determine the IOCTL sent */
	switch (cmd) {
	case MICOM_MSG_IOCTL_SEND_MSG_NO_ACK:
		/* expects no acknowledgement from micom */
		if (micom_user_msg->cmd > 0xFF) {
			dev_err(micom_dev, "message overflow\n");
			status = -ERANGE;
		} else {
			sdp_micom_msg_lock();
			micom_prepare_n_send_msg(micom_user_msg);
			sdp_micom_msg_unlock();
		}
		break;
	case MICOM_MSG_IOCTL_SEND_MSG:
		/* expects micom acknowledgement for command sent */
		if (micom_user_msg->cmd > 0xFF) {
			dev_err(micom_dev, "message overflow\n");
			status = -ERANGE;
		} else {
			sdp_micom_msg_lock();
			while (retry <= ACK_RETRY_MAX) {
				/* dynamic initialization of acknowledge \
				 * completion.
				 */
				mutex_lock(&dev_msg_complete_lock);
				init_completion(&micom_msg_ack);
				mutex_unlock(&dev_msg_complete_lock);

				micom_prepare_n_send_msg(micom_user_msg);

				/* wait for acknowledgement for 10 seconds*/
				/* refer to TDcSamMicomInterface.cpp */
				/*dev_info(micom_dev,
				"waiting for micom to respond...attempt %d\n",
				retry);*/
				retry++;
				/* wait for completion event with 100 msecs
				 * timeout
				 */
				if (!wait_for_completion_timeout(
						&micom_msg_ack,
						msecs_to_jiffies(400))) {
					/* If this was the last retry, set the
					 * status as EAGAIN. */
					if (retry >= ACK_RETRY_MAX) {
						dev_warn(micom_dev,
						"ack[%X] timed out\n", ack);
						status = -EAGAIN;
					} else
						continue;
				} else if (micom_check_n_copy_ack(
					micom_user_msg)) {
					/* check for ack event and write device
					 * acknowledgement data from
					 * micom_user_msg to local buffer only
					 * if ack cmd matches with the cmd or
					 * cmd_ack provided by user.
					 */
					status = copy_to_user((void *)arg,
						micom_user_msg, sizeof(struct
						sdp_micom_usr_msg));

					if (status) {
						dev_err(micom_dev,
						"copy to user failed\n");
						status = -EFAULT;
						sdp_micom_msg_unlock();
						goto copy_to_user_fail;
					}
					break;
				}
			}
			sdp_micom_msg_unlock();
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
	mutex_unlock(&dev_msg_lock);

	return status;
}

/*
 *
 *   @fn	static bool micom_check_n_copy_ack(
 *				struct sdp_micom_usr_msg *micom_user_msg);
 *   @brief	check the for completion event and if it is correct one, copy
 *		message	from acknowledgement event buffer to sdp_micom_usr_msg.
 *   @details	everytime wait_for_completion_timeout() completes, this
 *		function is called to check the ack event. If the ack event
 *		occured with correct ack command, copy the micom data from
 *		ack event buffer and return status of ack event.
 *
 *   @param	micom_user_msg	pointer to sdp_micom_usr_msg.

 *   @return	returns status of ack event.
 */
static bool micom_check_n_copy_ack(struct sdp_micom_usr_msg *micom_user_msg)
{
	bool flag_ack = FALSE;
	struct sdp_micom_msg *ack_msg;

	ack_msg = &micom_ack_msg;

	/* check if the received acknowledgement matches with cmd/cmd_ack
	 * provided by user, else ignore ack event. */
	if (ack_msg->msg[0] == ((micom_user_msg->cmd_ack == 0x00) ?
		micom_user_msg->cmd : micom_user_msg->cmd_ack)) {
		/* set flag_ack to TRUE */
		flag_ack = TRUE;
		micom_user_msg->cmd = ack_msg->msg[0];
		memcpy(micom_user_msg->output_param,
			&(ack_msg->msg[1]), KEY_PACKET_PARAM_SIZE);
	}
	return flag_ack;
}

/*
 *
 *   @fn	static void micom_prepare_n_send_msg(
 *				struct sdp_micom_usr_msg *micom_user_msg);
 *   @brief	prepares the sdp_micom_msg and sends it to micom core.
 *   @details
 *
 *   @param	micom_user_msg	pointer to sdp_micom_usr_msg

 *   @return	returns nothing
 */
static void micom_prepare_n_send_msg(struct sdp_micom_usr_msg *micom_user_msg)
{
	struct sdp_micom_msg msg;

	/* store expected acknowledgement command into global variable ack.*/
	ack = micom_user_msg->cmd_ack == 0x00 ? micom_user_msg->cmd :
					micom_user_msg->cmd_ack;
	/* prepare sdp_micom_msg */
	msg.msg_type = MICOM_NORMAL_DATA;
	msg.length = KEY_PACKET_DATA_SIZE;
	msg.msg[0] = micom_user_msg->cmd;
	memcpy(&(msg.msg[1]), micom_user_msg->input_param,
		KEY_PACKET_PARAM_SIZE);

	/* send command to micom core for further processing */
	sdp_micom_send_msg(&msg);
}

#if micom_watchdog_enable
static int kthread_micom_watchdog(void *arg)
{
	unsigned char sleep_count=0;
	while(!kthread_should_stop() && !stop_thd_alive) {
		if( skip_for_suspend_mode == 1 ) {
			msleep_interruptible(1000);
			continue;
		}

		sdp_micom_send_cmd_sync(SDP_MICOM_CMD_ALIVE_MSG,
				SDP_MICOM_ACK_ALIVE_MSG, NULL, 0);
		for( sleep_count = 0 ; sleep_count < 10 ; sleep_count++ ) {
			if( send_i_am_ok ) {
				sdp_micom_send_cmd( SDP_MICOM_CMD_I_AM_OK, NULL, 0);
				send_i_am_ok = 0;
			}
			msleep_interruptible(400);
		}
	}
	return 0;
}
#endif 

#if defined(CONFIG_ARCH_SDP1304)
static int kthread_micom_watchdog_scaler(void *arg)
{
	unsigned int *addr = addr_scaler_blk;
	unsigned int result = 0;
	int err_cnt = 0;

	while(!kthread_should_stop()) {
		if( skip_for_suspend_mode == 1 ) {
			msleep_interruptible(1000);
			continue;
		}

		writel(0x3, addr);
		writel(0x0, addr);
		msleep_interruptible(30);
		result = readl(addr + 4) & 0x11;

		if (result == 0x11) {
			err_cnt++;
			if (err_cnt >= 50) {
				dev_err(micom_dev, "@Scaler watchdog..\n");
				stop_thd_alive = 1;
				break;
			}
		} else
			err_cnt = 0;
	}

	return 0;
}
#endif

void micom_start_scaler_watchdog(void)
{
#if defined(CONFIG_ARCH_SDP1304)
	if (thd_alive_scaler == NULL)
		thd_alive_scaler = (struct task_struct *)kthread_run(kthread_micom_watchdog_scaler,
						NULL, "kthread_watchdog_scaler");
#endif
}
EXPORT_SYMBOL(micom_start_scaler_watchdog);

/*
 *
 *   @fn	static void wt61p807_msg_cb(struct sdp_micom_msg *msg, \
 *						void *data);
 *   @brief	handles callbacks addressed to micom msg device
 *   @details	everytime an irq is generated from micom device, a relevant
 *		registered callback is called from micom core. This callbacks
 *		represents irq handler for micom normal msg device. If the irq
 *		is caused by an acknowledgement type event, it sends a
 *		notification of ack event completion to all waiting threads.
 *		(MICOM_MSG_IOCTL_SEND_MSG ioctls)
 *
 *   @param	msg	pointer to micom message structure
 *		data	micom msg device data
 *
 *   @return	returns nothing
 */

static void wt61p807_msg_cb(struct sdp_micom_msg *msg, void *dev_id)
{

	struct sdp_micom_msg *ack_msg = &micom_ack_msg;

	/* control specific msg */
	switch (msg->msg[0]) {
	case SDP_MICOM_ACK_JACK_ID:
		/*dev_info(micom_dev, "received jack id ack\n");*/
		m_msg_dev.scart_lv_1 = msg->msg[3];
		m_msg_dev.scart_lv_2 = msg->msg[4];
		if (msg->msg[2] & 0x01)
			m_msg_dev.jack_ident = 1;
		else
			m_msg_dev.jack_ident = 0;
		m_msg_dev.jack_ident_ready = 1;
		break;

	case SDP_MICOM_ACK_VERSION:
		m_msg_dev.micom_version = ((msg->msg[1]) | (msg->msg[2] << 8))& 0x7FFF ;
		m_msg_dev.boot_reason = msg->msg[3];
		dev_info(micom_dev, "Micom Version = %d \n", m_msg_dev.micom_version);
		dev_info(micom_dev, "AP Reboot reason = %d \n", m_msg_dev.boot_reason);
		break;

	default:
		break;
	}

	if (ack == msg->msg[0]) {
		/*dev_info(micom_dev, "received acknowledgement 0x%X\n",
				msg->msg[0]);*/
		/* copying normal ack message. */
		ack_msg->msg_type = msg->msg_type;
		ack_msg->length = msg->length;
		memcpy(&(ack_msg->msg), &(msg->msg), msg->length);

		/* wake up notification to thread waiting for
		 * acknowledgement event */
		mutex_lock(&dev_msg_complete_lock);
		complete(&micom_msg_ack);
		mutex_unlock(&dev_msg_complete_lock);
	} else if( msg->msg[0] == SDP_MICOM_ACK_ARE_YOU_ALIVE ) {
		send_i_am_ok = 1;
	}
}

static ssize_t show_jack_ident(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.jack_ident);
}

static ssize_t show_scart_lv_1(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.scart_lv_1);
}

static ssize_t show_scart_lv_2(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.scart_lv_2);
}

static ssize_t show_jack_ident_ready(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.jack_ident_ready);
}

static ssize_t show_micom_version(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%04d", m_msg_dev.micom_version);
}

static ssize_t show_boot_reason(struct device *dev,
			struct device_attribute *attr, char *buf)
{

	if(m_msg_dev.boot_reason == 0)
		return sprintf(buf,"AP Boot reason = %d (Power reboot)\n", m_msg_dev.boot_reason);
	else if(m_msg_dev.boot_reason == 1)
		return sprintf(buf, "AP Boot reason = %d (Watchdog reboot)\n", m_msg_dev.boot_reason);
	else if(m_msg_dev.boot_reason == 2)
		return sprintf(buf, "AP Boot reason = %d (S/W reboot)\n", m_msg_dev.boot_reason);
	else
		return sprintf(buf, "AP Boot reason is Wrong value!! (%d)\n", m_msg_dev.boot_reason);
}



/*
 *
 *   @fn	static int __init wt61p807_msg_probe(
 *					struct platform_device *pdev);
 *   @brief	probe for micom msg device
 *   @details	it uses micom-core for registering the micom cec
 *		device.
 *
 *   @param	pdev	pointer to platform device

 *   @return	returns status of device probing
 */
static int wt61p807_msg_probe(struct platform_device *pdev)
{

	struct wt61p807_msg_data *wt61p807_msg;
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
	cdev_init(&wt61p807_msg_cdev, &wt61p807_msg_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_msg_cdev, devid, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_msg = &m_msg_dev;

	wt61p807_msg->msg_dev = &wt61p807_msg_cdev;
	wt61p807_msg->micom_msg_major = MAJOR(devid);
	wt61p807_msg->ref_count = 0;

	wt61p807_msg->msg_class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(wt61p807_msg->msg_class)) {
		dev_err(&pdev->dev, "failed to create sys class\n");
	} else {
		wt61p807_msg->msg_device = device_create(
						wt61p807_msg->msg_class,
						NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_msg->msg_device)) {
			dev_err(&pdev->dev, "failed to create sys device\n");
			class_destroy(wt61p807_msg->msg_class);
		}
	}

	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_jack_ident);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_scart_lv_1);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_scart_lv_2);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_jack_ident_ready);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}

	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_micom_version);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}

	ret = device_create_file(wt61p807_msg->msg_device,
						&dev_attr_boot_reason);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto dev_fail;
	}

	platform_set_drvdata(pdev, wt61p807_msg);

	/* dynamic initialization of acknowledge completion */
	init_completion(&micom_msg_ack);
	micom_dev = &(pdev->dev);

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);

	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto alloc_fail;
	}

	micom_cb->id		= SDP_MICOM_DEV_MSG;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_msg_cb;
	micom_cb->dev_id	= wt61p807_msg;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		goto cb_fail;
	}
	/* global ack initialization */
	ack = 0x00;
	/* dynamic intialization of mutex for device */
	mutex_init(&dev_msg_lock);

#if defined(CONFIG_ARCH_SDP1304)
	addr_scaler_blk = (unsigned int *)ioremap(SDP_SCALER_FLAG_ADDR, SDP_SCALER_FLAG_SIZE);
#endif

#if micom_watchdog_enable

	/* create watchdog thread */
	if (thd_alive == NULL)
		thd_alive = (struct task_struct *)kthread_run(kthread_micom_watchdog,
						NULL, "kthread_watchdog");
#endif

	//micom version
	sdp_micom_send_cmd(SDP_MICOM_CMD_VERSION, 0, 0);
	dev_dbg(micom_dev,"[%s], send message get micom version \n",__func__);

	return ret;

cb_fail:
alloc_fail:
dev_fail:
cdev_add_fail:
	unregister_chrdev_region(devid, 1);
chrdev_alloc_fail:
	return ret;
}

/*
 *
 *   @fn	static int wt61p807_msg_remove(struct platform_device *pdev);
 *   @brief	remove micom msg device
 *   @details	it uses micom-core for deregistering the micom cec
 *		device.
 *
 *   @param	pdev	pointer to platform device
 *
 *   @return	returns zero
 */

static int wt61p807_msg_remove(struct platform_device *pdev)
{
	struct wt61p807_msg_data *wt61p807_msg;

	if (thd_alive) {
		kthread_stop(thd_alive);
		thd_alive = NULL;
	}

	if (thd_alive_scaler) {
		kthread_stop(thd_alive_scaler);
		thd_alive_scaler = NULL;
	}

	wt61p807_msg = platform_get_drvdata(pdev);
	mutex_destroy(&dev_msg_lock);

	/* destroy micom msg sysfs device and class */
	if (wt61p807_msg->msg_device != NULL) {
		device_destroy(wt61p807_msg->msg_class,
				MKDEV(wt61p807_msg->micom_msg_major, 0));
	}
	if (wt61p807_msg->msg_class != NULL)
		class_destroy(wt61p807_msg->msg_class);

	unregister_chrdev_region(MKDEV(wt61p807_msg->micom_msg_major, 0), 1);

#if defined(CONFIG_ARCH_SDP1304)
	if (addr_scaler_blk) {
		iounmap(addr_scaler_blk);
		addr_scaler_blk = NULL;
	}
#endif

	return 0;
}

static int wt61p807_msg_suspend(struct device *dev)
{
	skip_for_suspend_mode = 1;
	return 0;
}

static int wt61p807_msg_resume(struct device *dev)
{
	skip_for_suspend_mode = 0;
	return 0;
}

static const struct dev_pm_ops wt61p807_msg_driver_pm_ops = {
	.suspend = wt61p807_msg_suspend,
	.resume = wt61p807_msg_resume,
};

/* micom cec driver for probe and removal of device */
static struct platform_driver wt61p807_msg_driver = {
	.probe  = wt61p807_msg_probe,
	.remove = wt61p807_msg_remove,
	.driver = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.pm 	= &wt61p807_msg_driver_pm_ops,
	},
};

module_platform_driver(wt61p807_msg_driver);

MODULE_DESCRIPTION("Micom driver interface for Normal buffer data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
