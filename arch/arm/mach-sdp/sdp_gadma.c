/* arch/arm/plat-sdp/sdp_gadma.c
 *
 * Copyright (C) 2012 Samsung Electronics Co. Ltd.
 *	Dongseok Lee <drain.lee@samsung.com>
 */

/*
 * 121208, drain.lee : create gadma driver file.
 * 121217, drain.lee : bug fix, width, height calculate overflow.
 * 130103, drain.lee : add interleaved dma mode.
 * 140819, drain.lee : support clock gating.
 * 140822, drain.lee : fix compile warning.
 * 141128, drain.lee : support Hawk-A GADMA.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/clk.h>

#include <mach/sdp_gadma.h>

/* define for driver */
#define SDP_GADMA_DRV_NAME "sdp-gadma"
#define SDP_GADMA_DRV_VER  "141128(support Hawk-A GADMA)"

/* GADMA Register Offsets */
#define BLT0_CTL			0x00
#define BLT0_SIZE			0x04
#define BLT0_FILL_COL		0x08
#define BLT0_MASK_COL		0x0C
#define BLT0_START			0x10
#define BLT0_REG_ON			0x14
#define BLT0_SRC_BASE_L		0x20
#define BLT0_SRC_BASE_H		0x54
#define BLT0_SRC_CON		0x24
#define BLT0_CA_CON			0x34
#define BLT0_DST_BASE_L		0x38
#define BLT0_DST_BASE_H		0x58
#define BLT0_DST_CON		0x3C
#define BLT0_SRC_KEY		0x40
#define BLT0_DELAY			0x4C
#define BLT0_BUS_CON		0x50
#define BLT0_IRQ_PEND		0x70
#define BLT0_IRQ_MASK		0x74

/* Bit define */
#define BLT0_CTL_KEY_MODE			(0xF<<28)
#define BLT0_CTL_KEY_MODE_NORMAL	(0x0<<28)
#define BLT0_CTL_KEY_MODE_KEYING	(0x8<<28)
#define BLT0_CTL_BITBLT_MOED		(0x3<<24)
#define BLT0_CTL_PRE_ALPHA			(0x1<<14)
#define BLT0_CTL_MASK_EN			(0x1<<8)
#define BLT0_CTL_RGBA_MODE			(0x1<<4)
#define BLT0_CTL_OP_MODE			(0xF<<0)
#define BLT0_CTL_OP_MODE_BITBLT		(0x0<<0)
#define BLT0_CTL_OP_MODE_FILL		(0x1<<0)

#define BLT0_SIZE_WHITH				(0xFFFF<<16)
#define BLT0_SIZE_HEIGHT			(0xFFFF<<0)
#define BLT0_SIZE_MAX_WIDTH			65535/*bytes*/
#define BLT0_SIZE_MAX_HEIGHT		65535/*line*/

#define BLT0_START_TRG				(0x1<<0)
#define BLT0_REG_ON_START_DETECT	(0x1<<0)

#define BLT0_IRQ_PEND_BLTn(n)		(0x1<<(n))
#define BLT0_IRQ_MASK_BLTn(n)		(0x1<<(n))

/* GADMA chan info(multi chan per one device) (gdev) */
typedef struct sdp_gadma_device {
	void * __iomem iobase;
	struct resource *mem;
	int irq;
	struct clk *fclk;

	/* DMA-Engine Device */
	struct dma_device ddev;
} sdp_gadma_device_t;


/* GADMA chan info (gchan) */
typedef struct sdp_gadma_chan {
	struct sdp_gadma_device *gdev;
	spinlock_t lock;
	struct tasklet_struct tasklet;
	/* List of to be xfered descriptors */
	struct list_head work_list;

	/* DMA-Engine Channel */
	struct dma_chan dchan;
	dma_cookie_t completed;
} sdp_gadma_chan_t;

enum sdp_gadma_desc_status {
	/* In the DMAC pool */
	FREE = 0,

	/*
	 * Allocted to some channel during prep_xxx
	 * Also may be sitting on the work_list.
	 */
	PREP,

	/*
	 * Sitting on the work_list and already submitted
	 * to the GADMA core. Not more than two descriptors
	 * of a channel can be BUSY at any time.
	 */
	BUSY,

	/*
	 * Sitting on the channel work_list but xfer done
	 * by GADMA core
	 */
	DONE,
};

enum sdp_gadma_reqtype {
	/* block copy */
	BLOCK_COPY,

	/* 32-bpp dst fill by constant value */
	BLOCK_FILL,

	/* 8-bpp Src. Keying(Dst. Overwrite when not equal) */
	BLOCK_KEYING,
};

enum sdp_gadma_burst_size {
	BURST_SIZE_8 = 2,
	BURST_SIZE_16 = 1,
	BURST_SIZE_32 = 0,
};

typedef struct sdp_gadma_reqcfg {
	enum sdp_gadma_reqtype reqtype;
	u32 src_start_l;
	u32 src_start_h;
	u32 dst_start_l;
	u32 dst_start_h;
	u16 width;
	u16 height;
	u16 src_window_width;
	u16 dst_window_width;

	u32 bus_read_req_delay;
	enum sdp_gadma_burst_size bus_burst_size;

	u32 fill_color;/* Used for fill */
	u32 source_key;/* Used for 8bpp color keying */
} sdp_gadma_reqcfg_t;

typedef struct sdp_gadma_desc {
	/* To attach to a queue as child */
	struct list_head node;
	struct dma_async_tx_descriptor txd;
	struct sdp_gadma_chan *gchan;
	enum sdp_gadma_desc_status status;
	struct sdp_gadma_reqcfg reqcfg;

	/* requested address */
	dma_addr_t src;
	dma_addr_t dst;
	size_t len;
} sdp_gadma_desc_t;

static inline struct sdp_gadma_chan *
to_sdp_gadma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct sdp_gadma_chan, dchan);
}

static inline struct sdp_gadma_desc *
to_sdp_gadma_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct sdp_gadma_desc, txd);
}

/*
 * for register access functions
 */
static inline u32 sdp_gadma_read(struct sdp_gadma_device *gdev, u32 reg_off) {
	return readl((void *)((u32)gdev->iobase + reg_off));
}

static inline void sdp_gadma_write(struct sdp_gadma_device *gdev, u32 val, u32 reg_off) {
	writel(val, (void *)((u32)gdev->iobase + reg_off));
}

static int sdp_gadma_start(struct sdp_gadma_chan *gchan) {
	u32 start = 0;

	start = sdp_gadma_read(gchan->gdev, BLT0_START);
	if(start & BLT0_START_TRG) {
		return -EBUSY;
	}
	sdp_gadma_write(gchan->gdev, 0, BLT0_REG_ON);
	sdp_gadma_write(gchan->gdev, start & ~BLT0_START_TRG, BLT0_START);
	if(sdp_gadma_read(gchan->gdev, BLT0_REG_ON) & BLT0_REG_ON_START_DETECT) {
		return -EBUSY;
	}
	sdp_gadma_write(gchan->gdev, start | BLT0_START_TRG, BLT0_START);

	return 0;
}

static int sdp_gadma_stop(struct sdp_gadma_chan *gchan) {
	u32 start = 0;

	start = sdp_gadma_read(gchan->gdev, BLT0_START);
	sdp_gadma_write(gchan->gdev, start & ~BLT0_START_TRG, BLT0_START);
	sdp_gadma_write(gchan->gdev, 0, BLT0_REG_ON);

	return 0;
}

static int sdp_gadma_is_busy(struct sdp_gadma_chan *gchan) {
	u32 start = 0;

	start = sdp_gadma_read(gchan->gdev, BLT0_START);
	return start & BLT0_START_TRG;
}

static void sdp_gadma_interrupt_enable(struct sdp_gadma_chan *gchan) {
	u32 mask = 0;

	mask = sdp_gadma_read(gchan->gdev, BLT0_IRQ_MASK);
	sdp_gadma_write(gchan->gdev, mask|BLT0_IRQ_MASK_BLTn(gchan->dchan.chan_id), BLT0_IRQ_MASK);
}

static void sdp_gadma_interrupt_disable(struct sdp_gadma_chan *gchan) {
	u32 mask = 0;

	mask = sdp_gadma_read(gchan->gdev, BLT0_IRQ_MASK);
	sdp_gadma_write(gchan->gdev, mask&(~ BLT0_IRQ_MASK_BLTn(gchan->dchan.chan_id)), BLT0_IRQ_MASK);
}

int sdp_gadma_set_request_config
	(struct sdp_gadma_chan *gchan, struct sdp_gadma_reqcfg *reqcfg)
{
	u32 ctrl = 0;

	if(sdp_gadma_is_busy(gchan)) {
		return -EBUSY;
	}

	ctrl = sdp_gadma_read(gchan->gdev, BLT0_CTL) & ~(BLT0_CTL_KEY_MODE|BLT0_CTL_OP_MODE);
	ctrl = 0;

	if(reqcfg->reqtype == BLOCK_COPY) {
		sdp_gadma_write(gchan->gdev, ctrl|(BLT0_CTL_KEY_MODE_NORMAL|BLT0_CTL_OP_MODE_BITBLT), BLT0_CTL);
		sdp_gadma_write(gchan->gdev, reqcfg->src_start_l, BLT0_SRC_BASE_L);
		sdp_gadma_write(gchan->gdev, reqcfg->src_start_h, BLT0_SRC_BASE_H);
		sdp_gadma_write(gchan->gdev, reqcfg->src_window_width, BLT0_SRC_CON);

	} else if(reqcfg->reqtype == BLOCK_FILL) {
		sdp_gadma_write(gchan->gdev, ctrl|(BLT0_CTL_KEY_MODE_NORMAL|BLT0_CTL_OP_MODE_FILL), BLT0_CTL);
		sdp_gadma_write(gchan->gdev, 0, BLT0_SRC_BASE_L);
		sdp_gadma_write(gchan->gdev, 0, BLT0_SRC_BASE_H);
		sdp_gadma_write(gchan->gdev, 0, BLT0_SRC_CON);
		sdp_gadma_write(gchan->gdev, (u32)reqcfg->fill_color, BLT0_FILL_COL);

	} else if(reqcfg->reqtype == BLOCK_KEYING) {
		sdp_gadma_write(gchan->gdev, ctrl|(BLT0_CTL_KEY_MODE_KEYING|BLT0_CTL_OP_MODE_BITBLT), BLT0_CTL);
		sdp_gadma_write(gchan->gdev, reqcfg->src_start_l, BLT0_SRC_BASE_L);
		sdp_gadma_write(gchan->gdev, reqcfg->src_start_h, BLT0_SRC_BASE_H);
		sdp_gadma_write(gchan->gdev, reqcfg->src_window_width, BLT0_SRC_CON);
		sdp_gadma_write(gchan->gdev, (u32)reqcfg->source_key, BLT0_SRC_KEY);

	} else {
		return -EINVAL;
	}

	sdp_gadma_write(gchan->gdev, (u32)reqcfg->dst_start_l, BLT0_DST_BASE_L);
	sdp_gadma_write(gchan->gdev, (u32)reqcfg->dst_start_h, BLT0_DST_BASE_H);
	sdp_gadma_write(gchan->gdev, (u32)reqcfg->dst_window_width, BLT0_DST_CON);

	sdp_gadma_write(gchan->gdev, (u32)(reqcfg->width<<16) | reqcfg->height, BLT0_SIZE);
	sdp_gadma_write(gchan->gdev, (u32)reqcfg->bus_read_req_delay, BLT0_DELAY);
	sdp_gadma_write(gchan->gdev, (u32)reqcfg->bus_burst_size, BLT0_BUS_CON);

	return 0;
}

/*
 * for operation functions
 */

static int
sdp_gadma_clock_gate(struct sdp_gadma_chan *gchan)
{
	if(!gchan || !gchan->gdev) {
		return -ENODEV;
	}

	if(!gchan->gdev->fclk) {
		return 0;
	}
	//dev_info(&gchan->dchan.dev->device, "sdp_gadma_clock_gate %p\n", gchan->gdev->fclk);
	clk_disable_unprepare(gchan->gdev->fclk);

	return 0;
}

static int
sdp_gadma_clock_ungate(struct sdp_gadma_chan *gchan)
{
	if(!gchan || !gchan->gdev) {
		return -ENODEV;
	}

	if(!gchan->gdev->fclk) {
		return 0;
	}
	//dev_info(&gchan->dchan.dev->device, "sdp_gadma_clock_ungate %p\n", gchan->gdev->fclk);
	return clk_prepare_enable(gchan->gdev->fclk);
}


static inline struct sdp_gadma_desc *
sdp_gadma_alloc_desc(void)
{
	return kzalloc(sizeof(struct sdp_gadma_desc), GFP_KERNEL);
}

static void
sdp_gadma_free_desc(struct sdp_gadma_desc *desc)
{
	dma_async_tx_callback callback;
	void *param;

	callback = desc->txd.callback;
	param = desc->txd.callback_param;

	/* callback if only set DMA_PREP_INTERRUPT in flags*/
	if ((desc->txd.flags & DMA_PREP_INTERRUPT) && callback)
		callback(param);

	return kzfree(desc);
}

static void sdp_gadma_tasklet(unsigned long data) {
	struct sdp_gadma_chan *gchan = (struct sdp_gadma_chan *)data;
	struct sdp_gadma_desc *desc, *_desc;
	struct list_head list;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&gchan->lock, flags);

	INIT_LIST_HEAD(&list);

	list_for_each_entry_safe(desc, _desc, &gchan->work_list, node) {
		if(desc->status == DONE) {
			/* clear done desc */
			struct dma_async_tx_descriptor *txd = &desc->txd;

			if(!(txd->flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
				if( txd->flags & DMA_COMPL_DEST_UNMAP_SINGLE ) {
					dma_unmap_single(txd->chan->device->dev,
						desc->dst, desc->len, DMA_FROM_DEVICE);
				}
				else
				{
					dma_unmap_page(txd->chan->device->dev,
						desc->dst, desc->len, DMA_FROM_DEVICE);
				}
			}

			if(desc->src && !(txd->flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
				if( txd->flags & DMA_COMPL_SRC_UNMAP_SINGLE ) {
					dma_unmap_single(txd->chan->device->dev,
						desc->src, desc->len, DMA_TO_DEVICE);
				}
				else
				{
					dma_unmap_page(txd->chan->device->dev,
						desc->src, desc->len, DMA_TO_DEVICE);
				}
			}

			gchan->completed = desc->txd.cookie;
			list_move_tail(&desc->node, &list);
		}
	}

	/* start nexts desc */
	list_for_each_entry(desc, &gchan->work_list, node) {
		/* If already submitted */
		if (desc->status == BUSY)
			break;

		ret = sdp_gadma_set_request_config(gchan,
						&desc->reqcfg);
		if (!ret) {
			ret = sdp_gadma_start(gchan);
			if(ret) {
				dev_err(&gchan->dchan.dev->device, "%s:%d do not start DMA(%d)\n",
					__func__, __LINE__, desc->txd.cookie);
				break;
			}
			desc->status = BUSY;
			break;
		} else if (ret == -EBUSY) {
			/* now GADMA is Running */
			dev_err(&gchan->dchan.dev->device, "%s:%d now GADMA is Running(%d)\n",
					__func__, __LINE__, desc->txd.cookie);
			break;
		} else {
			/* Invalid request config */
			desc->status = DONE;
			dev_err(&gchan->dchan.dev->device, "%s:%d Invalid request config(%d)\n",
					__func__, __LINE__, desc->txd.cookie);
			tasklet_schedule(&gchan->tasklet);
		}
	}

	list_for_each_entry_safe(desc, _desc, &list, node) {
		sdp_gadma_free_desc(desc);
	}

	spin_unlock_irqrestore(&gchan->lock, flags);

//	return tasklet_schedule;
}

/* dma engine api */

static irqreturn_t sdp_gadma_isr(int irq, void *data)
{
	struct sdp_gadma_device *gdev = data;
	struct dma_chan *dchan = NULL;
	struct sdp_gadma_desc *desc = NULL;
	u32 irq_pend;

	irq_pend = sdp_gadma_read(gdev, BLT0_IRQ_PEND);

	if(!irq_pend) {
		return IRQ_NONE;
	}

	list_for_each_entry(dchan, &gdev->ddev.channels, device_node) {
		struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(dchan);

		if(irq_pend & BLT0_IRQ_PEND_BLTn(dchan->chan_id)) {
			list_for_each_entry(desc, &gchan->work_list, node) {
				if(desc->status == BUSY) {
					desc->status = DONE;
					break;
				} else if(desc->status == FREE || desc->status == PREP) {
					/* error */
					dev_err(&gchan->dchan.dev->device, "IRQ Pend. but no busy desc!\n");
					break;
				}
			}
		}
		sdp_gadma_write(gchan->gdev, BLT0_IRQ_PEND_BLTn(dchan->chan_id), BLT0_IRQ_PEND);

		tasklet_schedule(&gchan->tasklet);
	}

	return IRQ_HANDLED;
}

static dma_cookie_t
sdp_gadma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct sdp_gadma_chan *gchan = NULL;
	struct sdp_gadma_desc *desc = NULL, *last = NULL;
	dma_cookie_t cookie;
	unsigned long flags;

	if(!txd && !txd->chan) {
		return 0;
	}

	gchan = to_sdp_gadma_chan(txd->chan);
	last = to_sdp_gadma_desc(txd);

	spin_lock_irqsave(&gchan->lock, flags);

	cookie = txd->chan->cookie;

	while (!list_empty(&last->node)) {
		desc = list_entry(last->node.next, struct sdp_gadma_desc, node);

		if (++cookie < 0)
			cookie = 1;
		desc->txd.cookie = cookie;

		list_move_tail(&desc->node, &gchan->work_list);
	}

	if (++cookie < 0)
		cookie = 1;
	last->txd.cookie = cookie;

	list_move_tail(&last->node, &gchan->work_list);

	txd->chan->cookie = cookie;

	spin_unlock_irqrestore(&gchan->lock, flags);

	return cookie;
}

static int sdp_gadma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	unsigned long flags;
	int ret;

	ret = sdp_gadma_clock_ungate(gchan);
	if(ret < 0) {
		dev_err(&gchan->dchan.dev->device, "clock ungate error(%d)!\n", ret);
		return ret;
	}

	spin_lock_irqsave(&gchan->lock, flags);

	gchan->completed = chan->cookie = 1;

	tasklet_init(&gchan->tasklet, sdp_gadma_tasklet, (unsigned long)gchan);

	sdp_gadma_interrupt_enable(gchan);

	spin_unlock_irqrestore(&gchan->lock, flags);
	return 1;
}

static int sdp_gadma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	struct sdp_gadma_desc *desc = NULL;
	unsigned long flags;

	/* Only supports DMA_TERMINATE_ALL */
	if (cmd != DMA_TERMINATE_ALL)
		return -ENXIO;

	spin_lock_irqsave(&gchan->lock, flags);

	/* stop gadma */
	sdp_gadma_stop(gchan);

	/* Mark all desc done */
	list_for_each_entry(desc, &gchan->work_list, node) {
		desc->status = DONE;
	}

	spin_unlock_irqrestore(&gchan->lock, flags);

	sdp_gadma_tasklet((unsigned long)gchan);

	return 0;
}

static void sdp_gadma_free_chan_resources(struct dma_chan *chan)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&gchan->lock, flags);

	sdp_gadma_interrupt_disable(gchan);

	tasklet_kill(&gchan->tasklet);

	spin_unlock_irqrestore(&gchan->lock, flags);

	sdp_gadma_clock_gate(gchan);
}

static enum dma_status
sdp_gadma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		 struct dma_tx_state *txstate)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	dma_cookie_t last_done, last_used;
	int ret;

	last_done = gchan->completed;
	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, last_done, last_used);

	dma_set_tx_state(txstate, last_done, last_used, 0);

	return ret;
}

static void sdp_gadma_issue_pending(struct dma_chan *chan)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);

	sdp_gadma_tasklet((unsigned long)gchan);
}

struct dma_async_tx_descriptor *
sdp_gadma_prep_dma_memcpy(struct dma_chan *chan,
	dma_addr_t dest, dma_addr_t src, size_t len, unsigned long flags)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	struct sdp_gadma_desc *desc = NULL;
	u32 width = 0, height = 0, align = 0;

	desc = sdp_gadma_alloc_desc();
	if(!desc) {
		return NULL;
	}

	align = 0x1 << gchan->gdev->ddev.copy_align;

	/* cal width and height */
	if(len <= BLT0_SIZE_MAX_WIDTH) {
		width = len;
		height = 1;
	} else {
		for(width = (BLT0_SIZE_MAX_WIDTH+1-align); width > 0; width-=align) {
			if((len % width) == 0) {
				height = len / width;
				if(height <= BLT0_SIZE_MAX_HEIGHT) {
					break;
				}
			}
		}

		if(!width || !height || (width > BLT0_SIZE_MAX_WIDTH) || (height > BLT0_SIZE_MAX_HEIGHT)) {
			/* not supported length */
			dev_err(&gchan->dchan.dev->device, "not supported length(%dbytes)\n", len);
			sdp_gadma_free_desc(desc);
			return NULL;
		}
	}

	/* setup request config */
	INIT_LIST_HEAD(&desc->node);
	desc->gchan = gchan;
	desc->status = PREP;
	desc->src = src;
	desc->dst = dest;
	desc->len = len;

	desc->reqcfg.reqtype = BLOCK_COPY;
	desc->reqcfg.width = width;
	desc->reqcfg.height = height;
	desc->reqcfg.src_start_l = src & 0xFFFFFFFF;
	desc->reqcfg.src_window_width = width;
	desc->reqcfg.dst_start_l = dest & 0xFFFFFFFF;
	desc->reqcfg.dst_window_width = width;
#ifdef CONFIG_ARCH_SDP1412
	desc->reqcfg.bus_burst_size = BURST_SIZE_8;
#else
	desc->reqcfg.bus_burst_size = BURST_SIZE_32;
#endif

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	desc->reqcfg.src_start_h = (src >> 32) & 0xFFFFFFFF;
	desc->reqcfg.dst_start_h = (dest >> 32) & 0xFFFFFFFF;
#endif

	/*setup dmaengine tx desc*/
	desc->txd.chan = chan;
	desc->txd.tx_submit = sdp_gadma_tx_submit;
	desc->txd.flags = flags;

	return &desc->txd;
}

struct dma_async_tx_descriptor *
sdp_gadma_prep_dma_memset(struct dma_chan *chan,
	dma_addr_t dest, int value, size_t len, unsigned long flags)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	struct sdp_gadma_desc *desc = NULL;
	u32 width = 0, height = 0, align = 0;

	desc = sdp_gadma_alloc_desc();
	if(!desc) {
		return NULL;
	}

	align = 0x1 << gchan->gdev->ddev.copy_align;

	/* cal width and height */
	if(len <= BLT0_SIZE_MAX_WIDTH) {
		width = len;
		height = 1;
	} else {
		for(width = (BLT0_SIZE_MAX_WIDTH+1-align); width > 0; width-=align) {
			if((len % width) == 0) {
				height = len / width;
				if(height <= BLT0_SIZE_MAX_HEIGHT) {
					break;
				}
			}
		}

		if(!width || !height) {
			/* not supported length */
			dev_err(&gchan->dchan.dev->device, "not supported length(%dbytes)\n", len);
			sdp_gadma_free_desc(desc);
			return NULL;
		}
	}

	/* setup request config */
	INIT_LIST_HEAD(&desc->node);
	desc->gchan = gchan;
	desc->status = PREP;
	desc->dst = dest;
	desc->len = len;

	desc->reqcfg.reqtype = BLOCK_FILL;
	desc->reqcfg.width = width;
	desc->reqcfg.height = height;
	desc->reqcfg.dst_start_l = dest & 0xFFFFFFFF;
	desc->reqcfg.dst_window_width = width;
	desc->reqcfg.fill_color = (value&0xFF)|((value&0xFF)<<8)|
								((value&0xFF)<<16)|((value&0xFF)<<24);

#ifdef CONFIG_ARCH_SDP1412
	desc->reqcfg.bus_burst_size = BURST_SIZE_8;
#else
	desc->reqcfg.bus_burst_size = BURST_SIZE_32;
#endif

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	desc->reqcfg.dst_start_h = (dest >> 32) & 0xFFFFFFFF;
#endif

	/*setup dmaengine tx desc*/
	desc->txd.chan = chan;
	desc->txd.tx_submit = sdp_gadma_tx_submit;
	desc->txd.flags = flags;

	return &desc->txd;
}

/* for genaric 2D copy */
struct dma_async_tx_descriptor *
sdp_gadma_prep_interleaved_dma(struct dma_chan *chan,
	struct dma_interleaved_template *xt, unsigned long flags)
{
	struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(chan);
	struct sdp_gadma_desc *desc = NULL;

	if( xt->dir != DMA_MEM_TO_MEM ) {
		dev_err(&gchan->dchan.dev->device, "Not supported tarnsfer direction(dir: %d)\n", xt->dir);
		return NULL;
	}

	if( !xt->dst_inc || !xt->src_inc ) {
		dev_err(&gchan->dchan.dev->device, "Not supported no increment address(src: %d, dst: %d)\n", xt->src_inc, xt->dst_inc);
		return NULL;
	}

	if((xt->src_sgl || xt->dst_sgl) && (xt->frame_size == 0)) {
		dev_err(&gchan->dchan.dev->device, "Frame size is 0(src: %d, dst: %d)\n", xt->src_sgl, xt->dst_sgl);
		return NULL;
	}

	if(xt->frame_size > 1) {
		dev_err(&gchan->dchan.dev->device, "Not supported multi frame(%u)\n", xt->frame_size);
		return NULL;
	}

	desc = sdp_gadma_alloc_desc();
	if(!desc) {
		return NULL;
	}

	/* setup request config */
	INIT_LIST_HEAD(&desc->node);
	desc->gchan = gchan;
	desc->status = PREP;
	desc->src = xt->src_start;
	desc->dst = xt->dst_start;
	desc->len = xt->numf * xt->sgl[0].size;

	desc->reqcfg.reqtype = BLOCK_COPY;
	desc->reqcfg.width = xt->sgl[0].size;
	desc->reqcfg.height = xt->numf;
	desc->reqcfg.src_start_l = xt->src_start & 0xFFFFFFFF;
	desc->reqcfg.dst_start_l = xt->dst_start & 0xFFFFFFFF;

	if(xt->src_sgl) {
		desc->reqcfg.src_window_width = xt->sgl[0].size + xt->sgl[0].icg;
	} else {
		desc->reqcfg.src_window_width = xt->sgl[0].size;
	}

	if(xt->dst_sgl) {
		desc->reqcfg.dst_window_width = xt->sgl[0].size + xt->sgl[0].icg;
	} else {
		desc->reqcfg.dst_window_width = xt->sgl[0].size;
	}

#ifdef CONFIG_ARCH_SDP1412
	desc->reqcfg.bus_burst_size = BURST_SIZE_8;
#else
	desc->reqcfg.bus_burst_size = BURST_SIZE_32;
#endif

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	desc->reqcfg.src_start_h = (xt->src_start >> 32) & 0xFFFFFFFF;
	desc->reqcfg.dst_start_h = (xt->dst_start >> 32) & 0xFFFFFFFF;
#endif

	/*setup dmaengine tx desc*/
	desc->txd.chan = chan;
	desc->txd.tx_submit = sdp_gadma_tx_submit;
	desc->txd.flags = flags;

	return &desc->txd;
}

static void
sdp_gadma_free_chans(struct sdp_gadma_device *gdev)
{
	struct dma_chan *dchan, *_dchan;
	int ret;

	list_for_each_entry_safe(dchan, _dchan, &gdev->ddev.channels, device_node) {
		struct sdp_gadma_chan *gchan = to_sdp_gadma_chan(dchan);

		list_del(&dchan->device_node);
		ret = sdp_gadma_control(dchan, DMA_TERMINATE_ALL, 0);
		if(ret) {
			dev_err(&gchan->dchan.dev->device, "DMA terminate all failed(%d)", ret);
		}
		kfree(gchan);
	}
}


static int sdp_gadma_prove(struct platform_device *pdev)
{
	struct sdp_gadma_device *gdev = NULL;
	struct sdp_gadma_chan *gchan = NULL;
	struct sdp_gadma_platdata *platdata = NULL;
	struct dma_device *ddev = NULL;
	struct resource *res = NULL;
	int ret, i;
	u32 flags = 0;

	dev_info(&pdev->dev, "proving new DMA device...\n");

#ifdef CONFIG_OF
	platdata = NULL;
#else
	platdata = dev_get_platdata(&pdev->dev);


	if(!platdata) {
		dev_err(&pdev->dev, "platform data is NULL.\n\n");
		return -EINVAL;
	}

	if(platdata->init) {
		ret = platdata->init(platdata);
		if(ret) {
			dev_err(&pdev->dev, "platform init code return error(%d)\n", ret);
			return ret;
		}
	}
	else
	{
		dev_warn(&pdev->dev, "no platform init code.\n");
	}
#endif
	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if(!gdev) {
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(!res) {
		ret = -ENXIO;
		goto err_free_gdev;
	}
	gdev->mem = request_mem_region(res->start, resource_size(res), dev_name(&pdev->dev));
	if(!gdev->mem) {
		ret = -ENXIO;
		goto err_free_gdev;
	}

	gdev->iobase = ioremap(gdev->mem->start, resource_size(gdev->mem));
	if(!gdev->iobase) {
		ret = -ENXIO;
		goto err_free_mem;
	}
	dev_info(&pdev->dev, "iomem 0x%p++0x%x\n", gdev->iobase, (u32)resource_size(gdev->mem)-1);


	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if(!res) {
		ret = -ENXIO;
		goto err_iounmap;
	}
	gdev->irq = res->start;
	flags = 0;
	if(res->flags & IORESOURCE_IRQ_SHAREABLE) {
		flags = IRQF_SHARED;
	}
	ret = request_irq(gdev->irq, sdp_gadma_isr, flags, dev_name(&pdev->dev), gdev);
	if(ret) {
		goto err_iounmap;
	}
	dev_info(&pdev->dev, "IRQ# %d\n", gdev->irq);

	gdev->fclk = clk_get(&pdev->dev, "dma_clk");
	if(IS_ERR(gdev->fclk)) {
		dev_warn(&pdev->dev, "can't get dma_clk. so clock gating diasbled.\n");
		gdev->fclk = 0;
	} else {
		dev_info(&pdev->dev, "clock gating enabled. fclk %ldMHz\n", clk_get_rate(gdev->fclk)/1000000);
	}

	/* setup dma device */
	ddev = &gdev->ddev;
	ddev->dev = &pdev->dev;
	INIT_LIST_HEAD(&ddev->channels);
	ddev->device_alloc_chan_resources = sdp_gadma_alloc_chan_resources;
	ddev->device_free_chan_resources = sdp_gadma_free_chan_resources;
	ddev->device_control = sdp_gadma_control;
	ddev->device_tx_status = sdp_gadma_tx_status;
	ddev->device_issue_pending = sdp_gadma_issue_pending;
	ddev->copy_align = 0;

	ddev->device_prep_dma_memcpy = sdp_gadma_prep_dma_memcpy;
	dma_cap_set(DMA_MEMCPY, ddev->cap_mask);

	ddev->device_prep_dma_memset = sdp_gadma_prep_dma_memset;
	dma_cap_set(DMA_MEMSET, ddev->cap_mask);

	ddev->device_prep_interleaved_dma = sdp_gadma_prep_interleaved_dma;
	dma_cap_set(DMA_INTERLEAVE, ddev->cap_mask);


	/* create dma channal. now support only one channal */
	for(i = 0; i < 1; i++) {

		gchan = kzalloc(sizeof(*gchan), GFP_KERNEL);
		if(!gchan) {
			ret = -ENOMEM;
			goto err_clk_put;
		}
		gchan->gdev = gdev;
		spin_lock_init(&gchan->lock);
		INIT_LIST_HEAD(&gchan->work_list);

		gchan->dchan.device = ddev;
		gchan->dchan.chan_id = i;
		gchan->dchan.private = NULL;

		/* Add the channel to the DMAC list */
		ddev->chancnt++;
		list_add_tail(&gchan->dchan.device_node, &ddev->channels);
	}

	platform_set_drvdata(pdev, gdev);

	ret = dma_async_device_register(ddev);
	if(ret) {
		dev_err(&pdev->dev, "unable to register new DMA device!(%d)\n", ret);
		goto err_free_chans;
	}

	dev_info(&pdev->dev, "registered DMA device. total %dchannals.(align %dbyte)\n",
		ddev->chancnt, 1<<ddev->copy_align);

	return 0;

err_free_chans:
	sdp_gadma_free_chans(gdev);

err_clk_put:
	clk_put(gdev->fclk);

//err_free_irq:
	free_irq(gdev->irq, gdev);

err_iounmap:
	iounmap(gdev->iobase);

err_free_mem:
	release_mem_region(gdev->mem->start, resource_size(gdev->mem));

err_free_gdev:
	kfree(gdev);

	return ret;
}

static int sdp_gadma_remove(struct platform_device *pdev)
{
	struct sdp_gadma_device *gdev = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "removing DMA device...\n");

	sdp_gadma_free_chans(gdev);
	free_irq(gdev->irq, gdev);
	iounmap(gdev->iobase);
	release_mem_region(gdev->mem->start, resource_size(gdev->mem));
	kfree(gdev);

	dev_info(&pdev->dev, "removed DMA device.\n");
	return 0;
}

static struct platform_device_id sdp_gadma_id_table[] = {
	{
		.name = SDP_GADMA_DRV_NAME,
		.driver_data = 0,
	},
	{},
};

static const struct of_device_id sdp_gadma_dt_match[] = {
	{ .compatible = "samsung,sdp-gadma" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_gadma_dt_match);

static struct platform_driver sdp_gadma_driver = {
	.driver 	= {
		.name = SDP_GADMA_DRV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sdp_gadma_dt_match,
#endif
	},
	.probe		= sdp_gadma_prove,
	.remove 	= sdp_gadma_remove,
	.id_table = sdp_gadma_id_table,
};

static int __init sdp_gadma_init(void)
{
	pr_info("%s: registered SDP GADMA driver. version %s\n",
		sdp_gadma_driver.driver.name, SDP_GADMA_DRV_VER);
	return platform_driver_register(&sdp_gadma_driver);
}
subsys_initcall(sdp_gadma_init);

static void __exit sdp_gadma_exit(void)
{
	platform_driver_unregister(&sdp_gadma_driver);
}
module_exit(sdp_gadma_exit);

MODULE_DESCRIPTION("Samsung SoC GADMA Controller driver");
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Dongseok Lee <drain.lee@samsung.com>");

