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

#ifndef CATTREE_H_
#define CATTREE_H_

struct vdfs_cattree_record {
	struct emmcfs_cattree_key *key;
	/* Value type can be different */
	void *val;
};

#define VDFS_CATTREE_FOLDVAL(record) \
	((struct vdfs_catalog_folder_record *) (record->val))
#define VDFS_CATTREE_FILEVAL(record) \
	((struct vdfs_catalog_file_record *) (record->val))

struct vdfs_cattree_record *vdfs_cattree_find(struct vdfs_btree *tree,
		__u64 parent_id, const char *name, size_t len,
		enum emmcfs_get_bnode_mode mode);

struct vdfs_cattree_record *vdfs_cattree_find_inode(struct vdfs_btree *tree,
		__u64 object_id, __u64 parent_id, const char *name, size_t len,
		enum emmcfs_get_bnode_mode mode);

struct vdfs_cattree_record *vdfs_cattree_find_hlink(struct vdfs_btree *tree,
		__u64 object_id, enum emmcfs_get_bnode_mode mode);

int vdfs_cattree_remove(struct vdfs_btree *tree, __u64 object_id,
		__u64 parent_id, const char *name, size_t len, u8 record_type);

struct vdfs_cattree_record *vdfs_cattree_get_first_child(
		struct vdfs_btree *tree, __u64 catalog_id);

int vdfs_cattree_get_next_record(struct vdfs_cattree_record *record);

void vdfs_release_cattree_dirty(struct vdfs_cattree_record *record);

struct vdfs_cattree_record *vdfs_cattree_place_record(
		struct vdfs_btree *tree, u64 object_id, u64 parent_id,
		const char *name, size_t len, u8 record_type);

struct vdfs_cattree_record *vdfs_cattree_build_record(struct vdfs_btree * tree,
		__u32 bnode_id, __u32 pos);

#include "vdfs_layout.h"

/**
 * @brief	Catalog tree key compare function for case-sensitive usecase.
 */
bool vdfs_cattree_is_orphan(struct vdfs_cattree_record *record);

/**
 * @brief	Fill already allocated value area (hardlink).
 */
void emmcfs_fill_hlink_value(struct inode *inode,
		struct vdfs_catalog_hlink_record *hl_record);

int vdfs_cattree_insert_ilink(struct vdfs_btree *tree, __u64 object_id,
		__u64 parent_id, const char *name, size_t name_len);
int vdfs_cattree_remove_ilink(struct vdfs_btree *tree, __u64 object_id,
		__u64 parent_id, const char *name, size_t name_len);

int vdfs_cattree_insert_llink(struct vdfs_btree *tree, __u64 object_id,
		__u64 parent_id, const char *name, size_t name_len);
int vdfs_cattree_remove_llink(struct vdfs_btree *tree, __u64 object_id,
		__u64 parent_id, const char *name, size_t name_len);
#endif
