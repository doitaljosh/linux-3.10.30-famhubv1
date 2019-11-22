/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "sdp_dvb.h"

#include "dvb_frontend.h"

#include "si2190.h"

#define SDP_CH_SYSTEM_MODE		0x0008
#define SDP_CH_FFT			0x0034
#define SDP_CH_NCO			0x0040
#define SDP_CH_DEV_ADDR			0x0140
#define SDP_CH_SYNC_SYS			0x01AC

#define SDP_CH_RESET_VQ			0x1000
#define SDP_CH_TST_SEL_VQ		0x1020
#define SDP_CH_VSB_AGC_PWR		0x1028
#define SDP_CH_QAM_AGC_PWR		0x102C
#define SDP_CH_DN_CONV_FREQ		0x1030
#define SDP_CH_VSB_AGC_CTRL		0x1038
#define SDP_CH_STR_BIAS_QAM256		0x104C
#define SDP_CH_PPTL_CTRL12_V		0x1074
#define SDP_CH_PPTL_CTRL34_V		0x1078
#define SDP_CH_PTL2_GP_GI_V		0x107C
#define SDP_CH_FSTR_GI_V		0x1088
#define SDP_CH_STR_CTRL_QAM		0x108C
#define SDP_CH_SRAD34			0x10E4
#define SDP_CH_STR			0x10EC
#define SDP_CH_FSM12			0x10F0
#define SDP_CH_FSM34			0x10F4
#define SDP_CH_FSM9			0x1104
#define SDP_CH_CUR_MODE			0x1140

#define SDP_CH_186520D4			0x20D4
#define SDP_CH_CIRA_SYNC1		0x20E0
#define SDP_CH_CIRA_SYNC2		0x20E4
#define SDP_CH_TEST_MUX			0x20E8
#define SDP_CH_CIRA_OGI1		0x20F8
#define SDP_CH_CIRA_OGI23		0x20FC
#define SDP_CH_CSI_THR16		0x211C

#define SDP_CH_MASTER_LOCK		0x41A0

#define SDP_CH_FEC			0x5008
#define SDP_CH_DVBTC_PK_NUM		0x510C
#define SDP_CH_TS_LOCK_CHECK12		0x5140
#define SDP_CH_TS_LOCK_CHECK34		0x5144
#define SDP_CH_BUS_INTF23		0x5160
#define SDP_CH_18655168			0x5168
#define SDP_CH_AUTO_REACQ12		0x5170
#define SDP_CH_PF_CHECK			0x5180
#define SDP_CH_MPEG_INTF12		0x5198

#define SDP_CH_18656000			0x6000
#define SDP_CH_18656004			0x6004
#define SDP_CH_18656008			0x6008
#define SDP_CH_1865600C			0x600C
#define SDP_CH_18656020			0x6020

#define IC_SYS_VSB			(1 << 20)
#define IC_SYS_QAM			(2 << 20)
#define IC_SYS_DVBC			(3 << 20)
#define IC_SYS_DVBT			(4 << 20)
#define IC_SYS_ISDBT			(5 << 20)
#define IC_QAM_16			(0 << 16)
#define IC_QAM_32			(1 << 16)
#define IC_QAM_64			(2 << 16)
#define IC_QAM_128			(3 << 16)
#define IC_QAM_256			(4 << 16)
#define SYS_SW_RESET			(0x1F << 0)

#define IC_AUTO_SPECT_INV		(1 << 12)

#define IC_NCO_FREERUN_L(x)		((x) << 16)
#define IC_NCO_FREERUN_H(x)		((x) << 0)

#define IC_STR_ABS2_THR(x)		((x) << 16)
#define IC_USE_SYNCRST			(1 << 12)
#define IC_SYNCRST_THR(x)		((x) << 0)

#define VQ_RESET_MASK			(0x1F << 16)

#define IC_REACQ_ON			(1 << 31)
#define TV_LOCK_MON_EN			(1 << 30)
#define VQ_FEC_RESET			(1 << 20)
#define VQ_EQ_RESET			(1 << 19)
#define VQ_SYNC_RESET			(1 << 18)
#define VQ_CLKGEN_RESET			(1 << 17)
#define VQ_REACQ_RESET			(1 << 16)

#define AGC_REF_PWR_MASK		(0xFF << 0)
#define AGC_REF_PWR(x)			((x) << 0)

#define DN_CONV_FREQ_MASK		(0xFFFF << 0)
#define DN_CONV_FREQ(x)			((x) << 0)

#define WC_REF_PWR_MASK			(0xFF << 16)
#define WC_REF_PWR(x)			((x) << 16)

#define WC_PPTL_LOP_1(x)		((x) << 28)
#define WC_PPTL_LOP_2(x)		((x) << 24)
#define WC_PPTL_PILOT_AVG(x)		((x) << 20)
#define WC_PPTL_PILOT_VAR(x)		((x) << 16)
#define WC_PPTL_ON_THRESH(x)		((x) << 12)
#define WC_PPTL_OFF_THRESH(x)		((x) << 8)
#define WC_PPTL_MXMI_THRESH(x)		((x) << 0)

#define WC_INST_NOISE_PWR(x)		((x) << 24)
#define WC_PPTL_PILOT_AGC(x)		((x) << 20)
#define WC_PPTL_VAR_CTRL(x)		((x) << 16)
#define WC_MIN_PILOT_GAIN(x)		((x) << 8)
#define WC_MAX_PILOT_GAIN(x)		((x) << 0)

#define PTL2_GP1(x)			((x) << 28)
#define PTL2_GP2(x)			((x) << 24)
#define PTL2_GP3(x)			((x) << 20)
#define PTL2_GI1(x)			((x) << 12)
#define PTL2_GI2(x)			((x) << 8)
#define PTL2_GI3(x)			((x) << 4)
#define WC_AVG_SEL			(0 << 0)
#define WC_ORG_SEL			(1 << 0)

#define STR_GI1(x)			((x) << 28)
#define STR_GI2(x)			((x) << 24)
#define STR_GI3(x)			((x) << 20)
#define STR_GI4(x)			((x) << 16)
#define FSTR_FIRST_GP(x)		((x) << 12)
#define FSTR_FIRST_GI(x)		((x) << 8)
#define FSTR_SECOND_GP(x)		((x) << 4)
#define FSTR_SECOND_GI(x)		((x) << 0)

#define LEAKY_FACTOR_MASK		(0x7 << 28)
#define LEAKY_FACTOR(x)			((x) << 28)

#define SRAD_ENABLE			(1 << 0)

#define STR_FIRST_GP(x)			((x) << 28)
#define STR_FIRST_GI(x)			((x) << 24)
#define STR_SECOND_GP(x)		((x) << 20)
#define STR_SECOND_GI(x)		((x) << 16)
#define STR_LOCKTH(x)			((x) << 0)

#define AUTO_QAM_EN			(1 << 26)
#define AUTO_NORMAL_RESET_EN		(1 << 25)
#define AUTO_RESET_EN			(1 << 24)

#define SYNC_WAIT_TIME(x)		((x) << 24)
#define EQ_WAIT_TIME(x)			((x) << 16)
#define FEC_SYNC_WAIT_TIME(x)		((x) << 8)
#define RS_WAIT_TIME(x)			((x) << 0)

#define EQ_WAIT_TIME_128(x)		((x) << 8)
#define EQ_WAIT_TIME_32(x)		((x) << 0)

#define ISYNC_LOCK			(1 << 12)
#define IEQ_LOCK			(1 << 11)
#define IEQ_LOCK_Q			(1 << 10)
#define IFEC_LOCK			(1 << 9)
#define IFEC_LOCK_Q			(1 << 8)

#define TPS_SYNC_LOCK_EN		(1 << 31)
#define FEC_LOCK_EQ			(0 << 29)
#define FEC_LOCK_CIRA2			(1 << 29)
#define FEC_LOCK_CIRA1			(2 << 29)
#define FEC_LOCK_SYNC			(3 << 29)
#define EQ_INTF_RESERVED25		(4 << 25)
#define CIRA_IFFT_CONV_EN		(1 << 24)
#define EQ_IN_GAIN(x)			((x) << 16)
#define CIRA_SYNC_EN_SEL		(1 << 12)
#define EQ_INTF_RESERVED9		(6 << 9)
#define CIRA_SYNC_EN			(1 << 8)
#define CIRA_SYNC_EN_DLY(x)		((x) << 0)

#define TEST_MUX_SEL(x)			((x) << 16)
#define COMP_THRESHOLD(x)		((x) << 0)

#define Q16_0_THR(x)			((x) << 24)
#define Q16_0_THR2(x)			((x) << 16)
#define Q16_1_THR(x)			((x) << 8)
#define Q16_1_THR2(x)			((x) << 0)

#define MASTER_LOCK			(1 << 27)

#define WC_IQ_INV			(1 << 12)

#define TS_OUT_V_PATH			(1 << 24)
#define TS_OUT_TI_PATH			(0 << 24)
#define FEC_SW_RESET			(0x3F << 0)

#define NUM_TS_H(x)			((x) << 24)
#define NUM_TS_L(x)			((x) << 16)
#define RS_ERR_RANGE_H(x)		((x) << 8)
#define RS_ERR_RANGE_L(x)		((x) << 0)

#define PARTIAL_TS_LOCK			(1 << 31)
#define TS_LOCK_LAYER_C			(1 << 30)
#define TS_LOCK_LAYER_B			(1 << 29)
#define TS_LOCK_LAYER_A			(1 << 28)
#define TS_LOCK_THRESHOLD_A(x)		((x) << 0)

#define TS_LOCK_THRESHOLD_B(x)		((x) << 16)
#define TS_LOCK_THRESHOLD_C(x)		((x) << 0)

#define BUS_INTF_DRAM_ADDR_MASK		(0xFFE << 16)
#define BUS_INTF_DRAM_ADDR(x)		((((x) >> 20) & 0xFFE) << 16)

#define TMCC_OUT_SEL_DECODING		(0 << 4)
#define TMCC_OUT_SEL_USED		(1 << 4)

#define AUTO_REACQ_ON			(1 << 24)
#define AUTO_REACQ_SEL_AUTO		(1 << 20)

#define MPEG_OUT_CLK_POSITIVE		(0 << 24)
#define MPEG_OUT_CLK_NEGATIVE		(1 << 24)

enum ch_state {
	CH_STATE_NONE,
	CH_STATE_INIT,
	CH_STATE_SET,
	CH_STATE_LOCK,
};

enum ch_type {
	CH_TYPE_SDP1202,
	CH_TYPE_SDP1304
};

struct sdp_channel_data {
	enum ch_type type;
	u32 dram_base;
};

static const struct sdp_channel_data sdp1202_data = {
	.dram_base	= 0xC0000000,
	.type		= CH_TYPE_SDP1202,
};

static const struct sdp_channel_data sdp1304_data = {
	.dram_base	= 0x24800000,
	.type		= CH_TYPE_SDP1304,
};

struct sdp_channel {
	void __iomem *regs;

	struct dvb_frontend frontend;
	fe_modulation_t modulation;
	u32 frequency;
	enum ch_state state;
	const struct sdp_channel_data *data;
};

static int sdp_channel_init(struct dvb_frontend *fe)
{
	struct sdp_channel *ch = fe->demodulator_priv;
	u32 val;

	val = readl(ch->regs + SDP_CH_BUS_INTF23);
	val &= ~BUS_INTF_DRAM_ADDR_MASK;
	val |= BUS_INTF_DRAM_ADDR(ch->data->dram_base);
	writel(val, ch->regs + SDP_CH_BUS_INTF23);

	val = readl(ch->regs + SDP_CH_TST_SEL_VQ);
	val |= WC_IQ_INV;
	writel(val, ch->regs + SDP_CH_TST_SEL_VQ);

	/* VSB / QAM / DVB-C Init */

	val = readl(ch->regs + SDP_CH_VSB_AGC_PWR);
	val &= ~AGC_REF_PWR_MASK;
	val |= AGC_REF_PWR(0x90);
	writel(val, ch->regs + SDP_CH_VSB_AGC_PWR);

	val = readl(ch->regs + SDP_CH_QAM_AGC_PWR);
	val &= ~AGC_REF_PWR_MASK;
	val |= AGC_REF_PWR(0x90);
	writel(val, ch->regs + SDP_CH_QAM_AGC_PWR);

	val = readl(ch->regs + SDP_CH_DN_CONV_FREQ);
	val &= ~DN_CONV_FREQ_MASK;
	val |= DN_CONV_FREQ(0x668);
	writel(val, ch->regs + SDP_CH_DN_CONV_FREQ);

	val = readl(ch->regs + SDP_CH_VSB_AGC_CTRL);
	val &= ~WC_REF_PWR_MASK;
	val |= WC_REF_PWR(0x2E);
	writel(val, ch->regs + SDP_CH_VSB_AGC_CTRL);

	val = WC_PPTL_LOP_1(0x4) | WC_PPTL_LOP_2(0x1) |
		WC_PPTL_PILOT_AVG(0x2) | WC_PPTL_PILOT_VAR(0x3) |
		WC_PPTL_ON_THRESH(0x5) | WC_PPTL_OFF_THRESH(0x2) |
		WC_PPTL_MXMI_THRESH(0x70);
	writel(val, ch->regs + SDP_CH_PPTL_CTRL12_V);

	val = WC_INST_NOISE_PWR(0x60) | WC_PPTL_PILOT_AGC(0xE) |
		WC_PPTL_VAR_CTRL(0x6) | WC_MIN_PILOT_GAIN(0x10) |
		WC_MAX_PILOT_GAIN(0xD0);
	writel(val, ch->regs + SDP_CH_PPTL_CTRL34_V);

	val = PTL2_GP1(0x4) | PTL2_GP2(0x1) | PTL2_GP3(0xF) |
		PTL2_GI1(0x6) | PTL2_GI2(0x1) | PTL2_GI3(0xF) | WC_ORG_SEL;
	writel(val, ch->regs + SDP_CH_PTL2_GP_GI_V);

	val = STR_GI1(0x9) | STR_GI2(0x1) | STR_GI3(0x6) | STR_GI4(0x6) |
		FSTR_FIRST_GP(0x1) | FSTR_FIRST_GI(0x1) |
		FSTR_SECOND_GP(0x3) | FSTR_SECOND_GI(0x3);
	writel(val, ch->regs + SDP_CH_FSTR_GI_V);

	val = readl(ch->regs + SDP_CH_STR_CTRL_QAM);
	val &= ~LEAKY_FACTOR_MASK;
	val |= LEAKY_FACTOR(0x4);
	writel(val, ch->regs + SDP_CH_STR_CTRL_QAM);

	val = STR_FIRST_GP(0x1) | STR_FIRST_GI(0x0) |
		STR_SECOND_GP(0x3) | STR_SECOND_GI(0x2) |
		STR_LOCKTH(0x1900);
	writel(val, ch->regs + SDP_CH_STR);

	val = SYNC_WAIT_TIME(0x10) | EQ_WAIT_TIME(0x14) |
		FEC_SYNC_WAIT_TIME(0x10) | RS_WAIT_TIME(0x10);
	writel(val, ch->regs + SDP_CH_FSM34);

	val = EQ_WAIT_TIME_128(0x14) | EQ_WAIT_TIME_32(0x14);
	writel(val, ch->regs + SDP_CH_FSM9);

	val = 0x776603FF;	/* unknown register */
	writel(val, ch->regs + SDP_CH_CIRA_SYNC2);
	val = 0x06160E16;	/* unknown register */
	writel(val, ch->regs + SDP_CH_CIRA_OGI1);
	val = 0x0609E4F8;	/* unknown register */
	writel(val, ch->regs + SDP_CH_CIRA_OGI23);

	val = readl(ch->regs + SDP_CH_FFT);
	val |= IC_AUTO_SPECT_INV;
	writel(val, ch->regs + SDP_CH_FFT);

	/* DVB-T / ISDB-T Init */

	val = IC_NCO_FREERUN_L(0xAAAA) | IC_NCO_FREERUN_H(0x1A0A);
	writel(val, ch->regs + SDP_CH_NCO);

	val = IC_STR_ABS2_THR(0x40);
	writel(val, ch->regs + SDP_CH_SYNC_SYS);

	/* FEC */

	val = PARTIAL_TS_LOCK | TS_LOCK_LAYER_C | TS_LOCK_LAYER_B |
		TS_LOCK_LAYER_A | TS_LOCK_THRESHOLD_A(0x8);
	writel(val, ch->regs + SDP_CH_TS_LOCK_CHECK12);

	val = TS_LOCK_THRESHOLD_C(0x8) | TS_LOCK_THRESHOLD_B(0x8);
	writel(val, ch->regs + SDP_CH_TS_LOCK_CHECK34);

	val = 0x07CE0000;	/* unknown register */
	writel(val, ch->regs + SDP_CH_18655168);

	val = MPEG_OUT_CLK_NEGATIVE;
	writel(val, ch->regs + SDP_CH_MPEG_INTF12);

	val = readl(ch->regs + SDP_CH_PF_CHECK);
	val |= TMCC_OUT_SEL_USED;
	writel(val, ch->regs + SDP_CH_PF_CHECK);

	val = NUM_TS_H(0x9) | NUM_TS_L(0x0) |
		RS_ERR_RANGE_H(0xF) | RS_ERR_RANGE_L(0xFF);
	writel(val, ch->regs + SDP_CH_DVBTC_PK_NUM);

	ch->state = CH_STATE_INIT;

	return 0;
}

static void sdp_channel_sleep(struct sdp_channel *ch)
{
	u32 val;

	/* SYNC2 (VSB/QAM, DVB-C) */
	val = readl(ch->regs + SDP_CH_RESET_VQ);
	val &= ~(VQ_CLKGEN_RESET | VQ_REACQ_RESET);
	writel(val, ch->regs + SDP_CH_RESET_VQ);

	/* SYNC1 (DVB-T, ISDB-T) */
	val = readl(ch->regs + SDP_CH_SYSTEM_MODE);
	val &= ~SYS_SW_RESET;
	writel(val, ch->regs + SDP_CH_SYSTEM_MODE);

	/* FEC2 (DVB-T, ISDB-T) */
	val = readl(ch->regs + SDP_CH_FEC);
	val &= ~FEC_SW_RESET;
	writel(val, ch->regs + SDP_CH_FEC);
}

static void sdp_channel_reset(struct sdp_channel *ch)
{
	u32 val;

	sdp_channel_sleep(ch);

	udelay(1);

	/* SYNC2 (VSB/QAM, DVB-C) */
	val = readl(ch->regs + SDP_CH_RESET_VQ);
	val |= (VQ_CLKGEN_RESET | VQ_REACQ_RESET);
	writel(val, ch->regs + SDP_CH_RESET_VQ);

	/* SYNC1 (DVB-T, ISDB-T) */
	val = readl(ch->regs + SDP_CH_SYSTEM_MODE);
	val |= SYS_SW_RESET;
	writel(val, ch->regs + SDP_CH_SYSTEM_MODE);

	/* FEC (DVB-T, ISDB-T) */
	val = readl(ch->regs + SDP_CH_FEC);
	val |= FEC_SW_RESET;
	writel(val, ch->regs + SDP_CH_FEC);
}

static void sdp_channel_set_afe(struct sdp_channel *ch)
{
	/* AFE SET: unknown registers */
	writel(0x00FD0000, ch->regs + SDP_CH_18656000);
	writel(0x0A160413, ch->regs + SDP_CH_18656004);
	writel(0x88000141, ch->regs + SDP_CH_18656008);
	writel(0x0003D400, ch->regs + SDP_CH_1865600C);
	writel(0x88000149, ch->regs + SDP_CH_18656008);
	if (ch->data->type == CH_TYPE_SDP1304)
		writel(0x0C31A6E9, ch->regs + SDP_CH_18656020);
}

static void sdp_channel_set_mode(struct sdp_channel *ch,
		fe_modulation_t modulation)
{
	u32 val;
	u32 mode;
	u32 lock = FEC_LOCK_CIRA2;

	sdp_channel_set_afe(ch);

	switch (modulation) {
	case QAM_16:
		mode = IC_SYS_QAM | IC_QAM_16;
		break;
	case QAM_32:
		mode = IC_SYS_QAM | IC_QAM_32;
		break;
	case QAM_64:
		mode = IC_SYS_QAM | IC_QAM_64;
		break;
	case QAM_128:
		mode = IC_SYS_QAM | IC_QAM_128;
		break;
	case QAM_256:
		mode = IC_SYS_QAM | IC_QAM_256;
		break;
	case VSB_8:
	case VSB_16:
		mode = IC_SYS_VSB;
		break;
	default:
		/* NOTE: regards unsupported modulation as DVBC */
		mode = IC_SYS_DVBC;
		lock = FEC_LOCK_EQ;
		pr_err("sdp-channel: unsupported modulation(%d)\n", modulation);
		break;
	}

	val = readl(ch->regs + SDP_CH_RESET_VQ);
	val &= ~VQ_RESET_MASK;
	val |= IC_REACQ_ON | VQ_FEC_RESET | VQ_EQ_RESET | VQ_SYNC_RESET;
	writel(val, ch->regs + SDP_CH_RESET_VQ);

	writel(mode, ch->regs + SDP_CH_SYSTEM_MODE);

	val = readl(ch->regs + SDP_CH_SRAD34);
	val &= ~SRAD_ENABLE;
	writel(val, ch->regs + SDP_CH_SRAD34);

	val = readl(ch->regs + SDP_CH_FSM12);
	val &= ~(AUTO_QAM_EN | AUTO_NORMAL_RESET_EN | AUTO_RESET_EN);
	writel(val, ch->regs + SDP_CH_FSM12);

	/* EQ1 (VSB/QAM, DVB-C) */

	if (modulation == VSB_8 || modulation == VSB_16)
		val = 0x51A01AB0;
	else
		val = 0x51501AB0;

	writel(val, ch->regs + SDP_CH_186520D4);	/* unknown register */

	val = TPS_SYNC_LOCK_EN | lock | EQ_IN_GAIN(0x27) |
		CIRA_SYNC_EN_SEL | CIRA_SYNC_EN_DLY(0x95) |
		EQ_INTF_RESERVED25 | EQ_INTF_RESERVED9;
	writel(val, ch->regs + SDP_CH_CIRA_SYNC1);

	val = TEST_MUX_SEL(0x9D60) | COMP_THRESHOLD(0x4564);
	writel(val, ch->regs + SDP_CH_TEST_MUX);

	val = Q16_0_THR(0x16) | Q16_0_THR2(0x2C) |
		Q16_1_THR(0x0) | Q16_1_THR2(0x0);
	writel(val, ch->regs + SDP_CH_CSI_THR16);

	val = readl(ch->regs + SDP_CH_AUTO_REACQ12);
	val &= ~(AUTO_REACQ_ON | AUTO_REACQ_SEL_AUTO);
	writel(val, ch->regs + SDP_CH_AUTO_REACQ12);

	val = (FEC_SW_RESET | TS_OUT_V_PATH);
	writel(val, ch->regs + SDP_CH_FEC);
}

static int sdp_channel_set_frontend(struct dvb_frontend *fe)
{
	struct sdp_channel *ch = fe->demodulator_priv;
	struct dvb_tuner_ops *tuner = &fe->ops.tuner_ops;
	struct dtv_frontend_properties *prop = &fe->dtv_property_cache;
	int ret;

	if (!tuner->set_frequency) {
		pr_err("sdp-channel: tuner should have set_frequency\n");
		return -ENOSYS;
	}

	if (ch->state == CH_STATE_SET && ch->frequency == prop->frequency)
		return 0;

	ret = tuner->set_frequency(fe, prop->frequency);

	if (ret < 0)
		return ret;

	sdp_channel_sleep(ch);

	sdp_channel_set_mode(ch, prop->modulation);

	ch->modulation = prop->modulation;
	ch->frequency = prop->frequency;

	sdp_channel_reset(ch);

	ch->state = CH_STATE_SET;

	return 0;
}

static int sdp_channel_get_frontend(struct dvb_frontend *fe)
{
	struct sdp_channel *ch = fe->demodulator_priv;
	struct dvb_tuner_ops *tuner = &fe->ops.tuner_ops;
	struct dtv_frontend_properties *prop = &fe->dtv_property_cache;
	struct tuner_state state;
	int err;

	prop->modulation = ch->modulation;
	prop->frequency = ch->frequency;

	err = tuner->get_state(fe, DVBFE_TUNER_BANDWIDTH, &state);
	if (err < 0) {
		pr_err("sdp-channel: get state error: %d\n", err);
		return err;
	}

	prop->bandwidth_hz = state.bandwidth;

	return 0;
}

static int sdp_channel_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct sdp_channel *ch = fe->demodulator_priv;
	unsigned int val;

	val = readl(ch->regs + SDP_CH_MASTER_LOCK);

	if (val & MASTER_LOCK) {
		*status |= (FE_HAS_LOCK | FE_HAS_CARRIER | FE_HAS_SIGNAL);
		ch->state = CH_STATE_LOCK;
	}

	val = readl(ch->regs + SDP_CH_CUR_MODE);

	if (val & ISYNC_LOCK)
		*status |= FE_HAS_SYNC;

	return 0;
}

static struct dvb_frontend_ops sdp_channel_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name			= "SDP Frontend",
		.frequency_min		= 40000000,
		.frequency_max		= 1002000000,
		.frequency_stepsize	= 0,
		.caps =	FE_CAN_8VSB | FE_CAN_16VSB |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_128 |
			FE_CAN_QAM_256 | FE_CAN_QAM_AUTO
	},

	.init		= sdp_channel_init,

	.set_frontend	= sdp_channel_set_frontend,
	.get_frontend	= sdp_channel_get_frontend,
	.read_status	= sdp_channel_read_status,
};

static int init_frontend(struct sdp_channel *ch)
{
	int ret;
	struct dvb_frontend *fe;
	struct dvb_adapter *adapter;

	adapter = sdp_dvb_get_adapter();
	if (!adapter)
		return -ENODEV;

	fe = &ch->frontend;
	memcpy(&fe->ops, &sdp_channel_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = ch;

	fe = dvb_attach(si2190_attach, fe);
	if (!fe)
		return -ENODEV;

	ret = dvb_register_frontend(adapter, fe);
	if (ret) {
		dvb_frontend_detach(fe);
		return ret;
	}

	return 0;
}

static const struct of_device_id sdp_channel_dt_match[];

static int sdp_channel_probe(struct platform_device *pdev)
{
	struct sdp_channel *ch;
	struct resource *res;
	const struct of_device_id *id;
	int ret;

	ch = devm_kzalloc(&pdev->dev, sizeof(struct sdp_channel), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to find memory resource\n");
		return -ENODEV;
	}

	ch->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!ch->regs) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return -ENOMEM;
	}

	id = of_match_node(sdp_channel_dt_match, pdev->dev.of_node);
	if (!id) {
		dev_err(&pdev->dev, "failed to find matched id\n");
		return -ENODEV;
	}

	ch->data = id->data;

	ret = init_frontend(ch);
	if (ret) {
		dev_err(&pdev->dev, "failed to init frontend\n");
		return ret;
	}

	platform_set_drvdata(pdev, ch);

	return 0;
}

static int sdp_channel_remove(struct platform_device *pdev)
{
	struct sdp_channel *ch = platform_get_drvdata(pdev);

	dvb_unregister_frontend(&ch->frontend);
	dvb_frontend_detach(&ch->frontend);

	return 0;
}

static const struct of_device_id sdp_channel_dt_match[] = {
	{ .compatible = "samsung,sdp1202-channel", .data = &sdp1202_data, },
	{ .compatible = "samsung,sdp1304-channel", .data = &sdp1304_data, },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_channel_dt_match);

static struct platform_driver sdp_channel_driver = {
	.probe		= sdp_channel_probe,
	.remove		= sdp_channel_remove,
	.driver		= {
		.name	= "sdp-channel",
		.owner	= THIS_MODULE,
		.of_match_table = sdp_channel_dt_match,
	},
};
module_platform_driver(sdp_channel_driver);

MODULE_DESCRIPTION("Samsung SDP DVB demodulator driver");
MODULE_LICENSE("GPL");
