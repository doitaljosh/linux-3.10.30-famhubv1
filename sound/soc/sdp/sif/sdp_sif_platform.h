#ifndef __SDP_PLATFORMDRV_H__
#define __SDP_PLATFORMDRV_H__

#include "../sdp_dsp.h"


#define SIF_RATES (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_22050 )
#define SIF_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE)

#define ALSA_DEVICE_COUNT 	4 //PP ALSA
#define MM_ALSA_DEVICE 		0
#define MAIN_ALSA_DEVICE 	1
#define REMOTE_ALSA_DEVICE 	2
#define MM_ALSA_PP_DEVICE 	3 //PP ALSA

#if 1//Arangam
#define SDP_DMX_BUFFER_SIZE 2048
#else
#define SDP_DMX_BUFFER_SIZE 768
#endif

//#define ALSA_SIF_INTEGRATION 1

struct pcm_stream_info {
	int str_id;
	void *mad_substream;
	void (*period_elapsed) (void *mad_substream);
	unsigned long long buffer_ptr;
//	unsigned int buffer_ptr;
	int sfreq;
};

enum sdp_drv_status {
	SDP_PLATFORM_INIT = 1,
	SDP_PLATFORM_STARTED,
	SDP_PLATFORM_RUNNING,
	SDP_PLATFORM_PAUSED,
	SDP_PLATFORM_DROPPED,
};

enum sdp_controls {
	SDP_SND_ALLOC =			0x00,
	SDP_SND_PAUSE =			0x01,
	SDP_SND_RESUME =		0x02,
	SDP_SND_DROP =			0x03,
	SDP_SND_FREE =			0x04,
	SDP_SND_BUFFER_POINTER =	0x05,
	SDP_SND_STREAM_INIT =		0x06,
	SDP_SND_START	 =		0x07,
	SDP_MAX_CONTROLS =		0x07,
};

enum sdp_stream_ops {
	STREAM_OPS_PLAYBACK = 0,
	STREAM_OPS_CAPTURE,
};

enum sdp_audio_device_type {
	SND_SDP_DEVICE_HEADSET = 1,
	SND_SDP_DEVICE_IHF,
	SND_SDP_DEVICE_VIBRA,
	SND_SDP_DEVICE_HAPTIC,
	SND_SDP_DEVICE_CAPTURE,
	SND_SDP_DEVICE_COMPRESS,
};

/* PCM Parameters */
struct sdp_pcm_params {
	u16 codec;	/* codec type */
	u8 num_chan;	/* 1=Mono, 2=Stereo */
	u8 pcm_wd_sz;	/* 16/24 - bit*/
	u32 reserved;	/* Bitrate in bits per second */
	u32 sfreq;	/* Sampling rate in Hz */
	u32 ring_buffer_size;
	u32 period_count;	/* period elapsed in samples*/
	u32 ring_buffer_addr;
};

struct sdp_stream_params {
	u32 result;
	u32 stream_id;
	u8 codec;
	u8 ops;
	u8 stream_type;
	u8 device_type;
	struct sdp_pcm_params sparams;
};

struct sdp_compress_cb {
	void *param;
	void (*compr_cb)(void *param);
};

struct compress_sdp_ops {
	const char *name;
	int (*open) (struct snd_sdp_params *str_params,
			struct sdp_compress_cb *cb);
	int (*control) (unsigned int cmd, unsigned int str_id);
	int (*tstamp) (unsigned int str_id, struct snd_compr_tstamp *tstamp);
	int (*ack) (unsigned int str_id, unsigned long bytes);
	int (*close) (unsigned int str_id);
	int (*get_caps) (struct snd_compr_caps *caps);
	int (*get_codec_caps) (struct snd_compr_codec_caps *codec);

};

struct sdp_ops {
	int (*open) (struct sdp_stream_params *str_param);
	int (*device_control) (int cmd, void *arg);
	int (*close) (unsigned int str_id);
};

struct sdp_runtime_stream {
	int     stream_status;
	unsigned int id;
	size_t bytes_written;
	struct pcm_stream_info stream_info;
	struct sdp_ops *ops;
	struct compress_sdp_ops *compr_ops;
	spinlock_t	status_lock;
};

struct sdp_device {
	char *name;
	struct device *dev;
	struct sdp_ops *ops;
	struct compress_sdp_ops *compr_ops;
};

//int sdp_register_dsp(struct sdp_device *sdp);
//int sdp_unregister_dsp(struct sdp_device *sdp);
#endif

