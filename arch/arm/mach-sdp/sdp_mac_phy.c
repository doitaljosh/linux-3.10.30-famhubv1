/*
 * arch/arm/plat-sdp/sdp_phy.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * modified by dongseok.lee <drain.lee@samsung.com>
 * 
 * 20120420 drain.lee Create file.
 * 20130815 chagne EEE print level
 * 20130912 add PHY suspend/resume code(for restore BMCR)
 * 20140801 add EPHY driver
 * 20140901 RTL8211E disable EEE.
 * 20140926 fix suspend/resume.
 * 20141128 add phy debugfs.
 * 20150107 support RTL8304E switch.
 * 20150108 add ephy reinit when link is down.
 * 20150112 fix W=123 compile warning.
 * 20150113 add RTL8304E switch config_init callback
 * 20150313 EPHY auto mdix workaround
 */

#define SDP_MAC_PHY_VER		"20150313(EPHY auto mdix workaround)"
 
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy_fixed.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

MODULE_AUTHOR("modified by Dongseok Lee");


struct sdp_mac_phy_priv {
	u16 bmcr;/* saved phy bmcr value */
	struct dentry *debugfs_root;

	unsigned long ephy_next_mdix_jiffies;
};

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl821x_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static void rtl82xx_EEE_Disable(struct phy_device *phydev)
{
	int phyVal;

	dev_printk(KERN_INFO, &phydev->dev, "RTL82xx EEE Disable!\n");

	mutex_lock(&phydev->lock);

	/* WOL Disable */
	phy_write(phydev, 31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal & ~(0x1<<10);
	phy_write(phydev,  19, (u16)phyVal);
	phy_write(phydev,  31, 0);

	/* LED Disable */
	phy_write(phydev,  31, 7);
	phyVal = phy_read(phydev,  19);
	phyVal = phyVal | (0x1<<3);
	phy_write(phydev,  19, (u16)phyVal);
	phy_write(phydev,  17, 0);
	phy_write(phydev,  31, 0);

	/* EEE Disable */
	phy_write(phydev,  31, 0x4);
	phy_write(phydev,  16, 0x4077);
	phy_write(phydev,  31, 0x0);
	phy_write(phydev,  13, 0x7);
	phy_write(phydev,  14, 0x3c);
	phy_write(phydev,  13, 0x4007);
	phy_write(phydev,  14, 0x0);

	/* restart auto neg */
	genphy_restart_aneg(phydev);

	mutex_unlock(&phydev->lock);
}

static int rtl8201f_config_init(struct phy_device *phydev)
{
	rtl82xx_EEE_Disable(phydev);
	return 0;
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	rtl82xx_EEE_Disable(phydev);
	return 0;
}


#define RTL8304E_PORT0	0x0
#define RTL8304E_PORT1	0x1
#define RTL8304E_MAC2	0x5
#define RTL8304E_MAC3	0x6
#define RTL8304E_MAC2_PHY	0x7

static int rtl8304e_config_init(struct phy_device *phydev)
{
	struct mii_bus *bus = phydev->bus;
	struct phy_device *mac2_phy = bus->phy_map[RTL8304E_MAC2_PHY];

	if(mac2_phy) {
		if(mac2_phy->drv && mac2_phy->drv->config_init)
			mac2_phy->drv->config_init(mac2_phy);
	}

	return 0;
}

static int rtl8304e_read_status(struct phy_device *phydev)
{
	struct mii_bus *bus = phydev->bus;
	struct phy_device *port0 = bus->phy_map[RTL8304E_PORT0];
	struct phy_device *port1 = bus->phy_map[RTL8304E_PORT1];
	struct phy_device *mac2 = bus->phy_map[RTL8304E_MAC2];
	struct phy_device *mac3 = bus->phy_map[RTL8304E_MAC3];
	struct phy_device *mac2_phy = bus->phy_map[RTL8304E_MAC2_PHY];
	int val = 0;
	int old_link = 0;
	int change_link = 0;

	old_link = port0->link;
	genphy_read_status(port0);
	if(old_link != port0->link) change_link = 1;

	old_link = port1->link;
	genphy_read_status(port1);
	if(old_link != port1->link) change_link = 1;

	genphy_read_status(mac2);

	if(mac2_phy) {
		old_link = mac2_phy->link;
		genphy_read_status(mac2_phy);
		if(old_link != mac2_phy->link) change_link = 1;

		if(mac2_phy->link) {
			mac2->autoneg = AUTONEG_DISABLE;
			mac2->speed = mac2_phy->speed;
			mac2->duplex = mac2_phy->duplex;
			genphy_config_aneg(mac2);
		}
	}

	val = phy_read(mac3, 22);
	if(val < 0) {
		dev_err(&phydev->dev, "%s: phy_read return error %d\n", __func__, val);
		return val;
	}

	/* set mac3 link status up or down */
	if(port0->link || port1->link || (mac2_phy && mac2_phy->link)) {
		phy_write(mac3, 22, (u16)(val|(0x1<<15)));
	} else {
		phy_write(mac3, 22, (u16)(val&~(0x1<<15)));
	}
	genphy_read_status(mac3);

	if(change_link) {
		char buf[512];
		int pos = 0;

		if(port0->link)
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port0 Link is Up - %dMbps/%s, ",
				port0->speed, DUPLEX_FULL == port0->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port0 Link is Down, ");


		if(port1->link)
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port1 Link is Up - %dMbps/%s",
				port1->speed, DUPLEX_FULL == port1->duplex ? "Full" : "Half");
		else
			pos += snprintf(buf+pos, (size_t)(512-pos), "Port1 Link is Down");

		if(mac2_phy) {
			if(mac2_phy->link)
				pos += snprintf(buf+pos, (size_t)(512-pos), ", MAC2 Phy Link is Up - %dMbps/%s",
					mac2_phy->speed, DUPLEX_FULL == mac2_phy->duplex ? "Full" : "Half");
			else
				pos += snprintf(buf+pos, (size_t)(512-pos), ", MAC2 Phy Link is Down");
		}

		dev_info(&phydev->dev, "%s\n", buf);
	}

	return 0;
}

#define EPHY_MDIO17_MDI_MODE		(0x1U<<6)
#define EPHY_MDIO17_AUTO_MDIX_EN	(0x1U<<7)
static int ephy_config_init(struct phy_device *phydev)
{
	dev_info(&phydev->dev, "ephy_config_init!\n");

	mutex_lock(&phydev->lock);

	/* DSP Reg enter init */
	phy_write(phydev, 20, 0x0000);
	phy_write(phydev, 20, 0x0400);
	phy_write(phydev, 20, 0x0000);
	phy_write(phydev, 20, 0x0400);

	phy_write(phydev, 23, 0x4400);
	phy_write(phydev, 20, 0x4415);

	phy_write(phydev, 23, 0x3400);
	phy_write(phydev, 20, 0x4413);

	phy_write(phydev, 23, 0x0007);
	phy_write(phydev, 20, 0x4418);

	/* ephy auto mdix disable! */
	phy_write(phydev, 17, (u16)((u16)phy_read(phydev, 17)&~(EPHY_MDIO17_AUTO_MDIX_EN | EPHY_MDIO17_MDI_MODE)));

	mutex_unlock(&phydev->lock);

	return 0;
}


static u32 ephy_get_random_ms(struct phy_device *phydev)
{
		u32 random_ms;
		const u32 min_ms = 2000, max_ms = 2500, setp_ms = 100;

		/* set random next mdix time */
		get_random_bytes(&random_ms, sizeof(random_ms));
		random_ms %= max_ms - min_ms;
		random_ms += min_ms;
		return (random_ms / setp_ms) * setp_ms;
}

static int ephy_auto_mdix_fixup(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	unsigned long now = jiffies;
	u16 phyVal;

	if(time_after_eq(now, priv->ephy_next_mdix_jiffies))
	{
		u32 random_ms;

		random_ms = ephy_get_random_ms(phydev);
		priv->ephy_next_mdix_jiffies = now + msecs_to_jiffies(random_ms);

		/* mdix switch! */
		phyVal = (u16)phy_read(phydev, 17);
		phyVal &= (u16)(~EPHY_MDIO17_AUTO_MDIX_EN);

		/* toggle mdi/mdix mode */
		phyVal ^= (u16)EPHY_MDIO17_MDI_MODE;

		phy_write(phydev, 17, phyVal);

		if(AUTONEG_ENABLE == phydev->autoneg) {
			genphy_restart_aneg(phydev);
		}

		dev_dbg(&phydev->dev, "now switch to %s mode, "
			"next switching after %dms\n",
			phyVal&EPHY_MDIO17_MDI_MODE?"MDIX":"MDI ", random_ms);

		return phyVal;
	}
	return 0;
}

static int ephy_read_status(struct phy_device *phydev)
{
	int old_link = phydev->link;
	int ret = genphy_read_status(phydev);
	struct sdp_mac_phy_priv *priv = phydev->priv;

	/* phy link is down ephy reinit! */
	if(old_link && !phydev->link) {
		//dev_info(&phydev->dev, "ephy_read_status applay init!\n");

		/* DSP Reg enter init */
		phy_write(phydev, 20, 0x0000);
		phy_write(phydev, 20, 0x0400);
		phy_write(phydev, 20, 0x0000);
		phy_write(phydev, 20, 0x0400);

		phy_write(phydev, 23, 0x4400);
		phy_write(phydev, 20, 0x4415);

		phy_write(phydev, 23, 0x3400);
		phy_write(phydev, 20, 0x4413);

		phy_write(phydev, 23, 0x0007);
		phy_write(phydev, 20, 0x4418);

		priv->ephy_next_mdix_jiffies = jiffies + msecs_to_jiffies(ephy_get_random_ms(phydev));
	} else if(!old_link && !phydev->link) {
		/* link is already down */
		ephy_auto_mdix_fixup(phydev);
	}

	return ret;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
static int
ephy_dbg_bist_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;

	*val = 0;
	dev_info(&phydev->dev, "Built-in self-test start.\n");

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(ephy_dbg_bist_fops, ephy_dbg_bist_get, NULL,
	"%lld\n");



#define SDP1406_EPHY_LDO_ADDR 0x00580500
static int
sdp1406_ephy_dbg_ldo_get(void *data, u64 *val)
{
	struct phy_device *phydev = data;
	void *addr;

	*val = 0;

	addr = ioremap_nocache(SDP1406_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	*val = readl(addr);
	iounmap(addr);

	return 0;
}

static int
sdp1406_ephy_dbg_ldo_set(void *data, u64 val)
{
	struct phy_device *phydev = data;
	void *addr;

	if(val > 0x1F) {
		dev_err(&phydev->dev, "%s: ldo value(0x%llx) is invalied!\n", __FUNCTION__, val);
		return -EINVAL;
	}

	addr = ioremap_nocache(SDP1406_EPHY_LDO_ADDR, sizeof(u32));
	if(!addr) {
		dev_err(&phydev->dev, "%s: ioremap return NULL!\n", __FUNCTION__);
		return -ENOMEM;
	}

	writel((readl(addr)&~(0x1FUL<<20)) | ((u32)val<<20) | (0x1<<19), addr);
	wmb();
	dev_info(&phydev->dev, "set ldo 0x%02llx(raw 0x%08x)\n", val, readl(addr));
	iounmap(addr);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp1406_ephy_dbg_ldo_fops, sdp1406_ephy_dbg_ldo_get, sdp1406_ephy_dbg_ldo_set,
	"0x%08llx\n");



/* create debugfs node! */
static void
sdp_phy_add_debugfs(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	struct dentry *root;

	root = debugfs_create_dir(dev_name(&phydev->dev), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	priv->debugfs_root = root;

	if((phydev->drv->phy_id&0xFFFFFFF0) == 0xABCD0000/* EPHY */) {
		if (!debugfs_create_file("bist", S_IRUSR, root, phydev, &ephy_dbg_bist_fops))
			goto err_node;

		if(of_machine_is_compatible("samsung,sdp1406") || of_machine_is_compatible("samsung,sdp1406fhd")) {
			if (!debugfs_create_file("ldo", S_IRUSR, root, phydev, &sdp1406_ephy_dbg_ldo_fops))
				goto err_node;
		}
	}

	return;

err_node:
	debugfs_remove_recursive(root);
	priv->debugfs_root = NULL;
err_root:
	dev_err(&phydev->dev, "failed to initialize debugfs\n");
}

static void
sdp_phy_remove_debugfs(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;

	debugfs_remove_recursive(priv->debugfs_root);
}
#endif



#ifdef CONFIG_PM_DEBUG
#define PM_DEV_DBG(arg...)	dev_printk(KERN_DEBUG, arg)
#else
#define PM_DEV_DBG(arg...)	dev_dbg(arg)
#endif

static int realtek_suspend(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;
	int value;

	PM_DEV_DBG(&phydev->dev, "realtek_suspend.\n");

	mutex_lock(&phydev->lock);

	value = phy_read(phydev, MII_BMCR);

	priv->bmcr = (u16)value;
	PM_DEV_DBG(&phydev->dev, "realtek_suspend: save bmcr 0x%04x\n", priv->bmcr);

	mutex_unlock(&phydev->lock);

	return 0;
}

static int realtek_resume(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv = phydev->priv;

	PM_DEV_DBG(&phydev->dev, "realtek_resume.\n");

	mutex_lock(&phydev->lock);

	PM_DEV_DBG(&phydev->dev, "realtek_resume: restore bmcr 0x%04x\n", priv->bmcr);
	phy_write(phydev, MII_BMCR, priv->bmcr);
	priv->bmcr = 0x0;

	mutex_unlock(&phydev->lock);

	PM_DEV_DBG(&phydev->dev, "realtek_resume: call phy_scan_fixups\n");
	phy_scan_fixups(phydev);

	if(phydev->drv->config_init) {
		PM_DEV_DBG(&phydev->dev, "realtek_resume: call config_init\n");
		phydev->drv->config_init(phydev);
	}

	return 0;
}

static int realtek_probe(struct phy_device *phydev)
{
	struct sdp_mac_phy_priv *priv;

	if((phydev->drv->phy_id_mask & 0xF) == 0) {
		dev_info(&phydev->dev, "%s%c probed.\n", phydev->drv->name, (phydev->phy_id&0xF)+'A'-1);
	} else {
		dev_info(&phydev->dev, "%s probed.\n", phydev->drv->name);
	}

	priv = kzalloc(sizeof(struct sdp_mac_phy_priv), GFP_KERNEL);
	if(!priv) {
		return -ENOMEM;
	}

	phydev->priv = priv;

#ifdef CONFIG_DEBUG_FS
	sdp_phy_add_debugfs(phydev);
#endif
	return 0;
}

static void realtek_remove(struct phy_device *phydev)
{
	if((phydev->drv->phy_id_mask & 0xF) == 0) {
		dev_info(&phydev->dev, "%s%c removed.\n", phydev->drv->name, (phydev->phy_id&0xF)+'A'-1);
	} else {
		dev_info(&phydev->dev, "%s removed.\n", phydev->drv->name);
	}

#ifdef CONFIG_DEBUG_FS
	sdp_phy_remove_debugfs(phydev);
#endif

	kfree(phydev->priv);
	phydev->priv = NULL;
}


/* supported phy list */
static struct phy_driver sdp_phy_drivers[] = {
	{/* RTL8201E */
		.phy_id		= 0x001CC815,
		.name		= "RTL8201E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8201F */
		.phy_id		= 0x001CC816,
		.name		= "RTL8201F",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= rtl8201f_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8201x this is RTL8201x common phy driver. */
		.phy_id		= 0x001CC810,
		.name		= "RTL8201",
		.phy_id_mask	= 0x001ffff0,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},


	/* for RGMII Phy */
	{/* RTL8211E */
		.phy_id		= 0x001cc915,
		.name		= "RTL8211E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= rtl8211e_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},
	{/* RTL8211x this is RTL8211x common phy driver. */
		.phy_id		= 0x001cc910,
		.name		= "RTL8211",
		.phy_id_mask	= 0x001ffff0,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= NULL,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},

	{/* RTL8304E this is RTL8304E swtich driver. phy addr 0x0~0x6 is used */
		.phy_id		= 0x001CC852,
		.name		= "RTL8304E",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= rtl8304e_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= rtl8304e_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl821x_config_intr,
		.driver		= { .owner = THIS_MODULE,},
	},



	{/* EPHY test.. */
		.phy_id		= 0xABCD0000,
		.name		= "SDP-EPHY",
		.phy_id_mask	= 0xfffffff0,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.probe			= realtek_probe,
		.remove			= realtek_remove,
		.suspend		= realtek_suspend,
		.resume			= realtek_resume,
		.config_init	= ephy_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= ephy_read_status,
		.driver		= { .owner = THIS_MODULE,},
	},
};

static int __init sdp_phy_init(void)
{
	int i;
	int ret = 0;

	for(i = 0; i < (int)ARRAY_SIZE(sdp_phy_drivers); i++)
	{
		ret = phy_driver_register(&sdp_phy_drivers[i]);
		if(ret < 0) {
			pr_err("phy driver register failed(index:%d name:%s).\n", i, sdp_phy_drivers[i].name);
			for(i = i-1; i >= 0; i--) {
				phy_driver_unregister(&sdp_phy_drivers[i]);
			}
			return ret;
		}
	}

	pr_info("Registered sdp-phy drivers.ver %s\n", SDP_MAC_PHY_VER);

	return ret;
}

static void __exit sdp_phy_exit(void)
{
	int i;
	for(i = 0; i < (int)ARRAY_SIZE(sdp_phy_drivers); i++) {
		phy_driver_unregister(&sdp_phy_drivers[i]);
	}
}

module_init(sdp_phy_init);
module_exit(sdp_phy_exit);

static struct mdio_device_id __maybe_unused sdp_phy_tbl[] = {
	{ 0x001CC815, 0x001fffff },
	{ 0x001CC816, 0x001fffff },
	{ 0x001CC810, 0x001ffff0 },
	{ 0x001cc915, 0x001fffff },
	{ 0x001cc910, 0x001ffff0 },
	{ 0 }
};

MODULE_DEVICE_TABLE(mdio, sdp_phy_tbl);

#ifdef CONFIG_FIXED_PHY
/* for Fixed Phy Added */
static int __init sdp_fixed_phy_init(void)
{
	int ret = 0;
	struct device_node *fixed_link = NULL;
	struct fixed_phy_status status = {0};
	int phy_id;

	pr_info("sdp-fixed-phy: Added Fixed Phy devices\n");

	for_each_node_by_name(fixed_link, "fixed-link") {
		status.link = 1;
		of_property_read_u32(fixed_link, "phy-id", &phy_id);
		of_property_read_u32(fixed_link, "speed", &status.speed);
		of_property_read_u32(fixed_link, "duplex", &status.duplex);
		status.pause = of_property_read_bool(fixed_link, "pause");
		status.asym_pause = of_property_read_bool(fixed_link, "asym-pause");

		ret = fixed_phy_add(PHY_POLL, phy_id, &status);
		if (ret < 0) {
			pr_err( "sdp-fixed-phy: fail fixed_phy_add!(phyid:%d, ret:%d)\n", phy_id, ret);
		} else {
			pr_info("sdp-fixed-phy: \tphyid:%d, speed:%d, duplex:%s\n", phy_id, status.speed, status.duplex?"Full":"Half");
		}
	}

	return ret;
}

static void __exit sdp_fixed_phy_exit(void)
{

}

module_init(sdp_fixed_phy_init);
module_exit(sdp_fixed_phy_exit);
#endif
