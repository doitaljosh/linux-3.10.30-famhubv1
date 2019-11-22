/*
 * kdbg_thread_profiler.c
 *
 * Copyright (C) 2013 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <trace/events/task.h>

#include <kdebugd.h>
#define CREATE_TRACE_POINTS
#include <agent/endtask_trace.h>

#include "kdbg_util.h"

#include "agent/agent_kdbg_struct.h"
#include "agent/agent_core.h"
#include "agent/agent_packet.h"
#include "agent/agent_error.h"
#include "agent/agent_cm.h"
#include "agent/tvis_agent_cmds.h"
#include "agent/kdbg_thread_profiler.h"

/* global declarations */
static struct thread_entry *th_ent_list[MAX_THREAD_ARRAYS]; /* thread entry list */
static int th_ent_ctr[MAX_THREAD_ARRAYS]; /* counter for thread entries */
DEFINE_SPINLOCK(th_ent_lock); /* spin lock to protect thread entry list */

/*
 * add_thread_entry
 * adds a thread entry to the thread entry list.
 */
static inline void
add_thread_entry(struct thread_entry *th_ent)
{
	spin_lock(&th_ent_lock);
	/* thread list as circular array, discard the old entry when array is full */
	if (th_ent_list[THREAD_ARRAY_WRITE]) {
		/* wrap around */
		if (th_ent_ctr[THREAD_ARRAY_WRITE] >= MAX_THREAD_ENTRIES) {
			if (printk_ratelimit())
				th_error("add thread entry failure, idx %d.\n", th_ent_ctr[THREAD_ARRAY_WRITE]);
			th_ent_ctr[THREAD_ARRAY_WRITE] = 0;
		}
		/* fill the next entry */
		memcpy(&th_ent_list[THREAD_ARRAY_WRITE][th_ent_ctr[THREAD_ARRAY_WRITE]++], th_ent, sizeof(struct thread_entry));
		debug("add thread entry success, idx %d.\n", th_ent_ctr[THREAD_ARRAY_WRITE] - 1);
	}
	spin_unlock(&th_ent_lock);
}

/*
 * probe_task_newtask
 * tracepoint handler for new task.
 */
static void
probe_task_newtask(void *ignore, struct task_struct *task, unsigned long clone_flags)
{
	struct thread_entry th_ent;
	struct timespec ts;

	if (!task)
		return;

	get_task_struct(task);
	if (task->mm) {
		/* initialize the thread entry */
		th_ent.tid = (uint32_t)task->pid;
		th_ent.status = THREAD_CREATED;
		do_posix_clock_monotonic_gettime(&ts);
		th_ent.timestamp = timespec_to_ns(&ts);
		memcpy(th_ent.name, task->comm, TASK_COMM_LEN);
		th_ent.name[TASK_COMM_LEN-1] = 0;
		debug("new task, th_ent.tid %d, th_ent.status %d, th_ent.timestamp %ld.%09ld(%llx), th_ent.name %s\n",
				th_ent.tid, th_ent.status, ts.tv_sec, ts.tv_nsec, th_ent.timestamp, th_ent.name);

		/* add a thread entry to list */
		add_thread_entry(&th_ent);
	}
	put_task_struct(task);
}

/*
 * probe_task_rename
 * tracepoint handler for rename task.
 */
static void
probe_task_rename(void *ignore, struct task_struct *task, char *comm)
{
	struct thread_entry th_ent;
	struct timespec ts;

	if (!task || !comm)
		return;

	get_task_struct(task);
	if (task->mm) {
		/* initialize the thread entry */
		th_ent.tid = (uint32_t)task->pid;
		th_ent.status = THREAD_RENAMED;
		do_posix_clock_monotonic_gettime(&ts);
		th_ent.timestamp = timespec_to_ns(&ts);
		/* copy the changed name */
		memcpy(th_ent.name, comm, TASK_COMM_LEN);
		th_ent.name[TASK_COMM_LEN-1] = 0;
		debug("rename task, th_ent.tid %d, th_ent.name old %s, new %s\n",
				th_ent.tid, task->comm, comm);

		/* add a thread entry to list */
		add_thread_entry(&th_ent);
	}
	put_task_struct(task);
}

/*
 * probe_task_endtask
 * tracepoint handler for end task.
 */
static void
probe_task_endtask(void *ignore, struct task_struct *task)
{
	struct thread_entry th_ent;
	struct timespec ts;

	if (!task)
		return;

	/* initialize the thread entry */
	get_task_struct(task);
	th_ent.tid = (uint32_t)task->pid;
	memcpy(th_ent.name, task->comm, TASK_COMM_LEN);
	th_ent.name[TASK_COMM_LEN-1] = 0;
	put_task_struct(task);

	th_ent.status = THREAD_DESTROYED;
	do_posix_clock_monotonic_gettime(&ts);
	th_ent.timestamp = timespec_to_ns(&ts);

	debug("end task, th_ent.tid %d, th_ent.status %d, th_ent.timestamp %ld.%09ld(%llx), th_ent.name %s\n",
			th_ent.tid, th_ent.status, ts.tv_sec, ts.tv_nsec, th_ent.timestamp, th_ent.name);

	/* add a thread entry to list */
	add_thread_entry(&th_ent);
}

/*
 * kdbg_thread_profiler_write
 * writes the thread profiler information to ringbuffer.
 */
void kdbg_thread_profiler_write(void)
{
	int ctr = 0;

	if (!th_ent_list[THREAD_ARRAY_READ])
		return;

	if (!th_ent_ctr[THREAD_ARRAY_READ]) {
		/* scan the list and copy to read array */
		spin_lock(&th_ent_lock);
		if (th_ent_ctr[THREAD_ARRAY_WRITE] > 0) {
			memcpy(th_ent_list[THREAD_ARRAY_READ], th_ent_list[THREAD_ARRAY_WRITE],
					sizeof(struct thread_entry) * (size_t)th_ent_ctr[THREAD_ARRAY_WRITE]);
			/* maintain counter */
			th_ent_ctr[THREAD_ARRAY_READ] = th_ent_ctr[THREAD_ARRAY_WRITE];
			/* reset the counter */
			th_ent_ctr[THREAD_ARRAY_WRITE] = 0;
		}
		spin_unlock(&th_ent_lock);
	} else {
		/* write in ringbuffer */
		for (ctr = 0; ctr < th_ent_ctr[THREAD_ARRAY_READ]; ctr++) {
			if (thread_prof_write(&th_ent_list[THREAD_ARRAY_READ][ctr], sizeof(struct thread_entry)) == 0) {
				if (printk_ratelimit())
					th_error("ringbuffer write failure, idx %d.\n", ctr);
				break;
			} else {
				debug("ringbuffer write success, idx %d.\n", ctr);
			}
		}
		/* reset the counter */
		th_ent_ctr[THREAD_ARRAY_READ] = 0;
	}
}

/*
 * kdbg_thread_profiler_start
 * start the thread profiler and register the tracepoint
 */
int kdbg_thread_profiler_start(void)
{
	int ret = 0, ctr = 0;

	/* register the tracepoints */
	ret = register_trace_task_newtask(probe_task_newtask, NULL);
	if (ret) {
		th_error("couldn't activate tracepoint probe to task_newtask\n");
		return -1;
	}

	ret = register_trace_task_rename(probe_task_rename, NULL);
	if (ret) {
		th_error("couldn't activate tracepoint probe to task_rename\n");
		goto err_task_rename;
	}

	ret = register_trace_task_endtask(probe_task_endtask, NULL);
	if (ret) {
		th_error("couldn't activate tracepoint probe to task_endtask\n");
		goto err_task_endtask;
	}

	/* allocate memory fot thread list */
	for (ctr = 0; ctr < MAX_THREAD_ARRAYS; ctr++) {
		th_ent_list[ctr] = kzalloc(sizeof(struct thread_entry) * MAX_THREAD_ENTRIES, GFP_KERNEL);
		if (!th_ent_list[ctr]) {
			th_error("memory allocation failure\n");
			goto err_kzalloc;
		}

	}

	return 0;
err_kzalloc:
	/* deallocate memory of thread list */
	for (ctr = 0; ctr < MAX_THREAD_ARRAYS; ctr++) {
		if (th_ent_list[ctr]) {
			kfree(th_ent_list[ctr]);
			th_ent_list[ctr] = NULL;
		}
	}

	/* unregister the tracepoints */
	unregister_trace_task_endtask(probe_task_endtask, NULL);
err_task_endtask:
	unregister_trace_task_rename(probe_task_rename, NULL);
err_task_rename:
	unregister_trace_task_newtask(probe_task_newtask, NULL);
	return -1;
}

/*
 * kdbg_thread_profiler_stop
 * stop the thread profiler and deregister the tracepoint
 */
void kdbg_thread_profiler_stop(void)
{
	int ctr = 0;

	/* unregister the tracepoints */
	unregister_trace_task_newtask(probe_task_newtask, NULL);
	unregister_trace_task_endtask(probe_task_endtask, NULL);
	unregister_trace_task_rename(probe_task_rename, NULL);

	/* deallocate memory of thread list */
	for (ctr = 0; ctr < MAX_THREAD_ARRAYS; ctr++) {
		if (th_ent_list[ctr]) {
			kfree(th_ent_list[ctr]);
			th_ent_list[ctr] = NULL;
		}
	}
}
