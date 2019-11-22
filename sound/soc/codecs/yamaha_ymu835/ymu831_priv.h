/*
 * YMU831 ASoC codec driver - private header
 *
 * Copyright (c) 2012-2014 Yamaha Corporation
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

#ifndef YMU831_PRIV_H
#define YMU831_PRIV_H

#include <sound/soc.h>
#include "mcdriver.h"

#undef	MC_ASOC_TEST

/*
 * Virtual registers
 */
enum {
	MC_ASOC_DVOL_MUSICIN,
	MC_ASOC_DVOL_EXTIN,
	MC_ASOC_DVOL_VOICEIN,
	MC_ASOC_DVOL_REFIN,
	MC_ASOC_DVOL_ADIF0IN,
	MC_ASOC_DVOL_ADIF1IN,
	MC_ASOC_DVOL_ADIF2IN,
	MC_ASOC_DVOL_MUSICOUT,
	MC_ASOC_DVOL_EXTOUT,
	MC_ASOC_DVOL_VOICEOUT,
	MC_ASOC_DVOL_REFOUT,
	MC_ASOC_DVOL_DAC0OUT,
	MC_ASOC_DVOL_DAC1OUT,
	MC_ASOC_DVOL_DPATHDA,
	MC_ASOC_DVOL_DPATHAD,
	MC_ASOC_AVOL_LINEIN1,
	MC_ASOC_AVOL_MIC1,
	MC_ASOC_AVOL_MIC2,
	MC_ASOC_AVOL_MIC3,
	MC_ASOC_AVOL_MIC4,
	MC_ASOC_AVOL_HP,
	MC_ASOC_AVOL_SP,
	MC_ASOC_AVOL_RC,
	MC_ASOC_AVOL_LINEOUT1,
	MC_ASOC_AVOL_LINEOUT2,

	MC_ASOC_DVOL_MASTER,
	MC_ASOC_DVOL_VOICE,

	MC_ASOC_DVOL_APLAY_A,
	MC_ASOC_DVOL_APLAY_D,

	MC_ASOC_VOICE_RECORDING,

	MC_ASOC_MASTER_PLAY_VOL_ASGN,
	MC_ASOC_VOICE_PLAY_VOL_ASGN,
	MC_ASOC_VOICE_REC_VOL_ASGN,
	MC_ASOC_INPUT_PLAY_ANA_VOL_ASGN,
	MC_ASOC_INPUT_PLAY_DIG_VOL_ASGN,

	MC_ASOC_AUDIO_MODE_PLAY,
	MC_ASOC_AUDIO_MODE_CAP,
	MC_ASOC_OUTPUT_PATH,
	MC_ASOC_INPUT_PATH,

	MC_ASOC_ADIF0L_SOURCE,
	MC_ASOC_ADIF0R_SOURCE,
	MC_ASOC_ADIF1L_SOURCE,
	MC_ASOC_ADIF1R_SOURCE,
	MC_ASOC_ADIF2L_SOURCE,
	MC_ASOC_ADIF2R_SOURCE,

	MC_ASOC_ADC0L_SOURCE,
	MC_ASOC_ADC0R_SOURCE,
	MC_ASOC_ADC1_SOURCE,

	MC_ASOC_INPUT_PLAYBACK_PATH,

	MC_ASOC_DTMF_CONTROL,
	MC_ASOC_DTMF_OUTPUT,

	MC_ASOC_SWITCH_CLOCK,

	MC_ASOC_EXT_MASTERSLAVE,
	MC_ASOC_EXT_RATE,
	MC_ASOC_EXT_BITCLOCK_RATE,
	MC_ASOC_EXT_INTERFACE,
	MC_ASOC_EXT_BITCLOCK_INVERT,
	MC_ASOC_EXT_INPUT_DA_BIT_WIDTH,
	MC_ASOC_EXT_INPUT_DA_FORMAT,
	MC_ASOC_EXT_INPUT_PCM_MONOSTEREO,
	MC_ASOC_EXT_INPUT_PCM_BIT_ORDER,
	MC_ASOC_EXT_INPUT_PCM_FORMAT,
	MC_ASOC_EXT_INPUT_PCM_BIT_WIDTH,
	MC_ASOC_EXT_OUTPUT_DA_BIT_WIDTH,
	MC_ASOC_EXT_OUTPUT_DA_FORMAT,
	MC_ASOC_EXT_OUTPUT_PCM_MONOSTEREO,
	MC_ASOC_EXT_OUTPUT_PCM_BIT_ORDER,
	MC_ASOC_EXT_OUTPUT_PCM_FORMAT,
	MC_ASOC_EXT_OUTPUT_PCM_BIT_WIDTH,

	MC_ASOC_VOICE_MASTERSLAVE,
	MC_ASOC_VOICE_RATE,
	MC_ASOC_VOICE_BITCLOCK_RATE,
	MC_ASOC_VOICE_INTERFACE,
	MC_ASOC_VOICE_BITCLOCK_INVERT,
	MC_ASOC_VOICE_INPUT_DA_BIT_WIDTH,
	MC_ASOC_VOICE_INPUT_DA_FORMAT,
	MC_ASOC_VOICE_INPUT_PCM_MONOSTEREO,
	MC_ASOC_VOICE_INPUT_PCM_BIT_ORDER,
	MC_ASOC_VOICE_INPUT_PCM_FORMAT,
	MC_ASOC_VOICE_INPUT_PCM_BIT_WIDTH,
	MC_ASOC_VOICE_OUTPUT_DA_BIT_WIDTH,
	MC_ASOC_VOICE_OUTPUT_DA_FORMAT,
	MC_ASOC_VOICE_OUTPUT_PCM_MONOSTEREO,
	MC_ASOC_VOICE_OUTPUT_PCM_BIT_ORDER,
	MC_ASOC_VOICE_OUTPUT_PCM_FORMAT,
	MC_ASOC_VOICE_OUTPUT_PCM_BIT_WIDTH,

	MC_ASOC_MUSIC_PHYSICAL_PORT,
	MC_ASOC_EXT_PHYSICAL_PORT,
	MC_ASOC_VOICE_PHYSICAL_PORT,
	MC_ASOC_HIFI_PHYSICAL_PORT,

	MC_ASOC_ADIF0_SWAP,
	MC_ASOC_ADIF1_SWAP,
	MC_ASOC_ADIF2_SWAP,

	MC_ASOC_DAC0_SWAP,
	MC_ASOC_DAC1_SWAP,

	MC_ASOC_MUSIC_OUT0_SWAP,

	MC_ASOC_MUSIC_IN0_SWAP,
	MC_ASOC_MUSIC_IN1_SWAP,
	MC_ASOC_MUSIC_IN2_SWAP,

	MC_ASOC_EXT_IN_SWAP,

	MC_ASOC_VOICE_IN_SWAP,

	MC_ASOC_MUSIC_OUT1_SWAP,
	MC_ASOC_MUSIC_OUT2_SWAP,

	MC_ASOC_EXT_OUT_SWAP,

	MC_ASOC_VOICE_OUT_SWAP,

	MC_ASOC_DSP_PARAM,

	MC_ASOC_PLAYBACK_SCENARIO,
	MC_ASOC_CAPTURE_SCENARIO,

	MC_ASOC_AUDIO_MODE_PLAY_ADD,
	MC_ASOC_AUDIO_MODE_PLAY_DEL,
	MC_ASOC_AUDIO_MODE_CAP_ADD,
	MC_ASOC_AUDIO_MODE_CAP_DEL,
	MC_ASOC_OUTPUT_PATH_ADD,
	MC_ASOC_OUTPUT_PATH_DEL,

	MC_ASOC_PARAMETER_SETTING,

	MC_ASOC_MAIN_MIC,
	MC_ASOC_SUB_MIC,
	MC_ASOC_THIRD_MIC,
	MC_ASOC_HS_MIC,
	/* karaoke mixer */
	MC_ASOC_K_MSC_VOL,
	MC_ASOC_K_MIC_VOL,
	MC_ASOC_K_ECH_VOL,
	MC_ASOC_K_ECH_FB_GIN,
	MC_ASOC_K_ECH_FB_DLY,
	MC_ASOC_K_ECH_RM_TYP,
	MC_ASOC_K_KEY_CTR,
	MC_ASOC_K_V_CNC,
	MC_ASOC_K_MIC_EQ_0,
	MC_ASOC_K_MIC_EQ_1,
	MC_ASOC_K_MIC_EQ_2,
	MC_ASOC_K_MIC_EQ_3,
	MC_ASOC_K_MIC_EQ_4,
#ifdef MC_ASOC_TEST
	MC_ASOC_MIC1_BIAS,
	MC_ASOC_MIC2_BIAS,
	MC_ASOC_MIC3_BIAS,
	MC_ASOC_MIC4_BIAS,
#endif
	MC_ASOC_N_REG
};
#define MC_ASOC_N_VOL_REG				(MC_ASOC_DVOL_APLAY_D+1)


#define MC_ASOC_AUDIO_MODE_PLAY_OFF			(0)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIO			(1)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIOAE			(2)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIOVBM		(3)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIOVBV		(4)
#define MC_ASOC_AUDIO_MODE_PLAY_INCALL			(5)
#define MC_ASOC_AUDIO_MODE_PLAY_INCALLVB		(6)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIO_INCALL		(7)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIO_INCALLVB		(8)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIOAE_INCALL		(9)
#define MC_ASOC_AUDIO_MODE_PLAY_AUDIOAE_INCALLVB	(10)

#define MC_ASOC_AUDIO_MODE_CAP_OFF			(0)
#define MC_ASOC_AUDIO_MODE_CAP_AUDIO			(1)
#define MC_ASOC_AUDIO_MODE_CAP_AUDIOVBR			(2)
#define MC_ASOC_AUDIO_MODE_CAP_AUDIOVBV			(3)
#define MC_ASOC_AUDIO_MODE_CAP_INCALL			(4)
#define MC_ASOC_AUDIO_MODE_CAP_AUDIO_INCALL		(5)
#define MC_ASOC_AUDIO_MODE_CAP_VBOX		        (6)

#define MC_ASOC_OUTPUT_PATH_SP				(0)
#define MC_ASOC_OUTPUT_PATH_RC				(1)
#define MC_ASOC_OUTPUT_PATH_HP				(2)
#define MC_ASOC_OUTPUT_PATH_HS				(3)
#define MC_ASOC_OUTPUT_PATH_LO1				(4)
#define MC_ASOC_OUTPUT_PATH_LO2				(5)
#define MC_ASOC_OUTPUT_PATH_BT				(6)
#define MC_ASOC_OUTPUT_PATH_SP_RC			(7)
#define MC_ASOC_OUTPUT_PATH_SP_HP			(8)
#define MC_ASOC_OUTPUT_PATH_SP_LO1			(9)
#define MC_ASOC_OUTPUT_PATH_SP_LO2			(10)
#define MC_ASOC_OUTPUT_PATH_SP_BT			(11)
#define MC_ASOC_OUTPUT_PATH_LO1_RC			(12)
#define MC_ASOC_OUTPUT_PATH_LO1_HP			(13)
#define MC_ASOC_OUTPUT_PATH_LO1_BT			(14)
#define MC_ASOC_OUTPUT_PATH_LO2_RC			(15)
#define MC_ASOC_OUTPUT_PATH_LO2_HP			(16)
#define MC_ASOC_OUTPUT_PATH_LO2_BT			(17)
#define MC_ASOC_OUTPUT_PATH_LO1_LO2			(18)
#define MC_ASOC_OUTPUT_PATH_LO2_LO1			(19)

#define MC_ASOC_INPUT_PATH_ADIF0			(0)
#define MC_ASOC_INPUT_PATH_ADIF1			(1)
#define MC_ASOC_INPUT_PATH_ADIF2			(2)
#define MC_ASOC_INPUT_PATH_BT				(3)

#define MC_ASOC_ADC_SOURCE_OFF				(0)
#define MC_ASOC_ADC_SOURCE_MAIN_MIC			(1)
#define MC_ASOC_ADC_SOURCE_SUB_MIC			(2)
#define MC_ASOC_ADC_SOURCE_THIRD_MIC			(3)
#define MC_ASOC_ADC_SOURCE_HS_MIC			(4)
#define MC_ASOC_ADC_SOURCE_LINE1			(5)
#define MC_ASOC_ADC_SOURCE_LINE1M			(6)

#define MC_ASOC_DTMF_OUTPUT_SP				(0)
#define MC_ASOC_DTMF_OUTPUT_NORMAL			(1)

#define MC_ASOC_INPUT_PLAYBACK_PATH_OFF			(0)
#define MC_ASOC_INPUT_PLAYBACK_PATH_ON			(1)
#define MC_ASOC_INPUT_PLAYBACK_PATH_ONVB		(2)

/*
 * Driver private data structure
 */
struct mc_asoc_setup {
	struct MCDRV_INIT_INFO	init;
	struct MCDRV_INIT2_INFO	init2;
	unsigned char	rslot[3];
	unsigned char	tslot[3];
};

struct mc_asoc_port_params {
	UINT8	rate;
	UINT8	bits[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_mono[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_order[SNDRV_PCM_STREAM_LAST+1];
	UINT8	pcm_law[SNDRV_PCM_STREAM_LAST+1];
	UINT8	master;
	UINT8	inv;
	UINT8	srcthru;
	UINT8	format;
	UINT8	bckfs;
	UINT8	channels;
	UINT8	stream;	/* bit0: Playback, bit1: Capture */
};

struct mc_asoc_platform_data {
	void	(*set_ext_micbias)(int en);
	void	(*set_codec_ldod)(int status);
#if (defined(CONFIG_SND_SOC_MSM8960) || defined(CONFIG_SND_SOC_MSM8974))
	void	(*set_codec_ifsel)(int status);
#endif
	int	irq;
};

struct mc_asoc_data {
	struct mutex			mutex;
	struct mc_asoc_setup		setup;
	struct mc_asoc_port_params	port;
	struct MCDRV_CLOCKSW_INFO	clocksw_store;
	struct MCDRV_PATH_INFO		path_store;
	struct MCDRV_VOL_INFO		vol_store;
	struct MCDRV_DIO_INFO		dio_store;
	struct MCDRV_DIOPATH_INFO	diopath_store;
	struct MCDRV_SWAP_INFO		swap_store;
	struct MCDRV_HSDET_INFO		hsdet_store;
	struct MCDRV_HSDET2_INFO	hsdet2_store;
	struct mc_asoc_platform_data	*pdata;
	int	(*penableclkfn)(struct snd_soc_codec *, int, bool);
};

extern void	mc_asoc_write_data(UINT8 bSlaveAdr,
				const UINT8 *pbData,
				UINT32 dSize);
extern void	mc_asoc_read_data(UINT8	bSlaveAdr,
				UINT32 dAddress,
				UINT8 *pbData,
				UINT32 dSize);
extern	void	mc_asoc_set_codec_ldod(int status);
extern	UINT8	mc_asoc_get_bus_select(void);
extern	SINT32	mc_asoc_enable_clock(int enable,
				struct MCDRV_CLOCKSW_INFO *psClockSwInfo);
extern	UINT8	mc_asoc_get_KPrm(UINT8 *KPrm, UINT8 size);


/*
 * For debugging
 */
#ifdef CONFIG_SND_SOC_YAMAHA_YMU831_DEBUG

#define SHOW_REG_ACCESS
#define dbg_info(format, arg...)	snd_printd(KERN_INFO format, ## arg)
#define TRACE_FUNC()	snd_printd(KERN_INFO "<trace> %s()\n", __func__)
#define _McDrv_Ctrl	McDrv_Ctrl_dbg

extern SINT32	McDrv_Ctrl_dbg(
			UINT32 dCmd, void *pvPrm1, void *pvPrm2, UINT32 dPrm);

#else

#define dbg_info(format, arg...)
#define TRACE_FUNC()
#define _McDrv_Ctrl McDrv_Ctrl

#endif /* CONFIG_SND_SOC_YAMAHA_YMU831_DEBUG */

#endif /* YMU831_PRIV_H */
