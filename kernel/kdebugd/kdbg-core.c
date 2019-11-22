/*
 * kdbg-core.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee jung-seung(js07.lee@samsung.com)
 *
 * NOTE:
 *
 */
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>

#include <kdebugd.h>
#include "kdbg-version.h"

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
#include "sec_cpufreq.h"
#include "sec_cpuusage.h"
#include "sec_memusage.h"
#include "sec_netusage.h"
#include "sec_topthread.h"
#include "sec_diskusage.h"
#ifdef CONFIG_CACHE_ANALYZER
#include "sec_perfcounters.h"
#endif
#endif

#ifdef CONFIG_KDEBUGD_LIFE_TEST
#include "kdbg_key_test_player.h"
#endif

/* Kdbg_auto_key_test */
#include <linux/completion.h>
#include <linux/delay.h>
#define AUTO_KET_TEST_TOGGLE_KEY "987"
#ifdef CONFIG_KDEBUGD_BG
#define AUTO_KET_TEST_TOGGLE_KEY_BG_FG "bgtofg"
#define AUTO_KET_TEST_TOGGLE_KEY_BG "bgstart"
#endif
/* Kdbg_auto_key_test */

#ifdef CONFIG_KDEBUGD_FTRACE
#include <trace/kdbg_ftrace_helper.h>
#endif

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#include <agent/tvis_agent_debug.h>
#include <agent/tvis_agent_packet.h>
#include <agent/tvis_agent_cmds.h>
#include <agent/agent_core.h>
#include <agent/agent_cm.h>
#ifdef CONFIG_KDEBUGD_THREAD_PROFILER
#include <agent/kdbg_thread_profiler.h>
#endif

uint32_t common_debug_level = COMMON_DBG_ERR | COMMON_DBG_INFO;
#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

static DECLARE_WAIT_QUEUE_HEAD(kdebugd_wait);
static DEFINE_SPINLOCK(kdebugd_queue_lock);
struct debugd_queue kdebugd_queue;

struct task_struct *kdebugd_tsk;
unsigned int kdebugd_running;
#ifdef CONFIG_KDEBUGD_BG
unsigned int kdbg_mode;
#endif

struct task_struct *get_task_with_pid(void);

/* Denotes what action need to be taken on <TAB> key */
atomic_t g_tab_action = ATOMIC_INIT(KDBG_TAB_NO_ACTION);

void kdbg_reg_sys_event_handler(int (*psys_event_handler) (debugd_event_t *));

/* Get action executed on pressing TAB key */
KDBG_TAB_ACTION kdbg_get_tab_action(void)
{
	return atomic_read(&g_tab_action);
}
/* Set action executed on pressing TAB key */
void kdbg_set_tab_action(KDBG_TAB_ACTION action)
{
	atomic_set(&g_tab_action, action);
}

struct proc_dir_entry *kdebugd_dir;

/* System event handler call back function */
static int (*g_pKdbgSysEventHandler) (debugd_event_t *event);
/*
 * Debugd event queue management.
 */
static inline int queue_empty(struct debugd_queue *q)
{
	return q->event_head == q->event_tail;
}

static inline void queue_get_event(struct debugd_queue *q,
				   debugd_event_t *event)
{
	BUG_ON(!event);
	q->event_tail = (q->event_tail + 1) % DEBUGD_MAX_EVENTS;
	*event = q->events[q->event_tail];
}

void queue_add_event(struct debugd_queue *q, debugd_event_t *event)
{
	unsigned long flags;

	BUG_ON(!event);
	spin_lock_irqsave(&kdebugd_queue_lock, flags);
	q->event_head = (q->event_head + 1) % DEBUGD_MAX_EVENTS;
	if (q->event_head == q->event_tail) {
		static int notified;

		if (notified == 0) {
			PRINT_KD("\n");
			PRINT_KD("kdebugd: an event queue overflowed\n");
			notified = 1;
		}
		q->event_tail = (q->event_tail + 1) % DEBUGD_MAX_EVENTS;
	}
	q->events[q->event_head] = *event;
	spin_unlock_irqrestore(&kdebugd_queue_lock, flags);
	wake_up_interruptible(&kdebugd_wait);
}

/* Register system event handler */
void kdbg_reg_sys_event_handler(int (*psys_event_handler) (debugd_event_t *))
{
	if (!g_pKdbgSysEventHandler) {
		g_pKdbgSysEventHandler = psys_event_handler;
		PRINT_KD("Registering System Event Handler\n");
	} else if (!psys_event_handler) {
		g_pKdbgSysEventHandler = NULL;
		PRINT_KD("Deregistering System Event Handler\n");
	} else {
		PRINT_KD("ERROR: System Event Handler is Already Enabled\n");
	}
}

/*
 * debugd_get_event_as_numeric
 *
 * Description
 * This API returns the input given by user as long value which can be
 * processed by kdebugd developers.
 * and If user enters a invalid value i.e. which can not be converted to string,
 * is_number is set to zero and entered value is returned as a string
 * i.e. event->input_string.
 *
 * Usage
 * debugd_event_t event;
 * int is_number;
 * int option;
 *
 * option = debugd_get_event_as_numeric(&event, &is_number);
 * if (is_number) {
 *	valid long value, process it i.e. option
 * } else {
 * printk("Invalid Value %s\n", event.input_string);
 * }
 */
long debugd_get_event_as_numeric(debugd_event_t *event, int *is_number)
{
	debugd_event_t temp_event;
	long value = 0;
	char *ptr_end = NULL;
	int is_number_flag = 1;
	unsigned int base = 10;

	debugd_get_event(&temp_event);

	/* convert to numeric */
	if (temp_event.input_string[0] == '0'
	    && temp_event.input_string[1] == 'x') {
		base = 16;	/* hex */
	} else {
		base = 10;	/* decimal */
	}
	value = simple_strtol(temp_event.input_string, &ptr_end, base);

	/* check if pure number */
	if (!ptr_end || *ptr_end || ptr_end == temp_event.input_string) {
		value = -1L;
		is_number_flag = 0;
	}

	/* output parameters */
	if (is_number)
		*is_number = is_number_flag;

	if (event)
		*event = temp_event;

	return value;
}

/*
 * debugd_get_event
 *
 * Description
 * This API returns the input given by user as event and no checks are
 * performed on input.
 * It is returned to user as string in i.e. event->input_string.
 *
 * Usage
 * debugd_event_t event;
 * debugd_get_event(&event);
 * process it event.input_string
 * printk("Value %s\n", event.input_string);
 */
void debugd_get_event(debugd_event_t *event)
{
	debugd_event_t temp_event;

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	int ret = 1;

	while (1) {
#endif
		wait_event_interruptible(kdebugd_wait,
					 !queue_empty(&kdebugd_queue)
					 || kthread_should_stop());
		spin_lock_irq(&kdebugd_queue_lock);
		if (!queue_empty(&kdebugd_queue))
			queue_get_event(&kdebugd_queue, &temp_event);
		else
			temp_event.input_string[0] = '\0';

		spin_unlock_irq(&kdebugd_queue_lock);

#ifdef CONFIG_KDEBUGD_LIFE_TEST
		if (g_pKdbgSysEventHandler) {
			ret = (*g_pKdbgSysEventHandler) (&temp_event);
			if (ret)
				break;
		} else
			break;
	}
#endif

	if (event)
		*event = temp_event;
}

#define KDBG_MAX_TASK_PER_COL	4

/* IMP: Special case for those tasks whose name
 * ends with a space, e.g. "[BT]Compositor ".
 * If the flag is set, task_name has a space at the end.*/
static int kdbg_space_append_task;

/* kdbg_handle_tab_task()
 * @description: Handles the input given from TAB
 * feature of kdebugd.
 * @return void.
 */
static void kdbg_handle_tab_task(void)
{
	struct task_struct *g, *p;
	int task = 0;
	pid_t pid = -1;

	/* Remove additional space if added
	 * because of TAB feature */
	if (kdbg_buf[strlen(kdbg_buf) - 1] == ' ')
		kdbg_buf[strlen(kdbg_buf) - 1] = '\0';

	read_lock(&tasklist_lock);
	do_each_thread(g, p)
	{
		if ((strlen(p->comm) == strlen(kdbg_buf)) &&
				!strncmp(p->comm, kdbg_buf, strlen(p->comm))) {
			task++;
			pid = p->pid;
		}
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

	if (task == 1) {
		snprintf(kdbg_buf, sizeof(kdbg_buf), "%d", pid);
	/* IMP: Special case for those tasks whose name
	 * ends with a space, e.g. "[BT]Compositor ". */
	} else if ((task > 1) && (kdbg_buf[strlen(kdbg_buf) - 1] == ' '))
		kdbg_space_append_task = 1;
	else
		return;
}

/* kdbg_enter_action()
 * @description: What to do when you press enter
 * key from kdebugd.
 * @return: void
 */
void kdbg_enter_action(void)
{
	KDBG_TAB_ACTION action = kdbg_get_tab_action();

	switch (action) {
	case KDBG_TAB_SHOW_TASK:
		kdbg_handle_tab_task();
		break;
	default:
		break;
	}
}

/* kdbg_enter_special_action()
 * @description: handle special cases of enter key
 * @return: void
 */
void kdbg_enter_special_action(void)
{
	KDBG_TAB_ACTION action = kdbg_get_tab_action();

	switch (action) {
	case KDBG_TAB_SHOW_TASK:
		/* IMP: Special case for those tasks whose name
		 * ends with a space, e.g. "[BT]Compositor ".
		 * Re-insert space at the end of task name */
		if (kdbg_space_append_task) {
			kdbg_buf[strlen(kdbg_buf)] = ' ';
			kdbg_space_append_task = 0;
		}
		break;
	default:
		break;
	}
}

/* kdbg_substr_size()
 * @description: Finds the length of substring common
 * between two task->comm's passed in as argument.
 * @return: Length of common substring.
 */
static int kdbg_substr_size(char *a, char *b)
{
	int i = -1;
	int common = 0;
	while (++i < TASK_COMM_LEN) {
		if ((*(a+i) == *(b+i)) && *(a+i) != '\0')
			common++;
		else
			break;
	}
	return common;
}

#define MAX_TAB_OUT_LEN 24
/*
 * kdbg_tab_show_task()
 * @description: Displays current pids and task names to choose from.
 * @return: void.
 */
static void kdbg_tab_show_task(void)
{
	int col = 0;
	size_t size = 0, least_size = 0, max_size = 0;
	char pid[8] = "\0";
	char task[TASK_COMM_LEN] = "\0";
	char tab_out[MAX_TAB_OUT_LEN] = "\0";
	struct task_struct *g, *p;
	bool first_time = 1;

	PRINT_KD("\n");
	read_lock(&tasklist_lock);
	/* Dump pid list first */
	do_each_thread(g, p)
	{
		snprintf(pid, 8, "%d", p->pid);
		if (!strncmp(pid, kdbg_buf, strlen(kdbg_buf))
				|| !strncmp(p->comm, kdbg_buf, strlen(kdbg_buf))) {
			/* Find the length of substring common between two tasks */
			if (task[0] && (first_time || (least_size > 0))) {
				first_time = 0;
				size = (size_t)kdbg_substr_size(pid, task);
				if (!least_size || (least_size > size))
					least_size = size;
			}
			size = strlen(pid);
			if (max_size < size)
				max_size = size;

			strncpy(task, pid, strlen(pid));
			if(!p->mm) /* Kernel Thread */
				snprintf(tab_out, MAX_TAB_OUT_LEN, "[K]%d|%s", p->pid, p->comm);
			else if(p->tgid == p->pid) /* User Process */
				snprintf(tab_out, MAX_TAB_OUT_LEN, "[P]%d|%s", p->pid, p->comm);
			else /* User Thread */
				snprintf(tab_out, MAX_TAB_OUT_LEN, "[T]%d|%s", p->pid, p->comm);

			PRINT_KD("%-23s", tab_out);
			if (++col % KDBG_MAX_TASK_PER_COL == 0)
				PRINT_KD("\n");
		}
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

	/* Auto-complete */
	if (col == 1 || ((col > 0) && (least_size == max_size))) {
		memset(kdbg_buf, '\0', sizeof(kdbg_buf));
		strncpy(kdbg_buf, task, max_size);
		kdbg_buf[strlen(kdbg_buf)] = ' ';
	} else if (least_size) {
		memset(kdbg_buf, '\0', sizeof(kdbg_buf));
		strncpy(kdbg_buf, task, least_size);
	}

	PRINT_KD("\n");
	PRINT_KD("===> %s", kdbg_buf);
}

/*
 * kdbg_tab_action()
 * @description: Displays current pids and task names to choose from.
 * @return: void.
 */
void kdbg_tab_action(void)
{
	KDBG_TAB_ACTION action = kdbg_get_tab_action();

	switch (action) {
	case KDBG_TAB_SHOW_TASK:
		kdbg_tab_show_task();
		break;
#ifdef CONFIG_KDEBUGD_FTRACE
	case KDBG_TAB_FTRACE:
		kdbg_ftrace_handle_tab_event();
		break;
#endif
	default:
		break;
	}
}

/* kdbg_tab_process_event()
 * @description: Handle multiple threads with similar names.
 * @return: pid of selected task.
 */
long kdbg_tab_process_event(debugd_event_t *event)
{
	struct task_struct *g, *p;
	struct task_struct *tsk;
	debugd_event_t core_event;
	static long ret = -1L;
	pid_t pid = 0;
	int task = 0;

	read_lock(&tasklist_lock);
	/* Check for tasks with similar names */
	do_each_thread(g, p)
	{
		if ((strlen(p->comm) == strlen(event->input_string)) &&
			!strncmp(p->comm, event->input_string, strlen(p->comm))) {
			pid = p->pid;
			task++;
		}
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

	if (!task) /* No task found, with that name */
		ret = -1L;
	else if (task == 1) /* Only one task, with that name */
		ret = pid;
	else { /* More than one task found, with same name */
		task = 0;
		PRINT_KD("\n\n");
		PRINT_KD("Similar task-names with different pids found!!!\n");
		read_lock(&tasklist_lock);
		do_each_thread(g, p)
		{
			if ((strlen(p->comm) == strlen(event->input_string)) &&
					!strncmp(p->comm, event->input_string, strlen(p->comm))) {
				PRINT_KD("%-16d", p->pid);
				if (++task % KDBG_MAX_TASK_PER_COL == 0)
					PRINT_KD("\n");
			}
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);

		PRINT_KD("\n");
		PRINT_KD("Enter pid of task...\n");
		PRINT_KD("===>  ");
		ret = debugd_get_event_as_numeric(&core_event, NULL);
		if (ret < 0)
			ret = kdbg_tab_process_event(&core_event);
		else {
			tsk = get_task_with_given_pid(ret);
			if (!tsk || strncmp(tsk->comm, event->input_string, strlen(event->input_string)))
				ret = -1L;
		}
	}
	return ret;
}

/* command input common code */
/* Note: After getting the task, task lock has been taken inside this function
 * Use put_task_struct for the task if lock get succesfully
 */
struct task_struct *get_task_with_pid(void)
{
	struct task_struct *tsk;
	debugd_event_t core_event;
	long event;

	PRINT_KD("\n");
	PRINT_KD("Enter pid/name of task...\n");
	PRINT_KD("===>  ");

	/* Enable tab feature of kdebugd */
	kdbg_set_tab_action(KDBG_TAB_SHOW_TASK);
	event = debugd_get_event_as_numeric(&core_event, NULL);
	if (event < 0)
		event = kdbg_tab_process_event(&core_event);
	kdbg_set_tab_action(KDBG_TAB_NO_ACTION);

	PRINT_KD("\n");

	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	/*Take RCU read lock register can be changed */
	rcu_read_lock();

	tsk = find_task_by_pid_ns(event, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL)
		return NULL;

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s", tsk->pid, tsk->comm);
#ifdef CONFIG_SMP
	PRINT_KD("[%d]", task_cpu(tsk));
#endif
	PRINT_KD("\n");
	return tsk;
}

/* command input common code */
struct task_struct *get_task_with_given_pid(pid_t pid)
{
	struct task_struct *tsk;

	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */

	/*Take RCU read lock register can be changed */
	rcu_read_lock();

	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL)
		return NULL;

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s\n", tsk->pid, tsk->comm);
	return tsk;
}

struct task_struct *find_user_task_with_pid(void)
{
	struct task_struct *tsk;
	debugd_event_t core_event;
	long event;

	PRINT_KD("\n");
	PRINT_KD("Enter pid/name of task...\n");
	PRINT_KD("===>  ");

	/* Enable tab feature of kdebugd */
	kdbg_set_tab_action(KDBG_TAB_SHOW_TASK);
	event = debugd_get_event_as_numeric(&core_event, NULL);
	if (event < 0)
		event = kdbg_tab_process_event(&core_event);
	kdbg_set_tab_action(KDBG_TAB_NO_ACTION);

	PRINT_KD("\n");

	rcu_read_lock();

	tsk = find_task_by_pid_ns(event, &init_pid_ns);

	if (tsk)
		get_task_struct(tsk);
	/*Unlock */
	rcu_read_unlock();

	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("\n[ALERT] %s Thread",
			 (tsk == NULL) ? "No" : "Kernel");
		if (tsk)
			put_task_struct(tsk);
		return NULL;
	}

	PRINT_KD("\n\n");
	PRINT_KD("Pid: %d, comm: %20s", tsk->pid, tsk->comm);
#ifdef CONFIG_SMP
	PRINT_KD("[%d]", task_cpu(tsk));
#endif
	PRINT_KD("\n");

	return tsk;
}

void task_state_help(void)
{
	PRINT_KD("\nRSDTtZXxKW:\n");
	PRINT_KD
	("R : TASK_RUNNING       S:TASK_INTERRUPTIBLE   D:TASK_UNITERRUPTIBLE\n");
	PRINT_KD
	("T : TASK_STOPPED       t:TASK_TRACED          Z:EXIT_ZOMBIE\n");
	PRINT_KD
	("X : EXIT_DEAD          x:TASK_DEAD            K:TASK_WAKEKILL \n");
	PRINT_KD
	("W : TASK_WAKEING \n");

	PRINT_KD("\nSched Policy\n");
	PRINT_KD("SCHED_NORMAL : %d\n", SCHED_NORMAL);
	PRINT_KD("SCHED_FIFO   : %d\n", SCHED_FIFO);
	PRINT_KD("SCHED_RR     : %d\n", SCHED_RR);
	PRINT_KD("SCHED_BATCH  : %d\n", SCHED_BATCH);
}

/*
 *    uptime
 */

unsigned long kdbg_get_uptime(void)
{
	struct timespec uptime;

	do_posix_clock_monotonic_gettime(&uptime);

	return (unsigned long)uptime.tv_sec;
}


/*
 *    kdebugd()
 */

struct kdbg_entry {
	const char *name;
	int (*execute) (void);
	void (*turnoff) (void);
	KDBG_MENU_NUM menu_index;
};

struct kdbg_base {
	unsigned int index;
	struct kdbg_entry entry[KDBG_MENU_MAX];
};

static struct kdbg_base k_base;

int kdbg_register(char *name, int (*func) (void), void (*turnoff) (void),
		  KDBG_MENU_NUM menu_idx)
{
	unsigned int idx = k_base.index;
	struct kdbg_entry *cur;

	if (!name || !func) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Invalid params, name %p, func %p !!!\n", name,
			 func);
		return -ENOMEM;
	}

	if (idx >= KDBG_MENU_MAX) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not add kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -ENOMEM;
	}

	cur = &(k_base.entry[idx]);

	cur->name = name;
	cur->execute = func;
	cur->turnoff = turnoff;
	cur->menu_index = menu_idx;
	k_base.index++;

	return 0;
}

int kdbg_unregister(KDBG_MENU_NUM menu_idx)
{
	struct kdbg_entry *cur;
	int menu_found = 0;
	unsigned int i;

	if (menu_idx >= KDBG_MENU_MAX) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not remove kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -ENOMEM;
	}

	for (i = 0; i < k_base.index; i++) {
		if (menu_idx == k_base.entry[i].menu_index) {
			cur = &(k_base.entry[i]);
			/* reset execute function pointer */
			cur->execute = NULL;
			menu_found = 1;
			break;
		}
	}

	if (!menu_found) {
		PRINT_KD(KERN_ERR
			 "[ERROR] Can not remove kdebugd function, menu_idx %d !!!\n",
			 menu_idx);
		return -1;
	}

	return 0;
}

static void debugd_menu(void)
{
	unsigned int i;
	PRINT_KD("\n");
	PRINT_KD
		(" --- Menu --------------------------------------------------------------\n");
	PRINT_KD(" Select Kernel Debugging Category. - %s\n", KDBG_VERSION);
	for (i = 0; i < k_base.index; i++) {
		if (i % 4 == 0)
			PRINT_KD
			    (" -----------------------------------------------------------------------\n");
		if (k_base.entry[i].execute != NULL) {
			PRINT_KD(" %-2d) %s.\n", k_base.entry[i].menu_index,
				 k_base.entry[i].name);
		}
	}
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD(" 99) exit\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");
	PRINT_KD
	    (" Samsung Electronic - DMC - VD Division - S/W Platform Lab1 - Linux Part\n");
	PRINT_KD
	    (" -----------------------------------------------------------------------\n");

}

static int kdebugd(void *arg)
{
	long event;
	unsigned int idx;
	unsigned int i = 0;
	int menu_flag = 1;
	int menu_found = 0;

	do {
		if (kthread_should_stop())
			break;

		if (menu_flag) {
			debugd_menu();
			PRINT_KD("%s #> ", KDEBUGD_STRING);
		} else {
			menu_flag = 1;
		}

		event = debugd_get_event_as_numeric(NULL, NULL);

		idx = (unsigned int)event - 1;
		PRINT_KD("\n");

		/* exit */
		if (event == 99)
			break;

		/* execute operations */
		for (i = 0; i < k_base.index; i++) {
			if (event == (long)(k_base.entry[i].menu_index)) {
				if (k_base.entry[i].execute != NULL) {
					PRINT_KD("[kdebugd] %d. %s\n",
						 k_base.entry[i].menu_index,
						 k_base.entry[i].name);
					menu_flag =
					    (*k_base.entry[i].execute) ();
				}
				menu_found = 1;
				break;
			}
		}

		if (!menu_found) {
			for (idx = 0; idx < k_base.index; idx++) {
				if (k_base.entry[idx].turnoff != NULL)
					(*k_base.entry[idx].turnoff) ();
			}
		}

		menu_found = 0;
	} while (1);

	PRINT_KD("\n");
	PRINT_KD("[kdebugd] Kdebugd Exit....");
	kdebugd_running = 0;

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	kdbg_stop_key_test_player_thread();
#endif

	return 0;
}

int kdebugd_start(void)
{
	int ret = 0;

	kdebugd_tsk = kthread_create(kdebugd, NULL, "kdebugd");
	if (IS_ERR(kdebugd_tsk)) {
		ret = PTR_ERR(kdebugd_tsk);
		kdebugd_tsk = NULL;
		return ret;
	}

	kdebugd_tsk->flags |= PF_NOFREEZE;
	wake_up_process(kdebugd_tsk);

	return ret;
}

int kdebugd_status(void)
{
	int key;

	while (1) {

		PRINT_KD
		    ("------------------ COUNTER MONITOR STATUS -------------------\n");
		PRINT_KD
		    ("NUM   MONITOR               INIT STATE         RUN STATE\n");
		PRINT_KD
		    ("=== ===================== ===============    ============\n");

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		PRINT_KD("1   CPU USAGE             ");
		sec_cpuusage_get_status();
		PRINT_KD("2   TOP THREAD            ");
		sec_topthread_get_status();
		PRINT_KD("3   MEM USAGE             ");
		get_memusage_status();
		PRINT_KD("4   NET USAGE             ");
		get_netusage_status();
		PRINT_KD("5   DISK USAGE            ");
		get_diskusage_status();
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
		PRINT_KD("6   PMU_EVENTS            ");
		get_perfcounters_status();
#endif /* CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS */
#endif

#ifdef CONFIG_SCHED_HISTORY
#ifdef CONFIG_CACHE_ANALYZER
		PRINT_KD("7   SCHED HISTORY LOGGER  ");
#else
		PRINT_KD("6   SCHED HISTORY LOGGER  ");
#endif
		status_sched_history();
#endif
#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		PRINT_KD("8   CPU FREQUENCY         ");
		sec_cpufreq_get_status();
#endif
		PRINT_KD("99- Exit\n");
		PRINT_KD
		    ("------------------------- STATUS END ------------------------\n");
		PRINT_KD("*HELP*\n");
		PRINT_KD
		    ("  A) To Enable print and dump the respective Counter Monitor go to the kdebugd menu "
		     "and give the corresponding options\n");
		PRINT_KD
		    ("  B) [TurnOn] -->Initialize the feature and run in the background\n");
		PRINT_KD
		    ("  C) [TurnOff] -->Free the resources occoupied by the functionality\n\n");
		PRINT_KD("To Turn On/off feature enter corresponding number\n");
		PRINT_KD("-->");
		key = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (key) {

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		case 1:
			{
				sec_cpuusage_on_off();
			}
			break;
		case 2:
			{
				sec_topthread_on_off();
			}
			break;

		case 3:
			{
				sec_memusage_OnOff();
			}
			break;
		case 4:
			{
				sec_netusage_OnOff();
			}
			break;
		case 5:
			{
				sec_diskusage_OnOff();
			}
			break;
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
		case 6:
			{
				sec_perfcounters_OnOff();
			}
			break;
#endif /* CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS */
#endif
#ifdef CONFIG_SCHED_HISTORY
#ifdef CONFIG_CACHE_ANALYZER
		case 7:
#else
		case 6:
#endif
			{
				sched_history_OnOff();
			}
			break;
#endif
#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
		case 8:
			{
				sec_cpu_freq_on_off();
			}
			break;
#endif
		case 99:
			{
				PRINT_KD("Exiting...\n");
				return 1;
			}
		default:
			{
				PRINT_KD("Invalid Choice\n");
				break;
			}
		}
	}
	return 1;
}

/* Kdbg_auto_key_test */

/**********************************************************************
 *                                                                    *
 *              proc related functions                                *
 *                                                                    *
 **********************************************************************/

#define KDBG_AUTO_KEY_TEST_NAME	"kdbgd/auto_key_test"

/**
 * The buffer used to store character for this module
 *
 */
static char kdbg_auto_key_test_buffer[DEBUGD_MAX_CHARS];

/**
 * The size of the buffer
 *
 */
static int kdbg_auto_key_test_buffer_size;

/*auto key test activate*/
static int kdbg_auto_key_test_activate(void);

/* for /proc/kdbeugd/auto_key_test writing */
static int kdbg_auto_key_test_write(struct file *file, const char __user *buffer, size_t count, loff_t *data);

/* for /proc/kdbeugd/auto_key_test  reading */
static int kdbg_auto_key_test_read(struct file *pfile, char __user *buffer, size_t count, loff_t *offset);

/*auto key test activate*/
static const struct file_operations kdbg_auto_key_test_ops = {
    .read = kdbg_auto_key_test_read,
    .write = kdbg_auto_key_test_write,
};
static int kdbg_auto_key_test_activate(void)
{
	static struct proc_dir_entry *kdbg_auto_key_test_proc_dentry;
	int err = 0;
	if (!kdbg_auto_key_test_proc_dentry) {
		kdbg_auto_key_test_proc_dentry = proc_create(KDBG_AUTO_KEY_TEST_NAME, 0,
								NULL, &kdbg_auto_key_test_ops);
		if (kdbg_auto_key_test_proc_dentry == NULL) {
			printk(KERN_WARNING
					"/proc/kdebugd/auto_key_test creation failed\n");
			err = -ENOMEM;
		}
	}
	return err;
}

/**
 * This function is called with the /proc file is read
 *
 */
static int kdbg_auto_key_test_read(struct file *pfile, char __user *buffer, size_t count, loff_t *offset)
{
	int ret;

	if (*offset > 0) {
		/* we have finished to read, return 0 */
		ret = 0;
	} else {
		/* fill the buffer, return the buffer size */
		if (copy_to_user(buffer, kdbg_auto_key_test_buffer,
				(unsigned long)kdbg_auto_key_test_buffer_size)) {
			return -EFAULT;
		}

		ret = kdbg_auto_key_test_buffer_size;
	}

	return ret;
}

/**
 * This function is called with the /proc file is written
 *
 */
static int kdbg_auto_key_test_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *data)
{
	debugd_event_t event;
	char *ptr = NULL;

	/* get buffer size */
	if (count > DEBUGD_MAX_CHARS)
		kdbg_auto_key_test_buffer_size = DEBUGD_MAX_CHARS;
	else
		kdbg_auto_key_test_buffer_size = (int)count;

	/* write data to the buffer */
	if (copy_from_user
	    (kdbg_auto_key_test_buffer, buffer,
	     (unsigned long)kdbg_auto_key_test_buffer_size)) {
		return -EFAULT;
	}
	kdbg_auto_key_test_buffer[kdbg_auto_key_test_buffer_size - 1] = '\0';

	/* remove leading and trailing whitespaces */
	ptr = strstrip(kdbg_auto_key_test_buffer);

	BUG_ON(!ptr);

	if (!strncmp(ptr, "NO_WAIT", sizeof("NO_WAIT") - 1)) {
		ptr += sizeof("NO_WAIT") - 1;
	} else {
		/* wait till queue is empty */
		while (!queue_empty(&kdebugd_queue))
			msleep(200);
	}

	/* create a event */
	strncpy(event.input_string, ptr, sizeof(event.input_string) - 1);
	event.input_string[sizeof(event.input_string) - 1] = '\0';
#ifdef CONFIG_KDEBUGD_BG
	if (!strncmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG_FG, strlen(AUTO_KET_TEST_TOGGLE_KEY_BG_FG)))
		kdbg_mode = 0;
#endif

	/* Magic key to start/stop the thread */
#ifdef CONFIG_KDEBUGD_BG
	if (!strncmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY, strlen(AUTO_KET_TEST_TOGGLE_KEY))
				|| !strncmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG, strlen(AUTO_KET_TEST_TOGGLE_KEY_BG))) {
#else
	if (!strncmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY, strlen(AUTO_KET_TEST_TOGGLE_KEY))) {
#endif
		if (!kdebugd_running) {
			kdebugd_start();
			kdebugd_running = 1;
#ifdef CONFIG_KDEBUGD_BG
			if (!strncmp(event.input_string, AUTO_KET_TEST_TOGGLE_KEY_BG, strlen(AUTO_KET_TEST_TOGGLE_KEY_BG)))
				kdbg_mode = 1;
#endif
		} else {
			debugd_event_t temp_event = { "99"};
			do {
				/* kdebugd menu exit */
				queue_add_event(&kdebugd_queue, &temp_event);
				msleep(2000);
			} while (kdebugd_running);
		}
	} else if (kdebugd_running) {
		queue_add_event(&kdebugd_queue, &event);
	}

	return kdbg_auto_key_test_buffer_size;
}

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT


/* Kdbg_auto_key_test */


#define KDBG_AGENT_PROC_NAME	"kdbgd/cmd_queue"
/*
 * The buffer used to store character for this module
 *
 */
static char kdbg_proc_cmd_buffer[DEBUGD_MAX_CHARS];

/*
 * The size of the buffer
 *
 */
static int kdbg_proc_cmd_buffer_size;

/* Agent command handler function */
static void kernel_tool_cmd_handler(void *data, int len);
static void kdbg_cmd_handler(void *data, int len);

/*
 * This function can be called from userspace to get the
 * data that kernel tool wants to send to userspace.
 *
 */
static int kdbg_proc_cmd_handler_read(struct file *pfile, char __user *buffer, size_t count, loff_t *offset)
{
	int ret;

	if (*offset > 0) {
		/* we have finished to read, return 0 */
		ret = 0;
	} else {

		if (kdbg_proc_cmd_buffer_size < 0)
			return -EFAULT;

		/* fill the buffer, return the buffer size */
		if (copy_to_user(buffer, kdbg_proc_cmd_buffer,
		       (unsigned long)kdbg_proc_cmd_buffer_size))
			return -EFAULT;

		ret = kdbg_proc_cmd_buffer_size;
	}

	return ret;

}

void tvis_agent_hexdump(char *buf, int len)
{
#if 1
	int ofs = 0;
	int ctr = 0;

	/*
	 * Other option would be to add conditional print.
	 * We don't want any overhead if COMMON_DBG_PACKET is not enabled.
	 * So, just check the variable and return.
	 */
	if (!(COMMON_DBG_PACKET & common_debug_level))
		return;

	common_debug(COMMON_DBG_PACKET, "Packet Dump\n");

	for (ofs = 0; ofs < len && len < DEBUGD_MAX_CHARS; ofs++) {
		if (!(ofs % 16)) {
			if (ofs)
				printk("\n");
			PRINT_KD("%03d: ", ctr++);
		}
		PRINT_KD("%02x ", (unsigned char)buf[ofs]);
	}
	PRINT_KD("\n");
#else
	char str[80], octet[10];
	int ofs, i, l;

	common_debug(COMMON_DBG_PACKET, "Packet Dump\n");
	for (ofs = 0; ofs < len && len < DEBUGD_MAX_CHARS; ofs += 16) {
		sprintf(str, "%03d: ", ofs);

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf(octet, "%02x ", buf[ofs + i]);
			else
				strcpy(octet, "   ");

			strcat(str, octet);
		}
		strcat(str, "  ");
		l = strlen(str);

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = isprint(buf[ofs + i]) ? buf[ofs + i] : '.';

		str[l] = '\0';
		PRINT_KD("%s\n", str);
	}
#endif
}

/*
 * This func recieves command from Agent and sends to kernel tools.
 */
static void kernel_tool_cmd_handler(void *data, int len)
{

	struct tool_header *t_hdr;

	t_hdr = (struct tool_header *) data;

	/* call the tool specific command handlers */
	if (t_hdr->cmd >= KDBG_CMD_START_KDEBUGD && t_hdr->cmd < KDBG_CMD_MAX)
		kdbg_cmd_handler(data, len);
	else
		common_debug(COMMON_DBG_ERR, "Command Not Supported.\n");
}


static void kdbg_cmd_handler(void *data, int len)
{

	struct tool_header *t_hdr;
	enum agent_cm tvis_event = CM_INVAL;

	t_hdr = (struct tool_header *) data;

	/* if data length, expecting first 4 bytes will be event */
	if (t_hdr->data_len) {
		memcpy(&tvis_event, (char *) data + TOOL_HDR_SIZE, sizeof(enum agent_cm));
	}

	switch (t_hdr->cmd) {
	case KDBG_CMD_START_KDEBUGD:
		{
			int ret = 0;
			PRINT_KD("Agent Started Called\n");
			ret = kdbg_agent_init();
			/* Initialzed the thread */
			if (ret >= 0) {
				kdbg_agent_set_mode(AGENT_MODE);
				START_AGENT();
			} else {
				PRINT_KD("Fail to initialize Agent thread for Kdebugd\n");
			}
		}
		break;

	case KDBG_CMD_STOP_KDEBUGD:
		{
			PRINT_KD("Agent Stopped Called\n");
			kdbg_agent_reset();
			STOP_AGENT();
		}
		break;

	case KDBG_CMD_GET_THREAD_INFO:
		{
			PRINT_KD("Thread info packet recived\n");
			PRINT_KD("Event is %d\n", tvis_event);
			kdbg_agent_threadlist_set_mode(tvis_event);
		}
		break;

	case KDBG_CMD_CM_CPU_USAGE:
		{
			PRINT_KD("CPU Usage packet recived\n");
			PRINT_KD("Event is %d !! Prev event is %d !!\n", tvis_event, agent_cpuusage_get_status());
			if (tvis_event != agent_cpuusage_get_status())
				agent_cpuusage_set_status(tvis_event);
		}
		break;

	case KDBG_CMD_GET_MEM_USAGE:
		{
			PRINT_KD("Memory Usage packet recived\n");
			if (tvis_event  != agent_memusage_get_status())
				agent_memusage_set_status (tvis_event);
		}
		break;

	case KDBG_CMD_CM_PHY_MEM_USAGE:
		{
			PRINT_KD("Physical meme usage packet recived\n");
			if (tvis_event != agent_phy_mem_get_status()) {
				agent_phy_mem_set_status(tvis_event);
				agent_phy_memusage_on_off();
			}
		}
		break;

	case KDBG_CMD_CM_THREAD_USAGE:
		{
			PRINT_KD("Thread Usage packet recived\n");
			if (tvis_event  != agent_topthread_get_status()) {
				agent_topthread_set_status(tvis_event);
				sec_topthread_on_off();
			}
		}
		break;

	case KDBG_CMD_CM_NET_USAGE:
		{
			PRINT_KD("Network Usage packet recived\n");
			if (tvis_event  != agent_netusage_get_status())
				agent_netusage_set_status(tvis_event);
		}
		break;

	case KDBG_CMD_CM_DISK_USAGE:
		{
			PRINT_KD("Disk Usage packet recived\n");
			if (tvis_event  != agent_diskusage_get_status())
				agent_diskusage_set_status(tvis_event);
		}
		break;

#ifdef CONFIG_KDEBUGD_THREAD_PROFILER
	case KDBG_CMD_START_THREAD_PROFILE_INFO:
		{
			int ret = 0;

			/* start the thread profiler */
			ret = kdbg_thread_profiler_start();
			if (ret < 0)
				PRINT_KD("Thread Profile Start Failed.\n");
			else
				PRINT_KD("Thread Profile Started.\n");

		}
		break;

	case KDBG_CMD_STOP_THREAD_PROFILE_INFO:
		/* stop the thread profiler */
		kdbg_thread_profiler_stop();
		PRINT_KD("Thread Profile Stopped.\n");
		break;
#endif
	case KDBG_CMD_CM_MEM_INFO:
		PRINT_KD("Meminfo packet recived\n");
		if (tvis_event != agent_meminfo_get_status())
			agent_meminfo_set_status(tvis_event);
		break;

	default:
		break;
	}

	return;
}

/*
 * This function is called when Agent writes commands to kernelspace.
 * And sent commands are queued into the kdebugd existing queue for.
 * commands.
 */
static int kdbg_proc_cmd_handler_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *data)
{
	struct tool_header *t_hdr;

	/* Any valid packet written on proc/kdebugd
	should have count, else ignore the packet */
	if (!count)
		return -EFAULT;

	/* get buffer size */
	if (count > DEBUGD_MAX_CHARS)
		kdbg_proc_cmd_buffer_size = DEBUGD_MAX_CHARS;
	else
		kdbg_proc_cmd_buffer_size = (int)count;

	/* write data to the buffer */
	if (copy_from_user
	    (kdbg_proc_cmd_buffer, buffer,
	     (unsigned long)kdbg_proc_cmd_buffer_size)) {
		return -EFAULT;
	}
	kdbg_proc_cmd_buffer[kdbg_proc_cmd_buffer_size - 1] = '\0';

	/* initialize the t_hdr with kdbg_proc_cmd_buffer */
	t_hdr = (struct tool_header *)((void *)kdbg_proc_cmd_buffer);

	common_debug(COMMON_DBG_NETWORK, "Rcvd Cmd %d, Data Len %d\n", t_hdr->cmd, t_hdr->data_len);
	/* dump packet */
	if (t_hdr->data_len)
		tvis_agent_hexdump((char *)t_hdr, (int)TOOL_HDR_SIZE + t_hdr->data_len);

	/* kernel tool command handler */
	kernel_tool_cmd_handler((void *)kdbg_proc_cmd_buffer, (int)TOOL_HDR_SIZE + t_hdr->data_len);

	return kdbg_proc_cmd_buffer_size;
}

static const struct file_operations kdbg_proc_cmd_handler_ops = {
    .read = kdbg_proc_cmd_handler_read,
    .write = kdbg_proc_cmd_handler_write,
};
static int kdbg_proc_cmd_handler_init(void)
{
	static struct proc_dir_entry *kdbg_proc_cmd_handler_dentry;
	int err = 0;

	if (!kdbg_proc_cmd_handler_dentry) {
		kdbg_proc_cmd_handler_dentry =
			proc_create(KDBG_AGENT_PROC_NAME, 0, NULL, &kdbg_proc_cmd_handler_ops);
		if (kdbg_proc_cmd_handler_dentry == NULL) {
			printk(KERN_WARNING
					"%s creation failed\n", KDBG_AGENT_PROC_NAME);
			err = -ENOMEM;
		}
	}

	return err;
}

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

static int __init kdebugd_init(void)
{
	int rv = 0;
	kdebugd_dir = proc_mkdir("kdbgd", NULL);
	if (kdebugd_dir == NULL)
		rv = -ENOMEM;

	/* Kdbg_auto_key_test */
	kdbg_auto_key_test_activate();

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
	kdbg_smack_control();
	/* Initialized proc command handler*/
	kdbg_proc_cmd_handler_init();

#endif

	return rv;
}

static void __exit kdebugd_cleanup(void)
{
	remove_proc_entry("kdbgd", NULL);
}

module_init(kdebugd_init);
module_exit(kdebugd_cleanup);
