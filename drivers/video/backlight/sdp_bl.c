/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/mfd/sdp_micom.h>

/* FIXME: do you want to move these in sdp_micom.h? */
#define SDP_MICOM_GPIO_HIGH		1
#define SDP_MICOM_GPIO_LOW		0

static int sdp_bl_update_status(struct backlight_device *bd)
{
	char data = SDP_MICOM_GPIO_HIGH;

	if (bd->props.power != FB_BLANK_UNBLANK)
		data = SDP_MICOM_GPIO_LOW;
	if (bd->props.state & BL_CORE_FBBLANK)
		data = SDP_MICOM_GPIO_LOW;
	if (bd->props.state & BL_CORE_SUSPENDED)
		data = SDP_MICOM_GPIO_LOW;

	/* panel power: control micom gpio - SW_PVCC */
	sdp_micom_send_cmd(SDP_MICOM_CMD_SW_PVCC, &data, sizeof(data));

	/* backlight power: control micom gpio - SW_INVERTER */
	sdp_micom_send_cmd(SDP_MICOM_CMD_SW_INVERTER, &data, sizeof(data));

	return 0;
}

static const struct backlight_ops sdp_bl_ops = {
	.update_status	= sdp_bl_update_status,
};

static int sdp_bl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct backlight_properties props;
	struct backlight_device *bd;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	bd = backlight_device_register(dev_name(dev), dev, NULL, &sdp_bl_ops,
				       &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	backlight_update_status(bd);

	return 0;
}

static int sdp_bl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	bd->props.power = FB_BLANK_POWERDOWN;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	return 0;
}

static const struct of_device_id sdp_bl_dt_match[] = {
	{ .compatible = "samsung,sdp1202-bl", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_bl_dt_match);

static struct platform_driver sdp_bl_driver = {
	.probe		= sdp_bl_probe,
	.remove		= sdp_bl_remove,
	.driver = {
		.name	= "sdp-bl",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_bl_dt_match),
	},
};

module_platform_driver(sdp_bl_driver);

MODULE_DESCRIPTION("Samsung SDP SoCs backlight driver");
MODULE_LICENSE("GPL v2");
