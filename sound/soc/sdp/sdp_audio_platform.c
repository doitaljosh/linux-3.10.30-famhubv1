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
#include "sdp_audio_platform.h"

#define SDP_ALLOC_DMA 0

static struct sdp_device *sdp_mm_dev;


static DEFINE_MUTEX(sdp_lock);

int sdp_register_dsp(struct sdp_device *dev)
{

	pr_debug("sdp_register_dsp\n");

#if 0
	BUG_ON(!dev);
	if (!try_module_get(dev->dev->driver->owner))
		return -ENODEV;
	mutex_lock(&sdp_lock);
	if (sdp) {
		pr_debug("we already have a device %s\n", sdp->name);
		module_put(dev->dev->driver->owner);
		mutex_unlock(&sdp_lock);
		return -EEXIST;
	}

	pr_debug("registering device %s\n", dev->name);
	sdp = dev;
	mutex_unlock(&sdp_lock);
#endif 		
	if (sdp_mm_dev) {
		pr_debug("we already have a device %s\n", sdp_mm_dev->name);
		return -EEXIST;
	}
	sdp_mm_dev = dev;
	return 0;
}
EXPORT_SYMBOL_GPL(sdp_register_dsp);

int sdp_unregister_dsp(struct sdp_device *dev)
{

	pr_debug("sdp_unregister_dsp\n");

#if 0
	BUG_ON(!dev);
	if (dev != sdp)
		return -EINVAL;

	mutex_lock(&sdp_lock);

	if (!sdp) {
		mutex_unlock(&sdp_lock);
		return -EIO;
	}

	module_put(sdp->dev->driver->owner);
	pr_debug("unreg %s\n", sdp->name);
	sdp = NULL;
	mutex_unlock(&sdp_lock);
#endif 	
	if (dev != sdp_mm_dev)
		return -EINVAL;

	if (!sdp_mm_dev) {	
		return -EIO;
	}

	pr_debug("unreg %s\n", sdp_mm_dev->name);
	sdp_mm_dev = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(sdp_unregister_dsp);

#if SDP_ALLOC_DMA
static struct snd_pcm_hardware sdp_platform_pcm_hw = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_DOUBLE |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_RESUME |
			SNDRV_PCM_INFO_MMAP|
			SNDRV_PCM_INFO_MMAP_VALID |
			/*SNDRV_PCM_INFO_BLOCK_TRANSFER |*/
			SNDRV_PCM_INFO_SYNC_START),
	.formats = (SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_U16 |
			SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_U24 |
			SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |			
			SNDRV_PCM_FMTBIT_S32 | SNDRV_PCM_FMTBIT_U32),
	.rates = (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_22050 ),
	.rate_min = SDP_MIN_RATE,
	.rate_max = SDP_MAX_RATE,
	.channels_min =	SDP_MIN_CHANNEL,
	.channels_max =	SDP_MAX_CHANNEL,
	.buffer_bytes_max = SDP_MAX_BUFFER,
	.period_bytes_min = SDP_MIN_PERIOD_BYTES,
	.period_bytes_max = SDP_MAX_PERIOD_BYTES,
	.periods_min = SDP_MIN_PERIODS,
	.periods_max = SDP_MAX_PERIODS,
	.fifo_size = SDP_FIFO_SIZE,
};
#else
static struct snd_pcm_hardware sdp_platform_pcm_hw = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_DOUBLE |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_RESUME |
			SNDRV_PCM_INFO_MMAP|
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_SYNC_START),
	.formats = (SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_U16 |
			SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_U24 |
			SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |			
			SNDRV_PCM_FMTBIT_S32 | SNDRV_PCM_FMTBIT_U32),
	.rates = (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_22050 ),
	.rate_min = SDP_MIN_RATE,
	.rate_max = SDP_MAX_RATE,
	.channels_min =	SDP_MIN_CHANNEL,
	.channels_max =	SDP_MAX_CHANNEL,
	.buffer_bytes_max = SDP_MAX_BUFFER,
	.period_bytes_min = SDP_MIN_PERIOD_BYTES,
	.period_bytes_max = SDP_MAX_PERIOD_BYTES,
	.periods_min = SDP_MIN_PERIODS,
	.periods_max = SDP_MAX_PERIODS,
	.fifo_size = SDP_FIFO_SIZE,
};
#endif


/* helper functions */
inline void sdp_set_stream_status(struct sdp_runtime_stream *stream,
					int state)
{
	unsigned long flags;
	spin_lock_irqsave(&stream->status_lock, flags);
	stream->stream_status = state;
	spin_unlock_irqrestore(&stream->status_lock, flags);
}
EXPORT_SYMBOL_GPL(sdp_set_stream_status);
static inline int sdp_get_stream_status(struct sdp_runtime_stream *stream)
{
	int state;
	unsigned long flags;

	spin_lock_irqsave(&stream->status_lock, flags);
	state = stream->stream_status;
	spin_unlock_irqrestore(&stream->status_lock, flags);
	return state;
}

static void sdp_fill_pcm_params(struct snd_pcm_substream *substream,
				struct sdp_pcm_params *param)
{

	param->codec = SDP_CODEC_TYPE_PCM;
	param->num_chan = (u8) substream->runtime->channels;
	param->pcm_wd_sz = substream->runtime->sample_bits;
	param->reserved = 0;
	param->sfreq = substream->runtime->rate;
	param->ring_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	param->period_count = substream->runtime->period_size;
//	param->ring_buffer_addr = virt_to_phys(substream->dma_buffer.area);
	param->ring_buffer_addr = (unsigned int)substream->dma_buffer.area;
//	pr_debug("period_cnt = %d\n", param->period_count);
//	pr_debug("sfreq= %d, wd_sz = %d ring buffer addr::%x size:%x size2:%x\n", param->sfreq, param->pcm_wd_sz, 
//		param->ring_buffer_addr, param->ring_buffer_size, substream->runtime->buffer_size);
}

struct sdp_pcm_params param[4] = {0};
struct sdp_stream_params str_params[4] = {0};

static int sdp_platform_alloc_stream(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream = substream->runtime->private_data;
	int ret_val;

	/* set codec params and inform SDP driver the same */
	sdp_fill_pcm_params(substream, &param[substream->pcm->device]);
	substream->runtime->dma_area = substream->dma_buffer.area;
	str_params[substream->pcm->device].sparams = param[substream->pcm->device];
	str_params[substream->pcm->device].codec =  param[substream->pcm->device].codec;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		str_params[substream->pcm->device].ops = STREAM_OPS_PLAYBACK;
		str_params[substream->pcm->device].device_type = substream->pcm->device + 1;
		pr_debug("Playbck stream,Device %d\n",
					substream->pcm->device);
	} else {
		str_params[substream->pcm->device].ops = STREAM_OPS_CAPTURE;
		str_params[substream->pcm->device].device_type = SND_SDP_DEVICE_CAPTURE;
		pr_debug("Capture stream,Device Arangam %d\n",
					substream->pcm->device);
	}
	ret_val = stream->ops->open(&str_params[substream->pcm->device]);
//	pr_err("SDP_SND_PLAY/CAPTURE ret_val = %x DMA Area::%x\n", ret_val, substream->runtime->dma_area);
	if (ret_val < 0)
		return ret_val;

	stream->stream_info.str_id = ret_val;
	pr_debug("str id : %d, device_type : %d, stream[0x%x] \n",str_params[substream->pcm->device].device_type,  stream->stream_info.str_id, substream->runtime->private_data);
	return ret_val;
}

static void sdp_period_elapsed(void *mad_substream)
{
	struct snd_pcm_substream *substream = mad_substream;
	struct sdp_runtime_stream *stream;
	int status;

	if (!substream || !substream->runtime)
		return;
	stream = substream->runtime->private_data;
	if (!stream)
		return;
	status = sdp_get_stream_status(stream);
	if (status != SDP_PLATFORM_RUNNING)
		return;
	snd_pcm_period_elapsed(substream);
}

static int sdp_platform_init_stream(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream = substream->runtime->private_data;
	int ret_val;

//	pr_debug("setting buffer ptr param\n");
	sdp_set_stream_status(stream, SDP_PLATFORM_INIT);
	stream->stream_info.period_elapsed = sdp_period_elapsed;
	stream->stream_info.mad_substream = substream;
	stream->stream_info.buffer_ptr = 0;
	stream->stream_info.sfreq = substream->runtime->rate;
	ret_val = stream->ops->device_control(
			SDP_SND_STREAM_INIT, &stream->stream_info);
	if (ret_val)
		pr_err("control_set ret error %d\n", ret_val);
	return ret_val;

}
/* end -- helper functions */

static int sdp_platform_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sdp_runtime_stream *stream;
	int ret_val;

//	pr_debug("sdp_platform_open called\n");

	snd_soc_set_runtime_hwparams(substream, &sdp_platform_pcm_hw);

	
	ret_val = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);

	if (ret_val < 0)
		return ret_val;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;
	spin_lock_init(&stream->status_lock);

	/* get the sdp ops */
	mutex_lock(&sdp_lock);
	if (!sdp_mm_dev) {
		pr_err("no device available to run\n");
		mutex_unlock(&sdp_lock);
		kfree(stream);
		return -ENODEV;
	}
#if 0	
	if (!try_module_get(sdp->dev->driver->owner)) {
		mutex_unlock(&sdp_lock);
		kfree(stream);
		return -ENODEV;
	}
#endif 	
	stream->ops = sdp_mm_dev->ops;
	mutex_unlock(&sdp_lock);


	stream->stream_info.str_id = 0;
	sdp_set_stream_status(stream, SDP_PLATFORM_INIT);

	stream->stream_info.mad_substream = substream;
	/* allocate memory for SDP API set */
	runtime->private_data = stream;

//	pr_debug("sdp_platform_open end\n");

	return 0;
}

static int sdp_platform_close(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream;
	int ret_val = 0, str_id;

//	pr_debug("sdp_platform_close called\n");
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	if (str_id)
		ret_val = stream->ops->close(str_id);
//	module_put(sdp->dev->driver->owner);
	kfree(stream);
	return ret_val;
}

static int sdp_platform_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream;
	int ret_val = 0;
	int str_id = 0;

//	pr_debug("sdp_platform_pcm_prepare called\n");
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	if (stream->stream_info.str_id) {
		ret_val = stream->ops->device_control(
				SDP_SND_DROP, &stream->stream_info);
		return ret_val;
	}

	ret_val = sdp_platform_alloc_stream(substream);
	if (ret_val < 0)
		return ret_val;
	snprintf(substream->pcm->id, sizeof(substream->pcm->id),
			"%d", stream->stream_info.str_id);

	ret_val = sdp_platform_init_stream(substream);
	if (ret_val)
		return ret_val;
#if SDP_ALLOC_DMA
	substream->runtime->hw.info = SNDRV_PCM_INFO_MMAP;//SNDRV_PCM_INFO_MMAP_VALID;//SNDRV_PCM_INFO_BLOCK_TRANSFER;
#else
	substream->runtime->hw.info = SNDRV_PCM_INFO_BLOCK_TRANSFER;
#endif 	
	return ret_val;
}

static int sdp_platform_pcm_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	int ret_val = 0;
	int str_id = 0;
	struct sdp_runtime_stream *stream;
	int str_cmd, status;

	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
//		pr_debug("sdp: Trigger Start\n");
		printk(KERN_INFO "sdp_platform_pcm_trigger: cmd: %d [%s:pcm%d], [pid:%d,%s]\n", cmd, substream->name, substream->pcm->device, current->pid, current->comm);
		str_cmd = SDP_SND_START;
		status = SDP_PLATFORM_RUNNING;
		stream->stream_info.mad_substream = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
//		pr_debug("sdp: in stop\n");
		str_cmd = SDP_SND_DROP;
		status = SDP_PLATFORM_DROPPED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
//		pr_debug("sdp: in pause\n");
		str_cmd = SDP_SND_PAUSE;
		status = SDP_PLATFORM_PAUSED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
//		pr_debug("sdp: in pause release\n");
		str_cmd = SDP_SND_RESUME;
		status = SDP_PLATFORM_RUNNING;
		break;
	default:
		return -EINVAL;
	}
	ret_val = stream->ops->device_control(str_cmd, &stream->stream_info);
	if (!ret_val)
		sdp_set_stream_status(stream, status);

	return ret_val;
}


static snd_pcm_uframes_t sdp_platform_pcm_pointer
			(struct snd_pcm_substream *substream)
{
	struct sdp_runtime_stream *stream;
	int ret_val, status;
	struct pcm_stream_info *str_info;

//	pr_debug("sdp_platform_pcm_pointer\n");

	stream = substream->runtime->private_data;
	status = sdp_get_stream_status(stream);
	if (status == SDP_PLATFORM_INIT)
		return 0;
	str_info = &stream->stream_info;
	ret_val = stream->ops->device_control(
				SDP_SND_BUFFER_POINTER, str_info);
	if (ret_val) {
		pr_err("sdp: error code = %d\n", ret_val);
		return ret_val;
	}
	return bytes_to_frames(substream->runtime,stream->stream_info.buffer_ptr);
}



#if SDP_ALLOC_DMA

struct dma_coherent_mem {
	void		*virt_base;
	dma_addr_t	device_base;
	phys_addr_t	pfn_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};


static u64 sdp_pcm_dmamask = DMA_BIT_MASK(64);

static int sdp_pcm_allocate_dma_buffer(struct snd_pcm_substream *substream)
{
	struct dma_coherent_mem *mem;
	size_t size = SDP_MAX_BUFFER;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	substream->dma_buffer.dev.dev->dma_mask = &sdp_pcm_dmamask;
	substream->dma_buffer.dev.dev->coherent_dma_mask = (unsigned long long)DMA_BIT_MASK(64);

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(substream->dma_buffer.dev.dev, size,
			&buf->addr, GFP_KERNEL);
	//printk("sdp-pcm: alloc dma buffer: area=%p, addr=%p, size=%d\n",
	//		(void *)buf->area, (void *)buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;

	substream->dma_buffer.dev.dev->dma_mem = mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!mem->bitmap)
		return -ENOMEM;
	
	mem->virt_base = buf->area;
	mem->device_base = buf->addr;
	mem->size = pages;
	mem->pfn_base = PFN_DOWN(buf->addr);
	mem->flags = DMA_MEMORY_MAP;
#if 0
	printk("+++++++++++++++++++ %s:%d size(%x) mem->size(%x)  mem->virt_base(%x) mem->device_base(%x) pcm->card->dev->dma_mem(%x) ++++++++++++++++++++++\n",
							__func__, __LINE__,
							size,
							mem->size,
							mem->virt_base,
							mem->device_base,
							substream->dma_buffer.dev.dev->dma_mem->size);
#endif	
	return 0;
}

static int sdp_pcm_free_dma_buffer(struct snd_pcm_substream *substream)
{
	//TODO: free allocated memory in sdp_platform_pcm_hw_params
	
	return 0;
}
#endif

static int sdp_platform_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
#if SDP_ALLOC_DMA
	unsigned long totbytes = params_buffer_bytes(params);
	sdp_pcm_allocate_dma_buffer(substream);
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
#else	
	snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
#endif
	memset(substream->runtime->dma_area, 0, params_buffer_bytes(params));

	return 0;
}

static int sdp_platform_pcm_hw_free(struct snd_pcm_substream *substream)
{
	//	printk("sdp_platform_pcm_hw_free\n");
#if SDP_ALLOC_DMA

	return sdp_pcm_free_dma_buffer(substream);
#else

	return snd_pcm_lib_free_pages(substream);
#endif
}

static struct snd_pcm_ops sdp_platform_ops = {
	.open = sdp_platform_open,
	.close = sdp_platform_close,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = sdp_platform_pcm_prepare,
	.trigger = sdp_platform_pcm_trigger,
	.pointer = sdp_platform_pcm_pointer,
	.hw_params = sdp_platform_pcm_hw_params,
	.hw_free = sdp_platform_pcm_hw_free,
};

static void sdp_pcm_free(struct snd_pcm *pcm)
{
	pr_debug("sdp_pcm_free called\n");
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int sdp_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int retval = 0;

	pr_debug("sdp_pcm_new called\n");

#if SDP_ALLOC_DMA

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream)
	{
		if(!(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->dma_buffer.dev.dev = kzalloc(sizeof(struct device), GFP_KERNEL))){
			printk("%s:%d  not enough memory - memory allocation failed\n");
			return -1;
		}
		pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->dma_buffer.dev.dev->parent = card->dev;
	}
	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream)
	{
		if(!(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->dma_buffer.dev.dev = kzalloc(sizeof(struct device), GFP_KERNEL))){
			printk("%s:%d  not enough memory - memory allocation failed\n");
			return -1;
		}
		pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->dma_buffer.dev.dev->parent = card->dev;
	}
#else	

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		retval =  snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL),
			SDP_MIN_BUFFER, SDP_MAX_BUFFER);
		if (retval) {
			pr_err("dma buffer allocationf fail\n");
			return retval;
		}
	}
	
#endif

	return retval;
}

/* compress stream operations */
void sdp_compr_fragment_elapsed(void *arg)
{
	struct snd_compr_stream *cstream = (struct snd_compr_stream *)arg;

//	pr_debug("fragment elapsed by driver. cstream[0x%x] \n", cstream);
	if (cstream)
		snd_compr_fragment_elapsed(cstream);


//	pr_debug("fragment elapsed by driver. end\n");	
}
EXPORT_SYMBOL_GPL(sdp_compr_fragment_elapsed);

static int sdp_platform_compr_open(struct snd_compr_stream *cstream)
{

	int ret_val = 0;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sdp_runtime_stream *stream;

//	pr_debug("alsa compress open\n");

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;
	spin_lock_init(&stream->status_lock);

	/* get the sdp ops */
	if (!sdp_mm_dev) /*!try_module_get(sdp->dev.driver.owner))*/{
		pr_err("no device available to run\n");
		ret_val = -ENODEV;
		goto out_ops;
	}
	stream->compr_ops = sdp_mm_dev->compr_ops;

	stream->id = 0;
	sdp_set_stream_status(stream, SDP_PLATFORM_INIT);
	runtime->private_data = stream;
	return 0;
out_ops:
	kfree(stream);
	return ret_val;
}

static int sdp_platform_compr_free(struct snd_compr_stream *cstream)
{
	struct sdp_runtime_stream *stream;
	int ret_val = 0, str_id;

//	pr_debug("alsa compress free\n");

	stream = cstream->runtime->private_data;
	/*need to check*/
	str_id = stream->id;
	if (str_id)
		ret_val = stream->compr_ops->close(str_id);
//KYUNG 	module_put(sdp->dev->driver->owner);
	kfree(stream);
	pr_debug("%s: %d\n", __func__, ret_val);
	return 0;
}

static int sdp_platform_compr_set_params(struct snd_compr_stream *cstream,
					struct snd_compr_params *params)
{
	struct sdp_runtime_stream *stream;
	int retval;
	static struct snd_sdp_params str_params;
	static struct sdp_compress_cb cb;

//	pr_debug("alsa compress set params , cstream[0x%x]\n", cstream);
	stream = cstream->runtime->private_data;
	/* construct fw structure for this*/
	memset(&str_params, 0, sizeof(str_params));

	str_params.ops = STREAM_OPS_PLAYBACK;
	str_params.stream_type = SDP_STREAM_TYPE_MUSIC;
	str_params.device_type = SND_SDP_DEVICE_COMPRESS;

	switch (params->codec.id) {
	case SND_AUDIOCODEC_MP3: {
		str_params.codec = SDP_CODEC_TYPE_MP3;
		str_params.sparams.uc.mp3_params.codec = SDP_CODEC_TYPE_MP3;
		str_params.sparams.uc.mp3_params.num_chan = params->codec.ch_in;
		str_params.sparams.uc.mp3_params.pcm_wd_sz = 16;
		break;
	}

	case SND_AUDIOCODEC_AAC: {
		str_params.codec = SDP_CODEC_TYPE_AAC;
		str_params.sparams.uc.aac_params.codec = SDP_CODEC_TYPE_AAC;
		str_params.sparams.uc.aac_params.num_chan = params->codec.ch_in;
		str_params.sparams.uc.aac_params.pcm_wd_sz = 16;
		if (params->codec.format == SND_AUDIOSTREAMFORMAT_MP4ADTS)
			str_params.sparams.uc.aac_params.bs_format =
							AAC_BIT_STREAM_ADTS;
		else if (params->codec.format == SND_AUDIOSTREAMFORMAT_RAW)
			str_params.sparams.uc.aac_params.bs_format =
							AAC_BIT_STREAM_RAW;
		else {
			pr_err("Undefined format%d\n", params->codec.format);
			return -EINVAL;
		}
		str_params.sparams.uc.aac_params.externalsr =
						params->codec.sample_rate;
		break;
	}

	default:
		pr_err("codec not supported, id =%d\n", params->codec.id);
		return -EINVAL;
	}

	str_params.aparams.frag_size = cstream->runtime->fragment_size;
	str_params.aparams.ring_buf_info[0].addr  =	virt_to_phys(cstream->runtime->buffer);
	str_params.aparams.ring_buf_info[0].size = cstream->runtime->buffer_size;
	str_params.aparams.sg_count = 1;
	
	cb.param = cstream;
	cb.compr_cb = sdp_compr_fragment_elapsed;

//	pr_debug("sdp_platform_compr_set_params, frag_size[%d]\n", str_params.aparams.frag_size);
//	pr_debug("sdp_platform_compr_set_params, addr[0x%x]\n",str_params.aparams.ring_buf_info[0].addr);
//	pr_debug("sdp_platform_compr_set_params, size[%d]\n", str_params.aparams.ring_buf_info[0].size);	
	
	retval = stream->compr_ops->open(&str_params, &cb);
	if (retval < 0) {
		pr_err("stream allocation failed %d\n", retval);
		return retval;
	}

	stream->id = retval;
	return 0;
}

static int sdp_platform_compr_trigger(struct snd_compr_stream *cstream, int cmd)
{
	struct sdp_runtime_stream *stream =
		cstream->runtime->private_data;

//	pr_debug("alsa compress trigger\n");

	return stream->compr_ops->control(cmd, stream->id);
}

static int sdp_platform_compr_pointer(struct snd_compr_stream *cstream,
					struct snd_compr_tstamp *tstamp)
{
	struct sdp_runtime_stream *stream;

//	pr_debug("alsa compress pointer\n");

	stream  = cstream->runtime->private_data;
	stream->compr_ops->tstamp(stream->id, tstamp);
	tstamp->byte_offset = tstamp->copied_total %
				 (u32)cstream->runtime->buffer_size;
//	pr_debug("calc bytes offset/copied bytes as %d\n", tstamp->byte_offset);
	return 0;
}

static int sdp_platform_compr_ack(struct snd_compr_stream *cstream,
					size_t bytes)
{
	struct sdp_runtime_stream *stream;

//	pr_debug("alsa compress ask\n");

	stream  = cstream->runtime->private_data;
	stream->compr_ops->ack(stream->id, (unsigned long)bytes);
	stream->bytes_written += bytes;

	return 0;
}

static int sdp_platform_compr_get_caps(struct snd_compr_stream *cstream,
					struct snd_compr_caps *caps)
{
	struct sdp_runtime_stream *stream =
		cstream->runtime->private_data;

//	pr_debug("alsa compress get caps\n");

	return stream->compr_ops->get_caps(caps);
}

static int sdp_platform_compr_get_codec_caps(struct snd_compr_stream *cstream,
					struct snd_compr_codec_caps *codec)
{
	struct sdp_runtime_stream *stream =
		cstream->runtime->private_data;

//	pr_debug("alsa compress get codec caps\n");

	return stream->compr_ops->get_codec_caps(codec);
}



static struct snd_compr_ops sdp_platform_compr_ops = {

	.open = sdp_platform_compr_open,
	.free = sdp_platform_compr_free,
	.set_params = sdp_platform_compr_set_params,
	.trigger = sdp_platform_compr_trigger,
	.pointer = sdp_platform_compr_pointer,
	.ack = sdp_platform_compr_ack,
	.get_caps = sdp_platform_compr_get_caps,
	.get_codec_caps = sdp_platform_compr_get_codec_caps,
};

static struct snd_soc_platform_driver sdp_soc_platform_drv = {
	.ops		= &sdp_platform_ops,
	.compr_ops	= &sdp_platform_compr_ops,
	.pcm_new	= sdp_pcm_new,
	.pcm_free	= sdp_pcm_free,
};

/* MFLD - MSIC */
static struct snd_soc_dai_driver sdp_platform_dai[] = {
#if 0
{
	.name = "Headset-cpu-dai",
	.id = 0,
	.playback = {
		.channels_min = SDP_STEREO,
		.channels_max = SDP_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 5,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
#endif 


#if 1

{
	.name = "Speaker-cpu-dai",
	.id = 0,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
				SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |		
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE),
	},
	.capture = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
				SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |		
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE),		
	},

},
#endif 



#if 1//ALSA1, ALSA2 support

{
	.name = "Main-cpu-dai",
	.id = 1,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
				SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |		
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE),
	},
},

{
	.name = "Remote-cpu-dai",
	.id = 2,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
				SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 | 	
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE),
	},
},

//PP ALSA
#if 1
{
	.name = "PP-cpu-dai",
	.id = 3,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
				SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 | 	
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE),
	},
},
#endif 

#endif 


#if 0
{
	.name = "Vibra1-cpu-dai",
	.id = 2,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_MONO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "Vibra2-cpu-dai",
	.id = 3,
	.playback = {
		.channels_min = SDP_MONO,
		.channels_max = SDP_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
#endif 
{
	.name = "Compress-cpu-dai",
	.compress_dai = 1,
	.playback = {
		.channels_min = SDP_STEREO,
		.channels_max = SDP_STEREO,
		.rates = SDP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	/*.capture = {
		.channels_min = SDP_STEREO,
		.channels_max = SDP_STEREO,
		.rates = SNDRV_PCM_RATE_44100|SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},*/
},
};

int sdp_audio_pcm_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("sdp_platform_probe called\n");
	sdp_mm_dev = NULL;
	ret = snd_soc_register_platform(&pdev->dev, &sdp_soc_platform_drv);
	if (ret) {
		pr_err("registering soc platform failed\n");
		return ret;
	}

	ret = snd_soc_register_dais(&pdev->dev,
				sdp_platform_dai, ARRAY_SIZE(sdp_platform_dai));
	if (ret) {
		pr_err("registering cpu dais failed\n");
		snd_soc_unregister_platform(&pdev->dev);
	}

	return ret;

}

int sdp_audio_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(sdp_platform_dai));
	snd_soc_unregister_platform(&pdev->dev);

	pr_debug("sdp_audio_platform_remove success\n");

	return 0;
}


