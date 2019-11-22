/*
 * sdpmicom-regulator.c
 *
 * Copyright 2014 Samsung Electronics
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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define CHANGE_DELAY_US		100

struct sdpmicom_regulator_state {
	int volt;
	int state;
};

struct sdpmicom_regulator_config {
	const char *supply_name;

	int nr_gpios;
	
	struct sdpmicom_regulator_state *states;
	int nr_states;
	u32 def_state;

	struct regulator_init_data *init_data;

	u32 cmd_addr;
};

struct sdpmicom_regulator_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;

	int nr_gpios;

	struct sdpmicom_regulator_state *states;
	int nr_states;

	int state;

	u32 cmd_addr;	/* micom command address (ex: 0x7A, 0x7B) */
};

#ifdef CONFIG_SDP_MESSAGEBOX
extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);
#endif

static int sdpmicom_set_state(struct regulator_dev *dev, int state)
{
#ifdef CONFIG_SDP_MESSAGEBOX
	int i, ret, retry = 3;
	struct sdpmicom_regulator_data *data = rdev_get_drvdata(dev);
	char buff[12] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	if (data->nr_gpios > 2 || data->nr_gpios < 1) {
		dev_err(&dev->dev, "nr_gpios is not valid. %d\n",
			data->nr_gpios);
		return -EINVAL;
	}

	/* set command */
	buff[2] = data->cmd_addr;

	for (i = 0; i < data->nr_gpios; i++) {
		/* gpio high/low */
		buff[3 + i] = (state >> i) & 0x1;
	}
	
	/* calc checksum */
	buff[8] = buff[2] + buff[3] + buff[4];	
		
	/* send message */
	dev_dbg(&dev->dev, "send micom cmd : 0xff 0xff 0x%02x 0x%02x 0x%02x\n",
			buff[2], buff[3], buff[4]);
	do {
		ret = sdp_messagebox_write(buff, 9);
		if (ret == 9)
			break;
		dev_dbg(&dev->dev, "retry send message %d\n", retry);
	} while (retry--);
	if (!retry)
		dev_dbg(&dev->dev, "micom comm failed\n");
#endif
	return 0;
}

static int sdpmicom_regulator_get_value(struct regulator_dev *dev)
{
	struct sdpmicom_regulator_data *data = rdev_get_drvdata(dev);
	int ptr;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].state == data->state)
			return data->states[ptr].volt;

	return -EINVAL;
}

static int sdpmicom_regulator_set_voltage(struct regulator_dev *dev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	struct sdpmicom_regulator_data *data = rdev_get_drvdata(dev);
	int target = 0, best_val = INT_MAX;
	int ret, i;

	for (i = 0; i < data->nr_states; i++) {
		if (min_uV <= data->states[i].volt ||
			(i == (data->nr_states - 1) &&
			min_uV > data->states[i].volt)) {
			target = data->states[i].state;
			best_val = data->states[i].volt;
			
			if (selector)
				*selector = i;
			dev_dbg(&dev->dev, "state%d - %uuV selected\n",
				target, best_val);

			break;
		}
	}

	if (best_val == INT_MAX) {
		dev_err(&dev->dev, "can't find voltage %uuV\n", min_uV);
		return -EINVAL;
	}

	ret = sdpmicom_set_state(dev, target);
	if (ret)
		return ret;
	
	data->state = target;

	udelay(CHANGE_DELAY_US);

	return 0;
}

static int sdpmicom_regulator_list_voltage(struct regulator_dev *dev,
				      unsigned selector)
{
	struct sdpmicom_regulator_data *data = rdev_get_drvdata(dev);

	if (selector >= data->nr_states)
		return -EINVAL;

	return data->states[selector].volt;
}

static struct regulator_ops sdpmicom_regulator_voltage_ops = {
	.get_voltage = sdpmicom_regulator_get_value,
	.set_voltage = sdpmicom_regulator_set_voltage,
	.list_voltage = sdpmicom_regulator_list_voltage,
};

static struct sdpmicom_regulator_config *
of_get_regulator_configs(struct device *dev, struct device_node *np)
{
	struct sdpmicom_regulator_config *config;
	struct property *prop;
	int proplen, i;

	config = devm_kzalloc(dev,
			sizeof(struct sdpmicom_regulator_config),
			GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->init_data = of_get_regulator_init_data(dev, np);
	if (!config->init_data)
		return ERR_PTR(-EINVAL);

	config->supply_name = config->init_data->constraints.name;
	dev_info(dev, "regulator name = %s\n", config->supply_name);

	of_property_read_u32(np, "nr-gpios", &config->nr_gpios);
	if (!config->nr_gpios) {
		dev_err(dev, "No 'nr-gpios' property found\n");
		return ERR_PTR(-EINVAL);
	}

	of_property_read_u32(np, "default-state", &config->def_state);

	/* core: 0x7a, cpugpu: 0x7b */
	of_property_read_u32(np, "cmd-addr", &config->cmd_addr);
	
	/* get states */
	prop = of_find_property(np, "states", NULL);
	if (!prop) {
		dev_err(dev, "No 'states' property found\n");
		return ERR_PTR(-EINVAL);
	}

	proplen = prop->length / sizeof(int);

	config->states = devm_kzalloc(dev,
				sizeof(struct sdpmicom_regulator_state)
				* (proplen / 2),
				GFP_KERNEL);
	if (!config->states)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < proplen / 2; i++) {
		config->states[i].volt =
			be32_to_cpup((int *)prop->value + (i * 2));
		config->states[i].state =
			be32_to_cpup((int *)prop->value + (i * 2 + 1));
	}
	config->nr_states = i;
	
	return config;
}

static int sdpmicom_regulator_probe(struct platform_device *pdev)
{
	struct sdpmicom_regulator_config *config = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	struct sdpmicom_regulator_data *drvdata;
	struct regulator_config cfg = { };
	int ret, i;

	if (np) {
		config = of_get_regulator_configs(&pdev->dev, np);
		if (IS_ERR(config))
			return PTR_ERR(config);
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct sdpmicom_regulator_data),
			       GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		return -ENOMEM;
	}

	drvdata->desc.name = kstrdup(config->supply_name, GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		ret = -ENOMEM;
		goto err;
	}

	drvdata->states = kmemdup(config->states,
				  config->nr_states *
					 sizeof(struct sdpmicom_regulator_state),
				  GFP_KERNEL);
	if (drvdata->states == NULL) {
		dev_err(&pdev->dev, "Failed to allocate state data\n");
		ret = -ENOMEM;
		goto err_name;
	}
	drvdata->nr_states = config->nr_states;
	
	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.ops = &sdpmicom_regulator_voltage_ops;
	drvdata->desc.n_voltages = config->nr_states;

	drvdata->nr_gpios = config->nr_gpios;

	drvdata->state = config->def_state;
	drvdata->cmd_addr = config->cmd_addr;

	cfg.dev = &pdev->dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = drvdata;
	cfg.of_node = np;

	drvdata->dev = regulator_register(&drvdata->desc, &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		goto err_memstate;
	}

	platform_set_drvdata(pdev, drvdata);
	
	/* set default state */
	ret = sdpmicom_set_state(drvdata->dev, drvdata->state);
	if (ret) {
		dev_err(&pdev->dev, "default state set fail (%d)\n", drvdata->state);
		regulator_unregister(drvdata->dev);
		goto err_memstate;
	}
	for (i = 0; i < drvdata->nr_states; i++) {
		if (drvdata->states[i].state == drvdata->state)
			dev_info(&pdev->dev, "default volt %uV\n", drvdata->states[i].volt);
	}
	
	mdelay(100);
	
	return 0;

err_memstate:
	kfree(drvdata->states);
err_name:
	kfree(drvdata->desc.name);
err:
	return ret;	
}

static int sdpmicom_regulator_remove(struct platform_device *pdev)
{
	struct sdpmicom_regulator_data *drvdata = platform_get_drvdata(pdev);

	regulator_unregister(drvdata->dev);

	kfree(drvdata->states);
	kfree(drvdata->desc.name);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id regulator_sdpmicom_of_match[] = {
	{ .compatible = "regulator-sdpmicom", },
	{},
};
#endif

static struct platform_driver sdpmicom_regulator_driver = {
	.probe		= sdpmicom_regulator_probe,
	.remove		= sdpmicom_regulator_remove,
	.driver		= {
		.name		= "sdpmicom-regulator",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(regulator_sdpmicom_of_match),
	},
};

static int __init sdpmicom_regulator_init(void)
{
	return platform_driver_register(&sdpmicom_regulator_driver);
}
late_initcall(sdpmicom_regulator_init);

static void __exit sdpmicom_regulator_exit(void)
{
	platform_driver_unregister(&sdpmicom_regulator_driver);
}
module_exit(sdpmicom_regulator_exit);

MODULE_AUTHOR("Seihee Chon <sh.chon@samsung.com>");
MODULE_DESCRIPTION("sdp micom gpio voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sdpmicom-regulator");
