/*
 * sn1202033-regulator.c
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

#define SN1202033_BASE_VOLTAGE	680000
#define SN1202033_STEP_VOLTAGE	10000
#define SN1202033_N_VOLTAGES	128
#define SN1202033_VOUT_NUM		2


#define SN1202033_DELAY			200		/* 200 us */

struct sn1202033_data {
	struct i2c_client *client;
	struct regulator_dev *rdev;
};

static int sn1202033_write_reg(struct i2c_client *client, u8 subaddr, u8 val)
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

static int sn1202033_read_reg(struct i2c_client *client, u8 subaddr, u8 *val)
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

static int sn1202033_get_voltage_sel(struct regulator_dev *rdev)
{
	struct sn1202033_data *sn1202033 = rdev_get_drvdata(rdev);
	int ret;
	u8 subaddr, val = 0;
	int vout_port = rdev->desc->id + 1;
	
	if (vout_port < 1 || vout_port > 2)
		return -EINVAL;
	
	subaddr = vout_port - 1;
	
	ret = sn1202033_read_reg(sn1202033->client, subaddr, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int sn1202033_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct sn1202033_data *sn1202033 = rdev_get_drvdata(rdev);
	int ret;
	u8 subaddr;
	int vout_port = rdev->desc->id + 1;

	if (vout_port < 1 || vout_port > 2)
		return -EINVAL;
	
	subaddr = vout_port - 1;

	ret = sn1202033_write_reg(sn1202033->client, subaddr, (u8)selector);
	if (ret < 0)
		return ret;

	udelay(SN1202033_DELAY);

	return 0;
}

static int sn1202033_set_default(struct i2c_client *client, struct device *dev, int id)
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
	for (val = 0; val < SN1202033_N_VOLTAGES; val++) {
		temp = SN1202033_BASE_VOLTAGE + (val * SN1202033_STEP_VOLTAGE);
		if (def_volt == temp)
			break;
	}
	if (val == SN1202033_N_VOLTAGES) {
		dev_err(dev, "%s-default voltage finding failed!\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "vout%d default volt : %uuV(%u)\n", vout_port, def_volt, val);
	ret = sn1202033_write_reg(client, subaddr, val);
	if (ret < 0) {
		dev_err(dev, "%s-set default value failed!\n", __func__);
		return -EIO;
	}
	
	return 0;
}

static struct regulator_ops sn1202033_regulator_ops = {
	.get_voltage_sel = sn1202033_get_voltage_sel,
	.set_voltage_sel = sn1202033_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_desc sn1202033_regulator_desc[SN1202033_VOUT_NUM]  = {
	{
		.name		= "sn1202033-VOUT1",
		.ops		= &sn1202033_regulator_ops,
		.type		= REGULATOR_VOLTAGE,
		.id			= 0,
		.owner		= THIS_MODULE,
		.min_uV		= SN1202033_BASE_VOLTAGE,
		.uV_step	= SN1202033_STEP_VOLTAGE,
		.n_voltages	= SN1202033_N_VOLTAGES,
	},
	{
		.name		= "sn1202033-VOUT2",
		.ops		= &sn1202033_regulator_ops,
		.type		= REGULATOR_VOLTAGE,
		.id			= 1,
		.owner		= THIS_MODULE,
		.min_uV		= SN1202033_BASE_VOLTAGE,
		.uV_step	= SN1202033_STEP_VOLTAGE,
		.n_voltages	= SN1202033_N_VOLTAGES,
	},
};

static int sn1202033_probe(struct i2c_client *client,
							const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct regulator_init_data *reg_init_data;
	struct regulator_config config = {0, };
	struct regulator_dev *rdev;
	struct sn1202033_data *sn1202033[2];
	int ret;
	int i;
	const char * vout_name[SN1202033_VOUT_NUM];

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENOMEM;
	}

	for (i = 0; i < SN1202033_VOUT_NUM; i++) {
		sn1202033[i] = devm_kzalloc(dev, sizeof(struct sn1202033_data), GFP_KERNEL);
		if (!sn1202033[i])
			return -ENOMEM;

		sn1202033[i]->client = client;
		i2c_set_clientdata(client, sn1202033[i]);

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
	
	for (i = 0; i < SN1202033_VOUT_NUM; i++) {
		/* get vout port from device tree */
		//of_property_read_u32(np, "vout_port", &sn1202033->vout_port);
		//sn1202033[i]->vout_port = i + 1;

		reg_init_data->constraints.name = vout_name[i];
		//sn1202033_regulator_desc[i].name = vout_name[i];
				
		config.dev = dev;
		config.init_data = reg_init_data;
		config.driver_data = sn1202033[i];
		config.of_node = dev->of_node;


		/* set HW defalt */
		ret = sn1202033_set_default(client, dev, i);
		if (ret)
			return ret;

		rdev = regulator_register(&sn1202033_regulator_desc[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "regulator register failed for %s\n", id->name);
			return PTR_ERR(rdev);
		}
	
		sn1202033[i]->rdev = rdev;
	}

	return 0;
}

static int sn1202033_remove(struct i2c_client *client)
{
	struct sn1202033_data *sn1202033 = i2c_get_clientdata(client);

	regulator_unregister(sn1202033->rdev);
	
	return 0;
}

static const struct of_device_id sn1202033_dt_match[] = {
	{ .compatible = "ti,sn1202033", },
	{},
};
MODULE_DEVICE_TABLE(of, sn1202033_dt_match);;

static const struct i2c_device_id sn1202033_id[] = {
	{ .name = "sn1202033" },
	{},
};
MODULE_DEVICE_TABLE(i2c, sn1202033_id);

static struct i2c_driver sn1202033_i2c_driver = {
	.probe		= sn1202033_probe,
	.remove		= sn1202033_remove,
	.driver = {
		.name	= "sn1202033",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sn1202033_dt_match),
	},
	.id_table	= sn1202033_id,
};

static int __init sn1202033_init(void)
{
	return i2c_add_driver(&sn1202033_i2c_driver);
}
subsys_initcall(sn1202033_init);

static void __exit sn1202033_exit(void)
{
	return i2c_del_driver(&sn1202033_i2c_driver);
}
module_exit(sn1202033_exit);

MODULE_DESCRIPTION("SN1202033 voltage regulator driver");
MODULE_LICENSE("GPL v2");
