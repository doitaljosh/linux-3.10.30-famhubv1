/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <asm/io.h>

#include <linux/t2d_print.h>

#define DRIVER_NAME	"sdp_regctrl"

static int sdp_regctrl_debug = 1;

/* for using sysfs. (class, device, device attribute) */
static struct class *regctrl_class;
static struct device *regctrl_dev;

static unsigned int regctrl_addr;

static DEFINE_MUTEX(regctrl_mutex);

static ssize_t store_regsister_addr(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	mutex_lock(&regctrl_mutex);
	sscanf(buf, "%X", &regctrl_addr);
	mutex_unlock(&regctrl_mutex);
	return count;
}

static ssize_t show_regsister_addr(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%X\n", regctrl_addr);
}

static ssize_t store_regsister_value(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int val;
	unsigned int *vir_addr;

	sscanf(buf, "%X", &val);

	mutex_lock(&regctrl_mutex);

	vir_addr = (unsigned int *)ioremap(regctrl_addr, sizeof(unsigned int));
	writel(val, vir_addr);
	iounmap(vir_addr);

	mutex_unlock(&regctrl_mutex);
	return count;
}

static ssize_t show_regsister_value(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned int val;
	unsigned int *vir_addr;

	mutex_lock(&regctrl_mutex);

	vir_addr = (unsigned int *)ioremap(regctrl_addr, sizeof(unsigned int));
	val = readl(vir_addr);
	iounmap(vir_addr);

	mutex_unlock(&regctrl_mutex);

	return sprintf(buf, "addr=%X value=%X\n", regctrl_addr, val);
}

static DEVICE_ATTR(addr, S_IWUSR | S_IRUGO, show_regsister_addr, 
							store_regsister_addr);
static DEVICE_ATTR(value, S_IWUSR | S_IRUGO, show_regsister_value,
							store_regsister_value);

static char *sdp_regctrl_devnode(struct device *dev, umode_t *mode)
{
	*mode = 0666; /* rw-rw-rw- */
	return NULL;
}

static int __init sdp_regctrl_probe(struct platform_device *pdev)
{
	int res = 0;

	t2d_print(sdp_regctrl_debug, "[%s] Called\n", __func__);

	/* create class. (/sys/class/sdp_regctrl) */
	regctrl_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(regctrl_class)) {
		res = PTR_ERR(regctrl_class);
		goto out;
	}

	regctrl_class->devnode = sdp_regctrl_devnode;

	/* create class device. (/sys/class/sdp_regctrl/sdp_regctrl) */
	regctrl_dev = device_create(regctrl_class, NULL, pdev->dev.devt, NULL,
								DRIVER_NAME);
	if (IS_ERR(regctrl_dev)) {
		res = PTR_ERR(regctrl_dev);
		goto out_unreg_class;
	}
	/* create sysfs file. (/sys/class/sdp_regctrl/sdp_regctrl/...) */
	res = device_create_file(regctrl_dev, &dev_attr_addr);
	if (res) {
		dev_err(regctrl_dev, "[%s] failed to create sysfs\n", __func__);
		goto out_unreg_device;
	}

	res = device_create_file(regctrl_dev, &dev_attr_value);
	if (res) {
		dev_err(regctrl_dev, "[%s] failed to create sysfs\n", __func__);
		goto out_unreg_device;
	}

	return res;

out_unreg_device:
	device_destroy(regctrl_class, pdev->dev.devt);
out_unreg_class:
	class_destroy(regctrl_class);
out:
	return res;
}

static int sdp_regctrl_remove(struct platform_device *pdev)
{
	t2d_print(sdp_regctrl_debug, "[%s] Called\n", __func__);

	/* remove sysfs file */
	device_remove_file(regctrl_dev, &dev_attr_addr);
	device_remove_file(regctrl_dev, &dev_attr_value);
	/* remove class device */
	device_destroy(regctrl_class, pdev->dev.devt);
	/* remove class */
	class_destroy(regctrl_class);

	return 0;
}

static const struct of_device_id sdp_regctrl_dt_match[] = {
	{ .compatible = "samsung,sdp_regctrl" },
	{},
};

static struct platform_driver sdp_regctrl_driver = {
	.probe  = sdp_regctrl_probe,
	.remove = sdp_regctrl_remove,
	.driver = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.of_match_table	= sdp_regctrl_dt_match,
	},
};

module_platform_driver(sdp_regctrl_driver);

MODULE_DESCRIPTION("sdp chip register control driver");
MODULE_LICENSE("GPL");
