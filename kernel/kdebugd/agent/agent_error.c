/*
 * agent_error.c
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include "agent/agent_packet.h"
#include "agent/agent_error.h"

/* Write WARN_ON packet on ringbuffer */
size_t agent_warn(int cmd, char *file, const char *func, int line)
{
	struct agent_warn warn;

	warn.cmd = cmd;
	snprintf(warn.msg, AGENT_WARN_MSG_SIZE,
		"WARN_ON called from:\nFILE: %s\nFUNCTION: %s\nLINE: %d\n",
				file, func, line);

	return agent_write(AGENT_WARN_ON, (void *)&warn,
				AGENT_WARN_SRTUCT_SIZE);
}

/* Write BUG_ON packet on ringbuffer */
size_t agent_bug(int cmd, char *file, const char *func, int line)
{
	struct agent_warn warn;

	warn.cmd = cmd;
	snprintf(warn.msg, AGENT_WARN_MSG_SIZE,
		"BUG_ON called from:\nFILE: %s\nFUNCTION: %s\nLINE: %d\n",
				file, func, line);

	return agent_write(AGENT_BUG_ON, (void *)&warn, AGENT_WARN_SRTUCT_SIZE);
}

/* Write ERROR packet on ringbuffer */
size_t agent_error(int cmd, const char *fmt, ...)
{
	va_list args;
	struct agent_warn warn;

	warn.cmd = cmd;
	va_start(args, fmt);
	vsnprintf(warn.msg, AGENT_WARN_MSG_SIZE, fmt, args);
	va_end(args);

	return agent_write(AGENT_BUG_ON, (void *)&warn, AGENT_WARN_SRTUCT_SIZE);
}

