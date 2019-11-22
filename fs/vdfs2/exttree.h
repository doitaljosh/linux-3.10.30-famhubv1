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
		struct vdfs_extent_info *extent);
#endif
