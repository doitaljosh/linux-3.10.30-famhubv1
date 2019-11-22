/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt)    KBUILD_MODNAME ": " fmt
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

#include "defaults.h"
#include "util.h"
#include "domain.h"

static int __init kdbus_init(void)
{
	int ret;

	ret = subsys_virtual_register(&kdbus_subsys, NULL);
	if (ret < 0)
		return ret;

	/*
	 * Create the initial domain; it is world-accessible and
	 * provides the /dev/kdbus/control device node.
	 */
	ret = kdbus_domain_new(NULL, NULL, 0666, &kdbus_domain_init);
	if (ret < 0) {
		bus_unregister(&kdbus_subsys);
		pr_err("failed to initialize, error=%i\n", ret);
		return ret;
	}

#ifdef SBB_SUPPORT
	pr_info("initialized (SBB support included)\n");
#else
	pr_info("initialized\n");
#endif

	return 0;
}

static void __exit kdbus_exit(void)
{
	kdbus_domain_disconnect(kdbus_domain_init);
	kdbus_domain_unref(kdbus_domain_init);
	bus_unregister(&kdbus_subsys);
}

module_init(kdbus_init);
module_exit(kdbus_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("D-Bus, powerful, easy to use interprocess communication");
