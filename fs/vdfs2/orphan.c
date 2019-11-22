/**
 * @file	fs/emmcfs/orphan.c
 * @brief	The eMMCFS orphan inodes management.
 * @author	Igor Skalkin, i.skalkin@samsung.com
 * @date	24.07.2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * Orphan inodes management implementation.
 *
 * @see		TODO: documents
 *
 * Copyright 2012 by Samsung Electronics, Inc.,
 *e
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include "emmcfs.h"
#include "exttree.h"
#include "hlinktree.h"

int vdfs_fsm_free_runtime_fork(struct emmcfs_inode_info *inode_info,
		struct vdfs_fork_info *fork)
{
	int error = 0;
	int i = 0;

	while (fork->total_block_count && i < VDFS_EXTENTS_COUNT_IN_FORK) {
		__u64 block_offset = fork->extents[i].first_block;
		__u32 length = fork->extents[i].block_count;

		error = emmcfs_fsm_put_free_block(inode_info,
				block_offset, length, 0);
		if (error)
			break;

		fork->total_block_count -= fork->extents[i].block_count;
		fork->extents[i].first_block = 0;
		fork->extents[i].block_count = 0;
		i++;
	}
	return error;
}

static int clear_extents(struct emmcfs_inode_info *inode_info,
		struct vdfs_fork_info *fork)
{
	int error = 0;
	struct vdfs_exttree_record *extent;
	struct vdfs_sb_info *sbi = VDFS_SB(inode_info->vfs_inode.i_sb);

	while (fork->total_block_count > 0) {
		__u64 block_offset, iblock;
		__u64 ino = inode_info->vfs_inode.i_ino;
		__u32 length;

		mutex_w_lock(sbi->extents_tree->rw_tree_lock);
		extent = vdfs_exttree_find_first_record(sbi, ino,
				EMMCFS_BNODE_MODE_RO);
		if (IS_ERR(extent)) {
			/* TODO Remove this if when bug with total block
			 * count mismatch will be fixed */
			if (PTR_ERR(extent) != -ENOENT)
				error = PTR_ERR(extent);
			EMMCFS_ERR("Total blocks count mismatch: inode %llu "
			"left %u blocks", ino, fork->total_block_count);

			mutex_w_unlock(sbi->extents_tree->rw_tree_lock);
			return error;
		}
		block_offset = le64_to_cpu(extent->lextent->begin);
		length = le32_to_cpu(extent->lextent->length);
		iblock = le64_to_cpu(extent->key->iblock);
		vdfs_release_record((struct vdfs_btree_gen_record *) extent);
		error = emmcfs_fsm_put_free_block(inode_info,
				block_offset, length, 0);
		if (error) {
			mutex_w_unlock(sbi->extents_tree->rw_tree_lock);
			return error;
		}

		fork->total_block_count -= length;

		error = vdfs_exttree_remove(sbi->extents_tree, ino, iblock);

		mutex_w_unlock(sbi->extents_tree->rw_tree_lock);
	}

	return error;
}

int vdfs_kill_orphan_inode(struct emmcfs_inode_info *inode_info,
		enum orphan_inode_type type, void *loc_info)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode_info->vfs_inode.i_sb);
	int error = 0;
	char ino_name[UUL_MAX_LEN + 1];
	__u64 ino = inode_info->vfs_inode.i_ino;
	vdfs_start_transaction(sbi);

	switch (type) {
	case ORPHAN_TINY_OR_SPECIAL_FILE:
		break;
	case ORPHAN_SMALL_FILE:
		error = vdfs_free_cell(sbi,
			((struct vdfs_small_file_data_info *)loc_info)->cell);
		if (error)
			goto exit;
#ifdef CONFIG_VDFS_QUOTA
		else if (inode_info->quota_index != -1) {
			sbi->quotas[inode_info->quota_index].has -=
				sbi->small_area->cell_size;
			update_has_quota(sbi,
				sbi->quotas[inode_info->quota_index].ino,
				inode_info->quota_index);
		}
#endif
		break;
	case ORPHAN_REGULAR_FILE:
		error = vdfs_fsm_free_runtime_fork(inode_info, loc_info);
		if (error)
			goto exit;
		error = clear_extents(inode_info, loc_info);

		if (error)
			goto exit;
		break;
	}

	snprintf(ino_name, UUL_MAX_LEN + 1, "%llu", ino);

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	error = vdfs_cattree_remove(sbi, VDFS_ORPHAN_INODES_INO, ino_name,
			strlen(ino_name));
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	if (error)
		goto exit;

	error = vdfs_free_inode_n(sbi, ino, 1);

exit:
	vdfs_stop_transaction(sbi);
	return error;

}

/**
 * @brief		After mount orphan inodes processing.
 * @param [in]	sbi	Superblock information structure pointer.
 * @return		Returns 0 on success, not null error code on failure.
 */
int vdfs_process_orphan_inodes(struct vdfs_sb_info *sbi)
{
	struct vdfs_cattree_record *record;
	int ret = 0;
	struct emmcfs_inode_info *dummy_inode_info;
	struct inode *dummy_inode = sbi->sb->s_op->alloc_inode(sbi->sb);
	if (!dummy_inode)
		return -ENOMEM;
	dummy_inode->i_sb = sbi->sb;
	dummy_inode_info = EMMCFS_I(dummy_inode);

	for (;;) {
		long int flags;
		struct vdfs_catalog_file_record *file_rec;
		struct vdfs_small_file_data_info small;
		struct vdfs_fork_info fork;
		enum orphan_inode_type type;
		void *loc_info = NULL;
#ifdef CONFIG_VDFS_QUOTA
		struct dentry dummy_dentry;
#endif
		__u64 ino;
		__u16 file_mode;

		vdfs_start_transaction(sbi);

		mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
		record = vdfs_cattree_get_first_child(sbi->catalog_tree,
				VDFS_ORPHAN_INODES_INO);
		if (IS_ERR(record)) {
			if (PTR_ERR(record) != -ENOENT)
				ret = PTR_ERR(record);
			break;
		}

		if (record->key->parent_id !=
				cpu_to_le64(VDFS_ORPHAN_INODES_INO)) {
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);
			break;
		}


		file_rec = record->val;
		ino = le64_to_cpu(file_rec->common.object_id);

		ret = vdfs_xattrtree_remove_all(sbi->xattr_tree, ino);
		if (ret) {
			EMMCFS_ERR("can not clear xattrs for ino#%llu", ino);
			break;
		}

		flags = le32_to_cpu(file_rec->common.flags);
		file_mode = le16_to_cpu(file_rec->common.permissions.file_mode);

		if (test_bit(SMALL_FILE, &flags)) {
			small.cell = le64_to_cpu(file_rec->small.cell);
			type = ORPHAN_SMALL_FILE;
			loc_info = &small;
		} else if (!test_bit(TINY_FILE, &flags) && (S_ISREG(file_mode)
				|| S_ISLNK(file_mode))) {

			fork.total_block_count = le32_to_cpu(
				file_rec->data_fork.total_blocks_count);
			emmcfs_lfork_to_rfork(&file_rec->data_fork, &fork);

			type = ORPHAN_REGULAR_FILE;
			loc_info = &fork;
		} else
			type = ORPHAN_TINY_OR_SPECIAL_FILE;

		vdfs_release_record((struct vdfs_btree_gen_record *) record);
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

		dummy_inode->i_ino = ino;
#ifdef CONFIG_VDFS_QUOTA
		dummy_dentry.d_inode = dummy_inode;
		dummy_inode_info->quota_index = get_quota(&dummy_dentry);
		if (IS_ERR_VALUE(dummy_inode_info->quota_index))
			dummy_inode_info->quota_index = -1;

#endif
		ret = vdfs_kill_orphan_inode(dummy_inode_info,
				type, loc_info);
		if (ret)
			break;

		vdfs_stop_transaction(sbi);
	}

	sbi->sb->s_op->destroy_inode(dummy_inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	vdfs_stop_transaction(sbi);
	return ret;
}
