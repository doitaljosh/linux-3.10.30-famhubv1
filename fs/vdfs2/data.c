/**
 * @file	fs/emmcfs/data.c
 * @brief	Basic data operations.
 * @author	Ivan Arishchenko, i.arishchenk@samsung.com
 * @date	05/05/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements bio data operations and its related functions.
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
		int nr_vecs)
{
	/*int nr_vecs;*/
	gfp_t gfp_flags = GFP_NOFS | __GFP_HIGH;
	struct bio *bio = NULL;
	sector_t s_count = bdev->bd_inode->i_size >> SECTOR_SIZE_SHIFT;

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


static int get_sector(struct vdfs_extent *extents, unsigned int extents_count,
		sector_t isector, unsigned int block_shift, sector_t *result,
		sector_t *length)
{
	int count;
	sector_t first_isector = 0;
	sector_t last_isector = 0;
	unsigned int shift = block_shift - SECTOR_SIZE_SHIFT;

	for (count = 0; count < extents_count; count++) {
		last_isector += le32_to_cpu(extents->length) << shift;
		if (isector >= first_isector && isector < last_isector) {
			sector_t offset = isector - first_isector;
			*result = (le64_to_cpu(extents->begin) << shift) +
					offset;
			*length = (le32_to_cpu(extents->length) << shift) -
					offset;
			return 0;
		}
		first_isector = last_isector;
		extents++;
	}
	return -EINVAL;
}

static int get_table_sector(struct vdfs_sb_info *sbi, sector_t isector,
		sector_t *result, sector_t *length)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	struct vdfs_extent *extent_table = &exsb->tables[0];
	int ret = 0;
	sector_t max_size = sbi->sb->s_bdev->bd_inode->i_size >>
			SECTOR_SIZE_SHIFT;

	ret = get_sector(extent_table, VDFS_TABLES_EXTENTS_COUNT,
		isector, sbi->block_size_shift, result, length);

	if ((*length == 0) || (*result > max_size)) {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			EMMCFS_ERR("Error get block for metadata");
			ret = -EFAULT;
			goto error_exit;
		} else {
			BUG();
		}
	}
	/* TODO extended block */
	/* BUG_ON(ret); */
error_exit:
	return ret;
}



static int get_meta_block(struct vdfs_sb_info *sbi, sector_t iblock,
		sector_t *result, sector_t *length)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	struct vdfs_extent *extents = &exsb->meta[0];
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

static int get_block_meta_wrapper(struct inode *inode, pgoff_t page_index,
		sector_t *res_block, int type)
{
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	sector_t meta_iblock, length, start_iblock;
	int ret;

	if (!type) {
		ret = vdfs_get_meta_iblock(sbi, inode->i_ino, page_index,
			&start_iblock);
		if (ret)
			return ret;

		meta_iblock = start_iblock;
		if (is_tree(inode->i_ino)) {
			int mask;
			mask = (1 << (sbi->log_blocks_in_leb +
				sbi->block_size_shift - PAGE_CACHE_SHIFT)) - 1;
			meta_iblock += (page_index & mask) << (PAGE_CACHE_SHIFT
				- sbi->block_size_shift);
		}
		*res_block = 0;
		ret = get_meta_block(sbi, meta_iblock, res_block, &length);
		BUG_ON(*res_block == 0);
	} else {
		sector_t iblock;
		struct buffer_head bh_result;
		bh_result.b_blocknr = 0;
		iblock = ((sector_t)page_index) << (PAGE_CACHE_SHIFT -
				sbi->block_size_shift);
		ret = vdfs_get_block(inode, iblock, &bh_result, 0);
		*res_block = bh_result.b_blocknr;
	}
	return ret;
}

static void end_IO(struct bio *bio, int err)
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
			EMMCFS_BUG();
		}

	} while (bvec >= bio->bi_io_vec);

	if (bio->bi_private) {
		struct vdfs_wait_list *wait = bio->bi_private;
		complete(&wait->wait);
	}

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
int vdfs_table_IO(struct vdfs_sb_info *sbi, struct page **pages,
		s64 sectors_count, int rw, sector_t *isector)
{
	struct block_device *bdev = sbi->sb->s_bdev;
	struct bio *bio;
	sector_t start_sector = 0, length = 0, c_isector = *isector, sec_to_bio;
	sector_t total_sectors = sectors_count;
	int nr_vectr, ret;
	unsigned int count = 0, page_count = DIV_ROUND_UP(SECTOR_SIZE *
			sectors_count, PAGE_CACHE_SIZE);
	struct blk_plug plug;
	struct list_head wait_list_head;
	struct vdfs_wait_list *new_request;
	struct list_head *pos, *q; /* temporary storage */
	sector_t l_sector = 0;

	for (count = 0; count < page_count; count++) {
		lock_page(pages[count]);
		if (rw & WRITE)
			set_page_writeback(pages[count]);
	}
	INIT_LIST_HEAD(&wait_list_head);
	blk_start_plug(&plug);

	do {
		unsigned int size;

		nr_vectr = (page_count < BIO_MAX_PAGES) ? page_count :
				BIO_MAX_PAGES;

		ret = get_table_sector(sbi, c_isector + l_sector,
				&start_sector, &length);
		if (ret)
			goto error_exit;


		bio = allocate_new_bio(bdev, start_sector, nr_vectr);
		if (IS_ERR_OR_NULL(bio)) {
			ret = -ENOMEM;
			goto error_exit;
		}

		new_request = (struct vdfs_wait_list *)
				kzalloc(sizeof(*new_request), GFP_KERNEL);

		if (!new_request) {
			ret = -ENOMEM;
			bio_put(bio);
			goto error_exit;
		}
		INIT_LIST_HEAD(&new_request->list);
		init_completion(&new_request->wait);
		new_request->number = start_sector;
		list_add_tail(&new_request->list, &wait_list_head);
		bio->bi_end_io = end_IO;
		bio->bi_private = new_request;
		sec_to_bio = nr_vectr << (PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT);

		sec_to_bio = min(sec_to_bio, length);
		sec_to_bio = min(sec_to_bio, (sector_t)sectors_count);

		do {
			unsigned int add_size, add_offset, index;
			index = l_sector >> (PAGE_CACHE_SHIFT -
					SECTOR_SIZE_SHIFT);
			add_offset = (l_sector & (SECTOR_PER_PAGE - 1)) *
					SECTOR_SIZE;
			add_size = min((unsigned int)PAGE_CACHE_SIZE -
				add_offset, (unsigned int)sec_to_bio *
				SECTOR_SIZE);
			size = bio_add_page(bio, pages[index], add_size,
					add_offset);
			l_sector += (size >> SECTOR_SIZE_SHIFT);
			sectors_count -= (size >> SECTOR_SIZE_SHIFT);
			sec_to_bio -= (size >> SECTOR_SIZE_SHIFT);
			if (!size && (!bio->bi_vcnt)) {
				/* fail to add data into BIO */
				ret = -EFAULT;
				bio_put(bio);
				goto error_exit;
			} else if (!size) {
				/* no space left in bio */
				break;
			}
		} while (sec_to_bio);
		submit_bio(rw, bio);

	} while (sectors_count > 0);
	BUG_ON(sectors_count < 0);

error_exit:
	blk_finish_plug(&plug);

	list_for_each_safe(pos, q, &wait_list_head) {
		new_request = list_entry(pos, struct vdfs_wait_list, list);
		/* Synchronous write operation */
		wait_for_completion(&new_request->wait);
		list_del(pos);
		kfree(new_request);
	}

	for (count = 0; count < page_count; count++) {
		unlock_page(pages[count]);
		if (rw & WRITE)
			end_page_writeback(pages[count]);
	}

	if (!ret)
		*isector += total_sectors;

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
			unsigned int page_count)
{
	struct bio *bio;
	struct completion wait;
	unsigned int count = 0;
	int continue_load = 0;
	int nr_vectr = (page_count < BIO_MAX_PAGES) ?
				page_count : BIO_MAX_PAGES;

	struct blk_plug plug;


	init_completion(&wait);
again:
	blk_start_plug(&plug);

	/* Allocate a new bio */
	bio = allocate_new_bio(bdev, sector_addr, nr_vectr);
	if (IS_ERR_OR_NULL(bio)) {
		blk_finish_plug(&plug);
		EMMCFS_ERR("failed to allocate bio\n");
		return PTR_ERR(bio);
	}

	bio->bi_end_io = read_end_io;

	/* Initialize the bio */
	for (; count < page_count; count++) {
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

	if (sector_count > SECTOR_PER_PAGE)
		return -EINVAL;

	if (VDFS_IS_READONLY(sbi->sb)) {
		end_page_writeback(page);
		return 0;
	}

	/* Allocate a new bio */
	bio = allocate_new_bio(bdev, sector_addr, 1);
	if (IS_ERR_OR_NULL(bio))
		return PTR_ERR(bio);

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
		if (PageError(page))
			return -EFAULT;
	}
	return 0;
}
/**
 * @brief			Calculate aligned value.
 * @param [in]	value		Value to align.
 * @param [in]	gran_log2	Alignment granularity log2.
 * @return			val, aligned up to granularity boundary.
 */
static inline sector_t align_up(sector_t val, int gran_log2)
{
	return (val + (1<<gran_log2) - 1) & ~((1<<gran_log2) - 1);
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
int vdfs_check_and_sign_pages(struct page *page, char *magic, int magic_len,
		__u64 version)
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
		printk(KERN_ERR "invalid bitmap magic for %s,"
			" %lu, actual = %s\n", magic,
			page->mapping->host->i_ino, (char *)data);
		BUG();
	}
#endif
	memcpy(data + magic_len - VERSION_SIZE, &version, VERSION_SIZE);
	vdfs_update_block_crc(data, PAGE_SIZE, magic_len);
	kunmap(page);
	return 0;
}
/**
 * @brief		Validata page crc and magic number
 * @param [in]	page	page to validate
 * @param [in]	magic	magic to validate
 * @param [in]	magic_len	magic len in bytes
 * @return			0 - if crc and magic are valid
 *				1 - if crc or magic is invalid
 */
int vdfs_validate_page(struct page *page)
{
	void *data;
	int ret_val = 0;
	char *magic;
	int magic_len;
	int ino = page->mapping->host->i_ino;
	switch (ino) {
	case VDFS_SMALL_AREA_BITMAP:
		magic = SMALL_AREA_BITMAP_MAGIC;
		magic_len = SMALL_AREA_BITMAP_MAGIC_LEN;
		break;
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
	ret_val = vdfs_validate_crc(data, PAGE_SIZE, magic, magic_len);
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
void vdfs_update_block_crc(char *buff, int block_size, int magic_len)
{
#ifdef CONFIG_VDFS_CRC_CHECK
	int crc = 0;
	/* set crc to the end of the buffer */
	crc = cpu_to_le32(crc32(0, buff + magic_len,
		block_size - (CRC32_SIZE  +
		magic_len)));
	memcpy(VDFS_CRC32_OFFSET(buff, block_size), &crc, CRC32_SIZE);
#endif
}
/**
 * @brief				Validate the buffer magic and crc
 *					the magic should be in first bytes,
 *					the crc should be in last 4 bytes
 * @param [in]	buff			Buffer to validate.
 * @param [in]	buff_size		Size of the buffer
 * @param [in]	magic			Magic word to validate. If it's null
 *					the magic validation will be skipped.
 * @param [in]	magic_len		Length of the magic word in bytes
 * @return				0 - if crc and magic are valid
 *					1 - if crc or magic is invalid
  */
int vdfs_validate_crc(char *buff, int buff_size, const char *magic,
		int magic_len)
{
#ifdef CONFIG_VDFS_CRC_CHECK
	int crc = VDFS_VALID;
#endif
	int ret_val = 0;
	if (magic) {
		/* if magic is not valid */
		if (memcmp(buff, magic, magic_len - VERSION_SIZE) != 0) {
			EMMCFS_ERR("Magic is NOT valid. magic: %s", magic);
			ret_val = VDFS_INVALID;
		}
	}
#ifdef CONFIG_VDFS_CRC_CHECK
	/* if magic is valid */
	if (ret_val == 0) {
		crc = cpu_to_le32(crc32(0, buff + magic_len, buff_size -
			(CRC32_SIZE + magic_len)));
		if (memcmp(VDFS_CRC32_OFFSET(buff, buff_size),
				&crc, CRC32_SIZE) != 0) {
			EMMCFS_ERR("CRC is NOT valid. Buffer: %s", magic);
			ret_val = VDFS_INVALID;
		}
	}
#endif
	return ret_val;
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
int vdfs_set_bits(char *buff, int buff_size, int offset, int count,
		int magic_len, int block_size) {
	/* data block size in bits */
	const int datablock_size = (block_size - (magic_len
				+ CRC32_SIZE))<<3;
	/* pointer to begin of start block */
	char *start_blck = buff + ((offset / datablock_size) * block_size);
	/* pointer to last block */
	char *end_blck =  buff + (((offset + count - 1) / datablock_size) *
			block_size);
	char *cur_blck = NULL;
	int cur_position = 0;
	u_int32_t length = 0;
	int i = 0;
	char *end_buff;


	for (cur_blck = start_blck; cur_blck <= end_blck;
				cur_blck += block_size) {
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
		end_buff = cur_blck + block_size - CRC32_SIZE;
		/* set bits */
		for (i = 0; i < length; i++) {
			/* check the bound of array */
			if ((cur_blck + (cur_position>>3) +
					magic_len) > end_buff)
				return -EFAULT;
			/* set bits */
			if (test_and_set_bit(cur_position, (void *)(cur_blck +
					magic_len)))
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
int vdfs_clear_bits(char *buff, int buff_size, int offset, int count,
		int magic_len, int block_size) {
	/* data block size in bits */
	const int datablock_size = (block_size - (magic_len
				+ CRC32_SIZE))<<3;
	/* pointer to begin of start block */
	char *start_blck = buff + ((offset / datablock_size) * block_size);
	/* pointer to last block */
	char *end_blck =  buff + (((offset + count - 1) / datablock_size) *
			block_size);
	char *cur_blck = NULL;
	int cur_position = 0;
	u_int32_t length = 0;
	int i = 0;
	char *end_buff;

	/* go through all blcoks */
	for (cur_blck = start_blck; cur_blck <= end_blck;
				cur_blck += block_size) {
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
		end_buff = cur_blck + block_size - CRC32_SIZE;
		/* set bits */
		for (i = 0; i < length; i++) {
			/* check the boundary of array */
			if ((cur_blck + (cur_position>>3) + magic_len)
					> end_buff)
				return -EFAULT;

			/* set bits */
			if (!test_and_clear_bit(cur_position,
					(void *)(cur_blck + magic_len)))
				return -EFAULT;
			cur_position++;
		}
	}
	return 0;
}
static int  __find_next_bit_in_block(const void *addr, unsigned int size,
		unsigned long offset, unsigned int magic_len, char bit)
{
	int result = 0;
	/* start of the block */
	const unsigned long *int_addr = (const unsigned long *)(((char *)addr)
			+ magic_len);
	/* size of the block in bits */
	/*unsigned long int_size = ((block_size - (CRC32_SIZE +
			magic_len)) << 3);*/
	if (bit == '0')
		result = find_next_zero_bit(int_addr, size, offset);
	else
		result = find_next_bit(int_addr, size, offset);

	if (result >= size)
		return -1;
	else
		return result;
}
/**
 * @brief			Find next zero bit in the array.
 *					this function take care about
 *					all special symbols in the array
 *					like magic and crc number.
 * @param [in]	addr			Source buffer with data.
 * @param [in]	size			Size of the buffer in bits
 * @param [in]	offset			start bit position for the searching
 * @param [in]	block_size		Size of block for block device.
 * @param [in]	magic_len		Length of the magic
 * @return						Previous state of the bit
 */
static unsigned long  __find_next_bit(const void *addr,
		unsigned long size, unsigned long offset,
		unsigned int block_size, unsigned int magic_len,
		char bit)
{
	/* find first and last block in the buffer */
	unsigned long start_block = (offset>>3) / ((block_size - (CRC32_SIZE +
			magic_len)));
	unsigned long result = size;
	int res = 0;
	unsigned long cur_block = 0;
	/* size of the block in bits */
	unsigned long int_size = ((block_size - (CRC32_SIZE +
		magic_len)) << 3);
	unsigned long int_offset = offset % int_size;
	unsigned int int_block_size = 0;
	unsigned long end_block = (size - 1) / int_size;
	for (cur_block =  start_block; cur_block <= end_block;
			cur_block++) {
			int_block_size = size - cur_block * int_size;
			int_block_size = int_block_size > int_size ?
					int_size : int_block_size;

			res  = __find_next_bit_in_block(addr +
					(cur_block * block_size),
					int_block_size, int_offset,
					magic_len, bit);
			if (res < 0) {
				int_offset = 0;
				continue;
			} else {
				result = (unsigned long)res + cur_block *
						int_size;
				break;
			}
	}
	return result;
}
/**
 * @brief			Find next zero bit in the array.
 *					this function take care about
 *					all special symbols in the array
 *					like magic and crc number.
 * @param [in]	addr			Source buffer with data.
 * @param [in]	size			Size of the buffer in bits
 * @param [in]	offset			start bit position for the searching
 * @param [in]	block_size		Size of block for block device.
 * @param [in]	magic_len		Length of the magic
 * @return				Error code or bit position
 */

inline unsigned long  vdfs_find_next_zero_bit(const void *addr,
		unsigned long size, unsigned long offset,
		unsigned int block_size, unsigned int magic_len)
{
	return __find_next_bit(addr, size, offset, block_size,
			magic_len, '0');
}
/**
 * @brief			Find next nonzero bit in the array.
 *					this function take care about all
 *					special symbols in the array like
 *					magic and crc number.
 * @param [in]	addr		Source buffer with data.
 * @param [in]	size		Size of the buffer
 * @param [in]	offset		start bit position for the searching
 * @param [in]	block_size	Size of block for block device.
 * @return					Previous state of the bit
 */
inline unsigned long vdfs_find_next_bit(const void *addr, unsigned long size,
			unsigned long offset, unsigned int block_size,
			unsigned int magic_len)
{
	return __find_next_bit(addr, size, offset, block_size,
			magic_len, '1');
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
	__u64 version = ((__le64)VDFS_RAW_EXSB(sbi)->mount_counter << 32) |
			sbi->snapshot_info->sync_count;
	if (ino_n == VDFS_FREE_INODE_BITMAP_INO) {
		bitmap = kmap(page);
		if (!bitmap)
			return -ENOMEM;
		memset(bitmap, 0, PAGE_CACHE_SIZE);
		memcpy(bitmap, INODE_BITMAP_MAGIC, INODE_BITMAP_MAGIC_LEN -
				VERSION_SIZE);
		memcpy(bitmap + INODE_BITMAP_MAGIC_LEN - VERSION_SIZE,
				&version, VERSION_SIZE);
		kunmap(page);
	} else if (ino_n == VDFS_SMALL_AREA_BITMAP) {
		bitmap = kmap(page);
		if (!bitmap)
			return -ENOMEM;
		memset(bitmap, 0, PAGE_CACHE_SIZE);
		memcpy(bitmap, SMALL_AREA_BITMAP_MAGIC,
				SMALL_AREA_BITMAP_MAGIC_LEN - VERSION_SIZE);
		memcpy(bitmap + SMALL_AREA_BITMAP_MAGIC_LEN - VERSION_SIZE,
				&version, VERSION_SIZE);
		kunmap(page);
	}

	return 0;
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
		return sbi->hardlink_tree->inode->i_mapping;
	break;
	case (VDFS_HARDLINKS_TREE_INO):
		return sbi->xattr_tree->inode->i_mapping;
	case (VDFS_XATTR_TREE_INO):
		return sbi->small_area->bitmap_inode->i_mapping;
	case (VDFS_SMALL_AREA_BITMAP):
		return NULL;
	default:
	return NULL;
	}
}



static int get_pages_from_mapping(struct vdfs_sb_info *sbi,
		struct address_space **current_mapping,
		struct pagevec *pvec, pgoff_t *index)
{
	int nr_pages = 0;
	ino_t ino;
	int count;
	unsigned long size;

	do {
		if (*current_mapping) {
			size = (is_tree(current_mapping[0]->host->i_ino)) ?
					PAGEVEC_SIZE - (PAGEVEC_SIZE %
					(1 << (sbi->log_blocks_in_page
					+ sbi->log_blocks_in_leb))) :
					PAGEVEC_SIZE;
			nr_pages = pagevec_lookup_tag(pvec, *current_mapping,
				index, PAGECACHE_TAG_DIRTY, size);

			ino = current_mapping[0]->host->i_ino;

			for (count = 0; count < nr_pages; count++) {
				lock_page(pvec->pages[count]);
				BUG_ON(!PageDirty(pvec->pages[count]));
				if (PageWriteback(pvec->pages[count]))
					wait_on_page_writeback(
							pvec->pages[count]);
				BUG_ON(PageWriteback(pvec->pages[count]));
				clear_page_dirty_for_io(pvec->pages[count]);
				unlock_page(pvec->pages[count]);

			}
		}

		if (!nr_pages) {
			*current_mapping = vdfs_next_mapping(sbi,
					*current_mapping);
			*index = 0;
		}

	} while ((!nr_pages) && *current_mapping);

#ifdef CONFIG_VDFS_DEBUG
	if (nr_pages)
		vdfs_check_moved_iblocks(sbi, pvec->pages, nr_pages);
#endif

	return nr_pages;
}




static void meta_end_IO(struct bio *bio, int err)
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
			EMMCFS_BUG();
		}

		SetPageUptodate(page);

		if (bio->bi_rw & WRITE) {
			end_page_writeback(page);
		} else {
			BUG_ON(!PageLocked(page));
			/* if it's async read*/
			if (!bio->bi_private)
				unlock_page(page);
		}

	} while (bvec >= bio->bi_io_vec);

	if (bio->bi_private) {
		struct vdfs_wait_list *wait = bio->bi_private;
		complete(&wait->wait);
	}

	bio_put(bio);
}

static struct bio *allocate_new_request(struct vdfs_sb_info *sbi, sector_t
		start_block, struct list_head *wait_list_head, int size)
{
	struct bio *bio;
	sector_t start_sector = start_block << (sbi->block_size_shift -
			SECTOR_SIZE_SHIFT);
	struct vdfs_wait_list *new_request;
	struct block_device *bdev = sbi->sb->s_bdev;
	int bio_size = (size > BIO_MAX_PAGES) ? BIO_MAX_PAGES : size;


	bio = allocate_new_bio(bdev, start_sector, bio_size);
	if (!bio) {
		bio = ERR_PTR(-ENOMEM);
		goto exit;
	}

	if (wait_list_head) {
		new_request = kzalloc(sizeof(*new_request), GFP_KERNEL);

		if (!new_request) {
			bio_put(bio);
			bio = ERR_PTR(-ENOMEM);
			goto exit;
		}

		INIT_LIST_HEAD(&new_request->list);
		init_completion(&new_request->wait);
		new_request->number = start_sector;
		list_add_tail(&new_request->list, wait_list_head);
		bio->bi_private = new_request;
	} else
		bio->bi_private = NULL;

	bio->bi_end_io = meta_end_IO;

exit:
	return bio;

}

static int vdfs_sign_mapping_pages(struct vdfs_sb_info *sbi,
		struct pagevec *pvec, unsigned long ino)
{
	int ret = 0;
	__u64 version = ((__le64)VDFS_RAW_EXSB(sbi)->mount_counter << 32) |
			sbi->snapshot_info->sync_count;
	int magic_len = 0;

	if (is_tree(ino)) {
		struct vdfs_btree *tree;
		int i;
		switch (ino) {
		case VDFS_CAT_TREE_INO:
			tree = sbi->catalog_tree;
			break;
		case VDFS_EXTENTS_TREE_INO:
			tree = sbi->extents_tree;
			break;
		case VDFS_HARDLINKS_TREE_INO:
			tree = sbi->hardlink_tree;
			break;
		case VDFS_XATTR_TREE_INO:
			tree = sbi->xattr_tree;
			break;
		default:
			return -EFAULT;
		}
		for (i = 0; i < pvec->nr; i += tree->pages_per_node) {
			if ((pvec->nr - i) < tree->pages_per_node)
				break;
			ret = vdfs_check_and_sign_dirty_bnodes(&pvec->pages[i],
					tree, version);
		}
	} else {
		int i;
		char *magic = NULL;
		switch (ino) {
		case VDFS_SMALL_AREA_BITMAP:
			magic = SMALL_AREA_BITMAP_MAGIC;
			magic_len = SMALL_AREA_BITMAP_MAGIC_LEN;
			break;
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
static int vdfs_meta_write(struct vdfs_sb_info *sbi,
		unsigned int *written_pages_count)
{
	struct bio *bio = NULL;
	struct address_space *current_mapping = NULL;
	struct blk_plug plug;
	struct list_head wait_list_head;
	struct vdfs_wait_list *new_request;
	struct list_head *pos, *q; /* temporary storage */
	pgoff_t index, page_index = 0;
	unsigned int nr_pages;
	unsigned int size;
	int ret;
	struct pagevec pvec;
	sector_t last_block = 0, block;
	unsigned int blocks_per_page = 1 << (PAGE_CACHE_SHIFT -
			sbi->block_size_shift);

	INIT_LIST_HEAD(&wait_list_head);
	blk_start_plug(&plug);
	pagevec_init(&pvec, 0);


	index = 0;
	nr_pages = get_pages_from_mapping(sbi, &current_mapping, &pvec,
			&page_index);
	if (nr_pages) {
		ret = vdfs_sign_mapping_pages(sbi, &pvec,
				current_mapping->host->i_ino);
		if (ret) {
			blk_finish_plug(&plug);
			return ret;
		}
	}
	while (nr_pages) {
		ret = get_block_meta_wrapper(current_mapping->host,
				pvec.pages[index]->index, &block, 0);
		BUG_ON(ret);

		if (last_block + blocks_per_page != block) {
			if (bio)
				submit_bio(WRITE_FUA, bio);
again:
			bio = allocate_new_request(sbi, block, &wait_list_head,
					nr_pages);
			if (!bio)
				goto again;
		} else if (!bio) {
			blk_finish_plug(&plug);
			return -EINVAL;
		}

		set_page_writeback(pvec.pages[index]);
		size = bio_add_page(bio, pvec.pages[index], PAGE_CACHE_SIZE, 0);
		if (size < PAGE_CACHE_SIZE) {
			submit_bio(WRITE_FUA, bio);
			bio = NULL;
			last_block = 0;
		} else {
			nr_pages--;
			index++;
			last_block = block;
		}
#ifdef CONFIG_VDFS_STATISTIC
	sbi->umount_written_bytes += size;
#endif
		if (!nr_pages) {
			pagevec_reinit(&pvec);
			/*pagevec_release(&pvec);*/
			nr_pages = get_pages_from_mapping(sbi, &current_mapping,
					&pvec, &page_index);
			index = 0;
			if (nr_pages) {
				ret = vdfs_sign_mapping_pages(sbi,
						&pvec,
						current_mapping->host->i_ino);
				if (ret) {
					if (bio)
						bio_put(bio);
					blk_finish_plug(&plug);
					return ret;
				}
			}

		}
	};

	if (bio)
		submit_bio(WRITE_FUA, bio);

	blk_finish_plug(&plug);

	list_for_each_safe(pos, q, &wait_list_head) {
		new_request = list_entry(pos, struct vdfs_wait_list, list);
		/* Synchronous write operation */
		wait_for_completion(&new_request->wait);
		list_del(pos);
		kfree(new_request);
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
static int vdfs_meta_read(struct inode *inode, int type, struct page **pages,
		int page_count, int async)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct bio *bio = NULL;
	struct blk_plug plug;
	struct list_head wait_list_head;
	struct vdfs_wait_list *new_request;
	struct list_head *pos, *q; /* temporary storage */
	unsigned int size;
	int ret = 0;
	sector_t last_block = 0, block;

	unsigned int blocks_per_page = 1 << (PAGE_CACHE_SHIFT -
			sbi->block_size_shift);

	int count;

	INIT_LIST_HEAD(&wait_list_head);
	blk_start_plug(&plug);

	for (count = 0; count < page_count; count++) {
		struct page *page = pages[count];

		BUG_ON(!PageLocked(page));
		if (PageUptodate(page)) {
			/* if it's async call*/
			if (async)
				unlock_page(pages[count]);
			continue;
		}

		ret = get_block_meta_wrapper(inode, page->index, &block, type);
		if (ret || (block == 0)) {
			ret = (block == 0) ? -EINVAL : ret;
			goto exit;
		}

		if (last_block + blocks_per_page != block) {
			if (bio)
				submit_bio(READ, bio);
again:
			if (async)
				bio = allocate_new_request(sbi, block, NULL,
						page_count - count);
			else
				bio = allocate_new_request(sbi, block,
					&wait_list_head, page_count - count);
			if (!bio)
				goto again;
		} else if (!bio) {
			blk_finish_plug(&plug);
			return -EINVAL;
		}

		size = bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
		if (size < PAGE_CACHE_SIZE) {
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

	if (async)
		return ret;

	list_for_each_safe(pos, q, &wait_list_head) {
		new_request = list_entry(pos, struct vdfs_wait_list, list);
		/* Synchronous write operation */
		wait_for_completion(&new_request->wait);
		list_del(pos);
		kfree(new_request);
	}

	return ret;
}

int vdfs_sync_metadata(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	unsigned int pages_count = 0;

	if (sbi->snapshot_info->dirty_pages_count == 0)
		return 0;

	if (is_sbi_flag_set(sbi, EXSB_DIRTY)) {
		ret = vdfs_sync_second_super(sbi);
		sbi->snapshot_info->use_base_table = 1;
		if (ret)
			return ret;
	}

	ret = vdfs_meta_write(sbi, &pages_count);
	if (ret)
		return ret;

	vdfs_update_bitmaps(sbi);
	ret = update_translation_tables(sbi);
	if (ret)
		return ret;

	if (is_sbi_flag_set(sbi, EXSB_DIRTY)) {
		ret = vdfs_sync_first_super(sbi);
		if (!ret)
			clear_sbi_flag(sbi, EXSB_DIRTY);
	}

	return ret;
}


int vdfs_read_or_create_pages(struct vdfs_sb_info *sbi, struct inode *inode,
		struct page **pages, pgoff_t index, int page_count, int async)
{
	struct address_space *mapping = inode->i_mapping;
	int count, ret = 0, packtree_or_small_area = 0;
	struct page *page;
	char is_new = 0;
	int validate_crc = 0;

	if (inode->i_ino > VDFS_LSFILE) {
		/* small area or pack tree */
		packtree_or_small_area = 1;
		if (inode->i_ino == VDFS_SMALL_AREA) {
			struct buffer_head *bh_result = alloc_buffer_head(GFP_NOFS);
			struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
			__u32 inode_ttb = inode_info->fork.total_block_count;
			sector_t iblock;
			struct vdfs_extended_super_block *exsb =
					VDFS_RAW_EXSB(sbi);

			if (!bh_result)
				return -ENOMEM;

			iblock = ((sector_t)index) << (PAGE_CACHE_SHIFT -
					sbi->block_size_shift);
			iblock += (((sector_t)1) << (PAGE_CACHE_SHIFT -
					sbi->block_size_shift)) - 1;

			ret = vdfs_get_block(inode, iblock, bh_result, 1);
			mutex_lock_nested(&EMMCFS_I(inode)->truncate_mutex,
					SMALL_AREA);
			if (inode_ttb != inode_info->fork.total_block_count) {
				is_new = 1;
				vdfs_form_fork(&exsb->small_area, inode);
				i_size_write(inode,
					inode_info->fork.total_block_count
					<< sbi->block_size_shift);
				set_sbi_flag(sbi, EXSB_DIRTY);
			}
			mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
			free_buffer_head(bh_result);
		}
	} else
		ret = vdfs_check_page_offset(sbi, inode->i_ino, index, &is_new);

	if (ret)
		return ret;

	for (count = 0; count < page_count; count++) {
again:
		page = find_get_page(mapping, index + count);
		if (!page) {
			page = page_cache_alloc_cold(mapping);
			if (!page) {
				ret = -ENOMEM;
				goto exit_alloc_page;
			}
			ret = add_to_page_cache_lru(page, mapping,
					index + count, GFP_KERNEL);
			if (unlikely(ret)) {
				page_cache_release(page);
				goto again;
			}
			/* this flag indicates that pages are from disk.*/
			validate_crc = 1;
			if (is_new)
				SetPageUptodate(page);
		} else {
			lock_page(page);
			if (!async)
				BUG_ON(!PageUptodate(page));
		}
		pages[count] = page;
		if ((is_new) && (!packtree_or_small_area)) {
			ret = vdfs_init_bitmap_page(sbi, inode->i_ino, page);
			if (ret)
				goto exit_alloc_locked_page;
		}
	}

	ret = vdfs_meta_read(inode, packtree_or_small_area, pages,
			page_count, async);
	if (ret) {
		count = 0;
		goto exit_validate_page;
	}

	/*validate pages only for bitmaps*/
	for (count = 0; count < page_count; count++) {
		if (validate_crc && !is_tree(inode->i_ino) &&
				(!packtree_or_small_area)) {
			ret = vdfs_validate_page(pages[count]);
			if (ret) {
				EMMCFS_ERR("Error in validate page");
				goto exit_validate_page;
			}
		}
		/* if it's sync call*/
		if (!async)
			unlock_page(pages[count]);
	}

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
	for (; count < page_count; count++) {
		SetPageError(pages[count]);
		if (!async)
			unlock_page(pages[count]);
	}

	return ret;
}

struct page *vdfs_read_or_create_page(struct inode *inode, pgoff_t index)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct page *pages[1];
	int err = 0;

	err = vdfs_read_or_create_pages(sbi, inode, pages, index, 1, 0);
	if (err)
		return ERR_PTR(err);

	return pages[0];
}

struct page *vdfs_read_or_create_small_area_page(struct inode *inode,
		pgoff_t index)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct page *pages[4];
	int err = 0;
	int count = 1;
	int i;

	count = min_t(int, 4, EMMCFS_I(inode)->fork.total_block_count -
		index - 1);

	if (count <= 0)
		count = 1;

	err = vdfs_read_or_create_pages(sbi, inode, pages, index, count, 0);
	if (err)
		return ERR_PTR(err);

	for (i = 1; i < count; i++)
		page_cache_release(pages[i]);

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
	blocksize = 1 << inode->i_blkbits;
	if (page_has_buffers(page)) {
		bh = page_buffers(page);
		BUG_ON(buffer_locked(bh));
		if (buffer_mapped(bh)) {
			if (bh->b_blocknr == VDFS_INVALID_BLOCK) {
				clear_buffer_delay(bh);
				/* get extent which contains a iblock*/
				err = get_iblock_extent(&inode_info->vfs_inode,
					block_in_file, &extent,
					&offset_alloc_hint);
				/* buffer was allocated during writeback
				 * operation */
				if ((extent.first_block == 0) || err) {
					if (vdfs_get_block_da(inode,
						block_in_file, bh, 1))
						goto confused;
				} else {
					bh->b_blocknr = extent.first_block +
						(block_in_file - extent.iblock);
				}
				unmap_underlying_metadata(bh->b_bdev,
					bh->b_blocknr);
			}
		} else {
			/*
			* unmapped dirty buffers are created by
			* __set_page_dirty_buffers -> mmapped data
			*/
			if (buffer_dirty(bh))
				goto confused;
		}

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
		bh->b_size = 1 << blkbits;
		if (vdfs_get_block(inode, block_in_file, bh, 1))
			goto confused;
		if (buffer_new(bh))
			unmap_underlying_metadata(bh->b_bdev,
					bh->b_blocknr);

	}

	boundary_block = bh->b_blocknr;
	end_index = i_size >> PAGE_CACHE_SHIFT;
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
				bio_get_nr_vecs(bdev));
		if (IS_ERR_OR_NULL(bio))
			goto confused;
	}

	/*
	 * TODO: replace PAGE_SIZE with real user data size?
	 */
	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
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
	mapping_set_error(mapping, err);
out:
	mpd->bio = bio;
	return err;
}
