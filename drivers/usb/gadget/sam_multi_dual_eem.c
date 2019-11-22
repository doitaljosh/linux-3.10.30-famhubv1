/*
 * multi.c -- Samsung Multifunction Composite gadget driver
 *
 * Copyright (C) 2009 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/module.h>


#define DRIVER_DESC	"Samsung Multifunction Composite Gadget"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Aman Deep");
MODULE_LICENSE("GPL");

/***************************** All the files... *****************************/

/*
 *  
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 * 
 * we have used the multi.c composite driver for referance
 */


#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#include "f_mass_storage.c"
#include "f_eem.c"
#include "u_ether.c"

#ifdef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
/* mass storage gadget functionality */
struct msc_gadget {
        int                             nports;

        struct fsg_common               fsg_common;
        struct device                   dev;
};

static void sam_multi_msc_release(struct device *dev)
{
        /* Nothing needs to be done */
}

/* samsung multi function gadget functionality*/
struct sam_multi_gadget {

   struct usb_gadget   *gadget;    /* usb gadget file */

   struct msc_gadget   msc_gadget; /* mass storage gadegt structure */ 
// struct dual_eem_gadget  *dual_eem;  /* dual eem gadegt */

   struct device       dev;        /* device file */
};

static struct sam_multi_gadget sam_multi = {.msc_gadget.nports = 0 };

#endif

USB_GADGET_COMPOSITE_OPTIONS();
/* module parameters */
static int use_eem = 1;
static int use_storage = 1;
module_param(use_eem, int, S_IRUGO);
module_param(use_storage, int, S_IRUGO);


/***************************** Device Descriptor ****************************/

#define MULTI_VENDOR_NUM	0x1d6b	/* Linux Foundation */
#define MULTI_PRODUCT_NUM	0x0104	/* Multifunction Composite Gadget */


#define SAM_MULTI_CDC_CONFIG_NUM    1

#ifdef CONFIG_SAMSUNG_DEVICE_USBNET_COMMON
/* Serial Number for gadget driver */
#define SAM_MULTI_SERIAL_NUMBER    "201204E8474144474554"
#endif     //CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
};


static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &(struct usb_otg_descriptor){
		.bLength =		sizeof(struct usb_otg_descriptor),
		.bDescriptorType =	USB_DT_OTG,

		/*
		 * REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ...
		 */
		.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
	},
	NULL,
};


#define	SAM_MULTI_STRING_CDC_CONFIG_IDX 0

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = SAM_MULTI_SERIAL_NUMBER,
	[SAM_MULTI_STRING_CDC_CONFIG_IDX].s   = "Multifunction with DUAL CDC EEM and Storage",
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};




/****************************** Configurations ******************************/

static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

#ifndef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
static struct fsg_common fsg_common;
#endif

/********** CDC EEM **********/

static __init int cdc_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

    if (use_eem == 1) {
        multi_eem.eem_num = 0;        
	    ret = eem_bind_config(c, multi_eem.dev[0]);
	    if (ret < 0)
		    return ret;
    
        multi_eem.eem_num = 1;        
	    ret = eem_bind_config(c, multi_eem.dev[1]);
	    if (ret < 0)
		    return ret;
    }
    if (use_storage == 1) {
#ifdef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
       ret = fsg_bind_config(c->cdev, c, &sam_multi.msc_gadget.fsg_common);
#else
       ret = fsg_bind_config(c->cdev, c, &fsg_common);
#endif       
	    if (ret < 0)
		    return ret;
    }
	return 0;
}

static int cdc_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
		.bConfigurationValue	= SAM_MULTI_CDC_CONFIG_NUM,
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label          = strings_dev[SAM_MULTI_STRING_CDC_CONFIG_IDX].s;
	config.iConfiguration = strings_dev[SAM_MULTI_STRING_CDC_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, cdc_do_config);
}


/****************************** Gadget Bind ******************************/


static int __ref sam_multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int status = -EINVAL;;

    multi_eem.cdev = cdev;
    if (use_eem) {
	    /* set up network link layer */
        the_dev = NULL;
        the_dev = gether_setup(gadget, NULL);
	if (IS_ERR(the_dev))
		return PTR_ERR(the_dev);
	    
        multi_eem.dev[0] = the_dev;

	    /* set up second network link layer */
        the_dev = NULL;
        the_dev = gether_setup(gadget, NULL);
	if (IS_ERR(the_dev) < 0)
            goto fail1;

        // strcpy(the_dev->net->name, "usb1"); 
        multi_eem.dev[1] = the_dev;
    }	
	/* set up mass storage function */
	if (use_storage) {
		void *retp;
#ifdef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
       /* create a msc_gadget directory in sysfs */
       sam_multi.msc_gadget.dev.release = sam_multi_msc_release;
       sam_multi.msc_gadget.dev.parent = gadget->dev.parent->parent;
       dev_set_name (&sam_multi.msc_gadget.dev, "msc_gadget");
       
       status = device_register(&sam_multi.msc_gadget.dev);
                if (status) {
           WARNING(cdev, "msc_gadget directory is not created\n");
           put_device(&sam_multi.msc_gadget.dev);
                        goto fail4;
                }
       /* create port number 0 */
       sam_multi.msc_gadget.fsg_common.port_num = 0;
       sam_multi.msc_gadget.fsg_common.parent = &sam_multi.msc_gadget.dev;
       retp = fsg_common_from_params(&sam_multi.msc_gadget.fsg_common, cdev, &fsg_mod_data);
       if (IS_ERR(retp)) {
           status = PTR_ERR(retp);
           // Comment out for wrong file name crash at insmod
           device_unregister(&sam_multi.msc_gadget.dev);
           goto fail4;
       }
       sam_multi.msc_gadget.nports = 1; /* number of ports */
#else
		retp = fsg_common_from_params(&fsg_common, cdev, &fsg_mod_data);
		if (IS_ERR(retp)) {
			status = PTR_ERR(retp);
			goto fail4;
		}
#endif        
	}

	/* allocate string IDs */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (unlikely(status < 0))
		goto fail5;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	status = cdc_config_register(cdev);
	if (unlikely(status < 0))
		goto fail5;
	usb_composite_overwrite_options(cdev, &coverwrite);

	/* we're done */
	dev_info(&gadget->dev, DRIVER_DESC "\n");
	return 0;


	/* error recovery */
fail5:
#ifdef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
   if(use_storage) {
       fsg_common_put(&sam_multi.msc_gadget.fsg_common);
       // Comment out for wrong file name crash at insmod
       device_unregister(&sam_multi.msc_gadget.dev);
   }
#else
   if(use_storage) fsg_common_put(&fsg_common);
#endif

fail4:
	if(use_eem) {
        the_dev = multi_eem.dev[1];
         gether_cleanup(the_dev);
        multi_eem.dev[1] = NULL;
    }
fail1:
	if(use_eem) {
        the_dev = multi_eem.dev[0];
        gether_cleanup(the_dev);
        multi_eem.dev[0] = NULL;
    }
        the_dev = NULL;
	return status;
}

static int sam_multi_unbind(struct usb_composite_dev *cdev)
{
    if(use_eem) {
        the_dev = multi_eem.dev[0];
        gether_cleanup(the_dev);
        multi_eem.dev[0] = NULL;

        the_dev = multi_eem.dev[1];
        gether_cleanup(the_dev);
        multi_eem.dev[1] = NULL;
    }		

#ifdef CONFIG_SAMSUNG_DEVICE_STORAGE_GADGET
    if(use_storage) {
   fsg_common_put(&sam_multi.msc_gadget.fsg_common);
   //device_unregister(&sam_multi.msc_gadget.fsg_common.dev);
   device_unregister(&sam_multi.msc_gadget.dev);
    }
#else
    if(use_storage) fsg_common_put(&fsg_common);
#endif    
	return 0;
}


/****************************** Some noise ******************************/


static struct usb_composite_driver sam_multi_driver = {
	.name		= "g_sam_multi",
	.dev		= &device_desc,
	.max_speed	= USB_SPEED_SUPER,
	.strings	= dev_strings,
    	.bind		= sam_multi_bind,
	.unbind		= sam_multi_unbind,
};


static int __init sam_multi_init(void)
{
    if(!((use_eem == 1) || (use_storage == 1))) {
        printk("\n Error : module parameters value : not permitted ");    
        printk("\n Enable at least one function ");    
        return 0;
    }   
	return usb_composite_probe(&sam_multi_driver);
}
module_init(sam_multi_init);

static void __exit sam_multi_exit(void)
{
	usb_composite_unregister(&sam_multi_driver);
}
module_exit(sam_multi_exit);

