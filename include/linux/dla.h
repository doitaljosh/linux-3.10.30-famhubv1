#ifndef _FUTEX_DLA_H
#define _FUTEX_DLA_H

#define MAX_INPUT_PID 3
#define MAX_INPUT_SIZE 1024

extern struct mutex dla_mutex;
extern struct list_head futex_dla_list;
extern const struct file_operations dla_proc_fops;

struct dla_node {
	struct task_struct *task;
	struct list_head node;
	int processed;
};

ssize_t futex_dla_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *off);

struct task_struct *futex_check_deadlock(struct task_struct *waiter, u32 rand);

#endif /* _FUTEX_DLA_H */
