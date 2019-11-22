/*
 * agent_kdbg_struct.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */
#ifndef _AGENT_KDBG_STRUCT_H
#define _AGENT_KDBG_STRUCT_H

#include "sec_cpuusage.h"

#define SEC_CPUUSAGE_BUF_SIZE sizeof(struct sec_cpuusage_buf)
/* Buffer for storing per second cpu usage data to be
 * written on RB.
 */
#define THREAD_NAME_MAX_SIZE	16

struct agent_thread_info {
	char name[THREAD_NAME_MAX_SIZE]; /* Name of the thread */
	uint32_t pid; /* pid of the thread */
	uint32_t tgid; /* task goup ID */
};

struct agent_thread_list {
	uint16_t status; /* running, interruptible, stopped etc. */
	uint8_t cpu;	/* CPU where the task is currently running */
	uint8_t policy; /* NORMAL FIFO RR BATCH */
	struct agent_thread_info info;
};

#if 0
struct thread_details {
	uint32_t sec;		/* Up time in sec */
	/* Number of threads following behind the packet */
	uint32_t no_of_threads;
	struct thread_list list;
};
#endif

struct sec_cpuusage_buf {
	unsigned long sec_count;
	struct sec_cpuusage_entity cpu_data[NR_CPUS];
};

struct sec_process_memory {
	int pid;
	int code;
	int data;
	int libCode;
	int libData;
	int stack;
	int other;
	int heap_brk;
};

#if 0
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define	SEC_VIRT_MEMSUAGE_STRUCT_SIZE \
		sizeof(struct sec_virt_memusage_struct)
#endif

/* Structure for holding virual memory usage data */
struct sec_virt_memusage_struct {
	unsigned long sec;
	int vma_count;
	unsigned long vma_size;
};

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define	SEC_PHY_MEMSUAGE_STRUCT_SIZE \
		sizeof(struct sec_phy_memusage_struct)
#endif

/*Structure for holding physiual memory usage data */
struct sec_phy_memusage_struct {
	unsigned long sec;
	unsigned int pid;
#ifdef CONFIG_RSS_INFO
	unsigned long processCur;
	unsigned long code;
	unsigned long data;
	unsigned long libCode;
	unsigned long libData;
	unsigned long stack;
	unsigned long other;
#endif
};

#define TASK_NAME_LEN 16

/*
 * Task state bitmask. NOTE! These bits are also
 * encoded in fs/proc/array.c: get_task_state().
 *
 * We have two separate sets of flags: task->state
 * is about runnability, while task->exit_state are
 * about the task exiting. Confusing, but this way
 * modifying one set can't modify the other one by
 * mistake.
 */
#define TASK_RUNNING			0
#define TASK_INTERRUPTIBLE		1
#define TASK_UNINTERRUPTIBLE	2
#define __TASK_STOPPED			4
#define __TASK_TRACED			8
/* in tsk->exit_state */
#define EXIT_ZOMBIE				16
#define EXIT_DEAD				32
/* in tsk->state again */
#define TASK_DEAD				64
#define TASK_WAKEKILL			128
#define TASK_WAKING				256
#define TASK_STATE_MAX			512

enum task_thread_type {
	KERNEL_THREAD,
	USER_THREAD
};

struct task_list {
	unsigned char task_name[TASK_NAME_LEN];
	unsigned int state;
	unsigned int cpu;
	/* This is address, it should be long still its taken as a int
	 * because this structure use in network mode and by changing
	 * the machine long size will be varied.
	 * */
	unsigned int task_struct;
	/* This is address, it should be long still its taken as a int
	 * because this structure use in network mode and by changing
	 * the machine long size will be varied.
	 * */
	unsigned int pc;
	/* PID of the task*/
	unsigned int pid;
	/* Flags */
	unsigned int flags;
	/* Parent PID */
	unsigned int ppid;
	/* Eldest child*/
	unsigned int echidpid;
	/* younger child*/
	unsigned int ychidpid;
	/* Older  child*/
	unsigned int ochidpid;
	/* thread type */
	enum task_thread_type type;

	/* Stack size */
	unsigned int stack_size;
	/* Stack usage */
	unsigned int stack_usage;
	unsigned int priority;
	unsigned int spriority;
	unsigned int policy;
	unsigned int time_slice;
};

struct mapline {
	unsigned int vm_start;
	unsigned int vm_end;
	unsigned int vm_flags;
	unsigned int vm_pgoff;
	unsigned int dev_major;
	unsigned int dev_minor;
	unsigned int ino;
};

#define MAX_ADD 20
struct struct_backtrace {
	int depth;
	unsigned int frame_adress[MAX_ADD];
};

enum cm_event {
	START = 0,
	STOP
};

/* CPU Usage Table attiributes */
struct sec_cpuusage_entity {
	int user;   /* User CPU */
	int system; /* CPU spend in system*/
	int io;     /* I/O wait */
};

#define AGENT_CPU_USAGE_SIZE sizeof(struct agent_cpu_usage)
struct agent_cpu_usage {
	/* maintains the seconds
	 *  count at which the data is updated.*/
	unsigned long sec_count;
	/* Cpu usage data */
	struct sec_cpuusage_entity cpu_data[NR_CPUS];
};

struct thread_info_table {
	unsigned long sec;
	int available;
	/* Context switch count */
	unsigned long sec_topthread_ctx_cnt[NR_CPUS];
	int max_thread[NR_CPUS];
	struct topthread_info_entry  info[NR_CPUS * TOPTHREAD_MAX_THREAD];
};

struct sec_diskstats_struct {
	unsigned long sec;
	long nkbyte_read;
	long nkbyte_write;
	unsigned long utilization;
};

struct sec_netusage_struct {
	unsigned long sec;   /*maintains the seconds
			       count at which the data is updated.*/
	int rx;   /*packet size taken by profiler
		    while receiving the packet*/
	int tx;   /*packet size taken by profiler
		    while sending the packet*/
};

struct sec_memusage_struct {
	unsigned int sec;
	unsigned int totalram;
	unsigned int freeram;
	unsigned int bufferram;
	unsigned int cached;
	unsigned int anonpages_info;
};

struct sec_virt_memusage_struct {
	unsigned long sec;
	unsigned int vma_count;
	unsigned int vma_size;
};

struct sec_virt_memusage_struct {
	unsigned int sec;
	unsigned int pid;
	unsigned int ProcessCur;
	unsigned int Code;
	unsigned int Data;
	unsigned int LibCode;
	unsigned int LibData;
	unsigned int Stack;
	unsigned int Other;
};

struct sec_perfstats_struct {
	unsigned long sec;
	unsigned long long val[NR_CPUS][NUM_PERF_COUNTERS];
};

enum aop_source {
	AOP_SRC_TIMER = 0x1,
	AOP_SRC_PERF_COUNT_HW_CPU_CYCLES = 0x10,
	AOP_SRC_PERF_COUNT_HW_INSTRUCTIONS = 0x100,
	AOP_SRC_PERF_COUNT_HW_CACHE_L1D = 0x1000,
	AOP_SRC_PERF_COUNT_HW_CACHE_L1I = 0x10000,
	AOP_SRC_PERF_COUNT_HW_CACHE_LL = 0x100000,
	AOP_SRC_PERF_COUNT_HW_CACHE_DTLB = 0x100000,
	AOP_SRC_PERF_COUNT_HW_CACHE_ITLB = 0x1000000,
	AOP_SRC_PERF_COUNT_HW_CACHE_BPU = 0x10000000
};
#endif

#endif
