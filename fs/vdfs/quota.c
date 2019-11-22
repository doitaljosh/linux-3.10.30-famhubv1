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
#ifdef CONFIG_VDFS_QUOTA
int build_quota_manager(struct vdfs_sb_info *sbi)
{
	sbi->quotas = kzalloc(INITIAL_QUOTA_ARRAY_SIZE *
			sizeof(struct vdfs_quota), GFP_KERNEL);
	if (!sbi->quotas)
		return -ENOMEM;
	sbi->quota_current_size = INITIAL_QUOTA_ARRAY_SIZE;
	sbi->quota_first_free_index = 0;
	return 0;
}

void destroy_quota_manager(struct vdfs_sb_info *sbi)
{
	kfree(sbi->quotas);
}

int get_next_quota_index(struct vdfs_sb_info *sbi)
{
	int index;

	index = ++sbi->quota_first_free_index;

	if (index > sbi->quota_current_size) {
		void *old_array = sbi->quotas;
		sbi->quotas = kzalloc((size_t)(sbi->quota_current_size << 1) *
				sizeof(struct vdfs_quota), GFP_KERNEL);
		if (!sbi->quotas)
			return -ENOMEM;
		memcpy(sbi->quotas, old_array, (size_t)sbi->quota_current_size *
				sizeof(struct vdfs_quota));
		sbi->quota_current_size <<= 1;
		kfree(old_array);
	}

	return index;
}

int get_quota(struct dentry *dentry)
{
	struct vdfs_sb_info *sbi = VDFS_SB(dentry->d_inode->i_sb);
	int index;
	char temp[UUL_MAX_LEN + 1];
	ssize_t rc;

	memset(temp, 0, UUL_MAX_LEN + 1);
	rc = vdfs_getxattr(dentry, QUOTA_HAS_XATTR, temp, UUL_MAX_LEN);
	if (IS_ERR_VALUE(rc))
		return rc;

	index = get_next_quota_index(sbi);

	BUG_ON(index >= sbi->quota_current_size);

	sbi->quotas[index].ino = dentry->d_inode->i_ino;
	if (kstrtoull(temp, 10, &sbi->quotas[index].has) < 0)
		return -EINVAL;

	memset(temp, 0, UUL_MAX_LEN + 1);
	rc = vdfs_getxattr(dentry, QUOTA_XATTR, temp, UUL_MAX_LEN);
	if (IS_ERR_VALUE(rc))
		return rc;

	if (kstrtoull(temp, 10, &sbi->quotas[index].max) < 0)
		return -EINVAL;
	return index;
}

#endif
