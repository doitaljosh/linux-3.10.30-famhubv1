/*
*
* btpcm_api.h
*
*
*
* Copyright (C) 2013-2014 Broadcom Corporation.
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

#ifndef BTPCM_API_H
#define BTPCM_API_H

struct btpcm;

/*******************************************************************************
 **
 ** Function         btpcm_init
 **
 ** Description      BTPCM Init function.
 **
 ** Returns          Allocated BTPCM control block, NULL in case of error
 **
 *******************************************************************************/
struct btpcm *btpcm_init(const char *name);

/*******************************************************************************
 **
 ** Function         btpcm_exit
 **
 ** Description      BTPCM Exit function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_exit(struct btpcm *p_btpcm);

/*******************************************************************************
 **
 ** Function         btpcm_open
 **
 ** Description      BTPCM Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_open(struct btpcm *p_btpcm);

/*******************************************************************************
 **
 ** Function         btpcm_close
 **
 ** Description      BTPCM Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_close(struct btpcm *p_btpcm);

/*******************************************************************************
 **
 ** Function         btpcm_config
 **
 ** Description      BTPCM Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_config(struct btpcm *p_btpcm, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
        void (*callback) (void *p_opaque, void *p_buf, int nb_pcm_frames));

/*******************************************************************************
 **
 ** Function         btpcm_start
 **
 ** Description      BTPCM Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_start(struct btpcm *p_btpcm, int nb_pcm_frames, int nb_pcm_packets, int synchronization);

/*******************************************************************************
 **
 ** Function         btpcm_stop
 **
 ** Description      BTPCM Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_stop(struct btpcm *p_btpcm);

/*******************************************************************************
 **
 ** Function        btpcm_synchonization
 **
 ** Description     BTPCM Stream Synchronization function.
 **                 This function is called (for Broadcast AV channels) every time
 **                 a Broadcast Synchronization VSE is received (every 20ms).
 **                 The BTPCM can use this event to as timing reference to either
 **                 call the PCM callback or to perform PCM rate adaptation (to
 **                 compensate the clock drift between the Host and Controller).
 **                 Note that this function is subject to jitter (up to 20 ms)
 **
 ** Returns         void
 **
 *******************************************************************************/
void btpcm_synchonization(struct btpcm *p_btpcm);

#endif

