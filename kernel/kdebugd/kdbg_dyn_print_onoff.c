/*
 * kdbg_dyn_print_onoff.c
 *
 * Copyright (C) 2009 Samsung Electronics
 *
 * Created by vivek.kumar@samsung.com
 *
 * NOTE: Dynamically on/off prints of a user process.
 *
 */

#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <kdebugd/kdebugd.h>

/* Maximum tasks on which print filter can be applied */
#define KDBG_MAX_PRINTS_ENABLE	5

/* Structure for storing print list */
struct kdbg_print_filter {
	pid_t pid;	/* pid of task */
	bool task;	/* 0-thread, 1-process */
};

typedef enum {
	SELECTIVE_ENABLE = 1,	/* Enable prints for tasks in print list */
	ENABLE_ALL,		/* Enable print for all tasks */
	DISABLE_ALL		/* Disable print for all tasks */
} kdbg_prints_status;

/* List that stores the pid of process whose prints will appear
 * on console. Used in tty_write function in drivers/tty/tty_io.c.
 * Note: This global structure is used without any locks, as there
 * is no problem of data race and inconsistency. This structure is written
 * in this file and read from drivers/tty/tty_io.c.
 */
static struct kdbg_print_filter g_print_filter[KDBG_MAX_PRINTS_ENABLE];

/* Print status: enable all, disable all, selective print */
static atomic_t g_prints_status = ATOMIC_INIT(ENABLE_ALL);

/* Get the task print status */
static inline kdbg_prints_status kdbg_get_print_status(void)
{
	return atomic_read(&g_prints_status);
}

/* Set the task print status */
static inline void kdbg_set_print_status(kdbg_prints_status status)
{
	atomic_set(&g_prints_status, status);
}

/* Kdebugd: Print/Not print on the console
 * depending on kdebugd's dynamic print status
 * @return: 0 - Do not write on console
 * 1 - Write on console
 */
#define SHELL_NAME	"sh"

int kdbg_print_enable(void)
{
	int kdbg_cnt = 0;
	/* All tasks prints disabled */
	int print = 0;

	/* All tasks prints enabled */
	if (kdbg_get_print_status() == ENABLE_ALL) {
		print = 1;
	/* Selective tasks enable from print list */
	} else if (kdbg_get_print_status() == SELECTIVE_ENABLE) {
		while (kdbg_cnt < KDBG_MAX_PRINTS_ENABLE &&
			g_print_filter[kdbg_cnt].pid) {
			/* Check processes */
			if (g_print_filter[kdbg_cnt].task) {
				if (current->tgid == g_print_filter[kdbg_cnt].pid) {
					print = 1;
					break;
				}
			/* Check for threads */
			} else {
				if (current->pid == g_print_filter[kdbg_cnt].pid) {
					print = 1;
					break;
				}
			}
			kdbg_cnt++ ;
		}
	}
	/* Always print in case of shell(sh) */
	if (!print) {
		if (unlikely(strncmp(current->comm, SHELL_NAME, strlen(SHELL_NAME))))
			print = 0;
		else
			print = 1;
	}

	return print;
}

/* Reset print list */
static void kdbg_dyn_prints_reset(void)
{
	int max_cnt = 0;

	while (max_cnt < KDBG_MAX_PRINTS_ENABLE) {
		g_print_filter[max_cnt].pid = 0;
		g_print_filter[max_cnt].task = 0;
		max_cnt++;
	}
}

/* Display print list */
static void kdbg_show_print_list(void)
{
	int max_cnt = 0;
	struct task_struct *tsk = NULL;

	PRINT_KD("\n");

	if (!g_print_filter[0].pid)
		PRINT_KD("Print list empty.\n");
	else {
		PRINT_KD("Task            Name               Pid\n");
		PRINT_KD("  |               |                 |\n");
		while (max_cnt < KDBG_MAX_PRINTS_ENABLE) {
			if (!g_print_filter[max_cnt].pid)
				break;
			rcu_read_lock();
			tsk = find_task_by_pid_ns(
					g_print_filter[max_cnt].pid,
					&init_pid_ns);
			rcu_read_unlock();
			if (g_print_filter[max_cnt].task)
				PRINT_KD("Process        ");
			else
				PRINT_KD("Thread         ");

			PRINT_KD("%-15.15s     %04d\n",
					tsk ? tsk->comm : "<th_dead>",
					g_print_filter[max_cnt].pid);
			max_cnt++;
		}
	}
}

/* Displays the dynamic prints configuration */
static void kdbg_dyn_print_conf(void)
{
	PRINT_KD("Print Status:");

	switch (kdbg_get_print_status()) {
	case ENABLE_ALL:
		PRINT_KD(" Enabled All.");
		break;
	case DISABLE_ALL:
		PRINT_KD(" Disabled All.");
		break;
	case SELECTIVE_ENABLE:
		PRINT_KD(" Enabled Selective.");
		break;
	default:
		break;
	}

	PRINT_KD(" (Configure using b/c/d)\n");
}


/* Add pid to dynamic print list
 * If task=0, add thread to print list
 * If task=1, add process to thread list*/
static void kdbg_add_to_print_list(bool task)
{
	int max_cnt = 0, count = 0;
	unsigned int event, tgid1, tgid2 = 0;
	struct task_struct *tsk = NULL;
	bool updated = 0;

	/* Check if print list array is full */
	while (max_cnt < KDBG_MAX_PRINTS_ENABLE) {
		if (!g_print_filter[max_cnt].pid)
			break;
		max_cnt++;
	}

	if (max_cnt == KDBG_MAX_PRINTS_ENABLE) {
		PRINT_KD("Maximum limit of %d tasks reached.\n",
					KDBG_MAX_PRINTS_ENABLE);
		return;
	}

	/* Input: User thread only */
	tsk = find_user_task_with_pid();
	if (!tsk)
		return;
	else {
		event = tsk->pid;
		tgid1 = tsk->tgid;
		/* Decrement usage count which is incremented in
		 * find_user_task_with_pid */
		put_task_struct(tsk);
	}

	PRINT_KD("\n");
	while (count < max_cnt) {
		/* If input is thread */
		if (!task) {
			if (((event == g_print_filter[count].pid)
					&& !g_print_filter[count].task) ||
					((tgid1 == g_print_filter[count].pid)
					 && g_print_filter[count].task)) {
				PRINT_KD("Duplicate entry found!!!\n");
				updated = 1;
				break;
			}
		/* If input is process */
		} else {
			if ((tgid1 == g_print_filter[count].pid) &&
					g_print_filter[count].task) {
				PRINT_KD("Duplicate entry found!!!\n");
				updated = 1;
				break;
			}

			rcu_read_lock();
			tsk = find_task_by_pid_ns(g_print_filter[count].pid,
					&init_pid_ns);
			/* tgid value of print list pid */
			if (tsk) {
				get_task_struct(tsk);
				tgid2 = tsk->tgid;
				put_task_struct(tsk);
			}
			rcu_read_unlock();

			/* Update thread entry to process entry */
			if ((tgid1 == tgid2) &&
					!g_print_filter[count].task) {
				if (!updated) {
					/* Update thread entry to process entry */
					g_print_filter[count].pid = tgid1;
					g_print_filter[count].task = task;
					updated = 1;
				} else {
					/* Remove other redundant thread entries */
					g_print_filter[count].pid = 0;
					g_print_filter[count].task = 0;
				}
			}
		}
		count++;
	}
	/* No duplicate entry found, no updation done.
	 * Add pid to print list */
	if (!updated) {
		/* Add pid in case of thread and tgid in case of process */
		if (!task)
			g_print_filter[max_cnt].pid = event;
		else
			g_print_filter[max_cnt].pid = tgid1;
		g_print_filter[max_cnt].task = task;
	}
}

int kdbg_printf_status(void)
{
	long menu;
	int is_number = 0;
	debugd_event_t event;

	do {
		PRINT_KD("\n");
		PRINT_KD("--------------------------------------------------------\n");
		kdbg_dyn_print_conf();
		PRINT_KD("--------------------------------------------------------\n");
		PRINT_KD("a)  Dyn Prints Config: Display Print List.\n");
		PRINT_KD("b)  Dyn Prints Config: Reset Print List.\n");
		PRINT_KD("c)  Dyn Prints Config: Add to Print List (Thread).\n");
		PRINT_KD("d)  Dyn Prints Config: Add to Print List (Process).\n");
		PRINT_KD("--------------------------------------------------------\n");
		PRINT_KD("1)  Dyn Prints: Enable Print List.\n");
		PRINT_KD("2)  Dyn Prints: Enable all prints.\n");
		PRINT_KD("3)  Dyn Prints: Disable all prints.\n");
		PRINT_KD("--------------------------------------------------------\n");
		PRINT_KD("99) Dyn Prints: Exit Menu.\n");
		PRINT_KD("--------------------------------------------------------\n");
		PRINT_KD("\n");
		PRINT_KD("Select Option==>  ");

		menu = debugd_get_event_as_numeric(&event, &is_number);
		PRINT_KD("\n");

		/* Remove case conflict, since 'c'==99 */
		if (!is_number) {
			if (strlen(event.input_string) > 1) {
				/* invalid input */
				menu = -1;
			} else {
				menu = toupper(event.input_string[0]);
			}
		}

		switch (menu) {
		case 'A':
			kdbg_show_print_list();
			break;
		case 'B':
			kdbg_dyn_prints_reset();
			kdbg_set_print_status(ENABLE_ALL);
			PRINT_KD("Dynamic Print list reset done.\n");
			break;
		case 'C':
			kdbg_add_to_print_list(0);
			break;
		case 'D':
			kdbg_add_to_print_list(1);
			break;
		case 1:
			/* Return in case print list is empty */
			if (!g_print_filter[0].pid)
				PRINT_KD("Print list empty!!! Configure print list first.\n");
			else {
				kdbg_set_print_status(SELECTIVE_ENABLE);
				PRINT_KD("Prints enabled for tasks in print list\n");
			}
			break;
		case 2:
			kdbg_set_print_status(ENABLE_ALL);
			PRINT_KD("Prints enabled for all tasks.\n");
			break;
		case 3:
			kdbg_set_print_status(DISABLE_ALL);
			PRINT_KD("Prints disabled for all tasks.\n");
			break;
		case 99:
			PRINT_KD("Dyn Prints: Exit.\n");
			break;

		default:
			PRINT_KD("Invalid Choice\n");
		}
	} while (menu != 99);

	return 1;
}

