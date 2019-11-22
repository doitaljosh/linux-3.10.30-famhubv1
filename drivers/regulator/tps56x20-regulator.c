/*
 * tps56x20-regulator.c
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

#define TPS56X20_BASE_VOLTAGE	600000
#define TPS56X20_STEP_VOLTAGE	10000
#define TPS56X20_N_VOLTAGES		128

#define TPS56X20_DELAY			200		/* 200 us */

struct tps56x20_data {
	struct i2c_client *client;
	struct regulator_dev *rdev;
	u32 def_volt;
	bool is_poweroff;
	bool is_suspend;
};

static int tps56x20_write_reg(struct i2c_client *client, u8 subaddr, unsigned int val)
{
	u8 buf;
	u8 checksum = 0;
	int ret;
	int i;
	struct i2c_msg msg[2];

	if (subaddr == 0x0) {
		buf = (u8)val & 0x7F;	/* register value */

		/* add checksum bit */
		for (i = 0; i < 7; i++) {
			if (val & 0x1)
				checksum++;
			val = val >> 1;
		}
		if ((checksum % 2) == 0)
			buf |= 1 << 7;
	}

	msg[0].addr = client->addr;
	msg[0].flags = 0;		/* write */
	msg[0].len = 1;			/* subaddr size */
	msg[0].buf = &subaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_NOSTART;/* write */
	msg[1].len = 1;			/* data size */
	msg[1].buf = &buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c send failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;			
}

static int tps56x20_read_reg(struct i2c_client *client, u8 subaddr, u8 *val)
{
	u8 buf;
	int ret;
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].flags = 0;		/* write */
	msg[0].len = 1;			/* subaddr size */
	msg[0].buf = &subaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;/* read */
	msg[1].len = 1;			/* data size */
	msg[1].buf = &buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c read fail (%d)\n", __func__, ret);
		return ret;
	}

	*val = buf;

	return 0;
}

static int tps56x20_get_voltage_sel(struct regulator_dev *rdev)
{
	int ret;
	u8 val;
	struct tps56x20_data *tps56x20 = rdev_get_drvdata(rdev);

	if (tps56x20->is_suspend == true) {
		dev_warn(&rdev->dev, "get voltage already suspended\n");
		return -EPERM;
	}

	ret = tps56x20_read_reg(tps56x20->client, 0x0, &val);
	if (ret < 0)
		return ret;

	return val & 0x7F;
}

static int tps56x20_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	int ret;
	struct tps56x20_data *tps56x20 = rdev_get_drvdata(rdev);

	if (tps56x20->is_suspend == true) {
		dev_warn(&rdev->dev, "set voltage already suspended\n");
		return -EPERM;
	}

	ret = tps56x20_write_reg(tps56x20->client, 0x0, selector);
	if (ret < 0)
		return ret;

	udelay(TPS56X20_DELAY);

	return 0;
}

static int tps56x20_set_default( struct device *dev, struct tps56x20_data *tps56x20)
{
	u32 val, temp;
	int ret;
	struct i2c_client *client = tps56x20->client;

	/* find value from voltage */
	for (val = 0; val < TPS56X20_N_VOLTAGES; val++) {
		temp = TPS56X20_BASE_VOLTAGE + (val * TPS56X20_STEP_VOLTAGE);
		if (tps56x20->def_volt == temp)
			break;
	}
	if (val == TPS56X20_N_VOLTAGES) {
		dev_err(dev, "%s-default voltage finding failed!\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "default volt : %uuV(%u)\n", tps56x20->def_volt, val);
	ret = tps56x20_write_reg(client, 0x0, val);
	if (ret < 0) {
		dev_err(dev, "%s-set default value failed!\n", __func__);
		return -EIO;
	}
	
	return 0;
}

static int tps56x20_enable(struct regulator_dev *rdev)
{
	int ret;
	u8 val;
	struct tps56x20_data *tps56x20 = rdev_get_drvdata(rdev);
	struct i2c_client *client = tps56x20->client;

	if (tps56x20->is_poweroff == false)
		return 0;

	dev_info(&rdev->dev, "tps56x20 enable\n");

	/* read CONTROL B register */
	ret = tps56x20_read_reg(client, 0x9, &val);
	if (ret < 0)
		return -EIO;

	/* enable */
	val |= 1 << 7;
	ret = tps56x20_write_reg(client, 0x9, val);
	if (ret < 0)
		return -EIO;

	/* check enable bit */
	ret = tps56x20_read_reg(client, 0x9, &val);
	if (ret < 0)
		return -EIO;

	if (((val >> 7) & 0x1) == 0) {
		dev_warn(&rdev->dev, "failed to enable tps56x20\n");
		return -EAGAIN;
	}
	
	return 0;
}

static int tps56x20_disable(struct regulator_dev *rdev)
{
	int ret;
	u8 val;
	struct device *dev = &rdev->dev;
	struct tps56x20_data *tps56x20 = rdev_get_drvdata(rdev);
	struct i2c_client *client = tps56x20->client;

	if (tps56x20->is_poweroff == false)
		return 0;

	dev_info(&rdev->dev, "tps56x20 disable(power off)\n");
	
	if (tps56x20->is_poweroff == false) {
		dev_info(dev, "this pmic is not support power off\n");
		return 0;
	}

	/* read CONTROL B register */
	ret = tps56x20_read_reg(client, 0x9, &val);
	if (ret < 0)
		return -EIO;

	/* disable */
	val &= ~(1 << 7);
	ret = tps56x20_write_reg(client, 0x9, val);
	if (ret < 0)
		return -EIO;
		
	return 0;
}

static int tps56x20_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	u8 val;
	struct device *dev = &rdev->dev;
	struct tps56x20_data *tps56x20 = rdev_get_drvdata(rdev);
	struct i2c_client *client = tps56x20->client;

	/* 
	 * if not support power off,
	 * pmic is always enabled
	 */
	if (tps56x20->is_poweroff == false)
		return 1;

	/* read CONTROL B register */
	ret = tps56x20_read_reg(client, 0x9, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read enable bit\n");
		return -EIO;
	}

	return (val >> 7) & 0x1;
}

static struct regulator_ops tps56x20_regulator_ops = {
	.get_voltage_sel = tps56x20_get_voltage_sel,
	.set_voltage_sel = tps56x20_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.enable = tps56x20_enable,
	.disable = tps56x20_disable,
	.is_enabled = tps56x20_is_enabled,
};

static const struct regulator_desc tps56x20_regulator_desc  = {
	.name		= "tps56x20",
	.ops		= &tps56x20_regulator_ops,
	.type		= REGULATOR_VOLTAGE,
	.id			= 0,
	.owner		= THIS_MODULE,
	.min_uV		= TPS56X20_BASE_VOLTAGE,
	.uV_step	= TPS56X20_STEP_VOLTAGE,
	.n_voltages	= TPS56X20_N_VOLTAGES,
};

static int get_device_tree_settings(struct device *dev, struct tps56x20_data *tps56x20)
{
	struct device_node *np = dev->of_node;

	/* get default voltage */
	of_property_read_u32(np, "default_volt", &tps56x20->def_volt);
	if (!tps56x20->def_volt) {
		dev_err(dev, "can't get default voltage\n");
		return -EINVAL;
	}

	/* get power off */
	if (of_find_property(np, "tps56x20-support-poweroff", NULL)) {
		dev_info(dev, "support power off\n");
		tps56x20->is_poweroff = true;
	} else {
		tps56x20->is_poweroff = false;
	}
	
	return 0;
}

static int tps56x20_probe(struct i2c_client *client,
							const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct tps56x20_data *tps56x20;
	int ret;
	int retry = 3;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	tps56x20 = devm_kzalloc(dev, sizeof(struct tps56x20_data), GFP_KERNEL);
	if (!tps56x20)
		return -ENOMEM;

	tps56x20->client = client;
	i2c_set_clientdata(client, tps56x20);
	
	tps56x20->is_suspend = false;

	reg_init_data = of_get_regulator_init_data(dev, dev->of_node);
	if (!reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return -EINVAL;
	}

	config.dev = dev;
	config.init_data = reg_init_data;
	config.driver_data = tps56x20;
	config.of_node = dev->of_node;

#ifdef CONFIG_OF
	ret = get_device_tree_settings(dev, tps56x20);
	if (ret < 0)
		goto out;
#endif

	/* set HW defalt */
	if (of_find_property(dev->of_node, "regulator-boot-on", NULL)) {
		do {
			ret = tps56x20_set_default(dev, tps56x20);
			if (!ret)
				break;
			dev_warn(dev, "set default volt fail. retry.\n");
		} while (retry--);
		/* if all tries are failed, return */
		if (ret)
			return ret;
	}

	rdev = regulator_register(&tps56x20_regulator_desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "regulator register failed for %s\n", id->name);
		return PTR_ERR(rdev);
	}

	tps56x20->rdev = rdev;

out:
	return 0;
}

static int tps56x20_remove(struct i2c_client *client)
{
	struct tps56x20_data *tps56x20 = i2c_get_clientdata(client);
	
	if (tps56x20 == NULL)
		return -1;

	regulator_unregister(tps56x20->rdev);
	
	return 0;
}

#ifdef CONFIG_PM
static int tps56x20_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct tps56x20_data *tps56x20 = i2c_get_clientdata(client);

	if (tps56x20 == NULL)
		return -1;

	tps56x20->is_suspend = true;
	
	return 0;
}

static int tps56x20_resume(struct i2c_client *client)
{
	struct tps56x20_data *tps56x20 = i2c_get_clientdata(client);
	
	if (tps56x20 == NULL)
		return -1;
	
	tps56x20->is_suspend = false;
	
	return 0;
}
#else
#define tps56x20_suspend	NULL
#define tps56x20_resume		NULL
#endif


static const struct of_device_id tps56x20_dt_match[] = {
	{ .compatible = "ti,tps56920", },
	{ .compatible = "ti,tps56720", },
	{ .compatible = "ti,tps56c20", },
	{ .compatible = "ti,tps56520", },
	{},
};
MODULE_DEVICE_TABLE(of, tps56x20_dt_match);

static const struct i2c_device_id tps56x20_id[] = {
	{ .name = "tps56920" },
	{ .name = "tps56720" },
	{ .name = "tps56c20" },
	{ .name = "tps56520" },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps56x20_id);

static struct i2c_driver tps56x20_i2c_driver = {
	.probe		= tps56x20_probe,
	.remove		= tps56x20_remove,
	.suspend	= tps56x20_suspend,
	.resume		= tps56x20_resume,
	.driver = {
		.name	= "tps56x20",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps56x20_dt_match),
	},
	.id_table	= tps56x20_id,
};

static int __init tps56x20_init(void)
{
	return i2c_add_driver(&tps56x20_i2c_driver);
}
subsys_initcall(tps56x20_init);

static void __exit tps56x20_exit(void)
{
	return i2c_del_driver(&tps56x20_i2c_driver);
}
module_exit(tps56x20_exit);

MODULE_DESCRIPTION("TPS56x20 voltage regulator driver");
MODULE_LICENSE("GPL v2");
