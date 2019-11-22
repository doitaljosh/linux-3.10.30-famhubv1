/*
 *
 * btpcm_hrtimer.c
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/version.h>

#include <linux/hardirq.h>

#include <btpcm.h>
#include <btpcm_hrtimer.h>

/*
 * Defines
 */
struct btpcm_hrtimer_cb
{
    long resolution;
};

/*
 * Globals
 */
static struct btpcm_hrtimer_cb btpcm_hrtimer_cb = { .resolution = 0 };

/*
 * hrtimer interface
 */

static enum hrtimer_restart btpcm_hrtimer_callback(struct hrtimer *timer);
static void btpcm_hrtimer_tasklet(unsigned long priv);

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_init
 **
 ** Description      Initialize HR timer sub-system.
 **
 ** Returns          status
 **
 *******************************************************************************/
int btpcm_hrtimer_init(void)
{
    struct timespec tp;

    hrtimer_get_res(CLOCK_MONOTONIC, &tp);
    if (tp.tv_sec > 0 || !tp.tv_nsec)
    {
        BTPCM_ERR("Invalid resolution %u.%09ld\n",
               (unsigned)tp.tv_sec, tp.tv_nsec);
        return -EINVAL;
    }

    btpcm_hrtimer_cb.resolution = tp.tv_nsec;

    BTPCM_INFO("HR Timer resolution=%ld\n", btpcm_hrtimer_cb.resolution);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_exit
 **
 ** Description      Exit HR timer sub-system (stop/free all timers)
 **
 ** Returns          status
 **
 *******************************************************************************/
void btpcm_hrtimer_exit(void)
{
    BTPCM_DBG("\n");

    /* nothing to do */
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_alloc
 **
 ** Description      Allocate a HR timer.
 **
 ** Returns          Allocated HR timer
 **
 *******************************************************************************/
struct btpcm_hrtimer *btpcm_hrtimer_alloc(void (*callback)(void *p_opaque), void *p_opaque)
{
    struct btpcm_hrtimer *p_timer;

    BTPCM_DBG("\n");

    if (btpcm_hrtimer_cb.resolution == 0)
    {
        BTPCM_ERR("btpcm_hrtimer not initialized\n");
        return NULL;
    }

    p_timer = kzalloc(sizeof(*p_timer), GFP_KERNEL);
    if (!p_timer)
    {
        BTPCM_ERR("No more memory\n");
        return NULL;
    }

    hrtimer_init(&p_timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    p_timer->timer.function = btpcm_hrtimer_callback;
    p_timer->callback = callback;
    p_timer->p_opaque = p_opaque;

    atomic_set(&p_timer->running, 0);
    tasklet_init(&p_timer->tasklet, btpcm_hrtimer_tasklet, (unsigned long)p_timer);

    return p_timer;
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_free
 **
 ** Description      Free a HR timer.
 **
 ** Returns          Allocated HR timer
 **
 *******************************************************************************/
void btpcm_hrtimer_free(struct btpcm_hrtimer *p_timer)
{
    BTPCM_DBG("\n");

    if (btpcm_hrtimer_cb.resolution == 0)
    {
        BTPCM_ERR("btpcm_hrtimer not initialized\n");
        return;
    }

    if (!p_timer)
    {
        BTPCM_ERR("Null p_timer\n");
        return;
    }

    tasklet_kill(&p_timer->tasklet);
    kfree(p_timer);
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_start
 **
 ** Description      Start an HR timer.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_hrtimer_start(struct btpcm_hrtimer *p_timer, unsigned long period_ns)
{
    BTPCM_INFO("period=%lu\n", period_ns);

    if (btpcm_hrtimer_cb.resolution == 0)
    {
        BTPCM_ERR("btpcm_hrtimer not initialized\n");
        return -EINVAL;
    }

    if (!p_timer)
    {
        BTPCM_ERR("Null p_timer\n");
        return -EINVAL;
    }

    if (period_ns < 1000000)
    {
        BTPCM_ERR("period=%lu too small\n", period_ns);
        return -EINVAL;
    }

    tasklet_kill(&p_timer->tasklet);

    p_timer->period_time = ktime_set(0, period_ns);
    p_timer->base_time = hrtimer_cb_get_time(&p_timer->timer);
    hrtimer_start(&p_timer->timer, p_timer->period_time, HRTIMER_MODE_REL);
    atomic_set(&p_timer->running, 1);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_stop
 **
 ** Description      Stop an HR timer.
 **
 ** Returns          status
 **
 *******************************************************************************/
int btpcm_hrtimer_stop(struct btpcm_hrtimer *p_timer)
{
    BTPCM_DBG("\n");

    if (btpcm_hrtimer_cb.resolution == 0)
    {
        BTPCM_ERR("btpcm_hrtimer not initialized\n");
        return -EINVAL;
    }

    if (!p_timer)
    {
        BTPCM_ERR("Null p_timer\n");
        return -EINVAL;
    }

    atomic_set(&p_timer->running, 0);
    hrtimer_cancel(&p_timer->timer);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_callback
 **
 ** Description
 **
 ** Returns
 **
 *******************************************************************************/
static enum hrtimer_restart btpcm_hrtimer_callback(struct hrtimer *timer)
{
    struct btpcm_hrtimer *p_timer;

    p_timer = container_of(timer, struct btpcm_hrtimer, timer);
    if (!atomic_read(&p_timer->running))
        return HRTIMER_NORESTART;

    tasklet_schedule(&p_timer->tasklet);
    hrtimer_forward_now(timer, p_timer->period_time);

    return HRTIMER_RESTART;
}

/*******************************************************************************
 **
 ** Function         btpcm_hrtimer_tasklet
 **
 ** Description
 **
 ** Returns
 **
 *******************************************************************************/
static void btpcm_hrtimer_tasklet(unsigned long priv)
{
    struct btpcm_hrtimer *p_timer = (struct btpcm_hrtimer *)priv;

    if (atomic_read(&p_timer->running))
    {
        if (p_timer->callback)
        {
            p_timer->callback(p_timer->p_opaque);
        }
    }
}

