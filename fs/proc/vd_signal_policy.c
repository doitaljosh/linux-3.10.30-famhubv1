/*
*  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
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

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <linux/uaccess.h>

#include "vd_policy.h"
#include <linux/vd_signal_policy.h>

static struct vd_policy_list_head *signal_allow_list; /* task list */

#ifdef CONFIG_PROC_VD_SIGNAL_HANDLER_DEFAULT_TASK
	static const char * const signal_allow_task_name[] = {"nacl_helper_boo", "valgrind", "memcheck-arm-li", "massif-arm-linu", \
									"lackey-arm-linu", "helgrind-arm-li", "drd-arm-linux", \
									"callgrind-arm-l", "cachegrind-arm-", "gplayer-runtime"};
#else
	static const char * const signal_allow_task_name[] = {""};
#endif

/*Clean up of task list*/
static void vd_signal_policy_clean_list(void)
{
	vd_policy_list_clean(signal_allow_list);
	return;
}

/*Initialization funtion*/
static int vd_signal_policy_init(void)
{

	if (vd_policy_head_init(&signal_allow_list, signal_allow_task_name,
		sizeof(signal_allow_task_name)/sizeof(char *)))
		goto clean_up; /*fail to initialize*/

	return 0;

clean_up:
	pr_err("Fail to allocate space for /proc entry\n");
	return -ENOMEM;
}

/*Dispaly the list of task name in the task list*/
static int vd_policy_allow_task_read(struct seq_file *m, void *v)
{
	vd_policy_list_read(signal_allow_list, m, v);
	return 0;
}

/*If task is present return success (1) otherwise return 0*/
int vd_signal_policy_check(const char *task_name)
{
	return vd_policy_list_check(signal_allow_list, task_name);
}
EXPORT_SYMBOL(vd_signal_policy_check);

/*Add signal task name in list*/
static ssize_t vd_signal_policy_add(struct file *seq,
				const char __user *data, size_t len, loff_t *ppos)
{
	return vd_policy_list_add(signal_allow_list, seq, data, len, ppos);
}

static int vd_signal_policy_open(struct inode *inode, struct file *file)
{
	return single_open(file, vd_policy_allow_task_read, NULL);
}

/*File handler for /proc/vd_signal_policy_list*/
static const struct file_operations vd_signal_policy_fops = {
	.open		= vd_signal_policy_open,
	.read		= seq_read,
	.write		= vd_signal_policy_add,
	.release	= single_release
};


static int __init proc_vd_signal_policy_init(void)
{
	struct proc_dir_entry *proc_file_entry;

	if (!vd_signal_policy_init()) {
		proc_file_entry = proc_create("vd_signal_policy_list", 0, NULL,
					&vd_signal_policy_fops);
		if (proc_file_entry == NULL)
			return -ENOMEM;

		return 0;
	} else
		return -ENOMEM;

}

static void __exit proc_vd_signal_policy_exit(void)
{
	vd_signal_policy_clean_list();
	remove_proc_entry("vd_signal_policy_list", NULL);
}

module_init(proc_vd_signal_policy_init);
module_exit(proc_vd_signal_policy_exit);
MODULE_LICENSE("GPL");
