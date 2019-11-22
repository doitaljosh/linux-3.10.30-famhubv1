/*
 *
 * btpcm_ibiza.c
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

#define BTPCM_NB_STREAM_MAX 1

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/hardirq.h>

#include <linux/proc_fs.h>  /* Necessary because we use proc fs */
#include <linux/seq_file.h> /* for seq_file */

#include <asm/byteorder.h>  /* For Little/Big Endian */
#include <asm/io.h>

#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>

#include "btpcm.h"
#include "btpcm_hrtimer.h"

/*
 * Definitions
 */

/* For 16bits per samples, Stereo, the PCM Frame size is 4 (2 * 2) */
#define BTPCM_FRAME_SIZE        (BTPCM_SAMPLE_16BIT_SIZE * BTPCM_SAMPLE_STEREO_SIZE)

#ifndef BTPCM_HW_PCM_SAMPLE_SIZE
/* HW PCM Sample size is 4 bytes (Little Endian, MSB unused (24 bits)) */
#define BTPCM_HW_PCM_SAMPLE_SIZE                4
#endif

#if (BTPCM_HW_PCM_SAMPLE_SIZE != 2) && (BTPCM_HW_PCM_SAMPLE_SIZE != 4)
#error "BTPCM_HW_PCM_SAMPLE_SIZE must be either 2 or 4 bytes"
#endif

#ifdef BTPCM_IBIZA_TONE

/* For simulation (tone) use two 20 ms buffer instead of the HW buffer */
#define BTPCM_IBIZA_PCM_BUF_SIZE            (128 * 12 * BTPCM_HW_PCM_SAMPLE_SIZE)
static uint8_t btpcm_ibiza_tone_buf[BTPCM_IBIZA_PCM_BUF_SIZE * 2];
static int btpcm_ibiza_tone_write_offset;
#define __iomem
#define IOREAD8(p) (*(p))       /* Pointer can be used to access the Tone buffer */
#define IOREAD16(p) ioread16(p)
#define IOREAD32(p) ioread32(p)

#else /* BTPCM_IBIZA_TONE */

#include <linux/memory.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <asm/io.h> /* for ioremap/iounmap */

/* Start address of the Left PCM buffer */
#ifndef BTPCM_IBIZA_PCM_BUF_LEFT_ADDR
#define BTPCM_IBIZA_PCM_BUF_LEFT_ADDR       0x0							///0x13D220000
#endif

/* Size of each buffer */
#ifndef BTPCM_IBIZA_PCM_BUF_SIZE
#define BTPCM_IBIZA_PCM_BUF_SIZE            0x15000     /* 84K Bytes */
#endif

/* Address of the Left PCM Write */
#ifndef BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR
#define BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR     0x0						///0x334f0b14
#endif

/* Address of the Enable Mark */
#ifndef BTPCM_IBIZA_PCM_ENABLE_MARK
#define BTPCM_IBIZA_PCM_ENABLE_MARK       0x0
#endif

/* IO Function must be used to access the remapped memory */
/* Note that for ARM architecture, it's just a memory read Macro*/
#define IOREAD8(p) ioread8(p)
#define IOREAD16(p) ioread16(p)

#ifdef __LITTLE_ENDIAN
#define IOREAD32(p) ioread32(p)     /* Little Endian */
#else
#define IOREAD32(p) ioread32be(p)   /* Big Endian */
#endif

#endif /* !BTPCM_IBIZA_TONE */

#define BTPCM_IBIZA_PROC_NAME       "driver/btpcm-ibiza"

/* Commands accepted by /proc/driver/btpcm-ibiza */
#define BTPCM_IBIZA_CMD_DBG     '0'
#define BTPCM_IBIZA_CMD_REOPEN  '1'

enum{
	IBIZA_READY =0,
	IBIZA_WAIT,
	IBIZA_STOP,
};

struct ibiza_thread{
    wait_queue_head_t ibiza_wait_q;
    int	condition;
    int ibiza_count;
    int nb_frames;
    int (*cbThread)(void *arg);					// Thread function
    struct task_struct * cbThread_id;
    struct semaphore threadSync;
};
/* Ibiza Channel Control Block */
struct btpcm_ibiza
{
    int opened;
    void (*callback) (struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames);
    struct btpcm_hrtimer *hr_timer;
    int nb_frames; /* Number of PCM frames */
    int nb_packets; /* Number of packet */
    atomic_t started;
    int left_read_offset;
    void __iomem *p_left_io_pcm;   /* Remapped pointer to Left PCM Buffer */
    void __iomem *p_left_io_ptr;   /* Remapped pointer to Left Write Pointer */
    void __iomem *p_left_io_enablemark;   /* Remapped pointer to Enable Mark Pointer */
    void *p_buf; /* PCM buffer (used to construct an interleaved PCM buffer from the two HW Mono buffers) */
#ifdef BTPCM_IBIZA_DEBUG
    int timer_duration; /* timer duration in jiffies (for debug)  */
    int jiffies; /* debug */
#endif
#ifndef BTPCM_IBIZA_TONE
    uint64_t ibiza_pcm_buf_left_phy_addr;
    uint64_t ibiza_pcm_buf_size;
    uint64_t ibiza_pcm_left_write_phy_addr;
    uint64_t ibiza_enable_mark_addr;
    void * p_enable_mark_pointer_before;
#endif /* ! BTPCM_IBIZA_TONE */
    struct ibiza_thread ib_thread;
    wait_queue_head_t close_wait_q;
};

/*
 * Global variables
 */
#ifdef BTPCM_IBIZA_TONE
/* Two sinus waves (one for left and one for right) */
static const short btpcm_ibiza_sinwaves[2][64] =
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

static uint btpcm_ibiza_param_test = 0xE0ECC000;
module_param(btpcm_ibiza_param_test, uint, 0664);
MODULE_PARM_DESC(btpcm_ibiza_param_test, "BTPCM Ibiza Test Parameter");

#else /* BTPCM_IBIZA_TONE */

#define BTPCM_IBIZA_PARAM_PERM  (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)

/* Ibiza's Platform Default PCM Buffer Left Physical Address */
static uint64_t ibiza_pcm_buf_left_phy_addr = BTPCM_IBIZA_PCM_BUF_LEFT_ADDR;
static uint32_t ibiza_pcm_buf_left_phy_addr_U32 = ((0xFFFFFFFF00000000 & BTPCM_IBIZA_PCM_BUF_LEFT_ADDR) >> 32);
static uint32_t ibiza_pcm_buf_left_phy_addr_L32 = 0x00000000FFFFFFFF & BTPCM_IBIZA_PCM_BUF_LEFT_ADDR;
module_param(ibiza_pcm_buf_left_phy_addr_U32, uint, BTPCM_IBIZA_PARAM_PERM);
module_param(ibiza_pcm_buf_left_phy_addr_L32, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_buf_left_phy_addr_U32, "Ibiza PCM Buffer Left Physical Address Upper 32 bit");
MODULE_PARM_DESC(ibiza_pcm_buf_left_phy_addr_L32, "Ibiza PCM Buffer Left Physical Address Lower 32 bit");

/* Ibiza's Platform Default PCM Buffer Left/Right Size  */
static uint32_t ibiza_pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
module_param(ibiza_pcm_buf_size, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_buf_size, "Ibiza PCM Buffer Size (Left or Right)");

/* Ibiza's Platform Default PCM Buffer Left Write Pointer Address */
static uint64_t ibiza_pcm_left_write_phy_addr = BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR;
static uint32_t ibiza_pcm_left_write_phy_addr_U32 = ((0xFFFFFFFF00000000 & BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR) >> 32);
static uint32_t ibiza_pcm_left_write_phy_addr_L32 = 0x00000000FFFFFFFF & BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR;
module_param(ibiza_pcm_left_write_phy_addr_U32, uint, BTPCM_IBIZA_PARAM_PERM);
module_param(ibiza_pcm_left_write_phy_addr_L32, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_left_write_phy_addr_U32, "Ibiza PCM Buffer Left Write Pointer Physical Address Upper 32 bit");
MODULE_PARM_DESC(ibiza_pcm_left_write_phy_addr_L32, "Ibiza PCM Buffer Left Write Pointer Physical Address Lower 32 bit");

/* Ibiza's Platform Default Enable Mark Address*/
static uint64_t ibiza_enable_mark_addr = BTPCM_IBIZA_PCM_ENABLE_MARK;
static uint32_t ibiza_enable_mark_addr_U32 = ((0xFFFFFFFF00000000 & BTPCM_IBIZA_PCM_ENABLE_MARK) >> 32);
static uint32_t ibiza_enable_mark_addr_L32 = 0x00000000FFFFFFFF & BTPCM_IBIZA_PCM_ENABLE_MARK;
module_param(ibiza_enable_mark_addr_U32, uint, BTPCM_IBIZA_PARAM_PERM);
module_param(ibiza_enable_mark_addr_L32, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_enable_mark_addr_U32, "Ibiza Enable Mark Physical Address Upper 32 bit");
MODULE_PARM_DESC(ibiza_enable_mark_addr_L32, "Ibiza Enable Mark Physical Address Lower 32 bit");

#endif /* !BTPCM_IBIZA_TONE */

/*
 * Local functions
 */
#ifdef BTPCM_IBIZA_TONE
static void btpcm_ibiza_tone_init(struct btpcm_ibiza *p_ibiza);
static void btpcm_ibiza_tone_simulate_write(int nb_pcm_samples);
#endif

static void btpcm_ibiza_hrtimer_callback(void *p_opaque);

static int btpcm_ibiza_pcm_available(struct btpcm_ibiza *p_ibiza);
static void btpcm_ibiza_pcm_read(struct btpcm_ibiza *p_ibiza, void *p_dest_buffer, int nb_frames);

static int btpcm_ibiza_reopen(struct btpcm *p_btpcm);

static int btpcm_ibiza_get_left_write_offset(struct btpcm_ibiza *p_ibiza);

/* Functions in charge of /proc */
static int btpcm_ibiza_file_open(struct inode *inode, struct file *file);
ssize_t btpcm_ibiza_file_write(struct file *file, const char *buf, size_t count,
        loff_t *pos);
static int btpcm_ibiza_file_show(struct seq_file *s, void *v);

/* This structure gather "functions" that manage the /proc file */
static struct file_operations btpcm_ibiza_file_ops =
{
    .owner   = THIS_MODULE,
    .open    = btpcm_ibiza_file_open,
    .read    = seq_read,
    .write   = btpcm_ibiza_file_write,
    .llseek  = seq_lseek,
    .release = single_release
};

struct q_stop_info{
	struct work_struct work;
	struct btpcm *p_btpcm;
};

struct q_stop_info q_info;

static void btpcm_clear_resource(struct work_struct *work);
/*******************************************************************************
 **
 ** Function        btpcm_ibiza_init
 **
 ** Description     This function is called when the /proc file is open.
 **
 ** Returns         Status
 **
 *******************************************************************************/
static int btpcm_ibiza_file_open(struct inode *inode, struct file *file)
{
    return single_open(file, btpcm_ibiza_file_show, BTPCM_PDE_DATA(inode));
};

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_file_write
 **
 ** Description     This function is called when User Space writes in /proc file.
 **
 ** Returns         Status
 **
 *******************************************************************************/
ssize_t btpcm_ibiza_file_write(struct file *file, const char *buf,
        size_t count, loff_t *pos)
{
    unsigned char cmd;
    int status;

    /* copy the first byte from the data written (the command) */
    if (copy_from_user(&cmd, buf, 1))
    {
        return -EFAULT;
    }

    /* Print the command */
    BTPCM_INFO("Command='%c'\n", cmd);

    switch (cmd)
    {
    case BTPCM_IBIZA_CMD_DBG:
#ifdef BTPCM_IBIZA_TONE
        BTPCM_INFO("btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
#else
        BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_buf_left_phy_addr);
        BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
        BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_left_write_phy_addr);
        BTPCM_INFO("ibiza_enable_mark_addr=0x%llx\n", (unsigned long long)ibiza_enable_mark_addr);
#endif
        break;

    case BTPCM_IBIZA_CMD_REOPEN:
        status = btpcm_ibiza_reopen(0);
        if (status < 0)
        {
            count = -1;
        }
        break;

    default:
        BTPCM_ERR("Unknown command=%d\n", cmd);
        break;
    }

    return count;
}

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_file_show
 **
 ** Description     This function is called for each "step" of a sequence.
 **
 ** Returns         None
 **
 *******************************************************************************/
static int btpcm_ibiza_file_show(struct seq_file *s, void *v)
{
    struct btpcm *p_btpcm = s->private;
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);

    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return 0;
    }

    if (p_ibiza->opened)
    {
        seq_printf(s, "Stream=0 Opened\n");
    }
    else
    {
        seq_printf(s, "Stream=0 Closed\n");
    }

    if (atomic_read(&p_ibiza->started))
    {
        seq_printf(s, "Stream=0 Started\n");
    }
    else
    {
        seq_printf(s, "Stream=0 Stopped\n");
    }

    ibiza_pcm_buf_left_phy_addr = (ibiza_pcm_buf_left_phy_addr_U32);
    ibiza_pcm_buf_left_phy_addr <<= 32;
    ibiza_pcm_buf_left_phy_addr |= ibiza_pcm_buf_left_phy_addr_L32;

    ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr_U32;
    ibiza_pcm_left_write_phy_addr <<= 32;
    ibiza_pcm_left_write_phy_addr |= ibiza_pcm_left_write_phy_addr_L32;

    ibiza_enable_mark_addr = ibiza_enable_mark_addr_U32;
    ibiza_enable_mark_addr <<= 32;
    ibiza_enable_mark_addr |= ibiza_enable_mark_addr_L32;

#ifdef BTPCM_IBIZA_TONE
    seq_printf(s, "btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
#else
    if (p_ibiza->opened)
    {
        seq_printf(s, "BTPCM Ibiza is currently opened with:\n");
        seq_printf(s, "Current ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_pcm_buf_left_phy_addr);
        seq_printf(s, "Current ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
        seq_printf(s, "Current ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_pcm_left_write_phy_addr);
        seq_printf(s, "Current ibiza_enable_mark_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_enable_mark_addr);
    }
    seq_printf(s, "BTPCM Ibiza is will be reopened with:\n");
    seq_printf(s, "ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_buf_left_phy_addr);
    seq_printf(s, "ibiza_pcm_buf_size=%d\n", ibiza_pcm_buf_size);
    seq_printf(s, "ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_left_write_phy_addr);
    seq_printf(s, "ibiza_enable_mark_addr=0x%llx\n", (unsigned long long)ibiza_enable_mark_addr);
#endif

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_open
 **
 ** Description      BTPCM Tone Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_open(struct btpcm *p_btpcm)
{
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    if (p_ibiza->opened)
    {
        BTPCM_ERR("Stream already opened\n");
        return -EBUSY;
    }
    BTPCM_INFO("Open Done: No address acquiring now");
#ifdef BTPCM_IBIZA_TONE
    p_ibiza->p_left_io_pcm = &btpcm_ibiza_tone_buf[0];
#endif
    /* Mark this Channel as opened */
    p_ibiza->opened = 1;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_reopen
 **
 ** Description      BTPCM Tone Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_reopen(struct btpcm *p_btpcm)
{
    struct btpcm_ibiza *p_ibiza;

    if(p_btpcm == NULL)
    {
    	BTPCM_ERR("NULL p_btpcm\n");
    	return -EINVAL;
    }

    p_ibiza = p_btpcm->private_data;
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    if (!p_ibiza->opened)
    {
        BTPCM_ERR("Stream not opened\n");
        return -EBUSY;
    }

    if (atomic_read(&p_ibiza->started))
    {
        BTPCM_ERR("Stream not stopped\n");
        return -EBUSY;
    }

    BTPCM_INFO("(Re)Open, internally, the BTPCM Ibiza port\n");

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
    BTPCM_INFO("Nothing to do for Ibiza Tone Simulation\n");
#else /* BTPCM_IBIZA_TONE */

    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr=0x%llx\n", (unsigned long long)ibiza_enable_mark_addr);

    /* Save the Physical addresses which will be used */
    p_ibiza->ibiza_pcm_buf_left_phy_addr = ibiza_pcm_buf_left_phy_addr;
    p_ibiza->ibiza_pcm_buf_size = ibiza_pcm_buf_size;
    p_ibiza->ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr;
    p_ibiza->ibiza_enable_mark_addr = ibiza_enable_mark_addr;

#endif

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_close
 **
 ** Description      BTPCM Tone Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_close(struct btpcm *p_btpcm)
{
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

    BTPCM_ERR("Close\n");
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    if (!p_ibiza->opened)
    {
        BTPCM_ERR("Stream was not opened\n");
        return -EBUSY;
    }

    p_ibiza->opened = 0;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_config
 **
 ** Description      BTPCM Tone Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_config(struct btpcm *p_btpcm,
        void (*callback) (struct btpcm *p_btpcm, void *p_buf, int nb_pcm_frames))
{
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    BTPCM_DBG("freq=%d nb_channel=%d bps=%d cback=%p\n",
            p_btpcm->frequency, p_btpcm->nb_channel, p_btpcm->bits_per_sample, callback);

    if (!callback)
    {
        BTPCM_ERR("Null Callback\n");
        return -EINVAL;
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

    if (atomic_read(&p_ibiza->started))
    {
        BTPCM_ERR("Stream already started\n");
        return -EINVAL;
    }

    /* Save the configuration */
    p_ibiza->callback = callback;

    return 0;
}

static int callback_thread(void *data)
{
	struct btpcm *p_btpcm = (struct btpcm *)data;
	struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

	struct cpumask mask;
	struct sched_param param;

	static int lcl_count = 0;
	int retval =0;
    int nb_pcm_frames;
    int nb_pcm_packets;

	cpulist_parse("1-3", &mask);
	set_cpus_allowed_ptr(current, &mask);

	param.sched_priority = MAX_RT_PRIO - 1;
	sched_setscheduler(current, SCHED_FIFO, &param);

	BTPCM_INFO("Inside callback Thread:%d\n", retval);

    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance in thread\n");
        return -EINVAL;
    }

	while(retval >= 0)
	{
	    if (!p_ibiza)
	    {
	        BTPCM_ERR("no IBIZA instance in thread\n");
	        break;
	    }

		retval = wait_event_interruptible(p_ibiza->ib_thread.ibiza_wait_q,
				(p_ibiza->ib_thread.condition == IBIZA_READY ||
				p_ibiza->ib_thread.condition == IBIZA_STOP));
		if(p_ibiza->ib_thread.condition == IBIZA_STOP)
		{
			BTPCM_ERR("Stopping the kthread\n");
			retval = -1;
			break;
		}
		else if (retval == 0)			/* Condition is true */
		{
			if(p_ibiza->ib_thread.condition == IBIZA_READY)
			{
				p_ibiza->ib_thread.condition = IBIZA_WAIT;

				if(unlikely((p_ibiza->ib_thread.ibiza_count != 0) && (p_ibiza->ib_thread.ibiza_count - lcl_count > 1)))
					BTPCM_ERR("Missed HR timer fire: %d\n", (p_ibiza->ib_thread.ibiza_count - lcl_count));

				lcl_count = p_ibiza->ib_thread.ibiza_count;

			    nb_pcm_frames = 0;
			    nb_pcm_packets = 0;

			#ifdef BTPCM_IBIZA_TONE
			    btpcm_ibiza_tone_simulate_write(p_ibiza->nb_frames * p_ibiza->nb_packets);
			#endif

			    retval = down_interruptible(&p_ibiza->ib_thread.threadSync);
			    if(retval)
			    {
			    	BTPCM_ERR("Sem Interrupted. Time to go!!\n");
			    	break;
			    }
			    else
			    {
			    	/* If stream is stopped in between, break*/
			        if(!atomic_read(&p_ibiza->started))
			        {
			            BTPCM_ERR("Thread: Stream not started\n");
			            up(&p_ibiza->ib_thread.threadSync);
			            break;
			        }
			    }

			    /* While enough PCM samples available */
			    while((btpcm_ibiza_pcm_available(p_ibiza) >= p_ibiza->nb_frames) &&
			          (nb_pcm_packets < (p_ibiza->nb_packets + 1)))
			    {
			        /* Read (and convert) the HW PCM buffer */
			        btpcm_ibiza_pcm_read(p_ibiza, p_ibiza->p_buf + (nb_pcm_packets * p_ibiza->nb_frames * BTPCM_FRAME_SIZE), p_ibiza->nb_frames);
			        nb_pcm_frames += p_ibiza->nb_frames;
			        nb_pcm_packets++;
			    }

				/* Call to do rest of the work till submit*/
				p_ibiza->callback(p_btpcm, p_ibiza->p_buf, nb_pcm_frames);

				up(&p_ibiza->ib_thread.threadSync);
			}
		}
		else if(retval == -ERESTARTSYS )							/* Case when interrupted*/
		{
			BTPCM_ERR("Interrupted;exiting the thread\n");
		}
	}
    BTPCM_INFO("Thread Exiting\n");
    p_ibiza->ib_thread.cbThread_id = NULL;
    return retval;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_start
 **
 ** Description      BTPCM Tone Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_start(struct btpcm *p_btpcm, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    int err;
    uint64_t period_ns;
#ifndef BTPCM_IBIZA_TONE
    struct resource *p_resource;
    unsigned long remap_size;
#endif
#ifdef BTPCM_IBIZA_DEBUG
    uint64_t temp_period_ns;
#endif
	struct btpcm_ibiza *p_ibiza = NULL;

	if(p_btpcm == NULL)
	{
		 BTPCM_ERR("btpcm struct is NULL\n");
         return -EINVAL;
	}


    p_ibiza = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    BTPCM_INFO("nb_pcm_frames=%d nb_pcm_packets=%d sync=%d\n",
            nb_pcm_frames, nb_pcm_packets, synchronization);

    /* BAV (synchronization) not yet supported for Ibiza */
    if (synchronization)
    {
        BTPCM_ERR("BAV (synchronization) not yet supported for Ibiza\n");
        return -EINVAL;
    }

    if (!nb_pcm_frames || !nb_pcm_packets)
    {
        BTPCM_ERR("Bad nb_pcm_frames=%d nb_pcm_packets=%d\n", nb_pcm_frames, nb_pcm_packets);
        return -EINVAL;
    }

    if (atomic_read(&p_ibiza->started))
    {
        BTPCM_WRN("Stream already started\n");
        return -EINVAL;
    }

    flush_scheduled_work();

#ifndef BTPCM_IBIZA_TONE
    /*Doing assignment here so as to capture last moment address update*/
    //ibiza_pcm_buf_left_phy_addr = (ibiza_pcm_buf_left_phy_addr_U32 << 31) | (ibiza_pcm_buf_left_phy_addr_L32);
    ibiza_pcm_buf_left_phy_addr = (ibiza_pcm_buf_left_phy_addr_U32);
    ibiza_pcm_buf_left_phy_addr <<= 32;
    ibiza_pcm_buf_left_phy_addr |= ibiza_pcm_buf_left_phy_addr_L32;

    ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr_U32;
    ibiza_pcm_left_write_phy_addr <<= 32;
    ibiza_pcm_left_write_phy_addr |= ibiza_pcm_left_write_phy_addr_L32;

    ibiza_enable_mark_addr = ibiza_enable_mark_addr_U32;
    ibiza_enable_mark_addr <<= 32;
    ibiza_enable_mark_addr |= ibiza_enable_mark_addr_L32;

    /* Save the Physical addresses which will be used */
    p_ibiza->ibiza_pcm_buf_left_phy_addr = ibiza_pcm_buf_left_phy_addr;
    p_ibiza->ibiza_pcm_buf_size = ibiza_pcm_buf_size;
    p_ibiza->ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr;
    p_ibiza->ibiza_enable_mark_addr = ibiza_enable_mark_addr;

    BTPCM_INFO("Start Stream\n");
    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr=0x%llx\n", (unsigned long long)p_ibiza->ibiza_enable_mark_addr);

    if(p_ibiza->ibiza_pcm_buf_left_phy_addr == 0 ||
    		p_ibiza->ibiza_pcm_left_write_phy_addr == 0 ||
    		p_ibiza->ibiza_enable_mark_addr == 0) {
    	BTPCM_ERR("Address not assigned. Exiting\n");
    	return -ENOMEM;
    }

    /* This will be used in hr timer callback*/
    sema_init(&p_ibiza->ib_thread.threadSync, 1);
    init_waitqueue_head(&p_ibiza->ib_thread.ibiza_wait_q);
    p_ibiza->ib_thread.cbThread = callback_thread;
    p_ibiza->ib_thread.cbThread_id = kthread_create(p_ibiza->ib_thread.cbThread, p_btpcm, "IBIZA_THREAD");
    p_ibiza->ib_thread.condition = IBIZA_READY;

    /* Reserve the Memory Region (both Left and Right PCM Buffers) */
    p_resource = request_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr,
            p_ibiza->ibiza_pcm_buf_size * 2, "BTPCM Buffer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (PCM buffer) is unavailable\n");
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ibiza->p_left_io_pcm = ioremap(p_ibiza->ibiza_pcm_buf_left_phy_addr,
            p_ibiza->ibiza_pcm_buf_size * 2);
    if (!p_ibiza->p_left_io_pcm)
    {
        BTPCM_ERR("ioremap failed\n");
        release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr,
                p_ibiza->ibiza_pcm_buf_size * 2);
        return -ENOMEM;
    }
    BTPCM_INFO("io_left_wrt_ptr:%p", p_ibiza->p_left_io_pcm);

    /* Reserve the Memory Region (Left Write Pointer) */
    p_resource = request_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr,
            sizeof(void *), "BTPCM LeftWritePointer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (LeftWritePtr) is unavailable\n");
        iounmap(p_ibiza->p_left_io_pcm);
        release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr, p_ibiza->ibiza_pcm_buf_size * 2);
        //release_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -EBUSY;
    }

    /* in order to remove PREVENT warning */
    remap_size = sizeof(void *);

    /* Remap the IO Memory */
    p_ibiza->p_left_io_ptr = ioremap(p_ibiza->ibiza_pcm_left_write_phy_addr, remap_size);
    if (!p_ibiza->p_left_io_ptr)
    {
        BTPCM_ERR("ioremap failed\n");
        iounmap(p_ibiza->p_left_io_pcm);
        release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr,
                p_ibiza->ibiza_pcm_buf_size * 2);
        release_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr, remap_size);
        return -ENOMEM;
    }
    BTPCM_INFO("io_left_wrt_ptr:%p", p_ibiza->p_left_io_ptr);

	/* Reserve the Memory Region (Enable Mark Address) */
	p_resource = request_mem_region(p_ibiza->ibiza_enable_mark_addr, sizeof(void *), "BTPCM EnableMardPointer Ibiza");
	if (!p_resource)
	{
		BTPCM_ERR("Resources (EnableMark) is unavailable");
		iounmap(p_ibiza->p_left_io_pcm);
		iounmap(p_ibiza->p_left_io_ptr);
		release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr,
		p_ibiza->ibiza_pcm_buf_size * 2);
		release_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		return -EBUSY;
	}

	/* Remap the IO Memory */
	p_ibiza->p_left_io_enablemark = ioremap(p_ibiza->ibiza_enable_mark_addr, remap_size);
	if (!p_ibiza->p_left_io_enablemark)
	{
		BTPCM_ERR("ioremap failed for enableMark" );
		iounmap(p_ibiza->p_left_io_pcm);
		iounmap(p_ibiza->p_left_io_ptr);
		release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr,
		p_ibiza->ibiza_pcm_buf_size * 2);
		release_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		release_mem_region(p_ibiza->ibiza_enable_mark_addr, sizeof(void *));
		return -ENOMEM;
	}
#endif /* !BTPCM_IBIZA_TONE */

    /* Allocate a PCM buffer able to contain nb_frames samples (stereo,16 bits) */
    /* Add one more frame to be able to absorb overrun (HW goes a bit too fast) */
    p_ibiza->p_buf = kmalloc(nb_pcm_frames * (nb_pcm_packets + 1) * BTPCM_FRAME_SIZE, GFP_KERNEL);
    if (!p_ibiza->p_buf)
    {
        BTPCM_ERR("Unable to allocate buffer (size=%d)\n",
                (int)(nb_pcm_frames * (nb_pcm_packets + 1) * BTPCM_FRAME_SIZE));
        return -ENOMEM;
    }

    /* If synchronization, we don't need timer */
    if (!synchronization)
    {
        if (!p_ibiza->hr_timer)
        {
            p_ibiza->hr_timer = btpcm_hrtimer_alloc(btpcm_ibiza_hrtimer_callback,
                    p_btpcm);
            if (!p_ibiza->hr_timer)
            {
                BTPCM_ERR("No more timer\n");
                return -EINVAL;
            }
        }
    }

    /* High Resolution Timer */
    period_ns = (uint64_t)nb_pcm_frames * (uint64_t)nb_pcm_packets;
    period_ns *= 1000;  /* usec */
    period_ns *= 1000;  /* msec */
    period_ns *= 1000;  /* sec */
    do_div(period_ns, p_btpcm->frequency);

#ifdef BTPCM_IBIZA_DEBUG
    //temp_period_ns = period_ns;
    temp_period_ns = period_ns/2;
    do_div(temp_period_ns, 1000000);/* convert to msec for debug */
    p_ibiza->timer_duration = (int)temp_period_ns;
    BTPCM_INFO("temp_period_ns=%d(ms)\n", p_ibiza->timer_duration);
    p_ibiza->jiffies = jiffies;  /* Save the current timestamp (for debug) */
#endif

    p_ibiza->nb_frames = nb_pcm_frames;
    p_ibiza->nb_packets = nb_pcm_packets;
    //BTPCM_INFO("HR Timer_duration=%llu(ns) nb_frames=%d\n", period_ns, p_ibiza->nb_frames);
    BTPCM_INFO("HR Timer_duration=%llu(ns) nb_frames=%d\n", period_ns/2, p_ibiza->nb_frames);

    /* Mark the stream as started */
    atomic_set(&p_ibiza->started, 1);

    /* Read (and save) the Write Offset */
    /* Read and Write Offsets are at the same location (empty buffer) */
    p_ibiza->left_read_offset = btpcm_ibiza_get_left_write_offset(p_ibiza);

    /* If no synchronization, we need to start a periodic timer */
    if (!synchronization)
    {
        /* Start the HR timer */
        //err = btpcm_hrtimer_start(p_ibiza->hr_timer, period_ns);
        err = btpcm_hrtimer_start(p_ibiza->hr_timer, period_ns/2);
        if (err < 0)
        {
            BTPCM_ERR("Unable to start timer\n");
            kfree(p_ibiza->p_buf);
            p_ibiza->p_buf = NULL;
            atomic_set(&p_ibiza->started, 0);
            return -EINVAL;
        }
    }

    /* Starting the thread now after taking all the resources */
    if( p_ibiza->ib_thread.cbThread_id)
     {
     	wake_up_process( p_ibiza->ib_thread.cbThread_id);
     	BTPCM_INFO("Started PCM thread\n");
     }
     else
     {
     	BTPCM_ERR("Thread creation failed\n");
     	return -EINVAL;
     }
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_stop
 **
 ** Description      BTPCM Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_stop(struct btpcm *p_btpcm)
{
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

	q_info.p_btpcm = p_btpcm;
	INIT_WORK(&q_info.work, btpcm_clear_resource);

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return -EINVAL;
    }

    if (!atomic_read(&p_ibiza->started))
    {
        BTPCM_ERR("Stream not started\n");
        return -EINVAL;
    }

   	BTPCM_ERR("Queue clear resource:0x%p\n", p_btpcm);
   	schedule_work(&q_info.work);

   	return 0;
}

static void btpcm_clear_resource(struct work_struct *work)
{
	struct q_stop_info *info;
	struct btpcm *p_btpcm;
	struct btpcm_ibiza *p_ibiza;
	int err;

	info = container_of(work, struct q_stop_info, work);

	p_btpcm = info->p_btpcm;
	p_ibiza = p_btpcm->private_data;

	if(!p_ibiza)
	{
		BTPCM_ERR("No Ibiza Instance\n");
		return;
	}

	BTPCM_INFO("Going to clear BTPCM Timer\n");

	if (p_ibiza->hr_timer)
	{
		err = btpcm_hrtimer_stop(p_ibiza->hr_timer);
		if (err < 0)
		{
			BTPCM_ERR("Unable to stop timer\n");
			return;
		}
		btpcm_hrtimer_free(p_ibiza->hr_timer);
		p_ibiza->hr_timer = NULL;
	}
	BTPCM_INFO("Timer stopped and freed\n");

	err = down_interruptible(&p_ibiza->ib_thread.threadSync);
    if(err)
    {
    	BTPCM_ERR("Stop:Sem Interrupted!!\n");
    }

    /*Stop the thread*/
	p_ibiza->ib_thread.condition = IBIZA_STOP;
	//if(p_ibiza->ib_thread.cbThread_id)
	//	kthread_stop(p_ibiza->ib_thread.cbThread_id);
    wake_up_interruptible(&p_ibiza->ib_thread.ibiza_wait_q);

    BTPCM_INFO("Thread Woke up:0x%p\n", p_ibiza);

	if(p_ibiza->p_buf)
	{
		kfree(p_ibiza->p_buf);
		p_ibiza->p_buf = NULL;
	}

#ifndef BTPCM_IBIZA_TONE
    /* Unmap the IO Memory (Buffer) */
    if(p_ibiza->p_left_io_pcm)
        iounmap(p_ibiza->p_left_io_pcm);

    /* Dereserve the Memory Region (both Left and Right Buffers) */
    if(p_ibiza->ibiza_pcm_buf_left_phy_addr)
        release_mem_region(p_ibiza->ibiza_pcm_buf_left_phy_addr, p_ibiza->ibiza_pcm_buf_size * 2);

    /* Unmap the IO Memory (Left Write Pointer) */
    if(p_ibiza->p_left_io_ptr)
        iounmap(p_ibiza->p_left_io_ptr);

    /* Dereserve the Memory Region*/
    if(p_ibiza->ibiza_pcm_left_write_phy_addr)
        release_mem_region(p_ibiza->ibiza_pcm_left_write_phy_addr, sizeof(void *));

    /* Unmap the IO Memory (Enable Mark Pointer) */
    if(p_ibiza->p_left_io_enablemark)
        iounmap(p_ibiza->p_left_io_enablemark);

    /* Dereserve the Memory Region*/
    if(p_ibiza->ibiza_enable_mark_addr)
        release_mem_region(p_ibiza->ibiza_enable_mark_addr, sizeof(void *));

	p_ibiza->p_left_io_pcm = NULL;
	p_ibiza->ibiza_pcm_buf_left_phy_addr = 0;
	p_ibiza->p_left_io_ptr = NULL;
	p_ibiza->ibiza_pcm_left_write_phy_addr = 0;
	p_ibiza->p_left_io_enablemark = NULL;
	p_ibiza->ibiza_enable_mark_addr = 0;

    /* Mark the ibiza as stopped */
    atomic_set(&p_ibiza->started, 0);

	up(&p_ibiza->ib_thread.threadSync);

	wake_up_interruptible(&p_ibiza->close_wait_q);

	BTPCM_INFO("BTPCM stopped\n");
#endif

    return;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_sync
 **
 ** Description      BTPCM Synchronization function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_ibiza_sync(struct btpcm *p_btpcm)
{
    BTPCM_ERR("Not Yet Implemented\n");

#if 0
    int delta_error;
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return;
    }

    if (!atomic_read(&p_ibiza->started))
    {
        BTPCM_DBG("Stream stopped\n");
        if (p_ibiza->p_buf)
            kfree(p_ibiza->p_buf);
        p_ibiza->p_buf = NULL;
        return;
    }

    if (!p_ibiza->p_buf)
    {
        BTPCM_ERR("p_buff is NULL\n");
        atomic_set(&p_ibiza->started, 0);
        return;
    }

    if (!p_ibiza->callback)
    {
        BTPCM_ERR("callback is NULL\n");
        atomic_set(&p_ibiza->started, 0);
        if (p_ibiza->p_buf)
            kfree(p_ibiza->p_buf);
        p_ibiza->p_buf = NULL;
        return;
    }

    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_ibiza->jiffies);
    delta_error -= p_ibiza->timer_duration;
    p_ibiza->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 10)
    {
        BTPCM_ERR("delta_error=%d\n", delta_error);
    }

    /* TODO */
    /* Fill the buffer with the requested number of frames */
    btpcm_ibiza_read(p_btpcm,
            (short *)p_ibiza->p_buf,pcm_stream
            p_ibiza->nb_frames * BTPCM_FRAME_SIZE,
            &p_ibiza->sinus_index);

    p_ibiza->callback(p_btpcm, p_ibiza->p_buf, p_ibiza->nb_frames);
#endif
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_hrtimer_callback
 **
 **
 ** Description      BTPCM Tone High Resolution Timer callback.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_ibiza_hrtimer_callback(void *p_opaque)
{
	struct btpcm_ibiza *p_ibiza = NULL;	
    struct btpcm *p_btpcm = p_opaque;
#ifdef BTPCM_IBIZA_DEBUG
    int delta_error;
#endif
	if(!p_btpcm)
	{
		BTPCM_ERR("PCM struct is NULL\n");
        return;
	}
    p_ibiza = p_btpcm->private_data;

    if (!p_ibiza)
    {
        BTPCM_ERR("no IBIZA instance\n");
        return;
    }

    if (!atomic_read(&p_ibiza->started))
    {
        BTPCM_DBG("Stream stopped\n");
        if (p_ibiza->p_buf)
            kfree(p_ibiza->p_buf);
        p_ibiza->p_buf = NULL;
        return;
    }

    if (!p_ibiza->p_buf)
    {
        BTPCM_ERR("p_buff is NULL\n");
        atomic_set(&p_ibiza->started, 0);
        return;
    }

    if (!p_ibiza->callback)
    {
        BTPCM_ERR("callback is NULL\n");
        atomic_set(&p_ibiza->started, 0);
        if (p_ibiza->p_buf)
            kfree(p_ibiza->p_buf);
        p_ibiza->p_buf = NULL;
        return;
    }

#ifdef BTPCM_IBIZA_DEBUG
    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_ibiza->jiffies);
    delta_error -= p_ibiza->timer_duration;
    p_ibiza->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 16)
    {
        BTPCM_ERR("timer expired with delta_error=%d\n", delta_error);
    }
#endif

    p_ibiza->ib_thread.ibiza_count ++;
    if(p_ibiza->ib_thread.ibiza_count > 1200)		// 18ms *1200 ~ 10sec
    {
    	p_ibiza->ib_thread.ibiza_count = 0;
    	BTPCM_ERR("Streaming On..\n");
    }

    p_ibiza->ib_thread.condition = IBIZA_READY;
    //p_ibiza->ib_thread.nb_frames = nb_pcm_frames;
    /* wake up the thread waiting for the data to send*/
    wake_up_interruptible(&p_ibiza->ib_thread.ibiza_wait_q);
    //p_ibiza->callback(p_btpcm, p_ibiza->p_buf, nb_pcm_frames);
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_pcm_available
 **
 ** Description      Get number of PCM Frames available in the HW buffer
 **
 ** Returns          void
 **
 *******************************************************************************/
static int btpcm_ibiza_pcm_available(struct btpcm_ibiza *p_ibiza)
{
	int left_write_offset;
	int pcm_buf_size;

	left_write_offset = btpcm_ibiza_get_left_write_offset(p_ibiza);
	if (left_write_offset < 0)
	{
		return 0;   /* In case of error, indicate that no PCM available */
	}

	//added by Moon //bug fix if left_read_offset == -1
	if (p_ibiza->left_read_offset == -1)
	{
		BTPCM_ERR("p_ibiza->left_read_offset == -1\n");
		p_ibiza->left_read_offset = left_write_offset;
		return 0; //added by Moon
	}

	/* If Read and Write offset at same position => empty buffer */
	if (left_write_offset == p_ibiza->left_read_offset)
	{
		BTPCM_DBG("Empty HW PCM buffer\n");
		return 0;
	}

	if (left_write_offset > p_ibiza->left_read_offset)
	{
		BTPCM_DBG("1 returns=%d\n", (left_write_offset - p_ibiza->left_read_offset) / BTPCM_HW_PCM_SAMPLE_SIZE);
		return ((left_write_offset - p_ibiza->left_read_offset) / BTPCM_HW_PCM_SAMPLE_SIZE);
	}
	else
	{
#ifdef BTPCM_IBIZA_TONE
		pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
#else
		pcm_buf_size = p_ibiza->ibiza_pcm_buf_size;
#endif
		BTPCM_DBG("2 returns=%d\n", (left_write_offset - p_ibiza->left_read_offset + pcm_buf_size) / BTPCM_HW_PCM_SAMPLE_SIZE);
		return ((left_write_offset - p_ibiza->left_read_offset + pcm_buf_size) / BTPCM_HW_PCM_SAMPLE_SIZE);
	}
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_pcm_read
 **
 ** Description      Fill up a PCM buffer with a HW PCM data
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_pcm_read(struct btpcm_ibiza *p_ibiza, void *p_dest_buffer, int nb_frames)
{
    uint32_t *p_dest = (uint32_t *)p_dest_buffer;
    uint8_t *p_left_src = (uint8_t *)(p_ibiza->p_left_io_pcm + p_ibiza->left_read_offset);
    uint32_t temp32;
    int pcm_buf_size;

#ifdef BTPCM_IBIZA_TONE
    pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
#else
    pcm_buf_size = p_ibiza->ibiza_pcm_buf_size;
#endif

    /* Sanity */
    if ((p_ibiza->left_read_offset < 0) ||
        (p_ibiza->left_read_offset >= pcm_buf_size))
    {
        BTPCM_ERR("Bad Left Read Offset=%d\n", p_ibiza->left_read_offset);
        return;
    }

    /* Update Left Read Offset */
    p_ibiza->left_read_offset += nb_frames * BTPCM_HW_PCM_SAMPLE_SIZE;
    if (p_ibiza->left_read_offset >= pcm_buf_size)
    {
        p_ibiza->left_read_offset -= pcm_buf_size;
    }

    while(nb_frames--)
    {
        temp32 = (uint32_t)(IOREAD32(p_left_src));
        p_left_src += BTPCM_HW_PCM_SAMPLE_SIZE;
        *p_dest++ = (uint32_t)temp32;

        if (p_left_src >= (uint8_t *)(p_ibiza->p_left_io_pcm + pcm_buf_size))
        {
            /* Set Left Read pointer to the beginning of the Left Buffer */
            p_left_src = (uint8_t *)p_ibiza->p_left_io_pcm;
        }
    }
}

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_get_left_write_offset
 **
 ** Description     Read the LeftWritePointer and convert it to an offset (from the
 **                 Beginning of the Left PCM buffer)
 **
 ** Returns         offset (-1 if error)
 **
 *******************************************************************************/
static int btpcm_ibiza_get_left_write_offset(struct btpcm_ibiza *p_ibiza)
{
    int left_write_offset;
#ifndef BTPCM_IBIZA_TONE
    void *p_left_write_pointer;
    void *p_enable_mark_pointer;
#endif

    /* Sanity */
    if (!p_ibiza)
    {
        BTPCM_ERR("p_ibiza is NULL\n");
        return -1;
    }

#ifdef BTPCM_IBIZA_TONE
    left_write_offset = btpcm_ibiza_tone_write_offset;

    /* Sanity */
    if ((left_write_offset < 0) ||
        (left_write_offset >= BTPCM_IBIZA_PCM_BUF_SIZE))
    {
        BTPCM_ERR("Wrong pp_left_write_pointer=%d\n", left_write_offset);
        return -1;
    }
#else
    /* Read 32 bits of Left Write Pointer */
    p_left_write_pointer = (void *)(uintptr_t)IOREAD32(p_ibiza->p_left_io_ptr);
    p_enable_mark_pointer =  (void *)(uintptr_t)IOREAD16(p_ibiza->p_left_io_enablemark);

    /* Sanity */
    if ((p_left_write_pointer < (void *)(uintptr_t)p_ibiza->ibiza_pcm_buf_left_phy_addr) ||
        (p_left_write_pointer >= (void *)(uintptr_t)(p_ibiza->ibiza_pcm_buf_left_phy_addr + p_ibiza->ibiza_pcm_buf_size)))
    {
        BTPCM_ERR("Wrong p_left_write_pointer:%p\n", p_left_write_pointer);
        return -1;
    }

    /* Calculate the offset from the beginning of the Left PCM Buffer */
    /* Note that this calculation is done using Physical Addresses */
    left_write_offset = p_left_write_pointer - (void *)(uintptr_t)p_ibiza->ibiza_pcm_buf_left_phy_addr;

    /* Sanity */
    if ((left_write_offset < 0) ||
        (left_write_offset >= p_ibiza->ibiza_pcm_buf_size))
    {
        BTPCM_ERR("Wrong pp_left_write_pointer=%d\n", left_write_offset);
        return -1;
    }

    if(p_ibiza->p_enable_mark_pointer_before != p_enable_mark_pointer)
    {
        BTPCM_INFO("(%p)->(%p)_EmptyBuffer!", p_ibiza->p_enable_mark_pointer_before, p_enable_mark_pointer);
        p_ibiza->left_read_offset = left_write_offset;
    }

    p_ibiza->p_enable_mark_pointer_before = p_enable_mark_pointer;

#endif

    return left_write_offset;
}

#ifdef BTPCM_IBIZA_TONE
/*******************************************************************************
 **
 ** Function         btpcm_ibiza_tone_init
 **
 ** Description      Init Tone
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_tone_init(struct btpcm_ibiza *p_ibiza)
{
    int index;
    int sin_index;

    /* Generate a standard PCM stereo interlaced sinewave */
    for (index = 0, sin_index = 0; index < sizeof(btpcm_ibiza_tone_buf);
            index += BTPCM_HW_PCM_SAMPLE_SIZE, sin_index++)
    {
        if (index < sizeof(btpcm_ibiza_tone_buf) / 2)
        {
            /* Left Channel use a tone */
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 4)
            btpcm_ibiza_tone_buf[index] = 0xAA; /* LSB Unused */
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)btpcm_ibiza_sinwaves[0][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 2] = (uint8_t)(btpcm_ibiza_sinwaves[0][sin_index % 64] >> 8);
            btpcm_ibiza_tone_buf[index + 3] = 0xBB; /* MSB Unused (will be 0 in HW buffer) */
#endif
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 2)
            btpcm_ibiza_tone_buf[index + 0] = (uint8_t)btpcm_ibiza_sinwaves[0][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)(btpcm_ibiza_sinwaves[0][sin_index % 64] >> 8);
#endif
        }
        else
        {
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 4)
            /* Right Channel use another tone */
            btpcm_ibiza_tone_buf[index] = 0xCC; /* LSB Unused */
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)btpcm_ibiza_sinwaves[1][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 2] = (uint8_t)(btpcm_ibiza_sinwaves[1][sin_index % 64] >> 8);
            btpcm_ibiza_tone_buf[index + 3] = 0xDD; /* MSB Unused (will be 0 in HW buffer) */
#endif
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 2)
            btpcm_ibiza_tone_buf[index + 0] = (uint8_t)btpcm_ibiza_sinwaves[1][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)(btpcm_ibiza_sinwaves[1][sin_index % 64] >> 8);
#endif
        }
    }
    /* Set HW Write Pointer to the beginning of the buffer */
    btpcm_ibiza_tone_write_offset = 0;

    p_ibiza->left_read_offset = 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_tone_simulate_write
 **
 ** Description      Simulate data in tone buffer (update write pointer)
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_tone_simulate_write(int nb_pcm_samples)
{
    /* Sanity check (just to check) */
    if ((nb_pcm_samples * BTPCM_HW_PCM_SAMPLE_SIZE) >= BTPCM_IBIZA_PCM_BUF_SIZE)
    {
        BTPCM_ERR("Bad nb_pcm_samples=%d\n", nb_pcm_samples);
    }

    /* The buffer already contains tone. We just need to increment the Write Pointer */
    btpcm_ibiza_tone_write_offset += nb_pcm_samples * BTPCM_HW_PCM_SAMPLE_SIZE;

    /* Handle buffer wrap case */
    if (btpcm_ibiza_tone_write_offset >= BTPCM_IBIZA_PCM_BUF_SIZE)
    {
        btpcm_ibiza_tone_write_offset -= BTPCM_IBIZA_PCM_BUF_SIZE;
    }
}
#endif

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_init
 **
 ** Description      BTPCM Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_init(struct btpcm *p_btpcm, const char *name)
{
    struct btpcm_ibiza *p_ibiza;
    struct proc_dir_entry *entry;

    BTPCM_INFO("The Linux time reference (HZ) is %d\n", HZ);

    p_ibiza = kzalloc(sizeof(*p_ibiza), GFP_KERNEL);
    if (!p_ibiza)
    {
        return -ENOMEM;
    }
    init_waitqueue_head(&p_ibiza->close_wait_q);

    /* Create /proc/btpcmibiza entry */
    entry = proc_create_data(BTPCM_IBIZA_PROC_NAME, S_IRUGO | S_IWUGO, NULL,
            &btpcm_ibiza_file_ops, p_btpcm);
    if (entry)
    {
        BTPCM_INFO("/proc/%s entry created\n", BTPCM_IBIZA_PROC_NAME);
    }
    else
    {
        BTPCM_ERR("Failed to create /proc/%s entry\n", BTPCM_IBIZA_PROC_NAME);
        kfree(p_ibiza);
        return -EINVAL;
    }

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=%u\n", btpcm_ibiza_param_test);
    btpcm_ibiza_tone_init(p_ibiza);
#else
    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%llx\n", (unsigned long long)ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr(Default)=0x%llx\n", (unsigned long long)ibiza_enable_mark_addr);
#endif

    p_btpcm->private_data = p_ibiza;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_exit
 **
 ** Description      BTPCM Exit function.
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_exit(struct btpcm *p_btpcm)
{
    struct btpcm_ibiza *p_ibiza = p_btpcm->private_data;
    int retval;

    BTPCM_DBG("p_btpcm=%p\n", p_btpcm);
    if (!p_ibiza)
    {
    	BTPCM_ERR("no IBIZA instance\n");
    	return;
    }

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=%ul\n", btpcm_ibiza_param_test);
#endif

    remove_proc_entry(BTPCM_IBIZA_PROC_NAME, NULL);
    if(atomic_read(&p_ibiza->started))
    {
     	BTPCM_ERR("Wait for resource to get free\n");
     	retval = wait_event_interruptible_timeout(p_ibiza->close_wait_q, ((atomic_read(&p_ibiza->started)) == 0), (2 * HZ));
     	BTPCM_ERR("Close Wait return: %d\n", retval);
     	/* In cases where wait exits due to certain error, it's better not to free p_ibiza*/
     	/* #TODO: However, this creates memory leak which need to be handled */
     	if(retval < 0)
     		return;
    }


    if (atomic_read(&p_ibiza->started))
    {
        btpcm_ibiza_stop(p_btpcm);
        flush_scheduled_work();

    }

	if(p_ibiza)
	{
	    kfree(p_ibiza);
        p_btpcm->private_data = NULL;
	    p_ibiza = NULL;
	}
}

const struct btpcm_ops btpcm_ibiza_ops =
{
    btpcm_ibiza_init,
    btpcm_ibiza_exit,
    btpcm_ibiza_open,
    btpcm_ibiza_close,
    btpcm_ibiza_config,
    btpcm_ibiza_start,
    btpcm_ibiza_stop,
    btpcm_ibiza_sync,
};

