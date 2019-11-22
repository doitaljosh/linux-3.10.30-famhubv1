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

#ifndef EXTTREE_H_
#define EXTTREE_H_

#ifdef USER_SPACE
#include "vdfs_tools.h"
#endif

#include "vdfs_layout.h"
#ifndef USER_SPACE
#include "btree.h"
#endif

/* emmcfs_exttree_key.iblock special values */
#define IBLOCK_DOES_NOT_MATTER	0xFFFFFFFF /* any extent for specific inode */
#define IBLOCK_MAX_NUMBER	0xFFFFFFFE /* find last extent ... */

struct vdfs_exttree_record {
	/** Key */
	struct emmcfs_exttree_key *key;
	/** Extent for key */
	struct vdfs_extent *lextent;
};

struct vdfs_exttree_record *vdfs_extent_find(struct vdfs_sb_info *sbi,
		__u64 object_id, sector_t iblock, enum emmcfs_get_bnode_mode
		mode);

int vdfs_exttree_get_next_record(struct vdfs_exttree_record *record);

struct vdfs_exttree_record *vdfs_exttree_find_first_record(
		struct vdfs_sb_info *sbi, __u64 object_id,
		enum emmcfs_get_bnode_mode mode);

struct  vdfs_exttree_record *vdfs_find_last_extent(struct vdfs_sb_info *sbi,
		__u64 object_id, enum emmcfs_get_bnode_mode mode);

int vdfs_exttree_remove(struct vdfs_btree *btree, __u64 object_id,
		sector_t iblock);
/**
 * @brief	Extents tree key compare function.
 */
int emmcfs_exttree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);
int emmcfs_exttree_add(struct vdfs_sb_info *sbi, unsigned long object_id,
		struct vdfs_extent_info *extent, int force_insert);
#endif
