
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>   
#include <linux/module.h>
#include <linux/t2d_print.h>
#include <t2ddebugd/t2ddebugd.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <uapi/linux/dvb/audio.h>
//#include "lldAioIsr.h"
//#include "lldAioI2s.h"
//#include "sdp_audio.h"
#include "sdp_sif.h"
#include "sdp_sif_common.h"

#include "sdp_sif_platform.h"
#include "sdp_audio_sys.h"
#include "spISifType.h"
#include "devSif.h"
#ifdef CONFIG_T2D_DEBUGD
#include <linux/t2d_print.h>
#include <t2ddebugd/t2ddebugd.h>
#include "sdp_sif_t2d.h"
#endif // CONFIG_T2D_DEBUGD

#ifdef ALSA_SIF_INTEGRATION

static int t2ddbg_alsa_sif_flag = 1;

extern int __ref sdp_sif_init(struct sdp_sif_dev *pdev);


/*** carrier mute ***/
devSifHndl_t hSif;

static int sif_carrier_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int sif_carrier_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Enable_e MuteOnOff;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetFMCarrierMuteEn(hSif, &MuteOnOff);
	ucontrol->value.integer.value[0] = MuteOnOff;
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, MuteOnOff, current->pid, current->comm);

	return 0;
}

static int sif_carrier_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Enable_e MuteOnOff;

	if (ucontrol->value.integer.value[0] >= SIF_EN_MAX)
	{
		pr_err("[%s] Error: out of range.\n", __FUNCTION__);
		return -1;
	}

	devSif_Open(SIF_CHIP_0, &hSif);
	MuteOnOff = ucontrol->value.integer.value[0];
	devSif_SetFMCarrierMuteEn(hSif, MuteOnOff);
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, MuteOnOff, current->pid, current->comm);

	return 0;
}

/****high deviation *******/
static int sif_high_deviation_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}
 
static int sif_high_deviation_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Enable_e eDeviation;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetHiDeviation(hSif, &eDeviation);
	ucontrol->value.integer.value[0] = eDeviation ;
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, eDeviation, current->pid, current->comm);

	return 0;
}

static int sif_high_deviation_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Deviation_e eDeviation;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	if (ucontrol->value.integer.value[0] >= SIF_DEV_MAX)
	{
		pr_err("[%s] Error: out of range.\n", __FUNCTION__);
		return -1;
	}

	devSif_Open(SIF_CHIP_0, &hSif);
	eDeviation = (ucontrol->value.integer.value[0]) ? SIF_DEV_400 : SIF_DEV_200;
	devSif_SetHiDeviation(hSif, eDeviation);
	devSif_Close(hSif);

	return 0;
}

/****pilot value******/
static int sif_pilot_highlow_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xFFFF;
	return 0;
}

static int sif_pilot_highlow_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Thlds_s sSifThld;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetThld(hSif, &sSifThld);
	ucontrol->value.integer.value[0] = sSifThld.usPilotLowThld;
	ucontrol->value.integer.value[1] = sSifThld.usPilotHighThld;
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: low %d,high %d [pid:%d,%s]\n", __FUNCTION__, sSifThld.usPilotLowThld, sSifThld.usPilotHighThld, current->pid, current->comm);

	return 0;
}

static int sif_pilot_highlow_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Thlds_s sSifThld;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetThld(hSif, &sSifThld);
	sSifThld.usPilotLowThld = ucontrol->value.integer.value[0];
	sSifThld.usPilotHighThld = ucontrol->value.integer.value[1];
	devSif_SetThld(hSif, &sSifThld);
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: low %d,high %d [pid:%d,%s]\n", __FUNCTION__, sSifThld.usPilotLowThld, sSifThld.usPilotHighThld, current->pid, current->comm);

	return 0;
}

/****mts mode******/
static int sif_mts_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[16+6+1] = {
		// the order should match with Sif_Standard_e
		"STD_BTSC"      ,       // 0
		"STD_A2K"       ,       // 1
		"STD_EIAJ"      ,       // 2
		"STD_NICAM_I"   ,       // 3
		"STD_NICAM_DK"  ,       // 4
		"STD_NICAM_BG"  ,       // 5
		"STD_NICAM_L"   ,       // 6
		"STD_A2_DK1"    ,       // 7
		"STD_A2_DK2"    ,       // 8
		"STD_A2_DK3"    ,       // 9
		"STD_A2_BG"     ,       // 10
		"STD_I_MONO"    ,       // 11
		"STD_L_MONO"    ,       // 12
		"STD_ASD"       ,       // 13
		"STD_PALN"      ,       // 14
		"STD_NONE"      ,       // 15
		// the order should match with Sif_MtsMode_e
		"MTS_MONO"      ,       // 16 + 0
		"MTS_STEREO"    ,       // 16 + 1
		"MTS_FOREIGN"   ,       // 16 + 2
		"MTS_STEREO_DUALMONO",  // 16 + 3
		"MTS_MONO_NICAM",       // 16 + 4
		"MTS_DUALMONO"  ,       // 16 + 5
		"MTS_NONE"              // 16 + 6
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 16+6+1;
	if (uinfo->value.enumerated.item >= (16+6))
		uinfo->value.enumerated.item = 16+6;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

	return 0;
}

static int sif_mts_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Config_s sSifConfig;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetMtsMode(hSif, &sSifConfig);
	devSif_Close(hSif);

	ucontrol->value.enumerated.item[0] = sSifConfig.eStandard;
	ucontrol->value.enumerated.item[1] = sSifConfig.eMtsMode + SIF_STD_MAX + 1;

	return 0;
}
 
/****mts out mode******/
static int g_mts_out_mode = SD_MTS_MAX;
static int sif_mts_out_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[10] = {
		// the order should match with SdAnalogMtsMode_k
		"MONO",          ///< Set Multi Sound Mode to MONO.
		"MONO_NICAM",    ///< Set Multi Sound Mode to NICAM MONO.
		"STEREO",        ///< Set Multi Sound Mode to STEREO.
		"SAP",           ///< Set Multi Sound Mode to SAP.
		"STEREO_SAP",    ///< Set Multi Sound Mode to STEREO SAP.
		"LANGUAGE1",     ///< Set Multi Sound Mode to LANGUAGE1.
		"LANGUAGE2",     ///< Set Multi Sound Mode to LANGUAGE2.
		"DUAL1",
		"SD_MTS_DUAL2",
		"NONE"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 10;
	if (uinfo->value.enumerated.item >= 9)
		uinfo->value.enumerated.item = 9;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

	return 0;
}

static int sif_mts_out_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_mts_out_mode;

	return 0;
}

static int sif_mts_out_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	SdAnalogMtsMode_k eSdMtsMode;

	devSif_Open(SIF_CHIP_0, &hSif);
	eSdMtsMode = ucontrol->value.enumerated.item[0];
	devSif_SdSetMtsOutMode(hSif, eSdMtsMode);
	devSif_Close(hSif);

	g_mts_out_mode = eSdMtsMode;

	return 0;
}

 
/* SIF MODULE INIT/DEINIT*/


static int sif_start_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sif_start_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}


static int sif_start_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	printk("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);
	sdp_sif_start(sif_mc_priv->sif_dev);
	return 0;
}

static int sif_stop_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}


static int sif_stop_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	
	return 0;
}

static int sif_stop_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	printk("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);
	sdp_sif_stop(sif_mc_priv->sif_dev);
	return 0;
}



static int sif_vidioc_cap_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sif_vidioc_cap_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	ucontrol->value.enumerated.item[0] = sdp_sif_query_cap(sif_mc_priv->sif_dev);
	return 0;
}
static int sif_vidioc_cap_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{

	return 0;
}



static int sif_manual_debug_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sif_manual_debug_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{

	return 0;
}

static int sif_manual_debug_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	printk("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);
	sdp_sif_manual_debug(sif_mc_priv->sif_dev);

	return 0;
}



static int sif_asd_init_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sif_asd_init_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int sif_asd_init_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	printk("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);

	sdp_sif_asd_init(sif_mc_priv->sif_dev);
	return 0;
}




static int sif_afe_init_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int sif_afe_init_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{

	return 0;
}

static int sif_afe_init_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct sif_mc_private *sif_mc_priv = kcontrol->private_data;
	printk("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);
	sdp_sif_afe_init(sif_mc_priv->sif_dev);
	return 0;
}

static struct snd_kcontrol_new sif_init_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "start",
		.info = sif_start_info,
		.get = sif_start_get,
		.put = sif_start_put
		//.private_value = (unsigned long)sif_mc_priv
	},

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "stop",
		.info = sif_stop_info,
		.get = sif_stop_put,
		.put = sif_stop_put,
		//.private_value = (unsigned long)sif_mc_priv
	},

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "query cap",
		.info = sif_vidioc_cap_info,
		.get = sif_vidioc_cap_get,
		.put = sif_vidioc_cap_put,
		//.private_value = (unsigned long)sif_mc_priv
	},

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "manual debug",
		.info = sif_manual_debug_info,
		.get = sif_manual_debug_get,
		.put = sif_manual_debug_put,
		//.private_value = (unsigned long)sif_mc_priv 
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "asd init",
		.info = sif_asd_init_info,
		.get = sif_asd_init_get,
		.put = sif_asd_init_put,
		//.private_value = (unsigned long)sif_mc_priv 
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "afe init",
		.info = sif_afe_init_info,
		.get = sif_afe_init_get,
		.put = sif_afe_init_put,
		//.private_value = (unsigned long)sif_mc_priv
	}
};


/*********END**************/




static struct snd_kcontrol_new sif_controls[] = {
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "carrier mute",
 .info = sif_carrier_mute_info,
 .get = sif_carrier_mute_get,
 .put = sif_carrier_mute_put
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "high deviation",
 .info = sif_high_deviation_info,
 .get = sif_high_deviation_get,
 .put = sif_high_deviation_put
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "pilot highlow",
 .info = sif_pilot_highlow_info,
 .get = sif_pilot_highlow_get,
 .put = sif_pilot_highlow_put	
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "sif info analog mts",
 .info = sif_mts_mode_info,
 .get = sif_mts_mode_get,
 .put = NULL // Operation not permitted
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "sif analog mts out mode",
 .info = sif_mts_out_mode_info,
 .get = sif_mts_out_mode_get,
 .put = sif_mts_out_mode_put
},

};


static int sdp_sif_mc_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_ctl_elem_value value;	

	/* sif controls add */
	snd_soc_add_card_controls(card, sif_controls, ARRAY_SIZE(sif_controls));
	/*SIF Init contro*/
	snd_soc_add_card_controls(card, sif_init_controls, ARRAY_SIZE(sif_init_controls));

	return 0;
}


static struct snd_soc_dai_link sdp_sif_dai_link[] = {
{
	.name = "SIF",
	.stream_name = "sif control",
	.cpu_dai_name = "sif-cpu-dai",
	.codec_dai_name = "SIF Codec",
	.codec_name = "sdp-sif-codec.8",
	.platform_name = "sdp-sif-platform.9",
	.init = sdp_sif_mc_init,
},

};

/* SoC card */
static struct snd_soc_card snd_soc_sif_card = {
	.name = "sif_scard_audio",
	.owner = THIS_MODULE,
	.dai_link = sdp_sif_dai_link,
	.num_links = ARRAY_SIZE(sdp_sif_dai_link),
	.card_num = 2;
};


#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_alsa_sif_debug(void)
{
	long event;
	const int ID_MAX = 99;
	PRINT_T2D("[%s]\n", __func__);

	while (1) {
		PRINT_T2D("\n");
		PRINT_T2D(" 0 ) toggle ALSA SoC audio debug message [current: %d]\n", \
			t2ddbg_alsa_sif_flag);
		PRINT_T2D("=====================================\n");
		PRINT_T2D("%d ) exit\n", ID_MAX);

		PRINT_T2D(" => ");
		event = t2d_dbg_get_event_as_numeric(NULL, NULL);
		PRINT_T2D("\n");

		if (event >= 0 && event < ID_MAX) {
			switch (event) {
			case 0:
			{
				t2ddbg_alsa_sif_flag = !t2ddbg_alsa_sif_flag;
				printk("ALSA SoC audio debug message is %s\n", \
						t2ddbg_alsa_sif_flag ? "enabled" : "disabled");
			}
			break;
			default:
				break;
			}
		} else {
			break;
		}
	}
	return 1;
}
#endif





int sdp_sif_machine_probe(struct platform_device *pdev)
{
	int ret_val = 0;
	struct sif_mc_private *sif_mc_priv;

	printk("%s++\n", __func__);

#ifdef CONFIG_T2D_DEBUGD
	//t2d_dbg_register("DVB/V4L2 Audio debug", 7, t2ddebug_v4l2_audio_debug, NULL);
	t2d_dbg_register("SIF debug", 61, t2ddebug_sif_debug, NULL);
#endif // CONFIG_T2D_DEBUGD

	sif_mc_priv = kzalloc(sizeof(*sif_mc_priv), GFP_ATOMIC);

	if (!sif_mc_priv) {
		pr_err("allocation failed\n");
		return -ENOMEM;
	}

	sif_mc_priv->sif_dev = kzalloc(sizeof(struct sdp_sif_dev), GFP_ATOMIC);

	if (!sif_mc_priv->sif_dev) 
	{
		pr_err("allocation failed\n");
		kfree(sif_mc_priv);
		return -ENOMEM;
	}

	pr_err("%s:sif_mc_priv[%p] \n", __func__, sif_mc_priv);
	sdp_sif_init(sif_mc_priv->sif_dev);

	/* register the soc card */
	snd_soc_sif_card.dev = &pdev->dev;
	ret_val = snd_soc_register_card(&snd_soc_sif_card);

	if (ret_val) 
	{
		pr_err("%s failed %d\n", __func__, ret_val);
		kfree(sif_mc_priv->sif_dev);
		goto freeirq;
	}

	platform_set_drvdata(pdev, sif_mc_priv);

	pr_debug("%s--\n", __func__);

	return ret_val;

freeirq:
	kfree(sif_mc_priv);
	return ret_val;
}

EXPORT_SYMBOL_GPL(sdp_sif_machine_probe);

int sdp_sif_machine_remove(struct platform_device *pdev)
{
	struct sif_mc_private *sif_mc_priv = platform_get_drvdata(pdev);

	printk("%s++\n", __func__);
	//free_irq(platform_get_irq(pdev, 0), sif_mc_priv);
	snd_soc_unregister_card(&snd_soc_sif_card);
	kfree(sif_mc_priv->sif_dev);
	kfree(sif_mc_priv);
	platform_set_drvdata(pdev, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(sdp_sif_machine_remove);


MODULE_DESCRIPTION("ASoC SIF Machine driver");
MODULE_AUTHOR("Kyung <kyonggo@samsung.com>");
MODULE_LICENSE("GPL v2");

#endif
