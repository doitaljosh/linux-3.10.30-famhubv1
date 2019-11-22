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

#include "emmcfs.h"

static inline struct emmcfs_inode_info *
prev_orphan(struct vdfs_sb_info *sbi, struct inode *inode)
{
	/*
	 * @inode must be in orphan list and not be the first,
	 */
	BUG_ON(list_empty(&EMMCFS_I(inode)->orphan_list));
	BUG_ON(EMMCFS_I(inode)->orphan_list.prev == &sbi->orphan_inodes);

	return list_entry(EMMCFS_I(inode)->orphan_list.prev,
			struct emmcfs_inode_info, orphan_list);
}

int __vdfs_write_inode(struct vdfs_sb_info *sbi, struct inode *inode);

int vdfs_add_to_orphan(struct vdfs_sb_info *sbi, struct inode *inode)
{
	struct emmcfs_inode_info *prev;
	int ret = 0;

	set_vdfs_inode_flag(inode, ORPHAN_INODE);
	EMMCFS_I(inode)->next_orphan_id = 0;
	list_add_tail(&EMMCFS_I(inode)->orphan_list, &sbi->orphan_inodes);

	prev = prev_orphan(sbi, inode);
	BUG_ON(prev->next_orphan_id != 0);
	prev->next_orphan_id = inode->i_ino;
	ret = __vdfs_write_inode(sbi, &prev->vfs_inode);
	if (ret) {
		EMMCFS_ERR("fail to add inode to orphan list:ino %lu %d",
				 inode->i_ino, ret);
		list_del_init(&EMMCFS_I(inode)->orphan_list);
		prev->next_orphan_id = 0;
		clear_vdfs_inode_flag(inode, ORPHAN_INODE);
		return ret;
	}
	mark_inode_dirty(&prev->vfs_inode);
	return 0;

}

#define FINAL_ORPHAN_ID ((u64) -1)
void vdfs_del_from_orphan(struct vdfs_sb_info *sbi, struct inode *inode)
{
	struct emmcfs_inode_info *prev;
	if (!list_empty(&EMMCFS_I(inode)->orphan_list)) {
		int ret = 0;
		prev = prev_orphan(sbi, inode);
		BUG_ON(prev->next_orphan_id != inode->i_ino);
		prev->next_orphan_id = EMMCFS_I(inode)->next_orphan_id;
		mark_inode_dirty(&prev->vfs_inode);
		ret = __vdfs_write_inode(sbi, &prev->vfs_inode);
		if (ret)
			vdfs_fatal_error(sbi, "delete from orphan list");
		clear_vdfs_inode_flag(inode, ORPHAN_INODE);
		EMMCFS_I(inode)->next_orphan_id = FINAL_ORPHAN_ID;
		list_del_init(&EMMCFS_I(inode)->orphan_list);
	}
}

/**
 * @brief		After mount orphan inodes processing.
 * @param [in]	sbi	Superblock information structure pointer.
 * @return		Returns 0 on success, not null error code on failure.
 */
int vdfs_process_orphan_inodes(struct vdfs_sb_info *sbi)
{
	struct inode *root = sbi->sb->s_root->d_inode, *inode;

	while (EMMCFS_I(root)->next_orphan_id) {
		inode = vdfs_iget(sbi, (ino_t)EMMCFS_I(root)->next_orphan_id);
		if (inode->i_nlink ||
		    !is_vdfs_inode_flag_set(inode, ORPHAN_INODE)) {
			vdfs_fatal_error(sbi,
					"non-orphan ino#%lu in orphan list",
					inode->i_ino);
			return -EINVAL;
		}

		mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
		list_add_tail(&EMMCFS_I(inode)->orphan_list,
				&sbi->orphan_inodes);
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

		iput(inode); /* smash it */
	}

	return 0;
}
