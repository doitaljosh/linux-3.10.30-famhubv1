#include "emmcfs.h"
#include "debug.h"
#include "cattree.h"
/**
 * @brief		Converts VFS mode to eMMCFS record type.
 * @param [in]	mode	Mode of VFS object
 * @return		Returns record type corresponding to mode
 */


struct	vdfs_nfs_file {
	/** Node definition for inclusion in a red-black tree */
	struct rb_node node;
	/** indirect key for search */
	struct vdfs_indirect_key indirect_key;
	/** File catalog key */
	struct emmcfs_cattree_key key;
};

/**
 * @brief		Compare two keys
 * @param [in]	r_key	Rigth key to compare.
 * @param [in]	l_key	Left key to compare.
 * @return		Returns negative value if left is bigger
			,positive if left is smaller,
 *			return 0 it the records is equal.
 */
static inline int rb_compare_keys(struct vdfs_indirect_key *l_key,
		struct vdfs_indirect_key *r_key)
{
	if (l_key->generation != r_key->generation)
		return r_key->generation - l_key->generation;
	return r_key->ino - l_key->ino;
}
/**
 * @brief		Search for file.
 * @param [in]	root	Root of the tree to insert.
 * @param [in]	indirect_key	Key to search file, ino and generation.
 * @return		Returns pointer to file or NULL.
 */
static inline struct vdfs_nfs_file *rb_search_for_file(struct rb_root *root,
		struct vdfs_indirect_key *indirect_key)
{
	struct rb_node **new = &(root->rb_node);
	struct vdfs_nfs_file *this = NULL;
	int cmp_res = 0;

	/* go through all item */
	while (*new) {
		this =  rb_entry(*new, struct vdfs_nfs_file, node);

		cmp_res = rb_compare_keys(indirect_key, &this->indirect_key);
		if (cmp_res > 0)
			new = &((*new)->rb_left);
		else if (cmp_res < 0)
			new = &((*new)->rb_right);
		else
			return this;
	}
	return NULL;
}

/**
 * @brief		Add file to rb tree
 * @param [in]	root	Root of the tree
 * @param [in]	file	File to insert
 * @return		Returns 0 on success, errno on failure.
 */
static inline int rb_add_file(struct rb_root *root,
		struct vdfs_nfs_file *file)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	int cmp_res = 0;

	/* Figure out where to put new node */
	while (*new) {
		struct vdfs_nfs_file *this =  rb_entry(*new,
				struct vdfs_nfs_file, node);
		parent = *new;
		cmp_res = rb_compare_keys(&file->indirect_key,
				&this->indirect_key);
		if (cmp_res > 0)
			new = &((*new)->rb_left);
		else if (cmp_res < 0)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&file->node, parent, new);
	rb_insert_color(&file->node, root);

	return 0;
}

/**
 * @brief		Destroy element in the rb tree.
 * @param [in]	root	rb tree root node.
 * @param [in]	indirect_key key to delete
 * @return	0 if file was deleted, or -ENOENT if no such file
 * */
static inline int rb_destroy_file(struct rb_root *root,
		struct vdfs_indirect_key *indirect_key)
{
	struct vdfs_nfs_file *target;
	target = rb_search_for_file(root , indirect_key);
	if (target) {
		rb_erase(&target->node, root);
		kfree(target);
		return 0;
	} else
		return -ENOENT;
}

int get_record_type_on_mode(struct inode *inode, u8 *record_type)
{
	umode_t mode = inode->i_mode;

	if (is_vdfs_inode_flag_set(inode, HARD_LINK))
		*record_type = VDFS_CATALOG_HLINK_RECORD;
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
int emmcfs_fill_cattree_value(struct inode *inode, void *value_area)
{
	struct vdfs_catalog_folder_record *comm_rec = value_area;

	vdfs_get_vfs_inode_flags(inode);
	comm_rec->flags = cpu_to_le32(EMMCFS_I(inode)->flags);

	/* TODO - set magic */
/*	memcpy(comm_rec->magic, get_magic(le16_to_cpu(comm_rec->record_type)),
			sizeof(EMMCFS_CAT_FOLDER_MAGIC) - 1);*/
	comm_rec->permissions.file_mode = cpu_to_le16(inode->i_mode);
	comm_rec->permissions.uid = cpu_to_le32(inode->i_uid);
	comm_rec->permissions.gid = cpu_to_le32(inode->i_gid);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		comm_rec->total_items_count = cpu_to_le64(inode->i_rdev);
	else
		comm_rec->total_items_count = cpu_to_le64(inode->i_size);

	comm_rec->links_count = cpu_to_le64(inode->i_nlink);
	comm_rec->object_id = cpu_to_le64(inode->i_ino);

	comm_rec->creation_time.seconds = cpu_to_le64(inode->i_ctime.tv_sec);
	comm_rec->access_time.seconds = cpu_to_le64(inode->i_atime.tv_sec);
	comm_rec->modification_time.seconds =
			cpu_to_le64(inode->i_mtime.tv_sec);

	comm_rec->creation_time.nanoseconds =
			cpu_to_le64(inode->i_ctime.tv_nsec);
	comm_rec->access_time.nanoseconds =
			cpu_to_le64(inode->i_atime.tv_nsec);
	comm_rec->modification_time.nanoseconds =
			cpu_to_le64(inode->i_mtime.tv_nsec);
	comm_rec->generation = cpu_to_le32(inode->i_generation);

	if (S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode)) {
		struct vdfs_catalog_file_record *file_rec = value_area;
		if (is_vdfs_inode_flag_set(inode, TINY_FILE)) {
			file_rec->tiny.len = EMMCFS_I(inode)->tiny.len;
			file_rec->tiny.i_size =
				cpu_to_le64(EMMCFS_I(inode)->tiny.i_size);
			memcpy(file_rec->tiny.data, EMMCFS_I(inode)->tiny.data,
						TINY_DATA_SIZE);
		} else if (is_vdfs_inode_flag_set(inode, SMALL_FILE)) {
			file_rec->small.cell =
				cpu_to_le64(EMMCFS_I(inode)->small.cell);
			file_rec->small.len =
				cpu_to_le16(EMMCFS_I(inode)->small.len);
			file_rec->small.i_size =
				cpu_to_le64(EMMCFS_I(inode)->small.i_size);
		} else
			vdfs_form_fork(&file_rec->data_fork, inode);
	}
	return 0;
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
	hl_record->object_id = cpu_to_le64(inode->i_ino);
	hl_record->file_mode = cpu_to_le16(inode->i_mode);
}

/**
 * @brief			Add new object into catalog tree.
 * @param [in]	inode		The inode representing an object to be added
 * @param [in]	parent_id	Parent id of the object to be added
 * @param [in]	name		Name of the object to be added
 * @return			Returns 0 in case of success,
 *				error code otherwise
 */
int emmcfs_add_new_cattree_object(struct inode *inode, u64 parent_id,
		struct qstr *name)
{
	struct vdfs_sb_info	*sbi = inode->i_sb->s_fs_info;
	void *record;
	u8 record_type;
	int ret = 0;

	EMMCFS_BUG_ON(inode->i_ino < VDFS_1ST_FILE_INO);



	ret = get_record_type_on_mode(inode, &record_type);

	if (ret)
		return ret;

	record = emmcfs_alloc_cattree_key(name->len, record_type);

	if (IS_ERR(record))
		return PTR_ERR(record);

	emmcfs_fill_cattree_key(record, parent_id, name->name, name->len);
	/* if indirect key is exists and succfully removed, it means that this
	 * file was exported via nfs and user tries to rename this file */
	if (vdfs_remove_indirect_index(sbi->catalog_tree, inode) == 0) {
		/* add new */
		ret = vdfs_add_indirect_index(sbi->catalog_tree,
			inode->i_generation, inode->i_ino, record);
		if (ret)
			goto exit;
	}
	if (!(is_vdfs_inode_flag_set(inode, HARD_LINK)))
		ret = emmcfs_fill_cattree_value(inode,
				get_value_pointer(record));
	else
		emmcfs_fill_hlink_value(inode, get_value_pointer(record));

	if (ret)
		goto exit;

	ret = emmcfs_btree_insert(sbi->catalog_tree, record);
	if (ret)
		goto exit;
exit:
	kfree(record);
	return ret;
}
#if 0
/**
 * @brief		Get the object from catalog tree basing on hint.
 * @param [in]	inode_i The eMMCFS inode runtime structure representing
 *			object to get
 * @param [out]	fd	Find data storing info about the place where object
 *			was found
 * @param [in]	mode	Mode in which bnode is got
 * @return		Returns 0 in case of success, error code otherwise
 */
int emmcfs_get_from_cattree(struct emmcfs_inode_info *inode_i,
		struct emmcfs_find_data *fd, enum emmcfs_get_bnode_mode mode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode_i->vfs_inode.i_sb);
	struct emmcfs_cattree_key *key, *found_key;
	int ret = 0;


	EMMCFS_BUG_ON(fd->bnode);

	/* Form search key */
	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;
	key->parent_id = cpu_to_le64(inode_i->parent_id);
	/*key->name.length = utf8s_to_utf16s(inode_i->name,
			 strlen(inode_i->name),	key->name.unicode_str);*/
	/*key->name.length = emmcfs_unicode_strlen(inode_i->name);*/

	/*key2 = kzalloc(sizeof(*key), GFP_KERNEL);*/
	key->name.length = strlen(inode_i->name);
	memcpy(key->name.unicode_str, inode_i->name, strlen(inode_i->name));

	/* Check correctoness of bnode_hint */
	if (inode_i->bnode_hint.bnode_id == 0 || inode_i->bnode_hint.pos < 0)
		goto find;

	fd->bnode = emmcfs_get_bnode(sbi->catalog_tree,
			inode_i->bnode_hint.bnode_id, mode);
	if (IS_ERR(fd->bnode))
		/* Something wrong whith bnode, possibly it was destroyed */
		goto find;

	if (EMMCFS_BNODE_DSCR(fd->bnode)->type != EMMCFS_NODE_LEAF) {
		emmcfs_put_bnode(fd->bnode);
		goto find;
	}

	if (inode_i->bnode_hint.pos >=
			EMMCFS_BNODE_DSCR(fd->bnode)->recs_count) {
		emmcfs_put_bnode(fd->bnode);
		goto find;
	}

	/* Get record described by bnode_hint */
	found_key = emmcfs_get_btree_record(fd->bnode,
			inode_i->bnode_hint.pos);
	if (IS_ERR(found_key)) {
		ret = PTR_ERR(found_key);
		goto exit;
	}

	if (!sbi->catalog_tree->comp_fn(&key->gen_key, &found_key->gen_key)) {
		/* Hinted record is exactly what we are searching */
		fd->pos = inode_i->bnode_hint.pos;
		fd->bnode_id = inode_i->bnode_hint.bnode_id;
		EMMCFS_DEBUG_INO("ino#%lu hint match bnode_id=%u pos=%d",
				inode_i->vfs_inode.i_ino,
				inode_i->bnode_hint.bnode_id,
				inode_i->bnode_hint.pos);
		goto exit;
	}

	/* Unfortunately record had been moved since last time hint
	 * was updated */
	emmcfs_put_bnode(fd->bnode);
find:
	EMMCFS_DEBUG_INO("ino#%lu moved from bnode_id=%lu pos=%d",
			inode_i->vfs_inode.i_ino,
			(long unsigned int) inode_i->bnode_hint.bnode_id,
			inode_i->bnode_hint.pos);

	fd->bnode = emmcfs_btree_find(sbi->catalog_tree, &key->gen_key,
			&fd->pos, mode);

	if (IS_ERR(fd->bnode)) {
		ret = PTR_ERR(fd->bnode);
		goto exit;
	}


	if (fd->pos == -1) {
		ret = -ENOENT;
		goto exit;
	}

	found_key = emmcfs_get_btree_record(fd->bnode, fd->pos);
	if (IS_ERR(found_key)) {
		ret = PTR_ERR(found_key);
		goto exit;
	}
	if (sbi->catalog_tree->comp_fn(&key->gen_key, &found_key->gen_key)) {
		emmcfs_put_bnode(fd->bnode);
		ret = -ENOENT;
		/* Current hint is wrong */
		inode_i->bnode_hint.bnode_id = 0;
		inode_i->bnode_hint.pos = -1;
		goto exit;
	}

	/* Update hint with new record position */
	inode_i->bnode_hint.bnode_id = fd->bnode->node_id;
	inode_i->bnode_hint.pos = fd->pos;
exit:
	kfree(key);
	return ret;
}
#endif


/**
 * @brief		Remove indirect key for specified inode. Indirect key
 *			allows to find inode record in btree via inode ino and
 *			generation. It's used to export file via NFS.
 * @param [in] btree	Btree struct
 * @param [in] inode	Inode struct which record shold be deleted.
 * @return		0 if file was deleted, or -ENOENT if no such file
 * */
int vdfs_remove_indirect_index(struct vdfs_btree *tree, struct inode *inode)
{
	int ret = 0;
#ifdef CONFIG_VDFS_NFS_SUPPORT
	struct vdfs_indirect_key indirect_key;
	indirect_key.generation = inode->i_generation;
	indirect_key.ino = inode->i_ino;
	ret = rb_destroy_file(&tree->nfs_files_root, &indirect_key);
#endif
	return ret;
}

/**
 * @brief		Add indirect key for specified inode. Indirect key
 *			allows to find inode record in btree via inode ino and
 *			inode generation. It's used to export file via NFS.
 * @param [in] btree	Btree struct
 * @param [in] inode	Inode struct which should added to index.
 * @param [in] key	Key for this inode, usulaly it's key from
 *		cattree record
 * @return		0 on succes or error code
 **/
int vdfs_add_indirect_index(struct vdfs_btree *tree, __le32 generation,
		__u64 ino, struct emmcfs_cattree_key *key)
{
	int ret = 0;
#ifdef CONFIG_VDFS_NFS_SUPPORT
	struct vdfs_nfs_file *nfs_file  = kzalloc(sizeof(struct vdfs_nfs_file),
			GFP_KERNEL);
	if (nfs_file == NULL)
		return -ENOMEM;
	nfs_file->indirect_key.generation = generation;
	nfs_file->indirect_key.ino = ino;
	memcpy(&nfs_file->key, key, key->gen_key.key_len);
	ret = rb_add_file(&tree->nfs_files_root, nfs_file);
#endif
	return ret;
}

/**
 * @brief		Get inode using indirect key.
 * @param [in] btree	Btree struct
 * @param [in] fh	File handle
 * @return		Returns pointer to inode on success, errno on failure
 **/
struct inode *vdfs_get_indirect_inode(struct vdfs_btree *tree,
		struct vdfs_indirect_key *indirect_key)
{
#ifdef CONFIG_VDFS_NFS_SUPPORT
	struct vdfs_nfs_file *nfs_file =  NULL;
	struct vdfs_cattree_record *file_record = NULL;
	struct inode *inode = NULL;
	int ret = 0;
	struct emmcfs_cattree_key *rec_key;
	mutex_r_lock(tree->rw_tree_lock);
	nfs_file = rb_search_for_file(&tree->nfs_files_root, indirect_key);
	if (IS_ERR(nfs_file) || (nfs_file == NULL)) {
		ret = -ESTALE;
		goto exit;
	}

	rec_key = &nfs_file->key;
	/*look up for the file record */
	file_record = vdfs_cattree_find(tree, rec_key->parent_id,
		(const void *)rec_key->name.unicode_str,
		rec_key->name.length,
		EMMCFS_BNODE_MODE_RO);
	if (IS_ERR(file_record)) {
		ret = -ESTALE;
		goto exit;
	}
	inode = get_inode_from_record(file_record);
	if (IS_ERR(inode))
		ret = PTR_ERR(inode);

exit:
	if (file_record && !IS_ERR(file_record))
		vdfs_release_record(
				(struct vdfs_btree_gen_record *) file_record);
	mutex_r_unlock(tree->rw_tree_lock);
	if (ret)
		return ERR_PTR(ret);
	else
		return inode;
#endif
	return NULL;
}
