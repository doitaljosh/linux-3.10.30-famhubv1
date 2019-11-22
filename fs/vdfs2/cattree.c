/**
 * @file	fs/emmcfs/cattree.c
 * @brief	Operations with catalog tree.
 * @author
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements bnode operations and its related functions.
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
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

#include "emmcfs.h"

#else
#include "vdfs_tools.h"
#include <ctype.h>
#endif

#include "cattree.h"
#include "debug.h"
#include "emmcfs.h"

/**
 * @brief		Key compare function for catalog tree
 *			for case-sensitive usecase.
 * @param [in]	__key1	Pointer to the first key
 * @param [in]	__key2	Pointer to the second key
 * @return		Returns value	< 0	if key1 < key2,
					== 0	if key1 = key2,
					> 0	if key1 > key2 (like strcmp)
 */
int emmcfs_cattree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2)
{
	struct emmcfs_cattree_key *key1, *key2;
	__u8 *ch1, *ch2;
	int diff;
	int i;


	key1 = container_of(__key1, struct emmcfs_cattree_key, gen_key);
	key2 = container_of(__key2, struct emmcfs_cattree_key, gen_key);

	if (key1->parent_id != key2->parent_id)
		return (__s64) le64_to_cpu(key1->parent_id) -
			(__s64) le64_to_cpu(key2->parent_id);

	ch1 = key1->name.unicode_str;
	ch2 = key2->name.unicode_str;
	for (i = min(le32_to_cpu(key1->name.length),
				le32_to_cpu(key2->name.length)); i > 0; i--) {
		diff = le16_to_cpu(*ch1) - le16_to_cpu(*ch2);
		if (diff)
			return diff;
		ch1++;
		ch2++;
	}

	return le32_to_cpu(key1->name.length) -
		le32_to_cpu(key2->name.length);
}

/**
 * @brief		Key compare function for catalog tree
 *			for case-insensitive usecase.
 * @param [in]	__key1	Pointer to the first key
 * @param [in]	__key2	Pointer to the second key
 * @return		Returns value	< 0	if key1 < key2,
					== 0	if key1 = key2,
					> 0	if key1 > key2 (like strcmp)
 */
int emmcfs_cattree_cmpfn_ci(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2)
{
	struct emmcfs_cattree_key *key1, *key2;
	__u8 *ch1, *ch2;
	int diff;
	int i;


	key1 = container_of(__key1, struct emmcfs_cattree_key, gen_key);
	key2 = container_of(__key2, struct emmcfs_cattree_key, gen_key);

	if (key1->parent_id != key2->parent_id)
		return (__s64) le64_to_cpu(key1->parent_id) -
			(__s64) le64_to_cpu(key2->parent_id);

	ch1 = key1->name.unicode_str;
	ch2 = key2->name.unicode_str;
	for (i = min(le32_to_cpu(key1->name.length),
				le32_to_cpu(key2->name.length)); i > 0; i--) {
		diff = le16_to_cpu(tolower(*ch1)) - le16_to_cpu(tolower(*ch2));
		if (diff)
			return diff;
		ch1++;
		ch2++;
	}

	return le32_to_cpu(key1->name.length) -
		le32_to_cpu(key2->name.length);
}

/**
 * @brief			Interface to search specific object(file)
 *				in the catalog tree.
 * @param [in]	tree		btree information
 * @param [in]	parent_id	Parent id of file to search for
 * @param [in]	name		Name of file to search with specified parent id
 * @param [in]	len		Name length
 * @param [out]	fd		Searching cache info for quick result
 *				fetching later
 * @param [in]	mode		Mode in which bnode should be got
 *				(used later in emmcfs_btree_find)
 * @return			Returns 0 on success, error code otherwise
 */
int emmcfs_cattree_find_old(struct vdfs_btree *tree,
		__u64 parent_id, const char *name, int len,
		struct emmcfs_find_data *fd, enum emmcfs_get_bnode_mode mode)
{
	struct emmcfs_cattree_key *key, *found_key;
	int ret = 0;

	if (fd->bnode) {
		EMMCFS_ERR("bnode should be empty");
		if (!is_sbi_flag_set(tree->sbi, IS_MOUNT_FINISHED))
			return -EINVAL;
		else
			EMMCFS_BUG();
	}


	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	key->parent_id = cpu_to_le64(parent_id);
	/*key->name.length = utf8s_to_utf16s(name, len,
				 key->name.unicode_str);*/
	/*key->name.length = emmcfs_unicode_strlen(name);*/
	/*key2 = kzalloc(sizeof(*key2), GFP_KERNEL);*/
	key->name.length = len;
	memcpy(key->name.unicode_str, name, len);


	fd->bnode = emmcfs_btree_find(tree, &key->gen_key,
			&fd->pos, mode);

	if (IS_ERR(fd->bnode)) {
		ret = PTR_ERR(fd->bnode);
		goto exit;
	}

	if (fd->pos == -1) {
		EMMCFS_ERR("parent_id=%llu name=%s", parent_id, name);
		if (!is_sbi_flag_set(tree->sbi, IS_MOUNT_FINISHED))
			ret = -EFAULT;
		else
			EMMCFS_BUG();
	}

	found_key = emmcfs_get_btree_record(fd->bnode, fd->pos);
	if (IS_ERR(found_key)) {
		ret = PTR_ERR(found_key);
		goto exit;
	}

	if (tree->comp_fn(&key->gen_key, &found_key->gen_key)) {
		emmcfs_put_bnode(fd->bnode);
		/* TODO - ENOENT error is returned from emmcfs_get_bnode. Let's
		 * choose some guaranteely unique error to fix the fact that
		 * we correctly not found searched entry, even this name doesn't
		 * reflect real error*/
		ret = -ENOENT;
	}

exit:
	kfree(key);
	return ret;
}

/* Help function for building new cattree interfaces and trancling them inot
 * old */
static void temp_fill_record(struct emmcfs_bnode *bnode, int pos,
		struct vdfs_btree_record_info *rec_info)
{
	struct vdfs_cattree_record *record = (void *) &rec_info->gen_record;

	rec_info->rec_pos.bnode = bnode;
	rec_info->rec_pos.pos = pos;
	record->key = emmcfs_get_btree_record(bnode, pos);

	BUG_ON(!record->key);
	if (IS_ERR(record->key)) {
		kfree(record);
		EMMCFS_ERR("unable to get record");
		EMMCFS_BUG();
	}


	record->val = get_value_pointer(record->key);

}

struct vdfs_cattree_record *vdfs_cattree_find(struct vdfs_btree *btree,
		__u64 parent_id, const char *name, int len,
		enum emmcfs_get_bnode_mode mode)
{
	struct emmcfs_cattree_key *key;
	struct vdfs_cattree_record *record;

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	key->parent_id = cpu_to_le64(parent_id);
	key->name.length = len;
	memcpy(key->name.unicode_str, name, len);

	record = (struct vdfs_cattree_record *) vdfs_btree_find(btree,
			&key->gen_key, mode);
	if (IS_ERR(record))
		goto exit;

	if (btree->comp_fn(&record->key->gen_key, &key->gen_key)) {
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
		record = ERR_PTR(-ENOENT);
	}

exit:
	kfree(key);
	return record;
}


/**
 * @brief			Allocate key for catalog tree record.
 * @param [in]	name_len	Length of a name for an allocating object
 * @param [in]	record_type	Type of record which it should be
 * @return			Returns pointer to a newly allocated key
 *				or error code otherwise
 */
struct emmcfs_cattree_key *emmcfs_alloc_cattree_key(int name_len,
		u8 record_type)
{
	u32 record_len = 0;
	struct emmcfs_cattree_key *form_key;
	u32 key_len = sizeof(struct emmcfs_cattree_key) -
			sizeof(struct vdfs_unicode_string) +
			sizeof(__le32) +
			/* TODO - if the terminating #0 symbol necessary and
			 * is used then '+ 1' is necessary
			 */
			(name_len + 1) * sizeof(*form_key->name.unicode_str);

	switch (record_type) {
	case VDFS_CATALOG_FILE_RECORD:
		record_len = sizeof(struct vdfs_catalog_file_record);
	break;
	case VDFS_CATALOG_FOLDER_RECORD:
	case VDFS_CATALOG_RECORD_DUMMY:
		record_len = sizeof(struct vdfs_catalog_folder_record);
	break;
	case VDFS_CATALOG_HLINK_RECORD:
		record_len = sizeof(struct vdfs_catalog_hlink_record);
	break;
	case VDFS_CATALOG_PTREE_ROOT:
		record_len = sizeof(struct vdfs_pack_insert_point_value);
	break;
	default:
		EMMCFS_BUG();
	}

	form_key = kzalloc(key_len + record_len, GFP_KERNEL);
	if (!form_key)
		return ERR_PTR(-ENOMEM);

	form_key->gen_key.key_len = cpu_to_le32(key_len);
	form_key->gen_key.record_len = cpu_to_le32(key_len + record_len);
	form_key->record_type = record_type;

	return form_key;
}

/**
 * @brief			Fill already allocated key with data.
 * @param [out] fill_key	Key to fill
 * @param [in]	parent_id	Parent id of object
 * @param [in]	name		Object name
 * @return	void
 */
void emmcfs_fill_cattree_key(struct emmcfs_cattree_key *fill_key,
		u64 parent_id, const char *name, unsigned int len)
{
	fill_key->name.length = cpu_to_le32(len);
	memcpy(fill_key->name.unicode_str, name, len);
	fill_key->parent_id = cpu_to_le64(parent_id);
}

struct vdfs_cattree_record *vdfs_cattree_place_record(
		struct vdfs_btree *tree, u64 parent_id,
		const char *name, int len, u8 record_type)
{
	void *insert_data = NULL;
	int err = 0;

	insert_data = emmcfs_alloc_cattree_key(len, record_type);

	if (IS_ERR(insert_data))
		return insert_data;

	emmcfs_fill_cattree_key(insert_data, parent_id, name, len);

	err = emmcfs_btree_insert(tree, insert_data);
	kfree(insert_data);

	if (err)
		return ERR_PTR(err);

	return vdfs_cattree_find(tree, parent_id, name, len,
			EMMCFS_BNODE_MODE_RW);
}


/**
 * @brief                   Find first child object for the specified catalog.
 * @param [in] sbi          Pointer to superblock information
 * @param [in] catalog_id   Id of the parent catalog
 * @return             Returns cattree record containing first child object
 *                     in case of success, error code otherwise
 */
struct vdfs_cattree_record *vdfs_cattree_get_first_child(
	struct vdfs_btree *btree, __u64 catalog_id)
{
	struct emmcfs_cattree_key *search_key;
	struct vdfs_cattree_record *record;
	int ret;

	search_key = kzalloc(sizeof(*search_key), GFP_KERNEL);
	if (!search_key)
		return ERR_PTR(-ENOMEM);

	search_key->parent_id = cpu_to_le64(catalog_id);
	search_key->name.length = 0;

	record = (struct vdfs_cattree_record *) vdfs_btree_find(btree,
			&search_key->gen_key, EMMCFS_BNODE_MODE_RO);
	if (IS_ERR(record))
		goto exit;

	if (le64_to_cpu(record->key->parent_id) == catalog_id)
		goto exit;

	ret = vdfs_get_next_btree_record((struct vdfs_btree_gen_record *)
			record);
	if (ret) {
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
		record = ERR_PTR(ret);
	}
exit:
	kfree(search_key);
	return record;
}

int vdfs_cattree_get_next_record(struct vdfs_cattree_record *record)
{
	struct vdfs_btree_record_info *rec_info =
		VDFS_BTREE_REC_I((void *) record);

	struct emmcfs_bnode *bnode = rec_info->rec_pos.bnode;
	int pos = rec_info->rec_pos.pos;
	void *raw_record = NULL;

	raw_record = emmcfs_get_next_btree_record(&bnode, &pos);

	/* Ret value have to be pointer, or error, not null */
	BUG_ON(!raw_record);
	if (IS_ERR(raw_record))
		return PTR_ERR(raw_record);

	temp_fill_record(bnode, pos, rec_info);

	return 0;
}

int vdfs_cattree_remove(struct vdfs_sb_info *sbi,
		__u64 parent_id, const char *name, int len)
{
	int ret = 0;
	struct emmcfs_cattree_key *key;
	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	key->parent_id = cpu_to_le64(parent_id);
	key->name.length = len;
	memcpy(key->name.unicode_str, name, len);
	ret = emmcfs_btree_remove(sbi->catalog_tree, &key->gen_key);
	kfree(key);
	return ret;
}

#ifdef USER_SPACE
int get_cattree_record_size(int key_type)
{
	if (key_type == VDFS_CATALOG_FOLDER_RECORD)
		return sizeof(struct vdfs_catalog_folder_record) +
			sizeof(struct emmcfs_cattree_key);

	if (key_type == VDFS_CATALOG_FILE_RECORD)
		return sizeof(struct vdfs_catalog_file_record) +
			sizeof(struct emmcfs_cattree_key);

	if (key_type == VDFS_CATALOG_HLINK_RECORD)
		return sizeof(struct vdfs_catalog_hlink_record) +
			sizeof(struct emmcfs_cattree_key);

	/* if key wasn't recognized */
	log_error("get_cattree_record_size() Unknown value for key_type %x",
			key_type);
	assert(0);
	return 0;
}

int vdfs_strcpy(struct  vdfs_unicode_string *string, const char *cstring)
{
	u_int32_t str_len;

	str_len = strlen(cstring);
	if (str_len > VDFS_FILE_NAME_LEN)
		return -ENAMETOOLONG;
	string->length = str_len;
	memcpy(string->unicode_str, cstring, str_len);
	return 0;
}

/**
 * @brief			Allocate key for catalog tree record.
 * @param [in]	name_len	Length of a name for an allocating object
 * @param [in]	record_type	Type of record which it should be
 * @return			Returns pointer to a newly allocated key
 *				or error code otherwise
 */
struct emmcfs_cattree_key *vdfs_alloc_cattree_record(int name_len,
		int record_type)
{
	u32 record_len = 0;
	struct emmcfs_cattree_key *form_key;
	u32 key_len = sizeof(struct emmcfs_cattree_key) -
			sizeof(struct vdfs_unicode_string) +
			sizeof(__le32) +
			/* TODO - if the terminating #0 symbol necessary and
			 * is used then '+ 1' is necessary
			 */
			(name_len + 1) * sizeof(*form_key->name.unicode_str);

	if (record_type == VDFS_CATALOG_FILE_RECORD)
		record_len = sizeof(struct vdfs_catalog_file_record);
	else if (record_type == VDFS_CATALOG_FOLDER_RECORD)
		record_len = sizeof(struct vdfs_catalog_folder_record);
	else if (record_type == VDFS_CATALOG_HLINK_RECORD)
		record_len = sizeof(struct vdfs_catalog_folder_record);

	form_key = kzalloc(key_len + record_len, GFP_KERNEL);
	if (!form_key)
		return ERR_PTR(-ENOMEM);

	form_key->gen_key.key_len = cpu_to_le32(key_len);
	form_key->gen_key.record_len = cpu_to_le32(key_len + record_len);
	form_key->record_type = record_type;
	return form_key;
}

int init_cattree_folder_record_value(
	struct vdfs_catalog_folder_record *folder,
	u_int64_t total_items_count, u_int64_t links_count,
	u_int64_t uuid, struct emmcfs_posix_permissions	*permissions,
	const struct emmcfs_date *time_creation,
	const struct emmcfs_date *time_modification,
	const struct emmcfs_date *time_access)
{
	folder->flags = 0;
	folder->total_items_count = total_items_count;
	folder->links_count = links_count;
	folder->object_id = uuid;
	memcpy(&folder->permissions, permissions,
		sizeof(folder->permissions));
	memcpy(&folder->creation_time, time_creation,
		sizeof(folder->creation_time));
	memcpy(&folder->modification_time, time_modification,
		sizeof(folder->modification_time));
	memcpy(&folder->access_time, time_access, sizeof(folder->access_time));
	return 0;
}

static int cattree_init_root_bnode(struct emmcfs_bnode *root_bnode)
{
	int ret = 0;
	const char *name = VDFS_ROOTDIR_NAME;
	struct vdfs_sb_info *sbi = root_bnode->host->sbi;
	struct vdfs_catalog_folder_record *root_folder_value;
	struct emmcfs_posix_permissions perm_root;
	void *root_record;

	memset(root_bnode->data, 0x0, get_bnode_size(sbi));
	temp_stub_init_new_node_descr(root_bnode, EMMCFS_NODE_LEAF);

	root_record = vdfs_alloc_cattree_record(strlen(name),
			VDFS_CATALOG_FOLDER_RECORD);
	if (IS_ERR(root_record))
		return PTR_ERR(root_record);

	emmcfs_fill_cattree_key(root_record, 0, name, strlen(name));
	root_folder_value = get_value_pointer(root_record);

	if (sbi->root_path) {
		ret = get_permissions_for_root_dir_from_path(sbi, &perm_root);
		if (ret)
			goto exit;
	} else
		get_permissions_for_root_dir(&perm_root);
	root_folder_value->flags = 0;
	root_folder_value->total_items_count = 0;
	root_folder_value->links_count = cpu_to_le64(1);
	root_folder_value->permissions.file_mode = cpu_to_le16(16877);

	root_folder_value->object_id = VDFS_ROOT_INO;

	memcpy(&root_folder_value->permissions, &perm_root,
		sizeof(root_folder_value->permissions));
	memcpy(&root_folder_value->creation_time, &sbi->timestamp,
		sizeof(root_folder_value->creation_time));
	memcpy(&root_folder_value->modification_time, &sbi->timestamp,
		sizeof(root_folder_value->modification_time));
	memcpy(&root_folder_value->access_time, &sbi->timestamp,
			sizeof(root_folder_value->access_time));

	temp_stub_insert_into_node(root_bnode, root_record, 0);
exit:
	free(root_record);
	return ret;
}

int init_cattree(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct vdfs_tools_btree_info *cattree_btree = &sbi->cattree;
	struct emmcfs_bnode *root_bnode = 0;

	log_activity("Create catalog tree");


	ret = btree_init(sbi, cattree_btree, EMMCFS_BTREE_CATALOG,
			sizeof(struct emmcfs_cattree_key) +
			sizeof(struct vdfs_catalog_file_record));
	if (ret)
		goto error_exit;


	/* Init root bnode */
	root_bnode = vdfs_alloc_new_bnode(&cattree_btree->vdfs_btree);
	if (IS_ERR(root_bnode)) {
		ret = (PTR_ERR(root_bnode));
		root_bnode = 0;
		goto error_exit;
	}
	ret = cattree_init_root_bnode(root_bnode);
	if (ret)
		goto error_exit;
	cattree_btree->tree.subsystem_name = "CATALOG TREE";
	cattree_btree->tree.sub_system_id = VDFS_CAT_TREE_INO;
	cattree_btree->vdfs_btree.comp_fn = emmcfs_cattree_cmpfn;
	util_update_crc(cattree_btree->bnode_array[0]->data,
			get_bnode_size(sbi), NULL, 0);
	util_update_crc(root_bnode->data, get_bnode_size(sbi), NULL, 0);
	return 0;

error_exit:
	log_error("Can't init catalog tree");
	return ret;
}


u_int32_t catalog_tree_get_height(struct vdfs_sb_info *sbi)
{
	u_int32_t cattree_level;
	u_int32_t nodes_count;
	u_int32_t records_in_one_bnode;

	/* one bnode - one leb => bnodes on volume = lebs on volume */

	records_in_one_bnode = (2 << sbi->log_erase_block_size) /
		(sizeof(struct emmcfs_cattree_key) +
		 sizeof(struct vdfs_catalog_file_record));

	/** heuristic - reduce records in bnode to
	 * allocate more space for journal */
	if (records_in_one_bnode > 4)
		records_in_one_bnode = records_in_one_bnode / 2;

	assert(records_in_one_bnode > 1);

	cattree_level = 0;
	nodes_count = sbi->volume_size_in_erase_blocks;
	while (nodes_count > 1) {
		/* here it is assumed that there are about 30 records in one  */
		nodes_count /= records_in_one_bnode;
		cattree_level++;
	}
	return cattree_level;	/* no additional increment required, */
				/* because of postincrement in cycle */
}

/**
 * @brief Function fork_init Fill fork structure of file
 * @param [in] fork		point to fork to fill
 * @param [in] begin		start offset position of file
 * @param [in] length		size of file
 * @param [in] block_size
  */
void fork_init(struct vdfs_fork *_fork, u_int64_t begin, u_int64_t length,
		unsigned int block_size)
{
	int i ;
	memset(_fork, 0, sizeof(struct vdfs_fork));
	_fork->magic = EMMCFS_FORK_MAGIC;
	_fork->size_in_bytes = cpu_to_le64(length);
	_fork->total_blocks_count = cpu_to_le32(
			byte_to_block(length, block_size));
	for (i = 0; i < VDFS_EXTENTS_COUNT_IN_FORK; i++)
		memset(&_fork->extents[i].extent, 0,
				sizeof(struct vdfs_extent));
	_fork->extents[0].extent.begin = cpu_to_le64(begin/block_size);
	_fork->extents[0].extent.length = _fork->total_blocks_count;
}
/****************************************************************************/
/**
 * @brief Function vdfs_fill_cattree_record_value for VDFS subsystem
 * @param [in] record			structure of cattree record to fill
 * @param [in] record_type		type of record to fill
 * @param [in] total_items_count	Amount of files in the directory
 * @param [in] links_count		Link's count for file
 * @param [in] uuid			object_id of object
 * @param [in] permissions		permissions to object
 * @param [in] time_creation
 * @param [in] time_modification
 * @param [in] time_access
 * @param [in] begin			start adress of object in volume
 * @param [in] length			size of object
 * @param [in] block_size
 * @return 0 on success, error code otherwise
 */
void vdfs_fill_cattree_record_value(struct vdfs_cattree_record *record,
		u_int64_t total_items_count,
		u_int64_t links_count, u_int64_t uuid,
		struct emmcfs_posix_permissions	*permissions,
		const struct emmcfs_date time_creation,
		const struct emmcfs_date time_modification,
		const struct emmcfs_date time_access,
		u_int64_t   begin,
		u_int64_t   length,
		unsigned int block_size)
{
	struct vdfs_catalog_folder_record *val = VDFS_CATTREE_FOLDVAL(record);
	struct vdfs_catalog_hlink_record *hl =
			(struct vdfs_catalog_hlink_record *)
					(record->val);
	if (record->key->record_type == VDFS_CATALOG_HLINK_RECORD) {
		hl->object_id = uuid;
		hl->file_mode = permissions->file_mode;
		return;
	}
	val->total_items_count = cpu_to_le64(total_items_count);
	val->links_count = links_count;
	val->object_id = uuid;
	memcpy(&val->permissions, permissions,
		sizeof(val->permissions));
	memcpy(&val->creation_time, &time_creation,
		sizeof(val->creation_time));
	memcpy(&val->modification_time, &time_modification,
		sizeof(val->modification_time));
	memcpy(&val->access_time, &time_access,
			sizeof(val->access_time));
	if (record->key->record_type == VDFS_CATALOG_FILE_RECORD) {
		struct vdfs_catalog_file_record *file_rec = (
				struct vdfs_catalog_file_record *) val;
		if (val->flags & (1 << TINY_FILE)) {
			file_rec->tiny.len = length;
			file_rec->tiny.i_size = length;
		} else if (val->flags & (1 << SMALL_FILE)) {
			file_rec->small.len = length;
			file_rec->small.i_size = length;
		} else
			fork_init(&file_rec->data_fork, begin, length,
					block_size);
	}
}

#endif
