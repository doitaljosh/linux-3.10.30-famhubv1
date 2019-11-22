/*
 * kdbg_thread_profiler.h
 *
 * Copyright (C) 2013 Samsung Electronics
 * Created by rajesh.bhagat (rajesh.b1@samsung.com)
 *
 * NOTE:
 *
 */
#ifndef _KDBG_THREAD_PROFILER_H
#define _KDBG_THREAD_PROFILER_H

/* #define KDBG_THREAD_PROFILER_DEBUG */
#define th_warn(x...)  PRINT_KD("[WARN] kdbg_th_prof: " x)
#define th_info(x...)  PRINT_KD("[INFO] kdbg_th_prof: " x)
#define th_error(x...) PRINT_KD("[ERROR] kdbg_th_prof: " x)
#define th_menu(x...)  PRINT_KD(x)

#ifdef KDBG_THREAD_PROFILER_DEBUG
#define debug(x...) PRINT_KD("[DEBUG] kdbg_th_prof: " x)
#else
#define debug(x...)
#endif

/* Thread Profiler output data */
#define thread_prof_write(data, len) \
	agent_write(KDBG_CMD_THREAD_PROFILE_INFO, data, len)

#define MAX_THREAD_ARRAYS	(2)
#define MAX_THREAD_ENTRIES	(128)

/* thready array for writing to ringbuffer */
enum thread_array {
	THREAD_ARRAY_WRITE,
	THREAD_ARRAY_READ
};

/* thread status information */
enum thread_status {
	THREAD_CREATED = 0x00000001,
	THREAD_DESTROYED,
	THREAD_RENAMED
};

/* thread entry information */
struct thread_entry {
	uint32_t tid; /* thread id */
	uint32_t status; /* thread status */
	s64 timestamp; /* start/stop timestamp */
	char name[TASK_COMM_LEN]; /* name of the thread */
};

/* extern declarations */
int kdbg_thread_profiler_start(void);
void kdbg_thread_profiler_stop(void);
void kdbg_thread_profiler_write(void);

#endif


