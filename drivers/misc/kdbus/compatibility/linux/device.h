#include_next <linux/device.h>

#ifndef KDBUS_DEVICE_H
#define KDBUS_DEVICE_H

#include <linux/version.h>
// main.c
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
int subsys_virtual_register(struct bus_type *subsys,
			    const struct attribute_group **groups);
#endif
#endif /* KDBUS_DEVICE_H */
