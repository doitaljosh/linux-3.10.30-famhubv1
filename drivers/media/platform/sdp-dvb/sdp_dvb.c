/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "dvbdev.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct sdp_dvb {
	struct platform_device *pdev;
	struct dvb_adapter adapter;
};

static struct sdp_dvb *_sdp_dvb;

struct dvb_adapter *sdp_dvb_get_adapter(void)
{
	if (!_sdp_dvb)
		return NULL;

	return &_sdp_dvb->adapter;
}
EXPORT_SYMBOL(sdp_dvb_get_adapter);

static int sdp_dvb_probe(struct platform_device *pdev)
{
	struct sdp_dvb *dvb;
	int r;

	dvb = devm_kzalloc(&pdev->dev, sizeof(struct sdp_dvb), GFP_KERNEL);
	if (!dvb)
		return -ENOMEM;

	r = dvb_register_adapter(&dvb->adapter, "sdp-dvb", THIS_MODULE,
			&pdev->dev, adapter_nr);
	if (r < 0)
		return r;

	dvb->pdev = pdev;

	platform_set_drvdata(pdev, dvb);
	_sdp_dvb = dvb;

	return 0;
}

static int sdp_dvb_remove(struct platform_device *pdev)
{
	struct sdp_dvb *dvb = platform_get_drvdata(pdev);

	_sdp_dvb = NULL;

	dvb_unregister_adapter(&dvb->adapter);

	return 0;
}

static const struct of_device_id sdp_dvb_dt_match[] = {
	{ .compatible = "samsung,sdp-dvb" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_dvb_dt_match);

static struct platform_driver sdp_dvb_driver = {
	.probe          = sdp_dvb_probe,
	.remove         = sdp_dvb_remove,
	.driver         = {
		.name   = "sdp-dvb",
		.owner  = THIS_MODULE,
		.of_match_table = sdp_dvb_dt_match,
	},
};

static int __init sdp_dvb_init(void)
{
	return platform_driver_register(&sdp_dvb_driver);
}
subsys_initcall(sdp_dvb_init);

static void __exit sdp_dvb_exit(void)
{
	platform_driver_unregister(&sdp_dvb_driver);
}
module_exit(sdp_dvb_exit);

MODULE_DESCRIPTION("Samsung SDP DVB adapter driver");
MODULE_LICENSE("GPL");

