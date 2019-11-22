/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * Copyright (C) 2013 Samsung R&D Institute India-Delhi.
 * Author: Dronamraju Santosh Pavan Kumar <dronamraj.k@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * @file snd_amp.h
 * @brief Header file for the ntp7412s2 audio amplifier chip.
 * @author Dronamraju Santosh pavan Kumar <dronamraj.k@samsung.com>
 * @date   2013/11/12
 *
 */

#ifndef _SND_AMP_H
#define _SND_AMP_H

#define NUM_PEQ_DATA 52
#define NUM_PEQ_VOL 2

#define amp_status_true 1
#define amp_status_false 0

#define SNDRV_CTL_IOCTL_AMP_PEQ_TABLE		0
#define SNDRV_CTL_IOCTL_GET_AMP_STATUS		1
#define SNDRV_CTL_IOCTL_GET_PEQ_CHECKSUM	2
#define SNDRV_CTL_IOCTL_GET_LOCAL_PEQ_CHECKSUM	3
/* structure members */
struct amp_eq_vol {
	unsigned char addr;
	unsigned char value;
};

struct amp_biquad_data {
	unsigned char addr;
	unsigned int value;
};

struct amp_peq {
	struct amp_biquad_data peqdata[NUM_PEQ_DATA];
	struct amp_eq_vol peqvol[NUM_PEQ_VOL];
};

enum amp_type_id {
	ID_AMP_MAIN = 0,
	ID_AMP_SUBWOOFER = 1,
};
#endif /*_SND_AMP_H*/

