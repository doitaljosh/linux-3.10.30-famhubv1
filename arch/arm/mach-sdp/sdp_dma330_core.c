/* linux/arch/arm/common/pl330.c
 *
 * Copyright (C) 2010 Samsung Electronics Co Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

 /*
  * arch/arm/plat-sdp/sdp_dma330_core.c
  *
  * modified by dongseok lee <drain.lee@samsung.com>
  *
  * 20120702	drain.lee	porting for kerenl 3.0.20
  * 20121031	drain.lee	apply kernel patch
							2012-02-15	Javi Merino	ARM: 7326/2: PL330: ARM: 7326/2: PL330: fix null pointer dereference in pl330_chan_ctrl() <46e33c606af8e0caeeca374103189663d877c0d6>
							2012-01-03	Javi Merino ARM: 7242/1: PL330: Detach the request from the pl330_thread when it finishes successful <f98b9a26fe08f7f9d7fb26ee3d9f167f79b2f6b6>
							2011-12-23	Javi Merino	ARM: 7237/1: PL330: Fix driver freeze <abb959f8a3f125a6e6641abbd020111516dfc8f6>
							2011-11-21	Javi Merino	ARM: 7165/2: PL330: Fix typo in _prepare_ccr() <1c8a7c1fbfc7ae2362d26559df26b99c806b68b5>
							2011-11-21	Javi Merino	ARM: 7163/2: PL330: Only register usable channels <2674dd0b1c07d8b8dfb4872fc7b41841f05cb957>
							2011-10-22	Javi Merino	ARM: 7136/1: pl330: Fix a race condition <ee3f615819404a9438b2dd01b7a39f276d2737f2>
  * 20140822	drain.lee	fix compile warning
  */


//#define DEBUG
//#define PL330_DEBUG_MCGEN
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

#include <mach/sdp_dma330.h>

/* Register and Bit field Definitions */
#define DS		0x0
#define DS_ST_STOP	0x0
#define DS_ST_EXEC	0x1
#define DS_ST_CMISS	0x2
#define DS_ST_UPDTPC	0x3
#define DS_ST_WFE	0x4
#define DS_ST_ATBRR	0x5
#define DS_ST_QBUSY	0x6
#define DS_ST_WFP	0x7
#define DS_ST_KILL	0x8
#define DS_ST_CMPLT	0x9
#define DS_ST_FLTCMP	0xe
#define DS_ST_FAULT	0xf

#define DPC		0x4
#define INTEN		0x20
#define ES		0x24
#define INTSTATUS	0x28
#define INTCLR		0x2c
#define FSM		0x30
#define FSC		0x34
#define FTM		0x38

#define _FTC		0x40
#define FTC(n)		(_FTC + (n)*0x4)

#define _CS		0x100
#define CS(n)		(_CS + (n)*0x8)
#define CS_CNS		(1 << 21)

#define _CPC		0x104
#define CPC(n)		(_CPC + (n)*0x8)

#define _SA		0x400
#define SA(n)		(_SA + (n)*0x20)

#define _DA		0x404
#define DA(n)		(_DA + (n)*0x20)

#define _CC		0x408
#define CC(n)		(_CC + (n)*0x20)

#define CC_SRCINC	(1 << 0)
#define CC_DSTINC	(1 << 14)
#define CC_SRCPRI	(1 << 8)
#define CC_DSTPRI	(1 << 22)
#define CC_SRCNS	(1 << 9)
#define CC_DSTNS	(1 << 23)
#define CC_SRCIA	(1 << 10)
#define CC_DSTIA	(1 << 24)
#define CC_SRCBRSTLEN_SHFT	4
#define CC_DSTBRSTLEN_SHFT	18
#define CC_SRCBRSTSIZE_SHFT	1
#define CC_DSTBRSTSIZE_SHFT	15
#define CC_SRCCCTRL_SHFT	11
#define CC_SRCCCTRL_MASK	0x7
#define CC_DSTCCTRL_SHFT	25
#define CC_DRCCCTRL_MASK	0x7
#define CC_SWAP_SHFT	28

#define _LC0		0x40c
#define LC0(n)		(_LC0 + (n)*0x20)

#define _LC1		0x410
#define LC1(n)		(_LC1 + (n)*0x20)

#define DBGSTATUS	0xd00
#define DBG_BUSY	(1 << 0)

#define DBGCMD		0xd04
#define DBGINST0	0xd08
#define DBGINST1	0xd0c

#define CR0		0xe00
#define CR1		0xe04
#define CR2		0xe08
#define CR3		0xe0c
#define CR4		0xe10
#define CRD		0xe14

#define PERIPH_ID	0xfe0
#define PCELL_ID	0xff0

#define CR0_PERIPH_REQ_SET	(1 << 0)
#define CR0_BOOT_EN_SET		(1 << 1)
#define CR0_BOOT_MAN_NS		(1 << 2)
#define CR0_NUM_CHANS_SHIFT	4
#define CR0_NUM_CHANS_MASK	0x7
#define CR0_NUM_PERIPH_SHIFT	12
#define CR0_NUM_PERIPH_MASK	0x1f
#define CR0_NUM_EVENTS_SHIFT	17
#define CR0_NUM_EVENTS_MASK	0x1f

#define CR1_ICACHE_LEN_SHIFT	0
#define CR1_ICACHE_LEN_MASK	0x7
#define CR1_NUM_ICACHELINES_SHIFT	4
#define CR1_NUM_ICACHELINES_MASK	0xf

#define CRD_DATA_WIDTH_SHIFT	0
#define CRD_DATA_WIDTH_MASK	0x7
#define CRD_WR_CAP_SHIFT	4
#define CRD_WR_CAP_MASK		0x7
#define CRD_WR_Q_DEP_SHIFT	8
#define CRD_WR_Q_DEP_MASK	0xf
#define CRD_RD_CAP_SHIFT	12
#define CRD_RD_CAP_MASK		0x7
#define CRD_RD_Q_DEP_SHIFT	16
#define CRD_RD_Q_DEP_MASK	0xf
#define CRD_DATA_BUFF_SHIFT	20
#define CRD_DATA_BUFF_MASK	0x3ff

#define	PART		0x330
#define DESIGNER	0x41
#define REVISION	0x0
#define INTEG_CFG	0x0
#define PERIPH_ID_VAL	((PART << 0) | (DESIGNER << 12))

#define PCELL_ID_VAL	0xb105f00d

#define PL330_STATE_STOPPED		(1 << 0)
#define PL330_STATE_EXECUTING		(1 << 1)
#define PL330_STATE_WFE			(1 << 2)
#define PL330_STATE_FAULTING		(1 << 3)
#define PL330_STATE_COMPLETING		(1 << 4)
#define PL330_STATE_WFP			(1 << 5)
#define PL330_STATE_KILLING		(1 << 6)
#define PL330_STATE_FAULT_COMPLETING	(1 << 7)
#define PL330_STATE_CACHEMISS		(1 << 8)
#define PL330_STATE_UPDTPC		(1 << 9)
#define PL330_STATE_ATBARRIER		(1 << 10)
#define PL330_STATE_QUEUEBUSY		(1 << 11)
#define PL330_STATE_INVALID		(1 << 15)

#define PL330_STABLE_STATES (PL330_STATE_STOPPED | PL330_STATE_EXECUTING \
				| PL330_STATE_WFE | PL330_STATE_FAULTING)

#define CMD_DMAADDH	0x54
#define CMD_DMAEND	0x00
#define CMD_DMAFLUSHP	0x35
#define CMD_DMAGO	0xa0
#define CMD_DMALD	0x04
#define CMD_DMALDP	0x25
#define CMD_DMALP	0x20
#define CMD_DMALPEND	0x28
#define CMD_DMAKILL	0x01
#define CMD_DMAMOV	0xbc
#define CMD_DMANOP	0x18
#define CMD_DMARMB	0x12
#define CMD_DMASEV	0x34
#define CMD_DMAST	0x08
#define CMD_DMASTP	0x29
#define CMD_DMASTZ	0x0c
#define CMD_DMAWFE	0x36
#define CMD_DMAWFP	0x30
#define CMD_DMAWMB	0x13

#define SZ_DMAADDH	3
#define SZ_DMAEND	1
#define SZ_DMAFLUSHP	2
#define SZ_DMALD	1
#define SZ_DMALDP	2
#define SZ_DMALP	2
#define SZ_DMALPEND	2
#define SZ_DMAKILL	1
#define SZ_DMAMOV	6
#define SZ_DMANOP	1
#define SZ_DMARMB	1
#define SZ_DMASEV	2
#define SZ_DMAST	1
#define SZ_DMASTP	2
#define SZ_DMASTZ	1
#define SZ_DMAWFE	2
#define SZ_DMAWFP	2
#define SZ_DMAWMB	1
#define SZ_DMAGO	6

#define BRST_LEN(ccr)	((((ccr) >> CC_SRCBRSTLEN_SHFT) & 0xf) + 1)
#define BRST_SIZE(ccr)	(1 << (((ccr) >> CC_SRCBRSTSIZE_SHFT) & 0x7))

#define BYTE_TO_BURST(b, ccr)  ((b) / BRST_SIZE(ccr) / BRST_LEN(ccr))
#define BURST_TO_BYTE(c, ccr)  ((c) * BRST_SIZE(ccr) * BRST_LEN(ccr))


/* add ccr util func */
#define GET_SRC_BRST_LEN(ccr)	((((ccr) >> CC_SRCBRSTLEN_SHFT) & 0xF)+1)
#define GET_SRC_BRST_SIZE(ccr)	((((ccr) >> CC_SRCBRSTSIZE_SHFT) & 0x7))
#define GET_DST_BRST_LEN(ccr)	((((ccr) >> CC_DSTBRSTLEN_SHFT) & 0xF)+1)
#define GET_DST_BRST_SIZE(ccr)	((((ccr) >> CC_DSTBRSTSIZE_SHFT) & 0x7))

#define SET_SRC_BRST_LEN(len, ccr) do{  ccr = ((ccr)&~(0xF<<CC_SRCBRSTLEN_SHFT)) | ((((len)-1)&0xF)<< CC_SRCBRSTLEN_SHFT);  }while(0)
#define SET_SRC_BRST_SIZE(size, ccr) do{  ccr = ((ccr)&~(0x7<<CC_SRCBRSTSIZE_SHFT)) | (((size)&0x7)<< CC_SRCBRSTSIZE_SHFT); }while(0)
#define SET_DST_BRST_LEN(len, ccr) do{  ccr = ((ccr)&~(0xF<<CC_DSTBRSTLEN_SHFT)) | ((((len)-1)&0xF)<< CC_DSTBRSTLEN_SHFT);  }while(0)
#define SET_DST_BRST_SIZE(size, ccr) do{  ccr = ((ccr)&~(0x7<<CC_DSTBRSTSIZE_SHFT)) | (((size)&0x7)<< CC_DSTBRSTSIZE_SHFT); }while(0)

/*
 * With 256 bytes, we can do more than 2.5MB and 5MB xfers per req
 * at 1byte/burst for P<->M and M<->M respectively.
 * For typical scenario, at 1word/burst, 10MB and 20MB xfers per req
 * should be enough for P<->M and M<->M respectively.
 */
#define MCODE_BUFF_PER_REQ	256

/* If the _pl330_req is available to the client */
#define IS_FREE(req)	(*((u8 *)((req)->mc_cpu)) == CMD_DMAEND)

/* Use this _only_ to wait on transient states */
#define UNTIL(t, s)	while (!(_state(t) & (s))) cpu_relax();

#ifdef PL330_DEBUG_MCGEN
static unsigned cmd_line;
#define PL330_DBGCMD_DUMP(off, x...)	do { \
						printk(KERN_DEBUG "%x:", cmd_line); \
						printk(x); \
						cmd_line += off; \
					} while (0)
#define PL330_DBGMC_START(addr)		(cmd_line = addr)
#else
#define PL330_DBGCMD_DUMP(off, x...)	do {} while (0)
#define PL330_DBGMC_START(addr)		do {} while (0)
#endif

#define PL330_DBG_FLOW(fmt, args...) \
	PL330_DBGCMD_DUMP(0, "%s: " fmt, __FUNCTION__, args);


#define PL330_ALIGN	1

struct _xfer_spec {
	u32 ccr;
	struct pl330_req *r;
	struct pl330_xfer *x;
};

enum dmamov_dst {
	SAR = 0,
	CCR,
	DAR,
};

enum pl330_dst {
	SRC = 0,
	DST,
};

enum pl330_cond {
	SINGLE,
	BURST,
	ALWAYS,
};

struct _pl330_req {
	u32 mc_bus;
	void *mc_cpu;
	/* Number of bytes taken to setup MC for the req */
	u32 mc_len;
	struct pl330_req *r;
	/* Hook to attach to DMAC's list of reqs with due callback */
	struct list_head rqd;
};

/* ToBeDone for tasklet */
struct _pl330_tbd {
	bool reset_dmac;
	bool reset_mngr;
	u8 reset_chan;
};

/* A DMAC Thread */
struct pl330_thread {
	u8 id;
	int ev;
	/* If the channel is not yet acquired by any client */
	bool free;
	/* Parent DMAC */
	struct pl330_dmac *dmac;
	/* Only two at a time */
	struct _pl330_req req[2];
	/* Index of the last enqueued request */
	unsigned lstenq;
	/* Index of the last submitted request or -1 if the DMA is stopped */
	int req_running;
};

enum pl330_dmac_state {
	UNINIT,
	INIT,
	DYING,
};

/* A DMAC */
struct pl330_dmac {
	spinlock_t		lock;
	/* Holds list of reqs with due callbacks */
	struct list_head	req_done;
	/* Pointer to platform specific stuff */
	struct pl330_info	*pinfo;
	/* Maximum possible events/irqs */
	int			events[32];
	/* BUS address of MicroCode buffer */
	u32			mcode_bus;
	/* CPU address of MicroCode buffer */
	void			*mcode_cpu;
	/* List of all Channel threads */
	struct pl330_thread	*channels;
	/* Pointer to the MANAGER thread */
	struct pl330_thread	*manager;
	/* To handle bad news in interrupt */
	struct tasklet_struct	tasks;
	struct _pl330_tbd	dmac_tbd;
	/* State of DMAC operation */
	enum pl330_dmac_state	state;
};

static inline void _callback(struct pl330_req *r, enum pl330_op_err err)
{
	if (r && r->xfer_cb)
		r->xfer_cb(r->token, err);
}

static inline bool _queue_empty(struct pl330_thread *thrd)
{
	return (IS_FREE(&thrd->req[0]) && IS_FREE(&thrd->req[1]))
		? true : false;
}

static inline bool _queue_full(struct pl330_thread *thrd)
{
	return (IS_FREE(&thrd->req[0]) || IS_FREE(&thrd->req[1]))
		? false : true;
}

static inline bool is_manager(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;

	/* MANAGER is indexed at the end */
	if (thrd->id == pl330->pinfo->pcfg.num_chan)
		return true;
	else
		return false;
}

/* If manager of the thread is in Non-Secure mode */
static inline bool _manager_ns(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;

	return (pl330->pinfo->pcfg.mode & DMAC_MODE_NS) ? true : false;
}

static inline u32 get_id(struct pl330_info *pi, u32 off)
{
	void __iomem *regs = pi->base;
	u32 id = 0;

	id |= (readb(regs + off + 0x0) << 0);
	id |= (readb(regs + off + 0x4) << 8);
	id |= (readb(regs + off + 0x8) << 16);
	id |= (readb(regs + off + 0xc) << 24);

	return id;
}

static inline u32 _emit_ADDH(unsigned dry_run, u8 buf[],
		enum pl330_dst da, u16 val)
{
	if (dry_run)
		return SZ_DMAADDH;

	buf[0] = CMD_DMAADDH;
	buf[0] |= (da << 1);
	*((u16 *)&buf[1]) = val;

	PL330_DBGCMD_DUMP(SZ_DMAADDH, "\tDMAADDH %s %u\n",
		da == 1 ? "DA" : "SA", val);

	return SZ_DMAADDH;
}

static inline u32 _emit_END(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAEND;

	buf[0] = CMD_DMAEND;

	PL330_DBGCMD_DUMP(SZ_DMAEND, "\tDMAEND\n");

	return SZ_DMAEND;
}

static inline u32 _emit_FLUSHP(unsigned dry_run, u8 buf[], u8 peri)
{
	if (dry_run)
		return SZ_DMAFLUSHP;

	buf[0] = CMD_DMAFLUSHP;

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAFLUSHP, "\tDMAFLUSHP %u\n", peri >> 3);

	return SZ_DMAFLUSHP;
}

static inline u32 _emit_LD(unsigned dry_run, u8 buf[],	enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMALD;

	buf[0] = CMD_DMALD;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMALD, "\tDMALD%c\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMALD;
}

static inline u32 _emit_LDP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMALDP;

	buf[0] = CMD_DMALDP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMALDP, "\tDMALDP%c %u\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMALDP;
}

static inline u32 _emit_LP(unsigned dry_run, u8 buf[],
		unsigned loop, u8 cnt)
{
	if (dry_run)
		return SZ_DMALP;

	buf[0] = CMD_DMALP;

	if (loop)
		buf[0] |= (1 << 1);

	cnt--; /* DMAC increments by 1 internally */
	buf[1] = cnt;

	PL330_DBGCMD_DUMP(SZ_DMALP, "\tDMALP_%c %u\n", loop ? '1' : '0', cnt);

	return SZ_DMALP;
}

struct _arg_LPEND {
	enum pl330_cond cond;
	bool forever;
	unsigned loop;
	u8 bjump;
};

static inline u32 _emit_LPEND(unsigned dry_run, u8 buf[],
		const struct _arg_LPEND *arg)
{
	enum pl330_cond cond = arg->cond;
	bool forever = arg->forever;
	unsigned loop = arg->loop;
	u8 bjump = arg->bjump;

	if (dry_run)
		return SZ_DMALPEND;

	buf[0] = CMD_DMALPEND;

	if (loop)
		buf[0] |= (1 << 2);

	if (!forever)
		buf[0] |= (1 << 4);

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	buf[1] = bjump;

	PL330_DBGCMD_DUMP(SZ_DMALPEND, "\tDMALP%s%c_%c bjmpto_%x\n",
			forever ? "FE" : "END",
			cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'),
			loop ? '1' : '0',
			bjump);

	return SZ_DMALPEND;
}

static inline u32 _emit_KILL(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAKILL;

	buf[0] = CMD_DMAKILL;

	return SZ_DMAKILL;
}

static inline u32 _emit_MOV(unsigned dry_run, u8 buf[],
		enum dmamov_dst dst, u32 val)
{
	if (dry_run)
		return SZ_DMAMOV;

	buf[0] = CMD_DMAMOV;
	buf[1] = dst;
	*((u32 *)&buf[2]) = val;

#ifdef PL330_DEBUG_MCGEN
	char ccrdesc[100] = {[0] = 0,};
	if(dst == CCR)
	{
		
		int pos = 0;
		if( val&CC_SRCINC ) pos += sprintf(ccrdesc+pos, "SAI ");
		pos += sprintf(ccrdesc+pos, "SB%d ", (val>>CC_SRCBRSTLEN_SHFT & 0xF)+1);
		pos += sprintf(ccrdesc+pos, "SS%d ", 0x8 << (val>>CC_SRCBRSTSIZE_SHFT & 0x7));
		pos += sprintf(ccrdesc+pos, "SC%d ", (val>>CC_SRCCCTRL_SHFT & 0x7));
		pos += sprintf(ccrdesc+pos, "%3dbyte ", (1<<GET_SRC_BRST_SIZE(val)) * GET_SRC_BRST_LEN(val));

		if( val&CC_DSTINC ) pos += sprintf(ccrdesc+pos, "DAI ");
		pos += sprintf(ccrdesc+pos, "DB%d ", (val>>CC_DSTBRSTLEN_SHFT & 0xF)+1);
		pos += sprintf(ccrdesc+pos, "DS%d ", 0x8 << (val>>CC_DSTBRSTSIZE_SHFT & 0x7));
		pos += sprintf(ccrdesc+pos, "DC%d ", (val>>CC_DSTCCTRL_SHFT & 0x7));
		pos += sprintf(ccrdesc+pos, "%3dbyte ", (1<<GET_DST_BRST_SIZE(val)) * GET_DST_BRST_LEN(val));

//		pos += sprintf(ccrdesc+pos, "ES%d ", 0x8<<(val>>CC_SWAP_SHFT & 0x7));
	}
#endif/*PL330_DEBUG_MCGEN*/

	PL330_DBGCMD_DUMP(SZ_DMAMOV, "\tDMAMOV %s 0x%x %s\n",
		dst == SAR ? "SAR" : (dst == DAR ? "DAR" : "CCR"), val, ccrdesc);

	return SZ_DMAMOV;
}

static inline u32 _emit_NOP(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMANOP;

	buf[0] = CMD_DMANOP;

	PL330_DBGCMD_DUMP(SZ_DMANOP, "\tDMANOP\n");

	return SZ_DMANOP;
}

static inline u32 _emit_RMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMARMB;

	buf[0] = CMD_DMARMB;

	PL330_DBGCMD_DUMP(SZ_DMARMB, "\tDMARMB\n");

	return SZ_DMARMB;
}

static inline u32 _emit_SEV(unsigned dry_run, u8 buf[], u8 ev)
{
	if (dry_run)
		return SZ_DMASEV;

	buf[0] = CMD_DMASEV;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	PL330_DBGCMD_DUMP(SZ_DMASEV, "\tDMASEV %u\n", ev >> 3);

	return SZ_DMASEV;
}

static inline u32 _emit_ST(unsigned dry_run, u8 buf[], enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMAST;

	buf[0] = CMD_DMAST;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMAST, "\tDMAST%c\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMAST;
}

static inline u32 _emit_STP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMASTP;

	buf[0] = CMD_DMASTP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMASTP, "\tDMASTP%c %u\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMASTP;
}

static inline u32 _emit_STZ(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMASTZ;

	buf[0] = CMD_DMASTZ;

	PL330_DBGCMD_DUMP(SZ_DMASTZ, "\tDMASTZ\n");

	return SZ_DMASTZ;
}

static inline u32 _emit_WFE(unsigned dry_run, u8 buf[], u8 ev,
		unsigned invalidate)
{
	if (dry_run)
		return SZ_DMAWFE;

	buf[0] = CMD_DMAWFE;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	if (invalidate)
		buf[1] |= (1 << 1);

	PL330_DBGCMD_DUMP(SZ_DMAWFE, "\tDMAWFE %u%s\n",
		ev >> 3, invalidate ? ", I" : "");

	return SZ_DMAWFE;
}

static inline u32 _emit_WFP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMAWFP;

	buf[0] = CMD_DMAWFP;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (0 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (0 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAWFP, "\tDMAWFP%c %u\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'P'), peri >> 3);

	return SZ_DMAWFP;
}

static inline u32 _emit_WMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAWMB;

	buf[0] = CMD_DMAWMB;

	PL330_DBGCMD_DUMP(SZ_DMAWMB, "\tDMAWMB\n");

	return SZ_DMAWMB;
}

struct _arg_GO {
	u8 chan;
	u32 addr;
	unsigned ns;
};

static inline u32 _emit_GO(unsigned dry_run, u8 buf[],
		const struct _arg_GO *arg)
{
	u8 chan = arg->chan;
	u32 addr = arg->addr;
	unsigned ns = arg->ns;

	if (dry_run)
		return SZ_DMAGO;

	buf[0] = CMD_DMAGO;
	buf[0] |= (ns << 1);

	buf[1] = chan & 0x7;

	*((u32 *)&buf[2]) = addr;

	return SZ_DMAGO;
}

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

/* Returns Time-Out */
static bool _until_dmac_idle(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->pinfo->base;
	unsigned long loops = msecs_to_loops(5);

	do {
		/* Until Manager is Idle */
		if (!(readl(regs + DBGSTATUS) & DBG_BUSY))
			break;

		cpu_relax();
	} while (--loops);

	if (!loops)
		return true;

	return false;
}

static inline void _execute_DBGINSN(struct pl330_thread *thrd,
		u8 insn[], bool as_manager)
{
	void __iomem *regs = thrd->dmac->pinfo->base;
	u32 val;

	val = (insn[0] << 16) | (insn[1] << 24);
	if (!as_manager) {
		val |= (1 << 0);
		val |= (thrd->id << 8); /* Channel Number */
	}
	writel(val, regs + DBGINST0);

	val = *((u32 *)&insn[2]);
	writel(val, regs + DBGINST1);

	/* If timed out due to halted state-machine */
	if (_until_dmac_idle(thrd)) {
		dev_err(thrd->dmac->pinfo->dev, "DMAC halted!\n");
		return;
	}

	/* Get going */
	writel(0, regs + DBGCMD);
}

/*
 * Mark a _pl330_req as free.
 * We do it by writing DMAEND as the first instruction
 * because no valid request is going to have DMAEND as
 * its first instruction to execute.
 */
static void mark_free(struct pl330_thread *thrd, int idx)
{
	struct _pl330_req *req = &thrd->req[idx];

	_emit_END(0, req->mc_cpu);
	req->mc_len = 0;

	thrd->req_running = -1;
}

static inline u32 _state(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->pinfo->base;
	u32 val;

	if (is_manager(thrd))
		val = readl(regs + DS) & 0xf;
	else
		val = readl(regs + CS(thrd->id)) & 0xf;

	switch (val) {
	case DS_ST_STOP:
		return PL330_STATE_STOPPED;
	case DS_ST_EXEC:
		return PL330_STATE_EXECUTING;
	case DS_ST_CMISS:
		return PL330_STATE_CACHEMISS;
	case DS_ST_UPDTPC:
		return PL330_STATE_UPDTPC;
	case DS_ST_WFE:
		return PL330_STATE_WFE;
	case DS_ST_FAULT:
		return PL330_STATE_FAULTING;
	case DS_ST_ATBRR:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_ATBARRIER;
	case DS_ST_QBUSY:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_QUEUEBUSY;
	case DS_ST_WFP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_WFP;
	case DS_ST_KILL:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_KILLING;
	case DS_ST_CMPLT:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_COMPLETING;
	case DS_ST_FLTCMP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_FAULT_COMPLETING;
	default:
		return PL330_STATE_INVALID;
	}
}

static void _stop(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->pinfo->base;
	u8 insn[6] = {0, 0, 0, 0, 0, 0};

	if (_state(thrd) == PL330_STATE_FAULT_COMPLETING)
		UNTIL(thrd, PL330_STATE_FAULTING | PL330_STATE_KILLING);

	/* Return if nothing needs to be done */
	if (_state(thrd) == PL330_STATE_COMPLETING
		  || _state(thrd) == PL330_STATE_KILLING
		  || _state(thrd) == PL330_STATE_STOPPED)
		return;

	_emit_KILL(0, insn);

	/* Stop generating interrupts for SEV */
	writel(readl(regs + INTEN) & ~(1 << thrd->ev), regs + INTEN);

	_execute_DBGINSN(thrd, insn, is_manager(thrd));
}

/* Start doing req 'idx' of thread 'thrd' */
static bool _trigger(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->pinfo->base;
	struct _pl330_req *req;
	struct pl330_req *r;
	struct _arg_GO go;
	unsigned ns;
	u8 insn[6] = {0, 0, 0, 0, 0, 0};
	int idx;

	/* Return if already ACTIVE */
	if (_state(thrd) != PL330_STATE_STOPPED)
		return true;

	idx = 1 - thrd->lstenq;
	if (!IS_FREE(&thrd->req[idx]))
		req = &thrd->req[idx];
	else {
		idx = thrd->lstenq;
		if (!IS_FREE(&thrd->req[idx]))
			req = &thrd->req[idx];
		else
			req = NULL;
	}

	/* Return if no request */
	if (!req || !req->r)
		return true;

	r = req->r;

	if (r->cfg)
		ns = r->cfg->nonsecure ? 1 : 0;
	else if (readl(regs + CS(thrd->id)) & CS_CNS)
		ns = 1;
	else
		ns = 0;

	/* See 'Abort Sources' point-4 at Page 2-25 */
	if (_manager_ns(thrd) && !ns)
		dev_info(thrd->dmac->pinfo->dev, "%s:%d Recipe for ABORT!\n",
			__func__, __LINE__);

	go.chan = thrd->id;
	go.addr = req->mc_bus;
	go.ns = ns;
	_emit_GO(0, insn, &go);

	/* Set to generate interrupts for SEV */
	writel(readl(regs + INTEN) | (1 << thrd->ev), regs + INTEN);

	/* Only manager can execute GO */
	_execute_DBGINSN(thrd, insn, true);

	thrd->req_running = idx;

	return true;
}

static bool _start(struct pl330_thread *thrd)
{
	switch (_state(thrd)) {
	case PL330_STATE_FAULT_COMPLETING:
		UNTIL(thrd, PL330_STATE_FAULTING | PL330_STATE_KILLING);

		if (_state(thrd) == PL330_STATE_KILLING)
			UNTIL(thrd, PL330_STATE_STOPPED)

	case PL330_STATE_FAULTING:
		_stop(thrd);
		dev_warn(thrd->dmac->pinfo->dev, "%s:%d Channal%d Status is FAULTING!\n",
			__func__, __LINE__, thrd->id);

	case PL330_STATE_KILLING:
	case PL330_STATE_COMPLETING:
		UNTIL(thrd, PL330_STATE_STOPPED)

	case PL330_STATE_STOPPED:
		return _trigger(thrd);

	case PL330_STATE_WFP:
	case PL330_STATE_QUEUEBUSY:
	case PL330_STATE_ATBARRIER:
	case PL330_STATE_UPDTPC:
	case PL330_STATE_CACHEMISS:
	case PL330_STATE_EXECUTING:
		return true;

	case PL330_STATE_WFE: /* For RESUME, nothing yet */
	default:
		return false;
	}
}

static inline int _ldst_memtomem(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	while (cyc--) {
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
//		off += _emit_RMB(dry_run, &buf[off]);
		off += _emit_ST(dry_run, &buf[off], ALWAYS);
//		off += _emit_WMB(dry_run, &buf[off]);
	}

	return off;
}

static inline int _ldst_devtomem(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], SINGLE, pxs->r->peri);
		off += _emit_LDP(dry_run, &buf[off], SINGLE, pxs->r->peri);
		off += _emit_ST(dry_run, &buf[off], ALWAYS);
		off += _emit_FLUSHP(dry_run, &buf[off], pxs->r->peri);
	}

	return off;
}

static inline int _ldst_memtodev(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], SINGLE, pxs->r->peri);
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
		off += _emit_STP(dry_run, &buf[off], SINGLE, pxs->r->peri);
		off += _emit_FLUSHP(dry_run, &buf[off], pxs->r->peri);
	}

	return off;
}

static int _bursts(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	switch (pxs->r->rqtype) {
	case MEMTODEV:
		off += _ldst_memtodev(dry_run, &buf[off], pxs, cyc);
		break;
	case DEVTOMEM:
		off += _ldst_devtomem(dry_run, &buf[off], pxs, cyc);
		break;
	case MEMTOMEM:
		off += _ldst_memtomem(dry_run, &buf[off], pxs, cyc);
		break;
	default:
		off += 0x40000000; /* Scare off the Client */
		break;
	}

	return off;
}

/* Returns bytes consumed and updates bursts */
static inline int _loop(unsigned dry_run, u8 buf[],
		unsigned long *bursts, const struct _xfer_spec *pxs)
{
	int cyc, cycmax, szlp, szlpend, szbrst, off;
	unsigned lcnt0, lcnt1, ljmp0, ljmp1;
	struct _arg_LPEND lpend;

	/* Max iterations possibile in DMALP is 256 */
	if (*bursts >= 256*256) {
		lcnt1 = 256;
		lcnt0 = 256;
		cyc = *bursts / lcnt1 / lcnt0;
	} else if (*bursts > 256) {
		lcnt1 = 256;
		lcnt0 = *bursts / lcnt1;
		cyc = 1;
	} else {
		lcnt1 = *bursts;
		lcnt0 = 0;
		cyc = 1;
	}

	szlp = _emit_LP(1, buf, 0, 0);
	szbrst = _bursts(1, buf, pxs, 1);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 0;
	lpend.bjump = 0;
	szlpend = _emit_LPEND(1, buf, &lpend);

	if (lcnt0) {
		szlp *= 2;
		szlpend *= 2;
	}

	/*
	 * Max bursts that we can unroll due to limit on the
	 * size of backward jump that can be encoded in DMALPEND
	 * which is 8-bits and hence 255
	 */
	cycmax = (255 - (szlp + szlpend)) / szbrst;

	cyc = (cycmax < cyc) ? cycmax : cyc;

	off = 0;

	if (lcnt0) {
		off += _emit_LP(dry_run, &buf[off], 0, lcnt0);
		ljmp0 = off;
	}

	off += _emit_LP(dry_run, &buf[off], 1, lcnt1);
	ljmp1 = off;

	off += _bursts(dry_run, &buf[off], pxs, cyc);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 1;
	lpend.bjump = off - ljmp1;
	off += _emit_LPEND(dry_run, &buf[off], &lpend);

	if (lcnt0) {
		lpend.cond = ALWAYS;
		lpend.forever = false;
		lpend.loop = 0;
		lpend.bjump = off - ljmp0;
		off += _emit_LPEND(dry_run, &buf[off], &lpend);
	}

	*bursts = lcnt1 * cyc;
	if (lcnt0)
		*bursts *= lcnt0;

	return off;
}




static inline int _byte_to_ccr_src(int bytes, u32 ccr)
{
	int s=3;
	if(bytes <= 0) return -EINVAL;
	
	while(s >= 0)
	{
		const int bsize = 1<<s;
		if(  (bytes%bsize==0) && ((bytes/bsize) <= 16) )
		{
			SET_SRC_BRST_LEN(bytes/bsize, ccr);
			SET_SRC_BRST_SIZE(s, ccr);
			//pr_debug("_byte_to_ccr_dst byte=%d, bsize=%d, ccr=0x%x\n", bytes, bsize, ccr);
			return ccr;
		}
		s--;
	}
	return -EINVAL;
}

static inline int _byte_to_ccr_dst(int bytes, u32 ccr)
{
	int s=3;
	if(bytes <= 0) return -EINVAL;
	
	while(s >= 0)
	{
		const int bsize = 1<<s;
		if(  (bytes%bsize==0) && ((bytes/bsize) <= 16) )
		{
			SET_DST_BRST_LEN(bytes/bsize, ccr);
			SET_DST_BRST_SIZE(s, ccr);
			//pr_debug("_byte_to_ccr_dst byte=%d, bsize=%d, ccr=0x%x\n", bytes, bsize, ccr);
			return ccr;
		}
		s--;
	}
	return -EINVAL;
}


/* this is 64byte aligned fill */
static inline u32 _emit_FILL(unsigned dry_run, u8 buf[])
{
	unsigned cnt;
	cnt = 0;
	while ((((u32)buf & 0x3F) != 0x0))
	{
		if( !dry_run )
			*buf = 0xff;

		buf++;
		cnt++;
	}
	if( !dry_run ) PL330_DBGCMD_DUMP(cnt, "\tFILL %d\n", cnt);
	return (cnt);
}

static inline bool _is_valid(u32 ccr)
{
	enum pl330_dstcachectrl dcctl;
	enum pl330_srccachectrl scctl;

	dcctl = (ccr >> CC_DSTCCTRL_SHFT) & CC_DRCCCTRL_MASK;
	scctl = (ccr >> CC_SRCCCTRL_SHFT) & CC_SRCCCTRL_MASK;

	if (dcctl == DINVALID1 || dcctl == DINVALID2
			|| scctl == SINVALID1 || scctl == SINVALID2)
		return false;
	else
		return true;
}

static inline int _aligned_both(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	int off = 0;
	struct pl330_xfer *x = pxs->x;
	u32 ccr = pxs->ccr;
	unsigned long c, bursts = BYTE_TO_BURST(x->bytes, ccr);

	/* if 0 burst, return 0 */
	if(bursts == 0)
		return off;

	if(!dry_run) PL330_DBGCMD_DUMP(0,"%s: BURSTS COPY. %#x --> %#x %dbytes (%dbyte x %dbursts)\n",
		__FUNCTION__, x->src_addr, x->dst_addr, BURST_TO_BYTE(bursts, pxs->ccr), BURST_TO_BYTE(1, pxs->ccr), bursts);

	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);
	while (bursts) {
		c = bursts;
		off += _loop(dry_run, &buf[off], &c, pxs);
		bursts -= c;
	}	
	
	return off;
}
static inline int _aligned_src(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	int off = 0;
	struct pl330_xfer *x = pxs->x;
	u32 ccr = pxs->ccr;
	unsigned long c, bursts = BYTE_TO_BURST(x->bytes, ccr);
	const u32 align_mask = BRST_SIZE(pxs->ccr)-1;
	const u32 dstoff = (x->dst_addr & align_mask);

	/* if 0 burst, return 0 */
	if(bursts == 0)
		return off;

	if(!dry_run) PL330_DBGCMD_DUMP(0,"%s: BURSTS COPY. %#x --> %#x %dbytes (%dbyte x %dbursts)\n",
		__FUNCTION__, x->src_addr, x->dst_addr, BURST_TO_BYTE(bursts, pxs->ccr), BURST_TO_BYTE(1, pxs->ccr), bursts);

	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);
	while (bursts) {
		c = bursts;
		off += _loop(dry_run, &buf[off], &c, pxs);
		bursts -= c;
	}

	if(!dry_run) PL330_DBGCMD_DUMP(0, "%s: Store remain byte.\n", __FUNCTION__);

	ccr = _byte_to_ccr_dst(dstoff, ccr);
	off += _emit_MOV(dry_run, &buf[off], CCR, ccr);
	off += _emit_ST(dry_run, &buf[off], ALWAYS);

	return off;
}


/* with excess inital load */
static inline int _aligned_dst(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	int off = 0;
	struct pl330_xfer *x = pxs->x;
	u32 ccr = pxs->ccr;
	unsigned long c, bursts = BYTE_TO_BURST(x->bytes, ccr);
	const u32 align_mask = BRST_SIZE(pxs->ccr)-1;
	const u32 srcoff = (x->src_addr & align_mask);

	/* if 0 burst, return 0 */
	if(bursts == 0)
		return off;
	
	if(!dry_run) PL330_DBGCMD_DUMP(0,"%s: BURSTS COPY. %#x --> %#x %dbytes (%dbyte x %dbursts)\n",
		__FUNCTION__, x->src_addr, x->dst_addr, BURST_TO_BYTE(bursts, pxs->ccr), BURST_TO_BYTE(1, pxs->ccr), bursts);

	/* src Burest + 1 */
	if(bursts > 1 && GET_SRC_BRST_LEN(ccr) < 16) {
		u32 init_ccr = ccr;
		SET_SRC_BRST_LEN( GET_SRC_BRST_LEN(init_ccr)+1, init_ccr);
		off += _emit_MOV(dry_run, &buf[off], CCR, init_ccr);
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
		off += _emit_ST(dry_run, &buf[off], ALWAYS);
		bursts -= 2;
	} else {
		u32 init_ccr = ccr;
		SET_SRC_BRST_LEN( 1, init_ccr);
		off += _emit_MOV(dry_run, &buf[off], CCR, init_ccr);
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
		bursts -= 1;
	}

	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);
	while (bursts) {
		c = bursts;
		off += _loop(dry_run, &buf[off], &c, pxs);
		bursts -= c;
	}

	SET_SRC_BRST_LEN( GET_SRC_BRST_LEN(ccr)-1, ccr);
	off += _emit_MOV(dry_run, &buf[off], CCR, ccr);
	off += _emit_LD(dry_run, &buf[off], ALWAYS);

	ccr = _byte_to_ccr_src(srcoff, ccr);
	off += _emit_MOV(dry_run, &buf[off], CCR, ccr);
	off += _emit_LD(dry_run, &buf[off], ALWAYS);
	off += _emit_ST(dry_run, &buf[off], ALWAYS);

	return off;
}

static inline int _unaligned_both(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	int off = 0;

	// TODO: not implemented
	pr_err("%s : func is not implemented!!!\n", __FUNCTION__);
	BUG();
	return off;
}
		


/* this func is support burst aligned(BS*BL) size transfer*/
static inline int _setup_loops(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = pxs->x;
	u32 ccr = pxs->ccr;
	unsigned long bursts = BYTE_TO_BURST(x->bytes, ccr);
	int off = 0;
	const u32 align_mask = BRST_SIZE(pxs->ccr)-1;
//	const u32 align_size = align_mask+1;
	const u32 srcoff = (x->src_addr & align_mask);
	const u32 dstoff = (x->dst_addr & align_mask);

	
	/* if 0 burst, return 0 */
	if(bursts == 0)
		return off;

	if(!srcoff && !dstoff)
		off += _aligned_both(dry_run, &buf[off], pxs);
	else if(!srcoff && dstoff)
		off += _aligned_src(dry_run, &buf[off], pxs);
	else if(srcoff && !dstoff)
		off += _aligned_dst(dry_run, &buf[off], pxs);
	else
		off += _unaligned_both(dry_run, &buf[off], pxs);

	return off;
}


static inline int _setup_xfer(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = pxs->x;
	int off = 0;
	int remain_byte;
	unsigned long _bursts = 0;

	int cur_copy_bytes = 0;

	if(!dry_run) PL330_DBGCMD_DUMP(0,"%s: BS %d, BL %d, %#x --> %#x %dbytes\n",
		__FUNCTION__, BRST_SIZE(pxs->ccr), BRST_LEN(pxs->ccr), x->src_addr, x->dst_addr, x->bytes);

	/* DMAMOV SAR, x->src_addr */
	off += _emit_MOV(dry_run, &buf[off], SAR, x->src_addr);
	/* DMAMOV DAR, x->dst_addr */
	off += _emit_MOV(dry_run, &buf[off], DAR, x->dst_addr);


#define CONFIG_SDP_DMA330_MAKE_DST_ALIGNED
#ifdef CONFIG_SDP_DMA330_MAKE_DST_ALIGNED/* make dst aligned */
	if(x->dst_addr&(BRST_SIZE(pxs->ccr)-1)) {
		/* if dst addr is unaligned */
		struct _xfer_spec _pxs = *pxs;
		int _dst_off = x->dst_addr&(BRST_SIZE(pxs->ccr)-1);
		u32 _len = BRST_SIZE(pxs->ccr) - _dst_off;

		_len = min(_len, x->bytes);

		_pxs.ccr = _byte_to_ccr_src(PL330_ALIGN, _pxs.ccr);
		_pxs.ccr = _byte_to_ccr_dst(PL330_ALIGN, _pxs.ccr);

if(!dry_run)  PL330_DBGCMD_DUMP(0,
		"%s: Make dst aligned.(dst_off %#x, len %#x, bytes %d, BrstSize %#x)\n",
		__FUNCTION__, _dst_off, _len, x->bytes, BRST_SIZE(pxs->ccr));

		if( !_is_valid(_pxs.ccr) )
		{
			if(!dry_run) pr_err("invalid ccr 0x%x\n", _pxs.ccr);
			return INT_MAX;
		}
		_bursts = BYTE_TO_BURST(_len, _pxs.ccr);
		off += _emit_MOV(dry_run, &buf[off], CCR, _pxs.ccr);
		if(_bursts > 1)
		{
			off += _loop(dry_run, &buf[off], &_bursts, &_pxs);
		}
		else
		{
			off += _emit_LD(dry_run, &buf[off], ALWAYS);
			off += _emit_ST(dry_run, &buf[off], ALWAYS);
		}

		cur_copy_bytes += _len;
	}
#endif/* make dst aligned */

	x->bytes -= cur_copy_bytes;
	x->src_addr += cur_copy_bytes;
	x->dst_addr += cur_copy_bytes;

	/* Setup Loop(s) */
	off += _setup_loops(dry_run, &buf[off], pxs);

	x->bytes += cur_copy_bytes;
	x->src_addr -= cur_copy_bytes;
	x->dst_addr -= cur_copy_bytes;

	cur_copy_bytes += BURST_TO_BYTE(BYTE_TO_BURST(x->bytes - cur_copy_bytes, pxs->ccr), pxs->ccr);
	
	remain_byte = x->bytes - cur_copy_bytes;

	if ( remain_byte )
	{
		struct pl330_xfer _x = *x;
		struct _xfer_spec _pxs = *pxs;
		int align = BRST_SIZE(pxs->ccr);

		_pxs.x = &_x;
		_x.bytes = remain_byte;
		_x.src_addr += cur_copy_bytes;
		_x.dst_addr += cur_copy_bytes;

		/* calc max aligned byte */
		while(align > 1 && ((_x.bytes|_x.src_addr|_x.dst_addr)&(align-1))) {
			align = align >> 1;
		}
		
		_pxs.ccr = _byte_to_ccr_src(align, _pxs.ccr);
		_pxs.ccr = _byte_to_ccr_dst(align, _pxs.ccr);

		off += _aligned_both(dry_run, &buf[off], &_pxs);
	}

	return off;
}

#ifdef CONFIG_SDP_DMA330_2DCOPY
/* 2d xfer */
static inline int _setup_xfer_2d(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = pxs->x;
	int off = 0;
	const int prelength = (x->line_bytes) % (BRST_SIZE(pxs->ccr) * BRST_LEN(pxs->ccr));
//	unsigned long _bursts = 0;
	struct _arg_LPEND lpend = {
		.cond = ALWAYS,
	};
	unsigned lp0cnt, lp1cnt;
	u32 lp0jump, lp1jump;
	int i, cyc;
	u16 height = x->height;

	if (0&&prelength)
	{
		pr_err("current not supported unalign line width(burst byte: %dbyte).\n",
			(BRST_SIZE(pxs->ccr) * BRST_LEN(pxs->ccr)));
		return 0x40000000;
	}

	if(!dry_run) pr_debug("line byte %d, prelength %d\n", x->line_bytes, prelength);


/* max 256 line per step */
while(height > 0)
{

	/* Max iterations possibile in DMALP is 256 */
	if(height >= 256)
	{
		lp1cnt = 256;
		cyc = height/256;
	}	
	else// if(height < 256)
	{
		lp1cnt = height;
		cyc = 1;
	}


	if(prelength != x->line_bytes)
	{
		if(!dry_run) pr_debug("aligned copy\n");
		/* DMAMOV SAR, x->src_addr */
		off += _emit_MOV(dry_run, &buf[off], SAR, x->src_addr+((x->line_bytes+x->src_linespan_bytes)*(x->height-height)));
		/* DMAMOV DAR, x->dst_addr */
		off += _emit_MOV(dry_run, &buf[off], DAR, x->dst_addr+((x->line_bytes+x->dst_linespan_bytes)*(x->height-height)));
		/* DMAMOV CCR */
		off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);
		
		/* line loop */
		off += _emit_LP(dry_run, buf+off, 1, lp1cnt);
		lp1jump = off;


		/* if cyc > 1, multiple line xfer */
		for(i = 0; i < cyc; i++)
		{
			/* one line xfer */
			lp0cnt = BYTE_TO_BURST(x->line_bytes-prelength, pxs->ccr);
			
			off += _emit_LP(dry_run, buf+off, 0, lp0cnt);
			lp0jump = off;
			off += _ldst_memtomem(dry_run, buf+off, pxs, 1);

			lpend.loop = 0;
			lpend.bjump = off - lp0jump;
			off += _emit_LPEND(dry_run, buf+off, &lpend);
			
			off += _emit_ADDH(dry_run, buf+off, SRC, (x->src_linespan_bytes)+prelength);
			off += _emit_ADDH(dry_run, buf+off, DST, (x->dst_linespan_bytes)+prelength);
			/* end one line xfer */
		}

		lpend.loop = 1;
		lpend.bjump = off - lp1jump;
		off += _emit_LPEND(dry_run, buf+off, &lpend);
		/* line loop end */
	}

	// TODO: prelength xfer code
	if(prelength)
	{
		u32 ccr = pxs->ccr;
		
		if(!dry_run) pr_debug("unaligned copy\n");
		
		/* find once xfer value */
		//ccr = _byte_to_ccr_src(prelength, ccr);
		//ccr = _byte_to_ccr_dst(prelength, ccr);
		SET_SRC_BRST_LEN((prelength/(1<<GET_SRC_BRST_SIZE(ccr))), ccr);
		SET_DST_BRST_LEN((prelength/(1<<GET_DST_BRST_SIZE(ccr))), ccr);

		BUG_ON(ccr == -EINVAL);

		/* DMAMOV SAR, x->src_addr */
		off += _emit_MOV(dry_run, &buf[off], SAR, x->src_addr+ x->line_bytes-prelength+((x->line_bytes+x->src_linespan_bytes)*(x->height-height)));
		/* DMAMOV DAR, x->dst_addr */
		off += _emit_MOV(dry_run, &buf[off], DAR, x->dst_addr+ x->line_bytes-prelength+((x->line_bytes+x->dst_linespan_bytes)*(x->height-height)));

		/* DMAMOV CCR */
		off += _emit_MOV(dry_run, &buf[off], CCR, ccr);
		
		/* prelength line loop */
		off += _emit_LP(dry_run, buf+off, 1, lp1cnt);
		lp1jump = off;

		/* if cyc > 1, multiple line xfer */
		for(i = 0; i < cyc; i++)
		{
			/* one prelength line xfer */
			off += _ldst_memtomem(dry_run, buf+off, pxs, 1);

			off += _emit_ADDH(dry_run, buf+off, SRC, (x->src_linespan_bytes)+x->line_bytes-prelength);
			off += _emit_ADDH(dry_run, buf+off, DST, (x->dst_linespan_bytes)+x->line_bytes-prelength);
			/* end one prelength line xfer */
		}

		lpend.loop = 1;
		lpend.bjump = off - lp1jump;
		off += _emit_LPEND(dry_run, buf+off, &lpend);
		/* prelength line loop end */
	}

	height -= lp1cnt*cyc;
	
}

	return off;
}
#endif/*CONFIG_SDP_DMA330_2DCOPY*/

static inline int _setup_req_end(unsigned dry_run, struct pl330_thread *thrd,
		u8 buf[])
{
	int off = 0;

	/* DMAWMB wait for write memory barrier */
	off += _emit_WMB(dry_run, &buf[off]);
	/* DMASEV peripheral/event */
	off += _emit_SEV(dry_run, &buf[off], thrd->ev);
	/* DMAEND */
	off += _emit_END(dry_run, &buf[off]);

	return off;
}


/*
 * A req is a sequence of one or more xfer units.
 * Returns the number of bytes taken to setup the MC for the req.
 */
static int _setup_req(unsigned dry_run, struct pl330_thread *thrd,
		unsigned index, struct _xfer_spec *pxs, enum pl330_reqtype rqtype)
{
	struct _pl330_req *req = &thrd->req[index];
	struct pl330_xfer *x;
	u8 *buf = req->mc_cpu;
	int off = 0;

	PL330_DBGMC_START(req->mc_bus);

	/* DMAMOV CCR, ccr */
//	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);

	x = pxs->r->x;
	do {
		/* Error if xfer length is not aligned at burst size */
//		if (x->bytes % (BRST_SIZE(pxs->ccr) * BRST_LEN(pxs->ccr)))
//			return -EINVAL;

		pxs->x = x;
		if(rqtype == MEMTOMEM)
		{
			off += _setup_xfer(dry_run, &buf[off], pxs);
		}

#ifdef CONFIG_SDP_DMA330_2DCOPY
		else if(rqtype == MEMTOMEM_2D)
		{
			off += _setup_xfer_2d(dry_run, &buf[off], pxs);
		}
#endif /* CONFIG_SDP_DMA330_2DCOPY */

		x = x->next;
	} while (x);

	off += _setup_req_end(dry_run, thrd, &buf[off]);
	
	return off;
}

static inline u32 _prepare_ccr(const struct pl330_reqcfg *rqc)
{
	u32 ccr = 0;

	if (rqc->src_inc)
		ccr |= CC_SRCINC;

	if (rqc->dst_inc)
		ccr |= CC_DSTINC;

	/* We set same protection levels for Src and DST for now */
	if (rqc->privileged)
		ccr |= CC_SRCPRI | CC_DSTPRI;
	if (rqc->nonsecure)
		ccr |= CC_SRCNS | CC_DSTNS;
	if (rqc->insnaccess)
		ccr |= CC_SRCIA | CC_DSTIA;

	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_SRCBRSTLEN_SHFT);
	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_DSTBRSTLEN_SHFT);

	ccr |= (rqc->brst_size << CC_SRCBRSTSIZE_SHFT);
	ccr |= (rqc->brst_size << CC_DSTBRSTSIZE_SHFT);

	ccr |= (rqc->dcctl << CC_DSTCCTRL_SHFT);
	ccr |= (rqc->scctl << CC_SRCCCTRL_SHFT);

	ccr |= (rqc->swap << CC_SWAP_SHFT);

	return ccr;
}



/*
 * Submit a list of xfers after which the client wants notification.
 * Client is not notified after each xfer unit, just once after all
 * xfer units are done or some error occurs.
 */
int pl330_submit_req(void *ch_id, struct pl330_req *r)
{
	struct pl330_thread *thrd = ch_id;
	struct pl330_dmac *pl330;
	struct pl330_info *pi;
	struct _xfer_spec xs;
	unsigned long flags;
	void __iomem *regs;
	unsigned idx;
	u32 ccr;
	int ret = 0;

	/* No Req or Unacquired Channel or DMAC */
	if (!r || !thrd || thrd->free)
		return -EINVAL;

	pl330 = thrd->dmac;
	pi = pl330->pinfo;
	regs = pi->base;

	if (pl330->state == DYING
		|| pl330->dmac_tbd.reset_chan & (1 << thrd->id)) {
		dev_info(thrd->dmac->pinfo->dev, "%s:%d\n",
			__func__, __LINE__);
		return -EAGAIN;
	}

	/* If request for non-existing peripheral */
#ifdef CONFIG_SDP_DMA330_2DCOPY
	if ( !(r->rqtype == MEMTOMEM || r->rqtype == MEMTOMEM_2D) && r->peri >= pi->pcfg.num_peri) {
		dev_info(thrd->dmac->pinfo->dev,
				"%s:%d Invalid peripheral(%u)!\n",
				__func__, __LINE__, r->peri);
		return -EINVAL;
	}
#else
	if ( !(r->rqtype == MEMTOMEM ) && r->peri >= pi->pcfg.num_peri) {
		dev_info(thrd->dmac->pinfo->dev,
				"%s:%d Invalid peripheral(%u)!\n",
				__func__, __LINE__, r->peri);
		return -EINVAL;
	}
#endif

	spin_lock_irqsave(&pl330->lock, flags);

	if (_queue_full(thrd)) {
		ret = -EAGAIN;
		goto xfer_exit;
	}

	/* Prefer Secure Channel */
	if (!_manager_ns(thrd))
		r->cfg->nonsecure = 0;
	else
		r->cfg->nonsecure = 1;

	/* Use last settings, if not provided */
	if (r->cfg)
		ccr = _prepare_ccr(r->cfg);
	else
		ccr = readl(regs + CC(thrd->id));

	/* If this req doesn't have valid xfer settings */
	if (!_is_valid(ccr)) {
		ret = -EINVAL;
		dev_info(thrd->dmac->pinfo->dev, "%s:%d Invalid CCR(%x)!\n",
			__func__, __LINE__, ccr);
		goto xfer_exit;
	}

	idx = IS_FREE(&thrd->req[0]) ? 0 : 1;

	xs.ccr = ccr;
	xs.r = r;

	/* First dry run to check if req is acceptable */
#if 1/* disable, for more fast speed */
	ret = _setup_req(1, thrd, idx, &xs, r->rqtype);
#endif
	if (ret < 0)
		goto xfer_exit;

	if (ret > pi->mcbufsz / 2) {
		dev_info(thrd->dmac->pinfo->dev,
			"%s:%d Trying increasing mcbufsz. mcsize: %d\n",
				__func__, __LINE__, ret);
		ret = -ENOMEM;
		goto xfer_exit;
	}

	/* Hook the request */
	thrd->lstenq = idx;
	thrd->req[idx].mc_len = _setup_req(0, thrd, idx, &xs, r->rqtype);
	thrd->req[idx].r = r;
	dev_dbg(thrd->dmac->pinfo->dev,
		"req_idx:%d, ccr:%#x, mc_addr:0x%p, mc_len:%d\n",
		idx, xs.ccr, thrd->req[idx].mc_cpu, thrd->req[idx].mc_len);

	ret = 0;

xfer_exit:
	spin_unlock_irqrestore(&pl330->lock, flags);

	return ret;
}
EXPORT_SYMBOL(pl330_submit_req);

static void pl330_dotask(unsigned long data)
{
	struct pl330_dmac *pl330 = (struct pl330_dmac *) data;
	struct pl330_info *pi = pl330->pinfo;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pl330->lock, flags);

	/* The DMAC itself gone nuts */
	if (pl330->dmac_tbd.reset_dmac) {
		pl330->state = DYING;
		/* Reset the manager too */
		pl330->dmac_tbd.reset_mngr = true;
		/* Clear the reset flag */
		pl330->dmac_tbd.reset_dmac = false;
	}

	if (pl330->dmac_tbd.reset_mngr) {
		_stop(pl330->manager);
		/* Reset all channels */
		pl330->dmac_tbd.reset_chan = (1 << pi->pcfg.num_chan) - 1;
		/* Clear the reset flag */
		pl330->dmac_tbd.reset_mngr = false;
	}

	for (i = 0; i < pi->pcfg.num_chan; i++) {

		if (pl330->dmac_tbd.reset_chan & (1 << i)) {
			struct pl330_thread *thrd = &pl330->channels[i];
			void __iomem *regs = pi->base;
			enum pl330_op_err err;

			_stop(thrd);

			if (readl(regs + FSC) & (1 << thrd->id))
				err = PL330_ERR_FAIL;
			else
				err = PL330_ERR_ABORT;

			spin_unlock_irqrestore(&pl330->lock, flags);

			_callback(thrd->req[1 - thrd->lstenq].r, err);
			_callback(thrd->req[thrd->lstenq].r, err);

			spin_lock_irqsave(&pl330->lock, flags);

			thrd->req[0].r = NULL;
			thrd->req[1].r = NULL;
			mark_free(thrd, 0);
			mark_free(thrd, 1);

			/* Clear the reset flag */
			pl330->dmac_tbd.reset_chan &= ~(1 << i);
		}
	}

	spin_unlock_irqrestore(&pl330->lock, flags);

	return;
}

/* Returns 1 if state was updated, 0 otherwise */
int pl330_update(const struct pl330_info *pi)
{
	struct _pl330_req *rqdone;
	struct pl330_dmac *pl330;
	unsigned long flags;
	void __iomem *regs;
	u32 val;
	int id, ev, ret = 0;

	if (!pi || !pi->pl330_data)
		return 0;

	regs = pi->base;
	pl330 = pi->pl330_data;

	spin_lock_irqsave(&pl330->lock, flags);

	val = readl(regs + FSM) & 0x1;
	if (val)
		pl330->dmac_tbd.reset_mngr = true;
	else
		pl330->dmac_tbd.reset_mngr = false;

	val = readl(regs + FSC) & ((1 << pi->pcfg.num_chan) - 1);
	pl330->dmac_tbd.reset_chan |= val;
	if (val) {
		int i = 0;
		while (i < pi->pcfg.num_chan) {
			if (val & (1 << i)) {
				dev_info(pi->dev,
					"Reset Channel-%d\t CS-%x FTC-%x\n",
						i, readl(regs + CS(i)),
						readl(regs + FTC(i)));
				_stop(&pl330->channels[i]);
			}
			i++;
		}
	}

	/* Check which event happened i.e, thread notified */
	val = readl(regs + ES);
	if (pi->pcfg.num_events < 32
			&& val & ~((1 << pi->pcfg.num_events) - 1)) {
		pl330->dmac_tbd.reset_dmac = true;
		dev_err(pi->dev, "%s:%d Unexpected!\n", __func__, __LINE__);
		ret = 1;
		goto updt_exit;
	}

	for (ev = 0; ev < pi->pcfg.num_events; ev++) {
		if (val & (1 << ev)) { /* Event occured */
			struct pl330_thread *thrd;
			u32 inten = readl(regs + INTEN);
			int active;

			/* Clear the event */
			if (inten & (1 << ev))
				writel(1 << ev, regs + INTCLR);

			ret = 1;

			id = pl330->events[ev];

			thrd = &pl330->channels[id];

			active = thrd->req_running;
			if (active == -1) /* Aborted */
				continue;

			rqdone = &thrd->req[active];
			mark_free(thrd, active);

			/* Get going again ASAP */
			_start(thrd);

			/* For now, just make a list of callbacks to be done */
			list_add_tail(&rqdone->rqd, &pl330->req_done);
		}
	}

	/* Now that we are in no hurry, do the callbacks */
	while (!list_empty(&pl330->req_done)) {
		struct pl330_req *r;

		rqdone = container_of(pl330->req_done.next,
					struct _pl330_req, rqd);

		list_del_init(&rqdone->rqd);

		/* Detach the req */
		r = rqdone->r;
		rqdone->r = NULL;

		spin_unlock_irqrestore(&pl330->lock, flags);
		_callback(r, PL330_ERR_NONE);
		spin_lock_irqsave(&pl330->lock, flags);
	}

updt_exit:
	spin_unlock_irqrestore(&pl330->lock, flags);

	if (pl330->dmac_tbd.reset_dmac
			|| pl330->dmac_tbd.reset_mngr
			|| pl330->dmac_tbd.reset_chan) {
		ret = 1;
		tasklet_schedule(&pl330->tasks);
	}

	return ret;
}
EXPORT_SYMBOL(pl330_update);

int pl330_chan_ctrl(void *ch_id, enum pl330_chan_op op)
{
	struct pl330_thread *thrd = ch_id;
	struct pl330_dmac *pl330;
	unsigned long flags;
	int ret = 0, active;

	if (!thrd || thrd->free || thrd->dmac->state == DYING)
		return -EINVAL;

	pl330 = thrd->dmac;
	active = thrd->req_running;

	spin_lock_irqsave(&pl330->lock, flags);

	switch (op) {
	case PL330_OP_FLUSH:
		/* Make sure the channel is stopped */
		_stop(thrd);

		thrd->req[0].r = NULL;
		thrd->req[1].r = NULL;
		mark_free(thrd, 0);
		mark_free(thrd, 1);
		break;

	case PL330_OP_ABORT:
		/* Make sure the channel is stopped */
		_stop(thrd);

		/* ABORT is only for the active req */
		if (active == -1)
			break;

		thrd->req[active].r = NULL;
		mark_free(thrd, active);

		/* Start the next */
	case PL330_OP_START:
		if ((active == -1) && !_start(thrd))
			ret = -EIO;
		break;

	default:
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&pl330->lock, flags);
	return ret;
}
EXPORT_SYMBOL(pl330_chan_ctrl);

int pl330_chan_status(void *ch_id, struct pl330_chanstatus *pstatus)
{
	struct pl330_thread *thrd = ch_id;
	struct pl330_dmac *pl330;
	struct pl330_info *pi;
	void __iomem *regs;
	int active;
	u32 val;

	if (!pstatus || !thrd || thrd->free)
		return -EINVAL;

	pl330 = thrd->dmac;
	pi = pl330->pinfo;
	regs = pi->base;

	/* The client should remove the DMAC and add again */
	if (pl330->state == DYING)
		pstatus->dmac_halted = true;
	else
		pstatus->dmac_halted = false;

	val = readl(regs + FSC);
	if (val & (1 << thrd->id))
		pstatus->faulting = true;
	else
		pstatus->faulting = false;

	active = thrd->req_running;

	if (active == -1) {
		/* Indicate that the thread is not running */
		pstatus->top_req = NULL;
		pstatus->wait_req = NULL;
	} else {
		pstatus->top_req = thrd->req[active].r;
		pstatus->wait_req = !IS_FREE(&thrd->req[1 - active])
					? thrd->req[1 - active].r : NULL;
	}

	pstatus->src_addr = readl(regs + SA(thrd->id));
	pstatus->dst_addr = readl(regs + DA(thrd->id));

	return 0;
}
EXPORT_SYMBOL(pl330_chan_status);

/* Reserve an event */
static inline int _alloc_event(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;
	struct pl330_info *pi = pl330->pinfo;
	int ev;

	for (ev = 0; ev < pi->pcfg.num_events; ev++)
		if (pl330->events[ev] == -1) {
			pl330->events[ev] = thrd->id;
			return ev;
		}

	return -1;
}

static bool _chan_ns(const struct pl330_info *pi, int i)
{
	return pi->pcfg.irq_ns & (1 << i);
}


/* Upon success, returns IdentityToken for the
 * allocated channel, NULL otherwise.
 */
void *pl330_request_channel(const struct pl330_info *pi)
{
	struct pl330_thread *thrd = NULL;
	struct pl330_dmac *pl330;
	unsigned long flags;
	int chans, i;

	if (!pi || !pi->pl330_data)
		return NULL;

	pl330 = pi->pl330_data;

	if (pl330->state == DYING)
		return NULL;

	chans = pi->pcfg.num_chan;

	spin_lock_irqsave(&pl330->lock, flags);

	for (i = 0; i < chans; i++) {
		thrd = &pl330->channels[i];
		if ((thrd->free) && (!_manager_ns(thrd) ||
					_chan_ns(pi, i))) {
			thrd->ev = _alloc_event(thrd);
			if (thrd->ev >= 0) {
				thrd->free = false;
				thrd->lstenq = 1;
				thrd->req[0].r = NULL;
				mark_free(thrd, 0);
				thrd->req[1].r = NULL;
				mark_free(thrd, 1);
				break;
			}
		}
		thrd = NULL;
	}

	spin_unlock_irqrestore(&pl330->lock, flags);

	return thrd;
}
EXPORT_SYMBOL(pl330_request_channel);

/* Release an event */
static inline void _free_event(struct pl330_thread *thrd, int ev)
{
	struct pl330_dmac *pl330 = thrd->dmac;
	struct pl330_info *pi = pl330->pinfo;

	/* If the event is valid and was held by the thread */
	if (ev >= 0 && ev < pi->pcfg.num_events
			&& pl330->events[ev] == thrd->id)
		pl330->events[ev] = -1;
}

void pl330_release_channel(void *ch_id)
{
	struct pl330_thread *thrd = ch_id;
	struct pl330_dmac *pl330;
	unsigned long flags;

	if (!thrd || thrd->free)
		return;

	_stop(thrd);

	_callback(thrd->req[1 - thrd->lstenq].r, PL330_ERR_ABORT);
	_callback(thrd->req[thrd->lstenq].r, PL330_ERR_ABORT);

	pl330 = thrd->dmac;

	spin_lock_irqsave(&pl330->lock, flags);
	_free_event(thrd, thrd->ev);
	thrd->free = true;
	spin_unlock_irqrestore(&pl330->lock, flags);
}
EXPORT_SYMBOL(pl330_release_channel);

/* Initialize the structure for PL330 configuration, that can be used
 * by the client driver the make best use of the DMAC
 */
static void read_dmac_config(struct pl330_info *pi)
{
	void __iomem *regs = pi->base;
	u32 val;

	val = readl(regs + CRD) >> CRD_DATA_WIDTH_SHIFT;
	val &= CRD_DATA_WIDTH_MASK;
	pi->pcfg.data_bus_width = 8 * (1 << val);

	val = readl(regs + CRD) >> CRD_DATA_BUFF_SHIFT;
	val &= CRD_DATA_BUFF_MASK;
	pi->pcfg.data_buf_dep = val + 1;

	val = readl(regs + CR0) >> CR0_NUM_CHANS_SHIFT;
	val &= CR0_NUM_CHANS_MASK;
	val += 1;
	pi->pcfg.num_chan = val;

	val = readl(regs + CR0);
	if (val & CR0_PERIPH_REQ_SET) {
		val = (val >> CR0_NUM_PERIPH_SHIFT) & CR0_NUM_PERIPH_MASK;
		val += 1;
		pi->pcfg.num_peri = val;
		pi->pcfg.peri_ns = readl(regs + CR4);
	} else {
		pi->pcfg.num_peri = 0;
	}

	val = readl(regs + CR0);
	if (val & CR0_BOOT_MAN_NS)
		pi->pcfg.mode |= DMAC_MODE_NS;
	else
		pi->pcfg.mode &= ~DMAC_MODE_NS;

	val = readl(regs + CR0) >> CR0_NUM_EVENTS_SHIFT;
	val &= CR0_NUM_EVENTS_MASK;
	val += 1;
	pi->pcfg.num_events = val;

	pi->pcfg.irq_ns = readl(regs + CR3);

	pi->pcfg.periph_id = get_id(pi, PERIPH_ID);
	pi->pcfg.pcell_id = get_id(pi, PCELL_ID);
}

static inline void _reset_thread(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;
	struct pl330_info *pi = pl330->pinfo;

	thrd->req[0].mc_cpu = pl330->mcode_cpu
				+ (thrd->id * pi->mcbufsz);
	thrd->req[0].mc_bus = pl330->mcode_bus
				+ (thrd->id * pi->mcbufsz);
	thrd->req[0].r = NULL;
	mark_free(thrd, 0);

	thrd->req[1].mc_cpu = thrd->req[0].mc_cpu
				+ pi->mcbufsz / 2;
	thrd->req[1].mc_bus = thrd->req[0].mc_bus
				+ pi->mcbufsz / 2;
	thrd->req[1].r = NULL;
	mark_free(thrd, 1);
}

static int dmac_alloc_threads(struct pl330_dmac *pl330)
{
	struct pl330_info *pi = pl330->pinfo;
	int chans = pi->pcfg.num_chan;
	struct pl330_thread *thrd;
	int i;

	/* Allocate 1 Manager and 'chans' Channel threads */
	pl330->channels = kzalloc((1 + chans) * sizeof(*thrd),
					GFP_KERNEL);
	if (!pl330->channels)
		return -ENOMEM;

	/* Init Channel threads */
	for (i = 0; i < chans; i++) {
		thrd = &pl330->channels[i];
		thrd->id = i;
		thrd->dmac = pl330;
		_reset_thread(thrd);
		thrd->free = true;
	}

	/* MANAGER is indexed at the end */
	thrd = &pl330->channels[chans];
	thrd->id = chans;
	thrd->dmac = pl330;
	thrd->free = false;
	pl330->manager = thrd;

	return 0;
}

static int dmac_alloc_resources(struct pl330_dmac *pl330)
{
	struct pl330_info *pi = pl330->pinfo;
	int chans = pi->pcfg.num_chan;
	int ret;

	/*
	 * Alloc MicroCode buffer for 'chans' Channel threads.
	 * A channel's buffer offset is (Channel_Id * MCODE_BUFF_PERCHAN)
	 */

	if(pi->pcfg.mode&DMAC_MODE_NS) {
		/* AXI bus */
		pl330->mcode_cpu = dma_alloc_coherent(pi->dev,
					chans * pi->mcbufsz,
					(dma_addr_t *)&pl330->mcode_bus, GFP_KERNEL);
	} else {
		/* ACP */
		pl330->mcode_cpu = dma_alloc_writecombine(pi->dev,
					chans * pi->mcbufsz,
					(dma_addr_t *)&pl330->mcode_bus, GFP_KERNEL);
	}
	if (!pl330->mcode_cpu) {
		dev_err(pi->dev, "%s:%d Can't allocate memory!\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	dev_dbg(pi->dev, "mcode addr: %p\n", pl330->mcode_cpu);

	ret = dmac_alloc_threads(pl330);
	if (ret) {
		dev_err(pi->dev, "%s:%d Can't to create channels for DMAC!\n",
			__func__, __LINE__);
		dma_free_coherent(pi->dev,
				chans * pi->mcbufsz,
				pl330->mcode_cpu, pl330->mcode_bus);
		return ret;
	}

	return 0;
}

int pl330_add(struct pl330_info *pi)
{
	struct pl330_dmac *pl330;
	void __iomem *regs;
	int i, ret;

	if (!pi || !pi->dev)
		return -EINVAL;

	/* If already added */
	if (pi->pl330_data)
		return -EINVAL;

	/*
	 * If the SoC can perform reset on the DMAC, then do it
	 * before reading its configuration.
	 */
	if (pi->dmac_reset)
		pi->dmac_reset(pi);

	regs = pi->base;

	/* Check if we can handle this DMAC */
	if ((get_id(pi, PERIPH_ID) & 0xfffff) != PERIPH_ID_VAL
	   || get_id(pi, PCELL_ID) != PCELL_ID_VAL) {
		dev_err(pi->dev, "PERIPH_ID 0x%x, PCELL_ID 0x%x !\n",
			get_id(pi, PERIPH_ID), get_id(pi, PCELL_ID));
		return -EINVAL;
	}

	/* Read the configuration of the DMAC */
	read_dmac_config(pi);

	if (pi->pcfg.num_events == 0) {
		dev_err(pi->dev, "%s:%d Can't work without events!\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	pl330 = kzalloc(sizeof(*pl330), GFP_KERNEL);
	if (!pl330) {
		dev_err(pi->dev, "%s:%d Can't allocate memory!\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	/* Assign the info structure and private data */
	pl330->pinfo = pi;
	pi->pl330_data = pl330;

	spin_lock_init(&pl330->lock);

	INIT_LIST_HEAD(&pl330->req_done);

	/* Use default MC buffer size if not provided */
	if (!pi->mcbufsz)
		pi->mcbufsz = MCODE_BUFF_PER_REQ * 2;

	/* Mark all events as free */
	for (i = 0; i < pi->pcfg.num_events; i++)
		pl330->events[i] = -1;

	/* Allocate resources needed by the DMAC */
	ret = dmac_alloc_resources(pl330);
	if (ret) {
		dev_err(pi->dev, "Unable to create channels for DMAC\n");
		kfree(pl330);
		return ret;
	}

	tasklet_init(&pl330->tasks, pl330_dotask, (unsigned long) pl330);

	pl330->state = INIT;

	return 0;
}
EXPORT_SYMBOL(pl330_add);

static int dmac_free_threads(struct pl330_dmac *pl330)
{
	struct pl330_info *pi = pl330->pinfo;
	int chans = pi->pcfg.num_chan;
	struct pl330_thread *thrd;
	int i;

	/* Release Channel threads */
	for (i = 0; i < chans; i++) {
		thrd = &pl330->channels[i];
		pl330_release_channel((void *)thrd);
	}

	/* Free memory */
	kfree(pl330->channels);

	return 0;
}

static void dmac_free_resources(struct pl330_dmac *pl330)
{
	struct pl330_info *pi = pl330->pinfo;
	int chans = pi->pcfg.num_chan;

	dmac_free_threads(pl330);

	dma_free_coherent(pi->dev, chans * pi->mcbufsz,
				pl330->mcode_cpu, pl330->mcode_bus);
}

void pl330_del(struct pl330_info *pi)
{
	struct pl330_dmac *pl330;

	if (!pi || !pi->pl330_data)
		return;

	pl330 = pi->pl330_data;

	pl330->state = UNINIT;

	tasklet_kill(&pl330->tasks);

	/* Free DMAC resources */
	dmac_free_resources(pl330);

	kfree(pl330);
	pi->pl330_data = NULL;
}
EXPORT_SYMBOL(pl330_del);

