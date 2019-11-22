/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * @file sbb_interface.c
 * @Kernel module for SBB detection and network detection in Kernel space.
 * @date   2014/09/29
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "sbb-interface.h"


static ssize_t network_store (struct   kobject        *kobj,
                              struct   kobj_attribute *attr,
                              const    char           *buf,
                              size_t                   count);

static int     get_gpio_num  (unsigned int             port,
                              unsigned int             index,
				              unsigned int            *gpio);


static          char network_flag       = 0;
static          char sbb_connected_flag = SBB_NOT_INITIALIZED;


static DECLARE_WAIT_QUEUE_HEAD(nw_wq);
static DECLARE_WAIT_QUEUE_HEAD(gpio_wq);

static struct kobj_attribute network_attribute =
         __ATTR(network, 0666, NULL, network_store); 

static struct attribute *attrs[] = {
         &network_attribute.attr,
         NULL,  
}; 

static struct attribute_group attr_group = {
         .attrs = attrs,
};
 
 static struct kobject *sbb_interface_kobj;
 


int check_network_connected (void)
{
    return network_flag;

}

int check_network_connected_wait (void)
{
    wait_event_interruptible(nw_wq, network_flag != 0);
    return network_flag;
}

int check_sbb_connected (void)
{
    return sbb_connected_flag;
}

int check_sbb_connected_wait (void)
{
    wait_event_interruptible(gpio_wq, (sbb_connected_flag != -1));
    return sbb_connected_flag;
}


static ssize_t network_store(struct kobject        *kobj,
                             struct kobj_attribute *attr,
                             const  char           *buf,
                             size_t                 count)
{
    int val;

    sscanf(buf, "%du", &val);

    if (val == 1) {
        network_flag = 1;
		printk(KERN_INFO "tv-sbb n/w up\n");
        wake_up_all(&nw_wq);
    } else {    
        pr_err("%s :invalid arg\n", __FUNCTION__);
    }

         
    return count;
}

 
static int __init sbb_interface_init (void)
{
	struct device_node *dt_node;
	unsigned int gpio_num = 0;
	int err;
	
     
    sbb_interface_kobj = kobject_create_and_add("SBB", kernel_kobj);
      
    if (!sbb_interface_kobj) {
                 
        return -ENOMEM;
    }
         
    err = sysfs_create_group(sbb_interface_kobj, &attr_group);
         
    if (err) {                 
       kobject_put(sbb_interface_kobj);
		return -1;
    }
	
	dt_node = of_find_node_by_path("/tztv_sbb-interface");
	
	if (!dt_node) {
		pr_err("Failed to find device-tree node:/tztv_sbb-interface\n");
		return -ENODEV;
	}
	
	gpio_num = of_get_named_gpio(dt_node, "samsung,sbb-detect", 0);

     if (!gpio_is_valid(gpio_num)) {
         pr_err("%s Invalid GPIO port.\n", __FUNCTION__);
         return -1;
     }

    err = gpio_request(gpio_num, "SBB_DETECT_GPIO");

    if (err) 
    {
        pr_err("%s failed to request GPIO \n", __FUNCTION__);
        goto err;
    }        
        
    gpio_direction_input(gpio_num);

    sbb_connected_flag = !(gpio_get_value(gpio_num));
	
	gpio_free(gpio_num);
	
	if (sbb_connected_flag) {
		printk(KERN_INFO "sbb connected - %d", gpio_num);		
	} else {
		printk(KERN_INFO "sbb not connected %d", gpio_num);		
	}

    wake_up_all(&gpio_wq);

err:         
    return err;
}
 
 static  void  __exit  sbb_interface_exit (void)
{
    kobject_put(sbb_interface_kobj);
}

 
 module_init(sbb_interface_init);
 module_exit(sbb_interface_exit);
 MODULE_LICENSE("GPL");
 MODULE_AUTHOR("SAMSUNG");


EXPORT_SYMBOL(check_network_connected);
EXPORT_SYMBOL(check_network_connected_wait);
EXPORT_SYMBOL(check_sbb_connected);
EXPORT_SYMBOL(check_sbb_connected_wait);