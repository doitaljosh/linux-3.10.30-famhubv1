/*
 *
 * btpcm_hrtimer.h
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

#ifndef BTPCM_HRTIMER_H
#define BTPCM_HRTIMER_H

#include <linux/hrtimer.h>
#include <linux/interrupt.h>

struct btpcm_hrtimer {
    ktime_t base_time;
    ktime_t period_time;
    atomic_t running;
    struct hrtimer timer;
    struct tasklet_struct tasklet;
    void  (*callback) (void *p_opaque);
    void *p_opaque;
};

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_init
 **
 ** Description      Initialize HR timer sub-system.
 **
 ** Returns          status
 **
 *******************************************************************************/
int btpcm_hrtimer_init(void);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_exit
 **
 ** Description      Exit HR timer sub-system (stop/free all timers)
 **
 ** Returns          status
 **
 *******************************************************************************/
void btpcm_hrtimer_exit(void);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_alloc
 **
 ** Description      Allocate a HR timer.
 **
 ** Returns          Allocated HR timer
 **
 *******************************************************************************/
struct btpcm_hrtimer *btpcm_hrtimer_alloc(void (*callback)(void *p_opaque), void *p_opaque);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_free
 **
 ** Description      Free a HR timer.
 **
 ** Returns          status
 **
 *******************************************************************************/
void btpcm_hrtimer_free(struct btpcm_hrtimer *p_timer);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_start
 **
 ** Description      Start an HR timer.
 **
 ** Returns          status
 **
 *******************************************************************************/
int btpcm_hrtimer_start(struct btpcm_hrtimer *p_timer, unsigned long period_ns);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_stop
 **
 ** Description      Stop an HR timer.
 **
 ** Returns          status
 **
 *******************************************************************************/
int btpcm_hrtimer_stop(struct btpcm_hrtimer *p_timer);

#endif

