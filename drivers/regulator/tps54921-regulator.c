/*
 * tps54921-regulator.c
 *
 * Copyright 2013 Samsung Electronics
 *
 * Author: Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#define TPS54921_BASE_VOLTAGE	720000
#define TPS54921_STEP_VOLTAGE	10000
#define TPS54921_N_VOLTAGES	77

#define TPS54921_DELAY		200		/* 200 us */

#define TPS54921_INVALID_VAL	0xffffffff

struct tps54921_data {
	struct i2c_client *client;
	struct regulator_dev *rdev;
	u32 def_volt;
	bool is_suspend;
	u32 cur_val;
};

static int tps54921_write_reg(struct i2c_client *client, unsigned int val)
{
	u8 buf;
	u8 checksum = 0;
	int ret;
	int i;
	struct i2c_msg msg;

	buf = (u8)val & 0x7F;	/* register value */

	/* add checksum bit */
	for (i = 0; i < 7; i++) {
		if (val & 0x1)
			checksum++;
		val = val >> 1;
	}
	if (checksum % 2)
		buf |= 1 << 7;

	msg.addr = client->addr;
	msg.flags = 0;		/* write */
	msg.len = 1;			/* subaddr size */
	msg.buf = &buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c send failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;			
}

static int tps54921_get_voltage_sel(struct regulator_dev *rdev)
{
	struct tps54921_data *tps54921 = rdev_get_drvdata(rdev);

	if (tps54921->is_suspend == true) {
		dev_warn(&rdev->dev, "get voltage already suspended\n");
		return -EPERM;
	}

	if (tps54921->cur_val == TPS54921_INVALID_VAL)
		return -EPERM;

	return (int)tps54921->cur_val;
}

static int tps54921_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	int ret;
	struct tps54921_data *tps54921 = rdev_get_drvdata(rdev);

	if (tps54921->is_suspend == true) {
		dev_warn(&rdev->dev, "set voltage already suspended\n");
		return -EPERM;
	}

	ret = tps54921_write_reg(tps54921->client, selector);
	if (ret < 0)
		return ret;

	udelay(TPS54921_DELAY);

	tps54921->cur_val = selector;

	return 0;
}

static int tps54921_set_default(struct device *dev, struct tps54921_data *tps54921)
{
	u32 val, temp;
	int ret;
	struct i2c_client *client = tps54921->client;

	/* find value from voltage */
	for (val = 0; val < TPS54921_N_VOLTAGES; val++) {
		temp = TPS54921_BASE_VOLTAGE + (val * TPS54921_STEP_VOLTAGE);
		if (tps54921->def_volt == temp)
			break;
	}
	if (val == TPS54921_N_VOLTAGES) {
		dev_err(dev, "%s-default voltage finding failed!\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "default volt : %uuV(%u)\n", tps54921->def_volt, val);
	ret = tps54921_write_reg(client, val);
	if (ret < 0) {
		dev_err(dev, "%s-set default value failed!\n", __func__);
		return -EIO;
	}
	
	return 0;
}

static struct regulator_ops tps54921_regulator_ops = {
	.get_voltage_sel = tps54921_get_voltage_sel,
	.set_voltage_sel = tps54921_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_desc tps54921_regulator_desc  = {
	.name		= "tps54921",
	.ops		= &tps54921_regulator_ops,
	.type		= REGULATOR_VOLTAGE,
	.id		= 0,
	.owner		= THIS_MODULE,
	.min_uV		= TPS54921_BASE_VOLTAGE,
	.uV_step	= TPS54921_STEP_VOLTAGE,
	.n_voltages	= TPS54921_N_VOLTAGES,
};

static int get_device_tree_settings(struct device *dev, struct tps54921_data *tps54921)
{
	struct device_node *np = dev->of_node;

	/* get default voltage */
	of_property_read_u32(np, "default_volt", &tps54921->def_volt);
	if (!tps54921->def_volt) {
		dev_err(dev, "can't get default voltage\n");
		return -EINVAL;
	}

	return 0;
}

static int tps54921_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct tps54921_data *tps54921;
	int ret;
	int retry = 3;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	tps54921 = devm_kzalloc(dev, sizeof(struct tps54921_data), GFP_KERNEL);
	if (!tps54921)
		return -ENOMEM;

	tps54921->client = client;
	i2c_set_clientdata(client, tps54921);
	
	tps54921->is_suspend = false;

	tps54921->cur_val = TPS54921_INVALID_VAL;

	reg_init_data = of_get_regulator_init_data(dev, dev->of_node);
	if (!reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return -EINVAL;
	}

	config.dev = dev;
	config.init_data = reg_init_data;
	config.driver_data = tps54921;
	config.of_node = dev->of_node;

#ifdef CONFIG_OF
	ret = get_device_tree_settings(dev, tps54921);
	if (ret < 0)
		goto out;
#endif

	/* set HW defalt */
	if (of_find_property(dev->of_node, "regulator-boot-on", NULL)) {
		do {
			ret = tps54921_set_default(dev, tps54921);
			if (!ret)
				break;
			dev_warn(dev, "set default volt fail. retry.\n");
		} while (retry--);
		/* if all tries are failed, return */
		if (ret)
			return ret;
	}

	rdev = regulator_register(&tps54921_regulator_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "regulator register failed for %s\n", id->name);
		return PTR_ERR(rdev);
	}

	tps54921->rdev = rdev;

out:
	return 0;
}

static int tps54921_remove(struct i2c_client *client)
{
	struct tps54921_data *tps54921 = i2c_get_clientdata(client);
	
	if (tps54921 == NULL)
		return -1;

	regulator_unregister(tps54921->rdev);
	
	return 0;
}

#ifdef CONFIG_PM
static int tps54921_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct tps54921_data *tps54921 = i2c_get_clientdata(client);

	if (tps54921 == NULL)
		return -1;

	tps54921->is_suspend = true;
	
	return 0;
}

static int tps54921_resume(struct i2c_client *client)
{
	struct tps54921_data *tps54921 = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int ret, retry = 3;
	
	if (tps54921 == NULL)
		return -1;
	
	tps54921->is_suspend = false;

	if (of_find_property(dev->of_node, "regulator-boot-on", NULL)) {
		do {
			ret = tps54921_set_default(dev, tps54921);
			if (!ret)
				break;
			dev_warn(dev, "set default volt fail. retry.\n");
		} while (retry--);
		/* if all tries are failed, return */
		if (ret)
			return ret;
	}
	
	return 0;
}
#else
#define tps54921_suspend	NULL
#define tps54921_resume		NULL
#endif

static const struct of_device_id tps54921_dt_match[] = {
	{ .compatible = "ti,tps54921", },
	{},
};
MODULE_DEVICE_TABLE(of, tps54921_dt_match);

static const struct i2c_device_id tps54921_id[] = {
	{ .name = "tps54921" },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps54921_id);

static struct i2c_driver tps54921_i2c_driver = {
	.probe		= tps54921_probe,
	.remove		= tps54921_remove,
	.suspend	= tps54921_suspend,
	.resume		= tps54921_resume,
	.driver = {
		.name	= "tps54921",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps54921_dt_match),
	},
	.id_table	= tps54921_id,
};

static int __init tps54921_init(void)
{
	return i2c_add_driver(&tps54921_i2c_driver);
}
subsys_initcall(tps54921_init);

static void __exit tps54921_exit(void)
{
	return i2c_del_driver(&tps54921_i2c_driver);
}
module_exit(tps54921_exit);

MODULE_DESCRIPTION("TPS54921 voltage regulator driver");
MODULE_LICENSE("GPL v2");
