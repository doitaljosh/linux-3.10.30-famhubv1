/*
 * tvis_agent_debug.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#ifndef _TVIS_AGENT_DEBUG_H
#define _TVIS_AGENT_DEBUG_H

#ifndef __KERNEL__
#include <stdint.h>
#endif

extern uint32_t tvis_debug_level;
extern uint32_t agent_debug_level;
extern uint32_t common_debug_level;
extern uint32_t tool_debug_level;

/* debug levels */
#define AGENT_DBG_INIT		0x00000001
#define AGENT_DBG_PACKET	0x00000002
#define AGENT_DBG_NETWORK	0x00000004
#define AGENT_DBG_MQ		0x00000008
#define AGENT_DBG_LISTENER	0x00000010
#define AGENT_DBG_EXIT		0x00000020
#define AGENT_DBG_RB		0x00000040
#define AGENT_DBG_TRAN		0x00000080
#define AGENT_DBG_CONFIG	0x00000100
#define AGENT_DBG_FILE_MODE	0x00000200
#define AGENT_DBG_ERR		0x00000400
#define AGENT_DBG_INFO		0x00000800
#define AGENT_DBG_PROC		0x00001000
#define AGENT_DBG_ALL      	0xffffffff

#define TVIS_DBG_INIT		0x00000001
#define TVIS_DBG_PACKET		0x00000002
#define TVIS_DBG_FILE		0x00000003
#define TVIS_DBG_NETWORK	0x00000004
#define TVIS_DBG_MENU		0x00000008
#define TVIS_DBG_EXIT		0x00000010
#define TVIS_DBG_ERR		0x00000020
#define TVIS_DBG_INFO		0x00000040
#define TVIS_DBG_ALL       	0xffffffff

#define COMMON_DBG_NETWORK	0x00000001
#define COMMON_DBG_PACKET	0x00000002
#define COMMON_DBG_MQ		0x00000004
#define COMMON_DBG_CONFIG	0x00000008
#define COMMON_DBG_ERR		0x00000010
#define COMMON_DBG_INFO		0x00000020
#define COMMON_DBG_ALL      	0xffffffff

#define TOOL_DBG_RB		0x00000001
#define TOOL_DBG_ERR		0x00000002
#define TOOL_DBG_INFO		0x00000004
#define TOOL_DBG_ALL        	0xffffffff

#define AGENT_DEBUG
#ifdef AGENT_DEBUG
#define agent_debug(level, fmt, args...) \
	do { \
		if (level & agent_debug_level) \
			printf("Agent: (%s):%s[%u]: " fmt, __FILE__, __FUNCTION__, __LINE__, ##args); \
	} while (0)
#else
#define agent_debug(level, fmt, args...)
#endif

#define agent_abort(fmt, args...) \
	do { \
		printf("Agent: (%s):%s[%u]: " fmt, __FILE__, __FUNCTION__, __LINE__, ##args); \
		tvis_agent_backtrace_print(stdout, 0, NULL); \
		abort(); \
	} while (0)

#define TVIS_DEBUG
#ifdef TVIS_DEBUG
#define tvis_debug(level, fmt, args...) \
	do { \
		if (level & tvis_debug_level) \
			printf("TVis: (%s):%s[%u]: " fmt, __FILE__, __FUNCTION__, __LINE__, ##args); \
	} while (0)
#else
#define tvis_debug(level, fmt, args...)
#endif


#define COMMON_DEBUG
#ifdef COMMON_DEBUG
#define common_debug(level, fmt, args...) \
	do { \
		if (level & common_debug_level) \
			printk("Common: (%s):%s[%u]: " fmt, __FILE__, __FUNCTION__, __LINE__, ##args); \
	} while (0)
#else
#define common_debug(level, fmt, args...)
#endif

#define TOOL_DEBUG
#ifdef TOOL_DEBUG
#define tool_debug(level, fmt, args...) \
	do { \
		if (level & tool_debug_level) \
			printf("Tool: (%s):%s[%u]: " fmt, __FILE__, __FUNCTION__, __LINE__, ##args); \
	} while (0)
#else
#define tool_debug(level, fmt, args...)
#endif
#endif
