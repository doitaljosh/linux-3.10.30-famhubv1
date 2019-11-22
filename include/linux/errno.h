#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

#include <uapi/linux/errno.h>

#ifdef __KERNEL__

/*
 * These should never be seen by user programs.  To return one of ERESTART*
 * codes, signal_pending() MUST be set.  Note that ptrace can observe these
 * at syscall exit tracing, but they will never be left for the debugged user
 * process to see.
 */
#define ERESTARTSYS	512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514	/* restart if no handler.. */
#define ENOIOCTLCMD	515	/* No ioctl command */
#define ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
#define EPROBE_DEFER	517	/* Driver requests probe retry */
#define EOPENSTALE	518	/* open found a stale dentry */

/* Defined for the NFSv3 protocol */
#define EBADHANDLE	521	/* Illegal NFS file handle */
#define ENOTSYNC	522	/* Update synchronization mismatch */
#define EBADCOOKIE	523	/* Cookie is stale */
#define ENOTSUPP	524	/* Operation is not supported */
#define ETOOSMALL	525	/* Buffer or request is too small */
#define ESERVERFAULT	526	/* An untranslatable error occurred */
#define EBADTYPE	527	/* Type not supported by server */
#define EJUKEBOX	528	/* Request initiated, but will not complete before timeout */
#define EIOCBQUEUED	529	/* iocb queued, will get completion event */


/*
 *  * SAMSUNG USB PATCH, by ksfree.kim
 *   */
#define SAMSUNG_PATCH_WITH_USB_HOTPLUG          // patch for usb hotplug
#define SAMSUNG_PATCH_WITH_USB_HOTPLUG_MREADER  // patch for usb multicard reader
#define SAMSUNG_PATCH_WITH_USB_ENHANCEMENT      // stable patch for enhanced speed  and compatibility
#define SAMSUNG_PATCH_WITH_STORAGE_GADGET_PARTITION_SUPPORT             // patch for partition media plug in/out support
#define SAMSUNG_PATCH_WITH_NEW_xHCI_API_FOR_BUGGY_DEVICE                // patch adds new xHCI API for supporting address device command with BSR=1 flag set.
#define SAMSUNG_PATCH_WITH_USB_HID_DISCONNECT_BUGFIX                    // patch fixes hid disconnect issues at suspend and manual disconnect time
#if defined(CONFIG_ARCH_SDP1406) //HawkM only
#define SAMSUNG_PATCH_OHCI_HANG_RECOVERY_DURING_KILL_URB              // patch fixes the OHCI related suspend hang issue in hawk-m FHD added by aman.deep
#define SAMSUNG_PATCH_TASK_AFFINITY_FOR_PREVENT_OHCI_HANG
#endif //HawkM Only
#define SAMSUNG_PATCH_RMB_WMB_AT_UNLINK                                   //Patch to add rmb and wmb at unlinking of urb
#if defined(CONFIG_ARCH_SDP1406) || defined(CONFIG_ARCH_SDP1404)
#define SAMSUNG_USB_FULL_SPEED_BT_MODIFY_GIVEBACK_URB            //Patch to divide some of operations of hcd_giveback_urb to another function and put that function under a lock.
#endif
//#define SAMSUNG_PATCH_WITH_USB_XHCI_BUGFIX    // BugFIX patch for xHCI
//#define SAMSUNG_PATCH_WITH_USB_XHCI_ADD_DYNAMIC_RING_BUFFER           // Add Dynamic RingBuffer patch for xHCI, This patch has dependance for "SAMSUNG_PATCH_WITH_USB_XHCI_BUGFIX"
//#define SAMSUNG_PATCH_WITH_USB_XHCI_CODE_CLEANUP      // Add code cleanup and debug patch for xHCI, This patch has dependance for "SAMSUNG_PATCH_WITH_USB_XHCI_ADD_DYNAMIC_RING_BUFFER"


//#define SAMSUNG_PATCH_WITH_USB_TEMP     // patch for usb compatibility, but this patch need to verify for the each linux version.
//#define SAMSUNG_PATCH_WITH_USB_NOTCHECK_SPEED         //Don't check high-speed for invalid speed info(bcdUSB).
//#define SAMSUNG_PATCH_WITH_MOIP_HOTPLUG
//#define SAMSUNG_PATCH_WITH_HUB_BUGFIX                 // patch for some hubs malfunction
//#define SAMSUNG_PATCH_WITH_NOT_SUPPORTED_DEVICE       //Alarm of supported usb device (USB 2.0 certification)
//#define KKS_DEBUG     printk
//#define PACKET_PORT_SUPPORT
#define KKS_DEBUG(f,a...)
#define SAMSUNG_USB_BTWIFI_RESET_WAIT			//Patch to wait in a loop till WiFi, BTHUB or hub 1-1 ready for port-reset.
#if defined(CONFIG_SAMSUNG_USB_PARALLEL_RESUME)
#define PARALLEL_RESET_RESUME_USER_PORT_DEVICES         //Patch to enable the reset resume of usb devices connected on user port.
#endif

#endif
#endif
