/*
 *
 * btpcm.h
 *
 *
 *
 * Copyright (C) 2013 Broadcom Corporation.
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

#ifndef BTPCM_H
#define BTPCM_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
#define BTPCM_PDE_DATA(inode) PDE(inode)->data
#define BTPCM_PDE_PARENT_DATA(inode) PDE(inode)->parent->data
#else
#define BTPCM_PDE_DATA(inode) PDE_DATA(inode)
#define BTPCM_PDE_PARENT_DATA(inode) proc_get_parent_data(inode)
#endif

#define BTPCM_DBGFLAGS_DEBUG    0x01
#define BTPCM_DBGFLAGS_INFO     0x02
#define BTPCM_DBGFLAGS_WRN      0x04
//#define BTPCM_DBGFLAGS (BTPCM_DBGFLAGS_DEBUG | BTPCM_DBGFLAGS_INFO | BTPCM_DBGFLAGS_WRN)
#define BTPCM_DBGFLAGS (BTPCM_DBGFLAGS_INFO | BTPCM_DBGFLAGS_WRN)

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_DEBUG)
#define BTPCM_DBG(fmt, ...) \
    printk(KERN_DEBUG "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_DBG(fmt, ...)
#endif

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_INFO)
#define BTPCM_INFO(fmt, ...) \
    printk(KERN_INFO "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_INFO(fmt, ...)
#endif

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_WRN)
#define BTPCM_WRN(fmt, ...) \
    printk(KERN_WARNING "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define BTPCM_WRN(fmt, ...)
#endif

#define BTPCM_ERR(fmt, ...) \
    printk(KERN_ERR "BTPCM %s: " fmt, __FUNCTION__, ##__VA_ARGS__)


#define BTPCM_SAMPLE_8BIT_SIZE      (sizeof(char))
#define BTPCM_SAMPLE_16BIT_SIZE     (sizeof(short))

#define BTPCM_SAMPLE_MONO_SIZE      1
#define BTPCM_SAMPLE_STEREO_SIZE    2

enum btpcm_state
{
    STATE_FREE = 0,
    STATE_OPENED,
    STATE_CONFIGURED,
    STATE_STARTED
};

struct btpcm
{
    void (*callback) (void *p_opaque, void *p_buf, int nb_pcm_frames);
    void *p_opaque;         /* Saved context */
    int frequency;          /* Configured Frequency */
    int nb_channel;         /* Configured Number of Channels */
    int bits_per_sample;    /* Configured Number of bits per Sample */
    int nb_frames;          /* Configured Number of PCM Frames */
    enum btpcm_state state; /* free/opened/started */
    int timer;
    void *private_data;
};

struct btpcm_ops
{
    int (*init) (struct btpcm *p_btpcm, const char *name);
    void (*exit) (struct btpcm *p_btpcm);
    int (*open) (struct btpcm *p_btpcm);
    int (*close) (struct btpcm *p_btpcm);
    int (*config) (struct btpcm *p_btpcm,
            void (*callback) (struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames));
    int (*start) (struct btpcm *p_btpcm, int nb_pcm_frames, int nb_pcm_packets, int synchronization);
    int (*stop) (struct btpcm *p_btpcm);
    void (*sync) (struct btpcm *p_btpcm);
};

#endif /* BTPCM_H */
