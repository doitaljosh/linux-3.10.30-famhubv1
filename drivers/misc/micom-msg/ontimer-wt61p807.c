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
#include <linux/rtc.h>

#define DRIVER_NAME		"ontimer-wt61p807"
#define SDP_MICOM_DATA_LEN	5

struct wt61p807_ontimer {
	struct rtc_time tm;
	int type;
};

static ssize_t wt61p807_ontimer_set_type(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wt61p807_ontimer *tm = platform_get_drvdata(pdev);
	int temp;

	sscanf(buf, "%d", &temp);
	tm->type = temp;
	//printk( KERN_CRIT "Ontimer type:%d\n", temp);
	printk( "Ontimer type:%d\n", temp);

	return count;
}

static ssize_t wt61p807_ontimer_set_time(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wt61p807_ontimer *tm = platform_get_drvdata(pdev);
	unsigned long temp;

	sscanf(buf, "%ld", &temp);

	//printk( KERN_CRIT "Ontimer_set_time->(%ld)\n", temp);
	printk( "Ontimer_set_time->(%ld)\n", temp);
	rtc_time_to_tm(temp, &tm->tm);
	//printk( KERN_CRIT "Ontimer converted(%d/%d/%d %d:%d:%d)\n", tm->tm.tm_year, tm->tm.tm_mon, tm->tm.tm_mday, tm->tm.tm_hour, tm->tm.tm_min, 0);
	printk( "Ontimer converted(%d/%d/%d %d:%d:%d)\n", tm->tm.tm_year, tm->tm.tm_mon, tm->tm.tm_mday, tm->tm.tm_hour, tm->tm.tm_min, 0);

	return count;
}

static ssize_t wt61p807_ontimer_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wt61p807_ontimer *tm = platform_get_drvdata(pdev);
	char data[SDP_MICOM_DATA_LEN];
	char cmd, ack;
	int converted_year=0, converted_mon=0;

	if (strncmp(buf, "0", 1) == 0) {
		data[0] = (unsigned char)tm->type;
		cmd = SDP_MICOM_CMD_DISABLE_ONTIMER;
		ack = SDP_MICOM_ACK_DISABLE_ONTIMER;
	} else if (strncmp(buf, "1", 1) == 0) {
		if ( 100 <= tm->tm.tm_year ) {
			converted_year = tm->tm.tm_year - 100;	//Param 100 mean 2000 year
			converted_mon = tm->tm.tm_mon + 1;		//tm_mon has 0 ~ 11

			data[0] = tm->tm.tm_hour;
			data[1] = tm->tm.tm_min;
			data[2] = (converted_year << 1) | ((converted_mon & 0x08) >> 3);
			data[3] = ((converted_mon & 0x07) << 5) | tm->tm.tm_mday;
			data[4] = (unsigned char)tm->type;
			cmd = SDP_MICOM_CMD_ENABLE_ONTIMER;
			ack = SDP_MICOM_ACK_ENABLE_ONTIMER;
		} else {
			printk("Invalid Ontimer Setting(%04d year)\n", tm->tm.tm_year);
		}
	} else
		return count;

	//printk( KERN_CRIT "Ontimer-enable(%c)(%d/%d/%d %d:%d:%d)\n", buf[0], converted_year, converted_mon, tm->tm.tm_mday, tm->tm.tm_hour, tm->tm.tm_min, 0);
	printk( "Ontimer-enable(%c)(%d/%d/%d %d:%d:%d)\n", buf[0], converted_year, converted_mon, tm->tm.tm_mday, tm->tm.tm_hour, tm->tm.tm_min, 0);
	sdp_micom_send_cmd_sync(cmd, ack, data, SDP_MICOM_DATA_LEN);

	return count;
}

static DEVICE_ATTR(type, S_IWUSR, NULL, wt61p807_ontimer_set_type);
static DEVICE_ATTR(time, S_IWUSR, NULL, wt61p807_ontimer_set_time);
static DEVICE_ATTR(enable, S_IWUSR, NULL, wt61p807_ontimer_enable);

static struct attribute *wt61p807_ontimer_attributes[] = {
	&dev_attr_type.attr,
	&dev_attr_time.attr,
	&dev_attr_enable.attr,
	NULL
};

static const struct attribute_group wt61p807_ontimer_group = {
	.attrs = wt61p807_ontimer_attributes,
};

static int wt61p807_ontimer_probe(struct platform_device *pdev)
{
	struct wt61p807_ontimer *tm;
	int ret;

	tm = devm_kzalloc(&pdev->dev, sizeof(*tm), GFP_KERNEL);
	if (!tm) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, tm);

	ret = sysfs_create_group(&pdev->dev.kobj, &wt61p807_ontimer_group);
	if (ret)
		dev_err(&pdev->dev, "failed to create attribute group\n");

	return ret;
}

static int wt61p807_ontimer_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &wt61p807_ontimer_group);
	return 0;
}

static struct platform_driver wt61p807_ontimer_driver = {
	.probe = wt61p807_ontimer_probe,
	.remove = wt61p807_ontimer_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(wt61p807_ontimer_driver);

MODULE_DESCRIPTION("WT61P807 Ontimer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ontimer");
