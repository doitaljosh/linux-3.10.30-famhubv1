/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>
#include <media/rc-core.h>
#ifdef CONFIG_SBB_INTERFACE
#include "kernel-socket.h"
#include "sbb-interface.h"
#include <linux/kthread.h>
#endif

#define DRIVER_NAME	"rc-wt61p807"
#ifdef CONFIG_SBB_INTERFACE
static struct rw_handle* hnd;
#define PORT 4040
#define MSG_SIZE 2
#endif

#ifdef CONFIG_ARCH_SDP1404
extern int set_hmp_boostpulse(int duration);
#endif

struct wt61p807_rc_data {
	struct rc_dev *rc;
};

static void wt61p807_rc_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	struct wt61p807_rc_data *wt61p807_rc = dev_id;
#ifdef CONFIG_ARCH_SDP1404
	static unsigned char prekey = 0;
	static unsigned long pretime = 0;
#endif

	#ifdef CONFIG_SBB_INTERFACE
	char buff[MSG_SIZE] = {0};
	if(check_sbb_connected() && check_network_connected())
	{
		buff[0] = msg->msg[0];
		buff[1] = msg->msg[1];
		enqueue_msg(buff, 2, hnd->senderFifo);
	}
	#endif

	switch (msg->msg[0]) {
	case SDP_MICOM_EVT_KEYPRESS:
#ifdef CONFIG_ARCH_SDP1404
		/* boost mode */
		if ((prekey != msg->msg[1]) || jiffies_to_msecs(jiffies - pretime) > 500)
			set_hmp_boostpulse(10000000);
#endif
		rc_keydown(wt61p807_rc->rc, msg->msg[1], 0);
#ifdef CONFIG_ARCH_SDP1404
		prekey = (unsigned char)msg->msg[1];
		pretime = jiffies;
#endif
		break;
	case SDP_MICOM_EVT_KEYRELEASE:
		rc_keyup(wt61p807_rc->rc);
		break;
	}
}

#ifdef CONFIG_SBB_INTERFACE
static int wt61p807_rc_sbb_thread(void *arg)
{
	if (check_network_connected_wait())
	{
		hnd  = register_with_socket_core(SENDER_MODE, PORT, MSG_SIZE);
		kthread_run(kernel_start_tcp_cli, hnd, "clientthread");
	}
	return 0;
}
#endif

static ssize_t wt61p807_rc_store_rc_generate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int key_code;
	struct wt61p807_rc_data *wt61p807_rc = dev_get_drvdata(dev);

	sscanf(buf, "%d", &key_code);

	dev_dbg(dev, "[key generate][%p], key_code = %d\n", wt61p807_rc->rc, key_code);

	rc_keydown(wt61p807_rc->rc, key_code, 0);
	rc_keyup(wt61p807_rc->rc);

	return strnlen(buf, 4);
}

static DEVICE_ATTR(rc_generate, S_IWUSR,
		NULL, wt61p807_rc_store_rc_generate);

static int wt61p807_rc_probe(struct platform_device *pdev)
{
	struct wt61p807_rc_data *wt61p807_rc;
	struct rc_dev *rc;
	struct sdp_micom_cb *micom_cb;
	int ret;

	rc = rc_allocate_device();
	if (!rc) {
		dev_err(&pdev->dev, "rc_dev allocation failed\n");
		return -ENOMEM;
	}

	wt61p807_rc = devm_kzalloc(&pdev->dev,
			sizeof(struct wt61p807_rc_data), GFP_KERNEL);
	if (!wt61p807_rc) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, wt61p807_rc);

	rc->input_name		= "wt61p807 rc device";
	rc->input_phys		= "wt61p807/rc";
	rc->input_id.bustype	= BUS_VIRTUAL;
	rc->input_id.version	= 1;
	rc->driver_name		= DRIVER_NAME;
	rc->map_name		= RC_MAP_WT61P807;
	rc->priv		= pdev;
	rc->driver_type		= RC_DRIVER_SCANCODE;
	rc->allowed_protos	= RC_BIT_ALL;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(&pdev->dev, "rc_dev registration failed\n");
		rc_free_device(rc);
		return ret;
	}

	wt61p807_rc->rc = rc;

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);
	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		rc_unregister_device(rc);
		return -ENOMEM;
	}

	micom_cb->id		= SDP_MICOM_DEV_IR;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_rc_cb;
	micom_cb->dev_id	= wt61p807_rc;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		rc_unregister_device(rc);
		return ret;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_rc_generate.attr);
	if (ret)
		dev_err(&pdev->dev, "failed to create attribute group\n");

// sockets
	#ifdef CONFIG_SBB_INTERFACE
	kthread_run(wt61p807_rc_sbb_thread, hnd, "rc_sbb_thread");
	printk("rc_sbb_thread init ok\n");
	#endif

	return 0;
}

static int wt61p807_rc_remove(struct platform_device *pdev)
{
	struct wt61p807_rc_data *wt61p807_rc = platform_get_drvdata(pdev);

	rc_unregister_device(wt61p807_rc->rc);

	return 0;
}

static struct platform_driver wt61p807_rc_driver = {
	.probe = wt61p807_rc_probe,
	.remove = wt61p807_rc_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(wt61p807_rc_driver);

MODULE_DESCRIPTION("Remote controller driver for WT61P807");
MODULE_LICENSE("GPL");
