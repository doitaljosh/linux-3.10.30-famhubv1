/* arch/arm/mach-sdp/include/plat/sdp_gdma.h
 *
 * Copyright (C) 2012 Samsung Electronics Co. Ltd.
 * Dongseok Lee <drain.lee@samsung.com>
 */

/*
 * 2013/03/22, drain.lee : create gdma driver file.
 */

#ifndef __SDP_GDMA_H__
#define __SDP_GDMA_H__

typedef struct sdp_gdma_platdata {
	int (*init)(struct sdp_gdma_platdata *);	/* board dependent init */

#ifdef CONFIG_SDP_CLOCK_GATING
	/* address of used count */
	int *plat_clk_used_cnt;
	/* clock setting 0Hz */
	int (*plat_clk_gate)(void);
	/* restore clock value */
	int (*plat_clk_ungate)(void);
#endif/* CONFIG_SDP_CLOCK_GATING */
} sdp_gdma_platdata_t;

#endif /*__SDP_GDMA_H__*/

