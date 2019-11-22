/*
 *
 * btpcm_core.c
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

#include <linux/module.h>
#include <linux/slab.h>

#include "btpcm.h"
#include "btpcm_hrtimer.h"
#include "btpcm_api.h"


extern const struct btpcm_ops btpcm_tone_ops;
extern const struct btpcm_ops btpcm_alsa_ops;
extern const struct btpcm_ops btpcm_ibiza_ops;

/* BTPCM Operation (exported by another C file depending on compile option) */
static const struct btpcm_ops *p_btpcm_ops =
#if defined(BTPCM_TONE)
    &btpcm_tone_ops;
#elif defined(BTPCM_ALSA)
    &btpcm_alsa_ops;
#elif defined(BTPCM_IBIZA)
    &btpcm_ibiza_ops;
#endif
/*
 * Local functions
 */
static void btpcm_callback(struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames);
static int __init btpcm_module_init(void);
static void __exit btpcm_module_exit(void);

/*******************************************************************************
 **
 ** Function         btpcm_init
 **
 ** Description      BTPCM Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
struct btpcm *btpcm_init(const char *name)
{
    struct btpcm *p_btpcm;

    p_btpcm = kzalloc(sizeof(*p_btpcm), GFP_KERNEL);
    if (!p_btpcm)
    {
        BTPCM_ERR("Failed allocating btpcm\n");
        return NULL;
    }

    if (p_btpcm_ops->init)
    {
        if (p_btpcm_ops->init(p_btpcm, name) < 0)
        {
            kfree(p_btpcm);
            return NULL;
        }
    }
    return p_btpcm;
}
EXPORT_SYMBOL(btpcm_init);

/*******************************************************************************
 **
 ** Function         btpcm_exit
 **
 ** Description      BTPCM Exit function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_exit(struct btpcm *p_btpcm)
{
    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    if (p_btpcm_ops->exit)
    {
        p_btpcm_ops->exit(p_btpcm);
    }

    kfree(p_btpcm);
    return 0;
}
EXPORT_SYMBOL(btpcm_exit);

/*******************************************************************************
 **
 ** Function         btpcm_open
 **
 ** Description      BTPCM Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_open(struct btpcm *p_btpcm)
{
    int err;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    if (p_btpcm->state != STATE_FREE)
    {
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return -EINVAL;
    }

    if (p_btpcm_ops->open)
    {
        err = p_btpcm_ops->open(p_btpcm);
        if (err < 0)
        {
            return err;
        }
        p_btpcm->state = STATE_OPENED;
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_open);

/*******************************************************************************
 **
 ** Function         btpcm_close
 **timer
 ** Description      BTPCM Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_close(struct btpcm *p_btpcm)
{
    int err;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    switch (p_btpcm->state)
    {
    case STATE_FREE:
        BTPCM_WRN("stream already closed\n");
        /* No break on purpose */

    case STATE_OPENED:
    case STATE_CONFIGURED:
        break;

    case STATE_STARTED:
        btpcm_stop(p_btpcm);
        break;

    default:
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return -EINVAL;
    }

    if (p_btpcm_ops->close)
    {
        err = p_btpcm_ops->close(p_btpcm);
        if (err < 0)
        {
            return err;
        }
    }

    p_btpcm->state = STATE_FREE;

    return 0;
}
EXPORT_SYMBOL(btpcm_close);


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
        void (*callback) (void *p_opaque, void *p_buf, int nb_pcm_frames))
{
    int err;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    BTPCM_DBG("frequency=%d nb_channel=%d bits_per_sample=%d\n",
            frequency, nb_channel, bits_per_sample);

    if (!p_opaque)
    {
        BTPCM_ERR("Null p_opaque\n");
        return -EINVAL;
    }

    if (!callback)
    {
        BTPCM_ERR("No Callback Function\n");
        return -EINVAL;
    }

    /* Only stereo mode supported */
    if (nb_channel != 2)
    {
        BTPCM_ERR("Unsupported PCM NbChannel=%d\n", nb_channel);
        return -EINVAL;
    }

    /* Only 16 bits per sample mode supported */
    if (bits_per_sample != 16)
    {
        BTPCM_ERR("Unsupported PCM BitsPerSamples=%d\n", bits_per_sample);
        return -EINVAL;
    }

    switch (p_btpcm->state)
    {
    case STATE_OPENED:
    case STATE_CONFIGURED:
        break;

    case STATE_FREE:
    case STATE_STARTED:
    default:
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return -EINVAL;
    }

    p_btpcm->callback = callback;
    p_btpcm->p_opaque = p_opaque;
    p_btpcm->frequency = frequency;
    p_btpcm->nb_channel = nb_channel;
    p_btpcm->bits_per_sample = bits_per_sample;
    p_btpcm->nb_frames = 0;

    if (p_btpcm_ops->config)
    {
        err = p_btpcm_ops->config(p_btpcm, btpcm_callback);
        if (err < 0)
        {
            /* Mark this pcm_stream as opened */
            p_btpcm->state = STATE_OPENED;
            return err;
        }
        p_btpcm->state = STATE_CONFIGURED;
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_config);

/*******************************************************************************
 **
 ** Function         btpcm_start
 **
 ** Description      BTPCM Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_start(struct btpcm *p_btpcm, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    int err;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    BTPCM_INFO("nb_pcm_frames=%d nb_pcm_packets=%d synchronization=%d\n",
            nb_pcm_frames, nb_pcm_packets, synchronization);

    if (p_btpcm->state != STATE_CONFIGURED)
    {
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return -EINVAL;
    }

    p_btpcm->nb_frames = nb_pcm_frames;
    p_btpcm->state = STATE_STARTED;

    if (p_btpcm_ops->start)
    {
        err = p_btpcm_ops->start(p_btpcm, nb_pcm_frames, nb_pcm_packets, synchronization);
        if (err < 0)
        {
            BTPCM_ERR("Start op failed err=%d\n", err);
            p_btpcm->state = STATE_CONFIGURED;
            return err;
        }
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_start);

/*******************************************************************************
 **
 ** Function         btpcm_stop
 **
 ** Description      BTPCM Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_stop(struct btpcm *p_btpcm)
{
    int err;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return -EINVAL;
    }

    if (p_btpcm->state != STATE_STARTED)
    {
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return -EINVAL;
    }

    if (p_btpcm_ops->stop)
    {
        err = p_btpcm_ops->stop(p_btpcm);
        if (err < 0)
        {
            return err;
        }
        p_btpcm->state = STATE_CONFIGURED;
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_stop);

/*******************************************************************************
 **,
 ** Function        btpcm_synchonization
 **
 ** Description     BTPCM Stream Synchronization function.
 **                 This function is called for Broadcast AV channel every time
 **                 a Broadcast Synchronization VSE is received (every 20ms)
 **                 It can be used by the PCM source to perform rate adaptation
 **                 to compensate the clock drift between the Host and Controller
 **                 Note that this function is subject to jitter (up to 20 ms)
 **
 ** Returns         void
 **
 *******************************************************************************/
void btpcm_sync(struct btpcm *p_btpcm)
{
    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return;
    }

    if (p_btpcm->state != STATE_STARTED)
    {
        BTPCM_ERR("Bad state=%d", p_btpcm->state);
        return;
    }

    if (p_btpcm_ops->sync)
    {
        p_btpcm_ops->sync(p_btpcm);
    }
}
EXPORT_SYMBOL(btpcm_sync);

/*******************************************************************************
 **
 ** Function        btpcm_callback
 **
 ** Description     BTPCM Stream Callback function.
 **
 ** Returns         Void
 **
 *******************************************************************************/
static void btpcm_callback(struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames)
{
    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_btpcm)
    {
        BTPCM_ERR("no btpcm instance\n");
        return;
    }

    if (p_btpcm->state != STATE_STARTED)
    {
        BTPCM_ERR("Bad state=%d\n", p_btpcm->state);
        return;
    }
    if (p_btpcm->callback)
    {
        p_btpcm->callback(p_btpcm->p_opaque, p_buf, nb_pcm_frames);
    }
}

/*******************************************************************************
 **
 ** Function         btpcm_module_init
 **
 ** Description      BTPCM Module Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int __init btpcm_module_init(void)
{
    int rv;

    BTPCM_INFO("module inserted\n");

    rv = btpcm_hrtimer_init();
    if (rv < 0)
    {
        return rv;
    }

    return 0;
}
module_init(btpcm_module_init);

/*******************************************************************************
 **
 ** Function         btpcm_module_exit
 **
 ** Description      BTPCM Module Exit function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void __exit btpcm_module_exit(void)
{
    BTPCM_INFO("module removed\n");

    btpcm_hrtimer_exit();
}
module_exit(btpcm_module_exit);


MODULE_DESCRIPTION("Broadcom Bluetooth PCM driver");
MODULE_LICENSE("GPL");
