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
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/i2c.h>
#include <mach/soc.h>
#include <linux/kthread.h>
#include <linux/platform_data/ehci-sdp.h>

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);
static u64 sdp_dma_mask = DMA_BIT_MASK(32);

struct sdp_ehci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	//struct clk* clk_link;
	u32 swreset_addr;
	u32 swreset_mask;
	u32 owner_flag;
	u32 cnt;
};

static void sdp_ehci_swreset(struct sdp_ehci_hcd *sdp_ehci, int reset)
{
	u32 addr = sdp_ehci->swreset_addr;
	u32 mask = sdp_ehci->swreset_mask;
	
	sdp_set_clockgating(addr, mask, reset ? 0 : mask);
	if (!reset)
		msleep(2);
}

#ifdef CONFIG_OF
static DEFINE_SPINLOCK(hsic_lock);
static int sdp_ehci_remove(struct platform_device *pdev);
static int sdp_ehci_probe(struct platform_device *pdev);
volatile static struct usb_hcd* driver= NULL;

static int sdp_handle_ctrl_error_thread(void *ptr)
{

	struct platform_device *pdev; // = to_platform_device(hcd->self.controller);
	
		if (driver)
		{
			pdev = to_platform_device(driver->self.controller);
			try_module_get(THIS_MODULE);

			sdp_ehci_remove( pdev);

			mdelay(800);
			sdp_ehci_probe(pdev);

			module_put(THIS_MODULE);

			driver=NULL;
			
		}
		return 0;
}

static int sdp_handle_ctrl_error(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	unsigned long flags;
	
	spin_lock_irqsave (&hsic_lock, flags);

	if (driver==NULL)
	{
		driver=hcd;
		spin_unlock_irqrestore(&hsic_lock, flags);
		kthread_run(sdp_handle_ctrl_error_thread, hcd, "sdp_handle_ctrl_error_thread");
	}else
	{	
		spin_unlock_irqrestore(&hsic_lock, flags);
		ehci_warn(ehci, "sdp1302 hsic reset already running.\n");
		return 0;
	}
	
	return 0;
}
static int sdp_ehci_hub_control (
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	if(soc_is_sdp1302())
	{

		struct usb_device *udev = hcd->self.root_hub;
		
		if( typeReq == ClearPortFeature && wValue == USB_PORT_FEAT_C_ENABLE
			&& (udev->bus->busnum == 1||udev->bus->busnum == 2)&& !strcmp(udev->devpath,"0"))	
		{
			sdp_handle_ctrl_error(hcd);
			return 0;
		}	
	}

	return ehci_hub_control(hcd,typeReq,wValue,wIndex,buf,wLength);
}

#endif
static const struct hc_driver sdp_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "SDP EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	.reset			= ehci_setup,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	.get_frame_number	= ehci_get_frame,

	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	.hub_status_data	= ehci_hub_status_data,
#ifdef CONFIG_OF
	.hub_control		= sdp_ehci_hub_control,
#else 
	.hub_control		= ehci_hub_control,
#endif
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,

	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

#ifdef CONFIG_OF
static void __iomem *gpr_base;
static void sdp_ehci_gpio_init(struct platform_device *pdev)
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

static struct sdp_ehci_gpr
{
	u32 reg;
	u32 offset;
	u32 value;	
}  sdp_ehci_gpr_r;
static struct sdp_pad_gpr
{
	u32 reg;
	u32 offset;
	u32 value;	
	u32 level;	
}  sdp_pad_gpr_r;

static int sdp_ehci_parse_dt(struct sdp_ehci_hcd *sdp_ehci)
{
	struct platform_device *pdev = to_platform_device(sdp_ehci->dev);
	struct device_node *np = pdev->dev.of_node;
	int tmp;
	u32 addr, val;

	if (!np)
		return -1;

	if(of_property_read_u32_array(np, "samsung,sw-reset", &sdp_ehci_gpr_r.reg,3)) {
		printk("can't get samsung,ehci-gpr_reg\n");
		return -1;
	}
	addr = sdp_ehci_gpr_r.reg + sdp_ehci_gpr_r.offset;
	val = sdp_ehci_gpr_r.value;
	
	/* remember swreset register */
	sdp_ehci->swreset_addr = addr;
	sdp_ehci->swreset_mask = val;
	sdp_ehci_swreset(sdp_ehci, 1);	

	if(of_property_read_u32_array(np, "samsung,ehci-gpr_reg", &sdp_ehci_gpr_r.reg,3))
	{
	//	printk("can't get samsung,ehci-gpr_reg\n");
	//	return -1;
	}else {
		gpr_base=ioremap(sdp_ehci_gpr_r.reg,0x1000);
		writel(sdp_ehci_gpr_r.value,(void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		iounmap(gpr_base);
		}

	if(of_property_read_u32_array(np, "samsung,ehci-gpr_reg1", &sdp_ehci_gpr_r.reg,3))
	{
	//	printk("can't get samsung,ehci-gpr_reg1\n");
	//	return -1;
	}else{
		gpr_base=ioremap(sdp_ehci_gpr_r.reg,0x1000);
		writel(sdp_ehci_gpr_r.value,(void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		iounmap(gpr_base);
	}
	if(of_property_read_u32_array(np, "samsung,ehci-link-set", &sdp_ehci_gpr_r.reg,3)==0)
	{
		gpr_base=ioremap(sdp_ehci_gpr_r.reg,0x1000);
		tmp=readl((void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		tmp=tmp|(int)(sdp_ehci_gpr_r.value);
		writel(tmp,(void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		iounmap(gpr_base);
	}
	if(of_property_read_u32_array(np, "samsung,ehci-24.576Mhz", &sdp_ehci_gpr_r.reg,3)==0)
	{	printk("samsung,ehci-24.576Mhz\n");
	
		gpr_base=ioremap(sdp_ehci_gpr_r.reg,0x1000);
		tmp=readl((void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		tmp=tmp|(int)(sdp_ehci_gpr_r.value);
		writel(tmp,(void *)((u32)gpr_base+sdp_ehci_gpr_r.offset));
		iounmap(gpr_base);
	}

	/* reset off */	
	sdp_ehci_swreset(sdp_ehci, 0);

	if(of_property_read_u32_array(np, "samsung,vbus-enable", &sdp_pad_gpr_r.reg,4)==0)
	{
		gpr_base=ioremap(sdp_pad_gpr_r.reg,0x1000);
		tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset));
		tmp=tmp|(int)(sdp_pad_gpr_r.value);
		writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset));

		tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
		tmp=tmp|(int)(sdp_pad_gpr_r.level);
		writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
		iounmap(gpr_base);
		
	}else{
		sdp_pad_gpr_r.reg=0;
		sdp_pad_gpr_r.offset=0;
		sdp_pad_gpr_r.value=0;
		sdp_pad_gpr_r.level=0;
	}

	printk("get samsung,ehci-gpr_reg..\n");
	return 0;
}
static int hisc_i2c_set(int ch)
{
	u8 buf[1];
	u8 i2cSubAddr[2];
	int ret;
	struct i2c_msg msg[2];

	struct i2c_adapter *a = i2c_get_adapter(ch); 
	struct i2c_client client = {
		.addr = ((0x5a&0xFF)>>1), //7bit smsc hsic slave address:0x5a
		.adapter = a,
	};

	i2cSubAddr[0]=0xaa;
	i2cSubAddr[1]=0x55;

	buf[0]=0x00;
	
	msg[0].addr = client.addr;
	msg[0].flags = 0;       /* write */
	msg[0].len = 2;         /* subaddr size */
	msg[0].buf = i2cSubAddr;

	msg[1].addr = client.addr;
	msg[1].flags = I2C_M_NOSTART;/* write */
	msg[1].len = 1;         /* data size */
	msg[1].buf = buf;


	ret = i2c_transfer(client.adapter, msg, 2); // 2 : msg index number
	if(ret<0)
		pr_err("[%s]hsic i2c ch:%d error.\n",__func__,ch);
	else
		pr_info("[%s]hsic i2c ch:%d set.\n",__func__,ch);
	
	return ret;		

}

static int sdp_hsic_parse_dt(struct device_node *np,struct usb_hcd *hcd)
{
	int tmp,i;
	int val[10]={0};

	if (!np)
		return -1;
	
	if(of_property_read_u32_array(np, "samsung,hsic-reset", &sdp_pad_gpr_r.reg,4)==0)
	{	
		if(soc_is_sdp1302())
		{
			int sel;
			sel=get_sdp_board_type();
			if(sel == SDP_BOARD_LFD && hcd->self.busnum == 2)
			{
				//no setting
				sdp_pad_gpr_r.reg=0;
				sdp_pad_gpr_r.offset=0;
				sdp_pad_gpr_r.value=0;
				sdp_pad_gpr_r.level=0;
			}else
			{	
				printk("samsung,hsic-reset..\n");
				
				gpr_base=ioremap(sdp_pad_gpr_r.reg,0x1000);
				tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
				tmp=tmp&(int)(~sdp_pad_gpr_r.level);
				writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
				udelay(10);
				tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset));
				tmp=tmp|(int)(sdp_pad_gpr_r.value);
				writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset));

				tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
				tmp=tmp|(int)(sdp_pad_gpr_r.level);
				writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));

				iounmap(gpr_base);
			}
		}else
		{
			printk("samsung,hsic-reset..\n");

			gpr_base=ioremap(sdp_pad_gpr_r.reg,0x1000);
			tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
			tmp=tmp&(int)(~sdp_pad_gpr_r.level);
			writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
			udelay(10);
			tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset));
			tmp=tmp|(int)(sdp_pad_gpr_r.value);
			writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset));

			tmp=readl((void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));
			tmp=tmp|(int)(sdp_pad_gpr_r.level);
			writel(tmp,(void *)((u32)gpr_base+sdp_pad_gpr_r.offset+0x04));

			iounmap(gpr_base);
		}
		
	}else {
		sdp_pad_gpr_r.reg=0;
		sdp_pad_gpr_r.offset=0;
		sdp_pad_gpr_r.value=0;
		sdp_pad_gpr_r.level=0;
	}	
	
	if(of_property_read_u32_array(np, "hsic-i2c-ch", val,2)==0)
	{  
		mdelay(10);
		for(i=0;i<2;i++)
		{
			hisc_i2c_set(val[i]);
			
		}

	}

	return 0;
}
#endif

static void sdp_ehci_phy_init(struct sdp_ehci_hcd *sdp_ehci)
{
#if defined(CONFIG_OF)
	sdp_ehci_parse_dt(sdp_ehci);
#else
	if (sdp_ehci->dev->platform_data) {
		struct ehci_sdp_platdata *pdata = sdp_ehci->dev->platform_data;
		sdp_ehci->swreset_addr = pdata->swreset_addr;
		sdp_ehci->swreset_mask = pdata->swreset_mask;
		sdp_ehci_swreset(sdp_ehci, 0);
	}
#endif
}

static int sdp_ehci_get_handshake_fail_cnt(void *data, u64 *val)
{
	struct sdp_ehci_hcd *sdp_ehci = data;
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	*val = (u64)ehci->handshake_fail_cnt;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_handshake_fails_fops, sdp_ehci_get_handshake_fail_cnt, NULL, "%lld\n");

static int sdp_ehci_get_tune_cerr(void *data, u64 *val)
{
	*val = (u64)EHCI_TUNE_CERR;
	return 0;
}

static int sdp_ehci_set_tune_cerr(void *data, u64 val)
{
	int v = (int)val;
	if (v < 0 || v > 3)
		return -EINVAL;
	EHCI_TUNE_CERR = v;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_tune_cerr_fops,
		sdp_ehci_get_tune_cerr,
		sdp_ehci_set_tune_cerr,
		"%lld\n");

static int sdp_ehci_probe(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
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

	sdp_ehci = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_ehci_hcd), GFP_KERNEL);

	if (!sdp_ehci)
		return -ENOMEM;

	sdp_ehci->dev = &pdev->dev;
	sdp_ehci_phy_init(sdp_ehci);	//phy init , sw reset release

	hcd = usb_create_hcd(&sdp_ehci_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	sdp_ehci->hcd = hcd;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap_nocache(&pdev->dev, res->start, hcd->rsrc_len);

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

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	
	/* workdaround of 2nd port-reset fail */
	ehci->has_synopsys_reset_bug = 1;

	err = usb_add_hcd(hcd,(u32)irq, IRQF_DISABLED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_io;
	}

	platform_set_drvdata(pdev, sdp_ehci);
#ifdef CONFIG_OF
	sdp_hsic_parse_dt(pdev->dev.of_node,hcd);
	sdp_ehci_gpio_init(pdev);
#endif

#if defined(DEBUG)
	if (ehci->debug_dir) {
		debugfs_create_file("hanshake_fails", S_IRUGO | S_IWUGO,
					ehci->debug_dir, sdp_ehci,
					&debug_handshake_fails_fops);
	}
	if (ehci->debug_dir) {
		debugfs_create_file("tune_cerr", S_IRUGO | S_IWUGO,
					ehci->debug_dir, sdp_ehci,
					&debug_tune_cerr_fops);
	}
#endif
	return 0;

fail_io:
	usb_put_hcd(hcd);
	return err;
}

static int sdp_ehci_remove(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;

	usb_remove_hcd(hcd);

	usb_put_hcd(hcd);

	return 0;
}

static void sdp_ehci_shutdown(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int sdp_ehci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int i;

	dev_dbg(&pdev->dev, "ehci device suspend\n");
	
	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);
	/***/
	if(soc_is_sdp1406fhd() || soc_is_sdp1406uhd())
	{
		for (i=0; i<HCS_N_PORTS(ehci->hcs_params); i++)
		{
			u32 v = ehci_readl(ehci, &ehci->regs->port_status[i]);
			v = v >> 13;
			if(v & 0x1)
			{	dev_dbg(&pdev->dev, "ower !!!!\n");
				sdp_ehci->owner_flag=1;
			}
		}
	}
	/***/
	ehci_halt(ehci);

	spin_lock_irq(&ehci->lock);

	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irq(&ehci->lock);

	return 0;

}

static int sdp_ehci_resume(struct platform_device *pdev)
{	
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 i;
	
	dev_err(&pdev->dev, "ehci device resume start\n");
#ifdef CONFIG_OF	
	if (SDP_BOARD_JACKPACK != get_sdp_board_type())
	{
		sdp_ehci_parse_dt(sdp_ehci);
	}
#endif
	if (time_before(jiffies, ehci->next_statechange))
			msleep(10);
	
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_lock_irq(&ehci->lock);

	for (i=0; i<HCS_N_PORTS(ehci->hcs_params); i++) {
		u32 v = ehci_readl(ehci, &ehci->regs->port_status[i]);
		ehci_writel(ehci, v | PORT_POWER , &ehci->regs->port_status[i]);
	}
	udelay(20);

	ehci_writel(ehci, INTR_MASK, &ehci->regs->intr_enable);
/***/
	if(soc_is_sdp1406fhd() || soc_is_sdp1406uhd())
	{
		if(sdp_ehci->owner_flag!=1)
		{
			dev_dbg(&pdev->dev, "no owner !!!!\n");

			ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
		}else ehci_writel(ehci, 0, &ehci->regs->configured_flag);
	}else ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
/***/
	udelay(20);
	spin_unlock_irq(&ehci->lock);

#ifdef CONFIG_OF
	sdp_hsic_parse_dt(pdev->dev.of_node,hcd);
#endif

	dev_err(&pdev->dev, "ehci device resume end\n");
	return 0;

}

#endif
static const struct of_device_id sdp_ehci_match[] = {
	{ .compatible = "samsung,sdp-ehci" },
	{},
};

MODULE_DEVICE_TABLE(of, sdp_ehci_match);

static struct platform_driver sdp_ehci_driver = {
	.probe		= sdp_ehci_probe,
	.remove		= sdp_ehci_remove,
	.shutdown	= sdp_ehci_shutdown,
	.driver = {
		.name	= "sdp-ehci",
		.owner	= THIS_MODULE,
		.bus	= &platform_bus_type,
		.of_match_table = of_match_ptr(sdp_ehci_match),
	},
	#ifdef CONFIG_PM
	.suspend		= sdp_ehci_suspend,
	.resume		= sdp_ehci_resume,
	#endif
};

MODULE_ALIAS("platform:sdp-ehci");
