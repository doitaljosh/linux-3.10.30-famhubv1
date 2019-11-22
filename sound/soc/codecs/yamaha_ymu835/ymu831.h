/*
 * YMU831 ASoC codec driver
 *
 * Copyright (c) 2012 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef YMU831_H
#define YMU831_H

#include <sound/pcm.h>
#include <sound/soc.h>
#include "mcdriver.h"

#define MC_ASOC_NAME			"ymu831"
/*
 * dai: set_clkdiv
 */
/* div_id */
#define MC_ASOC_BCLK_MULT		5

/* div for MC_ASOC_BCLK_MULT */
#define MC_ASOC_LRCK_X64		(0)
#define MC_ASOC_LRCK_X48		(1)
#define MC_ASOC_LRCK_X32		(2)
#define MC_ASOC_LRCK_X512		(3)
#define MC_ASOC_LRCK_X256		(4)
#define MC_ASOC_LRCK_X192		(5)
#define MC_ASOC_LRCK_X128		(6)
#define MC_ASOC_LRCK_X96		(7)
#define MC_ASOC_LRCK_X24		(8)
#define MC_ASOC_LRCK_X16		(9)

/*
 * hwdep: ioctl
 */
#define MC_ASOC_MAGIC			'N'
#define MC_ASOC_IOCTL_SET_CTRL		(1)
#define MC_ASOC_IOCTL_READ_REG		(2)
#define MC_ASOC_IOCTL_WRITE_REG		(3)
#define MC_ASOC_IOCTL_NOTIFY_HOLD	(4)
#define MC_ASOC_IOCTL_GET_DSP_DATA	(5)
#define MC_ASOC_IOCTL_SET_DSP_DATA	(6)
#ifdef VOICE_TRIGGER
#define MC_ASOC_IOCTL_SET_CTRL_VT   (7)
#endif

struct ymc_ctrl_args {
	void		*param;
	unsigned long	size;
	unsigned long	option;
};

struct ymc_dspdata_args {
	unsigned char	*buf;
	unsigned long	bufsize;
	unsigned long	size;
};

#define YMC_IOCTL_SET_CTRL \
	_IOW(MC_ASOC_MAGIC, MC_ASOC_IOCTL_SET_CTRL, struct ymc_ctrl_args)

#define YMC_IOCTL_READ_REG \
	_IOWR(MC_ASOC_MAGIC, MC_ASOC_IOCTL_READ_REG, struct MCDRV_REG_INFO)

#define YMC_IOCTL_WRITE_REG \
	_IOWR(MC_ASOC_MAGIC, MC_ASOC_IOCTL_WRITE_REG, struct MCDRV_REG_INFO)

#define YMC_IOCTL_NOTIFY_HOLD \
	_IOWR(MC_ASOC_MAGIC, MC_ASOC_IOCTL_NOTIFY_HOLD, unsigned long)
#define YMC_IOCTL_GET_DSP_DATA \
	_IOWR(MC_ASOC_MAGIC, MC_ASOC_IOCTL_GET_DSP_DATA, \
		struct ymc_dspdata_args)
#define YMC_IOCTL_SET_DSP_DATA \
	_IOWR(MC_ASOC_MAGIC, MC_ASOC_IOCTL_SET_DSP_DATA, \
		struct ymc_dspdata_args)

#ifdef VOICE_TRIGGER
#define YMC_IOCTL_SET_CTRL_VT \
	_IOW(MC_ASOC_MAGIC, MC_ASOC_IOCTL_SET_CTRL_VT, struct ymc_ctrl_args)
#endif

#define YMC_NOTITY_HOLD_OFF		(0)
#define YMC_NOTITY_HOLD_ON		(1)

void	mc_asoc_set_enable_clock_func(
		int (*penableclkfn)(struct snd_soc_codec *, int, bool));

#endif
