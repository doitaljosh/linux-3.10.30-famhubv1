/*
 * agent_core.c
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#include <linux/atomic.h>
#include <kdebugd.h>
#ifdef CONFIG_KDEBUGD_THREAD_PROFILER
#include <agent/kdbg_thread_profiler.h>
#endif
#include <agent/agent_core.h>
#include <agent/agent_kdbg_struct.h>
#include <agent/kern_ringbuffer.h>
#include <agent/tvis_agent_packet.h>
#include <linux/delay.h>
#include <sec_topthread.h>
#include <sec_cpuusage.h>
#include <sec_memusage.h>
#include <sec_diskusage.h>
#include <sec_netusage.h>
#include <linux/kthread.h>

/* version information */
#define KDEBUGD_AGENT_VERSION_STRING		"v2.1"

/* Agent Kdebugd mode variable */
atomic_t g_kdbg_agent_mode = ATOMIC_INIT(KDEBUGD_MODE);
atomic_t g_kdbg_thread_list_mode = ATOMIC_INIT(CM_NONE);

static struct task_struct *kdbg_agent_tsk;

atomic_t g_agent_started = ATOMIC_INIT(0);

/* Agent minimum sampling sampling time in ms */
#define AGENT_SAMPLING_TIME 20

/* 1 second */
#define AGENT_SAMPLING_TIME_1_SEC (1000 / AGENT_SAMPLING_TIME)

/* AGENT/KDEBUGD mode related functions */
int kdbg_agent_get_mode(void)
{
	return atomic_read(&g_kdbg_agent_mode);
}

void kdbg_agent_set_mode(int mode)
{
	atomic_set(&g_kdbg_agent_mode, mode);
}


void kdbg_agent_threadlist_set_mode(enum agent_cm event)
{
	if (event == atomic_read(&g_kdbg_thread_list_mode))
		PRINT_KD("Already in same mode\n");
	else
		atomic_set(&g_kdbg_thread_list_mode, event);
}

static int kdbg_agent_threadlist_get_mode(void)
{
	 return atomic_read(&g_kdbg_thread_list_mode);
}

/**
 * kdbg_agent_stop:
 * Stops kdebugd functions started by agent.
 * @return: void
 */
static void kdbg_agent_stop(void)
{
	enum agent_cm tvis_event = CM_STOP;

	/* Stop kdebugd from writing data in ringbuffer */
	kdbg_agent_threadlist_set_mode(tvis_event);

	if (tvis_event != agent_cpuusage_get_status())
		agent_cpuusage_set_status(tvis_event);

	if (tvis_event  != agent_memusage_get_status())
		agent_memusage_set_status(tvis_event);

	if (tvis_event != agent_phy_mem_get_status()) {
		agent_phy_mem_set_status(tvis_event);
		agent_phy_memusage_on_off();
	}

	if (tvis_event != agent_topthread_get_status()) {
		agent_topthread_set_status(tvis_event);
		sec_topthread_on_off();
	}

	if (tvis_event != agent_netusage_get_status())
		agent_netusage_set_status(tvis_event);

	if (tvis_event != agent_diskusage_get_status())
		agent_diskusage_set_status(tvis_event);

	if (tvis_event != agent_meminfo_get_status())
		agent_meminfo_set_status(tvis_event);
}

static int  agent_worker(void *p)
{
	int i = 0;
	int ret;
	while (1) {
		/* Check if ringbuffer reader is dead */
		ret = kdbg_ringbuffer_reader_dead();
		if (ret) {
			PRINT_KD("Kdebugd ringbuffer reader dead...\n");
			/* Stop kdebugd agent */
			kdbg_agent_exit();
			STOP_AGENT();
			PRINT_KD("Closing Kdbg_agent_worker thread...\n");
			/* breaking from this function will
			 * itself exit this thread */
			kdbg_agent_tsk = NULL;
			break;
		}

		if (IS_AGENT_RUNNING()) {
			/* Write cpu usage data in each 20 ms
			 * If status is started */
			if (CM_START ==  agent_cpuusage_get_status())
				sec_cpuusage_agent_interrupt();

			/* Write mem usage data in each 20 ms*/
			if (CM_START ==  agent_memusage_get_status())
				kdbg_get_mem_stat_agent();

			/* Write disk usage data in each 20 ms*/
			if (CM_START ==  agent_diskusage_get_status())
				kdbg_get_disk_stat_agent();

			/* Write network usage data in each 20 ms*/
			if (CM_START ==  agent_netusage_get_status())
				sec_netusage_agent_interrupt();

#ifdef CONFIG_KDEBUGD_THREAD_PROFILER
			/* Write thread profiler data in each 20 ms */
			kdbg_thread_profiler_write();
#endif

			/* For 1 second sampling time */
			if (!(i % AGENT_SAMPLING_TIME_1_SEC)) {
				/* Write thread list info */
				if (CM_START ==
				kdbg_agent_threadlist_get_mode())
					kdbg_agent_thread_info();
				/* Write physical memory usage data */
				if (CM_START ==  agent_phy_mem_get_status())
					agent_show_phy_mem_stat();

				/* Write meminfo data */
				if (CM_START ==  agent_meminfo_get_status())
					agent_get_meminfo();

				i = 0;
			}
			i++;
		}
		/* usleep_range is preferred for smaller delays, input in us */
		usleep_range(AGENT_SAMPLING_TIME * 1000,
					AGENT_SAMPLING_TIME * 1000);
	}

	return 0;
}

static int kdbg_agent_thread_init(void)
{

	int ret = 0;

	if (kdbg_agent_tsk) {
		PRINT_KD("Thread is already created\n");
		return 1;
	}

	kdbg_agent_tsk = kthread_create(agent_worker, NULL,
						"Kdbg_agent_worker");

	if (IS_ERR(kdbg_agent_tsk)) {
		ret = PTR_ERR(kdbg_agent_tsk);
		kdbg_agent_tsk = NULL;
		PRINT_KD("Failed: Kdebugd Worker Thread Creation\n");
		return ret;
	}

	PRINT_KD(" Succsesfuly Created\n");
	kdbg_agent_tsk->flags |= PF_NOFREEZE;
	wake_up_process(kdbg_agent_tsk);

	return 0;
}

/**
 * kdbg_agent_init:
 * Initializes the ringbuffer and creates agent worker thread
 * @return: 0 on success, -1 on error
 */
int kdbg_agent_init(void)
{
	int ret;
	/* kdebugd ringbuffer open */
	ret = kdbg_ringbuffer_open();
	if (ret < 0)
		return ret;
	/* Initialize Kdbg_agent_worker thread */
	return kdbg_agent_thread_init();
}

/**
 * kdbg_agent_reset:
 * Stops kdebugd functions started by agent and resets ringbuffer.
 * @return: void
 */
void kdbg_agent_reset(void)
{
	/* Stops kdebugd functions started by agent */
	kdbg_agent_stop();
	PRINT_KD("Kdebugd features stopped...\n");

	/* kdebugd ringbuffer reset */
	kdbg_ringbuffer_reset();
	PRINT_KD("Kdebugd ringbuffer reset done\n");
}

/**
 * kdbg_agent_exit:
 * Stops kdebugd functions started by agent and resets ringbuffer.
 * @return: void
 */
int kdbg_agent_exit(void)
{
	/* Stops kdebugd functions started by agent */
	kdbg_agent_stop();
	PRINT_KD("Kdebugd features stopped...\n");

	/* kdebugd ringbuffer reset */
	kdbg_ringbuffer_close();
	PRINT_KD("Kdebugd ringbuffer close done\n");
	return 0;
}
