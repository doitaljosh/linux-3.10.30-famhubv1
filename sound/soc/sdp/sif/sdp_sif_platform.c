

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>   
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include "sdp_sif_platform.h"

#ifdef ALSA_SIF_INTEGRATION

static int sdp_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int retval = 0;

	printk("%s++\n", __func__);

	retval =  snd_pcm_lib_preallocate_pages_for_all(pcm,
				SNDRV_DMA_TYPE_CONTINUOUS,
				snd_dma_continuous_data(GFP_KERNEL),
				1024, 1024);
	if (retval) {
		pr_err("%s:dma buffer allocationf fail\n", __func__);
		return retval;
	}
	return retval;
}


static void sdp_pcm_free(struct snd_pcm *pcm)
{
	printk("%s++\n", __func__);
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int sif_platform_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sdp_runtime_stream *stream;
	int ret_val = 0;
	printk("%s++\n", __func__);
    return 0;
}


static int sif_platform_close(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream;
	int ret_val = 0;
	printk("%s++\n", __func__);
    return 0;
}


static struct snd_pcm_ops sif_platform_ops = {
	.open = sif_platform_open,
	.close = sif_platform_close,
};

static struct snd_soc_platform_driver sif_soc_platform_drv = {
	.ops		= &sif_platform_ops,
	.pcm_new	= sdp_pcm_new,
	.pcm_free	= sdp_pcm_free,
};


/* MFLD - MSIC */
static struct snd_soc_dai_driver sif_platform_dai[] = {

{
	.name = "sif-cpu-dai",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SIF_RATES,
		.formats = SIF_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SIF_RATES,
		.formats = SIF_FORMATS,		
	},

},

};


int sdp_sif_platform_probe(struct platform_device *pdev)
{
	int ret_val = 0;
	printk("%s++\n", __func__);
	ret_val = snd_soc_register_platform(&pdev->dev, &sif_soc_platform_drv);
	if (ret_val) {
		pr_err("registering soc platform failed\n");
		return ret_val;
	}
	printk("%s: snd_soc_register_platform success\n", __func__);
	ret_val = snd_soc_register_dais(&pdev->dev,
					sif_platform_dai, ARRAY_SIZE(sif_platform_dai));
	if (ret_val) {
		pr_err("registering cpu dais failed\n");
		snd_soc_unregister_platform(&pdev->dev);
	}
	printk("%s: snd_soc_register_dais  success\n", __func__);

	return ret_val;

}

int sdp_sif_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(sif_platform_dai));
	snd_soc_unregister_platform(&pdev->dev);

	pr_debug("sdp_audio_platform_remove success\n");

	return 0;
}
#endif

