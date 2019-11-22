/*********************************************************************************************
 *
 *	sdp_unzipc.c (Samsung DTV Soc unzip device driver)
 *
 *	author : seungjun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 *
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <linux/highmem.h>
#include <linux/mmc/core.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>

#include <linux/delay.h>
#include <mach/sdp_unzip.h>
#include <linux/of.h>
#include <mach/soc.h>
#include <linux/debugfs.h>
#ifdef CONFIG_SDP_HW_CLOCK
#include <mach/sdp_hwclock.h>
#endif

#define R_GZIP_IRQ				0x00
#define R_GZIP_IRQ_MASK			0x04
#define R_GZIP_CMD				0x08
#define R_GZIP_IN_BUF_ADDR		0x00C
#define R_GZIP_IN_BUF_SIZE		0x010
#define R_GZIP_IN_BUF_POINTER	0x014
#define R_GZIP_OUT_BUF_ADDR		0x018
#define R_GZIP_OUT_BUF_SIZE		0x01C
#define R_GZIP_OUT_BUF_POINTER	0x020
#define R_GZIP_LZ_ADDR		0x024
#define R_GZIP_DEC_CTRL		0x28
#define R_GZIP_PROC_DELAY	0x2C
#define R_GZIP_TIMEOUT		0x30
#define R_GZIP_IRQ_DELAY	0x34
#define R_GZIP_FILE_INFO	0x38
#define R_GZIP_ERR_CODE		0x3C
#define R_GZIP_PROC_STATE	0x40
#define R_GZIP_ENC_DATA_END_DELAY	0x44
#define R_GZIP_CRC32_VALUE_HDL		0x48
#define R_GZIP_CRC32_VALUE_SW		0x4C
#define R_GZIP_ISIZE_VALUE_HDL		0x50
#define R_GZIP_ISIZE_VALUE_SW		0x54
#define R_GZIP_ADDR_LIST1			0x58
#define R_GZIP_IN_BUF_WRITE_CTRL	(0xD8)
#define R_GZIP_IN_BUF_WRITE_POINTER	(0xDC)

#define V_GZIP_CTL_ADVMODE	(0x1 << 24)
#define V_GZIP_CTL_ISIZE	(0x0 << 21)
#define V_GZIP_CTL_CRC32	(0x0 << 20)
#define V_GZIP_CTL_OUT_PAR	(0x1 << 12)
#define V_GZIP_CTL_IN_PAR	(0x1 << 8)
#define V_GZIP_CTL_OUT_ENDIAN_LITTLE	(0x1 << 4)
#define V_GZIP_CTL_IN_ENDIAN_LITTLE		(0x1 << 0)

#define GZIP_WINDOWSIZE	4		//(0:256, 1:512, 2:1024, 3:2048, 4:4096...
#define GZIP_ALIGNSIZE	64

#define GZIP_PAGESIZE	4096
#define GZIP_OUTPUTSIZE	128*1024

typedef void * (sdp_mempool_alloc_t)(gfp_t gfp_mask, void *pool_data);
typedef void (sdp_mempool_free_t)(void *element, void *pool_data);

struct sdp_mempool {
	spinlock_t   lock;
	unsigned int max_nr;
	unsigned int remain_nr;
	void **elements;

	void *pool_data;
	sdp_mempool_alloc_t *alloc;
	sdp_mempool_free_t  *free;
};

static inline void __sdp_mempool_destroy(struct sdp_mempool *pool)
{
	while (pool->max_nr) {
		void *element = pool->elements[--pool->max_nr];
		pool->free(element, pool->pool_data);
	}
	kfree(pool->elements);
	kfree(pool);
}

static struct sdp_mempool *sdp_mempool_create(unsigned int max_nr,
					      sdp_mempool_alloc_t *alloc_fn,
					      sdp_mempool_free_t *free_fn,
					      void *pool_data)
{
	struct sdp_mempool *pool;
	gfp_t gfp_mask = GFP_KERNEL;
	int node_id = NUMA_NO_NODE;

	pool = kmalloc_node(sizeof(*pool), gfp_mask | __GFP_ZERO, node_id);
	if (!pool)
		return NULL;
	pool->elements = kmalloc_node(max_nr * sizeof(void *),
				      gfp_mask, node_id);
	if (!pool->elements) {
		kfree(pool);
		return NULL;
	}

	spin_lock_init(&pool->lock);
	pool->pool_data = pool_data;
	pool->alloc = alloc_fn;
	pool->free = free_fn;
	pool->max_nr = 0;

	/*
	 * First pre-allocate the guaranteed number of buffers.
	 */
	while (pool->max_nr < max_nr) {
		void *element;

		element = pool->alloc(gfp_mask, pool->pool_data);
		if (unlikely(!element)) {
			__sdp_mempool_destroy(pool);
			return NULL;
		}
		pool->elements[pool->max_nr++] = element;
	}

	BUG_ON(pool->max_nr != max_nr);
	pool->remain_nr = max_nr;

	return pool;
}

static void sdp_mempool_destroy(struct sdp_mempool *pool)
{
	BUG_ON(pool->max_nr != pool->remain_nr);
	__sdp_mempool_destroy(pool);
}

static void *sdp_mempool_alloc(struct sdp_mempool *pool, gfp_t gfp_mask)
{
	unsigned long flags;
	void *element = NULL;

	spin_lock_irqsave(&pool->lock, flags);
	if (likely(pool->remain_nr > 0))
		element = pool->elements[--pool->remain_nr];
	spin_unlock_irqrestore(&pool->lock, flags);

	if (unlikely(element == NULL))
		pr_err("SDP_UNZIP: memory pool is empty\n");

	return element;
}

static void sdp_mempool_free(void *element, struct sdp_mempool *pool)
{
	unsigned long flags;

	if (unlikely(!element))
		return;

	spin_lock_irqsave(&pool->lock, flags);
	if (!WARN_ON(pool->remain_nr == pool->max_nr))
		pool->elements[pool->remain_nr++] = element;
	spin_unlock_irqrestore(&pool->lock, flags);
}

struct sdp_unzip_t
{
	struct device *dev;
	struct semaphore sema;
	struct completion wait;
	void __iomem *base;
	phys_addr_t pLzBuf;
	sdp_unzip_cb_t isrfp;
	void *israrg;
	void *sdp1202buf;
	dma_addr_t sdp1202phybuf;
	dma_addr_t opages[32];
	void *vbuff;
	struct clk *rst;
	struct clk *clk;
	u32 isize;
	u32  opages_cnt;
	int decompressed;
	struct sdp_mempool *mempool;
	unsigned long long clock_ns;
};

static struct sdp_unzip_t *sdp_unzip = NULL;

static unsigned long long sdp_unzip_calls;
static unsigned long long sdp_unzip_errors;
static unsigned long long sdp_unzip_nsecs;
static unsigned int       sdp_unzip_quiet;

static unsigned long long sdp_unzip_get_nsecs(void)
{
#ifdef CONFIG_SDP_HW_CLOCK
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	return sched_clock();
#endif
}

void sdp_unzip_update_endpointer(void)
{
	sdp_unzip->clock_ns = sdp_unzip_get_nsecs();

#ifdef CONFIG_ARCH_SDP1202
	/* Start decoding */
	writel(1, sdp_unzip->base + R_GZIP_CMD);
#else
	/* Kick decoder to finish */
	writel(0x40000, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
#endif
}
EXPORT_SYMBOL(sdp_unzip_update_endpointer);

static void __maybe_unused sdp_unzip_clockgating(int bOn)
{
	
	if(!sdp_unzip->rst || !sdp_unzip->clk)
		return;
	
	if(bOn) {
		clk_enable(sdp_unzip->clk);
		udelay(1);
		clk_enable(sdp_unzip->rst);
		udelay(1);
	} else {
		udelay(1);
		clk_disable(sdp_unzip->rst);
		udelay(1);
		clk_disable(sdp_unzip->clk);
	}
}

static void sdp_unzip_dump(void)
{
	int i, j;
	u32 ibuff;
	unsigned int *buf;

	pr_err("-------------DUMP GZIP registers------------\n");
	for(i = 0; i < 0xDF ; i += 0x10)	{
		for(j = 0 ; j < 0x10 ; j += 4)
			pr_err("0x%08X ", readl(sdp_unzip->base + (u32) (i + j)));
		pr_err("\n");
	}
	pr_err("--------------------------------------------\n");

	ibuff = readl(sdp_unzip->base + R_GZIP_IN_BUF_ADDR);
	pr_err("Input buffer pointer phy=0x%08X vir=%p\n",
	       ibuff, sdp_unzip->vbuff);
	buf = (unsigned int *) sdp_unzip->vbuff;

	pr_err("-------------DUMP GZIP Input Buffers--------\n");
	for(i = 0 ; i < 10; i++)	{
		for(j = 0 ; j < 4 ; j++)
			pr_err("0x%08X ", buf[j+i*4]);
		pr_err("\n");
	}
	pr_err("--------------------------------------------\n");
}

static irqreturn_t sdp_unzip_isr(int irq, void* devId)
{
	int i;
	u32 value;
	int decompressed = readl(sdp_unzip->base + R_GZIP_ISIZE_VALUE_HDL);
	int err = 0;

	value = readl(sdp_unzip->base + R_GZIP_IRQ);
	writel(value, sdp_unzip->base + R_GZIP_IRQ);

	if((value == 0) || (value & 0x8))
	{
		sdp_unzip_errors++;
		err = readl(sdp_unzip->base + R_GZIP_ERR_CODE);
		if (!sdp_unzip_quiet) {
			pr_err("unzip: unzip interrupt flags=%d errorcode=0x%08X\n",
			       value, err);
			sdp_unzip_dump();
		}
	}

	if (sdp_unzip->sdp1202buf) {
		for (i = 0; i < sdp_unzip->opages_cnt; ++i) {
			struct page *page = phys_to_page(sdp_unzip->opages[i]);
			void *kaddr = kmap_atomic(page);
			if(kaddr == NULL)
			{
				pr_err("unzip: cannot kmap_atomic 0x%08llX address\n!!", sdp_unzip->opages[i]);
				continue;
			}
			memcpy(kaddr, sdp_unzip->sdp1202buf + i * PAGE_SIZE,
			       PAGE_SIZE);
			kunmap_atomic(kaddr);
		}
	}

#ifndef CONFIG_ARCH_SDP1202
	writel(0, sdp_unzip->base + R_GZIP_CMD);			//Gzip Reset
#endif

	sdp_unzip_clockgating(0);

	if (sdp_unzip->isrfp)
		sdp_unzip->isrfp(err, decompressed, sdp_unzip->israrg);

	sdp_unzip_calls += 1;
	sdp_unzip_nsecs += sdp_unzip_get_nsecs() - sdp_unzip->clock_ns;

	sdp_unzip->decompressed = err ? -abs(err) : decompressed;
	smp_wmb();
	complete(&sdp_unzip->wait);

	return IRQ_HANDLED;
}

struct sdp_unzip_buf *sdp_unzip_alloc(size_t len)
{
	struct sdp_unzip_buf *buf;

	BUG_ON(!sdp_unzip);

	/* In case of simplicity we do support the max buf size now */
	if (len > HW_MAX_IBUFF_SZ)
		return ERR_PTR(-EINVAL);

	buf = sdp_mempool_alloc(sdp_unzip->mempool, GFP_NOWAIT);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->size = len;
	return buf;
}

void sdp_unzip_free(struct sdp_unzip_buf *buf)
{
	BUG_ON(!sdp_unzip);
	if (IS_ERR_OR_NULL(buf))
		return;
	sdp_mempool_free(buf, sdp_unzip->mempool);
}

static void *sdp_unzip_mempool_alloc(gfp_t gfp_mask, void *pool)
{
	struct sdp_unzip_buf *buf;
	struct device *dev = pool;

	buf = kmalloc(sizeof(*buf), GFP_NOWAIT);
	if (!buf) {
		dev_err(dev, "failed to allocate sdp_unzip_buf\n");
		return NULL;
	}

	buf->__sz = HW_MAX_IBUFF_SZ;
	buf->vaddr = (void *)__get_free_pages(gfp_mask, get_order(buf->__sz));
	if (!buf->vaddr) {
		dev_err(dev, "failed to allocate unzip HW buf\n");
		goto err;
	}

	buf->paddr = dma_map_single(dev, buf->vaddr,
				    buf->__sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, buf->paddr)) {
		dev_err(dev, "unable to map input buffer\n");
		goto err;
	}

	return buf;

err:
	free_pages((unsigned long)buf->vaddr, get_order(buf->__sz));
	kfree(buf);

	return NULL;
}

static void sdp_unzip_mempool_free(void *mem_, void *pool)
{
	struct device *dev = pool;
	struct sdp_unzip_buf *buf = mem_;
	if (!buf || !buf->__sz)
		return;

	dma_unmap_single(dev, buf->paddr, buf->__sz, DMA_TO_DEVICE);
	free_pages((unsigned long)buf->vaddr, get_order(buf->__sz));
	kfree(buf);
}

static int sdp_unzip_decompress_start(void *vbuff, dma_addr_t pbuff,
				      int ilength, dma_addr_t *opages,
				      int npages, sdp_unzip_cb_t cb,
				      void *arg)
{
	int i;
	u32 value;

	/* Set Src Address
	   WTF? Why we always have extra GZIP_ALIGNSIZE? */
	ilength = ((ilength + GZIP_ALIGNSIZE) / GZIP_ALIGNSIZE)
		* GZIP_ALIGNSIZE;

	/* Set members */
	sdp_unzip->isize = ilength;
	sdp_unzip->vbuff = vbuff;
	sdp_unzip->isrfp = cb;
	sdp_unzip->israrg = arg;

	sdp_unzip_clockgating(1);

	/* WTF is below? In case of not SDP1202 we do
	 * write, then read, then write again.
	 * Is it really expected? */

#ifndef CONFIG_ARCH_SDP1202
	/* Gzip reset */
	writel(0, sdp_unzip->base + R_GZIP_CMD);
#endif
	readl(sdp_unzip->base + R_GZIP_CMD);
	/* Gzip reset */
	writel(0, sdp_unzip->base + R_GZIP_CMD);

	/* Set Source */
	writel(pbuff, sdp_unzip->base + R_GZIP_IN_BUF_ADDR);
	/* Set Src Size */
	writel(ilength, sdp_unzip->base + R_GZIP_IN_BUF_SIZE);
	/* Set LZ Buf Address */
	writel(sdp_unzip->pLzBuf, sdp_unzip->base + R_GZIP_LZ_ADDR);

	sdp_unzip->opages_cnt = npages;

	if (sdp_unzip->sdp1202buf) {
		sdp_unzip->sdp1202phybuf = __pa(sdp_unzip->sdp1202buf);
		/* Set phys addr of page */
		for (i = 0; i < npages; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
					  R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);

			writel(sdp_unzip->sdp1202phybuf + i * PAGE_SIZE,
			       sdp_unzip->base + off + ind * 4);
			sdp_unzip->opages[i] = opages[i];
		}
		writel(4096, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
	} else {
		/* Set page phys addr */
		for (i = 0; i < npages; ++i) {
			unsigned long off =
				(i == 0 ? R_GZIP_OUT_BUF_ADDR :
					  R_GZIP_ADDR_LIST1);
			unsigned int ind = (i == 0 ? 0 : i - 1);
			writel(opages[i], sdp_unzip->base + off + ind * 4);
			sdp_unzip->opages[i] = opages[i];
		}
		writel(4096, sdp_unzip->base + R_GZIP_OUT_BUF_SIZE);
	}

	value = GZIP_WINDOWSIZE << 16;
	value |= V_GZIP_CTL_OUT_PAR | V_GZIP_CTL_ADVMODE;
	value |= 0x11;

	/* Set Decoding Control Register */
	writel(value, sdp_unzip->base + R_GZIP_DEC_CTRL);
	/* Set Timeout Value */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_TIMEOUT);
	/* Set IRQ Mask Register */
	writel(0xffffffff, sdp_unzip->base + R_GZIP_IRQ_MASK);
	/* Set ECO value */
	writel(0x1E00, sdp_unzip->base + R_GZIP_PROC_DELAY);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);
	writel(0, sdp_unzip->base + R_GZIP_IN_BUF_WRITE_POINTER);
	if (soc_is_sdp1304())
		writel((1 << 8) | (3 << 4) | 1,
		       sdp_unzip->base + R_GZIP_IN_BUF_WRITE_CTRL);

#ifndef CONFIG_ARCH_SDP1202
	/* Start Decoding */
	writel(1, sdp_unzip->base + R_GZIP_CMD);
#endif

	return 0;
}

int sdp_unzip_decompress_async(struct sdp_unzip_buf *buf, int off,
			       struct page **opages, int npages,
			       sdp_unzip_cb_t cb, void *arg,
			       bool may_wait)
{
	dma_addr_t pages_phys[npages];
	int i, j, err = 0;

	if (!sdp_unzip) {
		pr_err("SDP Unzip Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Check contention */
	if (!may_wait && down_trylock(&sdp_unzip->sema))
		return -EBUSY;
	else if (may_wait)
		down(&sdp_unzip->sema);

	/* Prepare output pages */
	for (i = 0; i < npages; i++) {
		dma_addr_t phys = dma_map_page(sdp_unzip->dev, opages[i],
					       0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(sdp_unzip->dev, phys)) {
			dev_err(sdp_unzip->dev, "unable to map page %u\n", i);
			err = -EINVAL;
			goto err;
		}
		pages_phys[i] = phys;
	}

	err = sdp_unzip_decompress_start(buf->vaddr + off, buf->paddr + off,
					 buf->size, pages_phys, npages,
					 cb, arg);
	if (err)
		goto err;

	return err;

err:
	up(&sdp_unzip->sema);

	for (j = 0; j < i; j++)
		dma_unmap_page(sdp_unzip->dev, pages_phys[j],
			       PAGE_SIZE, DMA_FROM_DEVICE);

	return err;
}
EXPORT_SYMBOL(sdp_unzip_decompress_async);

/**
 * Must be called from the same task which has been started decompression
 */
int sdp_unzip_decompress_wait(void)
{
	int i, ret;

	wait_for_completion(&sdp_unzip->wait);
	smp_rmb();
	ret = xchg(&sdp_unzip->decompressed, 0);

	for (i = 0; i < sdp_unzip->opages_cnt; i++)
		dma_unmap_page(sdp_unzip->dev, sdp_unzip->opages[i],
			       PAGE_SIZE, DMA_FROM_DEVICE);

	up(&sdp_unzip->sema);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_wait);

int sdp_unzip_decompress_sync(void *ibuff, int ilength, struct page **opages,
			      int npages, bool may_wait)
{
	dma_addr_t ibuff_phys;
	struct sdp_unzip_buf buf;
	int ret;

	if (!sdp_unzip) {
		pr_err("SDP Unzip Engine is not Initialized!\n");
		return -EINVAL;
	}

	/* Prepare input buffer */
	ibuff_phys = dma_map_single(sdp_unzip->dev, ibuff,
				    ilength, DMA_TO_DEVICE);
	if (dma_mapping_error(sdp_unzip->dev, ibuff_phys)) {
		dev_err(sdp_unzip->dev, "unable to map input bufferr\n");
		return -EINVAL;
	}

	buf = (struct sdp_unzip_buf) {
		.vaddr = ibuff,
		.paddr = ibuff_phys,
		.size  = ilength
	};

	/* Start decompression */
	ret = sdp_unzip_decompress_async(&buf, 0, opages, npages,
					 NULL, NULL, may_wait);
	if (!ret) {
		/* Kick decompressor to start right now */
		sdp_unzip_update_endpointer();

		/* Wait and drop lock */
		ret = sdp_unzip_decompress_wait();
	}

	dma_unmap_single(sdp_unzip->dev, ibuff_phys,
			 ilength, DMA_TO_DEVICE);

	return ret;
}
EXPORT_SYMBOL(sdp_unzip_decompress_sync);

static void sdp_unzip_clk_free(void)
{
#ifdef CONFIG_OF
	if (!IS_ERR_OR_NULL(sdp_unzip->clk)) {
		clk_unprepare(sdp_unzip->clk);
		clk_put(sdp_unzip->clk);
	}
	if (!IS_ERR_OR_NULL(sdp_unzip->rst)) {
		clk_unprepare(sdp_unzip->rst);
		clk_put(sdp_unzip->rst);
	}
#endif
}

static int sdp_unzip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	int irq;
	int affinity = 0;
	void *buf;

#ifdef CONFIG_OF	
	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}
#endif

	sdp_unzip = devm_kzalloc(dev, sizeof(struct sdp_unzip_t), GFP_KERNEL);
	if(sdp_unzip == NULL)
	{
		dev_err(dev, "cannot allocate memory!!!\n");
		return -ENOMEM;
	}

	sema_init(&sdp_unzip->sema, 1);
	init_completion(&sdp_unzip->wait);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}
	
	sdp_unzip->base= devm_request_and_ioremap(&pdev->dev, res);

	if (sdp_unzip->base == NULL) {
		dev_err(dev, "ioremap failed\n");
		devm_kfree(dev, sdp_unzip);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ resource\n");
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}

	ret = request_irq(irq , sdp_unzip_isr, 0, dev_name(dev), sdp_unzip);

	if (ret != 0) {
		dev_err(dev, "cannot request IRQ %d\n", irq);
		devm_kfree(dev, sdp_unzip);
		return -ENODEV;
	}

#ifndef CONFIG_OF
	affinity = 1;
#else
	if(!of_property_read_u32(dev->of_node, "irq-affinity", &affinity))
#endif
		if(num_online_cpus() > affinity) {
			irq_set_affinity(irq, cpumask_of(affinity));
		}

	if(soc_is_sdp1202())
	{
		sdp_unzip->sdp1202buf = kmalloc(128*1024, GFP_KERNEL);
		if(sdp_unzip->sdp1202buf == NULL)
		{
			dev_err(dev, "output buffer allocation failed!!!\n");
			devm_kfree(dev, sdp_unzip);
			return -ENOMEM;
		}
	}

#ifdef CONFIG_OF
	sdp_unzip->clk = clk_get(dev, "gzip_clk");
	if (IS_ERR(sdp_unzip->clk)) {
		dev_err(dev, "cannot find gzip_clk: %ld!\n",
			PTR_ERR(sdp_unzip->clk));
		sdp_unzip->clk = NULL;
	}
	else
		clk_prepare(sdp_unzip->clk);
	sdp_unzip->rst = clk_get(dev, "gzip_rst");
	if (IS_ERR(sdp_unzip->rst)) {
		dev_err(dev, "cannot find gzip_rst: %ld!\n",
			PTR_ERR(sdp_unzip->rst));
		sdp_unzip->rst = NULL;
	}
	else
		clk_prepare(sdp_unzip->rst);
#endif

	buf = kmalloc(4096, GFP_KERNEL);
	if(buf == NULL)	{
		dev_err(dev, "cannot allocate lzbuf memory!!!\n");
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}
	sdp_unzip->pLzBuf = __pa(buf);

	sdp_unzip->mempool = sdp_mempool_create(HW_MAX_SIMUL_THR,
						sdp_unzip_mempool_alloc,
						sdp_unzip_mempool_free, dev);
	if (!sdp_unzip->mempool) {
		dev_err(dev, "cannot allocate mempool for sdp_unzip\n");
		kfree(buf);
		sdp_unzip_clk_free();
		devm_kfree(dev, sdp_unzip);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, (void *) sdp_unzip);
	sdp_unzip->dev = dev;

	dev_info(dev, "Registered unzip driver!!\n");

	return 0;
}

static int sdp_unzip_remove(struct platform_device *pdev)
{
	sdp_mempool_destroy(sdp_unzip->mempool);
	kfree(__va(sdp_unzip->pLzBuf));
	sdp_unzip_clk_free();
	devm_kfree(&pdev->dev, sdp_unzip);

	return 0;
}

static const struct of_device_id sdp_unzip_dt_match[] = {
	{ .compatible = "samsung,sdp-unzip", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_unzip_dt_match);

static struct platform_driver sdp_unzip_driver = {
	.probe		= sdp_unzip_probe,
	.remove		= sdp_unzip_remove,
	.driver = {
		.name	= "sdp-unzip",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_unzip_dt_match),
	},
};

int sdp_unzip_init(void)
{
	return platform_driver_register(&sdp_unzip_driver);
}
subsys_initcall(sdp_unzip_init);

static void __exit sdp_unzip_exit(void)
{
	platform_driver_unregister(&sdp_unzip_driver);
}
module_exit(sdp_unzip_exit);

#ifndef CONFIG_VD_RELEASE

struct sdp_unzip_file {
	void         *in_buff;
	void         *out_buff;
	size_t        in_sz;
	bool          up_to_date;
	struct page  *dst_pages[32];
};

static void __sdp_unzip_free(struct sdp_unzip_file *sdp_file, unsigned int sz)
{
	unsigned int i;

	vunmap(sdp_file->out_buff);
	for (i = 0; i < sz; i++)
		__free_page(sdp_file->dst_pages[i]);
	free_pages((unsigned long)sdp_file->in_buff,
		   get_order(HW_MAX_IBUFF_SZ));
	kfree(sdp_file);
}

static int sdp_unzip_open(struct inode *inode, struct file *file)
{
	unsigned int i = 0;
	struct sdp_unzip_file *sdp_file;

	sdp_file = kzalloc(sizeof(*sdp_file), GFP_KERNEL);
	if (!sdp_file)
		return -ENOMEM;

	sdp_file->in_buff = (void *)__get_free_pages(GFP_KERNEL,
						get_order(HW_MAX_IBUFF_SZ));
	if (!sdp_file->in_buff)
		goto err;

	for (i = 0; i < ARRAY_SIZE(sdp_file->dst_pages); ++i) {
		void *addr = (void *)__get_free_page(GFP_KERNEL);
		if (!addr)
			goto err;
		sdp_file->dst_pages[i] = virt_to_page(addr);
	}

	sdp_file->out_buff = vmap(sdp_file->dst_pages,
				  ARRAY_SIZE(sdp_file->dst_pages),
				  VM_MAP, PAGE_KERNEL);
	if (!sdp_file->out_buff)
		goto err;

	file->private_data = sdp_file;

	return nonseekable_open(inode, file);

err:
	__sdp_unzip_free(sdp_file, i);
	return -ENOMEM;
}

static int sdp_unzip_close(struct inode *inode, struct file *file)
{
	struct sdp_unzip_file *sdp_file = file->private_data;

	__sdp_unzip_free(sdp_file, ARRAY_SIZE(sdp_file->dst_pages));
	return 0;
}

static ssize_t sdp_unzip_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	ssize_t ret;
	struct sdp_unzip_file *sdp_file = file->private_data;
	size_t max_out = ARRAY_SIZE(sdp_file->dst_pages) * PAGE_SIZE;

	if (count < max_out)
		return -EINVAL;
	if (!sdp_file->in_sz)
		return 0;

	/* Yes, right, no synchronization here.
	 * sdp unzip sync has its own synchronization, so we do not care
	 * about corrupted data with simultaneous read/write on the same
	 * fd, we have to test different scenarious and data corruption
	 * is one of them */
	ret = sdp_unzip_decompress_sync(sdp_file->in_buff,
					ALIGN(sdp_file->in_sz, 8),
					sdp_file->dst_pages,
					ARRAY_SIZE(sdp_file->dst_pages),
					true);
	sdp_file->in_sz = 0;

	if (ret < 0 || !ret)
		return -EINVAL;

	if (copy_to_user(buf, sdp_file->out_buff, (unsigned long)ret))
		return -EFAULT;

	return ret;
}

static ssize_t sdp_unzip_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct sdp_unzip_file *sdp_file = file->private_data;

	/* Worry about synchronization? Please read comments in read function */

	if (count > HW_MAX_IBUFF_SZ)
		return -EINVAL;
	if (copy_from_user(sdp_file->in_buff, buf, count))
		return -EFAULT;

	sdp_file->in_sz = count;

	return (ssize_t)count;
}

static const struct file_operations sdp_unzip_fops = {
	.owner	= THIS_MODULE,
	.open    = sdp_unzip_open,
	.release = sdp_unzip_close,
	.llseek	 = no_llseek,
	.read	 = sdp_unzip_read,
	.write	 = sdp_unzip_write
};

static struct miscdevice sdp_unzip_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "sdp_unzip",
	.fops	= &sdp_unzip_fops,
};

static struct dentry *sdp_unzip_debugfs;

static void sdp_unzip_debugfs_create(void)
{
	sdp_unzip_debugfs = debugfs_create_dir("sdp_unzip", NULL);
	if (!IS_ERR_OR_NULL(sdp_unzip_debugfs)) {
		debugfs_create_u64("calls",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip_calls);
		debugfs_create_u64("errors",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip_errors);
		debugfs_create_u64("nsecs",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   sdp_unzip_debugfs, &sdp_unzip_nsecs);
		debugfs_create_bool("quiet",
				    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				    sdp_unzip_debugfs, &sdp_unzip_quiet);
	}
}

static void sdp_unzip_debugfs_destroy(void)
{
	debugfs_remove_recursive(sdp_unzip_debugfs);
}

static int __init sdp_unzip_file_init(void)
{
	if (!sdp_unzip)
		return -EINVAL;

	sdp_unzip_debugfs_create();
	return misc_register(&sdp_unzip_misc);
}

static void __exit sdp_unzip_file_cleanup(void)
{
	sdp_unzip_debugfs_destroy();
	misc_deregister(&sdp_unzip_misc);
}

module_init(sdp_unzip_file_init);
module_exit(sdp_unzip_file_cleanup);

#endif /* CONFIG_VD_RELEASE */

MODULE_DESCRIPTION("Samsung SDP SoCs HW Decompress driver");
MODULE_LICENSE("GPL v2");

