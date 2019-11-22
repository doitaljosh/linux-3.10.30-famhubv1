/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * block.c
 */

/*
 * This file implements the low-level routines to read and decompress
 * datablocks and metadata blocks.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"
#include "debug_print.h"

/*
 * Read the metadata block length, this is stored in the first two
 * bytes of the metadata block.
 */
static struct buffer_head *get_block_length(struct super_block *sb,
			u64 *cur_index, int *offset, int *length)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	struct buffer_head *bh;
	int is_gzip = (msblk->decompressor &&
		       msblk->decompressor->id == GZIP_COMPRESSION);
	int align = (is_gzip ? 8 : 2);
	int shift = 0;

	*length = 0;

	bh = sb_bread(sb, *cur_index);
	if (!bh)
		return NULL;

	for (; align; align--) {
		if (shift < 16) {
			/* How cares about endianness? Nobody. */
			*length |= (unsigned char) bh->b_data[*offset] << shift;
			shift += 8;
		}
		if (msblk->devblksize == ++*offset) {
			put_bh(bh);
			*offset = 0;
			bh = sb_bread(sb, ++*cur_index);
			if (!bh)
				return NULL;
		}
	}

	return bh;
}


/*
 * Read and decompress a metadata block or datablock.  Length is non-zero
 * if a datablock is being read (the size is stored elsewhere in the
 * filesystem), otherwise the length is obtained from the first two bytes of
 * the metadata block.  A bit in the length field indicates if the block
 * is stored uncompressed in the filesystem (usually because compression
 * generated a larger block - this does occasionally happen with compression
 * algorithms).
 */
int squashfs_read_data(struct super_block *sb, void **buffer, u64 index,
			int length, u64 *next_index, int srclength, int pages)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	struct buffer_head **bh;
	int offset = index & ((1 << msblk->devblksize_log2) - 1);
	u64 cur_index = index >> msblk->devblksize_log2;
	int bytes, compressed = 0, b = 0, k = 0, page = 0, avail;
	int is_metablock = 0;
	int decompress_err = 0;

	/* Debug state */
	struct debug_print_state d_state = {
		.sb = sb,
		.index = index,
		.srclength = srclength,
		.next_index = next_index,
		.__cur_index = cur_index,
		.__offset    = offset,
		.__length    = length,
		.get_block_length = get_block_length
	};

	int is_gzip = (msblk->decompressor &&
		       msblk->decompressor->id == GZIP_COMPRESSION);

	/* gzip decompressor requires 8byte alignment */
	int align = (is_gzip ? 8 : 2);

#ifdef CONFIG_SQUASHFS_HW2
	int (*do_hw)(struct block_device *bdev,
		     const struct hw_iovec *vec,
		     unsigned int vec_cnt,
		     struct page **out_pages,
		     unsigned int out_cnt) = NULL;
	const unsigned int hw_pages_cnt = 32;

	const struct block_device_operations *ops = sb->s_bdev->bd_disk->fops;
	do_hw = (is_gzip && hw_decompressor == HW2_DECOMPRESSOR && ops ?
		 ops->hw_decompress_vec : NULL);
	if (do_hw) {
		/* Strong HW requirements */
		if (msblk->block_size != (hw_pages_cnt << PAGE_SHIFT) ||
		    msblk->devblksize_log2 != PAGE_SHIFT) {
			static int log_limit;
			if (!log_limit) {
				ERROR("%s: HW decompressor requirements are "
				      "not satisfied\n", __func__);
				log_limit = 1;
			}
			do_hw = NULL;
		}
	}
#endif

	bh = kcalloc(((srclength + msblk->devblksize - 1)
		>> msblk->devblksize_log2) + 1, sizeof(*bh), GFP_KERNEL);
	if (bh == NULL)
		return -ENOMEM;

	if (length) {
		/*
		 * Datablock.
		 */
		bytes = -offset;
		compressed = SQUASHFS_COMPRESSED_BLOCK(length);
		length = SQUASHFS_COMPRESSED_SIZE_BLOCK(length);
		if (next_index)
			*next_index = index + length;

		TRACE("Block @ 0x%llx, %scompressed size %d, src size %d\n",
			index, compressed ? "" : "un", length, srclength);

		if (length < 0 || length > srclength ||
				(index + length) > msblk->bytes_used)
			goto read_failure;

		/* Prepare buffers in case of software decompression or
		   uncompressed block */
#ifdef CONFIG_SQUASHFS_HW2
read_data:
		if (!do_hw || !compressed)
#endif
		{
			for (b = 0; bytes < length; b++, cur_index++) {
				bh[b] = sb_getblk(sb, cur_index);
				if (bh[b] == NULL)
					goto read_failure;
				bytes += msblk->devblksize;
			}
			ll_rw_block(READ, b, bh);
		}
	} else {
		/*
		 * Metadata block.
		 */
		is_metablock = 1;
		if ((index + align) > msblk->bytes_used)
			goto read_failure;

		bh[0] = get_block_length(sb, &cur_index, &offset, &length);
		if (bh[0] == NULL)
			goto read_failure;
		b = 1;

		bytes = msblk->devblksize - offset;
		compressed = SQUASHFS_COMPRESSED(length);
		length = SQUASHFS_COMPRESSED_SIZE(length);
		if (next_index)
			*next_index = index + length + align;

		TRACE("Block @ 0x%llx, %scompressed size %d\n", index,
				compressed ? "" : "un", length);

		if (length < 0 || length > srclength ||
					(index + length) > msblk->bytes_used)
			goto read_failure;

		for (; bytes < length; b++) {
			bh[b] = sb_getblk(sb, ++cur_index);
			if (bh[b] == NULL)
				goto read_failure;
			bytes += msblk->devblksize;
		}
		ll_rw_block(READ, b - 1, bh + 1);
	}

	if (compressed) {
#ifdef CONFIG_SQUASHFS_HW2
		/* Hardware decompression only for data block */
		if (do_hw && !is_metablock) {
			int err, i;
			struct page *out_pages[pages];
			struct hw_iovec iovec = {
				.phys_off = index,
				.len = length
			};

			/* Strong HW requirements */
			BUG_ON(msblk->block_size != 128<<10);
			BUG_ON(msblk->devblksize_log2 != 12);

			for (i = 0; i < pages; ++i) {
				BUG_ON(!virt_addr_valid(buffer[i]));
				out_pages[i] = virt_to_page(buffer[i]);
			}

			err = do_hw(sb->s_bdev, &iovec, 1, out_pages, pages);
			/* Hardware is busy. Do software decompression */
			if (err == -EBUSY) {
				do_hw = NULL;
				goto read_data;
			} else if (err < 0)
				goto read_failure;

			length = err;
		}
		/* Do software decompression */
		else
#endif
		{
			length = squashfs_decompress(msblk, buffer, bh, b,
						     offset, length, srclength,
						     pages);

			if (length < 0) {
				decompress_err = 1;
				goto read_failure;
			}
		}
	} else {
		/*
		 * Block is uncompressed.
		 */
		int i, in, pg_offset = 0;

		for (i = 0; i < b; i++) {
			wait_on_buffer(bh[i]);
			if (!buffer_uptodate(bh[i]))
				goto read_failure;
		}

		for (bytes = length; k < b; k++) {
			in = min(bytes, msblk->devblksize - offset);
			bytes -= in;
			while (in) {
				if (pg_offset == PAGE_CACHE_SIZE) {
					page++;
					pg_offset = 0;
				}
				avail = min_t(int, in, PAGE_CACHE_SIZE -
						pg_offset);
				memcpy(buffer[page] + pg_offset,
						bh[k]->b_data + offset, avail);
				in -= avail;
				pg_offset += avail;
				offset += avail;
			}
			offset = 0;
			put_bh(bh[k]);
		}
	}

	for (; k < b; k++)
		if (bh[k])
			put_bh(bh[k]);

	kfree(bh);
	return length;

read_failure:
	ERROR("squashfs_read_data failed to read block 0x%llx\n",
					(unsigned long long) index);

	/* We are interested only in decompress errors */
	if (decompress_err && length != -EIO) {
		d_state.bh = bh;
		d_state.b = b;
		d_state.block_type = is_metablock;
		d_state.compressed = compressed;
		/* Print everything we can print in case of decompress error */
		debug_print(&d_state);
	}

	for (; k < b; k++)
		if (bh[k])
			put_bh(bh[k]);

	kfree(bh);
	return -EIO;
}
