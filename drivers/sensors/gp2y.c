/*
 * Copyright (c) 2010 SAMSUNG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/of_gpio.h>

#include "sensors_core.h"

#define DETECT_BY_SDP_ADC

#ifdef DETECT_BY_SDP_ADC
#include <linux/mfd/sdp_micom.h>
#define DETECT_ADC_VALUE	68
#define SDP_MICOM_CMD_LEN	9
#define SDP_MICOM_DATA_LEN	(SDP_MICOM_CMD_LEN - 4)
#define DRIVER_NAME	"gp2y"
#define SDP_MICOM_CMD_LEN	9

#define DEFAULT_POLLING_TIME_MS		200
#define CONT_CHECK_COUNT		2
#else
#define DEFAULT_POLLING_TIME_MS		300
#endif

/*#define GP2Y_DEBUG*/

#define gp2y_dbgmsg(str, args...) pr_info("%s: " str, __func__, ##args)
#define GP2Y_DEBUG
#ifdef GP2Y_DEBUG
#define gprintk(fmt, x...) \
	printk(KERN_INFO "%s(%d):" fmt, __func__, __LINE__, ## x)
#else
#define gprintk(x...) do { } while (0)
#endif

#define VENDOR_NAME	"SHARP"
#define CHIP_NAME       	"GP2Y"

enum {
	PROXIMITY_ENABLED = BIT(0),
};

enum ps_state {
	PS_STATE_NEAR = 0,
	PS_STATE_FAR = 1,
};


/* driver data */
struct gp2y_data {
	struct input_dev *proximity_input_dev;
	struct device *proximity_dev;
	int gpio_num;
	struct mutex power_lock;
	struct hrtimer prox_polling_timer;
	struct work_struct work_prox;
	struct workqueue_struct *wq;

	ktime_t	polling_interval;
	u8 power_state;
	int proximity_value;
	int adc_value;
	int conti_count;
};


static ssize_t proximity_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gp2y_data *gp2y = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		       (gp2y->power_state & PROXIMITY_ENABLED) ? 1 : 0);
}

static ssize_t proximity_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct gp2y_data *gp2y = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	gp2y_dbgmsg("new_value = %d, old state = %d\n",
		    new_value, (gp2y->power_state & PROXIMITY_ENABLED) ? 1 : 0);

	mutex_lock(&gp2y->power_lock);

	if (new_value && !(gp2y->power_state & PROXIMITY_ENABLED)) {

		gp2y->power_state |= PROXIMITY_ENABLED;

		input_report_abs(gp2y->proximity_input_dev, ABS_DISTANCE, 1);
		input_sync(gp2y->proximity_input_dev);

		gp2y->proximity_value = -1;
		gp2y->adc_value = 0;
		gp2y->conti_count = 0;

		hrtimer_start(&gp2y->prox_polling_timer, gp2y->polling_interval, HRTIMER_MODE_REL);

	} else if (!new_value && (gp2y->power_state & PROXIMITY_ENABLED)) {

		hrtimer_cancel(&gp2y->prox_polling_timer);
		cancel_work_sync(&gp2y->work_prox);

		gp2y->proximity_value = -1;
		gp2y->adc_value = 0;
		gp2y->conti_count = 0;

		gp2y->power_state &= ~PROXIMITY_ENABLED;
	}

	mutex_unlock(&gp2y->power_lock);
	
	return size;
}

static ssize_t get_vendor_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VENDOR_NAME);
}

static ssize_t get_chip_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CHIP_NAME);
}

static DEVICE_ATTR(vendor, S_IRUGO, get_vendor_name, NULL);
static DEVICE_ATTR(name, S_IRUGO, get_chip_name, NULL);


static ssize_t proximity_state_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct gp2y_data *gp2y = dev_get_drvdata(dev);

#ifdef DETECT_BY_SDP_ADC
	return (ssize_t)sprintf(buf, "%d\n", gp2y->adc_value);
#else
	return (ssize_t)sprintf(buf, "%d\n", gp2y->proximity_value);
#endif
}

static struct device_attribute dev_attr_proximity_enable =
	__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	       proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_proximity_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

static struct device_attribute dev_attr_proximity_raw_data =
	__ATTR(raw_data, S_IRUGO, proximity_state_show, NULL);

static struct device_attribute *prox_sensor_attrs[] = {
	&dev_attr_proximity_raw_data,
	&dev_attr_vendor,
	&dev_attr_name,
	NULL
};

#ifdef DETECT_BY_SDP_ADC
static void gp2y_get_value_cb(struct sdp_micom_msg *msg, void *dev_id){

	struct gp2y_data *gp2y = dev_id; 
	int proximity_value = 0;
	int proximity_input;
	int adc_value;

	if (!msg) {
		pr_err("%s: msg parameter is null\n", __func__);
		return;
	}

	adc_value = msg->msg[1];
	
	if (adc_value > DETECT_ADC_VALUE) {
		if (gp2y->conti_count++ >= CONT_CHECK_COUNT) {
			gp2y->conti_count = CONT_CHECK_COUNT;
			proximity_value = 1;
		} else {
			proximity_value = 0;
		}
	} else {
		proximity_value = 0;
		gp2y->conti_count = 0;
	}

	if (gp2y->proximity_value != proximity_value)
	{
		if (proximity_value == 1)
			proximity_input = PS_STATE_NEAR;
		else
			proximity_input = PS_STATE_FAR;

		input_report_abs(gp2y->proximity_input_dev,
				ABS_DISTANCE, proximity_input);
		input_sync(gp2y->proximity_input_dev);

		pr_info("prox value = old(%d) new(%d), adc_data[%d]\n", gp2y->proximity_value, proximity_value, adc_value);

	}
	gp2y->proximity_value = proximity_value;
	gp2y->adc_value = adc_value;

}
#endif

static void gp2y_work_func_prox(struct work_struct *work)
{
#ifdef DETECT_BY_SDP_ADC

	// read from micom
	sdp_micom_send_cmd(SDP_MICOM_CMD_GET_PROXIMITY_SENSOR, 0, 0);

#else
	struct gp2y_data *gp2y = container_of(work, struct gp2y_data, work_prox);

	int proximity_value = 0;
	int proximity_input;

	proximity_value = gpio_get_value_cansleep(gp2y->gpio_num);

	if (gp2y->proximity_value != proximity_value)
	{
		if (proximity_value == 1)
			proximity_input = PS_STATE_NEAR;
		else
			proximity_input = PS_STATE_FAR;

		input_report_abs(gp2y->proximity_input_dev,
						ABS_DISTANCE, proximity_input);
		input_sync(gp2y->proximity_input_dev);

		pr_info("prox value = old(%d) new(%d)\n", gp2y->proximity_value, proximity_value);
	}
	gp2y->proximity_value = proximity_value;

#endif
}

static enum hrtimer_restart gp2y_prox_timer_func(struct hrtimer *timer)
{
	struct gp2y_data *gp2y = container_of(timer, struct gp2y_data, prox_polling_timer);

	queue_work(gp2y->wq, &gp2y->work_prox);
	hrtimer_forward_now(&gp2y->prox_polling_timer, gp2y->polling_interval);
	return HRTIMER_RESTART;
}

static int gp2y_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct input_dev *input_dev;
	struct gp2y_data *gp2y;
	struct sdp_micom_cb *micom_cb;
	pr_info("%s, is called\n", __func__);

	gp2y = kzalloc(sizeof(struct gp2y_data), GFP_KERNEL);
	if (!gp2y) {
		pr_err("%s: failed to alloc memory for module data\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	gp2y->proximity_value = -1;
	gp2y->adc_value = 0;
	gp2y->conti_count = 0;

	mutex_init(&gp2y->power_lock);

	hrtimer_init(&gp2y->prox_polling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	gp2y->polling_interval = ns_to_ktime(DEFAULT_POLLING_TIME_MS * NSEC_PER_MSEC);
	gp2y->prox_polling_timer.function = gp2y_prox_timer_func;


	gp2y->wq = create_singlethread_workqueue("gp2y_wq");
	if (!gp2y->wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto err_create_workqueue;
	}

	INIT_WORK(&gp2y->work_prox, gp2y_work_func_prox);

	/* allocate proximity input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_allocate_device_proximity;
	}

	gp2y->proximity_input_dev = input_dev;
	input_set_drvdata(input_dev, gp2y);
	input_dev->name = "proximity_sensor";
	input_set_capability(input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	input_report_abs(gp2y->proximity_input_dev, ABS_DISTANCE, 1);
	input_sync(gp2y->proximity_input_dev);

	gp2y_dbgmsg("registering proximity input device\n");
	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err(" could not register proximity input device\n");
		goto err_input_allocate_device_proximity;
	}

	ret = sensors_create_symlink(&gp2y->proximity_input_dev->dev.kobj,
				input_dev->name);
	if (ret < 0) {
		pr_err("could not create proximity symlink\n");
		goto err_create_symlink_proximity;
	}

	ret = sysfs_create_group(&input_dev->dev.kobj,
				 &proximity_attribute_group);
	if (ret) {
		pr_err("could not create sysfs group\n");
		goto err_sysfs_create_group_proximity;
	}

#ifndef DETECT_BY_SDP_ADC
	gp2y->gpio_num = of_get_named_gpio(pdev->dev.of_node,
						"prox-detect", 0);
						
	pr_info("gpio number is %d\n", gp2y->gpio_num); 

	if (!gpio_is_valid(gp2y->gpio_num)) {
		dev_err(&pdev->dev, "could not get gpio number\n");
		goto err_setup_gpio;
	}
	
	ret = gpio_request_one(gp2y->gpio_num, GPIOF_DIR_IN, "prox-detect");
	if (ret) {
		if (ret == -EBUSY)
			dev_err(&pdev->dev, "gpio busy: %d\n", ret);
		else
			dev_err(&pdev->dev, "can't request gpio: %d\n", ret);
		goto err_setup_gpio;	
	}


#endif

	/* set sysfs for proximity sensor and light sensor */
	ret = sensors_register(gp2y->proximity_dev,
				gp2y, prox_sensor_attrs, "proximity_sensor");
	if (ret) {
		pr_err("%s: cound not register proximity sensor device(%d).\n",
			__func__, ret);
		goto err_proximity_sensor_register_failed;
	}

	platform_set_drvdata(pdev, gp2y);

#ifdef DETECT_BY_SDP_ADC
	
	micom_cb = devm_kzalloc(&pdev->dev, sizeof(struct sdp_micom_cb),GFP_KERNEL);

	if(!micom_cb){
		dev_err(&pdev->dev, "failed to allocate driver data \n");
	}
	
	micom_cb->id  		= SDP_MICOM_DEV_SENSOR;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= gp2y_get_value_cb;
	micom_cb->dev_id	= gp2y;
	
	ret = sdp_micom_register_cb(micom_cb);
	if(ret < 0){
		dev_err(&pdev->dev, "micom callback registration failed ! \n");
	}

#endif 


	pr_info("gp2y_probe is done.");

	return ret;

	/* error, unwind it all */
err_proximity_sensor_register_failed:
	sensors_unregister(gp2y->proximity_dev, prox_sensor_attrs);

#ifdef DETECT_BY_GPIO
err_setup_gpio:
	gpio_free(gp2y->gpio_num);
#endif
	destroy_workqueue(gp2y->wq);
err_create_workqueue:
	sysfs_remove_group(&gp2y->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
err_sysfs_create_group_proximity:
	sensors_remove_symlink(&gp2y->proximity_input_dev->dev.kobj,
				gp2y->proximity_input_dev->name);
err_create_symlink_proximity:
	input_unregister_device(gp2y->proximity_input_dev);
	input_free_device(input_dev);
err_input_allocate_device_proximity:
	mutex_destroy(&gp2y->power_lock);

	kfree(gp2y);
exit:
	pr_err("%s failed. ret = %d\n", __func__, ret);
	return ret;
}

static int gp2y_suspend(struct device *dev)
{
	return 0;
}

static int gp2y_resume(struct device *dev)
{
	return 0;
}

static int gp2y_remove(struct platform_device *pdev)
{
	struct gp2y_data *gp2y = platform_get_drvdata(pdev);

	sensors_unregister(gp2y->proximity_dev, prox_sensor_attrs);

	sensors_remove_symlink(&gp2y->proximity_input_dev->dev.kobj,
					gp2y->proximity_input_dev->name);

	sysfs_remove_group(&gp2y->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
	input_unregister_device(gp2y->proximity_input_dev);

#ifndef DETECT_BY_SDP_ADC
	gpio_free(gp2y->gpio_num);
#endif

	hrtimer_cancel(&gp2y->prox_polling_timer);
	cancel_work_sync(&gp2y->work_prox);

	destroy_workqueue(gp2y->wq);

	mutex_destroy(&gp2y->power_lock);

	kfree(gp2y);

	platform_set_drvdata(pdev, NULL);	

	return 0;
}

static const struct of_device_id gp2y_of_match[] = {
	{ .compatible = "sharp,gp2y" },
	{},
};
MODULE_DEVICE_TABLE(of, gp2y_of_match);

static const struct dev_pm_ops gp2y_pm_ops = {
	.suspend = gp2y_suspend,
	.resume = gp2y_resume
};

static struct platform_driver sensor_driver = {
	.probe		= gp2y_probe,
	.remove		= gp2y_remove,
	.driver		= {
		.name	= "gp2y",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(gp2y_of_match),
	},
};

module_platform_driver(sensor_driver);

MODULE_AUTHOR("SAMSUNG");
MODULE_DESCRIPTION("GP2Y proximity sensor driver");
MODULE_LICENSE("GPL");

