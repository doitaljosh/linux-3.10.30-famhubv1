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

#ifndef USER_SPACE
#include <linux/xattr.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
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
	size_t key_len;
	size_t name_len = strlen(name);
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

	/* Align record size by adding pad between key and value */
	key_len = ALIGN(key_len + val_len, 8) - (u16)val_len;

	memcpy(key->gen_key.magic, VDFS_XATTR_REC_MAGIC,
		strlen(VDFS_XATTR_REC_MAGIC));

	key->gen_key.key_len = cpu_to_le16(key_len);
	key->gen_key.record_len = cpu_to_le16(key_len + val_len);

	key->object_id = cpu_to_le64(object_id);
	memcpy(key->name, name, name_len);
	key->name_len = (__u8)name_len;

	memcpy(get_value_pointer(key), value, val_len);

	ret = emmcfs_btree_insert(tree, insert_data, 0);
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
	size_t len;


	key1 = container_of(__key1, struct vdfs_xattrtree_key, gen_key);
	key2 = container_of(__key2, struct vdfs_xattrtree_key, gen_key);

	if (key1->object_id < key2->object_id)
		return -1;
	if (key1->object_id > key2->object_id)
		return 1;

	len = min(key1->name_len, key2->name_len);
	if (len) {
		diff = memcmp(key1->name, key2->name, len);
		if (diff)
			return diff;
	}

	return (int)key1->name_len - (int)key2->name_len;
}

static struct vdfs_xattrtree_key *xattrtree_alloc_key(u64 object_id,
		const char *name)
{
	struct vdfs_xattrtree_key *key;
	size_t name_len = strlen(name);

	if (name_len >= VDFS_XATTR_NAME_MAX_LEN)
		return ERR_PTR(-EINVAL);

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);


	key->object_id = cpu_to_le64(object_id);
	key->name_len = (__u8)name_len;
	memcpy(key->name, name, name_len);

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

	kfree(key);

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

#ifdef CONFIG_VDFS_POSIX_ACL

struct posix_acl *vdfs_get_acl(struct inode *inode, int type)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	struct vdfs_xattrtree_record *record;
	struct posix_acl *acl;
	const char *name;
	size_t size;

	acl = get_cached_acl(inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	switch (type) {
		case ACL_TYPE_ACCESS:
			name = POSIX_ACL_XATTR_ACCESS;
			break;
		case ACL_TYPE_DEFAULT:
			name = POSIX_ACL_XATTR_DEFAULT;
			break;
		default:
			return ERR_PTR(-EINVAL);
	}

	mutex_r_lock(sbi->xattr_tree->rw_tree_lock);
	record = vdfs_xattrtree_find(sbi->xattr_tree, inode->i_ino,
				     name, EMMCFS_BNODE_MODE_RO);
	if (record == ERR_PTR(-ENODATA)) {
		acl = NULL;
	} else if (IS_ERR(record)) {
		acl = ERR_CAST(record);
	} else {
		size = le32_to_cpu(record->key->gen_key.record_len) -
			le32_to_cpu(record->key->gen_key.key_len);
		acl = posix_acl_from_xattr(&init_user_ns, record->val, size);
		vdfs_release_record((struct vdfs_btree_gen_record *) record);
	}
	mutex_r_unlock(sbi->xattr_tree->rw_tree_lock);

	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	return acl;
}

static int vdfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	const char *name;
	size_t size = 0;
	void *data = NULL;
	int ret;

	switch (type) {
		case ACL_TYPE_ACCESS:
			name = POSIX_ACL_XATTR_ACCESS;
			break;
		case ACL_TYPE_DEFAULT:
			name = POSIX_ACL_XATTR_DEFAULT;
			break;
		default:
			return -EINVAL;
	}

	if (acl) {
		size = posix_acl_xattr_size((int)acl->a_count);
		if (size > VDFS_XATTR_VAL_MAX_LEN)
			return -ERANGE;
		data = kmalloc(size, GFP_NOFS);
		if (!data)
			return -ENOMEM;
		ret = posix_acl_to_xattr(&init_user_ns, acl, data, size);
		if (ret < 0)
			goto err_encode;
		size = (size_t)ret;
	}

	vdfs_start_transaction(sbi);
	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);
	ret = xattrtree_remove_record(sbi->xattr_tree, inode->i_ino, name);
	if (!ret || ret == -ENOENT) {
		ret = 0;
		if (acl)
			ret = xattrtree_insert(sbi->xattr_tree, inode->i_ino,
						name, size, data);
	}
	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
	if (!ret) {
		inode->i_ctime = emmcfs_current_time(inode);
		mark_inode_dirty(inode);
	}
	vdfs_stop_transaction(sbi);
err_encode:
	kfree(data);
	if (!ret)
		set_cached_acl(inode, type, acl);
	return ret;
}

static int vdfs_get_acl_xattr(struct inode *inode, int type,
				void *buffer, size_t size)
{
	struct posix_acl *acl;
	int ret;

	if (!IS_POSIXACL(inode) || S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = vdfs_get_acl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (!acl)
		return -ENODATA;
	ret = posix_acl_to_xattr(&init_user_ns, acl, buffer, size);
	posix_acl_release(acl);
	return ret;
}

static int vdfs_set_acl_xattr(struct inode *inode, int type,
				const void *value, size_t size)
{
	struct posix_acl *acl;
	int ret = 0;

	if (!IS_POSIXACL(inode) || S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return value ? -EACCES : 0;
	if (!inode_owner_or_capable(inode))
		return -EPERM;
	acl = posix_acl_from_xattr(&init_user_ns, value, size);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl)
		ret = posix_acl_valid(acl);
	if (!ret)
		ret = vdfs_set_acl(inode, acl, type);
	posix_acl_release(acl);
	return ret;
}

int vdfs_init_acl(struct inode *inode, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	int ret = 0;

	if (S_ISLNK(inode->i_mode))
		goto out;
	if (IS_POSIXACL(dir)) {
		acl = vdfs_get_acl(dir, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	if (acl) {
		if (S_ISDIR(inode->i_mode)) {
			ret = vdfs_set_acl(inode, acl, ACL_TYPE_DEFAULT);
			if (ret)
				goto out;
		}
		ret = posix_acl_create(&acl, GFP_NOFS, &inode->i_mode);
		if (ret < 0)
			goto out;
		if (ret > 0)
			ret = vdfs_set_acl(inode, acl, ACL_TYPE_ACCESS);
	} else {
		inode->i_mode &= (umode_t)~current_umask();
	}
out:
	posix_acl_release(acl);
	return ret;
}

int vdfs_chmod_acl(struct inode *inode)
{
	struct posix_acl *acl;
	int ret;

	if (S_ISLNK(inode->i_mode) || !IS_POSIXACL(inode))
		return 0;
	acl = vdfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR_OR_NULL(acl))
		return PTR_ERR(acl);
	ret = posix_acl_chmod(&acl, GFP_NOFS, inode->i_mode);
	if (ret)
		return ret;
	ret = vdfs_set_acl(inode, acl, ACL_TYPE_ACCESS);
	posix_acl_release(acl);
	return ret;
}

#else

static int vdfs_get_acl_xattr(struct inode *inode, int type,
				void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static int vdfs_set_acl_xattr(struct inode *inode, int type,
				const void *value, size_t size)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_VDFS_POSIX_ACL */

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

	if (!strcmp(name, POSIX_ACL_XATTR_DEFAULT))
		return vdfs_set_acl_xattr(inode, ACL_TYPE_DEFAULT, value, size);
	if (!strcmp(name, POSIX_ACL_XATTR_ACCESS))
		return vdfs_set_acl_xattr(inode, ACL_TYPE_ACCESS, value, size);

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
	if (!ret) {
		inode->i_ctime = emmcfs_current_time(inode);
		mark_inode_dirty(inode);
	}
	vdfs_stop_transaction(sbi);

	return ret;
}

static struct vdfs_btree *select_tree(struct inode *inode)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_btree *btree;
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);

	if ((inode_info->installed_btrees) && (inode_info->record_type !=
			VDFS_CATALOG_RO_IMAGE_ROOT)) {
		if (!vdfs_is_tree_alive(inode))
			return ERR_PTR(-ENOENT);
		btree = inode_info->installed_btrees->xattr_tree;
	} else
		btree = sbi->xattr_tree;

	return btree;
}


static inline u64 get_disk_inode_no(struct inode *inode)
{
	u64 result = inode->i_ino;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	if (inode_info->installed_btrees)
		result -= inode_info->installed_btrees->cat_tree->start_ino;
	return result;
}

ssize_t vdfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t buf_size)
{
	struct vdfs_xattrtree_record *record;
	struct inode *inode = dentry->d_inode;
	ssize_t size;
	struct vdfs_btree *btree;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	if (check_xattr_prefix(name))
		return -EOPNOTSUPP;

	btree = select_tree(inode);
	if (IS_ERR(btree))
		return PTR_ERR(btree);

	if (!strcmp(name, POSIX_ACL_XATTR_DEFAULT))
		return vdfs_get_acl_xattr(inode, ACL_TYPE_DEFAULT,
						buffer, buf_size);
	if (!strcmp(name, POSIX_ACL_XATTR_ACCESS))
		return vdfs_get_acl_xattr(inode, ACL_TYPE_ACCESS,
						buffer, buf_size);

	if (btree->btree_type != VDFS_BTREE_INST_XATTR)
		mutex_r_lock(btree->rw_tree_lock);


	record = vdfs_xattrtree_find(btree, get_disk_inode_no(inode), name,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		if (btree->btree_type != VDFS_BTREE_INST_XATTR)
			mutex_r_unlock(btree->rw_tree_lock);
		return PTR_ERR(record);
	}

	size = (ssize_t)le32_to_cpu(record->key->gen_key.record_len) -
		(ssize_t)le32_to_cpu(record->key->gen_key.key_len);
	if (!buffer)
		goto exit;

	if (size > (ssize_t)buf_size) {
		size = -ERANGE;
		goto exit;
	}

	memcpy(buffer, record->val, (size_t)size);
exit:
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	if (btree->btree_type != VDFS_BTREE_INST_XATTR)
		mutex_r_unlock(btree->rw_tree_lock);
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
	struct vdfs_xattrtree_record *record;
	struct vdfs_btree *btree;
	ssize_t size = 0;
	int ret = 0;
	u64 disk_ino_no = get_disk_inode_no(inode);

	btree = select_tree(inode);
	if (IS_ERR(btree))
		return PTR_ERR(btree);

	if (btree->btree_type != VDFS_BTREE_INST_XATTR)
		mutex_r_lock(btree->rw_tree_lock);
	record = xattrtree_get_first_record(btree, disk_ino_no,
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(record)) {
		if (btree->btree_type != VDFS_BTREE_INST_XATTR)
			mutex_r_unlock(btree->rw_tree_lock);
		if (PTR_ERR(record) == -ENOENT)
			return 0; /* no exteneded attributes */
		else
			return PTR_ERR(record);
	}

	while (!ret && le64_to_cpu(record->key->object_id) == disk_ino_no) {
		size_t name_len = (size_t)record->key->name_len + 1lu;

		if (buffer) {
			if (buf_size < name_len) {
				ret = -ERANGE;
				break;
			}
			memcpy(buffer, record->key->name, name_len - 1lu);
			buffer[name_len - 1] = 0;
			buf_size -= name_len;
			buffer += name_len;
		}

		size += (ssize_t)name_len;

		ret = xattrtree_get_next_record(record);
	}

	if (ret == -ENOENT)
		/* It is normal if there is no more records in the btree */
		ret = 0;

	vdfs_release_record((struct vdfs_btree_gen_record *) record);
	if (btree->btree_type != VDFS_BTREE_INST_XATTR)
		mutex_r_unlock(btree->rw_tree_lock);

	return ret ? ret : size;
}

int vdfs_init_security_xattrs(struct inode *inode,
		const struct xattr *xattr_array, void *fs_data)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	const struct xattr *xattr;
	char *name = NULL;
	size_t name_len;
	int ret = 0;

	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);
	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		name_len = strlen(xattr->name) + 1lu;
		name = krealloc(name, XATTR_SECURITY_PREFIX_LEN +
				name_len, GFP_KERNEL);
		ret = -ENOMEM;
		if (!name)
			break;
		memcpy(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN);
		memcpy(name + XATTR_SECURITY_PREFIX_LEN, xattr->name,
				name_len);
#if 0
		/* always called for new inode */
		ret = xattrtree_remove_record(sbi->xattr_tree,
						inode->i_ino, name);
		if (ret && ret != -ENOENT)
			break;
#endif
		ret = 0;
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
void vdfs_update_quota(struct inode *inode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int qi = EMMCFS_I(inode)->quota_index;
	struct vdfs_xattrtree_record *record;

	if (qi == -1 || !sbi->quotas[qi].dirty)
		return;

	mutex_w_lock(sbi->xattr_tree->rw_tree_lock);
	record = vdfs_xattrtree_find(sbi->xattr_tree, sbi->quotas[qi].ino,
			QUOTA_HAS_XATTR, EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(record))
		goto out;

	mutex_lock(&sbi->fsm_info->lock);
	sbi->quotas[qi].dirty = 0;
	snprintf(record->val, UUL_MAX_LEN, "%019llu", sbi->quotas[qi].has);
	mutex_unlock(&sbi->fsm_info->lock);
	emmcfs_mark_bnode_dirty(VDFS_BTREE_REC_I
			((void *) record)->rec_pos.bnode);
	vdfs_release_record((struct vdfs_btree_gen_record *) record);
out:
	mutex_w_unlock(sbi->xattr_tree->rw_tree_lock);
}
#endif

#endif /* !USER_SPACE */

#ifdef USER_SPACE
void dummy_xattrtree_record_init(struct vdfs_xattrtree_key *xattr_record)
{
	int key_len, name_len;

	memset(xattr_record, 0, sizeof(*xattr_record));
	set_magic(xattr_record->gen_key.magic, XATTRTREE_LEAF);

	name_len = strlen(VDFS_XATTRTREE_ROOT_REC_NAME);
	key_len = sizeof(*xattr_record) - sizeof(xattr_record->name) + name_len;

	key_len = ALIGN(key_len, 8);

	xattr_record->gen_key.key_len = cpu_to_le32(key_len);
	/* Xattr root record has no value, so record_len == key_len */
	xattr_record->gen_key.record_len = key_len;
	xattr_record->name_len = name_len;
	memcpy(xattr_record->name, VDFS_XATTRTREE_ROOT_REC_NAME, name_len);
}

static void xattrtree_init_root_bnode(struct emmcfs_bnode *root_bnode)
{
	struct vdfs_xattrtree_key xattr_record;

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
			VDFS_XATTR_KEY_MAX_LEN +
			VDFS_XATTR_VAL_MAX_LEN);

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
		int name_len = strlen(name);
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
		name += (name_len + 1);
		len -= (name_len + 1);
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
	if (IS_ERR(record)) {
		if (PTR_ERR(record) == -ENOENT)
			return 0;
		return PTR_ERR(record);
	}

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
				ret = -errno;
				goto exit;
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
	if (!IS_ERR(record))
		vdfs_release_record((struct vdfs_btree_gen_record *)record);
	return ret;
}


int is_quota_xatrr(char *path, char *xattr_buf, int xattr_buf_len)
{
	return getxattr(path, QUOTA_XATTR, xattr_buf, xattr_buf_len);
}


#endif
