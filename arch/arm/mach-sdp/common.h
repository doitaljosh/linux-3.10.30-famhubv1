/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_SDP_COMMON_H
#define __ARCH_SDP_COMMON_H

void sdp_map_io(void);

extern struct smp_operations sdp_smp_ops;

#ifdef CONFIG_OF
void sdp_restart(char mode, const char *cmd);
void sdp_init_irq(void) __init;
extern void sdp_dt_init_machine(void) __init;
extern unsigned int sdp_get_mem_cfg(int nType);
extern void sdp_init_time(void);
#endif

#endif /* __ARCH_SDP_COMMON_H */
