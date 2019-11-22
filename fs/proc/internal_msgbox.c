#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/blkdev.h>
#include <linux/delay.h>

extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);

static ssize_t internal_msgbox_read(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	printk(KERN_CRIT "[MSGBOX] read not supported yet\n");
	return count;
}

static ssize_t internal_msgbox_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[9];
	int timeout = 100;

	if (!count)
	{
		printk(KERN_CRIT "[MSGBOX] count is zero\n");
		return count;
	}

	if (count>9)
	{
		printk(KERN_CRIT "[MSGBOX] input is too big\n");
		return count;
	}

	memset( buffer, 0x0, 9 );
	if (copy_from_user(buffer, buf, count))
	{
		printk(KERN_CRIT "[MSGBOX] copy_from_user() fail\n");
		return -EFAULT;
	}

	// debug
#if 0
	{
		int i;
		printk("\n");
		for(i=0; i<9;i++)
		{
			printk("[MSGBOX][%d] 0x%x\n", i, buffer[i]);
		}
	}
#endif
	
	do {
		if (sdp_messagebox_write(buffer, 9) == 9)
			break;
		udelay(100);
	} while(timeout--);
	
	if (!timeout)
		printk(KERN_ALERT "[MSGBOX] error: failed to get sdp message box's response\n");
		
	return count;
}

static const struct file_operations internal_msgbox_fops = {
	.write  = internal_msgbox_write,
	.read  = internal_msgbox_read,
};

static int __init proc_internal_msgbox_init(void)
{
	proc_create("internal_msgbox", S_IRUSR | S_IWUSR, NULL,
			&internal_msgbox_fops);

	return 0;
}
module_init(proc_internal_msgbox_init);
