/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * @file virtual-rc.c
 * @Kernel Virtual Remote controller driver for SBB
 * @date 2014/09/29
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <media/rc-core.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>
#include "kernel-socket.h"

#define DRIVER_NAME "Virtual-rc"
#define PORT        4040
#define MSG_SIZE    2

struct virtual_rc_data {
    struct rc_dev *rc;
};

static struct rw_handle* hnd;
static struct virtual_rc_data *virtual_rc;
static struct task_struct *task[2];


int virtual_rc_msg_wait(void *arg)
{

    struct fifo_handle* hnd = (struct fifo_handle*)arg;
    char buff[MSG_SIZE] = {0};

    printk(KERN_INFO "virtual_rc_msg_wait\n");

    while(1)
    {
        if(kthread_should_stop())
            break;

        dequeue_msg(buff, 2, hnd);

        switch (buff[0]) 
        {
        case SDP_MICOM_EVT_KEYPRESS:
            printk(KERN_INFO "Virual RC :Inside Key press");
            rc_keydown(virtual_rc->rc, buff[1], 0);
            break;
        case SDP_MICOM_EVT_KEYRELEASE:
            printk(KERN_INFO "Virual RC :Inside Key Release");
            rc_keyup(virtual_rc->rc);
            break;
        default:
            printk(KERN_INFO "Virual RC :Bad message");
            break;
        }
    }

    return 0;
}

/* module init method */

static int virtual_rc_probe(struct platform_device *pdev)
{

    struct rc_dev *rc;
    int ret;
	
    printk(KERN_INFO "virtual_rc started %s %s\n", __DATE__, __TIME__);
    rc = rc_allocate_device();
    if (!rc) {
        printk(KERN_ERR "rc_dev allocation failed\n");
        return -ENOMEM;
    }

    virtual_rc = kzalloc(sizeof(struct virtual_rc_data), GFP_KERNEL);
    if (!virtual_rc) {
        printk(KERN_ERR "failed to allocate driver data\n");
        return -ENOMEM;
    }

    rc->input_name		= "wt61p807 rc device";
    rc->input_phys		= "wt61p807/rc";
    rc->input_id.bustype	= BUS_VIRTUAL;
    rc->input_id.version	= 1;
    rc->driver_name	= DRIVER_NAME;
    rc->map_name		= RC_MAP_WT61P807;
    rc->driver_type		= RC_DRIVER_SCANCODE;
    rc->allowed_protos	= RC_BIT_ALL;

    ret = rc_register_device(rc);
    if (ret < 0) {
        printk(KERN_ERR "rc_dev registration failed\n");
        rc_free_device(rc);
        return ret;
    }

    virtual_rc->rc = rc;
// Registering with kernel sockets 

    hnd = register_with_socket_core(READER_MODE, PORT, MSG_SIZE);
    if (IS_ERR(hnd)) {
        printk(KERN_ERR "failed to register with socket core\n");
        rc_unregister_device(virtual_rc->rc);
        return PTR_ERR(hnd);
    }
    task[0] = kthread_run(kernel_start_tcp_srv, hnd, "virtual-rc");

    task[1] = kthread_run(virtual_rc_msg_wait, hnd->readerFifo, "virtual_rc_msg_wait");

    if (IS_ERR(task[0]) || IS_ERR(task[1])){
        printk(KERN_ERR "Unable to start msg and socket threads -virtual-rc\n");
        rc_unregister_device(virtual_rc->rc);
        unregister_with_socket_core(hnd);
        return -1;
    }

     return 0;
}

/* module  Exit method */
static int virtual_rc_remove(struct platform_device *pdev)
{
    kthread_stop(task[1]);
    kthread_stop(task[0]);
    rc_unregister_device(virtual_rc->rc);
    unregister_with_socket_core(hnd);

    printk(KERN_INFO "virtual_rc_exit \n");
    return 0;
}


static const struct of_device_id virtual_rc_of_match[] = {
	{ .compatible = "samsung,virtual-rc" },
	{},
};


static struct platform_driver virtual_rc_driver = {
	.probe = virtual_rc_probe,
	.remove = virtual_rc_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= virtual_rc_of_match,
	},
};


module_platform_driver(virtual_rc_driver);



MODULE_AUTHOR("SAMSUNG");
MODULE_DESCRIPTION("Virtual Remote controller driver for SBB WT61P807 ");
MODULE_LICENSE("GPL");
