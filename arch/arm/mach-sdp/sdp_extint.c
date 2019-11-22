/*************************************8********************************************************
 *
 *	sdp_extint.c (SDP External Interrupt Controller Driver)
 *
 *	author : seungjun.heo@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
	Sep,10,2012 	seungjun.heo	created
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <asm/uaccess.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#define EXTINT_REG_STAT_CLEAR	0x0
#define EXTINT_REG_MASK		0x4
#define EXTINT_REG_MASKED_STAT	0x8
#define EXTINT_REG_POLARITY	0xC
#define EXTINT_REG_TYPE		0x10

#define EXTINT_IRQ_TSD	0
#define EXTINT_IRQ_AIO	1
#define EXTINT_IRQ_AE	2
#define EXTINT_IRQ_JPEG	3
#define EXTINT_IRQ_DP	4
#define EXTINT_IRQ_GA	5
#define EXTINT_IRQ_MFD	6

#define MAX_EXTINT	4

int sdp_extint_enable(u32 phy_base, int n_mpirq);
int sdp_extint_disable(u32 phy_base, int n_mpirq);
int sdp_extint_request_irq(u32 phy_base, int n_mpirq, void (*fp)(void*), void* args);

#define NR_MP_INTR	32

struct sdp_extint_isr_t {
	void * 	args;
	void (*fp)(void*);
};

static struct sdp_extint_t {
	void *base;
	u32 phy_base;
	int nr_irqs;
	u32 irq;
	spinlock_t lock;
	struct sdp_extint_isr_t *handler;
	u32 backup;
} *sdp_extint;

static int sdp_extint_nr = 0;

static struct sdp_extint_t * sdp_get_extint(u32 phy_base)
{
	struct sdp_extint_t *sdp_int;
	int i;

	if(sdp_extint == NULL)
		return NULL;

	for(i = 0; i < sdp_extint_nr; i++) {
		sdp_int = &sdp_extint[i];
		if(sdp_int->phy_base == phy_base)
			return sdp_int;
	}
	return NULL;
}

static void __init sdp_extint_preinit(struct sdp_extint_t *sdp_int)	//mask 설정 안하는 이유 : 기본 셋팅이 masking
{
	writel(0xFFFFFFFF, (void *) ((u32) sdp_int->base + EXTINT_REG_STAT_CLEAR));	// all clear
}

static int _sdp_extint_enable(struct sdp_extint_t *sdp_int, int n_mpirq)
{
	unsigned long flags, val;

	if(!sdp_int) {
		pr_err("Cannot find Externel Interrupt Merger!!\n");
		return -1;
	}

	if(n_mpirq >= sdp_int->nr_irqs) {
		pr_err("Irq %d num is over Max IRQs!!\n", n_mpirq);
		return -1;
	}

	spin_lock_irqsave(&sdp_int->lock, flags);
	
	val = readl((void *) ((u32) sdp_int->base + EXTINT_REG_MASK));
	val &= ~(1UL << n_mpirq);
	writel(val, (void *) ((u32) sdp_int->base + EXTINT_REG_MASK));	//enable
	writel(1 << n_mpirq, (void *) ((u32) sdp_int->base + EXTINT_REG_STAT_CLEAR));	//clear

	spin_unlock_irqrestore(&sdp_int->lock, flags);

	return 0;
}

int sdp_extint_enable(u32 phy_base, int n_mpirq)
{
	struct sdp_extint_t *sdp_int = sdp_get_extint(phy_base);

	return _sdp_extint_enable(sdp_int, n_mpirq);
}
EXPORT_SYMBOL(sdp_extint_enable);

static int _sdp_extint_disable(struct sdp_extint_t *sdp_int, int n_mpirq)
{
	unsigned long flags, val;

	if(!sdp_int) {
		pr_err("Cannot find Externel Interrupt Merger!!\n");
		return -1;
	}

	if(n_mpirq >= sdp_int->nr_irqs) {
		pr_err("Irq %d num is over Max IRQs!!\n", n_mpirq);
		return -1;
	}

	spin_lock_irqsave(&sdp_int->lock, flags);
	
	val = readl((void *) ((u32) sdp_int->base + EXTINT_REG_MASK));
	val |= (1UL << n_mpirq);
	writel(val, (void *) ((u32) sdp_int->base + EXTINT_REG_MASK));	//disable
	writel(1 << n_mpirq, (void *) ((u32) sdp_int->base + EXTINT_REG_STAT_CLEAR));	//clear

	spin_unlock_irqrestore(&sdp_int->lock, flags);

	return 0;
}

int sdp_extint_disable(u32 phy_base, int n_mpirq)
{
	struct sdp_extint_t *sdp_int = sdp_get_extint(phy_base);

	return _sdp_extint_disable(sdp_int, n_mpirq);
}
EXPORT_SYMBOL(sdp_extint_disable);

static int _sdp_extint_request_irq(struct sdp_extint_t *sdp_int, int n_mpirq, void (*fp)(void*), void* args)
{
	if(!sdp_int) {
		pr_info("Cannot find Externel Interrupt Merger, this board may not have 2ndMP or US!!\n");
		return -1;
	}

	if(n_mpirq >= sdp_int->nr_irqs) {
		pr_err("Irq %d num is over Max IRQs!!\n", n_mpirq);
		return -1;
	}

	if(sdp_int->handler[n_mpirq].fp) {
		pr_err("[0x%08X] %d sub ISR slot not empty\n", sdp_int->phy_base, n_mpirq);
		return -1;
	}
		
	sdp_int->handler[n_mpirq].fp = fp;
	sdp_int->handler[n_mpirq].args = args;

	pr_info("[0x%08X] %d sub ISR is registered successfully\n", sdp_int->phy_base, n_mpirq);

	_sdp_extint_enable(sdp_int, n_mpirq);

	return 0;
}

int sdp_extint_request_irq(u32 phy_base, int n_mpirq, void (*fp)(void*), void* args)
{
	struct sdp_extint_t *sdp_int = sdp_get_extint(phy_base);

	return _sdp_extint_request_irq(sdp_int, n_mpirq, fp, args);
}
EXPORT_SYMBOL(sdp_extint_request_irq);

static void call_extint_fp (int n_mpirq, struct device *dev)
{
	struct sdp_extint_isr_t* p_extint;
	struct sdp_extint_t *sdp_int = dev_get_drvdata(dev);

	BUG_ON(sdp_int == NULL);

	p_extint = &sdp_int->handler[n_mpirq];

	if(p_extint->fp) {
		p_extint->fp(p_extint->args);
	}
	else {
		pr_err("[0x%08X] %d sub not exist ISR\n", sdp_int->phy_base, n_mpirq);
		pr_err("[0x%08X] %d sub is disabled\n", sdp_int->phy_base, n_mpirq);
		sdp_extint_disable(sdp_int->phy_base, n_mpirq);
	}
}

static irqreturn_t sdp_extint_isr(int irq, void* dev)
{
	int idx;
	int n_mpirq = 0;	
	u32 status;
	struct sdp_extint_t *sdp_int = dev_get_drvdata((struct device *) dev);

	BUG_ON(sdp_int == NULL);

	status = readl((void *) ((u32) sdp_int->base + EXTINT_REG_MASKED_STAT));

	if(!status) return IRQ_NONE;
	for(idx = 0; idx < sdp_int->nr_irqs; idx++){
		if(status & (1UL << idx))
		{
			n_mpirq = idx;
			call_extint_fp(n_mpirq, dev);
		}
	}
	writel(status, (void *) ((u32) sdp_int->base + EXTINT_REG_STAT_CLEAR));
	return IRQ_HANDLED;
}

static int sdp_extint_of_do_initregs(struct device *dev)
{
	int psize;
	const u32 *initregs;
	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -1;
	}

	/* Get "initregs" property */
	initregs = of_get_property(dev->of_node, "initregs", &psize);

	if (initregs != NULL) {
		int onesize;
		int i = 0;

		psize /= 4;/* each cell size 4byte */
		onesize = 3;
		for (i = 0; psize >= onesize; psize -= onesize, initregs += onesize, i++) {
			u32 addr, mask, val;
			u8 * __iomem iomem;

			addr = be32_to_cpu(initregs[0]);
			mask = be32_to_cpu(initregs[1]);
			val = be32_to_cpu(initregs[2]);

			iomem = ioremap(addr, sizeof(u32));
			if(iomem) {
				writel( (readl(iomem)&~mask) | (val&mask), iomem );
				dev_printk(KERN_DEBUG, dev,
					"of initreg addr 0x%08x, mask 0x%08x, val 0x%08x\n",
					addr, mask, val);
				iounmap(iomem);
			} else {
				return -ENOMEM;
			}
		}
	}
	return 0;
}

static int is_load_sdp_extint(struct device *dev)
{
	int psize;
	const u32 *have_extint;
	u32 addr, mask, val;
	void * __iomem iomem;
	int ret;

	have_extint = of_get_property(dev->of_node, "have_extint", &psize);
	if(have_extint == NULL)
	{
		return 1;
	}

	addr = be32_to_cpu(have_extint[0]);
	mask = be32_to_cpu(have_extint[1]);
	val = be32_to_cpu(have_extint[2]);

	iomem = ioremap(addr, sizeof(u32));
	if(iomem)	{
		if((readl(iomem) & mask) == val)
			ret = 1;
		else
			ret = 0;
	}	else	{
		dev_err(dev, "cannot allocate %08X address please check...\n", addr);
		return 0;
	};
	iounmap(iomem);
	return ret;
}

static int sdp_extint_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct sdp_extint_t *sdp_int;
	int ret = 0;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	if(!is_load_sdp_extint(dev))
	{
		dev_err(dev, "this board don't have extint chip\n");
		return -ENODEV;
	}

	if(!sdp_extint) {
		sdp_extint = devm_kzalloc(dev, sizeof(struct sdp_extint_t) * MAX_EXTINT, GFP_KERNEL);
		if (!sdp_extint) {
			dev_err(dev, "failed to allocate memory\n");
			return -ENOMEM;
		}
	}
	
	sdp_int = &sdp_extint[sdp_extint_nr];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		return -ENOENT;
	}

	sdp_int->base = devm_request_and_ioremap(&pdev->dev, res);

	if (!sdp_int->base) {
		dev_err(dev, "ioremap failed\n");
		return -ENODEV;
	}

	sdp_int->phy_base = (u32) res->start;

	if (of_property_read_u32(dev->of_node, "nr-irqs", &sdp_int->nr_irqs))
		sdp_int->nr_irqs = NR_MP_INTR;
	
	sdp_int->handler = devm_kzalloc(dev, sizeof(struct sdp_extint_isr_t) * ((u32) sdp_int->nr_irqs), GFP_KERNEL);
	if (!sdp_int->handler) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	sdp_extint_of_do_initregs(dev);
	
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;
	}

	sdp_int->irq = (u32) ret;

	sdp_extint_preinit(sdp_int);

	spin_lock_init(&sdp_int->lock);

	platform_set_drvdata(pdev, sdp_int);

	ret = request_irq(sdp_int->irq, sdp_extint_isr, IRQF_DISABLED | IRQF_SHARED, pdev->name, (void*) dev);
	if (ret)
	{
		dev_err(dev, "request_irq failed\n");
		return -ENODEV;
	}

	dev_info(dev, "sdp-extint initialized.. IRQ=%d, nirqs=%d\n", sdp_int->irq, sdp_int->nr_irqs);

	sdp_extint_nr++;

	return 0;
}

static int sdp_extint_remove(struct platform_device *pdev)
{
	return 0;
}

static int sdp_extint_suspend(struct device *dev)
{
	struct sdp_extint_t *sdp_int = dev_get_drvdata(dev);
	unsigned long flags;
	ktime_t start, end;

	start = ktime_get();

	BUG_ON(sdp_int == NULL);

	spin_lock_irqsave(&sdp_int->lock, flags);
	
	sdp_int->backup = readl((void *) ((u32) sdp_int->base + EXTINT_REG_MASK));

	spin_unlock_irqrestore(&sdp_int->lock, flags);

	end = ktime_get();

	dev_info(dev, "suspend time : %dns\n", (u32) ktime_to_ns(ktime_sub(end, start)));
	
	return 0;
}

static int sdp_extint_resume(struct device *dev)
{
	struct sdp_extint_t *sdp_int = dev_get_drvdata(dev);
	unsigned long flags;

	BUG_ON(sdp_int == NULL);
	
	sdp_extint_of_do_initregs(dev);

	spin_lock_irqsave(&sdp_int->lock, flags);
	
	writel(sdp_int->backup, (void *) ((u32) sdp_int->base + EXTINT_REG_MASK));	//enable
	writel(0xFFFFFFFF, (void *) ((u32) sdp_int->base + EXTINT_REG_STAT_CLEAR));	//clear

	spin_unlock_irqrestore(&sdp_int->lock, flags);	

	return 0;
}

static const struct of_device_id sdp_extint_dt_match[] = {
	{ .compatible = "samsung,sdp-extint" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_extint_dt_match);

static struct dev_pm_ops sdp_extint_pm = {
	.suspend_noirq	= sdp_extint_suspend,
	.resume_noirq	= sdp_extint_resume,
};


static struct platform_driver sdp_extint_driver = {
	.probe		= sdp_extint_probe,
	.remove 	= sdp_extint_remove,
	.driver 	= {
		.name = "sdp-extint",
#ifdef CONFIG_OF
		.of_match_table = sdp_extint_dt_match,
#endif
		.pm	= &sdp_extint_pm,
	},
};

static int __init sdp_extint_init(void)
{
	return platform_driver_register(&sdp_extint_driver);
}

static void __exit sdp_extint_exit(void)
{
	platform_driver_unregister(&sdp_extint_driver);
}

arch_initcall(sdp_extint_init);
module_exit(sdp_extint_exit);

MODULE_DESCRIPTION("Samsung DTV External Interrupt controller driver");
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("seungjun.heo <seungjun.heo@samsung.com>");

