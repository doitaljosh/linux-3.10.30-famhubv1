/*
 * xhci-plat.c - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * A lot of code borrowed from the Linux xHCI driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <mach/soc.h>
#include "xhci.h"

static ssize_t xhci_portsc_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{	
	struct xhci_hcd 	*xhci2;
	struct xhci_hcd 	*xhci3;
	struct usb_hcd		*hcd2;
	struct usb_hcd		*hcd3;
	unsigned int regval=0;
	

	hcd2 = bus_to_hcd(dev_get_drvdata(dev->parent));
	hcd3 = hcd2->shared_hcd;
	xhci2 = hcd_to_xhci(hcd2);
	xhci3 = hcd_to_xhci(hcd3);
		
	if (dev == &hcd3->self.root_hub->dev) { 		
		regval = xhci_readl(xhci3, *xhci3->usb3_ports); 	
	}
	
	if (dev == &hcd2->self.root_hub->dev) {
		regval = xhci_readl(xhci2, *xhci2->usb2_ports); 	
	}

	return snprintf(buf, PAGE_SIZE, "0x%x", regval);			
	
}


static ssize_t xhci_portsc_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	char recv_buffer[32];
	long value;
	struct xhci_hcd 	*xhci2;
	struct xhci_hcd 	*xhci3;
	struct usb_hcd		*hcd2;
	struct usb_hcd		*hcd3;
	
	hcd2 = bus_to_hcd(dev_get_drvdata(dev->parent));
	hcd3 = hcd2->shared_hcd;
	xhci2 = hcd_to_xhci(hcd2);
	xhci3 = hcd_to_xhci(hcd3);

	snprintf(recv_buffer, sizeof(recv_buffer), "%s", buf);

	
	if (dev == &hcd3->self.root_hub->dev) { 			
		if (!kstrtol(recv_buffer, 0, &value)) {
			xhci_writel(xhci3, (unsigned int)value, *xhci3->usb3_ports);
		} else {
			pr_err("conversion failed..\n");
			return -1;
		}
	}
	
	if (dev == &hcd2->self.root_hub->dev) {
		if (!kstrtol(recv_buffer, 0, &value)) {
			xhci_writel(xhci2, (unsigned int)value, *xhci2->usb2_ports);
		} else {
			pr_err("conversion failed..\n");
			return -1;
		}
	}
	
	return (ssize_t)count;
}




static DEVICE_ATTR(portsc, 0644, xhci_portsc_show, xhci_portsc_store);

static void xhci_plat_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_PLAT;
}

/* called during probe() after chip reset completes */
static int xhci_plat_setup(struct usb_hcd *hcd)
{
#ifdef CONFIG_ARCH_SDP1202
#define XHCI_GRXTHRCFG_OFFSET   (0xc10c)

	/*RX FIFO threadshold*/
	/*
		[29]receive packet count enable
		[27:24]Receive packet count
		[23:19]Max Receive Burst Szie 		
	*/
	int ret = 0;
	int temp;
	
	ret = xhci_gen_setup(hcd, xhci_plat_quirks);

	if (hcd->regs && usb_hcd_is_primary_hcd(hcd)) {
		temp = 1 << 29 | 3 << 24 | 3 << 19;
		writel(temp, (hcd->regs + XHCI_GRXTHRCFG_OFFSET));
		pr_info("xhci(Fox-AP) : Apply RxFifo Threshold control\n");
	}

	return ret;
#else
	return xhci_gen_setup(hcd, xhci_plat_quirks);
#endif

	
}

static const struct hc_driver xhci_plat_xhci_driver = {
	.description =		"xhci-hcd",
	.product_desc =		"xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		xhci_plat_setup,
	.start =		xhci_run,
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/* Root hub support */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,
};

#if defined(CONFIG_ARCH_SDP)
static u64 sdp_dma_mask = DMA_BIT_MASK(32);
#endif

static int xhci_plat_probe(struct platform_device *pdev)
{
	const struct hc_driver	*driver;
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd;
	int			ret;
	int			irq;

#if defined(CONFIG_ARCH_SDP)
	pdev->dev.dma_mask = &sdp_dma_mask;
#if defined(CONFIG_ARM_LPAE)
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else
	if (!pdev->dev.coherent_dma_mask)
	     pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	
#endif
	if (usb_disabled())
		return -ENODEV;

	driver = &xhci_plat_xhci_driver;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto put_hcd;
	}

#ifdef CONFIG_ARCH_SDP1202
	if (sdp_get_revision_id()) {
		hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
		if (!hcd->regs) {
			dev_dbg(&pdev->dev, "error mapping memory\n");
			ret = -EFAULT;
			goto release_mem_region;
		}
	}else {
		hcd->regs = ioremap_wc(hcd->rsrc_start, hcd->rsrc_len);
		if (!hcd->regs) {
			dev_dbg(&pdev->dev, "error mapping memory\n");
			ret = -EFAULT;
			goto release_mem_region;
		}
	}
#else
	hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto release_mem_region;
	}
#endif	

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto unmap_registers;

	/* USB 2.0 roothub is stored in the platform_device now. */
	hcd = dev_get_drvdata(&pdev->dev);
	if (hcd == NULL) {	
		ret = -ENODEV;
		goto unmap_registers;
	}

	xhci = hcd_to_xhci(hcd);	

	xhci->shared_hcd = usb_create_shared_hcd(driver, &pdev->dev,
			dev_name(&pdev->dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}
	
	/*
	 * Set the xHCI pointer before xhci_plat_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (ret)
		goto put_usb3_hcd;

	if (device_create_file(&hcd->self.root_hub->dev, &dev_attr_portsc)) {
		pr_err("Error device_create_file\n");
	}
		
	if (device_create_file(&hcd->shared_hcd->self.root_hub->dev, &dev_attr_portsc)) {
		pr_err("Error device_create_file\n");
	}

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

unmap_registers:
	iounmap(hcd->regs);

release_mem_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int xhci_plat_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd;
	struct xhci_hcd	*xhci;

	hcd = platform_get_drvdata(dev);
	if (hcd == NULL)	
		return -ENODEV;

	xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	kfree(xhci);

	return 0;
}

#if defined(CONFIG_PM)
static int xhci_plat_suspend(struct platform_device *dev, pm_message_t state)
{
	struct usb_hcd	*hcd;
	struct xhci_hcd	*xhci;

	hcd = platform_get_drvdata(dev);
	if (hcd == NULL)	
		return -ENODEV;

	xhci = hcd_to_xhci(hcd);

	return xhci_suspend(xhci);
}

static int xhci_plat_resume(struct platform_device *dev)
{
	struct usb_hcd	*hcd;
	struct xhci_hcd	*xhci;

#if defined(CONFIG_ARCH_SDP)
	struct usb_hcd		*secondary_hcd;
	int			retval = 0;
#endif

	hcd = platform_get_drvdata(dev);
	if (hcd == NULL)	
		return -ENODEV;

	xhci = hcd_to_xhci(hcd);

#if defined(CONFIG_ARCH_SDP)

	/* Wait a bit if either of the roothubs need to settle from the
	 * transition into bus suspend.
	 */
	if (time_before(jiffies, xhci->bus_state[0].next_statechange) ||
			time_before(jiffies,xhci->bus_state[1].next_statechange))
		msleep(100);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &xhci->shared_hcd->flags);

	
	xhci_dbg(xhci, "cleaning up memory\n");
	xhci_mem_cleanup(xhci);
	xhci_dbg(xhci, "xhci_stop completed - status = %x\n",
		    xhci_readl(xhci, &xhci->op_regs->status));

	/* USB core calls the PCI reinit and start functions twice:
	 * first with the primary HCD, and then with the secondary HCD.
	 * If we don't do the same, the host will never be started.
	 */
	if (!usb_hcd_is_primary_hcd(hcd))
		secondary_hcd = hcd;
	else
		secondary_hcd = xhci->shared_hcd;

	xhci_dbg(xhci, "Initialize the xhci_hcd\n");
	retval = xhci_init(hcd->primary_hcd);
	if (retval)
		return retval;
	xhci_dbg(xhci, "Start the primary HCD\n");
	retval = xhci_run(hcd->primary_hcd);
	if (!retval) {
		xhci_dbg(xhci, "Start the secondary HCD\n");
		retval = xhci_run(secondary_hcd);
	}
	hcd->state = HC_STATE_SUSPENDED;
	xhci->shared_hcd->state = HC_STATE_SUSPENDED;

	if (retval == 0) {
		usb_hcd_resume_root_hub(hcd);
		usb_hcd_resume_root_hub(xhci->shared_hcd);
	}

	/* Re-enable port polling. */
	xhci_dbg(xhci, "%s: starting port polling.\n", __func__);
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);
	
	return retval;	

#else
	return = xhci_resume(xhci, false);	
#endif

}
#endif

#if defined(CONFIG_OF)
static const struct of_device_id plat_xhci_match[] = {
    {.compatible = "samsung,xhci-hcd"},
    {},
};
#endif

static struct platform_driver usb_xhci_driver  = {
	.probe	= xhci_plat_probe,
	.remove	= xhci_plat_remove,
#if defined(CONFIG_PM)	
	.suspend = xhci_plat_suspend,
	.resume = xhci_plat_resume,
#endif
	.driver	= {
		.name = "xhci-hcd",
#if defined(CONFIG_OF)
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,
		.of_match_table = of_match_ptr(plat_xhci_match),
#endif		
	},
};
MODULE_ALIAS("platform:xhci-hcd");

int xhci_register_plat(void)
{
	return platform_driver_register(&usb_xhci_driver);
}

void xhci_unregister_plat(void)
{
	platform_driver_unregister(&usb_xhci_driver);
}

