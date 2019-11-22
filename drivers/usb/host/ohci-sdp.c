/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

struct sdp_ohci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
};

static int sdp_ohci_reset(struct usb_hcd *hcd)
{
	return ohci_init(hcd_to_ohci(hcd));
}

static int sdp_ohci_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ret = ohci_run(ohci);

	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start %s\n",
			hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver sdp_ohci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "SDP OHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ohci_hcd),

	.irq			= ohci_irq,
	.flags			= HCD_MEMORY | HCD_USB11,

	.reset			= sdp_ohci_reset,
	.start			= sdp_ohci_start,
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,

	.get_frame_number	= ohci_get_frame,

	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
	#ifdef CONFIG_PM
	.bus_suspend		= ohci_bus_suspend,
	.bus_resume		= ohci_bus_resume,
	#endif
	.start_port_reset	= ohci_start_port_reset,
	#ifdef SAMSUNG_PATCH_OHCI_HANG_RECOVERY_DURING_KILL_URB
	.unlink_pending_urb	= ohci_unlink_pending_urb,
	#endif
};

static void sdp_ohci_gpio_init(struct platform_device *pdev)
{
	int err;
	int gpio;

	if (!pdev->dev.of_node)
		return;

	gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
	if (!gpio_is_valid(gpio))
		return;

	err = gpio_request((u32)gpio, "usb_enable");
	if (err) {
		if (err != -EBUSY)
			dev_err(&pdev->dev, "Can't request gpio: %d\n", err);
		return;
	}

	gpio_direction_output((u32)gpio, 0);
	gpio_set_value((u32)gpio, 1);
}

static u64 sdp_dma_mask = DMA_BIT_MASK(32);

static int sdp_ohci_probe(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci;
	struct usb_hcd *hcd;
	struct ohci_hcd *ohci;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
#if defined(CONFIG_ARCH_SDP)
		pdev->dev.dma_mask = &sdp_dma_mask;
#if defined(CONFIG_ARM_LPAE)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else	
	if (!pdev->dev.coherent_dma_mask)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	
#endif

	sdp_ohci = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_ohci_hcd), GFP_KERNEL);

	if (!sdp_ohci)
		return -ENOMEM;

	sdp_ohci->dev = &pdev->dev;

	hcd = usb_create_hcd(&sdp_ohci_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	sdp_ohci->hcd = hcd;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, res->start, (unsigned long)hcd->rsrc_len);

	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	err = usb_add_hcd(hcd,(u32)irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_io;
	}

	platform_set_drvdata(pdev, sdp_ohci);
#ifdef CONFIG_OF
	sdp_ohci_gpio_init(pdev);
#endif

	return 0;

fail_io:
	usb_put_hcd(hcd);
	return err;
}

static int sdp_ohci_remove(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ohci->hcd;

	usb_remove_hcd(hcd);

	usb_put_hcd(hcd);

	return 0;
}

static void sdp_ohci_shutdown(struct platform_device *pdev)
{
	struct sdp_ohci_hcd *sdp_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ohci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct of_device_id sdp_ohci_match[] = {
	{ .compatible = "samsung,sdp-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_ohci_match);

static struct platform_driver sdp_ohci_driver = {
	.probe		= sdp_ohci_probe,
	.remove		= sdp_ohci_remove,
	.shutdown	= sdp_ohci_shutdown,
	.driver = {
		.name	= "sdp-ohci",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_ohci_match),
	}
};

MODULE_ALIAS("platform:sdp-ohci");
