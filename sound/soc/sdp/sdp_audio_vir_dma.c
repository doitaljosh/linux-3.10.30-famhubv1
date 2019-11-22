#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>   
#include <linux/types.h>
#include <linux/unistd.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/asound.h>
#include <sound/compress_params.h>

#include "lldAioIsr.h"
#include "lldAioI2s.h"

#include "sdp_audio.h"
#include "sdp_audio_platform.h"
#include "sdp_audio_sys.h"
#include "sdp_audio_dvb.h"



extern sdp_vir_dma_pcm_params vir_dma_pcm[ALSA_DEVICE_COUNT];
sdp_vir_dma_es_params vir_dma_es;

EXPORT_SYMBOL_GPL(vir_dma_es);

Audio_UniOutData_t psUniOutData;
EXPORT_SYMBOL_GPL(psUniOutData);
extern sdp_compress_es_params comp_es_param;

// see sdp_audio_vir_dma.c
int sdp_audio_get_pcm_gain(void);

/*
total size : 4byte
es size    : 4byte
codec     : codec type
data start addr      : data
 */


void sdp_vir_dma_es_isr(void)
{
	int ret_audio=0;
	unsigned int buff_addr=0;
	unsigned int codec_type=0;

	pspIAeData_t pAeHndl = (pspIAeData_t)vir_dma_es.p_ae0_hndl; 
	devAe_VRegGet(pAeHndl->pDevAeHndl, AE_CODEC_TYPE_GET,  &codec_type);		

	if(!((codec_type == AUDIO_AC3) || (codec_type == AUDIO_EAC3) || (codec_type == AUDIO_DTS) ||(codec_type == AUDIO_HEAAC_LTP)))
		return;

	buff_addr = (unsigned int)vir_dma_es.pvir_buf + vir_dma_es.copied_total;

	memcpy((void *)vir_dma_es.pbuf ,(void *)buff_addr, *(unsigned int*)(buff_addr+0)); //total size

	psUniOutData.sEsData.u32Size = *(unsigned int*)(vir_dma_es.pbuf+4); //es size
	psUniOutData.sEsData.eEsCodecType = *(unsigned int*)(vir_dma_es.pbuf+8);      //codec type
	psUniOutData.sEsData.pu8LChannelAddr = (vir_dma_es.pbuf+ 12);	      //addr
	psUniOutData.enumUniDecType = AUDIO_HW_DEC;

//	pr_debug("sdp_vir_dma_es_isr, total size[%d], es size[%d], addr[0x%x], codec type[%d] \n", *(unsigned int*)buff_addr,psUniOutData.sEsData.u32Size,psUniOutData.sEsData.pu8LChannelAddr, psUniOutData.sEsData.eEsCodecType  );	


	ret_audio = dev_audio_ae_submituniplaydata(vir_dma_es.p_ae2_hndl, &psUniOutData);	
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]ae2 submit uniplay data error\n"); 	
	}


	vir_dma_es.copied_total = vir_dma_es.copied_total + *(unsigned int*)buff_addr;

	if (vir_dma_es.cb->compr_cb)
	{
		vir_dma_es.cb->compr_cb(vir_dma_es.es_cstream);	
	}

}



//static char dummy_buf[0x2000];//not used

//static unsigned int pre_s_time=0;//not used
extern int dev_audio_ae_submituniplaydata_pp_pcm(spIAeHndl_t spIH , Audio_UniOutData_t* pFrame);
unsigned int pp_pcm_buf_full_flag = false;
unsigned int pcm_buf_full_flag = false;
unsigned int main_mix_buf_full_flag = false;
unsigned int remote0_mix_buf_full_flag = false;


#define SAMPLE_256_PCM_VALUE	SDP_DMX_BUFFER_SIZE //1024 
#define SAMPLE_256_PCM_VALUE_EFFECT	1024 

void sdp_vir_dma_pcm_isr(void)
{

	int ret_audio=0;
	unsigned long long pu64PTS[1];
	unsigned int pu32FrmSize[1];
	Audio_UniOutData_t sData;
	unsigned int uiUniEnFlag = 0;
	Ae_OutputInfo_t sAeOutputInfo;	

	if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params)
	{
		if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
		{	
			
			if(	vir_dma_pcm[MM_ALSA_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_DEVICE].copied_total =0;
			}

			if(vir_dma_pcm[MM_ALSA_DEVICE].start_submit_pcm == 0)		
			{
				return; 		
			}

			pu64PTS[0] = 0; 

			if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.num_chan == 2)
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE; 
			else				
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE/2;

 			memcpy(vir_dma_pcm[MM_ALSA_DEVICE].pbuf, (char*)(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[MM_ALSA_DEVICE].copied_total) , pu32FrmSize[0]); 

			if(vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding)
			{
				vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding = FALSE;

				memset(&sData,0x00,sizeof(Audio_UniOutData_t));

				switch(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.sfreq)
				{
					case 48000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_48KHZ;
						break;

					case 44100:
						sAeOutputInfo.enumUniSampleRate = AUDIO_44_1KHZ;
						break;

					case 32000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_32KHZ;
						break;

					case 22050:
						sAeOutputInfo.enumUniSampleRate = AUDIO_22_05KHZ;
						break;

					case 16000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_16KHZ;
						break;

					case 8000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_8KHZ;
						break;

					default:
						sAeOutputInfo.enumUniSampleRate =  AUDIO_48KHZ;
						break;

				}		


				if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz == 32)
					sAeOutputInfo.u32BitWidth = 24;
				else
					sAeOutputInfo.u32BitWidth = vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz;

				sAeOutputInfo.u32ChannelNum= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.num_chan;
				sAeOutputInfo.bLittleEndian = 1;
				ret_audio = dev_audio_ae_set_uniplay_datainfo(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl, AUDIO_OUT_SPEAKER, &sAeOutputInfo);

			}

			sData.enumUniDecType = AUDIO_SW_DEC;
			sData.sPcmData.pu8LChannelAddr = vir_dma_pcm[MM_ALSA_DEVICE].pbuf;
			sData.sPcmData.pu8RChannelAddr = vir_dma_pcm[MM_ALSA_DEVICE].pbuf;	
			sData.sPcmData.u32Size = pu32FrmSize[0];	
			ret_audio = dev_audio_ae_submituniplaydata_pcm(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl, &sData);

			if(pcm_buf_full_flag != true)
			{
				vir_dma_pcm[MM_ALSA_DEVICE].copied_total = vir_dma_pcm[MM_ALSA_DEVICE].copied_total + pu32FrmSize[0];
			
				if (vir_dma_pcm[MM_ALSA_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[MM_ALSA_DEVICE].str_info->period_elapsed(vir_dma_pcm[MM_ALSA_DEVICE].str_info->mad_substream);
			}
		}
		else
		{
			char *pcDst, *pcDst2;
			
			if(	vir_dma_pcm[MM_ALSA_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_DEVICE].copied_total =0;
			}

			if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.num_chan == 2)
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE;
			else				
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE/2; 

			if( vir_dma_pcm[MM_ALSA_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_DEVICE].copied_total =0;
			}
			
			pcDst = (char*)(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[MM_ALSA_DEVICE].copied_total);
			pcDst2 = vir_dma_pcm[MM_ALSA_DEVICE].pbuf;

			ret_audio = dev_audio_ae_pcmCapture(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl, &vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding, &vir_dma_pcm[MM_ALSA_DEVICE].pcCaptureBasePtr, 
										&vir_dma_pcm[MM_ALSA_DEVICE].pcCaptureRpPtr, &vir_dma_pcm[MM_ALSA_DEVICE].uiCaptureSrcBufSize, 	pcDst, pcDst2, pu32FrmSize[0], 
										vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz);		

			vir_dma_pcm[MM_ALSA_DEVICE].copied_total = vir_dma_pcm[MM_ALSA_DEVICE].copied_total + pu32FrmSize[0];		


			if (vir_dma_pcm[MM_ALSA_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[MM_ALSA_DEVICE].str_info->period_elapsed(vir_dma_pcm[MM_ALSA_DEVICE].str_info->mad_substream);		

		}
	}

	
	if(vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf != NULL)
	{
		if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params)// (vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->device_type == 2))
		{
			if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
			{

				if(	vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total >= vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
				{
					vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total =0;
				}

				if(vir_dma_pcm[MAIN_ALSA_DEVICE].start_submit_pcm == 0)		
				{
					return; 		
				}
						
				pu64PTS[0] = 0; 

				if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.num_chan == 2)
					pu32FrmSize[0] = SAMPLE_256_PCM_VALUE_EFFECT;
				else				
					pu32FrmSize[0] = SAMPLE_256_PCM_VALUE_EFFECT/2; 

				memcpy(vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf, (char*)(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total) , pu32FrmSize[0]); 

				if(vir_dma_pcm[MAIN_ALSA_DEVICE].bfirstdecoding)
				{
					vir_dma_pcm[MAIN_ALSA_DEVICE].bfirstdecoding = FALSE;
					memset(&sData,0x00,sizeof(Audio_UniOutData_t));

					switch(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.sfreq)
					{
						case 48000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_48KHZ;
							break;

						case 44100:
							sAeOutputInfo.enumUniSampleRate = AUDIO_44_1KHZ;
							break;

						case 32000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_32KHZ;
							break;

						case 22050:
							sAeOutputInfo.enumUniSampleRate = AUDIO_22_05KHZ;
							break;

						case 16000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_16KHZ;
							break;

						case 8000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_8KHZ;
							break;

						default:
							sAeOutputInfo.enumUniSampleRate =  AUDIO_48KHZ;
							break;

					}


					if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz == 32)
						sAeOutputInfo.u32BitWidth = 24;
					else
						sAeOutputInfo.u32BitWidth = vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz;

					sAeOutputInfo.u32ChannelNum= vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.num_chan;
					sAeOutputInfo.bLittleEndian = 1;

			
					ret_audio = dev_audio_ae_setsubmitmixdatainfo(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl, AUDIO_OUT_MIX_SUB0, &sAeOutputInfo);
					if(ret_audio != AUDIO_OK)
					{
						pr_err("[audio]ae2 submit mix data info error\n"); 	
					}						

				}

				sData.enumUniDecType = AUDIO_SW_DEC;
				sData.sPcmData.pu8LChannelAddr = vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf;
				sData.sPcmData.pu8RChannelAddr = vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf;	
				sData.sPcmData.u32Size = pu32FrmSize[0];	

				ret_audio = dev_audio_ae_pcmmixingsubmit(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_0, &sData);	

				if(main_mix_buf_full_flag != true)
				{
				vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total = vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total + pu32FrmSize[0];
				if (vir_dma_pcm[MAIN_ALSA_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[MAIN_ALSA_DEVICE].str_info->period_elapsed(vir_dma_pcm[MAIN_ALSA_DEVICE].str_info->mad_substream);
			}	
			}	

		}	
	}
	if(vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf != NULL)
	{	
		if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params)// (vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->device_type == 3))
		{
			if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
			{
				if(	vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total >= vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
				{
					vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total =0;
				}
		
				if(vir_dma_pcm[REMOTE_ALSA_DEVICE].start_submit_pcm == 0)		
				{
					return; 		
				}

				pu64PTS[0] = 0; 
				if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.num_chan == 2)
					pu32FrmSize[0] = SAMPLE_256_PCM_VALUE_EFFECT;
				else				
					pu32FrmSize[0] = SAMPLE_256_PCM_VALUE_EFFECT/2; 

				memcpy(vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf, (char*)(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total) , pu32FrmSize[0]); 

				if(vir_dma_pcm[REMOTE_ALSA_DEVICE].bfirstdecoding)
				{
					vir_dma_pcm[REMOTE_ALSA_DEVICE].bfirstdecoding = FALSE;
					memset(&sData,0x00,sizeof(Audio_UniOutData_t));

					switch(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.sfreq)
					{
						case 48000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_48KHZ;
							break;

						case 44100:
							sAeOutputInfo.enumUniSampleRate = AUDIO_44_1KHZ;
							break;

						case 32000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_32KHZ;
							break;

						case 22050:
							sAeOutputInfo.enumUniSampleRate = AUDIO_22_05KHZ;
							break;

						case 16000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_16KHZ;
							break;

						case 8000:
							sAeOutputInfo.enumUniSampleRate = AUDIO_8KHZ;
							break;

						default:
							sAeOutputInfo.enumUniSampleRate =  AUDIO_48KHZ;
							break;

					}
		

					if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz == 32)
						sAeOutputInfo.u32BitWidth = 24;
					else
						sAeOutputInfo.u32BitWidth = vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.pcm_wd_sz;

					sAeOutputInfo.u32ChannelNum= vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.num_chan;
					sAeOutputInfo.bLittleEndian = 1;


					ret_audio = dev_audio_ae_setsubmitmixdatainfo(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl, AUDIO_OUT_MIX_SUB1, &sAeOutputInfo);
					if(ret_audio != AUDIO_OK)
					{
						pr_err("[audio]ae2 submit mix data info error\n"); 	
					}

				}

				sData.enumUniDecType = AUDIO_SW_DEC;
				sData.sPcmData.pu8LChannelAddr = vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf;
				sData.sPcmData.pu8RChannelAddr = vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf;	
				sData.sPcmData.u32Size = pu32FrmSize[0];	

				ret_audio = dev_audio_ae_pcmmixingsubmit(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_1, &sData);	
				if(remote0_mix_buf_full_flag != true)
				{
				vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total = vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total + pu32FrmSize[0];
				if (vir_dma_pcm[REMOTE_ALSA_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[REMOTE_ALSA_DEVICE].str_info->period_elapsed(vir_dma_pcm[REMOTE_ALSA_DEVICE].str_info->mad_substream);
				}
				}

		}
	}

	if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params)// (vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->device_type) == 4)
	{

		if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
		{
			if(vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding == TRUE)
				return;
			
			if(	vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total =0;
			}

			if(vir_dma_pcm[MM_ALSA_PP_DEVICE].start_submit_pcm == 0)		
			{
				return; 		
			}


			pu64PTS[0] = 0; 
			if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.num_chan == 2)
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE;
			else				
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE/2; 			


			memcpy(vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf, (char*)(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total) , pu32FrmSize[0]); 

			if(vir_dma_pcm[MM_ALSA_PP_DEVICE].bfirstdecoding)
			{
				vir_dma_pcm[MM_ALSA_PP_DEVICE].bfirstdecoding = FALSE;
				memset(&sData,0x00,sizeof(Audio_UniOutData_t));
				
				switch(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.sfreq)
				{
					case 48000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_48KHZ;
						break;

					case 44100:
						sAeOutputInfo.enumUniSampleRate = AUDIO_44_1KHZ;
						break;

					case 32000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_32KHZ;
						break;

					case 22050:
						sAeOutputInfo.enumUniSampleRate = AUDIO_22_05KHZ;
						break;

					case 16000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_16KHZ;
						break;

					case 8000:
						sAeOutputInfo.enumUniSampleRate = AUDIO_8KHZ;
						break;

					default:
						sAeOutputInfo.enumUniSampleRate =  AUDIO_48KHZ;
						break;

				}		


				if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.pcm_wd_sz == 32)
					sAeOutputInfo.u32BitWidth = 24;
				else
					sAeOutputInfo.u32BitWidth = vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.pcm_wd_sz;

				sAeOutputInfo.u32ChannelNum= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.num_chan;
				sAeOutputInfo.bLittleEndian = 1;		
				ret_audio = dev_audio_ae_set_uniplay_datainfo(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl, AUDIO_OUT_SPEAKER, &sAeOutputInfo);

			}

			sData.enumUniDecType = AUDIO_SW_DEC;
			sData.sPcmData.pu8LChannelAddr = vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf;
			sData.sPcmData.pu8RChannelAddr = vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf;	
			sData.sPcmData.u32Size = pu32FrmSize[0];	
			sData.eCodecType = (Audio_Codec_e)get_compress_codec_type();

			ret_audio = dev_audio_ae_submituniplaydata_pp_pcm(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl, &sData);

			if(pp_pcm_buf_full_flag != true)
			{
				vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total = vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total + pu32FrmSize[0];					
				if (vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->period_elapsed(vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->mad_substream);
			}
		}
		else
		{
			char *pcDst, *pcDst2;
			
			if(	vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total =0;
			}

			if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.num_chan == 2)
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE;
			else				
				pu32FrmSize[0] = SAMPLE_256_PCM_VALUE/2; 

			if( vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_size )
			{
				vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total =0;
			}
			
			pcDst = (char*)(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_addr + vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total);
			pcDst2 = vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf;

			ret_audio = dev_audio_ae_pcmCapture(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl, &vir_dma_pcm[MM_ALSA_PP_DEVICE].bfirstdecoding, &vir_dma_pcm[MM_ALSA_PP_DEVICE].pcCaptureBasePtr, 
										&vir_dma_pcm[MM_ALSA_PP_DEVICE].pcCaptureRpPtr, &vir_dma_pcm[MM_ALSA_PP_DEVICE].uiCaptureSrcBufSize, 	pcDst, pcDst2, pu32FrmSize[0], 
										vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.pcm_wd_sz);		

			vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total = vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total + pu32FrmSize[0];		


			if (vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->period_elapsed)
					vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->period_elapsed(vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->mad_substream);		

		}

	}


}




EXPORT_SYMBOL_GPL(sdp_vir_dma_pcm_isr);


EXPORT_SYMBOL_GPL(sdp_vir_dma_es_isr);

//static Ae_BootParam_s sBootParam;		//not used

//#define AIO_PCLK	320000000.0
#define AIO_PCLK	333330000.0


#define PERIODIC_ISR_256SAMPLE_48K (unsigned long)((AIO_PCLK*256.0)/48000.0)
#define PERIODIC_ISR_256SAMPLE_441K (unsigned long)((AIO_PCLK*256.0)/44100.0)
#define PERIODIC_ISR_256SAMPLE_32K (unsigned long)((AIO_PCLK*256.0)/32000.0)
#define PERIODIC_ISR_256SAMPLE_24K (unsigned long)((AIO_PCLK*256.0)/24000.0)
#define PERIODIC_ISR_256SAMPLE_2205K (unsigned long)((AIO_PCLK*256.0)/22050.0)
#define PERIODIC_ISR_256SAMPLE_16K (unsigned long)((AIO_PCLK*256.0)/16000.0)
#define PERIODIC_ISR_256SAMPLE_8K (unsigned long)((AIO_PCLK*256.0)/8000.0)
int sdp_vir_dma_compress_open(struct snd_sdp_params *str_params,
	struct sdp_compress_cb *cb)
{

	int ret_audio = 0;
	//unsigned int str_id = 0;//not used

	vir_dma_es.sdp_params = str_params;
	vir_dma_es.cb = cb;	
	vir_dma_es.es_cstream = vir_dma_es.cb->param;
	vir_dma_es.pvir_buf = phys_to_virt(vir_dma_es.sdp_params->aparams.ring_buf_info[0].addr);
	vir_dma_es.frag_size = vir_dma_es.sdp_params->aparams.frag_size;

//	pr_debug("sdp_vir_dma_compress_open, frag_size[%d], size[%d]\n", vir_dma_es.sdp_params->aparams.frag_size,vir_dma_es.sdp_params->aparams.ring_buf_info[0].size);

//	pr_debug("sdp_vir_dma_compress_open, [0x%x]\n", vir_dma_es.es_cstream);

	ret_audio = dev_audio_aio_open(AIO_INST0, &(vir_dma_es.p_aio_hndl), AUDIO_CHIP_ID_MP0);
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]aio compress open error\n");	
	}

	ret_audio = dev_audio_ae_open(AE_INST0, &(vir_dma_es.p_ae0_hndl), AUDIO_CHIP_ID_MP0);		
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]ae0 compress open error\n"); 	
	}	
	ret_audio = dev_audio_ae_open(AE_INST2, &(vir_dma_es.p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]ae2 compress open error\n");	
	}


	vir_dma_es.str_id = 1;

	return vir_dma_es.str_id;
}


//static Ae_SoftVolume_s pstrtSoftVolume;
#define ES_BUF_SIZE 0x10000

int sdp_vir_dma_compress_control(unsigned int cmd, unsigned int str_id)
{

	int ret_audio=0;

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:

			vir_dma_es.pbuf = (unsigned char*)kmalloc(ES_BUF_SIZE, GFP_KERNEL);

#if 0
			memset(&sBootParam, 0, sizeof(Ae_BootParam_s));
			sBootParam.args.strtUniPlay.enumCodecType = AUDIO_MPEG ;// vir_dma_es.sdp_params->sparams.uc.mp3_params.codec;
			sBootParam.args.strtUniPlay.uwBufSize = 224 * 1024; // 224K BUFFER SIZE
			sBootParam.args.strtUniPlay.uwWMSize	= 128;		  // WATER MARK
			sBootParam.args.strtUniPlay.enumParserType = AE_PARSER_SYNC_ES;
			sBootParam.enumDecoderType = AE_DECODERTYPE_UNI;
			dev_audio_ae_start(vir_dma_es.p_ae0_hndl, &sBootParam);
			dev_audio_ae_start(vir_dma_es.p_ae2_hndl, &sBootParam);
			dev_audio_ae_setport(vir_dma_es.p_ae2_hndl, AE_PORT_MAIN, AE_SRC_UNI,TRUE);
			vir_dma_es.start_submit_es = 1;
			pspIAeData_t pAeHndl = (pspIAeData_t)vir_dma_es.p_ae2_hndl;
			pstrtSoftVolume.u16VolumeIndex = 0x100/2;
			devAe_SetSoftVolume(pAeHndl->pDevAeHndl, &pstrtSoftVolume );
#endif 
			ret_audio = dev_audio_ae_setport(vir_dma_es.p_ae2_hndl, AE_PORT_MAIN, AE_SRC_UNI,TRUE);
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]ae2 compress set port error\n"); 
			}


			vir_dma_es.copied_total =0;
			vir_dma_es.bfirstdecoding =1;


			break;

		case SNDRV_PCM_TRIGGER_STOP:


			/*dvb_audio_info.audio_attributes_t ¿¡ AD bit ¸¦ clear*/
#if 0	
			dev_audio_ae_stop(vir_dma_es.p_ae0_hndl);
			dev_audio_ae_stop(vir_dma_es.p_ae2_hndl);
#endif 	
			kfree(vir_dma_es.pbuf);

			break;

		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			break;


		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			break;

		case SNDRV_PCM_TRIGGER_SUSPEND:
			break;

		case SND_COMPR_TRIGGER_DRAIN:
			break;

		default:
			pr_err(" wrong control_ops reported\n");
	}	

	return 0;	

}

int sdp_vir_dma_compress_tstamp(unsigned int str_id, struct snd_compr_tstamp *tstamp)
{

		//pr_debug("sdp_vir_dma_compress_tstamp\n");
	if(tstamp == NULL) {
		pr_err("%s: Error: time stamp is not initialized\n", __FUNCTION__);
		return AUDIO_ERR;
	}
	//	tstamp->byte_offset = pcm_audio_hanl->byte_offset;
	tstamp->copied_total = vir_dma_es.copied_total;
	//	tstamp->pcm_frames = pcm_audio_hanl->pcm_frames;
	//	tstamp->pcm_io_frames = pcm_audio_hanl->pcm_io_frames;
	tstamp->sampling_rate = 48000;// pcm_audio_hanl->sampling_rate;	

	return 0;

}

int sdp_vir_dma_compress_ack(unsigned int str_id, unsigned long bytes)
{
	//pr_debug("sdp_vir_dma_compress_ack\n");

	return 0;

}

int sdp_vir_dma_compress_close(unsigned int str_id)
{

	int ret_audio=0;


	ret_audio = dev_audio_ae_close(vir_dma_es.p_ae0_hndl); 
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]ae0 compress close error\n"); 
	}

	ret_audio = dev_audio_ae_close(vir_dma_es.p_ae2_hndl); 
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]ae2 compress close error\n"); 

	}	

	ret_audio = dev_audio_aio_close(vir_dma_es.p_aio_hndl); 
	if(ret_audio != AUDIO_OK)
	{
		pr_err("[audio]aio compress close error\n"); 
	}

	return 0;

}

int sdp_vir_dma_compress_get_caps(struct snd_compr_caps *caps)
{

//	pr_debug("sdp_vir_dma_compress_get_caps\n");


	caps->num_codecs = 1;
	caps->direction = 0;
	caps->min_fragment_size = 2048*5;
	caps->max_fragment_size	= 2048*5;

	caps->min_fragments	 =8;
	caps->max_fragments	 =8;
	caps->codecs[0]=	1;

	return 0;

}

int sdp_vir_dma_compress_get_codec_caps(struct snd_compr_codec_caps *codec)
{


//	pr_debug("sdp_vir_dma_compress_get_codec_caps\n");

	return 0;

}

static struct compress_sdp_ops sdp_vir_dma_compress_ops = {
	.open		= sdp_vir_dma_compress_open,
	.control	= sdp_vir_dma_compress_control,
	.tstamp		= sdp_vir_dma_compress_tstamp,
	.ack		= sdp_vir_dma_compress_ack,
	.close		= sdp_vir_dma_compress_close,
	.get_caps	= sdp_vir_dma_compress_get_caps,
	.get_codec_caps	= sdp_vir_dma_compress_get_codec_caps,
};

static Ae_PcmSoftVolume_s pstrtPcmSoftVolume;

int sdp_vir_dma_pcm_open(struct sdp_stream_params *str_param)
{
	//unsigned int str_id=0;//not used
	int ret_audio=0;
	pspIAeData_t pAeHndl = NULL;
	if(str_param->device_type == 1)
	{
		vir_dma_pcm[MM_ALSA_DEVICE].sdp_params = str_param;
		if(1)//str_param->ops == STREAM_OPS_PLAYBACK)
		{
			ret_audio = dev_audio_aio_open(AIO_INST0, &(vir_dma_pcm[MM_ALSA_DEVICE].p_aio_hndl), AUDIO_CHIP_ID_MP0);
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma aio open error\n"); 
				return -1;
			}

			ret_audio = dev_audio_ae_open(AE_INST2, &(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae2 open error\n"); 
				return -1;			
			}		
			ret_audio = dev_audio_ae_open(AE_INST0, &(vir_dma_pcm[MM_ALSA_DEVICE].p_ae0_hndl), AUDIO_CHIP_ID_MP0);		
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae0 open error\n"); 
				return -1;			
			}		
		}
		vir_dma_pcm[MM_ALSA_DEVICE].pbuf = (unsigned char*)kmalloc(SDP_DMX_BUFFER_SIZE, GFP_KERNEL);	

		if(str_param->ops == STREAM_OPS_PLAYBACK)
		{
			#if 0
			unsigned int uiUni_en=0;
			dev_audio_ae_get_uniEn(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl, &uiUni_en);
			
			if(uiUni_en == 0)
			{			
				//dev_audio_ae_setport(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl, AE_PORT_MAIN, AE_SRC_DTV_DEC0,TRUE); 
				if(ret_audio != AUDIO_OK)
				{
					pr_err("[audio]dma ae0 open error\n"); 
					return -1;				
				}		
				pr_debug("[audio]set port src-dec0\n");			
			}
			#endif
			pAeHndl = (pspIAeData_t)vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl;

			pstrtPcmSoftVolume.u16VolumeIndex = sdp_audio_get_pcm_gain();
			devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume );
		}

		vir_dma_pcm[MM_ALSA_DEVICE].copied_total =0;
		vir_dma_pcm[MM_ALSA_DEVICE].str_id=1;
		return vir_dma_pcm[MM_ALSA_DEVICE].str_id;

	}
	else if(str_param->device_type == 2)
	{
		vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params = str_param;
		if(1)//str_param->ops == STREAM_OPS_PLAYBACK)
		{
			ret_audio = dev_audio_aio_open(AIO_INST0, &(vir_dma_pcm[MAIN_ALSA_DEVICE].p_aio_hndl), AUDIO_CHIP_ID_MP0);
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma aio open error\n"); 
				return -1;
			}

				ret_audio = dev_audio_ae_open(AE_INST2, &(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae2 open error\n"); 
				return -1;			
			}		
				ret_audio = dev_audio_ae_open(AE_INST0, &(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae0_hndl), AUDIO_CHIP_ID_MP0);		
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae0 open error\n"); 
				return -1;			
			}		
		}
		vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf = (unsigned char*)kmalloc(SDP_DMX_BUFFER_SIZE, GFP_KERNEL);	

	if(str_param->ops == STREAM_OPS_PLAYBACK)
	{
		pspIAeData_t pAeHndl = NULL;
	#if 0
		unsigned int uiUni_en=0;		
		dev_audio_ae_get_uniEn(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl, &uiUni_en);
		
		if(uiUni_en == 0)
		{			
			//dev_audio_ae_setport(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl, AE_PORT_MAIN, AE_SRC_DTV_DEC0,TRUE); 
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae0 open error\n"); 
				return -1;				
			}		
			pr_debug("[audio]set port src-dec0\n");			
		}
	#endif
			pAeHndl = (pspIAeData_t)vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl;

		pstrtPcmSoftVolume.u16VolumeIndex = sdp_audio_get_pcm_gain();
		devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume );
	}

		vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total =0;
		vir_dma_pcm[MAIN_ALSA_DEVICE].str_id=2;
		return vir_dma_pcm[MAIN_ALSA_DEVICE].str_id;

	}
	else if(str_param->device_type == 3)
	{
		vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params = str_param;
		if(1)//str_param->ops == STREAM_OPS_PLAYBACK)
		{
			ret_audio = dev_audio_aio_open(AIO_INST0, &(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_aio_hndl), AUDIO_CHIP_ID_MP0);
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma aio open error\n"); 
				return -1;
			}

			ret_audio = dev_audio_ae_open(AE_INST2, &(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae2 open error\n"); 
				return -1;			
			}		
			ret_audio = dev_audio_ae_open(AE_INST0, &(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae0_hndl), AUDIO_CHIP_ID_MP0);		
			if(ret_audio != AUDIO_OK)
			{
				pr_err("[audio]dma ae0 open error\n"); 
				return -1;			
			}		
		}
		vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf = (unsigned char*)kmalloc(SDP_DMX_BUFFER_SIZE, GFP_KERNEL);	

		if(str_param->ops == STREAM_OPS_PLAYBACK)
		{
			pspIAeData_t pAeHndl = NULL;
		#if 0
			unsigned int uiUni_en=0;			
			dev_audio_ae_get_uniEn(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl, &uiUni_en);
			
			if(uiUni_en == 0)
			{			
				//dev_audio_ae_setport(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl, AE_PORT_MAIN, AE_SRC_DTV_DEC0,TRUE); 
				if(ret_audio != AUDIO_OK)
				{
					pr_err("[audio]dma ae0 open error\n"); 
					return -1;				
				}		
				pr_debug("[audio]set port src-dec0\n");			
			}
		#endif
			pAeHndl = (pspIAeData_t)vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl;

			pstrtPcmSoftVolume.u16VolumeIndex = sdp_audio_get_pcm_gain();
			devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume );
		}

		vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total =0;
		vir_dma_pcm[REMOTE_ALSA_DEVICE].str_id=3;
		return vir_dma_pcm[REMOTE_ALSA_DEVICE].str_id;

	}
#if 1	
	else if(str_param->device_type == 4) //PP ALSA
	{
#if 1	
			vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params = str_param;
			if(1)//str_param->ops == STREAM_OPS_PLAYBACK)
			{
				ret_audio = dev_audio_aio_open(AIO_INST0, &(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_aio_hndl), AUDIO_CHIP_ID_MP0);
				if(ret_audio != AUDIO_OK)
				{
					pr_err("[audio]dma aio open error\n"); 
					return -1;
				}
	
				ret_audio = dev_audio_ae_open(AE_INST2, &(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl), AUDIO_CHIP_ID_MP0);	
				if(ret_audio != AUDIO_OK)
				{
					pr_err("[audio]dma ae2 open error\n"); 
					return -1;			
				}		
				ret_audio = dev_audio_ae_open(AE_INST0, &(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae0_hndl), AUDIO_CHIP_ID_MP0);		
				if(ret_audio != AUDIO_OK)
				{
					pr_err("[audio]dma ae0 open error\n"); 
					return -1;			
				}		
			}
			vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf = (unsigned char*)kmalloc(SDP_DMX_BUFFER_SIZE, GFP_KERNEL);	
	
			if(str_param->ops == STREAM_OPS_PLAYBACK)
			{
				pspIAeData_t pAeHndl = NULL;
			#if 0
				unsigned int uiUni_en=0;
				dev_audio_ae_get_uniEn(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl, &uiUni_en);
				
				if(uiUni_en == 0)
				{			
					//dev_audio_ae_setport(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl, AE_PORT_MAIN, AE_SRC_DTV_DEC0,TRUE); 
					if(ret_audio != AUDIO_OK)
					{
						pr_err("[audio]dma ae0 open error\n"); 
						return -1;				
					}		
					pr_debug("[audio]set port src-dec0\n");			
				}
			#endif
			
				pAeHndl = (pspIAeData_t)vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl;
	
				pstrtPcmSoftVolume.u16VolumeIndex = sdp_audio_get_pcm_gain();
				devAe_SetPcmSoftVolume(pAeHndl->pDevAeHndl, &pstrtPcmSoftVolume );
			}
	
			vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total =0;
			vir_dma_pcm[MM_ALSA_PP_DEVICE].str_id=4;
			return vir_dma_pcm[MM_ALSA_PP_DEVICE].str_id;
#endif 	
		}
#endif 
		return vir_dma_pcm[MM_ALSA_PP_DEVICE].str_id;
}


int sdp_vir_dma_pcm_device_control(int cmd, void *arg)
{
//	int retval = 0;//not used
	int str_id = 0;
	unsigned int in_sample_rate =44100;

	Audio_Enable_e eMixEnable;
	struct pcm_stream_info *stream_info = (struct pcm_stream_info *)arg;

	if(stream_info->str_id ==1)
	{
		in_sample_rate = vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.sfreq;

		switch (cmd) {
		case SDP_SND_START:
//			pr_debug("sdp: Trigger Start\n");

			if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
			{
			}	
			
			//		vir_dma_pcm.copied_total =0;
			vir_dma_pcm[MM_ALSA_DEVICE].start_submit_pcm = 1;

			switch(in_sample_rate) {
				case 32000:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_32K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_32K[%ld]\n", PERIODIC_ISR_256SAMPLE_32K);
					break;

				case 44100:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_441K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_441K[%ld]\n", PERIODIC_ISR_256SAMPLE_441K);
					break;

				case 48000:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K ,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
					break;

				case 24000:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_24K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_24K[%ld]\n", PERIODIC_ISR_256SAMPLE_24K);
					break;

				case 22050:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_2205K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_2205K[%ld]\n", PERIODIC_ISR_256SAMPLE_2205K);
					break;

				case 16000:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_16K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_16K[%ld]\n", PERIODIC_ISR_256SAMPLE_16K);
					break;

				case 8000:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_8K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_8K[%ld]\n", PERIODIC_ISR_256SAMPLE_8K);

					break;

				default:
					lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K,AUDIO_CHIP_ID_MP0);
					pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
					break;
			}

				vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding =1;
			lldAioISREnable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);

			break;
				case SDP_SND_DROP:
					vir_dma_pcm[MM_ALSA_DEVICE].start_submit_pcm = 0;
					dev_audio_ae_uni_flushAudioOut(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl);						
					pr_debug("sdp: in stop\n");					
					break;
				case SDP_SND_PAUSE:
//					pr_debug("sdp: in pause\n");
						vir_dma_pcm[MM_ALSA_DEVICE].start_submit_pcm = 0;
						vir_dma_pcm[MM_ALSA_DEVICE].bfirstdecoding =0;
					break;
				case SDP_SND_RESUME:
						vir_dma_pcm[MM_ALSA_DEVICE].start_submit_pcm = 1;
//					pr_debug("sdp: in pause release\n");
					break;

				case SDP_SND_STREAM_INIT: {

					//pr_debug("stream init called\n");
						vir_dma_pcm[MM_ALSA_DEVICE].str_info = (struct pcm_stream_info *)arg;
						str_id = vir_dma_pcm[MM_ALSA_DEVICE].str_info->str_id;
	
					break;
				}

				case SDP_SND_BUFFER_POINTER: {
//						struct stream_info *stream;

					//	pr_debug("device_control pointer\n");

//						stream_info = (struct pcm_stream_info *)arg;
						if(vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
						{
							if( vir_dma_pcm[MM_ALSA_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
							{
						//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
								vir_dma_pcm[MM_ALSA_DEVICE].copied_total = 0;
							}
						}
						else
						{
							if( vir_dma_pcm[MM_ALSA_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
							{
						//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
								vir_dma_pcm[MM_ALSA_DEVICE].copied_total = 0;
							}
						}


						stream_info->buffer_ptr = vir_dma_pcm[MM_ALSA_DEVICE].copied_total;
						//		pr_debug("device_control pointer, buffer_ptr:0x%x, ring size[%d] \n",vir_dma_pcm.copied_total, vir_dma_pcm.sdp_params->sparams.ring_buffer_size);

						break;
					}

					default:
						return -EINVAL;
		}
	}
	else if(stream_info->str_id == 2)
	{	
		in_sample_rate = vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.sfreq;

		switch (cmd) {
			case SDP_SND_START:
	//			pr_debug("sdp: Trigger Start\n");

			if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
			{
				eMixEnable = AUDIO_ENABLE;
				dev_audio_ae_setmixonoff(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_0, eMixEnable);
			}	
				
				//		vir_dma_pcm.copied_total =0;
				vir_dma_pcm[MAIN_ALSA_DEVICE].start_submit_pcm = 1;

				switch(in_sample_rate) {
					case 32000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_32K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_32K[%ld]\n", PERIODIC_ISR_256SAMPLE_32K);
						break;

					case 44100:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_441K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_441K[%ld]\n", PERIODIC_ISR_256SAMPLE_441K);
						break;

					case 48000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K ,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;

					case 24000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_24K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_24K[%ld]\n", PERIODIC_ISR_256SAMPLE_24K);
						break;

					case 22050:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_2205K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_2205K[%ld]\n", PERIODIC_ISR_256SAMPLE_2205K);
						break;

					case 16000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_16K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_16K[%ld]\n", PERIODIC_ISR_256SAMPLE_16K);
						break;

					case 8000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_8K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_8K[%ld]\n", PERIODIC_ISR_256SAMPLE_8K);

						break;

					default:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;
				}

				vir_dma_pcm[MAIN_ALSA_DEVICE].bfirstdecoding =1;
				lldAioISREnable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);

				break;
					case SDP_SND_DROP:
						vir_dma_pcm[MAIN_ALSA_DEVICE].start_submit_pcm = 0;
						eMixEnable = AUDIO_DISABLE;
						dev_audio_ae_setmixonoff(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_0, eMixEnable);							
						pr_debug("sdp: in stop\n");					
						break;
					case SDP_SND_PAUSE:
	//					pr_debug("sdp: in pause\n");
						vir_dma_pcm[MAIN_ALSA_DEVICE].start_submit_pcm = 0;
						vir_dma_pcm[MAIN_ALSA_DEVICE].bfirstdecoding =0;
						break;
					case SDP_SND_RESUME:
						vir_dma_pcm[MAIN_ALSA_DEVICE].start_submit_pcm = 1;
	//					pr_debug("sdp: in pause release\n");
						break;

					case SDP_SND_STREAM_INIT: {

						//pr_debug("stream init called\n");
						vir_dma_pcm[MAIN_ALSA_DEVICE].str_info = (struct pcm_stream_info *)arg;
						str_id = vir_dma_pcm[MAIN_ALSA_DEVICE].str_info->str_id;
						break;
					}

					case SDP_SND_BUFFER_POINTER: {
//						struct pcm_stream_info *stream_info;
//						struct stream_info *stream;

						//	pr_debug("device_control pointer\n");
						if(vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
						{
							if( vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total >= vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
							{
						//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
								vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total = 0;
							}
						}
					else
		{
							if( vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total >= vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
							{
				//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
								vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total = 0;
							}
						}
						stream_info->buffer_ptr = vir_dma_pcm[MAIN_ALSA_DEVICE].copied_total;
						//		pr_debug("device_control pointer, buffer_ptr:0x%x, ring size[%d] \n",vir_dma_pcm.copied_total, vir_dma_pcm.sdp_params->sparams.ring_buffer_size);


						break;
					}

					default:
						return -EINVAL;
		}
	}
	else if(stream_info->str_id == 3)
	{
		in_sample_rate = vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.sfreq;

		switch (cmd) {
			case SDP_SND_START:
	//			pr_debug("sdp: Trigger Start\n");

			if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
			{
				eMixEnable = AUDIO_ENABLE;
				dev_audio_ae_setmixonoff(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_1, eMixEnable);
			}	
				
				//		vir_dma_pcm.copied_total =0;
				vir_dma_pcm[REMOTE_ALSA_DEVICE].start_submit_pcm = 1;

				switch(in_sample_rate) {
					case 32000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_32K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_32K[%ld]\n", PERIODIC_ISR_256SAMPLE_32K);
						break;

					case 44100:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_441K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_441K[%ld]\n", PERIODIC_ISR_256SAMPLE_441K);
						break;

					case 48000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K ,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;

					case 24000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_24K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_24K[%ld]\n", PERIODIC_ISR_256SAMPLE_24K);
						break;

					case 22050:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_2205K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_2205K[%ld]\n", PERIODIC_ISR_256SAMPLE_2205K);
						break;

					case 16000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_16K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_16K[%ld]\n", PERIODIC_ISR_256SAMPLE_16K);
						break;

					case 8000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_8K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_8K[%ld]\n", PERIODIC_ISR_256SAMPLE_8K);

						break;

					default:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;
				}

				vir_dma_pcm[REMOTE_ALSA_DEVICE].bfirstdecoding =1;
				lldAioISREnable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);

				break;
					case SDP_SND_DROP:
						vir_dma_pcm[REMOTE_ALSA_DEVICE].start_submit_pcm = 0;
						eMixEnable = AUDIO_DISABLE;
						dev_audio_ae_setmixonoff(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl,AE_MIXER_INST_1, eMixEnable);								
	//					pr_debug("sdp: in stop\n");					
						break;
					case SDP_SND_PAUSE:
	//					pr_debug("sdp: in pause\n");
						vir_dma_pcm[REMOTE_ALSA_DEVICE].start_submit_pcm = 0;
						vir_dma_pcm[REMOTE_ALSA_DEVICE].bfirstdecoding =0;
						break;
					case SDP_SND_RESUME:
						vir_dma_pcm[REMOTE_ALSA_DEVICE].start_submit_pcm = 1;
	//					pr_debug("sdp: in pause release\n");
						break;

					case SDP_SND_STREAM_INIT: {

						//pr_debug("stream init called\n");
						vir_dma_pcm[REMOTE_ALSA_DEVICE].str_info = (struct pcm_stream_info *)arg;
						str_id = vir_dma_pcm[REMOTE_ALSA_DEVICE].str_info->str_id;
		
						break;
					}

					case SDP_SND_BUFFER_POINTER: {
//						struct pcm_stream_info *stream_info;
//						struct stream_info *stream;

						//	pr_debug("device_control pointer\n");

//						stream_info = (struct pcm_stream_info *)arg;

						if(vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
						{
							if( vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total >= vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
					{
		//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
							vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total = 0;
			}
		}
		else
		{
							if( vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total >= vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params->sparams.ring_buffer_size )
					{
		//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
								vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total = 0;
					}
		}


						stream_info->buffer_ptr = vir_dma_pcm[REMOTE_ALSA_DEVICE].copied_total;
					//		pr_debug("device_control pointer, buffer_ptr:0x%x, ring size[%d] \n",vir_dma_pcm.copied_total, vir_dma_pcm.sdp_params->sparams.ring_buffer_size);

					break;
				}

				default:
					return -EINVAL;
	}
	}
#if 1
	else if(stream_info->str_id == 4)	//PP ALSA
	{
#if 1	
		in_sample_rate = vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.sfreq;	
		switch (cmd) {
			case SDP_SND_START:
	//			pr_debug("sdp: Trigger Start\n");
	
				if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
				{
				}	
				
				//		vir_dma_pcm.copied_total =0;
				vir_dma_pcm[MM_ALSA_PP_DEVICE].start_submit_pcm = 1;
	
				switch(in_sample_rate) {
					case 32000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_32K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_32K[%ld]\n", PERIODIC_ISR_256SAMPLE_32K);
						break;
	
					case 44100:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_441K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_441K[%ld]\n", PERIODIC_ISR_256SAMPLE_441K);
						break;
	
					case 48000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K ,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;
	
					case 24000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_24K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_24K[%ld]\n", PERIODIC_ISR_256SAMPLE_24K);
						break;
	
					case 22050:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_2205K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_2205K[%ld]\n", PERIODIC_ISR_256SAMPLE_2205K);
						break;
	
					case 16000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_16K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_16K[%ld]\n", PERIODIC_ISR_256SAMPLE_16K);
						break;
	
					case 8000:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_8K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_8K[%ld]\n", PERIODIC_ISR_256SAMPLE_8K);
	
						break;
	
					default:
						lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K,AUDIO_CHIP_ID_MP0);
						pr_debug("PERIODIC_ISR_256SAMPLE_48K[%ld]\n", PERIODIC_ISR_256SAMPLE_48K);
						break;
				}
	
				vir_dma_pcm[MM_ALSA_PP_DEVICE].bfirstdecoding =1;
				lldAioISREnable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);
	
				break;
					case SDP_SND_DROP:
							vir_dma_pcm[MM_ALSA_PP_DEVICE].start_submit_pcm = 0;
	//					pr_debug("sdp: in stop\n");					
						break;
					case SDP_SND_PAUSE:
	//					pr_debug("sdp: in pause\n");
							vir_dma_pcm[MM_ALSA_PP_DEVICE].start_submit_pcm = 0;
							vir_dma_pcm[MM_ALSA_PP_DEVICE].bfirstdecoding =0;
						break;
					case SDP_SND_RESUME:
							vir_dma_pcm[MM_ALSA_PP_DEVICE].start_submit_pcm = 1;
	//					pr_debug("sdp: in pause release\n");
						break;
	
					case SDP_SND_STREAM_INIT: {
	
						//pr_debug("stream init called\n");
							vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info = (struct pcm_stream_info *)arg;
							str_id = vir_dma_pcm[MM_ALSA_PP_DEVICE].str_info->str_id;
		
						break;
					}
	
					case SDP_SND_BUFFER_POINTER: {
	//						struct stream_info *stream;
	
						//	pr_debug("device_control pointer\n");
	
	//						stream_info = (struct pcm_stream_info *)arg;
	
							if(vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->ops == STREAM_OPS_PLAYBACK)
							{
								if( vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_size )
								{
							//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
									vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total = 0;
								}
							}
							else
							{
								if( vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total >= vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params->sparams.ring_buffer_size )
								{
							//		pr_debug("device_control pointer, vir_dma_pcm.copied_total >= %d \n", vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
									vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total = 0;
								}
							}
	
	
							stream_info->buffer_ptr = vir_dma_pcm[MM_ALSA_PP_DEVICE].copied_total;
							//		pr_debug("device_control pointer, buffer_ptr:0x%x, ring size[%d] \n",vir_dma_pcm.copied_total, vir_dma_pcm.sdp_params->sparams.ring_buffer_size);
	
							break;
						}
	
						default:
							return -EINVAL;
			}
	#endif 		
		}
	
#endif 
	
	return 0;

}

int sdp_vir_dma_pcm_close(unsigned int str_id)
{
//	pr_debug("sdp_vir_dma_pcm_close\n");

	if(str_id == 1)
	{
		dev_audio_ae_close(vir_dma_pcm[MM_ALSA_DEVICE].p_ae0_hndl); 
		dev_audio_ae_close(vir_dma_pcm[MM_ALSA_DEVICE].p_ae2_hndl); 
		dev_audio_aio_close(vir_dma_pcm[MM_ALSA_DEVICE].p_aio_hndl); 

		dev_audio_ae_out_buffer_clear(AUDIO_CHIP_ID_MP0);

		if((vir_dma_pcm[MM_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf == NULL)
			 && (vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf == NULL))
		{
			lldAioISRDisable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);	
		}
		
		kfree(vir_dma_pcm[MM_ALSA_DEVICE].pbuf);

		vir_dma_pcm[MM_ALSA_DEVICE].pbuf = NULL;
		vir_dma_pcm[MM_ALSA_DEVICE].sdp_params  = NULL;

	}
	else if(str_id == 2)	
	{
		dev_audio_ae_close(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae0_hndl); 
		dev_audio_ae_close(vir_dma_pcm[MAIN_ALSA_DEVICE].p_ae2_hndl); 
		dev_audio_aio_close(vir_dma_pcm[MAIN_ALSA_DEVICE].p_aio_hndl); 
	
//		dev_audio_ae_out_buffer_clear(AUDIO_CHIP_ID_MP0);
		dev_audio_ae_out_mix0_buffer_clear(AUDIO_CHIP_ID_MP0);

		if((vir_dma_pcm[MM_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf == NULL)
			 && (vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf == NULL))
		{
			lldAioISRDisable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);	
		}

	
		kfree(vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf);
		vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf = NULL;					
		vir_dma_pcm[MAIN_ALSA_DEVICE].sdp_params  = NULL;		

	}

	else if(str_id == 3)			
	{
		dev_audio_ae_close(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae0_hndl); 
		dev_audio_ae_close(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_ae2_hndl); 
		dev_audio_aio_close(vir_dma_pcm[REMOTE_ALSA_DEVICE].p_aio_hndl); 

//		dev_audio_ae_out_buffer_clear(AUDIO_CHIP_ID_MP0);
		dev_audio_ae_out_mix1_buffer_clear(AUDIO_CHIP_ID_MP0);

		if((vir_dma_pcm[MM_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf == NULL)
			 && (vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf == NULL))
		{
			lldAioISRDisable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);	
		}
		kfree(vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf);
		vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf = NULL;					
		vir_dma_pcm[REMOTE_ALSA_DEVICE].sdp_params  = NULL;		
	}	
#if 1	
	else if(str_id == 4) //PP ALSA
	{

#if 1	
		dev_audio_ae_close(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae0_hndl); 
		dev_audio_ae_close(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_ae2_hndl); 
		dev_audio_aio_close(vir_dma_pcm[MM_ALSA_PP_DEVICE].p_aio_hndl); 

//		dev_audio_ae_out_buffer_clear(AUDIO_CHIP_ID_MP0);

		if((vir_dma_pcm[MM_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf == NULL)
			 && (vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf == NULL) && (vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf == NULL))
		{
			lldAioISRDisable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);	
		}
		
		kfree(vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf);
		vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf = NULL;
		vir_dma_pcm[MM_ALSA_PP_DEVICE].sdp_params  = NULL;				
#endif 
	}	
#endif 	
	return 0;

}

static struct sdp_ops sdp_vir_dma_pcm_ops = {
	.open				= sdp_vir_dma_pcm_open,
	.device_control		= sdp_vir_dma_pcm_device_control,
	.close				= sdp_vir_dma_pcm_close,
};

struct sdp_device sdp_vir_dma_device;


static struct cdev  pcm_audio_dump_cdev;
static struct class *pcm_audio_dump_class;
static dev_t dump_device = 0;


#define PCM_DUMP_DEV_NAME "pcmdump"

#define PCM_DUMP_MAJOR			(241) 




extern const struct file_operations pcm_audio_dump_fops;

static char *pcm_dump_dev_devnode(struct device *dev, umode_t *mode)
{
	 *mode = 0660; // rw-rw--- what ever perm you want to set
	 return NULL;
}

static void pcm_dump_dev_create(void)
{
	int ret_val = 0;
	char dev_name[20];
	dump_device = MKDEV(PCM_DUMP_MAJOR, 1);
	snprintf(dev_name, 32, "%s%d", PCM_DUMP_DEV_NAME, 0);
	if(register_chrdev_region(dump_device, 1, PCM_DUMP_DEV_NAME)) {
		ret_val = alloc_chrdev_region(&dump_device, 0, 1, PCM_DUMP_DEV_NAME);
		if(ret_val) {
			pr_err("%s chrdev region failed\n", PCM_DUMP_DEV_NAME);
			return ;
		}
	}
	cdev_init(&pcm_audio_dump_cdev, &pcm_audio_dump_fops);
	cdev_add(&pcm_audio_dump_cdev, dump_device, 1);
	pcm_audio_dump_class = class_create(THIS_MODULE, "PCM_AUD_DUMP");

	if(IS_ERR(pcm_audio_dump_class)) {
		ret_val = PTR_ERR(pcm_audio_dump_class);
		if(ret_val != -EEXIST){
			cdev_del(&pcm_audio_dump_cdev);		
			pr_err("%s cdev add failed\n", PCM_DUMP_DEV_NAME);			
		}
	}
	
	pcm_audio_dump_class->devnode = pcm_dump_dev_devnode;
	device_create(pcm_audio_dump_class, NULL, dump_device, NULL, dev_name);
	
}

static void pcm_dump_dev_remove(void)
{
	device_destroy(pcm_audio_dump_class, PCM_DUMP_MAJOR);	
	class_destroy(pcm_audio_dump_class);
	cdev_del(&pcm_audio_dump_cdev);
	unregister_chrdev_region(dump_device, 1);
}



int sdp_audio_dma_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("sdp_audio_dma_probe\n");

	sdp_vir_dma_device.name = "dev_vir_dma";
	//	sdp_vir_dma_device.dev = pdev ;
	sdp_vir_dma_device.ops = &sdp_vir_dma_pcm_ops ;	
	sdp_vir_dma_device.compr_ops = &sdp_vir_dma_compress_ops;		
	pcm_dump_dev_create();
	ret = sdp_register_dsp(&sdp_vir_dma_device);	

	lldAioISRSetFunction(AIO_INT_PERIOD, (void *)sdp_vir_dma_pcm_isr, AUDIO_CHIP_ID_MP0);
	lldAio_SetPeriod(PERIODIC_ISR_256SAMPLE_48K , AUDIO_CHIP_ID_MP0);		

	//	lldAioISREnable(AIO_INT_PERIOD, AUDIO_CHIP_ID_MP0);

	vir_dma_pcm[MM_ALSA_DEVICE].pbuf = NULL;
	vir_dma_pcm[MAIN_ALSA_DEVICE].pbuf = NULL;
	vir_dma_pcm[REMOTE_ALSA_DEVICE].pbuf = NULL;
	vir_dma_pcm[MM_ALSA_PP_DEVICE].pbuf = NULL;	 //PP ALSA

	return ret;

}

int sdp_audio_dma_remove(struct platform_device *pdev)
{

	pr_debug("sdp_audio_dma_remove\n");
	sdp_unregister_dsp(&sdp_vir_dma_device);
	pcm_dump_dev_remove();
	return 0;
}


