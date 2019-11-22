#include <asm/cacheflush.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <linux/delay.h>
#include <linux/io.h>

#include "bptime.h"

#define BPTIME_VERSON	1

struct time_info {
	unsigned long time_offset;
	bool	time_sync_on;
};

enum bptime_definition {
	/* BPTime Definition */
	PROCESS_TIME_USER = 1000,
	PROCESS_TIME_DEMON = 1001,
	/* Protocol Definition */
	BPTIME_SET = 2000,
	BPTIME_GET = 2001,
	BPTIME_SETLOCAL = 2002,
	BPTIME_WAIT_DEMON = 2003,
	BPTIME_REGISTER_DEMON = 2004,
	BPTIME_TIME_SYNC = 2005,
	BPTIME_WORK_SET_TIME = 2006,
	BPTIME_WORK_ENABLE_SYNC = 2007,
	BPTIME_WORK_DISABLE_SYNC = 2008,
	/* Transaction Type  */
	BPTIME_TRAN_SYNC = 3000,
	BPTIME_TRAN_ASYNC = 3001,
	BPTIME_TRAN_FINISH = 3002,
	BPTIME_TRAN_DEAD = 3003,
};

struct bptime_process {
	int type;
};

struct bptime_work {
	int work_type;
	int transact_type;
	wait_queue_head_t wait;
	unsigned long value;
	int result;
	struct list_head entry;
};

struct time_demon_work {
	wait_queue_head_t wait;
	struct bptime_work *work;
};

DEFINE_MUTEX(bptime_lock);
bool is_registered;
struct time_info bptime_info = { 0 , true};
struct time_demon_work demon_work;
static LIST_HEAD(time_work_list);


static bool check_time_demon(void)
{
	struct mm_struct *mm = current->mm;
	int ret = false;
	if (!mm)
		return false;
	down_read(&mm->mmap_sem);
	if (mm->exe_file) {
		char *buf = NULL;
		char *pos = ERR_PTR(-ENOMEM);
		unsigned int buf_len = PAGE_SIZE / 2;

		buf = kmalloc(buf_len, GFP_NOFS);
		if (!buf) {
			up_read(&mm->mmap_sem);
			return false;
		}
		buf[buf_len - 1] = '\0';

		pos = d_absolute_path(&mm->exe_file->f_path,
						buf, buf_len-1);

		if (IS_ERR(pos)) {
			printk(KERN_WARNING "fail to get absolute path:%ld\n",
								PTR_ERR(pos));
		} else {
#ifdef CONFIG_BPTIME_DEMON_NAME
			if (strncmp(pos, CONFIG_BPTIME_DEMON_NAME,
			strlen(CONFIG_BPTIME_DEMON_NAME)) == 0) {
				ret = true;
				printk(KERN_INFO "[bptime] registered demon:%s\n",
									pos);
			}
#endif
		}
		kfree(buf);
	}
	up_read(&mm->mmap_sem);
	return ret;
}

static struct bptime_work *create_work(int work_type, unsigned long value)
{
	struct bptime_work *work;
	if (is_registered == false)
		return NULL;
	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (work == NULL)
		return NULL;
	work->work_type = work_type;
	work->transact_type = BPTIME_TRAN_SYNC;
	init_waitqueue_head(&work->wait);
	work->value = value;
	INIT_LIST_HEAD(&work->entry);
	list_add_tail(&work->entry , &time_work_list);
	return work;
}

static struct bptime_work *get_work(void)
{
	struct bptime_work *work = NULL;
	if (!list_empty(&time_work_list)) {
		work = list_first_entry(&time_work_list,
				struct bptime_work , entry);
		list_del_init(&work->entry);
	}
	return work;
}

static long checking_result(struct bptime_work *work)
{
	if (work->result < 0)
		return work->result;

	switch (work->work_type) {
		case BPTIME_WORK_SET_TIME:{
			bptime_info.time_offset = work->result;
			}
			break;
		case BPTIME_WORK_ENABLE_SYNC:{
			bptime_info.time_offset = work->result;
			bptime_info.time_sync_on = true;
			}
			break;
		case BPTIME_WORK_DISABLE_SYNC:{
			bptime_info.time_sync_on = false;
			}
			break;
		default:{
			BUG_ON(true);
			}
			break;
	}
	return work->result;
}

static void wakeup_send_thread(struct bptime_work *work, int result)
{
	if (work != NULL) {
		if (work->transact_type == BPTIME_TRAN_SYNC) {
			work->result = result;
			work->transact_type = BPTIME_TRAN_FINISH;
			wake_up_interruptible(&work->wait);
		} else if (demon_work.work->transact_type == BPTIME_TRAN_DEAD) {
			checking_result(work);
			kfree(work);
		} else
			kfree(work);
	}
}

static void clean_all_demon_work(void)
{
	struct bptime_work *work = NULL;

	do {
		work = get_work();
		wakeup_send_thread(work, -ESRCH);
	} while (work != NULL);
}

static long bptime_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int ret = 0;
	struct bptime_process *time_proc = file->private_data;
	struct bptime_work *my_work = NULL;

	mutex_lock(&bptime_lock);

	switch (cmd) {
		case BPTIME_SET:{
			if ((is_registered == false) ||
				(bptime_info.time_sync_on == false))
				bptime_info.time_offset = arg;
			else {
				my_work = create_work(
					BPTIME_WORK_SET_TIME, arg);
				if (my_work == NULL) {
					ret = -ENOMEM;
					break;
				}
				wake_up_interruptible(&demon_work.wait);
			}
			break;
		}
		case BPTIME_GET:{
			void __user *ubuf = (void __user *)arg;
			if (put_user(bptime_info.time_offset,
					(unsigned long *)ubuf)) {
				ret = -EINVAL;
			}
			break;
		}
		case BPTIME_SETLOCAL:{
			if (time_proc->type != PROCESS_TIME_DEMON)
				ret = -EPERM;
			else {
				if (bptime_info.time_sync_on)
					bptime_info.time_offset = arg;
			}
			break;
		}
		case BPTIME_WAIT_DEMON:{
			if (time_proc->type != PROCESS_TIME_DEMON) {
				ret = -EPERM;
			} else {
				int result;
				void __user *ubuf = (void __user *)arg;
				get_user(result, (unsigned long *)ubuf);
				wakeup_send_thread(demon_work.work, result);
				mutex_unlock(&bptime_lock);

				wait_event_interruptible(demon_work.wait,
						!list_empty(&time_work_list));

				mutex_lock(&bptime_lock);
				ret = -EAGAIN;
				demon_work.work = get_work();
				if (demon_work.work != NULL) {
					ret = demon_work.work->work_type;
					if (put_user(demon_work.work->value,
							(unsigned long *)ubuf))
						wakeup_send_thread(
						demon_work.work, -EINVAL);
				}
			}
			break;
		}
		case BPTIME_REGISTER_DEMON:{
			ret = -EPERM;
#ifdef CONFIG_BPTIME_DEMON
			if (is_registered == false) {
				is_registered = check_time_demon();
				if (is_registered) {
					time_proc->type = PROCESS_TIME_DEMON;
					printk(KERN_INFO "[bptime] time-demon registered\n");
					ret = 0;
				}
			}
#endif
			break;
		}
		case BPTIME_TIME_SYNC:{
			if (is_registered == false)
				ret = -ESRCH;
			else if (arg == 0)
				my_work = create_work(
						BPTIME_WORK_DISABLE_SYNC, arg);
			else
				my_work = create_work(
						BPTIME_WORK_ENABLE_SYNC, arg);

			if (is_registered && my_work == NULL) {
				ret = -ENOMEM;
				break;
			} else
				wake_up_interruptible(&demon_work.wait);
			break;
		}
		default:{
			ret = -EINVAL;
			break;
		}
	}

	mutex_unlock(&bptime_lock);

	if ((my_work != NULL) &&
			(my_work->transact_type != BPTIME_TRAN_ASYNC)) {

		wait_event_interruptible(my_work->wait,
				(my_work->transact_type == BPTIME_TRAN_FINISH));

		mutex_lock(&bptime_lock);
		ret = -ESRCH;
		if (my_work->transact_type == BPTIME_TRAN_FINISH) {
			ret = checking_result(my_work);
			kfree(my_work);
		} else
			my_work->transact_type = BPTIME_TRAN_DEAD;
		mutex_unlock(&bptime_lock);
	}

	return ret;
}

static long bptime_open(struct inode *nodp, struct file *filp)
{
	struct bptime_process *time_proc;
	time_proc = kzalloc(sizeof(*time_proc), GFP_KERNEL);
	if (time_proc == NULL)
		return -ENOMEM;
	time_proc->type = PROCESS_TIME_USER;
	filp->private_data = time_proc;
	printk(KERN_INFO "[bptime] process open : %s(%d)\n",
				current->comm , current->pid);
	return 0;
}

static int bptime_release(struct inode *nodp, struct file *filp)
{
	if (filp->private_data) {
		struct bptime_process *time_proc = filp->private_data;
		if (time_proc->type == PROCESS_TIME_DEMON) {
			mutex_lock(&bptime_lock);
			is_registered = false;
			clean_all_demon_work();
			mutex_unlock(&bptime_lock);
		}
		kfree(time_proc);
		filp->private_data = NULL;
		printk(KERN_INFO "[bptime] process released : %s(%d)\n",
					current->comm , current->pid);
	}
	return 0;
}

static const struct file_operations bptime_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = bptime_ioctl,
	.open = bptime_open,
	.release = bptime_release,
};

static struct miscdevice bptime_misc = {
	.minor = BPTIME_MINOR,
	.name = "bptime",
	.mode = 0666,
	.fops = &bptime_fops
};

static int __init bptime_init(void)
{
	int ret;
	printk(KERN_INFO "##[bptime] bptime init:ver(%d)##\n",
						BPTIME_VERSON);
	ret = misc_register(&bptime_misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "[bptime] failed misc_register! : %d\n", ret);
		return ret;
	}
	init_waitqueue_head(&demon_work.wait);
	demon_work.work = NULL;
	is_registered = false;
	return 0;
}

static void __exit bptime_exit(void)
{
	int ret;
	ret = misc_deregister(&bptime_misc);
	if (unlikely(ret))
		printk(KERN_ERR "[bptime] failed to unregister misc device!\n");

}

module_init(bptime_init);
module_exit(bptime_exit);

