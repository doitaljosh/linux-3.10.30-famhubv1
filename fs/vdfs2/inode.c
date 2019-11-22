/**
 * @file	fs/emmcfs/inode.c
 * @brief	Basic inode operations.
 * @date	01/17/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements inode operations and its related functions.
 *
 * @see		TODO: documents
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/nls.h>
#include <linux/mpage.h>
#include <linux/version.h>
#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/bio.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>
#include <linux/security.h>

#include "emmcfs.h"
#include "cattree.h"
#include "packtree.h"
#include "hlinktree.h"
#include "debug.h"

/* For testing purpose use fake page cash size */
/*#define EMMCFS_PAGE_SHIFT 5*/
/*#define EMMCFS_BITMAP_PAGE_SHIFT (3 + EMMCFS_PAGE_SHIFT)*/
/*#define EMMCFS_BITMAP_PAGE_MASK (((__u64) 1 << EMMCFS_BITMAP_PAGE_SHIFT)\
 * - 1)*/

/**
 * @brief		Create inode.
 * @param [out]	dir		The inode to be created
 * @param [in]	dentry	Struct dentry with information
 * @param [in]	mode	Mode of creation
 * @param [in]	nd		Struct with name data
 * @return		Returns 0 on success, errno on failure
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static int emmcfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl);
#else
static int emmcfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd);
#endif

static int vdfs_get_block_prep_da(struct inode *inode, sector_t iblock,
		struct buffer_head *bh_result, int create) ;

/**
 * @brief		Allocate new inode.
 * @param [in]	dir		Parent directory
 * @param [in]	mode	Mode of operation
 * @return		Returns pointer to newly created inode on success,
 *			errno on failure
 */
static struct inode *emmcfs_new_inode(struct inode *dir, umode_t mode);
struct inode *get_inode_from_record(struct vdfs_cattree_record *record);

/**
 * @brief		Get root folder.
 * @param [in]	tree	Pointer to btree information
 * @param [out] fd	Buffer for finded data
 * @return		Returns 0 on success, errno on failure
 */
struct inode *get_root_inode(struct vdfs_btree *tree)
{
	struct inode *root_inode = NULL;
	struct vdfs_cattree_record *record = NULL;

	record = vdfs_cattree_find(tree, VDFS_ROOTDIR_OBJ_ID,
			VDFS_ROOTDIR_NAME, sizeof(VDFS_ROOTDIR_NAME) - 1,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		/* Pass error code to return value */
		root_inode = (void *)record;
		goto exit;
	}

	root_inode = get_inode_from_record(record);
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
exit:
	return root_inode;
}


static int vdfs_fill_cattree_record(struct inode *inode,
		struct vdfs_cattree_record *record)
{
	void *pvalue = record->val;
	int ret = 0;

	BUG_ON(!pvalue || IS_ERR(pvalue));

	EMMCFS_I(inode)->record_type = record->key->record_type;

	if (VDFS_GET_CATTREE_RECORD_TYPE(record) == VDFS_CATALOG_HLINK_RECORD)
		emmcfs_fill_hlink_value(inode, pvalue);
	else
		ret = emmcfs_fill_cattree_value(inode, pvalue);

	return ret;
}

/* Replacement for emmcfs_add_new_cattree_object */
static int vdfs_insert_cattree_object(struct vdfs_btree *tree,
		struct inode *inode, u64 parent_id)
{
	struct vdfs_cattree_record *record = NULL;
	u8 record_type;
	int ret = 0;
	char *name = EMMCFS_I(inode)->name;
	int len = strlen(name);

	ret = get_record_type_on_mode(inode, &record_type);

	if (ret)
		return ret;

	record = vdfs_cattree_place_record(tree, parent_id, name, len,
			record_type);

	if (IS_ERR(record))
		return PTR_ERR(record);

	ret = vdfs_fill_cattree_record(inode, record);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);

	return ret;
}

/**
 * @brief		Method to read (list) directory.
 * @param [in]	filp	File pointer
 * @param [in]	dirent	Directory entry
 * @param [in]	filldir	Callback filldir for kernel
 * @return		Returns count of files/dirs
 */
static int emmcfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct dentry *dentry = filp->f_dentry;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	__u64 catalog_id = inode->i_ino;
	int ret = 0;
	struct vdfs_cattree_record *record;
	struct vdfs_btree *btree = sbi->catalog_tree;
	/* return 0 if no more entries in the directory */
	switch (filp->f_pos) {
	case 0:
		if (filldir(dirent, ".", 1, filp->f_pos++, inode->i_ino,
					DT_DIR))
			goto exit_noput;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, filp->f_pos++,
			dentry->d_parent->d_inode->i_ino, DT_DIR))
			goto exit_noput;
		break;
	default:
		if (!filp->private_data)
			return 0;
		break;
	}

	if (inode_info->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		if (inode_info->ptree.tree_info == NULL) {
			struct installed_packtree_info *packtree;
			packtree = vdfs_get_packtree(inode);
			if (IS_ERR(packtree))
				return PTR_ERR(packtree);
			inode_info->ptree.tree_info = packtree;
			atomic_set(&(packtree->open_count), 0);
		}

		if (inode_info->record_type == VDFS_CATALOG_PTREE_ROOT)
			catalog_id =  VDFS_ROOT_INO;
		else
			catalog_id -=
				inode_info->ptree.tree_info->params.start_ino;

		btree = inode_info->ptree.tree_info->tree;
	}

	EMMCFS_DEBUG_MUTEX("cattree mutex r lock");
	mutex_r_lock(sbi->catalog_tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex r lock succ");
	if (!filp->private_data) {
		record = vdfs_cattree_get_first_child(btree, catalog_id);
	} else {
		char *name = filp->private_data;
		record = vdfs_cattree_find(btree, catalog_id,
				name, strlen(name), EMMCFS_BNODE_MODE_RO);
	}

	if (IS_ERR(record)) {
		ret = (PTR_ERR(record) == -EISDIR) ? 0 : PTR_ERR(record);
		goto fail;
	}

	while (1) {
		struct vdfs_catalog_folder_record *cattree_val;
		__u64 obj_id = 0;
		u8 record_type;
		umode_t object_mode;

		if (record->key->parent_id != cpu_to_le64(catalog_id))
			goto exit;

		cattree_val = record->val;
		record_type = record->key->record_type;
		if (record_type >= VDFS_CATALOG_PTREE_FOLDER) {
			object_mode = le16_to_cpu(
				((struct vdfs_pack_common_value *)record->val)->
				permissions.file_mode);
			obj_id = le64_to_cpu(((struct vdfs_pack_common_value *)
				record->val)->object_id) + EMMCFS_I(inode)->
					ptree.tree_info->params.start_ino;
		} else if (record_type == VDFS_CATALOG_PTREE_ROOT) {
			struct vdfs_pack_insert_point_value *val = record->val;
			object_mode =
				le16_to_cpu(val->common.permissions.file_mode);
			obj_id = le64_to_cpu(val->common.object_id);
		} else if (record_type == VDFS_CATALOG_HLINK_RECORD) {
			object_mode = le16_to_cpu((
					(struct vdfs_catalog_hlink_record *)
					cattree_val)->file_mode);
			obj_id = le64_to_cpu(
					((struct vdfs_catalog_hlink_record *)
					cattree_val)->object_id);
		} else {
			object_mode = le16_to_cpu(cattree_val->
					permissions.file_mode);
			obj_id = le64_to_cpu(cattree_val->object_id);
		}

		ret = filldir(dirent, record->key->name.unicode_str,
				record->key->name.length,
				filp->f_pos, obj_id, IFTODT(object_mode));

		if (ret) {
			char *private_data;
			if (!filp->private_data) {
				private_data = kzalloc(EMMCFS_CAT_MAX_NAME,
						GFP_KERNEL);
				filp->private_data = private_data;
				if (!private_data) {
					ret = -ENOMEM;
					goto fail;
				}
			} else {
				private_data = filp->private_data;
			}

			strncpy(private_data, record->key->name.unicode_str,
					EMMCFS_CAT_MAX_NAME);

			ret = 0;
			goto exit;
		}

		++filp->f_pos;
		ret = vdfs_cattree_get_next_record(record);
		if ((ret == -ENOENT) ||
			record->key->parent_id != cpu_to_le64(catalog_id)) {
			/* No more entries */
			kfree(filp->private_data);
			filp->private_data = NULL;
			ret = 0;
			break;
		} else if (ret) {
			goto fail;
		}

	}

exit:
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
exit_noput:
	EMMCFS_DEBUG_MUTEX("cattree mutex r lock un");
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	return ret;
fail:
	EMMCFS_DEBUG_INO("finished with err (%d)", ret);
	if (!IS_ERR_OR_NULL(record))
		vdfs_release_record((struct vdfs_btree_gen_record *) record);

	EMMCFS_DEBUG_MUTEX("cattree mutex r lock un");
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	return ret;
}

/**
 * @brief		Method to look up an entry in a directory.
 * @param [in]	dir		Parent directory
 * @param [in]	dentry	Searching entry
 * @param [in]	nd		Associated nameidata
 * @return		Returns pointer to found dentry, NULL if it is
 *			not found, ERR_PTR(errno) on failure
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
struct dentry *emmcfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags)
#else
struct dentry *emmcfs_lookup(struct inode *dir, struct dentry *dentry,
						struct nameidata *nd)
#endif
{
	struct super_block *sb = dir->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct vdfs_cattree_record *record;
	struct emmcfs_inode_info *dir_inode_info = EMMCFS_I(dir);
	struct inode *found_inode = NULL;
	int err = 0;
	struct vdfs_btree *tree = sbi->catalog_tree;
	__u64 catalog_id = dir->i_ino;
	struct dentry *ret;

	if (dentry->d_name.len > EMMCFS_CAT_MAX_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	if (!S_ISDIR(dir->i_mode))
		return ERR_PTR(-ENOTDIR);

	if (dir_inode_info->record_type == VDFS_CATALOG_PTREE_ROOT) {
		if (dir_inode_info->ptree.tree_info == NULL) {
			struct installed_packtree_info *packtree;
			packtree = vdfs_get_packtree(dir);
			if (IS_ERR_OR_NULL(packtree))
				return (struct dentry *)packtree;
			dir_inode_info->ptree.tree_info = packtree;
			atomic_set(&(packtree->open_count), 0);
		}
	}

	if (EMMCFS_I(dir)->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		tree = EMMCFS_I(dir)->ptree.tree_info->tree;
		if (EMMCFS_I(dir)->record_type == VDFS_CATALOG_PTREE_ROOT)
			catalog_id = VDFS_ROOT_INO;
		else
			catalog_id -=
			EMMCFS_I(dir)->ptree.tree_info->params.start_ino;
	}

	EMMCFS_DEBUG_MUTEX("cattree mutex r lock");
	mutex_r_lock(tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex r lock succ");

	record = vdfs_cattree_find(tree, catalog_id,
			dentry->d_name.name, dentry->d_name.len,
			EMMCFS_BNODE_MODE_RO);
	if (!record) {
		err = -EFAULT;
		goto exit;
	}
	if (IS_ERR(record)) {
		/* Pass error code to return value */
		err = PTR_ERR(record);
		goto exit;
	}

	found_inode = get_inode_from_record(record);
	if (!found_inode)
		err = -EFAULT;
	if (IS_ERR(found_inode))
		err = PTR_ERR(found_inode);

	vdfs_release_record((struct vdfs_btree_gen_record *) record);
#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(found_inode)->quota_index = EMMCFS_I(dir)->quota_index;
#endif

	if ((!err) &&
		(EMMCFS_I(dir)->record_type >= VDFS_CATALOG_PTREE_RECORD)) {
		EMMCFS_I(found_inode)->ptree.tree_info =
				EMMCFS_I(dir)->ptree.tree_info;
	}

exit:
	EMMCFS_DEBUG_MUTEX("cattree mutex r lock un");
	mutex_r_unlock(tree->rw_tree_lock);

	if (err && err != -ENOENT)
		return ERR_PTR(err);
	else {
		ret = d_splice_alias(found_inode, dentry);
#ifdef CONFIG_VDFS_QUOTA
		if (found_inode &&
			is_vdfs_inode_flag_set(found_inode, HAS_QUOTA) &&
				EMMCFS_I(found_inode)->quota_index == -1) {
			int index = get_quota(dentry);
			if (IS_ERR_VALUE(index))
				return ERR_PTR(index);

			EMMCFS_I(found_inode)->quota_index = index;
		}
#endif
		return ret;
	}
}

/**
 * @brief		Get free inode index[es].
 * @param [in]	sbi	Pointer to superblock information
 * @param [out]	i_ino	Resulting inode number
 * @param [in]	count	Requested inode numbers count.
 * @return		Returns 0 if success, err code if fault
 */
int vdfs_get_free_inode(struct vdfs_sb_info *sbi, ino_t *i_ino, int count)
{
	struct page *page = NULL;
	void *data;
	__u64 last_used = atomic64_read(&sbi->free_inode_bitmap.last_used);
	__u64 page_index = last_used;
	/* find_from is modulo, page_index is result of div */
	__u64 find_from = do_div(page_index, VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN));
	/* first id on the page. */
	unsigned int id_offset = page_index * VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN);
	/* bits per block */
	int data_size = VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN);
	int err = 0;
	int pass = 0;
	int start_page = page_index;
	pgoff_t total_pages = VDFS_LAST_TABLE_INDEX(sbi,
			VDFS_FREE_INODE_BITMAP_INO) + 1;
	*i_ino = 0;

	if (count > data_size)
		return -ENOMEM; /* todo we can allocate inode numbers chunk
		 only within one page*/

	while (*i_ino == 0) {
		page = vdfs_read_or_create_page(sbi->free_inode_bitmap.inode,
				page_index);
		if (IS_ERR_OR_NULL(page))
			return PTR_ERR(page);
		lock_page(page);

		data = kmap(page);
		*i_ino = bitmap_find_next_zero_area(data +
			INODE_BITMAP_MAGIC_LEN,
			data_size, find_from, count, 0);
		/* free area is found */
		if ((*i_ino + count - 1) < data_size) {
			EMMCFS_BUG_ON(*i_ino + id_offset < VDFS_1ST_FILE_INO);
			if (count > 1)
				bitmap_set(data + INODE_BITMAP_MAGIC_LEN,
						*i_ino, count);
			else
				EMMCFS_BUG_ON(test_and_set_bit(*i_ino,
						data + INODE_BITMAP_MAGIC_LEN));
			*i_ino += id_offset;
			if (atomic64_read(&sbi->free_inode_bitmap.last_used) <
				(*i_ino  + count - 1))
				atomic64_set(&sbi->free_inode_bitmap.last_used,
					*i_ino + count - 1);

			vdfs_add_chunk_bitmap(sbi, page, 1);
		} else { /* if no free bits in current page */
			*i_ino = 0;
			page_index++;
			/* if we reach last page go to first one */
			page_index = (page_index == total_pages) ? 0 :
					page_index;
			/* if it's second cycle expand the file */
			if (pass == 1)
				page_index = total_pages;
			/* if it's start page, increase pass counter */
			else if (page_index == start_page)
				pass++;
			id_offset = ((int)page_index) * data_size;
			/* if it's first page, increase the inode generation */
			if (page_index == 0) {
				/* for first page we should look up from
				 * EMMCFS_1ST_FILE_INO bit*/
				atomic64_set(&sbi->free_inode_bitmap.last_used,
					VDFS_1ST_FILE_INO);
				find_from = VDFS_1ST_FILE_INO;
				/* increase generation of the inodes */
				le32_add_cpu(
					&(VDFS_RAW_EXSB(sbi)->generation), 1);
				vdfs_dirty_super(sbi);
			} else
				find_from = 0;
			EMMCFS_DEBUG_INO("move to next page"
				" ind = %lld, id_off = %d, data = %d\n",
				page_index, id_offset, data_size);
		}

		kunmap(page);
		unlock_page(page);
		page_cache_release(page);

	}

	return err;
}

/**
 * @brief		Free several inodes.
 *		Agreement: inode chunks (for installed packtrees)
 *		can be allocated only within single page of inodes bitmap.
 *		So free requests also can not exceeds page boundaries.
 * @param [in]	sbi	Superblock information
 * @param [in]	inode_n	Start index of inodes to be free
 * @param [in]	count	Count of inodes to be free
 * @return		Returns error code
 */
int vdfs_free_inode_n(struct vdfs_sb_info *sbi, __u64 inode_n, int count)
{
	void *data;
	struct page *page = NULL;
	__u64 page_index = inode_n;
	/* offset inside page */
	__u32 int_offset = do_div(page_index, VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN));

	page = vdfs_read_or_create_page(sbi->free_inode_bitmap.inode,
			page_index);
	if (IS_ERR_OR_NULL(page))
		return PTR_ERR(page);

	lock_page(page);
	data = kmap(page);
	for (; count; count--)
		if (!test_and_clear_bit(int_offset + count - 1,
			data + INODE_BITMAP_MAGIC_LEN)) {
			EMMCFS_DEBUG_INO("emmcfs_free_inode_n %llu"
				, inode_n);
			EMMCFS_BUG();
		}

	vdfs_add_chunk_bitmap(sbi, page, 1);
	kunmap(page);
	unlock_page(page);
	page_cache_release(page);
	return 0;
}

/**
 * @brief		Unlink function.
 * @param [in]	dir		Pointer to inode
 * @param [in]	dentry	Pointer to directory entry
 * @return		Returns error codes
 */
static int vdfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	int is_hard_link = is_vdfs_inode_flag_set(inode, HARD_LINK);
	int ret = 0;
	char ino_name[UUL_MAX_LEN + 1];
	int len = 0;

	if (check_permissions(sbi))
		return -EINTR;
	/* packtree removal only through install.vdfs -u packtree_root_dir */
	if (EMMCFS_I(dir)->record_type >= VDFS_CATALOG_PTREE_RECORD)
		return -EPERM;

	EMMCFS_DEBUG_INO("unlink '%s', ino = %lu", dentry->d_iname,
			inode->i_ino);

	vdfs_start_transaction(sbi);
	mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	drop_nlink(inode);
	EMMCFS_BUG_ON(inode->i_nlink > VDFS_LINK_MAX);

	if (inode->i_nlink) {
		/* must be hard link */
		if (!is_vdfs_inode_flag_set(inode, HARD_LINK)) {
			/* volume broken? */
			EMMCFS_DEBUG_INO("Incorect nlink count inode %lu",
					inode->i_ino);
			ret = -EINVAL;
			goto exit_inc_nlink;
		}
		inode->i_ctime = emmcfs_current_time(dir);
	} else {
		snprintf(ino_name, UUL_MAX_LEN + 1, "%lu", inode->i_ino);
		len = strlen(ino_name);
		if (!EMMCFS_I(inode)->name || len >
			strlen(EMMCFS_I(inode)->name)) {
			kfree(EMMCFS_I(inode)->name);
			EMMCFS_I(inode)->name = kzalloc(len + 1, GFP_KERNEL);
		}
		EMMCFS_I(inode)->parent_id = VDFS_ORPHAN_INODES_INO;
		memcpy(EMMCFS_I(inode)->name, ino_name, len + 1);
		clear_vdfs_inode_flag(inode, HARD_LINK);
		ret = vdfs_insert_cattree_object(sbi->catalog_tree, inode,
			VDFS_ORPHAN_INODES_INO);
		if (ret)
			goto exit_inc_nlink;
	}
	ret = vdfs_cattree_remove(sbi, dir->i_ino, dentry->d_name.name,
			dentry->d_name.len);
	if (ret)
		goto exit_kill_orphan_record;
	vdfs_remove_indirect_index(sbi->catalog_tree, inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	if (is_hard_link) {
		if (!inode->i_nlink)
			/* remove from hard link tree must be success always */
			BUG_ON(vdfs_hard_link_remove(sbi, inode->i_ino));
		else
			/* write inode to bnode must be success always here*/
			BUG_ON(emmcfs_write_inode_to_bnode(inode));
	}

	if (!inode->i_nlink) {
		remove_inode_hash(dentry->d_inode);
		if (is_vdfs_inode_flag_set(inode, TINY_FILE))
			atomic64_dec(&sbi->tiny_files_counter);
	}

	mutex_lock_nested(&EMMCFS_I(dir)->truncate_mutex, VDFS_REG_DIR_M);
	if (dir->i_size != 0)
		dir->i_size--;
	else
		EMMCFS_DEBUG_INO("Files count mismatch");

	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	ret = emmcfs_write_inode_to_bnode(dir);
	mutex_unlock(&EMMCFS_I(dir)->truncate_mutex);

	goto exit;

exit_kill_orphan_record:
	BUG_ON(vdfs_cattree_remove(sbi, VDFS_ORPHAN_INODES_INO,
			EMMCFS_I(inode)->name, len));
exit_inc_nlink:
	if (is_hard_link) {
		set_vdfs_inode_flag(inode, HARD_LINK);
		kfree(EMMCFS_I(inode)->name);
		EMMCFS_I(inode)->name = NULL;
		EMMCFS_I(inode)->parent_id = 0;
	}
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	inc_nlink(inode);
exit:
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
	vdfs_stop_transaction(sbi);
	return ret;
}

/**
 * @brief		Gets file's extent with iblock less and closest
 *			to the given one
 * @param [in]	inode	Pointer to the file's inode
 * @param [in]	iblock	Requested iblock
 * @return		Returns extent, or err code on failure
 */
int get_iblock_extent(struct inode *inode, sector_t iblock,
		struct vdfs_extent_info *result, sector_t *hint_block)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct vdfs_fork_info *fork = &inode_info->fork;
	int ret = 0, pos;
	sector_t last_iblock;

	if (!fork->total_block_count)
		return 0;


	for (pos = 0; pos < fork->used_extents; pos++) {
		last_iblock = fork->extents[pos].iblock +
				fork->extents[pos].block_count - 1;
		if ((iblock >= fork->extents[pos].iblock) &&
				(iblock <= last_iblock)) {
			/* extent is found */
			memcpy(result, &fork->extents[pos], sizeof(*result));
			goto exit;
		}
	}
	/* required extent is not found
	 * if no extent(s) for the inode in extents overflow tree
	 * the last used extent in fork can be used for allocataion
	 * hint calculation */
	if (fork->used_extents < VDFS_EXTENTS_COUNT_IN_FORK) {
		memcpy(result, &fork->extents[pos - 1],
			sizeof(*result));
		goto not_found;
	}

	/* extent is't found in fork */
	/* now we must to look up for extent in extents overflow B-tree */
	ret = vdfs_exttree_get_extent(sbi, inode->i_ino, iblock, result);

	if (ret && ret != -ENOENT)
		return ret;

	if (result->first_block == 0) {
		/* no extents in extents overflow tree */
		memcpy(result, &fork->extents[VDFS_EXTENTS_COUNT_IN_FORK - 1],
				sizeof(*result));
		goto not_found;
	}

	last_iblock = result->iblock + result->block_count - 1;
	/*check : it is a required extent or not*/
	if ((iblock >= result->iblock) && (iblock <= last_iblock))
		goto exit;

not_found:
	if (iblock == result->iblock + result->block_count)
		*hint_block = result->first_block + result->block_count;
	else
		*hint_block = 0;

	result->first_block = 0;
exit:
	return 0;

}

/**
 * @brief			Add allocated space into the fork or the exttree
 * @param [in]	inode_info	Pointer to inode_info structure.
 * @param [in]	iblock		First logical block number of allocated space.
 * @param [in]	block		First physical block number of allocated space.
 * @param [in]	blk_cnt		Allocated space size in blocks.
 * @param [in]	update_bnode	It's flag which control the update of the
				bnode. 1 - update bnode rec, 0 - update only
				inode structs.
 * @return			Returns physical block number, or err_code
 */
static int insert_extent(struct emmcfs_inode_info *inode_info,
		struct vdfs_extent_info *extent, int update_bnode)
{

	struct inode *inode = &inode_info->vfs_inode;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct vdfs_fork_info *fork = &inode_info->fork;
	int pos = 0, ret = 0, count;

	/* try to expand extent in vdfs_inode_info fork by new extent*/
	sector_t last_iblock, last_extent_block;

	if (fork->used_extents == 0) {
		fork->used_extents++;
		memcpy(&fork->extents[pos], extent, sizeof(*extent));
		goto update_on_disk_layout;
	}

	if (extent->iblock < fork->extents[0].iblock)
		goto insert_in_fork;

	/* find a place for insertion */
	for (pos = 0; pos < fork->used_extents; pos++) {
		if (extent->iblock < fork->extents[pos].iblock)
			break;
	}
	/* we need previous extent */
	pos--;

	/* try to extend extent in fork */
	last_iblock = fork->extents[pos].iblock +
				fork->extents[pos].block_count - 1;

	last_extent_block = fork->extents[pos].first_block +
			fork->extents[pos].block_count - 1;

	if ((last_iblock + 1 == extent->iblock) &&
		(last_extent_block + 1 == extent->first_block)) {
		/* expand extent in fork */
		fork->extents[pos].block_count += extent->block_count;
		/* FIXME check overwrite next extent */
		goto update_on_disk_layout;
	}

	/* we can not expand last extent in fork */
	/* now we have a following options:
	 * 1. insert in fork
	 * 2. insert into extents overflow btree
	 * 3a. shift extents if fork to right, push out rightest extent
	 * 3b. shift extents in fork to right and insert in fork
	 * into extents overflow btee
	 * */
	pos++;
insert_in_fork:
	if (pos < VDFS_EXTENTS_COUNT_IN_FORK &&
			fork->extents[pos].first_block == 0) {
		/* 1. insert in fork */
		memcpy(&fork->extents[pos], extent, sizeof(*extent));
		fork->used_extents++;
	} else if (pos == VDFS_EXTENTS_COUNT_IN_FORK) {
		/* 2. insert into extents overflow btree */
		ret = emmcfs_extree_insert_extent(sbi, inode->i_ino,
						extent);
		if (ret)
			goto exit;

		goto update_on_disk_layout;
	} else {
		if (fork->used_extents == VDFS_EXTENTS_COUNT_IN_FORK) {
			/* 3a push out rightest extent into extents
			 * overflow btee */
			ret = emmcfs_extree_insert_extent(sbi, inode->i_ino,
				&fork->extents[VDFS_EXTENTS_COUNT_IN_FORK - 1]);
			if (ret)
				goto exit;
		} else
			fork->used_extents++;

		/*  3b. shift extents in fork to right  */
		for (count = fork->used_extents - 1; count > pos; count--)
			memcpy(&fork->extents[count], &fork->extents[count - 1],
						sizeof(*extent));
		memcpy(&fork->extents[pos], extent, sizeof(*extent));
	}

update_on_disk_layout:
	fork->total_block_count += extent->block_count;
	inode_add_bytes(inode, sbi->sb->s_blocksize * extent->block_count);
	if (update_bnode) {
		ret = emmcfs_write_inode_to_bnode(inode);
		EMMCFS_BUG_ON(ret);
	}

exit:
	return ret;
}

/**
 * @brief				Logical to physical block number
 *					translation for already allocated
 *					blocks.
 * @param [in]		inode_info	Pointer to inode_info structure.
 * @param [in]		iblock		Requested logical block number
 * @param [in, out]	max_blocks	Distance (in blocks) from returned
 *					block to end of extent is returned
 *					through this pointer.
 * @return				Returns physical block number.
 */
sector_t emmcfs_find_old_block(struct emmcfs_inode_info *inode_info,
	sector_t iblock, __u32 *max_blocks)
{
	struct vdfs_fork_info *fork = &inode_info->fork;
	unsigned int i;
	sector_t iblock_start = iblock;

	for (i = 0; i < fork->used_extents; i++)
		if (iblock < fork->extents[i].block_count) {
			if (max_blocks)
				*max_blocks = fork->extents[i].block_count -
					iblock;
			return fork->extents[i].first_block + iblock;
		} else
			iblock -= fork->extents[i].block_count;
	return vdfs_exttree_get_block(inode_info, iblock_start, max_blocks);
}


void vdfs_free_reserved_space(struct inode *inode, sector_t iblocks_count)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	/* if block count is valid and fsm exist, free the space.
	 * fsm may not exist in case of umount */
	if (iblocks_count && sbi->fsm_info) {
		mutex_lock(&sbi->fsm_info->lock);
		percpu_counter_add(&sbi->free_blocks_count, iblocks_count);
#ifdef CONFIG_VDFS_QUOTA
		if (EMMCFS_I(inode)->quota_index != -1) {
			sbi->quotas[EMMCFS_I(inode)->quota_index].has -=
				(iblocks_count << sbi->block_size_shift);
			update_has_quota(sbi,
				sbi->quotas[EMMCFS_I(inode)->quota_index].ino,
				EMMCFS_I(inode)->quota_index);
		}
#endif
		mutex_unlock(&sbi->fsm_info->lock);
	}
	return;
}

static int vdfs_reserve_space(struct vdfs_sb_info *sbi)
{

	int ret = 0;
	mutex_lock(&sbi->fsm_info->lock);
	if (percpu_counter_sum(&sbi->free_blocks_count))
		percpu_counter_dec(&sbi->free_blocks_count);
	else
		ret = -ENOSPC;
	mutex_unlock(&sbi->fsm_info->lock);
	return ret;
}
/*
 * */
int vdfs_get_block_prep_da(struct inode *inode, sector_t iblock,
		struct buffer_head *bh_result, int create) {
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct super_block *sb = inode->i_sb;
	sector_t offset_alloc_hint = 0, res_block;
	int err = 0;
	__u32 max_blocks = 1;
	__u32 buffer_size = bh_result->b_size >> sbi->block_size_shift;

	struct vdfs_extent_info extent;
	if (!create)
		BUG();
	memset(&extent, 0x0, sizeof(extent));
	mutex_lock_nested(&inode_info->truncate_mutex, SMALL_AREA);

	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
		is_vdfs_inode_flag_set(inode, SMALL_FILE))
		goto exit;

	/* get extent contains iblock*/
	err = get_iblock_extent(&inode_info->vfs_inode, iblock, &extent,
			&offset_alloc_hint);

	if (err)
		goto exit;

	if (extent.first_block)
		goto done;

	if (buffer_delay(bh_result))
		goto exit;

	err = vdfs_reserve_space(VDFS_SB(inode->i_sb));
	if (err)
		/* not enough space to reserve */
		goto exit;
#ifdef CONFIG_VDFS_QUOTA
	if (inode_info->quota_index != -1) {
		sbi->quotas[inode_info->quota_index].has +=
				(1 << sbi->block_size_shift);
		if (sbi->quotas[inode_info->quota_index].has >
				sbi->quotas[inode_info->quota_index].max) {
			err = -ENOSPC;
			vdfs_free_reserved_space(inode, 1);
			goto exit;
		}

		update_has_quota(sbi,
			sbi->quotas[inode_info->quota_index].ino,
			inode_info->quota_index);
	}
#endif
	map_bh(bh_result, inode->i_sb, VDFS_INVALID_BLOCK);
	set_buffer_new(bh_result);
	set_buffer_delay(bh_result);
	err = vdfs_runtime_extent_add(iblock, offset_alloc_hint,
			&inode_info->runtime_extents);
	if (err)
		vdfs_free_reserved_space(inode, 1);
	goto exit;
done:
	res_block = extent.first_block + (iblock - extent.iblock);
	max_blocks = extent.block_count - (iblock - extent.iblock);
	BUG_ON(res_block > extent.first_block + extent.block_count);

	if (res_block >
		(sb->s_bdev->bd_inode->i_size >> sbi->block_size_shift)) {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			EMMCFS_ERR("Block beyond block bound requested");
			err = -EFAULT;
			goto exit;
		} else {
			BUG();
		}
	}
	mutex_unlock(&inode_info->truncate_mutex);
	clear_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, res_block);
	bh_result->b_size = sb->s_blocksize * min(max_blocks, buffer_size);


	return 0;
exit:
	mutex_unlock(&inode_info->truncate_mutex);
	return err;
}
/**
 * @brief				Logical to physical block numbers
 *					translation.
 * @param [in]		inode		Pointer to inode structure.
 * @param [in]		iblock		Requested logical block number.
 * @param [in, out]	bh_result	Pointer to buffer_head.
 * @param [in]		create		"Expand file allowed" flag.
 * @param [in]		da		delay allocation flag, if 1, the buffer
 *					already reserved space.
 * @return				0 on success, or error code
 */
int vdfs_get_int_block(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create, int da)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct super_block *sb = inode->i_sb;
	sector_t offset_alloc_hint = 0, res_block;
	int alloc = 0;
	__u32 max_blocks = 1;
	int count = 1;
	__u32 buffer_size = bh_result->b_size >> sbi->block_size_shift;
	int err = 0;
	struct vdfs_extent_info extent;

	memset(&extent, 0x0, sizeof(extent));
	mutex_lock_nested(&inode_info->truncate_mutex, SMALL_AREA);

	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
				is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
		if (create)
			BUG();
		else
			goto exit_no_error_quota;
	}

	/* get extent contains iblock*/
	err = get_iblock_extent(&inode_info->vfs_inode, iblock, &extent,
			&offset_alloc_hint);

	if (err)
		goto exit_no_error_quota;

	if (extent.first_block)
		goto done;

	if (!create)
		goto exit_no_error_quota;

	if (da) {
		if(!vdfs_runtime_extent_exists(iblock,
				&inode_info->runtime_extents))
			BUG();
	} else {
		if (buffer_delay(bh_result))
			BUG();
	}
#ifdef CONFIG_VDFS_QUOTA
	if (!da && inode_info->quota_index != -1) {
		if (sbi->quotas[inode_info->quota_index].has +
				(count << sbi->block_size_shift) >
				sbi->quotas[inode_info->quota_index].max) {
			err = -ENOSPC;
			goto exit_no_error_quota;
		} else {
			sbi->quotas[inode_info->quota_index].has +=
					(count << sbi->block_size_shift);
			update_has_quota(sbi,
				sbi->quotas[inode_info->quota_index].ino,
				inode_info->quota_index);
		}
	}
#endif
	extent.block_count = count;
	extent.first_block = emmcfs_fsm_get_free_block(sbi, offset_alloc_hint,
			&extent.block_count, 0, 1, da);

	if (!extent.first_block) {
		err = -ENOSPC;
		goto exit;
	}

	extent.iblock = iblock;
	if (da)
		err = vdfs_runtime_extent_del(extent.iblock,
			&inode_info->runtime_extents);
	if (!err)
		err = insert_extent(inode_info, &extent, 1);
	if (err) {
		emmcfs_fsm_put_free_block(inode_info, extent.first_block,
			extent.block_count, da);
		goto exit;
	}

	alloc = 1;

done:
	res_block = extent.first_block + (iblock - extent.iblock);
	max_blocks = extent.block_count - (iblock - extent.iblock);
	BUG_ON(res_block > extent.first_block + extent.block_count);

	if (res_block >
		(sb->s_bdev->bd_inode->i_size >> sbi->block_size_shift)) {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			EMMCFS_ERR("Block beyond block bound requested");
			err = -EFAULT;
			goto exit;
		} else {
			BUG();
		}
	}

	mutex_unlock(&inode_info->truncate_mutex);
	clear_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, res_block);
	bh_result->b_size = sb->s_blocksize * min(max_blocks, buffer_size);

	if (alloc)
		set_buffer_new(bh_result);
	return 0;
exit:
#ifdef CONFIG_VDFS_QUOTA
	if (!da && inode_info->quota_index != -1) {
		sbi->quotas[inode_info->quota_index].has -=
				(count << sbi->block_size_shift);
		update_has_quota(sbi, sbi->quotas[inode_info->quota_index].ino,
				inode_info->quota_index);
	}
#endif
exit_no_error_quota:
	mutex_unlock(&inode_info->truncate_mutex);
	return err;

}

/**
 * @brief				Logical to physical block numbers
 *					translation.
 * @param [in]		inode		Pointer to inode structure.
 * @param [in]		iblock		Requested logical block number.
 * @param [in, out]	bh_result	Pointer to buffer_head.
 * @param [in]		create		"Expand file allowed" flag.
 * @return			Returns physical block number,
 *					0 if ENOSPC
 */
int vdfs_get_block(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create)
{
	return vdfs_get_int_block(inode, iblock, bh_result, create, 0);
}

int vdfs_get_block_da(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create)
{
	return vdfs_get_int_block(inode, iblock, bh_result, create, 1);
}

int vdfs_get_block_bug(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create)
{
	BUG();
	return 0;
}

static int vdfs_releasepage(struct page *page, gfp_t gfp_mask)
{
	if (!page_has_buffers(page))
		return 0;

	if (buffer_delay(page_buffers(page)))
		return 0;

	return try_to_free_buffers(page);
}

/**
 * @brief		Read page function.
 * @param [in]	file	Pointer to file structure
 * @param [out]	page	Pointer to page structure
 * @return		Returns error codes
 */
static int vdfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	int ret;
	/* check TINY and SMALL flag twice, first time without lock,
	and second time with lock, this is made for perfomance reason */
	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
		is_vdfs_inode_flag_set(inode, SMALL_FILE))
		if (page->index == 0) {
			mutex_lock(&inode_info->truncate_mutex);
			if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
			is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
				ret = read_tiny_small_page(page);
				mutex_unlock(&inode_info->truncate_mutex);
			} else {
				mutex_unlock(&inode_info->truncate_mutex);
				goto exit;
			}
			/* if there is error, print DEBUG INFO */
#if defined(CONFIG_VDFS_DEBUG)
			if (ret)
				EMMCFS_ERR("err = %d, ino#%lu name=%s,"
					"page index: %lu", ret, inode->i_ino,
					inode_info->name, page->index);
#endif
			return ret;
		}
exit:
	ret = mpage_readpage(page, vdfs_get_block);
	/* if there is error, print DEBUG iNFO */
#if defined(CONFIG_VDFS_DEBUG)
	if (ret)
		EMMCFS_ERR("err = %d, ino#%lu name=%s, page index: %lu",
			ret, inode->i_ino, inode_info->name, page->index);
#endif
	return ret;
}


/**
 * @brief		Read page function.
 * @param [in]	file	Pointer to file structure
 * @param [out]	page	Pointer to page structure
 * @return		Returns error codes
 */
static int vdfs_readpage_special(struct file *file, struct page *page)
{
	BUG();
}

/**
 * @brief			Read multiple pages function.
 * @param [in]	file		Pointer to file structure
 * @param [in]	mapping		Address of pages mapping
 * @param [out]	pages		Pointer to list with pages
 * param [in]	nr_pages	Number of pages
 * @return			Returns error codes
 */
static int vdfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = mapping->host;
	int err = 0;
	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
		is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
		err = vdfs_readpages_tinysmall(file, mapping, pages,
			nr_pages);
		goto exit;
	}

	err = mpage_readpages(mapping, pages, nr_pages, vdfs_get_block);
exit:
	/* if there is error, print DEBUG iNFO */
#if defined(CONFIG_VDFS_DEBUG)
	if (err) {
		struct page *page = list_entry(pages->prev, struct page, lru);
		EMMCFS_ERR("err = %d, ino#%lu name=%s, page index: %d", err,
			inode->i_ino, EMMCFS_I(inode)->name, nr_pages ?
			(int)page->index : -1);
	}
#endif
	return err;
}

static int vdfs_readpages_special(struct file *file,
		struct address_space *mapping, struct list_head *pages,
		unsigned nr_pages)
{
	BUG();
}


/**
 * @brief		Update all metadata.
 * @param [in]	sbi		Pointer to superblock information
 * @return		Returns error codes
 */
int vdfs_update_metadata(struct vdfs_sb_info *sbi)
{
	int error;

	sbi->snapshot_info->flags = 1;
	error = vdfs_sync_metadata(sbi);
	sbi->snapshot_info->flags = 0;

	return error;
}


/**
 * @brief		Write pages.
 * @param [in]	page	List of pages
 * @param [in]	wbc		Write back control array
 * @return		Returns error codes
 */
static int vdfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *bh;
	int ret = 0;

	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
			is_vdfs_inode_flag_set(inode, SMALL_FILE))
		ret = write_tiny_small_page(page, wbc);
	if (ret)
		goto exit;

	if (page->mapping->host->i_ino > VDFS_LSFILE)
		goto write;

	set_page_dirty(page);
	return AOP_WRITEPAGE_ACTIVATE;

write:
	if (!page_has_buffers(page))
		goto redirty_page;

	bh = page_buffers(page);
	if ((!buffer_mapped(bh) || buffer_delay(bh)) && buffer_dirty(bh))
		goto redirty_page;

	ret = block_write_full_page(page, vdfs_get_block_bug, wbc);
exit:
	if (ret)
		EMMCFS_ERR("err = %d, ino#%lu name=%s, page index: %lu, "
				" wbc->sync_mode = %d", ret, inode->i_ino,
				EMMCFS_I(inode)->name, page->index,
				wbc->sync_mode);
	return ret;
redirty_page:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
}

int vdfs_allocate_space(struct emmcfs_inode_info *inode_info)
{
	struct list_head *ptr;
	struct list_head *next;
	struct vdfs_runtime_extent_info *entry;
	struct inode *inode = &inode_info->vfs_inode;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	u32 count = 0;
	struct vdfs_extent_info extent;
	int err = 0;

	if (list_empty(&inode_info->runtime_extents))
		return 0;
	memset(&extent, 0x0, sizeof(extent));
	list_for_each_safe(ptr, next, &inode_info->runtime_extents) {
		entry = list_entry(ptr, struct vdfs_runtime_extent_info, list);
again:
		count = entry->block_count;

		extent.first_block = emmcfs_fsm_get_free_block(sbi, entry->
				alloc_hint, &count, 0, 1, 1);

		if (!extent.first_block) {
			/* it shouldn't happen because space
			 * was reserved early in aio_write */
			BUG();
			goto exit;
		}

		extent.iblock = entry->iblock;
		extent.block_count = count;
		err = insert_extent(inode_info, &extent, 0);
		if (err) {
			emmcfs_fsm_put_free_block(inode_info,
				extent.first_block, extent.block_count, 1);
			goto exit;
		}
		entry->iblock += count;
		entry->block_count -= count;
		/* if we still have blocks in the chunk */
		if (entry->block_count)
			goto again;
		else {
			list_del(&entry->list);
			kfree(entry);
		}
	}
exit:
	if (!err) {
#ifdef CONFIG_VDFS_QUOTA
		if (inode_info->quota_index != -1)
			update_has_quota(sbi,
				sbi->quotas[inode_info->quota_index].ino,
				inode_info->quota_index);
#endif
		err = emmcfs_write_inode_to_bnode(inode);
	}
	return err;
}

int vdfs_mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	int ret;
	struct blk_plug plug;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_mpage_data mpd = {
			.bio = NULL,
			.last_block_in_bio = 0,
	};
	vdfs_start_transaction(sbi);
	/* dont allocate space for packtree inodes */
	if ((inode_info->record_type == VDFS_CATALOG_FILE_RECORD) ||
		(inode_info->record_type == VDFS_CATALOG_HLINK_RECORD)) {
		/* if we have runtime extents, allocate space on volume*/
		if (!list_empty(&inode_info->runtime_extents)) {
			mutex_lock(&inode_info->truncate_mutex);
			ret = vdfs_allocate_space(inode_info);
			mutex_unlock(&inode_info->truncate_mutex);
		}
	}
	blk_start_plug(&plug);
	/* write dirty pages */
	ret = write_cache_pages(mapping, wbc, vdfs_mpage_writepage, &mpd);
	if (mpd.bio)
		vdfs_mpage_bio_submit(WRITE, mpd.bio);
	blk_finish_plug(&plug);
	vdfs_stop_transaction(sbi);
	return ret;
}
/**
 * @brief		Write some dirty pages.
 * @param [in]	mapping	Address space mapping (holds pages)
 * @param [in]	wbc		Writeback control - how many pages to write
 *			and write mode
 * @return		Returns 0 on success, errno on failure
 */

static int vdfs_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	int err = 0;
	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
		is_vdfs_inode_flag_set(inode, SMALL_FILE))
		err = writepage_tiny_small(mapping, wbc);
	else
		err = vdfs_mpage_writepages(mapping, wbc);
	/* if there is error, print DEBUG iNFO */
#if defined(CONFIG_VDFS_DEBUG)
	if (err)
		EMMCFS_ERR("err = %d, ino#%lu name=%s, "
				"wbc->sync_mode = %d", err, inode->i_ino,
				EMMCFS_I(inode)->name, wbc->sync_mode);
#endif
	return err;
}

/**
 * @brief		Write some dirty pages.
 * @param [in]	mapping	Address space mapping (holds pages)
 * @param [in]	wbc		Writeback control - how many pages to write
 *			and write mode
 * @return		Returns 0 on success, errno on failure
 */
static int vdfs_writepages_special(struct address_space *mapping,
		struct writeback_control *wbc)
{
	return 0;
}
/**
 * @brief		Write begin with snapshots.
 * @param [in]	file	Pointer to file structure
 * @param [in]	mapping Address of pages mapping
 * @param [in]	pos		Position
 * @param [in]	len		Length
 * @param [in]	flags	Flags
 * @param [in]	pagep	Pages array
 * @param [in]	fs_data	Data array
 * @return		Returns error codes
 */
static int emmcfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int rc = 0;
	vdfs_start_transaction(VDFS_SB(mapping->host->i_sb));
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	*pagep = NULL;
	rc = block_write_begin(file, mapping, pos, len, flags, pagep,
		NULL, vdfs_get_block_prep_da);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) ||\
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	rc = block_write_begin(mapping, pos, len, flags, pagep,
		vdfs_get_block_prep_da);
#else
	BUILD_BUG();
#endif
	if (rc)
		vdfs_stop_transaction(VDFS_SB(mapping->host->i_sb));
	return rc;
}

/**
 * @brief		TODO Write begin with snapshots.
 * @param [in]	file	Pointer to file structure
 * @param [in]	mapping	Address of pages mapping
 * @param [in]	pos		Position
 * @param [in]	len		Length
 * @param [in]	copied	Whould it be copied
 * @param [in]	page	Page pointer
 * @param [in]	fs_data	Data
 * @return		Returns error codes
 */
static int emmcfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	int i_size_changed = 0;

	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);
	mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold i_mutex.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page write out could otherwise come in and zero beyond i_size.
	 */
	if (pos+copied > inode->i_size) {
		i_size_write(inode, pos+copied);
		i_size_changed = 1;
	}

	unlock_page(page);
	page_cache_release(page);

	if (i_size_changed)
		emmcfs_write_inode_to_bnode(inode);
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
	vdfs_stop_transaction(VDFS_SB(inode->i_sb));
	return copied;
}

/**
 * @brief		Called during file opening process.
 * @param [in]	inode	Pointer to inode information
 * @param [in]	file	Pointer to file structure
 * @return		Returns error codes
 */
static int vdfs_file_open(struct inode *inode, struct file *filp)
{
	int rc = generic_file_open(inode, filp);
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	if (rc)
		return rc;
	atomic_inc(&(inode_info->open_count));
	return 0;
}

/**
 * @brief		Release file.
 * @param [in]	inode	Pointer to inode information
 * @param [in]	file	Pointer to file structure
 * @return		Returns error codes
 */
static int vdfs_file_release(struct inode *inode, struct file *file)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	EMMCFS_DEBUG_INO("#%lu", inode->i_ino);
	atomic_dec(&(inode_info->open_count));
	return 0;
}

/**
 * @brief		Function mkdir.
 * @param [in]	dir	Pointer to inode
 * @param [in]	dentry	Pointer to directory entry
 * @param [in]	mode	Mode of operation
 * @return		Returns error codes
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static int emmcfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
#else
static int emmcfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	return emmcfs_create(dir, dentry, S_IFDIR | mode, NULL);
}

/**
 * @brief		Function rmdir.
 * @param [in]	dir	Pointer to inode
 * @param [in]	dentry	Pointer to directory entry
 * @return		Returns error codes
 */
static int emmcfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (dentry->d_inode->i_size)
		return -ENOTEMPTY;

	return vdfs_unlink(dir, dentry);
}

/**
 * @brief		Direct IO.
 * @param [in]	rw		read/write
 * @param [in]	iocb	Pointer to io block
 * @param [in]	iov		Pointer to IO vector
 * @param [in]	offset	Offset
 * @param [in]	nr_segs	Number of segments
 * @return		Returns written size
 */
static __attribute__ ((unused)) ssize_t emmcfs_direct_IO(int rw,
		struct kiocb *iocb, const struct iovec *iov,
		loff_t offset, unsigned long nr_segs)
{
	ssize_t rc, inode_new_size = 0;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode->i_mapping->host;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);

	if (rw)
		vdfs_start_transaction(sbi);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	rc = blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs,
			vdfs_get_block);
#else
	rc = blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
		offset, nr_segs, vdfs_get_block, NULL);
#endif

	if (!rw)
		return rc;

	mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
	EMMCFS_DEBUG_MUTEX("truncate mutex lock success");

	if (!IS_ERR_VALUE(rc)) { /* blockdev_direct_IO successfully finished */
		if ((offset + rc) > i_size_read(inode))
			/* last accessed byte behind old inode size */
			inode_new_size = offset + rc;
	} else if (EMMCFS_I(inode)->fork.total_block_count >
			inode_size_to_blocks(inode))
		/* blockdev_direct_IO finished with error, but some free space
		 * allocations for inode may have occured, inode internal fork
		 * changed, but inode i_size stay unchanged. */
		inode_new_size = EMMCFS_I(inode)->fork.total_block_count <<
			sbi->block_size_shift;

	if (inode_new_size) {
		i_size_write(inode, inode_new_size);
		emmcfs_write_inode_to_bnode(inode);
	}

	EMMCFS_DEBUG_MUTEX("truncate mutex unlock");
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);

	vdfs_stop_transaction(VDFS_SB(inode->i_sb));
	return rc;
}

static int vdfs_truncate_pages(struct inode *inode, loff_t newsize)
{
	int error = 0;

	error = inode_newsize_ok(inode, newsize);
	if (error)
		goto exit;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			S_ISLNK(inode->i_mode))) {
		error = -EINVAL;
		goto exit;
	}

	if (IS_APPEND(inode) || IS_IMMUTABLE(inode)) {
		error = -EPERM;
		goto exit;
	}

	error = block_truncate_page(inode->i_mapping, newsize,
			vdfs_get_block);
exit:
	return error;
}


static int vdfs_update_inode(struct inode *inode, loff_t newsize)
{
	int error = 0;
	loff_t oldsize = inode->i_size;

	if (newsize < oldsize) {
		if (is_vdfs_inode_flag_set(inode, TINY_FILE)) {
			EMMCFS_I(inode)->tiny.len = newsize;
			EMMCFS_I(inode)->tiny.i_size = newsize;
		} else if (is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
			EMMCFS_I(inode)->small.len = newsize;
			EMMCFS_I(inode)->small.i_size = newsize;
		} else
			error = vdfs_truncate_blocks(inode, newsize);
	} else {
		if (is_vdfs_inode_flag_set(inode, TINY_FILE))
			EMMCFS_I(inode)->tiny.i_size = newsize;
		else if (is_vdfs_inode_flag_set(inode, SMALL_FILE))
			EMMCFS_I(inode)->small.i_size = newsize;
	}

	if (!error)
		i_size_write(inode, newsize);
	else
		return error;


	inode->i_mtime = inode->i_ctime =
			emmcfs_current_time(inode);

	return error;
}

/**
 * @brief		Set attributes.
 * @param [in]	dentry	Pointer to directory entry
 * @param [in]	iattr	Attributes to be set
 * @return		Returns error codes
 */
static int emmcfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error = 0;
#ifdef CONFIG_VDFS_QUOTA
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
#endif

	vdfs_start_transaction(VDFS_SB(inode->i_sb));
	error = inode_change_ok(inode, iattr);
	if (error)
		goto exit;
#ifdef CONFIG_VDFS_QUOTA
	if (is_vdfs_inode_flag_set(inode, SMALL_FILE) &&
			EMMCFS_I(inode)->quota_index != -1 &&
			sbi->quotas[EMMCFS_I(inode)->quota_index].has +
			sbi->small_area->cell_size >
			sbi->quotas[EMMCFS_I(inode)->quota_index].max) {

		error = -ENOSPC;
		goto exit;
	}
#endif
	if ((iattr->ia_valid & ATTR_SIZE) &&
			iattr->ia_size != i_size_read(inode)) {
		error = vdfs_truncate_pages(inode, iattr->ia_size);
		if (error)
			goto exit;

		truncate_pagecache(inode, inode->i_size, iattr->ia_size);

		mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
		error = vdfs_update_inode(inode, iattr->ia_size);
		if (error)
			goto exit_unlock;
	} else
		mutex_lock(&EMMCFS_I(inode)->truncate_mutex);

	setattr_copy(inode, iattr);

	error = emmcfs_write_inode_to_bnode(inode);

exit_unlock:
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
exit:
	vdfs_stop_transaction(VDFS_SB(inode->i_sb));

	return error;
}

/**
 * @brief		Make bmap.
 * @param [in]	mapping	Address of pages mapping
 * @param [in]	block	Block number
 * @return		TODO Returns 0 on success, errno on failure
 */
static sector_t emmcfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, vdfs_get_block);
}

/**
 * @brief			Function rename.
 * @param [in]	old_dir		Pointer to old dir struct
 * @param [in]	old_dentry	Pointer to old dir entry struct
 * @param [in]	new_dir		Pointer to new dir struct
 * @param [in]	new_dentry	Pointer to new dir entry struct
 * @return			Returns error codes
 */
static int emmcfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	struct emmcfs_find_data fd;
	struct vdfs_sb_info *sbi = old_dir->i_sb->s_fs_info;
	struct inode *mv_inode = old_dentry->d_inode;
	int ret = 0;
	struct emmcfs_cattree_key *rm_key = NULL, *key = NULL;
	if (check_permissions(sbi))
		return -EINTR;
	if (new_dentry->d_name.len > EMMCFS_CAT_MAX_NAME)
		return -ENAMETOOLONG;
	if (new_dentry->d_inode) {
		struct inode *new_dentry_inode = new_dentry->d_inode;
		if (S_ISDIR(new_dentry_inode->i_mode) &&
				new_dentry_inode->i_size > 0)
			return -ENOTEMPTY;

		ret = vdfs_unlink(new_dir, new_dentry);
		if (ret)
			return ret;
	}

	vdfs_start_transaction(sbi);
	/* Find old record */
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock");
	mutex_lock(&EMMCFS_I(mv_inode)->truncate_mutex);
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock succ");

	fd.bnode = NULL;
	ret = emmcfs_cattree_find_old(sbi->catalog_tree, old_dir->i_ino,
			(char *) old_dentry->d_name.name,
			old_dentry->d_name.len, &fd, EMMCFS_BNODE_MODE_RW);
	if (ret) {
		EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);
		goto exit;
	}

	rm_key = emmcfs_get_btree_record(fd.bnode, fd.pos);
	if (IS_ERR(rm_key)) {
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);
		ret = PTR_ERR(rm_key);
		goto exit;
	}

	/* found_key is a part of bnode, wich we are going to modify,
	 * so we have to copy save its current value */
	key = kzalloc(le32_to_cpu(rm_key->gen_key.key_len), GFP_KERNEL);
	if (!key) {
		emmcfs_put_bnode(fd.bnode);
		EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);
		ret = -ENOMEM;
		goto exit;
	}
	memcpy(key, rm_key, le32_to_cpu(rm_key->gen_key.key_len));
	emmcfs_put_bnode(fd.bnode);


	ret = emmcfs_add_new_cattree_object(mv_inode, new_dir->i_ino,
			&new_dentry->d_name);
	if (ret) {
		/* TODO change to some erroneous action */
		EMMCFS_DEBUG_INO("can not rename #%d; old_dir_ino=%lu "
				"oldname=%s new_dir_ino=%lu newname=%s",
				ret,
				old_dir->i_ino, old_dentry->d_name.name,
				new_dir->i_ino, new_dentry->d_name.name);
		kfree(key);
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);
		goto exit;
	}

	ret = emmcfs_btree_remove(sbi->catalog_tree, &key->gen_key);
	kfree(key);
	if (ret)
		/* TODO change to some erroneous action */
		EMMCFS_BUG();
	if (!(is_vdfs_inode_flag_set(mv_inode, HARD_LINK))) {
		char *saved_name;
		EMMCFS_I(mv_inode)->parent_id = new_dir->i_ino;
		kfree(EMMCFS_I(mv_inode)->name);
		saved_name = kzalloc(new_dentry->d_name.len + 1, GFP_KERNEL);
		if (!saved_name) {
			iput(mv_inode);
			ret = -ENOMEM;
			mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
			mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);
			goto exit;
		}

		strncpy(saved_name, new_dentry->d_name.name,
				new_dentry->d_name.len + 1);
		EMMCFS_I(mv_inode)->name = saved_name;
	}



	EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	mutex_unlock(&EMMCFS_I(mv_inode)->truncate_mutex);

	mutex_lock(&EMMCFS_I(old_dir)->truncate_mutex);
	if (old_dir->i_size != 0)
		old_dir->i_size--;
	else
		EMMCFS_DEBUG_INO("Files count mismatch");

	ret = emmcfs_write_inode_to_bnode(old_dir);
	mutex_unlock(&EMMCFS_I(old_dir)->truncate_mutex);
	if (ret)
		goto exit;

	mutex_lock(&EMMCFS_I(new_dir)->truncate_mutex);
	new_dir->i_size++;
	ret = emmcfs_write_inode_to_bnode(new_dir);
	mutex_unlock(&EMMCFS_I(new_dir)->truncate_mutex);
	if (ret)
		goto exit;

	mv_inode->i_ctime = emmcfs_current_time(mv_inode);
exit:
	vdfs_stop_transaction(sbi);
	return ret;
}

/**
 * @brief			Create hardlink record .
 * @param [in]	hlink_id	Hardlink id
 * @param [in]	par_ino_n	Parent inode number
 * @param [in]	name		Name
 * @return			Returns pointer to buffer with hardlink
 */
static void *form_hlink_record(ino_t ino_n, ino_t par_ino_n, umode_t file_mode,
		struct qstr *name)
{
	void *record = emmcfs_alloc_cattree_key(name->len,
			VDFS_CATALOG_HLINK_RECORD);
	struct vdfs_catalog_hlink_record *hlink_val;

	if (!record)
		return ERR_PTR(-ENOMEM);

	emmcfs_fill_cattree_key(record, par_ino_n, name->name, name->len);
	hlink_val = get_value_pointer(record);
	hlink_val->object_id = cpu_to_le64(ino_n);
	hlink_val->file_mode = cpu_to_le16(file_mode);

	return record;
}

/**
 * @brief			Add hardlink record .
 * @param [in]	cat_tree	Pointer to catalog tree
 * @param [in]	hlink_id	Hardlink id
 * @param [in]	par_ino_n	Parent inode number
 * @param [in]	name		Name
 * @return			Returns error codes
 */
static int add_hlink_record(struct vdfs_btree *cat_tree, ino_t ino_n,
		ino_t par_ino_n, umode_t file_mode, struct qstr *name)
{
	struct vdfs_catalog_hlink_record *hlink_value;
	struct vdfs_cattree_record *record;

	record = vdfs_cattree_place_record(cat_tree, par_ino_n, name->name,
			name->len, VDFS_CATALOG_HLINK_RECORD);
	if (IS_ERR(record))
		return PTR_ERR(record);

	hlink_value = (struct vdfs_catalog_hlink_record *)record->val;
	hlink_value->file_mode = cpu_to_le16(file_mode);
	hlink_value->object_id = cpu_to_le64(ino_n);

	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);
	return 0;
}

/**
 * @brief       Transform record from regular file to hard link
 *              Resulting record length stays unchanged, but only a part of the
 *              record is used for real data
 * */

static int transform_record_into_hlink(struct emmcfs_cattree_key *record,
		ino_t ino, ino_t par_ino, umode_t file_mode, struct qstr *name)
{
	struct emmcfs_cattree_key *hlink_rec;
	size_t bytes_to_copy;

	hlink_rec = form_hlink_record(ino, par_ino, file_mode, name);

	if (IS_ERR(hlink_rec))
		return -ENOMEM;

	EMMCFS_BUG_ON(record->gen_key.record_len <=
			hlink_rec->gen_key.record_len);

	bytes_to_copy = le32_to_cpu(hlink_rec->gen_key.record_len);
	hlink_rec->gen_key.record_len = record->gen_key.record_len;

	memcpy(record, hlink_rec, le32_to_cpu(bytes_to_copy));

	kfree(hlink_rec);

	return 0;
}

/**
 * @brief			Create link.
 * @param [in]	old_dentry	Old dentry (source name for hard link)
 * @param [in]	dir		The inode dir pointer
 * @param [out]	dentry		Pointer to result dentry
 * @return			Returns error codes
 */
static int emmcfs_link(struct dentry *old_dentry, struct inode *dir,
	struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct inode *par_inode = old_dentry->d_parent->d_inode;

	struct vdfs_sb_info *sbi = old_dentry->d_inode->i_sb->s_fs_info;
	struct emmcfs_cattree_key *found_key;
	struct vdfs_cattree_record *record;
	int ret;
	if (check_permissions(sbi))
		return -EINTR;
	if (dentry->d_name.len > EMMCFS_CAT_MAX_NAME)
		return -ENAMETOOLONG;

	if (inode->i_nlink >= VDFS_LINK_MAX)
		return -EMLINK;

	vdfs_start_transaction(sbi);

	mutex_lock(&EMMCFS_I(inode)->truncate_mutex);

	EMMCFS_DEBUG_MUTEX("cattree mutex w lock");
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock succ");

	record = vdfs_cattree_find(sbi->catalog_tree, par_inode->i_ino,
			old_dentry->d_name.name, old_dentry->d_name.len,
			EMMCFS_BNODE_MODE_RW);

	if (IS_ERR(record)) {
		ret = PTR_ERR(record);
		goto err_exit;
	}


	if (record->key->record_type != VDFS_CATALOG_HLINK_RECORD) {
		/* move inode metadata into hardlink btree */
		ret = vdfs_hard_link_insert(sbi,
			(struct vdfs_catalog_file_record *)record->val);

		if (ret) {
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);
			goto err_exit;
		}
		found_key = record->key;

		ret = transform_record_into_hlink(found_key, inode->i_ino,
				par_inode->i_ino, par_inode->i_mode,
				&old_dentry->d_name);

		if (ret) {
			vdfs_hard_link_remove(sbi, par_inode->i_ino);
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);
			goto err_exit;
		}

		kfree(EMMCFS_I(inode)->name);
		EMMCFS_I(inode)->name = NULL;
		set_vdfs_inode_flag(inode, HARD_LINK);
		vdfs_release_dirty_record(
				(struct vdfs_btree_gen_record *) record);

	} else
		vdfs_release_record((struct vdfs_btree_gen_record *) record);

	/*   */
	ret = add_hlink_record(sbi->catalog_tree, inode->i_ino, dir->i_ino,
			inode->i_mode, &dentry->d_name);
	if (ret)
		goto err_exit;

	EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	inode->i_ctime = emmcfs_current_time(inode);


#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	atomic_inc(&inode->i_count);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) ||\
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	ihold(inode);
#endif

	d_instantiate(dentry, inode);
	inode_inc_link_count(inode);

	ret = emmcfs_write_inode_to_bnode(inode);
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
	if (ret)
		goto exit;

	mutex_lock(&EMMCFS_I(dir)->truncate_mutex);
	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	dir->i_size++;
	ret = emmcfs_write_inode_to_bnode(dir);
	mutex_unlock(&EMMCFS_I(dir)->truncate_mutex);
	if (ret)
		goto exit;

	sbi->files_count++;
exit:
	vdfs_stop_transaction(sbi);
	return ret;
err_exit:
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
	goto exit;
}

/**
 * @brief			Make node.
 * @param [in,out]	dir		Directory where node will be created
 * @param [in]		dentry	Created dentry
 * @param [in]		mode	Mode for file
 * @param [in]		rdev	Device
 * @return			Returns 0 on success, errno on failure
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static int emmcfs_mknod(struct inode *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
#else
static int emmcfs_mknod(struct inode *dir, struct dentry *dentry,
			int mode, dev_t rdev)
#endif
{
	struct inode *created_ino;
	int ret;

	vdfs_start_transaction(VDFS_SB(dir->i_sb));

	if (!new_valid_dev(rdev)) {
		ret = -EINVAL;
		goto exit;
	}

	ret = emmcfs_create(dir, dentry, mode, NULL);
	if (ret)
		goto exit;

	created_ino = dentry->d_inode;
	init_special_inode(created_ino, created_ino->i_mode, rdev);
	mutex_lock(&EMMCFS_I(created_ino)->truncate_mutex);
	ret = emmcfs_write_inode_to_bnode(created_ino);
	mutex_unlock(&EMMCFS_I(created_ino)->truncate_mutex);
exit:
	vdfs_stop_transaction(VDFS_SB(dir->i_sb));
	return ret;
}

/**
 * @brief			Make symlink.
 * @param [in,out]	dir		Directory where node will be created
 * @param [in]		dentry	Created dentry
 * @param [in]		symname Symbolic link name
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_symlink(struct inode *dir, struct dentry *dentry,
	const char *symname)
{
	int ret;
	struct inode *created_ino;
	unsigned int len = strlen(symname);

	if ((len > EMMCFS_FULL_PATH_LEN) ||
			(dentry->d_name.len > EMMCFS_CAT_MAX_NAME))
		return -ENAMETOOLONG;

	vdfs_start_transaction(VDFS_SB(dir->i_sb));

	ret = emmcfs_create(dir, dentry, S_IFLNK | S_IRWXUGO, NULL);

	if (ret)
		goto exit;

	clear_vdfs_inode_flag(dentry->d_inode, TINY_FILE);

	created_ino = dentry->d_inode;
	ret = page_symlink(created_ino, symname, ++len);
exit:
	vdfs_stop_transaction(VDFS_SB(dir->i_sb));
	return ret;
}

/**
 * The eMMCFS address space operations.
 */
const struct address_space_operations emmcfs_aops = {
	.readpage	= vdfs_readpage,
	.readpages	= vdfs_readpages,
	.writepage	= vdfs_writepage,
	.writepages	= vdfs_writepages,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	.sync_page	= block_sync_page,
#endif
	.write_begin	= emmcfs_write_begin,
	.write_end	= emmcfs_write_end,
	.bmap		= emmcfs_bmap,
/*	.direct_IO	= emmcfs_direct_IO,*/
	.migratepage	= buffer_migrate_page,
	.releasepage = vdfs_releasepage,
/*	.set_page_dirty = __set_page_dirty_buffers,*/

};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
int vdfs_fail_migrate_page(struct address_space *mapping,
			struct page *newpage, struct page *page,
				enum migrate_mode mode)
{
#ifdef CONFIG_MIGRATION
	return fail_migrate_page(mapping, newpage, page);
#else
	return -EIO;
#endif
}
#endif


static const struct address_space_operations emmcfs_aops_special = {
	.readpage	= vdfs_readpage_special,
	.readpages	= vdfs_readpages_special,
	.writepage	= vdfs_writepage,
	.writepages	= vdfs_writepages_special,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	.sync_page	= block_sync_page,
#endif
	.write_begin	= emmcfs_write_begin,
	.write_end	= emmcfs_write_end,
	.bmap		= emmcfs_bmap,
/*	.direct_IO	= emmcfs_direct_IO, */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	.migratepage	= vdfs_fail_migrate_page,
#else
	.migratepage	= fail_migrate_page,
#endif
/*	.set_page_dirty = __set_page_dirty_buffers,*/
};

/**
 * The eMMCFS directory inode operations.
 */
static const struct inode_operations emmcfs_dir_inode_operations = {
	/* d.voytik-TODO-19-01-2012-11-15-00:
	 * [emmcfs_dir_inode_ops] add to emmcfs_dir_inode_operations
	 * necessary methods */
	.create		= emmcfs_create,
	.symlink	= emmcfs_symlink,
	.lookup		= emmcfs_lookup,
	.link		= emmcfs_link,
	.unlink		= vdfs_unlink,
	.mkdir		= emmcfs_mkdir,
	.rmdir		= emmcfs_rmdir,
	.mknod		= emmcfs_mknod,
	.rename		= emmcfs_rename,
	.setattr	= emmcfs_setattr,

	.setxattr	= vdfs_setxattr,
	.getxattr	= vdfs_getxattr,
	.removexattr	= vdfs_removexattr,
	.listxattr	= vdfs_listxattr,
};

/**
 * The eMMCFS symlink inode operations.
 */
static const struct inode_operations emmcfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.setattr	= emmcfs_setattr,

	.setxattr	= vdfs_setxattr,
	.getxattr	= vdfs_getxattr,
	.removexattr	= vdfs_removexattr,
	.listxattr	= vdfs_listxattr,
};


/**
 * The eMMCFS directory operations.
 */
const struct file_operations emmcfs_dir_operations = {
	/* d.voytik-TODO-19-01-2012-11-16-00:
	 * [emmcfs_dir_ops] add to emmcfs_dir_operations necessary methods */
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= emmcfs_readdir,
	.unlocked_ioctl = vdfs_dir_ioctl,
};

/**
 * TODO
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
int emmcfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
#else
static int emmcfs_file_fsync(struct file *file, int datasync)
#endif
{
	struct super_block *sb = file->f_mapping->host->i_sb;
	int ret = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	ret = generic_file_fsync(file, start, end, datasync);
#else
	ret = generic_file_fsync(file, datasync);
#endif
	if (ret)
		return ret;

	ret = vdfs_sync_fs(sb, 1);
	return (!ret || ret == -EINVAL) ? 0 : ret;
}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
long emmcfs_fallocate(struct inode *inode, int mode, loff_t offset, loff_t len);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
long emmcfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len);
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * @brief		Calculation of writing position in a case when data is
 *			appending to a target file
 * @param [in]	iocb	Struct describing writing file
 * @param [in]	pos	Position to write from
 * @return		Returns the writing position
 */
static inline loff_t get_real_writing_position(struct kiocb *iocb, loff_t pos)
{
	loff_t write_pos = 0;
	if (iocb->ki_filp->f_flags & O_APPEND)
		write_pos = i_size_read(INODE(iocb));

	write_pos = MAX(write_pos, pos);
	iocb->ki_pos = write_pos;
	return write_pos;
}


/**
	iocb->ki_pos = write_pos;
 * @brief		VDFS function for aio write
 * @param [in]	iocb	Struct describing writing file
 * @param [in]	iov	Struct for writing data
 * @param [in]	nr_segs	Number of segs to write
 * @param [in]	pos	Position to write from
 * @return		Returns number of bytes written or an error code
 */
static ssize_t vdfs_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = INODE(iocb);
	ssize_t ret = 0;

	if (check_permissions((struct vdfs_sb_info *)inode->i_sb->s_fs_info))
		return -EINTR;
	/* We are trying to write iocb->ki_left bytes from iov->iov_base */
	/* The target file is tiny or small */
	if (is_vdfs_inode_flag_set(inode, TINY_FILE) ||
		is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
		loff_t write_pos = get_real_writing_position(iocb, pos);
		ret = process_tiny_small_file(iocb, iov, nr_segs, write_pos);
	/* The target file is a normal file */
	} else
		ret = generic_file_aio_write(iocb, iov, nr_segs, pos);

#if defined(CONFIG_VDFS_DEBUG)
	if (IS_ERR_VALUE(ret))
		EMMCFS_ERR("err = %d, ino#%lu name=%s",
			ret, inode->i_ino, EMMCFS_I(inode)->name);
#endif
	return ret;
}

/**
 * @brief		VDFS function for aio read
 * @param [in]	iocb	Struct describing reading file
 * @param [in]	iov	Struct for read data
 * @param [in]	nr_segs	Number of segs to read
 * @param [in]	pos	Position to read from
 * @return		Returns number of bytes read or an error code
 */
static ssize_t vdfs_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = INODE(iocb);
	ssize_t ret;
	if (check_permissions((struct vdfs_sb_info *)inode->i_sb->s_fs_info))
		return -EINTR;

	ret = generic_file_aio_read(iocb, iov, nr_segs, pos);
	return ret;
}

/**
 * The eMMCFS file operations.
 */
static const struct file_operations emmcfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= vdfs_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= vdfs_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= vdfs_file_open,
	.release	= vdfs_file_release,
	.fsync		= emmcfs_file_fsync,
	.unlocked_ioctl = emmcfs_ioctl,
#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
	LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	.fallocate	= emmcfs_fallocate,
#endif
};

/**
 * The eMMCFS files inode operations.
 */
static const struct inode_operations emmcfs_file_inode_operations = {
		/* FIXME & TODO is this correct : use same function as in
		emmcfs_dir_inode_operations? */
		/*.truncate	= emmcfs_file_truncate, depricated*/
		.setattr	= emmcfs_setattr,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
		.fallocate	= emmcfs_fallocate,
#endif
		.setxattr	= vdfs_setxattr,
		.getxattr	= vdfs_getxattr,
		.removexattr	= vdfs_removexattr,
		.listxattr	= vdfs_listxattr,
};


static int vdfs_fill_inode_fork(struct inode *inode,
		struct vdfs_catalog_file_record *file_val)
{
	struct emmcfs_inode_info *inf = EMMCFS_I(inode);
	int ret = 0;

	/* TODO special files like fifo or soket does not have fork */
	if (is_vdfs_inode_flag_set(inode, TINY_FILE)) {
		memcpy(inf->tiny.data, file_val->tiny.data, TINY_DATA_SIZE);
		inf->tiny.len = le16_to_cpu(file_val->tiny.len);
		inf->tiny.i_size = le64_to_cpu(file_val->tiny.i_size);
		inode->i_size = inf->tiny.i_size;
		inode->i_blocks = 0;
	} else if (is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
		inf->small.cell = le64_to_cpu(file_val->small.cell);
		inf->small.len = le16_to_cpu(file_val->small.len);
		inf->small.i_size = le64_to_cpu(file_val->small.i_size);
		inode->i_size = inf->small.i_size;
		inode->i_blocks = 0;
	} else
		ret = vdfs_parse_fork(inode, &file_val->data_fork);

	return ret;
}


static int vdfs_fill_inode(struct inode *inode,
		struct vdfs_catalog_folder_record *folder_val)
{
	int ret = 0;
	/*struct vdfs_sb_info *sbi = */

	EMMCFS_I(inode)->flags = le32_to_cpu(folder_val->flags);
	vdfs_set_vfs_inode_flags(inode);

	atomic_set(&(EMMCFS_I(inode)->open_count), 0);

	inode->i_mode = le16_to_cpu(folder_val->permissions.file_mode);
	inode->i_uid = (uid_t)le32_to_cpu(folder_val->permissions.uid);
	inode->i_gid = (uid_t)le32_to_cpu(folder_val->permissions.gid);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	set_nlink(inode, le64_to_cpu(folder_val->links_count));
#else
	inode->i_nlink = le64_to_cpu(folder_val->links_count);
#endif
	inode->i_generation = inode->i_ino;

	inode->i_mtime.tv_sec =
			le64_to_cpu(folder_val->modification_time.seconds);
	inode->i_atime.tv_sec =
			le64_to_cpu(folder_val->access_time.seconds);
	inode->i_ctime.tv_sec =
			le64_to_cpu(folder_val->creation_time.seconds);

	inode->i_mtime.tv_nsec =
			le64_to_cpu(folder_val->modification_time.nanoseconds);
	inode->i_atime.tv_nsec =
			le64_to_cpu(folder_val->access_time.nanoseconds);
	inode->i_ctime.tv_nsec =
			le64_to_cpu(folder_val->creation_time.nanoseconds);

	if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &emmcfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &emmcfs_aops;
		inode->i_fop = &emmcfs_file_operations;

	} else if S_ISREG(inode->i_mode) {
		inode->i_op = &emmcfs_file_inode_operations;
		inode->i_mapping->a_ops = &emmcfs_aops;
		inode->i_fop = &emmcfs_file_operations;

	} else if S_ISDIR(inode->i_mode) {
		inode->i_size = (loff_t)le64_to_cpu(
				folder_val->total_items_count);
		inode->i_op = &emmcfs_dir_inode_operations;
		inode->i_fop = &emmcfs_dir_operations;
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
			S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		inode->i_mapping->a_ops = &emmcfs_aops;
		init_special_inode(inode, inode->i_mode,
			(dev_t)le64_to_cpu(folder_val->total_items_count));
	} else {
		/* UNKNOWN object type*/
		ret = -EINVAL;
	}

	return ret;
}

struct inode *get_inode_from_record(struct vdfs_cattree_record *record)
{
	struct vdfs_btree *tree =
		VDFS_BTREE_REC_I((void *) record)->rec_pos.bnode->host;
	struct vdfs_sb_info *sbi = tree->sbi;
	struct vdfs_catalog_folder_record *folder_rec = NULL;
	struct vdfs_catalog_file_record *file_rec = NULL;
	struct vdfs_hlink_record *hard_link_record = NULL;
	__u64 ino = 0;
	struct inode *inode;
	int ret = 0;

	if (IS_ERR(record) || !record)
		return ERR_PTR(-EFAULT);

	if (record->key->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		ino = le64_to_cpu((
			(struct vdfs_pack_common_value *)record->val)->
				object_id);
		if (tree->btree_type == VDFS_BTREE_PACK)
			ino += tree->start_ino;
	} else if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		struct vdfs_catalog_hlink_record *hlink_record = record->val;
		ino = le64_to_cpu(hlink_record->object_id);
	} else {
		struct vdfs_catalog_folder_record *f_record = record->val;
		ino = le64_to_cpu(f_record->object_id);
	}

	inode = iget_locked(sbi->sb, ino);

	if (!inode) {
		inode = ERR_PTR(-ENOMEM);
		goto exit;
	}

	if (!(inode->i_state & I_NEW))
		goto exit;

	EMMCFS_I(inode)->record_type = record->key->record_type;

	/* create inode from pack tree */
	if (record->key->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		ret = vdfs_read_packtree_inode(inode, record->key);
		if (ret)
			goto error_exit;
		else {
#ifdef CONFIG_VDFS_QUOTA
			EMMCFS_I(inode)->quota_index = -1;
#endif
			unlock_new_inode(inode);
			goto exit;
		}
	}

	/* create inode from catalog tree*/
	if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		mutex_r_lock_nested(sbi->hardlink_tree->rw_tree_lock, HL_TREE);
		hard_link_record = vdfs_hard_link_find(sbi, ino,
				EMMCFS_BNODE_MODE_RO);
		if (IS_ERR(hard_link_record)) {
			mutex_r_unlock(sbi->hardlink_tree->rw_tree_lock);
			ret = PTR_ERR(hard_link_record);
			goto error_exit;
		}
		file_rec = hard_link_record->hardlink_value;
		folder_rec = &file_rec->common;
	} else if (record->key->record_type == VDFS_CATALOG_FILE_RECORD) {
		file_rec = record->val;
		folder_rec = &file_rec->common;
	} else if (record->key->record_type == VDFS_CATALOG_FOLDER_RECORD) {
		folder_rec =
			(struct vdfs_catalog_folder_record *)record->val;
	} else {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			ret = -EFAULT;
			goto error_exit;
		} else
			EMMCFS_BUG();
	}

	ret = vdfs_fill_inode(inode, folder_rec);
	if (ret)
		goto error_put_hrd;

	if (file_rec && (S_ISLNK(inode->i_mode) || S_ISREG((inode->i_mode))))
		ret = vdfs_fill_inode_fork(inode, file_rec);

	if (ret)
		goto error_put_hrd;

	if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		set_vdfs_inode_flag(inode, HARD_LINK);
		vdfs_release_record((struct vdfs_btree_gen_record *)
				hard_link_record);
		mutex_r_unlock(sbi->hardlink_tree->rw_tree_lock);
	} else {
		char *new_name;
		unsigned int str_len;
		struct emmcfs_cattree_key *key = record->key;
		new_name = kzalloc(key->name.length + 1, GFP_KERNEL);
		if (!new_name) {
			ret = -ENOMEM;
			goto error_put_hrd;
		}

		str_len = min(key->name.length, (unsigned) EMMCFS_CAT_MAX_NAME);
		memcpy(new_name, key->name.unicode_str,
			min(key->name.length, (unsigned) EMMCFS_CAT_MAX_NAME));
		new_name[str_len] = 0;
		EMMCFS_BUG_ON(EMMCFS_I(inode)->name);
		EMMCFS_I(inode)->name = new_name;
		EMMCFS_I(inode)->parent_id = le64_to_cpu(key->parent_id);
	}

#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(inode)->quota_index = -1;
#endif
	unlock_new_inode(inode);

exit:
	return inode;
error_put_hrd:
	if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		if (hard_link_record)
			vdfs_release_record((struct vdfs_btree_gen_record *)
				hard_link_record);
		mutex_r_unlock(sbi->hardlink_tree->rw_tree_lock);
	}
error_exit:
	iget_failed(inode);
	return ERR_PTR(ret);
}

/**
 * @brief		The eMMCFS inode constructor.
 * @param [in]	dir		Directory, where inode will be created
 * @param [in]	mode	Mode for created inode
 * @return		Returns pointer to inode on success, errno on failure
 */

static struct inode *emmcfs_new_inode(struct inode *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct  vdfs_sb_info *sbi = VDFS_SB(sb);
	ino_t ino = 0;
	struct inode *inode;
	int err, i;
	struct vdfs_fork_info *ifork;

	err = vdfs_get_free_inode(sb->s_fs_info, &ino, 1);

	if (err)
		return ERR_PTR(err);

	/*EMMCFS_DEBUG_INO("#%lu", ino);*/
	inode = new_inode(sb);
	if (!inode) {
		err = -ENOMEM;
		goto err_exit_noiput;
	}

	inode->i_ino = ino;

	if (test_option(sbi, DMASK) && S_ISDIR(mode))
		mode &= ~sbi->dmask;

	if (test_option(sbi, FMASK) && S_ISREG(mode))
		mode &= ~sbi->fmask;

	inode_init_owner(inode, dir, mode);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	set_nlink(inode, 1);
#else
	inode->i_nlink = 1;
#endif
	inode->i_size = 0;
	inode->i_generation = le32_to_cpu(VDFS_RAW_EXSB(sbi)->generation);
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime =
			emmcfs_current_time(inode);
	atomic_set(&(EMMCFS_I(inode)->open_count), 0);

	/* todo actual inheritance mask and mode-dependent masking */
	EMMCFS_I(inode)->flags = EMMCFS_I(dir)->flags & EMMCFS_FL_INHERITED;
	vdfs_set_vfs_inode_flags(inode);

	if (S_ISDIR(mode))
		inode->i_op =  &emmcfs_dir_inode_operations;
	else if (S_ISLNK(mode))
		inode->i_op = &emmcfs_symlink_inode_operations;
	else
		inode->i_op = &emmcfs_file_inode_operations;

	inode->i_mapping->a_ops = &emmcfs_aops;
	inode->i_fop = (S_ISDIR(mode)) ?
			&emmcfs_dir_operations : &emmcfs_file_operations;

	/* Init extents with zeros - file is empty */
	ifork = &(EMMCFS_I(inode)->fork);
	ifork->used_extents = 0;
	for (i = VDFS_EXTENTS_COUNT_IN_FORK - 1; i >= 0; i--) {
		ifork->extents[i].first_block = 0;
		ifork->extents[i].block_count = 0;
		ifork->extents[i].iblock = 0;
	}
	ifork->total_block_count = 0;
	ifork->prealloc_start_block = 0;
	ifork->prealloc_block_count = 0;

	EMMCFS_I(inode)->parent_id = 0;

	if (insert_inode_locked(inode) < 0) {
		err = -EINVAL;
		goto err_exit;
	}

	return inode;

err_exit:
	iput(inode);
err_exit_noiput:
	if (vdfs_free_inode_n(sb->s_fs_info, ino, 1))
		EMMCFS_ERR("can not free inode while handling error");

	return ERR_PTR(err);
}


/**
 * @brief			Standard callback to create file.
 * @param [in,out]	dir		Directory where node will be created
 * @param [in]		dentry	Created dentry
 * @param [in]		mode	Mode for file
 * @param [in]		nd	Namedata for file
 * @return			Returns 0 on success, errno on failure
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static int emmcfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
#else
static int emmcfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
#endif
{
	struct super_block *sb = dir->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode;
	char *saved_name;
	int ret = 0;
	/* TODO - ext2 does it - detrmine if it's necessary here */
	/* dquot_initialize(dir); */
	if (check_permissions(sbi))
		return -EINTR;
	EMMCFS_DEBUG_INO("'%s' dir = %ld", dentry->d_name.name, dir->i_ino);

	if (strlen(dentry->d_name.name) > EMMCFS_UNICODE_STRING_MAX_LEN)
		return -ENAMETOOLONG;

	saved_name = kzalloc(dentry->d_name.len + 1, GFP_KERNEL);
	if (!saved_name) {
		ret = -ENOMEM;
		goto exit;
	}

	vdfs_start_transaction(sbi);
	inode = emmcfs_new_inode(dir, mode);

	if (IS_ERR(inode)) {
		kfree(saved_name);
		ret = PTR_ERR(inode);
		goto exit;
	}


	strncpy(saved_name, dentry->d_name.name, dentry->d_name.len + 1);

	EMMCFS_I(inode)->name = saved_name;
	EMMCFS_I(inode)->parent_id = dir->i_ino;
#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(inode)->quota_index = -1;
#endif

	EMMCFS_DEBUG_MUTEX("cattree mutex w lock");
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock succ");
	/* if it's a file */
	if ((S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
				(inode->i_ino >= VDFS_1ST_FILE_INO) &&
				/* and tiny files are enabled */
				(test_option(sbi, TINY) ||
				test_option(sbi, TINYSMALL))) {
		/* set tiny flag */
		set_vdfs_inode_flag(inode, TINY_FILE);
		atomic64_inc(&sbi->tiny_files_counter);
	}
	unlock_new_inode(inode);
	ret = vdfs_insert_cattree_object(sbi->catalog_tree, inode, dir->i_ino);
	if (ret)
		goto error_exit;

	ret = security_inode_init_security(inode, dir,
			&dentry->d_name, vdfs_init_security_xattrs, NULL);
	if (ret && ret != -EOPNOTSUPP) {
		BUG_ON(vdfs_cattree_remove(sbi, inode->i_ino,
			EMMCFS_I(inode)->name, strlen(EMMCFS_I(inode)->name)));
		goto error_exit;
	}

	d_instantiate(dentry, inode);
	vdfs_put_inode_into_dirty_list(inode);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	mutex_lock(&EMMCFS_I(dir)->truncate_mutex);
	dir->i_size++;
	sbi->files_count++;
	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	ret = emmcfs_write_inode_to_bnode(dir);
	mutex_unlock(&EMMCFS_I(dir)->truncate_mutex);
#ifdef CONFIG_VDFS_QUOTA
	if (EMMCFS_I(dir)->quota_index != -1)
		EMMCFS_I(inode)->quota_index =
				EMMCFS_I(dir)->quota_index;
#endif
exit:
	vdfs_stop_transaction(sbi);
	return ret;
error_exit:
	iput(inode);
	vdfs_free_inode_n(sbi, inode->i_ino, 1);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	kfree(saved_name);
	vdfs_stop_transaction(sbi);
	return ret;
}

/**
 * @brief			Write inode to bnode.
 * @param [in,out]	inode	The inode, that will be written to bnode
 * @return			Returns 0 on success, errno on failure
 */
int emmcfs_write_inode_to_bnode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	int ret = 0;

	if (inode->i_ino < VDFS_1ST_FILE_INO && inode->i_ino != VDFS_ROOT_INO)
		return 0;

	vdfs_put_inode_into_dirty_list(inode);

	if (EMMCFS_I(inode)->record_type >= VDFS_CATALOG_PTREE_ROOT)
		return 0;

	BUG_ON(!mutex_is_locked(&inode_info->truncate_mutex));

	if (is_vdfs_inode_flag_set(inode, HARD_LINK)) {
		struct vdfs_hlink_record *hard_link_info;

		mutex_w_lock(sbi->hardlink_tree->rw_tree_lock);
		hard_link_info = vdfs_hard_link_find(sbi, inode->i_ino,
				EMMCFS_BNODE_MODE_RW);

		if (IS_ERR(hard_link_info)) {
			ret = PTR_ERR(hard_link_info);
			mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
			goto exit;
		}

		ret = emmcfs_fill_cattree_value(inode,
				(void *)hard_link_info->hardlink_value);

		vdfs_release_dirty_record((struct vdfs_btree_gen_record *)
				hard_link_info);
		mutex_w_unlock(sbi->hardlink_tree->rw_tree_lock);
	} else {
		struct vdfs_cattree_record *record;

		mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
		record = vdfs_cattree_find(sbi->catalog_tree,
			inode_info->parent_id, inode_info->name,
			strlen(inode_info->name), EMMCFS_BNODE_MODE_RW);

		if (IS_ERR(record)) {
			ret = PTR_ERR(record);
			mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
			goto exit;
		}

		ret = emmcfs_fill_cattree_value(inode, record->val);

		vdfs_release_dirty_record((struct vdfs_btree_gen_record *)
				record);
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	}

exit:
	return ret;
}

/**
 * @brief		Method to read inode to inode cache.
 * @param [in]	sb	Pointer to superblock
 * @param [in]	ino	The inode number
 * @return		Returns pointer to inode on success,
 *			ERR_PTR(errno) on failure
 */
struct inode *vdfs_special_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	int ret = 0;
	gfp_t gfp_mask;
	loff_t size;

	EMMCFS_BUG_ON(ino >= VDFS_1ST_FILE_INO);
	EMMCFS_DEBUG_INO("inode #%lu", ino);
	inode = iget_locked(sb, ino);
	if (!inode) {
		ret = -ENOMEM;
		goto err_exit_no_fail;
	}

	if (!(inode->i_state & I_NEW))
		goto exit;

	inode->i_mode = 0;

	/* Metadata pages can not be migrated */
	gfp_mask = (mapping_gfp_mask(inode->i_mapping) & ~GFP_MOVABLE_MASK);
	mapping_set_gfp_mask(inode->i_mapping, gfp_mask);

	if (ino == VDFS_SMALL_AREA) {
		struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
		ret = vdfs_parse_fork(inode, &exsb->small_area);
		size = ((loff_t)inode_info->fork.total_block_count) <<
				sbi->log_block_size;
		inode->i_mapping->a_ops = &emmcfs_aops;

	} else {
		size = vdfs_special_file_size(sbi, ino);
		inode->i_mapping->a_ops = &emmcfs_aops_special;
	}
	i_size_write(inode, size);

	if (ret) {
		iput(inode);
		goto err_exit_no_fail;
	}
#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(inode)->quota_index = -1;
#endif
	unlock_new_inode(inode);
exit:
	return inode;
err_exit_no_fail:
	EMMCFS_DEBUG_INO("inode #%lu read FAILED", ino);
	return ERR_PTR(ret);
}

/**
 * @brief		Propagate flags from vfs inode i_flags
 *			to EMMCFS_I(inode)->flags.
 * @param [in]	inode	Pointer to vfs inode structure.
  * @return		none.
 */
void vdfs_get_vfs_inode_flags(struct inode *inode)
{
	EMMCFS_I(inode)->flags &= ~(1 << VDFS_IMMUTABLE);
	if (inode->i_flags & S_IMMUTABLE)
		EMMCFS_I(inode)->flags |= (1 << VDFS_IMMUTABLE);
}

/**
 * @brief		Set vfs inode i_flags according to
 *			EMMCFS_I(inode)->flags.
 * @param [in]	inode	Pointer to vfs inode structure.
  * @return		none.
 */
void vdfs_set_vfs_inode_flags(struct inode *inode)
{
	inode->i_flags &= ~S_IMMUTABLE;
	if (EMMCFS_I(inode)->flags & (1 << VDFS_IMMUTABLE))
		inode->i_flags |= S_IMMUTABLE;
}


