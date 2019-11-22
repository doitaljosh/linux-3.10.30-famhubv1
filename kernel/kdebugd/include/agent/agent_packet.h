/*
 * agent_packet.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#ifndef _AGENT_PACKET_H
#define _AGENT_PACKET_H

extern struct mutex g_agent_rb_lock;

size_t agent_write(int cmd, void *data, size_t data_len);
size_t agent_adv_write(void *data, size_t data_len);
#endif
