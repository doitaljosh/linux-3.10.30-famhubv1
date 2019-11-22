#include <linux/fs.h>
#include <linux/file.h>
#include <linux/futex.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include "rtmutex_common.h"
#include <linux/dla.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/slab.h>

DEFINE_MUTEX(dla_mutex);
LIST_HEAD(futex_dla_list);
static u32 counter_id = 1;

const struct file_operations dla_proc_fops = {
	.write = futex_dla_write
};

static void add_dla_list(struct task_struct *task)
{
	struct dla_node *dla_node;
	if (!task->mm) {
		pr_emerg("FUTEX_DLA ERROR: %s/%d is kernel thread.can't dump\n",
				task->comm, task->pid);
		return;
	}

	dla_node = kzalloc(sizeof(struct dla_node), GFP_KERNEL);
	if (unlikely(NULL == dla_node)) {
		pr_emerg("FUTEX_DLA ERROR: Cannot allocate node for DLA\n");
		return;
	}
	dla_node->task = task;
	dla_node->processed = 0;
	list_add_tail(&dla_node->node, &futex_dla_list);
	return;
}

static void dla_dump(void)
{
	struct dla_node *dla_node;
	struct dla_node *dla_other_node;
	struct dla_node *dla_another_node;

	list_for_each_entry_safe(dla_node, dla_another_node,
			&futex_dla_list, node) {
		/* remove all others from list and
		* release their task_struct too
		*/
		if (dla_node->processed)
			continue;
		dla_node->processed = 1;
		list_for_each_entry(dla_other_node, &futex_dla_list, node) {
			if ((dla_other_node->processed == 0) &&
			(dla_other_node->task->tgid == dla_node->task->tgid)) {
				dla_other_node->processed = 1;
			}
		}
		read_lock(&tasklist_lock);
		if (!(dla_node->task->flags & PF_EXITING)
				&& dla_node->task->sighand) {
			pr_emerg("FUTEX_DLA: raising SIGABRT for pid %s/%d\n",
					dla_node->task->comm,
					dla_node->task->pid);
			force_sig(SIGABRT, dla_node->task);
			read_unlock(&tasklist_lock);
		} else {
			read_unlock(&tasklist_lock);
			pr_emerg("FUTEX_DLA_ERROR: %s/%d exiting can't dump\n",
					dla_node->task->comm,
					dla_node->task->pid);
			put_task_struct(dla_node->task);
			list_del(&dla_node->node);
			kfree(dla_node);
		}
	}

#ifndef CONFIG_MINIMAL_CORE
	list_for_each_entry_safe(dla_node, dla_other_node,
			&futex_dla_list, node) {
		put_task_struct(dla_node->task);
		list_del(&dla_node->node);
		kfree(dla_node);
	}
#endif
}

ssize_t futex_dla_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *off)
{
	int len, i = 0;
	unsigned long num = 0, input_count = 0;
	pid_t input[MAX_INPUT_PID] = {0};
	struct task_struct *task;
	char *futex_dla_input_buf, c;

	if (count > MAX_INPUT_SIZE)
		len = MAX_INPUT_SIZE;
	else
		len = count;

	futex_dla_input_buf = kzalloc(MAX_INPUT_SIZE, GFP_KERNEL);
	if (!futex_dla_input_buf)
		return -ENOMEM;

	if (copy_from_user(futex_dla_input_buf, buffer, len))
		return -EFAULT;

	do {
		c = futex_dla_input_buf[i++];
		if (c >= '0' && c <= '9') {
			num = num * 10 + c - '0';
		} else {
			input[input_count++] = num;
			num = 0;
		}
	} while (i < MAX_INPUT_SIZE && input_count < MAX_INPUT_PID);

	mutex_lock(&dla_mutex);
	for (input_count = 0; input_count < 3 && input[input_count];
			input_count++, num = 0, counter_id++) {
		rcu_read_lock();
		task = find_task_by_vpid(input[input_count]);
		if (task)
			get_task_struct(task);
		rcu_read_unlock();

		if (!task) {
			pr_emerg("####FUTEX_DLA: NO Task exist with pid %d\n",
					input[input_count]);
			continue;
		} else {
			pr_emerg("####FUTEX_DLA: analysing %s/%d ##########\n",
					task->comm, task->pid);
		}
		while (task) {
			add_dla_list(task);
			task = futex_check_deadlock(task, counter_id);
		}
	}

	dla_dump();
	mutex_unlock(&dla_mutex);
	kfree(futex_dla_input_buf);
	return len;
}

