/*
 *
 * Filename: drivers/usb/core/internal_devlist.h
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * Descriptor information of priority usb devices head file
 */
#ifndef __USBCORE_PRIORITY_DEVLIST_H
#define __USBCORE_PRIORITY_DEVLIST_H

#define IS_BTHUB_FAMILY \
        (udev->descriptor.idVendor == 0x0a5c) && \
		((udev->descriptor.idProduct == 0x4500)\
                        || (udev->descriptor.idProduct == 0x4502) \
			|| (udev->descriptor.idProduct == 0x4503) \
			|| (udev->descriptor.idProduct == 0x22be) \
			|| (udev->descriptor.idProduct == 0x2045))

#define IS_WIFI \
	((udev->descriptor.idVendor == 0x0a5c) && \
		((udev->descriptor.idProduct == 0xbd27)\
		|| (udev->descriptor.idProduct == 0xbd1d))) \
		|| ((udev->descriptor.idVendor == 0x0cf3) && \
			 (udev->descriptor.idProduct == 0x1022))

#endif
