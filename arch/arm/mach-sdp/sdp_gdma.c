/* arch/arm/plat-sdp/sdp_gdma.c
 *
 * Copyright (C) 2012 Samsung Electronics Co. Ltd.
 *	Dongseok Lee <drain.lee@samsung.com>
 */

/*
 * 130322, drain.lee : create gdma driver file.
 * 131009, drain.lee : support device-tree.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>

#include <mach/sdp_gdma.h>

/* define for driver */
#define SDP_GDMA_DRV_NAME "sdp-gdma"
#define SDP_GDMA_DRV_VER  "131009(support device-tree)"
#define PRIVATE				static/*for private function */

/* GDMA Register Offsets */
#define GDMA_CON		0x00
#define GDMA_SIZE		0x04
#define GDMA_SRC		0x08
#define GDMA_DST		0x0C
#define GDMA_FILL0		0x10
#define GDMA_FILL1		0x14
#define GDMA_MAT0		0x18
#define GDAM_MAT1		0x1C
#define GDMA_BITEN0		0x20
#define GDMA_BITEN1		0x24
#define GDMA_STAT		0x28
#define GDMA_PMADDR		0x2C

/* Bit define */
#define GDMA_CON_DST_INT		(0x3<<12)
#define GDMA_CON_DST_INT_ARM	(0x0<<12)
#define GDMA_CON_DST_INT_DSP0	(0x1<<12)
#define GDMA_CON_DST_INT_DSP1	(0x2<<12)

#define GDMA_CON_BUSTLEN_MASK	(0x1F<<4)
#define GDMA_CON_BUSTLEN_VAL(x)	(((x)&0x1F)<<4)

#define GDMA_CON_FUNC_TYPE		(0x3<<0)
#define GDMA_CON_FUNC_TYPE_COPY	(0x0<<0)
#define GDMA_CON_FUNC_TYPE_FILL	(0x1<<0)
#define GDMA_CON_FUNC_TYPE_PM	(0x2<<0)

#define GDMA_SIZE_MAX			(0xFFFFFFFF)

#define GDMA_STAT_DMAIRQ_DSP1	(0x1<<7)
#define GDMA_STAT_DMAIRQ_DSP0	(0x1<<6)
#define GDMA_STAT_DMAIRQ_ARM	(0x1<<5)
#define GDMA_STAT_PM_SUCCESS	(0x1<<4)
#define GDMA_STAT_START			(0x1<<0)


/* GDMA chan info(multi chan per one device) (gdev) */
typedef struct sdp_gdma_device {
	void * __iomem iobase;
	struct resource *mem;
	int irq;

	/* DMA-Engine Device */
	struct dma_device ddev;
} sdp_gdma_device_t;


/* GDMA chan info (gchan) */
typedef struct sdp_gdma_chan {
	struct sdp_gdma_device *gdev;
	spinlock_t lock;
	struct tasklet_struct tasklet;
	/* List of to be xfered descriptors */
	struct list_head work_list;

	/* DMA-Engine Channel */
	struct dma_chan dchan;
	dma_cookie_t completed;
} sdp_gdma_chan_t;

enum sdp_gdma_desc_status {
	/* In the DMAC pool */
	FREE = 0,

	/*
	 * Allocted to some channel during prep_xxx
	 * Also may be sitting on the work_list.
	 */
	PREP,

	/*
	 * Sitting on the work_list and already submitted
	 * to the GDMA core. Not more than two descriptors
	 * of a channel can be BUSY at any time.
	 */
	BUSY,

	/*
	 * Sitting on the channel work_list but xfer done
	 * by GDMA core
	 */
	DONE,
};

enum sdp_gdma_reqtype {
	/* block copy */
	COPY,

	/* 64bit dst fill by constant value */
	FILL,

	/* 64bit pattern match mode */
	MATCH,
};

enum sdp_gdma_burst_size {
	BURST_SIZE_1 = 1,
	BURST_SIZE_8 = 8,
	BURST_SIZE_16 = 16,
};

typedef struct sdp_gdma_reqcfg {
	enum sdp_gdma_reqtype reqtype;
	u32 src_start;
	u32 dst_start;
	u32 size;

	u32 bus_read_req_delay;
	enum sdp_gdma_burst_size bus_burst_size;

	/* Used for fill */
	u32 fill_pattern_h;
	u32 fill_pattern_l;

	/* Used for match */
	u32 match_pattern_h;
	u32 match_pattern_l;
	u32 match_bit_en_h;
	u32 match_bit_en_l;
} sdp_gdma_reqcfg_t;

typedef struct sdp_gdma_desc {
	/* To attach to a queue as child */
	struct list_head node;
	struct dma_async_tx_descriptor txd;
	struct sdp_gdma_chan *gchan;
	enum sdp_gdma_desc_status status;
	struct sdp_gdma_reqcfg reqcfg;

	/* requested address */
	dma_addr_t src;
	dma_addr_t dst;
	size_t len;
} sdp_gdma_desc_t;

PRIVATE inline struct sdp_gdma_chan *
to_sdp_gdma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct sdp_gdma_chan, dchan);
}

PRIVATE inline struct sdp_gdma_desc *
to_sdp_gdma_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct sdp_gdma_desc, txd);
}

/*
 * for register access functions
 */
PRIVATE inline u32 sdp_gdma_read(struct sdp_gdma_device *gdev, u32 reg_off) {
	return readl((void *)((u32)gdev->iobase + reg_off));
}

PRIVATE inline void sdp_gdma_write(struct sdp_gdma_device *gdev, u32 val, u32 reg_off) {
	writel(val, (void *)((u32)gdev->iobase + reg_off));
}

int sdp_gdma_start(struct sdp_gdma_chan *gchan) {
	u32 start = 0;

	start = sdp_gdma_read(gchan->gdev, GDMA_STAT);
	if(start & GDMA_STAT_START) {
		return -EBUSY;
	}
	sdp_gdma_write(gchan->gdev, GDMA_STAT_START, GDMA_STAT);

	return 0;
}

int sdp_gdma_stop(struct sdp_gdma_chan *gchan) {
	return -ENOTSUPP;
}

int sdp_gdma_is_busy(struct sdp_gdma_chan *gchan) {
	u32 start = 0;

	start = sdp_gdma_read(gchan->gdev, GDMA_STAT);
	return start & GDMA_STAT_START;
}

void sdp_gdma_interrupt_enable(struct sdp_gdma_chan *gchan) {
}

void sdp_gdma_interrupt_disable(struct sdp_gdma_chan *gchan) {
}

int sdp_gdma_set_request_config
	(struct sdp_gdma_chan *gchan, struct sdp_gdma_reqcfg *reqcfg)
{
	if(sdp_gdma_is_busy(gchan)) {
		return -EBUSY;
	}

	if(reqcfg->reqtype == COPY) {
		sdp_gdma_write(gchan->gdev,
			GDMA_CON_DST_INT_ARM | GDMA_CON_FUNC_TYPE_COPY |
			GDMA_CON_BUSTLEN_VAL(reqcfg->bus_burst_size),
			GDMA_CON);

	} else if(reqcfg->reqtype == FILL) {
		return -ENOTSUPP;
	} else if(reqcfg->reqtype == MATCH) {
		return -ENOTSUPP;
	} else {
		return -EINVAL;
	}

	sdp_gdma_write(gchan->gdev, reqcfg->dst_start, GDMA_DST);
	sdp_gdma_write(gchan->gdev, reqcfg->src_start, GDMA_SRC);
	sdp_gdma_write(gchan->gdev, reqcfg->size, GDMA_SIZE);

	return 0;
}

/*
 * for operation functions
 */

PRIVATE inline struct sdp_gdma_desc *
sdp_gdma_alloc_desc(void)
{
	return kzalloc(sizeof(struct sdp_gdma_desc), GFP_KERNEL);
}

void
sdp_gdma_free_desc(struct sdp_gdma_desc *desc)
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
void sdp_gdma_tasklet(unsigned long data) {
	struct sdp_gdma_chan *gchan = (struct sdp_gdma_chan *)data;
	struct sdp_gdma_desc *desc, *_desc;
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

		ret = sdp_gdma_set_request_config(gchan,
						&desc->reqcfg);
		if (!ret) {
			ret = sdp_gdma_start(gchan);
			if(ret) {
				dev_err(&gchan->dchan.dev->device, "%s:%d do not start DMA(%d)\n",
					__func__, __LINE__, desc->txd.cookie);
				break;
			}
			desc->status = BUSY;
			break;
		} else if (ret == -EBUSY) {
			/* now GDMA is Running */
			dev_err(&gchan->dchan.dev->device, "%s:%d now GDMA is Running(%d)\n",
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
		sdp_gdma_free_desc(desc);
	}

	spin_unlock_irqrestore(&gchan->lock, flags);

//	return tasklet_schedule;
}

/* dma engine api */

irqreturn_t sdp_gdma_isr(int irq, void *data)
{
	struct sdp_gdma_device *gdev = data;
	struct dma_chan *dchan = NULL;
	struct sdp_gdma_desc *desc = NULL;
	u32 irq_pend;

	irq_pend = sdp_gdma_read(gdev, GDMA_STAT);

	if(!irq_pend) {
		return IRQ_NONE;
	}

	list_for_each_entry(dchan, &gdev->ddev.channels, device_node) {
		struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(dchan);

		if(irq_pend & GDMA_STAT_DMAIRQ_ARM) {
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
		sdp_gdma_write(gchan->gdev, GDMA_STAT_DMAIRQ_ARM, GDMA_STAT);

		tasklet_schedule(&gchan->tasklet);
	}

	return IRQ_HANDLED;
}

dma_cookie_t
sdp_gdma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct sdp_gdma_chan *gchan = NULL;
	struct sdp_gdma_desc *desc = NULL, *last = NULL;
	dma_cookie_t cookie;
	unsigned long flags;

	if(!txd && !txd->chan) {
		return 0;
	}

	gchan = to_sdp_gdma_chan(txd->chan);
	last = to_sdp_gdma_desc(txd);

	spin_lock_irqsave(&gchan->lock, flags);

	cookie = txd->chan->cookie;

	while (!list_empty(&last->node)) {
		desc = list_entry(last->node.next, struct sdp_gdma_desc, node);

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

static int sdp_gdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&gchan->lock, flags);

	gchan->completed = chan->cookie = 1;

	tasklet_init(&gchan->tasklet, sdp_gdma_tasklet, (unsigned long)gchan);

	sdp_gdma_interrupt_enable(gchan);

	spin_unlock_irqrestore(&gchan->lock, flags);
	return 1;
}

static int sdp_gdma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	struct sdp_gdma_desc *desc = NULL;
	unsigned long flags;

	/* Only supports DMA_TERMINATE_ALL */
	if (cmd != DMA_TERMINATE_ALL)
		return -ENXIO;

	spin_lock_irqsave(&gchan->lock, flags);

	/* stop gdma */
	sdp_gdma_stop(gchan);

	/* Mark all desc done */
	list_for_each_entry(desc, &gchan->work_list, node) {
		desc->status = DONE;
	}

	spin_unlock_irqrestore(&gchan->lock, flags);

	sdp_gdma_tasklet((unsigned long)gchan);

	return 0;
}

static void sdp_gdma_free_chan_resources(struct dma_chan *chan)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&gchan->lock, flags);

	sdp_gdma_interrupt_disable(gchan);

	tasklet_kill(&gchan->tasklet);

	spin_unlock_irqrestore(&gchan->lock, flags);
}

static enum dma_status
sdp_gdma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		 struct dma_tx_state *txstate)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	dma_cookie_t last_done, last_used;
	int ret;

	last_done = gchan->completed;
	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, last_done, last_used);

	dma_set_tx_state(txstate, last_done, last_used, 0);

	return ret;
}

static void sdp_gdma_issue_pending(struct dma_chan *chan)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);

	sdp_gdma_tasklet((unsigned long)gchan);
}

struct dma_async_tx_descriptor *
sdp_gdma_prep_dma_memcpy(struct dma_chan *chan,
	dma_addr_t dest, dma_addr_t src, size_t len, unsigned long flags)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	struct sdp_gdma_desc *desc = NULL;

	desc = sdp_gdma_alloc_desc();
	if(!desc) {
		return NULL;
	}

	/* cal width and height */
	if(len > GDMA_SIZE_MAX) {
		/* not supported length */
		dev_err(&gchan->dchan.dev->device, "not supported length(%dbytes)\n", len);
		sdp_gdma_free_desc(desc);
		return NULL;
	}

	/* setup request config */
	INIT_LIST_HEAD(&desc->node);
	desc->gchan = gchan;
	desc->status = PREP;
	desc->src = src;
	desc->dst = dest;
	desc->len = len;

	desc->reqcfg.reqtype = COPY;
	desc->reqcfg.src_start = src & 0xFFFFFFFF;
	desc->reqcfg.dst_start = dest & 0xFFFFFFFF;
	desc->reqcfg.size = (u32)len;

	desc->reqcfg.bus_burst_size = BURST_SIZE_16;

	/*setup dmaengine tx desc*/
	desc->txd.chan = chan;
	desc->txd.tx_submit = sdp_gdma_tx_submit;
	desc->txd.flags = flags;

	return &desc->txd;
}

struct dma_async_tx_descriptor *
sdp_gdma_prep_dma_memset(struct dma_chan *chan,
	dma_addr_t dest, int value, size_t len, unsigned long flags)
{
	struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(chan);
	struct sdp_gdma_desc *desc = NULL;

	desc = sdp_gdma_alloc_desc();
	if(!desc) {
		return NULL;
	}

	/* cal width and height */
	if(len > GDMA_SIZE_MAX) {
		/* not supported length */
		dev_err(&gchan->dchan.dev->device, "not supported length(%dbytes)\n", len);
		sdp_gdma_free_desc(desc);
		return NULL;
	}

	/* setup request config */
	INIT_LIST_HEAD(&desc->node);
	desc->gchan = gchan;
	desc->status = PREP;
	desc->dst = dest;
	desc->len = len;

	desc->reqcfg.reqtype = FILL;
	desc->reqcfg.dst_start = dest & 0xFFFFFFFF;
	desc->reqcfg.fill_pattern_h = value;
	desc->reqcfg.fill_pattern_l = value;
	desc->reqcfg.size = (u32)len;

	desc->reqcfg.bus_burst_size = BURST_SIZE_16;

	/*setup dmaengine tx desc*/
	desc->txd.chan = chan;
	desc->txd.tx_submit = sdp_gdma_tx_submit;
	desc->txd.flags = flags;

	return &desc->txd;
	return NULL;
}

static void
sdp_gdma_free_chans(struct sdp_gdma_device *gdev)
{
	struct dma_chan *dchan, *_dchan;
	int ret;

	list_for_each_entry_safe(dchan, _dchan, &gdev->ddev.channels, device_node) {
		struct sdp_gdma_chan *gchan = to_sdp_gdma_chan(dchan);

		list_del(&dchan->device_node);
		ret = sdp_gdma_control(dchan, DMA_TERMINATE_ALL, 0);
		if(ret) {
			dev_err(&gchan->dchan.dev->device, "DMA terminate all failed(%d)", ret);
		}
		kfree(gchan);
	}
}


static int sdp_gdma_prove(struct platform_device *pdev)
{
	struct sdp_gdma_device *gdev = NULL;
	struct sdp_gdma_chan *gchan = NULL;
	struct sdp_gdma_platdata *platdata = NULL;
	struct dma_device *ddev = NULL;
	struct resource *res = NULL;
	int ret, i;
	u32 flags = 0;

	dev_info(&pdev->dev, "proving new DMA device...\n");

#ifdef CONFIG_OF
	pdev->dev.platform_data = kzalloc(sizeof(struct sdp_gdma_platdata), GFP_KERNEL);
#endif

	platdata = dev_get_platdata(&pdev->dev);
	if(!platdata) {
		dev_err(&pdev->dev, "platform data is NULL.\n\n");
		return -EINVAL;
	}

#ifdef CONFIG_OF
#else
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
	ret = request_irq(gdev->irq, sdp_gdma_isr, flags, dev_name(&pdev->dev), gdev);
	if(ret) {
		goto err_iounmap;
	}
	dev_info(&pdev->dev, "IRQ# %d\n", gdev->irq);


	/* setup dma device */
	ddev = &gdev->ddev;
	ddev->dev = &pdev->dev;
	INIT_LIST_HEAD(&ddev->channels);
	ddev->device_alloc_chan_resources = sdp_gdma_alloc_chan_resources;
	ddev->device_free_chan_resources = sdp_gdma_free_chan_resources;
	ddev->device_control = sdp_gdma_control;
	ddev->device_tx_status = sdp_gdma_tx_status;
	ddev->device_issue_pending = sdp_gdma_issue_pending;
	ddev->copy_align = 0;

	ddev->device_prep_dma_memcpy = sdp_gdma_prep_dma_memcpy;
	dma_cap_set(DMA_MEMCPY, ddev->cap_mask);

	ddev->device_prep_dma_memset = sdp_gdma_prep_dma_memset;
	dma_cap_set(DMA_MEMSET, ddev->cap_mask);



	/* create dma channal. now support only one channal */
	for(i = 0; i < 1; i++) {

		gchan = kzalloc(sizeof(*gchan), GFP_KERNEL);
		if(!gchan) {
			ret = -ENOMEM;
			goto err_free_irq;
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
	sdp_gdma_free_chans(gdev);

err_free_irq:
	free_irq(gdev->irq, gdev);

err_iounmap:
	iounmap(gdev->iobase);

err_free_mem:
	release_mem_region(gdev->mem->start, resource_size(gdev->mem));

err_free_gdev:
	kfree(gdev);

	return ret;
}

static int sdp_gdma_remove(struct platform_device *pdev)
{
	struct sdp_gdma_device *gdev = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "removing DMA device...\n");

	sdp_gdma_free_chans(gdev);
	free_irq(gdev->irq, gdev);
	iounmap(gdev->iobase);
	release_mem_region(gdev->mem->start, resource_size(gdev->mem));
	kfree(gdev);
#ifdef CONFIG_OF
	kfree(dev_get_platdata(&pdev->dev));
#endif

	dev_info(&pdev->dev, "removed DMA device.\n");
	return 0;
}

static struct platform_device_id sdp_gdma_id_table[] = {
	{
		.name = SDP_GDMA_DRV_NAME,
		.driver_data = 0,
	},
};

static const struct of_device_id sdp_gdma_dt_match[] = {
	{ .compatible = "samsung,sdp-gdma" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_gdma_dt_match);

static struct platform_driver sdp_gdma_driver = {
	.driver 	= {
		.name = SDP_GDMA_DRV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sdp_gdma_dt_match,
#endif
	},
	.probe		= sdp_gdma_prove,
	.remove 	= sdp_gdma_remove,
	.id_table = sdp_gdma_id_table,
};

static int __init sdp_gdma_init(void)
{
	pr_info("%s: registered SDP GDMA driver. version %s\n",
		sdp_gdma_driver.driver.name, SDP_GDMA_DRV_VER);
	return platform_driver_register(&sdp_gdma_driver);
}
subsys_initcall(sdp_gdma_init);

static void __exit sdp_gdma_exit(void)
{
	platform_driver_unregister(&sdp_gdma_driver);
}
module_exit(sdp_gdma_exit);

MODULE_DESCRIPTION("Samsung SoC GDMA Controller driver");
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Dongseok Lee <drain.lee@samsung.com>");

