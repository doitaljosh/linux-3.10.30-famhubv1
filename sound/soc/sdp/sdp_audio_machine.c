
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
#include "lldAioIsr.h"
#include "lldAioI2s.h"
#include "sdp_audio.h"
#include "sdp_audio_platform.h"
#include "sdp_audio_sys.h"
#include "VirtualRegisterDef.h"
#include "spISifType.h"
#include "devSif.h"

#include "SpecialItemEnum_Base.h"

extern int kfactory_drv_get_data(int id, int *val); // ref. kfactory: rany.kwon@samsung.com


//GOLF KERNEL #include <t2ddebugd.h>
//#define ALSA_SIF_INTEGRATION 1
static int t2d_debug_ALSA_soc_onoff_flag = 1;

typedef struct  {

	spIAioHndl_t p_aio_hndl;
	spIAeHndl_t p_ae0_hndl;
	spIAeHndl_t p_ae1_hndl;	
	spIAeHndl_t p_ae2_hndl;	

} machine_audio_hndl;


machine_audio_hndl m_audio_hndl;

extern dec_pcm_state_info h_dec_pcm_state_info;


struct sdp_mc_private {
	void __iomem *int_base;
	u8 interrupt_status;
};


typedef enum
{	
	SPDIF_NEO_ES_DTS = 0,
	SPDIF_ES_DTS = 1,
	SPDIF_ES_DD = 2,
	SPDIF_PCM = 3,
	SPDIF_ES_AAC = 4,
	AIO_SPDIF_OUT_TYPE__MAX
}
Aio_Spdif_Out_Type_e;


typedef enum
{
    SD_AUDIO_IN_NULL,               ///<  default value ; use NULL out if there's no
    SD_AUDIO_IN_AUIN0,              ///<  AUIN 0
    SD_AUDIO_IN_AUIN1,              ///<  AUIN 1
    SD_AUDIO_IN_AUIN2,              ///<  AUIN 2
    SD_AUDIO_IN_AUIN3,              ///<  AUIN 3
    SD_AUDIO_IN_AUIN4,              ///<  AUIN 4
    SD_AUDIO_IN_AUIN5,              ///<  AUIN 5
    SD_AUDIO_IN_I2S,                ///<  I2S in
    SD_AUDIO_IN_I2S_MASTER,         ///<  I2S in - I2S Master Mode  In
    SD_AUDIO_IN_I2S_SLAVE,          ///<  I2S in - I2S Slave Mode In
    SD_AUDIO_IN_SPDIF,              ///<  S/PDIF
    SD_AUDIO_IN_SIF,                ///<   ATV use SIF input
    SD_AUDIO_IN_VIF,                ///<   ATV use VIF input
    SD_AUDIO_IN_DECODER,            ///<   audio decoder (used in DTV & MM)
    SD_AUDIO_IN_HDMI1,              ///< HDMI Port Input
    SD_AUDIO_IN_HDMI2,              ///< HDMI Port Input
    SD_AUDIO_IN_HDMI3,              ///< HDMI Port Input
    SD_AUDIO_IN_HDMI4,              ///< HDMI Port Input
    SD_AUDIO_IN_AUX1,               ///<   Reserved 1
    SD_AUDIO_IN_AUX2,               ///<   Reversed 2
    SD_AUDIO_IN_AUX3,               ///<   Reversed 2
    SD_AUDIO_IN_MAX
}  SdCommon_AudioPort_k;


typedef enum
{
    SD_BE_IN_VIDDEC0,
    SD_BE_IN_VIDDEC1,
    SD_BE_IN_VIDDEC2,
    SD_BE_IN_VIDDEC3,  
    SD_BE_IN_VIDDEC4,
    SD_BE_IN_VIDDEC5,
    SD_BE_IN_VIDDEC6,
    SD_BE_IN_VIDDEC7, 

    SD_BE_IN_EXTIN0,
    SD_BE_IN_EXTIN1,
    SD_BE_IN_EXTIN2,    

    SD_BE_IN_AUDDEC0,
    SD_BE_IN_AUDDEC1,
    SD_BE_IN_AUDDEC2,
    SD_BE_IN_AUDDEC3,

    SD_BE_IN_UNIPLAYER_VIDDEC0, ///< for uniplayer
    SD_BE_IN_UNIPLAYER_VIDDEC1,
    SD_BE_IN_UNIPLAYER_VIDDEC2,
    SD_BE_IN_UNIPLAYER_VIDDEC3,

    SD_BE_IN_UNIPLAYER_AUDDEC0,
    SD_BE_IN_UNIPLAYER_AUDDEC1,

    SD_BE_IN_UNIPLAYER_SWDEC,
    SD_BE_IN_SIF,

    SD_BE_IN_PICDEC0,
    SD_BE_IN_PICDEC1,

    SD_BE_IN_VIDDEC_0AND1,          ///< for KR 3D format

    SD_BE_IN_SCALER0_WITH_OSD,      ///< Main Scaler 출력, OSD Mux 된 영상을 Sub Scaler 입력으로 받음
    SD_BE_IN_SCALER0_WITHOUT_OSD,   ///< Main Scaler 출력만 Sub Scaler 입력으로 받음

    SD_BE_IN_UNIPLAYER_VIDDEC_UD,   ///< for uniplayer. h/w decoding movie for UD resolution
    SD_BE_IN_UNIPLAYER_PICDEC_UD,   ///< for uniplayer. h/w decoding picture for UD resolution
    SD_BE_IN_UNIPLAYER_SWDEC_UD,    ///< for uniplayer. s/w decoding for UD resolution

    SD_BE_IN_EXTVIDEO,              ///< for External Video handling
    SD_BE_IN_EXTAUDIO,              ///< for External Audio handling

    SD_BE_IN_EXTIN3,
    SD_BE_IN_EXTIN4,
    SD_BE_IN_EXTIN5,

    SD_BE_IN_EXTIN_HDMI0,
    SD_BE_IN_EXTIN_HDMI1,

    SD_BE_IN_MAX
} SdBackEndInID_k;


static unsigned int 	is_be_connected = SD_BE_IN_AUDDEC0;  //not use
static unsigned int 	is_master = SD_AUDIO_IN_I2S_SLAVE;  //not use

static unsigned int 	g_sf = AUDIO_48KHZ;
static unsigned int 	is_spdiftx_es=0;
static unsigned int 	is_remote_clone = AUDIO_OUT_REMOTE;
static unsigned int 	is_remote_mode = CLONE_VIEW_MODE_MP3;
static unsigned int 	pcm_main_out_value   = AE_SRC_DTV_DEC0; // initial value
static unsigned int 	pcm_remote_out_value = AE_SRC_DTV_DEC0; // initial value
static unsigned int 	pcm_scart_out_value  = AE_SRC_DTV_DEC0; // initial value


typedef enum
{
	AE_MIXER_INST_0_ON	= 0,
	AE_MIXER_INST_0_OFF = 1,		
	AE_MIXER_INST_1_ON	= 2,
	AE_MIXER_INST_1_OFF = 3,

	AE_MIXER_INST_ONOFF_MAX
}Ae_MixerInstOnOff_e;



#define 	VOLUME_RANGE		101
unsigned short g_MixingScaleVolumeTable[VOLUME_RANGE] = 
{
	1064,1056,960, 880, 816, 752, 688, 640, 592, 560,    
	528	,496, 480, 464, 448, 432, 416, 400, 384, 368,    
	360	,352, 344, 336, 328, 320, 312, 304, 296, 288,    
	280	,276, 272, 268, 264, 260, 256, 252, 248, 244,    
	240	,236, 232, 228, 224, 220, 216, 212, 208, 204,    
	200	,196, 192, 188, 184, 180, 176, 172, 168, 164,    
	160	,156, 152, 148, 144, 140, 136, 132, 128, 124,    
	120	,116, 112, 108, 104, 100, 96,  92,  88,  84,    
	80	,76	, 72,  68,  64,  60,  56,  52,  48,  44,    
	40	,36	, 32,  28,  24,  20,  16,  12,  8,   4,  0,  
}; 


static int		bt_kout_buf_get_share_dec_id = 0;
static int              bt_kout_buf_pcm_format_mode = 0;
static unsigned int 	bt_kout_buf_start_stop_stat = 0;
static int		bt_kout_buf_mute_on = 0;
static int		bt_kout_buf_set_delay = 0;
static int		bt_kout_buf_volume_value = 0;
static int		bt_kout_buf_set_format = 0;


static	unsigned int 	dolby_compressed_dec0_mode;
static int dolby_compressed_dec0_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 "RF", "LINE"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int dolby_compressed_dec0_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = dolby_compressed_dec0_mode;
	return 0;
}


static int dolby_compressed_dec0_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;	
	dolby_compressed_dec0_mode = ucontrol->value.enumerated.item[0];
#ifdef CONFIG_T2D_DEBUGD	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, dolby_compressed_dec0_mode, current->pid, current->comm);
#endif 
	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;	
	devAe_SetCompMode(pAeHndl->pDevAeHndl,AE_INST0, AUDIO_AC3, dolby_compressed_dec0_mode);
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 		

	return 0;	
}


static	unsigned int 	dolby_compressed_dec1_mode;
static int dolby_compressed_dec1_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 "RF", "LINE"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int dolby_compressed_dec1_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = dolby_compressed_dec1_mode;
	return 0;
}


static int dolby_compressed_dec1_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;	
	dolby_compressed_dec1_mode = ucontrol->value.enumerated.item[0];
#ifdef CONFIG_T2D_DEBUGD	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, dolby_compressed_dec1_mode, current->pid, current->comm);
#endif 
	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae1_hndl;	
	devAe_SetCompMode(pAeHndl->pDevAeHndl,AE_INST1, AUDIO_AC3, dolby_compressed_dec1_mode);
	dev_audio_ae_close(m_audio_hndl.p_ae1_hndl); 		

	return 0;	
}




static int g_bt_delay_main = 0;
static int bt_delay_main_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_bt_delay_main;
	return 0;
}

static int bt_delay_main_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);
	g_bt_delay_main = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;
	

	devAe_VRegSet(pAeHndl->pDevAeHndl, AE_BTDUMP_DELAY,  g_bt_delay_main);

	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl);
	return 0;
}


static int g_bt_delay_sub = 0;
static int bt_delay_sub_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_bt_delay_sub;
	return 0;
}

static int bt_delay_sub_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST1, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);
	g_bt_delay_sub = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae1_hndl;
	devAe_VRegSet(pAeHndl->pDevAeHndl, AE_BTDUMP_DELAY,  g_bt_delay_sub);

	dev_audio_ae_close(m_audio_hndl.p_ae1_hndl);
	return 0;
}


static int g_acm_delay_main = 0;
static int acm_delay_main_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_acm_delay_main;
	return 0;
}

static int acm_delay_main_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);
	g_acm_delay_main = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;
	devAe_VRegSet(pAeHndl->pDevAeHndl, AE_PCMDUMP_DELAY,  g_acm_delay_main);

	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl);
	return 0;
}


static int g_acm_delay_sub = 0;
static int acm_delay_sub_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_acm_delay_sub;
	return 0;
}

static int acm_delay_sub_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST1, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);
	g_acm_delay_sub = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae1_hndl;
	devAe_VRegSet(pAeHndl->pDevAeHndl, AE_PCMDUMP_DELAY,  g_acm_delay_sub);

	dev_audio_ae_close(m_audio_hndl.p_ae1_hndl);
	return 0;
}

 


static int bt_kernel_out_buf_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER64;
	uinfo->count = 5; // uBufferSize, uBaseAddress, uAccessPointtAddress, uFrameSizeAddress, uEnableMardAddress;
	uinfo->value.integer64.min = 0;
	uinfo->value.integer64.max = LLONG_MAX;
	return 0;
}

static int bt_kernel_out_buf_info_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int uBufferSize = 0;	
	unsigned int uBaseAddress=0;
	unsigned int uAccessPointtAddress=0;
	unsigned int uFrameSizeAddress=0;
	unsigned int uEnableMardAddress=0;	
	pspIAeData_t pAeHndl;		

	if(bt_kout_buf_pcm_format_mode)
	{
		//PCM
		if(bt_kout_buf_get_share_dec_id == 0)
		{
			dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);			
			pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;				
			dev_audio_ae_getbtdumpinfo(bt_kout_buf_get_share_dec_id, pAeHndl->pDevAeHndl,&uBaseAddress, &uBufferSize, &uAccessPointtAddress, &uFrameSizeAddress, &uEnableMardAddress);
			dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 			
		}
		else if(bt_kout_buf_get_share_dec_id == 1)
		{
			dev_audio_ae_open(AE_INST1, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);			
			pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae1_hndl;						
			dev_audio_ae_getbtdumpinfo(bt_kout_buf_get_share_dec_id, pAeHndl->pDevAeHndl,&uBaseAddress, &uBufferSize, &uAccessPointtAddress, &uFrameSizeAddress, &uEnableMardAddress);
			dev_audio_ae_close(m_audio_hndl.p_ae1_hndl); 				
		}
	}
	else
	{
		//ES
		if(bt_kout_buf_get_share_dec_id == 0)
		{
			dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);			
			pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;		
			dev_audio_ae_getesdumpinfo(bt_kout_buf_get_share_dec_id , pAeHndl->pDevAeHndl,&uBaseAddress, &uBufferSize, &uAccessPointtAddress, &uFrameSizeAddress, &uEnableMardAddress);
			dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 	

		}
		else
		{
			pr_err("INVALID(NOT SUPPORT DEC)(%d)\n", bt_kout_buf_get_share_dec_id); 					
			return AUDIO_ERR;
		}		
	}

	ucontrol->value.integer64.value[0] = (long long)uBufferSize;
	ucontrol->value.integer64.value[1] = (long long)uBaseAddress;
	ucontrol->value.integer64.value[2] = (long long)uAccessPointtAddress;
	ucontrol->value.integer64.value[3] = (long long)uFrameSizeAddress;
	ucontrol->value.integer64.value[4] = (long long)uEnableMardAddress;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s:  [pid:%d,%s] "
		"uBufferSize[0x%x],uBaseAddress[0x%x],uAccessPointtAddress[0x%x],uFrameSizeAddress[0x%x],uEnableMardAddress[0x%x]\n"
		"wptr=0x%x,fsize=0x%x\n",
		__FUNCTION__, current->pid, current->comm,
		uBufferSize, uBaseAddress, uAccessPointtAddress, uFrameSizeAddress, uEnableMardAddress,
		uAccessPointtAddress != NULL ? readl(uAccessPointtAddress) : 0,
		uFrameSizeAddress != NULL ? readl(uFrameSizeAddress) : 0);

	pr_debug("uBufferSize[0x%x],uBaseAddress[0x%x],uAccessPointtAddress[0x%x],uFrameSizeAddress[0x%x],uEnableMardAddress[0x%x] \n"
		  ,uBufferSize, uBaseAddress, uAccessPointtAddress, uFrameSizeAddress, uEnableMardAddress);
	
	return 0;
}



static int bt_kernel_out_buf_source_type_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2+2] = {
		//  0       1          2      3
		"CLONE", "DUAL", "ES", "PCM"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 2+2;
	if (uinfo->value.enumerated.item >= (2+2-1))
		uinfo->value.enumerated.item = 2+2-1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

	return 0;
}
static int bt_kernel_out_buf_dec_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{

	ucontrol->value.enumerated.item[0] = bt_kout_buf_get_share_dec_id;
	ucontrol->value.enumerated.item[1] = bt_kout_buf_pcm_format_mode + 2;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: dec %d pcm %d (%d %d) [pid:%d,%s]\n", __FUNCTION__, bt_kout_buf_get_share_dec_id, bt_kout_buf_pcm_format_mode, ucontrol->value.enumerated.item[0], ucontrol->value.enumerated.item[1], current->pid, current->comm);
	return 0;
}
static int bt_kernel_out_buf_dec_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: dec %d pcm %d (%d %d) [pid:%d,%s]\n", __FUNCTION__, bt_kout_buf_get_share_dec_id, bt_kout_buf_pcm_format_mode, ucontrol->value.enumerated.item[0], ucontrol->value.enumerated.item[1], current->pid, current->comm);

	if(ucontrol->value.enumerated.item[0] > 1){
		return -EINVAL;
	}else{
		bt_kout_buf_get_share_dec_id = ucontrol->value.enumerated.item[0];	// 0 or 1
	}

	if(ucontrol->value.enumerated.item[1] < 2){
		return -EINVAL;
	}else{
		bt_kout_buf_pcm_format_mode = ucontrol->value.enumerated.item[1] - 2;
	}

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: dec %d pcm %d [pid:%d,%s]\n", __FUNCTION__, bt_kout_buf_get_share_dec_id, bt_kout_buf_pcm_format_mode, current->pid, current->comm);
	return 0;	
}



static int start_stop_audio_stream_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int start_stop_audio_stream_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = bt_kout_buf_start_stop_stat;
	return 0;
}


static int start_stop_audio_stream_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	bt_kout_buf_start_stop_stat = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, bt_kout_buf_start_stop_stat, current->pid, current->comm);

	pspIAeData_t pAeHndl;	
	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;	
	if(bt_kout_buf_start_stop_stat == 1)
		lldAe_SetEsDumpEnable((pAe_Hndl_s)pAeHndl->pDevAeHndl, AUDIO_ENABLE);		
	else
		lldAe_SetEsDumpEnable((pAe_Hndl_s)pAeHndl->pDevAeHndl, AUDIO_DISABLE);				
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 		

	return 0;
}



static int set_mute_audio_stream_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = bt_kout_buf_mute_on;
	return 0;
}


static int set_mute_audio_stream_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	unsigned int nVolume, m_BTVolume;
	Audio_Enable_e eMute;
	pspIAeData_t pAeHndl;	

	bt_kout_buf_mute_on = ucontrol->value.integer.value[0];
	
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	//PCM
	if(TRUE == bt_kout_buf_mute_on)
	{
		nVolume = g_MixingScaleVolumeTable[0]; 
	}
	else
	{
		nVolume = bt_kout_buf_volume_value;
	}
	
	if(bt_kout_buf_get_share_dec_id == 0)
	{
		devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_BT_DUMP_MAIN, (unsigned short)nVolume);
	}
	else if(bt_kout_buf_get_share_dec_id == 1)
	{	
		devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_BT_DUMP_SUB, (unsigned short)nVolume);		
	}
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);	 	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;	
	//ES
	if(bt_kout_buf_get_share_dec_id == 0)
	{
		eMute = (bt_kout_buf_mute_on == TRUE) ? AUDIO_ENABLE :AUDIO_DISABLE;				
		if(pAeHndl->tAeInst > AE_INST2)
		{
			return AUDIO_ERR_INVALIDARG;
		}
		else
		{
			lldAe_SetEsDumpMute((pAe_Hndl_s)pAeHndl->pDevAeHndl, eMute);
		}	
 
	}
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 
		
	return 0;
}
static int set_delay_audio_stream_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = bt_kout_buf_set_delay;
	return 0;
}
static int set_delay_audio_stream_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	
	bt_kout_buf_set_delay = ucontrol->value.integer.value[0];

	devAeHndl_t hAe;
	pAe_Hndl_s pHAe;
	devAe_GetHndl(2, &hAe, 0);
	pHAe = (pAe_Hndl_s)hAe;
	
	snd_writel((unsigned short)bt_kout_buf_set_delay, pHAe->ae_sw_regs +  rAE_ESDUMP_DELAY_MAIN);	

	return 0;	
}



static int set_audio_stream_format_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return 0;
}

static int set_audio_stream_format_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = bt_kout_buf_set_format;
	return 0;
}


static int set_audio_stream_format_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	
	bt_kout_buf_set_format = ucontrol->value.enumerated.item[0];
	return 0;	
}



static int set_audio_stream_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = bt_kout_buf_volume_value;
	return 0;
}
static int set_audio_stream_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int m_BTVolume;
	pspIAeData_t pAeHndl;	

	bt_kout_buf_volume_value = ucontrol->value.integer.value[0];


	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	if ( (unsigned int)bt_kout_buf_volume_value >= VOLUME_RANGE )
	{
		pr_err("AUDIO PCM VOLUME : setting volume is too big (%d) \n", bt_kout_buf_volume_value); 					
		return AUDIO_ERR;
	}
	
	m_BTVolume = g_MixingScaleVolumeTable[bt_kout_buf_volume_value]; 

	if(bt_kout_buf_get_share_dec_id == 0)
	{
		devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_BT_DUMP_MAIN, (unsigned short)m_BTVolume);				
	}
	else if(bt_kout_buf_get_share_dec_id == 1)
	{	
		devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_BT_DUMP_SUB, (unsigned short)m_BTVolume);				
	}

	pr_debug("PCM Volume(%d)(%d)\n",bt_kout_buf_get_share_dec_id, (unsigned short)m_BTVolume );
	
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;	
}




static int audio_status_sub_codec_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[17] = {
	 "LPCM", "AC3", "MPEG","DTS", "WMA", "AAC", "ADPCM", "MULAW", "ALAW", "EAC3", "RA G2COOK", "RA LOSSLESS", "DRA"
	 	,"VORBIS", "HEAAC LTP", "DTS LBR", "UNKNOWN"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 17;
	if (uinfo->value.enumerated.item >= 16)
		uinfo->value.enumerated.item = 16;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int audio_status_sub_codec_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: [pid:%d,%s]\n", __FUNCTION__, current->pid, current->comm);

	unsigned short codec_type;	
	devAeHndl_t hAe;
	pAe_Hndl_s pHAe;
	devAe_GetHndl(2, &hAe, 0);
	pHAe = (pAe_Hndl_s)hAe;

	codec_type = snd_readw(pHAe->ae_sw_regs + rAE_CODEC_SUB);
	switch (codec_type)
	{
		case rAE_CODEC_LPCM:
			ucontrol->value.enumerated.item[0] = 0;
			break;
		case rAE_CODEC_AC3:
			ucontrol->value.enumerated.item[0] = 1;
			break;
		case rAE_CODEC_MPEG:
			ucontrol->value.enumerated.item[0] = 2;
			break;
		case rAE_CODEC_DTS:
			ucontrol->value.enumerated.item[0] = 3;
			break;
		case rAE_CODEC_WMA:		
			ucontrol->value.enumerated.item[0] = 4;
			break;
		case rAE_CODEC_AAC:		
			ucontrol->value.enumerated.item[0] = 5;
			break;
		case rAE_CODEC_ADPCM:	
			ucontrol->value.enumerated.item[0] = 6;
			break;
		case rAE_CODEC_MULAW_PCM:		
			ucontrol->value.enumerated.item[0] = 7;
			break;
		case rAE_CODEC_ALAW_PCM:
			ucontrol->value.enumerated.item[0] = 8;
			break;
		case rAE_CODEC_EAC3:
			ucontrol->value.enumerated.item[0] = 9;
			break;
		case rAE_CODEC_RA_G2COOK:
			ucontrol->value.enumerated.item[0] = 10;
			break;
		case rAE_CODEC_RA_LOSSLESS:
			ucontrol->value.enumerated.item[0] = 11;
			break;
		case rAE_CODEC_DRA:
			ucontrol->value.enumerated.item[0] = 12;
			break;				
		case rAE_CODEC_VORBIS:
			ucontrol->value.enumerated.item[0] = 13;
			break;
		case rAE_CODEC_HEAAC_LTP:
			ucontrol->value.enumerated.item[0] = 14;
			break;
		case rAE_CODEC_DTS_LBR_P0:
			ucontrol->value.enumerated.item[0] = 15;
			break;	
		default:
			ucontrol->value.enumerated.item[0] =  16;
			break;
	}

	return 0;
}



static int audio_status_main_codec_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[17] = {
	 "LPCM", "AC3", "MPEG","DTS", "WMA", "AAC", "ADPCM", "MULAW", "ALAW", "EAC3", "RA G2COOK", "RA LOSSLESS", "DRA"
	 	,"VORBIS", "HEAAC LTP", "DTS LBR", "UNKNOWN"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 17;
	if (uinfo->value.enumerated.item >= 16)
		uinfo->value.enumerated.item = 16;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int audio_status_main_codec_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	//t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: [pid:%d,%s]\n", __FUNCTION__, current->pid, current->comm);

	unsigned short codec_type;	
	devAeHndl_t hAe;
	pAe_Hndl_s pHAe;
	devAe_GetHndl(2, &hAe, 0);
	pHAe = (pAe_Hndl_s)hAe;

	codec_type = snd_readw(pHAe->ae_sw_regs + rAE_CODEC_MAIN);
	switch (codec_type)
	{
		case rAE_CODEC_LPCM:
			ucontrol->value.enumerated.item[0] = 0;
			break;
		case rAE_CODEC_AC3:
			ucontrol->value.enumerated.item[0] = 1;
			break;
		case rAE_CODEC_MPEG:
			ucontrol->value.enumerated.item[0] = 2;
			break;
		case rAE_CODEC_DTS:
			ucontrol->value.enumerated.item[0] = 3;
			break;
		case rAE_CODEC_WMA:		
			ucontrol->value.enumerated.item[0] = 4;
			break;
		case rAE_CODEC_AAC:		
			ucontrol->value.enumerated.item[0] = 5;
			break;
		case rAE_CODEC_ADPCM:	
			ucontrol->value.enumerated.item[0] = 6;
			break;
		case rAE_CODEC_MULAW_PCM:		
			ucontrol->value.enumerated.item[0] = 7;
			break;
		case rAE_CODEC_ALAW_PCM:
			ucontrol->value.enumerated.item[0] = 8;
			break;
		case rAE_CODEC_EAC3:
			ucontrol->value.enumerated.item[0] = 9;
			break;
		case rAE_CODEC_RA_G2COOK:
			ucontrol->value.enumerated.item[0] = 10;
			break;
		case rAE_CODEC_RA_LOSSLESS:
			ucontrol->value.enumerated.item[0] = 11;
			break;
		case rAE_CODEC_DRA:
			ucontrol->value.enumerated.item[0] = 12;
			break;				
		case rAE_CODEC_VORBIS:
			ucontrol->value.enumerated.item[0] = 13;
			break;
		case rAE_CODEC_HEAAC_LTP:
			ucontrol->value.enumerated.item[0] = 14;
			break;
		case rAE_CODEC_DTS_LBR_P0:
			ucontrol->value.enumerated.item[0] = 15;
			break;	
		default:
			ucontrol->value.enumerated.item[0] =  16;
			break;
	}

	return 0;
}




extern wait_queue_head_t status_info_lock_wtq;
static int audio_status_info_lock_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {
	 "Unlock(skip)", "Lock", "Unlock(repeat)"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 2;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int audio_status_info_lock_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	//t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: [pid:%d,%s]\n", __FUNCTION__, current->pid, current->comm);

	if(wait_event_interruptible_timeout(status_info_lock_wtq, (h_dec_pcm_state_info.enumLockStatus == AE_LOCK), 3*HZ)) {
		//pr_err("%s: wake up error\n", __FUNCTION__); this is not an error. user should retry...
		ucontrol->value.enumerated.item[0] = h_dec_pcm_state_info.enumLockStatus;				
		return 0;//-ERESTARTSYS;
	}
	ucontrol->value.enumerated.item[0] = h_dec_pcm_state_info.enumLockStatus;		
	return 0;
}

#if 0
static int  audio_status_info_lock_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int  audio_status_info_lock_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{	
	unsigned int count=0;

	ucontrol->value.integer.value[0] = 	h_dec_pcm_state_info.enumLockStatus;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: [pid:%d,%s]\n", __FUNCTION__, current->pid, current->comm);

	do 
	{
		ucontrol->value.integer.value[0] = 	h_dec_pcm_state_info.enumLockStatus;		
		mdelay(20);
		count++;
	} while (( count < 100) && (h_dec_pcm_state_info.enumLockStatus != AE_LOCK));
	
	return 0;
}
#endif


static unsigned int 	echo_onoff_stat;
static int echo_onoff_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {
	 "MIX0 ON", "MIX0 OFF","MIX1 ON","MIX1 OFF"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int echo_onoff_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	//t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: [pid:%d,%s]\n", __FUNCTION__, current->pid, current->comm);
	ucontrol->value.enumerated.item[0] = echo_onoff_stat;
	return 0;
}


static int echo_onoff_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	echo_onoff_stat = value;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0); 

	if(value == AE_MIXER_INST_0_ON)
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_0, AUDIO_ENABLE);
	else if(value == AE_MIXER_INST_0_OFF)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_0, AUDIO_DISABLE);
	else if(value == AE_MIXER_INST_1_ON)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_1, AUDIO_ENABLE);
	else if(value == AE_MIXER_INST_1_OFF)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_1, AUDIO_DISABLE);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 		

	return 0;	


}

#if 0
static int  echo_onoff_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int  echo_onoff_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0); 

	if(value == AE_MIXER_INST_0_ON)
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_0, AUDIO_ENABLE);
	else if(value == AE_MIXER_INST_0_OFF)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_0, AUDIO_DISABLE);
	else if(value == AE_MIXER_INST_1_ON)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_1, AUDIO_ENABLE);
	else if(value == AE_MIXER_INST_1_OFF)		
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, AE_MIXER_INST_1, AUDIO_DISABLE);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 		

	return 0;	

}
#endif 


static int  main_in_buffer_level_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int  main_in_buffer_level_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{

	unsigned int freesize, realbufsize;
	
	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_ae_getbuffreesize(m_audio_hndl.p_ae0_hndl,AUDIO_MEMCONFIG_IN_MAIN_SIZE , &freesize, &realbufsize);
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 

	ucontrol->value.integer.value[0] = freesize;
	ucontrol->value.integer.value[1] = realbufsize;	
	return 0;
}


static int  sub_in_buffer_level_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int  sub_in_buffer_level_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{

	unsigned int freesize, realbufsize;
	
	dev_audio_ae_open(AE_INST1, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_ae_getbuffreesize(m_audio_hndl.p_ae1_hndl,AUDIO_MEMCONFIG_IN_SUB_SIZE , &freesize, &realbufsize) ;
	dev_audio_ae_close(m_audio_hndl.p_ae1_hndl); 

	ucontrol->value.integer.value[0] = freesize;
	ucontrol->value.integer.value[1] = realbufsize;	
	return 0;
}





//not use
static int is_be_connected_main_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

//not use
static int is_be_connected_main_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	is_be_connected = value;

	return 0;	
}


static unsigned int 	is_spdiftx_es;
static int is_spdiftx_es_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 "PCM", "ES"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int is_spdiftx_es_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = is_spdiftx_es;
	return 0;
}


static int is_spdiftx_es_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	is_spdiftx_es =value;

}

#if 0
static int is_spdiftx_es_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}


static int is_spdiftx_es_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	is_spdiftx_es =value;

	return 0;	
}
#endif 

//not use
static int is_master_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

//not use
static int is_master_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	is_master = value;

	return 0;	
}

static int is_remote_clone_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}


static int is_remote_clone_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	is_remote_clone = value;

	return 0;	
}

static int is_remote_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}


static int is_remote_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	is_remote_mode =  value;

	return 0;	
}




static int pcm_out_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[7] = {
	 "DTV DEC0", "DTV DEC1", "HDMI ES", "HDMI PCM", "LineIn/Composite", "ATV", "MM"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item > 6)
		uinfo->value.enumerated.item = 6;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int pcm_main_out_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = pcm_main_out_value;
	return 0;
}


static int pcm_main_out_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	unsigned int value = ucontrol->value.enumerated.item[0];
	int ret = 0;
	 //CLK, MAP		
	spIAioHndl_t spIHndl;
	static Aio_I2sOutParam_s i2sParam;
	static Aio_SpdOutParam_s spdParam;
	Aio_I2sInParam_s  sI2sInParam;		
	pcm_main_out_value = value;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: src: %d [pid:%d,%s]\n", __FUNCTION__, pcm_main_out_value, current->pid, current->comm);

	ret = dev_audio_aio_open(AIO_INST0, &spIHndl, AUDIO_CHIP_ID_MP0);
	if(ret != AUDIO_OK){
		pr_err("[audio] AIO_INST0 open failed %d [pid:%d,%s]\n", ret, current->pid, current->comm);
		return -EIO;
	}

//CLK, MAP
#if 1
	switch(value) {
	
	case AE_SRC_DTV_DEC0:
        /* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);		
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);

	     /* I2STx 1*/		
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;		
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL;		
		dev_audio_aio_stopi2sout(spIHndl, 1);		
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);		


		/* SPDIFTx 0*/
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);
	
		break;	
		
	case AE_SRC_DTV_DEC1:
		/* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);				
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);

	     /* I2STx 1*/				
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;		
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL;		
		dev_audio_aio_stopi2sout(spIHndl, 1);				
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);	
		
		/* SPDIFTx 0*/
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);		
		break;
	
	case AE_SRC_SPDIF_ES:
		/* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);		
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);


	     /* I2STx 1*/				
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC ;//PLL0_AIOSYNC	
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;				
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL;		
		dev_audio_aio_stopi2sout(spIHndl, 1);		
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);

		/* SPDIFTx 0*/
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC ;		
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);

		break;
	
	case AE_SRC_SPDIF_PCM:
		/* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC;
        	i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);			
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);

	     /* I2STx 1*/			
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC ;//PLL0_AIOSYNC	
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;
		dev_audio_aio_stopi2sout(spIHndl, 1);			
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);


		/* SPDIFTx 0*/   
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0_AIOSYNC ;			
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		}else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);			
		break;
		
	case AE_SRC_PCM:

		/*I2STx0*/		
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;				
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		i2sParam.enumSmplFreq = g_sf;
		dev_audio_aio_stopi2sout(spIHndl, 0);			
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);
			
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;				
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;
		i2sParam.enumSmplFreq = g_sf;
		dev_audio_aio_stopi2sout(spIHndl, 1);			
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);

		/* SPDIFTx 0*/ 
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);			


		/* I2SRx0*/ 

		dev_audio_aio_stopi2sin(spIHndl, 0);	
		dev_audio_aio_geti2sinparam(spIHndl, 0, &sI2sInParam);  //Get Default
		sI2sInParam.enumI2sFormat = AIO_I2S_BASIC;
		sI2sInParam.enumBitLen	= AIO_24BIT;
		sI2sInParam.enumSmplFreq = AUDIO_48KHZ;
		sI2sInParam.enumClkScale = AIO_256FS;
		sI2sInParam.enumI2sMode = AIO_I2S_MASTER;
		sI2sInParam.enumInputMemMap = AUDIO_IN_ADC_RX;
		sI2sInParam.enumPllFinSource = AIO_PLLFIN_PULLPLL;
		sI2sInParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		dev_audio_aio_starti2sin(spIHndl, 0, &sI2sInParam);		
		break;
		
	case AE_SRC_ATV:
		/*I2STx0*/					
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_I2SRX1_MCLK;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL; 	
		dev_audio_aio_stopi2sout(spIHndl, 0);			
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);

		/*I2STx1*/		
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_I2SRX1_MCLK ;//I2SRX1_CLK
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL; 	
		dev_audio_aio_stopi2sout(spIHndl, 1);			
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);


		/* SPDIFTx 0*/ 
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_I2SRX0_MCLK ;	
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);			


		/* I2SRx1*/ 
		dev_audio_aio_stopi2sin(spIHndl, 1);
		dev_audio_aio_geti2sinparam(spIHndl, 1, &sI2sInParam);
		sI2sInParam.enumI2sFormat = AIO_I2S_BASIC;
		sI2sInParam.enumBitLen = AIO_24BIT;
		sI2sInParam.enumSmplFreq = AUDIO_48KHZ;
		sI2sInParam.enumInputMemMap = AUDIO_IN_ATV_RX;
		sI2sInParam.enumClkScale =	AIO_256FS;
		sI2sInParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		sI2sInParam.enumI2sMode = AIO_I2S_SLAVE;
		dev_audio_aio_starti2sin(spIHndl, 1, &sI2sInParam);

		
		break;
	
	case AE_SRC_UNI:
		/*I2STx0*/					
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource  = AIO_CLK_SOURCE_INTERNAL0; //PLL0
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL; 	
		dev_audio_aio_stopi2sout(spIHndl, 0);			
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);

		/*I2STx1*/			
		dev_audio_aio_geti2soutparam(spIHndl, 1, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource  = AIO_CLK_SOURCE_INTERNAL0; //PLL0
		i2sParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;
		dev_audio_aio_stopi2sout(spIHndl, 1);			
		dev_audio_aio_starti2sout(spIHndl, 1, &i2sParam);

		/* SPDIFTx 0*/ 
		dev_audio_aio_getspdifoutparam(spIHndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumSmplFreq = g_sf;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0 ;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;

		if(is_spdiftx_es) {
			spdParam.enumPcmEs = AIO_PCMES_ES;					
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		} else {
			spdParam.enumPcmEs = AIO_PCMES_PCM;		
			spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		}
		
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_stopspdifout(spIHndl, 0);		
		dev_audio_aio_startspdifout(spIHndl, 0, &spdParam);		
		break;
	
	default:
		pr_err("%s: invalid source id %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
		ret = -EINVAL;
		break;
	}	
#endif	
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_ae_setport(m_audio_hndl.p_ae2_hndl, AE_PORT_MAIN, value,TRUE);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 
	dev_audio_aio_close(spIHndl);

	return ret;
}


static int pcm_remote_out_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = pcm_remote_out_value;
	return 0;
}

static int pcm_remote_out_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	int ret = 0;
 //CLK, MAP
	spIAioHndl_t spIHndl;
	static Aio_I2sOutParam_s i2sParam;
	//static Aio_SpdOutParam_s spdParam;//not used
	Aio_I2sInParam_s  sI2sInParam;
	ret = dev_audio_aio_open(AIO_INST0, &spIHndl, AUDIO_CHIP_ID_MP0);
	if(ret != AUDIO_OK){
		pr_err("[audio] AIO_INST0 open failed %d [pid:%d,%s]\n", ret, current->pid, current->comm);
		return -EIO;
	}

	pcm_remote_out_value = ucontrol->value.enumerated.item[0];
	
	
#if 1	 //CLK, MAP
	switch(value) {

	case AE_SRC_DTV_DEC0:

		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1 ; //fix

		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;
		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
		
		break;

	case AE_SRC_DTV_DEC1:

		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1 ;

		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
		
		break;

	case AE_SRC_SPDIF_ES:
			
		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1_AIOSYNC ;//PLL1_AIOSYNC	
	
		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
		
			
		break;

	case AE_SRC_SPDIF_PCM:
			
		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1_AIOSYNC ;//PLL1_AIOSYNC	
				
		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
		
		break;
		
	case AE_SRC_PCM:
		
		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1;				
		i2sParam.enumSmplFreq = g_sf;
		
		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
		
			
		/* I2SRx0*/ 
		dev_audio_aio_stopi2sin(spIHndl, 0);	
		dev_audio_aio_geti2sinparam(spIHndl, 0, &sI2sInParam);  //Get Default
		sI2sInParam.enumI2sFormat = AIO_I2S_BASIC;
		sI2sInParam.enumBitLen	= AIO_24BIT;
		sI2sInParam.enumSmplFreq = AUDIO_48KHZ;
		sI2sInParam.enumClkScale = AIO_256FS;
		sI2sInParam.enumI2sMode = AIO_I2S_MASTER;
		sI2sInParam.enumInputMemMap = AUDIO_IN_ADC_RX;
		sI2sInParam.enumPllFinSource = AIO_PLLFIN_PULLPLL;
		sI2sInParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL1;
		dev_audio_aio_starti2sin(spIHndl, 0, &sI2sInParam);					
		break;
		
	case AE_SRC_ATV:
		
		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_I2SRX1_MCLK ;//I2SRX1_CLK

		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
			
			
		break;

	case AE_SRC_UNI:
			
		dev_audio_aio_geti2soutparam(spIHndl, 2, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumSmplFreq = g_sf;
		i2sParam.enumClkSource  = AIO_CLK_SOURCE_INTERNAL1; //PLL1

		if(is_remote_clone)
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE_CLON;
		else
			i2sParam.enumOutputMemMap = AUDIO_OUT_REMOTE;

		if(is_remote_mode)
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_MP3;
		else
			i2sParam.enumCloneViewMode = CLONE_VIEW_MODE_LPCM;

		dev_audio_aio_stopi2sout(spIHndl, 2);			
		dev_audio_aio_starti2sout(spIHndl, 2, &i2sParam);	
			
			
		break;

	default:
		pr_err("[audio] invalid source id %d [pid:%d,%s]\n", value, current->pid, current->comm);
		ret = -EINVAL;
		break;
	}	
#endif
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_ae_setport(m_audio_hndl.p_ae2_hndl, AE_PORT_REMOTE, value,TRUE);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 
	dev_audio_aio_close(spIHndl);
	return ret;
}


static int pcm_scart_out_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = pcm_scart_out_value;
	return 0;
}

static int pcm_scart_out_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	int ret = 0;
	 //CLK, MAP		
	spIAioHndl_t spIHndl;
	static Aio_I2sOutParam_s i2sParam;
	//static Aio_SpdOutParam_s spdParam;//not used
	
	ret = dev_audio_aio_open(AIO_INST0, &spIHndl, AUDIO_CHIP_ID_MP0);
	if(ret != AUDIO_OK){
		pr_err("[audio] AIO_INST0 open failed %d [pid:%d,%s]\n", ret, current->pid, current->comm);
		return -EIO;
	}
	pcm_scart_out_value = ucontrol->value.enumerated.item[0];


#if 1
	switch(value) {
	
	case AE_SRC_DTV_DEC0:
		/* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;	
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;	
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);		
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);	
		break;	
		
	case AE_SRC_DTV_DEC1:
		/* I2STx 0*/
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumBitLen = AIO_24BIT;
		i2sParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;		
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		dev_audio_aio_stopi2sout(spIHndl, 0);				
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);	
		break;	
	
		
	case AE_SRC_ATV:
		/*I2STx0*/					
		dev_audio_aio_geti2soutparam(spIHndl, 0, &i2sParam);
		i2sParam.enumClkScale = AIO_256FS;
		i2sParam.enumI2sFormat = AIO_I2S_BASIC;
		i2sParam.enumSmplFreq = g_sf;		
		i2sParam.enumClkSource = AIO_CLK_SOURCE_I2SRX1_MCLK;
		i2sParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		i2sParam.enumPllFinSource = AIO_PLLFIN_PULLPLL; 	
		dev_audio_aio_stopi2sout(spIHndl, 0);			
		dev_audio_aio_starti2sout(spIHndl, 0, &i2sParam);		
		break;	
	case AE_SRC_SPDIF_ES:
	case AE_SRC_SPDIF_PCM:
	case AE_SRC_PCM:
	case AE_SRC_UNI:
		pr_err("%s: source id %d can not be used when SCART out [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
		ret = -EINVAL;		
	default:
		pr_err("%s: invalid source id %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
		ret = -EINVAL;
		break;
	}	
#endif	
	
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_ae_setport(m_audio_hndl.p_ae2_hndl, AE_PORT_SCARTRF, value,TRUE);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return ret;
}


#if 00
static void sdp_pcm_out(unsigned int intr_status)
{
//	sdp_pcm_processing(&jack_data);
	/* TODO: add american headset detection post gpiolib support */
}
#endif

static int g_speaker_out_gain = 0;
static int g_speaker_out_mute = 0;
static int g_auipcm_mix_gain  = 100;
static int g_auipcm_mix_mute  = 0;
static int g_main_out_gain    = 100;
static int g_main_out_mute    = 0;

static int out_scart_monitor_gain = 0;
static int out_dual_tv_gain       = 0;
static int out_spdiftx_gain       = 0;
static int out_headphone_gain     = 0;

static int dec0_out_gain     = 0;
static int dec1_out_gain     = 0;




static unsigned short g_HeadphoneVolumeTable[] = 
{
	665, 496, 480, 464, 448, 432, 416, 400, 384, 368,
	 360, 352, 344, 336, 328, 320, 312, 304, 296, 288,
	 280, 276, 272, 268, 264, 260, 256, 252, 248, 244,
	 240, 236, 232, 228, 224, 220, 216, 212, 208, 204,
	 200, 196, 192, 188, 184, 180, 176, 172, 168, 164,
	 160, 156, 152, 148, 144, 140, 136, 132, 128, 124,
	 120, 116, 112, 108, 104, 100, 96, 92, 88, 84,
	 80,	76, 72, 68, 64, 60, 56, 52, 48, 44,
	 40,	36, 32, 28, 24, 20, 16, 12, 8, 4, 
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};


static const unsigned int	gu32VolumeTable_GL[] =
{
	0,    17,  41,  61,  77,  93, 109, 121,
	133, 141, 149, 157, 161, 165, 169, 173, 
	177, 181, 185, 189, 191, 193, 195, 197, 
	199, 201, 203, 205, 207, 209, 211, 212, 
	213, 214, 215, 216, 217, 218, 219, 220, 
	221, 222, 223, 224, 225, 226, 227, 228, 
	229, 230, 231, 232, 233, 234, 235, 236, 
	237, 238, 239, 240, 241, 242, 243, 244, 
	245, 246, 247, 248, 249, 250, 251, 252, 
	253, 254, 255, 256, 257, 258, 259, 260, 
	261, 262, 263, 264, 265, 266, 267, 268, 
	269, 270, 271, 272, 273, 274, 275, 276, 
	277, 278, 279, 280, 281, 
};

typedef enum
{
    SD_TV_SRS_NONE,
    SD_TV_SRS_MUSIC,
    SD_TV_SRS_MOVIE,
    SD_TV_SRS_SPEECH,
    SD_TV_SRS_SILVER,
    SD_TV_SRS_CUSTOM,
    SD_TV_SRS_SOCCER_STADIUM,
    SD_TV_SRS_SOCCER_COMMENTATOR,
    SD_TV_SRS_SOCCER_AFRICA,
    SD_TV_SRS_MAX
} SdPostSoundTV_SRSMode_k;

SdPostSoundTV_SRSMode_k g_dnse_mode = SD_TV_SRS_NONE; // dnse mode
int g_dnse_cusv = 0; // SRS surround
int g_dnse_cusd = 0; // Dialog Clarity


typedef enum
{
    SD_TV_AV_OFF,
    SD_TV_AV_NORMAL,       ///< auto volume on
    SD_TV_AV_SRS_NIGHT,    ///< deep auto volume
    SD_TV_AV_MAX
} SdPostSoundTV_AutoVolMode_k;

typedef enum
{
    SD_TV_3DAUDIO_OFF,

    SD_TV_3DAUDIO_MIN_2D, 
    SD_TV_3DAUDIO_MID_2D,
    SD_TV_3DAUDIO_HIG_2D,

    SD_TV_3DAUDIO_MIN_3D, 
    SD_TV_3DAUDIO_MID_3D,
    SD_TV_3DAUDIO_HIG_3D,

    SD_TV_3DAUDIO_MAX

} SdPostSoundTV_3DAudio_k;

//static int dnse_value;//not used

static Ae_SoftVolume_s pstrtSoftVolume;
static Ae_PcmSoftVolume_s pstrtPcmSoftVolume;

static int spdif_bypass_mode = 0;
static int audio_mute_mode =0;
static int headphone_connect=0;


#ifdef CONFIG_SDP_SND_NTP7412S2
int ntp7412s2_audio_amplifier_init(struct snd_soc_pcm_runtime *runtime);
#endif /* CONFIG_SDP_SND_NTP7412S2 */

 typedef enum
 {

	SPEAKER_MUTE_ON,
	SPEAKER_MUTE_OFF,
	MONITOR_MUTE_ON,
	MONITOR_MUTE_OFF,
	SPDIFTX_MUTE_ON,
	SPDIFTX_MUTE_OFF,
	SCART_MON_MUTE_ON,
	SCART_MON_MUTE_OFF,
	SCART_RF_MUTE_ON,
	SCART_RF_MUTE_OFF,
	DUAL_TV_MUTE_ON,
	DUAL_TV_MUTE_OFF,
	SOUND_EFFECT_MUTE_ON,
	SOUND_EFFECT_MUTE_OFF,
	HEADPHONE_MUTE_ON,
	HEADPHONE_MUTE_OFF,
	MIX_MAIN_MUTE_ON,
	MIX_MAIN_MUTE_OFF,
	MIX_REMOTE_MUTE_ON,
	MIX_REMOTE_MUTE_OFF,
  	MIX_SUB0_MUTE_ON,
	MIX_SUB0_MUTE_OFF,
	MIX_SUB1_MUTE_ON,
	MIX_SUB1_MUTE_OFF,
	PCM_DUMP_MAIN_MUTE_ON,
	PCM_DUMP_MAIN_MUTE_OFF,
	PCM_DUMP_SUB_MUTE_ON,
	PCM_DUMP_SUB_MUTE_OFF,
	BT_DUMP_MAIN_MUTE_ON,
	BT_DUMP_MAIN_MUTE_OFF,	
  	BT_DUMP_SUB_MUTE_ON,
  	BT_DUMP_SUB_MUTE_OFF,

	AUDIO_OUT_MUTE_TYPE_MAX
 }
 Audio_OutMuteType_e;


static int out_hw_all_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "ALL MUTE OFF", "ALL MUTE ON"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_all_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_main_out_mute;
	return 0;
}

static int out_hw_all_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	g_main_out_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, value);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, value);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, value);
	
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}



static int out_hw_speaker_mute=0;
static int out_hw_speaker_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "SPEAKER MUTE OFF", "SPEAKER MUTE ON"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_speaker_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_speaker_mute;
	return 0;
}

static int out_hw_speaker_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_speaker_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}

static int out_hw_headphone_mute=0;
static int out_hw_headphone_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "HEADPHONE MUTE OFF", "HEADPHONE MUTE ON"
	};


	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_headphone_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_headphone_mute;
	return 0;
}

static int out_hw_headphone_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_headphone_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}


static int out_hw_spdif_mute=0;
static int out_hw_spdif_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "SPDIFTX MUTE OFF", "SPDIFTX MUTE ON"
	};


	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_spdif_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_spdif_mute;
	return 0;
}

static int out_hw_spdif_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_spdif_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}



static int out_hw_monitor_mute=0;
static int out_hw_monitor_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "MONITOR MUTE OFF", "MONITOR MUTE ON"
	};


	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_monitor_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_monitor_mute;
	return 0;
}

static int out_hw_monitor_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_monitor_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MONITOR, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}


static int out_hw_scart_mon_mute=0;
static int out_hw_scart_mon_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "SCART MON MUTE OFF", "SCART MON MUTE ON"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_scart_mon_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_scart_mon_mute;
	return 0;
}

static int out_hw_scart_mon_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_scart_mon_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_MON, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}

static int out_hw_scart_rf_mute=0;
static int out_hw_scart_rf_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 	 "SCART RF MUTE OFF", "SCART RF MUTE ON"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_hw_scart_rf_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = out_hw_scart_rf_mute;
	return 0;
}

static int out_hw_scart_rf_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	out_hw_scart_rf_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_RF, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	return 0;
}


static int out_audio_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[32] = {
		 "SPEAKER MUTE ON", "SPEAKER MUTE OFF", "MONITOR MUTE ON", "MONITOR MUTE OFF", "SPDIFTX MUTE ON","SPDIFTX MUTE OFF",  "SCART MON MUTE ON", "SCART MON MUTE OFF",  "SCART RF MUTE ON", "SCART RF MUTE OFF"
	 	,  "DUAL TV MUTE ON", "DUAL TV MUTE OFF",  "SOUND EFFECT MUTE ON", "SOUND EFFECT MUTE OFF",  "HEADPHONE MUTE ON", "HEADPHONE MUTE OFF",  "MIX MAIN MUTE ON", "MIX MAIN MUTE OFF",  "MIX REMOTE MUTE ON", "MIX REMOTE MUTE OFF"
	 	,  "MIX SUB0 MUTE ON", "MIX SUB0 MUTE OFF",  "MIX SUB1 MUTE ON", "MIX SUB1 MUTE OFF",  "PCM DUMP MAIN MUTE ON", "PCM DUMP MAIN MUTE OFF",  "PCM DUMP SUB MUTE ON", "PCM DUMP SUB MUTE OFF",  "BT DUMP MAIN MUTE ON", "BT DUMP MAIN MUTE OFF"	 	
	 	,  "BT DUMP SUB MUTE ON", "BT DUMP SUB MUTE OFF"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 32;
	if (uinfo->value.enumerated.item > 31)
		uinfo->value.enumerated.item = 31;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int out_audio_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = audio_mute_mode;

	return 0;
}

static int out_audio_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	

	audio_mute_mode = value;

	switch(audio_mute_mode)
	{

		case SPEAKER_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, AIO_MUTE);			
			break;
		case SPEAKER_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, AIO_UNMUTE);			
			break;			

		case MONITOR_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MONITOR, AIO_MUTE);			
			break;
		case MONITOR_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MONITOR, AIO_UNMUTE); 		
			break;			
			
		case SPDIFTX_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, AIO_MUTE);			
			break;
		case SPDIFTX_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, AIO_UNMUTE);			
			break;			
			
		case SCART_MON_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_MON, AIO_MUTE);			
			break;
		case SCART_MON_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_MON, AIO_UNMUTE); 		
			break;			

		case SCART_RF_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_RF, AIO_MUTE);			
			break;
		case SCART_RF_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_RF, AIO_UNMUTE); 		
			break;			

		case DUAL_TV_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_DUAL_TV, AIO_MUTE);			
			break;
		case DUAL_TV_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_DUAL_TV, AIO_UNMUTE); 		
			break;			

		case SOUND_EFFECT_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SOUND_EFFECT, AIO_MUTE);			
			break;
		case SOUND_EFFECT_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SOUND_EFFECT, AIO_UNMUTE); 		
			break;			

		case HEADPHONE_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, AIO_MUTE);			
			break;
		case HEADPHONE_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, AIO_UNMUTE); 		
			break;			

		case MIX_MAIN_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_MAIN, AIO_MUTE);			
			break;
		case MIX_MAIN_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_MAIN, AIO_UNMUTE); 		
			break;			

		case MIX_REMOTE_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_REMOTE, AIO_MUTE);			
			break;
		case MIX_REMOTE_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_REMOTE, AIO_UNMUTE); 		
			break;			

		case MIX_SUB0_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_SUB0, AIO_MUTE);			
			break;
		case MIX_SUB0_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_SUB0, AIO_UNMUTE); 		
			break;			

		case MIX_SUB1_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_SUB1, AIO_MUTE);			
			break;
		case MIX_SUB1_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MIX_SUB1, AIO_UNMUTE); 		
			break;			

		case PCM_DUMP_MAIN_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_PCM_DUMP_MAIN, AIO_MUTE);			
			break;
		case PCM_DUMP_MAIN_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_PCM_DUMP_MAIN, AIO_UNMUTE); 		
			break;			

		case PCM_DUMP_SUB_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_PCM_DUMP_SUB, AIO_MUTE);			
			break;
		case PCM_DUMP_SUB_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_PCM_DUMP_SUB, AIO_UNMUTE); 		
			break;			

		case BT_DUMP_MAIN_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_BT_DUMP_MAIN, AIO_MUTE);			
			break;
		case BT_DUMP_MAIN_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_BT_DUMP_MAIN, AIO_UNMUTE); 		
			break;			

		case BT_DUMP_SUB_MUTE_ON:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_BT_DUMP_SUB, AIO_MUTE);			
			break;
		case BT_DUMP_SUB_MUTE_OFF:
			dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_BT_DUMP_SUB, AIO_UNMUTE); 		
			break;			

		default:
			break;	
		

	}		
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 

	return 0;
}







static int in_sample_rate_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = h_dec_pcm_state_info.enumSmplFreq;
	return 0;
}

static int in_sample_rate_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}




static int headphone_connect_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
	 "HEADPHONE DISCONNECT","HEADPHONE CONNECT"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int headphone_connect_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = headphone_connect;

	return 0;
}

static int headphone_connect_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	int value = ucontrol->value.enumerated.item[0];	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: headphone_connect %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	headphone_connect = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	
	if(value == 1) //CONNECT
		devAe_VRegSet(pAeHndl->pDevAeHndl, AE_HEADPHONE_CONNECT_SET,  AUDIO_ENABLE);
	else if(value == 0)//DISCONNECT
		devAe_VRegSet(pAeHndl->pDevAeHndl, AE_HEADPHONE_CONNECT_SET,  AUDIO_DISABLE);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}






static int spdif_bypass_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[5] = {
	 "NEO ES DTS", "ES DTS", "ES DolbyDigital", "PCM", "ES AAC"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item > 4)
		uinfo->value.enumerated.item = 4;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int spdif_bypass_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = spdif_bypass_mode;

	return 0;
}

static int spdif_bypass_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.enumerated.item[0];
	//pspIAeData_t pAeHndl;//not used

	static Aio_SpdOutParam_s spdParam;
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	

	spdif_bypass_mode = value;

	if(spdif_bypass_mode == SPDIF_NEO_ES_DTS)
	{//bypass NEO ES
		dev_audio_aio_stopspdifout(m_audio_hndl.p_aio_hndl, 0);

		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_NEO_ES;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_DTS;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}	
	else if(spdif_bypass_mode == SPDIF_ES_DTS)
	{//bypass DTS
		dev_audio_aio_stopspdifout(m_audio_hndl.p_aio_hndl, 0);

		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_ES;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_DTS;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}
	else if(spdif_bypass_mode == SPDIF_ES_AAC)
	{//bypass DTS
		dev_audio_aio_stopspdifout(m_audio_hndl.p_aio_hndl, 0);

		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_AAC;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_AAC;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}	
	else if(spdif_bypass_mode == SPDIF_ES_DD)
	{//bypass DD
		dev_audio_aio_stopspdifout(m_audio_hndl.p_aio_hndl, 0);

		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_ES;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF_ES;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_AC3;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}	
	else if(spdif_bypass_mode == SPDIF_PCM)
	{ //pcm
		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_PCM;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}
	else	
	{ //default pcm
		dev_audio_aio_getspdifoutparam(m_audio_hndl.p_aio_hndl, 0, &spdParam);
		spdParam.enumBitLen = AIO_24BIT;
		spdParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		spdParam.enumClkScale = AIO_256FS;
		spdParam.enumSmplFreq = AUDIO_48KHZ;
		spdParam.enumPcmEs = AIO_PCMES_PCM;
		spdParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		spdParam.enumDualType = AUDIO_L_R;
		spdParam.enumDevOn = AUDIO_ENABLE;
		spdParam.enumDataFormat = AUDIO_LPCM;
		dev_audio_aio_startspdifout(m_audio_hndl.p_aio_hndl, 0, &spdParam);
	}		
		
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 

	return 0;
}


static int out_speaker_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
//static int main_gain_get(struct snd_kcontrol *kcontrol,
//			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_speaker_out_gain;
//	pr_debug("main_gain_get, gain[%d]\n", main_gain);
	return 0;
}

static int out_speaker_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
//static int main_gain_put(struct snd_kcontrol *kcontrol,
//			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	g_speaker_out_gain = value;
	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	pstrtSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &pstrtSoftVolume );
//	pr_debug("main_gain_put, gain table[%d] = %d\n", value, pstrtSoftVolume.u16VolumeIndex);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}

static int pcm_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_auipcm_mix_gain;
//	pr_debug("g_auipcm_mix_gain_get, gain[%d]\n", g_auipcm_mix_gain);
	return 0;
}

int sdp_audio_get_pcm_gain(void)
{
	return gu32VolumeTable_GL[g_auipcm_mix_gain];
}

static int pcm_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	g_auipcm_mix_gain = value;
	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	pstrtPcmSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[g_auipcm_mix_gain];;

	devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume);

//	pr_debug("pcm_gain_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}

static int main_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
//static int out_speaker_gain_get(struct snd_kcontrol *kcontrol,
//			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_main_out_gain;
//	pr_debug("g_speaker_out_gain_get, gain[%d]\n", g_speaker_out_gain);
	return 0;
}

//static int out_speaker_gain_put(struct snd_kcontrol *kcontrol,
//			    struct snd_ctl_elem_value *ucontrol)
static int main_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	g_main_out_gain = value;

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	u16Gain = u16Gain /8;
	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SPEAKER, (unsigned short)u16Gain);
//	pr_debug("g_speaker_out_gain_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}


#define AUDIO_CHECK_RANGE(var, max) \
{ \
	if ((unsigned int)(var) >= (max)) \
	{ 	\
		pr_err("gain value err\n"); \
		return AUDIO_ERR; \
	} \
}
const unsigned short	AUD_SCALEOUT_MAX =(500);	


static int dec0_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = dec0_out_gain;
	return 0;
}

static int dec0_out_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	short value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	short Gain;
	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);
	dec0_out_gain = value;
	dev_audio_ae_open(AE_INST0, &(m_audio_hndl.p_ae0_hndl), AUDIO_CHIP_ID_MP0);	
//	value = value*(-1);
	AUDIO_CHECK_RANGE(value, AUD_SCALEOUT_MAX + 1);	
	value = (value *16) /10;	
	Gain = value/8;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae0_hndl;
	devAe_SetScaleDecOut(pAeHndl->pDevAeHndl, Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl); 

	return 0;
}


static int dec1_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = dec1_out_gain;
	return 0;
}

static int dec1_out_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	short value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	short Gain;
	
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);
	dec1_out_gain = value;
	dev_audio_ae_open(AE_INST1, &(m_audio_hndl.p_ae1_hndl), AUDIO_CHIP_ID_MP0);	
//	value = value*(-1);
	AUDIO_CHECK_RANGE(value, AUD_SCALEOUT_MAX + 1);	
	value = (value *16) /10;	
	Gain = value/8;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae1_hndl;
	devAe_SetScaleDecOut(pAeHndl->pDevAeHndl, Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae1_hndl); 

	return 0;
}


static int out_scart_monitor_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = out_scart_monitor_gain;
//	pr_debug("out_scart_monitor_gain_get, gain[%d]\n", out_scart_monitor_gain);
	return 0;
}

static int out_scart_monitor_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;	

	out_scart_monitor_gain = value;	
	u16Gain = u16Gain/8;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SCART_MON, (unsigned short)u16Gain);


//	pr_debug("out_scart_monitor_gain_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}


static int  out_dual_tv_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = out_dual_tv_gain;
//	pr_debug("out_dual_tv_gain_get, gain[%d]\n", out_dual_tv_gain);
	return 0;
}

static int  out_dual_tv_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;		
	out_dual_tv_gain = value;	
	u16Gain = u16Gain/8;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_DUAL_TV, (unsigned short)u16Gain);


//	pr_debug("out_dual_tv_gain_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}


static int out_headphone_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = out_headphone_gain;
//	pr_debug("out_headphone_gain_get, gain[%d]\n", out_headphone_gain);
	return 0;
}

static int out_headphone_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	out_headphone_gain = value;	

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;		

	u16Gain = u16Gain/8;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_HEADPHONE, (unsigned short)u16Gain);

//	pr_debug("out_headphone_gain_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}


static int  dtv_installation_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	 struct soc_mixer_control *mc = NULL;
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	unsigned int reg;
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: dtv_installation %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	mc = (struct soc_mixer_control *)kcontrol->private_value;

	reg = mc->reg;


	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	devAe_SetDTVInstallation(pAeHndl->pDevAeHndl, reg /*Ae_DTVInstallationType_e*/,value/* Ae_InstallationFilterType_e*/ );

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}


static int  dtv_installation_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int  out_spdiftx_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	out_spdiftx_gain = value;	

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;		

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SPDIFTX, (unsigned short)u16Gain);


//	pr_debug("out_spdiftx_put, gain[%d]\n", value);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}

static int  out_spdiftx_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = out_spdiftx_gain;
//	pr_debug("out_spdiftx_get, gain[%d]\n", out_spdiftx_gain);
	return 0;
}


static int dnse_geq_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	mc = (struct soc_mixer_control *)kcontrol->private_value;
	reg = mc->reg;

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	ucontrol->value.integer.value[0] = sDNSe.geqUsrG[reg] + 10;

	return 0;
}

static int dnse_geq_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	mc =(struct soc_mixer_control *)kcontrol->private_value;

	reg = mc->reg;
	
	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);
	
	/*
		SRS Aep 사용,-10dB ~ + 10dB 범위만 사용함 (UI : 20Step)
	*/
	//reg0 Band100Hz
	//reg1 Band300Hz		
	//reg2 Band1KHz
	//reg3 Band3KHz  	
	sDNSe.cDNSe = DNSe_CUSTM;
	sDNSe.cGEQM = DNSe_ON;
	sDNSe.geqUsrG[reg] =  ucontrol->value.integer.value[0] -10 ; 

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);	

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

//	pr_debug("dnse_geq_put, Band reg[%d]=[%d]\n", reg, ucontrol->value.integer.value[0]);

	return 0;
}

static const int16_t gs16BalanceTable[] = 
{
     0xffCE, 0xffe4, 0xffe8, 0xffec, 0xfff0, 0xfff4, 0xfff7, 0xfffa, 0xfffc, 0xfffe,
     0x00, 0x02, 0x04, 0x06, 0x09, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x32
};
const unsigned int	BALANCE_RANGE	= 21;

static int g_balance=10;
static int dnse_balance_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_balance;
	return 0;
}

static int dnse_balance_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	//Ae_DNSe_t sDNSe;//not used
	pspIAeData_t pAeHndl;
	Ae_Balance_s strtBalance;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	if (ucontrol->value.integer.value[0] >= BALANCE_RANGE)
	{
		pr_err("[%s] Error: balance out of range.\n", __FUNCTION__);
		return -1;
	}

	// set balance(0:right mute - 20:left mute)
	g_balance = ucontrol->value.integer.value[0];
	strtBalance.sBal = gs16BalanceTable[g_balance];
	strtBalance.enumScaleMode = AE_LOG_SCALE;
	dev_audio_ae_setbalance(m_audio_hndl.p_ae2_hndl, &strtBalance);


	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	//pr_debug("dnse_balance_put, Balance value[%d]\n", ucontrol->value.integer.value[0]);

	return 0;
}


static int g_dnse_out_delay = 0;
static int dnse_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_dnse_out_delay;
	return 0;
}

static int dnse_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_dnse_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_SPEAKER, ucontrol->value.integer.value[0]);
#if 0
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_MONITOR, ucontrol->value.integer.value[0]);
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_SCART_MON, ucontrol->value.integer.value[0]);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 0, AUDIO_OUT_SCART_RF, ucontrol->value.integer.value[0]);
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 2, AUDIO_OUT_DUAL_TV, ucontrol->value.integer.value[0]);
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 3, AUDIO_OUT_SOUND_EFFECT, ucontrol->value.integer.value[0]);
#endif 
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 


	return 0;
}


static int g_speaker_out_delay = 0;
static int speaker_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_speaker_out_delay;
	return 0;
}

static int speaker_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_speaker_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_SPEAKER, ucontrol->value.integer.value[0]);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}

static int g_monitor_out_delay = 0;
static int monitor_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_monitor_out_delay;
	return 0;
}

static int monitor_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_monitor_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_MONITOR, ucontrol->value.integer.value[0]);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}

static int g_scart_mon_out_delay = 0;
static int scart_mon_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_scart_mon_out_delay;
	return 0;
}

static int scart_mon_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_scart_mon_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_SCART_MON, ucontrol->value.integer.value[0]);	
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}

static int g_scart_rf_out_delay = 0;
static int scart_rf_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_scart_rf_out_delay;
	return 0;
}

static int scart_rf_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_scart_rf_out_delay = ucontrol->value.integer.value[0];

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 0, AUDIO_OUT_SCART_RF, ucontrol->value.integer.value[0]);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}

static int g_dual_tv_out_delay = 0;
static int dual_tv_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_dual_tv_out_delay;
	return 0;
}

static int dual_tv_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_dual_tv_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 2, AUDIO_OUT_DUAL_TV, ucontrol->value.integer.value[0]);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 

	return 0;
}

static int g_sound_effect_out_delay = 0;
static int sound_effect_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_sound_effect_out_delay;
	return 0;
}

static int sound_effect_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_sound_effect_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 3, AUDIO_OUT_SOUND_EFFECT, ucontrol->value.integer.value[0]);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}

static int g_spdif_out_delay = 0;
static int spdiftx_delay_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_spdif_out_delay;
	return 0;
}

static int spdiftx_delay_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: delay %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_spdif_out_delay = ucontrol->value.integer.value[0];
	
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);	
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_SPDIF_TX, 0, AIO_SPDIF_TX, g_spdif_out_delay);		
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl); 
	
	return 0;
}


static int g_stereo_mode_for_main = 0;

static int stereo_mode_for_main_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {
	 "stereo", "left only" , "right only", "left,right mix"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int stereo_mode_for_main_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_stereo_mode_for_main;
	return 0;
}

static int stereo_mode_for_main_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	g_stereo_mode_for_main =  ucontrol->value.enumerated.item[0];

	pspIAeData_t pAeHndl;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetDualChannel(pAeHndl->pDevAeHndl, g_stereo_mode_for_main);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 
	return 0;
}




static int dnse_auto_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {
	 "av_off", "av_normal", "av_srs_night"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int dnse_auto_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl,	&sDNSe);
	ucontrol->value.enumerated.item[0] = sDNSe.cSVOL;

	return 0;
}

static int dnse_auto_vol_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;

	Ae_SoftVolume_s  strtSoftVolmueTmp, strtSoftVolmue;
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	


	strtSoftVolmueTmp.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	strtSoftVolmue.u16VolumeIndex = 0;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

//KYUNG 	if ( ucontrol->value.integer.value[0]/*eAutoVolume*/ == SD_TV_AV_OFF )
	if (ucontrol->value.enumerated.item[0]/*eAutoVolume*/ == SD_TV_AV_OFF )
	{
		sDNSe.cSVOL = SVOL_NONE;
	}
//KYUNG 	else if ( SD_TV_AV_NORMAL == ucontrol->value.integer.value[0] )
	else if ( ucontrol->value.enumerated.item[0] == SD_TV_AV_NORMAL)
	{
		sDNSe.cSVOL = SVOL_NORM;
	}
//KYUNG 	else if ( SD_TV_AV_SRS_NIGHT == ucontrol->value.integer.value[0] )
	else if (ucontrol->value.enumerated.item[0] ==  SD_TV_AV_SRS_NIGHT)
	{
		sDNSe.cSVOL = SVOL_NIGH;
	}
	else
	{ 
		pr_debug("Auto Volume type is Max\n");
	}		

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);	



	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmueTmp );


	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

//	pr_debug("dnse_auto_vol_put, Auto Volume[%d]\n", ucontrol->value.integer.value[0]);

	return 0;
}


static int dnse_srs_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	#define sound_mode_mode_num_items 7
	static char *texts[sound_mode_mode_num_items] = {
		"none", "music", "movie", "speech", "silver", "soccer_stadium", "soccer_commentator"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = sound_mode_mode_num_items;
	if (uinfo->value.enumerated.item >= sound_mode_mode_num_items)
		uinfo->value.enumerated.item = sound_mode_mode_num_items-1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int dnse_srs_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;	
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	Ae_SoftVolume_s  strtSoftVolmueTmp, strtSoftVolmue;

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	

	mc =(struct soc_mixer_control *)kcontrol->private_value;

	if(mc){
		reg = mc->reg;
	}else{
		reg = 0;
	}

	strtSoftVolmueTmp.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	strtSoftVolmue.u16VolumeIndex = 0;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );


	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	if(reg == 0)
	{
		g_dnse_mode = ucontrol->value.enumerated.item[0];

		t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s (sound mode / dnse mode): enum %d [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);

		switch(g_dnse_mode){
		case SD_TV_SRS_NONE:               sDNSe.cDNSe = DNSe_NONEM; break;			
		case SD_TV_SRS_MOVIE:              sDNSe.cDNSe = DNSe_MOVIE; break;
		case SD_TV_SRS_MUSIC:              sDNSe.cDNSe = DNSe_MUSIC; break;
		case SD_TV_SRS_SPEECH:             sDNSe.cDNSe = DNSe_SPECH; break;
		case SD_TV_SRS_SILVER:             sDNSe.cDNSe = DNSe_SILVE; break;
		case SD_TV_SRS_SOCCER_STADIUM:     sDNSe.cDNSe = DNSe_STADM; break;
		case SD_TV_SRS_SOCCER_COMMENTATOR: sDNSe.cDNSe = DNSe_COMNT; break;
		default:
			{
				pr_debug("SRS : Not Support Mode (%ld)\n", ucontrol->value.integer.value[0]/*sSRSParam.eSRSMode*/);
				strtSoftVolmue.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];

				pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
				devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );

				return -1;
			}
		}
		dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);	
	//	pr_debug("dnse_srs_put, SRS Mode[%d]\n", ucontrol->value.integer.value[0]);		

	}
	else if(reg == 1)
	{
		sDNSe.cDNSe = DNSe_CUSTM;
		g_dnse_cusv = ucontrol->value.integer.value[0];

		t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s dnse: value %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	 	sDNSe.cCUSV = (Ae_CntrButtonCmd) ucontrol->value.integer.value[0]/*sSRSParam.bSRSSurround*/;
		dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);	
//		pr_debug("dnse_srs_put, SRS Surround[%d]\n", ucontrol->value.integer.value[0]);		

	}	
	else if(reg == 2)
	{
		sDNSe.cDNSe = DNSe_CUSTM;
		g_dnse_cusd = ucontrol->value.integer.value[0];

		t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s dnsd: value %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

		sDNSe.cCUSD = (Ae_CntrButtonCmd) ucontrol->value.integer.value[0]/*sSRSParam.bDialogClarity*/;
		dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);
//		pr_debug("dnse_srs_put, Dialog Clarity[%d]\n", ucontrol->value.integer.value[0]);		
	}

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmueTmp );

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 

	return 0;
}

static int dnse_srs_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;

	mc =(struct soc_mixer_control *)kcontrol->private_value;
	reg = mc->reg;

	switch(reg){
	case 0: ucontrol->value.enumerated.item[0] = g_dnse_mode; break;
	case 1: ucontrol->value.integer.value[0] = g_dnse_cusv; break;
	case 2: ucontrol->value.integer.value[0] = g_dnse_cusd; break;
	}

	return 0;
}



static int dnse_3d_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int dnse_3d_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;	

	Ae_SoftVolume_s  strtSoftVolmueTmp, strtSoftVolmue;
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	


	strtSoftVolmueTmp.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	strtSoftVolmue.u16VolumeIndex = 0;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );


	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	switch(ucontrol->value.integer.value[0]/*eMode3DAudio*/)
	{
		case SD_TV_3DAUDIO_MIN_3D:
			sDNSe.cDPTH = DEPTH_MMIN;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_MID_3D:
			sDNSe.cDPTH = DEPTH_MMID;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_HIG_3D:
			sDNSe.cDPTH = DEPTH_MMAX;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_MIN_2D:
			sDNSe.cDPTH = DEPTH_MMIN;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		case SD_TV_3DAUDIO_MID_2D:
			sDNSe.cDPTH = DEPTH_MMID;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		case SD_TV_3DAUDIO_HIG_2D:
			sDNSe.cDPTH = DEPTH_MMAX;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		default :
			sDNSe.cDPTH = DEPTH_NONE;
			sDNSe.sRefInd = CONTENTS3D;
			break;
	}
	

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);	
	

	
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmueTmp );


	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 
	
//	pr_debug("dnse_3d_put, 3D Audio[%d]\n", ucontrol->value.integer.value[0]);


	return 0;
}

static int dnse_personal_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	mc = (struct soc_mixer_control *)kcontrol->private_value;
	reg = mc->reg;

	//t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: reg %d, value %ld [pid:%d,%s]\n", __FUNCTION__, reg, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	switch(reg){
	case 0:	ucontrol->value.integer.value[0] = sDNSe.cDHAP; break;
	case 1:	ucontrol->value.integer.value[0] = sDNSe.pta_level[0]; break;
	case 2:	ucontrol->value.integer.value[0] = sDNSe.pta_level[1]; break;
	case 3:	ucontrol->value.integer.value[0] = sDNSe.pta_level[2]; break;
	case 4:	ucontrol->value.integer.value[0] = sDNSe.pta_level[3]; break;
	case 5:	ucontrol->value.integer.value[0] = sDNSe.pta_level[4]; break;
	case 6:	ucontrol->value.integer.value[0] = sDNSe.pta_level[5]; break;
	}
	return 0;
}

static int dnse_personal_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	//int i=0;//not used

	mc = (struct soc_mixer_control *)kcontrol->private_value;

	reg = mc->reg;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: reg %d, enum %ld [pid:%d,%s]\n", __FUNCTION__, reg, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	if(reg ==0)
	{
		sDNSe.cDHAP = (ucontrol->value.integer.value[0]/*bOnOff*/==TRUE)?DNSe_ON : DNSe_OFF;	
		pr_debug("dnse_personal_put, On/Off[%ld]\n", ucontrol->value.integer.value[0]);
	}
	else if(reg ==1)
	{
		sDNSe.pta_level[0] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}
	else if(reg ==2)
	{
		sDNSe.pta_level[1] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}
	else if(reg ==3)
	{
		sDNSe.pta_level[2] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}
	else if(reg ==4)
	{
		sDNSe.pta_level[3] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}
	else if(reg ==5)
	{
		sDNSe.pta_level[4] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}
	else if(reg ==6)
	{
		sDNSe.pta_level[5] = ucontrol->value.integer.value[0]; // sPersonalSetting.pta_level[i];
		pr_debug("dnse_personal_put, pts level[%d]=%ld \n",reg,  ucontrol->value.integer.value[0]);
	}		
	
	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);		

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl); 


	return 0;
}

/*** carrier mute ***/
#ifndef ALSA_SIF_INTEGRATION
devSifHndl_t hSif;

static int carrier_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int carrier_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Enable_e MuteOnOff;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetFMCarrierMuteEn(hSif, &MuteOnOff);
	ucontrol->value.integer.value[0] = MuteOnOff;
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, MuteOnOff, current->pid, current->comm);

	return 0;
}

static int carrier_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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
static int high_deviation_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int high_deviation_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Sif_Enable_e eDeviation;

	devSif_Open(SIF_CHIP_0, &hSif);
	devSif_GetHiDeviation(hSif, &eDeviation);
	ucontrol->value.integer.value[0] = eDeviation ;
	devSif_Close(hSif);

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, eDeviation, current->pid, current->comm);

	return 0;
}

static int high_deviation_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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
static int pilot_highlow_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xFFFF;
	return 0;
}

static int  pilot_highlow_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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

static int  pilot_highlow_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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
static int mts_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int mts_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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
static int mts_out_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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
static int mts_out_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_mts_out_mode;
	return 0;
}
static int mts_out_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	SdAnalogMtsMode_k eSdMtsMode;

	devSif_Open(SIF_CHIP_0, &hSif);
	eSdMtsMode = ucontrol->value.enumerated.item[0];
	devSif_SdSetMtsOutMode(hSif, eSdMtsMode);
	devSif_Close(hSif);

	g_mts_out_mode = eSdMtsMode;
	return 0;
}
#endif
 
/***lip sync usb test***/
static unsigned int main_speaker_delay;
static unsigned int sound_share_delay;
static int lipsync_test_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 225;
	return 0;
}
static int lipsync_test_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = main_speaker_delay;
	ucontrol->value.integer.value[1] = sound_share_delay;
	return 0;
}

static int lipsync_test_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	pspIAeData_t pAeHndl;
	Ae_ParamCommand_e enumCMD;

	main_speaker_delay = ucontrol->value.integer.value[0];
	sound_share_delay = ucontrol->value.integer.value[1];
	enumCMD = ucontrol->value.integer.value[2];

	/* set main speaker delay */
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	dev_audio_aio_setoutdelay(m_audio_hndl.p_aio_hndl, AIO_I2S_TX, 1, AUDIO_OUT_SPEAKER, main_speaker_delay);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);

	/* set sound shared delay */
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_VRegSet(pAeHndl->pDevAeHndl, enumCMD, sound_share_delay);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return ret;
}

/***start loop back test***/
#define true 1
#define false 0
#define NOISE_THRESHOLD 400000
static unsigned int s32minleftlevel;
static unsigned int s32maxleftlevel;
static unsigned int s32minrightlevel;
static unsigned int s32maxrightlevel;
int thld_l = 0, thld_r = 0;
int piresult = 0;

static int loopback_test_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 28;
	return 0;
}
static int loopback_test_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{

	ucontrol->value.integer.value[0] = piresult;
	ucontrol->value.integer.value[1] = thld_l;
	ucontrol->value.integer.value[2] = thld_r;

	return 0;
}
static int loopback_test_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;

	spIAioHndl_t spIHndl = NULL;
	pspIAeData_t pAeHndl = NULL;
	devAeHndl_t devHndl;
	Ae_LoopbackMode_e eloopbackmode;
	Ae_LevelMeter_t pstrlevelmeter;
	Aio_I2sOutParam_s sI2sOutParam;
	unsigned int devnum;

	eloopbackmode = ucontrol->value.integer.value[0];
	thld_l = ucontrol->value.integer.value[1];
	thld_r = ucontrol->value.integer.value[2];

	s32minleftlevel = (unsigned int)(thld_l*80000);
	s32minrightlevel =(unsigned int)(thld_r*80000);
	s32maxleftlevel = (unsigned int)(thld_l*120000);
	s32maxrightlevel = (unsigned int)(thld_r*120000);

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	spIHndl = m_audio_hndl.p_aio_hndl;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	dev_audio_ae_setport(m_audio_hndl.p_ae2_hndl, AE_PORT_SCARTRF, AE_SRC_DTV_DEC0, 0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devHndl = pAeHndl->pDevAeHndl;

	/* start loop back */
	if ((eloopbackmode == AE_LOOPBACK_AV1) || (eloopbackmode == AE_LOOPBACK_AV2) || (eloopbackmode == AE_LOOPBACK_COMP))
	{
		devnum = I2S_TX1_DEV_NUM;
		dev_audio_aio_geti2soutparam(spIHndl, 1, &sI2sOutParam);
		sI2sOutParam.enumOutputMemMap = AUDIO_OUT_SPK_MON;
		sI2sOutParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		dev_audio_aio_stopi2sout(spIHndl, 1);
		dev_audio_aio_starti2sout(spIHndl, 1, &sI2sOutParam);
		dev_audio_aio_mute(spIHndl, AUDIO_OUT_SCART_RF, AIO_UNMUTE);
		dev_audio_aio_mute(spIHndl, AUDIO_OUT_MONITOR, AIO_UNMUTE);
		dev_audio_aio_mute(spIHndl, AUDIO_OUT_SPEAKER, AIO_UNMUTE);

		pstrlevelmeter.eLoopbackLevelMeterEn = AUDIO_ENABLE;
		devAe_SetLoopbackLevelMeter(devHndl, eloopbackmode, &pstrlevelmeter);
	} else if (eloopbackmode == AE_LOOPBACK_SCART) {

		/* FIXME: Replaced spIAudDec_SetAnalogInputSwitch(0, AUDDEC_INPUTMUX_2)
			  Function With Below Function.
		*/
		snd_hw_writel(0x19A14044, 1);

		dev_audio_aio_geti2soutparam(spIHndl, 0, &sI2sOutParam);
		sI2sOutParam.enumOutputMemMap = AUDIO_OUT_SCARTRF;
		sI2sOutParam.enumClkSource = AIO_CLK_SOURCE_INTERNAL0;
		dev_audio_aio_stopi2sout(spIHndl, 0);
		dev_audio_aio_starti2sout(spIHndl, 0, &sI2sOutParam);
		dev_audio_aio_mute(spIHndl,AUDIO_OUT_SCART_RF, AIO_UNMUTE);

		pstrlevelmeter.eLoopbackLevelMeterEn = AUDIO_ENABLE;
		devAe_SetLoopbackLevelMeter(devHndl, eloopbackmode, &pstrlevelmeter);
		dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	} else if (eloopbackmode == AE_LOOPBACK_SPDIF) {
		Aio_SpdOutParam_s sSpdOutParam;
		Aio_SpdInParam_s spdInParam;

		dev_audio_aio_stopspdifout(spIHndl, 0);
		dev_audio_aio_stopspdifin(spIHndl, 0);

		dev_audio_aio_getspdifoutparam(spIHndl, 0, &sSpdOutParam);
		sSpdOutParam.enumOutputMemMap = AUDIO_OUT_SPDIF;
		sSpdOutParam.enumDataFormat = AUDIO_LPCM;
		sSpdOutParam.enumClkScale = AIO_256FS;
		sSpdOutParam.enumPcmEs = AIO_PCMES_PCM;
		dev_audio_aio_startspdifout(spIHndl, 0, &sSpdOutParam);

		/*FIXME: Replaced spISys_WriteModReg32(REG_MP0_AIO,0x186410b0, 0x5000)
			 Function With Below Function.
		*/
		snd_hw_writel(0x186410b0, 0x5000);

		dev_audio_aio_getspdifinparam(spIHndl, 0,&spdInParam);
		spdInParam.enumBitLen= AIO_24BIT;
		spdInParam.enumPcmEs = AIO_PCMES_PCM;
		spdInParam.enumInputMemMap = AUDIO_IN_SPD_RX;
		dev_audio_aio_startspdifin(spIHndl, 0, &spdInParam);
		dev_audio_aio_mute(spIHndl,AUDIO_OUT_SPDIFTX, AIO_UNMUTE);
		msleep(200);

		pstrlevelmeter.eLoopbackLevelMeterEn = AUDIO_ENABLE;
		devAe_SetLoopbackLevelMeter(devHndl, eloopbackmode, &pstrlevelmeter);
	}

	msleep(1800);

	/*Measure loop back */
	devAe_GetLoopbackLevelMeter(devHndl, eloopbackmode, &pstrlevelmeter);

	if(eloopbackmode == AE_LOOPBACK_SPDIF) {
		if(pstrlevelmeter.u32Left1KHzLevel != 0) {
			if ((s32minleftlevel < pstrlevelmeter.u32Left1KHzLevel) && (pstrlevelmeter.u32Left1KHzLevel < s32maxleftlevel) &&
				(s32minrightlevel < pstrlevelmeter.u32Right400HzLevel) && (pstrlevelmeter.u32Right400HzLevel < s32maxrightlevel))
			{
				piresult = true;
			} else {
				printk("Left 1KHz %d, noise %d\n", pstrlevelmeter.u32Left1KHzLevel, pstrlevelmeter.u32LeftNoiseLevel);
				printk("Right 1KHz %d, noise %d\n", pstrlevelmeter.u32Right400HzLevel, pstrlevelmeter.u32RightNoiseLevel);
				piresult = false;
			}
		} else {
			if(pstrlevelmeter.bSpdLoopbackTestRes == 1) {
				piresult = true;
			} else {
				piresult = false;
			}
		}
	} else {
		printk("LEVEL METER Result\n");
		printk("Left 1KHz %d, noise %d\n", pstrlevelmeter.u32Left1KHzLevel, pstrlevelmeter.u32LeftNoiseLevel);
		printk("Right 1KHz %d, noise %d\n", pstrlevelmeter.u32Right400HzLevel, pstrlevelmeter.u32RightNoiseLevel);
		printk("MIN Left 1KHz %d, MAX Left %d\n", s32minleftlevel, s32maxleftlevel);
		printk("MIN Right 1KHz %d, MAX Right %d\n", s32minrightlevel, s32maxrightlevel);
	
		if ((s32minleftlevel < pstrlevelmeter.u32Left1KHzLevel) && (pstrlevelmeter.u32Left1KHzLevel < s32maxleftlevel) &&
			(s32minrightlevel <  pstrlevelmeter.u32Right400HzLevel) && (pstrlevelmeter.u32Right400HzLevel < s32maxrightlevel) &&
			(pstrlevelmeter.u32LeftNoiseLevel < NOISE_THRESHOLD) && (pstrlevelmeter.u32RightNoiseLevel < NOISE_THRESHOLD))
		{
			piresult = true;
		} else {
			printk("L : %d, R : %d\n", pstrlevelmeter.u32Left1KHzLevel, pstrlevelmeter.u32Right400HzLevel);
			piresult = false;
		}
	}
	thld_l= pstrlevelmeter.u32Left1KHzLevel/100000;
	thld_r = pstrlevelmeter.u32Right400HzLevel/100000;

	/*stop loopback */
	if((eloopbackmode== AE_LOOPBACK_AV1)||(eloopbackmode == AE_LOOPBACK_AV2) ||(eloopbackmode == AE_LOOPBACK_COMP))
	{
		devnum = I2S_TX1_DEV_NUM;
	} else if (eloopbackmode == AE_LOOPBACK_SCART) {
		devnum = I2S_TX0_DEV_NUM;
	} else if (eloopbackmode == AE_LOOPBACK_SPDIF) {
		devnum = SPDIF_TX0_DEV_NUM;
	}

	if (eloopbackmode == AE_LOOPBACK_SPDIF) {
		dev_audio_aio_stopspdifout(spIHndl, devnum);
	} else {
		dev_audio_aio_stopi2sout(spIHndl, devnum);
	}
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);

	pstrlevelmeter.eLoopbackLevelMeterEn = AUDIO_DISABLE;
	devAe_SetLoopbackLevelMeter(devHndl, eloopbackmode, &pstrlevelmeter);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return ret;
}

static int mpeg_stereo_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	#define mpeg_stereo_mode_num_items 4
	static char *texts[mpeg_stereo_mode_num_items] = {
	 "stereo", "joint_stereo", "dual_channel", "single_channel"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = mpeg_stereo_mode_num_items;
	if (uinfo->value.enumerated.item >= mpeg_stereo_mode_num_items)
		uinfo->value.enumerated.item = mpeg_stereo_mode_num_items-1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int mpeg_stereo_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = h_dec_pcm_state_info.enumMpegStereoMode;
	return 0;
}


#if 0
static const struct snd_kcontrol_new audio_info_lock[] = {
	SOC_SINGLE_EXT("audio info lock", 0, 0, 256, 0,
		       audio_status_info_lock_get, audio_status_info_lock_put),
};
#endif 

#if 0
static const struct snd_kcontrol_new echo_onoff[] = {
	SOC_SINGLE_EXT("echo onoff", 0, 0, 256, 0,
		       echo_onoff_get, echo_onoff_put),
};
#endif 

static const struct snd_kcontrol_new main_in_buffer_level[] = {
	SOC_SINGLE_EXT("main buf level", 0, 0, 256, 0,
		       main_in_buffer_level_get, main_in_buffer_level_put),
};

static const struct snd_kcontrol_new sub_in_buffer_level[] = {
	SOC_SINGLE_EXT("sub buf level", 0, 0, 256, 0,
		       sub_in_buffer_level_get, sub_in_buffer_level_put),
};


static const struct snd_kcontrol_new is_be_connected_main[] = {
	SOC_SINGLE_EXT("connction main", 0, 0, 256, 0,
		       is_be_connected_main_get, is_be_connected_main_put),
};

#if 0
static const struct snd_kcontrol_new spdiftx_es[] = {
	SOC_SINGLE_EXT("spdiftx mode", 0, 0, 256, 0,
		       is_spdiftx_es_get, is_spdiftx_es_put),
};
#endif 

static const struct snd_kcontrol_new i2srx_master[] = {
	SOC_SINGLE_EXT("i2srx master", 0, 0, 256, 0,
		       is_master_get, is_master_put),
};


static const struct snd_kcontrol_new remote_clone[] = {
	SOC_SINGLE_EXT("remote clon", 0, 0, 256, 0,
		       is_remote_clone_get, is_remote_clone_put),
};

static const struct snd_kcontrol_new remote_mode[] = {
	SOC_SINGLE_EXT("remote mode", 0, 0, 256, 0,
		       is_remote_mode_get, is_remote_mode_put),
};

static const struct snd_kcontrol_new in_sample_rate[] = {
	SOC_SINGLE_EXT("sample rate", 0, 0, 65535, 0,
		       in_sample_rate_get, NULL),
};


static const struct snd_kcontrol_new dtv_installation[] = {
	SOC_SINGLE_EXT("dtv installation", 0, 0, 256, 0,
		       dtv_installation_get, dtv_installation_put),
};

static const struct snd_kcontrol_new dnse_geq_controls[] = {
	SOC_SINGLE_EXT("geq 100hz", 0, 0, 300, 0,
		       dnse_geq_get, dnse_geq_put),
	SOC_SINGLE_EXT("geq 300hz", 1, 0, 300, 0,
			   dnse_geq_get, dnse_geq_put),
	SOC_SINGLE_EXT("geq 1khz", 2, 0, 300, 0,
			   dnse_geq_get, dnse_geq_put),
	SOC_SINGLE_EXT("geq 3khz", 3, 0, 300, 0,
			   dnse_geq_get, dnse_geq_put),		   
	SOC_SINGLE_EXT("geq 10khz", 4, 0, 300, 0,
		       dnse_geq_get, dnse_geq_put),
		       
};

static const struct snd_kcontrol_new dnse_balance_control =
	SOC_SINGLE_EXT("balance", 0, 0, 20, 0, dnse_balance_get, dnse_balance_put);



static const struct snd_kcontrol_new dnse_speaker_delay_controls[] = {
	SOC_SINGLE_EXT("out delay", 0, 0, 500, 0,
		       dnse_delay_get, dnse_delay_put),
};




static const struct snd_kcontrol_new speaker_delay_controls[] = {
	SOC_SINGLE_EXT("out speaker delay", 0, 0, 500, 0,
		       speaker_delay_get, speaker_delay_put),
};

static const struct snd_kcontrol_new monitor_delay_controls[] = {
	SOC_SINGLE_EXT("out headphone delay", 0, 0, 500, 0,
		       monitor_delay_get, monitor_delay_put),
};

static const struct snd_kcontrol_new scart_mon_delay_controls[] = {
	SOC_SINGLE_EXT("out scart mon delay", 0, 0, 500, 0,
		       scart_mon_delay_get, scart_mon_delay_put),
};

static const struct snd_kcontrol_new scart_rf_delay_controls[] = {
	SOC_SINGLE_EXT("out scart rf delay", 0, 0, 500, 0,
		       scart_rf_delay_get, scart_rf_delay_put),
};

static const struct snd_kcontrol_new dual_tv_delay_controls[] = {
	SOC_SINGLE_EXT("out dual tv delay", 0, 0, 500, 0,
		       dual_tv_delay_get, dual_tv_delay_put),
};

static const struct snd_kcontrol_new sound_effect_delay_controls[] = {
	SOC_SINGLE_EXT("out sound effect delay", 0, 0, 500, 0,
		       sound_effect_delay_get, sound_effect_delay_put),
};


static const struct snd_kcontrol_new spdiftx_delay_controls[] = {
	SOC_SINGLE_EXT("out spdiftx delay", 0, 0, 500, 0,
		       spdiftx_delay_get, spdiftx_delay_put),
};





#if 0 //KYUNG 
static const struct snd_kcontrol_new dnse_auto_vol_controls[] = {
	SOC_SINGLE_EXT("auto vol", 0, 0, 300, 0,
		       dnse_auto_vol_get, dnse_auto_vol_put),
};
#endif 

static const struct snd_kcontrol_new dnse_srs_controls[] = {
	SOC_SINGLE_EXT("dnse mode", 0, 0, 300, 0,
		       dnse_srs_get, dnse_srs_put),
	SOC_SINGLE_EXT("dnse cusv", 1, 0, 300, 0,
			   dnse_srs_get, dnse_srs_put),
	SOC_SINGLE_EXT("dnse cusd", 2, 0, 300, 0,
			   dnse_srs_get, dnse_srs_put),
		       
};

static const struct snd_kcontrol_new dnse_3d_controls[] = {
	SOC_SINGLE_EXT("3d", 0, 0, 300, 0,
		       dnse_3d_get, dnse_3d_put),
};

/*not yet*/
static const struct snd_kcontrol_new dnse_personal_controls[] = {
	SOC_SINGLE_EXT("personal on", 0, 0, 300, 0,
		       dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 0", 1, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 1", 2, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 2", 3, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 3", 4, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 4", 5, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT("personal 5", 6, 0, 300, 0,
			   dnse_personal_get, dnse_personal_put),

		       
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// rany.kwon@samsung.com: temporal backward compatibility TODO: remove this
static const struct snd_kcontrol_new main_gain_controls_old[] = {
	SOC_SINGLE_EXT("TV Speaker", 0, 0, ARRAY_SIZE(gu32VolumeTable_GL)-1, 0,
		       main_gain_get, main_gain_put),
};


static const struct snd_kcontrol_new main_gain_controls[] = {
	SOC_SINGLE_EXT("main gain", 0, 0, ARRAY_SIZE(gu32VolumeTable_GL)-1, 0,
		       main_gain_get, main_gain_put),
};

static const struct snd_kcontrol_new pcm_in_gain_controls[] = {
	SOC_SINGLE_EXT("pcm gain", 0, 0, ARRAY_SIZE(gu32VolumeTable_GL)-1, 0,
		       pcm_gain_get, pcm_gain_put),
};


static const struct snd_kcontrol_new out_bt_main_delay_controls[] = {
	SOC_SINGLE_EXT("out bt main delay", 0, 0, 500, 0,
		       bt_delay_main_get, bt_delay_main_put),
};

static const struct snd_kcontrol_new out_bt_sub_delay_controls[] = {
	SOC_SINGLE_EXT("out bt sub delay", 0, 0, 500, 0,
		       bt_delay_sub_get, bt_delay_sub_put),
};

static const struct snd_kcontrol_new out_acm_main_delay_controls[] = {
	SOC_SINGLE_EXT("out acm main delay", 0, 0, 500, 0,
		       acm_delay_main_get, acm_delay_main_put),
};


static const struct snd_kcontrol_new out_acm_sub_delay_controls[] = {
	SOC_SINGLE_EXT("out acm main delay", 0, 0, 500, 0,
		       acm_delay_sub_get, acm_delay_sub_put),
};



 


static const struct snd_kcontrol_new out_speaker_gain_controls[] = {
	SOC_SINGLE_EXT("out speaker gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0,
		       out_speaker_gain_get, out_speaker_gain_put),
};
 

static const struct snd_kcontrol_new dec0_out_gain_controls[] = {
	SOC_SINGLE_EXT("dec0 out gain", 0, 0, 500 ,0,
		       dec0_out_gain_get, dec0_out_gain_put),
};

static const struct snd_kcontrol_new dec1_out_gain_controls[] = {
	SOC_SINGLE_EXT("dec1 out gain", 0, 0, 500,0,
		       dec1_out_gain_get, dec1_out_gain_put),
};

static const struct snd_kcontrol_new out_scart_monitor_gain_controls[] = {
	SOC_SINGLE_EXT("out scart monitor gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0,
		       out_scart_monitor_gain_get, out_scart_monitor_gain_put),
};

static const struct snd_kcontrol_new out_dual_tv_gain_controls[] = {
	SOC_SINGLE_EXT("out dual tv gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0,
		       out_dual_tv_gain_get, out_dual_tv_gain_put),
};

static const struct snd_kcontrol_new out_headphone_gain_controls[] = {
	SOC_SINGLE_EXT("out headphone gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0,
		       out_headphone_gain_get, out_headphone_gain_put),
};

static const struct snd_kcontrol_new out_spdiftx_gain_controls[] = {
	SOC_SINGLE_EXT("out spdiftx gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0,
		       out_spdiftx_get, out_spdiftx_put),
};

/* start loop back */
static struct snd_kcontrol_new start_loopback_test = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name =  "loop back",
 .info = loopback_test_info,
 .get =  loopback_test_get,
 .put =  loopback_test_put,
};

/* lipsync test */
static struct snd_kcontrol_new lipsync_usb_test = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "lipsync test",
 .info = lipsync_test_info,
 .get = lipsync_test_get,
 .put = lipsync_test_put,
};

#ifndef ALSA_SIF_INTEGRATION
/* sif related controls */
static struct snd_kcontrol_new sif_controls[] = {
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "carrier mute",
 .info = carrier_mute_info,
 .get = carrier_mute_get,
 .put = carrier_mute_put
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "high deviation",
 .info = high_deviation_info,
 .get = high_deviation_get,
 .put = high_deviation_put
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "pilot highlow",
 .info = pilot_highlow_info,
 .get = pilot_highlow_get,
 .put = pilot_highlow_put	
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "sif info analog mts",
 .info = mts_mode_info,
 .get = mts_mode_get,
 .put = NULL // Operation not permitted
},
{
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name = "sif analog mts out mode",
 .info = mts_out_mode_info,
 .get = mts_out_mode_get,
 .put = mts_out_mode_put
},

};
#endif

static struct snd_kcontrol_new stereo_mode_for_main_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name =  "set stereo mode",
 .info =  stereo_mode_for_main_info,
 .get =  stereo_mode_for_main_get,
 .put =  stereo_mode_for_main_put,
};


static struct snd_kcontrol_new dnse_auto_vol_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name =  "auto vol",
 .info =  dnse_auto_vol_info,
 .get =  dnse_auto_vol_get,
 .put =  dnse_auto_vol_put,
};


static struct snd_kcontrol_new mpeg_stereo_mode_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "mpeg mode",
 .info  = mpeg_stereo_mode_info,
 .get   = mpeg_stereo_mode_get,
 .put   = NULL // Operation not permitted
};

static struct snd_kcontrol_new out_hw_all_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out all mute",
 .info  = out_hw_all_mute_info,
 .get   = out_hw_all_mute_get,
 .put   = out_hw_all_mute_put
};


static struct snd_kcontrol_new out_hw_speaker_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out speaker mute",
 .info  = out_hw_speaker_mute_info,
 .get   = out_hw_speaker_mute_get,
 .put   = out_hw_speaker_mute_put
};

static struct snd_kcontrol_new out_hw_headphone_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out headphone mute",
 .info  = out_hw_headphone_mute_info,
 .get   = out_hw_headphone_mute_get,
 .put   = out_hw_headphone_mute_put
};

static struct snd_kcontrol_new out_hw_spdif_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out spdif mute",
 .info  = out_hw_spdif_mute_info,
 .get   = out_hw_spdif_mute_get,
 .put   = out_hw_spdif_mute_put
};

static struct snd_kcontrol_new out_hw_monitor_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out monitor mute",
 .info  = out_hw_monitor_mute_info,
 .get   = out_hw_monitor_mute_get,
 .put   = out_hw_monitor_mute_put
};

static struct snd_kcontrol_new out_hw_scart_mon_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out scart mon mute",
 .info  = out_hw_scart_mon_mute_info,
 .get   = out_hw_scart_mon_mute_get,
 .put   = out_hw_scart_mon_mute_put
};

static struct snd_kcontrol_new out_hw_scart_rf_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out scart rf mute",
 .info  = out_hw_scart_rf_mute_info,
 .get   = out_hw_scart_rf_mute_get,
 .put   = out_hw_scart_rf_mute_put
};


static struct snd_kcontrol_new out_audio_mute_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "out mute",
 .info  = out_audio_mute_info,
 .get   = out_audio_mute_get,
 .put   = out_audio_mute_put
};

static struct snd_kcontrol_new headphone_connect_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "headphone gain control enable",
 .info  = headphone_connect_info,
 .get   = headphone_connect_get,
 .put   = headphone_connect_put
};


static struct snd_kcontrol_new spdif_bypass_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "spdif bypass",
 .info  = spdif_bypass_info,
 .get   = spdif_bypass_get,
 .put   = spdif_bypass_put
};

static struct snd_kcontrol_new echo_onoff_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "echo onoff",
 .info  = echo_onoff_info,
 .get   = echo_onoff_get,
 .put   = echo_onoff_put
};



static struct snd_kcontrol_new bt_kernel_out_buffer_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "bt kernel out buffer source type",
		.info  = bt_kernel_out_buf_source_type_info,
		.get   = bt_kernel_out_buf_dec_source_get,
		.put   = bt_kernel_out_buf_dec_source_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "bt kernel out buffer info",
		.info  = bt_kernel_out_buf_info,
		.get   = bt_kernel_out_buf_info_get,
		.put   = NULL
	},

#if 0 // yet, not used ...
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "bt kernel out buffer format",
		.info  = set_audio_stream_format_info,
		.get   = set_audio_stream_format_get,
		.put   = set_audio_stream_format_put
	},
#endif

	SOC_SINGLE_BOOL_EXT("bt kernel out buffer enable", NULL,
			start_stop_audio_stream_get,
			start_stop_audio_stream_put),
	SOC_SINGLE_BOOL_EXT("bt kernel out buffer mute",   NULL,
			set_mute_audio_stream_get,
			set_mute_audio_stream_put),
	SOC_SINGLE_EXT(     "out es delay",     0, 0,        INT_MAX, 0,
			set_delay_audio_stream_get,
			set_delay_audio_stream_put),
	SOC_SINGLE_EXT(     "bt kernel out buffer gain",      0, 0, VOLUME_RANGE-1, 0,
			set_audio_stream_volume_get,
			set_audio_stream_volume_put)
};



static struct snd_kcontrol_new audio_info_main_codec_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "audio info dec0 codec",
 .info  = audio_status_main_codec_info,
 .get   = audio_status_main_codec_get,
 .put   = NULL
};

static struct snd_kcontrol_new audio_info_sub_codec_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "audio info dec1 codec",
 .info  = audio_status_sub_codec_info,
 .get   = audio_status_sub_codec_get,
 .put   = NULL
};

static struct snd_kcontrol_new dolby_compressed_dec0_mode_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "dolby compressed dec0 mode",
 .info  = dolby_compressed_dec0_mode_info,
 .get   = dolby_compressed_dec0_mode_get,
 .put   = dolby_compressed_dec0_mode_put
};

static struct snd_kcontrol_new dolby_compressed_dec1_mode_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "dolby compressed dec1 mode",
 .info  = dolby_compressed_dec1_mode_info,
 .get   = dolby_compressed_dec1_mode_get,
 .put   = dolby_compressed_dec1_mode_put
};



static struct snd_kcontrol_new audio_info_lock_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "audio info lock",
 .info  = audio_status_info_lock_info,
 .get   = audio_status_info_lock_get,
 .put   = NULL
};


static struct snd_kcontrol_new spdif_mode_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "spdiftx mode",
 .info  = is_spdiftx_es_info,
 .get   = is_spdiftx_es_get,
 .put   = is_spdiftx_es_put
};


static struct snd_kcontrol_new pcm_main_out_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "main out",
 .info  = pcm_out_info,
 .get   = pcm_main_out_get,
 .put   = pcm_main_out_put
};


static struct snd_kcontrol_new pcm_remote_out_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "remote out",
 .info  = pcm_out_info,
 .get   = pcm_remote_out_get,
 .put   = pcm_remote_out_put
};


static struct snd_kcontrol_new pcm_scart_out_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "scart out",
 .info  = pcm_out_info,
 .get   = pcm_scart_out_get,
 .put   = pcm_scart_out_put
};


static struct snd_kcontrol_new sound_mode_controls = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "sound mode",
 .info  = dnse_srs_info,
 .get   = dnse_srs_get,
 .put   = dnse_srs_put,
 .private_value = SOC_SINGLE_VALUE(0, 0, 0, 0)
};



//////////////////////////////////////////////////////////////////////////////////////////////
// new control interfaces

// 3d effect mode
static int g_dnse_3d_effect_mode = 0;
static int dnse_3d_effect_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[6] = {
		// the order should match with SdPostSoundTV_3DAudio_k
		"NONE", "2D MIN", "2D MIDDLE", "2D HIGH", "3D MIN", "3D MIDDLE", "3D HIGH"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item > 6)
		uinfo->value.enumerated.item = 6;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int dnse_3d_effect_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_dnse_3d_effect_mode;
	return 0;
}
static int dnse_3d_effect_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	Ae_SoftVolume_s  strtSoftVolmueTmp, strtSoftVolmue;
	unsigned int value = ucontrol->value.enumerated.item[0];
	int ret = 0;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	strtSoftVolmueTmp.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	strtSoftVolmue.u16VolumeIndex = 0;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	switch(/*eMode3DAudio*/ value)
	{
		case SD_TV_3DAUDIO_MIN_3D:
			sDNSe.cDPTH = DEPTH_MMIN;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_MID_3D:
			sDNSe.cDPTH = DEPTH_MMID;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_HIG_3D:
			sDNSe.cDPTH = DEPTH_MMAX;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		case SD_TV_3DAUDIO_MIN_2D:
			sDNSe.cDPTH = DEPTH_MMIN;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		case SD_TV_3DAUDIO_MID_2D:
			sDNSe.cDPTH = DEPTH_MMID;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		case SD_TV_3DAUDIO_HIG_2D:
			sDNSe.cDPTH = DEPTH_MMAX;
			sDNSe.sRefInd = CONTENTS2D;
			break;
		case SD_TV_3DAUDIO_OFF:
			sDNSe.cDPTH = DEPTH_NONE;
			sDNSe.sRefInd = CONTENTS3D;
			break;
		default :
			pr_err("%s: invalid parameter %d\n", __FUNCTION__, value);
			ret = -EINVAL;
			break;
	}

	if(ret == 0) g_dnse_3d_effect_mode = value;

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmueTmp );

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

//	pr_debug("dnse_3d_put, 3D Audio[%d]\n", ucontrol->value.integer.value[0]);

	return ret;
}
static const struct snd_kcontrol_new dnse_3d_effect_mode_control = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "3d effect mode",
 .info  = dnse_3d_effect_mode_info,
 .get   = dnse_3d_effect_mode_get,
 .put   = dnse_3d_effect_mode_put
};


// auto volume level
static int g_dnse_auto_volume_level_mode = 0;
static int dnse_auto_volume_level_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {
		// the order should match with SdPostSoundTV_AutoVolMode_k
		"OFF", "NORMAL", "NIGHT"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int dnse_auto_volume_level_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl,	&sDNSe);
	ucontrol->value.enumerated.item[0] = g_dnse_auto_volume_level_mode;
	return 0;
}
static int dnse_auto_volume_level_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	Ae_SoftVolume_s  strtSoftVolmueTmp, strtSoftVolmue;
	unsigned int value = ucontrol->value.enumerated.item[0];
	int ret = 0;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	strtSoftVolmueTmp.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	strtSoftVolmue.u16VolumeIndex = 0;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmue );

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	switch(/*eAutoVolume*/ value){
	case SD_TV_AV_OFF:       sDNSe.cSVOL = SVOL_NONE; break;
	case SD_TV_AV_NORMAL:    sDNSe.cSVOL = SVOL_NORM; break;
	case SD_TV_AV_SRS_NIGHT: sDNSe.cSVOL = SVOL_NIGH; break;
	default:
		pr_err("%s: invalid parameter %d\n", __FUNCTION__, value);
		ret = -EINVAL;
		break;
	}

	if(ret == 0) g_dnse_auto_volume_level_mode = value;

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &strtSoftVolmueTmp );

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	//pr_debug("dnse_auto_vol_put, Auto Volume[%d]\n", ucontrol->value.integer.value[0]);

	return ret;
}
static const struct snd_kcontrol_new dnse_auto_volume_level_mode_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "auto volume level mode",
	.info  = dnse_auto_volume_level_mode_info,
	.get   = dnse_auto_volume_level_mode_get,
	.put   = dnse_auto_volume_level_mode_put
};


// dialog clarity effect & srs surround effect
static const struct snd_kcontrol_new dnse_effects_enable_controls[] = {
	SOC_SINGLE_BOOL_EXT("dialog clarity effect enable", SOC_SINGLE_VALUE(2, 0, 0, 0), dnse_srs_get, dnse_srs_put),
	SOC_SINGLE_BOOL_EXT("srs surround effect enable",   SOC_SINGLE_VALUE(1, 0, 0, 0), dnse_srs_get, dnse_srs_put),
};

// dtv installation filter type
static int g_dtv_installation_filter_type = 0;
static int dtv_installation_filter_type_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[7] = {
		"WALL TYPE GROUP 0", "WALL TYPE GROUP 1","WALL TYPE GROUP 2","WALL TYPE GROUP 3","WALL TYPE GROUP 4",
			"WALL TYPE GROUP 5","WALL TYPE GROUP 6"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item > 6)
		uinfo->value.enumerated.item = 6;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int dtv_installation_filter_type_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_dtv_installation_filter_type;
	return 0;
}
static int  dtv_installation_filter_type_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.enumerated.item[0], current->pid, current->comm);
	g_dtv_installation_filter_type = ucontrol->value.enumerated.item[0];
	return 0;
}
static const struct snd_kcontrol_new dtv_installation_filter_type_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "dtv installation filter type",
	.info  = dtv_installation_filter_type_info,
	.get   = dtv_installation_filter_type_get,
	.put   = dtv_installation_filter_type_put,
};




// dtv installation type
static int g_dtv_installation_mount_type = 0;
static int dtv_installation_mount_type_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
		"STAND", "WALL MOUNT"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int dtv_installation_mount_type_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = g_dtv_installation_mount_type;
	return 0;
}
static int  dtv_installation_mount_type_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: dtv_installation %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	g_dtv_installation_mount_type = value;
	
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	devAe_SetDTVInstallation(pAeHndl->pDevAeHndl, value /*Ae_DTVInstallationType_e*/, g_dtv_installation_filter_type /* Ae_InstallationFilterType_e*/ );

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	return 0;
}
static const struct snd_kcontrol_new dtv_installation_mount_type_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "dtv installation mount type",
	.info  = dtv_installation_mount_type_info,
	.get   = dtv_installation_mount_type_get,
	.put   = dtv_installation_mount_type_put,
};



// echo effect enable
static int g_main_out_mix_echo_effect_enable = 0;
static int g_remote_out_mix_echo_effect_enable = 0;
static int mix_echo_effect_enable_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	switch(kcontrol->private_value){
	case AE_MIXER_INST_0: ucontrol->value.integer.value[0] = g_main_out_mix_echo_effect_enable;   break;
	case AE_MIXER_INST_1: ucontrol->value.integer.value[0] = g_remote_out_mix_echo_effect_enable; break;
	default:
		pr_err("%s: internal error. invalid parameter. %d\n", __FUNCTION__, kcontrol->private_value);
		return -EINVAL;
	}
	return 0;
}
static int mix_echo_effect_enable_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	switch(kcontrol->private_value){
	case AE_MIXER_INST_0:
	case AE_MIXER_INST_1:
		dev_audio_ae_setechoonoff(m_audio_hndl.p_ae2_hndl, kcontrol->private_value, value ? AUDIO_ENABLE : AUDIO_DISABLE);
		break;
	default:
		pr_err("%s: internal error. invalid parameter. %d\n", __FUNCTION__, kcontrol->private_value);
		return -EINVAL;
	}

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}
static struct snd_kcontrol_new auipcm_mix_echo_effect_enable_controls[] = {
	SOC_SINGLE_BOOL_EXT(  "main out mix echo effect enable", AE_MIXER_INST_0, mix_echo_effect_enable_get, mix_echo_effect_enable_put),
	SOC_SINGLE_BOOL_EXT("remote out mix echo effect enable", AE_MIXER_INST_1, mix_echo_effect_enable_get, mix_echo_effect_enable_put),
};


// equalizer gain control for each band
static int dnse_eq_gain_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	mc = (struct soc_mixer_control *)kcontrol->private_value;
	reg = mc->reg;

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	ucontrol->value.integer.value[0] = sDNSe.geqUsrG[reg];

	return 0;
}
static int dnse_eq_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	Ae_DNSe_t sDNSe;
	pspIAeData_t pAeHndl;
	struct soc_mixer_control *mc = NULL;
	unsigned int reg;
	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	mc =(struct soc_mixer_control *)kcontrol->private_value;
	reg = mc->reg;

	dev_audio_ae_getdnse(m_audio_hndl.p_ae2_hndl, 	&sDNSe);

	/*
		SRS Aep 사용,-10dB ~ + 10dB 범위만 사용함 (UI : 20Step)
	*/
	//reg0 Band100Hz
	//reg1 Band300Hz
	//reg2 Band1KHz
	//reg3 Band3KHz
	sDNSe.cDNSe = DNSe_CUSTM;
	sDNSe.cGEQM = DNSe_ON;
	sDNSe.geqUsrG[reg] =  ((ucontrol->value.integer.value[0])/5) -10;

	dev_audio_ae_setdnse(m_audio_hndl.p_ae2_hndl, &sDNSe);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

//	pr_debug("dnse_geq_put, Band reg[%d]=[%d]\n", reg, ucontrol->value.integer.value[0]);
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: eq %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	return 0;
}
static const struct snd_kcontrol_new dnse_eq_gain_controls[] = {
	SOC_SINGLE_EXT("100hz eq gain", 0, 0, 100, 0, dnse_eq_gain_get, dnse_eq_gain_put),
	SOC_SINGLE_EXT("300hz eq gain", 1, 0, 100, 0, dnse_eq_gain_get, dnse_eq_gain_put),
	SOC_SINGLE_EXT( "1khz eq gain", 2, 0, 100, 0, dnse_eq_gain_get, dnse_eq_gain_put),
	SOC_SINGLE_EXT( "3khz eq gain", 3, 0, 100, 0, dnse_eq_gain_get, dnse_eq_gain_put),
	SOC_SINGLE_EXT("10khz eq gain", 4, 0, 100, 0, dnse_eq_gain_get, dnse_eq_gain_put)
};


// headphone out enable
static int headphone_out_enable = 1; // initially enabled
static int headphone_out_enable_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = headphone_out_enable;
	return 0;
}
static int headphone_out_enable_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: headphone_connect %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	headphone_out_enable = value;

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_VRegSet(pAeHndl->pDevAeHndl, AE_HEADPHONE_CONNECT_SET,  value ? AUDIO_ENABLE : AUDIO_DISABLE);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	return 0;
}
static struct snd_kcontrol_new headphone_out_enable_control =
	SOC_SINGLE_BOOL_EXT("headphone out enable", NULL, headphone_out_enable_get, headphone_out_enable_put);


// audio info decoder buf size
static const int AUDIO_INFO_DEC_BUF_INST_MASK=0x00ff;
static const int AUDIO_INFO_DEC_BUF_VALUE_MASK=0xff00;
static int  audio_info_dec_buf_size_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int free_size = 0, total_size = 0;

	AeInst_t inst = AE_INST0;
	spIAeHndl_t *p_hndl = NULL;
	Ae_MemConfig_e mem_conf;
	struct soc_mixer_control *mc = NULL;

	mc = (struct soc_mixer_control *)kcontrol->private_value;

	switch(mc->reg & AUDIO_INFO_DEC_BUF_INST_MASK){
	case AE_INST0:
		inst = AE_INST0;
		p_hndl = &(m_audio_hndl.p_ae0_hndl);
		mem_conf = AUDIO_MEMCONFIG_IN_MAIN_SIZE;
		break;
	case AE_INST1:
		inst = AE_INST1;
		p_hndl = &(m_audio_hndl.p_ae1_hndl);
		mem_conf = AUDIO_MEMCONFIG_IN_SUB_SIZE;
		break;
	default:
		pr_err("%s: something wrong! %x\n", __FUNCTION__, kcontrol->private_value);
		return -EIO;
	}

	dev_audio_ae_open(inst, p_hndl, AUDIO_CHIP_ID_MP0);
	dev_audio_ae_getbuffreesize(*p_hndl, mem_conf, &free_size, &total_size);
	dev_audio_ae_close(m_audio_hndl.p_ae0_hndl);

	if((mc->reg & AUDIO_INFO_DEC_BUF_VALUE_MASK) == 0xF00){
		ucontrol->value.integer.value[0] = free_size;
	}else{
		ucontrol->value.integer.value[0] = total_size;
	}
	return 0;
}
static const struct snd_kcontrol_new audio_info_dec_buf_size_controls[] = {
	SOC_SINGLE_EXT("audio info dec0 buf total size", AE_INST0,         0, INT_MAX, 0, audio_info_dec_buf_size_get, NULL),
	SOC_SINGLE_EXT("audio info dec0 buf free size",  AE_INST0 | 0xF00, 0, INT_MAX, 0, audio_info_dec_buf_size_get, NULL),
	SOC_SINGLE_EXT("audio info dec1 buf total size", AE_INST1,         0, INT_MAX, 0, audio_info_dec_buf_size_get, NULL),
	SOC_SINGLE_EXT("audio info dec1 buf free size",  AE_INST1 | 0xF00, 0, INT_MAX, 0, audio_info_dec_buf_size_get, NULL),
};


static struct snd_kcontrol_new main_out_source_select_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "main out source select",
	.info  = pcm_out_info,
	.get   = pcm_main_out_get,
	.put   = pcm_main_out_put
};


static struct snd_kcontrol_new remote_out_source_select_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "remote out source select",
	.info  = pcm_out_info,
	.get   = pcm_remote_out_get,
	.put   = pcm_remote_out_put
};


static struct snd_kcontrol_new scart_out_source_select_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "scart out source select",
	.info  = pcm_out_info,
	.get   = pcm_scart_out_get,
	.put   = pcm_scart_out_put
};

static struct snd_kcontrol_new mpeg_stereo_mode_control = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "mpeg stereo mode",
 .info  = mpeg_stereo_mode_info,
 .get   = mpeg_stereo_mode_get,
 .put   = NULL // Operation not permitted
};


// main out
static int main_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_main_out_gain;
	return 0;
}
static int main_out_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	g_main_out_gain = value;

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	u16Gain = u16Gain /8;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SPEAKER, (unsigned short)u16Gain);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}

static int main_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_main_out_mute;
	return 0;
}
static int main_out_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	g_main_out_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, value);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, value);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_MONITOR, value);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, value);

	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new main_out_controls[] = {
	// main out
	SOC_SINGLE_EXT(     "main out gain", 0, 0, ARRAY_SIZE(gu32VolumeTable_GL)-1, 0, main_out_gain_get, main_out_gain_put),
	SOC_SINGLE_BOOL_EXT("main out mute", NULL, main_out_mute_get, main_out_mute_put),
};


// aui/pcm mix
static int auipcm_mix_gain_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_auipcm_mix_gain;
	return 0;
}
static int auipcm_mix_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	g_auipcm_mix_gain = value;

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	pstrtPcmSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[g_auipcm_mix_gain];;
	devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}

static int auipcm_mix_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_auipcm_mix_mute;
	return 0;
}
static int auipcm_mix_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: mute %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	g_auipcm_mix_mute = value;

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	if(g_auipcm_mix_mute){
		pstrtPcmSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[0];
	}else{
		pstrtPcmSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[g_auipcm_mix_gain];
	}
	devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	return 0;
}
static const struct snd_kcontrol_new auipcm_mix_controls[] = {

	// aui/pcm mix
	SOC_SINGLE_EXT(     "aui/pcm mix gain", 0, 0, ARRAY_SIZE(gu32VolumeTable_GL)-1, 0, auipcm_mix_gain_get, auipcm_mix_gain_put),
        SOC_SINGLE_BOOL_EXT("aui/pcm mix mute", NULL,                                      auipcm_mix_mute_put, auipcm_mix_mute_put),
};


// speaker out
static int speaker_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_speaker_out_gain;
	return 0;
}
static int speaker_out_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	g_speaker_out_gain = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	pstrtSoftVolume.u16VolumeIndex = gu32VolumeTable_GL[g_speaker_out_gain];
	devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &pstrtSoftVolume );
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}
static int speaker_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_speaker_out_mute;
	return 0;
}
static int speaker_out_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	g_speaker_out_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPEAKER, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new speaker_out_controls[] = {

        // speaker out
	SOC_SINGLE_EXT(     "speaker out gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0, speaker_out_gain_get, speaker_out_gain_put),
	SOC_SINGLE_BOOL_EXT("speaker out mute", NULL,                                          speaker_out_mute_get, speaker_out_mute_put)
};


// remote out
static int g_remote_out_gain = 100;
static int remote_out_gain_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_remote_out_gain;
	return 0;
}
static int  remote_out_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);

	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	g_remote_out_gain = value;
	u16Gain = u16Gain/8;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_DUAL_TV, (unsigned short)u16Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	return 0;
}
static int g_remote_out_mute = 0;
static int remote_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_remote_out_mute;
	return 0;
}
static int  remote_out_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_DUAL_TV, value ? AIO_MUTE : AIO_UNMUTE);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new remote_out_controls[] = {
	SOC_SINGLE_EXT(     "remote out gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0, remote_out_gain_get, remote_out_gain_put),
	SOC_SINGLE_BOOL_EXT("remote out mute", NULL,                                          remote_out_mute_get, remote_out_mute_put),
};


// scart out
static int g_scart_out_gain = 100;
static int scart_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_scart_out_gain;
	return 0;
}
static int scart_out_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	g_scart_out_gain = value;
	u16Gain = u16Gain/8;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SCART_MON, (unsigned short)u16Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}
static int g_scart_out_mute = 0;
static int scart_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_scart_out_mute;
	return 0;
}
static int  scart_out_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	g_scart_out_mute = value;
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SCART_MON, value ? AIO_MUTE : AIO_UNMUTE);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new scart_out_controls[] = {
	SOC_SINGLE_EXT(     "scart out gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0, scart_out_gain_get, scart_out_gain_put),
	SOC_SINGLE_BOOL_EXT("scart out mute", NULL,                                          scart_out_mute_get, scart_out_mute_put),
};


// hp/line out
static int g_hpline_out_gain = 0;
static int hpline_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_hpline_out_gain;
	return 0;
}

static int hpline_out_gain_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	g_hpline_out_gain = value;
	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	u16Gain = u16Gain/8;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_HEADPHONE, (unsigned short)u16Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);
	return 0;
}
static int g_hpline_out_mute = 0;
static int hpline_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_hpline_out_mute;
	return 0;
}
static int hpline_out_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: enum %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	g_hpline_out_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_HEADPHONE, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new hpline_out_controls[] = {
	SOC_SINGLE_EXT(     "hp/line out gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0, hpline_out_gain_get, hpline_out_gain_put),
	SOC_SINGLE_BOOL_EXT("hp/line out mute", NULL,                                          hpline_out_mute_get, hpline_out_mute_put),
};


// spdif ouot
static int g_spdif_raw_out_gain = 0;
static int spdif_raw_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_spdif_raw_out_gain;
	return 0;
}
static int spdif_raw_out_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	g_spdif_raw_out_gain = value;
	u16Gain = (g_spdif_raw_out_gain *16) /10;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SPDIFTX, (unsigned short)u16Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}

static const struct snd_kcontrol_new spdif_raw_out_gain[] = {
	SOC_SINGLE_EXT("spdif out raw gain", 0, 0, 10, 0,
		       spdif_raw_out_gain_get, spdif_raw_out_gain_put),
};



// spdif ouot
static int g_spdif_out_gain = 0;
static int spdif_out_gain_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = out_spdiftx_gain;
	return 0;
}
static int spdif_out_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	uint16_t u16Gain;
	int value = ucontrol->value.integer.value[0];

	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: volume %ld [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	out_spdiftx_gain = value;
	u16Gain = (g_HeadphoneVolumeTable[value] *16) /10;
	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;
	devAe_SetScaleOut(pAeHndl->pDevAeHndl, AUDIO_OUT_SPDIFTX, (unsigned short)u16Gain);
	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}
static int g_spdif_out_mute = 0;
static int spdif_out_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = g_spdif_out_mute;
	return 0;
}

static int spdif_out_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %d [pid:%d,%s]\n", __FUNCTION__, value, current->pid, current->comm);
	dev_audio_aio_open(AIO_INST0, &(m_audio_hndl.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	g_spdif_out_mute = value;
	dev_audio_aio_mute(m_audio_hndl.p_aio_hndl, AUDIO_OUT_SPDIFTX, value);
	dev_audio_aio_close(m_audio_hndl.p_aio_hndl);
	return 0;
}
static const struct snd_kcontrol_new spdif_out_controls[] = {
	SOC_SINGLE_EXT(     "spdif out gain", 0, 0, ARRAY_SIZE(g_HeadphoneVolumeTable)-1, 0, spdif_out_gain_get, spdif_out_gain_put),
	SOC_SINGLE_BOOL_EXT("spdif out mute", NULL,                                          spdif_out_mute_get, spdif_out_mute_put),
};


// dha band# eq gain (dynamic hearing aid)
static const struct snd_kcontrol_new dnse_dha_eq_controls[] = {
	SOC_SINGLE_BOOL_EXT("dha eq enable", SOC_SINGLE_VALUE(0,0,0,0), dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band0 eq gain", 1, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band1 eq gain", 2, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band2 eq gain", 3, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band3 eq gain", 4, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band4 eq gain", 5, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
	SOC_SINGLE_EXT(     "dha band5 eq gain", 6, 0, 60, 0,           dnse_personal_get, dnse_personal_put),
};


static const struct snd_kcontrol_new audio_info_sampling_freq_control =
	SOC_SINGLE_EXT("audio info sampling freq", 0, 0, 65535, 0, in_sample_rate_get, NULL);


// hpline out enable
static int hpline_out_enable_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = headphone_connect;
	return 0;
}
static int hpline_out_enable_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	pspIAeData_t pAeHndl;
	int value = ucontrol->value.integer.value[0];
	t2d_print(t2d_debug_ALSA_soc_onoff_flag, "%s: %ld [pid:%d,%s]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid, current->comm);

	dev_audio_ae_open(AE_INST2, &(m_audio_hndl.p_ae2_hndl), AUDIO_CHIP_ID_MP0);
	headphone_connect = value;

	pAeHndl = (pspIAeData_t)m_audio_hndl.p_ae2_hndl;

	if(value == 1) //CONNECT
		devAe_VRegSet(pAeHndl->pDevAeHndl, AE_HEADPHONE_CONNECT_SET,  AUDIO_ENABLE);
	else if(value == 0)//DISCONNECT
		devAe_VRegSet(pAeHndl->pDevAeHndl, AE_HEADPHONE_CONNECT_SET,  AUDIO_DISABLE);

	dev_audio_ae_close(m_audio_hndl.p_ae2_hndl);

	return 0;
}
static const struct snd_kcontrol_new hpline_out_enable_control =
	SOC_SINGLE_BOOL_EXT("hp/line out enable", NULL, hpline_out_enable_get, hpline_out_enable_put);
// remote out mode
static int remote_out_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = { "NONE", "CLONE", "DUAL" }; // TODO: fixme

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= 3)
		uinfo->value.enumerated.item = 0;

	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int remote_out_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	printk("%s not implemented. fixme.\n", __FUNCTION__);
	// TODO: fixme
	return 0;
}
static int remote_out_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	printk("%s not implemented. fixme.\n", __FUNCTION__);
	// TODO: fixme
	return 0;
}
static struct snd_kcontrol_new remote_out_mode_control = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "remote out mode",
 .info  = remote_out_mode_info,
 .get   = remote_out_mode_get,
 .put   = remote_out_mode_put,
};
// spdif out format
static struct snd_kcontrol_new spdif_out_format_control = {
 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
 .name  = "spdif out format",
 .info  = spdif_bypass_info,
 .get   = spdif_bypass_get,
 .put   = spdif_bypass_put
};


// new control interfaces
//////////////////////////////////////////////////////////////////////////////////////////////

static int sdp_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;

	struct snd_ctl_elem_value value;
	
#if 0	
	/*audio info lock*/
	snd_soc_add_card_controls(card, audio_info_lock,
					 ARRAY_SIZE(audio_info_lock));
#endif 

#if 0
	/*echo on/off*/
	snd_soc_add_card_controls(card, echo_onoff,
					 ARRAY_SIZE(echo_onoff));
#endif 

	snd_soc_add_card_controls(card, spdif_raw_out_gain,
					 ARRAY_SIZE(spdif_raw_out_gain));


	/*is_be_connected_main*/
	snd_soc_add_card_controls(card, main_in_buffer_level,
					 ARRAY_SIZE(main_in_buffer_level));
	/*is_spdiftx_es*/
	snd_soc_add_card_controls(card, sub_in_buffer_level,
					 ARRAY_SIZE(sub_in_buffer_level));
	
	/*is_be_connected_main*/
	snd_soc_add_card_controls(card, is_be_connected_main,
					 ARRAY_SIZE(is_be_connected_main));
#if 0
	/*is_spdiftx_es*/
	snd_soc_add_card_controls(card, spdiftx_es,
					 ARRAY_SIZE(spdiftx_es));
#endif 

	/*is_master*/
	snd_soc_add_card_controls(card, i2srx_master,
					 ARRAY_SIZE(i2srx_master));
	/*is_remote_clone*/
	snd_soc_add_card_controls(card, remote_clone,
					 ARRAY_SIZE(remote_clone));
	/*is_remote_mode*/
	snd_soc_add_card_controls(card, remote_mode,
					 ARRAY_SIZE(remote_mode));	

	/*dtv installation*/
	snd_soc_add_card_controls(card, dtv_installation,
					 ARRAY_SIZE(dtv_installation));

	/*in sample rate info*/
	snd_soc_add_card_controls(card, in_sample_rate,
				     ARRAY_SIZE(in_sample_rate));

	/*mpeg mode info*/
	snd_soc_add_card_controls(card, &mpeg_stereo_mode_controls, 1);

	/*all mute*/
	snd_soc_add_card_controls(card, &out_hw_all_mute_controls, 1);

	/*speaker mute*/
	snd_soc_add_card_controls(card, &out_hw_speaker_mute_controls, 1);
	
	/*headphone mute*/
	snd_soc_add_card_controls(card, &out_hw_headphone_mute_controls, 1);
	
	/*monitor mute mute*/
	snd_soc_add_card_controls(card, &out_hw_spdif_mute_controls, 1);
	
	/*out audio mute*/
	snd_soc_add_card_controls(card, &out_hw_monitor_mute_controls, 1);
	
	/*scart mon mute*/
	snd_soc_add_card_controls(card, &out_hw_scart_mon_mute_controls, 1);
	
	/*scart rf mute*/
	snd_soc_add_card_controls(card, &out_hw_scart_rf_mute_controls, 1);
	
	/*out audio mute*/
	snd_soc_add_card_controls(card, &out_audio_mute_controls, 1);

	/*headphone connect*/
	snd_soc_add_card_controls(card, &headphone_connect_controls, 1);

	/*spdif bypass*/
	snd_soc_add_card_controls(card, &spdif_bypass_controls, 1);

	/*echo onoff*/
	snd_soc_add_card_controls(card, &echo_onoff_controls, 1);	

	/*bt kernel out buffer*/
	snd_soc_add_card_controls(card, bt_kernel_out_buffer_controls, ARRAY_SIZE(bt_kernel_out_buffer_controls));

	/*audio main codec type*/
	snd_soc_add_card_controls(card, &audio_info_main_codec_controls, 1);

	/*audio sub codec type*/
	snd_soc_add_card_controls(card, &audio_info_sub_codec_controls, 1);	

	/*audio lock*/
	snd_soc_add_card_controls(card, &audio_info_lock_controls, 1);

	snd_soc_add_card_controls(card, &dolby_compressed_dec0_mode_controls, 1);

	snd_soc_add_card_controls(card, &dolby_compressed_dec1_mode_controls, 1);

	/*spdif mode*/
	snd_soc_add_card_controls(card, &spdif_mode_controls, 1);

	/*main out*/
	snd_soc_add_card_controls(card, &pcm_main_out_controls, 1);

	/*remote out*/
	snd_soc_add_card_controls(card, &pcm_remote_out_controls, 1);

	/*scart out*/
	snd_soc_add_card_controls(card, &pcm_scart_out_controls, 1);

	/*Graphic EQ*/
	snd_soc_add_card_controls(card, dnse_geq_controls,
				     ARRAY_SIZE(dnse_geq_controls));

	/*Balance*/
	snd_soc_add_card_controls(card, &dnse_balance_control, 1);

	/*Speaker delay*/
	snd_soc_add_card_controls(card, dnse_speaker_delay_controls,
				     ARRAY_SIZE(dnse_speaker_delay_controls));

	snd_soc_add_card_controls(card, speaker_delay_controls,
				     ARRAY_SIZE(speaker_delay_controls));		

	snd_soc_add_card_controls(card, monitor_delay_controls,
				     ARRAY_SIZE(monitor_delay_controls));		

	snd_soc_add_card_controls(card, scart_mon_delay_controls,
			     ARRAY_SIZE(scart_mon_delay_controls));		

	snd_soc_add_card_controls(card, scart_rf_delay_controls,
				     ARRAY_SIZE(scart_rf_delay_controls));		

	snd_soc_add_card_controls(card, dual_tv_delay_controls,
				     ARRAY_SIZE(dual_tv_delay_controls));		

	snd_soc_add_card_controls(card, sound_effect_delay_controls,
			     ARRAY_SIZE(sound_effect_delay_controls));		


	snd_soc_add_card_controls(card, spdiftx_delay_controls,
				     ARRAY_SIZE(spdiftx_delay_controls));		


	snd_soc_add_card_controls(card, &stereo_mode_for_main_controls,	1);

	/*Auto Volume*/
#if 1	
	snd_soc_add_card_controls(card, &dnse_auto_vol_controls,	1);
#endif 
	/*DNSe*/
	snd_soc_add_card_controls(card, dnse_srs_controls,
				     ARRAY_SIZE(dnse_srs_controls));

	/*sound mode info (same as DNSe)*/
	snd_soc_add_card_controls(card, &sound_mode_controls, 1);

	/*3D*/
	snd_soc_add_card_controls(card, dnse_3d_controls,
				     ARRAY_SIZE(dnse_3d_controls));

	/*Personal Audio*/
	snd_soc_add_card_controls(card, dnse_personal_controls,
				     ARRAY_SIZE(dnse_personal_controls));

	/*Main Volume*/
	snd_soc_add_card_controls(card, main_gain_controls,
				     ARRAY_SIZE(main_gain_controls));

	/*Main Volume: "TV Speaker" rany.kwon@samsung.com TODO: remove this */
	snd_soc_add_card_controls(card, main_gain_controls_old,
				     ARRAY_SIZE(main_gain_controls_old));

	snd_soc_add_card_controls(card, out_bt_main_delay_controls,
				     ARRAY_SIZE(out_bt_main_delay_controls));	

	snd_soc_add_card_controls(card, out_bt_sub_delay_controls,
				     ARRAY_SIZE(out_bt_sub_delay_controls));	
	
	snd_soc_add_card_controls(card, out_acm_main_delay_controls,
				     ARRAY_SIZE(out_acm_main_delay_controls));	
	
	snd_soc_add_card_controls(card, out_acm_sub_delay_controls,
				     ARRAY_SIZE(out_acm_sub_delay_controls));		

	/*Out PCM Volume with Effect*/
	snd_soc_add_card_controls(card, pcm_in_gain_controls,
				     ARRAY_SIZE(pcm_in_gain_controls));	

	/*Speaker Gain control controled Main only*/  
	snd_soc_add_card_controls(card, out_speaker_gain_controls,
				     ARRAY_SIZE(out_speaker_gain_controls));
	
	/*dec0 out gain*/  
	snd_soc_add_card_controls(card, dec0_out_gain_controls,
					 ARRAY_SIZE(dec0_out_gain_controls));
	/*dec1 out gain*/  
	snd_soc_add_card_controls(card, dec1_out_gain_controls,
					 ARRAY_SIZE(dec1_out_gain_controls));

	/*Scart Monitor Gain control controled PCM only*/  
	snd_soc_add_card_controls(card, out_scart_monitor_gain_controls,
				     ARRAY_SIZE(out_scart_monitor_gain_controls));

	/*Dual path Gain control controled PCM only*/  
	snd_soc_add_card_controls(card, out_dual_tv_gain_controls,
				     ARRAY_SIZE(out_dual_tv_gain_controls));

	/*Headphone path Gain control controled PCM only*/  
	snd_soc_add_card_controls(card, out_headphone_gain_controls,
				     ARRAY_SIZE(out_headphone_gain_controls));

	/*SPDIFtxl path Gain control controled PCM only*/  
	snd_soc_add_card_controls(card, out_spdiftx_gain_controls,
				     ARRAY_SIZE(out_spdiftx_gain_controls));		
	/* start loop back control add */
	snd_soc_add_card_controls(card, &start_loopback_test, 1);

	/* lip sync usb test control add */
	snd_soc_add_card_controls(card, &lipsync_usb_test, 1);
#ifndef ALSA_SIF_INTEGRATION	
	/* sif controls add */
	snd_soc_add_card_controls(card, sif_controls, ARRAY_SIZE(sif_controls));
#endif	
	/* set initial volume values */
	value.value.integer.value[0]=100;
	main_out_gain_put(NULL, &value);
	auipcm_mix_gain_put(NULL, &value);



	///////////////////////////////////////////////////////////////////////////////
	// register new control interfaces
	snd_soc_add_card_controls(card, &dnse_3d_effect_mode_control,      1);
	snd_soc_add_card_controls(card, &dnse_auto_volume_level_mode_control,   1);
	snd_soc_add_card_controls(card,  dnse_effects_enable_controls,     ARRAY_SIZE(dnse_effects_enable_controls));
	snd_soc_add_card_controls(card,  dnse_eq_gain_controls,            ARRAY_SIZE(dnse_eq_gain_controls));
	//snd_soc_add_card_controls(card, &headphone_out_enable_control,     1);
	snd_soc_add_card_controls(card,  audio_info_dec_buf_size_controls, ARRAY_SIZE(audio_info_dec_buf_size_controls));
	snd_soc_add_card_controls(card, &headphone_out_enable_control,     1);
	snd_soc_add_card_controls(card, &main_out_source_select_control,   1);
	snd_soc_add_card_controls(card, &remote_out_source_select_control, 1);
	snd_soc_add_card_controls(card, &scart_out_source_select_control,  1);
	snd_soc_add_card_controls(card, &mpeg_stereo_mode_control,         1);
	snd_soc_add_card_controls(card,  main_out_controls,                ARRAY_SIZE(main_out_controls));
	snd_soc_add_card_controls(card,  auipcm_mix_controls,              ARRAY_SIZE(auipcm_mix_controls));
	snd_soc_add_card_controls(card,  speaker_out_controls,             ARRAY_SIZE(speaker_out_controls));
	snd_soc_add_card_controls(card,  remote_out_controls,              ARRAY_SIZE(remote_out_controls));
	snd_soc_add_card_controls(card,  scart_out_controls,               ARRAY_SIZE(scart_out_controls));
	snd_soc_add_card_controls(card,  hpline_out_controls,              ARRAY_SIZE(hpline_out_controls));
	snd_soc_add_card_controls(card,  spdif_out_controls,               ARRAY_SIZE(spdif_out_controls));
	snd_soc_add_card_controls(card, &dtv_installation_mount_type_control,   1);
	snd_soc_add_card_controls(card, &dtv_installation_filter_type_control,   1);	
	snd_soc_add_card_controls(card,  auipcm_mix_echo_effect_enable_controls, ARRAY_SIZE(auipcm_mix_echo_effect_enable_controls));
	snd_soc_add_card_controls(card,  dnse_dha_eq_controls,             ARRAY_SIZE(dnse_dha_eq_controls));
	snd_soc_add_card_controls(card, &audio_info_sampling_freq_control, 1);
	snd_soc_add_card_controls(card, &spdif_out_format_control,         1);
	// register new control interfaces
	///////////////////////////////////////////////////////////////////////////////



#if 0 // disabled by DA
	///////////////////////////////////////////////////////////////////////////////
	// boot time value setup using factory data (which can be controlled by user)
	// TODO: these should be moved to some where else !! rany.kwon@samsung.com
	int factory_value;
	struct snd_ctl_elem_value ucontrol;

	kfactory_drv_get_data(ID_SOUND_WALL_FILTER_TYPE, &factory_value);
	ucontrol.value.enumerated.item[0] = factory_value;
	dtv_installation_filter_type_put(NULL, &ucontrol);

	//kfactory_drv_get_data(ID_SOUND_SPDIF_PCM_GAIN, &factory_value);
	//ucontrol.value.integer.value[0] = -1 * factory_value;
	//spdif_out_gain_put(NULL, &ucontrol);
	//
	///////////////////////////////////////////////////////////////////////////////
#endif	
	return 0;
}




static struct snd_soc_dai_link sdp_msic_dailink[] = {
#if 0	
	{
		.name = "Medfield Headset",
		.stream_name = "Headset",
		.cpu_dai_name = "Headset-cpu-dai",
		.codec_dai_name = "SDP Codec Headset",
		.codec_name = "sdp-codec",
		.platform_name = "sdp-platform",
		.init = sdp_init,
	},
#endif 	
	{
		.name = "SDP Speaker",
		.stream_name = "Speaker",
		.cpu_dai_name = "Speaker-cpu-dai",
		.codec_dai_name = "SDP Codec Speaker",
		.codec_name = "sdp-codec.2",
		.platform_name = "sdp-platform.3",
		.init = sdp_init,
	},

#if 1 //ALSA1 , ALSA2 support

	{
		.name = "SDP Main Speaker",
		.stream_name = "Main Spk",
		.cpu_dai_name = "Main-cpu-dai",
		.codec_dai_name = "Main Codec Speaker",
		.codec_name = "sdp-codec.2",
		.platform_name = "sdp-platform.3",
//		.init = sdp_init,
	},


	{
		.name = "SDP Remote Speaker",
		.stream_name = "Remote Spk",
		.cpu_dai_name = "Remote-cpu-dai",
		.codec_dai_name = "Remote Codec Speaker",
		.codec_name = "sdp-codec.2",
		.platform_name = "sdp-platform.3",
//		.init = sdp_init,
	},

//PP ALSA
#if 1
	{
		.name = "SDP PP Speaker",
		.stream_name = "PP Spk",
		.cpu_dai_name = "PP-cpu-dai",
		.codec_dai_name = "PP Codec Speaker",
		.codec_name = "sdp-codec.2",
		.platform_name = "sdp-platform.3",
//		.init = sdp_init,
	},
#endif 


#endif 


#if 0	
	{
		.name = "Medfield Vibra",
		.stream_name = "Vibra1",
		.cpu_dai_name = "Vibra1-cpu-dai",
		.codec_dai_name = "SDP Codec Vibra1",
		.codec_name = "sdp-codec",
		.platform_name = "sdp-platform",
		.init = NULL,
	},
	{
		.name = "Medfield Haptics",
		.stream_name = "Vibra2",
		.cpu_dai_name = "Vibra2-cpu-dai",
		.codec_dai_name = "SDP Codec Vibra2",
		.codec_name = "sdp-codec",
		.platform_name = "sdp-platform",
		.init = NULL,
	},
#endif 	
	{
		.name = "SDP Compress",
		.stream_name = "Speaker",
		.cpu_dai_name = "Compress-cpu-dai",
		.codec_dai_name = "SDP Codec Speaker",
		.codec_name = "sdp-codec.2",
		.platform_name = "sdp-platform.3",
		.init = NULL,
	},
};

/* SoC card */
static struct snd_soc_card snd_soc_card_sdp = {
	.name = "sdp_scard_audio",
	.owner = THIS_MODULE,
	.dai_link = sdp_msic_dailink,
	.num_links = ARRAY_SIZE(sdp_msic_dailink),
	.card_num = 0;
};
struct snd_soc_card *p_snd_soc_card_sdp = &snd_soc_card_sdp;
EXPORT_SYMBOL(p_snd_soc_card_sdp);
#if 00
static irqreturn_t snd_sdp_pcm_intr_handler(int irq, void *dev)
{
	struct sdp_mc_private *mc_private = (struct sdp_mc_private *) dev;

	memcpy_fromio(&mc_private->interrupt_status,
			((void *)(mc_private->int_base)),
			sizeof(u8));
	return IRQ_WAKE_THREAD;
}

static irqreturn_t snd_sdp_pcm_detection(int irq, void *data)
{
	struct sdp_mc_private *mc_drv_ctx = (struct sdp_mc_private *) data;

	sdp_pcm_out(mc_drv_ctx->interrupt_status);

	return IRQ_HANDLED;
}

#endif
#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_audio_debug(void)
{
	long event;
	const int ID_MAX = 99;
	PRINT_T2D("[%s]\n", __func__);

	while (1) {
		PRINT_T2D("\n");
		PRINT_T2D(" 0 ) toggle ALSA SoC audio debug message [current: %d]\n", t2d_debug_ALSA_soc_onoff_flag);
		PRINT_T2D("=====================================\n");
		PRINT_T2D("%d ) exit\n", ID_MAX);

		PRINT_T2D(" => ");
		event = t2d_dbg_get_event_as_numeric(NULL, NULL);
		PRINT_T2D("\n");

		if (event >= 0 && event < ID_MAX) {
			switch (event) {
			case 0:
			{
				t2d_debug_ALSA_soc_onoff_flag = !t2d_debug_ALSA_soc_onoff_flag;
				printk("ALSA SoC audio debug message is %s\n", t2d_debug_ALSA_soc_onoff_flag ? "enabled" : "disabled");
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

static int snd_sdp_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0;//, irq;//not used
	struct sdp_mc_private *mc_drv_ctx;
	//struct resource *irq_mem;//not used

	pr_debug("snd_sdp_mc_probe called\n");

#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("ALSA SoC Audio debug", 5, t2ddebug_audio_debug, NULL);
#endif // CONFIG_T2DDEBUGD

	/* retrive the irq number */
//	irq = platform_get_irq(pdev, 1);

	/* audio interrupt base of SRAM location where
	 * interrupts are stored by System FW */
	mc_drv_ctx = kzalloc(sizeof(*mc_drv_ctx), GFP_ATOMIC);
	if (!mc_drv_ctx) {
		pr_err("allocation failed\n");
		return -ENOMEM;
	}
#if 0
	irq_mem = platform_get_resource_byname(
				pdev, IORESOURCE_MEM, "IRQ_BASE");
	if (!irq_mem) {
		pr_err("no mem resource given\n");
		ret_val = -ENODEV;
		goto unalloc;
	}
	mc_drv_ctx->int_base = ioremap_nocache(irq_mem->start,
					resource_size(irq_mem));
	if (!mc_drv_ctx->int_base) {
		pr_err("Mapping of cache failed\n");
		ret_val = -ENOMEM;
		goto unalloc;
	}
	/* register for interrupt */
	ret_val = request_threaded_irq(irq, snd_sdp_pcm_intr_handler,
			snd_sdp_pcm_detection,
			IRQF_SHARED, pdev->dev.driver->name, mc_drv_ctx);
	if (ret_val) {
		pr_err("cannot register IRQ\n");
		goto unalloc;
	}
#endif 	


#if 1
	/* register the soc card */
	snd_soc_card_sdp.dev = &pdev->dev;
	ret_val = snd_soc_register_card(&snd_soc_card_sdp);
	if (ret_val) {
		pr_err("snd_soc_register_card failed %d\n", ret_val);
		goto freeirq;
	}

	platform_set_drvdata(pdev, mc_drv_ctx);
	// rany.kwon: temporarly make DEC0 as a default main input
	{
		struct snd_ctl_elem_value ucontrol;
		ucontrol.value.integer.value[0] = 0;
		pcm_main_out_put(NULL, &ucontrol);
	}
	pr_debug("successfully exited probe\n");
	return ret_val;
#endif 	

freeirq:
	//free_irq(irq, mc_drv_ctx);

#if 00
unalloc:
#endif
	kfree(mc_drv_ctx);


	return ret_val;
}

static int snd_sdp_mc_remove(struct platform_device *pdev)
{
	struct sdp_mc_private *mc_drv_ctx = platform_get_drvdata(pdev);

	pr_debug("snd_sdp_mc_remove called\n");
	free_irq(platform_get_irq(pdev, 0), mc_drv_ctx);
	snd_soc_unregister_card(&snd_soc_card_sdp);
	kfree(mc_drv_ctx);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id sdp_audio_mc_dt_match[] = {
	{ .compatible = "samsung,sdp-mc-audio" },
	{},
};

MODULE_DEVICE_TABLE(of, sdp_audio_mc_dt_match);
static struct platform_driver snd_sdp_mc_driver = {
	.driver		= {
		.name		= "sdp-mc-audio.5",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_audio_mc_dt_match),		
	},
	.probe		= snd_sdp_mc_probe,
	.remove		= snd_sdp_mc_remove,
	
};
static const struct of_device_id sdp_audio_codec_dt_match[] = {
	{ .compatible = "samsung,sdp-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_audio_codec_dt_match);

extern int sdp_codec_device_probe(struct platform_device *pdev);
extern int sdp_codec_device_remove(struct platform_device *pdev);

static struct platform_driver sdp_codec_driver = {
	.driver		= {
		.name		= "sdp-codec.2",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_audio_codec_dt_match),		
	},
	.probe		= sdp_codec_device_probe,
	.remove		= sdp_codec_device_remove,

};

static const struct of_device_id sdp_audio_platform_dt_match[] = {
	{ .compatible = "samsung,sdp-platform" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_audio_platform_dt_match);

extern int sdp_audio_pcm_probe(struct platform_device *pdev);
extern int sdp_audio_pcm_remove(struct platform_device *pdev);

static struct platform_driver sdp_audio_platform_driver = {
	.driver		= {
		.name		= "sdp-platform.3",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_audio_platform_dt_match),		
	},
	.probe		= sdp_audio_pcm_probe,
	.remove		= sdp_audio_pcm_remove,
	
};


static const struct of_device_id sdp_audio_dma_dt_match[] = {
	{ .compatible = "samsung,sdp-dma" },
	{},
};

MODULE_DEVICE_TABLE(of, sdp_audio_dma_dt_match);

extern int sdp_audio_dma_probe(struct platform_device *pdev);
extern int sdp_audio_dma_remove(struct platform_device *pdev);

static struct platform_driver sdp_audio_dma_driver = {
	.driver		= {
		.name		= "sdp-dma.4",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_audio_dma_dt_match),		
	},
	.probe		= sdp_audio_dma_probe,
	.remove		= sdp_audio_dma_remove,
	
};

//module_platform_driver(snd_sdp_mc_driver);

#define AUDIO_DEV_NAME "sdp-audio"
#define AUDIO_DRIVER_VER		"0.1"

extern int __ref audio_probe(struct platform_device *pdev);
extern int __ref audio_remove(struct platform_device *pdev);
extern int audio_suspend(struct platform_device *pdev, pm_message_t state);
extern int audio_resume(struct platform_device *pdev);

static const struct of_device_id sdp_audio_dt_match[] = {
        { .compatible = "samsung,sdp-audio" },
        {},
};
MODULE_DEVICE_TABLE(of, sdp_audio_dt_match);

static struct platform_driver audio_platform_drv = {
        .probe  = audio_probe,
        .remove = audio_remove,
        .driver = {
                .name   = "sdp-audio.1", //AUDIO_DEV_NAME,
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(sdp_audio_dt_match),
        },
        .suspend = audio_suspend,
        .resume = audio_resume,
};
static const struct of_device_id sdp_audio_spdif_dt_match[] = {
        { .compatible = "samsung,sdp-audio-spdif" },
        {},
};
MODULE_DEVICE_TABLE(of, sdp_audio_spdif_dt_match);

//module_platform_driver(audio_platform_drv);


extern int __ref audio_spdif_remove(struct platform_device *pdev);
extern int __ref audio_spdif_probe(struct platform_device *pdev);

struct platform_driver audio_spdif_drv = {
        .probe  = audio_spdif_probe,
        .remove = audio_spdif_remove,
        .driver = {
                .name   = "sdp-audio-spdif", //AUDIO_DEV_NAME_SPDIF,
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(sdp_audio_spdif_dt_match),
        },
        //.suspend = audio_spdif_suspend,
        //.resume = audio_spdif_resume,
};

static const struct of_device_id sdp_sif_dt_match[] = {
        { .compatible = "samsung,sdp-sif" },
        {},
};
MODULE_DEVICE_TABLE(of, sdp_sif_dt_match);

extern int __ref sdp_sif_probe(struct platform_device *pdev);
extern int __ref sdp_sif_remove(struct platform_device *pdev);
extern int sdp_sif_suspend(struct platform_device *, pm_message_t state);
extern int sdp_sif_resume(struct platform_device *);


#define SDP_SIF_NAME "sdp-sif"

static struct platform_driver sdp_sif_driver = {
        .driver         = {
                .name           = SDP_SIF_NAME,
                .owner          = THIS_MODULE,
                .of_match_table = of_match_ptr(sdp_sif_dt_match),
        },
        .probe          = sdp_sif_probe,
        .remove         = sdp_sif_remove,
        .suspend = sdp_sif_suspend,
        .resume = sdp_sif_resume,

};

/************ALSA SIF *************/

#ifdef ALSA_SIF_INTEGRATION

/*CODEC*/

static const struct of_device_id sdp_sif_codec_dt_match[] = {
	{ .compatible = "samsung,sdp-sif-codec" },
	{},
};


MODULE_DEVICE_TABLE(of, sdp_sif_codec_dt_match);

extern int sdp_codec_sif_device_probe(struct platform_device *pdev);
extern int sdp_codec_sif_device_remove(struct platform_device *pdev);

static struct platform_driver sdp_sif_codec_driver = {
	.driver		= {
		.name		= "sdp-sif-codec.8",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_sif_codec_dt_match),		
	},
	.probe		= sdp_codec_sif_device_probe,
	.remove		= sdp_codec_sif_device_remove,

};

/*PLATFORM*/
static const struct of_device_id sdp_sif_platform_dt_match[] = {
	{ .compatible = "samsung,sdp-sif-platform" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_sif_platform_dt_match);

extern int sdp_sif_platform_probe(struct platform_device *pdev);
extern int sdp_sif_platform_remove(struct platform_device *pdev);

static struct platform_driver sdp_sif_platform_driver = {
	.driver		= {
		.name		= "sdp-sif-platform.9",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_sif_platform_dt_match),		
	},
	.probe		= sdp_sif_platform_probe,
	.remove		= sdp_sif_platform_remove,
};


/*MACHINE*/
extern int sdp_sif_machine_probe(struct platform_device *pdev);
extern int sdp_sif_machine_remove(struct platform_device *pdev);
extern int sdp_sif_suspend(struct platform_device * pdev, pm_message_t state);
extern int sdp_sif_resume(struct platform_device *pdev);

static const struct of_device_id sdp_sif_mc_dt_match[] = {
	{ .compatible = "samsung,sdp-mc-sif" },
	{},
};

MODULE_DEVICE_TABLE(of, sdp_sif_mc_dt_match);
static struct platform_driver sdp_sif_mc_driver = {
	.driver		= {
		.name		= "sdp-mc-sif.10",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(sdp_sif_mc_dt_match),		
	},
	.probe		= sdp_sif_machine_probe,
	.remove		= sdp_sif_machine_remove,
	.suspend	= sdp_sif_suspend,
	.resume		= sdp_sif_resume,
};

#endif
/**********END*******************/


static int __init audio_alsa_init(void)
{
    int retval = 0;

        // register driver
	retval = platform_driver_register(&audio_platform_drv);
    retval = platform_driver_register(&audio_spdif_drv);
    retval = platform_driver_register(&sdp_sif_driver);
    retval = platform_driver_register(&sdp_codec_driver);
    retval = platform_driver_register(&sdp_audio_platform_driver);
    retval = platform_driver_register(&sdp_audio_dma_driver);
    retval = platform_driver_register(&snd_sdp_mc_driver);
#ifdef ALSA_SIF_INTEGRATION

    retval = platform_driver_register(&sdp_sif_codec_driver);
    retval = platform_driver_register(&sdp_sif_platform_driver);
    retval = platform_driver_register(&sdp_sif_mc_driver);
#endif
    pr_debug("%s driver ver %s registerd\n", AUDIO_DEV_NAME, AUDIO_DRIVER_VER);

    return retval;

}


static void __exit audio_alsa_exit(void)
{
        // release platform driver
#ifdef ALSA_SIF_INTEGRATION
	platform_driver_unregister(&sdp_sif_mc_driver);
	platform_driver_unregister(&sdp_sif_platform_driver);
	platform_driver_unregister(&sdp_sif_codec_driver);
#endif
	platform_driver_unregister(&snd_sdp_mc_driver);
	platform_driver_unregister(&sdp_codec_driver);
	platform_driver_unregister(&sdp_audio_platform_driver);
	platform_driver_unregister(&sdp_audio_dma_driver);

	platform_driver_unregister(&audio_platform_drv);
	platform_driver_unregister(&audio_spdif_drv);
	platform_driver_unregister(&sdp_sif_driver);

    pr_debug("%s driver ver %s unregisterd\n",AUDIO_DEV_NAME, AUDIO_DRIVER_VER);

}


module_init(audio_alsa_init);
module_exit(audio_alsa_exit);

MODULE_DESCRIPTION("ASoC SDP Machine driver");
MODULE_AUTHOR("Kyung <kyonggo@samsung.com>");
MODULE_LICENSE("GPL v2");
