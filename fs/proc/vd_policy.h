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

#ifndef _VD_POLICY_H
#define _VD_POLICY_H

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <linux/uaccess.h>

struct vd_policy_list {
	struct list_head list;  /* kernel's list structure */
	unsigned int  value_len;		/* value_name length */
	char value_name[TASK_COMM_LEN];   /* value_name for allowed task*/
};

struct vd_policy_list_head {
	struct vd_policy_list *list_name;
	struct rw_semaphore vd_policy_list_sem; /*read/wirte semaphore */
};


void vd_policy_list_clean(struct vd_policy_list_head *list_head);

int vd_policy_head_init(struct vd_policy_list_head **list_head,
			const char * const list_data[], unsigned int size);

int vd_policy_list_read(struct vd_policy_list_head *list_head,
			struct seq_file *m, void *v);

int vd_policy_list_check(struct vd_policy_list_head *list_head,
			const char *k_value_name);

ssize_t vd_policy_list_add(struct vd_policy_list_head *list_head,
			struct file *seq, const char __user *data,
			size_t orig_len, loff_t *ppos);

#endif
