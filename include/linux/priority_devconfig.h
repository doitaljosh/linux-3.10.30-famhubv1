/*
 *
 * Filename: include/linux/priority_devconfig.h
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * Priority usb devices tree management head file
 */

#ifndef __INSTANT_RESUME_H
#define __INSTANT_RESUME_H

#ifndef EXTERN
#define EXTERN extern
#endif

#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/kobject.h>
#include <linux/kthreadapi.h>
#include <mach/soc.h>
#include <linux/completion.h>

#define MAX_DEVCONFIG		5	/* defined in config.c */
#define MAX_DEVPATH_SIZE	16
#define MAX_PRIORITY_DEVICES	8
#define MAX_HUB_PORTS		8
#define MAX_INSTANT_TREES	32
#define MIN_PRIORITY            1
#define MAX_PRIORITY            5
#define BT_FAMILY               0X01
#define WIFI_FAMILY             0X02
#define WIFI_WAIT_TIMEOUT       10000
#define USER_DEV_TIMEOUT	5000
#define IS_HAWKP		soc_is_sdp1404()
#define IS_HAWKM		soc_is_sdp1406()

#ifdef CONFIG_ARCH_SDP1404 
#define WIFI_RESET_CONTROL 	0xB3 
#define WIFI_RESET_CONTROL_ACK 	0xB3
#endif

typedef struct _host_controller_info {
	unsigned int	busnum;
} host_controller_info;

typedef struct _product_info {
	unsigned int  	idVendor;
	unsigned int 	idProduct;
	unsigned int 	priority;
	unsigned int	dependancy_index;	/* this is to check user entered data error */
	unsigned int	is_checked:1;	/* this is to check device checking */
	unsigned int    usb_family;
	void		*devnode;
} product_info;

typedef enum {
	INSTANT_STATE_INIT,
	INSTANT_STATE_COLDBOOT,
	INSTANT_STATE_SUSPEND,
	INSTANT_STATE_RESUME,
	INSTANT_STATE_INVALID,
}INSTANT_STATE;

typedef enum {
	USBDEV_DISCONNECTED,
	USBDEV_CONNECTED,
	USBDEV_POWERED
}usbdev_state;

typedef struct _resume_args {
	struct usb_device 	*udev;
	struct usb_hub 		*parent_hub;
	int	 		parent_portnum;
} resume_args;

struct instant_resume_tree {
        spinlock_t              lock;
        struct mutex            priority_lock;
        INSTANT_STATE           state;
        struct list_head        dev_list;
        unsigned int            busnum;
        unsigned int            num_of_nodes;
        unsigned long           priority_count;
        unsigned int            will_resume;

	host_controller_info		hinfo;

        /* flags */
        unsigned int            priority:1;
        unsigned int            snapshot:1;
        unsigned int            hub_debounce:1;
        unsigned int            parallel:1;
};

/*
 * 1. function for reset-resume or enumeration
 * 2. arguments to function
 * 3. pointer to parent node
 * 4. state of devnode
 * 5. udev pointer of devnode
 * 6. next sibling pointer of devnode
 * 7. priority of devnode
 * 8. is_hub ?
 * 9. resume_devnode children
 * 10. list pointer
 * 11. parallel thread attached
 * 12. flags
 * 13. busnum
 * 14. devpath
 * 15. kobj
 * 16. product information
 */

struct resume_devnode {
	int (*reset_resume)	(resume_args *arg1);
	void (*enumerate_device)	(struct usb_hub *hub, int portnum);
	int (*get_status)	(resume_args *arg1);
	resume_args		args;

	struct resume_devnode	*parent;

	usbdev_state		state;
	struct usb_device	*udev;
	
	struct resume_devnode	*sibling;
	unsigned int 		priority;
	struct usb_hub		*hub;
	struct resume_devnode	*child[MAX_HUB_PORTS];
	struct list_head	dev_list;		
 
	unsigned int		is_snapshot:1;
	unsigned int		is_debounce:1;
	unsigned int		is_parallel:1;
	unsigned int		is_hub:1;
	unsigned int		instant_enable:1;
	unsigned int		is_checked:1;
	unsigned int		is_instant_point:1;
	unsigned int		skip_resume:1;
	int 			busnum;
	char 			devpath[MAX_DEVPATH_SIZE + 1];
	struct kobject 		kobj;
	product_info		*info;
        struct usb_hub          *parent_hub;
        int                     its_portnum;
	unsigned int            vendor_id;
        unsigned int            product_id;
	unsigned int            usb_family;
	struct instant_resume_tree *head;
	struct k_info 		kthread_info;
	spinlock_t              node_lock;
};

enum {
	USBDEV_RESUME_DISABLE,
	USBDEV_RESUME_ENABLE
};

struct instant_resume_control {
        unsigned int                    active;
	volatile long unsigned int      is_defer_khubd;
	bool			 	off_khubd;
	volatile long unsigned int      family_ctrl;
        struct instant_resume_tree      *instant_tree[MAX_INSTANT_TREES];
	
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
	struct completion 		user_dev;
	/* spinlock_t			list_lock;*/
	struct list_head		other_dev_list;
	struct k_info			user_port_thread_info;
	volatile long unsigned int	instant_point_fail_count;
	bool                            user_port_resume;	
#endif
};

EXTERN int instant_resume_update_state_disconnected(struct usb_device* udev, usbdev_state state);
EXTERN product_info * resume_device_match_id (struct usb_device *udev);
EXTERN struct instant_resume_tree *create_instant_resume_tree(unsigned int busnum);
EXTERN int instant_resume_traverse_tree(struct instant_resume_tree* head, struct usb_device* udev, product_info *info);
EXTERN void instant_resume_create_or_update_node(struct usb_device* udev, struct instant_resume_tree *head, product_info *info);
EXTERN int instant_resume_print_tree(struct instant_resume_tree* head);
EXTERN void usb_instant_resume_cleanup(void);
EXTERN struct instant_resume_tree * instant_resume_tree_swap(product_info *info, struct usb_device *udev, struct instant_resume_tree *head);

#ifdef CONFIG_ARCH_SDP1404
typedef int (*pfn_tztv_kf_drv_get_data)(int *);
void enable_wifi_reset(void);

enum sdp_sys_info {
	SYSTEM_INFO_FOX_LED_13_TV_SIDE          = 0, // FOXP FHD Model 
	SYSTEM_INFO_FOX_UHD_FPGA_13_TV_SIDE     = 1, // FOXP UHD FPGA FOX_TV 
	SYSTEM_INFO_FOX_UHD_FOXN_13_TV_SIDE     = 2,  // FOXP UHD FOXN 
	SYSTEM_INFO_FOX_NTV_FPGA_13_TV_SIDE     = 3, // FOXP NTV FPGA FOX_OLED 
	SYSTEM_INFO_FOX_NTV_FOXN_13_TV_SIDE     = 4, // FOXP NTV FOXN 
	SYSTEM_INFO_GOLF_UHD_14_TV_SIDE         = 5, // GOLFP UHD ONE CONNECTION (9K) GOLF_9K 
	SYSTEM_INFO_GOLF_UHD_8500_14_TV_SIDE    = 6,    // GOLFP UHD ONE CONNECTION READY(8.5K) GOLF_8_5K 
	SYSTEM_INFO_GOLF_NTV_14_TV_SIDE         = 7, // GOLFP NTV ONE CONNECTION X 
	SYSTEM_INFO_GOLF_FHD_14_TV_SIDE         = 8, // GOLFP FHD X 
	SYSTEM_INFO_NOVA_UHD_14_TV_SIDE         = 9, // NT14U UHD ONE CONNECTION READY(8.5K) GOLF_8_5K 
	SYSTEM_INFO_NOVA_UHD_7000_14_TV_SIDE    = 10,   // NT14U UHD ONE CONNECTION READY(7K) GOLF_7K 
	SYSTEM_INFO_HAWK_UHD_15_TV_SIDE     = 11,   // HAWKP UHD ONE CONNECTION   HAWK_9K 
	SYSTEM_INFO_HAWK_NTV_15_TV_SIDE     = 12, // HAWKP NTV 
	SYSTEM_INFO_HAWKM_UHD7K_TV_SIDE     = 13, // HAWKM UHD (7K) X 
	SYSTEM_INFO_HAWKM_UHD6K_TV_SIDE     = 14,   // HAWKM UHD (6K) X 
	SYSTEM_INFO_HAWKM_FHD_TV_SIDE     = 15 // HAWKM FHD  
};
#endif
#endif
