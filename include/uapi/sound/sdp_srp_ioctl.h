/*
 *
 * Copyright 2012 Samsung Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @file        srp_ioctl.h
 * @brief
 * @author      Yunji Kim (yunji.kim@samsung.com)
 * @version     1.1.0
 * @history
 *   2012.02.28 : Create
 */

#ifndef __SDP_SRP_IOCTL_H__
#define __SDP_SRP_IOCTL_H__


#define SRP_INIT_BLOCK_MODE             0
#define SRP_INIT_NONBLOCK_MODE          1

#define SRP_PENDING_STATE_RUNNING       0
#define SRP_PENDING_STATE_PENDING       1

struct srp_buf_info {
	void *mmapped_addr;
	void *addr;
	unsigned int mmapped_size;
	unsigned int size;
	int num;
};

struct srp_read_buf {
    void *mmapped_addr;
    void *addr;
    unsigned int *mmapped_size;
    unsigned int *size;
    int *num;
};

struct srp_dec_info {
	unsigned int sample_rate;
	unsigned int channels;
};

struct srp_set_extra_dec_info
{
	unsigned int eEncoding;		/**< Type of data expected for this port (e.g. PCM, AMR, MP3, etc) */

	unsigned int nChannels;		/**< Number of channels in the data stream (not
				 	     necessarily the same as the number of channels
				 	     to be rendered. */
	unsigned int nBitsPerSample; 	/**< Number of bits in each sample */
	unsigned int nSampleRate;	/**< Sampling rate of the source data.	Use 0 for
					     variable or unknown sampling rate. */
	unsigned int nBlockAlign;	/**< Number of block aligned */
	unsigned int nBitRate;		/**< Bit rate of the input data.  Use 0 for variable
					     rate or unknown bit rates */
	unsigned char	 *pCodecExtraData;
	unsigned int  nCodecExtraDataSize;
	unsigned int  nAudioCodecTag; 	// 4cc
	unsigned int  nUserInfo;
	unsigned int  nAudioDecodingType; // normal, clip, seamless, pvr

	unsigned int bSecureMode; // normal/secure mode

	unsigned int ePCMMode;	// adpcm type : 0x7F000001-QT, 0x7F000002-WAV, 0x7F000003-MS
	unsigned int eNumData;	//adpcm NumericalData type: 0-signed, 1-unsigned
	unsigned int eEndian;	// adpcm endian type : 0-big, 1-little

};


#define SRP_INIT			(0x10000)
#define SRP_DEINIT			(0x10001)
#define SRP_GET_MMAP_SIZE		(0x10002)
#define SRP_FLUSH			(0x20002)
#define SRP_SEND_EOS			(0x20005)
#define SRP_GET_IBUF_INFO		(0x20007)
#define SRP_GET_OBUF_INFO		(0x20008)
#define SRP_STOP_EOS_STATE		(0x30007)
#define SRP_GET_DEC_INFO		(0x30008)
#define SRP_GET_MAP_INFO		(0x30009)
#define SRP_READ			(0x3000A)
#define SRP_SET_DEC_INFO		(0x3000B)
#define SRP_SET_DEC_EXTRA_INFO		(0x3000C)
#define SRP_GET_TZ_HANDLE		(0x3000D)
#define SRP_SET_TZ_ENABLE		(0x3000E)
#define SRP_GET_PTS			(0x3000F)
#define SRP_SET_PTS			(0x30010)

#define CMD_ES_SUBMIT			(0x30020)
#define CMD_ES_FLUSH			(0x30021)
#define CMD_ES_CODEC_TYPE		(0x30022)


#endif /* __SDP_SRP_IOCTL_H__ */

