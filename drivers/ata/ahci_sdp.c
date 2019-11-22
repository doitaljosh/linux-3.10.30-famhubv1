/*
 * AHCI SATA SDP driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 * Copyright 2013  Samsung electronics
 *   Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_sdp.h>
#include <linux/of.h>
#include <mach/map.h>
#include "ahci.h"

static void ahci_host_stop(struct ata_host *host);

enum ahci_type {
	AHCI,		/* standard platform ahci */
};

static struct platform_device_id ahci_devtype[] = {
	{
		.name = "ahci",
		.driver_data = AHCI,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, ahci_devtype);

static struct ata_port_operations ahci_sdp_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_host_stop,
};

static const struct ata_port_info ahci_port_info[] = {
	/* by features */
	[AHCI] = {
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_sdp_ops,
	},
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("ahci_sdp"),
};

static int ahci_prep_init(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;

	u32 val;
	
	/* OOBR: COMWAKE/COMINIT min/max value */
	writel((1 << 31), mmio + 0xbc);	/* write enable */
	val = (1 << 31) | ((6 << 24) | (14 << 16) | (24 << 8) | (43 << 0));
	writel(val, mmio + 0xbc);

	return 0;
}

static int of_init_platform_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *phy_node;
	struct ahci_sdp_data pdata;
	struct sdp_sata_phy *phy;

	if (!np) {
		dev_warn(&pdev->dev, "This device does not have a device node.\n");
		return -EINVAL;
	}

	memset(&pdata, 0, sizeof(pdata));

#if defined(CONFIG_OF)
	/* get phy device */
	phy_node = of_parse_phandle(np, "sata_phy", 0);
	if (phy_node) {
		phy = devm_sdp_sata_phy_get_by_node(&pdev->dev, phy_node);
		if (!IS_ERR(phy)) {
			pdata.phy = phy;
			dev_info(&pdev->dev, "got phy device %s.\n", dev_name(phy->dev));
		} else
			dev_warn(&pdev->dev, "phy device is not existed.\n");
		of_node_put(phy_node);
	} else {
		dev_warn(&pdev->dev, "no phy device specified.\n");
	}
#endif
	pdata.init = ahci_prep_init;
	pdata.resume = ahci_prep_init;

	platform_device_add_data(pdev, &pdata, sizeof(pdata));

	return 0;
}

static int ahci_sdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_sdp_data *pdata;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct ata_port_info pi = ahci_port_info[id ? id->driver_data : 0];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	struct resource *mem;
	int irq;
	int n_ports;
	int i;
	int rc;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "no mmio space\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -EINVAL;
	}

	/* platform data */
	of_init_platform_data(pdev);
	pdata = dev_get_platdata(dev);

	if (pdata && pdata->ata_port_info)
		pi = *pdata->ata_port_info;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		return -ENOMEM;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	hpriv->mmio = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!hpriv->mmio) {
		dev_err(dev, "can't map %pR\n", mem);
		return -ENOMEM;
	}

	hpriv->clk = devm_clk_get(dev, NULL);
	if (!IS_ERR(hpriv->clk)) {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			goto err0;
		}
		udelay(50);
	}

	/* init phy: do this after ahci is clock enabled. */
	sdp_sata_phy_init(pdata->phy);

	/* XXX: assume this device is fully accessed by full system address space */
	dma_set_coherent_mask(dev, DMA_BIT_MASK(sizeof(dma_addr_t) * 8));
	dev->dma_mask = &dev->coherent_dma_mask;

	ahci_save_initial_config(dev, hpriv,
		pdata ? pdata->force_port_map : 0,
		pdata ? pdata->mask_port_map  : 0);

	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	ahci_set_em_messages(hpriv, &pi);

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));

	host = ata_host_alloc_pinfo(dev, ppi, n_ports);
	if (!host) {
		rc = -ENOMEM;
		goto err1;
	}

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		printk(KERN_INFO "ahci: SSS flag set, parallel bus scan disabled\n");

	/*
	 * Some platforms might need to prepare for mmio region access,
	 * which could be done in the following init call. So, the mmio
	 * region shouldn't be accessed before init (if provided) has
	 * returned successfully.
	 */
	if (pdata && pdata->init) {
		rc = pdata->init(dev);
		if (rc)
			goto err1;
	}

	if (pi.flags & ATA_FLAG_EM)
		ahci_reset_em(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_port_desc(ap, "mmio %pR", mem);
		ata_port_desc(ap, "port 0x%x", 0x100 + ap->port_no * 0x80);

		/* set enclosure management message type */
		if (ap->flags & ATA_FLAG_EM)
			ap->em_message_type = hpriv->em_msg_type;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}

	rc = ahci_reset_controller(host);
	if (rc)
		goto err2;

	ahci_init_controller(host);
	ahci_print_info(host, "sdp");

	rc = ata_host_activate(host, irq, ahci_interrupt, IRQF_SHARED,
			       &ahci_platform_sht);
	if (!rc)
		return 0;
err2:
	if (pdata && pdata->exit)
		pdata->exit(dev);
err1:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);
err0:
	return rc;
}

static int ahci_sdp_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct ahci_sdp_data *pdata = dev_get_platdata(&pdev->dev);
	
	ata_platform_remove_one(pdev);

	sdp_sata_phy_exit(pdata->phy);

	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);
	return 0;
}

static void ahci_host_stop(struct ata_host *host)
{
	struct device *dev = host->dev;
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);

	if (pdata && pdata->exit)
		pdata->exit(dev);
}

#ifdef CONFIG_PM_SLEEP
static int ahci_sdp_suspend(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 ctl;
	int rc;

	if (hpriv->flags & AHCI_HFLAG_NO_SUSPEND) {
		dev_err(dev, "firmware update required for suspend/resume\n");
		return -EIO;
	}

	/*
	 * AHCI spec rev1.1 section 8.3.3:
	 * Software must disable interrupts prior to requesting a
	 * transition of the HBA to D3 state.
	 */
	ctl = readl(mmio + HOST_CTL);
	ctl &= ~HOST_IRQ_EN;
	writel(ctl, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */

	rc = ata_host_suspend(host, PMSG_SUSPEND);
	if (rc)
		return rc;

	if (pdata && pdata->suspend)
		return pdata->suspend(dev);

	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return 0;
}

static int ahci_sdp_resume(struct device *dev)
{
	struct ahci_sdp_data *pdata = dev_get_platdata(dev);
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	if (!IS_ERR(hpriv->clk)) {
		rc = clk_prepare_enable(hpriv->clk);
		if (rc) {
			dev_err(dev, "clock prepare enable failed");
			return rc;
		}
	}

	sdp_sata_phy_init(pdata->phy);
	
	if (pdata && pdata->resume) {
		rc = pdata->resume(dev);
		if (rc)
			goto disable_unprepare_clk;
	}

	if (dev->power.power_state.event == PM_EVENT_SUSPEND) {
		rc = ahci_reset_controller(host);
		if (rc)
			goto disable_unprepare_clk;

		ahci_init_controller(host);
	}

	ata_host_resume(host);

	return 0;

disable_unprepare_clk:
	if (!IS_ERR(hpriv->clk))
		clk_disable_unprepare(hpriv->clk);

	return rc;
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_pm_ops, ahci_sdp_suspend, ahci_sdp_resume);

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = "samsung,sdp-ahci", },
	{},
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver ahci_driver = {
	.probe = ahci_sdp_probe,
	.remove = ahci_sdp_remove,
	.driver = {
		.name = "ahci",
		.owner = THIS_MODULE,
		.of_match_table = ahci_of_match,
		.pm = &ahci_pm_ops,
	},
	.id_table	= ahci_devtype,
};
module_platform_driver(ahci_driver);

MODULE_DESCRIPTION("AHCI SATA SDP driver");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("sdp:ahci");
