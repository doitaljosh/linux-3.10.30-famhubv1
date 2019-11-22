#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/blkdev.h>

extern int authuld_compare(struct task_struct *me, struct task_struct *parent, char *random);

static ssize_t authuld_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	int i;
	char buffer[256];

	if (!count)
	{
		printk(KERN_CRIT "[AUTHULD] count is zero\n");
		return count;
	}

	if (count>256)
	{
		printk(KERN_CRIT "[AUTHULD] input is too big\n");
		return count;
	}

	memset(buffer, 0x0, 256); 
	if (copy_from_user(buffer, buf, count))
	{
		printk(KERN_CRIT "[AUTHULD] copy_from_user() fail\n");
		return -EFAULT;
	}

	// copy verification
	for(i=0; i<256; i++)
	{
		if( buffer[i] != 0x0)
			break;
	}

	// buffer datas are same to 0x0 => No copy
	if( i==256 )
	{
		printk(KERN_CRIT "[AUTHULD] no copy user data(all data are zero)\n");
		return count;
	}

	authuld_compare(current, current->real_parent, buffer );

	return count;
}

static const struct file_operations authuld_fops = {
	.write  = authuld_write,
};

static int __init proc_authuld_init(void)
{
	proc_create("vd_guard", S_IRUSR | S_IWUSR, NULL,
			&authuld_fops);

	return 0;
}
module_init(proc_authuld_init);
