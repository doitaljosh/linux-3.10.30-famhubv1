/**
 * @file	hlinktree.c
 * @brief	Hard link B-tree, contains hard link value
 * @date	24/05/2013
 *
 * VDFS -- Samsung eMMC chip oriented File System.
 *
 * This file contains hard link btree implementation
 *
 * @see
 *
 * Copyright 2013 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef USER_SPACE
#include <linux/slab.h>
#include <linux/ctype.h>

#else
#include "vdfs_tools.h"
#include <ctype.h>
#endif
#include "emmcfs.h"
#include "hlinktree.h"
#include "debug.h"


/**
 * @brief		Extents tree key compare function.
 * @param [in]	__key1	first key to compare.
 * @param [in]	__key2	second key to compare.
 * @return		-1 if key1 less than key2, 0 if equal, 1 in all other
 *			situations.
 */
int vdfs_hlinktree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2)
{
	struct vdfs_hlinktree_key *key1, *key2;

	key1 = container_of(__key1, struct vdfs_hlinktree_key, gen_key);
	key2 = container_of(__key2, struct vdfs_hlinktree_key, gen_key);

	return cmp_2_le64 (key1->inode_ino, key2->inode_ino);
}
#ifndef USER_SPACE
static struct kmem_cache *hardlinks_tree_key_cachep;
/**
 * @brief		hlinktree key structure initializer.
 * @param [in,out] key	hlinktree key for initialization.
 * @return		void
 */
static void hlinktree_key_ctor(void *key)
{
	memset(key, 0, sizeof(struct vdfs_hlinktree_key));
}

int vdfs_hlinktree_cache_init(void)
{
	hardlinks_tree_key_cachep = kmem_cache_create("vdfs_hlinktree_key",
		sizeof(struct vdfs_hlinktree_key), 0,
		SLAB_HWCACHE_ALIGN, hlinktree_key_ctor);
	if (!hardlinks_tree_key_cachep) {
		EMMCFS_ERR("failed to initialize hardlinks key cache\n");
		return -ENOMEM;
	}
	return 0;
}

void vdfs_hlinktree_cache_destroy(void)
{
	kmem_cache_destroy(hardlinks_tree_key_cachep);
}

struct vdfs_hlinktree_key *vdfs_get_hlinktree_key(void)
{
	struct vdfs_hlinktree_key *key =
		kmem_cache_alloc(hardlinks_tree_key_cachep, __GFP_WAIT);
	return key ? key : ERR_PTR(-ENOMEM);
}

void vdfs_put_hlinktree_key(struct vdfs_hlinktree_key *key)
{
	kmem_cache_free(hardlinks_tree_key_cachep, key);
}
#endif


/**
 * @brief		Interface for finding hard links in hlinktree
 * @param [in] sbi		vdfs superblock info, used to get exttree.
 * @param [in] inode_i_ino	inode ino number to perform search
 * @param [out] mode		getting bnode mode (RW or RO).
 * @return		resulting record.
 */
struct vdfs_hlink_record *vdfs_hard_link_find(
		struct vdfs_sb_info *sbi, __u64 inode_i_ino,
		enum emmcfs_get_bnode_mode mode)
{
	struct vdfs_hlink_record *record;
	struct vdfs_btree *btree = sbi->hardlink_tree;
	struct vdfs_hlinktree_key *key;

	key = vdfs_get_hlinktree_key();
	if (IS_ERR(key))
		return ERR_PTR(-ENOMEM);

	key->inode_ino = cpu_to_le64(inode_i_ino);

	record = (struct vdfs_hlink_record *) vdfs_btree_find(btree,
			&key->gen_key, mode);

	vdfs_put_hlinktree_key(key);
	return record;
}

/**
 * @brief		Interface for inserting  hard links in hlinktree
 * @param [in] sbi		vdfs superblock info, used to get exttree.
 * @param [in] inode_i_ino	inode ino number to perform search
 * @param [out] mode		getting bnode mode (RW or RO).
 * @return		resulting record.
 */
int vdfs_hard_link_insert(struct vdfs_sb_info *sbi,
		struct vdfs_catalog_file_record *file_value)
{
	int ret = 0;
	struct vdfs_hdrtree_record *record;

	record = kzalloc(sizeof(*record), GFP_KERNEL);

	if (!record)
		return -ENOMEM;
	mutex_w_lock_nested(sbi->hardlink_tree->rw_tree_lock, HL_TREE);
	/* Fill the magic */
	memcpy(record->key.gen_key.magic, VDFS_HRDTREE_KEY_MAGIC,
			sizeof(VDFS_HRDTREE_KEY_MAGIC) - 1);

	/* Fill the key */
	record->key.gen_key.key_len = cpu_to_le32(sizeof(record->key));
	record->key.gen_key.record_len = cpu_to_le32(sizeof(*record));
	record->key.inode_ino = file_value->common.object_id;

	/* Fill the value */
	memcpy(&record->hardlink_value, file_value, sizeof(*file_value));

	ret = emmcfs_btree_insert(sbi->hardlink_tree, record);
	kfree(record);
	mutex_w_unlock(sbi->hardlink_tree->rw_tree_lock);
	return ret;
}
#ifndef USER_SPACE
int vdfs_hard_link_remove(struct vdfs_sb_info *sbi, __u64 inode_i_ino)
{
	int ret;
	struct vdfs_hlinktree_key *key;

	mutex_w_lock_nested(sbi->hardlink_tree->rw_tree_lock, HL_TREE);
	key = vdfs_get_hlinktree_key();
	if (!key)
		return -ENOMEM;

	key->inode_ino = le64_to_cpu(inode_i_ino);
	ret = emmcfs_btree_remove(sbi->hardlink_tree, &key->gen_key);

	vdfs_put_hlinktree_key(key);
	mutex_w_unlock(sbi->hardlink_tree->rw_tree_lock);
	return ret;
}
#endif
