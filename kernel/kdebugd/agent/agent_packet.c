/*
 * agent_packet.c
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#include <linux/mutex.h>
#include "kdebugd.h"
#include "agent/kern_ringbuffer.h"
#include "agent/tvis_agent_packet.h"
#include "agent/agent_packet.h"

/* data sent from agent to tvis */
#define agent_data(cmd, arg, len) \
	agent_write(cmd, arg, len)

/* Mutex lock to write cmd packet and data packet together in RB */
DEFINE_MUTEX(g_agent_rb_lock);

/* Write tool packet in ringbuffer */
size_t agent_write(int cmd, void *data, size_t data_len)
{
	size_t t_hdr_bytes;
	size_t data_bytes;
	static int first_time;
	struct tool_header t_hdr;

	if (!data || !data_len) {
		PRINT_KD("Error in creating packet, data=%p, data_len=%d\n",
								data, data_len);
		return 0;
	}

	/* mutex_init should be done at ringbuffer init time
	 * but as of now, we stick to this implementation */
	if (!first_time) {
		mutex_init(&g_agent_rb_lock);
		first_time = 1;
	}

	mutex_lock(&g_agent_rb_lock);

	t_hdr.cmd = cmd;
	t_hdr.data_len = (int32_t)data_len;

	t_hdr_bytes = kdebugd_ringbuffer_writer((char *)(&t_hdr),
							TOOL_HDR_SIZE);
	if (t_hdr_bytes  < TOOL_HDR_SIZE) {
		PRINT_KD("Error in writing tool header in RB\n");
		mutex_unlock(&g_agent_rb_lock);
		return 0;
	}

	data_bytes = kdebugd_ringbuffer_writer((char *)(data), data_len);
	if (data_bytes  < data_len) {
		PRINT_KD("Error in writing tool data in RB\n");
		mutex_unlock(&g_agent_rb_lock);
		return 0;
	}

	mutex_unlock(&g_agent_rb_lock);
	return t_hdr_bytes + data_bytes;
}

/* For writing the data in RB in advance */
size_t agent_adv_write(void *data, size_t data_len)
{
	size_t bytes_written;

	bytes_written = kdbg_rb_adv_writer(data, data_len);
	if (bytes_written < data_len) {
		/* PRINT_KD("Error in writing advance data in RB\n"); */
		return 0;
	}
	return bytes_written;
}
