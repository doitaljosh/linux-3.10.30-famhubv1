#ifndef __SDP_DSP_H__
#define __SDP_DSP_H__

enum sdp_codec_types {
	/*  AUDIO/MUSIC	CODEC Type Definitions */
	SDP_CODEC_TYPE_UNKNOWN = 0,
	SDP_CODEC_TYPE_PCM,	/* Pass through Audio codec */
	SDP_CODEC_TYPE_MP3,
	SDP_CODEC_TYPE_MP24,
	SDP_CODEC_TYPE_AAC,
	SDP_CODEC_TYPE_AACP,
	SDP_CODEC_TYPE_eAACP,
};

enum stream_type {
	SDP_STREAM_TYPE_NONE = 0,
	SDP_STREAM_TYPE_MUSIC = 1,
};

struct snd_pcm_params {
	u16 codec;	/* codec type */
	u8 num_chan;	/* 1=Mono, 2=Stereo */
	u8 pcm_wd_sz;	/* 16/24 - bit*/
	u32 reserved;	/* Bitrate in bits per second */
	u32 sfreq;	/* Sampling rate in Hz */
	u8 use_offload_path;
	u8 reserved2;
	u16 reserved3;
	u8 channel_map[8];
} __packed;

/* MP3 Music Parameters Message */
struct snd_mp3_params {
	u16 codec;
	u8  num_chan;	/* 1=Mono, 2=Stereo	*/
	u8  pcm_wd_sz; /* 16/24 - bit*/
	u8  crc_check; /* crc_check - disable (0) or enable (1) */
	u8  reserved1; /* unused*/
	u16 reserved2;	/* Unused */
} __packed;

#define AAC_BIT_STREAM_ADTS		0
#define AAC_BIT_STREAM_ADIF		1
#define AAC_BIT_STREAM_RAW		2

/* AAC Music Parameters Message */
struct snd_aac_params {
	u16 codec;
	u8 num_chan; /* 1=Mono, 2=Stereo*/
	u8 pcm_wd_sz; /* 16/24 - bit*/
	u8 bdownsample; /*SBR downsampling 0 - disable 1 -enabled AAC+ only */
	u8 bs_format; /* input bit stream format adts=0, adif=1, raw=2 */
	u16  reser2;
	u32 externalsr; /*sampling rate of basic AAC raw bit stream*/
	u8 sbr_signalling;/*disable/enable/set automode the SBR tool.AAC+*/
	u8 reser1;
	u16  reser3;
} __packed;

/* WMA Music Parameters Message */
struct snd_wma_params {
	u16 codec;
	u8  num_chan;	/* 1=Mono, 2=Stereo */
	u8  pcm_wd_sz;	/* 16/24 - bit*/
	u32 brate;	/* Use the hard coded value. */
	u32 sfreq;	/* Sampling freq eg. 8000, 441000, 48000 */
	u32 channel_mask;  /* Channel Mask */
	u16 format_tag;	/* Format Tag */
	u16 block_align;	/* packet size */
	u16 wma_encode_opt;/* Encoder option */
	u8 op_align;	/* op align 0- 16 bit, 1- MSB, 2 LSB */
	u8 reserved;	/* reserved */
} __packed;

/* Codec params struture */
union  snd_sdp_codec_params {
	struct snd_pcm_params pcm_params;
	struct snd_mp3_params mp3_params;
	struct snd_aac_params aac_params;
	struct snd_wma_params wma_params;
} __packed;

/* Address and size info of a frame buffer */
struct sdp_address_info {
	u32 addr; /* Address at IA */
	u32 size; /* Size of the buffer */
};

struct snd_sdp_alloc_params_ext {
	struct sdp_address_info  ring_buf_info[8];
	u8 sg_count;
	u8 reserved;
	u16 reserved2;
	u32 frag_size;	/*Number of samples after which period elapsed
				  message is sent valid only if path  = 0*/
} __packed;

struct snd_sdp_stream_params {
	union snd_sdp_codec_params uc;
} __packed;

struct snd_sdp_params {
	u32 stream_id;
	u8 codec;
	u8 ops;
	u8 stream_type;
	u8 device_type;
	struct snd_sdp_alloc_params_ext aparams;
	struct snd_sdp_stream_params sparams;

};

#endif /* __SDP_DSP_H__ */

