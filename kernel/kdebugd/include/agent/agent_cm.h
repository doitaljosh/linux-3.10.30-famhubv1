/*
 * agent_cm.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */
#ifndef _AGENT_CM_H
#define _AGENT_CM_H

#include <linux/mm.h>

/* COUNTER MONITOR status in agent mode */
enum agent_cm {
	CM_INVAL = -1,
	CM_START,
	CM_STOP,
	CM_NONE
};

/* COUNTER MONITOR MODE (CM_START/CM_END)*/
struct agent_cm_status {
	enum agent_cm cpuusage;
	enum agent_cm topthread;
	enum agent_cm memusage;
	enum agent_cm phymem;
	enum agent_cm netusage;
	enum agent_cm diskusage;
	enum agent_cm meminfo;
};

/* Strucutre to hold total memory usage information (kernel + user) */
struct mem_info {
	int sec; /* time in second */
	struct kernel_mem_usage kernel_mem_info; /* structure to hold kernel memory information */
	struct user_mem_usage user_mem_info; /* structure to hold user memory information */
};

/* To be used in kdbg-core.c */
void sec_phy_memusage_prints_on_off(void);

/* AGENT CM: cpu usage get status */
enum agent_cm agent_cpuusage_get_status(void);

/* AGENT CM: cpu usage set status */
void agent_cpuusage_set_status(enum agent_cm  status);

/* AGENT CM: mem usage get status */
enum agent_cm agent_memusage_get_status(void);

/* AGENT CM: mem usage set status */
void agent_memusage_set_status(enum agent_cm status);

/* AGENT CM: physical mem usage get status */
enum agent_cm agent_phy_mem_get_status(void);

/* AGENT CM: physical mem usage set status */
void agent_phy_mem_set_status(enum agent_cm status);

/* AGENT CM: top thread get status */
enum agent_cm agent_topthread_get_status(void);

/* AGENT CM: top thread set status */
void agent_topthread_set_status(enum agent_cm status);

/* AGENT CM: network usage get status */
enum agent_cm agent_netusage_get_status(void);

/* AGENT CM: network usage set status */
void agent_netusage_set_status(enum agent_cm status);

/* AGENT CM: disk usage get status */
enum agent_cm agent_diskusage_get_status(void);

/* AGENT CM: disk usage set status */
void agent_diskusage_set_status(enum agent_cm status);

/* AGENT CM: imeminfo get status */
enum agent_cm agent_meminfo_get_status(void);

/* AGENT CM: meminfo set status */
void agent_meminfo_set_status(enum agent_cm status);

/* AGENT CM: get meminfo */
void agent_get_meminfo(void);

#endif
