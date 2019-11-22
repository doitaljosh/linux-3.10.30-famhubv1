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
#include "debug.h"
#include "cattree.h"

int get_record_type_on_mode(struct inode *inode, u8 *record_type)
{
	umode_t mode = inode->i_mode;

	if (is_vdfs_inode_flag_set(inode, HARD_LINK))
		*record_type = VDFS_CATALOG_HLINK_RECORD;
	else if (is_dlink(inode))
		*record_type = VDFS_CATALOG_DLINK_RECORD;
	else if (S_ISDIR(mode) || S_ISFIFO(mode) ||
		S_ISSOCK(mode) || S_ISCHR(mode) || S_ISBLK(mode))
		*record_type = VDFS_CATALOG_FOLDER_RECORD;
	else if (S_ISREG(mode) || S_ISLNK(mode))
		*record_type = VDFS_CATALOG_FILE_RECORD;
	else
		return -EINVAL;
	return 0;
}

/**
 * @brief			Fill already allocated value area (file or
 *				folder) with data from VFS inode.
 * @param [in]	inode		The inode to fill value area with
 * @param [out] value_area	Pointer to already allocated memory area
 *				representing the corresponding eMMCFS object
 * @return			Returns 0 on success or type of record
 *				if it's < 0
 */
void emmcfs_fill_cattree_value(struct inode *inode, void *value_area)
{
	struct vdfs_catalog_folder_record *comm_rec = value_area;

	vdfs_get_vfs_inode_flags(inode);
	comm_rec->flags = cpu_to_le32(EMMCFS_I(inode)->flags);

	/* TODO - set magic */
/*	memcpy(comm_rec->magic, get_magic(le16_to_cpu(comm_rec->record_type)),
			sizeof(EMMCFS_CAT_FOLDER_MAGIC) - 1);*/
	comm_rec->file_mode = cpu_to_le16(inode->i_mode);
	comm_rec->uid = cpu_to_le32(i_uid_read(inode));
	comm_rec->gid = cpu_to_le32(i_gid_read(inode));

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		comm_rec->total_items_count = cpu_to_le64(inode->i_rdev);
	else
		comm_rec->total_items_count = cpu_to_le64(inode->i_size);

	comm_rec->links_count = cpu_to_le64(inode->i_nlink);

	comm_rec->creation_time = vdfs_encode_time(inode->i_ctime);
	comm_rec->access_time = vdfs_encode_time(inode->i_atime);
	comm_rec->modification_time = vdfs_encode_time(inode->i_mtime);

	comm_rec->generation = cpu_to_le32(inode->i_generation);
	comm_rec->next_orphan_id = cpu_to_le64(EMMCFS_I(inode)->next_orphan_id);

	if (is_dlink(inode)) {
		struct vdfs_catalog_dlink_record *dlink = value_area;

		dlink->data_inode =
			cpu_to_le64(EMMCFS_I(inode)->data_link.inode->i_ino);
		dlink->data_offset =
			cpu_to_le64(EMMCFS_I(inode)->data_link.offset);
		dlink->data_length = cpu_to_le64(inode->i_size);
	} else if (S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode)) {
		struct vdfs_catalog_file_record *file_rec = value_area;
		vdfs_form_fork(&file_rec->data_fork, inode);
	}
}


/**
 * @brief			Fill already allocated value area (hardlink).
 * @param [in]	inode		The inode to fill value area with
 * @param [out]	hl_record	Pointer to already allocated memory area
 *				representing the corresponding eMMCFS
 *				hardlink object
 * @return	void
 */
void emmcfs_fill_hlink_value(struct inode *inode,
		struct vdfs_catalog_hlink_record *hl_record)
{
	hl_record->file_mode = cpu_to_le16(inode->i_mode);
}

