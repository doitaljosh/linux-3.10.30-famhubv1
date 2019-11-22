/*
 *
 * btsbc_module.c
 *
 *
 *
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation (the "GPL"), and may
 * be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL for more details.
 *
 *
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php
 * or by writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
 *
 *
 */


#ifndef SBC_MODULE_H
#define SBC_MODULE_H

#include <linux/module.h>
#include <linux/slab.h>

#include "sbc_private.h"


#define BTSBC_DBGFLAGS_DEBUG    0x01
#define BTSBC_DBGFLAGS_INFO     0x02
#define BTSBC_DBGFLAGS_WRN      0x04
#define BTSBC_DBGFLAGS (BTSBC_DBGFLAGS_DEBUG | BTSBC_DBGFLAGS_INFO | BTSBC_DBGFLAGS_WRN)

#if defined(BTSBC_DBGFLAGS) && (BTSBC_DBGFLAGS & BTSBC_DBGFLAGS_DEBUG)
#define BTSBC_DBG(fmt, ...) \
    printk(KERN_DEBUG"BTSBC %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTSBC_DBG(fmt, ...)
#endif

#if defined(BTSBC_DBGFLAGS) && (BTSBC_DBGFLAGS & BTSBC_DBGFLAGS_INFO)
#define BTSBC_INFO(fmt, ...) \
    printk(KERN_INFO"BTSBC %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTSBC_INFO(fmt, ...)
#endif

#if defined(BTSBC_DBGFLAGS) && (BTSBC_DBGFLAGS & BTSBC_DBGFLAGS_WRN)
#define BTSBC_WRN(fmt, ...) \
    printk(KERN_WARNING "BTSBC %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTSBC_WRN(fmt, ...)
#endif

#define BTSBC_ERR(fmt, ...) \
    printk(KERN_ERR "BTSBC %s: " fmt, __FUNCTION__, ##__VA_ARGS__)

/* The following definitions may be needed to compile BlueZ's SBC as module */
#ifndef __BIT_TYPES_DEFINED__
typedef unsigned char           uint8_t;
typedef unsigned short          uint16_t;
typedef unsigned int            uint32_t;
typedef unsigned long long      uint64_t;
typedef signed char             int8_t;
typedef short                   int16_t;
typedef int                     int32_t;
typedef long long               int64_t;
#endif

#define CHAR_BIT    8
#define ssize_t unsigned long
#define size_t  unsigned long
#define intptr_t unsigned long

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 0
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 1
#endif

#ifndef __BYTE_ORDER
#define  __BYTE_ORDER  __LITTLE_ENDIAN
#endif

#endif /* SBC_MODULE_H */
