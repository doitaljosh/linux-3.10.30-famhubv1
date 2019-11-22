#ifndef __T2D_PRINT_H__
#define __T2D_PRINT_H__

#define T2D_PRINT_PREFIX	"[T2D_DBG] "

#define T2D_PRINT_ON	(1)
#define T2D_PRINT_OFF	(0)

#if (CONFIG_T2D_PRINT == 1)
#define t2d_print(onoff, format, args...)	\
	do {					\
		if (onoff)			\
			printk(KERN_INFO T2D_PRINT_PREFIX format, ##args);	\
	} while(0)
#else
#define t2d_print(onoff, format, args...)
#endif
#endif
