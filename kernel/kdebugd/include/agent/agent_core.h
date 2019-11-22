/*
 * agent_core.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#ifndef _AGENT_CORE_H
#define _AGENT_CORE_H

#include <agent/agent_cm.h>
/* Set this value such that it exceeds the number of kdebugd commands */
#define KDEBUGD_AGENT_CMD_START 500
#define AGENT_ERROR_PRINT_FREQ 1500

extern atomic_t g_agent_started;
#define KDBG_AGENT_START 1
#define KDBG_AGENT_STOP  0
#define START_AGENT()    	 atomic_set(&g_agent_started, KDBG_AGENT_START)
#define STOP_AGENT()    	 atomic_set(&g_agent_started, KDBG_AGENT_STOP)
#define IS_AGENT_RUNNING()  atomic_read(&g_agent_started)

enum kdbg_agent_mode {
	KDEBUGD_MODE = 0,
	AGENT_MODE
};

void kdbg_agent_threadlist_set_mode(enum agent_cm event);
/* COUNTER MONITOR mode variable defined in agent_core.c */
extern struct agent_cm_status g_cm_status;

/* get kdebugd-agent mode */
int kdbg_agent_get_mode(void);

/* set kdebugd-agent mode */
void kdbg_agent_set_mode(int mode);
int kdbg_agent_init(void);
int kdbg_agent_exit(void);
void kdbg_agent_reset(void);
int agent_show_phy_mem_stat(void);
void agent_phy_memusage_on_off(void);

extern int kdbg_agent_thread_info(void);
#endif
