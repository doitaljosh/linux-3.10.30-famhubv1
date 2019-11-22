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

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>

#include "emmcfs.h"
#include "debug.h"


/**
 * @brief		Finalize IO writing.
 * param [in]	bio	BIO structure to be written.
 * param [in]	err	With error or not.
 * @return	void
 */
static void end_io_write(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);
		if (!uptodate) {
			SetPageError(page);
			if (page->mapping)
				set_bit(AS_EIO, &page->mapping->flags);
		}
		end_page_writeback(page);
	} while (bvec >= bio->bi_io_vec);

	if (bio->bi_private)
		complete(bio->bi_private);
	bio_put(bio);
}

/**
 * @brief		Finalize IO writing.
 * param [in]	bio	BIO structure to be read.
 * param [in]	err	With error or not.
 * @return	void
 */
static void read_end_io(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct completion *wait = bio->bi_private;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
	} while (bvec >= bio->bi_io_vec);
	complete(wait);
	bio_put(bio);
}

/**
 * @brief			Allocate new BIO.
 * @param [in]	bdev		The eMMCFS superblock information.
 * @param [in]	first_sector	BIO first sector.
 * @param [in]	nr_vecs		Number of BIO pages.
 * @return			Returns pointer to allocated BIO structure.
 */
struct bio *allocate_new_bio(struct block_device *bdev, sector_t first_sector,
		unsigned int nr_vecs)
{
	/*int nr_vecs;*/
	gfp_t gfp_flags = GFP_NOFS | __GFP_HIGH;
	struct bio *bio = NULL;
	sector_t s_count = (sector_t)(bdev->bd_inode->i_size >>
			SECTOR_SIZE_SHIFT);

	if ((first_sector > s_count) || ((first_sector + nr_vecs) > s_count))
		return ERR_PTR(-EFAULT);

	bio = bio_alloc(gfp_flags, nr_vecs);

	if (bio == NULL && (current->flags & PF_MEMALLOC)) {
		while (!bio && (nr_vecs /= 2))
			bio = bio_alloc(gfp_flags, nr_vecs);
	}

	if (bio) {
		bio->bi_bdev = bdev;
		bio->bi_sector = first_sector;
	}

	return bio;
}


#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	#define SUBMIT_BIO_WAIT_FOR_FLUSH_FLAGS WRITE_BARRIER
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) || \
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	#define SUBMIT_BIO_WAIT_FOR_FLUSH_FLAGS WRITE_FLUSH_FUA
#else
	BUILD_BUG();
#endif


int vdfs_get_table_sector(struct vdfs_sb_info *sbi, sector_t iblock,
		sector_t *result)
{
	struct vdfs_layout_sb *l_sb = sbi->raw_superblock;
	struct vdfs_extent *extent_table = &l_sb->exsb.tables;

	sector_t max_size = (sector_t)(sbi->sb->s_bdev->bd_inode->i_size >>
			SECTOR_SIZE_SHIFT);

	if (iblock > le64_to_cpu(extent_table->length))
		return -EINVAL;

	*result = (le64_to_cpu(extent_table->begin) + iblock);
	*result <<= (sbi->block_size_shift - SECTOR_SIZE_SHIFT);

	if (*result >= max_size)
		return -EINVAL;

	return 0;
}

static int get_meta_block(struct vdfs_sb_info *sbi, sector_t iblock,
		sector_t *result, sector_t *length)
{
	struct vdfs_layout_sb *l_sb = sbi->raw_superblock;
	struct vdfs_extent *extents = &l_sb->exsb.meta[0];
	int count;
	sector_t first_iblock = 0;
	sector_t last_iblock = 0;

	for (count = 0; count < VDFS_META_BTREE_EXTENTS; count++) {
		last_iblock += le64_to_cpu(extents->length);
		if (iblock >= first_iblock && iblock < last_iblock) {
			sector_t offset = iblock - first_iblock;
			*result = (le64_to_cpu(extents->begin)) +
					offset;
			*length = (le32_to_cpu(extents->length)) -
					offset;
			return 0;
		}
		first_iblock = last_iblock;
		extents++;
	}
	return -EINVAL;
}

int get_block_file_based(struct inode *inode, pgoff_t page_idx,
		sector_t *res_block)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	int ret = 0;
	struct vdfs_extent_info pext;

	if (!inode_info->installed_btrees) {
		ret = get_iblock_extent(inode, page_idx, &pext, NULL);
		if (ret)
			return ret;

		*res_block = pext.first_block + page_idx -
			pext.iblock;
	} else {
		struct vdfs_extent_info extent_image;
		sector_t image_iblock;
		memset(&extent_image, 0x0, sizeof(extent_image));

		/* get extent contains iblock*/
		ret = get_iblock_extent(inode, page_idx, &pext, NULL);
		if (ret)
			return ret;

		image_iblock = pext.first_block + (page_idx -
				pext.iblock);

		ret = get_iblock_extent(
			inode_info->installed_btrees->cat_tree->inode,
			image_iblock, &extent_image, NULL);
		if (ret)
			return ret;

		if (!extent_image.first_block)
			return -EINVAL;

		*res_block = extent_image.first_block + image_iblock -
			extent_image.iblock;
	}

	return 0;
}

static int get_block_meta_wrapper(struct inode *inode, pgoff_t page_idx,
		sector_t *res_block, int type, int start_block)
{
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	sector_t meta_iblock, length, start_iblock;
	sector_t iblock;
	struct buffer_head bh_result;
	int ret = 0;


	switch (type) {
	case VDFS_META_READ:
		BUG_ON((inode->i_ino < VDFS_FSFILE) ||
				(inode->i_ino > VDFS_LSFILE));
		ret = vdfs_get_meta_iblock(sbi, inode->i_ino, page_idx,
			&start_iblock);
		if (ret)
			return ret;

		meta_iblock = start_iblock;
		if (is_tree(inode->i_ino)) {
			unsigned int mask;

			mask = ((unsigned int)1 << (sbi->log_blocks_in_leb +
				sbi->block_size_shift -
				(unsigned int)PAGE_CACHE_SHIFT)) -
				(unsigned int)1;
			meta_iblock += (page_idx & (pgoff_t)mask)
				<< (PAGE_CACHE_SHIFT - sbi->block_size_shift);
		}
		*res_block = 0;
		ret = get_meta_block(sbi, meta_iblock, res_block, &length);
		BUG_ON(*res_block == 0);
		break;
	case VDFS_PACKTREE_READ:
		bh_result.b_blocknr = 0;
		iblock = ((sector_t)page_idx) << (PAGE_CACHE_SHIFT -
				sbi->block_size_shift);
		ret = vdfs_get_block(inode, iblock, &bh_result, 0);
		*res_block = bh_result.b_blocknr;
		break;
	case VDFS_FBASED_READ_M:
	case VDFS_FBASED_READ_C:
		*res_block = page_idx;
		break;
	case VDFS_FBASED_READ_UNC:
		ret = get_block_file_based(inode, (pgoff_t)start_block +
			(page_idx & ((1lu <<
			(pgoff_t)((pgoff_t)EMMCFS_I(inode)->log_chunk_size -
			(pgoff_t)PAGE_SHIFT)) - 1lu)) , res_block);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void table_end_IO(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);
		if (!uptodate)
			SetPageError(page);
		unlock_page(page);
	} while (bvec >= bio->bi_io_vec);

	bio_put(bio);
}

/**
 * @brief			Write meta data (struct page **)
 *				The function supports metadata fragmentation
 * @param [in]	sbi		The VDFS superblock information.
 * @param [in]	pages		Pointer to locked pages.
 * @param [in]	sector_addr	Start isector address.
 * @param [in]	page_count	Number of pages to be written.
 *				    and write snapshot head page in sync mode
 * @return			Returns 0 on success, errno on failure.
 */
int vdfs_table_IO(struct vdfs_sb_info *sbi, void *buffer,
		__u64 buffer_size, int rw, sector_t *iblock)
{
	struct block_device *bdev = sbi->sb->s_bdev;
	struct bio *bio;
	sector_t start_sector = 0;
	sector_t submited_pages = 0;
	int ret;
	unsigned int count = 0, nr_vectr;
	unsigned int pages_count = (unsigned int)DIV_ROUND_UP(buffer_size,
			(__u64)PAGE_SIZE);
	struct blk_plug plug;
	struct page *page;

	blk_start_plug(&plug);

	do {
		nr_vectr = (pages_count < BIO_MAX_PAGES) ? pages_count :
				BIO_MAX_PAGES;

		ret = vdfs_get_table_sector(sbi, *iblock, &start_sector);
		if (ret)
			goto error_exit;

		bio = allocate_new_bio(bdev, start_sector, nr_vectr);
		if (IS_ERR_OR_NULL(bio)) {
			ret = -ENOMEM;
			goto error_exit;
		}

		bio->bi_end_io = table_end_IO;

		do {
			int size;

			page = vmalloc_to_page((char *)buffer +
					(submited_pages << PAGE_SHIFT));
			BUG_ON(!page);
			lock_page(page);
			size = bio_add_page(bio, page, PAGE_SIZE, 0);
			if (!size && (!bio->bi_vcnt)) {
				/* fail to add data into BIO */
				ret = -EFAULT;
				bio_put(bio);
				goto error_exit;
			} else if (!size) {
				/* no space left in bio */
				break;
			}
			pages_count--;
			submited_pages++;
		} while (pages_count > 0);
		submit_bio(rw, bio);

	} while (pages_count > 0);

error_exit:
	blk_finish_plug(&plug);

	for (count = 0; count < submited_pages; count++) {
		page = vmalloc_to_page((char *)buffer + (count << PAGE_SHIFT));
		BUG_ON(!page);
		wait_on_page_locked(page);
		if (TestClearPageError(page))
			ret = -EIO;
	}

	if (!ret)
		*iblock += submited_pages;

	return ret;
}

/**
 * @brief			Read page from the given sector address.
 *				Fill the locked page with data located in the
 *				sector address. Read operation is synchronous,
 *				and caller must unlock the page.
 * @param [in]	bdev		Pointer to block device.
 * @param [in]	page		Pointer to locked page.
 * @param [in]	sector_addr	Sector address.
 * @param [in]	page_count	Number of pages to be read.
 * @return			Returns 0 on success, errno on failure
 */
int vdfs_read_pages(struct block_device *bdev,
			struct page **page,
			sector_t sector_addr,
			unsigned int pages_count)
{
	struct bio *bio;
	struct completion wait;
	unsigned int count = 0;
	int continue_load = 0;

	struct blk_plug plug;

	if (pages_count > BIO_MAX_PAGES) {
		/* the function supports only one */
		EMMCFS_ERR("to many pages to read");
		return -EINVAL;
	}

	init_completion(&wait);
again:
	blk_start_plug(&plug);

	/* Allocate a new bio */
	bio = allocate_new_bio(bdev, sector_addr, pages_count);
	if (IS_ERR_OR_NULL(bio)) {
		blk_finish_plug(&plug);
		EMMCFS_ERR("failed to allocate bio\n");
		return PTR_ERR(bio);
	}

	bio->bi_end_io = read_end_io;

	/* Initialize the bio */
	for (; count < pages_count; count++) {
		if ((unsigned) bio_add_page(bio, page[count],
				PAGE_CACHE_SIZE, 0) < PAGE_CACHE_SIZE) {
			if (bio->bi_vcnt) {
				continue_load = 1;
				sector_addr += (count << (PAGE_CACHE_SHIFT -
						SECTOR_SIZE_SHIFT));
			} else {
				EMMCFS_ERR("FAIL to add page to BIO");
				bio_put(bio);
				blk_finish_plug(&plug);
				return -EFAULT;
			}

			break;
		}
	}
	bio->bi_private = &wait;
	submit_bio(READ, bio);
	blk_finish_plug(&plug);

	/* Synchronous read operation */
	wait_for_completion(&wait);

	if (continue_load) {
		continue_load = 0;
		goto again;
	}

	return 0;
}


/**
 * @brief			Read page from the given sector address.
 *				Fill the locked page with data located in the
 *				sector address. Read operation is synchronous,
 *				and caller must unlock the page.
 * @param [in]	bdev		Pointer to block device.
 * @param [in]	page		Pointer to locked page.
 * @param [in]	sector_addr	Sector address.
 * @param [in]	sector_count	Number of sectors to be read.
 * @param [in]	offset		Offset value in page.
 * @return			Returns 0 on success, errno on failure.
 */
int vdfs_read_page(struct block_device *bdev,
			struct page *page,
			sector_t sector_addr,
			unsigned int sector_count,
			unsigned int offset)
{
	struct bio *bio;
	struct completion wait;
	struct blk_plug plug;

	if (sector_count > SECTOR_PER_PAGE + 1)
		return -EINVAL;

	if (PageUptodate(page))
		return 0;

	/* Allocate a new bio */
	bio = allocate_new_bio(bdev, sector_addr, 1);
	if (IS_ERR_OR_NULL(bio))
		return PTR_ERR(bio);

	init_completion(&wait);
	blk_start_plug(&plug);

	/* Initialize the bio */
	bio->bi_end_io = read_end_io;
	if ((unsigned) bio_add_page(bio, page, SECTOR_SIZE * sector_count,
			offset)	< SECTOR_SIZE * sector_count) {
		EMMCFS_ERR("FAIL to add page to BIO");
		bio_put(bio);
		blk_finish_plug(&plug);
		return -EFAULT;
	}
	bio->bi_private = &wait;
	submit_bio(READ, bio);
	blk_finish_plug(&plug);

	/* Synchronous read operation */
	wait_for_completion(&wait);

	if (PageError(page))
		return -EFAULT;

	return 0;
}

/**
 * @brief			Write page to the given sector address.
 *				Write the locked page to the sector address.
 *				Write operation is synchronous, and caller
 *				must unlock the page.
 * @param [in]	bdev		The eMMCFS superblock information.
 * @param [in]	page		Pointer to locked page.
 * @param [in]	sector_addr	Sector address.
 * @param [in]	sector_count	Number of sector to be written.
 * @param [out] written_bytes	Number of actually written bytes
 * @return			Returns 0 on success, errno on failure.
 */
int vdfs_write_page(struct vdfs_sb_info *sbi,
			struct page *page,
			sector_t sector_addr,
			unsigned int sector_count,
			unsigned int offset, int sync_mode)
{
	struct bio *bio;
	struct completion wait;
	struct block_device *bdev = sbi->sb->s_bdev;
	struct blk_plug plug;

	if (sector_count > SECTOR_PER_PAGE) {
		end_page_writeback(page);
		return -EINVAL;
	}

	if (VDFS_IS_READONLY(sbi->sb)) {
		end_page_writeback(page);
		return 0;
	}

	/* Allocate a new bio */
	bio = allocate_new_bio(bdev, sector_addr, 1);
	if (IS_ERR_OR_NULL(bio)) {
		end_page_writeback(page);
		return PTR_ERR(bio);
	}

	blk_start_plug(&plug);
	if (sync_mode)
		init_completion(&wait);

	/* Initialize the bio */
	bio->bi_end_io = end_io_write;
	if ((unsigned) bio_add_page(bio, page, SECTOR_SIZE * sector_count,
			offset) < SECTOR_SIZE * sector_count) {
		EMMCFS_ERR("FAIL to add page to BIO");
		bio_put(bio);
		blk_finish_plug(&plug);
		end_page_writeback(page);
		return -EFAULT;
	}
	if (sync_mode)
		bio->bi_private = &wait;

	submit_bio(WRITE_FLUSH_FUA, bio);
	blk_finish_plug(&plug);
	if (sync_mode) {
		/* Synchronous write operation */
		wait_for_completion(&wait);
	}
	return 0;
}

/**
 * @brief		Sign  pages with crc number
 * @param [in]	mapping	Mapping with pages to sign
 * @param [in]	magic_len	Length of the magic string, the first
 *				magic_len bytes will be skiped during crc
 *				calculation.
 * @return			0 - if page signed successfully
 *				or error code otherwise
 */
static int vdfs_check_and_sign_pages(struct page *page, char *magic,
		unsigned int magic_len, __u64 version)
{
	void *data;
	data = kmap(page);
	if (!data) {
		EMMCFS_ERR("Can not allocate virtual memory");
		return -ENOMEM;
	}
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	if (memcmp(data, magic, magic_len - VERSION_SIZE) !=
					0) {
		printk(KERN_INFO" invalide bitmap magic for %s,"
			" %lu, actual = %s\n", magic,
			page->mapping->host->i_ino, (char *)data);
		BUG();
	}
#endif
	memcpy((char *)data + magic_len - VERSION_SIZE, &version, VERSION_SIZE);
	vdfs_update_block_crc(data, PAGE_SIZE, magic_len);
	kunmap(page);
	return 0;
}

static int vdfs_validate_bitmap(struct page *page, void *buff,
		unsigned int buff_size, const char *magic,
		unsigned int magic_len)
{
	struct vdfs_sb_info *sbi = VDFS_SB(page->mapping->host->i_sb);
	__le64 real_version, version = vdfs_get_page_version(sbi,
			page->mapping->host, page->index);
#ifdef CONFIG_VDFS_CRC_CHECK
	unsigned int crc;
#endif
	int ret_val = 0;
	struct vdfs_meta_block *p_buff = buff;


	/* check magic */
	if (magic) {
		/* if magic is not valid */
		if (memcmp(buff, magic, magic_len - VERSION_SIZE) != 0) {
			EMMCFS_ERR("read %s bitmap from disk fail: wrong magic"
					, magic);
			ret_val = -EINVAL;
		}
	}

	real_version = ((((__le64)(p_buff->mount_count)) << 32)
			| p_buff->sync_count);
	if (real_version != version) {
		EMMCFS_ERR("read bitmap %s from disk fail:version missmatch "
				"iblock:%lu,"
				"must be :%u.%u, "
				"readed :%u.%u", magic,
				page->index,
				VDFS_MOUNT_COUNT(version),
				VDFS_SYNC_COUNT(version),
				VDFS_MOUNT_COUNT(real_version),
				VDFS_SYNC_COUNT(real_version));
		ret_val = -EINVAL;
	}

#ifdef CONFIG_VDFS_CRC_CHECK
	crc = cpu_to_le32(crc32(0, (char *)buff + magic_len, buff_size -
			(CRC32_SIZE + magic_len)));
	if (memcmp(VDFS_CRC32_OFFSET((char *)buff, buff_size), &crc,
				CRC32_SIZE) != 0) {
		EMMCFS_ERR("read bimap %s:%lu from disk fail: CRC missmatch"
				, magic, page->index);
		ret_val = -EINVAL;

		pr_err("index:%lu phy addr: 0x%llx\n", page->index,
				(long long unsigned int)
				page_to_phys(page));

#ifdef CONFIG_VDFS_DEBUG
		if (!(VDFS_IS_READONLY(sbi->sb))) {
			int ret;
			struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;

			sector_t debug_area =
				le64_to_cpu(vdfs_sb->exsb.debug_area.begin);


			EMMCFS_ERR("dump bitmap page to disk");
			set_page_writeback(page);
			ret = vdfs_write_page(sbi, page, debug_area <<
				(PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT),
				8, 0, 1);

			if (ret)
				EMMCFS_ERR("fail to write a page to flash");
		} else
			EMMCFS_ERR("can not dump page to disk: read only fs");
		mutex_lock(&sbi->dump_meta);
		preempt_disable();
		_sep_printk_start();
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS,
				16, 1, buff, PAGE_SIZE, 1);
		preempt_enable();
		_sep_printk_end();
		mutex_unlock(&sbi->dump_meta);
#endif
	}
#endif
	return ret_val;
}
/**
 * @brief		Validata page crc and magic number
 * @param [in]	page	page to validate
 * @param [in]	magic	magic to validate
 * @param [in]	magic_len	magic len in bytes
 * @return			0 - if crc and magic are valid
 *				1 - if crc or magic is invalid
 */
static int vdfs_validate_page(struct page *page)
{
	void *data;
	int ret_val = 0;
	char *magic;
	unsigned int magic_len;
	ino_t ino = page->mapping->host->i_ino;

	if ((ino > (ino_t)VDFS_LSFILE) || is_tree(ino))
		return 0;

	BUG_ON(!PageLocked(page));

	switch (ino) {
	case VDFS_FREE_INODE_BITMAP_INO:
		magic = INODE_BITMAP_MAGIC;
		magic_len = INODE_BITMAP_MAGIC_LEN;
		break;
	case VDFS_SPACE_BITMAP_INO:
		magic = FSM_BMP_MAGIC;
		magic_len = FSM_BMP_MAGIC_LEN;
		break;
	default:
		return ret_val;
	}
	data = kmap(page);
	if (!data) {
		EMMCFS_ERR("Can not allocate virtual memory");
		return -ENOMEM;
	}
	ret_val = vdfs_validate_bitmap(page, data, PAGE_SIZE, magic, magic_len);

	kunmap(page);
	return ret_val;
}

/**
 * @brief				Update the buffer with magic and crc
 *					numbers. the magic will be placed in
 *					first bytes, the crc will be placed
 *					in last 4 bytes.
 * @param [in]	buff			Buffer to update.
 * @param [in]	block_size		Block size
 * @param [in]	magic_len		Length of the magic string, the first
 *				magic_len bytes will be skiped during crc
 *				calculation.
 */
void vdfs_update_block_crc(char *buff, unsigned int blck_size,
		unsigned int magic_len)
{
#ifdef CONFIG_VDFS_CRC_CHECK
	unsigned int crc = 0;
	/* set crc to the end of the buffer */
	crc = cpu_to_le32(crc32(0, buff + magic_len,
		blck_size - (CRC32_SIZE  +
		magic_len)));
	memcpy(VDFS_CRC32_OFFSET(buff, blck_size), &crc, CRC32_SIZE);
#endif
}


/**
 * @brief				Set bits inside buffer and update
 *					sign and crc values for updated
 *					buffer.
 * @param [in]	buff			Buffer to validate.
 * @param [in]	buff_size		Size of the buffer
 * @param [in]	offset			Offset of the start bit for setting
 * @param [in]	count			Number of bits to be set
 * @param [in]	magic_len		Length of the magic word in bytes
 * @param [in]	block_size		Size of block for block device.

 */
int vdfs_set_bits(char *buff, int buff_size, unsigned int offset,
		unsigned int count, unsigned int magic_len,
		unsigned int blck_size) {
	/* data block size in bits */
	const unsigned int datablock_size = (blck_size - (magic_len
				+ CRC32_SIZE)) << 3;
	/* pointer to begin of start block */
	char *start_blck = buff + ((offset / datablock_size) * blck_size);
	/* pointer to last block */
	char *end_blck =  buff + (((offset + count - 1) / datablock_size) *
			blck_size);
	char *cur_blck = NULL;
	unsigned int cur_position = 0;
	u_int32_t length = 0, i = 0;
	char *end_buff;


	for (cur_blck = start_blck; cur_blck <= end_blck;
				cur_blck += blck_size) {
		/* if it first block */
		if (cur_blck == start_blck)
			cur_position = offset % datablock_size;
		else
			cur_position = 0;

		length = (datablock_size - cur_position);
		if (count < length)
			length = count;
		else
			count -= length;
		end_buff = cur_blck + blck_size - CRC32_SIZE;
		/* set bits */
		for (i = 0; i < length; i++) {
			/* check the bound of array */
			if ((cur_blck + (cur_position>>3) +
					magic_len) > end_buff)
				return -EFAULT;
			/* set bits */
			if (test_and_set_bit((int)cur_position,
				(void *)(cur_blck + magic_len)))
				return -EFAULT;

			cur_position++;
		}
	}
	return 0;
}

/**
 * @brief			Clear bits inside buffer and update
 *					sign and crc values for updated
 *					buffer.
 * @param [in]	buff			Buffer to validate.
 * @param [in]	buff_size		Size of the buffer
 * @param [in]	offset			Offset of the start bit for setting
 * @param [in]	count			Number of bits to be set
 * @param [in]	magic_len		Length of the magic word in bytes
 * @param [in]	block_size		Size of block for block device.
 * @return				Error code or 0 if success
 */
int vdfs_clear_bits(char *buff, int buff_size, unsigned int offset,
		unsigned int count, unsigned int magic_len,
		unsigned int blck_size) {
	/* data block size in bits */
	const unsigned int datablock_size = (blck_size - (magic_len
				+ CRC32_SIZE))<<3;
	/* pointer to begin of start block */
	char *start_blck = buff + ((offset / datablock_size) * blck_size);
	/* pointer to last block */
	char *end_blck =  buff + (((offset + count - 1) / datablock_size) *
			blck_size);
	char *cur_blck = NULL;
	unsigned int cur_position = 0;
	u_int32_t length = 0, i = 0;
	char *end_buff;

	/* go through all blcoks */
	for (cur_blck = start_blck; cur_blck <= end_blck;
				cur_blck += blck_size) {
		/* if it first block */
		if (cur_blck == start_blck)
			cur_position = offset % datablock_size;
		else
			cur_position = 0;

		length = (datablock_size - cur_position);
		if (count < length) {
			length = count;
			count -= length;
		} else
			count -= length;
		end_buff = cur_blck + blck_size - CRC32_SIZE;
		/* set bits */
		for (i = 0; i < length; i++) {
			/* check the boundary of array */
			if ((cur_blck + (cur_position>>3) + magic_len)
					> end_buff)
				return -EFAULT;

			/* set bits */
			if (!test_and_clear_bit((int)cur_position,
					(void *)(cur_blck + magic_len)))
				return -EFAULT;
			cur_position++;
		}
	}
	return 0;
}

/**
 * @brief			Fill buffer with zero and update the
 *				buffer with magic.
 * @param [in]	buff		Buffer to update.
 * @param [in]	block_size	Block size
 * @param [in]	ino		Inode number
 */
static int vdfs_init_bitmap_page(struct vdfs_sb_info *sbi, ino_t ino_n,
		struct page *page)
{
	void *bitmap;
	struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;

	__u64 version = ((__u64)le32_to_cpu(vdfs_sb->exsb.mount_counter) << 32)
			| sbi->snapshot_info->sync_count;

	if (ino_n == VDFS_FREE_INODE_BITMAP_INO) {
		bitmap = kmap_atomic(page);
		if (!bitmap)
			return -ENOMEM;
		memset((char *)bitmap, 0, PAGE_CACHE_SIZE);
		memcpy((char *)bitmap, INODE_BITMAP_MAGIC,
				INODE_BITMAP_MAGIC_LEN - VERSION_SIZE);
		memcpy((char *)bitmap + INODE_BITMAP_MAGIC_LEN - VERSION_SIZE,
				&version, VERSION_SIZE);
		kunmap_atomic(bitmap);
	}

	return 0;
}

static void __dump_tagged_pages(struct address_space *mapping, unsigned tag)
{
	struct radix_tree_iter iter;
	void **slot;

	radix_tree_for_each_tagged(slot, &mapping->page_tree, &iter, 0, tag) {
		struct page *page = *slot;

		pr_err("mapping %p index %ld page %p\n",
		       mapping, iter.index, page);
		if (page)
			pr_err("page %ld mapping %p index %ld "
			       "flags %lx refcount %d\n",
			       page_to_pfn(page), page->mapping, page->index,
			       page->flags, page_count(page));
	}
}

static void dump_tagged_pages(struct address_space *mapping, unsigned tag)
{
	spin_lock_irq(&mapping->tree_lock);
	__dump_tagged_pages(mapping, tag);
	spin_unlock_irq(&mapping->tree_lock);
}

static struct address_space *vdfs_next_mapping(struct vdfs_sb_info *sbi,
		struct address_space *current_mapping)
{
	ino_t ino;

	ino = (current_mapping == NULL) ? 0 : current_mapping->host->i_ino;

	switch (ino) {
	case (0):
		return sbi->catalog_tree->inode->i_mapping;
	break;
	case (VDFS_CAT_TREE_INO):
		return sbi->fsm_info->bitmap_inode->i_mapping;
	break;
	case (VDFS_SPACE_BITMAP_INO):
		return sbi->extents_tree->inode->i_mapping;
	break;
	case (VDFS_EXTENTS_TREE_INO):
		return sbi->free_inode_bitmap.inode->i_mapping;
	break;
	case (VDFS_FREE_INODE_BITMAP_INO):
		return sbi->xattr_tree->inode->i_mapping;
	case (VDFS_XATTR_TREE_INO):
		return NULL;
	default:
	return NULL;
	}
}

static int vdfs_sign_mapping_pages(struct vdfs_sb_info *sbi,
		struct pagevec *pvec, unsigned long ino);

static int get_pages_from_mapping(struct vdfs_sb_info *sbi,
		struct address_space **current_mapping,
		struct pagevec *pvec, pgoff_t *index)
{
	unsigned nr_pages = 0;
	ino_t ino;
	int ret;
	unsigned long size;

	do {
		if (*current_mapping) {
			size = (is_tree(current_mapping[0]->host->i_ino)) ?
					(unsigned long)
					(PAGEVEC_SIZE - (PAGEVEC_SIZE %
					(1 << (sbi->log_blocks_in_page
					+ sbi->log_blocks_in_leb)))) :
					(unsigned long)PAGEVEC_SIZE;
			nr_pages = pagevec_lookup_tag(pvec, *current_mapping,
				index, PAGECACHE_TAG_DIRTY, size);

			ino = current_mapping[0]->host->i_ino;

			ret = vdfs_sign_mapping_pages(sbi, pvec, ino);
			if (ret) {
				pagevec_release(pvec);
				return ret;
			}
		}

		if (!nr_pages) {
			*current_mapping = vdfs_next_mapping(sbi,
					*current_mapping);
			*index = 0;

			if (*current_mapping &&
			    mapping_tagged(*current_mapping,
				    PAGECACHE_TAG_WRITEBACK)) {
				EMMCFS_ERR("inode #%ld already has writeback",
					   (*current_mapping)->host->i_ino);
				dump_tagged_pages(*current_mapping,
						PAGECACHE_TAG_WRITEBACK);
			}
		}

	} while ((!nr_pages) && *current_mapping);

#ifdef CONFIG_VDFS_DEBUG
	if (nr_pages)
		vdfs_check_moved_iblocks(sbi, pvec->pages, nr_pages);
#endif

	return (int)nr_pages;
}




static void meta_end_IO(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct vdfs_sb_info *sbi =
			bvec->bv_page->mapping->host->i_sb->s_fs_info;

	if (bio->bi_rw & WRITE) {
		EMMCFS_BUG_ON(atomic_read(&sbi->meta_bio_count) <= 0);
		if (atomic_dec_and_test(&sbi->meta_bio_count))
			wake_up_all(&sbi->meta_bio_wait);
	}

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (!uptodate) {
			SetPageError(page);
			if (page->mapping)
				set_bit(AS_EIO, &page->mapping->flags);
		}

		if (bio->bi_rw & WRITE) {
			end_page_writeback(page);
		} else {
			if (uptodate)
				SetPageUptodate(page);
			unlock_page(page);
		}

	} while (bvec >= bio->bi_io_vec);

	bio_put(bio);
}

static struct bio *allocate_new_request(struct vdfs_sb_info *sbi, sector_t
		start_block, unsigned size)
{
	struct bio *bio;
	sector_t start_sector = start_block << (sbi->block_size_shift -
			SECTOR_SIZE_SHIFT);
	struct block_device *bdev = sbi->sb->s_bdev;
	unsigned int bio_size = (size > BIO_MAX_PAGES) ? BIO_MAX_PAGES : size;

	bio = allocate_new_bio(bdev, start_sector, bio_size);
	if (bio)
		bio->bi_end_io = meta_end_IO;

	return bio;
}


static int vdfs_sign_mapping_pages(struct vdfs_sb_info *sbi,
		struct pagevec *pvec, unsigned long ino)
{
	int ret = 0;
	struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;

	__u64 version = ((__u64)le32_to_cpu(vdfs_sb->exsb.mount_counter) << 32)
		| sbi->snapshot_info->sync_count;
	unsigned int magic_len = 0;
	struct vdfs_base_table_record *table = vdfs_get_table(sbi, ino);
	if (is_tree(ino)) {
		__le64 table_index = 0;
		struct vdfs_btree *tree;
		unsigned int i;
		switch (ino) {
		case VDFS_CAT_TREE_INO:
			tree = sbi->catalog_tree;
			break;
		case VDFS_EXTENTS_TREE_INO:
			tree = sbi->extents_tree;
			break;
		case VDFS_XATTR_TREE_INO:
			tree = sbi->xattr_tree;
			break;
		default:
			return -EFAULT;
		}

		for (i = 0; i < pvec->nr; i += tree->pages_per_node) {
			struct page **pages = pvec->pages + i;
			unsigned int j;

			if ((pvec->nr - i) < tree->pages_per_node) {
				EMMCFS_ERR("incomplete bnode: %ld %ld %ld",
						ino, pages[0]->index,
						pvec->nr - i);
				return -EFAULT;
			}

			for (j = 1; j < tree->pages_per_node; j++) {
				if (pages[j]->index != pages[0]->index + j) {
					EMMCFS_ERR("noncontiguous bnode pages: "
							"%ld %ld != %ld + %d",
							ino, pages[j]->index,
							pages[0]->index, j);
					return -EFAULT;
				}
			}

			ret = vdfs_check_and_sign_dirty_bnodes(pages,
					tree, version);
			if (ret)
				break;
			table_index = pvec->pages[i]->index
					>> (sbi->log_blocks_in_leb +
					sbi->block_size_shift - PAGE_SHIFT);
			table[table_index].mount_count =
					vdfs_sb->exsb.mount_counter;
			table[table_index].sync_count =
					sbi->snapshot_info->sync_count;
		}
	} else {
		unsigned int i;
		char *magic = NULL;
		switch (ino) {
		case VDFS_FREE_INODE_BITMAP_INO:
			magic = INODE_BITMAP_MAGIC;
			magic_len = INODE_BITMAP_MAGIC_LEN;
			break;
		case VDFS_SPACE_BITMAP_INO:
			magic = FSM_BMP_MAGIC;
			magic_len = FSM_BMP_MAGIC_LEN;
			break;
		default:
			return -EFAULT;
		}
		for (i = 0; i < pvec->nr; i++) {

			ret = vdfs_check_and_sign_pages(pvec->pages[i], magic,
				magic_len, version);
			if (ret)
				break;
			table[pvec->pages[i]->index].mount_count =
				vdfs_sb->exsb.mount_counter;
			table[pvec->pages[i]->index].sync_count =
					sbi->snapshot_info->sync_count;
		}
	}
	return ret;
}

/**
 * @brief			Write meta data (struct page **)
 *				The function supports metadata fragmentation
 * @param [in]	sbi		The VDFS superblock information.
 * @param [in]	pages		Pointer to locked pages.
 * @param [in]	sector_addr	Start isector address.
 * @param [in]	page_count	Number of pages to be written.
 *				    and write snapshot head page in sync mode
 * @return			Returns 0 on success, errno on failure.
 */
static int vdfs_meta_write(struct vdfs_sb_info *sbi)
{
	struct address_space *current_mapping = NULL;
	pgoff_t next_index = 0;
	sector_t next_block = 0, block;
	struct bio *bio = NULL;
	struct blk_plug plug;
	struct pagevec pvec;
	struct page *page;
	int ret, ret2;
	unsigned int i = 0;

	pagevec_init(&pvec, 0);
	blk_start_plug(&plug);

	while (1) {
		if (i == pvec.nr) {
			pagevec_release(&pvec);
			ret = get_pages_from_mapping(sbi,
					&current_mapping, &pvec, &next_index);
			if (ret <= 0)
				break;
			i = 0;
		}

		page = pvec.pages[i];

		ret = get_block_meta_wrapper(current_mapping->host,
				page->index, &block, 0, 0);
		BUG_ON(ret);

		lock_page(page);
		BUG_ON(!PageDirty(page));
		BUG_ON(PageWriteback(page));
		BUG_ON(page->mapping != current_mapping);
		clear_page_dirty_for_io(page);
		set_page_writeback(page);
		unlock_page(page);

		while (!bio || next_block != block ||
		       !bio_add_page(bio, page, PAGE_CACHE_SIZE, 0)) {
			if (bio) {
				atomic_inc(&sbi->meta_bio_count);
				submit_bio(WRITE_FUA, bio);
			}
			bio = allocate_new_request(sbi, block, pvec.nr - i);
			next_block = block;
		}

		i++;
		next_block += (unsigned)(1 << sbi->log_blocks_in_page);
#ifdef CONFIG_VDFS_STATISTIC
		sbi->umount_written_bytes += PAGE_CACHE_SIZE;
#endif
	}

	if (bio) {
		atomic_inc(&sbi->meta_bio_count);
		submit_bio(WRITE_FUA, bio);
	}

	blk_finish_plug(&plug);

	current_mapping = NULL;
	while ((current_mapping = vdfs_next_mapping(sbi, current_mapping))) {
		ret2 = filemap_fdatawait_range(current_mapping, 0, LLONG_MAX);
		if (ret2) {
			vdfs_fatal_error(sbi,
				"cannot write matadata inode %lu: %d",
				current_mapping->host->i_ino, ret2);
			ret = ret2;
		}

		spin_lock_irq(&current_mapping->tree_lock);
		if (mapping_tagged(current_mapping, PAGECACHE_TAG_DIRTY)) {
			EMMCFS_ERR("inode #%ld has dirty tag set",
					current_mapping->host->i_ino);
			__dump_tagged_pages(current_mapping,
					    PAGECACHE_TAG_DIRTY);
			ret = -EFAULT;
		}
		if (mapping_tagged(current_mapping, PAGECACHE_TAG_WRITEBACK)) {
			EMMCFS_ERR("inode #%ld has writeback tag set",
					current_mapping->host->i_ino);
			__dump_tagged_pages(current_mapping,
					    PAGECACHE_TAG_WRITEBACK);
			ret = -EFAULT;
		}
		spin_unlock_irq(&current_mapping->tree_lock);
	}

	if (atomic_read(&sbi->meta_bio_count)) {
		/* it must be never happened */
		EMMCFS_ERR("not all bio complited");
		wait_event_timeout(sbi->meta_bio_wait,
				!atomic_read(&sbi->meta_bio_count), HZ * 5);
	}

	return ret;
}

/**
 * @brief			Read meta data (struct page **)
 *				The function supports metadata fragmentation
 *				non-Uptodate pages must be locked
 * @param [in]	sbi		The VDFS superblock information.
 * @param [in]	pages		Pointer to locked pages.
 * @param [in]	sector_addr	Start isector address.
 * @param [in]	page_count	Number of pages to be written.
 *				    and write snapshot head page in sync mode
 * @return			Returns 0 on success, errno on failure.
 */
int vdfs__read(struct inode *inode, int type, struct page **pages,
		unsigned int pages_count, int start_block)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct bio *bio = NULL;
	struct blk_plug plug;
	int size;
	int ret = 0;
	sector_t last_block = 0, block;

	unsigned int blocks_per_page = (unsigned)(1 << (PAGE_CACHE_SHIFT -
			sbi->block_size_shift));
	unsigned int count;

	blk_start_plug(&plug);

	for (count = 0; count < pages_count; count++) {
		struct page *page = pages[count];

		if (PageUptodate(page)) {
			unlock_page(page);
			continue;
		}

		BUG_ON(!PageLocked(page));

		ret = get_block_meta_wrapper(inode, page->index, &block, type,
				start_block);
		if (ret || (block == 0)) {
			ret = (block == 0) ? -EINVAL : ret;
			for (; count < pages_count; count++)
				unlock_page(pages[count]);
			goto exit;
		}

		if (last_block + blocks_per_page != block) {
			if (bio)
				submit_bio(READ, bio);
again:
			bio = allocate_new_request(sbi, block,
					pages_count - count);
			if (!bio)
				goto again;
		} else if (!bio) {
			blk_finish_plug(&plug);
			return -EINVAL;
		}

		size = bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
		if (size < (int)PAGE_CACHE_SIZE) {
			submit_bio(READ, bio);
			bio = NULL;
			goto again;
		}
		last_block = block;
	};

exit:
	if (bio)
		submit_bio(READ, bio);

	blk_finish_plug(&plug);

	for (count = 0; count < pages_count; count++) {
		if (!PageUptodate(pages[count]))
			wait_on_page_locked(pages[count]);
		if (TestClearPageError(pages[count]))
			ret = -EIO;
	}

	return ret;
}

int vdfs_sync_metadata(struct vdfs_sb_info *sbi)
{
	int ret = 0;

	if (sbi->snapshot_info->dirty_pages_count == 0)
		return 0;

	if (sbi->sb->s_flags & MS_RDONLY) {
		EMMCFS_ERR("Can't sync on read-only filesystem");
		return 0;
	}

	if (is_sbi_flag_set(sbi, EXSB_DIRTY)) {
		ret = vdfs_sync_second_super(sbi);
		sbi->snapshot_info->use_base_table = 1;
		if (ret) {
			vdfs_fatal_error(sbi, "cannot sync 2nd sb: %d", ret);
			return ret;
		}
	}

	ret = vdfs_meta_write(sbi);
	if (ret)
		return ret;

	vdfs_update_bitmaps(sbi);
	ret = update_translation_tables(sbi);
	if (ret) {
		vdfs_fatal_error(sbi,
				"cannot commit translation tables: %d", ret);
		return ret;
	}

	if (is_sbi_flag_set(sbi, EXSB_DIRTY)) {
		ret = vdfs_sync_first_super(sbi);
		clear_sbi_flag(sbi, EXSB_DIRTY);
		if (ret)
			vdfs_fatal_error(sbi, "cannot sync 1st sb: %d", ret);
	}

	vdfs_commit_free_space(sbi);

	return ret;
}

int vdfs_read_comp_pages(struct inode *inode, pgoff_t index,
			      int pages_count, struct page **pages,
			      enum vdfs_read_type type)
{
	struct address_space *mapping = NULL;
	int count, ret = 0;
	struct page *page = NULL;
	sector_t page_idx;
	if (type != VDFS_FBASED_READ_C && type != VDFS_FBASED_READ_M) {
		EMMCFS_ERR("function can't be used for data type %d", type);
		return -EINVAL;
	}
	mapping = inode->i_sb->s_bdev->bd_inode->i_mapping;


	for (count = 0; count < pages_count; count++) {
		ret = get_block_file_based(inode, index + (pgoff_t)count,
				&page_idx);
		if (ret)
			goto exit_alloc_page;
		page = find_or_create_page(mapping, (pgoff_t)page_idx,
				GFP_NOFS | __GFP_HIGHMEM);
		if (!page) {
			ret = -ENOMEM;
			goto exit_alloc_page;
		}

		if (!PageChecked(page))
			ClearPageUptodate(page);
		pages[count] = page;
	}

	ret = vdfs__read(inode, type, pages, (unsigned)pages_count, 0);
	if (ret)
		goto exit_read_data;
	if (PageUptodate(page))
		PageChecked(page);
	return ret;
exit_alloc_page:
	EMMCFS_ERR("Error in allocate page");
	for (; count > 0; count--) {
		unlock_page(pages[count - 1]);
		page_cache_release(pages[count - 1]);
	}
	return ret;
exit_read_data:
	EMMCFS_ERR("Error in exit_read data");
	release_pages(pages, pages_count, 0);
	return ret;

}

/* type : 0 - meta , 1 - packtree, 2 - filebased decompression */
int vdfs_read_or_create_pages(struct inode *inode, pgoff_t index,
			      unsigned int pages_count, struct page **pages,
			      enum vdfs_read_type type, int start_block,
			      int force_insert)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct address_space *mapping = NULL;
	int count, ret = 0;
	struct page *page = NULL;
	char is_new = 0;
	int validate_err;
#ifdef CONFIG_VDFS_DEBUG
	int reread_count = VDFS_META_REREAD;
#endif
	if (type == VDFS_META_READ && (inode->i_ino <= VDFS_LSFILE)) {
		ret = vdfs_check_page_offset(sbi, inode, index, &is_new,
				force_insert);
		if (ret)
			return ret;
	}

	mapping = inode->i_mapping;

	for (count = 0; count < (int)pages_count; count++) {
		page = find_or_create_page(mapping, index + (unsigned)count,
				GFP_NOFS | __GFP_HIGHMEM);
		if (!page) {
			ret = -ENOMEM;
			goto exit_alloc_page;
		}
		pages[count] = page;

		if (is_new) {
			ret = vdfs_init_bitmap_page(sbi, inode->i_ino, page);
			if (ret)
				goto exit_alloc_locked_page;
			SetPageUptodate(page);
			SetPageChecked(page);
		}
	}
#ifdef CONFIG_VDFS_DEBUG
do_reread:
#endif
	validate_err = 0;
	ret = vdfs__read(inode, type, pages, pages_count, start_block);
	if (ret)
		goto exit_validate_page;

	if (inode->i_ino > VDFS_LSFILE)
		return ret;

	for (count = 0; count < (int)pages_count; count++) {
		page = pages[count];
		if (!PageChecked(page) && (!is_tree(inode->i_ino))) {
			lock_page(page);
			if (PageChecked(page)) {
				unlock_page(page);
				continue;
			}

			ret = vdfs_validate_page(page);
			if (ret) {
				validate_err = 1;
				ClearPageUptodate(page);
			} else
				SetPageChecked(page);
			unlock_page(page);
		}
	}

	ret = (validate_err) ? -EINVAL : 0;

#ifdef CONFIG_VDFS_DEBUG
	if (ret && (--reread_count >= 0)) {
		pr_err("do re-read bitmap %d\n",
			VDFS_META_REREAD -
			reread_count);
		for (count = 0; count < (int)pages_count; count++)
			lock_page(pages[count]);
		goto do_reread;
	}
#endif

	if (ret && (is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)))
			vdfs_fatal_error(sbi, "bitmap validate FAIL");

	if (ret)
		goto exit_validate_page;

	return ret;
exit_alloc_page:
	EMMCFS_ERR("Error in allocate page");
	for (; count > 0; count--) {
		unlock_page(pages[count - 1]);
		page_cache_release(pages[count - 1]);
	}
	return ret;
exit_alloc_locked_page:
	EMMCFS_ERR("Error in init bitmap page");
	for (; count >= 0; count--) {
		unlock_page(pages[count]);
		page_cache_release(pages[count]);
	}
	return ret;
exit_validate_page:
	EMMCFS_ERR("Error in exit_validate_page");
	release_pages(pages, (int)pages_count, 0);
	return ret;
}

struct page *vdfs_read_or_create_page(struct inode *inode, pgoff_t index,
		enum vdfs_read_type type)
{
	struct page *pages[1];
	int err = 0;

	err = vdfs_read_or_create_pages(inode, index, 1, pages, type,
			0, 0);
	if (err)
		return ERR_PTR(err);

	return pages[0];
}

/**
 * @brief			This function write data to the file
 * @param [in]		iocb	The kiocb struct to advance by
 *				performing an operation
 * @param [in]		iov	data buffer
 * @param [in]		nr_segs	count of blocks to map
 * @param [in]		pos
 * @return			0 if success, negative value if error
 */
ssize_t vdfs_gen_file_buff_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos)
{
	ssize_t ret = 0;
	struct blk_plug plug;
	struct inode *inode = INODE(iocb);
	mutex_lock(&inode->i_mutex);
	blk_start_plug(&plug);
	ret = generic_file_buffered_write(iocb, iov, nr_segs, pos,
			&iocb->ki_pos, iov->iov_len, 0);

	mutex_unlock(&inode->i_mutex);
	if (ret > 0 || ret == -EIOCBQUEUED) {
		ssize_t err;

		err = generic_write_sync(iocb->ki_filp, 0, ret);
		if (err < 0 && ret > 0)
			ret = err;
	}
	blk_finish_plug(&plug);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(iocb);
	return ret;
}



struct bio *vdfs_mpage_bio_submit(int rw, struct bio *bio)
{
	bio->bi_end_io = end_io_write;
	submit_bio(rw, bio);
	return NULL;
}


int vdfs_mpage_writepage(struct page *page,
		struct writeback_control *wbc, void *data)
{
	struct vdfs_mpage_data *mpd = data;
	struct bio *bio = mpd->bio;
	struct address_space *mapping = page->mapping;
	struct inode *inode = page->mapping->host;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct block_device *bdev = sbi->sb->s_bdev;
	sector_t offset_alloc_hint = 0;
	unsigned blocksize;
	sector_t block_in_file;
	struct vdfs_extent_info extent;
	const unsigned blkbits = inode->i_blkbits;
	int err = 0;
	sector_t boundary_block = 0;
	unsigned long end_index;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct buffer_head *bh;
	loff_t i_size = i_size_read(inode);
	memset(&extent, 0x0, sizeof(extent));
	block_in_file = (sector_t)page->index << (PAGE_CACHE_SHIFT - blkbits);
	blocksize = (unsigned int)(1 << inode->i_blkbits);
	if (page_has_buffers(page)) {
		bh = page_buffers(page);
		BUG_ON(buffer_locked(bh));
		mutex_lock(&inode_info->truncate_mutex);
		if (buffer_mapped(bh)) {
			if (buffer_delay(bh)) {
				/* get extent which contains a iblock*/
				err = get_iblock_extent(&inode_info->vfs_inode,
					block_in_file, &extent,
					&offset_alloc_hint);
				/* buffer was allocated during writeback
				 * operation */
				if ((extent.first_block == 0) || err) {
					err = vdfs_get_block_da(inode,
							block_in_file, bh, 1);
					if (err) {
						mutex_unlock(&inode_info->
								truncate_mutex);
						goto out;
					}
				} else {
					bh->b_blocknr = extent.first_block +
						(block_in_file - extent.iblock);
					clear_buffer_delay(bh);
				}
				unmap_underlying_metadata(bh->b_bdev,
					bh->b_blocknr);
			}
		} else {
			/*
			* unmapped dirty buffers are created by
			* __set_page_dirty_buffers -> mmapped data
			*/
			if (buffer_dirty(bh)) {
				mutex_unlock(&inode_info->truncate_mutex);
				goto confused;
			}
		}
		mutex_unlock(&inode_info->truncate_mutex);

		if (!buffer_dirty(bh) || !buffer_uptodate(bh))
			goto confused;

	} else {
		/*
		* The page has no buffers: map it to disk
		*/
		BUG_ON(!PageUptodate(page));

		create_empty_buffers(page, blocksize, 0);
		bh = page_buffers(page);

		bh->b_state = 0;
		bh->b_size = (size_t)(1 << blkbits);
		if (vdfs_get_block(inode, block_in_file, bh, 1))
			goto confused;
		if (buffer_new(bh))
			unmap_underlying_metadata(bh->b_bdev,
					bh->b_blocknr);

	}

	boundary_block = bh->b_blocknr;
	end_index = (long unsigned int)(i_size >> PAGE_CACHE_SHIFT);
	if (page->index >= end_index) {
		/*
		 * The page straddles i_size.  It must be zeroed out on each
		 * and every writepage invocation because it may be mmapped.
		 * "A file is mapped in multiples of the page size.  For a file
		 * that is not a multiple of the page size, the remaining memory
		 * is zeroed when mapped, and writes to that region are not
		 * written out to the file."
		 */
		unsigned offset = i_size & (PAGE_CACHE_SIZE - 1);

		if (page->index > end_index || !offset)
			goto confused;
		zero_user_segment(page, offset, PAGE_CACHE_SIZE);
	}

	/*
	 * If it's the end of contiguous chunk, submit the BIO.
	 */
	if (bio && mpd->last_block_in_bio != boundary_block - 1)
		bio = vdfs_mpage_bio_submit(WRITE, bio);


alloc_new:
	bdev = bh->b_bdev;
	boundary_block = bh->b_blocknr;
	if (boundary_block == 0)
		BUG();
	if (IS_ERR_OR_NULL(bio)) {
		bio = allocate_new_bio(bdev, boundary_block << (blkbits - 9),
				(unsigned)bio_get_nr_vecs(bdev));
		if (IS_ERR_OR_NULL(bio))
			goto confused;
	}

	/*
	 * TODO: replace PAGE_SIZE with real user data size?
	 */
	if (bio_add_page(bio, page, PAGE_SIZE, 0) < (int)PAGE_SIZE) {
		bio = vdfs_mpage_bio_submit(WRITE, bio);
		goto alloc_new;
	}

	/*
	 * OK, we have our BIO, so we can now mark the buffers clean.  Make
	 * sure to only clean buffers which we know we'll be writing.
	 */
	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);

		clear_buffer_dirty(head);
	}

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);
	mpd->last_block_in_bio = boundary_block;
	goto out;

confused:
	if (IS_ERR_OR_NULL(bio))
		bio = NULL;
	else
		bio = vdfs_mpage_bio_submit(WRITE, bio);
	if (buffer_mapped(bh))
		if (bh->b_blocknr == 0)
			BUG();
	err = mapping->a_ops->writepage(page, wbc);
	/*
	 * The caller has a ref on the inode, so *mapping is stable
	 */
out:
	mapping_set_error(mapping, err);
	mpd->bio = bio;
	return err;
}


int vdfs_dump_chunk_to_disk(void *mapped_chunk, size_t chunk_length,
		const char *name, unsigned int length)
{
	int ret = 0;
	struct file *fd;
#ifdef CONFIG_PLAT_TIZEN
	/* Tizen, dump to /opt */
	const char path[] = "/opt/vdfs3_debug_err_chunk.bin";
#elif defined(CONFIG_ARCH_SDP)
	/* Orsey */
	const char path[] = "/mtd_rwarea/vdfs3_debug_err_chunk.bin";
#else
	/* qemu */
	const char path[] = "/tmp/vdfs3_debug_err_chunk.bin";
#endif

	EMMCFS_ERR("dump the chunk to file %s", path);

	fd = filp_open((const char *)path, O_CREAT | O_WRONLY | O_TRUNC,
			S_IRWXU);
	if (!IS_ERR(fd)) {
		loff_t pos;
		ssize_t written;
		mm_segment_t fs;

		pos = fd->f_path.dentry->d_inode->i_size;
		fs = get_fs();
		set_fs(KERNEL_DS);

		written = vfs_write(fd, name, length, &pos);
		if (written < 0) {
			EMMCFS_ERR("cannot write to file %s err:%d",
					path, written);
			ret = (int)written;
			goto exit;
		}

		written = vfs_write(fd, mapped_chunk, chunk_length, &pos);
		if (written < 0) {
			EMMCFS_ERR("cannot write to file %s err:%d",
					path, written);
			ret = (int)written;
		}
exit:
		set_fs(fs);
		filp_close(fd, NULL);
	}

	return ret;
}


