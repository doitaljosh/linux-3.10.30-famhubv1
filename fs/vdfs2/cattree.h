
#ifndef CATTREE_H_
#define CATTREE_H_

#define VDFS_ROOTDIR_NAME "root"
#define VDFS_ROOTDIR_OBJ_ID ((__u64) 0)
#define EMMCFS_CATALOG_KEY_SIGNATURE_FILE "NDlf"
#define EMMCFS_CATALOG_KEY_SIGNATURE_FOLDER "NDld"
#define EMMCFS_CATALOG_KEY_SIGNATURE_HLINK "NDlh"
#define EMMCFS_EXT_OVERFL_LEAF "NDle"

struct vdfs_cattree_record {
	struct emmcfs_cattree_key *key;
	/* Value type can be different */
	void *val;
};

#define VDFS_CATTREE_FOLDVAL(record) \
	((struct vdfs_catalog_folder_record *) (record->val))

struct vdfs_cattree_record *vdfs_cattree_find(struct vdfs_btree *tree,
		__u64 parent_id, const char *name, int len,
		enum emmcfs_get_bnode_mode mode);

int vdfs_cattree_remove(struct vdfs_sb_info *sbi,
		__u64 parent_id, const char *name, int len);

struct vdfs_cattree_record *vdfs_cattree_get_first_child(
		struct vdfs_btree *tree, __u64 catalog_id);

int vdfs_cattree_get_next_record(struct vdfs_cattree_record *record);

void vdfs_release_cattree_dirty(struct vdfs_cattree_record *record);

struct vdfs_cattree_record *vdfs_cattree_place_record(
		struct vdfs_btree *tree, u64 parent_id,
		const char *name, int len, u8 record_type);

struct vdfs_cattree_record *vdfs_cattree_build_record(struct vdfs_btree * tree,
		__u32 bnode_id, __u32 pos);

#include "vdfs_layout.h"

/**
 * @brief	Catalog tree key compare function for case-sensitive usecase.
 */
int emmcfs_cattree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);

/**
 * @brief	Fill already allocated value area (hardlink).
 */
void emmcfs_fill_hlink_value(struct inode *inode,
		struct vdfs_catalog_hlink_record *hl_record);

#endif
