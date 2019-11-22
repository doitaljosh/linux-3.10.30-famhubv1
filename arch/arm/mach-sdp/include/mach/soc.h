/*
 * SDP cpu type detection
 *
 * Copyright (C) 2013 Samsung Electronics
 *
 * Written by SeungJun Heo <seungjun.heo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef _MACH_SOC_H_
#define _MACH_SOC_H_

enum sdp_board	{
	SDP_BOARD_DEFAULT,
	SDP_BOARD_MAIN,
	SDP_BOARD_JACKPACK,
	SDP_BOARD_LFD,
	SDP_BOARD_SBB,
	SDP_BOARD_HCN,
	SDP_BOARD_VGW,
	SDP_BOARD_FPGA,
	SDP_BOARD_AV,
	SDP_BOARD_MTV,
	SDP_BOARD_MAX
};

extern enum sdp_board get_sdp_board_type(void);

extern int sdp_get_revision_id(void);

#if defined(CONFIG_OF)
unsigned int sdp_rev(void);
unsigned int sdp_soc(void);

enum sdp_chipid {
	NON_CHIPID = 0,
	SDP1202_CHIPID,
	SDP1302_CHIPID,
	SDP1304_CHIPID,
	SDP1307_CHIPID,
	SDP1404_CHIPID,
	SDP1406FHD_CHIPID,
	SDP1406UHD_CHIPID,	
	SDP1412_CHIPID,
};

/* software-defined */
#define IS_SDP_SOC(class, id)			\
static inline int is_##class (void)		\
{					\
	return ((sdp_soc() == (id)) ? 1 : 0);	\
}

IS_SDP_SOC(sdp1202, SDP1202_CHIPID)		//Fox-AP
IS_SDP_SOC(sdp1304, SDP1304_CHIPID)		//Golf-AP
IS_SDP_SOC(sdp1302, SDP1302_CHIPID)		//Golf-S
IS_SDP_SOC(sdp1307, SDP1307_CHIPID)		//Golf-V
IS_SDP_SOC(sdp1404, SDP1404_CHIPID)		//Hawk-P
//IS_SDP_SOC(sdp1406, SDP1406_CHIPID)		//Hawk-M
IS_SDP_SOC(sdp1406fhd, SDP1406FHD_CHIPID)		//Hawk-P
IS_SDP_SOC(sdp1406uhd, SDP1406UHD_CHIPID)		//Hawk-P
IS_SDP_SOC(sdp1412, SDP1412_CHIPID) 	//Hawk-A


#define soc_is_sdp1202()	is_sdp1202()
#define soc_is_sdp1302()	is_sdp1302()
#define soc_is_sdp1304()	is_sdp1304()
#define soc_is_sdp1307()	is_sdp1307()
#define soc_is_sdp1404()	is_sdp1404()
#define soc_is_sdp1406fhd()	is_sdp1406fhd()
#define soc_is_sdp1406uhd()	is_sdp1406uhd()
#define soc_is_sdp1412()	is_sdp1412()

//#define soc_is_sdp1406()	is_sdp1406()
static inline int soc_is_sdp1406(void)
{
	return (((sdp_soc() == (SDP1406FHD_CHIPID)) || (sdp_soc() == (SDP1406UHD_CHIPID))) ? 1 : 0);
}
#define soc_is_sdp1302mpw()	(is_sdp1302() && ((sdp_rev() & 0xf) == 1))

#else

#if defined(CONFIG_ARCH_SDP1202)
#define soc_is_sdp1202()	(1)
#else
#define soc_is_sdp1202()	(0)
#endif

#if defined(CONFIG_ARCH_SDP1207)
#define soc_is_sdp1207()	(1)
#else
#define soc_is_sdp1207()	(0)
#endif

#define soc_is_sdp1302()	(0)
#define soc_is_sdp1304()	(0)
#define soc_is_sdp1307()	(0)

#endif
#endif
