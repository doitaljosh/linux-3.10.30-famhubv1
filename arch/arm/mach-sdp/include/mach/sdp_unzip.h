/*
 * linux/arch/arm/mach-sdp/sdp_unzip.h
 *
 * Copyright (C) 2012 Samsung Electronics.co
 * Author : seunjgun.heo@samsung.com
 *
 * 2014/03/6, roman.pen: sync/async decompression and refactoring
 * 2014/04/15,roman.pen: allocation/deallocation of decompression buffer
 *
 */
#ifndef __SDP_UNZIP_H
#define __SDP_UNZIP_H

/* Max HW input buffer size for compressed data
 *   128k + one page for alignment
 */
#define HW_MAX_IBUFF_SZ    (33 << PAGE_SHIFT)
/* Max number of simultaneous decompression threads */
#define HW_MAX_SIMUL_THR    2

/**
 * sdp_unzip_init - decompress gzip file
 * @ibuff : input buffer pointer. must be aligned QWORD(8bytes)
 * @ilength : input buffer pointer. must be multiple of 8.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 */
int sdp_unzip_init(void);

/**
 * sdp_unzip_buf - buffer for sdp needs
 */
struct sdp_unzip_buf {
	void       *vaddr;
	dma_addr_t  paddr;
	size_t      size;
	size_t      __sz;
};

/**
 * sdp_unzip_alloc - allocates buffer for input compressed data
 *
 * Note:
 *   this buffer should be used only for sync/async decompression
 */
struct sdp_unzip_buf *sdp_unzip_alloc(size_t len);

/**
 * sdp_unzip_alloc - frees previously allocated buffer
 */
void sdp_unzip_free(struct sdp_unzip_buf *buf);

/* Completion callback for async mode */
typedef void (*sdp_unzip_cb_t)(int err, int decompressed, void *arg);

/**
 * sdp_unzip_decompress_async - decompress gzip block asynchronously
 * @buff : input buffer pointer. must be aligned 64bytes
 * @off : offset inside buffer to start decompression from
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @cb: completion callback
 * @arg: completion argument
 * @may_wait : return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
int sdp_unzip_decompress_async(struct sdp_unzip_buf *buf, int off,
			       struct page **opages, int npages,
			       sdp_unzip_cb_t cb, void *arg, bool may_wait);

/* Kick async decompressor to finish */
void sdp_unzip_update_endpointer(void);

/**
 * sdp_unzip_decompress_wait - waits for decompressor
 *
 * Return:
 *   < 0   - error
 *   other - decompressed bytes
 */
int sdp_unzip_decompress_wait(void);

/**
 * sdp_unzip_decompress_sync - decompress gzip block synchronously
 * @ibuff : input buffer pointer. must be aligned 64bytes
 * @ilength : input buffer pointer. must be multiple of 64.
 * @opages: array of output buffer pages. page size = 4K(fixed)
 * @npages: number of output buffer pages. maximum number is 32.
 * @may_wait: return -EBUSY if cannot wait but decompressor is busy
 * NOTE: sync and async functions can't be used simultaneously
 */
int sdp_unzip_decompress_sync(void *ibuff, int ilength, struct page **opages,
			      int npages, bool may_wait);

#endif
