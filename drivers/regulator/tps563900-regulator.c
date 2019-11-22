/*
 * tps563900-regulator.c
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

#define TPS563900_BASE_VOLTAGE	680000
#define TPS563900_STEP_VOLTAGE	10000
#define TPS563900_N_VOLTAGES	128
#define TPS563900_VOUT_NUM		2

#define TPS563900_DELAY			200		/* 200 us */

struct tps563900_data {
	struct i2c_client *client;
	struct regulator_dev *rdev;
};

static int tps563900_write_reg(struct i2c_client *client, u8 subaddr, u8 val)
{
	int ret;
	struct i2c_msg msg[2];

	val |= 0x80;

	msg[0].addr = client->addr;
	msg[0].flags = 0;		/* write */
	msg[0].len = 1;			/* subaddr size */
	msg[0].buf = &subaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_NOSTART;/* write */
	msg[1].len = 1;			/* data size */
	msg[1].buf = &val;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: i2c send failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;			
}

static int tps563900_read_reg(struct i2c_client *client, u8 subaddr, u8 *val)
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

	*val = buf & 0x3F;

	return 0;
}

static int tps563900_get_voltage_sel(struct regulator_dev *rdev)
{
	struct tps563900_data *tps563900 = rdev_get_drvdata(rdev);
	int ret;
	u8 subaddr, val = 0;
	int vout_port = rdev->desc->id + 1;
	
	if (vout_port < 1 || vout_port > 2)
		return -EINVAL;
	
	subaddr = vout_port - 1;
	
	ret = tps563900_read_reg(tps563900->client, subaddr, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int tps563900_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct tps563900_data *tps563900 = rdev_get_drvdata(rdev);
	int ret;
	u8 subaddr;
	int vout_port = rdev->desc->id + 1;

	if (vout_port < 1 || vout_port > 2)
		return -EINVAL;
	
	subaddr = vout_port - 1;

	ret = tps563900_write_reg(tps563900->client, subaddr, (u8)selector);
	if (ret < 0)
		return ret;

	udelay(TPS563900_DELAY);

	return 0;
}

static int tps563900_set_default(struct i2c_client *client, struct device *dev, int id)
{
	struct device_node *np = dev->of_node;
	u32 def_volt;
	u32 val, temp;
	u8 subaddr;
	int ret;
	int vout_port = id + 1;

	if (vout_port < 1 || vout_port > 2) {
		dev_err(dev, "%s-invalid vout_port (%d)\n", __func__, vout_port);
		return -EINVAL;
	}
	
	subaddr = vout_port - 1;

	/* get default voltage */
	if (id == 0)
		of_property_read_u32(np, "default_volt1", &def_volt);
	else if (id == 1)
		of_property_read_u32(np, "default_volt2", &def_volt);

	/* find value from voltage */
	for (val = 0; val < TPS563900_N_VOLTAGES; val++) {
		temp = TPS563900_BASE_VOLTAGE + (val * TPS563900_STEP_VOLTAGE);
		if (def_volt == temp)
			break;
	}
	if (val == TPS563900_N_VOLTAGES) {
		dev_err(dev, "%s-default voltage finding failed!\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "vout%d default volt : %uuV(%u)\n", vout_port, def_volt, val);
	ret = tps563900_write_reg(client, subaddr, val);
	if (ret < 0) {
		dev_err(dev, "%s-set default value failed!\n", __func__);
		return -EIO;
	}
	
	return 0;
}

static struct regulator_ops tps563900_regulator_ops = {
	.get_voltage_sel = tps563900_get_voltage_sel,
	.set_voltage_sel = tps563900_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_desc tps563900_regulator_desc[TPS563900_VOUT_NUM]  = {
	{
		.name		= "tps563900-VOUT1",
		.ops		= &tps563900_regulator_ops,
		.type		= REGULATOR_VOLTAGE,
		.id			= 0,
		.owner		= THIS_MODULE,
		.min_uV		= TPS563900_BASE_VOLTAGE,
		.uV_step	= TPS563900_STEP_VOLTAGE,
		.n_voltages	= TPS563900_N_VOLTAGES,
	},
	{
		.name		= "tps563900-VOUT2",
		.ops		= &tps563900_regulator_ops,
		.type		= REGULATOR_VOLTAGE,
		.id			= 1,
		.owner		= THIS_MODULE,
		.min_uV		= TPS563900_BASE_VOLTAGE,
		.uV_step	= TPS563900_STEP_VOLTAGE,
		.n_voltages	= TPS563900_N_VOLTAGES,
	},
};

static int tps563900_probe(struct i2c_client *client,
							const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct tps563900_data *tps563900[2];
	int ret;
	int i;
	const char * vout_name[TPS563900_VOUT_NUM];

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	for (i = 0; i < TPS563900_VOUT_NUM; i++) {
		tps563900[i] = devm_kzalloc(dev, sizeof(struct tps563900_data), GFP_KERNEL);
		if (!tps563900[i])
			return -ENOMEM;

		tps563900[i]->client = client;
		i2c_set_clientdata(client, tps563900[i]);

		vout_name[i] = devm_kzalloc(dev, 10, GFP_KERNEL);
		if (!vout_name[i])
			return -ENOMEM;
	}

	reg_init_data = of_get_regulator_init_data(dev, dev->of_node);
	if (!reg_init_data) {
		dev_err(dev, "Not able to get OF regulator init data\n");
		return -EINVAL;
	}

	/* get vout name */
	of_property_read_string(np, "vout1-name", &vout_name[0]);
	of_property_read_string(np, "vout2-name", &vout_name[1]);
	
	for (i = 0; i < TPS563900_VOUT_NUM; i++) {
		reg_init_data->constraints.name = vout_name[i];
				
		config.dev = dev;
		config.init_data = reg_init_data;
		config.driver_data = tps563900[i];
		config.of_node = dev->of_node;

		/* set HW defalt */
		ret = tps563900_set_default(client, dev, i);
		if (ret)
			return ret;

		rdev = regulator_register(&tps563900_regulator_desc[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "regulator register failed for %s\n", id->name);
			return PTR_ERR(rdev);
		}
	
		tps563900[i]->rdev = rdev;
	}

	return 0;
}

static int tps563900_remove(struct i2c_client *client)
{
	struct tps563900_data *tps563900 = i2c_get_clientdata(client);

	regulator_unregister(tps563900->rdev);
	
	return 0;
}

static const struct of_device_id tps563900_dt_match[] = {
	{ .compatible = "ti,tps563900", },
	{},
};
MODULE_DEVICE_TABLE(of, tps563900_dt_match);;

static const struct i2c_device_id tps563900_id[] = {
	{ .name = "tps563900" },
	{},
};
MODULE_DEVICE_TABLE(i2c, tps563900_id);

static struct i2c_driver tps563900_i2c_driver = {
	.probe		= tps563900_probe,
	.remove		= tps563900_remove,
	.driver = {
		.name	= "tps563900",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps563900_dt_match),
	},
	.id_table	= tps563900_id,
};

static int __init tps563900_init(void)
{
	return i2c_add_driver(&tps563900_i2c_driver);
}
subsys_initcall(tps563900_init);

static void __exit tps563900_exit(void)
{
	return i2c_del_driver(&tps563900_i2c_driver);
}
module_exit(tps563900_exit);

MODULE_DESCRIPTION("TPS563900 voltage regulator driver");
MODULE_LICENSE("GPL v2");
