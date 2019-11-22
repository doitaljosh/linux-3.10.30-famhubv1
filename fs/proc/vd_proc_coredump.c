/*
*  Copyright (C) 2013-2014 Samsung Electronics Co., Ltd.
*
*  This program is free software; you can redistribute it and/or
*  modify it under the terms of the GNU General Public License
*  as published by the Free Software Foundation; either version 2
*  of the License, or (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc.,51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,USA.
*/

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#ifdef CONFIG_NOT_ALLOW_REBOOT_DURING_COREDUMP

#define MAX_COREDUMP_REBOOT_WRITE_BUF		(3)
#define ALLOW_REBOOT_DURING_COREDUMP		(0)
#define NOT_ALLOW_REBOOT_DURING_COREDUMP	(1)

atomic_t coredump_reboot_status = ATOMIC_INIT(ALLOW_REBOOT_DURING_COREDUMP);

/* Function: proc_coredump_reboot_write
 *
 * This is a proc helper function for allowing/disallowing coredump at reboot.
 */
static ssize_t proc_coredump_reboot_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char buffer[MAX_COREDUMP_REBOOT_WRITE_BUF] = {0};
	long status;

	/*
	 * Prologue of proc write function
	 */

	if (!count)
		return (ssize_t)count;

	if (count >= sizeof(buffer)) {
		printk(KERN_ERR "Invalid input, values allowed (0 or 1).\n");
		return -EINVAL;
	}

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	if (strict_strtol(buffer, 0, &status) != 0)
		return -EINVAL;

	switch (status) {
	case ALLOW_REBOOT_DURING_COREDUMP:
			printk(KERN_INFO "Allow Reboot during Coredump.\n");
			atomic_set(&coredump_reboot_status, 0);
		break;
	case NOT_ALLOW_REBOOT_DURING_COREDUMP:
			printk(KERN_INFO "Not Allow Reboot during Coredump.\n");
			atomic_set(&coredump_reboot_status, 1);
		break;
	default:
			printk(KERN_ERR "Invalid input, values allowed (0 or 1).\n");
			return -EINVAL;
		break;
	}

	/*
	 * Epilogue of proc write function
	 */

	return (ssize_t)count;
}

static ssize_t proc_coredump_reboot_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	char buffer[MAX_COREDUMP_REBOOT_WRITE_BUF] = {0};
	size_t len;

	len = (size_t)snprintf(buffer, sizeof(buffer), "%d\n", atomic_read(&coredump_reboot_status));

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations proc_coredump_reboot_ops = {
	.read = proc_coredump_reboot_read,
	.write = proc_coredump_reboot_write,
};
#endif

#define MAX_COREDUMP_TARGET_VERSION_WRITE_BUF		(64)

char target_version[MAX_COREDUMP_TARGET_VERSION_WRITE_BUF] = "";

static ssize_t proc_coredump_target_version_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char buffer[MAX_COREDUMP_TARGET_VERSION_WRITE_BUF] = {0};

	/*
	 * Prologue of proc write function
	 */

	if (!count)
		return (ssize_t)count;

	if (count >= sizeof(buffer)) {
		printk(KERN_ERR "Invalid input, maximum lengh supported MAX_COREDUMP_TARGET_VERSION_WRITE_BUF(%d).\n",
				MAX_COREDUMP_TARGET_VERSION_WRITE_BUF);
		return -EINVAL;
	}

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	strncpy(target_version, buffer, MAX_COREDUMP_TARGET_VERSION_WRITE_BUF - 1);
	target_version[MAX_COREDUMP_TARGET_VERSION_WRITE_BUF - 1] = '\0';

	/*
	 * Epilogue of proc write function
	 */

	return (ssize_t)count;
}

static ssize_t proc_coredump_target_version_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, target_version, strlen(target_version));
}

const struct file_operations proc_coredump_target_version_ops = {
	.read = proc_coredump_target_version_read,
	.write = proc_coredump_target_version_write,
};

/*
 * Create Proc Entry
 */
static int __init init_vd_proc_coredump(void)
{
	struct proc_dir_entry *entry;

#ifdef CONFIG_NOT_ALLOW_REBOOT_DURING_COREDUMP
	entry = proc_create("not_allow_reboot_during_coredump", S_IRUSR | S_IWUSR, NULL,
		&proc_coredump_reboot_ops);
	if (!entry)
		printk(KERN_ERR "Failed to create not_allow_reboot_during_coredump proc entry\n");
#endif
	entry = proc_create("target_version", S_IRUSR | S_IWUSR, NULL,
		&proc_coredump_target_version_ops);
	if (!entry)
		printk(KERN_ERR "Failed to create target_version proc entry\n");
	return 0;
}

module_init(init_vd_proc_coredump);

