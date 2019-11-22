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

#ifndef _AHCI_SDP_H
#define _AHCI_SDP_H

#include <linux/compiler.h>
#include <linux/of.h>
#include <linux/device.h>

struct device;
struct ata_port_info;

struct ahci_sdp_data {
	int (*init)(struct device *dev);
	void (*exit)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);

	const struct ata_port_info *ata_port_info;
	unsigned int force_port_map;
	unsigned int mask_port_map;
	struct sdp_sata_phy *phy;
};

/* phy device: temporary impl for multi-platforms support */
struct sdp_sata_phy_ops {
	int (*init) (struct sdp_sata_phy *phy);
	int (*exit) (struct sdp_sata_phy *phy);
};
struct sdp_sata_phy {
	struct device		*dev;
	struct sdp_sata_phy_ops	ops;
	struct list_head	link;
};

int sdp_sata_phy_init(struct sdp_sata_phy *phy);
int sdp_sata_phy_exit(struct sdp_sata_phy *phy);
struct sdp_sata_phy* devm_sdp_sata_phy_get_by_node(struct device *dev, struct device_node *node);
int sdp_sata_phy_register(struct sdp_sata_phy *phy);
int sdp_sata_phy_unregister(struct sdp_sata_phy *phy);

#endif /* _AHCI_SDP_H */
