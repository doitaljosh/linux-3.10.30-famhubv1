#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/cdev.h>
#include <linux/delay.h>

#include <linux/mm.h>  /* mmap related stuff */
#include <linux/slab.h>
#include "agent/kern_ringbuffer.h"
#include "agent/tvis_agent_packet.h"
#include "agent/mmap_kernel.h"

/* #define DUMMY_WRITER */
#ifdef DUMMY_WRITER
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/delay.h>
#endif

/* VM_RESERVED flag is removed from kernel 3.7.x.
 * Need to verify again!!! */
#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

/* character device structures */
static dev_t mmap_dev;
static struct cdev mmap_cdev;

struct mmap_info {
	ringbuffer_t *rb;	/* the data */
	int reference;		/* how many times it is mmapped */
};

#define MAX_KERN_DEVICES 2
struct map_buffer {
	const char *path;
	ringbuffer_t *buf;
};

struct map_buffer buffer_map[MAX_KERN_DEVICES] = {
	{TOOL_KDEBUGD_DEVICE, NULL},
	{TOOL_KDEBUGD_ALT_DEVICE, NULL}
};

/*as mmap is used by more than 1 tool, we need to have different buffers */
ringbuffer_t *get_rb_id_by_name(const char *name)
{
	int i;
	for (i = 0; i < MAX_KERN_DEVICES; i++) {
		if (!strcmp(buffer_map[i].path, name))
			return buffer_map[i].buf;
	}

	return NULL;
}

int kdbg_agent_mmap(struct file *filp, struct vm_area_struct *vma)
{

	int ret;
	unsigned long length;
	ringbuffer_t *rb = NULL;
	char path_buf[256];
	int tool_dev_id = -1;
	char *p = NULL;
	struct mmap_info *info = NULL;
	unsigned long phy_add;

	if (!filp)
		return -1;

	if (vma->vm_end > vma->vm_start)
		length = vma->vm_end - vma->vm_start;
	else
		return -1;

	info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* get the path for tool device */
	p = d_path((&filp->f_path), path_buf, 256);
	if (p) {
		int i;
		for (i = 0; i < MAX_KERN_DEVICES; i++) {
			if (!strcmp(buffer_map[i].path, p)) {
				/* save */
				tool_dev_id = i;
				break;
			}
		}
	}

	if (tool_dev_id == -1) {
		printk(KERN_ERR "error in getting device path\n");
		kfree(info);
		return -1;
	}

	/* check length - do not allow larger mappings than the number of
	 * pages allocated */
	if (length > (MAX_RB_SIZE + PAGE_SIZE)) {
		kfree(info);
		return -EIO;
	}

	/* obtain new memory for buffer*/
	if (!buffer_map[tool_dev_id].buf) {
		rb_debug("Creating new memory for ringbuffer\n");
		rb = kern_ringbuffer_create(MAX_RB_SIZE >> PAGE_SHIFT);
		if (!rb) {
			rb_err("memory not available");
			kfree(info);
			return -ENOMEM;
		}

		info->rb = rb;
		buffer_map[tool_dev_id].buf = info->rb;
	} else {
		rb_debug("mapping rinbguffer memory to user\n");
		info->rb = buffer_map[tool_dev_id].buf;
		kern_ringbuffer_reset(buffer_map[tool_dev_id].buf);
	}
	/* assign this info struct to the file */
	filp->private_data = info;


	vma->vm_flags |= VM_RESERVED;
	/* assign the file private data to the vm private data */
	vma->vm_private_data = filp->private_data;

	phy_add = (unsigned long)virt_to_phys((void *)info->rb);

	ret = remap_pfn_range(vma, vma->vm_start, phy_add >> PAGE_SHIFT,
					length, vma->vm_page_prot);
	if (ret < 0) {
		rb_err("Error in mapping pfn\n");
		return ret;
	}
	rb_debug("mmap successful\n");
	return 0;

}

int kdbg_agent_close(struct inode *inode, struct file *filp)
{
	struct mmap_info *info = filp->private_data;

	if (info) {
		/*
		 * we want this memory to be available later,
		 * when agent restarts
		 */
		/* kern_ringbuffer_free(info->rb); */
		info->rb->user_fd = -1;
		kfree(info);
	}
	filp->private_data = NULL;

	rb_debug("Close successful\n");
	return 0;
}

int kdbg_agent_open(struct inode *inode, struct file *filp)
{
	rb_debug("open successful\n");
	return 0;
}

const struct file_operations kdbg_agent_fops = {
	.open = kdbg_agent_open,
	.release = kdbg_agent_close,
	.mmap = kdbg_agent_mmap,
};

/* Global kernel ringbuffer id for kernel tools */
static ringbuffer_t *g_rbuf;

/* Opens Kdebugd ringbuffer, returns -1 on error */
int kdbg_ringbuffer_open(void)
{
	if (!g_rbuf) {
		g_rbuf = kern_ringbuffer_open(TOOL_KDEBUGD_DEVICE, MAX_RB_SIZE);
		if (!g_rbuf) {
			g_rbuf = kern_ringbuffer_open(TOOL_KDEBUGD_ALT_DEVICE, MAX_RB_SIZE);
			if (!g_rbuf) {
				rb_err("Kdebugd ringbuffer open error.\n");
				return -1;
			}
		}
		rb_print("Kdebugd ringbuffer opened!!!");
	} else {
		rb_print("Kdebugd ringbuffer already open!!!\n");
	}
	return 0;
}

#define RB_WRITE_ERR_PRINT_FREQ	250
#define RB_DROP_PKT_FREQ	5

size_t kdebugd_ringbuffer_writer(void *arg, size_t size)
{
	size_t available;
	static int err_count;
	int drop_count = 0;

	if (!g_rbuf)
		return 0;

	available = kern_ringbuffer_write_space(g_rbuf);
	while (available < size) {
		/* print every 5 seconds, to control the
		 * frequency of noisy error prints */
		if (!(err_count % RB_WRITE_ERR_PRINT_FREQ)) {
			printk(KERN_ERR "%s: Not enough space..available: %d, to write: %d\n",
				__func__, available, size);
		}

		msleep(20);
		available = kern_ringbuffer_write_space(g_rbuf);
		err_count++;
		drop_count++;
		/* Check available space for 5 times i.e. 0.1sec,
		 * then drop the packet */
		if (drop_count == RB_DROP_PKT_FREQ)
			return 0;
	}
	return kern_ringbuffer_write(g_rbuf, arg, size);
}

/* Data is written with advanced rb write ptr */
size_t kdbg_rb_adv_writer(void *arg, size_t size)
{
	if (!g_rbuf)
		return 0;

	return kern_ringbuffer_adv_write(g_rbuf, arg, size);
}

/* Advance rb write pointer after writing advance data in RB */
int kdbg_ringbuffer_reset_adv_writer(void)
{
	if (!g_rbuf)
		return -1;

	return kern_ringbuffer_reset_adv_writer(g_rbuf);
}

/* Advance rb write pointer after writing advance data in RB */
int kdbg_ringbuffer_write_advance(size_t size)
{
	if (!g_rbuf)
		return -1;

	kern_ringbuffer_write_advance(g_rbuf, size);
	return 1;
}

/* Increment advance ringbuffer write pointer of RB */
int kdbg_ringbuffer_inc_adv_writer(size_t size)
{
	if (!g_rbuf)
		return -1;

	kern_ringbuffer_inc_adv_writer(g_rbuf, size);
	return 1;
}
size_t kdbg_ringbuffer_write_space(void)
{
	if (!g_rbuf)
		return 0;

	return kern_ringbuffer_write_space(g_rbuf);
}

/* kernel ringbuffer reader for kdebugd dead or alive*/
int kdbg_ringbuffer_reader_dead(void)
{
	if (!g_rbuf)
		return -1;

	return kern_ringbuffer_reader_dead(g_rbuf);
}

/* kernel ringbuffer for kdebugd reset */
void kdbg_ringbuffer_reset(void)
{
	kern_ringbuffer_reset(g_rbuf);
}

int kdbg_ringbuffer_close(void)
{
	struct file *filp;
	struct mmap_info *info;

	filp = filp_open(TOOL_KDEBUGD_DEVICE, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp)) {
		filp = filp_open(TOOL_KDEBUGD_ALT_DEVICE, O_RDONLY | O_LARGEFILE, 0);
		if (IS_ERR(filp)) {
			rb_err("Kdebugd ringbuffer close error.\n");
			return -1;
		}
	}

	info = filp->private_data;

	if (info) {
		/*
		 * we want this memory to be available later,
		 * when agent restarts
		 */
		kern_ringbuffer_free(info->rb);
		info->rb->user_fd = -1;
		kfree(info);
	}
	filp->private_data = NULL;
	g_rbuf = NULL;

	filp_close(filp, NULL);

	rb_debug("Close successful\n");
	return 0;
}

#ifdef DUMMY_WRITER
static struct task_struct *task;
static int ringbuffer_writer(void *arg)
{
	ringbuffer_t *buffer = NULL;

	while (!kthread_should_stop()) {
		buffer = kern_ringbuffer_open(TOOL_KDEBUGD_DEVICE, MAX_RB_SIZE);
		if (!buffer) {
			msleep(5000);
			continue;
		}

		rb_print("Agent connected to ringbuffer_writer\n");
		rb_print("Size: %d\n", kern_ringbuffer_write_space(buffer));
		while (!kthread_should_stop()) {
			char *str1 = "abcdefghijklmnopqrstuvwxyz";
			char *str2 = "0123456789";
			char *str3 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
			int size = 0;

			struct tool_header t_hdr;

			size = kern_ringbuffer_write_space(buffer);
			rb_print("Size: %d\n",
					kern_ringbuffer_write_space(buffer));
			if (size >= (26 + TOOL_HDR_SIZE)) {
				t_hdr.cmd = 0xaaaaaaaa;
				t_hdr.data_len = 26;
				kern_ringbuffer_write(buffer, (char *)&t_hdr,
							sizeof(t_hdr));
				size = size - TOOL_HDR_SIZE;
				kern_ringbuffer_write(buffer, str1, 26);
				size = size - 26;
			}

			if (size >= (10 + TOOL_HDR_SIZE)) {
				t_hdr.cmd = 0x11111111;
				t_hdr.data_len = 10;
				kern_ringbuffer_write(buffer, (char *)&t_hdr,
							sizeof(t_hdr));
				size = size - TOOL_HDR_SIZE;
				kern_ringbuffer_write(buffer, str2, 10);
				size = size - 10;
			}

			if (size >= (26 + TOOL_HDR_SIZE)) {
				t_hdr.cmd = 0xCCCCCCCC;
				t_hdr.data_len = 26;
				kern_ringbuffer_write(buffer, (char *)&t_hdr,
							sizeof(t_hdr));
				size = size - TOOL_HDR_SIZE;
				kern_ringbuffer_write(buffer, str3, 26);
				size = size - 26;
			}
			msleep(1000);
			if (kern_ringbuffer_reader_dead(buffer)) {
				rb_print("user application closed buffer...\n");
				kern_ringbuffer_close(buffer);
				break;
			}
		}
	}

	return 0;
}
#endif


#define AGENT_MAJOR 1000

static int __init mmap_agent_init(void)
{
	int ret = 0;
	mmap_dev = MKDEV(AGENT_MAJOR, 0);
	ret = register_chrdev_region(mmap_dev , 1, "agent_kdbg_mmap");
	if (ret < 0) {
		printk(KERN_WARNING "Kdebugd Agent: unable to get major %d\n",
				AGENT_MAJOR);
		return ret;
	}
	/* initialize the device structure and
	 * register the device with the kernel */
	cdev_init(&mmap_cdev, &kdbg_agent_fops);
	ret = cdev_add(&mmap_cdev, mmap_dev, 1);
	if (ret < 0) {
		printk(KERN_ERR "could not allocate chrdev for mmap\n");
		unregister_chrdev_region(mmap_dev, 1);
		return ret;
	}

#ifdef DUMMY_WRITER
	task = kthread_run(ringbuffer_writer, NULL, "ringbuffer_writer");
	if (IS_ERR(task)) {
		printk(KERN_CONT "Failed to create test thread\n");
		return -1;
	}
#endif
	return 0;
}

static void __exit mmap_agent_exit(void)
{
	/* remove the character deivce */
	cdev_del(&mmap_cdev);
	unregister_chrdev_region(mmap_dev, 1);
#ifdef DUMMY_WRITER
	kthread_stop(task);
#endif
}

module_init(mmap_agent_init);
module_exit(mmap_agent_exit);
