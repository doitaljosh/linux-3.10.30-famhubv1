/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * @file sbb_interface.h
 * @Kernel module for SBB detection and network detection in Kernel space.
 * @date   2014/09/29
 *
 */


#ifndef _SBB_INTERFACE_
#define _SBB_INTERFACE_

enum SBBSTATUS {
SBB_NOT_INITIALIZED =-1,
SBB_NOT_CONNECTED = 0,
SBB_CONNECTED = 1
};

int check_network_connected      (void);
int check_network_connected_wait (void);
int check_sbb_connected          (void);
int check_sbb_connected_wait     (void);


#endif
