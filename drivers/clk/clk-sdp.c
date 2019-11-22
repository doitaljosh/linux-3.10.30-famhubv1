/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/mach/map.h>

static DEFINE_SPINLOCK(sdp_clk_lock);
static void __iomem *reg_base;
static phys_addr_t reg_phy_base;
static phys_addr_t reg_phy_end;

static void __iomem *reg_base_ap;
static phys_addr_t reg_phy_base_ap;
static phys_addr_t reg_phy_end_ap;


#define PLL0_PMS	0x000
#define PLL1_PMS	0x004
#define PLL2_PMS	0x008
#define PLL3_PMS	0x00C
#define PLL4_PMS	0x010
#define PLL5_PMS	0x014
#define PLL6_PMS	0x018
#define PLL7_PMS	0x01C
#define PLL8_PMS	0x020

#define PLL2_K		0x028
#define PLL3_K		0x02C
#define PLL4_K		0x030
#define PLL5_K		0x034
#define PLL6_K		0x038
#define PLL7_K		0x03C
#define PLL8_K		0x040

#define MASK_CLK0	0x144
#define MASK_CLK1	0x148
#define MASK_CLK2	0x14C

#define SW_RESET0	0x154
#define SW_RESET1	0x158

#define SDP1106_PLL0_PMS		0x0
#define SDP1106_PLL1_PMS		0x28
#define SDP1106_PLL2_PMS		0x20
#define SDP1106_PLL2_K			0x40


typedef enum
{
	PLL_2XXXX = 0,
	PLL_3XXXX = 1,	
	PLL_2555X = 2,
}PLL_TYPE;

/* clock structure */
struct sdp_fixed_rate {
	const char	*name;
	const char	*parent;
	unsigned long	flags;
	unsigned long	fixed_rate;
};

struct sdp_pll {
	const char	*name;
	const char	*parent;
	unsigned int	off_ams;
	unsigned int	off_k;
	unsigned int	div;
	PLL_TYPE	pll_type;
};

struct sdp_gate {
	const char	*name;
	const char	*parent;
	const char	*dev_name;
	unsigned long	flags;
	unsigned long	offset;
	u8		bit_idx;
	u8		gate_flags;
	u16		reserved;
	const char	*alias;
};

struct sdp_fixed_div {
	const char	*name;
	const char	*parent;
	const char	*dev_name;
	unsigned long	flags;
	unsigned int	mult;
	unsigned int	div;
	const char	*alias;
};

struct sdp_clocks {
	struct sdp_fixed_rate *fixed_rate;
	unsigned int size_of_fixed_rate;
	struct sdp_pll *pll_clk;
	unsigned int size_of_pll_clk;
	struct sdp_fixed_div *fixed_div;
	unsigned int size_of_fixed_div;
	struct sdp_gate *gate_clk;
	unsigned int size_of_gate_clk;
};



static struct sdp_fixed_rate sdp1106_fixed_rate[] __initdata = {
	{ .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

static struct sdp_pll sdp1106_pll[] __initdata = {

	[0] = { .name = "pll0_cpu", .parent = "fin_pll", 
		.off_ams = SDP1106_PLL0_PMS, .pll_type = PLL_3XXXX,
	},	

	[1] = { .name = "pll1_bus", .parent = "fin_pll", 
		.off_ams = SDP1106_PLL1_PMS, .pll_type = PLL_3XXXX,
	},

	[2] = { .name = "pll2_ddr", .parent = "fin_pll", 
		.off_ams = SDP1106_PLL2_PMS, .off_k = SDP1106_PLL2_K,
		.pll_type = PLL_3XXXX,
	},
};


static struct sdp_fixed_div sdp1106_fixed_div[] __initdata = {

	{ .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	
	{ .name = "twd_clk", .parent = "arm_clk",
		.mult = 1, .div = 4, .alias = "twd_clk", },

	{ .name = "ahb_clk", .parent = "pll1_bus",
		.mult = 1, .div = 2, .alias = "ahb_clk", },

	{ .name = "apb_pclk", .parent = "pll1_bus",
		.mult = 1, .div = 2, .alias = "apb_pclk", },
	
	{ .name = "emmc_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 10, .alias = "emmc_clk", },
};

/* TODO: fill the parent clock */
static struct sdp_gate sdp1106_gate[] __initdata = {
	{ .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SW_RESET0, .bit_idx = 23, },
	/* TODO */
};


/* clock data */
static struct sdp_fixed_rate sdp1202_fixed_rate[] __initdata = {
	{ .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

static __initdata struct sdp_pll sdp1202_pll[] = {
	{ .name = "pll0_cpu", .parent = "fin_pll", .off_ams = PLL0_PMS, },
	{ .name = "pll1_ams", .parent = "fin_pll", .off_ams = PLL1_PMS, },
	{ .name = "pll2_gpu", .parent = "fin_pll", .off_ams = PLL2_PMS, },
	{ .name = "pll3_dsp", .parent = "fin_pll", .off_ams = PLL3_PMS, },
	{ .name = "pll4_lvds", .parent = "fin_pll",
		.off_ams = PLL4_PMS, .off_k = PLL4_K, },
	{ .name = "pll5_ddr", .parent = "fin_pll",
		.off_ams = PLL5_PMS, .off_k = PLL5_K, },
};

/* TODO: fill the parent clock */
static struct sdp_gate sdp1202_gate[] __initdata = {
	{ .name = "rstn_uart", .parent = "ahb_clk",
		.alias = "uart", .dev_name = "sdp1202-uart",
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		.offset = SW_RESET0, .bit_idx = 14, },
	{ .name = "rstn_i2c", .parent = "ahb_clk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SW_RESET0, .bit_idx = 13, },
	/* TODO */
};

static struct sdp_fixed_div sdp1202_fixed_div[] __initdata = {
	{ .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	{ .name = "spi_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 2), .alias = "spi_clk", },
	{ .name = "irr_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 10), .alias = "irr_clk", },
	{ .name = "sci_clk", .parent = "pll1_ams",
		.mult = 1, .div = 37, .alias = "sci_clk", },
	{ .name = "emmc_clk", .parent = "pll1_ams",
		.dev_name = "sdp1202-dw-mshc",
		.mult = 1, .div = 10, .alias = "emmc_clk", },
	{ .name = "gpu_clk", .parent = "pll2_gpu",
		.mult = 1, .div = 2, .alias = "gpu_clk", },
	{ .name = "dma_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "dma_clk", },
	{ .name = "ahb_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 6, .alias = "ahb_clk", },
	{ .name = "video_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 2, .alias = "video_clk", },
	{ .name = "gzip_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "gzip_clk", },
	{ .name = "g2d_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 3, .alias = "g2d_clk", },
	{ .name = "plane_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 5, .alias = "plane_clk", },
	{ .name = "lvds_clk", .parent = "pll4_lvds",
		.mult = 1, .div = 1, .alias = "lvds_clk", },
	{ .name = "lvds2_clk", .parent = "pll4_lvds",
		.mult = 2, .div = 1, .alias = "lvds2_clk", },
	{ .name = "ebus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 8, .alias = "ebus_clk", },
	{ .name = "bus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "bus_clk", },
	{ .name = "ddr2_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 2, .alias = "ddr2_clk", },
	{ .name = "ddr4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "ddr4_clk", },
	{ .name = "ddr16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "ddr16_clk", },
	{ .name = "xmif4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "xmif4_clk", },
	{ .name = "xmif16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "xmif16_clk", },
	{ .name = "apb_pclk", .parent = "ahb_clk",
		.mult = 1, .div = 1, .alias = "apb_pclk", },
};

static struct sdp_fixed_div sdp1304_fixed_div[] __initdata = {
	{ .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm_clk", },
	{ .name = "spi_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 2), .alias = "spi_clk", },
	{ .name = "irr_clk", .parent = "pll1_ams",
		.mult = 1, .div = (37 << 10), .alias = "irr_clk", },
	{ .name = "sci_clk", .parent = "pll1_ams",
		.mult = 1, .div = 37, .alias = "sci_clk", },
	{ .name = "emmc_clk", .parent = "pll1_ams",
		.dev_name = "sdp-mmc",
		.mult = 1, .div = 4, .alias = "emmc_clk", },
	{ .name = "gpu_clk", .parent = "pll2_gpu",
		.mult = 1, .div = 2, .alias = "gpu_clk", },
	{ .name = "dma_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "dma_clk", },
	{ .name = "ahb_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 8, .alias = "ahb_clk", },
	{ .name = "video_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 2, .alias = "video_clk", },
	{ .name = "gzip_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "gzip_clk", },
	{ .name = "g2d_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 4, .alias = "g2d_clk", },
	{ .name = "plane_clk", .parent = "pll3_dsp",
		.mult = 1, .div = 5, .alias = "plane_clk", },
	{ .name = "lvds_clk", .parent = "pll4_lvds",
		.mult = 1, .div = 1, .alias = "lvds_clk", },
	{ .name = "lvds2_clk", .parent = "pll4_lvds",
		.mult = 2, .div = 1, .alias = "lvds2_clk", },
	{ .name = "ebus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 8, .alias = "ebus_clk", },
	{ .name = "bus_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "bus_clk", },
	{ .name = "ddr2_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 2, .alias = "ddr2_clk", },
	{ .name = "ddr4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "ddr4_clk", },
	{ .name = "ddr16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "ddr16_clk", },
	{ .name = "xmif4_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 4, .alias = "xmif4_clk", },
	{ .name = "xmif16_clk", .parent = "pll5_ddr",
		.mult = 1, .div = 16, .alias = "xmif16_clk", },
	{ .name = "apb_pclk", .parent = "ahb_clk",
		.mult = 1, .div = 1, .alias = "apb_pclk", },
};

static struct sdp_fixed_rate sdp1305_fixed_rate[] __initdata = {
	{ .name = "mp_fin_pll", .flags = CLK_IS_ROOT, },
};

static __initdata struct sdp_pll sdp1305_pll[] = {
	{ .name = "pll0_mp_lvdsrx", .parent = "mp_fin_pll",
		.off_ams = PLL0_PMS, },
	{ .name = "pll1_mp_bus", .parent = "mp_fin_pll",
		.off_ams = PLL1_PMS, },
	{ .name = "pll2_mp_core", .parent = "mp_fin_pll",
		.off_ams = PLL2_PMS, .off_k = PLL2_K, },
	{ .name = "pll7_mp_pull", .parent = "mp_fin_pll",
		.off_ams = PLL7_PMS, .off_k = PLL7_K, },
	{ .name = "pll3_mp_aud0", .parent = "pll7_mp_pull",
		.off_ams = PLL3_PMS, .off_k = PLL3_K, },
	{ .name = "pll4_mp_aud1", .parent = "pll7_mp_pull",
		.off_ams = PLL4_PMS, .off_k = PLL4_K, },
	{ .name = "pll5_mp_vid", .parent = "pll7_mp_pull",
		.off_ams = PLL5_PMS, .off_k = PLL5_K, },
	{ .name = "pll6_mp_lvds", .parent = "pll5_mp_vid",
		.off_ams = PLL6_PMS, .off_k = PLL6_K, },
	{ .name = "pll8_mp_ddr", .parent = "mp_fin_pll",
		.off_ams = PLL8_PMS, .off_k = PLL8_K, },
};

static struct sdp_fixed_div sdp1305_fixed_div[] __initdata = {
	{ .name = "dpscl_mp_lvdsx2", .parent = "pll0_mp_lvdsrx",
		.mult = 1, .div = 1, .alias = "dpscl_mp_lvdsx2", },
	{ .name = "vdsp0_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = 5, .alias = "vdsp0_mp_clk", },
	{ .name = "ga2d_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = 5, .alias = "ga2d_mp_clk", },
	{ .name = "tsd_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = 8, .alias = "tsd_mp_clk", },
	{ .name = "bus_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = (6<<1), .alias = "bus_mp_clk", },
	{ .name = "bushalf_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = ((6<<1)<<1), .alias = "bushalf_mp_clk", },
	{ .name = "intc_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = 6, .alias = "intc_mp_clk", },
	{ .name = "mipi_mp_clk", .parent = "pll1_mp_bus",
		.mult = 1, .div = (10<<1), .alias = "mipi_mp_clk", },
	{ .name = "tdsp_mp_clk", .parent = "pll2_mp_core",
		.mult = 1, .div = 2, .alias = "tdsp_mp_clk", },
	{ .name = "sebus_mp_clk", .parent = "pll2_mp_core",
		.mult = 1, .div = 4, .alias = "sebus_mp_clk", },
	{ .name = "dp_scl_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "dp_scl_mp_clk", },
	{ .name = "dpnrfc_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "dpnrfc_mp_clk", },
	{ .name = "avd_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "avd_mp_clk", },
	{ .name = "mfd_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "mfd_mp_clk", },
	{ .name = "henc_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "henc_mp_clk", },
	{ .name = "jpeg_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "jpeg_mp_clk", },
	{ .name = "ve_mp_clk", .parent = "pll5_mp_vid",
		.mult = 1, .div = 1, .alias = "ve_mp_clk", },
	/* TODO: impelent clock tree for the AUDIO group */
	/* TODO: impelent clock tree for the LVDS group */
	/* TODO: impelent clock tree for the 148.5MHz group (PULL PLL) */
	/* TODO: impelent clock tree for the DDR group */
};

static struct sdp_gate sdp1305_gate[] __initdata = {
	/* TODO: add description from bit 0 to bit 15 */
	{ .name = "disp_pclk_nr", .parent = "dpnrfc_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK0, .bit_idx = 16, },
	{ .name = "disp_pclk_dcar", .parent = "dpnrfc_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK0, .bit_idx = 17, },
	{ .name = "disp_pclk_ipc", .parent = "dpnrfc_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK0, .bit_idx = 19, },
	{ .name = "disp_pclk_ucar", .parent = "dpnrfc_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK0, .bit_idx = 20, },
	{ .name = "dp_scl_osc_clk", .parent = "dp_scl_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK2, .bit_idx = 2, },
	{ .name = "dp_scl_sub_clk", .parent = "dp_scl_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK2, .bit_idx = 3, },
	{ .name = "dp_scl_pclk", .parent = "dp_scl_mp_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = MASK_CLK2, .bit_idx = 4, },
/*	{ .name = "dmx_msp_clk", .parent = "tsd_mp_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = MASK_CLK0, .bit_idx = 1, },
	{ .name = "dmx_sw_reset_clk", .parent = "tsd_mp_clk",
		.flags  = CLK_SET_PARENT_GATE,
		.offset = SW_RESET0, .bit_idx = 22,},
*/
	/* TODO: add description from bit 27 to bit 31,
	and for other registers also  */
};

/* Defines and declaration for the clocks belonging to SDP1404 Soc */
#define SDP1404_CPU0_PMS		0x000
#define SDP1404_CPU1_PMS		0x004
#define SDP1404_DDR0_PMS		0x008
#define SDP1404_GPU_PMS			0x00C
#define SDP1404_PULL0_PMS		0x010
#define SDP1404_PULL1_PMS		0x014
#define SDP1404_AUD0_PMS		0x018
#define SDP1404_AUD1_PMS		0x01c
#define SDP1404_VID_PMS			0x020
#define SDP1404_VX1_PMS			0x024
#define SDP1404_CORE_PMS		0x028
#define SDP1404_LVDS_PMS		0x02C
#define SDP1404_AUD2_PMS		0x030
#define SDP1404_DDR1_PMS		0x15C
#define SDP1404_EMMC_PMS		0x170
#define SDP1404_DPNRFC_PMS		0x174
#define SDP1404_DPSCL_PMS		0x178
#define SDP1404_DEC_PMS			0x17C

#define SDP1404_PULL0_K			0x034
#define SDP1404_PULL1_K			0x036
#define SDP1404_AUD0_K			0x038
#define SDP1404_AUD1_K			0x03A
#define SDP1404_VID_K			0x03C
#define SDP1404_DDR0_K			0x03E
#define SDP1404_CORE_K			0x040
#define SDP1404_AUD2_K			0x042
#define SDP1404_DDR1_K			0x160
#define SDP1404_VX1_K			0x162
#define SDP1404_DPNRFC_K		0x180
#define SDP1404_EMMC_K			0x182
#define SDP1404_DEC_K			0x184
#define SDP1404_DPSCL_K			0x186

#define SDP1404_MASK_CH			0x140
#define SDP1404_MASK_CLK0		0x114
#define SDP1404_MASK_CLK1		0x118
#define SDP1404_MASK_CLK2		0x11C
#define SDP1404_MASK_CLK3		0x120
#define SDP1404_SW_RESET0		0x0B0
#define SDP1404_SW_RESET1		0x0B4
#define SDP1404_SW_RESET2		0x0B8
#define SDP1404_SW_RESET3		0x0BC
#define SDP1404_SW_RESET4		0x0C0

#define SDP1404_RESERVED_HDMI		0x498


static struct sdp_fixed_rate sdp1404_fixed_rate[] __initdata = {
	{ .name = "fin_pll", .flags = CLK_IS_ROOT, },
	{ .name = "fin_lvds", .flags = CLK_IS_ROOT, },
};

static __initdata struct sdp_pll sdp1404_pll[] = {
	{ .name = "pll0_cpu0", .parent = "fin_pll",
		.off_ams = SDP1404_CPU0_PMS, },
	{ .name = "pll1_cpu1", .parent = "fin_pll",
		.off_ams = SDP1404_CPU1_PMS, },
	{ .name = "pll2_ddr0", .parent = "fin_pll",
		.off_ams = SDP1404_DDR0_PMS, .off_k = SDP1404_DDR0_K, },
	{ .name = "pll3_ddr1", .parent = "fin_pll",
		.off_ams = SDP1404_DDR1_PMS, .off_k = SDP1404_DDR1_K, },
	{ .name = "pll4_gpu", .parent = "fin_pll",
		.off_ams = SDP1404_GPU_PMS, },
	{ .name = "pll5_pull0", .parent = "fin_pll",
		.off_ams = SDP1404_PULL0_PMS, .off_k = SDP1404_PULL0_K, },
	{ .name = "pll6_pull1", .parent = "fin_pll",
		.off_ams = SDP1404_PULL1_PMS, .off_k = SDP1404_PULL1_K, },
	{ .name = "pll7_aud0", .parent = "pll5_pull0",
		.off_ams = SDP1404_AUD0_PMS, .off_k = SDP1404_AUD0_K, .div = 4, },
	{ .name = "pll8_aud1", .parent = "pll5_pull0",
		.off_ams = SDP1404_AUD1_PMS, .off_k = SDP1404_AUD1_K, .div = 4, },
	{ .name = "pll9_aud2", .parent = "pll5_pull0",
		.off_ams = SDP1404_AUD2_PMS, .off_k = SDP1404_AUD2_K, .div = 4, },
	{ .name = "pll10_vid", .parent = "pll5_pull0",
		.off_ams = SDP1404_VID_PMS, .off_k = SDP1404_VID_K, .div = 4, },
	{ .name = "pll11_vx1", .parent = "fin_pll",
		.off_ams = SDP1404_VX1_PMS, .off_k = SDP1404_VX1_K, },
	{ .name = "pll12_core", .parent = "fin_pll",
		.off_ams = SDP1404_CORE_PMS, .off_k = SDP1404_CORE_K, },
	{ .name = "pll13_lvds", .parent = "fin_lvds",
		.off_ams = SDP1404_LVDS_PMS, },
	{ .name = "pll14_emmc", .parent = "fin_pll",
		.off_ams = SDP1404_EMMC_PMS, .off_k = SDP1404_EMMC_K, },
	{ .name = "pll15_dpnrfc", .parent = "fin_pll",
		.off_ams = SDP1404_DPNRFC_PMS, .off_k = SDP1404_DPNRFC_K, },
	{ .name = "pll16_dpscl", .parent = "fin_pll",
		.off_ams = SDP1404_DPSCL_PMS, .off_k = SDP1404_DPSCL_K, },
	{ .name = "pll17_dec", .parent = "fin_pll",
		.off_ams = SDP1404_DEC_PMS, .off_k = SDP1404_DEC_K, },
};

#if 0
/* alias = con_id, dev_name = dev_id */
static struct sdp_fdiv_clock sdp1404_fdiv_clks[] __initdata = {
	{ .id = sdp1404_clk_apb_pclk, .name = "apb_pclk", .parent = "fin_pll",
		.mult = 1, .div = 32, .alias = "apb_pclk", },
};
#endif 

static struct sdp_fixed_div sdp1404_fixed_div[] __initdata = {
	{ .name = "arm0_clk", .parent = "pll0_cpu0",
		.mult = 1, .div = 1, .alias = "arm0_clk", },
	{ .name = "arm1_clk", .parent = "pll1_cpu1",
		.mult = 1, .div = 1, .alias = "arm1_clk", },
	{ .name = "ddr0_clk", .parent = "pll2_ddr0",
		.mult = 1, .div = 2, .alias = "ddr0_clk", },
	{ .name = "ddr1_clk", .parent = "pll3_ddr1",
		.mult = 1, .div = 2, .alias = "ddr1_clk", },
	{ .name = "cci_clk", .parent = "pll3_ddr1",
		.mult = 1, .div = 4, .alias = "cci_clk", },
	{ .name = "gpu_clk", .parent = "pll4_gpu",
		.mult = 1, .div = 2, .alias = "gpu_clk", },
	{ .name = "dac_clk", .parent = "pll5_pull0",
		.mult = 1, .div = 4, .alias = "dac_clk", },
	{ .name = "27mhz_clk", .parent = "pll6_pull1",
		.mult = 1, .div = 2, .alias = "27mhz_clk", },
	{ .name = "ahb_clk", .parent = "pll12_core",
		.mult = 1, .div = 6, .alias = "ahb_clk", },
	{ .name = "pcie_sata_phy_clk", .parent = "pll12_core",
		.mult = 1, .div = 10, .alias = "pcie_sata_phy_clk", },
	{ .name = "freq_check_clk", .parent = "pll12_core",
		.mult = 1, .div = 4, .alias = "freq_check_clk", },
	{ .name = "jpeg_clk", .parent = "pll12_core",
		.mult = 1, .div = 6, .alias = "jpeg_clk", },
	{ .name = "aio_f_clk", .parent = "pll12_core",
		.mult = 1, .div = 6, .alias = "aio_f_clk", },
	{ .name = "avdx2_clk", .parent = "pll12_core",
		.mult = 1, .div = 4, .alias = "avdx2_clk", },
	{ .name = "sata_f_clk", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "sata_f_clk", },
	{ .name = "henc_clk", .parent = "pll12_core",
		.mult = 1, .div = 2, .alias = "henc_clk", },
	{ .name = "sdmmc_cclk", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "sdmmc_cclk", },
	{ .name = "cap_core_clk", .parent = "pll12_core",
		.mult = 1, .div = 6, .alias = "cap_core_clk", },
	{ .name = "hevc_clk", .parent = "pll12_core",
		.mult = 1, .div = 4, .alias = "hevc_clk", },
	{ .name = "vdsp_f_clk", .parent = "pll12_core",
		.mult = 1, .div = 4, .alias = "vdsp_f_clk", },
	{ .name = "usb2a_clkcore", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "usb2a_clkcore", },
	{ .name = "usb2b_clkcore", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "usb2b_clkcore", },
	{ .name = "usb3a_clkcore", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "usb3a_clkcore", },
	{ .name = "usb3b_clkcore", .parent = "pll12_core",
		.mult = 1, .div = 20, .alias = "usb3b_clkcore", },
	{ .name = "dma_f_clk", .parent = "pll12_core",
		.mult = 1, .div = 8, .alias = "dma_f_clk", },
	{ .name = "emmc_clk", .parent = "pll14_emmc",
		.mult = 1, .div = 2, .alias = "emmc_clk", },
	{ .name = "dp_nrfc_clk", .parent = "pll15_dpnrfc",
		.mult = 1, .div = 1, .alias = "dp_nrfc_clk", },
	{ .name = "dp_scl_clk", .parent = "pll16_dpscl",
		.mult = 1, .div = 1, .alias = "dp_scl_clk", },
	{ .name = "apb_pclk", .parent = "pll16_dpscl",
		.mult = 1, .div = 2, .alias = "apb_pclk", },
	{ .name = "ga2d_f_clk", .parent = "pll16_dpscl",
		.mult = 1, .div = 1, .alias = "ga2d_f_clk", },
	{ .name = "mfd_f_clk", .parent = "pll16_dpscl",
		.mult = 1, .div = 1, .alias = "mfd_f_clk", },
	{ .name = "uddec_f_clk", .parent = "pll17_dec",
		.mult = 1, .div = 1, .alias = "uddec_f_clk", },
	{ .name = "emac_clk", .parent = "pll16_dpscl",
		.mult = 1, .div = 2, .alias = "emac_clk", },

};

static struct sdp_gate sdp1404_gate[] __initdata = {
	{ .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET0, .bit_idx = 18, },
	{ .name = "sata_clk", .parent = "sata_f_clk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 30, },
	{ .name = "sata_rst_clk", .parent = "pll12_core",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 29, },
	{ .name = "usb3a", .parent = "pll12_core",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 15, },
	{ .name = "usb3a_phy_usb2", .parent = "pll12_core",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 13, },
	{ .name = "usb3a_phy", .parent = "pll12_core",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 11, },
	{ .name = "pcie_clk", .parent = "pcie_sata_phy_clk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 30, },
	{ .name = "pcie_gpr_clk", .parent = "pcie_sata_phy_clk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1404_SW_RESET1, .bit_idx = 31, },
	{ .name = "avd_clk", .parent = "avdx2_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 31, },
	{ .name = "aio_clk", .parent = "aio_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 29, },
	{ .name = "vdsp_clk", .parent = "vdsp_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 22, },
	{ .name = "mfd_pclk", .parent = "mfd_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 20, },
	{ .name = "dp_scl_pclk", .parent = "dp_scl_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 18, },
	{ .name = "dp_scl_sub_clk", .parent = "dp_scl_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 17, },
	{ .name = "disp_pclk_nr", .parent = "dp_nrfc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 15, },
	{ .name = "disp_pclk_dcar", .parent = "dp_nrfc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 14, },
	{ .name = "disp_pclk_ipc", .parent = "dp_nrfc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 13, },
	{ .name = "disp_pclk_ucar", .parent = "dp_nrfc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 12, },
	{ .name = "msp_bclk", .parent = "freq_check_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 9, },	
	{ .name = "mmc_clk", .parent = "emmc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 7, },
	{ .name = "sdmmc_clk", .parent = "sdmmc_cclk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 6, },
	{ .name = "hevc_hclk", .parent = "hevc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 4, },
	{ .name = "dma_clk", .parent = "dma_f_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 3, },
	{ .name = "usb2a_clk", .parent = "usb2a_clkcore",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 2, },
	{ .name = "usb2b_clk", .parent = "usb2b_clkcore",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 1, },
	{ .name = "usb3a_clk", .parent = "usb3a_clkcore",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 0, },
	{ .name = "usb3b_clk", .parent = "usb3b_clkcore",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 31, },
	{ .name = "pcie_mask", .parent = "pcie_sata_phy_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 29, },
	{ .name = "ga2d_clk", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 27, },
	{ .name = "cap_clk", .parent = "cap_core_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 26, },
	{ .name = "hen0_clk", .parent = "henc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 25, },
	{ .name = "hen1_clk", .parent = "henc_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 24, },
	{ .name = "osdp0_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 23, },
	{ .name = "gp0_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 22, },
	{ .name = "sgp0_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 21, },
	{ .name = "osdp0_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 20, },
	{ .name = "gp0_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 19, },
	{ .name = "sgp0_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 18, },
	{ .name = "osdp1_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 17, },
	{ .name = "gp1_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 16, },
	{ .name = "sgp1_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 15, },
	{ .name = "osdp1_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 14, },
	{ .name = "gp1_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 13, },
	{ .name = "sgp1_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 12, },
	{ .name = "uddec_clk", .parent = "uddec_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 18, },
	{ .name = "gfx0_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 17, },
	{ .name = "gfx0_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 16, },
	{ .name = "gfx1_clk_c", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 15, },
	{ .name = "gfx1_clk_o", .parent = "ga2d_f_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 14, },
	{ .name = "jpeg1_clk", .parent = "jpeg_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 8, },
	{ .name = "jpeg0_clk", .parent = "jpeg_clk",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK3, .bit_idx = 7, },
	{ .name = "gzip_rst", .parent = "ahb_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1404_SW_RESET0, .bit_idx = 14, },
	{ .name = "gzip_clk", .parent = "ahb_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1404_MASK_CLK2, .bit_idx = 28, },
	{ .name = "tsd_clk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_MASK_CLK1, .bit_idx = 9, },
	{ .name = "hdmi_pclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_RESERVED_HDMI, .bit_idx = 0, },
	{ .name = "hdmi_sysclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_RESERVED_HDMI, .bit_idx = 1, },
	{ .name = "hdmi_modclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1404_RESERVED_HDMI, .bit_idx = 2, },
};

/* Defines and declaration for the clocks belonging to SDP1406 Soc */
#define SDP1406_CPU_PMS			0x000
#define SDP1406_DDR0_PMS		0x004
#define SDP1406_VID_PMS			0x008
#define SDP1406_PULL0_PMS		0x00C
#define SDP1406_PULL1_PMS		0x010
#define SDP1406_AUD0_PMS		0x014
#define SDP1406_AUD1_PMS		0x018
#define SDP1406_VX1_PMS			0x01C
#define SDP1406_USB_PMS			0x020
#define SDP1406_EMAC_PMS		0x024
#define SDP1406_DDR1_PMS		0x0D4
#define SDP1406_CORE_PMS		0x12C

#define SDP1406_PULL0_K			0x028
#define SDP1406_PULL1_K			0x02A
#define SDP1406_AUD0_K			0x02C
#define SDP1406_AUD1_K			0x02E
#define SDP1406_VX1_K			0x030
#define SDP1406_DDR0_K			0x032
#define SDP1406_EMAC_K			0x034
#define SDP1406_USB_K			0x036
#define SDP1406_VID_K			0x044
#define SDP1406_DDR1_K			0x0D8
#define SDP1406_CORE_K			0x0DA

#define SDP1406_MASK_CTRL0		0x124
#define SDP1406_MASK_CTRL1		0x128
#define SDP1406_MASK_CTRL2		0x12C

#define SDP1406_SW_RESET0		0x0E0
#define SDP1406_SW_RESET1		0x0E4
#define SDP1406_SW_RESET2		0x0E8
#define SDP1406_SW_RESET3		0x0EC

#define SDP1406_RESERVED_HDMI		0x128
static struct sdp_fixed_rate sdp1406_fixed_rate[] __initdata = {
	{ .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

static __initdata struct sdp_pll sdp1406_pll[] = {
	{ .name = "pll1_cpu", .parent = "fin_pll",
		.off_ams = SDP1406_CPU_PMS, },
	{ .name = "pll2_ddr0", .parent = "fin_pll",
		.off_ams = SDP1406_DDR0_PMS, .off_k = SDP1406_DDR0_K, },
	{ .name = "pll3_ddr1", .parent = "fin_pll",
		.off_ams = SDP1406_DDR1_PMS, .off_k = SDP1406_DDR1_K, },
	{ .name = "pll4_pull0", .parent = "fin_pll",
		.off_ams = SDP1406_PULL0_PMS, .off_k = SDP1406_PULL0_K, },
	{ .name = "pll5_pull1", .parent = "fin_pll",
		.off_ams = SDP1406_PULL1_PMS, .off_k = SDP1406_PULL1_K, },
	{ .name = "pll6_aud0", .parent = "pll4_pull0",
		.off_ams = SDP1406_AUD0_PMS, .off_k = SDP1406_AUD0_K, .div = 68, },
	{ .name = "pll7_aud1", .parent = "pll4_pull0",
		.off_ams = SDP1406_AUD1_PMS, .off_k = SDP1406_AUD1_K, .div = 68, },
	{ .name = "pll8_vid", .parent = "pll4_pull0",
		.off_ams = SDP1406_VID_PMS, .off_k = SDP1406_VID_K, .div = 68, },
	{ .name = "pll9_vx1", .parent = "fin_pll",
		.off_ams = SDP1406_VX1_PMS, .off_k = SDP1406_VX1_K, },
	{ .name = "pll10_usb", .parent = "fin_pll",
		.off_ams = SDP1406_USB_PMS, .off_k = SDP1406_USB_K, },
	{ .name = "pll11_emac", .parent = "fin_pll",
		.off_ams = SDP1406_EMAC_PMS, .off_k = SDP1406_EMAC_K, },
	{ .name = "pll12_core", .parent = "fin_pll",
		.off_ams = SDP1406_CORE_PMS, .off_k = SDP1406_CORE_K, },
};

static struct sdp_fixed_div sdp1406_fixed_div[] __initdata = {
	{ .name = "arm_clk", .parent = "pll1_cpu",
		.mult = 1, .div = 1, .alias = "arm0_clk", },
	{ .name = "ddr0_clk", .parent = "pll2_ddr0",
		.mult = 1, .div = 2, .alias = "ddr0_clk", },
	{ .name = "ddr1_clk", .parent = "pll3_ddr1",
		.mult = 1, .div = 2, .alias = "ddr1_clk", },
	{ .name = "apb_pclk", .parent = "pll12_core",
		.mult = 1, .div = 8, .alias = "apb_pclk", },
	{ .name = "dma_clk", .parent = "pll12_core",
		.mult = 1, .div = 8, .alias = "dma_clk", },
	{ .name = "emac_clk", .parent = "pll11_emac",
		.mult = 1, .div = 1, .alias = "emac_clk", },
	{ .name = "usb2_clk", .parent = "pll10_usb",
		.mult = 1, .div = 2, .alias = "usb2_clk", },
	{ .name = "gpu_clk", .parent = "pll12_core",
		.mult = 1, .div = 4, .alias = "gpu_clk", },
	{ .name = "ahb_clk", .parent = "pll12_core",
		.mult = 1, .div = 8, .alias = "ahb_clk", },
	{ .name = "emmc_clk", .parent = "pll5_pull1",
		.mult = 1, .div = 1, .alias = "emmc_clk", },
	{ .name = "emac_clk", .parent = "pll12_core",
		.mult = 1, .div = 8, .alias = "emac_clk", },
};

static struct sdp_gate sdp1406_gate[] __initdata = {
/*
	{ .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1406_SW_RESET0, .bit_idx =19, },
*/
	{ .name = "gzip_rst", .parent = "ahb_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1406_SW_RESET0, .bit_idx = 15, },
	{ .name = "gzip_clk", .parent = "ahb_clk",
		.flags = CLK_SET_PARENT_GATE,
		.offset = SDP1406_MASK_CTRL0, .bit_idx = 3, },

	{ .name = "hen0_clk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL0, .bit_idx = 1, },
	{ .name = "jpeg0_clk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL0, .bit_idx = 21, },
	{ .name = "jpeg1_clk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL0, .bit_idx = 22, },
	{ .name = "mfd_pclk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL1, .bit_idx = 13, },
	{ .name = "hevc_hclk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL1, .bit_idx = 18, },
	{ .name = "uddec_clk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL1, .bit_idx = 19, },
	{ .name = "tfc_clk", .parent = "pll12_core",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_MASK_CTRL1, .bit_idx = 26, },
	{ .name = "hdmi_pclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_RESERVED_HDMI, .bit_idx = 12, },
	{ .name = "hdmi_sysclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_RESERVED_HDMI, .bit_idx = 11, },
	{ .name = "hdmi_modclk", .parent = "fin_pll",
		.flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
		.offset = SDP1406_RESERVED_HDMI, .bit_idx = 10, },
        { .name = "osdp_clk_c", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 9, },
        { .name = "osdp_clk_vo", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 8, },
        { .name = "gp_clk_c", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 16, },
        { .name = "gp_clk_vo", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 17, },
        { .name = "sgp_clk_c", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 7, },
        { .name = "sgp_clk_vo", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 6, },
        { .name = "gfxp_clk_c", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL0, .bit_idx = 0, },
        { .name = "gfxp_clk_vo", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL1, .bit_idx = 5, },
        { .name = "ga2d_clk", .parent = "ga2d_f_clk",
                .flags = CLK_SET_PARENT_GATE | CLK_IGNORE_UNUSED,
                .offset = SDP1406_MASK_CTRL0, .bit_idx = 2, },
};

/* Defines and declaration for the clocks belonging to SDP1406 Soc */
#define SDP1412_SW_RESET0		0x0
#define SDP1412_SW_RESET1		0x4
#define SDP1412_SW_RESET2		0x8

#define SDP1412_CPU0_PMS		0x10

#define SDP1412_DDR0_PMS		0x1C
#define SDP1412_DDR0_K			0x20

#define SDP1412_AUD0_PMS		0x2C
#define SDP1412_AUD0_K			0x30

#define SDP1412_EPHY0_PMS		0x3C


static struct sdp_fixed_rate sdp1412_fixed_rate[] __initdata = {
	{ .name = "fin_pll", .flags = CLK_IS_ROOT, },
};

static __initdata struct sdp_pll sdp1412_pll[] = {
	{ .name = "pll0_cpu", .parent = "fin_pll",
		.off_ams = SDP1412_CPU0_PMS, .pll_type = PLL_2555X,},				

	{ .name = "pll1_ddr", .parent = "fin_pll",
		.off_ams = SDP1412_DDR0_PMS, .off_k = SDP1412_DDR0_K,
		.pll_type = PLL_2555X,},
		
	{ .name = "pll2_aud", .parent = "fin_pll",
		.off_ams = SDP1412_AUD0_PMS, .off_k = SDP1412_AUD0_K,
		.pll_type = PLL_2555X,},
		
	{ .name = "pll3_ephy", .parent = "fin_pll",
		.off_ams = SDP1412_EPHY0_PMS, .pll_type = PLL_2555X,},
};


static struct sdp_fixed_div sdp1412_fixed_div[] __initdata = {
	{ .name = "arm_clk", .parent = "pll0_cpu",
		.mult = 1, .div = 1, .alias = "arm0_clk", },

	{ .name = "ddr0_clk", .parent = "pll1_ddr",
		.mult = 1, .div = 2, .alias = "ddr0_clk", },

	{ .name = "apb_pclk", .parent = "pll3_ephy",
		.mult = 1, .div = 12, .alias = "apb_pclk", },

	{ .name = "ahb_clk", .parent = "pll3_ephy",
		.mult = 1, .div = 12, .alias = "ahb_clk", },

	{ .name = "emmc_clk", .parent = "pll3_ephy",
		.mult = 1, .div = 4, .alias = "emmc_clk", },

};

static struct sdp_gate sdp1412_gate[] __initdata = {
	{ .name = "rstn_i2c", .parent = "apb_pclk",
		.flags = CLK_SET_RATE_PARENT,
		.offset = SDP1412_SW_RESET0, .bit_idx = 27, },
};

static unsigned long _get_rate(const char *clk_name)
{
	struct clk *clk;
	unsigned long rate;

	clk = clk_get(NULL, clk_name);
	if (IS_ERR(clk)) {
		pr_err("%s: could not find clock %s\n", __func__, clk_name);
		return 0;
	}
	rate = clk_get_rate(clk);
	clk_put(clk);
	return rate;
}

static void __init sdp_clk_add_provider(struct device_node *root,
		const char *name, struct clk *clk)
{
	struct device_node *np;

	if (!name || !clk)
		return;

	for_each_child_of_node(root, np) {
		if (!of_find_property(np, "sdp,clock", NULL))
			continue;
		if (!strcmp(name, np->name))
			of_clk_add_provider(np, of_clk_src_simple_get, clk);
	}
}

static void __init sdp_clk_register_fixed_rate(struct device_node *np,
		struct sdp_fixed_rate *clks, unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int i;
	int r;

	for (i = 0; i < nr_clk; i++, clks++) {
		clk = clk_register_fixed_rate(NULL, clks->name, clks->parent,
				clks->flags, clks->fixed_rate);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		sdp_clk_add_provider(np, clks->name, clk);

		r = clk_register_clkdev(clk, clks->name, NULL);
		if (r)
			pr_err("%s: failed to register clock lookup for %s\n",
					__func__, clks->name);
	}
}

static void __init sdp_clk_of_register_fixed_ext(struct device_node *root,
		struct sdp_fixed_rate *clk, unsigned int nr_clk,
		struct of_device_id *clk_matches)
{
	const struct of_device_id *match;
	struct device_node *np;
	u32 freq;

	for_each_matching_node_and_match(np, clk_matches, &match) {
		if (of_property_read_u32(np, "clock-frequency", &freq))
			continue;
		clk[(u32)match->data].fixed_rate = freq;
	}

	sdp_clk_register_fixed_rate(root, clk, nr_clk);
}

static inline unsigned long sdp_pll2xxxx_calc_f_out(u64 f_in,
		int p, int m, int s, s16 k)
{
	f_in = (f_in * (u64) m) + (u64) (((long long int)f_in * (int) k) >> 16);
	do_div(f_in, ((u32) p * (1UL << s)));

	return (unsigned long)f_in;
}

/* pll2553X/pll2650X */
struct sdp_clk_pll2xxxx {
	struct clk_hw hw;
	const void __iomem *reg_pms;
	const void __iomem *reg_k;
	unsigned int div;
};

#define to_clk_pll2xxxx(_hw) container_of(_hw, struct sdp_clk_pll2xxxx, hw)

#define PLL2XXXX_MDIV_MASK  (0x3FF)
#define PLL2XXXX_PDIV_MASK  (0x3F)
#define PLL2XXXX_SDIV_MASK  (0x7)
#define PLL2XXXX_KDIV_MASK  (0xFFFF)
#define PLL2XXXX_MDIV_SHIFT (8)
#define PLL2XXXX_PDIV_SHIFT (20)
#define PLL2XXXX_SDIV_SHIFT (0)

static unsigned long sdp_pll2xxxx_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct sdp_clk_pll2xxxx *pll = to_clk_pll2xxxx(hw);
	u32 pll_pms;
	u32 val;
	s16 pll_k;
	u32 mdiv;
	u32 pdiv;
	u32 sdiv;

	pll_pms = __raw_readl(pll->reg_pms);
	mdiv = (pll_pms >> PLL2XXXX_MDIV_SHIFT) & PLL2XXXX_MDIV_MASK;
	pdiv = (pll_pms >> PLL2XXXX_PDIV_SHIFT) & PLL2XXXX_PDIV_MASK;
	sdiv = (pll_pms >> PLL2XXXX_SDIV_SHIFT) & PLL2XXXX_SDIV_MASK;

	if (pll->reg_k)
	{
		/* Shifting done to handle the case of two 16 bit K values packed in one
		    32 bit register */
		val = __raw_readl((void *)(((u32)pll->reg_k) & 0xFFFFFFFC));
		if(((u32) pll->reg_k) & 0x3)
			pll_k = (s16) (val >> 16);
		else
			pll_k = (s16) (val & PLL2XXXX_KDIV_MASK);
	}
	else
		pll_k = 0;
	parent_rate /= pll->div;

	return sdp_pll2xxxx_calc_f_out(parent_rate,
			(int) pdiv, (int) mdiv, (int) sdiv, pll_k);
}

static long sdp_pll2xxxx_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, *prate);

	/* TODO: use pms table */

	return (long) sdp_pll2xxxx_recalc_rate(hw, *prate);
}

static int sdp_pll2xxxx_set_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, prate);

	/* TODO */

	return 0;
}

static const struct clk_ops sdp_pll2xxxx_clk_ops = {
	.recalc_rate = sdp_pll2xxxx_recalc_rate,
	.round_rate = sdp_pll2xxxx_round_rate,
	.set_rate = sdp_pll2xxxx_set_rate,
};

#define PLL3XXXX_MDIV_MASK  (0x3FF)
#define PLL3XXXX_PDIV_MASK  (0x3F)
#define PLL3XXXX_SDIV_MASK  (0x7)
#define PLL3XXXX_KDIV_MASK  (0x8000)
#define PLL3XXXX_MDIV_SHIFT (8)
#define PLL3XXXX_PDIV_SHIFT (20)
#define PLL3XXXX_SDIV_SHIFT (16)

static unsigned long sdp_pll3xxxx_recalc_rate(struct clk_hw *hw, 
		unsigned long parent_rate)
{
	struct sdp_clk_pll2xxxx *pll = to_clk_pll2xxxx(hw);
	uint64_t freq = 0;
	unsigned int pms;
	int k;
	int sign = 1;
	
	pms = __raw_readl(pll->reg_pms);
	freq = (parent_rate >> (pms&PLL3XXXX_SDIV_MASK)) 
		/ ((pms>>PLL3XXXX_PDIV_SHIFT)&PLL3XXXX_PDIV_MASK);
				
	if (pll->reg_k)	{		
		k = (int) __raw_readl((void *)(((u32)pll->reg_k)));
		if (k & PLL3XXXX_KDIV_MASK)
		{
			k = 0x10000 - k;
			sign = -1;
		}
				
		freq = (freq * ((pms>>PLL3XXXX_MDIV_SHIFT)&PLL3XXXX_MDIV_MASK)) 
			+ (((freq * (u64) k) >> PLL3XXXX_SDIV_SHIFT) * (u64) sign);
	}
	else {
		freq *= ((pms>>PLL3XXXX_MDIV_SHIFT)&PLL3XXXX_MDIV_MASK);		
	}

	return (unsigned long)freq;
}

static long sdp_pll3xxxx_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, *prate);

	/* TODO: use pms table */

	return (long) sdp_pll3xxxx_recalc_rate(hw, *prate);
}

static int sdp_pll3xxxx_set_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, prate);

	/* TODO: remove 'TODO'  */

	return 0;
}


static const struct clk_ops sdp_pll3xxxx_clk_ops = {
	.recalc_rate = sdp_pll3xxxx_recalc_rate,
	.round_rate = sdp_pll3xxxx_round_rate,
	.set_rate = sdp_pll3xxxx_set_rate,
};

#define PLL2555X_MDIV_MASK  (0x3FF)
#define PLL2555X_PDIV_MASK  (0x3F)
#define PLL2555X_SDIV_MASK  (0x7)
#define PLL2555X_KDIV_MASK  (0xFFFF)
#define PLL2555X_MDIV_SHIFT (14)
#define PLL2555X_PDIV_SHIFT (24)
#define PLL2555X_SDIV_SHIFT (11)


static unsigned long sdp_pll2555x_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct sdp_clk_pll2xxxx *pll = to_clk_pll2xxxx(hw);
	u32 pll_pms;
	u32 val;
	s16 pll_k;
	u32 mdiv;
	u32 pdiv;
	u32 sdiv;

	pll_pms = __raw_readl(pll->reg_pms);
	mdiv = (pll_pms >> PLL2555X_MDIV_SHIFT) & PLL2555X_MDIV_MASK;
	pdiv = (pll_pms >> PLL2555X_PDIV_SHIFT) & PLL2555X_PDIV_MASK;
	sdiv = (pll_pms >> PLL2555X_SDIV_SHIFT) & PLL2555X_SDIV_MASK;

	if (pll->reg_k)
	{
		/* Shifting done to handle the case of two 16 bit K values packed in one
		    32 bit register */
		val = __raw_readl((void *)(((u32)pll->reg_k) & 0xFFFFFFFC));
		if(((u32) pll->reg_k) & 0x3)
			pll_k = val >> 16;
		else
			pll_k = val & PLL2555X_KDIV_MASK;
	}
	else
		pll_k = 0;
	parent_rate /= pll->div;
	
	return sdp_pll2xxxx_calc_f_out(parent_rate,
			(int) pdiv, (int) mdiv, (int) sdiv, pll_k);
}

static long sdp_pll2555x_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, *prate);

	/* TODO: use pms table */

	return (long)sdp_pll2555x_recalc_rate(hw, *prate);
}

static int sdp_pll2555x_set_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long prate)
{
	pr_debug("[%s:%d] drate %lu prate %lu\n", __func__, __LINE__,
			drate, prate);

	/* TODO */

	return 0;
}

static const struct clk_ops sdp_pll2555x_clk_ops = {
	.recalc_rate = sdp_pll2555x_recalc_rate,
	.round_rate = sdp_pll2555x_round_rate,
	.set_rate = sdp_pll2555x_set_rate,
};

static struct clk * __init sdp_clk_register_pll_all(const char *name,
		const char *parent, const void __iomem *reg_pms,
		const void __iomem *reg_k, unsigned int div, PLL_TYPE pll_type)
{
	struct sdp_clk_pll2xxxx *pll;
	struct clk *clk;
	struct clk_init_data init = {
		.name = name,
		.flags = CLK_GET_RATE_NOCACHE,
		.parent_names = &parent,
		.num_parents = 1,
	};
	int r;

	if (pll_type == PLL_2XXXX) {
		init.ops = &sdp_pll2xxxx_clk_ops;
	} else if (pll_type == PLL_3XXXX) {
		init.ops = &sdp_pll3xxxx_clk_ops;
	} else if ((pll_type == PLL_2555X) ) {
		init.ops = &sdp_pll2555x_clk_ops;		
	} else {
		pr_err("%s: could not allocate pll type %s\n", __func__, name);		
		return NULL;
	}
		
	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	pll->hw.init = &init;
	pll->reg_pms = reg_pms;
	pll->reg_k = reg_k;
	pll->div = div;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	r = clk_register_clkdev(clk, name, NULL);
	if (r)
		pr_err("%s: failed to register lookup for %s\n", __func__,
				name);

	return clk;
}

static void __init sdp_clk_register_pll(struct device_node *np,
		struct sdp_pll *clks, unsigned int nr_clk)
{
	struct clk *clk;
	int i;

	for (i = 0; i < (int) nr_clk; i++, clks++) {
		clk = sdp_clk_register_pll_all(clks->name, clks->parent,
				(void *) ((u32) reg_base + clks->off_ams),
				clks->off_k ? (void *) ((u32) reg_base + clks->off_k) : NULL, clks->div ? clks->div : 1, 
				clks->pll_type);
		sdp_clk_add_provider(np, clks->name, clk);

		pr_info("SDP: %s %ldHz\n", clks->name, _get_rate(clks->name));
	}
}

static void __init sdp_clk_register_gate(struct device_node *np,
		struct sdp_gate *clks, unsigned int nr_clk)
{
	struct clk *clk;
	int i;
	int r;

	for (i = 0; i < (int) nr_clk; i++, clks++) {
		clk = clk_register_gate(NULL, clks->name, clks->parent,
				clks->flags, (void *) ((u32) reg_base + clks->offset),
				clks->bit_idx, clks->gate_flags, &sdp_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		sdp_clk_add_provider(np, clks->name, clk);

		if (clks->alias) {
			r = clk_register_clkdev(clk, clks->alias,
					clks->dev_name);
			if (r)
				pr_err("%s: failed to register lookup %s\n",
						__func__, clks->alias);
		}
	}
}

static void __init sdp_clk_register_fixed_div(struct device_node *np,
		struct sdp_fixed_div *clks, unsigned int nr_clk)
{
	struct clk *clk;
	int i;
	int r;

	for (i = 0; i < (int) nr_clk; i++, clks++) {
		clk = clk_register_fixed_factor(NULL, clks->name, clks->parent,
				clks->flags, clks->mult, clks->div);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					clks->name);
			continue;
		}

		sdp_clk_add_provider(np, clks->name, clk);

		if (clks->alias) {
			r = clk_register_clkdev(clk, clks->alias,
					clks->dev_name);
			if (r)
				pr_err("%s: failed to register lookup %s\n",
						__func__, clks->alias);
		}
	}
}

static __initdata struct of_device_id ext_clk_match[] = {
	{ .compatible = "samsung,sdp-clock-fin", .data = (void *)0, },
};

static void __init sdp_clk_init(struct device_node *np,
		struct sdp_clocks *sdp_clk)
{
	void __iomem *base;

	/* Temoporary: should be removed*/
	struct resource res;
	if (of_address_to_resource(np, 0, &res)) {
		pr_err("%s: failed get the clock resource \n", __func__);
		return ;
	}
	reg_phy_base = res.start;
	reg_phy_end = res.end;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: failed to map clock controller registers,"
				" aborting clock initialization\n", __func__);
		return;
	}

	reg_base = base;

	sdp_clk_of_register_fixed_ext(np, sdp_clk->fixed_rate,
			sdp_clk->size_of_fixed_rate, ext_clk_match);

	sdp_clk_register_pll(np, sdp_clk->pll_clk, sdp_clk->size_of_pll_clk);

	/* TODO: register mux */

	sdp_clk_register_fixed_div(np, sdp_clk->fixed_div,
			sdp_clk->size_of_fixed_div);

	sdp_clk_register_gate(np, sdp_clk->gate_clk,
			sdp_clk->size_of_gate_clk);

}

static void set_ap_reg(void)
{
	reg_phy_base_ap = reg_phy_base;
	reg_phy_end_ap  = reg_phy_end;
	reg_base_ap		= reg_base;
}


static void __init sdp1106_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1106_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1106_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1106_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1106_pll);
	sdp_soc_clk.fixed_div = sdp1106_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1106_fixed_div);
	sdp_soc_clk.gate_clk = sdp1106_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1106_gate);

	sdp_clk_init(np, &sdp_soc_clk);
	set_ap_reg();

	pr_info("SDP1106 clocks: ARM %ldHz DDR %ldHz APB %ldHz\n",
			_get_rate("arm_clk"), _get_rate("pll2_ddr"),
			_get_rate("apb_pclk"));

}
CLK_OF_DECLARE(sdp1106_clk, "samsung,sdp1106-clock", sdp1106_clk_init);


static void __init sdp1202_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1202_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1202_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1202_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1202_pll);
	sdp_soc_clk.fixed_div = sdp1202_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1202_fixed_div);
	sdp_soc_clk.gate_clk = sdp1202_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1202_gate);

	sdp_clk_init(np, &sdp_soc_clk);

	pr_info("SDP1202 clocks: ARM %ldHz DDR %ldHz ABP %ldHz\n",
			_get_rate("arm_clk"), _get_rate("pll5_ddr"),
			_get_rate("apb_clk"));

	set_ap_reg();

}
CLK_OF_DECLARE(sdp_1202_clk, "samsung,sdp1202-clock", sdp1202_clk_init);

static void __init sdp1304_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1202_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1202_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1202_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1202_pll);
	sdp_soc_clk.fixed_div = sdp1304_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1304_fixed_div);
	sdp_soc_clk.gate_clk = sdp1202_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1202_gate);

	sdp_clk_init(np, &sdp_soc_clk);
	set_ap_reg();
	pr_info("SDP1304 clocks: ARM %ldHz DDR %ldHz APB %ldHz AHB %ldHz\n",
			_get_rate("arm_clk"), _get_rate("pll5_ddr"),
			_get_rate("apb_pclk"), _get_rate("ahb_clk"));
}
CLK_OF_DECLARE(sdp_1304_clk, "samsung,sdp1304-clock", sdp1304_clk_init);

static void __init sdp1305_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1305_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1305_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1305_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1305_pll);
	sdp_soc_clk.fixed_div = sdp1305_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1305_fixed_div);
	sdp_soc_clk.gate_clk = sdp1305_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1305_gate);

	sdp_clk_init(np, &sdp_soc_clk);
}
CLK_OF_DECLARE(sdp_1305_clk, "samsung,sdp1305-clock", sdp1305_clk_init);

static void __init sdp1404_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1404_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1404_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1404_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1404_pll);
	sdp_soc_clk.fixed_div = sdp1404_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1404_fixed_div);
	sdp_soc_clk.gate_clk = sdp1404_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1404_gate);

	sdp_clk_init(np, &sdp_soc_clk);
	set_ap_reg();
	pr_info ("SDP1404 %-12s:%10lu Hz\n", "fin_pll", _get_rate("fin_pll"));
}
CLK_OF_DECLARE(sdp_1404_clk, "samsung,sdp1404-clock", sdp1404_clk_init);

static void __init sdp1406_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1406_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1406_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1406_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1406_pll);
	sdp_soc_clk.fixed_div = sdp1406_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1406_fixed_div);
	sdp_soc_clk.gate_clk = sdp1406_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1406_gate);

	sdp_clk_init(np, &sdp_soc_clk);
	set_ap_reg();
	pr_info ("SDP1406 %-12s:%10lu Hz\n", "fin_pll", _get_rate("fin_pll"));
}
CLK_OF_DECLARE(sdp_1406_clk, "samsung,sdp1406-clock", sdp1406_clk_init);

static void __init sdp1412_clk_init(struct device_node *np)
{
	struct sdp_clocks sdp_soc_clk;

	sdp_soc_clk.fixed_rate = sdp1412_fixed_rate;
	sdp_soc_clk.size_of_fixed_rate = ARRAY_SIZE(sdp1412_fixed_rate);
	sdp_soc_clk.pll_clk = sdp1412_pll;
	sdp_soc_clk.size_of_pll_clk = ARRAY_SIZE(sdp1412_pll);
	sdp_soc_clk.fixed_div = sdp1412_fixed_div;
	sdp_soc_clk.size_of_fixed_div = ARRAY_SIZE(sdp1412_fixed_div);
	sdp_soc_clk.gate_clk = sdp1412_gate;
	sdp_soc_clk.size_of_gate_clk = ARRAY_SIZE(sdp1412_gate);

	sdp_clk_init(np, &sdp_soc_clk);
	set_ap_reg();
	pr_info ("SDP1412 %-12s:%10lu Hz %-12s:%10lu Hz\n", "fin_pll", _get_rate("fin_pll"), "APB_PCLK",  _get_rate("apb_pclk"));
}

CLK_OF_DECLARE(sdp_1412_clk, "samsung,sdp1412-clock", sdp1412_clk_init);


u32 sdp_read_clkrst_mux(u32 phy_addr);

u32 sdp_read_clkrst_mux(u32 phy_addr)
{
	void __iomem *addr;
	u32 val;
	
	if((phy_addr < reg_phy_base_ap) || (phy_addr > reg_phy_end_ap)) {
		panic("%s: invalid address for clockgating! addr=0x%08X\n", __func__, phy_addr);
		return (u32) -EINVAL;
	}
	addr = (void *) ((u32) reg_base_ap + (phy_addr - (u32) reg_phy_base_ap));

	val = readl(addr);
	
	return val;
}
EXPORT_SYMBOL(sdp_read_clkrst_mux);

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value);

int sdp_set_clkrst_mux(u32 phy_addr, u32 mask, u32 value)
{
	void __iomem *addr;
	unsigned long flags;
	u32 val;

	if((phy_addr < reg_phy_base_ap) || (phy_addr > reg_phy_end_ap)) {
		panic("%s: invalid address for clockgating! addr=0x%08X\n", __func__, phy_addr);
		return -EINVAL;
	}

	addr = (void *)((u32) reg_base_ap + (phy_addr - (u32) reg_phy_base_ap));
	
	spin_lock_irqsave(&sdp_clk_lock, flags);	// XXX: shared with clk_gate
	
	val = readl(addr);
	val &= (~mask);
	val |= value;
	writel(val, addr);
	readl(addr);

	spin_unlock_irqrestore(&sdp_clk_lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_set_clkrst_mux);

int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value)
{
	return sdp_set_clkrst_mux(phy_addr, mask, value);
}
EXPORT_SYMBOL(sdp_set_clockgating);

