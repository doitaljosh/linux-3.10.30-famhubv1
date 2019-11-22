/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the term of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/sched.h>

#define DRIVER_NAME		"system-wt61p807"

#define BOOT_REASON_PRE_POWER_ON	39

struct wt61p807_system {
	int poweron_mode;
	struct class *system_class;
	struct device *system_dev;
};

static void wt61p807_system_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	struct wt61p807_system *system = dev_id;

	if (!system)
		return;

	system->poweron_mode = msg->msg[1];
}

static ssize_t wt61p807_system_show_poweron_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct wt61p807_system *system = dev_get_drvdata(dev);

	if ((system->poweron_mode < 0) || (system->poweron_mode == BOOT_REASON_PRE_POWER_ON))
		sdp_micom_send_cmd_sync(SDP_MICOM_CMD_POWERON_MODE, SDP_MICOM_ACK_POWERON_MODE, NULL, 0);

	return sprintf(buf, "%d\n", system->poweron_mode);
}

static ssize_t wt61p807_system_store_poweron_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value = 0;
	struct wt61p807_system *system = dev_get_drvdata(dev);

	printk(KERN_ERR "changed boot reason(%d -> %d). requested by(%s)\n", system->poweron_mode, value, current->comm);

	sscanf(buf, "%d", &value);

	system->poweron_mode = value;

	return strnlen(buf, 255);
}


static DEVICE_ATTR(poweronmode, 0644,
		wt61p807_system_show_poweron_mode, wt61p807_system_store_poweron_mode);

static struct attribute *wt61p807_system_attributes[] = {
	&dev_attr_poweronmode.attr,
	NULL
};

static const struct attribute_group wt61p807_system_group = {
	.attrs = wt61p807_system_attributes,
};

static int wt61p807_system_probe(struct platform_device *pdev)
{
	struct sdp_micom_cb *micom_cb;
	struct wt61p807_system *system;
	int ret;

	system = devm_kzalloc(&pdev->dev,
			sizeof(struct wt61p807_system), GFP_KERNEL);
	if (!system) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	system->poweron_mode = -1;

	platform_set_drvdata(pdev, system);

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);
	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate micom callback\n");
		return -ENOMEM;
	}

	micom_cb->id		= SDP_MICOM_DEV_SYSTEM;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_system_cb;
	micom_cb->dev_id	= system;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &wt61p807_system_group);
	if (ret)
		dev_err(&pdev->dev, "failed to create attribute group\n");

	/* fixed sysfs */
	system->system_class = class_create(THIS_MODULE, "tv_system");
	if (IS_ERR(system->system_class)) {
		ret = PTR_ERR(system->system_class);
		return -EFAULT;
	}

	system->system_dev = device_create(system->system_class, &pdev->dev, pdev->dev.devt, system,
								"micom_info");
	if (IS_ERR(system->system_dev)) {
		ret = PTR_ERR(system->system_dev);
		return -EFAULT;
	}

	ret = device_create_file(system->system_dev, &dev_attr_poweronmode);
	if (ret)
		dev_err(&pdev->dev, "failed to create sysfs.\n");

	return ret;
}

static int wt61p807_system_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &wt61p807_system_group);
	return 0;
}

static int wt61p807_system_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct wt61p807_system *system = platform_get_drvdata(pdev);
	unsigned long start_time = jiffies;

	if (system) {
		/* reset boot reason */
		system->poweron_mode = -1;
	}

	return 0;
}

static int wt61p807_system_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver wt61p807_system_driver = {
	.probe = wt61p807_system_probe,
	.remove = wt61p807_system_remove,
	.suspend = wt61p807_system_suspend,
	.resume = wt61p807_system_resume,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(wt61p807_system_driver);

MODULE_DESCRIPTION("WT61P807 system driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:system");
