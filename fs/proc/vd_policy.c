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

/*Clean up of task list and free the list head*/
void vd_policy_list_clean(struct vd_policy_list_head *list_head)
{

	struct vd_policy_list *list_name;
	struct list_head *ptr, *tmp_ptr;

	if (list_head == NULL)
		return;

	list_name = list_head->list_name;

	down_write(&list_head->vd_policy_list_sem);
	list_for_each_safe(ptr, tmp_ptr, &list_name->list) {
		kfree(list_entry(ptr, struct vd_policy_list, list));
	}

	up_write(&list_head->vd_policy_list_sem);

	kfree(list_head);
	list_head = NULL;
	return;
}
EXPORT_SYMBOL(vd_policy_list_clean);

static int vd_policy_initialize_list(struct vd_policy_list *list_to_create,
			const char * const value_names[], unsigned int size)
{
	unsigned int i = 0, len;
	struct vd_policy_list *tmp;

	if (size == 0) {
		/*Empty string initilization*/
		list_to_create->value_name[0] = '\0';

	} else {
		/* Initialize the first element (no malloc required*/
		tmp = list_to_create;
		len = strlen(value_names[i]);
		strncpy(tmp->value_name, value_names[i], len);
		tmp->value_name[len] = '\0';
		tmp->value_len = len;
		i++;
	}

	while (i < size) {
		tmp = kmalloc(sizeof(struct vd_policy_list), GFP_ATOMIC);
		if (unlikely(tmp == NULL))
			return -ENOMEM;
		len = strlen(value_names[i]);
		strncpy(tmp->value_name, value_names[i], len);
		tmp->value_name[len] = '\0';
		tmp->value_len = len;
		list_add(&tmp->list, &list_to_create->list);

		i++;
	}
	return 0;
}

/*Initialization funtion*/
int vd_policy_head_init(struct vd_policy_list_head **k_list_head,
			const char * const list_data[], unsigned int size)
{
	struct vd_policy_list_head *list_head;
	struct vd_policy_list *list_name;

	list_head = kmalloc(sizeof(struct vd_policy_list_head), GFP_ATOMIC);
	if (list_head == NULL)
		return -ENOMEM;

	list_name = kmalloc(sizeof(struct vd_policy_list), GFP_ATOMIC);
	if (list_name == NULL) {
		kfree(list_head);
		list_head = NULL;
		pr_err("Fail to allocate space for task_list entry\n");
		return -ENOMEM;
	}

	list_head->list_name = list_name;
	*k_list_head = list_head;

	init_rwsem(&list_head->vd_policy_list_sem);
	INIT_LIST_HEAD(&list_name->list);

	if (vd_policy_initialize_list(list_name, list_data,	size))
		goto clean_up; /*fail to initialize*/

	return 0;

clean_up:
	vd_policy_list_clean(list_head);
	pr_err("Fail to allocate space for /proc entry\n");
	return -ENOMEM;
}
EXPORT_SYMBOL(vd_policy_head_init);

int vd_policy_list_read(struct vd_policy_list_head *list_head,
		struct seq_file *m, void *v)
{
	struct vd_policy_list *list_name = list_head->list_name;
	struct vd_policy_list *list_entry;
	struct list_head *pos;

	down_read(&list_head->vd_policy_list_sem);

	list_for_each(pos, &list_name->list) {
		list_entry = list_entry(pos, struct vd_policy_list, list);
		seq_printf(m, " %s ", list_entry->value_name);
	}
	/* read the head*/
	list_entry =  list_first_entry(pos->prev, struct vd_policy_list, list);
	seq_printf(m, " %s\n", list_entry->value_name);

	up_read(&list_head->vd_policy_list_sem);
	return 0;
}
EXPORT_SYMBOL(vd_policy_list_read);

/*If task is present return success (1) otherwise return 0*/
int vd_policy_list_check(struct vd_policy_list_head *list_head,
			const char *k_value_name)
{
	struct vd_policy_list *list_name = list_head->list_name;
	struct vd_policy_list *entry = NULL;
	struct list_head *ptr;

	down_read(&list_head->vd_policy_list_sem);

	list_for_each(ptr, &list_name->list) {
	entry = list_entry(ptr, struct vd_policy_list, list);
		if (!strcmp(entry->value_name, k_value_name)) {
			up_read(&list_head->vd_policy_list_sem);
			return true;
		}
	}
	/*check for the last node*/
	entry =  list_first_entry(ptr->prev, struct vd_policy_list, list);
	if (!strcmp(entry->value_name, k_value_name)) {
		up_read(&list_head->vd_policy_list_sem);
			return true;
	}
	up_read(&list_head->vd_policy_list_sem);
	return false;
}
EXPORT_SYMBOL(vd_policy_list_check);

/*If task is present entry is not added in task list
 *On success return number of bytes writtern
 *On Failure return
	-EFAULT if not able to complete copy_form_user
	-ENOMEM if no memory is available for new element
*/
ssize_t vd_policy_list_add(struct vd_policy_list_head *list_head,
		struct file *seq, const char __user *data,
		size_t orig_len, loff_t *ppos)
{
	struct vd_policy_list *list_name = list_head->list_name;
	struct vd_policy_list *tmp, *entry;
	struct list_head *ptr;
	unsigned int len;

	if (unlikely(orig_len > TASK_COMM_LEN))
		len = TASK_COMM_LEN;
	else
		len = orig_len;

	tmp = kmalloc(sizeof(struct vd_policy_list), GFP_ATOMIC);
	if (unlikely(tmp == NULL))
		goto fail_no_mem;

	if (unlikely(copy_from_user(tmp->value_name, data, len))) {
		/*return  cleanup and error*/
		kfree(tmp);
		return -EFAULT;
	}
	tmp->value_name[len-1] = '\0';
	tmp->value_len = len;

	down_write(&list_head->vd_policy_list_sem);
	list_for_each(ptr, &list_name->list) {
	entry = list_entry(ptr, struct vd_policy_list, list);
	if (!strncmp(entry->value_name, tmp->value_name, len)) {
			up_write(&list_head->vd_policy_list_sem);
			kfree(tmp);
			*ppos = 0;
			return (ssize_t)orig_len;
		}
	}

	list_add(&tmp->list, &list_name->list);
	up_write(&list_head->vd_policy_list_sem);

	*ppos = 0;
	return (ssize_t)orig_len;

fail_no_mem:
	pr_err("Fail to allocate space for /proc entry\n");
	return -ENOMEM;
}
EXPORT_SYMBOL(vd_policy_list_add);
