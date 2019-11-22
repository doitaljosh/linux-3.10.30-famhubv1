/*
 * HW decompression extension for MMC block device
 */

#include <linux/blkdev.h>

/* Vector decompression */
extern int hw_decompress_fn(struct block_device *bdev,
			    const struct hw_iovec *vec,
			    unsigned int vec_cnt,
			    struct page **out_pages,
			    unsigned int out_cnt);
