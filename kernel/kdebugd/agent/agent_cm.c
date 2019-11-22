/*
 * agent_cm.c
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#include <kdebugd.h>
#include <agent/agent_cm.h>

/* COUNTER MONITOR status variable */
static struct agent_cm_status g_agent_cm_status = {CM_STOP, CM_STOP, CM_STOP,
						CM_STOP, CM_STOP, CM_STOP, CM_STOP};

/* COUNTER MONITOR cpu usage function */
enum agent_cm agent_cpuusage_get_status(void)
{
	return g_agent_cm_status.cpuusage;
}

void agent_cpuusage_set_status(enum agent_cm status)
{
	g_agent_cm_status.cpuusage = status;
}

/* COUNTER MONITOR memory usage function */
enum agent_cm agent_memusage_get_status(void)
{
	return g_agent_cm_status.memusage;
}

void agent_memusage_set_status(enum agent_cm status)
{
	g_agent_cm_status.memusage = status;
}

/* COUNTER MONITOR physical memory usage function */
enum agent_cm agent_phy_mem_get_status(void)
{
	return g_agent_cm_status.phymem;
}

void agent_phy_mem_set_status(enum agent_cm status)
{
	g_agent_cm_status.phymem = status;
}

/* COUNTER MONITOR thread usage function */
enum agent_cm agent_topthread_get_status(void)
{
	return g_agent_cm_status.topthread;
}

void agent_topthread_set_status(enum agent_cm status)
{
	g_agent_cm_status.topthread = status;
}

/* COUNTER MONITOR nework usage function */
enum agent_cm agent_netusage_get_status(void)
{
	return g_agent_cm_status.netusage;
}

void agent_netusage_set_status(enum agent_cm status)
{
	g_agent_cm_status.netusage = status;
}

/* COUNTER MONITOR disk usage function */
enum agent_cm agent_diskusage_get_status(void)
{
	return g_agent_cm_status.diskusage;
}

void agent_diskusage_set_status(enum agent_cm status)
{
	g_agent_cm_status.diskusage = status;
}

/* COUNTER MONITOR meminfo functions */
enum  agent_cm agent_meminfo_get_status(void)
{
	return g_agent_cm_status.meminfo;
}

void agent_meminfo_set_status(enum agent_cm status)
{
	g_agent_cm_status.meminfo = status;
}
