/*********************************************************************************************
 *
 *	sdp_hwmem_io.h (Samsung Soc DMA memory allocation)
 *
 *	author : tukho.kim@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
// Jul,09,2010 	tukho.kim	created
 ********************************************************************************************/

#ifndef __SDP_HWMEM_IO_H__
#define __SDP_HWMEM_IO_H__

#include <linux/interrupt.h>

#define CMD_HWMEM_GET		(0x11)
#define CMD_HWMEM_RELEASE	(0x12)

//#define CMD_HWMEM_INV		(0x21)	 
#define CMD_HWMEM_CLEAN		(0x22)
#define CMD_HWMEM_FLUSH		(0x23)

#define CMD_HWMEM_INV_RANGE			(0x28)
#define CMD_HWMEM_CLEAN_RANGE		(0x29)
#define CMD_HWMEM_FLUSH_RANGE		(0x2A)

#define CMD_HWMEM_GET_REVISION_ID	(0x2B)

#define CMD_HWMEM_SET_CLOCKGATING	(0x2C)

#define CMD_HWMEM_DDR_CHECK			(0x35)


typedef struct sdp_get_hwmem_t {
	int			node;	
	int			uncached;
	size_t		size;

// return value
	unsigned long 	phy_addr;
	unsigned long 	vir_addr;
}SDP_GET_HWMEM_T;

typedef struct sdp_clk_hwmem_T	{
	unsigned long	phy_addr;
	unsigned long	mask;
	unsigned long	value;
}SDP_CLKGATING_HWMEM_T;

typedef struct sdp_rel_hwmem_t {
	unsigned long 	phy_addr;
	unsigned long 	vir_addr;
	size_t		size;
}SDP_REL_HWMEM_T;

enum sdp_ddr
{
	SDP_DDR_0,
	SDP_DDR_1,
	SDP_DDR_2,
	SDP_DDR_MAX,
};

typedef enum
{
	SDP_DDR_CHECK_0 = (1 << SDP_DDR_0),
	SDP_DDR_CHECK_1 = (1 << SDP_DDR_1),
	SDP_DDR_CHECK_2 = (1 << SDP_DDR_2),
} sdp_ddrtype_e;

typedef struct
{
	int sdp_ddr0_check_result;      ///< DDR_0 check result. [OK:1/NG:0] By default, it uses only. 
	int sdp_ddr1_check_result;      ///< DDR_1 check result. [OK:1/NG:0] By default, it uses only. 
	int sdp_ddr2_check_result;      ///< DDR_2 check result. [OK:1/NG:0] By default, it uses only. 
	uint32_t counter;
}sdp_ddrcheck_res_t; 

typedef struct sdp_hwmem_ddr_check_t {
	uint32_t test_time;
	uint32_t test_size;
	uint32_t write_pattern;
	sdp_ddrtype_e ddr_type;
	sdp_ddrcheck_res_t ddr_return;	
}SDP_HWMEM_DDR_CHECK_T;

typedef SDP_REL_HWMEM_T SDP_CACHE_HWMEM_T;

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);
extern int sdp_pmu_regset(void* reg_addr, u32 mask, u32 value);
extern void sdp_gpu_hwmem_inv_range(const void* vir_addr, const size_t size);
extern void sdp_gpu_hwmem_clean_range(const void* vir_addr, const size_t size);
extern void sdp_gpu_hwmem_flush_range(const void* vir_addr, const size_t size);
extern void sdp_hwmem_flush_all(void);
extern void sdp_irq_set_affinity(unsigned int irq, const struct cpumask *cpumask);


#endif /* __SDP_HWMEM_IO_H__ */

