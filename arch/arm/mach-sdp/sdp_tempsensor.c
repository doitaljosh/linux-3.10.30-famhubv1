/*
 * sdp_tempsensor.c
 *   simple version of sdp_thermal.c
 * 
 * Copyright (C) 2014 Samsung Electronics
 * Ikjoon Jang <ij.jang@samsung.com>
 * Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#define ts_err(sensor, fmt, ...)	dev_err (sensor->dev, fmt, ##__VA_ARGS__)
#define ts_info(sensor, fmt, ...)	dev_info(sensor->dev, fmt, ##__VA_ARGS__)
#define ts_dbg(sensor, fmt, ...)	dev_dbg (sensor->dev, fmt, ##__VA_ARGS__)

struct sdp_temp_sensor;
struct sdp_temp_sensor_chip {
	int (*start)(struct sdp_temp_sensor *sensor);
	int (*stop)(struct sdp_temp_sensor *sensor);
	int (*read)(struct sdp_temp_sensor *sensor);
};

struct sdp_temp_sensor {
	struct device		*dev;
	void __iomem		*regs;
#define TSFLAGS_DEVICE_ACTIVE	(1)
	u32			flags;
	int			last_temp;
	struct workqueue_struct *wq;
	struct delayed_work 	polling;
#define DEFAULT_SAMPLING_RATE	100
	unsigned long		sampling_rate;
	spinlock_t		lock;
	const struct sdp_temp_sensor_chip *chip;
};

/* chip-dependent impls */
static int hawkus_correction;
static int hawkus_ts_start(struct sdp_temp_sensor *sensor)
{
	u32 v;
	void __iomem *fuse_regs;
	int timeout;

	/* TODO: remove hard-coded address of Hawk-us */
	/* read fused correction values  */
	fuse_regs = ioremap(0x1ad80000, 0x20);
	if (!fuse_regs) {
		ts_err(sensor, "Failed to mmap to hawk-us chip-id registers.\n");
		return -ENODEV;
	}
	writel(0x1f, fuse_regs + 0x4);
	for (timeout = 100; timeout > 0; timeout--) {
		v = readl(fuse_regs + 0x0);
		if (v == 0)
			break;
		msleep(1);
	}
	if (timeout <= 0) {
		iounmap(fuse_regs);
		ts_err(sensor, "Failed to read temp-correction value from e-fused registers. temperature might be uncorrect.\n");
		return -ETIMEDOUT;
	}

	v = readl(fuse_regs + 0x10);	/* most top 16 bit */
	hawkus_correction = 25 - (int)(v & 0xff);
	iounmap(fuse_regs);

	/* start the sensor! */
	v = readl(sensor->regs + 0x18);
	v |= 0x1;
	writel(v, sensor->regs + 0x18);

	ts_info (sensor, "correction=%d\n", hawkus_correction);
	return 0;
}
static int hawkus_ts_read(struct sdp_temp_sensor *sensor)
{
	u32 v = readl(sensor->regs + 0x2c);
	int temp = ((v >> 16) & 0xff);
	return temp + hawkus_correction;
}
static struct sdp_temp_sensor_chip chip_hawkus = {
	.start	= hawkus_ts_start,
	.read	= hawkus_ts_read,
};

static void sdp_ts_handle_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct sdp_temp_sensor *sensor =
		container_of(delayed_work, struct sdp_temp_sensor, polling);
	const struct sdp_temp_sensor_chip *chip = sensor->chip;

	spin_lock(&sensor->lock);

	sensor->last_temp = chip->read(sensor);

	spin_unlock(&sensor->lock);

	/* reschedule the next work */
	queue_delayed_work_on(0, sensor->wq, &sensor->polling, sensor->sampling_rate);
}

static int sdp_ts_init(struct platform_device *pdev)
{
	struct sdp_temp_sensor *sensor = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	const struct sdp_temp_sensor_chip *chip = sensor->chip;
	int ret;
	u32 val;

	if (!chip) {
		ts_err(sensor, "unknown chip defined.\n");
		return -ENODEV;
	}
	if (!of_property_read_u32(np, "sampling_rate", &val))
		sensor->sampling_rate = val;

	sensor->wq = create_freezable_workqueue(dev_name(&pdev->dev));
	if (!sensor->wq) {
		ts_err(sensor, "polling wq creation failed\n");
		ret = -ENOMEM;
		goto err_ts_init;
	}

	spin_lock_init(&sensor->lock);

	if (sensor->chip->start)
		sensor->chip->start(sensor);

	/* run! */
	INIT_DELAYED_WORK(&sensor->polling, sdp_ts_handle_work);
	queue_delayed_work_on(0, sensor->wq, &sensor->polling,
				sensor->sampling_rate);

	return 0;
err_ts_init:
	if (sensor->wq) {
		destroy_workqueue(sensor->wq);
		sensor->wq = NULL;
	}
	return ret;
}

/* DEVICE ATTR */
static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdp_temp_sensor *sensor;
	int temperature = 0;

	if (!dev)
		return -ENODEV;
	sensor = dev_get_drvdata(dev);
	if (!sensor)
		return -ENODEV;

	temperature = sensor->last_temp;
	
	return snprintf(buf, 5, "%d\n", temperature);
}
static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);

static struct of_device_id sdp_ts_match[] = {
	{ .compatible = "samsung,sdp-tempsensor-hawkus", .data = &chip_hawkus, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sdp_ts_match);

static int sdp_ts_probe(struct platform_device *pdev)
{
	struct sdp_temp_sensor *sensor = NULL;
	struct resource *res;
	int ret;

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		ret = -ENODEV;
		goto err;
	}
	sensor->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!sensor->regs) {
		dev_err(&pdev->dev, "failed ioremap()!\n");
		ret = -ENOMEM;
		goto err;
	}
	
	/* initialization */
	platform_set_drvdata(pdev, sensor);
	sensor->dev = &pdev->dev;
	sensor->chip = (const struct sdp_temp_sensor_chip *)of_match_node(sdp_ts_match, pdev->dev.of_node)->data;

	ret = sdp_ts_init(pdev);
	if (ret < 0)
		goto err;
	
	device_create_file(&pdev->dev, &dev_attr_temperature);

	ts_info(sensor, "registered successfully.\n");
	return 0;
err:
	if (sensor)
		kfree(sensor);	
	return ret;
}

static int sdp_ts_remove(struct platform_device *pdev)
{
	struct sdp_temp_sensor *sensor = platform_get_drvdata(pdev);

	if (!sensor)
		return -ENODEV;

	cancel_delayed_work(&sensor->polling);
	destroy_workqueue(sensor->wq);

	device_remove_file(&pdev->dev, &dev_attr_temperature);
	
	if (sensor->chip->stop)
		sensor->chip->stop(sensor);

	kfree(sensor);

	pr_info("%s is removed\n", dev_name(&pdev->dev));
	
	return 0;
}

static int sdp_ts_suspend(struct device *dev)
{
	return 0;
}

static int sdp_ts_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdp_temp_sensor *sensor = platform_get_drvdata(pdev);

	if (sensor)
		sensor->chip->start(sensor);

	return 0;
}

static struct dev_pm_ops sdp_ts_pmops = {
	.suspend	= sdp_ts_suspend,
	.resume		= sdp_ts_resume,
};

static struct platform_driver sdp_ts_driver = {
	.probe		= sdp_ts_probe,
	.remove		= sdp_ts_remove,
	.driver		= {
		.name   = "sdp-tempsensor",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_ts_match),
		.pm	= &sdp_ts_pmops,
	},
};

static int __init sdp_ts_driver_init(void)
{
	return platform_driver_register(&sdp_ts_driver);
}

static void __exit sdp_ts_driver_exit(void)
{
	platform_driver_unregister(&sdp_ts_driver);
}

late_initcall_sync(sdp_ts_driver_init);
module_exit(sdp_ts_driver_exit);

