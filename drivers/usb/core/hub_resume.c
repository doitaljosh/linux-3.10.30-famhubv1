/*
 * Filename: drivers/usb/core/hub_resume.c 
 * Developed by: 14_USB 
 * Date:25th September 2014  
 * 
 * This file provides functions to  resume priority USB devices for an usb hub. 
 * USB devices are either reset-resume or enumerated depending on the case. 
 * Enumeration shall happen if the device is in disconnected state. Device in 
 * suspended state shall get its state reset to resume.
 */
extern int usb_resume_interface(struct usb_device *udev, struct usb_interface *intf, pm_message_t msg, int reset_resume);
extern int usb_resume_device(struct usb_device *udev, pm_message_t msg);

/*
 * This is basically a miniature form of hub_events()
 * Hopefully, kref of hub has been incremented inside this function
 * This code avoids the dequeue-ing of hub event list
 */
void hub_port_enumerate_device(struct usb_hub *hub, int port)
{
       struct usb_device       *hdev;
       struct usb_interface    *intf;
       struct device           *hub_dev;
       u16 hubstatus;
       u16 hubchange;
       u16 portstatus;
       u16 portchange;
       int ret;
       int connect_change, wakeup_change;

#ifdef CONFIG_ARCH_NVT72668
       struct usb_hcd *hcd;
#endif
      
       hdev = hub->hdev;
       hub_dev = hub->intfdev;
       intf = to_usb_interface(hub_dev);

#ifdef CONFIG_ARCH_NVT72668
       hcd =  bus_to_hcd(hdev->bus);
#endif

       dev_dbg(hub_dev, "func < %s > state %d ports %d chg %04x evt %04x\n",
                        __func__,hdev->state, hub->descriptor
                               ? hub->descriptor->bNbrPorts
                               : 0,
                        /* NOTE: expects max 15 ports... */
                        (u16) hub->change_bits[0],
                        (u16) hub->event_bits[0]);

       /*
        * Lock the device, then check to see if we were
        * disconnected while waiting for the lock to succeed.
        */
       spin_lock_irq(&hub_event_lock);
       kref_get(&hub->kref);
       spin_unlock_irq(&hub_event_lock);
       usb_lock_device(hdev);
       if (unlikely(hub->disconnected))
               goto loop_disconnected;

       /*
        * If the hub has died, clean up after it
        */
       if (hdev->state == USB_STATE_NOTATTACHED) {
               hub->error = -ENODEV;
               hub_quiesce(hub, HUB_DISCONNECT);
               goto loop;
       }

       /*
        * Autoresume
        */
       ret = usb_autopm_get_interface(intf);
       if (ret) {
               dev_dbg(hub_dev, "Can't autoresume: %d\n", ret);
               goto loop;
       }

       /*
        * If this is an inactive hub, do nothing
        */
       if (hub->quiescing)
               goto loop_autopm;

       if (hub->error) {
               dev_dbg (hub_dev, "resetting for error %d\n", hub->error);

               ret = usb_reset_device(hdev);
               if (ret) {
                       dev_dbg (hub_dev, "error resetting hub: %d\n", ret);
                       goto loop_autopm;
               }

               hub->nerrors = 0;
               hub->error = 0;
       }

       /*
        * Deal with port status changes
        */
       if (test_bit(port, hub->busy_bits)) {
               printk(KERN_EMERG "%s Hub is busy\n", dev_name(&hdev->dev));
               goto loop_autopm;
       }

       connect_change = test_bit(port, hub->change_bits);
       wakeup_change = test_and_clear_bit(port, hub->wakeup_bits);
       if (!test_and_clear_bit(port, hub->event_bits) && !connect_change && !wakeup_change)
               goto loop_autopm;

       ret = hub_port_status(hub, port, &portstatus, &portchange);

#ifdef CONFIG_ARCH_NVT72668
       if(test_and_clear_bit((unsigned long)port, &hcd->porcd)) {
               portchange |=USB_PORT_STAT_C_CONNECTION;
       }
#endif

       if (ret < 0)
               goto loop_autopm;

       if (portchange & USB_PORT_STAT_C_CONNECTION) {
               usb_clear_port_feature(hdev, port, USB_PORT_FEAT_C_CONNECTION);
               connect_change = 1;
       }

       if (portchange & USB_PORT_STAT_C_ENABLE) {
               if (!connect_change)
                       dev_dbg (hub_dev, "port %d enable change, " "status %08x\n", port, portstatus);
               usb_clear_port_feature(hdev, port, USB_PORT_FEAT_C_ENABLE);

               /*
                * EM interference sometimes causes badly
                * shielded USB devices to be shutdown by
                * the hub, this hack enables them again.
                * Works at least with mouse driver.
                */
               if (!(portstatus & USB_PORT_STAT_ENABLE) && !connect_change && hub->ports[port - 1]->child) {
                       dev_err (hub_dev, "port %i " "disabled by hub (EMI?), " "re-enabling...\n", port);
                       connect_change = 1;
               }
       }

       if (hub_handle_remote_wakeup(hub, port, portstatus, portchange))
               connect_change = 1;

       if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
               u16 status = 0;
               u16 unused;

               dev_dbg(hub_dev, "over-current change on port " "%d\n", port);
               usb_clear_port_feature(hdev, port, USB_PORT_FEAT_C_OVER_CURRENT);
               msleep(100);    /* Cool down */
               hub_power_on(hub, true);
               hub_port_status(hub, port, &status, &unused);
               if (status & USB_PORT_STAT_OVERCURRENT)
                       dev_err(hub_dev, "over-current " "condition on port %d\n", port);
       }

       if (portchange & USB_PORT_STAT_C_RESET) {
               dev_dbg (hub_dev, "reset change on port %d\n", port);
               usb_clear_port_feature(hdev, port, USB_PORT_FEAT_C_RESET);
       }

       if ((portchange & USB_PORT_STAT_C_BH_RESET) && hub_is_superspeed(hub->hdev)) {
               dev_dbg(hub_dev, "warm reset change on port %d\n", port);
               usb_clear_port_feature(hdev, port, USB_PORT_FEAT_C_BH_PORT_RESET);
       }

       if (portchange & USB_PORT_STAT_C_LINK_STATE) {
               usb_clear_port_feature(hub->hdev, port, USB_PORT_FEAT_C_PORT_LINK_STATE);
       }

       if (portchange & USB_PORT_STAT_C_CONFIG_ERROR) {
               dev_warn(hub_dev, "config error on port %d\n", port);
               usb_clear_port_feature(hub->hdev, port, USB_PORT_FEAT_C_PORT_CONFIG_ERROR);
       }

       /*
        * Warm reset a USB3 protocol port if it's in
        * SS.Inactive state.
        */
       if (hub_port_warm_reset_required(hub, portstatus)) {
               int status;
               struct usb_device *udev = hub->ports[port - 1]->child;

               dev_dbg(hub_dev, "warm reset port %d\n", port);
               if (!udev) {
                       status = hub_port_reset(hub, port, NULL, HUB_BH_RESET_TIME, true);
                       if (status < 0)
                               hub_port_disable(hub, port, 1);
                       } else {
                               usb_lock_device(udev);
                               status = usb_reset_device(udev);
                               usb_unlock_device(udev);
                       }
               connect_change = 0;
       }

       if (connect_change)
               hub_port_connect_change(hub, port, portstatus, portchange);

       /*
        * Deal with hub status changes
        */
       if (test_and_clear_bit(0, hub->event_bits) == 0)
               ;       /* do nothing */
       else if (hub_hub_status(hub, &hubstatus, &hubchange) < 0)
               dev_err (hub_dev, "get_hub_status failed\n");
       else {
               if (hubchange & HUB_CHANGE_LOCAL_POWER) {
                       dev_dbg (hub_dev, "power change\n");
                       clear_hub_feature(hdev, C_HUB_LOCAL_POWER);
                       if (hubstatus & HUB_STATUS_LOCAL_POWER)
                               /* FIXME: Is this always true? */
                               hub->limited_power = 1;
                       else
                               hub->limited_power = 0;
               }
               if (hubchange & HUB_CHANGE_OVERCURRENT) {
                       u16 status = 0;
                       u16 unused;

                       dev_dbg(hub_dev, "over-current change\n");
                       clear_hub_feature(hdev, C_HUB_OVER_CURRENT);
                       msleep(500);    /* Cool down */
                        hub_power_on(hub, true);
                       hub_hub_status(hub, &status, &unused);
                       if (status & HUB_STATUS_OVERCURRENT)
                               dev_err(hub_dev, "over-current " "condition\n");
               }
       }

 loop_autopm:
       /*
        * Balance the usb_autopm_get_interface() above
        */
       usb_autopm_put_interface_no_suspend(intf);
 loop:
       /*
        * Balance the usb_autopm_get_interface_no_resume() in
        * kick_khubd() and allow autosuspend.
        */
       usb_autopm_put_interface(intf);
 loop_disconnected:
       usb_unlock_device(hdev);
       kref_put(&hub->kref, hub_release);
}
                                                                          
int hub_port_reset_resume(resume_args *arg1)
{
        struct usb_device *udev = arg1->udev;
        struct usb_hub *parent_hub = arg1->parent_hub;
	struct usb_hcd			*hcd = bus_to_hcd(udev->bus);
	struct usb_device_descriptor	descriptor = udev->descriptor;
	int 				i, ret = 0;
        int port1 = arg1->parent_portnum;

        dev_err(&udev->dev, "device state at the time of reset = %d\n",udev->state);
	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "device reset not allowed in state %d\n",
				udev->state);
		return -EINVAL;
	}

	if (!(udev->parent)) {
		/* this requires hcd-specific logic; see ohci_restart() */
		dev_dbg(&udev->dev, "%s for root hub!\n", __func__);
		return -EISDIR;
	}

	/* Disable LPM and LTM while we reset the device and reinstall the alt
	 * settings.  Device-initiated LPM settings, and system exit latency
	 * settings are cleared when the device is reset, so we have to set
	 * them up again.
	 */
	ret = usb_unlocked_disable_lpm(udev);
	if (ret) {
		dev_err(&udev->dev, "%s Failed to disable LPM\n.", __func__);
		goto re_enumerate;
	}
	ret = usb_disable_ltm(udev);
	if (ret) {
		dev_err(&udev->dev, "%s Failed to disable LTM\n.",
				__func__);
		goto re_enumerate;
	}

	set_bit(port1, parent_hub->busy_bits);
	for (i = 0; i < SET_CONFIG_TRIES; ++i) {

		/* ep0 maxpacket size may change; let the HCD know about it.
		 * Other endpoints will be handled by re-enumeration. */
		usb_ep0_reinit(udev);
		ret = hub_port_init(parent_hub, udev, port1, i);
		if (ret >= 0 || ret == -ENOTCONN || ret == -ENODEV)
			break;
	}
	clear_bit(port1, parent_hub->busy_bits);

	if (ret < 0)
		goto re_enumerate;
 
	/* Device might have changed firmware (DFU or similar) */
	if (descriptors_changed(udev, &descriptor)) {
		dev_info(&udev->dev, "device firmware changed\n");
		udev->descriptor = descriptor;	/* for disconnect() calls */
		goto re_enumerate;
  	}

	/* Restore the device's previous configuration */
	if (!udev->actconfig)
		goto done;

	mutex_lock(hcd->bandwidth_mutex);
	ret = usb_hcd_alloc_bandwidth(udev, udev->actconfig, NULL, NULL);
	if (ret < 0) {
		dev_warn(&udev->dev,
				"Busted HC?  Not enough HCD resources for "
				"old configuration.\n");
		mutex_unlock(hcd->bandwidth_mutex);
		goto re_enumerate;
	}
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			udev->actconfig->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&udev->dev,
			"can't restore configuration #%d (error=%d)\n",
			udev->actconfig->desc.bConfigurationValue, ret);
		mutex_unlock(hcd->bandwidth_mutex);
		goto re_enumerate;
  	}
	mutex_unlock(hcd->bandwidth_mutex);
	usb_set_device_state(udev, USB_STATE_CONFIGURED);

	/* Put interfaces back into the same altsettings as before.
	 * Don't bother to send the Set-Interface request for interfaces
	 * that were already in altsetting 0; besides being unnecessary,
	 * many devices can't handle it.  Instead just reset the host-side
	 * endpoint state.
	 */
	for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_host_config *config = udev->actconfig;
		struct usb_interface *intf = config->interface[i];
		struct usb_interface_descriptor *desc;

		desc = &intf->cur_altsetting->desc;
		if (desc->bAlternateSetting == 0) {
			usb_disable_interface(udev, intf, true);
			usb_enable_interface(udev, intf, true);
			ret = 0;
		} else {
			/* Let the bandwidth allocation function know that this
			 * device has been reset, and it will have to use
			 * alternate setting 0 as the current alternate setting.
			 */
			intf->resetting_device = 1;
			ret = usb_set_interface(udev, desc->bInterfaceNumber,
					desc->bAlternateSetting);
			intf->resetting_device = 0;
		}
		if (ret < 0) {
			dev_err(&udev->dev, "failed to restore interface %d "
				"altsetting %d (error=%d)\n",
				desc->bInterfaceNumber,
				desc->bAlternateSetting,
				ret);
			goto re_enumerate;
		}
	}

done:
	/* Now that the alt settings are re-installed, enable LTM and LPM. */
	usb_unlocked_enable_lpm(udev);
	usb_enable_ltm(udev);
	return 0;
 
re_enumerate:
	/* LPM state doesn't matter when we're about to destroy the device. */
	hub_port_logical_disconnect(parent_hub, port1);
	return -ENODEV;
}

int resume_device_interface(struct usb_device *udev, pm_message_t msg)
{
        int                     status = 0;
        int                     i;
        struct usb_interface    *intf;

        if (udev->state == USB_STATE_NOTATTACHED) {
                status = -ENODEV;
                goto done;
        }
        udev->can_submit = 1;


        /* Resume the device */
        if (udev->state == USB_STATE_SUSPENDED || udev->reset_resume)
                status = usb_resume_device(udev, msg);

        /* Resume the interfaces */
        if (status == 0 && udev->actconfig) {
                for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
                        intf = udev->actconfig->interface[i];
                        usb_resume_interface(udev, intf, msg,
                                        udev->reset_resume);
                }
        }
        usb_mark_last_busy(udev);

 done:
        dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
        if (!status)
                udev->reset_resume = 0;
        return status;
}

