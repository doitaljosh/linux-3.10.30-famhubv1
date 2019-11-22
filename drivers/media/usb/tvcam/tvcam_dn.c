/*
 * USB tvcam_dn driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the 2.6.3 version of drivers/media/usb/tvcam/tvcam_dn.c
 * but has been rewritten to be easier to read and use.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

struct tvcam_dn_set_t
{
	__u8  req;
	__u32 size;
	__u32 timeout;
} __attribute__ ((packed));

struct tvcam_dn_get_t
{
	__u16 vid;
	__u16 pid;
	__u32 trans_size;
} __attribute__ ((packed));

#define TEST_TYPE  0x00
#define TEST_USB_SET_COMMAND    _IOR (TEST_TYPE, 0, struct tvcam_dn_set_t)
#define TEST_USB_GET_COMMAND    _IOR (TEST_TYPE, 0, struct tvcam_dn_get_t)


/* table of devices that work with this driver */
static const struct usb_device_id tvcam_dn_table[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(0x04e8, 0x205c, 2) }, /* Falcon */
	{ USB_DEVICE_INTERFACE_NUMBER(0x04e8, 0x2061, 2) }, /* FalconPlus */
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, tvcam_dn_table);


/* Get a minor range for your devices from the usb maintainer */
#define tvcam_dn_dev_t_MINOR_BASE	192

/* Structure to hold all of our device specific stuff */
struct tvcam_dn_dev_t {
	struct usb_device       *udev;			/* the usb device for this device */
	struct usb_interface    *interface;		/* the interface for this device */
	size_t                  bulk_out_size;
	__u8                    bulk_out_endpointAddr;
	struct kref             kref;
	struct mutex            io_mutex;
};
#define to_tvcam_dn_dev(d) container_of(d, struct tvcam_dn_dev_t, kref)

static struct usb_driver tvcam_dn_driver;

static void tvcam_dn_delete(struct kref *kref)
{
	struct tvcam_dn_dev_t *dev = to_tvcam_dn_dev(kref);

	usb_put_dev(dev->udev);
	kfree(dev);
}

static int tvcam_dn_open(struct inode *inode, struct file *file)
{
	struct tvcam_dn_dev_t *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&tvcam_dn_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
			__func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	//retval = usb_autopm_get_interface(interface);
	//if (retval)
	//	goto exit;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

exit:
	return retval;
}

static int tvcam_dn_release(struct inode *inode, struct file *file)
{
	struct tvcam_dn_dev_t *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	//mutex_lock(&dev->io_mutex);
	//if (dev->interface)
	//	usb_autopm_put_interface(dev->interface);
	//mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, tvcam_dn_delete);
	return 0;
}

static ssize_t tvcam_dn_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct tvcam_dn_dev_t *dev;
	int actual_length = 0;
	int ret_val = 0;
	int timeout = 1000;
	__u8 *data = NULL;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	data = kmalloc(count, GFP_KERNEL);
	if (data == NULL) {
		return -ENOMEM;
	}

	if (copy_from_user(data, user_buffer, count)) {
		return -EFAULT;
	}

	ret_val = usb_bulk_msg(dev->udev,
					usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
					data,
					count,
					&actual_length,
					timeout);

	kfree(data);

	return actual_length;
}

static long tvcam_dn_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tvcam_dn_dev_t *dev;
	//int retval = 0;
	struct tvcam_dn_set_t *pdata;
	struct tvcam_dn_get_t *pdata_dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	switch (cmd)
	{
	case TEST_USB_SET_COMMAND:
		pdata = (struct tvcam_dn_set_t *)arg;

		//printk("[%s] 0x%x size:%d timeout:%d\n", __func__, pdata->req, pdata->size, pdata->timeout);

		usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), pdata->req,
			USB_TYPE_VENDOR | USB_DIR_OUT, pdata->size&0xFFFF,
			(pdata->size>>16)&0xFFFF, NULL, 0, pdata->timeout);
		break;

	case TEST_USB_GET_COMMAND:
		pdata_dev = (struct tvcam_dn_get_t *)arg;
		pdata_dev->vid = le16_to_cpu(dev->udev->descriptor.idVendor);
		pdata_dev->pid = le16_to_cpu(dev->udev->descriptor.idProduct);
		pdata_dev->trans_size = dev->bulk_out_size;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations tvcam_dn_fops = {
	.owner =	THIS_MODULE,
	.open =		tvcam_dn_open,
	.write =	tvcam_dn_write,
	.release =	tvcam_dn_release,
	.unlocked_ioctl = tvcam_dn_ioctl,
	.llseek =	noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver tvcam_dn_class = {
	.name =		"tvcam_dn%d",
	.fops =		&tvcam_dn_fops,
	.minor_base =	tvcam_dn_dev_t_MINOR_BASE,
};

static int tvcam_dn_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct tvcam_dn_dev_t *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&dev->kref);
	mutex_init(&dev->io_mutex);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_out_size = usb_endpoint_maxp(endpoint);
		}
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &tvcam_dn_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB tvcam_dn device now attached to tvcam_dn-%d",
		 interface->minor);
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, tvcam_dn_delete);
	return retval;
}

static void tvcam_dn_disconnect(struct usb_interface *interface)
{
	struct tvcam_dn_dev_t *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &tvcam_dn_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	/* decrement our usage count */
	kref_put(&dev->kref, tvcam_dn_delete);

	dev_info(&interface->dev, "USB tvcam_dn #%d now disconnected", minor);
}

static struct usb_driver tvcam_dn_driver = {
	.name =		"tvcam_dn",
	.probe =	tvcam_dn_probe,
	.disconnect =	tvcam_dn_disconnect,
	.id_table =	tvcam_dn_table,
};

module_usb_driver(tvcam_dn_driver);

MODULE_LICENSE("GPL");

