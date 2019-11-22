#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/ahci_sdp.h>
#include <asm/io.h>

struct golfv_phy {
	struct sdp_sata_phy phy;
	struct clk *clk;
	u32 __iomem *gpr_regs;
};

#define to_golfv_phy(phy)	((struct golfv_phy*)(phy))

static int golfv_phy_init(struct sdp_sata_phy *_phy)
{
	struct golfv_phy *phy = to_golfv_phy(_phy);
	u32 val;
	void __iomem *gpr_base = phy->gpr_regs;

	/* don't know default state in bootloader, force to disable */
	if (phy->clk) {
		clk_prepare_enable(phy->clk);
		udelay(10);
		clk_disable_unprepare(phy->clk);
		udelay(10);
	}
	/* XXX: assume REFCLK = GMAC_PLL, 125Mhz set by bootloader.
	 * in clk-api manner, this clk's parent should be 'gmac_pll'. */
#if 0
	val = readl((void*)(VA_SFR0_BASE + 0x000908a4)) | (1 << 14);
	writel(val, (void*)(VA_SFR0_BASE + 0x000908a4));
#endif
	/* SATA_clk_set to 1 for 25MHz & 125MHz */
	val = readl(gpr_base) | (1 << 31);
	writel(val, gpr_base);

	/* Step 3. Set MPLL_CK_OFF to 1'b1, MPLL prescale for 25MHz 2'b01 */
	/* MPLL_NCY5 [5:4] 00 */
	val = readl(gpr_base) & (~(0x3 << 4));
	writel(val, gpr_base);

	/* Step 4. Set MPLL_PRESCAACLE, MPLL_NCY, MPLL_NCY5 to appropriate value */
	udelay(1);

	/* Step 5. Set MPLL_CK_OFF to 1'b0 */
	// MPLL_NCY 00101
	val = readl(gpr_base + 0x4) & (~(1 << 16));
	val = (val & (~0x1F)) | 0x5;
	writel(val, gpr_base + 0x4);

	/* Step 6. Perform a PHY reset by either toggling RESET_N. (more than one ACLK_I cycle) */
	if (phy->clk)
		clk_prepare_enable(phy->clk);

	/* Step 7. Wait 100us */
	udelay(200);

	/* Step 8. Deassert ARESETX_I,  Step 9. Wait 200ns: done by ahci_sdp */
	return 0;
}

static int golfv_phy_exit(struct sdp_sata_phy *_phy)
{
	struct golfv_phy *phy = to_golfv_phy(_phy);
	u32 val;
	void __iomem *gpr_base = phy->gpr_regs;

	/* phy power down, write 1;b1 to P0PHYCR PD bit */
#if 0
	dev_info(&pdev->dev, "phy power down\n");
	val = readl(hpriv->mmio + 0x178);
	val = val | (1 << 23);
	writel(val, hpriv->mmio + 0x178);
#endif

	/* wait more than 200ns */
	udelay(1);
	
	/* Set MPLL_CK_OFF to 1'b1 */
	val = readl(gpr_base + 0x4) | (1 << 16);
	writel(val, gpr_base + 0x4);

	if (phy->clk)
		clk_disable_unprepare(phy->clk);

	return 0;
}

static int golfv_ahci_phy_probe(struct platform_device *pdev)
{
	struct golfv_phy *phy;
	struct resource *gpr_res;
	u32 __iomem *gpr_regs;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	
	gpr_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!gpr_res) {
		dev_err(&pdev->dev, "failed to get gpr resource.\n");
		return -ENODEV;
	}
	gpr_regs = devm_ioremap_resource(&pdev->dev, gpr_res);
	if (IS_ERR(gpr_regs)) {
		dev_err(&pdev->dev, "failed to map gpr resource.\n");
		return PTR_RET(gpr_regs);
	}

	dev_info(&pdev->dev, "probed.\n");

	phy->clk = clk_get(&pdev->dev, NULL);
	phy->gpr_regs = gpr_regs;
	phy->phy.dev = &pdev->dev;
	phy->phy.ops.init = golfv_phy_init;
	phy->phy.ops.exit = golfv_phy_exit;
	
	platform_set_drvdata(pdev, phy);
		
	return sdp_sata_phy_register(&phy->phy);
}

static int golfv_ahci_phy_remove(struct platform_device *pdev)
{
	struct golfv_phy *phy = platform_get_drvdata(pdev);
	sdp_sata_phy_unregister(&phy->phy);
	if (phy->clk)
		clk_put(phy->clk);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id golfv_ahci_phy_ids[] = {
	{ .compatible = "samsung,golfv-sata-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, golfv_ahci_phy_ids);
#endif

static struct platform_driver golfv_ahci_phy_driver = {
	.probe		= golfv_ahci_phy_probe,
	.remove		= golfv_ahci_phy_remove,
	.driver		= {
		.name	= "sdp_golfv_sata_phy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(golfv_ahci_phy_ids),
	},
};
module_platform_driver(golfv_ahci_phy_driver);

MODULE_ALIAS("platform: sdp_golfv_ahci_phy");
MODULE_AUTHOR("ij.jang@samsung.com");
MODULE_DESCRIPTION("SDP GOLLF-V SATA phy driver");
MODULE_LICENSE("GPL");

