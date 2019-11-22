#ifndef __SDP_HWMEM_IO_H__
#define __SDP_HWMEM_IO_H__

#define CMD_HWMEM_DDR_CHECK	(0x35)

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

#endif
