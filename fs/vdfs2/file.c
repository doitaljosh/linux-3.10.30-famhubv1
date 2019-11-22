/**
 * @file	fs/emmcfs/file.c
 * @brief	The eMMCFS file operations.
 * @author	TODO
 * @date	TODO
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 * TODO: Detailed description
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

#include <linux/version.h>
#include "emmcfs.h"
#include "exttree.h"

/**
 * @brief			This function called when a file is opened with
 *				O_TRUNC or truncated with truncate()/
 *				ftruncate() system calls and truncate file
 *				exttree extents according new file size in
 *				blocks.
 * @param [in]	inode_info	inode_info pointer.
 * @param [in]	new_size	new file size in blocks.
 * @return			Returns TODO
 */
static int truncate_exttree(struct emmcfs_inode_info *inode_info,
	u64 new_size)
{
	int ret = 0;
	__u64 object_id = inode_info->vfs_inode.i_ino;
	struct vdfs_sb_info *sbi = inode_info->vfs_inode.i_sb->s_fs_info;
	struct vdfs_exttree_record *record;
	struct vdfs_fork_info *fork = &inode_info->fork;

	while (true) {
		__u64 iblock = 0;
		__u64 ext_off = 0;
		__u32 ext_len = 0;

		record = vdfs_find_last_extent(sbi, object_id,
				EMMCFS_BNODE_MODE_RW);
		if (IS_ERR(record)) {
			if (PTR_ERR(record) == -ENOENT)
				return 0;

			return PTR_ERR(record);
		}


		iblock = le64_to_cpu(record->key->iblock);
		ext_off = le64_to_cpu(record->lextent->begin);
		ext_len = le32_to_cpu(record->lextent->length);

		if (iblock >= new_size) {
			ret = emmcfs_fsm_put_free_block(inode_info,
					ext_off, ext_len, 0);
			if (ret) {
				EMMCFS_ERR("can not free extent while "
						"truncating exttree "
						"ext_off = %llu "
						"ext_len = %u, err = %d",
						ext_off, ext_len, ret);
				break;
			}
			fork->total_block_count -= ext_len;
			inode_sub_bytes(&inode_info->vfs_inode,
				sbi->sb->s_blocksize * ext_len);
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);
			ret = vdfs_exttree_remove(sbi->extents_tree,
					object_id, iblock);
			continue;
		} else if ((iblock + ext_len) > new_size) {
			__u64 delta = iblock + ext_len - new_size;
			ret = emmcfs_fsm_put_free_block(inode_info,
					ext_off + ext_len - delta, delta, 0);
			if (ret) {
				EMMCFS_ERR("can not free part of extent while "
						"truncating exttree "
						"ext_off = %llu "
						"ext_len = %u, err = %d",
						ext_off, ext_len, ret);
				break;
			}

			record->lextent->length = cpu_to_le32(ext_len - delta);
			fork->total_block_count -= delta;
			inode_sub_bytes(&inode_info->vfs_inode,
				sbi->sb->s_blocksize * delta);
			vdfs_mark_record_dirty((struct vdfs_btree_gen_record *)
					record);
			break;
		} else {
			break;
		}
	}

	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	return ret;
}

/**
 * @brief			This function is called when a file is opened
 *				with O_TRUNC or truncated with truncate()/
 *				ftruncate() system calls and truncate internal
 *				fork according new file size in blocks.
 * @param [in]	inode_info	The inode_info pointer.
 * @param [in]	new_size	New file size in blocks.
 * @return	void
 */
static int truncate_fork(struct emmcfs_inode_info *inode_info,
		loff_t new_size_iblocks)
{
	struct vdfs_fork_info *fork = &inode_info->fork;
	struct vdfs_sb_info *sbi = inode_info->vfs_inode.i_sb->s_fs_info;
	int err = 0, i;
	struct vdfs_extent_info *extent = NULL;


	if (!fork->used_extents)
		return 0;

	for (i = fork->used_extents - 1; i >= 0; i--) {
		extent = &fork->extents[i];
		if (extent->iblock >= new_size_iblocks) {
			err = emmcfs_fsm_put_free_block(inode_info,
				extent->first_block, extent->block_count, 0);
			if (err)
				goto exit;

			fork->total_block_count -= extent->block_count;
			inode_sub_bytes(&inode_info->vfs_inode,
				sbi->sb->s_blocksize * extent->block_count);
			fork->used_extents--;
			memset(extent, 0x0, sizeof(*extent));
		} else
			break;
	}

	if (extent->iblock + extent->block_count > new_size_iblocks) {
		sector_t block_count = extent->iblock + extent->block_count
				- new_size_iblocks;
		sector_t cut_from_block = extent->first_block +
				new_size_iblocks - extent->iblock;

		/* truncate extent */
		err = emmcfs_fsm_put_free_block(inode_info, cut_from_block,
				block_count, 0);
		if (err)
			goto exit;

		extent->block_count -= block_count;
		fork->total_block_count -= block_count;
		inode_sub_bytes(&inode_info->vfs_inode, sbi->sb->s_blocksize *
			block_count);
	}
exit:
	return err;
}

/**
 * @brief			This function is called when a file is opened
 *				with O_TRUNC or	truncated with truncate()/
 *				ftruncate() system calls.
 *				1) truncates exttree extents according to new
 *				file size.
 *				2) if fork intetnal extents contains more
 *				extents than new file size in blocks, internal
 *				fork also truncated.
 * @param [in]	inode		VFS inode pointer
 * @param [in]	new_size	New file size in bytes.
 * @return			Returns 0 on success, not null error code on
 *				failure.
 */
int vdfs_truncate_blocks(struct inode *inode, loff_t new_size)
{
	int ret = 0;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_fork_info *fork = &inode_info->fork;
	struct vdfs_sb_info	*sbi = inode->i_sb->s_fs_info;
	sector_t new_size_iblocks;
	sector_t freed_runtime_iblocks;

	if (inode->i_ino < VDFS_1ST_FILE_INO)
		EMMCFS_BUG();
	if (!(S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return -EPERM;

	new_size_iblocks = (new_size + sbi->block_size - 1) >>
			(sbi->block_size_shift);

	EMMCFS_DEBUG_FSM("truncate ino %lu\told_size %d\tnew_size %llu",
		inode->i_ino, fork->total_block_count, new_size);
	freed_runtime_iblocks = vdfs_truncate_runtime_blocks(new_size_iblocks,
		&inode_info->runtime_extents);
	vdfs_free_reserved_space(inode, freed_runtime_iblocks);
	if (fork->used_extents == VDFS_EXTENTS_COUNT_IN_FORK) {
		mutex_w_lock(sbi->extents_tree->rw_tree_lock);
		ret = truncate_exttree(inode_info, new_size_iblocks);
		mutex_w_unlock(sbi->extents_tree->rw_tree_lock);
		if (ret)
			goto error_exit;
	}
	if (fork->extents[fork->used_extents - 1].iblock +
		fork->extents[fork->used_extents - 1].block_count >
			new_size_iblocks)
		ret = truncate_fork(inode_info, new_size_iblocks);
error_exit:
	return ret;
}
