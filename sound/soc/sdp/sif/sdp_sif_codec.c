

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

#include "sdp_sif_platform.h"

#ifdef ALSA_SIF_INTEGRATION

static int sdp_pcm_sif_mute(struct snd_soc_dai *dai, int mute)
{	
	
	printk("%s++\n", __func__);
	return 0;
}

static int sdp_pcm_sif_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;
	
	printk("%s++\n", __func__);
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

static inline unsigned int sdp_sif_read(struct snd_soc_codec *codec,
			unsigned int reg)
{
	u8 value = 0;
	int ret = 0;
	printk("%s++\n", __func__);
	if (ret)
		pr_err("read of %x failed, err %d\n", reg, ret);
	return value;

}

static inline int sdp_sif_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	int ret = 0;
	
	printk("%s++\n", __func__);
	if (ret)
		pr_err("write of %x failed, err %d\n", reg, ret);
	return ret;
}


static const struct snd_soc_dai_ops sdp_sif_dai_ops = {
	.digital_mute	= sdp_pcm_sif_mute,
	.hw_params	= sdp_pcm_sif_hw_params,
};

static struct snd_soc_dai_driver sdp_sif_dais[] = {

{
	.name = "SIF Codec",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SIF_RATES,
		.formats = SIF_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SIF_RATES,
		.formats = SIF_FORMATS,
	},
	.ops = &sdp_sif_dai_ops,

},

};


/* codec registration */
static int sdp_sif_codec_probe(struct snd_soc_codec *codec)
{
	printk("%s++\n", __func__);
	return 0;
}

static int sdp_sif_codec_remove(struct snd_soc_codec *codec)
{
	
	printk("%s++\n", __func__);
	return 0;
}


struct snd_soc_codec_driver sdp_sif_codec = {
	.probe		= sdp_sif_codec_probe,
	.remove		= sdp_sif_codec_remove,
	.read		= sdp_sif_read,
	.write		= sdp_sif_write,
//	.set_bias_level	= sdp_set_vaud_bias,
//	.idle_bias_off	= TRUE,
//	.dapm_widgets	= sdp_dapm_widgets,
//	.num_dapm_widgets	= ARRAY_SIZE(sdp_dapm_widgets),
//	.dapm_routes	= sdp_audio_map,
//	.num_dapm_routes	= ARRAY_SIZE(sdp_audio_map),
};

int sdp_codec_sif_device_probe(struct platform_device *pdev)
{
	printk("%s++: dev_name[%s]\n", __func__, dev_name(&pdev->dev));
	return snd_soc_register_codec(&pdev->dev, &sdp_sif_codec,
			sdp_sif_dais, ARRAY_SIZE(sdp_sif_dais));
}

int sdp_codec_sif_device_remove(struct platform_device *pdev)
{
	printk("%s++\n", __func__);
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}



MODULE_DESCRIPTION("ASoC SPD codec driver");
MODULE_AUTHOR("Kyung <kyonggo@samsung.com>");
MODULE_LICENSE("GPL v2");
#endif

