/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table panel_wt61p807[] = {
	/* panel key */
	{ 0x02, KEY_POWER },		/* power key */
	{ 0x07, KEY_EMAIL },		/* volume up (+) */
	{ 0x0B, KEY_FINANCE },		/* volume down (-) */
	{ 0x10, KEY_PROG2 },		/* channel down (down) */
	{ 0x12, KEY_PREVIOUSSONG },	/* channel up (up) */
	{ 0x2D, KEY_REFRESH },		/* exit */
	{ 0x68, KEY_NEXTSONG }		/* enter */
};

static struct rc_map_list panel_wt61p807_map = {
	.map = {
		.scan    = panel_wt61p807,
		.size    = ARRAY_SIZE(panel_wt61p807),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_PANEL_WT61P807,
	}
};

static int __init init_panel_map_wt61p807(void)
{
	return rc_map_register(&panel_wt61p807_map);
}

static void __exit exit_panel_map_wt61p807(void)
{
	rc_map_unregister(&panel_wt61p807_map);
}

module_init(init_panel_map_wt61p807)
module_exit(exit_panel_map_wt61p807)

MODULE_LICENSE("GPL");
