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
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/file.h>

#include <linux/crypto.h>

#include "emmcfs.h"
#include "cattree.h"
#include "packtree.h"
#include "debug.h"
#include "installed.h"


#ifdef CONFIG_VDFS_HW2_SUPPORT
#include <mach/sdp_unzip.h>
#endif

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
int __vdfs_write_inode(struct vdfs_sb_info *sbi, struct inode *inode);

int vdfs_is_tree_alive(struct inode *inode)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	if (EMMCFS_I(inode)->installed_btrees)
		return inode_info->installed_btrees->start_ino ? 1 : 0;
	else
		return inode_info->ptree.tree_info->params.start_ino ? 1 : 0;
}
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

	record = vdfs_cattree_find_inode(tree,
			VDFS_ROOT_INO, VDFS_ROOTDIR_OBJ_ID,
			VDFS_ROOTDIR_NAME, strlen(VDFS_ROOTDIR_NAME),
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		/* Pass error code to return value */
		root_inode = (void *)record;
		goto exit;
	}

	root_inode = get_inode_from_record(record, NULL);
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
exit:
	return root_inode;
}


static void vdfs_fill_cattree_record(struct inode *inode,
		struct vdfs_cattree_record *record)
{
	void *pvalue = record->val;

	BUG_ON(!pvalue || IS_ERR(pvalue));

	EMMCFS_I(inode)->record_type = record->key->record_type;

	if (VDFS_GET_CATTREE_RECORD_TYPE(record) == VDFS_CATALOG_HLINK_RECORD)
		emmcfs_fill_hlink_value(inode, pvalue);
	else
		emmcfs_fill_cattree_value(inode, pvalue);
}

static struct vdfs_btree *select_btree(struct inode *dir_inode,
		__u64 *catalog_id)
{
	struct emmcfs_inode_info *dir_inode_info = EMMCFS_I(dir_inode);
	struct vdfs_btree *btree = NULL;
	struct vdfs_sb_info *sbi = dir_inode->i_sb->s_fs_info;

	if (dir_inode_info->record_type == VDFS_CATALOG_PTREE_ROOT) {
		if (dir_inode_info->ptree.tree_info == NULL) {
			struct installed_packtree_info *packtree;
			packtree = vdfs_get_packtree(dir_inode);
			if (IS_ERR(packtree))
				return (struct vdfs_btree *)packtree;
			dir_inode_info->ptree.tree_info = packtree;
			atomic_set(&(packtree->open_count), 0);
		} else if (!vdfs_is_tree_alive(dir_inode))
			return ERR_PTR(-ENOENT);

		*catalog_id = VDFS_ROOT_INO;
		btree = dir_inode_info->ptree.tree_info->tree;

	} else if (dir_inode_info->record_type == VDFS_CATALOG_RO_IMAGE_ROOT) {
		if (dir_inode_info->installed_btrees == NULL) {
			struct installed_info *installed_btrees;
			installed_btrees = vdfs_get_installed_tree(dir_inode);
			if (IS_ERR(installed_btrees)) {
				void *ret = installed_btrees;
				return (struct vdfs_btree *)ret;
			}
			dir_inode_info->installed_btrees = installed_btrees;
		} else if (!vdfs_is_tree_alive(dir_inode))
				return ERR_PTR(-ENOENT);

		*catalog_id = VDFS_ROOT_INO;
		btree = dir_inode_info->installed_btrees->cat_tree;

	} else if (dir_inode_info->record_type >= VDFS_CATALOG_PTREE_RECORD) {

		btree = (!vdfs_is_tree_alive(dir_inode)) ? ERR_PTR(-ENOENT) :
			dir_inode_info->ptree.tree_info->tree;
		*catalog_id -=
			dir_inode_info->ptree.tree_info->params.start_ino;

	} else if (dir_inode_info->installed_btrees != NULL) {

		*catalog_id -= dir_inode_info->installed_btrees->start_ino;
		btree = (!vdfs_is_tree_alive(dir_inode)) ? ERR_PTR(-ENOENT) :
				dir_inode_info->installed_btrees->cat_tree;

	} else {
		btree = sbi->catalog_tree;
	}

	return btree;
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
	struct dentry *dentry = filp->f_dentry;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	__u64 catalog_id = inode->i_ino;
	int ret = 0;
	struct vdfs_cattree_record *record;
	struct vdfs_btree *btree = NULL;
	loff_t pos = 2; /* "." and ".." */

	/* return 0 if no more entries in the directory */
	switch (filp->f_pos) {
	case 0:
		if (filldir(dirent, ".", 1, filp->f_pos++, inode->i_ino,
					DT_DIR))
			goto exit_noput;
		/* fallthrough */
		/* filp->f_pos increases and so processing is done immediately*/
	case 1:
		if (filldir(dirent, "..", 2, filp->f_pos++,
			dentry->d_parent->d_inode->i_ino, DT_DIR))
			goto exit_noput;
		break;
	default:
		break;
	}

	mutex_r_lock(sbi->catalog_tree->rw_tree_lock);
	btree = select_btree(inode, &catalog_id);

	if (IS_ERR(btree)) {
		mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
		return PTR_ERR(btree);
	}

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
		umode_t object_mode;
		u8 record_type;
		__u64 obj_id;

		if (record->key->parent_id != cpu_to_le64(catalog_id))
			goto exit;

		if ((record->key->record_type == VDFS_CATALOG_ILINK_RECORD) ||
		    (record->key->record_type == VDFS_CATALOG_LLINK_RECORD) ||
				vdfs_cattree_is_orphan(record))
			goto skip;

		if (!filp->private_data && pos < filp->f_pos)
			goto next;

		cattree_val = record->val;
		record_type = record->key->record_type;
		obj_id = le64_to_cpu(record->key->object_id);
		if (record_type >= VDFS_CATALOG_PTREE_FOLDER) {
			struct vdfs_pack_common_value *pack_val = record->val;
			struct installed_packtree_info *tree_info =
					EMMCFS_I(inode)->ptree.tree_info;

			object_mode = le16_to_cpu(pack_val->file_mode);
			obj_id += tree_info->params.start_ino;
		} else if (record_type == VDFS_CATALOG_PTREE_ROOT) {
			struct vdfs_pack_insert_point_value *val = record->val;

			object_mode = le16_to_cpu(val->common.file_mode);
		} else if (record_type == VDFS_CATALOG_HLINK_RECORD) {
			object_mode = le16_to_cpu((
					(struct vdfs_catalog_hlink_record *)
					cattree_val)->file_mode);
		} else {
			object_mode = le16_to_cpu(cattree_val->file_mode);
		}

		if (btree->btree_type == VDFS_BTREE_INST_CATALOG)
			obj_id += btree->start_ino;

		ret = filldir(dirent, record->key->name, record->key->name_len,
				filp->f_pos, obj_id, IFTODT(object_mode));

		if (ret) {
			char *private_data;

			if (!filp->private_data) {
				private_data = kmalloc(VDFS_FILE_NAME_LEN + 1,
						GFP_KERNEL);
				filp->private_data = private_data;
				if (!private_data) {
					ret = -ENOMEM;
					goto fail;
				}
			} else {
				private_data = filp->private_data;
			}

			memcpy(private_data, record->key->name,
					record->key->name_len);
			private_data[record->key->name_len] = 0;

			ret = 0;
			goto exit;
		}

		++filp->f_pos;
next:
		++pos;
skip:
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
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
exit_noput:
	return ret;
fail:
	EMMCFS_DEBUG_INO("finished with err (%d)", ret);
	if (!IS_ERR_OR_NULL(record))
		vdfs_release_record((struct vdfs_btree_gen_record *) record);

	EMMCFS_DEBUG_MUTEX("cattree mutex r lock un");
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	return ret;
}

static int vdfs_release_dir(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	filp->private_data = NULL;
	return 0;
}

static loff_t vdfs_llseek_dir(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;

	mutex_lock(&inode->i_mutex);
	vdfs_release_dir(inode, file);
	mutex_unlock(&inode->i_mutex);

	return generic_file_llseek(file, offset, whence);
}

/**
 * @brief		Method to look up an entry in a directory.
 * @param [in]	dir		Parent directory
 * @param [in]	dentry	Searching entry
 * @param [in]	nd		Associated nameidata
 * @return		Returns pointer to found dentry, NULL if it is
 *			not found, ERR_PTR(errno) on failure
 */
struct dentry *vdfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct vdfs_cattree_record *record;
	struct inode *inode;
	struct vdfs_btree *tree = sbi->catalog_tree;
	struct dentry *ret = NULL;
	__u64 catalog_id = dir->i_ino;

	if (dentry->d_name.len > VDFS_FILE_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	mutex_r_lock(sbi->catalog_tree->rw_tree_lock);
	tree = select_btree(dir, &catalog_id);

	if (IS_ERR(tree)) {
		mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
		return (struct dentry *)tree;
	}

	record = vdfs_cattree_find(tree, catalog_id,
			dentry->d_name.name, dentry->d_name.len,
			EMMCFS_BNODE_MODE_RO);

	if (!IS_ERR(record) && ((record->key->record_type ==
			VDFS_CATALOG_ILINK_RECORD))) {
		struct emmcfs_cattree_key *key;
		key = kzalloc(sizeof(*key), GFP_KERNEL);
		if (!key) {
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);

			inode = ERR_PTR(-ENOMEM);
			goto exit;
		}
		key->parent_id = cpu_to_le64(catalog_id);
		key->object_id = cpu_to_le64(record->key->object_id - 1);
		key->name_len = (u8)dentry->d_name.len;
		memcpy(key->name, dentry->d_name.name,
				(size_t)dentry->d_name.len);
		vdfs_release_record((struct vdfs_btree_gen_record *)record);
		record = (struct vdfs_cattree_record *)
				vdfs_btree_find(tree, &key->gen_key,
				EMMCFS_BNODE_MODE_RO);
		kfree(key);
		if (!IS_ERR(record) && (record->key->parent_id
			!= catalog_id || record->key->name_len !=
			dentry->d_name.len || memcmp(record->key->name,
				dentry->d_name.name,
				(size_t)record->key->name_len))) {
			vdfs_release_record((struct vdfs_btree_gen_record *)
					record);
			record = ERR_PTR(-ENOENT);
		}
	}

	if (!IS_ERR(record)) {
		if ((record->key->record_type == VDFS_CATALOG_ILINK_RECORD) ||
				WARN_ON(vdfs_cattree_is_orphan(record)))
			inode = NULL;
		else
			inode = get_inode_from_record(record, dir);
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
	} else if (record == ERR_PTR(-ENOENT))
		inode = NULL;
	else
		inode = ERR_CAST(record);
exit:
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	ret = d_splice_alias(inode, dentry);
	if (IS_ERR(ret))
		return ret;
#ifdef CONFIG_VDFS_QUOTA
	if (!IS_ERR_OR_NULL(inode)) {
		EMMCFS_I(inode)->quota_index = EMMCFS_I(dir)->quota_index;
		if (EMMCFS_I(inode)->quota_index == -1 &&
		    is_vdfs_inode_flag_set(inode, HAS_QUOTA)) {
			int index = get_quota(dentry);

			if (index < 0)
				return ERR_PTR(index);

			EMMCFS_I(inode)->quota_index = index;
		}
	}
#endif
	return ret;
}

static struct inode *__vdfs_iget(struct vdfs_sb_info *sbi, ino_t ino)
{
	struct inode *inode;
	struct vdfs_cattree_record *record;
	struct vdfs_btree *tree = sbi->catalog_tree;
	struct inode *image_root = NULL;
	int ret = 0;

	vdfs_assert_btree_lock(sbi->catalog_tree);
	record = vdfs_cattree_get_first_child(sbi->catalog_tree, ino);

	if (IS_ERR(record)) {
		inode = ERR_CAST(record);
		goto out;
	}

	/* it could be an installed image */
	if (record->key->record_type == VDFS_CATALOG_LLINK_RECORD) {
		struct vdfs_cattree_record *llink = record;
		__u64 catalog_id;

		record = vdfs_cattree_find(sbi->catalog_tree,
				le64_to_cpu(llink->key->object_id),
				llink->key->name,
				llink->key->name_len,
				EMMCFS_BNODE_MODE_RO);
		vdfs_release_record((struct vdfs_btree_gen_record *)llink);
		if (IS_ERR(record)) {
			inode = ERR_CAST(record);
			goto out;
		}

		if ((record->key->record_type != VDFS_CATALOG_PTREE_ROOT) &&
		    (record->key->record_type != VDFS_CATALOG_RO_IMAGE_ROOT)) {
			inode = ERR_PTR(-ENOENT);
			goto exit;
		}

		image_root = ilookup(sbi->sb,
			(unsigned long)le64_to_cpu(record->key->object_id));
		if (!image_root) {
			image_root = get_inode_from_record(record, NULL);
			if (IS_ERR(image_root)) {
				inode = image_root;
				image_root = NULL;
				goto exit;
			}
		}
		vdfs_release_record((struct vdfs_btree_gen_record *)record);
		tree = select_btree(image_root, &catalog_id);
		ino -= tree->start_ino;

		record = vdfs_cattree_get_first_child(tree, ino);
		if (IS_ERR(record)) {
			inode = ERR_CAST(record);
			goto exit;
		}
	}

again:
	if (record->key->parent_id != ino) {
		inode = ERR_PTR(-ENOENT);
		goto exit;
	}

	if (record->key->record_type == VDFS_CATALOG_ILINK_RECORD) {
		struct vdfs_cattree_record *ilink = record;
		record = vdfs_cattree_find_inode(tree,
				ino, ilink->key->object_id,
				ilink->key->name, ilink->key->name_len,
				EMMCFS_BNODE_MODE_RO);
		vdfs_release_record((struct vdfs_btree_gen_record *) ilink);
	} else if (le64_to_cpu(record->key->object_id) == ino) {
		/* hard-link body */
	} else {
		/* it could be: first child not ilink */
		ret = vdfs_get_next_btree_record(
				(struct vdfs_btree_gen_record *) record);
		if (ret) {
			inode = ERR_PTR(ret);
			goto out;
		}
		goto again;
	}

	inode = get_inode_from_record(record, image_root);
exit:
	iput(image_root);
	vdfs_release_record((struct vdfs_btree_gen_record *)record);
out:
	return inode;
}

/*
 * @brief	Lookup inode by number
 */
struct inode *vdfs_iget(struct vdfs_sb_info *sbi, ino_t ino)
{
	struct inode *inode;

	inode = ilookup(sbi->sb, ino);
	if (!inode) {
		mutex_r_lock(sbi->catalog_tree->rw_tree_lock);
		inode = __vdfs_iget(sbi, ino);
		mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	}
	return inode;
}

/**
 * @brief		Get free inode index[es].
 * @param [in]	sbi	Pointer to superblock information
 * @param [out]	i_ino	Resulting inode number
 * @param [in]	count	Requested inode numbers count.
 * @return		Returns 0 if success, err code if fault
 */
int vdfs_get_free_inode(struct vdfs_sb_info *sbi, ino_t *i_ino,
		unsigned int count)
{
	struct page *page = NULL;
	void *data;
	__u64 last_used = atomic64_read(&sbi->free_inode_bitmap.last_used);
	pgoff_t page_index = (pgoff_t)last_used;
	/* find_from is modulo, page_index is result of div */
	__u64 find_from = do_div(page_index, VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN));
	/* first id on the page. */
	__u64 id_offset = page_index * VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN);
	/* bits per block */
	unsigned int data_size = VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN);
	int err = 0;
	int pass = 0;
	pgoff_t start_page = page_index;
	pgoff_t total_pages = (pgoff_t)VDFS_LAST_TABLE_INDEX(sbi,
			VDFS_FREE_INODE_BITMAP_INO) + 1;
	*i_ino = 0;

	if (count > data_size)
		return -ENOMEM; /* todo we can allocate inode numbers chunk
		 only within one page*/

	while (*i_ino == 0) {
		unsigned long *addr;
		page = vdfs_read_or_create_page(sbi->free_inode_bitmap.inode,
				page_index, VDFS_META_READ);
		if (IS_ERR_OR_NULL(page))
			return PTR_ERR(page);
		lock_page(page);

		data = kmap(page);
		addr = (void *)((char *)data + INODE_BITMAP_MAGIC_LEN);
		*i_ino = bitmap_find_next_zero_area(addr,
				data_size,
				(unsigned long)find_from,
				count, 0);
		/* free area is found */
		if ((unsigned int)(*i_ino + count - 1) < data_size) {
			EMMCFS_BUG_ON(*i_ino + id_offset < VDFS_1ST_FILE_INO);
			if (count > 1) {
				bitmap_set(addr, (int)*i_ino, (int)count);
			} else {
				EMMCFS_BUG_ON(test_and_set_bit((int)*i_ino,
							addr));
			}
			*i_ino += (ino_t)id_offset;
			if (atomic64_read(&sbi->free_inode_bitmap.last_used) <
				(*i_ino  + (ino_t)count - 1lu))
				atomic64_set(&sbi->free_inode_bitmap.last_used,
					*i_ino + (ino_t)count - 1lu);

			vdfs_add_chunk_bitmap(sbi, page, 1);
		} else { /* if no free bits in current page */
			struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;
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
			id_offset = page_index * (pgoff_t)data_size;
			/* if it's first page, increase the inode generation */
			if (page_index == 0) {
				/* for first page we should look up from
				 * EMMCFS_1ST_FILE_INO bit*/
				atomic64_set(&sbi->free_inode_bitmap.last_used,
					VDFS_1ST_FILE_INO);
				find_from = VDFS_1ST_FILE_INO;
				/* increase generation of the inodes */
				le32_add_cpu(&(vdfs_sb->exsb.generation), 1);
				vdfs_dirty_super(sbi);
			} else
				find_from = 0;
			EMMCFS_DEBUG_INO("move to next page"
				" ind = %lu, id_off = %llu, data = %d\n",
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
int vdfs_free_inode_n(struct vdfs_sb_info *sbi, ino_t inode_n, int count)
{
	void *data;
	struct page *page = NULL;
	__u64 page_index = inode_n;
	/* offset inside page */
	__u32 int_offset = do_div(page_index, VDFS_BIT_BLKSIZE(PAGE_SIZE,
			INODE_BITMAP_MAGIC_LEN));

	page = vdfs_read_or_create_page(sbi->free_inode_bitmap.inode,
			(pgoff_t)page_index, VDFS_META_READ);
	if (IS_ERR_OR_NULL(page))
		return PTR_ERR(page);

	lock_page(page);
	data = kmap(page);
	for (; count; count--)
		if (!test_and_clear_bit((long)int_offset + count - 1,
			(void *)((char *)data + INODE_BITMAP_MAGIC_LEN))) {
			EMMCFS_DEBUG_INO("emmcfs_free_inode_n %lu"
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
	int ret = 0;

	ret = vdfs_check_permissions(inode, 1);
	if (ret)
		return ret;
	/* packtree removal only through install.vdfs -u packtree_root_dir */
	if (EMMCFS_I(dir)->record_type >= VDFS_CATALOG_PTREE_RECORD)
		return -EPERM;
	/* a inodes from installed vdfs images and the installation point
	 * can not be removed via unlink(). In order to remove installed files
	 * and folders an user must close all files from installed image
	 * and call  tune.vdfs -u [install point]
	 */
	if (EMMCFS_I(inode)->installed_btrees)
		return -EPERM;

	EMMCFS_DEBUG_INO("unlink '%s', ino = %lu", dentry->d_iname,
			inode->i_ino);

	if (!inode->i_nlink) {
		EMMCFS_ERR("inode #%lu has no links left!", inode->i_ino);
		return -EFAULT;
	}

	vdfs_start_transaction(sbi);
	vdfs_assert_i_mutex(inode);
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	drop_nlink(inode);
	EMMCFS_BUG_ON(inode->i_nlink > VDFS_LINK_MAX);

	if (is_vdfs_inode_flag_set(inode, HARD_LINK)) {
		/* remove hard-link reference */
		ret = vdfs_cattree_remove(sbi->catalog_tree, inode->i_ino,
				dir->i_ino, dentry->d_name.name,
				dentry->d_name.len,
				VDFS_CATALOG_HLINK_RECORD);
		if (ret)
			goto exit_inc_nlink;
	} else if (inode->i_nlink) {
		EMMCFS_ERR("inode #%lu has nlink=%u but it's not a hardlink!",
				inode->i_ino, inode->i_nlink + 1);
		ret = -EFAULT;
		goto exit_inc_nlink;
	}

	if (inode->i_nlink) {
		inode->i_ctime = emmcfs_current_time(dir);
		goto keep;
	}

	if (is_dlink(inode)) {
		struct inode *data_inode = EMMCFS_I(inode)->data_link.inode;

		/*
		 * This is third i_mutex in the stack: parent locked as
		 * I_MUTEX_PARENT, target inode locked as I_MUTEX_NORMAL.
		 * I_MUTEX_XATTR is ok, newer kernels have more suitable
		 * I_MUTEX_NONDIR2 which is actually renamed I_MUTEX_QUOTA.
		 */
		mutex_lock_nested(&data_inode->i_mutex, I_MUTEX_XATTR);
		drop_nlink(data_inode);
		if (!data_inode->i_nlink) {
			ret = vdfs_add_to_orphan(sbi, data_inode);
			if (ret) {
				inc_nlink(data_inode);
				mutex_unlock(&data_inode->i_mutex);
				goto exit_inc_nlink;
			}

			ret = __vdfs_write_inode(sbi, data_inode);
			if (ret) {
				vdfs_fatal_error(sbi, "fail to update orphan \
						list %d", ret);
				inc_nlink(data_inode);
				mutex_unlock(&data_inode->i_mutex);
				goto exit_inc_nlink;
			}
		}
		mark_inode_dirty(data_inode);
		mutex_unlock(&data_inode->i_mutex);
	}

	ret = vdfs_add_to_orphan(sbi, inode);
	if (ret)
		goto exit_inc_nlink;
	ret = __vdfs_write_inode(sbi, inode);
	if (ret) {
		vdfs_fatal_error(sbi, "fail to update orphan list %d", ret);
		goto exit_inc_nlink;
	}
keep:
	mark_inode_dirty(inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	vdfs_assert_i_mutex(dir);
	if (dir->i_size != 0)
		dir->i_size--;
	else
		EMMCFS_DEBUG_INO("Files count mismatch");

	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	mark_inode_dirty(dir);
exit:
	vdfs_stop_transaction(sbi);
	return ret;

exit_inc_nlink:
	inc_nlink(inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	goto exit;
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
	int ret = 0;
	unsigned int pos;
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
	ret = vdfs_exttree_get_extent(sbi, inode, iblock, result);

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
	if (hint_block) {
		if (iblock == result->iblock + result->block_count)
			*hint_block = result->first_block + result->block_count;
		else
			*hint_block = 0;
	}

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
	int ret = 0;
	unsigned int pos = 0, count;

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
						extent, 1);
		if (ret)
			goto exit;

		goto update_on_disk_layout;
	} else {
		if (fork->used_extents == VDFS_EXTENTS_COUNT_IN_FORK) {
			/* 3a push out rightest extent into extents
			 * overflow btee */
			ret = emmcfs_extree_insert_extent(sbi, inode->i_ino,
				&fork->extents[VDFS_EXTENTS_COUNT_IN_FORK - 1],
				 1);
			if (ret)
				goto exit;
		} else
			fork->used_extents++;

		/*  3b. shift extents in fork to right  */
		for (count = fork->used_extents - 1lu; count > pos; count--)
			memcpy(&fork->extents[count], &fork->extents[count - 1],
						sizeof(*extent));
		memcpy(&fork->extents[pos], extent, sizeof(*extent));
	}

update_on_disk_layout:
	fork->total_block_count += extent->block_count;
	inode_add_bytes(inode, sbi->sb->s_blocksize * extent->block_count);
	if (update_bnode)
		mark_inode_dirty(inode);

exit:
	return ret;
}

void vdfs_free_reserved_space(struct inode *inode, sector_t iblocks_count)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	/* if block count is valid and fsm exist, free the space.
	 * fsm may not exist in case of umount */
	if (iblocks_count && sbi->fsm_info) {
		mutex_lock(&sbi->fsm_info->lock);
		BUG_ON(sbi->reserved_blocks_count < iblocks_count);
		sbi->reserved_blocks_count -= iblocks_count;
		sbi->free_blocks_count += iblocks_count;
#ifdef CONFIG_VDFS_QUOTA
		if (EMMCFS_I(inode)->quota_index != -1)
			sbi->quotas[EMMCFS_I(inode)->quota_index].rsv -=
				iblocks_count << sbi->block_size_shift;
#endif
		mutex_unlock(&sbi->fsm_info->lock);
	}
}

static int vdfs_reserve_space(struct inode *inode, sector_t iblocks_count)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int ret = 0;

	mutex_lock(&sbi->fsm_info->lock);
	if (sbi->free_blocks_count >= iblocks_count) {
		sbi->free_blocks_count -= iblocks_count;
		sbi->reserved_blocks_count += iblocks_count;
	} else
		ret = -ENOSPC;

#ifdef CONFIG_VDFS_QUOTA
	if (!ret && EMMCFS_I(inode)->quota_index != -1) {
		int qi = EMMCFS_I(inode)->quota_index;

		if (sbi->quotas[qi].has + sbi->quotas[qi].rsv +
		    (iblocks_count << sbi->block_size_shift) >
		    sbi->quotas[qi].max) {
			ret = -ENOSPC;
			sbi->free_blocks_count += iblocks_count;
			sbi->reserved_blocks_count -= iblocks_count;
		} else {
			sbi->quotas[qi].rsv +=
				iblocks_count << sbi->block_size_shift;
		}
	}
#endif
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
	mutex_lock(&inode_info->truncate_mutex);

	/* get extent contains iblock*/
	err = get_iblock_extent(&inode_info->vfs_inode, iblock, &extent,
			&offset_alloc_hint);

	if (err)
		goto exit;

	if (extent.first_block)
		goto done;

	if (buffer_delay(bh_result))
		goto exit;

	err = vdfs_reserve_space(inode, 1);
	if (err)
		/* not enough space to reserve */
		goto exit;
	map_bh(bh_result, inode->i_sb, VDFS_INVALID_BLOCK);
	set_buffer_new(bh_result);
	set_buffer_delay(bh_result);
	err = vdfs_runtime_extent_add(iblock, offset_alloc_hint,
			&inode_info->runtime_extents);
	if (err) {
		vdfs_free_reserved_space(inode, 1);
		goto exit;
	}
	goto exit;
done:
	res_block = extent.first_block + (iblock - extent.iblock);
	max_blocks = extent.block_count - (__u32)(iblock - extent.iblock);
	BUG_ON(res_block > extent.first_block + extent.block_count);

	if (res_block > (sector_t)(sb->s_bdev->bd_inode->i_size >>
				sbi->block_size_shift)) {
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
 * @param [in]		fsm_flags	see VDFS_FSM_*
 * @return				0 on success, or error code
 */
int vdfs_get_int_block(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create, int fsm_flags)
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

	BUG_ON(!mutex_is_locked(&inode_info->truncate_mutex));
	memset(&extent, 0x0, sizeof(extent));

	/* get extent contains iblock*/
	err = get_iblock_extent(&inode_info->vfs_inode, iblock, &extent,
			&offset_alloc_hint);

	if (err)
		goto exit;

	if (extent.first_block)
		goto done;

	if (!create)
		goto exit;

	if (fsm_flags & VDFS_FSM_ALLOC_DELAYED) {
		if(!vdfs_runtime_extent_exists(iblock,
				&inode_info->runtime_extents))
			BUG();
	} else {
		if (buffer_delay(bh_result))
			BUG();
	}
#ifdef CONFIG_VDFS_QUOTA
	if (inode_info->quota_index != -1) {
		int qi = inode_info->quota_index;

		mutex_lock(&sbi->fsm_info->lock);
		if (fsm_flags & VDFS_FSM_ALLOC_DELAYED) {
			sbi->quotas[qi].rsv -= (u64)
					(count) << sbi->block_size_shift;
		} else if (sbi->quotas[qi].has + sbi->quotas[qi].rsv +
			   ((u64)(count) << sbi->block_size_shift) >
						   sbi->quotas[qi].max) {
			err = -ENOSPC;
			mutex_unlock(&sbi->fsm_info->lock);
			goto exit_no_error_quota;
		}
		sbi->quotas[qi].has += (u64)count << sbi->block_size_shift;
		sbi->quotas[qi].dirty = 1;
		mutex_unlock(&sbi->fsm_info->lock);
	}
#endif
	extent.block_count = (u32)count;
	extent.first_block = emmcfs_fsm_get_free_block(sbi, offset_alloc_hint,
			&extent.block_count, fsm_flags);

	if (!extent.first_block) {
		err = -ENOSPC;
		goto exit;
	}

	extent.iblock = iblock;
	err = insert_extent(inode_info, &extent, 1);
	if (err) {
		fsm_flags |= VDFS_FSM_FREE_UNUSED;
		if (fsm_flags & VDFS_FSM_ALLOC_DELAYED) {
			fsm_flags |= VDFS_FSM_FREE_RESERVE;
			clear_buffer_mapped(bh_result);
		}
		emmcfs_fsm_put_free_block(inode_info, extent.first_block,
				extent.block_count, fsm_flags);
		goto exit;
	}

	if (fsm_flags & VDFS_FSM_ALLOC_DELAYED) {
		err = vdfs_runtime_extent_del(extent.iblock,
			&inode_info->runtime_extents);
		BUG_ON(err);
	}

	alloc = 1;

done:
	res_block = extent.first_block + (iblock - extent.iblock);
	max_blocks = extent.block_count - (u32)(iblock - extent.iblock);
	BUG_ON(res_block > extent.first_block + extent.block_count);

	if (res_block > (sector_t)(sb->s_bdev->bd_inode->i_size >>
				sbi->block_size_shift)) {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			EMMCFS_ERR("Block beyond block bound requested");
			err = -EFAULT;
			goto exit;
		} else {
			BUG();
		}
	}

	clear_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, res_block);
	clear_buffer_delay(bh_result);
	bh_result->b_size = sb->s_blocksize * min(max_blocks, buffer_size);

	if (alloc)
		set_buffer_new(bh_result);
	return 0;
exit:
#ifdef CONFIG_VDFS_QUOTA
	if (inode_info->quota_index != -1) {
		int qi = inode_info->quota_index;

		mutex_lock(&sbi->fsm_info->lock);
		sbi->quotas[qi].has -= (u64)count << sbi->block_size_shift;
		sbi->quotas[qi].dirty = 1;
		if (fsm_flags & VDFS_FSM_ALLOC_DELAYED)
			sbi->quotas[qi].rsv += (u64)count <<
			sbi->block_size_shift;
		mutex_unlock(&sbi->fsm_info->lock);
	}
exit_no_error_quota:
#endif
	if (err && create && (fsm_flags & VDFS_FSM_ALLOC_DELAYED))
		vdfs_fatal_error(sbi, "delayed allocation failed for "
				"inode #%lu: %d", inode->i_ino, err);
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
	int ret = 0;
	struct emmcfs_inode_info *inode_info= EMMCFS_I(inode);
	struct mutex *lock = &inode_info->truncate_mutex;
	mutex_lock(lock);
	ret = vdfs_get_int_block(inode, iblock, bh_result, create, 0);
	mutex_unlock(lock);
	return ret;
}

int vdfs_get_block_da(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create)
{
	return vdfs_get_int_block(inode, iblock, bh_result, create,
					VDFS_FSM_ALLOC_DELAYED);
}

static int vdfs_get_block_bug(struct inode *inode, sector_t iblock,
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

static int installed_vdfs_get_block(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct super_block *sb = inode->i_sb;
	struct vdfs_extent_info extent_installed, extent_image;
	__u32 max_blocks_image, max_blocks_installed, max_blocks;
	__u32 buffer_size = bh_result->b_size >> sbi->block_size_shift;
	int ret = 0;
	sector_t dummy;
	sector_t res_block, image_iblock;

	BUG_ON(create);

	memset(&extent_installed, 0x0, sizeof(extent_installed));
	memset(&extent_image, 0x0, sizeof(extent_image));

	/* get extent contains iblock*/
	ret = get_iblock_extent(&inode_info->vfs_inode, iblock,
			&extent_installed, &dummy);
	if (ret)
		return ret;

	image_iblock = extent_installed.first_block + (sector_t)(iblock -
			extent_installed.iblock);

	ret = get_iblock_extent(inode_info->installed_btrees->cat_tree->inode,
			image_iblock, &extent_image, &dummy);
	if (ret)
		return ret;

	if (!extent_image.first_block)
		return -EINVAL;

	res_block = extent_image.first_block + (image_iblock -
			extent_image.iblock);

	max_blocks_image = extent_image.block_count - (u32)(image_iblock -
			extent_image.iblock);
	max_blocks_installed = extent_installed.block_count - (u32)(iblock -
			extent_installed.iblock);
	max_blocks = min(max_blocks_image, max_blocks_installed);

	clear_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, res_block);
	bh_result->b_size = sb->s_blocksize * min(max_blocks, buffer_size);

	return 0;
}
#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED

static int vdfs_access_remote_vm(struct mm_struct *mm,
		unsigned long addr, void *buf, int len)
{
	struct vm_area_struct *vma;
	void *old_buf = buf;
	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes = 0, ret, offset;
		void *maddr;
		struct page *page = NULL;

		ret = get_user_pages(NULL, mm, addr, 1,
				0, 1, &page, &vma);
		if (ret > 0) {
			bytes = len;
			offset = addr & (PAGE_SIZE-1);
			if (bytes > (int)PAGE_SIZE - offset)
				bytes = (int)PAGE_SIZE - offset;

			maddr = kmap(page);
			copy_from_user_page(vma, page, addr,
					buf, (char *)maddr + offset,
					(size_t)bytes);
			kunmap(page);
			page_cache_release(page);
		} else
			break;
		len -= bytes;
		buf = (char *)buf  + bytes;
		addr += (unsigned)bytes;
	}
	return (char *)buf - (char *)old_buf;
}

static unsigned int get_pid_cmdline(struct mm_struct *mm, char *buffer)
{
	unsigned int res = 0, len = mm->arg_end - mm->arg_start;

	if (len > PAGE_SIZE)
		len = PAGE_SIZE;
	res = (unsigned int)vdfs_access_remote_vm(mm,
			mm->arg_start, buffer, (int)len);

	if (res > 0 && buffer[res-1] != '\0' && len < PAGE_SIZE) {
		len = strnlen(buffer, res);
		if (len < res) {
			res = len;
		} else {
			len = mm->env_end - mm->env_start;
			if (len > PAGE_SIZE - res)
				len = PAGE_SIZE - res;
			res += (unsigned int)
				vdfs_access_remote_vm(mm, mm->env_start,
					buffer+res, (int)len);
			res = strnlen(buffer, res);
		}
	}
	return res;
}
/*
 * Returns true if currant task cannot read this inode
 * because it's alowed to read only authenticated files.
 */
static int current_reads_only_authenticated(struct inode *inode, bool mm_locked)
{
	struct task_struct *task = current;
	struct mm_struct *mm;
	int ret = 0;

	if (!S_ISREG(inode->i_mode) ||
	    !is_sbi_flag_set(VDFS_SB(inode->i_sb), VOLUME_AUTH) ||
	    is_vdfs_inode_flag_set(inode, VDFS_AUTH_FILE))
		return ret;

	mm = get_task_mm(task);
	if (!mm)
		return ret;

	if (!mm_locked)
		down_read(&mm->mmap_sem);
	if (mm->exe_file) {
		struct inode *caller = mm->exe_file->f_dentry->d_inode;

		ret = !memcmp(&caller->i_sb->s_magic, VDFS_SB_SIGNATURE,
			sizeof(VDFS_SB_SIGNATURE) - 1) &&
			is_vdfs_inode_flag_set(caller, VDFS_READ_ONLY_AUTH);
		if (ret) {
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
			if (!EMMCFS_I(inode)->informed_about_fail_read) {
#endif
			unsigned int len;
			char *buffer = kzalloc(PAGE_SIZE, GFP_KERNEL);
			pr_err("mmap is not permited for:"
					" ino - %lu: name -%s, pid - %d,"
					" Can't read "
					"non-auth data from ino - %lu,"
					" name - %s ",
					caller->i_ino, EMMCFS_I(caller)->name,
					task_pid_nr(task),
					inode->i_ino, EMMCFS_I(inode)->name);
			if (!buffer)
				goto out;
			len = get_pid_cmdline(mm, buffer);
			if (len > 0) {
				size_t i = 0;
				pr_err("Pid %d cmdline - ", task_pid_nr(task));
				for (i = 0; i <= len;
						i += strlen(buffer + i) + 1)
					pr_cont("%s ", buffer + i);
				pr_cont("\n");
			}
			kfree(buffer);
		}
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
		}
#endif
	}
out:
	if (!mm_locked)
		up_read(&mm->mmap_sem);

	mmput(mm);

	return ret;
}
#endif

/**
 * @brief		Read page function.
 * @param [in]	file	Pointer to file structure
 * @param [out]	page	Pointer to page structure
 * @return		Returns error codes
 */
static int vdfs_readpage(struct file *file, struct page *page)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(page->mapping->host);

	if (inode_info->installed_btrees)
		return mpage_readpage(page, installed_vdfs_get_block);
	else
		return mpage_readpage(page, vdfs_get_block);
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

	if (EMMCFS_I(inode)->installed_btrees)
		return mpage_readpages(mapping, pages, nr_pages,
				installed_vdfs_get_block);
	else
		return mpage_readpages(mapping, pages, nr_pages,
				vdfs_get_block);

}

static int vdfs_readpages_special(struct file *file,
		struct address_space *mapping, struct list_head *pages,
		unsigned nr_pages)
{
	BUG();
}

#define COMPR_TABLE_EXTENTS_PER_PAGE (PAGE_CACHE_SIZE \
		/ sizeof(struct vdfs_comp_extent))

int get_chunk_extent(struct emmcfs_inode_info *inode_i, pgoff_t chunk_idx,
		struct vdfs_comp_extent_info *cext)
{
	struct vdfs_comp_extent *raw_extent;
	struct page *page;
	void *data;
	pgoff_t page_idx;
	int pos;
	pgoff_t last_block;
	loff_t start;
	loff_t extent_offset;
	int ret = 0;

	extent_offset = inode_i->comp_table_start_offset +
		chunk_idx * sizeof(struct vdfs_comp_extent);

	page_idx = (pgoff_t)extent_offset >> PAGE_CACHE_SHIFT;
	pos = extent_offset & (PAGE_CACHE_SIZE - 1);

	ret = vdfs_read_comp_pages(&inode_i->vfs_inode, page_idx,
		1, &page, VDFS_FBASED_READ_M);
	if (ret)
		return ret;

	data = kmap_atomic(page);
	raw_extent = (void *)((char *)data + pos);

	if (memcmp(raw_extent->magic, VDFS_COMPR_EXT_MAGIC,
				sizeof(raw_extent->magic))) {
		EMMCFS_ERR("Wrong magic in compressed extent: #%ld %lld",
			   inode_i->vfs_inode.i_ino, (long long)extent_offset);
		ret = -EINVAL;
		goto out_unmap;
	}

	start = (pgoff_t)le64_to_cpu(raw_extent->start);
	cext->start_block = (pgoff_t)start >> (pgoff_t)PAGE_CACHE_SHIFT;
	cext->offset = start & (PAGE_CACHE_SIZE - 1);
	cext->len_bytes = (int)le32_to_cpu(raw_extent->len_bytes);
	last_block = (pgoff_t)(start + cext->len_bytes + PAGE_CACHE_SIZE - 1)
		>> PAGE_CACHE_SHIFT;
	cext->blocks_n = (int)(last_block - cext->start_block);
	cext->flags = le16_to_cpu(raw_extent->flags);
out_unmap:
	kunmap_atomic(data);

	if (!ret && (cext->len_bytes < 0 || cext->offset < 0)) {
		EMMCFS_ERR("Invalid compressed extent: #%ld %lld",
			   inode_i->vfs_inode.i_ino, (long long)extent_offset);
		ret = -EINVAL;
	}

	mark_page_accessed(page);
	page_cache_release(page);
	return ret;
}


static int get_file_descriptor(struct emmcfs_inode_info *inode_i,
		struct vdfs_comp_file_descr *descr)
{
	void *data;
	pgoff_t page_idx;
	int pos;
	loff_t descr_offset;
	int ret = 0;
	if (inode_i->comp_size < sizeof(*descr))
		return -EINVAL;
	descr_offset = inode_i->comp_size - sizeof(*descr);
	page_idx = (pgoff_t)(descr_offset >> PAGE_CACHE_SHIFT);
	pos = descr_offset & (PAGE_CACHE_SIZE - 1);

	if (PAGE_CACHE_SIZE - (descr_offset -
			((descr_offset >> PAGE_CACHE_SHIFT)
			<< PAGE_CACHE_SHIFT)) < sizeof(*descr)) {
		struct page *pages[2];
		ret = vdfs_read_comp_pages(&inode_i->vfs_inode, page_idx,
			2, pages, VDFS_FBASED_READ_M);
		if (ret)
			return ret;

		data = vmap(pages, 2, VM_MAP, PAGE_KERNEL);
		memcpy(descr, (char *)data + pos, sizeof(*descr));
		vunmap(data);
		for (page_idx = 0; page_idx < 2; page_idx++) {
			mark_page_accessed(pages[page_idx]);
			page_cache_release(pages[page_idx]);
		}
	} else {
		struct page *page;
		ret = vdfs_read_comp_pages(&inode_i->vfs_inode, page_idx,
			1, &page, VDFS_FBASED_READ_M);
		if (ret)
			return ret;
		data = kmap_atomic(page);
		memcpy(descr, (char *)data + pos, sizeof(*descr));

		kunmap_atomic(data);

		mark_page_accessed(page);
		page_cache_release(page);
	}
	return ret;
}


#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
static int get_chunk_hash(struct emmcfs_inode_info *inode_i,
		u32 chunk_idx, int comp_chunks_count, unsigned char *hash,
		struct vdfs_comp_extent_info *cext)
{
	void *data;
	pgoff_t page_idx;
	int pos;
	loff_t hash_offset;
	int ret = 0;

	hash_offset = inode_i->comp_table_start_offset +
		(unsigned int)comp_chunks_count *
		sizeof(struct vdfs_comp_extent) + VDFS_HASH_LEN * chunk_idx;
	page_idx = (pgoff_t)(hash_offset >> PAGE_CACHE_SHIFT);
	pos = hash_offset & (PAGE_CACHE_SIZE - 1);
	if (PAGE_CACHE_SIZE - (hash_offset - ((hash_offset >> PAGE_CACHE_SHIFT)
			<< PAGE_CACHE_SHIFT)) < VDFS_HASH_LEN) {
		struct page *pages[2];
		ret = vdfs_read_comp_pages(&inode_i->vfs_inode, page_idx,
			2, pages, VDFS_FBASED_READ_M);
		if (ret)
			return ret;

		data = vmap(pages, 2, VM_MAP, PAGE_KERNEL);
		memcpy(hash, (char *)data + pos, VDFS_HASH_LEN);
		vunmap(data);
		for (page_idx = 0; page_idx < 2; page_idx++) {
			mark_page_accessed(pages[page_idx]);
			page_cache_release(pages[page_idx]);
		}
	} else {
		struct page *page;
		ret = vdfs_read_comp_pages(&inode_i->vfs_inode, page_idx,
			1, &page, VDFS_FBASED_READ_M);
		if (ret)
			return ret;

		data = kmap_atomic(page);
		memcpy(hash, (char *)data + pos, VDFS_HASH_LEN);

		kunmap_atomic(data);

		mark_page_accessed(page);
		page_cache_release(page);
	}
	return ret;
}
#endif

static void copy_pages(struct page **src_pages, struct page **dst_pages,
		int src_offset, int dst_offset, unsigned long length)
{
	void *src, *dst;
	int len;
	while (length) {
		len = min(PAGE_SIZE - max(src_offset, dst_offset), length);
		src = kmap_atomic(*src_pages);
		dst = kmap_atomic(*dst_pages);
		memcpy((char *)dst + dst_offset, (char *)src + src_offset,
				(size_t)len);
		kunmap_atomic(dst);
		kunmap_atomic(src);
		length -= (unsigned long)len;
		src_offset += len;
		dst_offset += len;
		if (src_offset == PAGE_SIZE) {
			src_pages++;
			src_offset = 0;
		}
		if (dst_offset == PAGE_SIZE) {
			dst_pages++;
			dst_offset = 0;
		}
	}
}

#ifdef CONFIG_VDFS_DATA_AUTHENTICATION

static void print_path_to_object(struct vdfs_sb_info *sbi, ino_t start_ino)
{
	struct inode *inode;
	char *buffer, *new_buffer = NULL;
	int buffer_len = 1;
	const char *device = sbi->sb->s_id;

	buffer = kzalloc(1, GFP_KERNEL);
	if (!buffer)
		return;
	inode = vdfs_iget(sbi, start_ino);

	if (!IS_ERR(inode))
		do {
			size_t name_len;
			struct emmcfs_inode_info *inode_i = EMMCFS_I(inode);
			ino_t next_ino = 0;

			if (!inode_i->name) {
				iput(inode);
				break;
		}

		name_len = strlen(inode_i->name);
		new_buffer = kzalloc(name_len + 1, GFP_KERNEL);
		if (!new_buffer) {
			iput(inode);
			EMMCFS_ERR("cannot allocate memory to print a path");
			break;
		}
		memcpy(new_buffer, inode_i->name, name_len);
		new_buffer[name_len] = 0x2f;
		memcpy(new_buffer + name_len + 1, buffer, buffer_len);
		buffer_len += name_len + 1;
		kfree(buffer);
		buffer = new_buffer;
		new_buffer = NULL;
		next_ino = (ino_t)inode_i->parent_id;
		iput(inode);
		/* if next_ino == 1, next dir is root */
		if (next_ino == 1)
			break;
		inode = vdfs_iget(sbi, next_ino);
	} while (!IS_ERR(inode));

	EMMCFS_ERR("VDFS(%s) path : %s", device, buffer);

	kfree(buffer);
}

static int vdfs_check_hash_chunk(struct emmcfs_inode_info *inode_i,
		struct page **comp_pages,
		struct vdfs_comp_extent_info *cext, int comp_extent_idx)
{
	unsigned char hash_orig[VDFS_HASH_LEN];
	unsigned char hash_calc[VDFS_HASH_LEN];
	void *data = NULL;
	int ret = 0;

	data = vmap(comp_pages, (unsigned int)cext->blocks_n,
			VM_MAP, PAGE_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		return ret;
	}

	ret = calculate_hash_sha1((unsigned char *)
			((char *)data + cext->offset),
			(unsigned int)cext->len_bytes,
			hash_calc);
	vunmap(data);
	if (ret)
		return ret;
	ret = get_chunk_hash(inode_i, (u32)comp_extent_idx,
			(int)inode_i->comp_extents_n, hash_orig, cext);
	if (ret)
		return ret;
	if (memcmp(hash_orig, hash_calc, VDFS_HASH_LEN)) {
		struct vdfs_sb_info *sbi = inode_i->vfs_inode.i_sb->s_fs_info;
		EMMCFS_ERR("File based decompression - hash mismatch"
				" for inode - %lu, file name - %s, chunk - %d",
				inode_i->vfs_inode.i_ino, inode_i->name,
				comp_extent_idx);
		if (inode_i->parent_id != 1)
			print_path_to_object(sbi, inode_i->parent_id);
		ret = -EINVAL;
	}
	return ret;
}
#endif

#ifdef CONFIG_VDFS_HW2_SUPPORT
static int read_and_decompress_pages(struct inode *inode,
		const struct vdfs_comp_extent_info *cext,
		pgoff_t comp_extent_idx, int pages_per_chunk,
		struct page **dst_pages)
{
	int ret = 0;
	void *buffer = NULL;

	struct page **pages = vdfs_get_hw_buffer(inode, cext->start_block,
			&buffer, cext->blocks_n);

	if (!pages)
		return -EBUSY;

	ret = vdfs__read(inode, VDFS_FBASED_READ_C, pages,
			(unsigned int)cext->blocks_n, 0);
	if (ret)
		goto exit;

#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
	if (is_vdfs_inode_flag_set(inode, VDFS_AUTH_FILE)) {
		ret = vdfs_check_hash_chunk(EMMCFS_I(inode),
				pages, (struct vdfs_comp_extent_info *)cext,
				(int)comp_extent_idx);
		if (ret)
			goto exit;
	}
#endif
	ret = sdp_unzip_decompress_sync((char *)buffer + cext->offset,
			ALIGN(cext->len_bytes, 8), dst_pages,
			pages_per_chunk, 1);
	if (ret >= 0)
		ret = 0;
exit:
	vdfs_put_hw_buffer(pages);
	return ret;
}
#endif

static int vdfs_readpage_tuned(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct emmcfs_inode_info *inode_i = EMMCFS_I(inode);
	struct page **comp_pages = NULL, **dst_pages = NULL;
	int comp_extent_idx = (int)(page->index >> (inode_i->log_chunk_size -
			PAGE_SHIFT));
	struct vdfs_comp_extent_info cext;
	bool page_locked = true;
	int ret = 0, i;
#ifdef CONFIG_VDFS_RETRY
	int retry_count = 0;
#endif
	int pages_per_chunk = 1 << (inode_i->log_chunk_size - PAGE_SHIFT);
	if (page->index >= ((i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT)) {
		clear_highpage(page);
		SetPageUptodate(page);
		goto out_kfree;
	}
	if (inode_i->compr_type == VDFS_COMPR_UNDEF) {
		EMMCFS_ERR("not initialized decompression for the file %s",
				inode_i->name);
		ret = -EINVAL;
		goto out_kfree;
	}

	ret = get_chunk_extent(inode_i, (pgoff_t)comp_extent_idx, &cext);
	if (ret)
		goto out_kfree;
	if ((cext.flags & VDFS_CHUNK_FLAG_UNCOMPR))
		pages_per_chunk = cext.blocks_n;
	/* We must lock all pages in the chunk one by one from the beginning,
	 * otherwise we might deadlock with concurrent read of other page. */

	unlock_page(page);
	page_locked = false;

	dst_pages = kmalloc(sizeof(struct page *) * (size_t)pages_per_chunk,
			GFP_KERNEL);
	if ((!dst_pages)) {
		ret = -ENOMEM;
		goto out_kfree;
	}

	if (cext.flags & VDFS_CHUNK_FLAG_UNCOMPR) {
		pgoff_t mask, index;

#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
#ifdef CONFIG_VDFS_RETRY
retry_uncompressed:
#endif
#endif
		mask = (1lu << (inode_i->log_chunk_size - PAGE_SHIFT)) - 1lu;
		index = page->index;
		index &= ~(mask);

		/* read unpacked chunk */
		ret = vdfs_read_or_create_pages(inode,
				(pgoff_t)
				(comp_extent_idx << (inode_i->log_chunk_size -
				PAGE_SHIFT)),
				(unsigned int)cext.blocks_n, dst_pages,
				VDFS_FBASED_READ_UNC, (int)cext.start_block, 0);
		if (ret)
			goto out_kfree;
#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
		if (is_vdfs_inode_flag_set(&inode_i->vfs_inode,
				VDFS_AUTH_FILE)) {
			ret = vdfs_check_hash_chunk(inode_i, dst_pages, &cext,
				comp_extent_idx);
#ifdef CONFIG_VDFS_RETRY
			if (ret && retry_count < 3) {
				retry_count++;
				EMMCFS_ERR("authentication retry %d",
						retry_count);
				for (i = 0; i < cext.blocks_n; i++) {
					lock_page(dst_pages[i]);
					ClearPageUptodate(dst_pages[i]);
					ClearPageChecked(dst_pages[i]);
					page_cache_release(dst_pages[i]);
					unlock_page(dst_pages[i]);
				}
				goto retry_uncompressed;
			}
#endif
		}
#endif
		goto out_uncompressed;
	}

	for (i = 0; i < pages_per_chunk; i++) {
		pgoff_t index;

		index = (pgoff_t)comp_extent_idx << (inode_i->log_chunk_size -
				PAGE_SHIFT);

		dst_pages[i] = find_or_create_page(inode_i->vfs_inode.i_mapping,
				index + (pgoff_t)i, GFP_NOFS);
		if (!dst_pages[i]) {
			while (--i >= 0) {
				unlock_page(dst_pages[i]);
				page_cache_release(dst_pages[i]);
			}
			ret = -ENOMEM;
			goto out_kfree;
		}
	}

	/* Somebody already read it for us */
	if (PageUptodate(page)) {
		for (i = 0; i < pages_per_chunk; i++) {
			unlock_page(dst_pages[i]);
			page_cache_release(dst_pages[i]);
		}
		ret = 0;
		goto out_kfree;
	}


#ifdef CONFIG_VDFS_HW2_SUPPORT
	if (inode_i->compr_type == VDFS_COMPR_GZIP &&
		pages_per_chunk == VDFS_HW_COMPR_PAGE_PER_CHUNK) {
		ret = read_and_decompress_pages(inode, &cext,
				(pgoff_t)comp_extent_idx,
				pages_per_chunk, dst_pages);
		if (!ret)
			goto out_dst_pages;
	}

#endif

	/* read and unpack packed chunk */
	/* Data might be uncompressed and unaligned to blocks.
	 * Thats why comp_pages has to have 2 extra pages.     */
	comp_pages = kmalloc(sizeof(struct page *) *
			((unsigned long)pages_per_chunk + 2lu), GFP_KERNEL);
	if ((!comp_pages)) {
		ret = -ENOMEM;
		goto out_kfree;
	}
#ifdef CONFIG_VDFS_RETRY
again:
#endif
	ret = vdfs_read_comp_pages(inode, cext.start_block,
			cext.blocks_n, comp_pages, VDFS_FBASED_READ_C);
	if (ret)
		goto out_dst_pages;
#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
	if (is_vdfs_inode_flag_set(&inode_i->vfs_inode, VDFS_AUTH_FILE)) {
		ret = vdfs_check_hash_chunk(inode_i, comp_pages, &cext,
				comp_extent_idx);
#ifdef CONFIG_VDFS_RETRY
		if (ret && retry_count < 3) {
			retry_count++;
			EMMCFS_ERR("authentication retry %d", retry_count);
			goto again;
		} else
#endif
		if (ret)
			goto out_dst_pages;
	}
#endif
	ret = inode_i->decompressor_fn(comp_pages, dst_pages, cext.blocks_n,
		pages_per_chunk, cext.offset, cext.len_bytes);

	if (ret == -EIO) {
#ifdef CONFIG_VDFS_DEBUG
		dump_fbc_error(comp_pages, cext.blocks_n, cext.len_bytes, &cext,
				comp_extent_idx, inode_i);
#endif
#ifdef CONFIG_VDFS_RETRY
		if (retry_count < 3) {
			retry_count++;
			EMMCFS_ERR("decomression retry %d", retry_count);
			for (i = 0; i < cext.blocks_n; i++) {
				/* we are going to re-read data from flash */
				lock_page(comp_pages[i]);
				ClearPageUptodate(comp_pages[i]);
				ClearPageChecked(comp_pages[i]);
				page_cache_release(comp_pages[i]);
				unlock_page(comp_pages[i]);
			}
			goto again;
		}

#endif
	}



	for (i = 0; i < cext.blocks_n; i++) {
		mark_page_accessed(comp_pages[i]);
		page_cache_release(comp_pages[i]);
	}

out_dst_pages:
	for (i = 0; i < pages_per_chunk; i++) {
		if (!ret)
			SetPageUptodate(dst_pages[i]);
		unlock_page(dst_pages[i]);
	}
out_uncompressed:
	for (i = 0; i < pages_per_chunk; i++)
		page_cache_release(dst_pages[i]);
#ifdef CONFIG_VDFS_RETRY
	if (retry_count && (retry_count != 3))
		EMMCFS_ERR("decomression retry successfully done");
#endif

out_kfree:
	kfree(dst_pages);
	kfree(comp_pages);
	if (page_locked)
		unlock_page(page);
	return ret;
}

static int vdfs_data_link_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct inode *data_inode = EMMCFS_I(inode)->data_link.inode;
	u64 offset = EMMCFS_I(inode)->data_link.offset;
	pgoff_t index = page->index + (pgoff_t)(offset >> PAGE_CACHE_SHIFT);
	struct page *data[2];

	data[0] = read_mapping_page(data_inode->i_mapping, index, NULL);
	if (IS_ERR(data[0]))
		goto err0;
	if (offset % PAGE_CACHE_SIZE) {
		data[1] = read_mapping_page(data_inode->i_mapping,
					    index + 1, NULL);
		if (IS_ERR(data[1]))
			goto err1;
	}
	copy_pages(data, &page, offset % PAGE_CACHE_SIZE, 0, PAGE_CACHE_SIZE);
	if (offset % PAGE_CACHE_SIZE)
		page_cache_release(data[1]);
	page_cache_release(data[0]);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;

err1:
	page_cache_release(data[0]);
	data[0] = data[1];
err0:
	unlock_page(page);
	return PTR_ERR(data[0]);
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

	BUG_ON(inode->i_ino <= VDFS_LSFILE);

	if (!page_has_buffers(page))
		goto redirty_page;

	bh = page_buffers(page);
	if ((!buffer_mapped(bh) || buffer_delay(bh)) && buffer_dirty(bh))
		goto redirty_page;

	ret = block_write_full_page(page, vdfs_get_block_bug, wbc);
#if defined(CONFIG_VDFS_DEBUG)
	if (ret)
		EMMCFS_ERR("err = %d, ino#%lu name=%s, page index: %lu, "
				" wbc->sync_mode = %d", ret, inode->i_ino,
				EMMCFS_I(inode)->name, page->index,
				wbc->sync_mode);
#endif
	return ret;
redirty_page:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
}

static int vdfs_allocate_space(struct emmcfs_inode_info *inode_info)
{
	struct list_head *ptr;
	struct list_head *next;
	struct vdfs_runtime_extent_info *entry;
	struct inode *inode = &inode_info->vfs_inode;
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	u32 count = 0, total = 0;
	struct vdfs_extent_info extent;
	int err = 0;

	if (list_empty(&inode_info->runtime_extents))
		return 0;
	memset(&extent, 0x0, sizeof(extent));

	mutex_lock(&inode_info->truncate_mutex);

	list_for_each_safe(ptr, next, &inode_info->runtime_extents) {
		entry = list_entry(ptr, struct vdfs_runtime_extent_info, list);
again:
		count = entry->block_count;

		extent.first_block = emmcfs_fsm_get_free_block(sbi, entry->
				alloc_hint, &count, VDFS_FSM_ALLOC_DELAYED);

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
				extent.first_block, extent.block_count,
				VDFS_FSM_FREE_UNUSED | VDFS_FSM_FREE_RESERVE);
			goto exit;
		}
		entry->iblock += count;
		entry->block_count -= count;
		total += count;
		/* if we still have blocks in the chunk */
		if (entry->block_count)
			goto again;
		else {
			list_del(&entry->list);
			kfree(entry);
		}
	}
exit:

	mutex_unlock(&inode_info->truncate_mutex);

#ifdef CONFIG_VDFS_QUOTA
	if (inode_info->quota_index != -1) {
		int qi = inode_info->quota_index;

		mutex_lock(&sbi->fsm_info->lock);
		sbi->quotas[qi].rsv -= total << sbi->block_size_shift;
		sbi->quotas[qi].has += total << sbi->block_size_shift;
		sbi->quotas[qi].dirty = 1;
		mutex_unlock(&sbi->fsm_info->lock);
	}
#endif
	if (!err)
		mark_inode_dirty(inode);
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
	vdfs_start_writeback(sbi);
	/* dont allocate space for packtree inodes */
	if ((inode_info->record_type == VDFS_CATALOG_FILE_RECORD) ||
		(inode_info->record_type == VDFS_CATALOG_HLINK_RECORD)) {
		/* if we have runtime extents, allocate space on volume*/
		if (!list_empty(&inode_info->runtime_extents))
			ret = vdfs_allocate_space(inode_info);
	}
	blk_start_plug(&plug);
	/* write dirty pages */
	ret = write_cache_pages(mapping, wbc, vdfs_mpage_writepage, &mpd);
	if (mpd.bio)
		vdfs_mpage_bio_submit(WRITE, mpd.bio);
	blk_finish_plug(&plug);
	vdfs_update_quota(inode);
	vdfs_stop_writeback(sbi);
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
	return vdfs_mpage_writepages(mapping, wbc);
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
	rc = block_write_begin(mapping, pos, len, flags, pagep,
		vdfs_get_block_prep_da);

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
	int ret;

	ret = block_write_end(file, mapping, pos, len, copied, page, fsdata);
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold i_mutex.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page write out could otherwise come in and zero beyond i_size.
	 */
	if (pos + (loff_t)ret > inode->i_size) {
		i_size_write(inode, pos + copied);
		i_size_changed = 1;
	}

	unlock_page(page);
	page_cache_release(page);

	if (i_size_changed)
		mark_inode_dirty(inode);
	vdfs_stop_transaction(VDFS_SB(inode->i_sb));
	return ret;
}

/**
 * @brief		Called during file opening process.
 * @param [in]	inode	Pointer to inode information
 * @param [in]	file	Pointer to file structure
 * @return		Returns error codes
 */
static int vdfs_file_open(struct inode *inode, struct file *filp)
{
	int rc = 0;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	mutex_lock(&inode->i_mutex);
	if ((is_vdfs_inode_flag_set(inode, VDFS_COMPRESSED_FILE))
			&&
			(filp->f_flags &
			(O_CREAT | O_TRUNC | O_WRONLY | O_RDWR))) {
		rc = -EPERM;
		goto unlock_exit;
	}

	if (inode_info->installed_btrees) {
		atomic_inc(&(inode_info->installed_btrees->open_count));
		if (!vdfs_is_tree_alive(inode)) {
			atomic_dec(&(inode_info->installed_btrees->open_count));
			rc = -ENOENT;
			goto unlock_exit;
		}
	}

	rc = generic_file_open(inode, filp);
	if (rc) {
		if (inode_info->installed_btrees)
			atomic_dec(&(inode_info->installed_btrees->open_count));
		goto unlock_exit;
	}

	if (!inode_info->installed_btrees)
		atomic_inc(&(inode_info->open_count));

unlock_exit:
	mutex_unlock(&inode->i_mutex);
	return rc;
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
	if (inode_info->installed_btrees)
		atomic_dec(&(inode_info->installed_btrees->open_count));
	else
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
static ssize_t emmcfs_direct_IO(int rw, struct kiocb *iocb,
		const struct iovec *iov, loff_t offset, unsigned long nr_segs)
{
	ssize_t rc, inode_new_size = 0;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode->i_mapping->host;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);

	if (rw)
		vdfs_start_transaction(sbi);
	rc = blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs,
			vdfs_get_block);
	if (!rw)
		return rc;

	vdfs_assert_i_mutex(inode);

	if (!IS_ERR_VALUE(rc)) { /* blockdev_direct_IO successfully finished */
		if ((offset + rc) > i_size_read(inode))
			/* last accessed byte behind old inode size */
			inode_new_size = (ssize_t)(offset) + rc;
	} else if (EMMCFS_I(inode)->fork.total_block_count >
			DIV_ROUND_UP(i_size_read(inode), VDFS_BLOCK_SIZE))
		/* blockdev_direct_IO finished with error, but some free space
		 * allocations for inode may have occured, inode internal fork
		 * changed, but inode i_size stay unchanged. */
		inode_new_size =
			(ssize_t)EMMCFS_I(inode)->fork.total_block_count <<
			sbi->block_size_shift;

	if (inode_new_size) {
		i_size_write(inode, inode_new_size);
		mark_inode_dirty(inode);
	}

	vdfs_update_quota(inode);
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

	if (is_vdfs_inode_flag_set(inode, VDFS_COMPRESSED_FILE)) {
		EMMCFS_ERR("Truncating compressed file is depricated");
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

	if (newsize < oldsize)
		error = vdfs_truncate_blocks(inode, newsize);
	if (error)
		return error;

	i_size_write(inode, newsize);
	inode->i_mtime = inode->i_ctime = emmcfs_current_time(inode);

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
	if (EMMCFS_I(inode)->installed_btrees)
		return -EPERM;

	vdfs_start_transaction(VDFS_SB(inode->i_sb));
	error = inode_change_ok(inode, iattr);
	if (error)
		goto exit;

	vdfs_assert_i_mutex(inode);

	if ((iattr->ia_valid & ATTR_SIZE) &&
			iattr->ia_size != i_size_read(inode)) {
		error = vdfs_truncate_pages(inode, iattr->ia_size);
		if (error)
			goto exit;

		truncate_pagecache(inode, inode->i_size, iattr->ia_size);

		error = vdfs_update_inode(inode, iattr->ia_size);
		if (error)
			goto exit;
	}

	setattr_copy(inode, iattr);

	mark_inode_dirty(inode);

#ifdef CONFIG_VDFS_POSIX_ACL
	if (iattr->ia_valid & ATTR_MODE)
		error = vdfs_chmod_acl(inode);
#endif

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
	struct vdfs_sb_info *sbi = old_dir->i_sb->s_fs_info;
	struct inode *mv_inode = old_dentry->d_inode;
	struct vdfs_cattree_record *record;
	char *saved_name = NULL;
	u8 record_type;
	int ret;

	ret = vdfs_check_permissions(mv_inode, 1);
	if (ret)
		return ret;
	if (new_dentry->d_name.len > VDFS_FILE_NAME_LEN)
		return -ENAMETOOLONG;

	vdfs_start_transaction(sbi);

	if (new_dentry->d_inode) {
		if (S_ISDIR(new_dentry->d_inode->i_mode))
			ret = emmcfs_rmdir(new_dir, new_dentry);
		else
			ret = vdfs_unlink(new_dir, new_dentry);
		if (ret)
			goto exit;
	}

	/*
	 * mv_inode->i_mutex is not always locked here, but this seems ok.
	 * We have source/destination dir i_mutex and catalog_tree which
	 * protects everything.
	 */

	/* Find old record */
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock");
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock succ");

	if (!is_vdfs_inode_flag_set(mv_inode, HARD_LINK)) {
		saved_name = kstrdup(new_dentry->d_name.name, GFP_KERNEL);
		ret = -ENOMEM;
		if (!saved_name)
			goto error;
	}

	ret = get_record_type_on_mode(mv_inode, &record_type);
	if (ret)
		goto error;

	/*
	 * Insert new record
	 */
	record = vdfs_cattree_place_record(sbi->catalog_tree, mv_inode->i_ino,
			new_dir->i_ino, new_dentry->d_name.name,
			new_dentry->d_name.len, record_type);
	if (IS_ERR(record)) {
		ret = PTR_ERR(record);
		goto error;
	}

	/*
	 * Full it just in case, writeback anyway will fill it again.
	 */
	vdfs_fill_cattree_record(mv_inode, record);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);

	/*
	 * Remove old record
	 */
	ret = vdfs_cattree_remove(sbi->catalog_tree, mv_inode->i_ino,
			old_dir->i_ino, old_dentry->d_name.name,
			old_dentry->d_name.len,
			EMMCFS_I(mv_inode)->record_type);
	if (ret)
		goto remove_record;

	if (!(is_vdfs_inode_flag_set(mv_inode, HARD_LINK))) {
		EMMCFS_I(mv_inode)->parent_id = new_dir->i_ino;
		kfree(EMMCFS_I(mv_inode)->name);
		EMMCFS_I(mv_inode)->name = saved_name;
	}

	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	mv_inode->i_ctime = emmcfs_current_time(mv_inode);
	mark_inode_dirty(mv_inode);

	vdfs_assert_i_mutex(old_dir);
	if (old_dir->i_size != 0)
		old_dir->i_size--;
	else
		EMMCFS_DEBUG_INO("Files count mismatch");
	mark_inode_dirty(old_dir);

	vdfs_assert_i_mutex(new_dir);
	new_dir->i_size++;
	mark_inode_dirty(new_dir);
exit:
	vdfs_stop_transaction(sbi);
	return ret;

remove_record:
	vdfs_cattree_remove(sbi->catalog_tree, mv_inode->i_ino, new_dir->i_ino,
			new_dentry->d_name.name, new_dentry->d_name.len,
			EMMCFS_I(mv_inode)->record_type);
error:
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	vdfs_stop_transaction(sbi);
	kfree(saved_name);
	return ret;
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

	record = vdfs_cattree_place_record(cat_tree, ino_n, par_ino_n,
			name->name, name->len, VDFS_CATALOG_HLINK_RECORD);
	if (IS_ERR(record))
		return PTR_ERR(record);

	hlink_value = (struct vdfs_catalog_hlink_record *)record->val;
	hlink_value->file_mode = cpu_to_le16(file_mode);

	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);
	return 0;
}

/**
 * @brief       Transform record from regular file to hard link
 *              Resulting record length stays unchanged, but only a part of the
 *              record is used for real data
 * */
static int transform_into_hlink(struct inode *inode)
{
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct vdfs_cattree_record *record, *hlink;
	struct vdfs_catalog_hlink_record *hlink_val;
	u8 record_type;
	int ret, val_len;

	/*
	 * Remove inode-link
	 */
	ret = vdfs_cattree_remove_ilink(sbi->catalog_tree,
			inode->i_ino, EMMCFS_I(inode)->parent_id,
			EMMCFS_I(inode)->name,
			strlen(EMMCFS_I(inode)->name));
	if (ret)
		goto out_ilink;

	get_record_type_on_mode(inode, &record_type);

	/*
	 * Insert hard-link body
	 */
	hlink = vdfs_cattree_place_record(sbi->catalog_tree,
			inode->i_ino, inode->i_ino, NULL, 0, record_type);
	ret = PTR_ERR(hlink);
	if (IS_ERR(hlink))
		goto out_hlink;

	record = vdfs_cattree_find_inode(sbi->catalog_tree,
			inode->i_ino, EMMCFS_I(inode)->parent_id,
			EMMCFS_I(inode)->name,
			strlen(EMMCFS_I(inode)->name),
			EMMCFS_BNODE_MODE_RW);
	ret = PTR_ERR(record);
	if (IS_ERR(record))
		goto out_record;

	val_len = le16_to_cpu(record->key->gen_key.record_len) -
		  le16_to_cpu(record->key->gen_key.key_len);

	memcpy(hlink->val, record->val, (size_t)val_len);
	memset(record->val, 0, (size_t)val_len);
	record->key->record_type = VDFS_CATALOG_HLINK_RECORD;
	hlink_val = record->val;
	hlink_val->file_mode = cpu_to_le16(inode->i_mode);

	EMMCFS_I(inode)->parent_id = 0;
	kfree(EMMCFS_I(inode)->name);
	EMMCFS_I(inode)->name = NULL;
	set_vdfs_inode_flag(inode, HARD_LINK);

	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) hlink);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);

	return 0;

out_record:
	/* FIXME ugly */
	vdfs_release_record((struct vdfs_btree_gen_record *) hlink);
	vdfs_cattree_remove(sbi->catalog_tree, inode->i_ino, inode->i_ino, NULL,
			0, EMMCFS_I(inode)->record_type);
out_hlink:
	vdfs_cattree_insert_ilink(sbi->catalog_tree, inode->i_ino,
			EMMCFS_I(inode)->parent_id, EMMCFS_I(inode)->name,
			strlen(EMMCFS_I(inode)->name));
out_ilink:
	return ret;
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
	struct vdfs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	vdfs_assert_i_mutex(inode);

	ret = vdfs_check_permissions(inode, 1);
	if (ret)
		return ret;

	if (dentry->d_name.len > VDFS_FILE_NAME_LEN)
		return -ENAMETOOLONG;

	if (inode->i_nlink >= VDFS_LINK_MAX)
		return -EMLINK;

	vdfs_start_transaction(sbi);
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	if (!is_vdfs_inode_flag_set(inode, HARD_LINK)) {
		ret = transform_into_hlink(inode);
		if (ret)
			goto err_exit;
	}

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

	mark_inode_dirty(inode);

	vdfs_assert_i_mutex(dir);
	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	dir->i_size++;
	mark_inode_dirty(dir);

	sbi->files_count++;
exit:
	vdfs_stop_transaction(sbi);
	return ret;
err_exit:
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	goto exit;
}

int vdfs_data_link_create(struct dentry *parent, const char *name,
		struct inode *data_inode, __u64 data_offset, __u64 data_length)
{
	struct inode *dir = parent->d_inode;
	struct vdfs_sb_info *sbi = dir->i_sb->s_fs_info;
	struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;
	struct vdfs_catalog_dlink_record *dlink;
	struct vdfs_cattree_record *record;
	ino_t ino;
	int err;

	if (dir->i_sb != data_inode->i_sb || !S_ISREG(data_inode->i_mode) ||
			is_dlink(data_inode))
		return -EBADF;

	/*
	 * The same locking madness like in sys_link sequence.
	 */
	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);
	mutex_lock(&data_inode->i_mutex);
	vdfs_start_transaction(sbi);

	err = vdfs_get_free_inode(sbi, &ino, 1);
	if (err)
		goto err_alloc_ino;

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	if (!is_vdfs_inode_flag_set(data_inode, HARD_LINK)) {
		err = transform_into_hlink(data_inode);
		if (err)
			goto err_transform;
	}

	record = vdfs_cattree_place_record(sbi->catalog_tree,
			ino, dir->i_ino, name, strlen(name),
			VDFS_CATALOG_DLINK_RECORD);
	err = PTR_ERR(record);
	if (IS_ERR(record))
		goto err_place_record;

	dlink = (struct vdfs_catalog_dlink_record *)record->val;

	dlink->common.flags = 0;
	dlink->common.file_mode = cpu_to_le16(S_IFREG |
				(data_inode->i_mode & 0555));
	dlink->common.uid = cpu_to_le32(i_uid_read(data_inode));
	dlink->common.gid = cpu_to_le32(i_gid_read(data_inode));
	dlink->common.total_items_count = cpu_to_le64(data_length);
	dlink->common.links_count = cpu_to_le64(1);
	dlink->common.creation_time = vdfs_encode_time(data_inode->i_ctime);
	dlink->common.access_time = vdfs_encode_time(data_inode->i_atime);
	dlink->common.modification_time = vdfs_encode_time(data_inode->i_mtime);
	dlink->common.generation = le32_to_cpu(vdfs_sb->exsb.generation);

	dlink->data_inode = le64_to_cpu(data_inode->i_ino);
	dlink->data_offset = le64_to_cpu(data_offset);
	dlink->data_length = le64_to_cpu(data_length);

	vdfs_release_dirty_record((struct vdfs_btree_gen_record *)record);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	inc_nlink(data_inode);
	mark_inode_dirty(data_inode);

	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	dir->i_size++;
	mark_inode_dirty(dir);

	vdfs_stop_transaction(sbi);
	mutex_unlock(&data_inode->i_mutex);
	/* prune negative dentry */
	shrink_dcache_parent(parent);
	mutex_unlock(&dir->i_mutex);

	return 0;

err_place_record:
err_transform:
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	vdfs_free_inode_n(sbi, ino, 1);
err_alloc_ino:
	vdfs_stop_transaction(sbi);
	mutex_unlock(&data_inode->i_mutex);
	mutex_unlock(&dir->i_mutex);
	return err;
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
	mark_inode_dirty(created_ino);
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
	int len = (int)strlen(symname);

	if ((len > EMMCFS_FULL_PATH_LEN) ||
			(dentry->d_name.len > VDFS_FILE_NAME_LEN))
		return -ENAMETOOLONG;

	vdfs_start_transaction(VDFS_SB(dir->i_sb));

	ret = emmcfs_create(dir, dentry, S_IFLNK | S_IRWXUGO, NULL);
	if (ret)
		goto exit;

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
	.direct_IO	= emmcfs_direct_IO,
	.migratepage	= buffer_migrate_page,
	.releasepage = vdfs_releasepage,
/*	.set_page_dirty = __set_page_dirty_buffers,*/

};

const struct address_space_operations vdfs_data_link_aops = {
	.readpage	= vdfs_data_link_readpage,
};

const struct address_space_operations vdfs_tuned_aops = {
	.readpage	= vdfs_readpage_tuned,
	/*.readpages	= vdfs_readpages_tuned,*/
	/*.migratepage	= buffer_migrate_page,*/
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static int vdfs_fail_migrate_page(struct address_space *mapping,
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
	.writepages	= vdfs_writepages_special,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	.sync_page	= block_sync_page,
#endif
	.write_begin	= emmcfs_write_begin,
	.write_end	= emmcfs_write_end,
	.bmap		= emmcfs_bmap,
	.direct_IO	= emmcfs_direct_IO,
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
	.lookup		= vdfs_lookup,
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
#ifdef VDFS_POSIX_ACL
	.get_acl	= vdfs_get_acl,
#endif
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
	.llseek		= vdfs_llseek_dir,
	.read		= generic_read_dir,
	.readdir	= emmcfs_readdir,
	.release	= vdfs_release_dir,
	.unlocked_ioctl = vdfs_dir_ioctl,
};


/**
 * This writes unwitten data and metadata for one file ... and everything else.
 * It's impossible to flush single inode without flushing all changes in trees.
 */
static int vdfs_file_fsync(struct file *file, loff_t start, loff_t end,
		int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct super_block *sb = inode->i_sb;
	int ret;

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;

	if (!datasync || (inode->i_state & I_DIRTY_DATASYNC)) {
		down_read(&sb->s_umount);
		ret = sync_filesystem(sb);
		up_read(&sb->s_umount);
	}

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
int __vdfs_file_fsync(struct file *file, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	int ret;

	/* In old kernels fsync is called under i_mutex, we don't need it. */
	mutex_unlock(&inode->i_mutex);
	ret = vdfs_file_fsync(file, 0, LLONG_MAX, datasync);
	mutex_lock(&inode->i_mutex);

	return ret;
}
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

	ret = vdfs_check_permissions(inode, 1);
	if (ret)
		return ret;

	/* We are trying to write iocb->ki_left bytes from iov->iov_base */
	ret = generic_file_aio_write(iocb, iov, nr_segs, pos);

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

	ret = vdfs_check_permissions(inode, 0);
	if (ret)
		return ret;

#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED
	if (current_reads_only_authenticated(inode, false)) {
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
		if (!EMMCFS_I(inode)->informed_about_fail_read)
#endif
			pr_err("read is not permited: %lu:%s",
				inode->i_ino, EMMCFS_I(inode)->name);
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
		EMMCFS_I(inode)->informed_about_fail_read = 1;
#else
		return -EPERM;
#endif
	}
#endif

	ret = generic_file_aio_read(iocb, iov, nr_segs, pos);
#if defined(CONFIG_VDFS_DEBUG)
	if (ret < 0 && ret != -EIOCBQUEUED && ret != -EINTR)
		EMMCFS_ERR("err = %d, ino#%lu name=%s",
			(int)ret, inode->i_ino, EMMCFS_I(inode)->name);
#endif
	return ret;
}

static ssize_t vdfs_file_splice_read(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED
	struct inode *inode = in->f_mapping->host;

	if (current_reads_only_authenticated(inode, false)) {
		pr_err("read is not permited:  %lu:%s",
				inode->i_ino, EMMCFS_I(inode)->name);
#ifndef CONFIG_VDFS_DEBUG_AUTHENTICAION
		return -EPERM;
#endif
	}
#endif

	return generic_file_splice_read(in, ppos, pipe, len, flags);
}


#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED
static int check_execution_available(struct inode *inode,
		struct vm_area_struct *vma)
{
	if (!is_sbi_flag_set(VDFS_SB(inode->i_sb), VOLUME_AUTH))
		return 0;
	if (!is_vdfs_inode_flag_set(inode, VDFS_AUTH_FILE)) {
		if (vma->vm_flags & VM_EXEC) {
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
			if (!EMMCFS_I(inode)->informed_about_fail_read) {
				EMMCFS_I(inode)->informed_about_fail_read = 1;
				pr_err("Try to execute non-auth file %lu:%s",
						inode->i_ino,
						EMMCFS_I(inode)->name);
			}
		}
#else
			EMMCFS_ERR("Try to execute non-auth file %lu:%s",
				inode->i_ino,
				EMMCFS_I(inode)->name);
			return -EPERM;
		}
		/* Forbid remmaping to executable */
		vma->vm_flags &= (unsigned long)~VM_MAYEXEC;
#endif
	}

	if (current_reads_only_authenticated(inode, true))
#ifndef CONFIG_VDFS_DEBUG_AUTHENTICAION
		return -EPERM;
#else
		return 0;
#endif

	return 0;
}
#endif

static int vdfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED
	struct inode *inode = file->f_dentry->d_inode;
	int ret = check_execution_available(inode, vma);
	if (ret)
		return ret;
#endif
	return generic_file_mmap(file, vma);
}

static int vdfs_file_readonly_mmap(struct file *file,
		struct vm_area_struct *vma)
{
#ifdef CONFIG_VDFS_EXEC_ONLY_AUTHENTICATED
	struct inode *inode = file->f_dentry->d_inode;
	int ret = check_execution_available(inode, vma);
	if (ret)
		return ret;
#endif
	return generic_file_readonly_mmap(file, vma);
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
	.mmap		= vdfs_file_mmap,
	.splice_read	= vdfs_file_splice_read,
	.open		= vdfs_file_open,
	.release	= vdfs_file_release,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	.fsync		= vdfs_file_fsync,
#else
	.fsync		= __vdfs_file_fsync,
#endif
	.unlocked_ioctl = vdfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vdfs_compat_ioctl,
#endif
#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
	LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	.fallocate	= emmcfs_fallocate,
#endif
};

static const struct file_operations vdfs_tuned_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= vdfs_file_aio_read,
	.mmap		= vdfs_file_readonly_mmap,
	.splice_read	= vdfs_file_splice_read,
	.open		= vdfs_file_open,
	.release	= vdfs_file_release,
	.fsync		= vdfs_file_fsync,
	.unlocked_ioctl = vdfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vdfs_compat_ioctl,
#endif
};
const struct inode_operations vdfs_special_inode_operations = {
	.setattr	= emmcfs_setattr,
	.setxattr	= vdfs_setxattr,
	.getxattr	= vdfs_getxattr,
	.listxattr	= vdfs_listxattr,
	.removexattr	= vdfs_removexattr,
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

static int vdfs_fill_inode(struct inode *inode,
		struct vdfs_catalog_folder_record *folder_val)
{
	int ret = 0;

	EMMCFS_I(inode)->flags = le32_to_cpu(folder_val->flags);
	vdfs_set_vfs_inode_flags(inode);

	atomic_set(&(EMMCFS_I(inode)->open_count), 0);

	inode->i_mode = le16_to_cpu(folder_val->file_mode);
	i_uid_write(inode, le32_to_cpu(folder_val->uid));
	i_gid_write(inode, le32_to_cpu(folder_val->gid));
	set_nlink(inode, (unsigned int)le64_to_cpu(folder_val->links_count));
	inode->i_generation = le32_to_cpu(folder_val->generation);
	EMMCFS_I(inode)->next_orphan_id =
		le64_to_cpu(folder_val->next_orphan_id);

	inode->i_mtime = vdfs_decode_time(folder_val->modification_time);
	inode->i_atime = vdfs_decode_time(folder_val->access_time);
	inode->i_ctime = vdfs_decode_time(folder_val->creation_time);

	if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &emmcfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &emmcfs_aops;
		inode->i_fop = &emmcfs_file_operations;

	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &emmcfs_file_inode_operations;
		inode->i_mapping->a_ops = &emmcfs_aops;
		inode->i_fop = &emmcfs_file_operations;

	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_size = (loff_t)le64_to_cpu(
				folder_val->total_items_count);
		inode->i_op = &emmcfs_dir_inode_operations;
		inode->i_fop = &emmcfs_dir_operations;
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
			S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		inode->i_mapping->a_ops = &emmcfs_aops;
		init_special_inode(inode, inode->i_mode,
			(dev_t)le64_to_cpu(folder_val->total_items_count));
		inode->i_op = &vdfs_special_inode_operations;
	} else {
		/* UNKNOWN object type*/
		ret = -EINVAL;
	}

	return ret;
}

static enum compr_type get_comprtype_by_descr(
		struct vdfs_comp_file_descr *descr)
{
	if (!memcmp(descr->magic + 1, VDFS_COMPR_LZO_FILE_DESCR_MAGIC,
			sizeof(descr->magic) - 1))
		return VDFS_COMPR_LZO;

	if (!memcmp(descr->magic + 1, VDFS_COMPR_ZIP_FILE_DESCR_MAGIC,
			sizeof(descr->magic) - 1))
		return VDFS_COMPR_ZLIB;

	if (!memcmp(descr->magic + 1, VDFS_COMPR_GZIP_FILE_DESCR_MAGIC,
			sizeof(descr->magic) - 1))
		return VDFS_COMPR_GZIP;

	return -EINVAL;
}

static u32 calc_compext_table_crc(struct page **pages, int pages_num,
		int offset, size_t table_len)
{
	struct vdfs_comp_file_descr *descr = NULL;
	void *data;
	void *tmp_descr;
	u32 crc = 0, stored_crc;
	BUG_ON(!pages_num);
	data = vmap(pages, (unsigned int)pages_num, VM_MAP, PAGE_KERNEL);
	tmp_descr = ((char *)data + offset + table_len - sizeof(*descr));
	descr = tmp_descr;
	stored_crc = descr->crc;
	descr->crc = 0;
	crc = crc32(crc, (char *)data + offset, table_len);
	descr->crc = stored_crc;
	offset = 0;
	vunmap(data);
	return crc;
}
#ifdef CONFIG_VDFS_DATA_AUTHENTICATION

static int vdfs_verify_file_signature(struct emmcfs_inode_info *inode_i,
		struct page **pages, unsigned int pages_num,
		unsigned int extents_num, loff_t start_offset)
{
	void *data = NULL;
	int ret = 0;
	data = vmap(pages, pages_num, VM_MAP, PAGE_KERNEL);
	ret = vdfs_verify_rsa_sha1_signature((char *)data + (start_offset
			& (PAGE_SIZE - 1))
			+ extents_num * sizeof(struct vdfs_comp_extent),
			(extents_num + 1) * VDFS_HASH_LEN,
			(char *)data +
			(start_offset & (PAGE_SIZE - 1)) + extents_num *
			(sizeof(struct vdfs_comp_extent)) +
			(extents_num + 1) * VDFS_HASH_LEN);
	vunmap(data);
	if (ret) {
		EMMCFS_ERR("File based decompression RSA signature mismatch."
				"Inode number - %lu, file name - %s",
				inode_i->vfs_inode.i_ino, inode_i->name);
		ret = -EINVAL;
	}
	return ret;
}

static int vdfs_check_hash_meta(struct emmcfs_inode_info *inode_i,
		struct vdfs_comp_file_descr *descr)
{
	unsigned char hash_orig[VDFS_HASH_LEN];
	unsigned char hash_calc[VDFS_HASH_LEN];
	int ret = 0;

	ret = calculate_hash_sha1((unsigned char *)descr,
		(char *)&descr->crc - (char *)&descr->magic, hash_calc);
	if (ret)
		return ret;
	ret = get_chunk_hash(inode_i, le16_to_cpu(descr->extents_num),
			(int)le16_to_cpu(descr->extents_num), hash_orig, NULL);
	if (ret)
		return ret;
	if (memcmp(hash_orig, hash_calc, VDFS_HASH_LEN)) {
		EMMCFS_ERR("File based decompression - hash metadata"
				"mismatch for inode - %lu, file_name - %s",
				inode_i->vfs_inode.i_ino, inode_i->name);
		print_path_to_object(inode_i->vfs_inode.i_sb->s_fs_info,
				inode_i->parent_id);
		ret = -EINVAL;
	}

	return ret;
}



#endif
int init_file_decompression(struct emmcfs_inode_info *inode_i, int debug)
{
	struct inode *inode = &inode_i->vfs_inode;
	struct page **pages;
	struct vdfs_comp_file_descr descr;
	int ret = 0;
	pgoff_t start_idx;
	loff_t start_offset;
	enum compr_type compr_type;
	unsigned long table_size_bytes;
	u32 crc = 0;
	unsigned extents_num;
	unsigned int pages_num, i;
	loff_t unpacked_size;
	truncate_inode_pages(inode->i_mapping, 0);
		inode_i->comp_size = inode_i->vfs_inode.i_size;

	ret = get_file_descriptor(inode_i, &descr);
	if (ret)
		return ret;

	if ((descr.magic[0] != VDFS_COMPR_DESCR_START) &&
			(descr.magic[0] != VDFS_AUTHE_DESCR_START) &&
			(descr.magic[0] != VDFS_FORCE_DESCR_START)) {
		if (!debug)
			return -EOPNOTSUPP;
		EMMCFS_ERR("Wrong descriptor magic start %c "
				"in compressed file %s", descr.magic[0],
				inode_i->name ? inode_i->name : "<no name>");
		return -EINVAL;
	}

	if (descr.magic[0] == VDFS_FORCE_DESCR_START)
		set_vdfs_inode_flag(inode, VDFS_READ_ONLY_AUTH);

	compr_type = get_comprtype_by_descr(&descr);

	switch (compr_type) {
	case VDFS_COMPR_ZLIB:
		inode_i->decompressor_fn = unpack_chunk_zlib;
		break;
	case VDFS_COMPR_GZIP:
		inode_i->decompressor_fn = unpack_chunk_gzip;
		break;
	case VDFS_COMPR_LZO:
		inode_i->decompressor_fn = unpack_chunk_lzo;
		break;
	default:
		if (!debug)
			return -EOPNOTSUPP;
		EMMCFS_ERR("Wrong descriptor magic (%.*s) "
				"in compressed file %s",
			(int)sizeof(descr.magic), descr.magic,
			inode_i->name ? inode_i->name : "<no name>");
		return compr_type;
	}

	if (le32_to_cpu(descr.layout_version) != VDFS_COMPR_LAYOUT_VER) {
		EMMCFS_ERR("Wrong layout version %d (expected %d)",
				le32_to_cpu(descr.layout_version),
				VDFS_COMPR_LAYOUT_VER);
		return -EINVAL;
	}

	inode_i->compr_type = compr_type;

	extents_num = le16_to_cpu(descr.extents_num);
	unpacked_size = (long long)le64_to_cpu(descr.unpacked_size);

	table_size_bytes = extents_num * sizeof(struct vdfs_comp_extent) +
		sizeof(struct vdfs_comp_file_descr);

	if (descr.magic[0] == VDFS_AUTHE_DESCR_START ||
		descr.magic[0] == VDFS_FORCE_DESCR_START)
		table_size_bytes += (unsigned long)VDFS_HASH_LEN *
			(extents_num + 1lu) +
			(unsigned long)VDFS_CRYPTED_HASH_LEN;

	start_idx = (long unsigned int)((inode_i->comp_size - table_size_bytes)
			>> PAGE_CACHE_SHIFT);
	start_offset = inode_i->comp_size - table_size_bytes;
	pages_num = (pgoff_t)(((inode_i->comp_size + PAGE_CACHE_SIZE - 1) >>
			PAGE_CACHE_SHIFT)) - start_idx;


	/* Now we can now how many pages do we need, read the rest of them */

	pages = kmalloc(pages_num * sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	ret = vdfs_read_comp_pages(inode, start_idx, (int)pages_num, pages,
				VDFS_FBASED_READ_M);
	if (ret) {
		kfree(pages);
		return ret;
	}

	crc = calc_compext_table_crc(pages, (int)pages_num,
			start_offset & (PAGE_SIZE - 1), table_size_bytes);
	if (crc != le32_to_cpu(descr.crc)) {
		EMMCFS_ERR("File based decompression crc mismatch: %s",
				inode_i->name);
		ret = -EINVAL;
		goto out;
	}
	inode_i->comp_table_start_offset = start_offset;
	inode_i->comp_extents_n = (__u32)extents_num;
	inode_i->log_chunk_size = (int)le32_to_cpu(descr.log_chunk_size);

#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
	if (is_sbi_flag_set(VDFS_SB(inode->i_sb), VOLUME_AUTH) &&
			(descr.magic[0] == VDFS_AUTHE_DESCR_START ||
		descr.magic[0] == VDFS_FORCE_DESCR_START)) {
		ret = vdfs_verify_file_signature(inode_i,
				pages, pages_num, extents_num,
				start_offset);
		if (ret)
			goto out;
		ret = vdfs_check_hash_meta(inode_i, &descr);
		if (ret)
			goto out;
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
		EMMCFS_I(inode)->informed_about_fail_read = 0;
#endif
		set_vdfs_inode_flag(inode, VDFS_AUTH_FILE);
	}
#endif
	inode->i_mapping->a_ops = &vdfs_tuned_aops;
	inode->i_size = unpacked_size;
	inode->i_fop = &vdfs_tuned_file_operations;

	/* deny_write_access() */
	if (S_ISREG(inode->i_mode))
		atomic_set(&inode->i_writecount, -1);

out:
#ifdef CONFIG_VDFS_RETRY
	if (ret) {
		for (i = 0; i < pages_num; i++) {
				lock_page(pages[i]);
				ClearPageUptodate(pages[i]);
				ClearPageChecked(pages[i]);
				page_cache_release(pages[i]);
				unlock_page(pages[i]);
		}
	} else
#endif

	for (i = 0; i < pages_num; i++) {
		mark_page_accessed(pages[i]);
		page_cache_release(pages[i]);
	}
	kfree(pages);
	return ret;
}

int disable_file_decompression(struct emmcfs_inode_info *inode_i)
{
	struct inode *inode = &inode_i->vfs_inode;

	truncate_inode_pages(inode->i_mapping, 0);

	inode->i_size = inode_i->comp_size;
	inode_i->comp_extents_n = 0;
	inode_i->compr_type = VDFS_COMPR_UNDEF;
	inode->i_mapping->a_ops = &emmcfs_aops;
	inode->i_fop = &emmcfs_file_operations;

	if (S_ISREG(inode->i_mode))
		atomic_set(&inode->i_writecount, 0);

	return 0;
}

struct inode *get_inode_from_record(struct vdfs_cattree_record *record,
		struct inode *parent)
{
	struct vdfs_btree *tree =
		VDFS_BTREE_REC_I((void *) record)->rec_pos.bnode->host;
	struct vdfs_sb_info *sbi = tree->sbi;
	struct vdfs_catalog_folder_record *folder_rec = NULL;
	struct vdfs_catalog_file_record *file_rec = NULL;
	struct vdfs_cattree_record *hlink_rec = NULL;
	struct inode *inode;
	int ret = 0;
	__u64 ino;

	if (IS_ERR(record) || !record)
		return ERR_PTR(-EFAULT);

	ino = le64_to_cpu(record->key->object_id);
	if (record->key->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		if (tree->btree_type == VDFS_BTREE_PACK)
			ino += tree->start_ino;
	}

	if (tree->btree_type == VDFS_BTREE_INST_CATALOG)
		ino += tree->start_ino;

	inode = iget_locked(sbi->sb, (unsigned long)ino);

	if (!inode) {
		inode = ERR_PTR(-ENOMEM);
		goto exit;
	}

	if (!(inode->i_state & I_NEW))
		goto exit;

	/* create inode from pack tree */
	if (record->key->record_type >= VDFS_CATALOG_PTREE_RECORD) {
		ret = vdfs_read_packtree_inode(inode, record->key);
		if (ret)
			goto error_exit;
		/* to find a parent the parent_id must contains real parent
		 inode number */
#ifdef CONFIG_VDFS_QUOTA
		EMMCFS_I(inode)->quota_index = -1;
#endif

		if (EMMCFS_I(inode)->record_type != VDFS_CATALOG_PTREE_ROOT)
			EMMCFS_I(inode)->ptree.tree_info =
				EMMCFS_I(parent)->ptree.tree_info;
		unlock_new_inode(inode);
		goto exit;
	}

	/* follow hard link */
	if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		struct vdfs_btree_record_info *rec_info =
					VDFS_BTREE_REC_I((void *) record);
		struct vdfs_btree *btree = rec_info->rec_pos.bnode->host;

		hlink_rec = vdfs_cattree_find_hlink(btree,
				record->key->object_id, EMMCFS_BNODE_MODE_RO);
		if (IS_ERR(hlink_rec)) {
			ret = PTR_ERR(hlink_rec);
			hlink_rec = NULL;
			goto error_exit;
		}
		if (hlink_rec->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
			ret = -EMLINK; /* hard link to hard link? */
			goto error_exit;
		}
		record = hlink_rec;
		set_vdfs_inode_flag(inode, HARD_LINK);
	}

	EMMCFS_I(inode)->record_type = record->key->record_type;
	/* create inode from catalog tree*/
	if (record->key->record_type == VDFS_CATALOG_FILE_RECORD) {
		file_rec = record->val;
		folder_rec = &file_rec->common;
	} else if (record->key->record_type == VDFS_CATALOG_FOLDER_RECORD) {
		folder_rec =
			(struct vdfs_catalog_folder_record *)record->val;
	} else if (record->key->record_type == VDFS_CATALOG_RO_IMAGE_ROOT) {
		struct vdfs_image_install_point *install_point = record->val;
		folder_rec = &install_point->common;
	} else if (record->key->record_type == VDFS_CATALOG_DLINK_RECORD) {
		struct vdfs_catalog_dlink_record *dlink = record->val;
		struct vdfs_cattree_record *data_record;
		struct inode *data_inode;
		__u64 dlink_ino = le64_to_cpu(dlink->data_inode);

		if (tree->btree_type == VDFS_BTREE_INST_CATALOG)
			dlink_ino += tree->start_ino;

		data_inode = ilookup(sbi->sb, (unsigned long)dlink_ino);
		if (!data_inode) {
			struct vdfs_btree_record_info *rec_info =
					VDFS_BTREE_REC_I((void *) record);
			struct vdfs_btree *btree =
					rec_info->rec_pos.bnode->host;
			data_record = vdfs_cattree_find_hlink(btree,
					le64_to_cpu(dlink->data_inode),
					EMMCFS_BNODE_MODE_RO);
			if (IS_ERR(data_record)) {
				data_inode = ERR_CAST(data_record);
			} else {
				data_inode = get_inode_from_record(data_record,
						parent);
				vdfs_release_record(
				(struct vdfs_btree_gen_record *)data_record);
			}
		}
		if (IS_ERR(data_inode)) {
			ret = PTR_ERR(data_inode);
			goto error_exit;
		}
		if (!S_ISREG(data_inode->i_mode)) {
			iput(data_inode);
			ret = -EINVAL;
			goto error_exit;
		}

		folder_rec = &dlink->common;
		EMMCFS_I(inode)->data_link.inode = data_inode;
		inode->i_size = (loff_t)le64_to_cpu(dlink->data_length);
		EMMCFS_I(inode)->data_link.offset =
				le64_to_cpu(dlink->data_offset);
	} else {
		if (!is_sbi_flag_set(sbi, IS_MOUNT_FINISHED)) {
			ret = -EFAULT;
			goto error_exit;
		} else
			EMMCFS_BUG();
	}

	ret = vdfs_fill_inode(inode, folder_rec);
	if (ret)
		goto error_exit;

	if (parent && EMMCFS_I(parent)->installed_btrees)
		EMMCFS_I(inode)->installed_btrees =
				EMMCFS_I(parent)->installed_btrees;

	if (tree->btree_type == VDFS_BTREE_INST_CATALOG)
		if (S_ISREG(inode->i_mode))
			/* deny_write_access() */
			atomic_set(&inode->i_writecount, -1);


	if (file_rec && (S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode))) {
		ret = vdfs_parse_fork(inode, &file_rec->data_fork);
		if (ret)
			goto error_exit;
	}

	if (inode->i_nlink > 1 && !is_vdfs_inode_flag_set(inode, HARD_LINK)) {
		EMMCFS_ERR("inode #%lu has nlink=%u but it's not a hardlink!",
				inode->i_ino, inode->i_nlink);
		ret = -EFAULT;
		goto error_exit;
	}

	if (record->key->record_type == VDFS_CATALOG_RO_IMAGE_ROOT) {
		ret = vdfs_fill_image_root(record, inode);
		if (ret)
			goto error_exit;
	} else if (!hlink_rec) {
		char *new_name;
		struct emmcfs_cattree_key *key = record->key;

		new_name = kmalloc((size_t)key->name_len + 1lu, GFP_KERNEL);
		if (!new_name) {
			ret = -ENOMEM;
			goto error_exit;
		}

		memcpy(new_name, key->name, key->name_len);
		new_name[key->name_len] = 0;
		EMMCFS_BUG_ON(EMMCFS_I(inode)->name);
		EMMCFS_I(inode)->name = new_name;
		EMMCFS_I(inode)->parent_id = le64_to_cpu(key->parent_id);
	}

#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(inode)->quota_index = -1;
#endif
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
	EMMCFS_I(inode)->informed_about_fail_read = 0;
#endif

	if (record->key->record_type == VDFS_CATALOG_DLINK_RECORD) {
		if (is_vdfs_inode_flag_set(EMMCFS_I(inode)->data_link.inode,
				VDFS_AUTH_FILE)) {
			set_vdfs_inode_flag(inode, VDFS_AUTH_FILE);
		}
		if (is_vdfs_inode_flag_set(EMMCFS_I(inode)->data_link.inode,
				VDFS_READ_ONLY_AUTH))
			set_vdfs_inode_flag(inode, VDFS_READ_ONLY_AUTH);
		inode->i_mapping->a_ops = &vdfs_data_link_aops;
		atomic_set(&inode->i_writecount, -1); /* deny_write_access() */
	}
	if (is_vdfs_inode_flag_set(inode, VDFS_COMPRESSED_FILE)) {
#ifdef CONFIG_VDFS_RETRY
		int retry_count = 0;
retry:
#endif
		ret = init_file_decompression(EMMCFS_I(inode), 1);
		if (ret) {
#ifdef CONFIG_VDFS_RETRY
			if (retry_count < 3) {
				retry_count++;
				EMMCFS_ERR("init decompression retry %d",
						retry_count);
				goto retry;
			}
#endif
			goto error_exit;
		}
	}

	if (hlink_rec)
		vdfs_release_record((struct vdfs_btree_gen_record *)hlink_rec);

	unlock_new_inode(inode);

exit:
	if (tree->btree_type != EMMCFS_BTREE_CATALOG) /*parent - install point*/
		EMMCFS_I(inode)->parent_id += (EMMCFS_I(inode)->parent_id ==
				VDFS_ROOT_INO) ? (tree->start_ino - 1) :
						tree->start_ino;

	return inode;
error_exit:
	if (hlink_rec)
		vdfs_release_record((struct vdfs_btree_gen_record *)hlink_rec);
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
	struct vdfs_layout_sb *vdfs_sb = sbi->raw_superblock;

	err = vdfs_get_free_inode(sb->s_fs_info, &ino, 1);

	if (err)
		return ERR_PTR(err);

	/*EMMCFS_DEBUG_INO("#%lu", ino);*/
	inode = new_inode(sb);
	if (!inode) {
		err = -ENOMEM;
		goto err_exit;
	}

	inode->i_ino = ino;

	if (test_option(sbi, DMASK) && S_ISDIR(mode))
		mode = mode & (umode_t)(~sbi->dmask);

	if (test_option(sbi, FMASK) && S_ISREG(mode))
		mode = mode & (umode_t)(~sbi->fmask);

	inode_init_owner(inode, dir, mode);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	set_nlink(inode, 1);
#else
	inode->i_nlink = 1;
#endif
	inode->i_size = 0;
	inode->i_generation = le32_to_cpu(vdfs_sb->exsb.generation);
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

	return inode;
err_exit:
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
	struct vdfs_cattree_record *record;
	u8 record_type;

	ret = vdfs_check_permissions(dir, 1);
	if (ret)
		return ret;

	EMMCFS_DEBUG_INO("'%s' dir = %ld", dentry->d_name.name, dir->i_ino);

	if (dentry->d_name.len > VDFS_FILE_NAME_LEN)
		return -ENAMETOOLONG;

	saved_name = kzalloc(dentry->d_name.len + 1, GFP_KERNEL);
	if (!saved_name)
		return -ENOMEM;

	vdfs_start_transaction(sbi);
	inode = emmcfs_new_inode(dir, mode);

	if (IS_ERR(inode)) {
		kfree(saved_name);
		ret = PTR_ERR(inode);
		goto err_trans;
	}

	strncpy(saved_name, dentry->d_name.name, dentry->d_name.len + 1);

	EMMCFS_I(inode)->name = saved_name;
	EMMCFS_I(inode)->parent_id = dir->i_ino;
#ifdef CONFIG_VDFS_QUOTA
	EMMCFS_I(inode)->quota_index = -1;
#endif

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	ret = get_record_type_on_mode(inode, &record_type);
	if (ret)
		goto err_notree;
	record = vdfs_cattree_place_record(sbi->catalog_tree, inode->i_ino,
			dir->i_ino, dentry->d_name.name,
			dentry->d_name.len, record_type);
	if (IS_ERR(record)) {
		ret = PTR_ERR(record);
		goto err_notree;
	}
	vdfs_fill_cattree_record(inode, record);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *) record);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

#ifdef CONFIG_VDFS_POSIX_ACL
	ret = vdfs_init_acl(inode, dir);
	if (ret)
		goto err_unlock;
#endif

	ret = security_inode_init_security(inode, dir,
			&dentry->d_name, vdfs_init_security_xattrs, NULL);
	if (ret && ret != -EOPNOTSUPP)
		goto err_unlock;

	ret = insert_inode_locked(inode);
	if (ret)
		goto err_unlock;

	vdfs_assert_i_mutex(dir);
	dir->i_size++;
	if (S_ISDIR(inode->i_mode))
		sbi->folders_count++;
	else
		sbi->files_count++;

	dir->i_ctime = emmcfs_current_time(dir);
	dir->i_mtime = emmcfs_current_time(dir);
	mark_inode_dirty(dir);
#ifdef CONFIG_VDFS_QUOTA
	if (EMMCFS_I(dir)->quota_index != -1)
		EMMCFS_I(inode)->quota_index =
				EMMCFS_I(dir)->quota_index;
#endif
	d_instantiate(dentry, inode);
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
		EMMCFS_I(inode)->informed_about_fail_read = 0;
#endif
	unlock_new_inode(inode);
	vdfs_stop_transaction(sbi);
	return ret;

err_notree:
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	vdfs_free_inode_n(sbi, inode->i_ino, 1);
	inode->i_ino = 0;
err_unlock:
	clear_nlink(inode);
	iput(inode);
err_trans:
	vdfs_stop_transaction(sbi);
	return ret;
}

int __vdfs_write_inode(struct vdfs_sb_info *sbi, struct inode *inode)
{
	struct vdfs_cattree_record *record;

	if (is_vdfs_inode_flag_set(inode, HARD_LINK))
		record = vdfs_cattree_find_hlink(sbi->catalog_tree,
				inode->i_ino, EMMCFS_BNODE_MODE_RW);
	else
		record = vdfs_cattree_find_inode(sbi->catalog_tree,
				inode->i_ino, EMMCFS_I(inode)->parent_id,
				EMMCFS_I(inode)->name,
				strlen(EMMCFS_I(inode)->name),
				EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(record)) {
		vdfs_fatal_error(sbi, "fail to update inode %lu", inode->i_ino);
		return PTR_ERR(record);
	}
	emmcfs_fill_cattree_value(inode, record->val);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *)record);
	return 0;
}

/**
 * @brief			Write inode to bnode.
 * @param [in,out]	inode	The inode, that will be written to bnode
 * @return			Returns 0 on success, errno on failure
 */
int vdfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct super_block *sb = inode->i_sb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	int ret;

	if (inode->i_ino < VDFS_1ST_FILE_INO && inode->i_ino != VDFS_ROOT_INO)
		return 0;

	if (EMMCFS_I(inode)->record_type >= VDFS_CATALOG_PTREE_ROOT)
		return 0;

	vdfs_start_writeback(sbi);
	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	ret = __vdfs_write_inode(sbi, inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	vdfs_stop_writeback(sbi);

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
	int ret = 0;
	gfp_t gfp_mask;
	loff_t size;

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

	size = vdfs_special_file_size(sbi, ino);
	inode->i_mapping->a_ops = &emmcfs_aops_special;

	i_size_write(inode, size);

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
	EMMCFS_I(inode)->flags &= ~(1lu << (unsigned long)VDFS_IMMUTABLE);
	if (inode->i_flags & S_IMMUTABLE)
		EMMCFS_I(inode)->flags |=
			(1lu << (unsigned long)VDFS_IMMUTABLE);
}

/**
 * @brief		Set vfs inode i_flags according to
 *			EMMCFS_I(inode)->flags.
 * @param [in]	inode	Pointer to vfs inode structure.
  * @return		none.
 */
void vdfs_set_vfs_inode_flags(struct inode *inode)
{
	inode->i_flags &= ~(unsigned long)S_IMMUTABLE;
	if (EMMCFS_I(inode)->flags & (1lu << (unsigned long)VDFS_IMMUTABLE))
		inode->i_flags |= S_IMMUTABLE;
}

struct inode *vdfs_get_image_inode(struct vdfs_sb_info *sbi,
		__u64 parent_id, __u8 *name, size_t name_len)
{
	struct inode *inode;
	struct vdfs_cattree_record *record;

	mutex_r_lock(sbi->catalog_tree->rw_tree_lock);
	record = vdfs_cattree_find(sbi->catalog_tree, parent_id, name, name_len,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		/* Pass error code to return value */
		inode = (void *)record;
		goto err_exit;
	}

	inode = get_inode_from_record(record, NULL);
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
err_exit:
	mutex_r_unlock(sbi->catalog_tree->rw_tree_lock);
	return inode;
}
