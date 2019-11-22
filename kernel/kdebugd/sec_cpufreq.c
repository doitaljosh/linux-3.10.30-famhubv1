/*
 *  linux/kernel/sec_cpufreq.c
 *
 *  CPU frequency monitor,cpu frequency chnage releated functions
 *
 *  Copyright (C) 20013  Samsung
 *
 *  2013-07-03  Created by umesh.t@samsung.com.
 *
 */

#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <kdebugd.h>
#include "sec_cpufreq.h"
#include <linux/delay.h>
#include "kdbg_util.h"
#include <linux/tracepoint.h>
#include <trace/events/power.h>
#include <linux/kthread.h>
#include "../../../kernel/kdebugd/sec_workq.h"


/* The database that will store cpu frequency data data. */
struct sec_cpufreq_struct *cpufreq_monitor_db;

/* spinlock to protect event_database */
static spinlock_t kdbg_cpufreq_lock = __SPIN_LOCK_UNLOCKED(kdbg_cpufreq_lock);

/* Dump thread pointer */
struct task_struct *dump_thread;

/* Counter to print cpufreq header */
static int cpufreq_header_interval;

/* default system frequency */
static unsigned int system_default_freq;

/* To store previous entry */
static struct sec_cpufreq_struct prev_info_db;

/* To check tracepoint registered */
static int tp_registered;

/* State flag, reason for taking atomic is all the code is
 * running with state transition which is done by veriouse
 * thread in multiprocessor system keep accounting of this
 * variable is necessary */
static atomic_t g_sec_cpufreq_state = ATOMIC_INIT(E_NONE);

static int  cpufreq_monitor_db_init(void);
static inline void unregister_cpufreq_tracepoint(void);

/* Destroy implementation funciton used internaly */
static void sec_cpufreq_destroy_impl(void)
{
	/* stop running thread first */
	if (dump_thread) {
		kthread_stop(dump_thread);
		dump_thread = NULL;
	}

	/* Unregister tracepoint */
	if (tp_registered) {
		unregister_cpufreq_tracepoint();
		/* reset the flag */
		tp_registered = 0;
	}

	/* free memory */
	spin_lock(&kdbg_cpufreq_lock);

	if (cpufreq_monitor_db) {
		KDBG_MEM_DBG_KFREE(cpufreq_monitor_db);
		cpufreq_monitor_db = NULL;
	}
	spin_unlock(&kdbg_cpufreq_lock);

	/* reset cpufreq header interval */
	cpufreq_header_interval = 0;

	/* Reset default frequency parameter also */
	system_default_freq = 0;
}
/* Get the current state of cpufreq */
static inline sec_counter_mon_state_t sec_cpufreq_get_state(void)
{
	return atomic_read(&g_sec_cpufreq_state);
}

/* Set the new state of cpufreq */
static int sec_cpufreq_set_state_impl(sec_counter_mon_state_t new_state)
{
	int ret = 0;
	sec_counter_mon_state_t prev_state = sec_cpufreq_get_state();

	SEC_CPUFREQ_DEBUG("Changing state:%d --> %d\n", prev_state, new_state);

	switch (new_state) {

	case E_INITIALIZED:
		if (prev_state == E_NONE || prev_state == E_DESTROYED) {

			/* get current cpu frequency */
			system_default_freq = cpufreq_quick_get(0);
			if (!system_default_freq) {
				PRINT_KD("Failed to get current system frequency\n");
				return 0;
			}

			PRINT_KD("\n");
			PRINT_KD("[INFO] Current System Frequency: %d\n", system_default_freq);

			/* Init cpufreq database */
			ret = cpufreq_monitor_db_init();
			if (ret)
				break;

			/*ON init flag.. */
			atomic_set(&g_sec_cpufreq_state, E_INITIALIZED);
		} else if (prev_state == E_RUNNING
				|| prev_state == E_RUN_N_PRINT) {
			atomic_set(&g_sec_cpufreq_state, E_INITIALIZED);
		} else if (prev_state == E_INITIALIZED) {
			PRINT_KD("Already Initialized\n");
			ret = -ERR_DUPLICATE;
		} else {
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);     /* internal transition state */
			BUG_ON("Invalid cpu frequency state");
		}
		break;
	case E_RUNNING:
		if (prev_state == E_INITIALIZED) {
			atomic_set(&g_sec_cpufreq_state, E_RUNNING);
		} else if (prev_state == E_RUN_N_PRINT) {
			atomic_set(&g_sec_cpufreq_state, E_RUNNING);
		} else if (prev_state == E_RUNNING) {
			PRINT_KD("Already Running...\n");
			ret = -ERR_DUPLICATE;
		} else if (prev_state == E_NONE || prev_state == E_DESTROYED) {
			PRINT_KD("ERROR: %s: only one transition supported\n",
					__func__);
			ret = -ERR_NOT_SUPPORTED;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);     /* internal transition state */
			BUG_ON("Invalid cpu frequency state");
		}
		break;
	case E_RUN_N_PRINT:
		if (prev_state == E_RUNNING) {
			atomic_set(&g_sec_cpufreq_state, E_RUN_N_PRINT);
		} else if (prev_state == E_RUN_N_PRINT) {
			PRINT_KD("Already Running state\n");
			ret = -ERR_DUPLICATE;
		} else if (prev_state == E_NONE || prev_state == E_DESTROYED) {
			PRINT_KD("ERROR: %s: only one transition supported\n",
					__func__);
			ret = -ERR_NOT_SUPPORTED;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);     /* internal transition state */
			BUG_ON("Invalid cpu frequency state");
		}
		break;
	case E_DESTROYED:
		if (prev_state == E_INITIALIZED || prev_state == E_RUN_N_PRINT) {

			/* First set the state so that interrupt stop all the work */
			atomic_set(&g_sec_cpufreq_state, E_DESTROYING);
			sec_cpufreq_destroy_impl();
			atomic_set(&g_sec_cpufreq_state, E_DESTROYED);

		} else if (prev_state == E_DESTROYING) {
			PRINT_KD("Already Destroying state\n");
			ret = -ERR_DUPLICATE;
		} else {
			ret = -ERR_INVALID;
			/* TODO: WARN_ON can be better */
			BUG_ON(prev_state == E_DESTROYING);     /*internal transition state */
			BUG_ON("Invalid cpu frequency state");
		}

		break;
	default:
		PRINT_KD("ERROR: Invalid State argument\n");
		BUG_ON(prev_state == E_DESTROYING);
		ret = -ERR_INVALID_ARG;
		break;
	}

	return ret;
}

static int sec_cpufreq_set_state(sec_counter_mon_state_t new_state, int sync)
{

	struct kdbg_work_t sec_cpufreq_destroy_work;
	struct completion done;
	int ret = 0;

	if (sync) {
		init_completion(&done);

		sec_cpufreq_destroy_work.data = (void *)new_state;
		sec_cpufreq_destroy_work.pwork =
			(void *)sec_cpufreq_set_state_impl;
		kdbg_workq_add_event(sec_cpufreq_destroy_work, &done);

		wait_for_completion(&done);

		/* Check the failed case */
		if (sec_cpufreq_get_state() != new_state) {
			PRINT_KD("State change Failed\n");
			ret = -1;
		}

	} else {
		/* In some cases, wait is cannot permitted.
		   In that case set state immediately . */
		sec_cpufreq_set_state_impl(new_state);
	}

	return ret;

}


void sec_cpufreq_get_status(void)
{
	switch (sec_cpufreq_get_state()) {
	case E_NONE:            /* Falls Through */
	case E_DESTROYED:
		PRINT_KD("Not Initialized    Not Running\n");
		break;
	case E_DESTROYING:
		PRINT_KD("Not Initialized    <Destroying...>\n");
		break;
	case E_RUN_N_PRINT:
		PRINT_KD("Initialized        Running\n");
		break;
	case E_RUNNING:
		PRINT_KD("Initialized        Background Sampling ON\n");
		break;
	case E_INITIALIZED:
		PRINT_KD("Initialized        Not Running\n");
		break;
	default:
		/* TODO: WARN_ON can be better */
		BUG_ON("ERROR: Invalid state");
	}
}

void sec_cpu_freq_on_off(void)
{

	sec_counter_mon_state_t state = sec_cpufreq_get_state();

	/* Destroying state is intermediate state of destroy
	   and it should not be stale, avoid this */
	BUG_ON(state == E_DESTROYING);

	if (state == E_NONE || state == E_DESTROYED) {

		if (sec_cpufreq_set_state(E_INITIALIZED, 1) < 0) {     /* Send with sync */
			return;
		}
	} else {

		WARN_ON(state < E_NONE || state > E_DESTROYED);

		/* Before Destroying make sure state is E_INITIALIZED */
		while (state > E_INITIALIZED)
			sec_cpufreq_set_state(--state, 1);     /* Send with sync */

		/* System is in E_INITIALIZED State now destroyed it
		   and free all the resources*/
		sec_cpufreq_set_state(E_DESTROYED, 1); /* Send with sync */
	}
}

/* cpufreq_monitor_db_init
 * cpufreq database allocation and initialization function.
 */
static int  cpufreq_monitor_db_init(void)
{
	int i = 0;

	/* Inititalize previous database kept to store previous entry */
	memset(&prev_info_db, 0, sizeof(prev_info_db));
	prev_info_db.cpu_freq = system_default_freq;


	/* Initialize the cpu frequency database */
	BUG_ON(cpufreq_monitor_db);

	/*Using in timer context, use with GFP_ATOMIC flag */
	cpufreq_monitor_db =
		(struct sec_cpufreq_struct *)
		KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				CPU_FREQ_MAX_CHAIN_LEN *
				sizeof(struct sec_cpufreq_struct),
				GFP_ATOMIC);

	if (!cpufreq_monitor_db) {
		PRINT_KD("ERROR: CPU Frequency: Insufficient memory\n");
		return -ERR_NO_MEM;
	}

	/* Init memory to zero*/
	memset(cpufreq_monitor_db, 0, sizeof(struct sec_cpufreq_struct) * CPU_FREQ_MAX_CHAIN_LEN);

	/* initlize database to zero */
	spin_lock(&kdbg_cpufreq_lock);

	for (i = 0; i < CPU_FREQ_MAX_CHAIN_LEN; i++)
		cpufreq_monitor_db[i].available = 1;

	spin_unlock(&kdbg_cpufreq_lock);

	return 0;
}

/*
 * find_available_entry
 * find available event in event database.
 */
static inline int find_available_entry(void)
{
	int i = 0;
	int entry_idx = -1;

	spin_lock(&kdbg_cpufreq_lock);

	/* find an available entry */
	for (i = 0; i < CPU_FREQ_MAX_CHAIN_LEN; i++) {
		if (cpufreq_monitor_db[i].available) {
			entry_idx = i;
			break;
		}
	}

	spin_unlock(&kdbg_cpufreq_lock);

	return entry_idx;
}

/*
 * find_populated_entry
 * find already populated entry in database.
 */
static inline int find_populated_entry(void)
{
	int i = 0;
	int entry_idx = -1;

	spin_lock(&kdbg_cpufreq_lock);

	for (i = 0; i < CPU_FREQ_MAX_CHAIN_LEN; i++) {
		if (cpufreq_monitor_db[i].available == 0) {
			entry_idx = i;
			break;
		}
	}

	spin_unlock(&kdbg_cpufreq_lock);

	return entry_idx;
}

/*
 * find_and_update_entry
 * find and update entry in databse, if found for same second.
 */
static inline int find_and_update_entry(unsigned long freq, unsigned long sec)
{
	int i = 0;
	int ret = -1;

	spin_lock(&kdbg_cpufreq_lock);

	for (i = 0; i < CPU_FREQ_MAX_CHAIN_LEN; i++) {
		if (cpufreq_monitor_db[i].available == 0
				&& cpufreq_monitor_db[i].sec == sec
				&& cpufreq_monitor_db[i].cpu_freq != freq) {
			cpufreq_monitor_db[i].cpu_freq = freq;
			ret = 0;
			break;
		}
	}

	spin_unlock(&kdbg_cpufreq_lock);

	return ret;
}

/*
 * print_cpufreq_header
 * print header
 */
static inline void print_cpufreq_header(void)
{
	PRINT_KD("\n");
	PRINT_KD("--------------------------------------------\n");
	PRINT_KD("sec    cpu_freq[KHz]     pid     comm\n");
	PRINT_KD("--------------------------------------------\n");
}

/*
 * print_event_data_entry
 * print cpufrequency entry
 */
static inline void print_event_data_entry(int entry_idx)
{
	if (cpufreq_monitor_db[entry_idx].available == 0) {
		if (cpufreq_monitor_db[entry_idx].pid) {
			PRINT_KD("%lu\t%9lu\t%d\t%s\n", cpufreq_monitor_db[entry_idx].sec,
					cpufreq_monitor_db[entry_idx].cpu_freq,
					cpufreq_monitor_db[entry_idx].pid,
					cpufreq_monitor_db[entry_idx].comm);
		} else {
			PRINT_KD("%lu\t%9lu\t%s\t%s\n", cpufreq_monitor_db[entry_idx].sec,
					cpufreq_monitor_db[entry_idx].cpu_freq,
					" - ",
					" - ");
		}

		/* update previous databse to print entry when no frequency change detected */
		memcpy(&prev_info_db, &cpufreq_monitor_db[entry_idx], sizeof(struct sec_cpufreq_struct));

		/* Reset used database and make it available */
		memset(&cpufreq_monitor_db[entry_idx], 0, sizeof(cpufreq_monitor_db[entry_idx]));
		cpufreq_monitor_db[entry_idx].available = 1;
	}
}

/*
 * cpufreq_display
 * Display cpufrequency data.
 */
static void cpufreq_display(int entry_idx)
{
	spin_lock(&kdbg_cpufreq_lock);

	if (entry_idx >= 0) {
		if ((cpufreq_header_interval % 20) == 0)
			print_cpufreq_header();

		print_event_data_entry(entry_idx);

		cpufreq_header_interval++;
	}

	spin_unlock(&kdbg_cpufreq_lock);
}

/*
 * post_cpufreq_work
 * posts the cpufreq work to kdebugd.
 */
static inline void post_cpufreq_work(int entry_idx)
{
	struct kdbg_work_t cpufreq_work;

	/* post work */
	cpufreq_work.data = (void *)entry_idx;
	cpufreq_work.pwork =
		(void *)cpufreq_display;

	/* add the work to the queue */
	kdbg_workq_add_event(cpufreq_work, NULL);
}

/* Update cpu frequency database */
static void probe_cpu_frequency(void *ignore, unsigned int frequency, unsigned int cpu_id)
{
	int entry_idx = -1;
	struct timespec ts1;
	static unsigned long old_freq;
	char comm_name[TASK_COMM_LEN];
	unsigned long task_pid;
	int ret = -1;

	/* check databse is initialzation */
	if (!cpufreq_monitor_db)
		return;

	/* get timestamp */
	do_posix_clock_monotonic_gettime(&ts1);

	/* Find already populated entry for this second, if cound then update it with latest */
	ret = find_and_update_entry(frequency, (unsigned long)ts1.tv_sec);
	if (!ret)
		return;

	if (old_freq != frequency) {
		/* find new entry to populate */
		entry_idx = find_available_entry();
		if (entry_idx < 0)
			return;

		/* decrease ref count for current */
		get_task_struct(current);

		/* get current task comm name */
		get_task_comm(comm_name, current);

		/*get current task pid */
		task_pid = (unsigned long)current->pid;

		/* increase re count for current */
		put_task_struct(current);

		spin_lock(&kdbg_cpufreq_lock);

		/* update database for new entry */
		cpufreq_monitor_db[entry_idx].sec = ts1.tv_sec;
		cpufreq_monitor_db[entry_idx].cpu_freq = frequency;
		cpufreq_monitor_db[entry_idx].cpu_id = (int)cpu_id;
		cpufreq_monitor_db[entry_idx].pid = (int)task_pid;
		memcpy(cpufreq_monitor_db[entry_idx].comm, comm_name, strlen(comm_name) + 1);
		cpufreq_monitor_db[entry_idx].available = 0;

		spin_unlock(&kdbg_cpufreq_lock);

		/* update old_freq */
		old_freq = frequency;
	}
}

/* thread to dump cpu frequency data */
static int cpufreq_dump_thread(void *data)
{
	int entry_idx = -1;
	struct timespec ts1;

	while (!kthread_should_stop()) {
		do_posix_clock_monotonic_gettime(&ts1);

		/* search if any entry exist in database, if found then post this entry to workqueue */
		entry_idx = find_populated_entry();

		/* No Entry Found in database, get new entry and update it with prev entry */
		if ((entry_idx < 0) && prev_info_db.cpu_freq) {

			/* find new entry to populate */
			entry_idx = find_available_entry();
			if (entry_idx < 0)
				continue;

			/* Update with previous entry */
			spin_lock(&kdbg_cpufreq_lock);

			cpufreq_monitor_db[entry_idx].sec = ts1.tv_sec;
			cpufreq_monitor_db[entry_idx].cpu_freq = prev_info_db.cpu_freq;

			/*avoid pid and comm name when no frequency change detacted in system,default frequency */
			if (prev_info_db.pid) {
				cpufreq_monitor_db[entry_idx].cpu_id = prev_info_db.cpu_id;
				cpufreq_monitor_db[entry_idx].pid = prev_info_db.pid;
				memcpy(cpufreq_monitor_db[entry_idx].comm, prev_info_db.comm, strlen(prev_info_db.comm) + 1);
			}
			cpufreq_monitor_db[entry_idx].available = 0;

			spin_unlock(&kdbg_cpufreq_lock);

		}

		if (entry_idx >= 0) {

			spin_lock(&kdbg_cpufreq_lock);
			/* avoid entry for same second,replace it with current time */
			if (cpufreq_monitor_db[entry_idx].sec < ts1.tv_sec)
				cpufreq_monitor_db[entry_idx].sec = ts1.tv_sec;
			spin_unlock(&kdbg_cpufreq_lock);

			/* post work to kdebugd queue */
			post_cpufreq_work(entry_idx);

			/* Reset Entry Index */
			entry_idx = -1;
		}

		ssleep(1);
	}

	return 0;
}

static int cpu_freq_tracer_dump(void)
{
	/* Create kernel thread to print output at every second */
	dump_thread = kthread_run(cpufreq_dump_thread, NULL, "cpufeq dump thread");
	if (IS_ERR(dump_thread)) {
		PRINT_KD("Failed to create cpu frequency dump thread\n");
		return -1;
	}

	return 0;
}

/* Unregister Tracepoint API */
static inline void unregister_cpufreq_tracepoint(void)
{
	unregister_trace_cpu_frequency(probe_cpu_frequency, NULL);
	tracepoint_synchronize_unregister();
}

/* cpu frequency start function */
static int sec_cpufreq_start(void)
{
	int ret = 0;
	sec_counter_mon_state_t state = sec_cpufreq_get_state();

	/* start background sampling, if stopped state */
	if (state == E_NONE || state == E_DESTROYED || state == E_INITIALIZED) {

		/* Set E_INITIALIZED state when its already not set */
		if (state != E_INITIALIZED) {
			if (sec_cpufreq_set_state(E_INITIALIZED, 1) < 0) {
				PRINT_KD("Error in Initialization\n");
				return 0;
			}

			/* get current state */
			state = sec_cpufreq_get_state();
		}

		/* When state is E_INITIALIZED
		   1. Register tracepoint when databse is intitialized and state set to E_INITIALIZED
		   2. change next state to RUN_N_PRINT
		   3. start dump thread */
		if  (state == E_INITIALIZED) {

			/* Register tracepoint */
			ret = register_trace_cpu_frequency(probe_cpu_frequency, NULL);
			if (ret) {
				PRINT_KD("Trace point Register Failed\n");
				return 0;
			}

			/*On succesful registration of trace point set the flag */
			tp_registered = 1;

			/* Change state untill RUN_N_PRINT */
			while (state < E_RUN_N_PRINT) {
				if (sec_cpufreq_set_state(++state, 1) < 0) {
					PRINT_KD("Error while setting state : %d\n", state);
					/* Unregister probe function before leaving */
					unregister_cpufreq_tracepoint();
					return 0;
				}
			}

			/* start thread to dump previous entry when no frequency change occur */
			ret = cpu_freq_tracer_dump();
			if (ret) {
				PRINT_KD("Failed to start Dump Thread\n");
				/* Unregister probe function before leaving */
				unregister_cpufreq_tracepoint();
				return 0;
			}
		}
	}

	return ret;
}

/* turnoff cpu frequency */
static void turnoff_sec_cpufreq(void)
{
	sec_counter_mon_state_t state = sec_cpufreq_get_state();

	if (E_INITIALIZED == state || state == E_RUN_N_PRINT) {

		/* set state to destroy */
		sec_cpufreq_set_state(E_DESTROYED, 1);
	}
}

int kdbg_cpufreq_init(void)
{
	kdbg_register("COUNTER MONITOR: CPU Frequency", sec_cpufreq_start,
			turnoff_sec_cpufreq,
			KDBG_MENU_COUNTER_MONITOR_CPU_FREQ);

	return 0;
}

