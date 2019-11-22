/**
 * dwc3-sdp.c - Samsung SDP DWC3 Specific Glue layer, cloned from dwc3-exynos.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Ikjoon Jang <ij.jang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>
#include <linux/of.h>
#include <linux/of_platform.h>

struct dwc3_sdp {
	struct device		*dev;
	struct clk		*clk;
};

static int dwc3_sdp_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	of_device_unregister(pdev);

	return 0;
}
static int dwc3_sdp_probe(struct platform_device *pdev)
{
	struct dwc3_sdp		*sdp;
	struct clk		*clk;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	int			ret = -ENOMEM;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	sdp = devm_kzalloc(dev, sizeof(*sdp), GFP_KERNEL);
	if (!sdp) {
		dev_err(&pdev->dev, "not enough memory\n");
		goto err;
	}
	platform_set_drvdata(pdev, sdp);

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
#if defined(CONFIG_ARM_LPAE)
# define SDP_DWC3_DMA_MASK	DMA_BIT_MASK(64)
#else
# define SDP_DWC3_DMA_MASK	DMA_BIT_MASK(32)
#endif
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	dev->coherent_dma_mask = SDP_DWC3_DMA_MASK;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "couldn't get clock. just not use it.\n");
		sdp->clk = NULL;
	} else {
		sdp->clk = clk;
		clk_prepare_enable(sdp->clk);
	}
	
	sdp->dev = dev;

	/* add core platform device */
	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to add dwc3 core\n");
		goto err1;
	}

	return 0;
err1:
	if (sdp->clk)
		clk_disable_unprepare(sdp->clk);
err:
	return ret;
}

static int dwc3_sdp_remove(struct platform_device *pdev)
{
	struct dwc3_sdp	*sdp = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, dwc3_sdp_remove_child);
	
	if (sdp->clk)
		clk_disable_unprepare(sdp->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_sdp_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dwc3_sdp	*sdp = platform_get_drvdata(pdev);

	dev_info(dev, "suspend");

	if (sdp->clk)
		clk_disable_unprepare(sdp->clk);

	return 0;
}

static int dwc3_sdp_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dwc3_sdp	*sdp = platform_get_drvdata(pdev);

	dev_info(dev, "resume");

	if (sdp->clk)
		clk_prepare_enable(sdp->clk);
	
	return 0;
}

static const struct dev_pm_ops dwc3_sdp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_sdp_suspend, dwc3_sdp_resume)
};
#define DWC3_SDP_PM_OPS	&(dwc3_sdp_pm_ops)
#else	/* !CONFIG_PM_SLEEP */
#define DWC3_SDP_PM_OPS	NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id sdp_dwc3_match[] = {
	{ .compatible = "samsung,sdp-dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_dwc3_match);
#endif

static struct platform_driver dwc3_sdp_driver = {
	.probe		= dwc3_sdp_probe,
	.remove		= dwc3_sdp_remove,
	.driver		= {
		.name	= "sdp-dwc3",
		.of_match_table = of_match_ptr(sdp_dwc3_match),
		.pm	= DWC3_SDP_PM_OPS,
	},
};

module_platform_driver(dwc3_sdp_driver);

MODULE_ALIAS("platform:sdp-dwc3");
MODULE_AUTHOR("Ikjoon Jang<ij.jang@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 SDP Glue Layer");

