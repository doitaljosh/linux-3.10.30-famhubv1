#include_next <linux/idr.h>

#ifndef KDBUS_IDR_H
#define KDBUS_IDR_H

#include <linux/version.h>

//endpoint.c and namespace.c
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp_mask);

#endif
#endif /* KDBUS_IDR_H */
