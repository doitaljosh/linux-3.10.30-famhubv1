/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SDP_MICOM_H
#define __SDP_MICOM_H

extern void sdp_micom_uart_init(unsigned int base, int port);
extern void sdp_micom_uart_sendcmd(unsigned char *s, unsigned char size);
extern void sdp_micom_request_suspend(void);
extern void sdp_micom_uart_receivecmd(unsigned char *s, unsigned char size);
extern int sdp_micom_uart_receivecmd_timeout(unsigned char *s, unsigned char size, int timeout);
extern void sdp_micom_request_poweroff(void);
extern void sdp_micom_send_byte_cmd(u8 cmd2);

#endif
