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
#include <linux/vd_task_policy.h>

static struct vd_policy_list_head *allow_task_head; /* task list */
static struct vd_policy_list_head *except_task_head; /*except list*/


/* Only allowTask is need to show information */
static const char * const allow_task_name[] = {"exeTV", "exeAPP", "exeSBB", "X",
							"Compositor"};
/* exceptTask list for not to display show information */
static const char * const except_task_name[] = {"AppUpdate", "BIServer",
							"MainServer", "PDSServer"};

/*Clean up of task list*/
static void vd_task_clean_list(void)
{
	vd_policy_list_clean(allow_task_head);
	vd_policy_list_clean(except_task_head);
	return;
}

/*Initialization funtion*/
static int vd_task_policy_init(void)
{

	if (vd_policy_head_init(&allow_task_head, allow_task_name,
		sizeof(allow_task_name)/sizeof(char *)))
		goto clean_up; /*fail to initialize*/

	if (vd_policy_head_init(&except_task_head, except_task_name,
		sizeof(except_task_name)/sizeof(char *)))
		goto clean_up; /*fail to initialize*/

	return 0;

clean_up:
	pr_err("Fail to allocate space for /proc entry\n");
	return -ENOMEM;
}

/*Dispaly the list of task name in the task list*/
static int vd_policy_allow_task_read(struct seq_file *m, void *v)
{
	vd_policy_list_read(allow_task_head, m, v);
	return 0;
}
/*Dispaly the list of task name in the task list*/
static int vd_policy_except_task_read(struct seq_file *m, void *v)
{
	vd_policy_list_read(except_task_head, m, v);
	return 0;
}

/*If task is present return success (0) otherwise return  1*/
int vd_policy_allow_task_check(const char *k_task_name)
{
	return vd_policy_list_check(allow_task_head, k_task_name);
}
EXPORT_SYMBOL(vd_policy_allow_task_check);

/*If task is present return success (0) otherwise return  1*/
int vd_policy_except_task_check(const char *k_task_name)
{
	return vd_policy_list_check(except_task_head, k_task_name);
}
EXPORT_SYMBOL(vd_policy_except_task_check);

/*Add task group name in the task list*/
static ssize_t vd_task_policy_allow_add(struct file *seq,
				const char __user *data, size_t len, loff_t *ppos)
{
	return vd_policy_list_add(allow_task_head, seq, data, len, ppos);
}

/*Add task group name in the task list*/
static ssize_t vd_task_policy_except_add(struct file *seq,
				const char __user *data, size_t len, loff_t *ppos)
{
	return vd_policy_list_add(except_task_head, seq, data, len, ppos);
}

static int vd_task_policy_allow_open(struct inode *inode, struct file *file)
{
	return single_open(file, vd_policy_allow_task_read, NULL);
}

static int vd_task_policy_except_open(struct inode *inode, struct file *file)
{
	return single_open(file, vd_policy_except_task_read, NULL);
}

static const struct file_operations vd_task_policy_allowed_task_fops = {
	.open		= vd_task_policy_allow_open,
	.read		= seq_read,
	.write		= vd_task_policy_allow_add,
	.release	= single_release
};

static const struct file_operations vd_task_policy_except_task_fops = {
	.open		= vd_task_policy_except_open,
	.read		= seq_read,
	.write		= vd_task_policy_except_add,
	.release	= single_release
};

static int __init proc_vd_task_policy_init(void)
{
	struct proc_dir_entry *proc_file_entry;

	if (!vd_task_policy_init()) {
		proc_file_entry = proc_create("vd_allowed_task_list", 0, NULL,
					&vd_task_policy_allowed_task_fops);
		if (proc_file_entry == NULL)
			goto fail;

		proc_file_entry = proc_create("vd_except_task_list", 0, NULL,
					&vd_task_policy_except_task_fops);
		if (proc_file_entry == NULL) {
			remove_proc_entry("vd_allowed_task_list", NULL);
			goto fail;
		}
		return 0;
	} else
		return -ENOMEM;

fail:
	vd_task_clean_list();
	return -ENOMEM;

}

static void __exit proc_vd_task_policy_exit(void)
{
	vd_task_clean_list();
	remove_proc_entry("vd_allowed_task_list", NULL);
	remove_proc_entry("vd_except_task_list", NULL);
}

module_init(proc_vd_task_policy_init);
module_exit(proc_vd_task_policy_exit);
MODULE_LICENSE("GPL");
