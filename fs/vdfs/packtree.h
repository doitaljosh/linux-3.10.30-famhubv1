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

#ifndef PACKTREE_H_
#define PACKTREE_H_

/**
 * The vdfs packtree operations.
 */
#define SQUASHFS_COMPRESSED_BIT_BLOCK	(1 << 24)

struct installed_packtree_info {
	struct vdfs_pack_insert_point_info params;
	struct list_head list;
	struct vdfs_btree *tree;
	__u64 *chunk_table; /* chunk table (sorted array of chunk offsets) */

	atomic_t open_count; /* count of open files in this packtree */
	/* hold unpacked pages */
	struct inode *unpacked_inode;
#ifdef CONFIG_VDFS_DEBUG
	/* print only once*/
	int print_once;
#endif

};

struct xattr_entry {
	__le16	type;
	__le16	size;
	char	data[0];
};

struct xattr_val {
	__le32	vsize;
	char	value[0];
};

struct inode *get_packtree_image_inode(struct vdfs_sb_info *sbi,
		__u64 parent_id, __u8 *name, int name_len);

int *destroy_packtrees_list(struct vdfs_sb_info *sbi);

void destroy_packtree(struct installed_packtree_info *ptr);
int vdfs_uninstall_packtree(struct file *filp);
int read_squashfs_image_simple(struct inode *inode, __u64 offset, __u32 length,
		void *data);

extern const struct address_space_operations emmcfs_aops;
extern const struct file_operations emmcfs_dir_operations;

#endif
