/******************************************************************
* 		File : sdp_module_linker.h
*		Description : 
*		Author : tukho.kim@samsung.com		
*******************************************************************/

/******************************************************************
* 17/June/2014, working verison, tukho.kim, created
*******************************************************************/

#ifndef __SDP_MODULE_LINKER_H__
#define __SDP_MODULE_LINKER_H__

#include <linux/types.h>


#define SDP_MLINKER_MUTEX	(1 << 31)

//#define SDP_MLINKER_INVOKE	(0 << 30)
//#define SDP_MLINKER_STATUS	(1 << 30)



typedef void *	sdp_mlinker_hndl;


extern int sdp_register_mlinker(const u32 	attr, 			// attribute
								const void *function, 		// function pointer
								const char *fmt,			// function format
								const char *name);			// block name - mfd, uddec, or chip name

#define sdp_register_mlinker_normal(fmt, function, name) \
		sdp_register_mlinker(0, fmt, function, name)
#define sdp_register_mlinker_mutex(fmt, function, name) \
		sdp_register_mlinker(SDP_MLINKER_MUTEX, fmt, function, name)

extern void sdp_unregister_mlinker(const void *function);

extern int sdp_invoke_mlinker(sdp_mlinker_hndl *phndl,
								const char *name, 
								const char *fmt,
								...) __attribute__ ((format (printf, 3, 4)));

extern void sdp_inform_mlinker(const sdp_mlinker_hndl hndl);


#endif /* __SDP_MODULE_LINKER_H__ */
