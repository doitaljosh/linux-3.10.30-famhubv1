/*
 * linux/fs/fs-readahead.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Ajeet Kr Yadav <ajeet.y@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Implemention of selective readahead functionality to selectively
 * enable/disable the readahead per device per partition via
 * entries in "/proc/fs/readahead".
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "proc/internal.h"

#define PROC_RA_DIR_NAME "fs/readahead"
#define PROC_FILE_SIZE (2*(sizeof(char)))

#define ASCII_TO_DIGIT(a)	((a) - '0')	/* Only vaild for '0' to '9' */
#define DIGIT_TO_ASCII(d)	((d) + '0')	/* Only vaild for 0 to 9 */

struct ra_proc_file_entry {
	struct list_head list;	/* list for this structure */
	struct proc_dir_entry *proc;	/* device readahead proc file */
	const char *name;	/* alias to proc->name */
	int count;	/* reference count for this structure */
	atomic_t state;	/* readahead setting of device */
};

/**
 * These are structure to hold information about
 * "/proc/fs/readahead" directory and files in it
 */
static DEFINE_SPINLOCK(readahead_lock);
static struct proc_dir_entry *readahead_dir;
static struct ra_proc_file_entry *readahead_list;

/**
 * This function finds & return the device entry in
 * readahead_list if it exists otherwise NULL.
 * @param name: device name string, ex: "sda1"
 * @param add: add value to reference count
 * @return on success pointer to node, else NULL
 */
static struct ra_proc_file_entry *
find_readahead_entry(const char *name, int add)
{
	struct ra_proc_file_entry *tmp = NULL;
	struct ra_proc_file_entry *ent = NULL;

	spin_lock(&readahead_lock);

	if (!readahead_list)
		goto err;

	/* Search list, return the node with same name */
	list_for_each_entry(tmp, &readahead_list->list, list) {
		if (!strcmp(name, tmp->name)) {
			ent = tmp;
			ent->count += add;
			break;
		}
	}

err:
	spin_unlock(&readahead_lock);
	return ent;
}

/**
 * This function is called then the /proc file is read
 * @param filp		file pointer		
 * @param buffer	user space buffer pointer
 * @param count		number of bytes to read
 * @param offset	file pointer position
 * @return On success no of bytes read, else error value
 */
ssize_t read_readahead_proc(struct file *filp, char __user *buffer, size_t count, loff_t *offset)
{
	struct inode * inode = filp->f_path.dentry->d_inode;
	struct proc_dir_entry * dp = PDE(inode);
 	char val = 0;

	if (!buffer || !dp->data)
		return -EPERM;
		
	if (*offset) {
		/* We have finished to read */
		return 0;
	}

	/* Copy data to buffer, return the read data size */
	val = atomic_read((atomic_t *)dp->data);
	*buffer = (char) DIGIT_TO_ASCII(val);
	*(buffer + 1) = '\0';

	*offset = PROC_FILE_SIZE;

	return PROC_FILE_SIZE;	
}

/**
 * This function is called with the /proc file is written
 * @param filp          file pointer
 * @param buffer        user space buffer pointer
 * @param count         number of bytes to write
 * @param offset        file pointer position
 * @return On success number of bytes written, else error value
 */
ssize_t write_readahead_proc(struct file *filp, const char __user *buffer, size_t count, loff_t *offset)
{
        struct inode * inode = filp->f_path.dentry->d_inode;
        struct proc_dir_entry * dp = PDE(inode);
	char val = 0;

	if (!buffer || !dp->data)
		return -EPERM;   
		
	if (!count || (count > PROC_FILE_SIZE)) {
		/* Trying to write more data than allowed */
		return -EPERM;
	}

	/* Copy data from user buffer */
	if (copy_from_user(&val, buffer, sizeof(char)))
		return -EFAULT;
		
	/* Identify the option 0/1 */
	if (val < '0' || val > '1')
		return -EPERM;

	/* Update data, return the written data size */
	atomic_set((atomic_t *)dp->data, ASCII_TO_DIGIT(val));	
		
	return count;	
}

static const struct file_operations readahead_fops = {
        .write          = write_readahead_proc,
        .read           = read_readahead_proc,
};

/**
 * This function creates an instance of proc file in
 * "/proc/fs/readahead" directory. If device name is
 * valid than it checks whether it already had an entry
 * corresponding to this, if not it creates the new entry.
 * @param dev_name	device name string
 * @return on success pointer to state, else NULL
 */
atomic_t *
create_readahead_proc(const char *dev_name)
{
	struct proc_dir_entry *proc_file = NULL;
	struct ra_proc_file_entry *proc_ent = NULL;

	if (!dev_name)
		goto err;

	/* Check wheather "/proc/fs/readahead" exists */
	if (!readahead_dir) {
		pr_warn("Warning: Not found /proc/%s\n",
				PROC_RA_DIR_NAME);
		goto err;
	}

	/* Check wheather "/proc/fs/readahead/xxx" exists */
	proc_ent = find_readahead_entry(dev_name, 1);
	if (!proc_ent) {
		/* New device mounted, create ra_proc_file_entry entry */
		proc_ent = kmalloc(sizeof(struct ra_proc_file_entry),
				   GFP_KERNEL);
		if (!proc_ent)
			goto err;

		/* 1st instance, readahead enabled (default) */
		atomic_set(&proc_ent->state, 1);
		proc_ent->count = 1;

		/* Create the proc_dir_entry for new device */
		proc_file = proc_create(dev_name, 0644, readahead_dir, &readahead_fops);
		if (!proc_file) {
			remove_proc_entry(dev_name, readahead_dir);
			kfree(proc_ent); proc_ent = NULL;
			pr_warn("Warning: Cannot create /proc/%s/%s\n",
					PROC_RA_DIR_NAME, dev_name);
			goto err;
		}

		/* Initialise proc_dir_entry structure */
		proc_file->data = &proc_ent->state;
		proc_file->mode = S_IFREG | S_IRUGO;
		proc_file->uid = 0;
		proc_file->gid = 0;

		/* Fill ra_proc_file_entry entries */
		proc_ent->name = proc_file->name;
		proc_ent->proc = proc_file;

		/* Add new ra_proc_file_entry to list */
		spin_lock(&readahead_lock);
		if (readahead_list) {
			list_add(&proc_ent->list, &readahead_list->list);
		} else {
			INIT_LIST_HEAD(&(proc_ent->list));
			readahead_list = proc_ent;
		}
		spin_unlock(&readahead_lock);
	}
err:
	pr_debug("\n##### Create readahead proc: %s\n", dev_name);
	return proc_ent ? &proc_ent->state : NULL;
}
EXPORT_SYMBOL_GPL(create_readahead_proc);

/**
 * This function removes an instance of proc file from
 * "/proc/fs/readahead" directory. First it finds the
 * entry corresponding to this device, once found it
 * first decreases it ref count, if it zero than it safe to remove
 * the entry
 * @param dev_name	device name string
 * @return void
 */
void
remove_readahead_proc(const char *dev_name)
{
	struct ra_proc_file_entry *proc_ent = NULL;

	if (!dev_name)
		return;

	/* Remove the proc directory */
	if (readahead_dir) {
		proc_ent = find_readahead_entry(dev_name, -1);
		spin_lock(&readahead_lock);
		if (proc_ent && !proc_ent->count) {
			remove_proc_entry(dev_name, readahead_dir);
			list_del(&proc_ent->list);
			kfree(proc_ent);
		}
		spin_unlock(&readahead_lock);
	}
	pr_debug("\n##### Remove readahead proc: %s\n", dev_name);
	return;
}
EXPORT_SYMBOL_GPL(remove_readahead_proc);


/**
 * This function finds & return the readahead state
 * of the given device if found, else NULL.
 * @param name: device name string, ex: "sda1"
 * @return on success pointer to state, else NULL
 */
atomic_t *
get_readahead_entry(const char *name)
{
	struct ra_proc_file_entry *ent = NULL;
	ent = find_readahead_entry(name, 0);
	return ent ? &ent->state : NULL;
}
EXPORT_SYMBOL_GPL(get_readahead_entry);

/**
 * This function is called when the module is loaded.
 * It creates "/proc/fs/readahead" directory.
 * @param void
 * @return On success 0, else error value
 */
static int __init
readahead_init(void)
{
	int ret = 0;

	/* Create the "/proc/fs/readahead" directory */
	readahead_dir = proc_mkdir(PROC_RA_DIR_NAME, NULL);

	if (!readahead_dir) {
		pr_warn("Warning: Cannot create /proc/%s\n", PROC_RA_DIR_NAME);
		ret = -ENOMEM;
	}

	return ret;
}

/**
 * This function is called when the module is unloaded.
 * It recursively removes "/proc/fs/readahead" directory.
 * @param void
 * @return void
 */
static void __exit
readahead_exit(void)
{
	/* Remove the "/proc/fs/readahead" directory */
	if (readahead_dir) {
		remove_proc_entry(PROC_RA_DIR_NAME, NULL);
		readahead_dir = NULL;
	}
	return;
}

module_init(readahead_init);
module_exit(readahead_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Selectively enable/disable readahead for block devices"
				   "using /proc/fs/readahead/XXX files");

