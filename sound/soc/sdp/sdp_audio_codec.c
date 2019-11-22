#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>   

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "sdp_audio_platform.h"

static inline unsigned int sdp_read(struct snd_soc_codec *codec,
			unsigned int reg)
{
	u8 value = 0;
	int ret;
	ret=0;
//	ret = intel_scu_ipc_ioread8(reg, &value);
	if (ret)
		pr_err("read of %x failed, err %d\n", reg, ret);
	return value;

}

static inline int sdp_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	int ret;
	ret=0;
	if (ret)
		pr_err("write of %x failed, err %d\n", reg, ret);
	return ret;
}


/* speaker and headset mutes, for audio pops and clicks */
static int sdp_pcm_hs_mute(struct snd_soc_dai *dai, int mute)
{

	return 0;
}

#if 1 //ALSA1, ALSA2 support
static int sdp_main_pcm_spkr_mute(struct snd_soc_dai *dai, int mute)
{
	
	return 0;
}

static int sdp_main_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
//		format = BIT(4)|BIT(5);
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		format = 0;
		break;


	case SNDRV_PCM_FORMAT_S8:
		format = 0;
		break;
	
	default:
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM2C2,
//			BIT(4)|BIT(5), format);

	switch (params_rate(params)) {
	case 48000:
		pr_debug("MAIN RATE_48000\n");
		rate = 0;
		break;

	case 44100:
		pr_debug("MAIN RATE_44100\n");
		rate = BIT(7);
		break;

	case 32000:
		pr_debug("MAIN RATE_32000\n");
		rate = BIT(7);
		break;

	case 22050:
		pr_debug("MAIN RATE_22050\n");
		rate = BIT(7);
		break;

	case 16000:
		pr_debug("MAIN RATE_16000\n");
		rate = BIT(7);
		break;

	case 8000:
		pr_debug("MAIN RATE_8000\n");
		rate = BIT(7);
		break;
	

	default:
		pr_debug("MAIN  ERR rate %d\n", params_rate(params));
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM1C1, BIT(7), rate);

	return 0;
}

static int sdp_remote_pcm_spkr_mute(struct snd_soc_dai *dai, int mute)
{
	
	return 0;
}

static int sdp_remote_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
//		format = BIT(4)|BIT(5);
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		format = 0;
		break;


	case SNDRV_PCM_FORMAT_S8:
		format = 0;
		break;
	
	default:
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM2C2,
//			BIT(4)|BIT(5), format);

	switch (params_rate(params)) {
	case 48000:
		pr_debug("REMOTE RATE_48000\n");
		rate = 0;
		break;

	case 44100:
		pr_debug("REMOTE RATE_44100\n");
		rate = BIT(7);
		break;

	case 32000:
		pr_debug("REMOTE RATE_32000\n");
		rate = BIT(7);
		break;

	case 22050:
		pr_debug("REMOTE RATE_22050\n");
		rate = BIT(7);
		break;

	case 16000:
		pr_debug("REMOTE RATE_16000\n");
		rate = BIT(7);
		break;

	case 8000:
		pr_debug("REMOTE RATE_8000\n");
		rate = BIT(7);
		break;
	

	default:
		pr_debug("REMOTE ERR rate %d\n", params_rate(params));
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM1C1, BIT(7), rate);

	return 0;
}


static int sdp_pp_pcm_spkr_mute(struct snd_soc_dai *dai, int mute)
{
	
	return 0;
}

static int sdp_pp_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
//		format = BIT(4)|BIT(5);
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		format = 0;
		break;


	case SNDRV_PCM_FORMAT_S8:
		format = 0;
		break;
	
	default:
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM2C2,
//			BIT(4)|BIT(5), format);

	switch (params_rate(params)) {
	case 48000:
		pr_debug("REMOTE RATE_48000\n");
		rate = 0;
		break;

	case 44100:
		pr_debug("REMOTE RATE_44100\n");
		rate = BIT(7);
		break;

	case 32000:
		pr_debug("REMOTE RATE_32000\n");
		rate = BIT(7);
		break;

	case 22050:
		pr_debug("REMOTE RATE_22050\n");
		rate = BIT(7);
		break;

	case 16000:
		pr_debug("REMOTE RATE_16000\n");
		rate = BIT(7);
		break;

	case 8000:
		pr_debug("REMOTE RATE_8000\n");
		rate = BIT(7);
		break;
	

	default:
		pr_debug("REMOTE ERR rate %d\n", params_rate(params));
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM1C1, BIT(7), rate);

	return 0;
}



#endif 


static int sdp_pcm_spkr_mute(struct snd_soc_dai *dai, int mute)
{
	
	return 0;
}

static int sdp_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
//		format = BIT(4)|BIT(5);
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		format = 0;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		format = 0;
		break;


	case SNDRV_PCM_FORMAT_S8:
		format = 0;
		break;
	
	default:
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM2C2,
//			BIT(4)|BIT(5), format);

	switch (params_rate(params)) {
	case 48000:
		pr_debug("RATE_48000\n");
		rate = 0;
		break;

	case 44100:
		pr_debug("RATE_44100\n");
		rate = BIT(7);
		break;

	case 32000:
		pr_debug("RATE_32000\n");
		rate = BIT(7);
		break;

	case 22050:
		pr_debug("RATE_22050\n");
		rate = BIT(7);
		break;

	case 16000:
		pr_debug("RATE_16000\n");
		rate = BIT(7);
		break;

	case 8000:
		pr_debug("RATE_8000\n");
		rate = BIT(7);
		break;
	

	default:
		pr_debug("ERR rate %d\n", params_rate(params));
		return -EINVAL;
	}
//	snd_soc_update_bits(dai->codec, SDP_PCM1C1, BIT(7), rate);

	return 0;
}

/* Codec DAI section */
static const struct snd_soc_dai_ops sdp_headset_dai_ops = {
	.digital_mute	= sdp_pcm_hs_mute,
	.hw_params	= sdp_pcm_hw_params,
};

static const struct snd_soc_dai_ops sdp_speaker_dai_ops = {
	.digital_mute	= sdp_pcm_spkr_mute,
	.hw_params	= sdp_pcm_hw_params,
};
#if 1 //ALSA1, ALSA2 support
static const struct snd_soc_dai_ops sdp_main_speaker_dai_ops = {
	.digital_mute	= sdp_main_pcm_spkr_mute,
	.hw_params	= sdp_main_pcm_hw_params,
};
static const struct snd_soc_dai_ops sdp_remote_speaker_dai_ops = {
	.digital_mute	= sdp_remote_pcm_spkr_mute,
	.hw_params	= sdp_remote_pcm_hw_params,
};

//PP ALSA
#if 1
static const struct snd_soc_dai_ops sdp_pp_speaker_dai_ops = {
	.digital_mute	= sdp_pp_pcm_spkr_mute,
	.hw_params	= sdp_pp_pcm_hw_params,
};
#endif 
#endif 

static const struct snd_soc_dai_ops sdp_vib1_dai_ops = {
	.hw_params	= sdp_pcm_hw_params,
};

static const struct snd_soc_dai_ops sdp_vib2_dai_ops = {
	.hw_params	= sdp_pcm_hw_params,
};

static struct snd_soc_dai_driver sdp_dais[] = {
#if 0
{	
	.name = "SDP Codec Headset",
	.playback = {
		.stream_name = "Headset",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 5,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	.ops = &sdp_headset_dai_ops,
},
#endif 

{	.name = "SDP Codec Speaker",
	.playback = {
		.stream_name = "Speaker",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
#if 1//Arangam	
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
#endif	
	.ops = &sdp_speaker_dai_ops,

},


#if 1 //ALSA1,ALSA2 support
{	.name = "Main Codec Speaker",
	.playback = {
		.stream_name = "Main Spk",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	
	.ops = &sdp_main_speaker_dai_ops,

},

{	.name = "Remote Codec Speaker",
	.playback = {
		.stream_name = "Remote Spk",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	
	.ops = &sdp_remote_speaker_dai_ops,

},

//PP ALSA
#if 1
{	.name = "PP Codec Speaker",
	.playback = {
		.stream_name = "PP Spk",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	
	.ops = &sdp_pp_speaker_dai_ops,
},
#endif 


#endif





#if 0
{	.name = "SDP Codec Vibra1",
	.playback = {
		.stream_name = "Vibra1",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	.ops = &sdp_vib1_dai_ops,
},
{	.name = "SDP Codec Vibra2",
	.playback = {
		.stream_name = "Vibra2",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SDP_RATES,
		.formats = SDP_FORMATS,
	},
	.ops = &sdp_vib2_dai_ops,
},
#endif 

};


/* codec registration */
static int sdp_codec_probe(struct snd_soc_codec *codec)
{
	//pr_debug("codec_probe called\n");
#if 0
	/* PCM interface config
	 * This sets the pcm rx slot conguration to max 6 slots
	 * for max 4 dais (2 stereo and 2 mono)
	 */
	snd_soc_write(codec, SN95031_PCM2RXSLOT01, 0x10);
	snd_soc_write(codec, SN95031_PCM2RXSLOT23, 0x32);
	snd_soc_write(codec, SN95031_PCM2RXSLOT45, 0x54);
	snd_soc_write(codec, SN95031_PCM2TXSLOT01, 0x10);
	snd_soc_write(codec, SN95031_PCM2TXSLOT23, 0x32);
	/* pcm port setting
	 * This sets the pcm port to slave and clock at 19.2Mhz which
	 * can support 6slots, sampling rate set per stream in hw-params
	 */
	snd_soc_write(codec, SN95031_PCM1C1, 0x00);
	snd_soc_write(codec, SN95031_PCM2C1, 0x01);
	snd_soc_write(codec, SN95031_PCM2C2, 0x0A);
	snd_soc_write(codec, SN95031_HSMIXER, BIT(0)|BIT(4));
	/* vendor vibra workround, the vibras are muted by
	 * custom register so unmute them
	 */
	snd_soc_write(codec, SN95031_SSR5, 0x80);
	snd_soc_write(codec, SN95031_SSR6, 0x80);
	snd_soc_write(codec, SN95031_VIB1C5, 0x00);
	snd_soc_write(codec, SN95031_VIB2C5, 0x00);
	/* configure vibras for pcm port */
	snd_soc_write(codec, SN95031_VIB1C3, 0x00);
	snd_soc_write(codec, SN95031_VIB2C3, 0x00);

	/* soft mute ramp time */
	snd_soc_write(codec, SN95031_SOFTMUTE, 0x3);
	/* fix the initial volume at 1dB,
	 * default in +9dB,
	 * 1dB give optimal swing on DAC, amps
	 */
	snd_soc_write(codec, SN95031_HSLVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_HSRVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_IHFLVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_IHFRVOLCTRL, 0x08);
	/* dac mode and lineout workaround */
	snd_soc_write(codec, SN95031_SSR2, 0x10);
	snd_soc_write(codec, SN95031_SSR3, 0x40);
#endif 
	return 0;
}

static int sdp_codec_remove(struct snd_soc_codec *codec)
{
	pr_debug("codec_remove called\n");
	return 0;
}


struct snd_soc_codec_driver sdp_codec = {
	.probe		= sdp_codec_probe,
	.remove		= sdp_codec_remove,
	.read		= sdp_read,
	.write		= sdp_write,
//	.set_bias_level	= sdp_set_vaud_bias,
//	.idle_bias_off	= TRUE,
//	.dapm_widgets	= sdp_dapm_widgets,
//	.num_dapm_widgets	= ARRAY_SIZE(sdp_dapm_widgets),
//	.dapm_routes	= sdp_audio_map,
//	.num_dapm_routes	= ARRAY_SIZE(sdp_audio_map),
};

int sdp_codec_device_probe(struct platform_device *pdev)
{
	pr_debug("codec device probe called for %s\n", dev_name(&pdev->dev));
	return snd_soc_register_codec(&pdev->dev, &sdp_codec,
			sdp_dais, ARRAY_SIZE(sdp_dais));
}

int sdp_codec_device_remove(struct platform_device *pdev)
{
	pr_debug("codec device remove called\n");
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}



MODULE_DESCRIPTION("ASoC SPD codec driver");
MODULE_AUTHOR("Kyung <kyonggo@samsung.com>");
MODULE_LICENSE("GPL v2");
