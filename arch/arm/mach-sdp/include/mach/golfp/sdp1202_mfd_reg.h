#ifndef __SDP1202_MFD_REG_H
#define __SDP1202_MFD_REG_H

#ifdef __cplusplus
extern "C" {
#endif

//#include "SamType.h"
//#include "SamChip.h"
//MFD100804
#define XMIF_A_REG_BASE 					(0x18410000)
#define XMIF_A_DDRS_0						(XMIF_A_REG_BASE+0x00)
#define XMIF_B_REG_BASE 					(0x18420000)
#define XMIF_B_DDRS_0						(XMIF_B_REG_BASE+0x00)

#define REG_CLK0				(0x18090944)			// FoxMP0	
#define REG_CLK1				(0x18090948)			// FoxMP0
#define REG_CLK2				(0x10090948)			// FoxAP	
#define REG_CLK3				(0x1A090944)			// FoxMP1	
#define REG_CLK4				(0x1A090948)			// FoxMP1	

#define BIT_CLK_MFC				(0x1<<25) //BIT_CLK_VDEC


//#define GIC_SPI(x)	(32+(x))

#define INT_JPEG			GIC_SPI(3)
#define INT_MFD			GIC_SPI(6)
#define INT_HENC		GIC_SPI(10)


#define MPISR_IRQ_JPEG 3
#define MPISR_IRQ_MFD 6
#define MPISR_IRQ_HEN 10




/*---------------------------------------------------------------------*/
/*                 [ MFC ]                                             */
/*---------------------------------------------------------------------*/


#define MFC_REG_BASE 						(0x18680000)


#if 1
#define REG_MFC_DEC_RESET					(MFC_REG_BASE)  	//decoder h/w module (excluding cpu) reset
#define REG_MFC_MAIL_BOX					(MFC_REG_BASE+0x0004)  	//mail box register
#define	REG_MFC_RISC_HOST_INT			(MFC_REG_BASE+0x0008)  	//risc to host interrupt

#define	REG_MFC_WDTPSV					(MFC_REG_BASE+0x0010)
#define	REG_MFC_WDTCON					(MFC_REG_BASE+0x0014)
#define	REG_MFC_WDTSRC					(MFC_REG_BASE+0x0018)
#define	REG_MFC_WDTCNT					(MFC_REG_BASE+0x001c)
#define	REG_MFC_STC33						(MFC_REG_BASE+0x0020)
#define	REG_MFC_STC32						(MFC_REG_BASE+0x0024)
#define	REG_MFC_STC_OFFSET				(MFC_REG_BASE+0x0028)
#define	REG_MFC_STC33_2					(MFC_REG_BASE+0x002c)


// new register name for USER_DEFINED_REG
#define	REG_MFC_USER_DEFINED(N)			(MFC_REG_BASE+0x0030 + 4*N)

#define	REG_MFC_CODEC_TYPE_0				(MFC_REG_BASE+0x0070)
#define	REG_MFC_CODEC_TYPE_1				(MFC_REG_BASE+0x0074)

#define	REG_MFC_FIRMWARE_STATUS			(MFC_REG_BASE+0x0080)
#define REG_MFC_IC_MF_EN				(MFC_REG_BASE+0x00D4)
#define	REG_MFC_RS_MAX_DRAM_ADDR		(MFC_REG_BASE+0x00D8)
#define	REG_MFC_RS_BUS_TIMEOUT	(MFC_REG_BASE+0x00DC)
#define	REG_MFC_RS_BUS_STATUS	(MFC_REG_BASE+0x00E0)
#define	REG_MFC_RS_BUS_ERR_ADDR	(MFC_REG_BASE+0x00E4)
#define	REG_MFC_RS_IFPCR			(MFC_REG_BASE+0x00E8)
#define	REG_MFC_RS_EXPCR			(MFC_REG_BASE+0x00EC)
#define	REG_MFC_RS_EPCR			(MFC_REG_BASE+0x00F0)
#define	REG_MFC_RS_IFINSN			(MFC_REG_BASE+0x00F4)
#define	REG_MFC_RS_EXINSN			(MFC_REG_BASE+0x00F8)
#define	REG_MFC_RS_CNT			(MFC_REG_BASE+0x00FC)


#define REG_MFC_VI_CONTROL0				(MFC_REG_BASE+0x0100)
#define REG_MFC_VI_STATUS0 				(MFC_REG_BASE+0x0104)
#define REG_MFC_VI_PUNDER_RANGE0			(MFC_REG_BASE+0x0108)
#define REG_MFC_VI_POVER_RANGE0			(MFC_REG_BASE+0x010c)
#define REG_MFC_VI_VBUFF0_INTP			(MFC_REG_BASE+0x0110)
#define REG_MFC_VI_VBUFF0_INTM			(MFC_REG_BASE+0x0114)
#define REG_MFC_VI_VBUFF0_START			(MFC_REG_BASE+0x0118)
#define REG_MFC_VI_VBUFF0_SIZE			(MFC_REG_BASE+0x011c)
#define REG_MFC_VI_VBUFF0_WRPTR			(MFC_REG_BASE+0x0120)
#define REG_MFC_VI_VBUFF0_RDPTR			(MFC_REG_BASE+0x0124)
#define REG_MFC_VI_VBUFF0_PTS33			(MFC_REG_BASE+0x0128)
#define REG_MFC_VI_VBUFF0_PTS32			(MFC_REG_BASE+0x012c)
#define REG_MFC_VI_VBUFF0_DTS33			(MFC_REG_BASE+0x0130)
#define REG_MFC_VI_VBUFF0_DTS32			(MFC_REG_BASE+0x0134)
#define REG_MFC_VI_PES_STREAM_ID0		(MFC_REG_BASE+0x0138)

#define REG_MFC_VI_CONTROL1				(MFC_REG_BASE+0x0140)
#define REG_MFC_VI_STATUS1 				(MFC_REG_BASE+0x0144)
#define REG_MFC_VI_PUNDER_RANGE1			(MFC_REG_BASE+0x0148)
#define REG_MFC_VI_POVER_RANGE1			(MFC_REG_BASE+0x014c)
#define REG_MFC_VI_VBUFF1_INTP			(MFC_REG_BASE+0x0150)
#define REG_MFC_VI_VBUFF1_INTM			(MFC_REG_BASE+0x0154)
#define REG_MFC_VI_VBUFF1_START			(MFC_REG_BASE+0x0158)
#define REG_MFC_VI_VBUFF1_SIZE			(MFC_REG_BASE+0x015c)
#define REG_MFC_VI_VBUFF1_WRPTR			(MFC_REG_BASE+0x0160)
#define REG_MFC_VI_VBUFF1_RDPTR			(MFC_REG_BASE+0x0164)
#define REG_MFC_VI_VBUFF1_PTS33			(MFC_REG_BASE+0x0168)
#define REG_MFC_VI_VBUFF1_PTS32			(MFC_REG_BASE+0x016c)
#define REG_MFC_VI_VBUFF1_DTS33			(MFC_REG_BASE+0x0170)
#define REG_MFC_VI_VBUFF1_DTS32			(MFC_REG_BASE+0x0174)
#define REG_MFC_VI_PES_STREAM_ID1		(MFC_REG_BASE+0x0178)

#define REG_MFC_DM0_CONTROL      			(MFC_REG_BASE+0x0200)  	// control register
#define REG_MFC_DM0_PIC_COUNT    		(MFC_REG_BASE+0x0208) 	// current partition ID A,B,C
#define REG_MFC_DM0_CUR_NAL_CNT			(MFC_REG_BASE+0x020c)  	// remaining byte
#define REG_MFC_DM0_PAR_FLUSH_TIMER		(MFC_REG_BASE+0x0210) 	// 0: disable, otherwise: timer for flushing input fifo
#define REG_MFC_DM0_PAR_BUFFER_THRE		(MFC_REG_BASE+0x0214)
#define REG_MFC_DM0_PAR_INT				(MFC_REG_BASE+0x0218)
#define REG_MFC_DM0_PAR_INT_CONTROL		(MFC_REG_BASE+0x021c)
#define REG_MFC_DM0_WT_D_PTR  			(MFC_REG_BASE+0x0220) 	// write pointer for VES buffer		// hijang : changed
#define REG_MFC_DM0_WT_DSR_PTR			(MFC_REG_BASE+0x0224)  	// write pointer for VES descriptor buffer	// hijang : changed
#define REG_MFC_DM0_RD_D_PTR 				(MFC_REG_BASE+0x0228)  	// read  pointer for VES buffer	// hijang : changed
#define REG_MFC_DM0_RD_DSR_PTR   			(MFC_REG_BASE+0x022c) 	// read  pointer for VES descriptor buffer	// hijang : changed
#define REG_MFC_DM0_DATA_BASE			(MFC_REG_BASE+0x0230)  	// [7:0] VES buffer base, [31:8] VES buffer size 	// hijang : changed
#define REG_MFC_DM0_DSR_BASE				(MFC_REG_BASE+0x0234)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed
#define REG_MFC_DM0_CPB_ADDR_CS			(MFC_REG_BASE+0x0238)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed
#define REG_MFC_DM0_CPB_ZERO_CNT_CS		(MFC_REG_BASE+0x023c)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed

#define REG_MFC_DM1_CONTROL      			(MFC_REG_BASE+0x0240)  	// control register
#define REG_MFC_DM1_STATUS          			(MFC_REG_BASE+0x0244)  	// state register
#define REG_MFC_DM1_PIC_COUNT    		(MFC_REG_BASE+0x0248)  	// current partition ID A,B,C
#define REG_MFC_DM1_CUR_NAL_CNT			(MFC_REG_BASE+0x024c) 	// remaining byte
#define REG_MFC_DM1_PAR_FLUSH_TIMER  	(MFC_REG_BASE+0x0250)  	// 0: disable, otherwise: timer for flushing input fifo
#define REG_MFC_DM1_PAR_BUFFER_THRE		(MFC_REG_BASE+0x0254)
#define REG_MFC_DM1_PAR_INT				(MFC_REG_BASE+0x0258)
#define REG_MFC_DM1_PAR_INT_CONTROL		(MFC_REG_BASE+0x025c)
#define REG_MFC_DM1_WT_D_PTR  			(MFC_REG_BASE+0x0260)  	// write pointer for VES buffer		// hijang : changed
#define REG_MFC_DM1_WT_DSR_PTR			(MFC_REG_BASE+0x0264)  	// write pointer for VES descriptor buffer	// hijang : changed
#define REG_MFC_DM1_RD_D_PTR 				(MFC_REG_BASE+0x0268)  	// read  pointer for VES buffer	// hijang : changed
#define REG_MFC_DM1_RD_DSR_PTR   			(MFC_REG_BASE+0x026c)  	// read  pointer for VES descriptor buffer	// hijang : changed
#define REG_MFC_DM1_DATA_BASE			(MFC_REG_BASE+0x0270)  	// [7:0] VES buffer base, [31:8] VES buffer size 	// hijang : changed
#define REG_MFC_DM1_DSR_BASE				(MFC_REG_BASE+0x0274)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed
#define REG_MFC_DM1_CPB_ADDR_CS			(MFC_REG_BASE+0x0278)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed
#define REG_MFC_DM1_CPB_ZERO_CNT_CS		(MFC_REG_BASE+0x027c)  	// [7:0] VES descriptor buffer base, [31:8] buffer size	// hijang : changed


#define REG_MFC_DPFU_GET_32_BITS       		(MFC_REG_BASE+0x0300)  	// get_bit address
#define REG_MFC_DPFU_GET_N_BITS		(MFC_REG_BASE+0x0304)
#define REG_MFC_DPFU_SHOW_32_BITS 		(MFC_REG_BASE+0x0380)  	// look_bit address

#define REG_MFC_DPFU_FRAME_ADDR			(MFC_REG_BASE+0x0400)  	// VES parsing unit start address
#define REG_MFC_DPFU_FRAME_SIZE			(MFC_REG_BASE+0x0404)  	// VES parsing unit byte count
#define	REG_MFC_DPFU_FRAME_ENABLE		(MFC_REG_BASE+0x0408)  	// VES parsing unit prefetch enable
#define	REG_MFC_DPFU_FRAME_BUF_SEL		(MFC_REG_BASE+0x040c)  	// VES parsing unit buffer size and base
#define	REG_MFC_DPFU_BYTE_ALIGN		(MFC_REG_BASE+0x0410)
#define	REG_MFC_DPFU_NO_EM			(MFC_REG_BASE+0x0414)
#define	REG_MFC_DPFU_RD_BITS			(MFC_REG_BASE+0x0418)


#define REG_MFC_SDPFU_GET_32_BITS       		(MFC_REG_BASE+0x0500)  	// get_bit address
#define REG_MFC_SDPFU_GET_N_BITS		(MFC_REG_BASE+0x0504)
#define REG_MFC_SDPFU_SHOW_32_BITS 		(MFC_REG_BASE+0x0580)  	// look_bit address

#define REG_MFC_DPFU1_FRAME_ADDR			(MFC_REG_BASE+0x0600)  	// VES parsing unit start address
#define REG_MFC_DPFU1_FRAME_SIZE			(MFC_REG_BASE+0x0604)  	// VES parsing unit byte count
#define	REG_MFC_DPFU1_FRAME_ENABLE		(MFC_REG_BASE+0x0608)  	// VES parsing unit prefetch enable
#define	REG_MFC_DPFU1_FRAME_BUF_SEL		(MFC_REG_BASE+0x060c)  	// VES parsing unit buffer size and base
#define	REG_MFC_DPFU1_BYTE_ALIGN		(MFC_REG_BASE+0x0610)
#define	REG_MFC_DPFU1_NO_EM			(MFC_REG_BASE+0x0614)
#define	REG_MFC_DPFU1_RD_BITS			(MFC_REG_BASE+0x0618)
#define	REG_MFC_DPFU_SEL			(MFC_REG_BASE+0x061C)

#define REG_MFC_MC_CONTROL				(MFC_REG_BASE+0x2100)
#define REG_MFC_MC_MAX_BURST_SEL			(MFC_REG_BASE+0x2104)
#define	REG_MFC_MC_DRAMBASE_ADDR_A			(MFC_REG_BASE+0x2108)
#define	REG_MFC_MC_DRAMBASE_ADDR_B			(MFC_REG_BASE+0x210c)
#define	REG_MFC_MC_STATUS				(MFC_REG_BASE+0x2110)
#define	REG_MFC_MC_RS_IBASE				(MFC_REG_BASE+0x2114)
#define	REG_MFC_MC_BASERAM_A(n) 			(MFC_REG_BASE+0x2200+4*n)
#define	REG_MFC_MC_BASERAM_B(n) 			(MFC_REG_BASE+0x2400+4*n)

#define	REG_MFC_STANDARD_SEL			(MFC_REG_BASE+0x0804)
#define	REG_MFC_SL_START				(MFC_REG_BASE+0x0808)
#define	REG_MFC_SL_DONE_INTP			(MFC_REG_BASE+0x080C)
#define	REG_MFC_SL_DONE_INTM			(MFC_REG_BASE+0x0810)
#define	REG_MFC_SL_TIMEOUT			(MFC_REG_BASE+0x0814)
#define	REG_MFC_HSIZE_PX				(MFC_REG_BASE+0x0818)
#define	REG_MFC_VSIZE_PX				(MFC_REG_BASE+0x081C)
#define	REG_MFC_HSIZE_MB				(MFC_REG_BASE+0x0820)
#define	REG_MFC_VSIZE_MB				(MFC_REG_BASE+0x0824)
#define	REG_MFC_FIRST_ROW_MB		(MFC_REG_BASE+0x0828)
#define	REG_MFC_FIRST_COL_MB			(MFC_REG_BASE+0x082C)
#define	REG_MFC_PROFILE				(MFC_REG_BASE+0x0830)
#define	REG_MFC_PICTURE_TYPE			(MFC_REG_BASE+0x0834)
#define	REG_MFC_PICTURE_SCAN			(MFC_REG_BASE+0x0838)
#define	REG_MFC_PICTURE_STRUCT		(MFC_REG_BASE+0x083C)
#define	REG_MFC_FIELD_TYPE			(MFC_REG_BASE+0x0840)
#define	REG_MFC_INTERPOLATION_TYPE	(MFC_REG_BASE+0x0844)
#define	REG_MFC_LF_CONTROL			(MFC_REG_BASE+0x0848)
#define	REG_MFC_LF_ALPHA_OFF			(MFC_REG_BASE+0x084C)
#define	REG_MFC_LF_BETA_OFF			(MFC_REG_BASE+0x0850)

#define	REG_MFC_RLI_FBBI(n)				(MFC_REG_BASE+0x0900+4*n)

//#define	REG_MFC_PIXEL_CACHE_CTRL1	(MFC_REG_BASE+0x0A04)
#define	REG_MFC_PIXEL_CACHE_INVALID	(MFC_REG_BASE+0x0A04)
#define	REG_MFC_MPEG2_FCODE_EXT		(MFC_REG_BASE+0x0A08)
#define	REG_MFC_FOR_REF_BASE_INDEX	(MFC_REG_BASE+0x0B00)
#define	REG_MFC_BACK_REF_BASE_INDEX	(MFC_REG_BASE+0x0B04)
#define	REG_MFC_RECON_BASE_INDEX		(MFC_REG_BASE+0x0B08)
#define	REG_MFC_MV_BASE_INDEX		(MFC_REG_BASE+0x0B0C)
#define	REG_MFC_ACDC_COEF_BASE_INDEX	(MFC_REG_BASE+0x0B10)
//#define	ENC_NUM_SDRAM				(MFC_REG_BASE+0x0B14)

#define	REG_MFC_ROUND_CONTROL		(MFC_REG_BASE+0x0C00)
#define	REG_MFC_QUANT_TYPE			(MFC_REG_BASE+0x0C04)
#define	REG_MFC_USE_ALT_SCAN			(MFC_REG_BASE+0x0C08)
#define	REG_MFC_BACK_REF_BOT_IC		(MFC_REG_BASE+0x0C0C)
#define	REG_MFC_BACK_REF_TOP_IC		(MFC_REG_BASE+0x0C10)
#define	REG_MFC_FOR_REF_BOT_IC		(MFC_REG_BASE+0x0C14)
#define	REG_MFC_FOR_REF_TOP_IC		(MFC_REG_BASE+0x0C18)
#define	REG_MFC_TFF					(MFC_REG_BASE+0x0C1C)
#define	REG_MFC_INTRA_DC_PREDICSION	(MFC_REG_BASE+0x0C20)
#define	REG_MFC_OT_BASE_INDEX		(MFC_REG_BASE+0x0C24)
#define REG_MFC_MR1_MB_POS			(MFC_REG_BASE+0x0C28)
#define REG_MFC_LF1_MB_POS			(MFC_REG_BASE+0x0C2C)
#define REG_MFC_SHARED_MB_ADDR			(MFC_REG_BASE+0x0C30)
#define REG_MFC_SHARED_HW_INT			(MFC_REG_BASE+0x0C34)
#define REG_MFC_LF1_PREFETCH_OFF		(MFC_REG_BASE+0x0C38) // Arun - Added for Firenze
#define REG_MFC_MP_PREFETCH_OFF			(MFC_REG_BASE+0x0C3C) // Arun - Added for Firenze

#define	REG_MFC_Q_MAT8_I_Y(n)			(MFC_REG_BASE+0x1000+4*n)
#define	REG_MFC_Q_MAT8_NI_Y(n)			(MFC_REG_BASE+0x1100+4*n)
#define	REG_MFC_Q_MAT4_I_Y(n)			(MFC_REG_BASE+0x1200+4*n)
#define	REG_MFC_Q_MAT4_I_U(n)			(MFC_REG_BASE+0x1240+4*n)
#define	REG_MFC_Q_MAT4_I_V(n)			(MFC_REG_BASE+0x1280+4*n)
#define	REG_MFC_Q_MAT4_NI_Y(n)			(MFC_REG_BASE+0x12C0+4*n)
#define	REG_MFC_Q_MAT4_NI_U(n)			(MFC_REG_BASE+0x1300+4*n)
#define	REG_MFC_Q_MAT4_NI_V(n)			(MFC_REG_BASE+0x1340+4*n)

#define	REG_MFC_DMA_START				(MFC_REG_BASE+0x1600)
#define	REG_MFC_DMA_DONE_INTP		(MFC_REG_BASE+0x1604)
#define	REG_MFC_DMA_DONE_INTM		(MFC_REG_BASE+0x1608)
#define	REG_MFC_DMA_SRC_BASE_INDEX	(MFC_REG_BASE+0x160C)
#define	REG_MFC_DMA_SRC_MB_POS		(MFC_REG_BASE+0x1610)
#define	REG_MFC_DMA_DST_BASE_INDEX	(MFC_REG_BASE+0x1614)
#define	REG_MFC_DMA_DST_MB_POS		(MFC_REG_BASE+0x1618)
#define	REG_MFC_DMA_BURST_MB_SIZE	(MFC_REG_BASE+0x161C)
#define	REG_MFC_DMA_STATUS			(MFC_REG_BASE+0x1620)
#define	REG_MFC_DMA_CRC_EN			(MFC_REG_BASE+0x1624)
#define	REG_MFC_DMA_LUMA_CRC			(MFC_REG_BASE+0x1628)
#define	REG_MFC_DMA_CHRO_CRC			(MFC_REG_BASE+0x162C)

#define	REG_MFC_VO_MODE				(MFC_REG_BASE+0x1800)
#define	REG_MFC_VO_FIELDID			(MFC_REG_BASE+0x1804)
#define	REG_MFC_VO_DPB_T0			(MFC_REG_BASE+0x1808)
#define	REG_MFC_VO_DPB_B0			(MFC_REG_BASE+0x180C)
#define	REG_MFC_VO_RNG_MAPY0		(MFC_REG_BASE+0x1810)
#define	REG_MFC_VO_RNG_MAPUV0		(MFC_REG_BASE+0x1814)
#define	REG_MFC_VO_RNG_REDUCTION0	(MFC_REG_BASE+0x1818)
#define	REG_MFC_VO_FRAME0			(MFC_REG_BASE+0x181C)
#define	REG_MFC_VO_CHANGED0			(MFC_REG_BASE+0x1820)
#define	REG_MFC_VO_DPB_T1			(MFC_REG_BASE+0x1824)
#define	REG_MFC_VO_DPB_B1			(MFC_REG_BASE+0x1828)
#define	REG_MFC_VO_RNG_MAPY1		(MFC_REG_BASE+0x182C)
#define	REG_MFC_VO_RNG_MAPUV1		(MFC_REG_BASE+0x1830)
#define	REG_MFC_VO_RNG_REDUCTION1	(MFC_REG_BASE+0x1834)
#define	REG_MFC_VO_FRAME1			(MFC_REG_BASE+0x1838)
#define	REG_MFC_VO_CHANGED1			(MFC_REG_BASE+0x183C)
#define	REG_MFC_VO_IRQ_INTP0			(MFC_REG_BASE+0x1840)
#define	REG_MFC_VO_IRQ_INTM0			(MFC_REG_BASE+0x1844)
#define	REG_MFC_VO_IRQ_INTP1			(MFC_REG_BASE+0x1848)
#define	REG_MFC_VO_IRQ_INTM1			(MFC_REG_BASE+0x184C)
#define	REG_MFC_VO_STC				(MFC_REG_BASE+0x1850)
#define	REG_MFC_VO_STC2				(MFC_REG_BASE+0x1854)

// RISC to HOST Channnel Return Interface
#define	REG_MFC_USER_EXT_DEF(n)		(MFC_REG_BASE+0x2000+4*n)
//SoC_D00001893	: add mfd watchdog
#define	MFC_SW_WATCH_DOG_VAL			(MFC_REG_BASE+0x2000)
#define	MFC_SW_WATCH_DOG_CNT			(MFC_REG_BASE+0x2004)
#define	MFC_HOST_CMD_ACK					(MFC_REG_BASE+0x2008)
#define	MFC_DEBUG_HEADERPRINT_CH0		(MFC_REG_BASE+0x2060)	//MFC_20101001
#define	MFC_DEBUG_HEADERPRINT_CH1		(MFC_REG_BASE+0x2064)	//MFC_20101001
#define	MFC_SELECT_DISP_60				(MFC_REG_BASE+0x2068)	//MFD_20110720
#define	MFC_LFD_MODE					(MFC_REG_BASE+0x206c)	// SoC_D00003441
#define	MFC_DEBUG_GLOBALPRINT_CH0		(MFC_REG_BASE+0x2070)
#define	MFC_DEBUG_GLOBALPRINT_CH1		(MFC_REG_BASE+0x2074)

#define MFC_ABNORMALWRAP_MODE           (MFC_REG_BASE+0x208C)
#define MFC_ABNORMALWRAP_CH0_SAVED_WP   (MFC_REG_BASE+0x2090)
#define MFC_ABNORMALWRAP_CH0_SAVED_RP   (MFC_REG_BASE+0x2094)
#define MFC_ABNORMALWRAP_CH1_SAVED_WP   (MFC_REG_BASE+0x2098)
#define MFC_ABNORMALWRAP_CH1_SAVED_RP   (MFC_REG_BASE+0x209C)
//W0000154845
#define MFC_VIR_VO_WP					(MFC_REG_BASE+0x20A4)
#define MFC_VIR_VO_RP					(MFC_REG_BASE+0x20A8)


//Need to check juwon
#define	REG_MFC_MPEG4_FRAME_WIDTH		REG_MFC_HSIZE_PX
#define	REG_MFC_MPEG4_FRAME_HEIGHT		REG_MFC_VSIZE_PX
#define	REG_MFC_MPEG4_ROW_MB_NUM		REG_MFC_HSIZE_MB
#define	REG_MFC_MPEG4_COL_MB_NUM		REG_MFC_VSIZE_MB

//H.264 register
#define 	REG_MFC_H264_REGSPMV						(MFC_REG_BASE+0x4000)	//spatial mode for direct MV	// H264_DIRECT_MODE
#define 	REG_MFC_H264_MBFF_MD						(MFC_REG_BASE+0x4004)	//adpative frame/field flag	// H264_MBAFF_MODE
#define 	REG_MFC_H264_CABAC_INIT_IDC				(MFC_REG_BASE+0x4008)	//CABAC init IDC			// H264_CABAC_CONTEXT
#define 	REG_MFC_H264_VLC_DEC_CNTL				(MFC_REG_BASE+0x400C	)//Exp-Golomb decode control	// H264_EXPG_CONTROL
#define 	REG_MFC_H264_VLC_DEC_DATA				(MFC_REG_BASE+0x4010	)//vlc decoding data			// H264_EXPG_DATA
#define 	REG_MFC_H264_CS_IPMD						(MFC_REG_BASE+0x4014	)//constrained intra mode		// H264_CNSTR_INTRA
#define 	REG_MFC_H264_CRF_FLAG						(MFC_REG_BASE+0x4018)	//indicate reference picture	// H264_REF_PICT
#define 	REG_MFC_H264_ENTROPY_FLAG				(MFC_REG_BASE+0x401C)	//entropy mode				// H264_ENTROPY_MODE
#define 	REG_MFC_H264_CHR_IDX_OFF					(MFC_REG_BASE+0x4020)	//offset for chroma qc		// H264_CR_QP_OFF
#define 	REG_MFC_H264_INIT_QP						(MFC_REG_BASE+0x4024)	//initial qp				// H264_INIT_QP
#define 	REG_MFC_H264_IDX0_SIZE					(MFC_REG_BASE+0x4028)	//idx list0 size				// H264_N_RI_LIST0
#define 	REG_MFC_H264_IDX1_SIZE					(MFC_REG_BASE+0x402C)	//idx list1 size				// H264_N_RI_LIST1
#define 	REG_MFC_H264_PTYPE_L1						(MFC_REG_BASE+0x4030)	//reference picture type of l1	// H264_REF_P_INFO                                 						// 2-> which field is closer
#define 	REG_MFC_H264_WPRED_FLAG					(MFC_REG_BASE+0x4034)	//weighting prediction flag		// H264_WGHT_PRD
#define 	REG_MFC_H264_WPRED_IDC					(MFC_REG_BASE+0x4038)	//weighting prediction IDC		// H264_WGHT_BI_PRD
#define 	REG_MFC_H264_LUM_LOG2_WD				(MFC_REG_BASE+0x403C)	//lum log2 wd				// H264_Y_LOG2_WT_DM
#define 	REG_MFC_H264_CHR_LOG2_WD				(MFC_REG_BASE+0x4040)	//chroma log2 wd			// H264_C_LOG2_WT_DM
#define 	REG_MFC_H264_ERR_CHK_MASK				(MFC_REG_BASE+0x404C)	//Error check mask 			// H264_SL_ERRC_MASK
#define 	REG_MFC_H264_COL_MV_WR_MD				(MFC_REG_BASE+0x4050)
#define 	REG_MFC_H264_FCV_SRAM_BASE				(MFC_REG_BASE+0x5000)	//need 2x32 location for direct mode flist conversion		// H264_FBBI_RLI
#define 	REG_MFC_H264_SF_SRAM_BASE				(MFC_REG_BASE+0x5200)//need 2x32 + 16 location for direct mode scale factor
#define 	REG_MFC_H264_MR_WT_SRAM_BASE			(MFC_REG_BASE+0x5400)//256 location for weight or scale factor


//VC1 register
#define 	REG_MFC_VC1_BITPLANE					(MFC_REG_BASE+0x8000)
#define 	REG_MFC_VC1_BITPLANE_RAW				(MFC_REG_BASE+0x8004)
#define 	REG_MFC_VC1_BITPLANE_BASE_INDEX		(MFC_REG_BASE+0x8008)						// ==> BITPLANE_BASE_INDEX
#define 	REG_MFC_VC1_ROW_MB_NUM_QUO2		(MFC_REG_BASE+0x800C)				// ==> ROW_MB_NUM_QUO2
#define 	REG_MFC_VC1_ROW_MB_NUM_MOD2		(MFC_REG_BASE+0x8010)				// ==> ROW_MB_NUM_MOD2
#define 	REG_MFC_VC1_ROW_MB_NUM_QUO3		(MFC_REG_BASE+0x8014)				// ==> ROW_MB_NUM_QUO3
#define 	REG_MFC_VC1_ROW_MB_NUM_MOD3		(MFC_REG_BASE+0x8018)				// ==> ROW_MB_NUM_MOD3
#define 	REG_MFC_VC1_COL_MB_NUM_QUO2		(MFC_REG_BASE+0x801C)				// ==> COL_MB_NUM_QUO2
#define 	REG_MFC_VC1_COL_MB_NUM_MOD2		(MFC_REG_BASE+0x8020)				// ==> COL_MB_NUM_MOD2
#define 	REG_MFC_VC1_COL_MB_NUM_QUO3		(MFC_REG_BASE+0x8024)				// ==> COL_MB_NUM_QUO3
#define 	REG_MFC_VC1_COL_MB_NUM_MOD3		(MFC_REG_BASE+0x8028)				// ==> COL_MB_NUM_MOD3
#define 	REG_MFC_VC1_AC_CODING_SET_CBCR 		(MFC_REG_BASE+0x802C)
#define 	REG_MFC_VC1_AC_CODING_SET_Y 			(MFC_REG_BASE+0x8030)
#define 	REG_MFC_VC1_DC_CODING_SET			(MFC_REG_BASE+0x8034)
#define 	REG_MFC_VC1_DQUANT					(MFC_REG_BASE+0x8038)
#define 	REG_MFC_VC1_QUANT_EDGE_SEL			(MFC_REG_BASE+0x803C)
#define 	REG_MFC_VC1_PQUANT_VALUE 				(MFC_REG_BASE+0x8040)
#define 	REG_MFC_VC1_ALT_PQUANT				(MFC_REG_BASE+0x8044)
#define 	REG_MFC_VC1_PQINDEX 					(MFC_REG_BASE+0x8048)
#define 	REG_MFC_VC1_HALF_QP					(MFC_REG_BASE+0x804C)
#define 	REG_MFC_VC1_DQBILEVEL					(MFC_REG_BASE+0x8050)
#define 	REG_MFC_VC1_VSTRANS					(MFC_REG_BASE+0x8054)
#define 	REG_MFC_VC1_TTMBF						(MFC_REG_BASE+0x8058)
#define 	REG_MFC_VC1_TRANS_TYPE				(MFC_REG_BASE+0x805C)
#define 	REG_MFC_VC1_MVRANGE					(MFC_REG_BASE+0x8060)
#define 	REG_MFC_VC1_DMVRANGE					(MFC_REG_BASE+0x8064)
#define 	REG_MFC_VC1_MV_MODE					(MFC_REG_BASE+0x8068)
#define 	REG_MFC_VC1_HALF_PEL					(MFC_REG_BASE+0x806C)
#define 	REG_MFC_VC1_4MVSWITCH				(MFC_REG_BASE+0x8070)
#define 	REG_MFC_VC1_MBMODETAB				(MFC_REG_BASE+0x8074)
#define 	REG_MFC_VC1_MVTAB						(MFC_REG_BASE+0x8078)
#define 	REG_MFC_VC1_CBPTAB					(MFC_REG_BASE+0x807C)
#define 	REG_MFC_VC1_2MVBPTAB					(MFC_REG_BASE+0x8080)
#define 	REG_MFC_VC1_4MVBPTAB					(MFC_REG_BASE+0x8084)
#define 	REG_MFC_VC1_FASTUVMC					(MFC_REG_BASE+0x8088)
#define 	REG_MFC_VC1_ANCHOR_PIC_TYPE			(MFC_REG_BASE+0x808C)
#define 	REG_MFC_VC1_SCALEFACTOR				(MFC_REG_BASE+0x8090)
#define 	REG_MFC_VC1_BFRACTION					(MFC_REG_BASE+0x8094)
#define 	REG_MFC_VC1_CONDOVER					(MFC_REG_BASE+0x8098)
#define 	REG_MFC_VC1_REFDIST					(MFC_REG_BASE+0x809C)
#define 	REG_MFC_VC1_NUMREF					(MFC_REG_BASE+0x80A0)
#define 	REG_MFC_VC1_REFFIELD					(MFC_REG_BASE+0x80A4)
#define 	REG_MFC_VC1_BREFDIST					(MFC_REG_BASE+0x80A8)
#define 	REG_MFC_VC1_SYNC_MARKER				(MFC_REG_BASE+0x80AC)
#define 	REG_MFC_VC1_RANGE_RED_EN				(MFC_REG_BASE+0x80B0)
#define 	REG_MFC_VC1_RANGE_RED_TYPE			(MFC_REG_BASE+0x80B4)
#define 	REG_MFC_VC1_BACK_REF_FCM				(MFC_REG_BASE+0x80B8)							// ==> BACK_REF_FCM
#define 	REG_MFC_VC1_FOR_REF_FCM				(MFC_REG_BASE+0x80BC)							// ==> FOR_REF_FCM
#define 	REG_MFC_VC1_EOS_INDEX					(MFC_REG_BASE+0x80C0)		// by woong				// ==> EOS_INDEX_PLUS_1MB
#define 	REG_MFC_VC1_CODEC_CONTROL				(MFC_REG_BASE+0x80CC)		// by woong				// ==> EOS_INDEX_PLUS_1MB
#define		REG_MFC_VC1_SL_ERRC_MASK				(MFC_REG_BASE+0x80D0)		// Arun - For Firenze

//MPEG4 register
#define REG_MFC_MPEG4_SPRITE_ENABLE				(MFC_REG_BASE+0x9000)
#define REG_MFC_MPEG4_DATA_PARTITION				(MFC_REG_BASE+0x9004)
#define REG_MFC_MPEG4_REVERSIBLE_VLC				(MFC_REG_BASE+0x9008)
#define REG_MFC_MPEG4_SPRITE_WARPING_ACCURACY	(MFC_REG_BASE+0x900C)
#define REG_MFC_MPEG4_NO_SPRITE_WARPING_POINT	(MFC_REG_BASE+0x9010)
#define REG_MFC_MPEG4_DC_VLC_THR					(MFC_REG_BASE+0x9014)
#define REG_MFC_MPEG4_FCODE_FOR					(MFC_REG_BASE+0x9018)
#define REG_MFC_MPEG4_FCODE_BACK					(MFC_REG_BASE+0x901C)
#define REG_MFC_MPEG4_HAS_SKIP						(MFC_REG_BASE+0x9020)
#define REG_MFC_MPEG4_ALT_I_AC_CHR_DCT			(MFC_REG_BASE+0x9024)
#define REG_MFC_MPEG4_ALT_I_AC_CHR_DCT_INDEX		(MFC_REG_BASE+0x9028)
#define REG_MFC_MPEG4_ALT_I_AC_LUM_DCT			(MFC_REG_BASE+0x902C)
#define REG_MFC_MPEG4_ALT_I_AC_LUM_DCT_INDEX		(MFC_REG_BASE+0x9030)
#define REG_MFC_MPEG4_ALT_I_DC_DCT					(MFC_REG_BASE+0x9034)
#define REG_MFC_MPEG4_ALT_P_AC_DCT				(MFC_REG_BASE+0x9038)
#define REG_MFC_MPEG4_ALT_P_AC_DCT_INDEX			(MFC_REG_BASE+0x903C)
#define REG_MFC_MPEG4_ALT_P_DC_DCT				(MFC_REG_BASE+0x9040)
#define REG_MFC_MPEG4_ALT_MV						(MFC_REG_BASE+0x9044)
#define REG_MFC_MPEG4_FOR_SCALE					(MFC_REG_BASE+0x9048)
#define REG_MFC_MPEG4_FOR_SCALE_P1				(MFC_REG_BASE+0x904C)
#define REG_MFC_MPEG4_FOR_SCALE_M1				(MFC_REG_BASE+0x9050)
#define REG_MFC_MPEG4_BACK_SCALE					(MFC_REG_BASE+0x9054)
#define REG_MFC_MPEG4_BACK_SCALE_P1				(MFC_REG_BASE+0x9058)
#define REG_MFC_MPEG4_BACK_SCALE_M1				(MFC_REG_BASE+0x905C)
#define REG_MFC_MPEG4_BW_VOP_TYPE					(MFC_REG_BASE+0x9060)
#define REG_MFC_MPEG4_GMC_DMV						(MFC_REG_BASE+0x9064)
#define REG_MFC_MPEG4_QUANT_SCALE					(MFC_REG_BASE+0x9068)
#define REG_MFC_MPEG4_RESYNC_MARKER				(MFC_REG_BASE+0x906C)
#define REG_MFC_MPEG4_FIRST_SLICE					(MFC_REG_BASE+0x9070)
#define REG_MFC_MPEG4_CODEC_CONT					(MFC_REG_BASE+0x9074)
#define REG_MFC_MPEG4_BUILD_413					(MFC_REG_BASE+0x9078)
#define REG_MFC_MPEG4_SP_BASE_INDEX				(MFC_REG_BASE+0x907C)
#define REG_MFC_MPEG4_DBL_FLT_BASE_INDEX			(MFC_REG_BASE+0x9080)
#define REG_MFC_MPEG4_SL_ERRC_MASK				(MFC_REG_BASE+0x908C)

//H.263 register
#define REG_MFC_H263_PLUS_PTYPE		(MFC_REG_BASE+0x9090)	// [0] - 1 : PLUSPTYPE exist , 0 : No PLUSPTYPE
#define REG_MFC_H263_UMV_MODE			(MFC_REG_BASE+0x9094)	// [0] - 1 : Unrestricted Motion Vector mode
#define REG_MFC_H263_AP_MODE			(MFC_REG_BASE+0x9098)	// [0] - 1 : Advanced Prediction Vector mode
#define REG_MFC_H263_AIC_MODE			(MFC_REG_BASE+0x909c)	// [0] - 1 : Advanced Intra Coding mode
#define REG_MFC_H263_MQ_MODE			(MFC_REG_BASE+0x90a0)	// [0] - 1 : Modified Quantization mode

//MPEG2 register
#define 	REG_MFC_MPEG2_FRMPRED_FRMDCT 		(MFC_REG_BASE+0xA000) // [0] frame prediction and frame DCT 0:field or frame 1: only frame pred&DCT
#define 	REG_MFC_MPEG2_CONCEAL_MV 			(MFC_REG_BASE+0xA004) // [0] concealment motion vectors 0: no motion vectors for Intra MB 1: motion vectors are coded for Intra MB
#define 	REG_MFC_MPEG2_FCODE 					(MFC_REG_BASE+0xA008)
#define 	REG_MFC_MPEG2_INTRA_VLC_FORMAT		(MFC_REG_BASE+0xA00C) // [0] Intra VLC format
#define 	REG_MFC_MPEG2_FULL_PEL				(MFC_REG_BASE+0xA010) // [0] 0 : full_pel_forward_vector, 1 : full_pel_backward_vector
#define 	REG_MFC_MPEG2_QSCALE_CODE 			(MFC_REG_BASE+0xA014) // [4:0] quantiser_scale_code in Slice header
#define 	REG_MFC_MPEG2_MB_ADDRESS				(MFC_REG_BASE+0xA018) // Current decoding position [6:0] horizontal [22:16] Vertical
#define 	REG_MFC_MPEG2_STATUS 					(MFC_REG_BASE+0xA01C)
#define		REG_MFC_MPEG2_PADDING_EN			(MFC_REG_BASE+0xA020)
#define		REG_MFC_MPEG2_SL_ERRC_MASK			(MFC_REG_BASE+0xA024)
#define		REG_MFC_MPEG2_DCAR_BASE				(MFC_REG_BASE+0xA028)
#define		REG_MFC_MPEG2_DCAR_START_COL		(MFC_REG_BASE+0xA02C)

//AVS register
#define 	REG_MFC_AVS_SKIP_MODE_FLAG 		(MFC_REG_BASE+0xB000)
#define 	REG_MFC_AVS_PIC_REF_FLAG 		(MFC_REG_BASE+0xB004)
#define 	REG_MFC_AVS_QUANT 				(MFC_REG_BASE+0xB008)
#define 	REG_MFC_AVS_FIXED_QUANT			(MFC_REG_BASE+0xB00C)
#define 	REG_MFC_AVS_SL_WT_FLAG			(MFC_REG_BASE+0xB010)
#define 	REG_MFC_AVS_MB_WT_FLAG 			(MFC_REG_BASE+0xB014)
#define 	REG_MFC_AVS_BW_PIC_STRUCT		(MFC_REG_BASE+0xB018)
#define 	REG_MFC_AVS_SL_ERR_MASK 			(MFC_REG_BASE+0xB01C)
#define 	REG_MFC_AVS_SCALE_F(n)	 		(MFC_REG_BASE+0xB200+4*n)


//RMVB register
#define 	REG_MFC_RV_QUANT			(MFC_REG_BASE+0xB400)
#define 	REG_MFC_RV_OSV_QUANT 		(MFC_REG_BASE+0xB404)
#define 	REG_MFC_RV_FIRST_QUANT 			(MFC_REG_BASE+0xB408)
#define 	REG_MFC_RV_BW_PIC_STRUCT		(MFC_REG_BASE+0xB40C)
#define 	REG_MFC_RV_REF_QUANT			(MFC_REG_BASE+0xB410)
#define 	REG_MFC_RV_FW_RATIO 		(MFC_REG_BASE+0xB414)
#define 	REG_MFC_RV_BW_RATIO		(MFC_REG_BASE+0xB418)
#define 	REG_MFC_RV_SL_ERR_MASK 		(MFC_REG_BASE+0xB41C)
#define 	REG_MFC_RV_SLICE_BITSIZE 			(MFC_REG_BASE+0xB420)


//VP8 register
#define		REG_MFC_VP8_DPFU_FRAME_ENABLE		(MFC_REG_BASE+0xB800)
#define		REG_MFC_VP8_DPFU_FRAME_BUF_SEL		(MFC_REG_BASE+0xB804)
#define		REG_MFC_VP8_DPFU_PAR0_ADDR			(MFC_REG_BASE+0xB808)
#define		REG_MFC_VP8_DPFU_PAR0_SIZE			(MFC_REG_BASE+0xB80C)
#define		REG_MFC_VP8_DPFU_PAR1_ADDR			(MFC_REG_BASE+0xB810)
#define		REG_MFC_VP8_DPFU_PAR1_SIZE			(MFC_REG_BASE+0xB814)
#define		REG_MFC_VP8_DPFU_PAR2_ADDR			(MFC_REG_BASE+0xB818)
#define		REG_MFC_VP8_DPFU_PAR2_SIZE			(MFC_REG_BASE+0xB81C)
#define		REG_MFC_VP8_DPFU_PAR3_ADDR			(MFC_REG_BASE+0xB820)
#define		REG_MFC_VP8_DPFU_PAR3_SIZE			(MFC_REG_BASE+0xB824)
#define		REG_MFC_VP8_DPFU_PAR4_ADDR			(MFC_REG_BASE+0xB828)
#define		REG_MFC_VP8_DPFU_PAR4_SIZE			(MFC_REG_BASE+0xB82C)
#define		REG_MFC_VP8_DPFU_PAR5_ADDR			(MFC_REG_BASE+0xB830)
#define		REG_MFC_VP8_DPFU_PAR5_SIZE			(MFC_REG_BASE+0xB834)
#define		REG_MFC_VP8_DPFU_PAR6_ADDR			(MFC_REG_BASE+0xB838)
#define		REG_MFC_VP8_DPFU_PAR6_SIZE			(MFC_REG_BASE+0xB83C)
#define		REG_MFC_VP8_DPFU_PAR7_ADDR			(MFC_REG_BASE+0xB840)
#define		REG_MFC_VP8_DPFU_PAR7_SIZE			(MFC_REG_BASE+0xB844)
#define		REG_MFC_VP8_SEG_MAP_UP				(MFC_REG_BASE+0xB848)
#define		REG_MFC_VP8_MB_NO_CF_SKIP			(MFC_REG_BASE+0xB84C)
#define		REG_MFC_VP8_SL_ERR_MASK				(MFC_REG_BASE+0xB850)
#define		REG_MFC_VP8_BOOL_INIT				(MFC_REG_BASE+0xB854)
#define		REG_MFC_VP8_BOOL_REQ				(MFC_REG_BASE+0xB858)
#define		REG_MFC_VP8_BOOL_ACK				(MFC_REG_BASE+0xB85C)
#define		REG_MFC_VP8_PROB_SRAM				(MFC_REG_BASE+0xB860)
#define		REG_MFC_VP8_PROB_DATA				(MFC_REG_BASE+0xB864)
#define		REG_MFC_VP8_TOKEN_CNTL				(MFC_REG_BASE+0xB868)
#define		REG_MFC_VP8_TOKEN_MAILBOX			(MFC_REG_BASE+0xB86C)
#define		REG_MFC_VP8_DQ_PARA_REG0			(MFC_REG_BASE+0xB870)
#define		REG_MFC_VP8_DQ_PARA_REG1			(MFC_REG_BASE+0xB874)
#define		REG_MFC_VP8_DQ_PARA_REG2			(MFC_REG_BASE+0xB878)
#define		REG_MFC_VP8_DQ_PARA_REG3			(MFC_REG_BASE+0xB87C)
#define		REG_MFC_VP8_DQ_PARA_REG4			(MFC_REG_BASE+0xB880)
#define		REG_MFC_VP8_DQ_PARA_REG5			(MFC_REG_BASE+0xB884)
#define		REG_MFC_VP8_DQ_PARA_REG6			(MFC_REG_BASE+0xB888)
#define		REG_MFC_VP8_DQ_PARA_REG7			(MFC_REG_BASE+0xB88C)
#define		REG_MFC_VP8_LF_FILTER_TYPE			(MFC_REG_BASE+0xB890)
#define		REG_MFC_VP8_MODE_REF_LF_DELTA_EN	(MFC_REG_BASE+0xB894)
#define		REG_MFC_VP8_SEG_EN					(MFC_REG_BASE+0xB898)
#define		REG_MFC_VP8_MB_SEGMENT_ABS_DELTA	(MFC_REG_BASE+0xB89C)
#define		REG_MFC_VP8_SHARP_LEVEL				(MFC_REG_BASE+0xB8A0)
#define		REG_MFC_VP8_DEF_LF_LEVEL_0			(MFC_REG_BASE+0xB8A4)
#define		REG_MFC_VP8_SEG_FEAT_DATA_0			(MFC_REG_BASE+0xB8A8)
#define		REG_MFC_VP8_SEG_FEAT_DATA_1			(MFC_REG_BASE+0xB8AC)
#define		REG_MFC_VP8_SEG_FEAT_DATA_2			(MFC_REG_BASE+0xB8B0)
#define		REG_MFC_VP8_SEG_FEAT_DATA_3			(MFC_REG_BASE+0xB8B4)
#define		REG_MFC_VP8_REF_LF_DELTA_0			(MFC_REG_BASE+0xB8B8)
#define		REG_MFC_VP8_REF_LF_DELTA_1			(MFC_REG_BASE+0xB8BC)
#define		REG_MFC_VP8_REF_LF_DELTA_2			(MFC_REG_BASE+0xB8C0)
#define		REG_MFC_VP8_REF_LF_DELTA_3			(MFC_REG_BASE+0xB8C4)
#define		REG_MFC_VP8_MODE_LF_DELTA_0			(MFC_REG_BASE+0xB8C8)
#define		REG_MFC_VP8_MODE_LF_DELTA_1			(MFC_REG_BASE+0xB8CC)
#define		REG_MFC_VP8_MODE_LF_DELTA_2			(MFC_REG_BASE+0xB8D0)
#define		REG_MFC_VP8_MODE_LF_DELTA_3			(MFC_REG_BASE+0xB8D4)
#define		REG_MFC_VP8_GLD_FRM_SIGN			(MFC_REG_BASE+0xB8D8)
#define		REG_MFC_VP8_ALT_FRM_SIGN			(MFC_REG_BASE+0xB8DC)
#define		REG_MFC_VP8_FPX						(MFC_REG_BASE+0xB8E0)
#define		REG_MFC_VP8_REFRESH_ENTRP_P			(MFC_REG_BASE+0xB8E4)

/*---------------------------------------------------------------------*/
/*                 [ HEN ]                                             */
/*---------------------------------------------------------------------*/
//#define		HEN_REG_BASE							(0x19000000)

#define 		REG_HEN_SW_RESET						(HEN_REG_BASE)  	
#define		REG_HEN_RISC_HOST_INT				(HEN_REG_BASE+0x0008)  

#define		REG_HEN_USER_DEFINED(N)				(HEN_REG_BASE+0x0030 + 4*N)

#define 		REG_HEN_RISC2HOST_CMD_WR_INDEX		(HEN_REG_BASE + 0x0030)
#define 		REG_HEN_RISC2HOST_CMD_RD_INDEX		(HEN_REG_BASE + 0x0034)
#define 		REG_HEN_HOST2RISC_CMD_INST0			(HEN_REG_BASE + 0x0038)
#define 		REG_HEN_HOST2RISC_CMD_INST1			(HEN_REG_BASE + 0x003C)
#define 		REG_HEN_START_MODE0					(HEN_REG_BASE + 0x0040)
#define 		REG_HEN_START_MODE1					(HEN_REG_BASE + 0x0044)
#define 		REG_HEN_RESOL_INFO0					(HEN_REG_BASE + 0x0048)
#define 		REG_HEN_RESOL_INFO1					(HEN_REG_BASE + 0x004c)
#define 		REG_HEN_ENC_PARAM0					(HEN_REG_BASE + 0x0050)
#define 		REG_HEN_ENC_PARAM1					(HEN_REG_BASE + 0x0054)

#define		REG_HEN_FIRMWARE_STATUS				(HEN_REG_BASE + 0x0080)

#define		REG_HEN_MC_DRAMBASE_ADDR_A			(HEN_REG_BASE + 0x0508)
#define		REG_HEN_MC_DRAMBASE_ADDR_B			(HEN_REG_BASE + 0x050C)
#define		REG_HEN_MC_STATUS					(HEN_REG_BASE + 0x0510)
#define		REG_HEN_RS_IBASE						(HEN_REG_BASE + 0x0514)

#define		REG_HEN_MC_BASERAM_A(n) 				(HEN_REG_BASE + 0x0600+4*n)
#define		REG_HEN_MC_BASERAM_B(n) 				(HEN_REG_BASE + 0x0700+4*n)

#define		REG_HEN_HSIZE_PX						(HEN_REG_BASE + 0x081C)
#define		REG_HEN_PROFILE						(HEN_REG_BASE + 0x0830)
#define		REG_HEN_PICTURE_STRUCT				(HEN_REG_BASE + 0x083C)
#define		REG_HEN_LF_CONTROL					(HEN_REG_BASE + 0x0848)
#define		REG_HEN_LF_ALPHA_OFF					(HEN_REG_BASE + 0x084C)
#define		REG_HEN_LF_BETA_OFF					(HEN_REG_BASE + 0x0850)
#define		REG_HEN_VI_IRQ_INTP					(HEN_REG_BASE + 0x1808)
#define		REG_HEN_VI_IRQ_INTM					(HEN_REG_BASE + 0x180C)

#define		REG_HEN_USER_EXT_DEF(n)				(HEN_REG_BASE + 0x2000+4*n)

#define		REG_HEN_SW_WATCH_DOG_VAL			(HEN_REG_BASE + 0x2000)
#define		REG_HEN_SW_WATCH_DOG_CNT			(HEN_REG_BASE + 0x2004)
#define		REG_HEN_HOST_CMD_ACK				(HEN_REG_BASE + 0x2008)
#define		REG_HEN_ENC_BUF_STATUS0				(HEN_REG_BASE + 0x2010)
#define		REG_HEN_ENC_BUF_STATUS1				(HEN_REG_BASE + 0x2014)
#define		REG_HEN_ENC_FRAME_SIZE				(HEN_REG_BASE + 0x2018)
#define		REG_HEN_ENC_FRAME_TYPE				(HEN_REG_BASE + 0x201c)

#define		REG_HEN_EDFU_SF_EPB_ON_CTRL			(HEN_REG_BASE + 0xC054)
#define		REG_HEN_STR_BF_MODE_CTRL			(HEN_REG_BASE + 0xC05C)

#define		REG_HEN_PIC_TYPE_CTRL				(HEN_REG_BASE + 0xC504)
#define		REG_HEN_B_RECON_WRITE_ON			(HEN_REG_BASE + 0xC508)
#define		REG_HEN_MSLICE_CTRL					(HEN_REG_BASE + 0xC50C)
#define		REG_HEN_MSLICE_MB					(HEN_REG_BASE + 0xC510)
#define		REG_HEN_MSLICE_BYTE					(HEN_REG_BASE + 0xC514)
#define		REG_HEN_CIR_CTRL						(HEN_REG_BASE + 0xC518)
#define		REG_HEN_MAP_FOR_CUR					(HEN_REG_BASE + 0xC51C)
#define		REG_HEN_PADDING_CTRL					(HEN_REG_BASE + 0xC520)
#define		REG_HEN_INT_MASK						(HEN_REG_BASE + 0xC528)
#define		REG_HEN_RC_ROI_SET					(HEN_REG_BASE + 0xC594)
#define		REG_HEN_RC_CONFIG					(HEN_REG_BASE + 0xC5A0)
#define		REG_HEN_RC_FRAME_RATE				(HEN_REG_BASE + 0xC5A4)
#define		REG_HEN_RC_BIT_RATE					(HEN_REG_BASE + 0xC5A8)
#define		REG_HEN_RC_QBOUND					(HEN_REG_BASE + 0xC5AC)
#define		REG_HEN_RC_RPARA						(HEN_REG_BASE + 0xC5B0)
#define		REG_HEN_RC_MB_CTRL					(HEN_REG_BASE + 0xC5B4)

#define		REG_HEN_H264_ENTRP_MODE				(HEN_REG_BASE + 0xD004)
#define		REG_HEN_H264_NUM_OF_REF				(HEN_REG_BASE + 0xD010)
#define		REG_HEN_H264_MDINTER_WEIGHT		(HEN_REG_BASE + 0xD01C)
#define		REG_HEN_H264_MDINTRA_WEIGHT		(HEN_REG_BASE + 0xD020)
#define		REG_HEN_H264_TRANS_8X8_FLAG			(HEN_REG_BASE + 0xD034)

/*---------------------------------------------------------------------*/
/*                 [ JPEG ]													     */
/*---------------------------------------------------------------------*/
#define JPEG_BASE_ADDR(x)									JPEG_REGISTER_BASE_ADDR+((x==0)?0:JPEG_REGISTER_BASE_OFFSET)

#define JPEG_INTR_ADDR(x)									((UInt32)(JPEG_BASE_ADDR(x) + 0x00))
#define JPEG_INTR_MASK_ADDR(x)								((UInt32)(JPEG_BASE_ADDR(x) + 0x04))
#define JPEG_COMMAND_ADDR(x)                               ((UInt32)(JPEG_BASE_ADDR(x) + 0x08))
#define JPEG_START_ADDR(x)                                 ((UInt32)(JPEG_BASE_ADDR(x) + 0x0C))
#define JPEG_DEC_STATUS_ADDR(x)                            ((UInt32)(JPEG_BASE_ADDR(x) + 0x10))
#define JPEG_DEC_SOS_INFO_ADDR(x)                          ((UInt32)(JPEG_BASE_ADDR(x) + 0x14))
#define JPEG_DEC_MCU_INDEX_ADDR(x)                         ((UInt32)(JPEG_BASE_ADDR(x) + 0x18))
#define JPEG_DEC_MAIN_MARK_ADDR(x)                         ((UInt32)(JPEG_BASE_ADDR(x) + 0x1c))
#define JPEG_DEC_SOF_INFO_ADDR(x)                          ((UInt32)(JPEG_BASE_ADDR(x) + 0x20))
#define JPEG_DEC_IMG_SIZE_ADDR(x)                          ((UInt32)(JPEG_BASE_ADDR(x) + 0x24))
#define JPEG_DEC_CS_MARK_ADDR(x)                           ((UInt32)(JPEG_BASE_ADDR(x) + 0x28))
#define JPEG_DEC_ERR_CODE_ADDR(x)                          ((UInt32)(JPEG_BASE_ADDR(x) + 0x2C))
#define JPEG_DEC_COMP0_INFO_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x30))
#define JPEG_DEC_COMP1_INFO_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x34))
#define JPEG_DEC_COMP2_INFO_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x38))
#define JPEG_DEC_COMP3_INFO_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x3C))
#define JPEG_DEC_JFIF_INFO0_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x40))
#define JPEG_DEC_JFIF_INFO1_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x44))
#define JPEG_DEC_ADOBE_INFO0_ADDR(x)                       ((UInt32)(JPEG_BASE_ADDR(x) + 0x48))
#define JPEG_DEC_ADOBE_INFO1_ADDR(x)						((UInt32)(JPEG_BASE_ADDR(x) + 0x4C))
#define JPEG_DEC_BUFF_START_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x50))
#define JPEG_DEC_BUFF_SIZE_ADDR(x)                         ((UInt32)(JPEG_BASE_ADDR(x) + 0x54))
#define JPEG_DEC_BUFF_RDPTR_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x58))
#define JPEG_DEC_MEM_WR_SET_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0x5C))
#define JPEG_DEC_COMP0_BADDR_ADDR(x)                       ((UInt32)(JPEG_BASE_ADDR(x) + 0x60))
#define JPEG_DEC_COMP1_BADDR_ADDR(x)                       ((UInt32)(JPEG_BASE_ADDR(x) + 0x64))
#define JPEG_DEC_COMP2_BADDR_ADDR(x)                       ((UInt32)(JPEG_BASE_ADDR(x) + 0x68))
#define JPEG_DEC_COMP3_BADDR_ADDR(x)                       ((UInt32)(JPEG_BASE_ADDR(x) + 0x6C))
#define JPEG_DEC_COMP01_WR_LINE_OFFSET_ADDR(x)             ((UInt32)(JPEG_BASE_ADDR(x) + 0x70))
#define JPEG_DEC_COMP23_WR_LINE_OFFSET_ADDR(x)             ((UInt32)(JPEG_BASE_ADDR(x) + 0x74))
#define JPEG_DEC_COMP01_LINE_OFFSET_ADDR(x)                ((UInt32)(JPEG_BASE_ADDR(x) + 0x78))
#define JPEG_DEC_COMP23_LINE_OFFSET_ADDR(x)                ((UInt32)(JPEG_BASE_ADDR(x) + 0x7C))
#define JPEG_DEC_COMP01_INTERLEAVE_BLOCK_XSIZE_ADDR(x)     ((UInt32)(JPEG_BASE_ADDR(x) + 0x80))
#define JPEG_DEC_COMP23_INTERLEAVE_BLOCK_XSIZE_ADDR(x)     ((UInt32)(JPEG_BASE_ADDR(x) + 0x84))
#define JPEG_DEC_COMP01_INTERLEAVE_BLOCK_YSIZE_ADDR(x)     ((UInt32)(JPEG_BASE_ADDR(x) + 0x88))
#define JPEG_DEC_COMP23_INTERLEAVE_BLOCK_YSIZE_ADDR(x)     ((UInt32)(JPEG_BASE_ADDR(x) + 0x8C))
#define JPEG_DEC_COMP0_PROGRESSIVE_ADDR(x)                 ((UInt32)(JPEG_BASE_ADDR(x) + 0x90))
#define JPEG_DEC_COMP1_PROGRESSIVE_ADDR(x)                 ((UInt32)(JPEG_BASE_ADDR(x) + 0x94))
#define JPEG_DEC_COMP2_PROGRESSIVE_ADDR(x)                 ((UInt32)(JPEG_BASE_ADDR(x) + 0x98))
#define JPEG_DEC_COMP3_PROGRESSIVE_ADDR(x)                 ((UInt32)(JPEG_BASE_ADDR(x) + 0x9C))
#define JPEG_DEC_SEARCH_MAX_COUNT_ADDR(x)                  ((UInt32)(JPEG_BASE_ADDR(x) + 0xA0))
#define JPEG_DEC_MCU_STOP_ADDR(x)                          ((UInt32)(JPEG_BASE_ADDR(x) + 0xA4))
#define JPEG_DEC_PROC_DELAY_EN_ADDR(x)                     ((UInt32)(JPEG_BASE_ADDR(x) + 0xA8))
#define JPEG_DEC_PROC_DELAY_VALUE_ADDR(x)                  ((UInt32)(JPEG_BASE_ADDR(x) + 0xAC))
#define JPEG_DEC_ENDIAN_SET_ADDR(x)                        ((UInt32)(JPEG_BASE_ADDR(x) + 0xB0))
#define JPEG_DEC_EXTENSION_ADDR(x)                         ((UInt32)(JPEG_BASE_ADDR(x) + 0xB4))
#define JPEG_DEC_ROI_DEC_ON_ADDR(x)	                    ((UInt32)(JPEG_BASE_ADDR(x) + 0xB8))
#define JPEG_DEC_ROI_DEC_POS_ADDR(x)			((UInt32)(JPEG_BASE_ADDR(x) + 0xBC))		// occam flamengo
#define JPEG_DEC_ROI_DEC_POS_END_ADDR(x)		((UInt32)(JPEG_BASE_ADDR(x) + 0xC0))		// occam flamengo
#define JPEG_DEC_DOWN_SCALE_ADDR(x)				((UInt32)(JPEG_BASE_ADDR(x) + 0xC4))		// occam flamengo
#define JPEG_DEC_TIMEOUT_ADDR(x)							((UInt32)(JPEG_BASE_ADDR(x) + 0xC8))		// jinchen.kim to keep commonize SamJpeg.h

#ifdef __cplusplus
}
#endif

#endif
#endif /*__SDP1202_MFD_REG_H*/


