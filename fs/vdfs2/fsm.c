/**
 * @file	fs/emmcfs/fsm.c
 * @brief	The eMMCFS free space management (FSM) implementation.
 * @author	Igor Skalkin, i.skalkin@samsung.com
 * @author	Ivan Arishchenko, i.arishchenk@samsung.com
 * @date	30/03/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * In this file free space management (FSM) is implemented.
 *
 * @see		TODO: documents
 *
 * Copyright 2012 by Samsung Electronics, Inc.
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>

#include "emmcfs.h"




/**
 * @brief			Get free space block chunk from tree
 *					and update free space manager bitmap.
 * @param [in]	inode_info		The inode information structure.
 * @param [in]	block_offset		Desired physical number of first block
 *					of block chunk.
 * @param [in]	length_in_blocks	Blocks count.
 * @param [in]	da			delya allocation flag. if it is 1, don't
 *					change the free space value
 *
 * @return				Returns physical number of first block
 *					of block chunk (may differs from
 *					block_offset parameter if desired block
 *					already used by file system), 0 if
 *					function fails (no free space).
 */
__u64 emmcfs_fsm_get_free_block(struct vdfs_sb_info *sbi, __u64 block_offset,
		__u32 *length_in_blocks, int alignment, int lock, int da)
{
	struct emmcfs_fsm_info *fsm = sbi->fsm_info;
	__u64 start_page = 0, end_page = 0, index;

	if ((block_offset + *length_in_blocks) >=
		(sbi->total_leb_count << sbi->log_blocks_in_leb))
		block_offset = 0;

	mutex_lock(&fsm->lock);
	/* check free space only if it's not delay allocation case.
	 * for delay allocation case, this check is made in write_begin
	 * function. */
	if ((*length_in_blocks >
			percpu_counter_sum(&sbi->free_blocks_count)) &&
		(da == 0)) {
		block_offset = 0;
		goto exit;
	}

	if (alignment) {
		/* align metadata to superpage size */
		__u32 blocks_per_superpage = 1 << (sbi->log_super_page_size -
				sbi->log_block_size);
		unsigned int additional_offset;
		__u32 length = *length_in_blocks + blocks_per_superpage;

		EMMCFS_BUG_ON(*length_in_blocks & (blocks_per_superpage - 1));

		block_offset = fsm_btree_lookup(&fsm->tree, block_offset,
				&length, 0);

		if (!block_offset)
			goto exit;

		EMMCFS_BUG_ON(length !=
				(*length_in_blocks + blocks_per_superpage));

		/* sample for offset == 12 (13) & 0x3 = 1
		 * blocks_per_superpage can be only power of 2 (4,8,16) */
		additional_offset = (block_offset) & (blocks_per_superpage - 1);

		if (!additional_offset) {
			/* start is already alligned */
			fsm_btree_free(&fsm->tree,
				block_offset + *length_in_blocks,
				blocks_per_superpage);
		} else {
			/* cat from start and from end */
			__u32 cut_left =
				blocks_per_superpage - additional_offset;
			fsm_btree_free(&fsm->tree, block_offset, cut_left);
			block_offset += cut_left;

			fsm_btree_free(&fsm->tree,
				block_offset + *length_in_blocks,
				additional_offset);
		}

	} else {
		block_offset = fsm_btree_lookup(&fsm->tree, block_offset,
				length_in_blocks, 1);
	}

	if (block_offset) {
		start_page = block_offset;
		end_page = block_offset + *length_in_blocks;
		/* calculate start block */
		do_div(start_page, VDFS_BIT_BLKSIZE(sbi->block_size,
				FSM_BMP_MAGIC_LEN));
		/* calculate end block */
		do_div(end_page, VDFS_BIT_BLKSIZE(sbi->block_size,
				FSM_BMP_MAGIC_LEN));
		/* index of start page */
		start_page = (unsigned int)start_page >>
				(PAGE_SHIFT - sbi->block_size_shift);

		end_page = (unsigned int)end_page >>
				(PAGE_SHIFT - sbi->block_size_shift);

		if (!da) {
			percpu_counter_sub(&fsm->sbi->free_blocks_count,
			*length_in_blocks);
		}
		EMMCFS_BUG_ON(block_offset + *length_in_blocks >
					(fsm->length_in_bytes << 3));
		EMMCFS_BUG_ON(vdfs_set_bits(fsm->data, fsm->page_count *
				PAGE_SIZE, block_offset, *length_in_blocks,
				FSM_BMP_MAGIC_LEN,
				sbi->block_size) != 0);
		for (index = 0; index < *length_in_blocks; index++) {
			int erase_block_num = (block_offset + index) >>
				sbi->log_erase_block_size_in_blocks;
			sbi->erase_blocks_counters[erase_block_num]++;
			BUG_ON(sbi->erase_blocks_counters[erase_block_num] >
				sbi->erase_block_size_in_blocks);
		}
	}
exit:
	mutex_unlock(&fsm->lock);
	if (block_offset)
		for (index = start_page; index <= end_page; index++)
			vdfs_add_chunk_bitmap(sbi, fsm->pages[index], lock);

	return block_offset;
}

/**
 * @brief				Free block chunk (put free space chunk
 *					to tree and update free space manager
 *					bitmap.
 * @param [in]	fsm			FSM information structure.
 * @param [in]	block_offset		Physical number of first block of
 *					block chunk.
 * @param [in]	length_in_blocks	Blocks count.
 * @return	void
 */
static int fsm_free_block_chunk(struct emmcfs_fsm_info *fsm,
	__u64 block_offset, __u32 length_in_blocks, int da)
{
	int err = 0;
	int i;
	__u64 start_page = block_offset;
	__u64 end_page	= block_offset + length_in_blocks, page_index;
	__u64 total_size = (fsm->sbi->total_leb_count <<
			fsm->sbi->log_blocks_in_leb) * fsm->sbi->block_size;
	/* calculate start block */
	do_div(start_page, VDFS_BIT_BLKSIZE(fsm->sbi->block_size,
			FSM_BMP_MAGIC_LEN));
	/* calculate end block */
	do_div(end_page, VDFS_BIT_BLKSIZE(fsm->sbi->block_size,
			FSM_BMP_MAGIC_LEN));
	/* index of start page */
	start_page = (unsigned int)start_page >>\
			(PAGE_SHIFT - fsm->sbi->block_size_shift);
	/* index of end page */
	end_page = (unsigned int)end_page >>\
			(PAGE_SHIFT - fsm->sbi->block_size_shift);


	mutex_lock(&fsm->lock);
	fsm_btree_free(&fsm->tree, block_offset, length_in_blocks);

	/* check boundary */
	if (block_offset + length_in_blocks > (fsm->length_in_bytes << 3)) {
		if (!is_sbi_flag_set(fsm->sbi, IS_MOUNT_FINISHED)) {
			mutex_unlock(&fsm->lock);
			return err;
		} else
			EMMCFS_BUG();
	}
	/* clear bits */
	err = vdfs_clear_bits(fsm->data, fsm->page_count * PAGE_SIZE,
			block_offset, length_in_blocks, FSM_BMP_MAGIC_LEN,
			fsm->sbi->block_size);
	if (err) {
		if (!is_sbi_flag_set(fsm->sbi, IS_MOUNT_FINISHED)) {
			mutex_unlock(&fsm->lock);
			return err;
		} else
			EMMCFS_BUG();
	}
	if (!da)
		percpu_counter_add(&fsm->sbi->free_blocks_count,
			(s64)length_in_blocks);

	for (i = 0; i < length_in_blocks; i++) {
		__u32 index = block_offset + i;
		__u64 erase_block_num = index >>
				fsm->sbi->log_erase_block_size_in_blocks;

		if (!--fsm->sbi->erase_blocks_counters[erase_block_num]) {
			int rc;
			u64 erase_block_size_in_secors = 1llu <<
					(fsm->sbi->log_erase_block_size -
					SECTOR_SIZE_SHIFT);
#ifdef VDFS_DEBUG
			u32 erase_block_first_block = erase_block_num <<
				fsm->sbi->log_erase_block_size_in_blocks;
			int i;
#endif

			if ((erase_block_num << fsm->sbi->log_erase_block_size)
				+ fsm->sbi->erase_block_size > total_size)
				continue;

#ifdef VDFS_DEBUG
			for (i = 0; i < fsm->sbi->erase_block_size_in_blocks;
									i++)
				BUG_ON(test_bit(erase_block_first_block + i,
						fsm->data));
#endif

			rc = blkdev_issue_discard(fsm->sbi->sb->s_bdev,
				erase_block_num * erase_block_size_in_secors,
				erase_block_size_in_secors, GFP_NOFS, 0);
			/*if (rc == -EOPNOTSUPP)
				EMMCFS_ERR("TRIM doesn't support"); */
			if (rc && rc != -EOPNOTSUPP) {
				EMMCFS_ERR("sb_issue_discard %d %llu %u", rc,
						erase_block_num,
						fsm->sbi->erase_block_size);
				BUG();
			}

		}
	}
	mutex_unlock(&fsm->lock);
	for (page_index = start_page; page_index <= end_page; page_index++)
		vdfs_add_chunk_bitmap(fsm->sbi, fsm->pages[page_index], 1);

	return 0;
}

/**
 * @brief				The fsm_free_block_chunk wrapper. This
 *					function is called during truncate or
 *					unlink inode processes.
 * @param [in]	sbi			Superblock information structure.
 * @param [in]	block_offset		Physical number of first block of
 *					inserted chunk
 * @param [in]	length_in_blocks	Inserted blocks count.
 * @param [in]	da			Delay allocation flag
 * @return	error code
 */
int emmcfs_fsm_put_free_block(struct emmcfs_inode_info *inode_info,
		__u64 offset, __u32 length_in_blocks, int da)
{
	int ret;
	struct vdfs_sb_info *sbi = VDFS_SB(inode_info->vfs_inode.i_sb);
	ret = fsm_free_block_chunk(sbi->fsm_info, offset, length_in_blocks, da);
#ifdef CONFIG_VDFS_QUOTA
	if (inode_info->quota_index != -1 && !ret) {
		sbi->quotas[inode_info->quota_index].has -=
				(length_in_blocks << sbi->block_size_shift);
		update_has_quota(sbi, sbi->quotas[inode_info->quota_index].ino,
				inode_info->quota_index);
	}
#endif
	return ret;
}

/**
 * @brief			Puts back to tree preallocated blocks.
 * @param [in]	inode_info	The inode information structure.
 * @return			Returns 0 on success, -1 on failure.
 */
void emmcfs_fsm_discard_preallocation(struct emmcfs_inode_info *inode_info)
{
	struct vdfs_sb_info *sbi = inode_info->vfs_inode.i_sb->s_fs_info;
	struct vdfs_fork_info *fork = &inode_info->fork;

	mutex_lock(&sbi->fsm_info->lock);
	if (fork->prealloc_start_block && fork->prealloc_block_count) {
		fsm_btree_free(&sbi->fsm_info->tree, fork->prealloc_start_block,
			fork->prealloc_block_count);
		fork->prealloc_block_count = 0;
		fork->prealloc_start_block = 0;
	}
	mutex_unlock(&sbi->fsm_info->lock);
}

/**
 * @brief			The fsm_btree_insert wrapper. It inserts
 *				leaf to FSM tree.
 * @param [in]	fsm		Free space manager information structure.
 * @param [in]	block_offset	Physical number of first block of inserted
 *				chunk.
 * @param [in]	length		Inserted blocks count.
 * @return			Returns 0 on success, -1 on failure.
 */
static int fsm_insert(struct emmcfs_fsm_info *fsm,
			__u64 block_offset, __u64 length)
{
	struct  emmcfs_fsm_btree *btree = &fsm->tree;
	struct fsm_btree_key key;
	key.block_offset = block_offset;
	key.length = length;
	EMMCFS_DEBUG_FSM("INSERT off %llu\tlen %llu ",
			 ((struct fsm_btree_key *)&key)->block_offset,
			 ((struct fsm_btree_key *)&key)->length);

	BUG_ON(length == 0);

	if (fsm_btree_insert(btree, &key) != 0)
		return -1;
	percpu_counter_add(&fsm->sbi->free_blocks_count, length);
	return 0;
}

/**
 * @brief			Build free space management tree from
 *				on-disk bitmap.
 * @param [in,out]	fsm	Free space manager information structure.
 * @return			Returns error code.
 */
static int fsm_get_tree(struct emmcfs_fsm_info *fsm)
{
	int ret = 0;
	int mode = FIND_CLEAR_BIT;
	__u64 i;
	__u64 free_start = 0, free_end = 0, pos = 0;
	__u64 bits_count;
	struct vdfs_sb_info *sbi = fsm->sbi;
	bits_count = sbi->total_leb_count << sbi->log_blocks_in_leb;

	while (pos + 1 < bits_count) {
		switch (mode) {
		case FIND_CLEAR_BIT:
			free_start = vdfs_find_next_zero_bit(fsm->data,
					bits_count, pos, sbi->block_size,
					FSM_BMP_MAGIC_LEN);
			/* count erase blocks*/
			for (i = pos; i < free_start; i++) {
				sbi->erase_blocks_counters[(i <<
					sbi->block_size_shift) >>
					sbi->log_erase_block_size]++;
			}

			pos += free_start - pos;
			mode = FIND_SET_BIT;
			break;
		case FIND_SET_BIT:
			free_end = vdfs_find_next_bit(fsm->data, bits_count,
			pos, sbi->block_size, FSM_BMP_MAGIC_LEN);
			pos += free_end - pos;
			ret = fsm_insert(fsm, free_start,
					free_end - free_start);
			if (ret)
				goto exit;
			mode = FIND_CLEAR_BIT;
			break;
		default:
			ret = -EINVAL;
			goto exit;
		}
	}
	if (mode == FIND_SET_BIT && free_start < bits_count)
		fsm_insert(fsm, free_start, pos - 1 - free_start);
exit:
	return ret;
}

/**
 * @brief			Build free space management.
 * @param [in,out]	sb	The VFS superblock
 * @return			Returns 0 on success, -errno on failure.
 */
int emmcfs_fsm_build_management(struct super_block *sb)
{
	struct emmcfs_fsm_info *fsm;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	int err = 0;
	int page_index = 0;
	u64 total_size = (sbi->total_leb_count << sbi->log_blocks_in_leb)
			* sbi->block_size;
	if (sbi->log_erase_block_size == 0) {
		EMMCFS_ERR("wrong erase block size");
		return -EINVAL;
	}

	sbi->erase_blocks_counters =
			kzalloc(sizeof(u32) * ((total_size >>
				sbi->log_erase_block_size) + 1), GFP_KERNEL);
	if (!sbi->erase_blocks_counters)
		return -ENOMEM;

	fsm = kzalloc(sizeof(struct emmcfs_fsm_info), GFP_KERNEL);
	if (!fsm)
		return -ENOMEM;

	fsm->page_count = ((sbi->lebs_bm_blocks_count<<sbi->block_size_shift) +
		(1 << PAGE_SHIFT) - 1) >> PAGE_SHIFT;

	sbi->fsm_info = fsm;
	fsm->sbi = sbi;

	fsm->free_space_start = le64_to_cpu(VDFS_RAW_EXSB(sbi)->
			volume_body.begin);
	mutex_init(&fsm->lock);

	fsm->pages = kzalloc(sizeof(*fsm->pages) * fsm->page_count, GFP_KERNEL);

	if (!fsm->pages) {
		err = -ENOMEM;
		goto fail;
	}
	for (page_index = 0; page_index < fsm->page_count ; page_index++)
		fsm->pages[page_index] = ERR_PTR(-EIO);

	fsm->bitmap_inode = vdfs_special_iget(sb, VDFS_SPACE_BITMAP_INO);

	if (IS_ERR(fsm->bitmap_inode)) {
		err = PTR_ERR(fsm->bitmap_inode);
		goto fail;
	}

	err = vdfs_read_or_create_pages(sbi, fsm->bitmap_inode, fsm->pages, 0,
			fsm->page_count, 0);
	if (err)
		goto fail_no_release;

	fsm->data = vmap(fsm->pages, fsm->page_count, VM_MAP, PAGE_KERNEL);

	if (!fsm->data) {
		EMMCFS_ERR("can't map pages\n");
		err = -ENOMEM;
		goto fail;
	}
	fsm->length_in_bytes = (sbi->lebs_bm_blocks_count-1) * sbi->block_size +
		((sbi->lebs_bm_bits_in_last_block + 7) >> 3);
	emmcfs_init_fsm_btree(&fsm->tree);

	fsm_get_tree(fsm);

	return 0;

fail:
	if (fsm->pages) {
		for (page_index = 0; page_index < fsm->page_count; page_index++)
			if (!IS_ERR(fsm->pages[page_index]))
				page_cache_release(fsm->pages[page_index]);
fail_no_release:
		kfree(fsm->pages);
	}
	if (fsm->bitmap_inode)
		iput(fsm->bitmap_inode);
	kfree(fsm);
	kfree(sbi->erase_blocks_counters);
	sbi->fsm_info = NULL;
	return err;
}

/**
 * @brief			Destroy free space management.
 * @param [in,out]	sb	The VFS superblock.
 * @return		void
 */
void emmcfs_fsm_destroy_management(struct super_block *sb)
{
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct emmcfs_fsm_info *fsm = sbi->fsm_info;
	int page_index = 0;

	EMMCFS_BUG_ON(!fsm->data || !fsm->pages);

	fsm_print_tree(&fsm->tree);

	vunmap((void *)fsm->data);
	for (page_index = 0; page_index < fsm->page_count; page_index++)
		page_cache_release(fsm->pages[page_index]);

	iput(fsm->bitmap_inode);

	fsm_btree_destroy(&fsm->tree);

	kfree(sbi->fsm_info->pages);
	kfree(sbi->fsm_info);
	kfree(sbi->erase_blocks_counters);
	sbi->fsm_info = NULL;
}
