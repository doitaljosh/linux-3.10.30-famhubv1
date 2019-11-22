/*
 *
 * btpcm_tone.c
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

#define USE_BTPCM_HRTIMER


#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include <linux/timer.h>
#include <linux/jiffies.h>

#include "btpcm.h"
#include "btpcm_hrtimer.h"


#ifndef USE_BTPCM_HRTIMER
#if (HZ != 100) && (HZ != 1000)
#error "Only HZ 100 and 1000 supported"
#endif
#endif

/*
 * Definitions
 */
/* For 48Hz, the reference frame number is 96 (12*8). 96 frames means 2 ms */
#define BTPCM_TONE_NB_FRAMES    (12 * 8)

/* For 16bits per samples, Stereo, the PCM Frame size is 4 (2 * 2) */
#define BTPCM_FRAME_SIZE        (BTPCM_SAMPLE_16BIT_SIZE * BTPCM_SAMPLE_STEREO_SIZE)

/* Two sinus waves */
const short sinwaves[2][64] =
{
        {
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488,
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488
        },
        {
         0,    244,    488,    722,    957,  1173,  1389,   1578,
      1768,   1923,   2079,   2194,   2310,  2381,  2452,   2476,
      2500,   2476,   2452,   2381,   2310,  2194,  2079,   1923,
      1768,   1578,   1389,   1173,    957,   722,   488,    244,
         0,   -244,   -488,   -722,   -957, -1173, -1389,  -1578,
     -1768,  -1923,  -2079,  -2194,  -2310, -2381, -2452,  -2476,
     -2500,  -2476,  -2452,  -2381,  -2310, -2194, -2079,  -1923,
     -1768,  -1578,  -1389,  -1173,   -957,  -722,  -488,   -244
        }
};

/* Tone Channel Control Block */
struct btpcm_tone
{
    void (*callback) (struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames);
#ifdef USE_BTPCM_HRTIMER
    struct btpcm_hrtimer *hr_timer;
#else
    struct timer_list timer;
#endif
    int timer_duration; /* timer duration in jiffies */
    int nb_frames; /* Number of frames per packet */
    int nb_packets; /* Number of packets per timer period */
    atomic_t started;
    void *p_buf;
    int sinus_index;        /* Sinus PCM Index */
    int jiffies;
};

#ifdef USE_BTPCM_HRTIMER
static void btpcm_tone_hrtimer_callback(void *p_opaque);
#else
static void btpcm_tone_timer_routine(unsigned long data);
#endif

/*******************************************************************************
 **
 ** Function         btpcm_tone_read
 **
 ** Description      Fill up a PCM buffer with a Sinux
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_tone_read(int pcm_channel, short *p_buffer, int nb_bytes, int *p_sinus_index)
{
    int index;
    int sinus_index = *p_sinus_index;

    /* Generate a standard PCM stereo interlaced sinewave */
    for (index = 0; index < (nb_bytes / 4); index++)
    {
        p_buffer[index * 2] = sinwaves[pcm_channel][sinus_index % 64];
        p_buffer[index * 2 + 1] = sinwaves[pcm_channel][sinus_index % 64];
        sinus_index++;
    }
    *p_sinus_index = sinus_index;
}

#ifdef USE_BTPCM_HRTIMER
/*******************************************************************************
 **
 ** Function         btpcm_tone_hrtimer_callback
 **
 ** Description      BTPCM Tone High Resolution Timer callback.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_tone_hrtimer_callback(void *p_opaque)
{
    struct btpcm *p_btpcm = p_opaque;
    int nb_packets;
    int delta_error;
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return;
    }

    if (!atomic_read(&p_tone->started))
    {
        BTPCM_DBG("tone stopped\n");
        if (p_tone->p_buf)
            kfree(p_tone->p_buf);
        p_tone->p_buf = NULL;
        return;
    }

    if (!p_tone->p_buf)
    {
        BTPCM_ERR("p_buff is NULL\n");
        atomic_set(&p_tone->started, 0);
        return;
    }

    if (!p_tone->callback)
    {
        BTPCM_ERR("callback is NULL\n");
        atomic_set(&p_tone->started, 0);
        if (p_tone->p_buf)
            kfree(p_tone->p_buf);
        p_tone->p_buf = NULL;
        return;
    }

    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_tone->jiffies);
    delta_error -= p_tone->timer_duration;
    p_tone->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 16)
    {
        BTPCM_ERR("timer expired with delta_error=%d\n", delta_error);
    }

    /* Send the requested number of packets */
    for (nb_packets = 0 ; nb_packets < p_tone->nb_packets; nb_packets++)
    {
        /* Fill the buffer with the requested number of frames */
        btpcm_tone_read(0,
                (short *)p_tone->p_buf,
                p_tone->nb_frames * BTPCM_FRAME_SIZE,
                &p_tone->sinus_index);
        p_tone->callback(p_btpcm, p_tone->p_buf, p_tone->nb_frames);
    }
}

#else
/*******************************************************************************
 **
 ** Function         btpcm_tone_timer_routine
 **
 ** Description      BTPCM Tone Timer routine.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_tone_timer_routine(unsigned long data)
{
    int nb_packets;
    int delta_error;
    struct btpcm *p_btpcm = (void *)(uintptr_t)data;
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return;
    }

    if (!atomic_read(&p_tone->started))
    {
        BTPCM_DBG("tone stopped\n");
        /* del_timer(&p_tone->timer);*/ // is it needed?
        kfree(p_tone->p_buf);
        p_tone->p_buf = 0;
        return;
    }stream

    if (!p_tone->p_buf)
    {
        BTPCM_ERR("p_buff is NULL stream=%d\n", tone_stream);
        p_tone->started = 0;,
        /* del_timer(&p_tone->timer);*/ // is it needed?
        return;
    }

    if (!p_tone->callback)
    {
        BTPCM_ERR("callback is NULL\n");
        p_tone->started = 0;
        /* del_timer(&p_tone->timer);*/ // is it needed?
        kfree(p_tone->p_buf);
        p_tone->p_buf = 0;
        return;
    }

    /* Calculate the timer error */
    delta_error = (int)jiffies_to_msecs(jiffies - p_tone->jiffies);
    delta_error -= p_tone->timer_duration;
    p_tone->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 0)
    {
        BTPCM_ERR("timer expired with delta_error=%d\n", delta_error);
    }
    else
    {
        delta_error = 0;
    }

    /* Restart the timer */
    mod_timer(&p_tone->timer,
            jiffies + p_tone->timer_duration - delta_error); /* restarting timer */

    /* Send the requested number of packets */
    for (nb_packets = 0 ; nb_packets < p_tone->nb_packets; nb_packets++)
    {
        /* fill the buffer with the requested number of frames */
        btpcm_tone_read(0,
                (short *)p_tone->p_buf,
                p_tone->nb_frames * BTPCM_FRAME_SIZE,
                &p_tone->sinus_index);
        p_tone->callback(p_btpcm, p_tone->p_opaque, p_tone->p_buf, p_tone->nb_frames);
    }
}
#endif

/*******************************************************************************
 **
 ** Function         btpcm_tone_open
 **
 ** Description      BTPCM Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_open(struct btpcm *p_btpcm)
{
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return -EINVAL;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_close
 **
 ** Description      BTPCM Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_close(struct btpcm *p_btpcm)
{
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return -EINVAL;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_config
 **
 ** Description      BTPCM Tone Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_config(struct btpcm *p_btpcm,
        void (*callback) (struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames))
{
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return -EINVAL;
    }

    BTPCM_DBG("freq=%d nb_channel=%d bps=%d cback=%p\n",
            p_btpcm->frequency, p_btpcm->nb_channel,
            p_btpcm->bits_per_sample, callback);

    if (!callback)
    {
        BTPCM_ERR("Null Callback\n");
        return -EINVAL;
    }

    if (p_btpcm->frequency != 48000)
    {
        BTPCM_ERR("frequency=%d unsupported\n", p_btpcm->frequency);
	p_btpcm->frequency = 48000;
//        return -EINVAL;
    }

    if (p_btpcm->nb_channel != 2)
    {
        BTPCM_ERR("nb_channel=%d unsupported\n", p_btpcm->nb_channel);
        return -EINVAL;
    }

    if (p_btpcm->bits_per_sample != 16)
    {
        BTPCM_ERR("bits_per_sample=%d unsupported\n", p_btpcm->bits_per_sample);
        return -EINVAL;
    }

    if (atomic_read(&p_tone->started))
    {
        BTPCM_ERR("Tone already started\n");
        return -EINVAL;
    }

    p_tone->callback = callback;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_start
 **
 ** Description      BTPCM Tone Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_start(struct btpcm *p_btpcm, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    int err;
#ifdef USE_BTPCM_HRTIMER
    uint64_t period_ns;
#endif
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return -EINVAL;
    }

    BTPCM_DBG("nb_pcm_frames=%d\n", nb_pcm_frames);

#ifndef USE_BTPCM_HRTIMER
    if ((HZ != 100) && (HZ != 1000))
    {
        BTPCM_ERR("The Linux time reference=%d is neither 100Hz nor 1000Hz\n", HZ);
        return -EINVAL;
    }
#endif

    if (!nb_pcm_frames || !nb_pcm_packets)
    {
        BTPCM_ERR("Bad nb_pcm_frames=%d nb_pcm_packets=%d\n", nb_pcm_frames, nb_pcm_packets);
        return -EINVAL;
    }
    nb_pcm_frames *= nb_pcm_packets;

    if (atomic_read(&p_tone->started))
    {
        BTPCM_ERR("Tone already started\n");
        return -EINVAL;
    }

#ifdef USE_BTPCM_HRTIMER
    /* If synchronization, we don't need timer */
    if (synchronization == 0)
    {
        if (!p_tone->hr_timer)
        {
            p_tone->hr_timer = btpcm_hrtimer_alloc(btpcm_tone_hrtimer_callback,
                    p_btpcm);
            if (!p_tone->hr_timer)
            {
                BTPCM_ERR("No more timer\n");
                return -EINVAL;
            }
        }
    }
#endif

#ifndef USE_BTPCM_HRTIMER
#if (HZ == 100)     /* If the reference timer is 100Hz (10ms) */
    switch (nb_pcm_frames)
    {
    case BTPCM_TONE_NB_FRAMES:  /* 96 frames, 2 ms */
        p_tone->timer_duration = 1; /* 10 ms timer */
        p_tone->nb_frames = nb_pcm_frames;
        p_tone->nb_packets = 5; /* 5 packets every time */
        break;

    case (BTPCM_TONE_NB_FRAMES * 5):  /* 480 frames, 10 ms */
        p_tone->timer_duration = 1; /* 10 ms timer */
        p_tone->nb_frames = nb_pcm_frames;
        p_tone->nb_packets = 1; /* 1 packet every time */
        break;

    case (BTPCM_TONE_NB_FRAMES * 10):  /* 960 frames, 20 ms */
        p_tone->timer_duration = 2; /* 20 ms timer */
        p_tone->nb_frames = nb_pcm_frames;
        p_tone->nb_packets = 1; /* 1 packet every time */
        break;

    case (BTPCM_TONE_NB_FRAMES * 20):  /* 1920 frames, 40 ms */
        p_tone->timer_duration = 4; /* 40 ms timer */
        p_tone->nb_frames = nb_pcm_frames;
        p_tone->nb_packets = 1; /* 1 packet every time */
        break;

    default:
        BTPCM_ERR("nb_pcm_frames=%d unsupported\n", nb_pcm_frames);
        return -EINVAL;
    }
#endif /* HZ == 100 */
#if (HZ == 1000) /* If the reference timer is 1000Hz (1ms) */
    if ((nb_pcm_frames > (BTPCM_TONE_NB_FRAMES * 20)) ||    /* No more than 20 frames */
        ((nb_pcm_frames % BTPCM_TONE_NB_FRAMES) != 0))      /* Entire number */
    {struct
        BTPCM_ERR("nb_pcm_frames=%d unsupported\n", nb_pcm_frames);
        return -EINVAL;
    }

    /* 2ms per SBC frames */
    p_tone->timer_duration = nb_pcm_frames / BTPCM_TONE_NB_FRAMES * 2;
    p_tone->nb_frames = nb_pcm_frames;
    p_tone->nb_packets = 1; /* 1 packet every time */
#endif /* HZ == 1000 */
    BTPCM_DBG("Timer_duration=%d (ms) nb_frames=%d nb_packets=%d\n",
            p_tone->timer_duration,
            p_tone->nb_frames,
            p_tone->nb_packets);
#endif /* !USE_BTPCM_HRTIMER */

/* If High Resolution Timer */
#if defined (USE_BTPCM_HRTIMER)
    period_ns = nb_pcm_frames;
    period_ns *= 1000;
    period_ns *= 1000;
    period_ns *= 1000;
    /* 64 bits division cannot be used directly in Linux Kernel */
    do_div(period_ns, p_btpcm->frequency);
    //p_tone->timer_duration = period_ns / 1000000; /* for debug */
    BTPCM_DBG("period_ns=%lu(ns)\n", period_ns);
    p_tone->nb_frames = nb_pcm_frames;
    p_tone->nb_packets = 1; /* 1 packet every time */
    BTPCM_DBG("HR Timer_duration=%lu(ns) nb_frames=%d nb_packets=%d\n",
            period_ns,
            p_tone->nb_frames,
            p_tone->nb_packets);
#endif

    /* Allocate a PCM buffer able to contain nb_frames samples (stereo,16 bits) */
    p_tone->p_buf = kmalloc(p_tone->nb_frames * BTPCM_FRAME_SIZE, GFP_KERNEL);
    if (!p_tone->p_buf)
    {
        BTPCM_ERR("Unable to allocate buffer (size=%d)\n", (int)(p_tone->nb_frames * BTPCM_FRAME_SIZE));
        return -ENOMEM;
    }

    /* Mark the tone as started */
    atomic_set(&p_tone->started, 1);

    p_tone->jiffies = jiffies;  /* Save the current timestamp (for debug) */

    /* If no synchronization, we need to start a periodic timer */
    if (synchronization == 0)
    {
#ifdef USE_BTPCM_HRTIMER
        /* Start the HR timer */
        err = btpcm_hrtimer_start(p_tone->hr_timer, period_ns);
        if (err < 0)
        {
            BTPCM_ERR("Unable to start timer\n");
            kfree(p_tone->p_buf);
            atomic_set(&p_tone->started, 0);
            return -EINVAL;
        }
#else
        init_timer(&p_tone->timer);
        p_tone->timer.function = btpcm_tone_timer_routine;
        p_tone->timer.data = p_btpcm;
        p_tone->timer.expires = jiffies + p_tone->timer_duration; /* 10 or 20 ms */
        add_timer(&p_tone->timer); /* Starting the timer */
#endif
    }
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_stop
 **
 ** Description      BTPCM Tone Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_stop(struct btpcm *p_btpcm)
{
    int err;
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return -EINVAL;
    }

    if (!atomic_read(&p_tone->started))
    {
        BTPCM_ERR("Tone not started\n");
        return -EINVAL;
    }

#ifdef USE_BTPCM_HRTIMER
    if (p_tone->hr_timer)
    {
        err = btpcm_hrtimer_stop(p_tone->hr_timer);
        if (err < 0)
        {
            BTPCM_ERR("Unable to stop timer\n");
            return -EINVAL;
        }
        btpcm_hrtimer_free(p_tone->hr_timer);
        p_tone->hr_timer = NULL;
    }
#else
    /* Timer will stop itself */
#endif

    /* Mark the tone as stopped */
    atomic_set(&p_tone->started, 0);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_sync
 **
 ** Description      BTPCM Tone Synchronization function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_tone_sync(struct btpcm *p_btpcm)
{
    int delta_error;
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return;
    }

    if (!atomic_read(&p_tone->started))
    {
        BTPCM_DBG("stream stopped\n");
        if (p_tone->p_buf)
            kfree(p_tone->p_buf);
        p_tone->p_buf = NULL;
        return;
    }

    if (!p_tone->p_buf)
    {
        BTPCM_ERR("p_buff is NULL\n");
        atomic_set(&p_tone->started, 0);
        return;
    }

    if (!p_tone->callback)
    {
        BTPCM_ERR("callback is NULL\n");
        atomic_set(&p_tone->started, 0);
        if (p_tone->p_buf)
            kfree(p_tone->p_buf);
        p_tone->p_buf = NULL;
        return;
    }

    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_tone->jiffies);
    delta_error -= p_tone->timer_duration;
    p_tone->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 10)
    {
        BTPCM_ERR("delta_error=%d\n", delta_error);
    }

    /* Fill the buffer with the requested number of frames */
    btpcm_tone_read(0,
            (short *)p_tone->p_buf,
            p_tone->nb_frames * BTPCM_FRAME_SIZE,
            &p_tone->sinus_index);

    p_tone->callback(p_btpcm, p_tone->p_buf, p_tone->nb_frames);
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_init
 **
 ** Description      BTPCM Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_tone_init(struct btpcm *p_btpcm, const char *name)
{
    struct btpcm_tone *p_tone;

    BTPCM_INFO("The Linux time reference (HZ) is %d\n", HZ);

    p_tone = kzalloc(sizeof(*p_tone), GFP_KERNEL);
    if (!p_tone)
    {
        return -ENOMEM;
    }

    p_btpcm->private_data = p_tone;
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_tone_exit
 **
 ** Description      BTPCM Exit function.
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_tone_exit(struct btpcm *p_btpcm)
{
    struct btpcm_tone *p_tone = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_tone)
    {
        BTPCM_ERR("no TONE instance\n");
        return;
    }

    if (atomic_read(&p_tone->started))
    {
        btpcm_tone_stop(p_btpcm);
    }

    kfree(p_tone);
}

const struct btpcm_ops btpcm_tone_ops =
{
    btpcm_tone_init,
    btpcm_tone_exit,
    btpcm_tone_open,
    btpcm_tone_close,
    btpcm_tone_config,
    btpcm_tone_start,
    btpcm_tone_stop,
    btpcm_tone_sync,
};

