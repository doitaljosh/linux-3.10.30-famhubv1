/*
 * phy-hawk.c - USB PHY for USB3.0 controller in HAWK platform.
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * Author: Ikjoon Jang <ij.jang@samsung.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/usb/hawk_usb.h>
#include <mach/soc.h>

#define	phy_to_hawkphy(x)	container_of((x), struct hawk_usb3_phy, phy)

#undef DEBUG_HAWK_PHY
#ifdef DEBUG_HAWK_PHY
#define phy_dbg(p, fmt, ...)	dev_info(p->phy.dev, fmt, ##__VA_ARGS__)
#else
#define phy_dbg(p, fmt, ...)
#endif

#define phy_info(p, fmt, ...)	dev_info(p->phy.dev, fmt, ##__VA_ARGS__)

static int vboost_level = -1;
module_param(vboost_level, int, 0);
MODULE_PARM_DESC(vboost_level, "Tx voltage boost level");

static void phy_clk_enable(struct hawk_usb3_phy *phy)
{
	phy_dbg(phy, "clk enable.\n");

	if (phy->usb2_clk)
		clk_prepare_enable(phy->usb2_clk);
	if (phy->usb3_clk)
		clk_prepare_enable(phy->usb3_clk);
	udelay(500);
}

static void phy_clk_disable(struct hawk_usb3_phy *phy)
{
	phy_dbg(phy, "clk disable.\n");

	if (phy->usb2_clk)
		clk_disable_unprepare(phy->usb2_clk);
	if (phy->usb3_clk)
		clk_disable_unprepare(phy->usb3_clk);
	udelay(500);
}

/* we're using clk api to control sw reset */
static void phy_reset_assert(struct hawk_usb3_phy *phy, int assert)
{
	int reset_on = atomic_cmpxchg(&phy->reset_state, !assert, !!assert);
	
	if (reset_on != !!assert) {
		phy_info(phy, "reset %s.\n", assert ? "on" : "off");
		if (reset_on)
			phy_clk_enable(phy);
		else
			phy_clk_disable(phy);
	}
}

static inline u32 set_bits_masked(int bits, int shift, u32 newval, u32 val)
{
	const u32 mask = ((1 << bits) - 1);
	val &= ~(mask << shift);
	val |= (newval & mask) << shift;
	return val;
}

static inline u32 phy_readl(struct hawk_usb3_phy *phy, void __iomem *reg)
{
	u32 ret = readl(reg);
	phy_dbg (phy, "readl(%p) := %08x\n", reg, ret);
	return ret;
}

static inline void phy_writel(struct hawk_usb3_phy *phy, u32 val, void __iomem *reg)
{
	writel(val, reg);
	phy_dbg(phy, "%p := %08x\n", reg, val);
}

static void _hawk_phy_init(struct hawk_usb3_phy *phy)
{
	u32 v, vboost_val;

	if (atomic_inc_return(&phy->enable_count) != 1)
		return;

	/* module parameter */
	if (vboost_level >= 0 && vboost_level < 8)
		vboost_val = vboost_level;
	else
		vboost_val = (get_sdp_board_type() == SDP_BOARD_AV) ? 4 : 5;

	phy_reset_assert(phy, 1);

	/* HSPHY clock */
	v = phy_readl(phy, phy->gpr0_regs + 0x10);
	v = set_bits_masked(2, 1, 2, v);	/* REFCLKSEL = internal */
	v = set_bits_masked(3, 3, 7, v);	/* FSEL = 7 */
	phy_writel (phy, v, phy->gpr0_regs + 0x10);

	v = phy_readl(phy, phy->gpr1_regs + 0x00);
	v = set_bits_masked(1, 8, 0, v);	/* PLLBTUNE = 0 */
	phy_writel (phy, v, phy->gpr1_regs + 0x00);

	/* SSPHY clock */
	v = phy_readl(phy, phy->gpr0_regs + 0x10);
	v = set_bits_masked(9, 23, 0, v);	/* ref_alt_clk 100Mhz */
	v = set_bits_masked(1, 19, 1, v);	/* ssc_en */
	v = set_bits_masked(3, 20, 2, v);	/* ssc 2 */
	v = set_bits_masked(1, 17, 1, v);	/* ref_ssp_en */
	v = set_bits_masked(1, 16, 0, v);	/* ref_clkdiv2 = 0 */
	v = set_bits_masked(8, 9, 0x7d, v);	/* mpll_multiplier = 25 */
	phy_writel (phy, v, phy->gpr0_regs + 0x10);

	/* Eye tuning */
	phy_writel (phy, 0x20ff0209, phy->gpr0_regs + 0x18);	/* pcs_tx_swing_full = 127 */
	phy_writel (phy, 0x00002050 | vboost_val, phy->gpr0_regs + 0x20);

	/* reset off */
	phy_reset_assert(phy, 0);

	/* rxdet_meas_time */
	v = phy_readl(phy, phy->phy_regs + 0x40);
	v = set_bits_masked(8, 4, 0x4, v);
	phy_writel(phy, v, phy->phy_regs + 0x40);

	/* power_presence */
	v = phy_readl(phy, phy->gpr1_regs + 0x08);
	v = set_bits_masked(1, 0, 1, v);
	phy_writel(phy, v, phy->gpr1_regs + 0x08);

	dev_info(phy->phy.dev, "initialzed. tx vboost = %u\n", vboost_val);
}

static void _hawk_phy_exit(struct hawk_usb3_phy *phy)
{
	int cnt = atomic_dec_return(&phy->enable_count);
	if (cnt == 0) {
		dev_info(phy->phy.dev, "shutdown.\n");
	}
	WARN_ON(cnt < 0);
}

static int hawk_phy_init(struct usb_phy *_phy)
{
	struct hawk_usb3_phy *phy = phy_to_hawkphy(_phy);
	_hawk_phy_init(phy);
	return 0;
}

static void hawk_phy_shutdown(struct usb_phy *_phy)
{
	struct hawk_usb3_phy *phy = phy_to_hawkphy(_phy);
	_hawk_phy_exit(phy);
}

static int hawk_usb3_probe(struct platform_device *pdev)
{
	struct hawk_usb3_phy *phy;
	struct resource	*phy_res, *gpr0_res, *gpr1_res;
	struct clk *clk_usb2, *clk_usb3;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		dev_err(&pdev->dev, "Failed to allocate phy device.\n");
		return -ENOMEM;
	}

	gpr0_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpr1_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (IS_ERR(gpr0_res) || IS_ERR(gpr1_res)) {
		dev_err(&pdev->dev, "Failed to GPR resource\n");
		return -ENODEV;
	}
	phy->gpr0_regs = devm_ioremap_resource(&pdev->dev, gpr0_res);
	phy->gpr1_regs = devm_ioremap_resource(&pdev->dev, gpr1_res);
	if (IS_ERR(phy->gpr0_regs) || IS_ERR(phy->gpr1_regs)) {
		dev_err (&pdev->dev, "Failed to map GPR.\n");
		return -ENOMEM;
	}
	
	phy_res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (IS_ERR(phy_res)) {
		dev_err(&pdev->dev, "Failed to PHY resource\n");
		return -ENODEV;
	}
	phy->phy_regs = devm_ioremap_resource(&pdev->dev, phy_res);
	if (IS_ERR(phy->phy_regs)) {
		dev_err (&pdev->dev, "Failed to map PHY.\n");
		return -ENOMEM;
	}
	
	clk_usb2 = devm_clk_get(&pdev->dev, "usb2_clk");
	if (!IS_ERR(clk_usb2))
		phy->usb2_clk = clk_usb2;
	else
		dev_warn(&pdev->dev, "No USB2 clock specified.\n");

	clk_usb3 = devm_clk_get(&pdev->dev, "usb3_clk");
	if (!IS_ERR(clk_usb3))
		phy->usb3_clk = clk_usb3;
	else
		dev_warn(&pdev->dev, "No USB3 clock specified.\n");

	phy->phy.dev		= &pdev->dev;
	phy->phy.label		= "hawk-usb3-phy";
	phy->phy.init		= hawk_phy_init;
	phy->phy.shutdown	= hawk_phy_shutdown;
	phy->phy.type		= USB_PHY_TYPE_USB3;

	platform_set_drvdata(pdev, phy);

	atomic_set(&phy->reset_state, 1);

	usb_add_phy_dev(&phy->phy);

	dev_info(&pdev->dev, "registered. %p %p %p\n", phy->gpr0_regs, phy->gpr1_regs, phy->phy_regs);
	return 0;
}

static int hawk_usb3_remove(struct platform_device *pdev)
{
	struct hawk_usb3_phy *phy = platform_get_drvdata(pdev);

	usb_remove_phy(&phy->phy);
	phy_reset_assert(phy, 1);

	return 0;
}

static int hawk_usb3_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hawk_usb3_phy *phy = platform_get_drvdata(pdev);
#if 0
	_hawk_phy_exit(&phy->phy);
#endif
	dev_info(phy->phy.dev, "suspend.\n");
	return 0;
}

static int hawk_usb3_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hawk_usb3_phy *phy = platform_get_drvdata(pdev);
#if 0
	_hawk_phy_init(&phy->phy);
#endif
	dev_info(phy->phy.dev, "resume.\n");
	return 0;
}

static SIMPLE_DEV_PM_OPS(hawk_usb3_pm_ops, hawk_usb3_suspend, hawk_usb3_resume);

static const struct of_device_id hawk_usb3_id_table[] = {
	{ .compatible = "samsung,hawk-usb3-phy" },
	{}
};
MODULE_DEVICE_TABLE(of, hawk_usb3_id_table);

static struct platform_driver hawk_usb3_driver = {
	.probe		= hawk_usb3_probe,
	.remove		= hawk_usb3_remove,
	.driver		= {
		.name	= "hawk-usb3-phy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(hawk_usb3_id_table),
		.pm	= &hawk_usb3_pm_ops,
	},
};

module_platform_driver(hawk_usb3_driver);

MODULE_ALIAS("platform: hawk-usb3-phy");
MODULE_AUTHOR("ij.jang@samsung.com");
MODULE_DESCRIPTION("HAWK USB3 phy driver");
MODULE_LICENSE("GPL");

