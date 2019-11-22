/*
 * Filename: drivers/usb/core/priority_devconfig.c
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * This file provides functions to  manage the device list of priority USB devices for an usb hub.
 */
#include <linux/gfp.h>
#include <linux/priority_devconfig.h>
#include <linux/slab.h>
#include "hub.h"
#include <linux/completion.h>
#ifdef CONFIG_ARCH_SDP1404
#include <linux/module.h> 
#include <linux/mfd/sdp_micom.h> 
#endif
#undef DEBUG

#ifdef DEBUG
        #define MSG(string, args...) \
 		        printk(KERN_EMERG "%s:%d : " string, __FUNCTION__, __LINE__, ##args)
#else
        #define MSG(string, args...)
#endif

int instant_resume_update_state_disconnected(struct usb_device* udev, usbdev_state state);
product_info * resume_device_match_id (struct usb_device *udev);
struct instant_resume_tree *create_instant_resume_tree(unsigned int busnum);
int instant_resume_traverse_tree(struct instant_resume_tree* head, struct usb_device* udev, product_info *info);
int instant_resume_update_state_connected(struct usb_device* udev, struct instant_resume_tree *head, product_info *info);
int instant_resume_print_tree(struct instant_resume_tree* head);
int resume_device_interface(struct usb_device *udev, pm_message_t msg);
struct instant_resume_tree  *instant_resume_tree_swap (product_info * info,struct usb_device *udev, struct instant_resume_tree *head);
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
void instant_resume_reset_user_device(void);
#endif
extern void wake_khubd(void);
extern struct completion bt_end;
/* 
 * Product Info which are to be used in case of samsung parallel resume feature
 *
 * Update below structure in case of any new device is added in tree.
 */
product_info devconfig_info[MAX_PRIORITY_DEVICES+1]  = {
	{.idVendor =  0x0A5C, .idProduct = 0xBD27, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI COMBO BCM*/
	{.idVendor =  0x0A5C, .idProduct = 0xBD1D, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI ONLY BCM*/
	{.idVendor =  0x0CF3, .idProduct = 0x1022, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI ONLY QCA*/
	{.idVendor =  0x0A5C, .idProduct = 0x4500, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BTHUB BCM*/
	{.idVendor =  0x0A5C, .idProduct = 0x2045, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT_COMBO BCM*/
	{.idVendor =  0x0A5C, .idProduct = 0x4502, .priority = 2, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT1 DEVICE BCM */
	{.idVendor =  0x0A5C, .idProduct = 0x4503, .priority = 3, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT2 DEVICE BCM */
	{.idVendor =  0x0A5C, .idProduct = 0x22BE, .priority = 4, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL},  /* BT3 DEVICE BCM */
};


extern struct instant_resume_control instant_ctrl;

/*
 * return value:
 * -1  	=> not matched
 * 0	=> matched and no dependancy
 * 1-n	=> matched and dependancy index
 *
 */

product_info * resume_device_match_id (struct usb_device *udev) 
{
	int i;
	product_info	*status = NULL;

	for(i = 0; i < MAX_PRIORITY_DEVICES && (devconfig_info[i].idProduct || devconfig_info[i].idVendor); i++) {
		if(udev->descriptor.idProduct == devconfig_info[i].idProduct && \
				udev->descriptor.idVendor == devconfig_info[i].idVendor) {
                        MSG("%s device id matched..%d\n", udev->devpath, i);
			status = &devconfig_info[i];
			//status = devconfig_info[i].dependancy_index;
			udev->priority = devconfig_info[i].priority;
			udev->usb_family = devconfig_info[i].usb_family;
			devconfig_info[i].is_checked = 1;
			break;
		}
		else {
			udev->priority = 0xFF;
                        MSG("%s device id not matched..%d\n", udev->devpath, i);
		}		
	}
	return status;
}

/* Build config tree for instant resume */
struct instant_resume_tree *create_instant_resume_tree(unsigned int busnum)
{
	struct instant_resume_tree *head;

	if(instant_ctrl.active & (0x01 << (busnum-1))) {
		return instant_ctrl.instant_tree[busnum -1];
	}

     	head = kzalloc(sizeof(*head), GFP_KERNEL);
        if (!head)
                return NULL;
	
	head->state = INSTANT_STATE_INIT; 
        spin_lock_init(&head->lock);
		
        INIT_LIST_HEAD(&head->dev_list);
        head->hinfo.busnum = busnum;

        /* flags */
        head->priority = 0;
        head->snapshot = 0;
        head->hub_debounce = 0;
        head->parallel = 0;
	head->num_of_nodes = 0;
        head->priority_count = 0;
	head->will_resume = 0;
        mutex_init(&head->priority_lock);
	instant_ctrl.active |= (0x1 << (busnum-1));
	instant_ctrl.instant_tree[busnum - 1] = head;
	return head;
}


int kthreadfn(void *ptrinfo);

extern int hub_port_reset_resume(resume_args *arg1);
extern void hub_port_enumerate_device(struct usb_hub *hub, int portnum);

struct resume_devnode* instant_resume_create_node(struct usb_device *udev, int priority, struct instant_resume_tree *head)
{
	struct resume_devnode* dev;
	int i;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
        //Adding APIs
//	dev->get_status = hub_port_get_status;
	dev->reset_resume = hub_port_reset_resume;
	dev->enumerate_device = hub_port_enumerate_device;
	dev->args.udev = udev;
	dev->args.parent_portnum = udev->portnum;
	dev->args.parent_hub = NULL;

	dev->state = USBDEV_CONNECTED;
	dev->udev  = udev;
	
	dev->priority = priority;
	dev->is_snapshot = 0;
	dev->is_debounce = 0;
	if(dev->priority == MIN_PRIORITY)
		dev->is_parallel = 0;
	else
    	dev->is_parallel = 1;

	dev->is_hub	 = (udev->maxchild ? 1 : 0);

	dev->busnum = udev->bus->busnum;

        memset(dev->devpath, 0, MAX_DEVPATH_SIZE + 1);
	strncpy(dev->devpath, udev->devpath, MAX_DEVPATH_SIZE);
	
	dev->hub = udev->hub;

	for (i = 0; i < MAX_HUB_PORTS; i++) {
		dev->child[i] = NULL;;
	}
 
        if(udev->parent != NULL){      
	        dev->parent = (udev->parent)->devnode;
                if(dev->parent != NULL){
                        dev->parent->child[udev->portnum-1] = dev;
                } 
        }

	dev->sibling = NULL;

	/* update parent child pointers */
	if(udev->parent != NULL) {
		udev->parent->child[udev->portnum-1] = udev;
		udev->parent->connected_ports |= (1 << (udev->portnum - 1));
	}
	udev->devnode = dev;	
        dev->parent_hub = usb_hub_to_struct_hub(udev->parent); 
        dev->its_portnum = udev->portnum;

	/* Updating the parent devnode of children's devnode */
	for(i = 0; i < MAX_HUB_PORTS; i++){
                if(udev->child[i] != NULL){
                        if(udev->child[i]->devnode){
                                udev->child[i]->devnode->parent = dev;
                        }
                }
        }
	
	/* Updating the children devnode of current devnode */
	for(i = 0; i < MAX_HUB_PORTS; i++){
                if(udev->child[i] != NULL){
                        dev->child[i] = udev->child[i]->devnode;
                }
        }

	dev->head = head;
	dev->vendor_id =  udev->descriptor.idVendor;
    dev->product_id = udev->descriptor.idProduct;
	dev->usb_family = udev->usb_family;
	spin_lock_init(&dev->node_lock);
	/* initialize kthread associated with it */
	dev->kthread_info.threadfn = kthreadfn;
	dev->kthread_info.kname = "instant thread";
	dev->kthread_info.data = (void*)dev;
     

	create_run_kthread(&dev->kthread_info);

	return dev;
}

int is_devconfig_tree_build(void)
{
	int i, flag = 1;
	for(i = 0; i < MAX_PRIORITY_DEVICES; i++) {
		if(devconfig_info[i].is_checked == 0) {
			flag = 0;
			break;
		}
	}
	return flag;
}
int instant_resume_print_tree(struct instant_resume_tree* head);

int is_parental_dependant(struct usb_device *parent_udev, struct usb_device *udev)
{
	unsigned int flag = 0;

	while(udev != NULL) {
		if(udev == parent_udev)
			flag = 1;
		udev = udev->parent;
	}
	return flag;
}

int instant_resume_traverse_tree(struct instant_resume_tree* head, struct usb_device* udev, product_info *info)
{
	struct resume_devnode *dev, *entry;
	struct list_head	*ptr;
	int i = 0, found;
	
	while(udev != NULL) {
		if(udev->is_scanned)
			break;
		dev = instant_resume_create_node(udev, udev->priority, head);
		/* add 1st node */
                if(dev == NULL){
                        break;
                }
            
		if(head->num_of_nodes == 0) {
                        spin_lock(&head->lock);
			list_add_tail(&dev->dev_list, &head->dev_list);
                        spin_unlock(&head->lock);
		}
		else {  /* traverse the list for correct position */
			found = 0;
			list_for_each(ptr, &head->dev_list) {
				entry = list_entry(ptr, struct resume_devnode, dev_list);
				if ((is_parental_dependant(udev, entry->udev)) || (!(is_parental_dependant(entry->udev, udev)) && (udev->priority < entry->priority))) {
                        		spin_lock(&head->lock);
				    	list_add_tail(&dev->dev_list, &entry->dev_list);
        		                spin_unlock(&head->lock);
				    	found = 1;
					break;
				}
		    	}
			if(!found) /* add in list as last member */ {
	                        spin_lock(&head->lock);
				list_add_tail(&dev->dev_list, &head->dev_list);
                        	spin_unlock(&head->lock);
			}
		}
		if(i == 0) { /* device node */
			dev->instant_enable = 1;
			dev->is_checked = 1;
			dev->skip_resume = 1;
			info=resume_device_match_id(udev);
			if(info)
				info->devnode=udev->devnode;

		}
		else if(i == 1) { /* parent device node */
			if((resume_device_match_id (udev)) == NULL)
				dev->is_instant_point = 1; 
		}
		udev->is_scanned = 1;
		head->num_of_nodes++;
		udev = udev->parent;
		i++;
	}
	/* are all devices checked ? */
	if(is_devconfig_tree_build()) {
                MSG("instant tree state : INSTANT_STATE_COLDBOOT\n");
		head->state = INSTANT_STATE_COLDBOOT; 
	}

	return i;
}

int instant_resume_update_state_disconnected(struct usb_device* udev, usbdev_state state)
{
	struct resume_devnode *dev = udev->devnode;
	struct instant_resume_tree *head = NULL;	
	if(dev == NULL) {
                MSG("not a priority device..\n");
		return 0;
	}
	head = instant_ctrl.instant_tree[dev->busnum -1];
	if(head!=NULL){
		MSG("%s device is disconnected.\n", udev->devpath);
		if(dev->priority <= MAX_PRIORITY){
			mutex_lock(&head->priority_lock);
			set_bit(dev->priority, &head->priority_count);
			mutex_unlock(&head->priority_lock);
			if(dev->usb_family == BT_FAMILY)
				set_bit(dev->priority, &instant_ctrl.family_ctrl);
		}
		dev->udev = (struct usb_device*)NULL;
	 	dev->parent_hub = NULL;
		dev->state = USBDEV_DISCONNECTED; 
		if(udev->parent != NULL)
	  		udev->parent->child[udev->portnum-1] = NULL;
		if(dev->is_instant_point){
			dev->head->will_resume = 0;
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
			instant_ctrl.user_port_resume = false;
#endif
		}
	}
	else {
		MSG("%s Instant tree is empty. \n", udev->devpath);
	}
	return 0;
}
struct resume_devnode* instant_resume_find_devnode_by_id(struct instant_resume_tree *head, struct usb_device *udev)
{
        struct resume_devnode *dev = NULL, *entry;
        struct list_head *ptr;

        list_for_each(ptr, &head->dev_list) {
                entry = list_entry(ptr, struct resume_devnode, dev_list);

                if((entry->vendor_id == udev->descriptor.idVendor) && (entry->product_id == udev->descriptor.idProduct)){
                        dev = entry;
			break;
		}
        }
        return dev;
}

struct resume_devnode* instant_resume_find_devnode_by_devpath(struct instant_resume_tree *head, struct usb_device *udev)
{
	struct resume_devnode *dev = NULL, *entry;
	struct list_head *ptr;

       	list_for_each(ptr, &head->dev_list) {
                entry = list_entry(ptr, struct resume_devnode, dev_list);

		if(!strcmp(entry->devpath, udev->devpath)){
			dev = entry;
			break;
		} 
        }
	return dev;
}

void instant_resume_create_or_update_node(struct usb_device* udev, struct instant_resume_tree *head, product_info *info)
{
	struct resume_devnode *dev, *parent_dev;
        int num_of_nodes,i = 0;
        struct usb_device *parent_udev;

	if (!udev || !head)
		return;

	dev = instant_resume_find_devnode_by_id(head, udev);

	if((dev == NULL)) {
                MSG("First time entry is being created for %s\n",dev_name(&udev->dev));

                        /* build config tree */
                        num_of_nodes = instant_resume_traverse_tree(head, udev, info);
                        MSG(" Num of added devices = %d\n",num_of_nodes);
                        /* print list build till now */
                        instant_resume_print_tree(head);
		        return;
	}

        MSG("%s device is Re-connected\n", udev->devpath);
	dev->udev = (struct usb_device*)udev;
	udev->devnode = dev;
	dev->state = USBDEV_CONNECTED; 
        dev->parent_hub = usb_hub_to_struct_hub(udev->parent); 
        for(i = 0; i < MAX_HUB_PORTS; i++){
                if(dev->child[i] != NULL){
                        dev->child[i]->parent_hub = usb_hub_to_struct_hub(udev);
                       
                }
        }

	/* Updating parent field */
        parent_udev = udev->parent;
        parent_dev = dev->parent;
        if ((parent_udev != NULL) && (parent_dev != NULL)) {
		parent_udev->devnode = parent_dev;
		parent_dev->udev = parent_udev;
		parent_dev->state = USBDEV_CONNECTED;
		parent_dev->parent_hub = usb_hub_to_struct_hub(parent_udev->parent);

                if ((udev != NULL) && (dev != NULL)){
			parent_udev->child[udev->portnum - 1] = udev;
			parent_dev->child[udev->portnum - 1] = dev;
		}

                parent_udev = parent_udev->parent;
                parent_dev = parent_dev->parent;
                udev = udev->parent;
                dev = dev->parent;
        }

        return;
}

int instant_resume_print_tree(struct instant_resume_tree* head)
{
	struct list_head *ptr;
    	struct resume_devnode *entry;
	int i;

        MSG("print tree: num_of_nodes = %d\n", head->num_of_nodes);
    	list_for_each(ptr, &head->dev_list) {
        	entry = list_entry(ptr, struct resume_devnode, dev_list);
                MSG("%s: level =%d--> udev = %u",entry->udev->devpath, entry->udev->level ,entry->udev);
		for(i = 0; i < 16; i++ ) {	
			if((entry->udev->connected_ports & (1 << i)))
                                MSG(" : %d child --> devpath = %s ",(i+1), entry->udev->child[i]->devpath);
		}

    	}
        MSG("\n print done");
        return 0;
}

#define PARALLEL_OPERATION
int kthreadfn(void *ptrinfo)
{
	struct k_info *pinfo = ptrinfo;
	struct resume_devnode	*dev = NULL;
        int flag = 0;
        struct resume_devnode *entry;
        struct list_head *ptr;
        struct usb_device *udev;
	struct instant_resume_tree *head;
     

	do {
                
                wait_event(pinfo->waitQ, pinfo->wait_condition_flag||kthread_should_stop());
                dev = (struct resume_devnode*) (pinfo->data);
		flag = 0;
#ifdef PARALLEL_OPERATION
                head = instant_ctrl.instant_tree[dev->busnum -1];
		if((dev->is_instant_point != 1) && (dev->kthread_info.wait_condition_flag)){
                        if(dev->state == USBDEV_DISCONNECTED){
                                if(dev->parent_hub != NULL){
					if((dev->usb_family == WIFI_FAMILY) && (instant_ctrl.family_ctrl != 0)){
						wait_for_completion_timeout(&bt_end, msecs_to_jiffies(WIFI_WAIT_TIMEOUT));
						INIT_COMPLETION(bt_end);
					}
                                        hub_port_enumerate_device(dev->parent_hub , dev->its_portnum);
				}
                                mutex_lock(&head->priority_lock);
                                clear_bit(dev->priority, &head->priority_count);
                                mutex_unlock(&head->priority_lock);
				clear_bit(dev->priority, &instant_ctrl.family_ctrl);
				
				/* Send complete event after reset completion of BT hub family */
				if((instant_ctrl.family_ctrl == 0) && (dev->usb_family == BT_FAMILY))
					complete(&bt_end);                                   
                        }else{
                                udev = dev->udev;
                                if(udev != NULL){
					if((dev->usb_family == WIFI_FAMILY) && (instant_ctrl.family_ctrl != 0)){
                                                wait_for_completion_timeout(&bt_end, msecs_to_jiffies(WIFI_WAIT_TIMEOUT));
                                                INIT_COMPLETION(bt_end);
                                        }
                                        resume_device_interface(udev, PMSG_RESUME);   
				}
                                mutex_lock(&head->priority_lock); 
                                clear_bit(dev->priority, &head->priority_count);		
                                mutex_unlock(&head->priority_lock);
				clear_bit(dev->priority, &instant_ctrl.family_ctrl);

				/* Send complete event after reset completion of BT hub family */
				if((instant_ctrl.family_ctrl == 0) && (dev->usb_family == BT_FAMILY))
					complete(&bt_end);
                        }
                }else{
                        list_for_each(ptr, &head->dev_list) {
                        /* entry is container of list_head here */
                                entry = list_entry(ptr, struct resume_devnode, dev_list);
                                if (entry == dev) {
                                        flag = 1;
                                        continue;
                                }
                                if (flag == 0)
                                        continue;
                                else {
                                	udev = entry->udev;
                                	if(entry->state == USBDEV_DISCONNECTED){
                                        	if(!entry->is_parallel){    
                                                	if(entry->parent_hub != NULL)
                                                        	hub_port_enumerate_device(entry->parent_hub , entry->its_portnum);
                                                        mutex_lock(&head->priority_lock);
                                                        clear_bit(entry->priority, &head->priority_count);
                                                        mutex_unlock(&head->priority_lock);
							clear_bit(entry->priority, &instant_ctrl.family_ctrl);

							/* Send complete event after reset completion of BT hub family */
							if((instant_ctrl.family_ctrl == 0) && (entry->usb_family == BT_FAMILY))
								complete(&bt_end);
                                                }else{
                                                	entry->kthread_info.wait_condition_flag = 1;
                                                        wake_up(&entry->kthread_info.waitQ);
                                                }

                                        }else{
                                       		if(!entry->is_parallel){ 
                                                	if(udev != NULL)
                                                        	resume_device_interface(udev, PMSG_RESUME);
                                                        mutex_lock(&head->priority_lock);
                                                        clear_bit(entry->priority, &head->priority_count);
                                                        mutex_unlock(&head->priority_lock);
							clear_bit(entry->priority,&instant_ctrl.family_ctrl);
								
							/* Send complete event after reset completion of BT hub family */
							if((instant_ctrl.family_ctrl == 0) && (entry->usb_family == BT_FAMILY))
								complete(&bt_end);
                                                }else{
                                                	entry->kthread_info.wait_condition_flag = 1;
                                                        wake_up(&entry->kthread_info.waitQ);
                                                }
                                        
                                        }
                                        
                                }

                      }
              }             


#endif
#ifdef SERIAL_OPERATION
	head = instant_ctrl.instant_tree[dev->busnum -1];
        list_for_each(ptr, &head->dev_list) {
                /* entry is container of list_head here */
                entry = list_entry(ptr, struct resume_devnode, dev_list);
                if (entry == dev) {
                        flag = 1;
                        continue;
                }
                if (flag == 0)
                        continue;
                else {
                        if((entry->priority >= MIN_PRIORITY) && (entry->priority <= MAX_PRIORITY)){
                                udev = entry->udev;
                                if(entry->state == USBDEV_DISCONNECTED){
                               
                                        hub_port_enumerate_device(entry->parent_hub , entry->its_portnum); 
                                }else{
                                        resume_device_interface(udev, PMSG_RESUME);
                                }
                        }      
                }
        }
#endif

        pinfo->wait_condition_flag = 0;
        if(head->priority_count == 0){
		clear_bit(dev->busnum, &instant_ctrl.is_defer_khubd);
		if(instant_ctrl.is_defer_khubd == 0){
			INIT_COMPLETION(bt_end);
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
			/* Going to reset user port devices */
			instant_resume_reset_user_device();
#endif
			/* Enabling khubd to run */
			instant_ctrl.off_khubd = false;
	                wake_khubd();
                }
        }
        
        } while (!pinfo->wait_condition_flag && !kthread_should_stop());
        return 1;
}

/**
 * usb_instant_resume_cleanup() - cleanup function for instant resume of usb devices
 *
 * This seraches for all populated instant resume tree lists of device nodes and
 * stop each one's instant resume kernel threads, remove each device node from the list,
 * free the memory to container structure variable. Lists' head node and its container operated last.
 *
 */
void usb_instant_resume_cleanup(void)
{
        struct resume_devnode *entry;
        struct list_head *ptr=NULL, *n;
        struct instant_resume_tree *head;
        int i;
        for(i = 0; i < MAX_INSTANT_TREES; i++)
        {
                head = instant_ctrl.instant_tree[i];
		//NULL check
                if(head != NULL)
                {
			if ( list_empty(&head->dev_list))
				continue;
			
                        list_for_each_safe(ptr,n, &head->dev_list)
                        {
                                entry = list_entry(ptr, struct resume_devnode, dev_list);
                                if(entry != NULL)
                                {
					//NULL check
                                        stop_kthread(&entry->kthread_info);	
					//NULL check
                                	if(&entry->dev_list != NULL)
	                                	list_del_init(&entry->dev_list);
                                   	//NULL check
					if(entry != NULL)
						kfree(entry);
                                }
                        }
			//NULL check
                        if(&head->dev_list != NULL)
                        	list_del_init(&head->dev_list);
			//NULL check
			if(head != NULL){
                        	kfree(head);
                        	head=NULL;
				instant_ctrl.instant_tree[i]=NULL;
			}
                }
        }
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
	/* Stopping the user port thread */
	stop_kthread(&instant_ctrl.user_port_thread_info);
#endif
        return;
}

void instant_resume_busnum_tree_update(struct instant_resume_tree *head)
{
	struct list_head *ptr;
	struct resume_devnode *entry;
	if(head != NULL)
	{
		list_for_each(ptr, &head->dev_list){
			entry = list_entry(ptr, struct resume_devnode, dev_list);
			if(entry != NULL)
						entry->busnum = head->hinfo.busnum;
		}
	}
}

struct instant_resume_tree *instant_resume_tree_swap (product_info *info,struct usb_device *udev, struct instant_resume_tree *head)
{
	struct instant_resume_tree *tmp,*head_2;
	int tmp_bus;
	struct resume_devnode *devnode;
	devnode=(struct resume_devnode *)info->devnode;
	head_2=devnode->head;

	if(udev->bus->busnum!=devnode->busnum) {
		if(head!=NULL) {
			tmp=head;
			head=head_2;
			head_2=tmp;
			tmp_bus=head->hinfo.busnum;
			head->hinfo.busnum=udev->bus->busnum;
			head_2->hinfo.busnum=tmp_bus;
		}
		else {
			head=head_2;
			head_2=NULL;
			head->hinfo.busnum=udev->bus->busnum;
		}
		
		instant_ctrl.instant_tree[devnode->busnum-1]=head_2;
		instant_ctrl.instant_tree[udev->bus->busnum-1]=head;
		
		instant_resume_busnum_tree_update(head);
		instant_resume_busnum_tree_update(head_2);
	}


	return head;
}
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
void instant_resume_reset_user_device(void)
{
	struct list_head *ptr = NULL, *n;
	struct usb_device *udev;

	if(!wait_for_completion_timeout(&instant_ctrl.user_dev, msecs_to_jiffies(USER_DEV_TIMEOUT))) {
		printk(KERN_EMERG "\n timeout: waiting PM completion\n");
	}
	INIT_COMPLETION(instant_ctrl.user_dev);
	/* reset-resume other usb devices */
	/* print dev path of all devices in list */
	list_for_each_safe(ptr, n, &instant_ctrl.other_dev_list) {
	/* udev is container of list_head here */			
		udev = list_entry(ptr, struct usb_device, other_dev_list);
	        if(udev) {
	        	resume_device_interface(udev, PMSG_RESUME);
	        	if(&udev->other_dev_list != NULL)
	        		list_del(&udev->other_dev_list);
	    	}else
	        	printk(KERN_EMERG"\ndev ptr = NULL\n");
	}

}

void user_port_reset_thread(void *ptrinfo)
{
	struct k_info *pinfo = ptrinfo;
	do {
		wait_event(pinfo->waitQ, pinfo->wait_condition_flag || kthread_should_stop());
		/* khubd put to sleep */
		instant_ctrl.off_khubd = true;
		/* user port reset */
		instant_resume_reset_user_device();
		/* waking khubd */ 
		instant_ctrl.off_khubd = false;
		wake_khubd();
		
		pinfo->wait_condition_flag = 0;
	} while (!pinfo->wait_condition_flag && !kthread_should_stop());
}
#endif

#ifdef CONFIG_ARCH_SDP1404 
static void *_resolve_symbol(char *name)
{
        void *ret = 0;
        const struct kernel_symbol *sym;

        mutex_lock(&module_mutex);
        sym = find_symbol(name, NULL, NULL, 1, true);
        mutex_unlock(&module_mutex);

        if (sym) {
                ret = (void *)sym->value;
        }

        return ret;
}

int get_tv_chip_data(char *fn_str)
{
        int val = 0;
        pfn_tztv_kf_drv_get_data fn_num;
        fn_num  = (pfn_tztv_kf_drv_get_data)_resolve_symbol(fn_str);
        if(fn_num){
                fn_num(&val);
        }
        return val;
}
EXPORT_SYMBOL_GPL(get_tv_chip_data);

void enable_wifi_reset()
{

                        enum sdp_sys_info tv_side_main_chip;
                        tv_side_main_chip = get_tv_chip_data("tztv_config_chip_type_tv");
                        if ((  tv_side_main_chip  == SYSTEM_INFO_NOVA_UHD_7000_14_TV_SIDE ) ||
                                (tv_side_main_chip == SYSTEM_INFO_GOLF_UHD_14_TV_SIDE ) ||
                                ( tv_side_main_chip == SYSTEM_INFO_NOVA_UHD_14_TV_SIDE ) ||     //Need to check for this device
                                ( tv_side_main_chip == SYSTEM_INFO_GOLF_UHD_8500_14_TV_SIDE ))   //Need to check for this device
                        {
                                char cmd = WIFI_RESET_CONTROL, ack = WIFI_RESET_CONTROL_ACK, data[2] = {0, 0};
                                int len = 1;
                                int ret,cnt = 0;
                                data[0] = 2;

                                do
                                {
                                        ret = sdp_micom_send_cmd_ack(cmd, ack, data, len);
                                        if( ret )
                                        {
                                                msleep( 50 );
                                        }
                                        cnt++;
                                } while( ret && (cnt < 2) );
                        }




}
EXPORT_SYMBOL_GPL(enable_wifi_reset);
#endif
