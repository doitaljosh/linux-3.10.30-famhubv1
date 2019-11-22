/*
 * agent_error.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */
#ifndef _AGENT_ERROR_H
#define _AGENT_ERROR_H

#define AGENT_WARN_ON	111
#define AGENT_BUG_ON	222

/* BUG_ON sent from agent to tvis */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define kdbg_bug(cmd, arg) \
	do { \
		if (kdbg_agent_get_mode() == KDEBUGD_MODE) \
			BUG_ON(arg); \
		else if (arg) \
			agent_bug(cmd, __FILE__, __func__, __LINE__); \
	} while (0)

#else
#define kdbg_bug(cmd, arg) BUG_ON(arg)

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

/* WARN_ON sent from agent to tvis */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define kdbg_warn(cmd, arg) \
	do { \
		if (kdbg_agent_get_mode() == KDEBUGD_MODE) \
			WARN_ON(arg); \
		else if (arg) \
			agent_warn(cmd, __FILE__, __func__, __LINE__); \
	} while (0)

#else
#define kdbg_warn(cmd, arg) WARN_ON(arg)

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

/* ERROR sent from agent to tvis */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define kdbg_error(cmd, fmt, args...) \
	do { \
		if (kdbg_agent_get_mode() == KDEBUGD_MODE) \
			PRINT_KD(fmt, ##args); \
		else \
			agent_error(cmd, fmt, ##args); \
	} while (0)

#else
#define kdbg_error(cmd, fmt, args...) \
	PRINT_KD(fmt, ##args)

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

/* SYM_ERROR sent from agent to tvis */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define kdbg_sym_error(cmd, fmt, args...) \
	do { \
		if (kdbg_agent_get_mode() == KDEBUGD_MODE) \
			sym_errk(fmt, ##args); \
		else \
			agent_error(cmd, fmt, ##args); \
	} while (0)

#else
#define kdbg_sym_error(cmd, fmt, args...) \
	sym_errk(fmt, ##args)

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */


#define AGENT_WARN_MSG_SIZE 128

/* Agent warning structure */
struct agent_warn {
	int cmd;
	char msg[AGENT_WARN_MSG_SIZE];
};

#define AGENT_WARN_SRTUCT_SIZE sizeof(struct agent_warn)

/* Write WARN_ON packet on ringbuffer */
size_t agent_warn(int cmd, char *file, const char *func, int line);

/* Write BUG_ON packet on ringbuffer */
size_t agent_bug(int cmd, char *file, const char *func, int line);

/* Write ERROR packet on ringbuffer */
size_t agent_error(int cmd, const char *fmt, ...) __attribute__ ((format (gnu_printf, 2, 3)));

#endif
