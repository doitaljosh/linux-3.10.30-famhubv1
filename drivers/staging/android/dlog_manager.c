#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h> 

#define DLOG_MGR_BUFFER_SIZE		PAGE_SIZE
#define INDEX_DLOG_MGR_COMMAND		10000
#define INDEX_DLOG_MGR_ERROR_DEFINE	10
#define DLOG_MGR_VERSON			1
#define DLOG_MGR_TAG_NAME_SIZE		100

#define DLOG_FILTER_SYSTEM_INDEX 	0
#define DLOG_FILTER_TURN_OFF		0
#define DLOG_FILTER_TURN_ON			1

#define DEBUGGER 0

#define LOG_DEBUG(x...) \
	do { \
		if (DEBUGGER) \
			pr_info(x); \
	} while (0)

enum dlog_mgr_command {
	COMMAND_REGISTER_DLOG_MGR_TAG = INDEX_DLOG_MGR_COMMAND,
	COMMAND_TURN_ON_OFF_DLOG_MGR_TAG,
	COMMAND_GET_TAG_NAME,
	COMMAND_SET_CONTROL_BY_UTIL,
};

enum dlog_mgr_error_define {
	DLOG_MGR_OK = 0,
	DLOG_MGR_ERROR_COPY_FROM_USER = INDEX_DLOG_MGR_ERROR_DEFINE,
	DLOG_MGR_ERROR_COPY_TO_USER,
	DLOG_MGR_NO_TAG_NAME,
	DLOG_MGR_NO_MEMORY_SPACE,
	DLOG_MGR_INVALID_ARG,
	DLOG_MGR_FAIL_INSERT_PAGE,
	DLOG_MGR_FAIL_ALLOC_PAGE,
	DLOG_MGR_FAIL_TAG_NAME_SIZE,
	DLOG_MGR_ERROR_DLOG_DB_NULL,
	DLOG_MGR_ERROR_DLOG_DB_FULL,
};

struct ioctl_command {
	char 	tag[DLOG_MGR_TAG_NAME_SIZE];
	int	arg;
};

struct dlog_mgr_buffer_info {
	char*	addr;
	char*	tag_name[PAGE_SIZE];
	int 	index;
};

struct dlog_util{
	struct list_head dlog_tag_items;
};

struct dlog_tag_item
{
	struct list_head entry;
	int offset;
};

DEFINE_MUTEX(dlog_mgr_lock);
static struct page * dlog_mgr_page;
static struct dlog_mgr_buffer_info dlog_mgr_db;

static int add_dlog_tag_to_db( char * tag_name, int arg )
{
	int size = strlen(tag_name) + 1;
	int ret = 0;
	if( size > DLOG_MGR_TAG_NAME_SIZE )
		return -DLOG_MGR_FAIL_TAG_NAME_SIZE;

	dlog_mgr_db.tag_name[dlog_mgr_db.index] = kzalloc(size, GFP_KERNEL);
	if(dlog_mgr_db.tag_name[dlog_mgr_db.index] == NULL)
		return -ENOMEM;
	strncpy(dlog_mgr_db.tag_name[dlog_mgr_db.index], tag_name, size);
	dlog_mgr_db.addr[dlog_mgr_db.index] = (char)arg;
	ret = dlog_mgr_db.index;
	LOG_DEBUG("[dlog_mgr][%s:%d][%s:%d]---[tag:%s][%d]--index[%d]\n", 
			__FUNCTION__, __LINE__, current->comm, current->pid, 
			dlog_mgr_db.tag_name[dlog_mgr_db.index], dlog_mgr_db.addr[dlog_mgr_db.index], 
			dlog_mgr_db.index);
	dlog_mgr_db.index++;
	return ret;
}

static long command_register_dlog_mgr_tag(unsigned long arg)
{
	int ret = 0;
	void __user *ubuf = (void __user *)arg;
	struct ioctl_command tag_cmd;

	if(copy_from_user(&tag_cmd, ubuf, sizeof(tag_cmd)))
		return -DLOG_MGR_ERROR_COPY_FROM_USER;		

	mutex_lock(&dlog_mgr_lock);
	if( dlog_mgr_db.index >= DLOG_MGR_BUFFER_SIZE ) {
		ret = -DLOG_MGR_ERROR_DLOG_DB_FULL;
		goto error;
	}
		
	for ( ret = 0 ; ret < dlog_mgr_db.index ; ret++ ) {
		if(dlog_mgr_db.tag_name[ret] == NULL) {
			ret = -DLOG_MGR_ERROR_DLOG_DB_NULL;
			goto error;
		}

		if(strcmp(dlog_mgr_db.tag_name[ret], tag_cmd.tag) == 0)
			break;
	}

	if( ret >= dlog_mgr_db.index )
	{
		tag_cmd.tag[DLOG_MGR_TAG_NAME_SIZE-1] = '\0';
		ret = add_dlog_tag_to_db( tag_cmd.tag, tag_cmd.arg );
	}	
error:
	mutex_unlock(&dlog_mgr_lock);
	return ret;
}

static long command_tag_turn_on_off(unsigned long arg)
{
	void __user *ubuf = (void __user *)arg;
	struct ioctl_command tag_cmd;
	int ret = 0;
	if( copy_from_user(&tag_cmd, ubuf, sizeof(tag_cmd)) )
		return -DLOG_MGR_ERROR_COPY_FROM_USER;

	mutex_lock(&dlog_mgr_lock);
	for ( ret = 0 ; ret < dlog_mgr_db.index ; ret++ ) {
		if(strcmp(dlog_mgr_db.tag_name[ret], tag_cmd.tag) == 0) {
			dlog_mgr_db.addr[ret] = (char)tag_cmd.arg;
			LOG_DEBUG("[dlog_mgr][%s:%d][%s:%d]---[tag:%s][%d]--requested[tags:%s][%d]\n", __FUNCTION__, __LINE__,
                                                current->comm, current->pid, dlog_mgr_db.tag_name[ret], dlog_mgr_db.addr[ret], 
						tag_cmd.tag, tag_cmd.arg);
			break;
		}
	}
	if( ret >= dlog_mgr_db.index ) {
		tag_cmd.tag[DLOG_MGR_TAG_NAME_SIZE-1] = '\0';
		ret = add_dlog_tag_to_db(tag_cmd.tag, tag_cmd.arg);	
	}
	mutex_unlock(&dlog_mgr_lock);
	return ret;
}

static int dlog_mgr_open(struct inode *nodp, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static long command_get_tag_name(unsigned long arg)
{
	void __user *ubuf = (void __user *)arg;
	struct ioctl_command tag_cmd;
	int ret = 0;
        if( copy_from_user(&tag_cmd, ubuf, sizeof(tag_cmd)) )
                return -DLOG_MGR_ERROR_COPY_FROM_USER;

	if( tag_cmd.arg >= DLOG_MGR_BUFFER_SIZE ) 
		return -DLOG_MGR_INVALID_ARG;

	mutex_lock(&dlog_mgr_lock);
	if( dlog_mgr_db.tag_name[tag_cmd.arg] != NULL ) {
		strncpy(tag_cmd.tag, dlog_mgr_db.tag_name[tag_cmd.arg], DLOG_MGR_TAG_NAME_SIZE-1);
		tag_cmd.arg = dlog_mgr_db.addr[tag_cmd.arg];
		if(copy_to_user(ubuf, &tag_cmd, sizeof(tag_cmd)))
			ret = -DLOG_MGR_ERROR_COPY_TO_USER;
	} else 
		ret = -DLOG_MGR_NO_TAG_NAME;
	mutex_unlock(&dlog_mgr_lock);
	return ret;
}

static long dlog_mgr_tag_control_by_dlogutil(struct file* file, unsigned long arg)
{
			int ret = 0;
			struct dlog_util* util = (struct dlog_util*)file->private_data;
			if(util == NULL) {
				util = kzalloc(sizeof(*util), GFP_KERNEL);
				if(util == NULL)
					return -ENOMEM;
				INIT_LIST_HEAD(&util->dlog_tag_items);
				file->private_data = util;
			} 
			
			ret = command_tag_turn_on_off(arg);
			if(ret > -1 && ret <= DLOG_MGR_BUFFER_SIZE) {
				struct dlog_tag_item* item = kzalloc(sizeof(*item), GFP_KERNEL);
				if(item == NULL)
					return -ENOMEM;
				item->offset = ret;
				list_add_tail(&item->entry,&util->dlog_tag_items);	
			}

			return ret;
}

static long dlog_mgr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	
	switch (cmd) {
		case COMMAND_REGISTER_DLOG_MGR_TAG:
			ret = command_register_dlog_mgr_tag(arg);
			break;
		case COMMAND_TURN_ON_OFF_DLOG_MGR_TAG:
			ret = command_tag_turn_on_off(arg);
			break;
		case COMMAND_GET_TAG_NAME:
			ret = command_get_tag_name(arg);
			break;
		case COMMAND_SET_CONTROL_BY_UTIL:
			ret = dlog_mgr_tag_control_by_dlogutil(file, arg);		
			break;
		
		default:
			break;
	}
	return ret;
}

int dlog_mgr_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;

	vma->vm_flags = vma->vm_flags & ~VM_MAYWRITE;

	LOG_DEBUG("[dlog_mgr] vma request size(0x%x - 0x%x) --> %d\n" ,
			(unsigned int)vma->vm_start , (unsigned int)vma->vm_end , (int)(vma->vm_end - vma->vm_start));

	if((vma->vm_end - vma->vm_start) != DLOG_MGR_BUFFER_SIZE ) {
		ret = -DLOG_MGR_INVALID_ARG;
		goto error;
	} 

	ret = vm_insert_page(vma, vma->vm_start, dlog_mgr_page);
	if (ret) {
		ret = -DLOG_MGR_FAIL_INSERT_PAGE;
		goto error;
	}
	return 0; 
error:
	return ret; 
}

int dlog_mgr_release(struct inode *nodp, struct file *filp)
{
	int ret =0; 
	struct dlog_util* util = filp->private_data;
	struct dlog_tag_item* w;
	if(util != NULL) {
			LOG_DEBUG("[dlog_mgr][release][%s:%d][%s:%d]---\n", __FUNCTION__, __LINE__,
					current->comm, current->pid);

			while (!list_empty(&util->dlog_tag_items)) {  
				w = list_first_entry(&util->dlog_tag_items, struct dlog_tag_item, entry);
				BUG_ON(w == NULL);  
				list_del(&w->entry);
				if(w->offset > -1 && w->offset <= DLOG_MGR_BUFFER_SIZE) {
					LOG_DEBUG("[dlog_mgr][release][%s:%d][%s:%d]---[offset:%d][tag:%s][%d]\n", __FUNCTION__, __LINE__,
							current->comm, current->pid,w->offset, dlog_mgr_db.tag_name[w->offset], dlog_mgr_db.addr[w->offset]);

					mutex_lock(&dlog_mgr_lock);
					if(w->offset == DLOG_FILTER_SYSTEM_INDEX) {
						dlog_mgr_db.addr[w->offset] = (char) DLOG_FILTER_TURN_ON;
					} else {
						dlog_mgr_db.addr[w->offset] = (char) DLOG_FILTER_TURN_OFF;
					}		
					mutex_unlock(&dlog_mgr_lock);
				}
				kfree(w); 
			}
		kfree(util);
	}
	return ret;
}
int init_dlog_mgr_mmap(void)
{   
	memset(&dlog_mgr_db, 0, sizeof(dlog_mgr_db));
	dlog_mgr_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if ( !dlog_mgr_page ) 
		return -DLOG_MGR_FAIL_ALLOC_PAGE;
	dlog_mgr_db.addr = (char *)page_address(dlog_mgr_page);
	add_dlog_tag_to_db("TIZEN_LOGGING_FRAMEWORK_DLOG_FILTER_ENABLE", 1);
	return 0;
}

static const struct file_operations dlog_mgr_fops = {
	.owner = THIS_MODULE,
	.open = dlog_mgr_open,
	.unlocked_ioctl = dlog_mgr_ioctl,
	.mmap = dlog_mgr_mmap,
	.release = dlog_mgr_release,
};

static struct miscdevice dlog_mgr_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dlog_mgr",
	.mode = 0666,
	.fops = &dlog_mgr_fops
};

static int __init dlog_mgr_init(void)
{
	int ret;
	LOG_DEBUG("##[dlog_mgr] dlogd init:ver(%d)##\n", DLOG_MGR_VERSON);
	ret = misc_register(&dlog_mgr_misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "[dlog_mgr] failed misc_register! : %d\n", ret);
		return ret;
	}
	return init_dlog_mgr_mmap();
}

static void __exit dlog_mgr_exit(void)
{
	int ret;
	ret = misc_deregister(&dlog_mgr_misc);
	if (unlikely(ret))
		printk(KERN_ERR "[dlog_mgr] failed to unregister misc device!\n");
}

module_init(dlog_mgr_init);
module_exit(dlog_mgr_exit);
