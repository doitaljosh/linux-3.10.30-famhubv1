/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
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
 * zlib_wrapper.c
 */


#include <linux/mutex.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_SQUASHFS_HW1
#include <mach/sdp_unzip.h>
#endif

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"

static void *zlib_init(struct squashfs_sb_info *dummy, void *buff, int len)
{
	z_stream *stream = kmalloc(sizeof(z_stream), GFP_KERNEL);
	if (stream == NULL)
		goto failed;
	stream->workspace = vmalloc(zlib_inflate_workspacesize());
	if (stream->workspace == NULL)
		goto failed;

	return stream;

failed:
	ERROR("Failed to allocate zlib workspace\n");
	kfree(stream);
	return ERR_PTR(-ENOMEM);
}


static void zlib_free(void *strm)
{
	z_stream *stream = strm;

	if (stream)
		vfree(stream->workspace);
	kfree(stream);
}


#define GZIP_HEADER_SIZE 10
static int gzip_verify_header(struct squashfs_sb_info *msblk,
			      struct buffer_head **bh, int *k_, int b,
			      int *offset_, int *length_)
{
	char hdr[GZIP_HEADER_SIZE];
	int k = *k_;
	int offset = *offset_;
	int length = *length_;
	int ret = 0;

	int avail = min(length, msblk->devblksize - offset);
	int copied = 0;

	if (k == b || length < GZIP_HEADER_SIZE)
		goto out;
	wait_on_buffer(bh[k]);
	if (!buffer_uptodate(bh[k]))
		goto out;

	if (avail < GZIP_HEADER_SIZE) {
		memcpy(hdr, bh[k]->b_data + offset, avail);
		k++;

		if (k == b)
			goto out;
		wait_on_buffer(bh[k]);
		if (!buffer_uptodate(bh[k]))
			goto out;

		copied = avail;
		offset = 0;
	}

	/* Copy remains or full header */
	memcpy(hdr + copied, bh[k]->b_data + offset, GZIP_HEADER_SIZE - copied);
	offset += GZIP_HEADER_SIZE - copied;
	length -= GZIP_HEADER_SIZE;

	/* Verify gzip */
	if (hdr[0] != 0x1f || hdr[1] != 0x8b ||
	    hdr[2] != 0x08 || hdr[3] != 0x00) {
		ERROR("%s: not gzip header\n", __func__);
		goto out;
	}

	ret = 1;

out:
	*k_ = k;
	*offset_ = offset;
	*length_ = length;

	return ret;
}


static int zlib_uncompress(struct squashfs_sb_info *msblk, void **buffer,
	struct buffer_head **bh, int b, int offset, int length, int srclength,
	int pages)
{
	int zlib_err, zlib_init = 0;
	int k = 0, page = 0;
	z_stream *stream = msblk->stream;
	int err = -EINVAL;
	int is_gzip = (msblk->decompressor->id == GZIP_COMPRESSION);

	/* In case of gzip verify header and advance bh,offset pair if needed */
	if (is_gzip &&
	    !gzip_verify_header(msblk, bh, &k, b, &offset, &length)) {
		err = -EIO;
		goto out;
	}

	mutex_lock(&msblk->read_data_mutex);

	stream->avail_out = 0;
	stream->avail_in = 0;

	do {
		if (stream->avail_in == 0 && k < b) {
			int avail = min(length, msblk->devblksize - offset);
			length -= avail;
			wait_on_buffer(bh[k]);
			if (!buffer_uptodate(bh[k])) {
				err = -EIO;
				goto release_mutex;
			}

			stream->next_in = bh[k]->b_data + offset;
			stream->avail_in = avail;
			offset = 0;
		}

		if (stream->avail_out == 0 && page < pages) {
			stream->next_out = buffer[page++];
			stream->avail_out = PAGE_CACHE_SIZE;
		}

		if (!zlib_init) {
			int wbits = (is_gzip ? -MAX_WBITS : DEF_WBITS);

			zlib_err = zlib_inflateInit2(stream, wbits);
			if (zlib_err != Z_OK) {
				ERROR("zlib_inflateInit returned unexpected "
					"result 0x%x, srclength %d\n",
					zlib_err, srclength);
				goto release_mutex;
			}
			zlib_init = 1;
		}

		zlib_err = zlib_inflate(stream, Z_SYNC_FLUSH);

		if (stream->avail_in == 0 && k < b)
			k++;
	} while (zlib_err == Z_OK);

	if (zlib_err != Z_STREAM_END) {
		ERROR("zlib_inflate error, data probably corrupt\n");
		goto release_mutex;
	}

	zlib_err = zlib_inflateEnd(stream);
	if (zlib_err != Z_OK) {
		ERROR("zlib_inflate error, data probably corrupt\n");
		goto release_mutex;
	}

	/* gzip has 8 byte trailer (crc32 + isize), also we have to do
	   8 byte alignment because of hw requirements, so in case of gzip
	   we will always have some remainings */
	if (is_gzip && k < b) {
		int next_k = k;
		int total_remains = stream->avail_in + length;
		/* 8b trailer + 8b alignment */
		if (total_remains >= 16) {
			ERROR("zlib_uncompress(gzip) error, data remaining: "
			      "available %u, length %u\n",
			      stream->avail_in, length);
			goto release_mutex;
		}
		next_k += !!stream->avail_in;
		next_k += !!length;

		k = min(next_k, b);
	}

	if (k < b) {
		ERROR("zlib_uncompress error, data remaining\n");
		goto release_mutex;
	}

	length = stream->total_out;
	mutex_unlock(&msblk->read_data_mutex);
	return length;

release_mutex:
	mutex_unlock(&msblk->read_data_mutex);

out:
	return err;
}

const struct squashfs_decompressor squashfs_zlib_comp_ops = {
	.init = zlib_init,
	.free = zlib_free,
	.decompress = zlib_uncompress,
	.id = ZLIB_COMPRESSION,
	.name = "zlib",
	.supported = 1
};


#ifdef CONFIG_SQUASHFS_HW1
static int gzip_hw_uncompress(struct squashfs_sb_info *msblk, void **buffer,
	struct buffer_head **bh, int b, int offset, int length, int srclength,
	int pages)
{
	int i, k = 0, avail, read = 0, ret = 0;
	struct page *out_pages[pages];
	void *ibuf;

	ibuf = (void *)__get_free_pages(GFP_KERNEL, get_order(length));
	if (!ibuf) {
		ret = -ENOMEM;
		goto read_failure;
	}

	for (i = 0; i < pages; ++i) {
		BUG_ON(!virt_addr_valid(buffer[i]));
		out_pages[i] = virt_to_page(buffer[i]);
	}

	/* Wait for all buffer heads, memcpy into bounce buffer */
	for (; k < b; k++) {
		wait_on_buffer(bh[k]);
		if (!buffer_uptodate(bh[k]))
			goto read_failure;

		avail = min(length - read, msblk->devblksize - offset);
		memcpy(ibuf + read, bh[k]->b_data + offset, avail);
		offset = 0;
		read += avail;
	}

	ret = sdp_unzip_decompress_sync(ibuf, length, out_pages, pages, false);

read_failure:
	free_pages((unsigned long)ibuf, get_order(length));

	/* Do software decompression in case of contention */
	if (ret == -EBUSY)
		return zlib_uncompress(msblk, buffer, bh, b, offset,
				       length, srclength, pages);

	return ret;
}
#endif /* CONFIG_SQUASHFS_HW1 */


static int gzip_sw_hw_uncompress(struct squashfs_sb_info *msblk, void **buffer,
	struct buffer_head **bh, int b, int offset, int length, int srclength,
	int pages)
{
#ifdef CONFIG_SQUASHFS_HW1
	if (hw_decompressor == HW1_DECOMPRESSOR)
		return gzip_hw_uncompress(msblk, buffer, bh, b, offset,
					  length, srclength, pages);
	else
#endif
		return zlib_uncompress(msblk, buffer, bh, b, offset,
				       length, srclength, pages);
}


const struct squashfs_decompressor squashfs_gzip_comp_ops = {
	.init = zlib_init,
	.free = zlib_free,
	.decompress = gzip_sw_hw_uncompress,
	.id = GZIP_COMPRESSION,
	.name = "gzip",
	.supported = 1
};
