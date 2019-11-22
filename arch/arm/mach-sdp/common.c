/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/mfd/sdp_micom.h>

#include <asm/arch_timer.h>
#include <mach/map.h>
#include <mach/soc.h>
#include "common.h"

#pragma GCC diagnostic push  // require GCC 4.6
#pragma GCC diagnostic ignored "-Wcast-qual"

/* SW chipid */
static unsigned int sdp_chipid_dt = 0;

static void __init sdp_check_chipid_from_of(void)
{
	if (of_machine_is_compatible("samsung,sdp1202"))
	{
		sdp_chipid_dt = SDP1202_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1302"))
	{
		sdp_chipid_dt = SDP1302_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1304"))
	{
		sdp_chipid_dt = SDP1304_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1307"))
	{
		sdp_chipid_dt = SDP1307_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1404"))
	{
		sdp_chipid_dt = SDP1404_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1406fhd"))
	{
		sdp_chipid_dt = SDP1406FHD_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1406"))
	{
		sdp_chipid_dt = SDP1406UHD_CHIPID;
	}
	else if (of_machine_is_compatible("samsung,sdp1412"))
	{
		sdp_chipid_dt = SDP1412_CHIPID;
	}
	else
	{
		sdp_chipid_dt = NON_CHIPID;
	}
}

unsigned int sdp_soc(void)
{
	return sdp_chipid_dt;
}
EXPORT_SYMBOL(sdp_soc);

/* hw chipid */
struct sdp_chipid_info {
	unsigned int (*get_chipid_hw)(struct sdp_chipid_info *info);
	unsigned int (*get_revision_id)(struct sdp_chipid_info *info);
	int (*init)(struct sdp_chipid_info *info, struct device_node *np);
	void __iomem *regs;
	u32 values[4];
};
static struct sdp_chipid_info *sdp_chipid;

unsigned int sdp_rev(void)
{
	if (sdp_chipid && sdp_chipid->get_chipid_hw)
		return sdp_chipid->get_chipid_hw(sdp_chipid);
	else
		return 0;
}
EXPORT_SYMBOL(sdp_rev);

int sdp_get_revision_id(void)
{
	if (sdp_chipid && sdp_chipid->get_revision_id)
		return (int) sdp_chipid->get_revision_id(sdp_chipid);
	else
		return 0;
}
EXPORT_SYMBOL(sdp_get_revision_id);

static unsigned int _get_chipid_hw(struct sdp_chipid_info *info)
{
	return info->values[1];
}
static unsigned int _get_revision_id(struct sdp_chipid_info *info)
{
	if (sdp_chipid_dt == SDP1406FHD_CHIPID ||
		sdp_chipid_dt == SDP1406UHD_CHIPID)
		return (info->values[1] >> 10) & 0x7;
		
	return (info->values[1] >> 10) & 0x3;
}

static int _init(struct sdp_chipid_info *info, struct device_node *np)
{
#define SDP_CHIPID_STATUS		0x00
#define SDP_CHIPID_CTRL			0x04
#define SDP_CHIPID_VALUE_H		0x08
#define SDP_CHIPID_VALUE_L		0x0C
#define SDP_CHIPID_VALUE_MSB		0x10
	info->regs = of_iomap(np, 0);

	writel(0x1f, info->regs + SDP_CHIPID_CTRL);
	while (readl(info->regs + SDP_CHIPID_STATUS) != 0)
		barrier();

	info->values[0] = readl(info->regs + SDP_CHIPID_VALUE_L);
	info->values[1] = readl(info->regs + SDP_CHIPID_VALUE_H);
	info->values[2] = readl(info->regs + SDP_CHIPID_VALUE_MSB);
	info->values[3] = 0;
	return (info->values[1] == 0) ? -1 : 0;
}

static struct sdp_chipid_info info_golf = {
	.get_chipid_hw		= _get_chipid_hw,
	.get_revision_id	= _get_revision_id,
	.init			= _init,
};

/* hawk-p */
static int hawkp_init(struct sdp_chipid_info *info, struct device_node *np)
{
	info->regs = of_iomap(np, 0);
	if(info->regs == NULL)
	{
		pr_err("hawkp_init of_iomap failed!!\n");
		return -1;
	}

	writel(0x1f, info->regs + 0x04);
	while (readl(info->regs) != 0)
		barrier();

	info->values[0] = readl(info->regs + 0x08);
	info->values[1] = readl(info->regs + 0x0c);
	info->values[2] = readl(info->regs + 0x10);
	info->values[3] = readl(info->regs + 0x14);

	pr_info("hawk soc: hw fused values = %08x %08x %08x %08x\n",
			info->values[3], info->values[2], info->values[1], info->values[0]);

	//Hawk-P EVT0 bug fix
	if(soc_is_sdp1404() && ((info->values[3] & 0xFF000000) || ((info->values[1] & 0xF000) != 0x4000)))	{
		pr_info("HawkP EVT0 ChipID Fix!\n");
		info->values[0] = 0;
		info->values[1] = 0;
		info->values[2] = 0;
		info->values[3] = 0;
	}
	
	if(soc_is_sdp1412()) {		
		if(!(readl((unsigned int*)0xfe040024)&0x4)){
			pr_info("HawkA EVT0 ChipID Fix!\n");
			info->values[0] = 0;
			info->values[1] = 0;
			info->values[2] = 0;
			info->values[3] = 0;		
		}
	}

	return (info->values[1] == 0) ? -1 : 0;
}
static struct sdp_chipid_info info_hawkp = {
	.get_chipid_hw		= _get_chipid_hw,
	.get_revision_id	= _get_revision_id,
	.init			= hawkp_init,
};

static const struct of_device_id sdp_chipid_of_match[] __initconst = {
	{ .compatible	= "samsung,sdp-chipid",
		.data = (void*)&info_golf },
	{ .compatible	= "samsung,sdp-chipid-hawkp",
		.data = (void*)&info_hawkp },
	{},
};

static int __init sdp_chipid_of_init(void)
{
	struct device_node *np;

	/* get chipid from DT */
	sdp_check_chipid_from_of();

	/* get revision from HW chipid register */
	np = of_find_matching_node(NULL, sdp_chipid_of_match);
	if (np) {
		sdp_chipid = (struct sdp_chipid_info*) of_match_node(sdp_chipid_of_match, np)->data;
		if (sdp_chipid->init(sdp_chipid, np) < 0)
			pr_warn("SDP: cannot get revision value from HW.\n");
	}

	pr_info("SDP: SoC info id:0x%x, rev:0x%x, rev_id:0x%x\n",
		sdp_soc(), sdp_rev(), sdp_get_revision_id());

	of_node_put(np);

	return 0;
}

void __init sdp_init_irq(void)
{
	printk("[%d] %s\n", __LINE__, __func__);
	
	sdp_chipid_of_init();

	irqchip_init();
}

void sdp_restart(char mode, const char *cmd)
{
    if (of_machine_is_compatible("samsung,sdp1406fhd"))
    {
        printk("%s: Wait watchdog reboot ... \n", __func__);
	    
	sdp_micom_send_cmd(SDP_MICOM_CMD_RESTART, 0, 0);
				
        while(1);
    }
    else
    {
        pr_err("%s: cannot support non-DT\n", __func__);
        return;
    }
}


static enum sdp_board sdp_board_type = 0;

static int __init set_sdp_board_type_main(char *p)
{
	sdp_board_type = SDP_BOARD_MAIN;
	return 0;
}
static int __init set_sdp_board_type_jackpack(char *p)
{
	sdp_board_type = SDP_BOARD_JACKPACK;
	return 0;
}
static int __init set_sdp_board_type_lfd(char *p)
{
	sdp_board_type = SDP_BOARD_LFD;
	return 0;
}
static int __init set_sdp_board_type_sbb(char *p)
{
	sdp_board_type = SDP_BOARD_SBB;
	return 0;
}
static int __init set_sdp_board_type_hcn(char *p)
{
	sdp_board_type = SDP_BOARD_HCN;
	return 0;
}
static int __init set_sdp_board_type_vgw(char *p)
{
	sdp_board_type = SDP_BOARD_VGW;
	return 0;
}
static int __init set_sdp_board_type_fpga(char *p)
{
	sdp_board_type = SDP_BOARD_FPGA;
	return 0;
}
static int __init set_sdp_board_type_av(char *p)
{
	sdp_board_type = SDP_BOARD_AV;
	return 0;
}
static int __init set_sdp_board_type_mtv(char *p)
{
	sdp_board_type = SDP_BOARD_MTV;
	return 0;
}


enum sdp_board get_sdp_board_type(void)
{
	return sdp_board_type;
}

EXPORT_SYMBOL(get_sdp_board_type);

early_param("main", set_sdp_board_type_main);
early_param("jackpack", set_sdp_board_type_jackpack);
early_param("lfd", set_sdp_board_type_lfd);
early_param("sbb", set_sdp_board_type_sbb);
early_param("hcn", set_sdp_board_type_hcn);
early_param("vgw", set_sdp_board_type_vgw);
early_param("fpga", set_sdp_board_type_fpga);
early_param("av", set_sdp_board_type_av);
early_param("mtv", set_sdp_board_type_mtv);

#pragma GCC diagnostic pop   // require GCC 4.6

