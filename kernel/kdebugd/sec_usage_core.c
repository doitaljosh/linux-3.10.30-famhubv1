/*
 *  linux/kernel/sec_usage_core.c
 *
 *  Copyright (C) 2009 Samsung Electronics
 *
 *  2009-11-05  Created by Choi Young-Ho (yh2005.choi@samsung.com)
 *
 *  Counter Monitor  kusage_cored  register_counter_monitor_func
 *
 *  Disk Usage timer Mutex wait
 *  scheduling while atomic
 */

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <kdebugd.h>

#define MAX_COUNTER_MONITOR	10

wait_queue_head_t usage_cored_wq;

/* Queue used to keep counter Monitor functions*/
static counter_monitor_func_t counter_monitor_funcs[MAX_COUNTER_MONITOR] = {NULL};

/* Function and Veriable Used for Inittilization */
int kdbg_usage_core_init(void);
static int kdbg_core_init_flag;

/*
 * Name: register_counter_monitor_func
 * Desc: Counter Monitor
 * the Function regiter the counter monitor functions and call in
 * each on second duration.
 */
int register_counter_monitor_func(counter_monitor_func_t func)
{
	int i;

	if (func == NULL) {
		PRINT_KD("No Function Pointer specified\n");
		return -1;
	}

	/* Check flag for Initialization */
	if (!kdbg_core_init_flag) {
		/* This is the first fucntion to register.
		 * Usage core should Initialized first,
		 * no need to initialize it at boot time, initialize at
		 * time the of use*/
		kdbg_usage_core_init();
		kdbg_core_init_flag = 1;
	}

	for (i = 0; i < MAX_COUNTER_MONITOR; ++i) {
		if (counter_monitor_funcs[i] == NULL) {
			counter_monitor_funcs[i] = func;
			return 0;
		}
	}

	PRINT_KD("full of counter_monitor_funcs\n");
	return -1;
}

/* This function will unregister the registered Counter Monitor
 * Function. And return error if function is registered.
 * */
int unregister_counter_monitor_func (counter_monitor_func_t func)
{
	int i;

	/* Check for Initialization */
	if (!kdbg_core_init_flag) {
		PRINT_KD ("Not Initilized\n");
		return -1;
	}

	/* Check function in Counter Monitor Array and deregister it.*/
	for (i = 0; i < MAX_COUNTER_MONITOR; ++i) {
		if (counter_monitor_funcs[i] == func) {
			counter_monitor_funcs[i] = NULL;
			return 0;
		}
	}

	/* Return Error if Counter Monitor is not registered.*/
	PRINT_KD("Counter Monitor Func is Not Registered\n");
	return -1;
}


/*
 * Name: usage_cored
 * Desc: Counter Monitor
 */
static int usage_cored(void *p)
{
	int i = 0;

	while (1) {

		counter_monitor_func_t *funcp = &counter_monitor_funcs[0];
		/* Check complete queue, can not break in between.*/
		for (i = 0; i < MAX_COUNTER_MONITOR; ++i, funcp++) {

			if (*funcp != NULL) {
				(*funcp)();
			}
		}
		wait_event_interruptible_timeout(usage_cored_wq, 0, 1 * HZ);	/* sleep 1 sec */
	}

	BUG();

	return 0;
}

/*
 * Name: kdbg_usage_core_init
 * Desc: kusage_cored
 */
int kdbg_usage_core_init(void)
{
	static int init_flag;
	pid_t pid;
	struct task_struct *sec_usage_core_tsk = NULL;

	BUG_ON(init_flag);
	init_waitqueue_head(&usage_cored_wq);

	/* changing state to running is done by thread function, if
	 * success in creating thread */
	sec_usage_core_tsk = kthread_create(usage_cored, NULL, "sec_usage_core_thread");
	if (IS_ERR(sec_usage_core_tsk)) {
		sec_usage_core_tsk = NULL;
		printk("sec_usage_core_thread: bp_thread Thread Creation\n");
		return -1;
	}

	/* update task flag and wakeup process */
	sec_usage_core_tsk->flags |= PF_NOFREEZE;
	wake_up_process(sec_usage_core_tsk);

	pid = sec_usage_core_tsk->pid;
	BUG_ON(pid < 0);

	init_flag = 1;

	return 0;
}
