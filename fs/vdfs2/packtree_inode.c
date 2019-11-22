/**
 * @file	fs/emmcfs/inode_squashfs.c
 * @brief	Installed from squashfs image inode operations.
 * @author	Igor Skalkin, i.skalkin@samsung.com
 * @date	02/05/2013
 *
* eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements inode read page functions for compressed
 * inodes, metadata of which was copied from squashfs image.
 *
 * @see		TODO: documents
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */
#include <linux/zlib.h>
#include <linux/stat.h>
#include <linux/namei.h>
#include <linux/vmalloc.h>
#include "emmcfs.h"
#include "debug.h"
#include "packtree.h"
#ifdef CONFIG_VDFS_HW2_SUPPORT
#include <linux/blkdev.h>
#endif

#define list_to_page(head) (list_entry((head)->prev, struct page, lru))
#define list_to_page_index(pos, head, index) \
	for (pos = list_entry((head)->prev, struct page, lru); \
		pos->index != index;\
	pos = list_entry((pos)->prev, struct page, lru))

static const struct address_space_operations vdfs_packtree_aops;
static const struct inode_operations vdfs_packtree_dir_inode_operations;
static const struct inode_operations vdfs_packtree_file_inode_operations;
static const struct inode_operations vdfs_packtree_symlink_inode_operations;
static const struct inode_operations vdfs_packtree_root_dir_inode_operations;

static int vdfs_packtree_file_open(struct inode *inode, struct file *filp)
{
	int rc;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	if (filp->f_flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))
		return -EROFS;
	rc = generic_file_open(inode, filp);
	if (rc)
		return rc;
	atomic_inc(&(inode_info->ptree.tree_info->open_count));
	return 0;
}

static int vdfs_packtree_file_release(struct inode *inode, struct file *file)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	atomic_dec(&(inode_info->ptree.tree_info->open_count));
	return 0;
}

const struct file_operations vdfs_packtree_fops = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.mmap		= generic_file_readonly_mmap,
	.splice_read	= generic_file_splice_read,
	.open		= vdfs_packtree_file_open,
	.release	= vdfs_packtree_file_release,
};

/* todo */
void get_chunk_offset(struct installed_packtree_info *packtree_info,
		__u32 chunk_index, struct chunk_info *result, int is_frag)
{
	BUG_ON(chunk_index >= packtree_info->params.pmc.chunk_cnt); /* todo */
	result->data_start = packtree_info->chunk_table[chunk_index] >> 2;
	result->length = (packtree_info->chunk_table[chunk_index + 1] >> 2)
			- (packtree_info->chunk_table[chunk_index] >> 2);
	result->compressed = !(packtree_info->chunk_table[chunk_index] & 2);
	EMMCFS_BUG_ON((packtree_info->chunk_table[chunk_index] & 1) != is_frag);
}


static int sw_decompress(z_stream *strm, char *ibuff, int ilen,
	char *obuff, int olen, int compr_type)
{
	int rc = 0;

	strm->avail_out = olen;
	strm->next_out = obuff;
	if (compr_type == zlib) {
		strm->avail_in = ilen;
		strm->next_in = ibuff;
		rc = zlib_inflateInit(strm);
	} else if (compr_type == gzip) {
		strm->avail_in = ilen - 10;
		strm->next_in = ibuff + 10;
		rc = zlib_inflateInit2(strm, -MAX_WBITS);
	} else {
		pr_err("Unsupported compression type\n");
		return -EIO;
	}

	if (rc != Z_OK) {
		pr_err("zlib_inflateInit error %d", rc);
		return rc;
	}

	rc = zlib_inflate(strm, Z_SYNC_FLUSH);
	if ((rc != Z_OK) && (rc != Z_STREAM_END)) {
		pr_err("zlib_inflate error %d", rc);
		rc = (rc == Z_NEED_DICT) ? VDFS_Z_NEED_DICT_ERR : rc;
		return rc;
	}

	rc = zlib_inflateEnd(strm);
	return rc;
}

#ifdef CONFIG_VDFS_HW2_SUPPORT

static int get_chunk_start_sector(struct installed_packtree_info *ptree_info,
		__u32 chunk_index, sector_t *result, int *offset, int *clen,
		int is_fragment) {

	struct emmcfs_inode_info *image_inode_info =
			EMMCFS_I(ptree_info->tree->inode);
	struct chunk_info chunk;
	struct vdfs_sb_info *sbi = VDFS_SB(ptree_info->tree->inode->i_sb);
	__u32 length_in_blocks;
	sector_t iblock_requested;
	int i;

	get_chunk_offset(ptree_info, chunk_index, &chunk, is_fragment);

	iblock_requested = chunk.data_start >> sbi->block_size_shift;
	*result = iblock_requested;
	*offset = chunk.data_start & ((1 << sbi->block_size_shift) - 1);
	*clen = chunk.length;

	length_in_blocks =
		(*offset + *clen + ((1 << sbi->block_size_shift) - 1)) >>
			sbi->block_size_shift;

	for (i = 0; i < image_inode_info->fork.used_extents; i++) {
		struct vdfs_extent_info *x = &image_inode_info->fork.extents[i];
		if ((iblock_requested >= x->iblock) &&
			((iblock_requested - x->iblock + length_in_blocks) <=
					x->block_count)) {
			*result += x->first_block;
			*result <<= (sbi->block_size_shift - SECTOR_SIZE_SHIFT);
			return 0;
		} else
			*result -= x->block_count;
	}
	return -EINVAL;
}

static int read_and_unpack_hw(struct installed_packtree_info *ptree_info,
			      struct inode *inode, __u32 chunk_index,
			      __u32 index, int pages_cnt,
			      struct page **out_pages, int is_fragment)
{
	struct block_device *bdev = inode->i_sb->s_bdev;
	const struct block_device_operations *ops = bdev->bd_disk->fops;
	struct hw_iovec iovec;
	sector_t start_sector;
	int offset, clen;
	int ret;

	if (!ops->hw_decompress_vec)
		return -EINVAL;

	ret = get_chunk_start_sector(ptree_info, chunk_index, &start_sector,
			&offset, &clen, is_fragment);
	if (ret)
		return ret;

	iovec = (struct hw_iovec){
		.phys_off = ((unsigned long long)start_sector << 9) + offset,
		.len = clen
	};
	return ops->hw_decompress_vec(bdev, &iovec, 1, out_pages, pages_cnt);
}

#endif

static struct page **get_opages(struct inode *inode, __u32 index)
{
	int count, ret;
	struct address_space *mapping = inode->i_mapping;
	struct page **opages =
		kzalloc(TOTAL_PAGES * sizeof(struct page *), GFP_KERNEL);
	if (!opages)
		return ERR_PTR(-ENOMEM);

	for (count = 0; count < TOTAL_PAGES; count++) {
		struct page *page;
again:
		page = find_lock_page(mapping, index + count);
		if (!page) {
			page = page_cache_alloc_cold(mapping);
			if (!page) {
				count--;
				for (; count >= 0; count--) {
					unlock_page(opages[count]);
					page_cache_release(opages[count]);
				}
				kfree(opages);
				return ERR_PTR(-ENOMEM);
			}
			ret = add_to_page_cache_lru(page, mapping,
					index + count, GFP_KERNEL);
			if (unlikely(ret)) {
				page_cache_release(page);
				goto again;
			}
		}
		BUG_ON(!PageLocked(page));
		opages[count] = page;
	}

	return opages;
}


#ifdef CONFIG_VDFS_DEBUG
/*
 * Dump out the contents of some memory nicely...
 */
#ifdef CONFIG_SEPARATE_PRINTK_FROM_USER
void _sep_printk_start(void);
void _sep_printk_end(void);
#else
#define _sep_printk_start() {}
#define _sep_printk_end() {}
#endif

static void fill_single_long(unsigned long bottom, unsigned long top,
		unsigned long p, char **strptr, int need_extra_break)
{
	if (p >= bottom && p <= top) {
		unsigned long val;
		if (__get_user(val, (unsigned long *)p) == 0) {
			int j;
			/* TODO - indian - change to more obvious */
			unsigned char *sing = (unsigned char*)&val;
			for (j = 0; j < sizeof(unsigned long); ++j)
			{
				*strptr += snprintf(*strptr, 4, "%02x ", *sing);
				++sing;
			}
		} else {
			int j;
			for (j = 0; j < sizeof(unsigned long); ++j)
			{
				*strptr += snprintf(*strptr, 4, "?? ");
			}
		}
	}
	if (need_extra_break)
		*strptr += snprintf(*strptr, 2, " ");
}


static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;


	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk(KERN_ERR "%s(0x%08lx to 0x%08lx), crc32 = %u\n",
			str, bottom, top,
			crc32(0, (const unsigned char *)bottom, top - bottom));

	for (first = bottom; first < top; first += 16) {
		unsigned long p;
		char str[128];

		memset(str, ' ', sizeof(str));

		p = first;
		if (p <= top) {
			int i = 0;
			char* strptr = str;
			for (i = 0; i < 16 / sizeof (unsigned long); ++i) {
				fill_single_long(bottom, top, p, &strptr,
						i == 1);
				p += sizeof(unsigned long);
			}
		}
		printk(KERN_ERR "%08lx  %s\n", first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_sw_decomression_error(struct installed_packtree_info
		*ptree_info, char *ibuff, __u32 chunk_index, __u32 index,
		struct chunk_info *chunk, int left_bytes, int is_fragment,
		int unzip_error)
{
	__u64 packed_page_index;
	__u32 packed_page_count;
	int mapped_offset;

	mutex_lock(&ptree_info->dump_lock);
	preempt_disable();
	packed_page_index = chunk->data_start >> PAGE_SHIFT;
	mapped_offset = chunk->data_start - (packed_page_index << PAGE_SHIFT);
	packed_page_count = (chunk->length + mapped_offset + 4095)
			>> PAGE_SHIFT;
	_sep_printk_start();
	printk(KERN_ERR "--------------------------------------------------\n");
	printk(KERN_ERR "Software decomression ERROR");
	printk(KERN_ERR " Current : %s(%d)", current->comm,
			task_pid_nr(current));
	printk(KERN_ERR "--------------------------------------------------\n");
#if defined(VDFS_GIT_BRANCH) && defined(VDFS_GIT_REV_HASH) && \
		defined(VDFS_VERSION)

	printk(KERN_ERR "== VDFS Debugger - %15s ===== Core : %2d ====\n"
			, VDFS_VERSION, current_thread_info()->cpu);
#endif
	printk(KERN_ERR "--------------------------------------------------\n");
	printk(KERN_ERR "Source image name : %s",
			ptree_info->params.source_image_name.unicode_str);
	printk(KERN_ERR "--------------------------------------------------\n");
	printk(KERN_ERR "Upack chunk failed: chunk %d, output page index %d,"
			"frag %d\n", chunk_index, index, is_fragment);

	switch (unzip_error) {
	case (Z_ERRNO):
		printk(KERN_ERR "File operation error %d\n", Z_ERRNO);
		break;
	case (Z_STREAM_ERROR):
		printk(KERN_ERR "The stream state was inconsistent %d\n",
				Z_STREAM_ERROR);
		break;
	case (Z_DATA_ERROR):
		printk(KERN_ERR "Stream was freed prematurely %d\n",
				Z_DATA_ERROR);
		break;
	case (Z_MEM_ERROR):
		printk(KERN_ERR "There was not enough memory %d\n", Z_MEM_ERROR);
		break;
	case (Z_BUF_ERROR):
		printk(KERN_ERR "no progress is possible or if there was not "
				"enough room in the output buffer %d\n",
				Z_BUF_ERROR);
		break;
	case (Z_VERSION_ERROR):
		printk(KERN_ERR "zlib library version is incompatible with"
			" the version assumed by the caller %d\n",
			Z_VERSION_ERROR);
		break;
	case (VDFS_Z_NEED_DICT_ERR):
		printk(KERN_ERR " The Z_NEED_DICT error happened %d\n" ,
				VDFS_Z_NEED_DICT_ERR);
		break;
	default:
		printk(KERN_ERR "Unknown error code %d\n", unzip_error);
		break;
	}
	printk(KERN_ERR "Chunk: compressed %d, data start %llu, length %d\n",
			chunk->compressed,
			chunk->data_start,
			chunk->length);

	printk(KERN_ERR "----------------------------------------------------");
	ibuff += mapped_offset;
	dump_mem(KERN_ERR, "DUMP input buffer", (unsigned long)ibuff,
			(unsigned long)ibuff + chunk->length);

	preempt_enable();
	_sep_printk_end();
	mutex_unlock(&ptree_info->dump_lock);
}
#endif

static int read_and_unpack_chunk(struct installed_packtree_info *ptree_info,
	struct inode *inode, __u32 chunk_index, __u32 index, int is_fragment)
{
	struct inode *packed_inode = ptree_info->tree->inode;
	struct chunk_info chunk;
	int ret = 0;
	__u64 packed_page_index;
	__u32 packed_page_count;
	struct page **packed_pages = NULL;
	struct page **opages = NULL;
	int count;
	void *mapped_packed_pages, *mapped_opages, *pageaddr;
	z_stream strm;
	int mapped_offset;
	int left_pages = TOTAL_PAGES;
	__u64 left_bytes = left_pages << PAGE_SHIFT;
	int unzip_retries;

	get_chunk_offset(ptree_info, chunk_index, &chunk, is_fragment);

	if (chunk.length == 0) {/* sparse file */
		if (is_fragment)
			BUG();
		opages = get_opages(inode, index);
		if (IS_ERR(opages))
			return -ENOMEM;

		for (count = 0; count < TOTAL_PAGES; count++) {
			pageaddr = kmap(opages[count]);
			memset(pageaddr, 0, PAGE_SIZE);
			kunmap(opages[count]);
			flush_dcache_page(opages[count]);
		}
		goto exit_read_hw;
	}

	packed_page_index = chunk.data_start >> PAGE_SHIFT;
	mapped_offset = chunk.data_start - (packed_page_index << PAGE_SHIFT);
	packed_page_count =
		(chunk.length + mapped_offset + 4095)  >> PAGE_SHIFT;

	if (inode != ptree_info->unpacked_inode) {
		left_pages = min_t(int, left_pages,
			((i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT)
			- index);
		left_bytes = min_t(__u64, left_bytes,
			i_size_read(inode) - (index << PAGE_SHIFT));
	}
	opages = get_opages(inode, index);
	if (IS_ERR(opages))
		return -ENOMEM;

#ifdef CONFIG_VDFS_HW2_SUPPORT
	if ((VDFS_SB(packed_inode->i_sb)->use_hw_decompressor) &&
			(ptree_info->params.pmc.compr_type == gzip)
			&& chunk.compressed) {
		ret = read_and_unpack_hw(ptree_info, inode, chunk_index,
					 index, TOTAL_PAGES, opages,
					 is_fragment);

		if (ret != -EINVAL)
			goto exit_read_hw;
		ret = 0;
	}
#endif
	packed_pages = kzalloc(packed_page_count * sizeof(struct page *),
			GFP_KERNEL);
	if (!packed_pages) {
		ret = -ENOMEM;
		goto exit_read_hw;
	}

	ret = vdfs_read_or_create_pages(VDFS_SB(packed_inode->i_sb),
		packed_inode, packed_pages, packed_page_index,
		packed_page_count, 0);
	if (ret) {
		EMMCFS_ERR("vdfs_read_or_create_pages() returned error");
		goto exit_free_packed_page;
	}

	mapped_packed_pages = vm_map_ram(packed_pages, packed_page_count, -1,
			PAGE_KERNEL);
	if (!mapped_packed_pages) {
		ret = -ENOMEM;
		goto exit_no_vmem;
	}

	mapped_opages = vm_map_ram(opages, TOTAL_PAGES, -1, PAGE_KERNEL);
	if (!mapped_opages) {
		ret = -ENOMEM;
		goto exit;
	}

	if (chunk.compressed) {
		strm.workspace = vmalloc(zlib_inflate_workspacesize());
		if (!strm.workspace) {
			vm_unmap_ram(mapped_opages, TOTAL_PAGES);
			ret = -ENOMEM;
			goto exit;
		}

#ifdef CONFIG_VDFS_SW_DECOMPRESS_RETRY
		unzip_retries = 3;
#else
		unzip_retries = 0;
#endif
		do {
			ret = sw_decompress(&strm,
					    mapped_packed_pages + mapped_offset,
					    chunk.length, mapped_opages,
					    left_bytes,
					    ptree_info->params.pmc.compr_type);
			if (ret) {
				dump_sw_decomression_error(ptree_info,
							   mapped_packed_pages,
							   chunk_index, index,
							   &chunk, left_bytes,
							   is_fragment, ret);
				ret = -EIO;
			}
		} while (ret && unzip_retries--);

		vfree(strm.workspace);

		if (ret)
			EMMCFS_ERR("sw_decompression failed, see log above\n");
	} else
		memcpy(mapped_opages, mapped_packed_pages + mapped_offset,
			min_t(int, chunk.length, left_bytes));
	vm_unmap_ram(mapped_opages, TOTAL_PAGES);
exit:
	vm_unmap_ram(mapped_packed_pages, packed_page_count);
exit_no_vmem:
	for (count = 0; count < packed_page_count; count++)
		page_cache_release(packed_pages[count]);
exit_free_packed_page:
	kfree(packed_pages);
exit_read_hw:
	for (count = 0; count < TOTAL_PAGES; count++) {
		if (ret) {
			pageaddr = kmap(opages[count]);
			memset(pageaddr, 0, PAGE_SIZE);
			kunmap(opages[count]);
			flush_dcache_page(opages[count]);
			SetPageError(opages[count]);
		}

		if (!PageError(opages[count]))
			SetPageUptodate(opages[count]);
		unlock_page(opages[count]);
		page_cache_release(opages[count]);
	}
	kfree(opages);

	return ret;
}

static int get_packtree_data_page(struct page *page, __u32 chunk_index,
		__u32 offset, __u32 length)
{
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(page->mapping->host)->ptree.tree_info;
	struct address_space *mapping = ptree_info->unpacked_inode->i_mapping;
	__u16 sq_block_size_shift = ptree_info->params.pmc.squash_bss;
	pgoff_t unpacked_page_index;
	int ret = 0;
	struct page *unpacked_page;
	void *mapped_page, *mapped_unpacked_page;
	__u32 offset_in_unpacked_page, offset_in_page = 0;

	unpacked_page_index = (((chunk_index << sq_block_size_shift) + offset)
			>> PAGE_CACHE_SHIFT) + page->index;

	offset_in_unpacked_page = offset & (PAGE_SIZE - 1);
	length -= page->index * PAGE_SIZE;
	length = min_t(__u32, length, PAGE_SIZE);

get_page:
	unpacked_page = find_lock_page(mapping, unpacked_page_index);
	if (!unpacked_page) {
		ret = read_and_unpack_chunk(ptree_info,
				ptree_info->unpacked_inode, chunk_index,
				(chunk_index << (sq_block_size_shift -
				PAGE_CACHE_SHIFT)), 1);
		if (ret) {
			SetPageError(page);
			EMMCFS_ERR("read_and_unpack_chunk failed %d", ret);
			goto exit;
		}
		goto get_page;
	}

	if (!PageUptodate(unpacked_page)) {
		EMMCFS_ERR("Got locked but not uptodated page");
		unlock_page(unpacked_page);
		page_cache_release(unpacked_page);
		SetPageError(page);
		goto exit;
	}


	mapped_page = kmap(page);
	mapped_unpacked_page = kmap(unpacked_page);

	/* Data page is placed inside one unpacked page */
	if (offset_in_unpacked_page + length <= PAGE_SIZE)
		memcpy(mapped_page + offset_in_page, mapped_unpacked_page +
				offset_in_unpacked_page, length);
	/* Data page isn't placed in one unpacked page, so we need to copy
	 * just (length - offset_at_page) bytes, get next unpacked page
	 * and copy offset_at_page bytes from it to the data page
	 */
	else {
		memcpy(mapped_page, mapped_unpacked_page +
				offset_in_unpacked_page,
				PAGE_SIZE - offset_in_unpacked_page);

		offset_in_page = PAGE_SIZE - offset_in_unpacked_page;

		kunmap(page);
		kunmap(unpacked_page);
		unlock_page(unpacked_page);
		page_cache_release(unpacked_page);

		unpacked_page_index++;
		length = length - PAGE_SIZE + offset_in_unpacked_page;
		offset_in_unpacked_page = 0;
		goto get_page;
	}

	kunmap(page);
	kunmap(unpacked_page);
	unlock_page(unpacked_page);
	page_cache_release(unpacked_page);

	SetPageUptodate(page);
exit:
	return ret;
}

static int vdfs_read_inline_file_page(struct page *page)
{
	struct inode *inode = page->mapping->host;

	if (!PageUptodate(page)) {
		void *page_data = NULL;

		if (i_size_read(inode) > PAGE_SIZE) {
			SetPageError(page);
			return -EFAULT;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
		page_data = kmap_atomic(page, KM_USER0);
#else
		page_data = kmap_atomic(page);
#endif

		memcpy(page_data, EMMCFS_I(inode)->ptree.tiny.data,
			inode->i_size);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
		kunmap_atomic(page_data, KM_USER0);
#else
		kunmap_atomic(page_data);
#endif
		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);
	}
	return 0;
}

/**
 * @brief		Read page function.
 * @param [in]	file	Pointer to file structure
 * @param [out]	page	Pointer to page structure
 * @return		Returns error codes
 */
static int vdfs_packtree_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	void *pageaddr;
	int rc = 0;

	if (page->index >= ((i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT))
		goto out;

	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_PTREE_FILE_INLINE) {
		rc = vdfs_read_inline_file_page(page);
		if (rc)
			goto out;

		unlock_page(page);
		return 0;
	}

	/*many little files*/
	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_PTREE_FILE_FRAGMENT) {
		rc = get_packtree_data_page(page,
			EMMCFS_I(inode)->ptree.frag.chunk_num,
			EMMCFS_I(inode)->ptree.frag.unpacked_offset,
			i_size_read(inode));
		if (rc)
			goto out;

		unlock_page(page);
	/*one big file*/
	} else if (EMMCFS_I(inode)->record_type ==
			VDFS_CATALOG_PTREE_FILE_CHUNKS) {
		int chunk_index = EMMCFS_I(inode)->ptree.chunk.chunk_index +
				(page->index >> TOTAL_PAGES_SHIFT);
		/*if (chunk_info->data_start == 0xFFFFFFFF)  empty file
			goto pass;*/

		unlock_page(page);
		rc = read_and_unpack_chunk(EMMCFS_I(inode)->ptree.tree_info,
			inode, chunk_index,
			page->index & ~(TOTAL_PAGES - 1), 0);
	} else
		BUG();

	return 0;

out:
	pageaddr = kmap_atomic(page);
	memset(pageaddr, 0, PAGE_CACHE_SIZE);
	kunmap_atomic(pageaddr);
	flush_dcache_page(page);
	if (!PageError(page))
		SetPageUptodate(page);
	unlock_page(page);
	return 0;
}

#if 0
static int fragment_file_readpages(struct page **pages_for_vmap,
		unsigned nr_pages)
{
	int i, ret = 0;
	pgoff_t chunk_page_num;
	struct page **unpacked_pages;
	void *mapped_pages, *unpacked_mapped_pages;
	struct inode *inode = pages_for_vmap[0]->mapping->host;
	__u16 sq_block_size_shift = EMMCFS_I(inode)->ptree.tree_info->
				params.pmc.squash_bss;
	__u32 chunk_index = EMMCFS_I(inode)->ptree.frag.chunk_num;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	struct address_space *unpacked_mapping =
			ptree_info->unpacked_inode->i_mapping;
	__u32 offset = EMMCFS_I(inode)->ptree.frag.unpacked_offset +
			(pages_for_vmap[0]->index << PAGE_CACHE_SHIFT);

	mapped_pages = vmap(pages_for_vmap, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!mapped_pages) {
		for (i = 0; i < nr_pages; i++)
			unlock_page(pages_for_vmap[i]);
		return -ENOMEM;
	}

	chunk_page_num = (chunk_index << sq_block_size_shift)
			>> PAGE_CACHE_SHIFT;

	unpacked_pages = kzalloc(TOTAL_PAGES * sizeof(struct page *),
			GFP_KERNEL);
	if (!unpacked_pages) {
		for (i = 0; i < nr_pages; i++)
			unlock_page(pages_for_vmap[i]);
		return -ENOMEM;
	}

again:
	ret = find_get_pages_contig(unpacked_mapping, chunk_page_num,
			TOTAL_PAGES, unpacked_pages);
	if (ret != TOTAL_PAGES) {
		ret = read_and_unpack_chunk(ptree_info,
				ptree_info->unpacked_inode, chunk_index,
				(chunk_index << sq_block_size_shift)
				>> PAGE_CACHE_SHIFT);
		if (ret)
			goto exit;
		goto again;
	}
	for (i = 0; i < TOTAL_PAGES; i++)
		lock_page(unpacked_pages[i]);

	unpacked_mapped_pages = vmap(unpacked_pages, TOTAL_PAGES, VM_MAP,
				PAGE_KERNEL);
	if (!unpacked_mapped_pages) {
		ret = -ENOMEM;
		for (i = 0; i < TOTAL_PAGES; i++) {
			page_cache_release(unpacked_pages[i]);
			unlock_page(unpacked_pages[i]);
		}
		goto exit;
	}

	memcpy(mapped_pages, unpacked_mapped_pages + offset,
		i_size_read(inode) - (pages_for_vmap[0]->index << PAGE_SHIFT));

	vunmap(unpacked_mapped_pages);

	for (i = 0; i < TOTAL_PAGES; i++) {
		page_cache_release(unpacked_pages[i]);
		unlock_page(unpacked_pages[i]);
	}
exit:
	vunmap(mapped_pages);
	for (i = 0; i < nr_pages; i++) {
		SetPageUptodate(pages_for_vmap[i]);
		page_cache_release(pages_for_vmap[i]);
		unlock_page(pages_for_vmap[i]);
	}
	kfree(unpacked_pages);
	return ret;
}


/**
 * @brief		Read page function.
 * @param [in]	file	Pointer to file structure
 * @param [out]	page	Pointer to page structure
 * @return		Returns error codes
 */
static int vdfs_packtree_readpages(struct file *filp,
	struct address_space *mapping, struct list_head *pages,
	unsigned nr_pages)
{
	int i;
	int ret = 0;
	struct page **pages_for_vmap;
	struct inode *inode = mapping->host;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	unsigned page_count = nr_pages;
	__u32 last_page_index = 0;


	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_PTREE_FILE_FRAGMENT)
		page_count = (i_size_read(inode) >> PAGE_CACHE_SHIFT) + 1;

	pages_for_vmap =
		kzalloc(page_count * sizeof(struct page *), GFP_KERNEL);
	if (!pages_for_vmap)
		return -ENOMEM;

	for (i = 0; i < nr_pages; i++) {
		struct page *page = list_entry(pages->prev, struct page, lru);
		prefetchw(&page->flags);
		list_del(&page->lru);
again:
		if (!page) {
			ret = -EFAULT;
			goto exit;
		}
		ret = add_to_page_cache_lru(page, mapping,
				page->index, GFP_KERNEL);
		if (ret == -EEXIST) {
			page = find_get_page(mapping, page->index);
			if (page)
				page_cache_release(page);
			goto again;

		} else if (ret)
			goto exit;

		pages_for_vmap[i] = page;
		last_page_index = page->index;
	}

	for (i = nr_pages; i < page_count; i++) {
		struct page *page;
		int index = last_page_index + i + 1 - nr_pages;

again2:
		page = find_get_page(mapping, index);
		if (!page) {
			page = page_cache_alloc_cold(mapping);
			if (!page) {
				i--;
				for (; i > -1; i--)
					unlock_page(pages_for_vmap[i]);
					kfree(pages_for_vmap);
				return -ENOMEM;
			}
			ret = add_to_page_cache_lru(page, mapping, index,
					GFP_KERNEL);
			if (unlikely(ret)) {
				page_cache_release(page);
				goto again2;
			}
		} else
			lock_page(page);

		BUG_ON(!PageLocked(page));
		pages_for_vmap[i] = page;
	}

	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_PTREE_FILE_CHUNKS) {
		int chunk_index = EMMCFS_I(inode)->ptree.chunk.chunk_index +
			(pages_for_vmap[0]->index >> TOTAL_PAGES_SHIFT);
		int start_chunk_page_index = ((pages_for_vmap[0]->index >>
				TOTAL_PAGES_SHIFT) << TOTAL_PAGES_SHIFT);
		int total_pages = start_chunk_page_index + nr_pages;

		while (start_chunk_page_index < total_pages) {
			ret = read_and_unpack_chunk(ptree_info, inode,
				chunk_index, start_chunk_page_index);
			if (ret)
				goto exit;
			start_chunk_page_index += TOTAL_PAGES;
			chunk_index++;
		}
		goto exit;
	}
	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_PTREE_FILE_INLINE) {
		ret = vdfs_read_inline_file_page(pages_for_vmap[0]);
		kfree(pages_for_vmap);
		return ret;
	}
	ret = fragment_file_readpages(pages_for_vmap, page_count);
exit:
	kfree(pages_for_vmap);
	return ret;
}
#endif
/**
 * @brief		Reads the common part of packtree inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static int vdfs_read_pack_common(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	char *new_name;
	int str_len;
	struct vdfs_pack_common_value *common =
			(struct vdfs_pack_common_value *)
			get_value_pointer(key);

	memset(&EMMCFS_I(inode)->ptree, 0, sizeof(EMMCFS_I(inode)->ptree));

	EMMCFS_I(inode)->record_type = key->record_type;

	EMMCFS_I(inode)->flags = 0; /* todo */
	vdfs_set_vfs_inode_flags(inode);

	new_name = kzalloc(key->name.length + 1, GFP_KERNEL);
	if (!new_name)
		return -ENOMEM;

	str_len = min(key->name.length, (unsigned) EMMCFS_CAT_MAX_NAME);
	memcpy(new_name, key->name.unicode_str,
			min(key->name.length, (unsigned) EMMCFS_CAT_MAX_NAME));

	if (EMMCFS_I(inode)->name) {
		printk(KERN_INFO "inode_info->name is not NULL. Free it.\n");
		kfree(EMMCFS_I(inode)->name);
	}
	EMMCFS_I(inode)->name = new_name;
	EMMCFS_I(inode)->parent_id = le64_to_cpu(key->parent_id);

	inode->i_generation = inode->i_ino;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	set_nlink(inode, le64_to_cpu(common->links_count));
#else
	inode->i_nlink = le64_to_cpu(common->links_count);
#endif

	inode->i_size = le64_to_cpu(common->size);
	inode->i_mode = le16_to_cpu(common->permissions.file_mode);
	inode->i_uid = (uid_t)le32_to_cpu(common->permissions.uid);
	inode->i_gid = (uid_t)le32_to_cpu(common->permissions.gid);

	inode->i_mtime.tv_sec =
			le64_to_cpu(common->creation_time.seconds);
	inode->i_atime.tv_sec =
			le64_to_cpu(common->creation_time.seconds);
	inode->i_ctime.tv_sec =
			le64_to_cpu(common->creation_time.seconds);
	inode->i_mtime.tv_nsec =
			le64_to_cpu(common->creation_time.nanoseconds);
	inode->i_atime.tv_nsec =
			le64_to_cpu(common->creation_time.nanoseconds);
	inode->i_ctime.tv_nsec =
			le64_to_cpu(common->creation_time.nanoseconds);

	EMMCFS_I(inode)->ptree.xattr.offset = le64_to_cpu(common->xattr_offset);
	EMMCFS_I(inode)->ptree.xattr.size = le32_to_cpu(common->xattr_size);
	EMMCFS_I(inode)->ptree.xattr.count = le32_to_cpu(common->xattr_count);

	return 0;
}

/**
 * @brief		Reads the insert point specific part of packtree
 *			inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static void vdfs_read_pack_insert_point_value(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	struct vdfs_pack_insert_point_value *ipv =
		(struct vdfs_pack_insert_point_value *)
		get_value_pointer(key);

	memcpy(inode_info->ptree.root.packtree_layout_version,
			ipv->pmc.packtree_layout_version, 4);
	inode_info->ptree.root.start_ino = le64_to_cpu(ipv->start_ino);
	inode_info->ptree.root.pmc.inodes_cnt =
			le64_to_cpu(ipv->pmc.inodes_cnt);
	inode_info->ptree.root.pmc.packoffset =
			le64_to_cpu(ipv->pmc.packoffset);
	inode_info->ptree.root.pmc.nfs_offset =
			le64_to_cpu(ipv->pmc.nfs_offset);
	inode_info->ptree.root.pmc.xattr_off = le64_to_cpu(ipv->pmc.xattr_off);
	inode_info->ptree.root.pmc.chtab_off = le64_to_cpu(ipv->pmc.chtab_off);
	inode_info->ptree.root.pmc.squash_bss =
			le16_to_cpu(ipv->pmc.squash_bss);
	inode_info->ptree.root.pmc.compr_type =
			le16_to_cpu(ipv->pmc.compr_type);
	inode_info->ptree.root.pmc.chunk_cnt = le16_to_cpu(ipv->pmc.chunk_cnt);
	inode_info->ptree.root.source_image_parent_object_id =
			le64_to_cpu(ipv->source_image_parent_object_id);
	inode_info->ptree.root.source_image_name.length =
			le32_to_cpu(ipv->source_image_name.length);
	memcpy(inode_info->ptree.root.source_image_name.unicode_str,
			ipv->source_image_name.unicode_str,
	inode_info->ptree.root.source_image_name.length);
}

/**
 * @brief		Reads the pack file tiny value of packtree
 *			inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static int vdfs_read_pack_file_tiny_value(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	struct vdfs_pack_file_tiny_value *ftv =
		(struct vdfs_pack_file_tiny_value *)get_value_pointer(key);
	if (inode->i_size > VDFS_PACK_MAX_INLINE_FILE_SIZE)
		return -EFAULT;
	memcpy(EMMCFS_I(inode)->ptree.tiny.data, ftv->data, inode->i_size);
	return 0;
}

/**
 * @brief		Reads the pack file symlink value of packtree
 *			inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static int vdfs_read_pack_symlink_value(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	struct vdfs_pack_symlink_value *psv =
		(struct vdfs_pack_symlink_value *) get_value_pointer(key);

	EMMCFS_I(inode)->ptree.symlink.data = kzalloc(inode->i_size + 1,
		GFP_NOFS);
	if (!EMMCFS_I(inode)->ptree.symlink.data)
		return -ENOMEM;
	memcpy(EMMCFS_I(inode)->ptree.symlink.data, psv->data, inode->i_size);
	EMMCFS_I(inode)->ptree.symlink.data[inode->i_size] = 0;
	return 0;
}

/**
 * @brief		Reads the pack file overflowed value of packtree
 *			inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static void vdfs_read_pack_file_fragment_value(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_pack_file_fragment_value *ffv =
		(struct vdfs_pack_file_fragment_value *)get_value_pointer(key);
	inode_info->ptree.frag.unpacked_offset =
			le32_to_cpu(ffv->fragment.unpacked_offset);
	inode_info->ptree.frag.chunk_num =
			le32_to_cpu(ffv->fragment.chunk_index);
}

/**
 * @brief		Reads the pack file chunk value of packtree
 *			inode from record.
 * @param [in,out]	inode	The inode, that will be readed.
 * @param [in]		key	Pointer to record.
 * @return			Returns 0 on success, errno on failure
 */
static int vdfs_read_pack_file_chunk_value(struct inode *inode,
		struct emmcfs_cattree_key *key)
{
	struct vdfs_pack_file_chunk_value *fcv =
		(struct vdfs_pack_file_chunk_value *)get_value_pointer(key);
	EMMCFS_I(inode)->ptree.chunk.chunk_index =
			le32_to_cpu(fcv->chunk_index);
	return 0;
}

int vdfs_read_packtree_inode(struct inode *inode,
			struct emmcfs_cattree_key *key)
{
	__u8 rec_type = key->record_type;
	int ret = 0;

	ret = vdfs_read_pack_common(inode, key);
	if (ret)
		return ret;

	if (rec_type == VDFS_CATALOG_PTREE_ROOT) {
		vdfs_read_pack_insert_point_value(inode, key);
		inode->i_op = &vdfs_packtree_root_dir_inode_operations;
		inode->i_fop = &emmcfs_dir_operations;
	} else if (rec_type == VDFS_CATALOG_PTREE_FOLDER) {
		inode->i_op = &vdfs_packtree_dir_inode_operations;
		inode->i_fop = &emmcfs_dir_operations;
		inode->i_mode |= S_IFDIR;
	} else if (rec_type == VDFS_CATALOG_PTREE_SYMLINK) {
		ret = vdfs_read_pack_symlink_value(inode, key);
		inode->i_op = &vdfs_packtree_symlink_inode_operations;
		inode->i_mode |= S_IFLNK;
	} else if (rec_type == VDFS_CATALOG_PTREE_BLKDEV) {
		inode->i_mode |= S_IFBLK;
		init_special_inode(inode, inode->i_mode, new_decode_dev(
			le32_to_cpu(((struct vdfs_pack_device_value *)
				get_value_pointer(key))->rdev)));
	} else if (rec_type == VDFS_CATALOG_PTREE_CHRDEV) {
		inode->i_mode |= S_IFCHR;
		init_special_inode(inode, inode->i_mode, new_decode_dev(
			le32_to_cpu(((struct vdfs_pack_device_value *)
				get_value_pointer(key))->rdev)));
	} else if (rec_type == VDFS_CATALOG_PTREE_FIFO) {
		inode->i_mode |= S_IFIFO;
		init_special_inode(inode, inode->i_mode, 0);
	} else if (rec_type == VDFS_CATALOG_PTREE_SOCKET) {
		inode->i_mode |= S_IFSOCK;
		init_special_inode(inode, inode->i_mode, 0);
	} else {
		if (rec_type == VDFS_CATALOG_PTREE_FILE_INLINE)
			ret = vdfs_read_pack_file_tiny_value(inode, key);
		else if (rec_type == VDFS_CATALOG_PTREE_FILE_FRAGMENT)
			vdfs_read_pack_file_fragment_value(inode, key);
		else if (rec_type == VDFS_CATALOG_PTREE_FILE_CHUNKS)
			ret = vdfs_read_pack_file_chunk_value(inode, key);
		else
			BUG();

		inode->i_op = &vdfs_packtree_file_inode_operations;
		inode->i_mapping->a_ops = &vdfs_packtree_aops;
		inode->i_fop = &vdfs_packtree_fops;
		inode->i_mode |= S_IFREG;
	}

	return ret;
}

/**
 * The vdfs packtree follow link callback.
 */
static void *vdfs_packtree_follow_link(struct dentry *dentry,
		struct nameidata *nd)
{
	nd_set_link(nd, (char *)
			(EMMCFS_I(dentry->d_inode))->ptree.symlink.data);
	return NULL;
}

static ssize_t packtree_getxattr(struct dentry *dentry, const char *name,
	void *buffer, size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	ssize_t ret = 0;
	int i = 0;
	void *xattr_buff, *mapped_pages;
	struct xattr_entry *entry;
	struct xattr_val *val;
	struct page **pages;
	__u64 start_page;
	__u32 page_count;
	__u16 type, prefix_size;
	char *prefix;
	int attr_len_rest = inode_info->ptree.xattr.size;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	if (!ptree_info)
		return -EFAULT;
	start_page = (ptree_info->params.pmc.xattr_off >>
		log_blocks_per_page(inode->i_sb)) +
		(inode_info->ptree.xattr.offset >> PAGE_SHIFT);
	page_count = (((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1))
		+ inode_info->ptree.xattr.size + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	if (((ptree_info->params.pmc.xattr_off << PAGE_SHIFT) +
		inode_info->ptree.xattr.offset + inode_info->ptree.xattr.size) >
		ptree_info->params.pmc.chtab_off)
		return -ERANGE;

	if (!inode_info->ptree.xattr.count || !inode_info->ptree.xattr.size)
		return -ENODATA;

	if ((strcmp(name, "") == 0) || !buffer)
		return -EINVAL;

	pages = kzalloc(page_count * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = vdfs_read_or_create_pages(sbi, ptree_info->tree->inode,
			pages, start_page, page_count, 0);
	if (ret) {
		kfree(pages);
		return ret;
	}

	mapped_pages = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	xattr_buff = mapped_pages +
			((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1));

	for (i = 0; i < inode_info->ptree.xattr.count; i++) {

		if (attr_len_rest < sizeof(struct xattr_entry))
			goto exit_erange;
		attr_len_rest -= sizeof(struct xattr_entry);

		entry = (struct xattr_entry *)xattr_buff;
		type = le16_to_cpu(entry->type);
		switch (type) {
		case 0:
			prefix_size = 5;
			prefix = "user.";
			break;
		case 1:
			prefix_size = 8;
			prefix = "trusted.";
			break;
		case 2:
			prefix_size = 9;
			prefix = "security.";
			break;
		default:
			prefix_size = 0;
			prefix = NULL;
			break;
		}

		if (attr_len_rest < le16_to_cpu(entry->size))
			goto exit_erange;
		attr_len_rest -= le16_to_cpu(entry->size);

		if ((le16_to_cpu(entry->size) + prefix_size) > strlen(name))
			goto exit_erange;

		if (prefix && memcmp(name, prefix, prefix_size))
				goto next_attr;

		if (memcmp(&name[prefix_size], entry->data,
				le16_to_cpu(entry->size)) == 0) {
			xattr_buff += sizeof(struct xattr_entry) +
					le16_to_cpu(entry->size);

			if (attr_len_rest < sizeof(struct xattr_val))
				goto exit_erange;
			attr_len_rest -= sizeof(struct xattr_val);

			val = (struct xattr_val *)xattr_buff;

			ret = le32_to_cpu(val->vsize);
			if (ret > buf_size)
				goto exit_erange;
			if (attr_len_rest < ret)
				goto exit_erange;
			memcpy(buffer, val->value, ret);
			break;
		}
next_attr:	xattr_buff += sizeof(struct xattr_entry) +
				le16_to_cpu(entry->size);
		val = (struct xattr_val *)xattr_buff;
		xattr_buff += sizeof(struct xattr_val) +
				le32_to_cpu(val->vsize);
		attr_len_rest -= sizeof(struct xattr_val) +
				le32_to_cpu(val->vsize);
	}
exit:
	vunmap(mapped_pages);
	kfree(pages);
	return (ret != 0) ? ret : -ENODATA;
exit_erange:
	ret = -ERANGE;
	goto exit;
}
/* todo this function very similar to packtree_getxattr, refactoring with
 *  separation common code is necessary.
 */
static ssize_t packtree_listxattr(struct dentry *dentry, char *buffer,
	size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	ssize_t size = 0;
	__u16 type, prefix_size;
	int i = 0, ret = 0, rest = buf_size;
	void *xattr_buff, *mapped_pages;
	struct xattr_entry *entry;
	struct xattr_val *val;
	struct page **pages;
	char *prefix;
	__u64 start_page;
	__u32 page_count;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	if (!ptree_info)
		return -EFAULT;
	start_page = (ptree_info->params.pmc.xattr_off >>
		log_blocks_per_page(inode->i_sb)) +
		(inode_info->ptree.xattr.offset >> PAGE_SHIFT);
	page_count = (((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1))
		+ inode_info->ptree.xattr.size + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	if (((ptree_info->params.pmc.xattr_off << PAGE_SHIFT) +
		inode_info->ptree.xattr.offset + inode_info->ptree.xattr.size) >
		ptree_info->params.pmc.chtab_off)
		return -ERANGE;


	if (!inode_info->ptree.xattr.count ||
			!inode_info->ptree.xattr.size)
		return ret;

	pages = kzalloc(page_count * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = vdfs_read_or_create_pages(sbi, ptree_info->tree->inode,
			pages, start_page, page_count, 0);
	if (ret) {
		kfree(pages);
		return ret;
	}

	mapped_pages = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	xattr_buff = mapped_pages +
			((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1));

	for (i = 0; i < inode_info->ptree.xattr.count; i++) {
		entry = (struct xattr_entry *)xattr_buff;
		size = le16_to_cpu(entry->size);
		type = le16_to_cpu(entry->type);
		switch (type) {
		case 0:
			prefix_size = 5;
			prefix = "user.";
			break;
		case 1:
			prefix_size = 8;
			prefix = "trusted.";
			break;
		case 2:
			prefix_size = 9;
			prefix = "security.";
			break;
		default:
			prefix_size = 0;
			prefix = NULL;
			break;
		}
		if (size + prefix_size + 1 > rest) {
			ret = -ERANGE;
			break;
		}
		if (buffer) {
			if (prefix) {
				memcpy(buffer, prefix, prefix_size);
				buffer[prefix_size] = '\0';
				buffer += prefix_size;
			}
			memcpy(buffer, entry->data, size);
			buffer[size] = '\0';
			buffer += size + 1;
		}
		rest -= size + 1;
		xattr_buff += sizeof(struct xattr_entry) + size;
		val = (struct xattr_val *)xattr_buff;
		size = le32_to_cpu(val->vsize);
		xattr_buff += sizeof(struct xattr_val) + size;
		ret = buf_size - rest;
	}
	vunmap(mapped_pages);
	kfree(pages);
	return ret;
}

/**
 * The vdfs packtree root directory inode operations.
 */
static const struct inode_operations vdfs_packtree_root_dir_inode_operations = {
	/* d.voytik-TODO-19-01-2012-11-15-00:
	 * [emmcfs_dir_inode_ops] add to emmcfs_dir_inode_operations
	 * necessary methods */
	.lookup		= emmcfs_lookup,
	.setxattr	= vdfs_setxattr,
	.getxattr	= vdfs_getxattr,
	.removexattr	= vdfs_removexattr,
	.listxattr	= vdfs_listxattr,
};

/**
 * The vdfs packtree directory inode operations.
 */
static const struct inode_operations vdfs_packtree_dir_inode_operations = {
	.lookup		= emmcfs_lookup,
	.getxattr	= packtree_getxattr,
	.listxattr	= packtree_listxattr,
};

/**
 * The vdfs packtree address space operations.
 */
static const struct address_space_operations vdfs_packtree_aops = {
	.readpage	= vdfs_packtree_readpage,
	/*.readpages	= vdfs_packtree_readpages*/
};

/**
 * The vdfs packtree file inode operations.
 */
static const struct inode_operations vdfs_packtree_file_inode_operations = {
	.getxattr	= packtree_getxattr,
	.listxattr	= packtree_listxattr,
};

/**
 * The vdfs packtree symlink inode operations.
 */
static const struct inode_operations vdfs_packtree_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= vdfs_packtree_follow_link
};
