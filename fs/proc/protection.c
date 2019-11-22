#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/blkdev.h>

extern int flash_protect;
void set_write_protect_ro(int part_num);
void set_write_protect_rw(int part_num);
int get_partition_cnt(void);

static ssize_t flash_protect_write(struct file *file, const char __user *buf,
                size_t count, loff_t *ppos)
{
	char buffer[10];

	if (!count)
		return count;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	if (buffer[count - 1] == '\n')
	{
		buffer[count - 1] = '\0';
	}

	if (!strcmp(buffer, "1"))
	{
		printk( "\n[SABSP] Write Enabled!!!!!!!\n");
		flash_protect = 1;
	}
	else if (!strcmp(buffer, "0"))
	{
		printk( "\n[SABSP] Write protected!!!!!!!\n");
		flash_protect = 0;
	}
	else
	{
		printk("Usage : echo [0 | 1] > /proc/flash_writable\n");
		printk("        1 : write allowed, 0 : write protected\n");
	}

	return count;
}

static const struct file_operations flash_protect_fops = {
	.write	= flash_protect_write,
};

static ssize_t flash_protect_parts_write(struct file *file, const char __user *buf,
                size_t count, loff_t *ppos)
{
	char buffer[10];
	int part_num;

	if (!count)
		return count;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	if (buffer[count - 1] == '\n')
	{
		buffer[count - 1] = '\0';
	}

	part_num = simple_strtoul(buffer, NULL, 0);
	if(get_partition_cnt() >= part_num)
	{
		printk("set Writable partition %d \n", part_num);
		set_write_protect_rw(part_num);
	}
	else
	{
		printk("Usage : echo part_num > /proc/flash_writable_parts\n");
	}

	return count;
}

static const struct file_operations flash_protect_parts_fops = {
	.write	= flash_protect_parts_write,
};

static int __init proc_flash_protect_init(void)
{
	proc_create("flash_writable", S_IRUSR | S_IWUSR, NULL, 
			&flash_protect_fops);

	proc_create("flash_writable_parts", S_IRUSR | S_IWUSR, NULL, 
			&flash_protect_parts_fops);
	return 0;
}
module_init(proc_flash_protect_init);
