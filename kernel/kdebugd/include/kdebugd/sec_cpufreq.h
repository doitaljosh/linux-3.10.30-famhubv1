/*
 *  linux/kernel/sec_cpufreq.h
 *
 *  CPU frequency change monitoring
 *
 *  Copyright (C) 20013  Samsung
 *
 *  2013-07-03  Created by umesh.t@samsung.com.
 *
 */

#ifndef __LINUX_CPUFREQ_H__
#define __LINUX_CPUFREQ_H__

#define CPU_FREQ_MAX_CHAIN_LEN     (32)

/* #define CPUUSAGE_DEBUG */
#ifdef CPUFREQ_DEBUG
#define  SEC_CPUFREQ_DEBUG(fmt, args...) PRINT_KD(fmt, args)
#else
#define  SEC_CPUFREQ_DEBUG(fmt, args...)
#endif


/*
 *  Structure for CPU frequency change releated data.
 */
struct sec_cpufreq_struct {
	time_t sec;	          /* time in seconds*/
	int available;            /*to check arability of structure */
	unsigned long cpu_freq;   /* cpu frequency */
	int cpu_id;               /* cpu number*/
	int pid;		  /* process id*/
	char comm[TASK_COMM_LEN]; /* Store comm value*/
};

void sec_cpu_freq_on_off(void);
void sec_cpufreq_get_status(void);

#endif /* __LINUX_CPUFREQ_H__ */
