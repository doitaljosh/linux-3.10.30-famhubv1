/* arch/arm/mach-sdp/include/plat/sdp_gadma.h
 *
 * Copyright (C) 2012 Samsung Electronics Co. Ltd.
 * Dongseok Lee <drain.lee@samsung.com>
 */

/*
 * 2012/12/10, drain.lee : create gadma driver file.
 */

#ifndef __SDP_GADMA_H__
#define __SDP_GADMA_H__

typedef struct sdp_gadma_platdata {
	int (*init)(struct sdp_gadma_platdata *);	/* board dependent init */

#ifdef CONFIG_SDP_CLOCK_GATING
	/* address of used count */
	int *plat_clk_used_cnt;
	/* clock setting 0Hz */
	int (*plat_clk_gate)(void);
	/* restore clock value */
	int (*plat_clk_ungate)(void);
#endif/* CONFIG_SDP_CLOCK_GATING */
} sdp_gadma_platdata_t;

#endif /*__SDP_GADMA_H__*/

