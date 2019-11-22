/**
 * @file	fs/vdfs/xattr.c
 * @brief	Operations with catalog tree.
 * @author
 *
 * This file implements bnode operations and its related functions.
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
#include <linux/xattr.h>
#include <linux/fs.h>
#else
#include "vdfs_tools.h"
#include <sys/xattr.h>
#endif

#include "emmcfs.h"
#include "xattrtree.h"

#ifndef USER_SPACE
char *xattr_prefixes[] = {
	XATTR_USER_PREFIX,
	XATTR_SYSTEM_PREFIX,
	XATTR_TRUSTED_PREFIX,
	XATTR_SECURITY_PREFIX,
	NULL
};

static int check_xattr_prefix(const char *name)
{
	int ret = 0;
	char **prefix;

	prefix = xattr_prefixes;
	while (*prefix != NULL)	{
		ret = strncmp(name, *prefix, strlen(*prefix));
		if (ret == 0)
			break;
		prefix++;
	}

	return ret;
}
#endif

/* Now this function is not used in the utilities. Hide under ifdef to avoid
 * build warnings */
static int xattrtree_insert(struct vdfs_btree *tree, u64 object_id,
		const char *name, size_t val_len, const void *value)
{
	void *insert_data = NULL;
	struct vdfs_xattrtree_key *key;
	u32 key_len;
	int name_len = strlen(name);
	int ret = 0;


	if (name_len >= VDFS_XATTR_NAME_MAX_LEN ||
			val_len >= VDFS_XATTR_VAL_MAX_LEN) {
		EMMCFS_ERR("xattr name or val too long");
		return -EINVAL;
	}
	insert_data = kzalloc(tree->max_record_len, GFP_KERNEL);
	if (!insert_data)
		return -ENOMEM;

	key = insert_data;

	key_len = sizeof(*key) - sizeof(key->name) + name_len;
	memcpy(key->gen_key.magic, VDFS_XATTR_REC_MAGIC,
		strlen(VDFS_XATTR_REC_MAGIC));

	key->gen_key.key_len = cpu_to_le32(key_len);
	key->gen_key.record_len = cpu_to_le32(key_len + val_len);

	key->object_id = cpu_to_le64(object_id);
	strncpy(key->name, name, name_len);
	key->name_len = name_len;

	memcpy(get_value_pointer(key), value, val_len);

	ret = emmcfs_btree_insert(tree, insert_data);
	kfree(insert_data);

	return ret;
}


/**
 * @brief		Xattr tree key compare function.
 * @param [in]	__key1	Pointer to the first key
 * @param [in]	__key2	Pointer to the second key
 * @return		Returns value	< 0	if key1 < key2,
					== 0	if key1 = key2,
					> 0	if key1 > key2 (like strcmp)
 */
int vdfs_xattrtree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2)
{
	struct vdfs_xattrtree_key *key1, *key2;
	int diff;


	key1 = container_of(__key1, struct vdfs_xattrtree_key, gen_key);
	key2 = container_of(__key2, struct vdfs_xattrtree_key, gen_key);

	if (key1->object_id != key2->object_id)
		return (__s64) le64_to_cpu(key1->object_id) -
			(__s64) le64_to_cpu(key2->object_id);

	diff = memcmp(key1->name, key2->name,
			min(key1->name_len, key2->name_len));
	if (diff)
		return diff;

	return key1->name_len - key2->name_len;
}

static struct vdfs_xattrtree_key *xattrtree_alloc_key(u64 object_id,
		const char *name)
{
	struct vdfs_xattrtree_key *key;
	int name_len = strlen(name);

	if (name_len >= VDFS_XATTR_NAME_MAX_LEN)
		return ERR_PTR(-EINVAL);

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);


	key->object_id = cpu_to_le64(object_id);
	key->name_len = name_len;
	strncpy(key->name, name, name_len);

	return key;
}

static struct vdfs_xattrtree_record *vdfs_xattrtree_find(struct vdfs_btree
	*btree, u64 object_id, const char *name,
	enum emmcfs_get_bnode_mode mode)
{
	struct vdfs_xattrtree_key *key;
	struct vdfs_xattrtree_record *record;

	key = xattrtree_alloc_key(object_id, name);
	if (IS_ERR(key))
		return (void *) key;

	record = (struct vdfs_xattrtree_record *) vdfs_btree_find(btree,
			&key->gen_key, mode);
	if (IS_ERR(record))
		goto exit;

	if (*name != '\0' &&
			btree->comp_fn(&key->gen_key, &record->key->gen_key)) {
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
		record = ERR_PTR(-ENODATA);
	}

exit:
	kfree(key);
	/* Correct return in case absent xattr is ENODATA */
	if (PTR_ERR(record) == -ENOENT)
		record = ERR_PTR(-ENODATA);
	return record;
}

#ifndef USER_SPACE
static int xattrtree_remove_record(struct vdfs_btree *tree, u64 object_id,
		const char *name)
{
	struct vdfs_xattrtree_key *key;
	int ret;

	key = xattrtree_alloc_key(object_id, name);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = emmcfs_btree_remove(tree, &key->gen_key);

	return ret;
}
#endif

static int xattrtree_get_next_record(struct vdfs_xattrtree_record *record)
{
	return vdfs_get_next_btree_record((struct vdfs_btree_gen_record *)
			record);
}

static struct vdfs_xattrtree_record *xattrtree_get_first_record(
		struct vdfs_btree *tree, u64 object_id,
		enum emmcfs_get_bnode_mode mode)
{
	struct vdfs_xattrtree_record *record;
	int ret = 0;

	record = vdfs_xattrtree_find(tree, object_id, "", mode);

	if (IS_ERR(record))
		return record;

	ret = xattrtree_get_next_record(record);
	if (ret)
		goto err_exit;

	if (le64_to_cpu(record->key->object_id) != object_id) {
		ret = -ENOENT;
		goto err_exit;
	}

	return record;

err_exit:
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	return ERR_PTR(ret);

}

#ifndef USER_SPACE
int vdfs_xattrtree_remove_all(struct vdfs_btree *tree, u64 object_id)
{
	struct vdfs_xattrtree_record *record = NULL;
	struct vdfs_xattrtree_key *rm_key =
		kzalloc(sizeof(*rm_key), GFP_KERNEL);
	int ret = 0;

	if (!rm_key)
		return -ENOMEM;


	while (!ret) {
		vdfs_start_transaction(tree->sbi);
		mutex_w_lock(tree->rw_tree_lock);

		record = xattrtree_get_first_record(tree, object_id,
				EMMCFS_BNODE_MODE_RO);
		if (IS_ERR(record)) {
			if (PTR_ERR(record) == -ENOENT)
				ret = 0;
			else
				ret = PTR_ERR(record);

			mutex_w_unlock(tree->rw_tree_lock);
			vdfs_stop_transaction(tree->sbi);
			break;
		}
		memcpy(rm_key, record->key, record->key->gen_key.key_len);
		vdfs_release_record((struct vdfs_btree_gen_record *) record);


		ret = emmcfs_btree_remove(tree, &rm_key->gen_key);
		mutex_w_unlock(tree->rw_tree_lock);
		vdfs_stop_transaction(tree->sbi);
	}

	kfree(rm_key);
	return ret;
}

int vdfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	int ret = 0;
	struct vdfs_xattrtree_record *record;
	struct inode *inode = dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);

	if (name == NULL)
		return -EINVAL;

	if (strlen(name) >= VDFS_XATTR_NAME_MAX_LEN ||
			size >= VDFS_XATTR_VAL_MAX_LEN)
		return -EINVAL;

	ret = check_xattr_prefix(name);
	if (ret)
		return -EOPNOTSUPP;

	vdfs_start_transaction(sbi);
	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);

	record = vdfs_xattrtree_find(sbi->xattr_tree, inode->i_ino, name,
			EMMCFS_BNODE_MODE_RW);

	if (!IS_ERR(record)) {
		/* record found */
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
		if (flags & XATTR_CREATE) {
			ret = -EEXIST;
			goto exit;
		} else {
			ret = xattrtree_remove_record(sbi->xattr_tree,
				inode->i_ino, name);
			if (ret)
				goto exit;
		}
	} else if (PTR_ERR(record) == -ENODATA) {
		/* no such record */
		if (flags & XATTR_REPLACE) {
			ret = -ENODATA;
			goto exit;
		} else
			goto insert_xattr;
	} else {
		/* some other error */
		ret = PTR_ERR(record);
		goto exit;
	}

insert_xattr:
	ret = xattrtree_insert(sbi->xattr_tree, inode->i_ino, name, size,
			value);
exit:
	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	vdfs_stop_transaction(sbi);

	return ret;
}

ssize_t vdfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t buf_size)
{
	struct vdfs_xattrtree_record *record;
	struct inode *inode = dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	ssize_t size;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	if (check_xattr_prefix(name))
		return -EOPNOTSUPP;

	down_read_nested(sbi->xattr_tree->rw_tree_lock, 1);
	record = vdfs_xattrtree_find(sbi->xattr_tree, inode->i_ino, name,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);
		return PTR_ERR(record);
	}

	size = le32_to_cpu(record->key->gen_key.record_len) -
		le32_to_cpu(record->key->gen_key.key_len);
	if (!buffer)
		goto exit;

	if (size > buf_size) {
		size = -ERANGE;
		goto exit;
	}

	memcpy(buffer, record->val, size);
exit:
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);

	return size;
}


int vdfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int ret = 0;
	if (strcmp(name, "") == 0)
		return -EINVAL;


	vdfs_start_transaction(sbi);
	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);

	ret = xattrtree_remove_record(sbi->xattr_tree, inode->i_ino, name);

	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	vdfs_stop_transaction(sbi);

	return ret;
}

ssize_t vdfs_listxattr(struct dentry *dentry, char *buffer, size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct vdfs_xattrtree_record *record;
	ssize_t size = 0;
	int ret = 0;

	mutex_r_lock(sbi->xattr_tree->rw_tree_lock);
	record = xattrtree_get_first_record(sbi->xattr_tree, inode->i_ino,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);
		if (PTR_ERR(record) == -ENOENT)
			return -ENODATA;
		else
			return PTR_ERR(record);
	}

	while (!ret && le64_to_cpu(record->key->object_id) == inode->i_ino) {
		int name_len = record->key->name_len + 1;

		if (buffer) {
			if (buf_size < name_len) {
				ret = -ERANGE;
				break;
			}
			memcpy(buffer, record->key->name, name_len - 1);
			buffer[name_len - 1] = 0;
			buf_size -= name_len;
			buffer += name_len;
		}

		size += name_len;

		ret = xattrtree_get_next_record(record);
	}

	if (ret == -ENOENT)
		/* It is normal if there is no more records in the btree */
		ret = 0;

	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);


	return ret ? ret : size;
}

int vdfs_init_security_xattrs(struct inode *inode,
		const struct xattr *xattr_array, void *fs_data)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	const struct xattr *xattr;
	char *name = NULL;
	int name_len, ret = 0;

	down_write_nested(sbi->xattr_tree->rw_tree_lock, 1);
	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		name_len = strlen(xattr->name) + 1;
		name = krealloc(name, XATTR_SECURITY_PREFIX_LEN +
				name_len, GFP_KERNEL);
		ret = -ENOMEM;
		if (!name)
			break;
		memcpy(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN);
		memcpy(name + XATTR_SECURITY_PREFIX_LEN, xattr->name, name_len);
		if (xattr->value)
			ret = xattrtree_insert(sbi->xattr_tree, inode->i_ino,
					name, xattr->value_len, xattr->value);
		if (ret)
			break;
	}
	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	kfree(name);

	return ret;
}

#ifdef CONFIG_VDFS_QUOTA
int update_has_quota(struct vdfs_sb_info *sbi, u64 ino, int index)
{
	struct vdfs_xattrtree_record *record;

	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);
	record = vdfs_xattrtree_find(sbi->xattr_tree, ino,
			QUOTA_HAS_XATTR, EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(record)) {
		mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
		return PTR_ERR(record);
	}

	snprintf(record->val, UUL_MAX_LEN, "%019llu", sbi->quotas[index].has);
	emmcfs_mark_bnode_dirty(VDFS_BTREE_REC_I
			((void *) record)->rec_pos.bnode);
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	return 0;
}
#endif
#endif

#ifdef USER_SPACE
void dummy_xattrtree_record_init(struct vdfs_raw_xattrtree_record *xattr_record)
{
	int key_len, name_len;
	memset(xattr_record, 0, sizeof(*xattr_record));
	set_magic(xattr_record->key.gen_key.magic, XATTRTREE_LEAF);


	name_len = strlen(VDFS_XATTRTREE_ROOT_REC_NAME);
	key_len = sizeof(xattr_record->key) - sizeof(xattr_record->key.name) +
		name_len;



	xattr_record->key.gen_key.key_len = cpu_to_le32(key_len);
	/* Xattr root record has no value, so record_len == key_len */
	xattr_record->key.gen_key.record_len = key_len;
	xattr_record->key.name_len = name_len;
	if (VDFS_XATTR_NAME_MAX_LEN >= name_len)
		strncpy(xattr_record->key.name, VDFS_XATTRTREE_ROOT_REC_NAME,
				name_len);
}

static void xattrtree_init_root_bnode(struct emmcfs_bnode *root_bnode)
{
	struct vdfs_raw_xattrtree_record xattr_record;

	temp_stub_init_new_node_descr(root_bnode, EMMCFS_NODE_LEAF);
	dummy_xattrtree_record_init(&xattr_record);
	temp_stub_insert_into_node(root_bnode, &xattr_record, 0);
}

int init_xattrtree(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct vdfs_tools_btree_info *xattr_btree = &sbi->xattrtree;
	struct emmcfs_bnode *root_bnode = 0;

	log_activity("Create xattr tree");

	xattr_btree->tree.sub_system_id = VDFS_XATTR_TREE_INO;
	xattr_btree->tree.subsystem_name = "XATTR TREE";
	ret = btree_init(sbi, xattr_btree, VDFS_BTREE_XATTRS,
			sizeof(struct vdfs_xattrtree_key) +
			sizeof(struct vdfs_raw_xattrtree_record));

	if (ret)
		goto error_exit;
	xattr_btree->vdfs_btree.comp_fn = vdfs_xattrtree_cmpfn;
	sbi->xattr_tree = &xattr_btree->vdfs_btree;
	/* Init root bnode */
	root_bnode = vdfs_alloc_new_bnode(&xattr_btree->vdfs_btree);
	if (IS_ERR(root_bnode)) {
		ret = (PTR_ERR(root_bnode));
		root_bnode = 0;
		goto error_exit;
	}
	xattrtree_init_root_bnode(root_bnode);
	util_update_crc(xattr_btree->vdfs_btree.head_bnode->data,
			get_bnode_size(sbi), NULL, 0);
	util_update_crc(root_bnode->data, get_bnode_size(sbi), NULL, 0);

	return 0;

error_exit:

	log_error("Can't init xattr tree");
	return ret;
}

int get_set_xattrs(struct vdfs_sb_info *sbi, char *path, u64 object_id,
		int has_quota, __u64 curr_size)
{
	int len, ret = 0;
	int set_curr_size = 0;
	char *val = malloc(XATTR_VAL_SIZE);
	if (!val) {
		log_error("MKFS can't allocate enough memory");
		return -ENOMEM;
	}
	char *buffer = malloc(SUPER_PAGE_SIZE_DEFAULT);
	char *name;
	ssize_t size = 0;
	if (!buffer) {
		log_error("MKFS can't allocate enough memory");
		free(val);
		return -ENOMEM;
	}
	memset(buffer, 0, SUPER_PAGE_SIZE_DEFAULT);
	len = listxattr(path, buffer, SUPER_PAGE_SIZE_DEFAULT);
	if (len < 0) {
		if (errno == ENOTSUP) {
			log_warning("Operation list xattr not supported ");
			errno = 0;
		} else if (errno != ENODATA) {
			ret = -errno;
			log_error("Can't list xattr because of %s",
					strerror(errno));
			errno = 0;
		}
		goto exit;
	} else if (len == 0)
		goto exit;

	name = buffer;
	while (len > 0) {
		int name_len = strlen(name) + 1;
		assert(name_len <= len);
		memset(val, 0, XATTR_VAL_SIZE);
		size = getxattr(path, name, val, XATTR_VAL_SIZE);
		if (size < 0) {
			log_error("Can not get xattr %s for %s: %s",
					 name, path, strerror(errno));
			return -1;
		}
		if (has_quota && !memcmp(name, QUOTA_HAS_XATTR, name_len)) {
			size = UUL_MAX_LEN;
			snprintf(val, size, "%019llu",
				curr_size);
			set_curr_size = 1;

		}
		ret = xattrtree_insert(&sbi->xattrtree.vdfs_btree, object_id,
				name, size, val);
		if (ret) {
			log_error("Can't add extended attribute %s for file %s",
					name, path);
			goto exit;
		}
		name += name_len;
		len -= name_len;
	}
	if (has_quota && !set_curr_size) {
		memset(val, 0, XATTR_VAL_SIZE);
		size = UUL_MAX_LEN;
		snprintf(val, size, "%019llu",
			curr_size);
		ret = xattrtree_insert(&sbi->xattrtree.vdfs_btree, object_id,
				QUOTA_HAS_XATTR, size, val);
	}

exit:
	free(buffer);
	free(val);
	return ret;
}
int unpack_xattr(struct vdfs_btree *xattr_tree, char *path, u64 object_id)
{
	int ret = 0;
	char name[EMMCFS_FULL_PATH_LEN];
	struct vdfs_xattrtree_record *record = xattrtree_get_first_record(
			xattr_tree, object_id, EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(record))
		goto exit;
	log_activity("Set xattrs for %s", path);
	while (record->key->object_id == object_id) {
		memset(name, 0, sizeof(name));
		memcpy(name, record->key->name, record->key->name_len);
		if (setxattr(path, name, record->val,
					record->key->gen_key.record_len -
					record->key->gen_key.key_len,
					XATTR_CREATE)) {
				log_error("cannot set xattr %s for file %s:"
						" %s\n",
						 record->key->name, path,
						 strerror(errno));
				return -1;
			}
		ret = xattrtree_get_next_record(record);
		if (ret) {
			if (ret == -ENOENT)
				/* There is no records anymore in the btree,
				 * it is not a error */
				ret = 0;
			goto exit;
		}
	}
exit:
	return ret;
}


int is_quota_xatrr(char *path, char *xattr_buf, int xattr_buf_len)
{
	return getxattr(path, QUOTA_XATTR, xattr_buf, xattr_buf_len);
}


#endif
