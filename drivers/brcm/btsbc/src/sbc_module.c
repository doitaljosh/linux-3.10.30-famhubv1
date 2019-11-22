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


#include "sbc_module.h"
#include "btsbc_api.h"
#include "sbc.h"

#ifndef BTSBC_CHANNEL_MAX
#define BTSBC_CHANNEL_MAX 2
#endif

#define CODEC_MODE_JOIN_STEREO      0x01
#define CODEC_MODE_STEREO           0x02
#define CODEC_MODE_DUAL             0x04
#define CODEC_MODE_MONO             0x08

#define CODEC_SBC_ALLOC_LOUDNESS    0x01
#define CODEC_SBC_ALLOC_SNR         0x02

#if defined(BTSBC_MEASUREMENT) && (BTSBC_MEASUREMENT < 1000)
#warning "BTSBC_MEASUREMENT too small. It will generate many print messages"
#endif

struct sbc_channel
{
    struct sbc_struct sbc;
    int configured;
#ifdef BTSBC_MEASUREMENT
    unsigned int measurement_cumulated;
    unsigned int measurement_cumulated_nb;
    unsigned int measurement_counter;
    unsigned int measurement_min;
    unsigned int measurement_max;
#endif
};


struct sbc_module
{
    struct sbc_channel *ccb[BTSBC_CHANNEL_MAX];
};

/*
 * Global variables
 */
struct sbc_module sbc_module_cb;

/*
 * Local functions
 */

/*******************************************************************************
 **
 ** Function         btsbc_alloc
 **
 ** Description      BTSBC Alloc function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_alloc(void)
{
    struct sbc_channel *p_sbc;
    int channel;

    for (channel = 0 ; channel < BTSBC_CHANNEL_MAX ; channel++)
    {
        if (sbc_module_cb.ccb[channel] == NULL)
        {
            /* Found one free channel */
            break;
        }
    }

    if (channel >= BTSBC_CHANNEL_MAX)
    {
        BTSBC_ERR("No more free SBC Channel\n");
        return -ENOMEM;
    }

    /* Allocate SBC */
    p_sbc = kmalloc(sizeof(struct sbc_channel), GFP_KERNEL);
    if (p_sbc == NULL)
    {
        BTSBC_ERR("kalloc failed\n");
        return -ENOMEM;
    }

    /* Clear it */
    memset (p_sbc, 0, sizeof(*p_sbc));

    /* Initialize it */
    if (sbc_init(&p_sbc->sbc, 0) < 0)
    {
        BTSBC_ERR("sbc_init failed\n");
        kfree(p_sbc);
        return -ENOMEM;
    }

#ifdef BTSBC_MEASUREMENT
    p_sbc->measurement_min = (unsigned int)-1;  /* Set min to an high value */
#endif

    p_sbc->configured = 0;

    /* Save the allocated SBC */
    sbc_module_cb.ccb[channel] = p_sbc;

    BTSBC_DBG("sbc_channel %d allocated\n", channel);

    return channel;
}
EXPORT_SYMBOL(btsbc_alloc);

/*******************************************************************************
 **
 ** Function         btsbc_free
 **
 ** Description      BTSBC Free function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_free(int sbc_channel)
{
    BTSBC_DBG("sbc_channel=%d\n", sbc_channel);

    if ((sbc_channel < 0) || (sbc_channel >= BTSBC_CHANNEL_MAX))
    {
        BTSBC_ERR("Bad SBC Channel=%d\n", sbc_channel);
        return -EINVAL;
    }

    if (sbc_module_cb.ccb[sbc_channel] == NULL)
    {
        BTSBC_ERR("SBC Channel=%d not opened\n", sbc_channel);
        return -EINVAL;
    }

    /* Free private memory */
    sbc_finish(&sbc_module_cb.ccb[sbc_channel]->sbc);

    /* Free the SBC */
    kfree(sbc_module_cb.ccb[sbc_channel]);

    sbc_module_cb.ccb[sbc_channel] = NULL;

    return 0;
}
EXPORT_SYMBOL(btsbc_free);

/*******************************************************************************
 **
 ** Function         btsbc_config
 **
 ** Description      BTSBC Module Configure function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_config(int sbc_channel, int frequency, unsigned char blocks,
        unsigned char subbands, unsigned char mode, unsigned char allocation, unsigned char bitpool)
{
    struct sbc_channel *p_ccb;
    size_t sbc_frame_size;


    BTSBC_DBG("sbc_channel=%d frequency=%d blocks=%d subbands=%d mode=%d allocation=%d bitpool=%d\n",
            sbc_channel, frequency, blocks, subbands, mode, allocation, bitpool);

    if ((sbc_channel < 0) || (sbc_channel >= BTSBC_CHANNEL_MAX))
    {
        BTSBC_ERR("Bad SBC Channel=%d\n", sbc_channel);
        return -EINVAL;
    }

    switch(frequency)
    {
    case 16000:
        frequency = SBC_FREQ_16000;
        break;
    case 32000:
        frequency = SBC_FREQ_32000;
        break;
    case 44100:
        frequency = SBC_FREQ_44100;
        break;
    case 48000:
        frequency = SBC_FREQ_48000;
        break;
    default:
        BTSBC_ERR("Bad Frequency=%d\n", frequency);
        return -EINVAL;
    }

    switch(blocks)
    {
    case 4:
        blocks = SBC_BLK_4;
        break;
    case 8:
        blocks = SBC_BLK_8;
        break;
    case 12:
        blocks = SBC_BLK_12;
        break;
    case 16:
        blocks = SBC_BLK_16;
        break;
    default:
        BTSBC_ERR("Bad NbBlock=%d\n", blocks);
        return -EINVAL;
    }

    switch(mode)
    {
    case CODEC_MODE_MONO:
        mode = SBC_MODE_MONO;
        break;
    case CODEC_MODE_DUAL:
        mode = SBC_MODE_DUAL_CHANNEL;
        break;
    case CODEC_MODE_STEREO:
        mode = SBC_MODE_STEREO;
        break;
    case CODEC_MODE_JOIN_STEREO:
        mode = SBC_MODE_JOINT_STEREO;
        break;
    default:
        BTSBC_ERR("Bad Mode=%d\n", mode);
        return -EINVAL;
    }

    switch(allocation)
    {
    case CODEC_SBC_ALLOC_LOUDNESS:
        allocation = SBC_AM_LOUDNESS;
        break;
    case CODEC_SBC_ALLOC_SNR:
        allocation = SBC_AM_SNR;
        break;
    default:
        BTSBC_ERR("Bad allocation=%d\n", allocation);
        return -EINVAL;
    }

    switch(subbands)
    {
    case 4:
        subbands = SBC_SB_4;
        break;
    case 8:
        subbands = SBC_SB_8;
        break;
    default:
        BTSBC_ERR("Bad NbsubBand=%d\n", subbands);
        return -EINVAL;
    }

    /* Get SBC reference */
    p_ccb = sbc_module_cb.ccb[sbc_channel];

    if (p_ccb == NULL)
    {
        BTSBC_ERR("SBC Channel=%d not opened\n", sbc_channel);
        return -EINVAL;
    }

    /* Save SBC parameters */
    p_ccb->sbc.frequency = (uint8_t)frequency;
    p_ccb->sbc.mode = mode;
    p_ccb->sbc.subbands = subbands;
    p_ccb->sbc.blocks = blocks;
    p_ccb->sbc.allocation = allocation;
    p_ccb->sbc.bitpool = bitpool;

    p_ccb->configured = 1;

    /* Get the size of an SBC frame */
    sbc_frame_size = sbc_get_frame_length(&p_ccb->sbc);
    BTSBC_INFO("sbc_frame_size=%lu\n", sbc_frame_size);

     return (int)sbc_frame_size;
}
EXPORT_SYMBOL(btsbc_config);

/*******************************************************************************
 **
 ** Function         btsbc_encode
 **
 ** Description      BTSBC Encode function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_encode(int sbc_channel, const void *input, unsigned int input_len,
            void *output, unsigned int output_len, int *p_written)
{
    struct sbc_channel *p_ccb;
    ssize_t sbc_ret;
    ssize_t written;
#ifdef BTSBC_MEASUREMENT
    struct timespec time_stamp_before, time_stamp_after;
    unsigned int duration;
#endif

    if ((sbc_channel < 0) || (sbc_channel >= BTSBC_CHANNEL_MAX))
    {
        BTSBC_ERR("Bad SBC Channel=%d\n", sbc_channel);
        return -EINVAL;
    }

    /* Get SBC reference */
    p_ccb = sbc_module_cb.ccb[sbc_channel];

    if (p_ccb == NULL)
    {
        BTSBC_ERR("SBC Channel=%d not opened\n", sbc_channel);
        return -EINVAL;
    }

    if (p_ccb->configured == 0)
    {
        BTSBC_ERR("SBC Channel=%d not configured\n", sbc_channel);
        return -EINVAL;
    }

#ifdef BTSBC_MEASUREMENT
    p_ccb->measurement_counter++;
    if (p_ccb->measurement_counter > BTSBC_MEASUREMENT)
    {
        /* Get Time Stamp before SBC Encoding */
        ktime_get_real_ts(&time_stamp_before);
    }
#endif

    /* Encode the PCM Buffer */
    sbc_ret = sbc_encode(&p_ccb->sbc, input, input_len,
            output, output_len, &written);

#ifdef BTSBC_MEASUREMENT
    if (p_ccb->measurement_counter > BTSBC_MEASUREMENT)
    {
        /* Reset counter */
        p_ccb->measurement_counter = 0;

        /* Get Time Stamp After SBC Encoding */
        ktime_get_real_ts(&time_stamp_after);

        /* Calculate the Encoding duration (in usec) */
        duration = (time_stamp_after.tv_nsec - time_stamp_before.tv_nsec) / 1000;

        if (duration < p_ccb->measurement_min)
        {
            p_ccb->measurement_min = duration;  /* Update Min duration */
        }
        if (duration > p_ccb->measurement_max)
        {
            p_ccb->measurement_max = duration;  /* Update Max duration */
        }

        /* Update number of measurement */
        p_ccb->measurement_cumulated_nb++;

        if (p_ccb->measurement_cumulated_nb)
        {
            /* Update the cumulated duration */
            p_ccb->measurement_cumulated += duration;
        }
        else
        {
            /* Set the cumulated duration (this is to prevent division by 0)*/
            p_ccb->measurement_cumulated = duration;
            p_ccb->measurement_cumulated_nb = 1;
            BTSBC_DBG("counter wrap\n");
        }
        BTSBC_DBG("SBC Encoding duration=%uus min=%uus max=%uus avg=%uus\n",
                duration, p_ccb->measurement_min, p_ccb->measurement_max,
                p_ccb->measurement_cumulated / p_ccb->measurement_cumulated_nb);
    }
#endif

    if (p_written)
    {
        *p_written = (int)written;
    }
    return sbc_ret;

}
EXPORT_SYMBOL(btsbc_encode);

/*******************************************************************************
 **
 ** Function         btsbc_module_init
 **
 ** Description      BTSBC Module Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int __init btsbc_module_init(void)
{
    BTSBC_INFO("module inserted\n");

    /* Clear the Control Block */
    memset(&sbc_module_cb, 0, sizeof(sbc_module_cb));

    return 0;
}
module_init(btsbc_module_init);

/*******************************************************************************
 **
 ** Function         btsbc_module_exit
 **
 ** Description      BTSBC Module Exit function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void __exit btsbc_module_exit(void)
{
    struct sbc_channel *p_ccb;
    int sbc_channel;

    /* For every SBC */
    for (sbc_channel = 0 ; sbc_channel < BTSBC_CHANNEL_MAX ; sbc_channel++)
    {
        /* Get SBC reference */
        p_ccb = sbc_module_cb.ccb[sbc_channel];
        /* If it's opened */
        if(p_ccb)
        {
            btsbc_free(sbc_channel);   /* Close it */
        }
    }

    BTSBC_INFO("module removed\n");
}
module_exit(btsbc_module_exit);


MODULE_DESCRIPTION("BlueZ SBC Codec");
MODULE_LICENSE("GPL");

