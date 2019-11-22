/**
 * VDFS -- Vertically Deliberate improved performance File System
 *
 * Copyright 2012 by Samsung Electronics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/zlib.h>
#include <linux/stat.h>
#include <linux/namei.h>
#include <linux/vmalloc.h>
#include "emmcfs.h"
#include "debug.h"
#include "packtree.h"
#ifdef CONFIG_VDFS_HW2_SUPPORT
#include <mach/sdp_unzip.h>
#include <linux/blkdev.h>
#endif
#include <linux/lzo.h>


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

static void get_chunk_offset(struct installed_packtree_info *packtree_info,
		__u32 chunk_index, struct chunk_info *result, int is_frag)
{
	BUG_ON(chunk_index >= packtree_info->params.pmc.chunk_cnt);
	result->data_start = packtree_info->chunk_table[chunk_index] >> 2;
	result->length =
		(__u32)((packtree_info->chunk_table[chunk_index + 1] >> 2)
			- (packtree_info->chunk_table[chunk_index] >> 2));
	result->compressed = !(packtree_info->chunk_table[chunk_index] & 2);
	EMMCFS_BUG_ON((packtree_info->chunk_table[chunk_index] & 1) !=
			(unsigned)is_frag);
}


static int sw_decompress(z_stream *strm, char *ibuff, int ilen,
	char *obuff, int olen, int compr_type)
{
	int rc = 0;

	strm->avail_out = (unsigned int)olen;
	strm->next_out = obuff;
	if (compr_type == VDFS_COMPR_ZLIB) {
		strm->avail_in = (unsigned)ilen;
		strm->next_in = ibuff;
		rc = zlib_inflateInit(strm);
	} else if (compr_type == VDFS_COMPR_GZIP) {
		strm->avail_in = (unsigned)(ilen - 10);
		strm->next_in = ibuff + 10;
		rc = zlib_inflateInit2(strm, -MAX_WBITS);
	} else {
		EMMCFS_ERR("Unsupported compression type\n");
		return -EIO;
	}

	if (rc != Z_OK) {
		EMMCFS_ERR("zlib_inflateInit error %d", rc);
		return rc;
	}

	rc = zlib_inflate(strm, Z_SYNC_FLUSH);
	if ((rc != Z_OK) && (rc != Z_STREAM_END)) {
		EMMCFS_ERR("zlib_inflate error %d", rc);
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
	*offset = (int)(chunk.data_start & (__u64)((1 << sbi->block_size_shift)
				- 1));
	*clen = (int)chunk.length;

	length_in_blocks = (unsigned)(*offset + *clen +
		((1 << sbi->block_size_shift) - 1)) >> sbi->block_size_shift;

	for (i = 0; i < (int)image_inode_info->fork.used_extents; i++) {
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
			      __u32 index,
			      struct page **out_pages, unsigned int pages_cnt,
			      int is_fragment)
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
		.phys_off = ((unsigned long long)start_sector << 9) +
			(sector_t)offset,
		.len = (unsigned int)clen
	};
	return ops->hw_decompress_vec(bdev, &iovec, 1, out_pages, pages_cnt);
}

#endif

static struct page **get_opages(struct inode *inode, __u32 index,
	int total_pages)
{
	int count, ret;
	struct address_space *mapping = inode->i_mapping;
	struct page **opages =
		kzalloc((unsigned)total_pages * sizeof(struct page *),
				GFP_KERNEL);
	if (!opages)
		return ERR_PTR(-ENOMEM);

	for (count = 0; count < total_pages; count++) {
		struct page *page;
again:
		page = find_lock_page(mapping, index + (__u32)count);
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
				index + (__u32)count, GFP_KERNEL);
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

static void print_uncomp_err_dump_header(char *name, int name_len)
{
	pr_err("--------------------------------------------------\n");
	EMMCFS_ERR("Software decomression ERROR");
	pr_err(" Current : %s(%d)", current->comm,
			task_pid_nr(current));
	pr_err("--------------------------------------------------\n");
#if defined(VDFS_GIT_BRANCH) && defined(VDFS_GIT_REV_HASH) && \
		defined(VDFS_VERSION)

	pr_err("== VDFS Debugger - %15s ===== Core : %2d ====\n"
			, VDFS_VERSION, current_thread_info()->cpu);
#endif
	pr_err("--------------------------------------------------\n");
	pr_err("Source image name : %.*s", name_len, name);
	pr_err("--------------------------------------------------\n");
}

static void print_zlib_error(int unzip_error)
{
	switch (unzip_error) {
	case (Z_ERRNO):
		pr_err("File operation error %d\n", Z_ERRNO);
		break;
	case (Z_STREAM_ERROR):
		pr_err("The stream state was inconsistent %d\n",
				Z_STREAM_ERROR);
		break;
	case (Z_DATA_ERROR):
		pr_err("Stream was freed prematurely %d\n", Z_DATA_ERROR);
		break;
	case (Z_MEM_ERROR):
		pr_err("There was not enough memory %d\n", Z_MEM_ERROR);
		break;
	case (Z_BUF_ERROR):
		pr_err("no progress is possible or if there was not "
				"enough room in the output buffer %d\n",
				Z_BUF_ERROR);
		break;
	case (Z_VERSION_ERROR):
		pr_err("zlib library version is incompatible with"
			" the version assumed by the caller %d\n",
			Z_VERSION_ERROR);
		break;
	case (VDFS_Z_NEED_DICT_ERR):
		pr_err(" The Z_NEED_DICT error happened %d\n" ,
				VDFS_Z_NEED_DICT_ERR);
		break;
	default:
		pr_err("Unknown error code %d\n", unzip_error);
		break;
	}

}

static void dump_sw_decomression_error(struct installed_packtree_info
		*ptree_info, char *ibuff, __u32 chunk_index, __u32 index,
		struct chunk_info *chunk, int left_bytes, int is_fragment,
		int unzip_error)
{
	__u64 packed_page_index;
	int mapped_offset;
	char *file_name;
	int name_length;

	packed_page_index = chunk->data_start >> PAGE_SHIFT;
	mapped_offset = (int)(chunk->data_start -
		(packed_page_index << (__u64)PAGE_SHIFT));
	ibuff += mapped_offset;
	file_name = kmalloc(VDFS_FILE_NAME_LEN, GFP_KERNEL);

	if (!file_name)
		pr_err("cannot allocate space for file name\n");
	else {
		name_length = snprintf(file_name, VDFS_FILE_NAME_LEN,
			"%.*s_%d_%llu_%d.dump",
			ptree_info->params.source_image.name_len,
			ptree_info->params.source_image.name,
			chunk->compressed,
			chunk->data_start,
			chunk->length);
		if (name_length > 0)
			vdfs_dump_chunk_to_disk(ibuff, chunk->length,
				(const char *)file_name, (unsigned)name_length);
	}

	mutex_lock(&ptree_info->tree->sbi->dump_meta);
	preempt_disable();

	_sep_printk_start();
	print_uncomp_err_dump_header(ptree_info->params.source_image.name,
			ptree_info->params.source_image.name_len);
	pr_err("Upack chunk failed: chunk %d, output page index %d,"
			"frag %d\n", chunk_index, index, is_fragment);

	print_zlib_error(unzip_error);

	pr_err("Chunk: compressed %d, data start %llu, length %d\n",
			chunk->compressed,
			chunk->data_start,
			chunk->length);

	pr_err("----------------------------------------------------");

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS,
			16, 1, ibuff, chunk->length, 1);

	preempt_enable();
	_sep_printk_end();
	mutex_unlock(&ptree_info->tree->sbi->dump_meta);
	kfree(file_name);
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
	int total_pages = 1 << (ptree_info->params.pmc.squash_bss -
			PAGE_CACHE_SHIFT);
	int left_pages = total_pages;
	int left_bytes = left_pages << PAGE_SHIFT;

	get_chunk_offset(ptree_info, chunk_index, &chunk, is_fragment);

	if (chunk.length == 0) {/* sparse file */
		if (is_fragment)
			BUG();
		opages = get_opages(inode, index, total_pages);
		if (IS_ERR(opages))
			return -ENOMEM;

		for (count = 0; count < total_pages; count++) {
			pageaddr = kmap(opages[count]);
			memset(pageaddr, 0, PAGE_SIZE);
			kunmap(opages[count]);
			flush_dcache_page(opages[count]);
		}
		goto exit_read_hw;
	}

	packed_page_index = chunk.data_start >> PAGE_SHIFT;
	mapped_offset = (int)(chunk.data_start -
			(packed_page_index << PAGE_SHIFT));
	packed_page_count =
		(unsigned)(((int)chunk.length + mapped_offset + 4095) >>
				PAGE_SHIFT);

	if (inode != ptree_info->unpacked_inode) {
		int __left_pages =
			(int)(((i_size_read(inode) + (loff_t)PAGE_SIZE - 1ll) >>
			(loff_t)PAGE_SHIFT) - (loff_t)index);

		left_pages = min_t(int, left_pages, __left_pages);
		left_bytes = min_t(int, left_bytes,
			(int)(i_size_read(inode) - (index << PAGE_SHIFT)));
	}
	opages = get_opages(inode, index, total_pages);
	if (IS_ERR(opages))
		return -ENOMEM;

#ifdef CONFIG_VDFS_HW2_SUPPORT
	if ((VDFS_SB(packed_inode->i_sb)->use_hw_decompressor) &&
	    (ptree_info->params.pmc.compr_type == VDFS_COMPR_GZIP) &&
	    (total_pages == HW_DECOMPRESSOR_PAGES_NUM) && chunk.compressed) {
		ret = read_and_unpack_hw(ptree_info, inode, chunk_index,
				index, opages, (unsigned int)total_pages,
				is_fragment);

		if (ret >= 0) {
			ret = 0;
			goto exit_read_hw;
		}
	}
#endif
	packed_pages = kzalloc(packed_page_count * sizeof(struct page *),
			GFP_KERNEL);
	if (!packed_pages) {
		ret = -ENOMEM;
		goto exit_read_hw;
	}

	ret = vdfs_read_or_create_pages(packed_inode,
			(pgoff_t)packed_page_index, packed_page_count,
			packed_pages, VDFS_PACKTREE_READ, 0, 0);
	if (ret) {
		EMMCFS_ERR("vdfs_read_or_create_pages() returned error");
		goto exit_free_packed_page;
	}

	mapped_packed_pages = vm_map_ram(packed_pages, packed_page_count, -1,
			PAGE_KERNEL);
	if (!mapped_packed_pages) {
		ret = -ENOMEM;
		goto exit_no_unmap;
	}


	mapped_opages = vm_map_ram(opages, (unsigned)total_pages, -1,
			PAGE_KERNEL);
	if (!mapped_opages) {
		ret = -ENOMEM;
		goto exit;
	}

	if (chunk.compressed) {
		strm.workspace =
			vmalloc((unsigned long)zlib_inflate_workspacesize());
		if (!strm.workspace) {
			vm_unmap_ram(mapped_opages, (unsigned)total_pages);
			ret = -ENOMEM;
			goto exit;
		}

		ret = sw_decompress(&strm, (char *)mapped_packed_pages +
			mapped_offset, (int)chunk.length,
			mapped_opages, left_bytes,
			ptree_info->params.pmc.compr_type);

#ifdef CONFIG_VDFS_RETRY
		if (ret) {
			int retry_count;
			for (retry_count = 0; retry_count < 3; retry_count++) {
				ret = sw_decompress(&strm,
					(char *)mapped_packed_pages +
					mapped_offset, (int)chunk.length,
					mapped_opages, left_bytes,
					ptree_info->params.pmc.compr_type);
				if (!ret)
					break;
			}
		}
#endif

		vfree(strm.workspace);
		if (ret) {
			EMMCFS_ERR("unzip err %d\n", ret);
#ifdef CONFIG_VDFS_DEBUG
			for (count = 0; count < (int)packed_page_count; count++)
				pr_err("page %lu phy addr:0x%llx\n",
				packed_pages[count]->index,
				(long long unsigned int)
				page_to_phys(packed_pages[count]));

			dump_sw_decomression_error(ptree_info,
				mapped_packed_pages, chunk_index, index,
				&chunk, left_bytes, is_fragment, ret);
#endif
			ret = -EIO;
		}

	} else
		memcpy(mapped_opages, (char *)mapped_packed_pages +
			mapped_offset, min_t(__u32, chunk.length,
				(__u32)left_bytes));
	vm_unmap_ram(mapped_opages, (unsigned)total_pages);
exit:
	vm_unmap_ram(mapped_packed_pages, packed_page_count);
exit_no_unmap:
	for (count = 0; count < (int)packed_page_count; count++)
		page_cache_release(packed_pages[count]);
exit_free_packed_page:
	kfree(packed_pages);
exit_read_hw:
	for (count = 0; count < total_pages; count++) {
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


	mapped_page = kmap_atomic(page);
	mapped_unpacked_page = kmap_atomic(unpacked_page);

	/* Data page is placed inside one unpacked page */
	if (offset_in_unpacked_page + length <= PAGE_SIZE)
		memcpy((char *)mapped_page + offset_in_page,
			(char *)mapped_unpacked_page + offset_in_unpacked_page,
			length);
	/* Data page isn't placed in one unpacked page, so we need to copy
	 * just (length - offset_at_page) bytes, get next unpacked page
	 * and copy offset_at_page bytes from it to the data page
	 */
	else {
		memcpy(mapped_page, (char *)mapped_unpacked_page +
				offset_in_unpacked_page,
				PAGE_SIZE - offset_in_unpacked_page);

		offset_in_page = PAGE_SIZE - offset_in_unpacked_page;

		kunmap_atomic(mapped_unpacked_page);
		kunmap_atomic(mapped_page);
		unlock_page(unpacked_page);
		page_cache_release(unpacked_page);

		unpacked_page_index++;
		length = length - PAGE_SIZE + offset_in_unpacked_page;
		offset_in_unpacked_page = 0;
		goto get_page;
	}

	kunmap_atomic(mapped_unpacked_page);
	kunmap_atomic(mapped_page);
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
			(size_t)inode->i_size);
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
			(__u32)i_size_read(inode));
		if (rc)
			goto out;

		unlock_page(page);
	/*one big file*/
	} else if (EMMCFS_I(inode)->record_type ==
			VDFS_CATALOG_PTREE_FILE_CHUNKS) {
		int total_pages_shift = EMMCFS_I(inode)->ptree.tree_info->
				params.pmc.squash_bss - PAGE_CACHE_SHIFT;
		int total_pages = 1 << total_pages_shift;
		__u32 chunk_index = EMMCFS_I(inode)->ptree.chunk.chunk_index +
			(page->index >> total_pages_shift);
		__u32 index;
		/*if (chunk_info->data_start == 0xFFFFFFFF)  empty file
			goto pass;*/

		index = (__u32)(page->index & ~((unsigned int)total_pages
					- 1lu));
		unlock_page(page);
		rc = read_and_unpack_chunk(EMMCFS_I(inode)->ptree.tree_info,
			inode, chunk_index, index, 0);
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
	struct vdfs_pack_common_value *common =
			(struct vdfs_pack_common_value *)
			get_value_pointer(key);

	memset(&EMMCFS_I(inode)->ptree, 0, sizeof(EMMCFS_I(inode)->ptree));

	EMMCFS_I(inode)->record_type = key->record_type;

	EMMCFS_I(inode)->flags = 0; /* todo */
	vdfs_set_vfs_inode_flags(inode);

	new_name = kmalloc(key->name_len + 1lu, GFP_KERNEL);
	if (!new_name)
		return -ENOMEM;

	memcpy(new_name, key->name, key->name_len);
	new_name[key->name_len] = 0;

	if (EMMCFS_I(inode)->name) {
		EMMCFS_ERR("inode_info->name is not NULL. Free it.\n");
		kfree(EMMCFS_I(inode)->name);
	}
	EMMCFS_I(inode)->name = new_name;
	EMMCFS_I(inode)->parent_id = le64_to_cpu(key->parent_id);

	inode->i_generation = inode->i_ino;

	set_nlink(inode, (unsigned int)le64_to_cpu(common->links_count));
	inode->i_size = (loff_t)le64_to_cpu(common->size);
	inode->i_mode = le16_to_cpu(common->file_mode);
	i_uid_write(inode, le32_to_cpu(common->uid));
	i_gid_write(inode, le32_to_cpu(common->gid));

	inode->i_ctime = vdfs_decode_time(common->creation_time);
	inode->i_mtime = inode->i_atime = inode->i_ctime;

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
	inode_info->ptree.root.pmc.chunk_cnt = le32_to_cpu(ipv->pmc.chunk_cnt);
	inode_info->ptree.root.source_image.parent_id =
		le64_to_cpu(ipv->source_image.parent_id);
	inode_info->ptree.root.source_image.name_len =
		ipv->source_image.name_len;
	memcpy(inode_info->ptree.root.source_image.name, ipv->source_image.name,
			inode_info->ptree.root.source_image.name_len);
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
	memcpy(EMMCFS_I(inode)->ptree.tiny.data, ftv->data,
			(size_t)inode->i_size);
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

	EMMCFS_I(inode)->ptree.symlink.data = kzalloc((size_t)inode->i_size +
			1lu, GFP_NOFS);
	if (!EMMCFS_I(inode)->ptree.symlink.data)
		return -ENOMEM;
	memcpy(EMMCFS_I(inode)->ptree.symlink.data, psv->data,
			(size_t)inode->i_size);
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
	ssize_t ret = 0;
	int i = 0;
	void *xattr_buff, *mapped_pages;
	struct xattr_entry *entry;
	struct xattr_val *val;
	struct page **pages;
	__u64 start_page;
	__u32 pages_count;
	__u16 type, prefix_size;
	char *prefix;
	int attr_len_rest = (int)inode_info->ptree.xattr.size;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	if (!ptree_info)
		return -EFAULT;
	start_page = ptree_info->params.pmc.xattr_off  +
		(inode_info->ptree.xattr.offset >> PAGE_SHIFT);
	pages_count = (__u32)(((inode_info->ptree.xattr.offset) &
		(PAGE_SIZE - 1)) + inode_info->ptree.xattr.size +
		(PAGE_SIZE - 1)) >> PAGE_SHIFT;

	if (((ptree_info->params.pmc.xattr_off << PAGE_SHIFT) +
		inode_info->ptree.xattr.offset + inode_info->ptree.xattr.size) >
		ptree_info->params.pmc.chtab_off)
		return -ERANGE;

	if (!inode_info->ptree.xattr.count || !inode_info->ptree.xattr.size)
		return -ENODATA;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	pages = kzalloc(pages_count * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = vdfs_read_or_create_pages(ptree_info->tree->inode,
		(pgoff_t)start_page, pages_count, pages, VDFS_PACKTREE_READ,
			0, 0);
	if (ret) {
		kfree(pages);
		return ret;
	}

	mapped_pages = vmap(pages, pages_count, VM_MAP, PAGE_KERNEL);
	xattr_buff = (char *)mapped_pages +
			((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1));

	for (i = 0; i < (int)inode_info->ptree.xattr.count; i++) {

		if (attr_len_rest < (int)sizeof(struct xattr_entry))
			goto exit_erange;
		attr_len_rest -= (int)sizeof(struct xattr_entry);

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
			xattr_buff = (char *)xattr_buff +
				sizeof(struct xattr_entry) +
				le16_to_cpu(entry->size);

			if (attr_len_rest < (int)sizeof(struct xattr_val))
				goto exit_erange;
			attr_len_rest -= (int)sizeof(struct xattr_val);

			val = (struct xattr_val *)xattr_buff;

			ret = (int)le32_to_cpu(val->vsize);
			if (attr_len_rest < ret)
				goto exit_erange;
			if (buffer) {
				if (ret > (int)buf_size)
					goto exit_erange;

				memcpy(buffer, val->value, (size_t)ret);
			}
			break;
		}
next_attr:	xattr_buff = (char *)xattr_buff + sizeof(struct xattr_entry) +
				le16_to_cpu(entry->size);
		val = (struct xattr_val *)xattr_buff;
		xattr_buff = (char *)xattr_buff + sizeof(struct xattr_val) +
				le32_to_cpu(val->vsize);
		attr_len_rest -= (int)sizeof(struct xattr_val) +
				(int)le32_to_cpu(val->vsize);
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
	ssize_t size = 0;
	__u16 type, prefix_size = 0;
	int i = 0, ret = 0, rest = (int)buf_size;
	void *xattr_buff, *mapped_pages;
	struct xattr_entry *entry;
	struct xattr_val *val;
	struct page **pages;
	char *prefix;
	__u64 start_page;
	__u32 pages_count;
	struct installed_packtree_info *ptree_info =
			EMMCFS_I(inode)->ptree.tree_info;
	if (!ptree_info)
		return -EFAULT;
	start_page = ptree_info->params.pmc.xattr_off +
		(inode_info->ptree.xattr.offset >> PAGE_SHIFT);
	pages_count = (__u32)((((inode_info->ptree.xattr.offset) &
		(PAGE_SIZE - 1)) + inode_info->ptree.xattr.size +
		(PAGE_SIZE - 1)) >> PAGE_SHIFT);

	if (((ptree_info->params.pmc.xattr_off << PAGE_SHIFT) +
		inode_info->ptree.xattr.offset + inode_info->ptree.xattr.size) >
		ptree_info->params.pmc.chtab_off)
		return -ERANGE;


	if (!inode_info->ptree.xattr.count ||
			!inode_info->ptree.xattr.size)
		return ret;

	pages = kzalloc(pages_count * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = vdfs_read_or_create_pages(ptree_info->tree->inode,
		(pgoff_t)start_page, pages_count, pages, VDFS_PACKTREE_READ,
			0, 0);
	if (ret) {
		kfree(pages);
		return ret;
	}

	mapped_pages = vmap(pages, pages_count, VM_MAP, PAGE_KERNEL);
	xattr_buff = (char *)mapped_pages +
			((inode_info->ptree.xattr.offset) & (PAGE_SIZE - 1));

	for (i = 0; i < (int)inode_info->ptree.xattr.count; i++) {
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
			memcpy(buffer, entry->data, (size_t)size);
			buffer[size] = '\0';
			buffer += size + 1;
		}
		rest -= prefix_size + size + 1;
		xattr_buff = (char *)xattr_buff + sizeof(struct xattr_entry) +
			size;
		val = (struct xattr_val *)xattr_buff;
		size = (ssize_t)le32_to_cpu(val->vsize);
		xattr_buff = (char *)xattr_buff + sizeof(struct xattr_val) +
			size;
		ret = (int)buf_size - rest;
	}
	vunmap(mapped_pages);
	kfree(pages);
	return ret;
}

#ifdef CONFIG_VDFS_DEBUG
void dump_fbc_error(struct page **pages, int pages_num, int len,
		struct vdfs_comp_extent_info *cext, loff_t chunk_start_pos,
		struct emmcfs_inode_info *inode_i)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode_i->vfs_inode.i_sb);
	char *fname = inode_i->name;
	int fname_len = fname ? (int)strlen(fname) : 0;
	void *packed = vmap(pages, (unsigned)pages_num, VM_MAP, PAGE_KERNEL);
	void *chunk;
	int count;
	char *file_name = NULL;
	int name_length;
	struct vdfs_extent_info extent;
	int ret;

	if (!packed) {
		pr_err("Can not dump erroneous chunk - no memory");
		return;
	}
	mutex_lock(&sbi->dump_meta);
	_sep_printk_start();

	chunk = (char *)packed + cext->offset;
	print_uncomp_err_dump_header(fname, fname_len);

	memset(&extent, 0x0, sizeof(extent));
	ret = get_iblock_extent(&inode_i->vfs_inode,
				cext->start_block, &extent, 0);
	if (ret || (extent.first_block == 0))
		goto dump_to_console;


	file_name = kmalloc(VDFS_FILE_NAME_LEN, GFP_KERNEL);
	if (!file_name)
		EMMCFS_ERR("cannot allocate space for file name\n");
	else {
		if (inode_i->installed_btrees) {
			/* file - based in image */
			fname = EMMCFS_I(inode_i->installed_btrees->
					cat_tree->inode)->name;

			name_length = snprintf(file_name, VDFS_FILE_NAME_LEN,
					"%lu_%.*s_%d_%llu_%d.dump",
				inode_i->installed_btrees->
				cat_tree->inode->i_ino,
				strlen(fname),
				fname,
				(cext->flags & VDFS_CHUNK_FLAG_UNCOMPR),
				((extent.first_block + cext->start_block) << 12)
				+ (sector_t)cext->offset,
				pages_num << PAGE_SHIFT);


		} else {
			name_length = snprintf(file_name, VDFS_FILE_NAME_LEN,
					"%lu_%.*s_%d_%llu_%d.dump",
				inode_i->vfs_inode.i_ino,
				strlen(fname),
				fname,
				(cext->flags & VDFS_CHUNK_FLAG_UNCOMPR),
				((extent.first_block +
				cext->start_block) << 12) +
				(sector_t)cext->offset,
				pages_num << PAGE_SHIFT);
		}

		if (name_length > 0)
			vdfs_dump_chunk_to_disk(packed,
				(size_t)(pages_num << PAGE_SHIFT),
				(const char *)file_name, (unsigned)name_length);
	}
dump_to_console:

	for (count = 0; count < pages_num; count++)
		pr_err("page index %lu phy addr:0x%llx\n",
				pages[count]->index,
				(long long unsigned int)
				page_to_phys(pages[count]));

	pr_err("chunk info:\n\t"
			"offset = %llu\n\t"
			"len_bytes = %d\n\t"
			"blocks_n = %d\n\t"
			"is_uncompr = %d\n",
			((extent.first_block +
			cext->start_block) << 12) + (sector_t)cext->offset,
			cext->len_bytes, cext->blocks_n,
			cext->flags & VDFS_CHUNK_FLAG_UNCOMPR);

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1, chunk,
			(size_t)cext->len_bytes, 1);
	vunmap(packed);

	_sep_printk_end();
	mutex_unlock(&sbi->dump_meta);
	kfree(file_name);
}
#endif

static int _unpack_chunk_zlib_gzip(struct page **src, struct page **dst,
		int pages_num, int pages_per_chunk, int offset, int len_bytes,
		enum compr_type compr_type)
{
	z_stream strm;
	void *packed, *unpacked;
	int ret = 0;

	BUG_ON(compr_type != VDFS_COMPR_ZLIB && compr_type != VDFS_COMPR_GZIP);

	strm.workspace = vmalloc((unsigned long)zlib_inflate_workspacesize());
	if (!strm.workspace)
		return -ENOMEM;

	packed = vm_map_ram(src, (unsigned)pages_num, -1, PAGE_KERNEL);
	if (!packed) {
		vfree(strm.workspace);
		return -ENOMEM;
	}

	unpacked = vm_map_ram(dst, (unsigned)pages_per_chunk, -1, PAGE_KERNEL);
	if (!unpacked) {
		vm_unmap_ram(packed, (unsigned)pages_num);
		vfree(strm.workspace);
		return -ENOMEM;
	}

	ret = sw_decompress(&strm, (char *)packed + offset, len_bytes, unpacked,
			pages_per_chunk << PAGE_CACHE_SHIFT,
			compr_type);

	if (ret) {
#ifdef CONFIG_VDFS_DEBUG
		print_zlib_error(ret);
#endif
		ret = -EIO;
	}

	vm_unmap_ram(unpacked, (unsigned)pages_per_chunk);
	vm_unmap_ram(packed, (unsigned)pages_num);
	vfree(strm.workspace);
	return ret;
}

int unpack_chunk_zlib(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes)
{
	return _unpack_chunk_zlib_gzip(src, dst, pages_num, pages_per_chunk,
			offset, len_bytes, VDFS_COMPR_ZLIB);
}

int unpack_chunk_gzip(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes)
{
	return _unpack_chunk_zlib_gzip(src, dst, pages_num, pages_per_chunk,
			offset, len_bytes, VDFS_COMPR_GZIP);
}

int unpack_chunk_lzo(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes)
{
	int res = 0;
	size_t out_len;
	void *packed = vm_map_ram(src, (unsigned)pages_num, -1, PAGE_KERNEL);
	void *unpacked = NULL;
	const unsigned char *packed_data;

	if (!packed)
		return -ENOMEM;

	unpacked = vm_map_ram(dst, (unsigned)pages_per_chunk, -1, PAGE_KERNEL);
	if (!unpacked) {
		vm_unmap_ram(packed, (unsigned)pages_num);
		return -ENOMEM;
	}

	packed_data = (char *)packed + offset;

	out_len = (size_t)(pages_per_chunk << PAGE_CACHE_SHIFT);
	res = lzo1x_decompress_safe(packed_data, (size_t)len_bytes, unpacked,
			&out_len);
	if (res != LZO_E_OK)
		res = -EIO;

	vm_unmap_ram(unpacked, (unsigned)pages_per_chunk);
	vm_unmap_ram(packed, (unsigned)pages_num);
	return res;
}


/**
 * The vdfs packtree root directory inode operations.
 */
static const struct inode_operations vdfs_packtree_root_dir_inode_operations = {
	/* d.voytik-TODO-19-01-2012-11-15-00:
	 * [emmcfs_dir_inode_ops] add to emmcfs_dir_inode_operations
	 * necessary methods */
	.lookup		= vdfs_lookup,
	.setxattr	= vdfs_setxattr,
	.getxattr	= vdfs_getxattr,
	.removexattr	= vdfs_removexattr,
	.listxattr	= vdfs_listxattr,
};

/**
 * The vdfs packtree directory inode operations.
 */
static const struct inode_operations vdfs_packtree_dir_inode_operations = {
	.lookup		= vdfs_lookup,
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
