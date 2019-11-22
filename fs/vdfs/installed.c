#include <linux/file.h>

#include "emmcfs.h"
#include "installed.h"


/* static struct lock_class_key installed_lock_key; */


static struct vdfs_btree *init_installed_tree(struct vdfs_sb_info *sbi,
		enum emmcfs_btree_type type, struct inode *image_inode,
		struct inode *root_inode)
{
	struct vdfs_btree *btree = NULL;
	struct emmcfs_inode_info *root_info = EMMCFS_I(root_inode);
	int ret = 0;

	btree = kzalloc(sizeof(struct vdfs_btree), GFP_KERNEL);
	if (!btree)
		return ERR_PTR(-ENOMEM);

	btree->btree_type = type;

	switch (type) {
	case VDFS_BTREE_INST_CATALOG:
		btree->comp_fn = emmcfs_cattree_cmpfn;
		btree->start_ino = root_info->vfs_inode.i_ino;
		btree->tree_metadata_offset =
				root_info->vdfs_image.catalog_tree_offset;
		btree->max_record_len =
			ALIGN(sizeof(struct emmcfs_cattree_key), 8) +
			sizeof(struct vdfs_catalog_file_record);
		btree->start_ino = root_info->vfs_inode.i_ino;
	break;
	case VDFS_BTREE_INST_EXT:
		btree->comp_fn = emmcfs_exttree_cmpfn;
		btree->tree_metadata_offset =
				root_info->vdfs_image.extents_tree_offset;
		btree->max_record_len = sizeof(struct emmcfs_exttree_key) +
				sizeof(struct emmcfs_exttree_record);
	break;
	case VDFS_BTREE_INST_XATTR:
		btree->comp_fn = vdfs_xattrtree_cmpfn;
		btree->tree_metadata_offset =
				root_info->vdfs_image.xattr_tree_offset;
		btree->max_record_len = VDFS_XATTR_KEY_MAX_LEN +
				VDFS_XATTR_VAL_MAX_LEN;
	break;
	default:
		BUG();
	}

	ret = fill_btree(sbi, btree, image_inode);
	if (ret) {
		EMMCFS_ERR("fill_btree %ld fail", image_inode->i_ino);
		kfree(btree);
		return ERR_PTR(ret);
	}

	return btree;
}

static struct installed_info *init_installed_image(struct vdfs_sb_info *sbi,
		struct inode *root)
{
	struct emmcfs_inode_info *root_info = EMMCFS_I(root);
	struct inode *image_inode = NULL;
	struct installed_info *installed_info = NULL;
	struct vdfs_btree *tree = NULL;

	BUG_ON(root_info->installed_btrees != NULL);

	installed_info = kzalloc(sizeof(*installed_info), GFP_KERNEL);
	if (!installed_info)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&installed_info->list);
	image_inode =  vdfs_get_image_inode(sbi,
			root_info->vdfs_image.parent_id,
			root_info->vdfs_image.image_name,
			strlen(root_info->vdfs_image.image_name));
	if (IS_ERR(image_inode)) {
		tree = (struct vdfs_btree *)image_inode;
		EMMCFS_ERR("image: parent_id %lu name_length %d, image_name %s",
			(long unsigned int)root_info->vdfs_image.parent_id,
			(int)strlen(root_info->vdfs_image.image_name),
			root_info->vdfs_image.image_name);
		goto fail;
	}

	tree = init_installed_tree(sbi, VDFS_BTREE_INST_CATALOG, image_inode,
			root);
	if (IS_ERR(tree))
		goto fail_iput;
	/* add installed catalog tree */
	installed_info->cat_tree = tree;
	tree = init_installed_tree(sbi, VDFS_BTREE_INST_EXT, image_inode, root);
	if (IS_ERR(tree)) {
		vdfs_put_btree(installed_info->cat_tree, 0);
		goto fail_iput;
	}
	/* add extents overflow installed tree */
	installed_info->ext_tree = tree;
	tree = init_installed_tree(sbi, VDFS_BTREE_INST_XATTR, image_inode,
			root);
	if (IS_ERR(tree)) {
		vdfs_put_btree(installed_info->cat_tree, 0);
		vdfs_put_btree(installed_info->ext_tree, 0);
		goto fail_iput;
	}
	/* add extended attribute installed tree */
	installed_info->xattr_tree = tree;
	atomic_set(&installed_info->open_count, 0);
	list_add(&installed_info->list, &sbi->installed_list);
	installed_info->start_ino = root->i_ino;

	return installed_info;
fail_iput:
	iput(image_inode);
fail:
	kfree(installed_info);
	return (struct installed_info *)tree;
}

static struct installed_info *lookup_in_opened_list(struct vdfs_sb_info *sbi,
		ino_t ino)
{
	struct list_head *pos;
	list_for_each(pos, &sbi->installed_list) {
		struct installed_info *installed;
		installed = list_entry(pos, struct installed_info, list);
		if (ino == installed->cat_tree->start_ino)
			return installed;
	}
	return NULL;
}

struct installed_info *vdfs_get_installed_tree(struct inode *root_inode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(root_inode->i_sb);
	struct installed_info *installed = NULL;

	BUG_ON(EMMCFS_I(root_inode)->record_type != VDFS_CATALOG_RO_IMAGE_ROOT);

	mutex_lock(&sbi->installed_list_lock);
	installed = lookup_in_opened_list(sbi, root_inode->i_ino);
	if (!installed)
		installed = init_installed_image(sbi, root_inode);
	mutex_unlock(&sbi->installed_list_lock);

	return installed;
}


void destroy_installed_image(struct installed_info *installed_image)
{
	struct inode *image_inode = installed_image->cat_tree->inode;

	vdfs_put_btree(installed_image->cat_tree, 0);
	vdfs_put_btree(installed_image->ext_tree, 0);
	vdfs_put_btree(installed_image->xattr_tree, 0);

	if (installed_image->start_ino)
		iput(image_inode);

	kfree(installed_image);
}

void vdfs_destroy_installed_list(struct vdfs_sb_info *sbi)
{
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, &sbi->installed_list) {
		struct installed_info *ptr =
			list_entry(pos, struct installed_info, list);
		list_del(pos);
		destroy_installed_image(ptr);
	}
}
static int install_ro_image(struct file *parent_dir, struct file *image_file,
		struct vdfs_image_install *vdfs_image)
{
	int ret = 0;
	struct inode *parent_inode = parent_dir->f_path.dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(parent_inode->i_sb);
	struct vdfs_image_install_point *install_point = NULL;
	ino_t ino;
	struct vdfs_cattree_record *record = NULL;
	struct inode *install_point_inode = NULL;
	struct inode *image_inode = image_file->f_dentry->d_inode;
	struct emmcfs_inode_info *image_info = EMMCFS_I(image_inode);

	ret = vdfs_get_free_inode(sbi, &ino, vdfs_image->image_inodes_count);
	if (ret)
		return ret;

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	ret = vdfs_cattree_insert_llink(sbi->catalog_tree,
			ino + vdfs_image->image_inodes_count - 1,
			parent_inode->i_ino, vdfs_image->install_dir,
			strlen(vdfs_image->install_dir));

	if (ret) {
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		vdfs_free_inode_n(sbi, ino,
				(int)vdfs_image->image_inodes_count);
		return ret;
	}

	record = vdfs_cattree_place_record(sbi->catalog_tree, ino,
			parent_inode->i_ino, vdfs_image->install_dir,
			strlen(vdfs_image->install_dir),
			VDFS_CATALOG_RO_IMAGE_ROOT);

	if (IS_ERR(record)) {
		vdfs_cattree_remove_llink(sbi->catalog_tree,
			ino + vdfs_image->image_inodes_count - 1,
			parent_inode->i_ino, vdfs_image->install_dir,
			strlen(vdfs_image->install_dir));

		vdfs_free_inode_n(sbi, ino,
				(int)vdfs_image->image_inodes_count);
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		return PTR_ERR(record);
	}

	install_point = (struct vdfs_image_install_point *)record->val;



	install_point->catalog_tree_offset =
			cpu_to_le64(vdfs_image->catalog_tree_offset);
	install_point->extents_tree_offset =
			cpu_to_le64(vdfs_image->extents_tree_offset);
	install_point->xattr_tree_offset =
			cpu_to_le64(vdfs_image->xattr_tree_offset);
	install_point->ino_count = cpu_to_le32(vdfs_image->image_inodes_count);
	install_point->image_parent_id = image_info->parent_id;

	install_point->common.flags = 0;
	install_point->common.links_count = cpu_to_le64(1);
	install_point->common.total_items_count =
			cpu_to_le64(vdfs_image->image_inodes_count -
			VDFS_1ST_FILE_INO);
	install_point->common.file_mode = cpu_to_le16(parent_inode->i_mode &
			(~(S_IWOTH | S_IWGRP | S_IWUSR)));

	install_point->common.uid = cpu_to_le32(
			from_kuid(&init_user_ns, current_fsuid()));
	if (parent_inode->i_mode & S_ISGID)
		install_point->common.gid =
				cpu_to_le32(i_gid_read(parent_inode));
	 else
		install_point->common.gid = cpu_to_le32(
				from_kgid(&init_user_ns, current_fsgid()));

	install_point->common.creation_time =
			vdfs_encode_time(emmcfs_current_time(parent_inode));

	memcpy(install_point->name, image_info->name,
			strlen(image_info->name));
	install_point->name_len = (u8)strlen(image_info->name);

	install_point_inode = get_inode_from_record(record, NULL);
	vdfs_release_dirty_record((struct vdfs_btree_gen_record *)record);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	if (IS_ERR(install_point_inode))
		BUG();

	iput(install_point_inode);
	return 0;
}

struct inode *vdfs_ro_image_installation(struct vdfs_sb_info *sbi,
		struct file *parent_dir, unsigned long arg)
{
	struct vdfs_image_install *vdfs_image = NULL;
	struct fd image_file;
	struct inode *image_inode = NULL;
	int ret = 0;
	struct inode *parent = parent_dir->f_path.dentry->d_inode;
	vdfs_image = kmalloc(sizeof(*vdfs_image), GFP_KERNEL);
	if (!vdfs_image)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(vdfs_image,
		(struct vdfs_image_install __user *)arg,
			sizeof(*vdfs_image))) {
		ret = -EFAULT;
		goto exit;
	}

	image_file = fdget((unsigned int)vdfs_image->image_fd);
	if (!image_file.file) {
		ret = -EBADF;
		goto exit_dput;
	}

	image_inode = image_file.file->f_dentry->d_inode;
	filemap_write_and_wait(image_inode->i_mapping);
	invalidate_bdev(sbi->sb->s_bdev);

	mutex_lock_nested(&parent->i_mutex, I_MUTEX_PARENT);
	mutex_lock(&image_inode->i_mutex);
	vdfs_start_transaction(sbi);
	ret = install_ro_image(parent_dir, image_file.file, vdfs_image);
	if (!ret) {
		shrink_dcache_parent(parent_dir->f_path.dentry);
		vdfs_update_image_and_dir(parent, image_inode);
	}
	vdfs_stop_transaction(sbi);
	mutex_unlock(&image_inode->i_mutex);
	mutex_unlock(&parent->i_mutex);
	if (ret)
		goto exit_dput;
	kfree(vdfs_image);
	return image_inode;
exit_dput:
	fdput(image_file);
exit:
	kfree(vdfs_image);
	return ERR_PTR(ret);

}

static int remove_install_point(struct inode *inode)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int ret = 0;
	struct dentry *de;


	ret = vdfs_cattree_remove_llink(sbi->catalog_tree, inode->i_ino +
			inode_info->vdfs_image.image_inodes_count - 1,
			inode_info->parent_id, inode_info->name,
			strlen(inode_info->name));


	if (ret)
		return ret;

	ret = vdfs_cattree_remove(sbi->catalog_tree, inode->i_ino,
			inode_info->parent_id, (const char *)inode_info->name,
		strlen(inode_info->name), inode_info->record_type);

	if (ret) {
		vdfs_cattree_insert_llink(sbi->catalog_tree,
			inode->i_ino +
			inode_info->vdfs_image.image_inodes_count
			- 1, inode_info->parent_id, inode_info->name,
				strlen(inode_info->name));
		return ret;
	}

	drop_nlink(inode);
	remove_inode_hash(inode);

	ret = vdfs_free_inode_n(sbi, inode->i_ino,
			(int)inode_info->vdfs_image.image_inodes_count);
	if (ret)
		EMMCFS_ERR("Cannot free installed inodes numbers");

	ret = vdfs_unlock_source_image(sbi, inode_info->vdfs_image.parent_id,
		inode_info->vdfs_image.image_name,
		strlen(inode_info->vdfs_image.image_name));

	if (ret)
		EMMCFS_ERR("Cannot clear immutable flag at image file");
	ret = 0;

	if (inode_info->installed_btrees) {
		struct installed_info *ptr =
				list_entry(&inode_info->installed_btrees->list,
						struct installed_info, list);
		list_del(&inode_info->installed_btrees->list);
		vdfs_release_image_inodes(sbi, inode->i_ino,
				inode_info->vdfs_image.image_inodes_count);
		destroy_installed_image(ptr);
	}

	de = d_find_alias(inode);
	if (de) {
		dput(de);
		d_drop(de);
	}

	return ret;
}

int vdfs_remove_ro_image(struct file *install_dir)
{
	struct inode *install_point_inode = install_dir->f_dentry->d_inode;
	struct emmcfs_inode_info *inode_info = EMMCFS_I(install_point_inode);
	struct vdfs_sb_info *sbi = VDFS_SB(install_point_inode->i_sb);
	int ret = 0;

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);
	if (inode_info->installed_btrees) {
		if (atomic_read(&inode_info->installed_btrees->open_count)) {
			mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
			return -EBUSY;
		}
	}

	ret = remove_install_point(install_point_inode);
	if (!ret)
		vdfs_update_parent_dir(install_dir);

	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
	return ret;
}

int vdfs_fill_image_root(struct vdfs_cattree_record *record,
		struct inode *inode)
{
	struct vdfs_image_install_point *install_point = record->val;
	struct vdfs_image_install *vdfs_image = &EMMCFS_I(inode)->vdfs_image;
	char *new_name;
	struct emmcfs_cattree_key *key = record->key;

	memset(vdfs_image, 0x0, sizeof(*vdfs_image));

	new_name = kmalloc(key->name_len + 1lu, GFP_KERNEL);
	if (!new_name)
		return -ENOMEM;

	memcpy(new_name, key->name, key->name_len);
	new_name[key->name_len] = 0;
	EMMCFS_BUG_ON(EMMCFS_I(inode)->name);
	EMMCFS_I(inode)->name = new_name;
	EMMCFS_I(inode)->parent_id = le64_to_cpu(key->parent_id);

	memcpy(&vdfs_image->image_name, &install_point->name,
			install_point->name_len);
	vdfs_image->parent_id = le64_to_cpu(install_point->image_parent_id);
	vdfs_image->image_inodes_count = le32_to_cpu(install_point->ino_count);
	vdfs_image->catalog_tree_offset =
			le64_to_cpu(install_point->catalog_tree_offset);
	vdfs_image->extents_tree_offset =
			le64_to_cpu(install_point->extents_tree_offset);
	vdfs_image->xattr_tree_offset =
			le64_to_cpu(install_point->xattr_tree_offset);

	return 0;
}
