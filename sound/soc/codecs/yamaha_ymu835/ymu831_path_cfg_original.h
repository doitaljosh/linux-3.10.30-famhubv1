/*
 * YMU831 ASoC codec driver
 *
 * Copyright (c) 2012-2014 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.	In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgment in the product documentation would be
 *	appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef YMU831_PATH_CFG_H
#define YMU831_PATH_CFG_H

#include "mcdriver.h"

#define PRESET_PATH_N	(104)
/* ========================================
	Preset Path settings
	========================================*/
static const struct MCDRV_PATH_INFO	stPresetPathInfo[PRESET_PATH_N] = {
	/* 0:playback:off, capture:off */
	{
		{{0x00AAAAAA}, {0x00AAAAAA} } ,	/* asMusicOut	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asExtOut	*/
		{{0x00AAAAAA} } ,		/* asHifiOut	*/
		{{0x00AAAAAA}, {0x00AAAAAA},
		 {0x00AAAAAA}, {0x00AAAAAA} },	/* asVboxMixIn	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAe0	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAe1	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAe2	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAe3	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asDac0	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asDac1	*/
		{{0x00AAAAAA} },		/* asVoiceOut	*/
		{{0x00AAAAAA} },		/* asVboxIoIn	*/
		{{0x00AAAAAA} },		/* asVboxHostIn	*/
		{{0x00AAAAAA} },		/* asHostOut	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAdif0	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAdif1	*/
		{{0x00AAAAAA}, {0x00AAAAAA} },	/* asAdif2	*/
		{{0x002AAAAA}, {0x002AAAAA} },	/* asAdc0	*/
		{{0x002AAAAA} },		/* asAdc1	*/
		{{0x002AAAAA}, {0x002AAAAA} },	/* asSp		*/
		{{0x002AAAAA}, {0x002AAAAA} },	/* asHp		*/
		{{0x002AAAAA} },		/* asRc		*/
		{{0x002AAAAA}, {0x002AAAAA} },	/* asLout1	*/
		{{0x002AAAAA}, {0x002AAAAA} },	/* asLout2	*/
		{{0x002AAAAA}, {0x002AAAAA},
		 {0x002AAAAA}, {0x002AAAAA} }	/* asBias	*/
	},
	/* 1:playback:audio, capture:off (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 2:playback:audio, capture:off (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 3:playback:audio, capture:off (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 4:playback:audioae, capture:off (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF1_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{MCDRV_D2SRC_ADC0_L_ON},
		 {MCDRV_D2SRC_ADC0_R_ON} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{MCDRV_ASRC_LINEIN1_L_ON},
		 {MCDRV_ASRC_LINEIN1_R_ON} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 5:playback:audioae, capture:off (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 6:playback:audioae, capture:off (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 7:playback:audiovbm, capture:off (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 8:playback:audiovbm, capture:off (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 9:playback:audiovbm, capture:off (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 10:playback:audiovbv, capture:off (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 11:playback:audiovbv, capture:off (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 12:playback:audiovbv, capture:off (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 13:playback:off, capture:audio */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 14:playback:audio, capture:audio (analog output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 15:playback:audio, capture:audio (BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 16:playback:audio, capture:audio (analog+BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 17:playback:audioae, capture:audio (analog output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 18:playback:audioae, capture:audio (BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 19:playback:audioae, capture:audio (analog+BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 20:playback:audiovbm, capture:audio (analog output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 21:playback:audiovbm, capture:audio (BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 22:playback:audiovbm, capture:audio (analog+BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 23:playback:audiovbv, capture:audio (analog output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 24:playback:audiovbv, capture:audio (BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 25:playback:audiovbv, capture:audio (analog+BT output) */
	{
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 26:playback:off, capture:audiovbr */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 27:playback:audio, capture:audiovbr (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 28:playback:audio, capture:audiovbr (BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 29:playback:audio, capture:audiovbr (analog+BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 30:playback:audioae, capture:audiovbr (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF1_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{MCDRV_D2SRC_ADC1_ON},
		 {MCDRV_D2SRC_ADC1_ON} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{MCDRV_ASRC_LINEIN1_M_ON} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 31:playback:audioae, capture:audiovbr (BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 32:playback:audioae, capture:audiovbr (analog+BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 33:playback:audiovbm, capture:audiovbr (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 34:playback:audiovbm, capture:audiovbr (BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON
		 |MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON
		 |MCDRV_D1SRC_AE1_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 35:playback:audiovbm, capture:audiovbr (analog+BT output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON
		 |MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON
		 |MCDRV_D1SRC_AE1_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 36:playback:audiovbv, capture:audiovbr (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 37:playback:audiovbv, capture:audiovbr (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 38:playback:audiovbv, capture:audiovbr (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 39:playback:off, capture:audiovbv */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 40:playback:audio, capture:audiovbv (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 41:playback:audio, capture:audiovbv (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 42:playback:audio, capture:audiovbv (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 43:playback:audioae, capture:audiovbv (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF0_ON},
		 {MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF1_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{MCDRV_D2SRC_ADC1_ON},
		 {MCDRV_D2SRC_ADC1_ON} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{MCDRV_ASRC_LINEIN1_M_ON} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 44:playback:audioae, capture:audiovbv (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 45:playback:audioae, capture:audiovbv (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 46:playback:audiovbm, capture:audiovbv (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 47:playback:audiovbm, capture:audiovbv (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 48:playback:audiovbm, capture:audiovbv (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 49:playback:audiovbv, capture:audiovbv (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 50:playback:audiovbv, capture:audiovbv (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 51:playback:audiovbv, capture:audiovbv (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_AE1_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 52:playback:incall, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 53:playback:incall, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 54:playback:incall, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 55:playback:audio+incall, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 56:playback:audio+incall, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 57:playback:audio+incall, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 58:playback:audioae+incall, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 59:playback:audioae+incall, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 60:playback:audioae+incall, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 61:playback:incall, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 62:playback:incall, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 63:playback:incall, capture:audio+incall (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 64:playback:audio+incall, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 65:playback:audio+incall, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 66:playback:audio+incall, capture:audio+incall (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 67:playback:audioae+incall, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 68:playback:audioae+incall, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 69:playback:audioae+incall, capture:audio+incall
							(analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_EXTIN_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 70:playback:incallvb, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 71:playback:incallvb, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 72:playback:incallvb, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 73:playback:audio+incallvb, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 74:playback:audio+incallvb, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 75:playback:audio+incallvb, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 76:playback:audioae+incallvb, capture:incall (analog output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 77:playback:audioae+incallvb, capture:incall (BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 78:playback:audioae+incallvb, capture:incall (analog+BT output) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 79:playback:incallvb, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 80:playback:incallvb, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 81:playback:incallvb, capture:audio+incall (analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 82:playback:audio+incallvb, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 83:playback:audio+incallvb, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 84:playback:audio+incallvb, capture:audio+incall
							(analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 85:playback:audioae+incallvb, capture:audio+incall (analog output) */
	{
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 86:playback:audioae+incallvb, capture:audio+incall (BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 87:playback:audioae+incallvb, capture:audio+incall
							(analog+BT output) */
	{
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_EXTIN_ON} },	/* asMusicOut	*/
		{{MCDRV_D1SRC_AE1_ON},
		 {MCDRV_D1SRC_AE1_ON} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_EXTIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_AE0_ON+MCDRV_D1SRC_VBOXREFOUT_ON} },
						/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 88:playback:audio (HiFi), capture:off */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_HIFIIN_ON},
		 {MCDRV_D1SRC_HIFIIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_HIFIIN_ON},
		 {MCDRV_D1SRC_HIFIIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 89:playback:off, capture:audio (HiFi) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{MCDRV_D1SRC_ADIF0_ON} },	/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 90:playback:audio (HiFi), capture:audio (HiFi) */
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{MCDRV_D1SRC_ADIF0_ON} },	/* asHifiOut	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_HIFIIN_ON},
		 {MCDRV_D1SRC_HIFIIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_HIFIIN_ON},
		 {MCDRV_D1SRC_HIFIIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 91:playback:off, capture:vbox */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {0x00000000},
		 {0x00000000}, },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 92:playback:audio, capture:vbox (analog output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {0x00000000},
		 {0x00000000}, },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{0x00000000} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 93:playback:audio, capture:vbox (BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 94:playback:audio, capture:vbox (analog+BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 95:playback:audioae, capture:vbox (analog output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {0x00000000},
		 {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} },	/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 96:playback:audioae, capture:vbox (BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 97:playback:audioae, capture:vbox (analog+BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 98:playback:audiovbm, capture:vbox (analog output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 99:playback:audiovbm, capture:vbox (BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 100:playback:audiovbm, capture:vbox (analog+BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_MUSICIN_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 101:playback:audiovbv, capture:vbox (analog output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{0x00000000} },		/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 102:playback:audiovbv, capture:vbox (BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{0x00000000}, {0x00000000} },	/* asDac0	*/
		{{0x00000000}, {0x00000000} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{0x00000000}, {0x00000000} },	/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{0x00000000}, {0x00000000} },	/* asLout1	*/
		{{0x00000000}, {0x00000000} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	/* 103:playback:audiovbv, capture:vbox (analog+BT output) */
	{
		{{0x00000000},
		 {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_MUSICIN_ON},
		 {MCDRV_D1SRC_ADIF2_ON} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{MCDRV_D2SRC_VBOXHOSTOUT_ON} }, /* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{0x00000000}, {0x00000000} },	/* asHp		*/
		{{0x00000000} },		/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
};


static const struct MCDRV_PATH_INFO	InputPlaybackPath[]	= {
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON},
		 {MCDRV_D1SRC_ADIF0_ON
		 |MCDRV_D1SRC_ADIF1_ON
		 |MCDRV_D1SRC_ADIF2_ON
		 |MCDRV_D1SRC_VBOXOUT_ON} },	/* asVboxMixIn	*/
		{{MCDRV_D1SRC_VBOXREFOUT_ON},
		 {MCDRV_D1SRC_VBOXREFOUT_ON} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{MCDRV_D2SRC_VOICEIN_ON} },	/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	},
};
static const int	InputPlaybackPathMapping[2][PRESET_PATH_N]	= {
	{
		0,		/* off, off				*/
		0, 0, 0,	/* audio, off				*/
		0, 0, 0,	/* audioae, off				*/
		0, 0, 0,	/* audiovbm, off			*/
		0, 0, 0,	/* audiovbv, off			*/
		0,		/* off, audio				*/
		0, 0, 0,	/* audio, audio				*/
		0, 0, 0,	/* audioae, audio			*/
		0, 0, 0,	/* audiovbm, audio			*/
		0, 0, 0,	/* audiovbv, audio			*/
		0,		/* off, audiovbr			*/
		0, 0, 0,	/* audio, audiovbr			*/
		0, 0, 0,	/* audioae, audiovbr			*/
		0, 0, 0,	/* audiovbm, audiovbr			*/
		0, 0, 0,	/* audiovbv, audiovbr			*/
		0,		/* off, audiovbv			*/
		0, 0, 0,	/* audio, audiovbv			*/
		0, 0, 0,	/* audioae, audiovbv			*/
		0, 0, 0,	/* audiovbm, audiovbv			*/
		0, 0, 0,	/* audiovbv, audiovbv			*/
		0, 0, 0,	/* incall, incall			*/
		0, 0, 0,	/* audio+incall, incall			*/
		0, 0, 0,	/* audioae+incall, incall		*/
		0, 0, 0,	/* incall, audio+incall			*/
		0, 0, 0,	/* audio+incall, audio+incall		*/
		0, 0, 0,	/* audioae+incall, audio+incall		*/
		0, 0, 0,	/* incallvb, incall			*/
		0, 0, 0,	/* audio+incallvb, incall		*/
		0, 0, 0,	/* audioae+incallvb, incall		*/
		0, 0, 0,	/* incallvb, audio+incall		*/
		0, 0, 0,	/* audio+incallvb, audio+incall		*/
		0, 0, 0,	/* audioae+incallvb, audio+incall	*/
		0,		/* audio (HiFi), off			*/
		0,		/* off, audio (HiFi)			*/
		0,		/* audio (HiFi), audio (HiFi)		*/
	},
	{
		1,		/* off, off				*/
		1, 1, 1,	/* audio, off				*/
		1, 1, 1,	/* audioae, off				*/
		1, 1, 1,	/* audiovbm, off			*/
		1, 1, 1,	/* audiovbv, off			*/
		1,		/* off, audio				*/
		1, 1, 1,	/* audio, audio				*/
		1, 1, 1,	/* audioae, audio			*/
		1, 1, 1,	/* audiovbm, audio			*/
		1, 1, 1,	/* audiovbv, audio			*/
		1,		/* off, audiovbr			*/
		1, 1, 1,	/* audio, audiovbr			*/
		1, 1, 1,	/* audioae, audiovbr			*/
		1, 1, 1,	/* audiovbm, audiovbr			*/
		1, 1, 1,	/* audiovbv, audiovbr			*/
		1,		/* off, audiovbv			*/
		1, 1, 1,	/* audio, audiovbv			*/
		1, 1, 1,	/* audioae, audiovbv			*/
		1, 1, 1,	/* audiovbm, audiovbv			*/
		1, 1, 1,	/* audiovbv, audiovbv			*/
		1, 1, 1,	/* incall, incall			*/
		1, 1, 1,	/* audio+incall, incall			*/
		1, 1, 1,	/* audioae+incall, incall		*/
		1, 1, 1,	/* incall, audio+incall			*/
		1, 1, 1,	/* audio+incall, audio+incall		*/
		1, 1, 1,	/* audioae+incall, audio+incall		*/
		1, 1, 1,	/* incallvb, incall			*/
		1, 1, 1,	/* audio+incallvb, incall		*/
		1, 1, 1,	/* audioae+incallvb, incall		*/
		1, 1, 1,	/* incallvb, audio+incall		*/
		1, 1, 1,	/* audio+incallvb, audio+incall		*/
		1, 1, 1,	/* audioae+incallvb, audio+incall	*/
		1,		/* audio (HiFi), off			*/
		1,		/* off, audio (HiFi)			*/
		1,		/* audio (HiFi), audio (HiFi)		*/
	},
};
static const struct MCDRV_PATH_INFO	DtmfPath[]	= {
	{
		{{0x00000000}, {0x00000000} },	/* asMusicOut	*/
		{{0x00000000}, {0x00000000} },	/* asExtOut	*/
		{{0x00000000} },		/* asHifiOut	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON},
		 {0x00000000}, {0x00000000} },	/* asVboxMixIn	*/
		{{0x00000000}, {0x00000000} },	/* asAe0	*/
		{{0x00000000}, {0x00000000} },	/* asAe1	*/
		{{0x00000000}, {0x00000000} },	/* asAe2	*/
		{{0x00000000}, {0x00000000} },	/* asAe3	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac0	*/
		{{MCDRV_D1SRC_AE0_ON},
		 {MCDRV_D1SRC_AE0_ON} },	/* asDac1	*/
		{{MCDRV_D2SRC_VBOXIOOUT_ON} },	/* asVoiceOut	*/
		{{0x00000000} },		/* asVboxIoIn	*/
		{{0x00000000} },		/* asVboxHostIn	*/
		{{0x00000000} },		/* asHostOut	*/
		{{0x00000000}, {0x00000000} },	/* asAdif0	*/
		{{0x00000000}, {0x00000000} },	/* asAdif1	*/
		{{0x00000000}, {0x00000000} },	/* asAdif2	*/
		{{0x00000000}, {0x00000000} },	/* asAdc0	*/
		{{0x00000000} },		/* asAdc1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {0x00000000} },		/* asSp		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asHp		*/
		{{MCDRV_ASRC_DAC0_L_ON} },	/* asRc		*/
		{{MCDRV_ASRC_DAC0_L_ON},
		 {MCDRV_ASRC_DAC0_R_ON} },	/* asLout1	*/
		{{MCDRV_ASRC_DAC1_L_ON},
		 {MCDRV_ASRC_DAC1_R_ON} },	/* asLout2	*/
		{{0x00000000}, {0x00000000},
		 {0x00000000}, {0x00000000} }	/* asBias	*/
	}
};
static const int	DtmfPathMapping[PRESET_PATH_N]	= {
	0,		/* off, off				*/
	0, 0, 0,	/* audio, off				*/
	0, 0, 0,	/* audioae, off				*/
	0, 0, 0,	/* audiovbm, off			*/
	0, 0, 0,	/* audiovbv, off			*/
	0,		/* off, audio				*/
	0, 0, 0,	/* audio, audio				*/
	0, 0, 0,	/* audioae, audio			*/
	0, 0, 0,	/* audiovbm, audio			*/
	0, 0, 0,	/* audiovbv, audio			*/
	0,		/* off, audiovbr			*/
	0, 0, 0,	/* audio, audiovbr			*/
	0, 0, 0,	/* audioae, audiovbr			*/
	0, 0, 0,	/* audiovbm, audiovbr			*/
	0, 0, 0,	/* audiovbv, audiovbr			*/
	0,		/* off, audiovbv			*/
	0, 0, 0,	/* audio, audiovbv			*/
	0, 0, 0,	/* audioae, audiovbv			*/
	0, 0, 0,	/* audiovbm, audiovbv			*/
	0, 0, 0,	/* audiovbv, audiovbv			*/
	0, 0, 0,	/* incall, incall			*/
	0, 0, 0,	/* audio+incall, incall			*/
	0, 0, 0,	/* audioae+incall, incall		*/
	0, 0, 0,	/* incall, audio+incall			*/
	0, 0, 0,	/* audio+incall, audio+incall		*/
	0, 0, 0,	/* audioae+incall, audio+incall		*/
	0, 0, 0,	/* incallvb, incall			*/
	0, 0, 0,	/* audio+incallvb, incall		*/
	0, 0, 0,	/* audioae+incallvb, incall		*/
	0, 0, 0,	/* incallvb, audio+incall		*/
	0, 0, 0,	/* audio+incallvb, audio+incall		*/
	0, 0, 0,	/* audioae+incallvb, audio+incall	*/
	0,		/* audio (HiFi), off			*/
	0,		/* off, audio (HiFi)			*/
	0,		/* audio (HiFi), audio (HiFi)		*/
};

#endif
