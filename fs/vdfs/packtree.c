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

#include <linux/buffer_head.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include "emmcfs.h"
#include "cattree.h"
#include "packtree.h"
#include "debug.h"

static struct lock_class_key packtree_lock_key;




/**
 * @brief		Uninstall packtree image function.
 * @param [in]	inode	Pointer to inode
 * @param [in]	params	Pointer to structure with information about
 *			parent directory of installed packtree image.
 * @return		Returns error codes
 */
int vdfs_uninstall_packtree(struct file *filp)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct emmcfs_inode_info *install_point = EMMCFS_I(inode);
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int rc = 0;
	struct dentry *de;
	ino_t start_ino = (ino_t)install_point->ptree.root.start_ino;

	if (install_point->record_type != VDFS_CATALOG_PTREE_ROOT)
		return -EINVAL;

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	if (EMMCFS_I(inode)->ptree.tree_info) {

		install_point->ptree.tree_info->params.start_ino = 0;

		if ((atomic_read(&(install_point->ptree.tree_info->open_count))
				!= 0)) {
			install_point->ptree.tree_info->params.start_ino =
					start_ino;
			mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
			return -EBUSY;
		}
	}

	rc = vdfs_cattree_remove_llink(sbi->catalog_tree, inode->i_ino +
		install_point->ptree.root.pmc.inodes_cnt,
		install_point->parent_id, install_point->name,
		strlen(install_point->name));

	if (rc)
		goto err;

	rc = vdfs_cattree_remove(sbi->catalog_tree, inode->i_ino,
		install_point->parent_id, (const char *)install_point->name,
		strlen(install_point->name), install_point->record_type);

	if (rc)
		vdfs_cattree_insert_llink(sbi->catalog_tree, inode->i_ino +
			install_point->ptree.root.pmc.inodes_cnt,
			install_point->parent_id, install_point->name,
			strlen(install_point->name));

err:	if (rc) {
		if (EMMCFS_I(inode)->ptree.tree_info)
			install_point->ptree.tree_info->params.start_ino =
					start_ino;
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		goto exit;
	}

	rc = vdfs_unlock_source_image(sbi,
			install_point->ptree.root.source_image.parent_id,
			install_point->ptree.root.source_image.name,
			install_point->ptree.root.source_image.name_len);
	if (rc)
		EMMCFS_ERR("Cannot clear immutable flag at image file");

	vdfs_update_parent_dir(filp);
	drop_nlink(inode);
	remove_inode_hash(inode);
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	rc = vdfs_free_inode_n(VDFS_SB(inode->i_sb),
			(ino_t)EMMCFS_I(inode)->ptree.root.start_ino,
			(int)EMMCFS_I(inode)->ptree.root.pmc.inodes_cnt + 1);

	if (rc)
		EMMCFS_ERR("Cannot free installed squahfs image inodes number");

	sbi->files_count -= EMMCFS_I(inode)->ptree.root.pmc.inodes_cnt;

	vdfs_release_image_inodes(sbi, start_ino,
			EMMCFS_I(inode)->ptree.root.pmc.inodes_cnt);
		if (EMMCFS_I(inode)->ptree.tree_info) {
			iput(EMMCFS_I(inode)->ptree.tree_info->tree->inode);
			iput(EMMCFS_I(inode)->ptree.tree_info->unpacked_inode);
		}

	de = d_find_alias(inode);
	if (de) {
		dput(de);
		d_drop(de);
	}

exit:
	return rc;
}

static int create_chunk_offsets_table(
		struct installed_packtree_info *packtree_info)
{
	int rc, i;
	unsigned int len = (packtree_info->params.pmc.chunk_cnt + 1) << 3;

	packtree_info->chunk_table = vzalloc((unsigned long)len);
	if (!packtree_info->chunk_table)
		return -ENOMEM;

	rc = read_squashfs_image_simple(packtree_info->tree->inode,
		packtree_info->params.pmc.chtab_off, len,
		packtree_info->chunk_table);
	if (rc) {
		vfree(packtree_info->chunk_table);
		return rc;
	}
	for (i = 0; i <= (int)packtree_info->params.pmc.chunk_cnt; i++)
		packtree_info->chunk_table[i] =
				le64_to_cpu(packtree_info->chunk_table[i]);
	return rc;
}

/**
 * The vdfs unpack inode address space operations.
 */
static const struct address_space_operations unpack_inode_aops = {
};

static struct installed_packtree_info *init_packtree(struct vdfs_sb_info *sbi,
		struct inode *packtree_root)
{
	struct inode *packtree_image_inode = NULL;
	struct installed_packtree_info *packtree_info = NULL;
	struct emmcfs_inode_info *root_inode_info = EMMCFS_I(packtree_root);
	int ret = 0;

	BUG_ON(root_inode_info->record_type != VDFS_CATALOG_PTREE_ROOT);
	BUG_ON(root_inode_info->ptree.tree_info != NULL);

	if (memcmp(root_inode_info->ptree.root.packtree_layout_version,
			VDFS_PACK_METADATA_VERSION,
			strlen(VDFS_PACK_METADATA_VERSION))) {
		printk(KERN_ERR "Packtree layout mismatch:\n"
			"Image file was expanded and installed by"
			" wrong version of tune.vdfs utility.");
		return ERR_PTR(-EINVAL);
	}

	packtree_info = kzalloc(sizeof(*packtree_info), GFP_KERNEL);
	if (!packtree_info)
		return ERR_PTR(-ENOMEM);

	packtree_info->tree = kzalloc(sizeof(struct vdfs_btree), GFP_KERNEL);
	if (!packtree_info->tree) {
		kfree(packtree_info);
		return ERR_PTR(-ENOMEM);
	}
#ifdef CONFIG_VDFS_DEBUG
	packtree_info->print_once = 0;
#endif


	packtree_info->tree->btree_type = VDFS_BTREE_PACK;
	packtree_info->tree->max_record_len =
			(unsigned short)vdfs_get_packtree_max_record_size();

	packtree_info->tree->tree_metadata_offset =
			root_inode_info->ptree.root.pmc.packoffset;
	packtree_info->tree->start_ino =
			(ino_t)root_inode_info->ptree.root.start_ino;

	memcpy(&packtree_info->params, &root_inode_info->ptree.root,
			sizeof(struct vdfs_pack_insert_point_info));
	packtree_image_inode = vdfs_get_image_inode(sbi,
			packtree_info->params.source_image.parent_id,
			packtree_info->params.source_image.name,
			packtree_info->params.source_image.name_len);
	if (IS_ERR(packtree_image_inode)) {
		ret = PTR_ERR(packtree_image_inode);
		EMMCFS_ERR(" get_packtree_image_inode fail");
		goto fail;
	}

	ret = fill_btree(sbi, packtree_info->tree, packtree_image_inode);
	if (ret) {
		EMMCFS_ERR("fill_btree %ld fail",
				packtree_image_inode->i_ino);
		iput(packtree_image_inode);
		goto fail;
	}
	packtree_info->tree->comp_fn = test_option(sbi, CASE_INSENSITIVE) ?
		emmcfs_cattree_cmpfn_ci : emmcfs_cattree_cmpfn;

	lockdep_set_class(&packtree_info->tree->rw_tree_lock,
			&packtree_lock_key);

	ret = create_chunk_offsets_table(packtree_info);
	if (ret) {
		vdfs_put_btree(packtree_info->tree, 1);
		kfree(packtree_info);
		return ERR_PTR(ret);
	}

	INIT_LIST_HEAD(&packtree_info->list);
	list_add(&packtree_info->list, &sbi->packtree_images.list);

	if (!packtree_info->unpacked_inode)
		packtree_info->unpacked_inode =
			iget_locked(sbi->sb,
				(unsigned long)
				packtree_info->params.start_ino + 1lu);
	if (!packtree_info->unpacked_inode) {
		ret = -ENOMEM;
		vdfs_put_btree(packtree_info->tree, 1);
		kfree(packtree_info);
		return ERR_PTR(ret);
	}
	if (!(packtree_info->unpacked_inode->i_state & I_NEW))
		return packtree_info;

	packtree_info->unpacked_inode->i_mode = 0;
	EMMCFS_I(packtree_info->unpacked_inode)->record_type =
			VDFS_CATALOG_UNPACK_INODE;
	unlock_new_inode(packtree_info->unpacked_inode);
	packtree_info->unpacked_inode->i_mapping->a_ops = &unpack_inode_aops;
	i_size_write(packtree_info->unpacked_inode, 0);

	return packtree_info;
fail:
	kfree(packtree_info->tree);
	kfree(packtree_info);
	return ERR_PTR(ret);
}

static struct installed_packtree_info *lookup_in_opened_list(
		struct vdfs_sb_info *sbi, __u64 i_ino)
{
	struct list_head *pos;

	list_for_each(pos, &sbi->packtree_images.list) {
			struct installed_packtree_info *packtree_info =
			(struct installed_packtree_info *)list_entry(pos,
					struct installed_packtree_info, list);

			if (i_ino == packtree_info->params.start_ino)
				return packtree_info;
		}
	return NULL;
}

struct installed_packtree_info *vdfs_get_packtree(struct inode *root_inode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(root_inode->i_sb);
	struct installed_packtree_info *pack_tree = NULL;

	BUG_ON(EMMCFS_I(root_inode)->record_type != VDFS_CATALOG_PTREE_ROOT);

	mutex_lock(&sbi->packtree_images.lock_pactree_list);
	pack_tree = lookup_in_opened_list(sbi, root_inode->i_ino);
	if (!pack_tree)
		pack_tree = init_packtree(sbi, root_inode);
	mutex_unlock(&sbi->packtree_images.lock_pactree_list);

	return pack_tree;
}


void destroy_packtree(struct installed_packtree_info *ptr)
{
	if (ptr->params.start_ino) {
		vdfs_put_btree(ptr->tree, 1);
		iput(ptr->unpacked_inode);
	} else
		vdfs_put_btree(ptr->tree, 0);
	vfree(ptr->chunk_table);
	kfree(ptr);
}

int *destroy_packtrees_list(struct vdfs_sb_info *sbi)
{
	struct list_head *pos, *q;

	mutex_lock(&sbi->packtree_images.lock_pactree_list);
	list_for_each_safe(pos, q, &sbi->packtree_images.list) {
		struct installed_packtree_info *ptr =
			list_entry(pos, struct installed_packtree_info, list);
		list_del(pos);
		destroy_packtree(ptr);
	}
	mutex_unlock(&sbi->packtree_images.lock_pactree_list);
	return NULL;
}


/**
 * @brief			Create and and to catalog Btree packed tree
 *				insertion point.
 * @param [in]	parent_dir	Intallation point parent file
 * @param [in]	image_file	Packed image file
 * @param [in]	pm		Installation parameters
 * @return			Returns 0 on success, errno on failure
 */
int vdfs_install_packtree(struct file *parent_dir, struct file *image_file,
		struct ioctl_install_params *pm)
{
	int ret = 0;
	struct emmcfs_inode_info *image_inode =
			EMMCFS_I(image_file->f_dentry->d_inode);
	struct inode *parent_inode = parent_dir->f_path.dentry->d_inode;
	struct vdfs_sb_info *sbi = VDFS_SB(parent_inode->i_sb);
	struct vdfs_cattree_record *record = NULL;
	struct vdfs_pack_insert_point_value *ipv;
	struct timespec curr_time = emmcfs_current_time(parent_inode);
	struct inode *install_point_inode = NULL;
	ino_t ino = 0;

	/* +2 becouse of: +1 - for insertion point and +1 for unpacked inode */
	ret = vdfs_get_free_inode(sbi, &ino, pm->pmc.inodes_cnt + 1);
	if (ret)
		return ret;

	mutex_w_lock(sbi->catalog_tree->rw_tree_lock);

	ret = vdfs_cattree_insert_llink(sbi->catalog_tree,
			ino + pm->pmc.inodes_cnt, parent_inode->i_ino,
			pm->dst_dir_name, strlen(pm->dst_dir_name));
	if (ret) {
		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		vdfs_free_inode_n(sbi, ino, (int)pm->pmc.inodes_cnt + 1);
		return ret;
	}

	record = vdfs_cattree_place_record(sbi->catalog_tree, ino,
			parent_inode->i_ino, pm->dst_dir_name,
			strlen(pm->dst_dir_name), VDFS_CATALOG_PTREE_ROOT);

	if (IS_ERR(record)) {
		vdfs_cattree_remove_llink(sbi->catalog_tree,
				ino + pm->pmc.inodes_cnt,
				parent_inode->i_ino, pm->dst_dir_name,
				strlen(pm->dst_dir_name));

		mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);
		vdfs_free_inode_n(sbi, ino, (int)pm->pmc.inodes_cnt + 1);
		return PTR_ERR(record);
	}

	ipv = (struct vdfs_pack_insert_point_value *)record->val;

	ipv->common.links_count = cpu_to_le64(1);
	ipv->common.size = cpu_to_le64(pm->pmc.inodes_cnt);
	ipv->common.file_mode = cpu_to_le16(parent_inode->i_mode);
	ipv->common.uid = cpu_to_le32(
			from_kuid(&init_user_ns, current_fsuid()));
	if (parent_inode->i_mode & S_ISGID)
		ipv->common.gid = cpu_to_le32(i_gid_read(parent_inode));
	else
		ipv->common.gid = cpu_to_le32(
				from_kgid(&init_user_ns, current_fsgid()));

	ipv->common.creation_time = vdfs_encode_time(curr_time);
	ipv->start_ino = cpu_to_le64(ino);
	memcpy(ipv->pmc.packtree_layout_version,
			pm->packtree_layout_version, 4);
	ipv->pmc.inodes_cnt = cpu_to_le64(pm->pmc.inodes_cnt);
	ipv->pmc.packoffset = cpu_to_le64(pm->pmc.packoffset);
	ipv->pmc.nfs_offset = cpu_to_le64(pm->pmc.nfs_offset);
	ipv->pmc.xattr_off = cpu_to_le64(pm->pmc.xattr_off);
	ipv->pmc.chtab_off = cpu_to_le64(pm->pmc.chtab_off);
	ipv->pmc.squash_bss = cpu_to_le16(pm->pmc.squash_bss);
	ipv->pmc.compr_type = cpu_to_le16(pm->pmc.compr_type);
	ipv->pmc.chunk_cnt = cpu_to_le32(pm->pmc.chunk_cnt);

	ipv->source_image.parent_id = cpu_to_le64(image_inode->parent_id);
	ipv->source_image.name_len = (u8)image_file->f_dentry->d_name.len;
	memcpy(ipv->source_image.name, image_file->f_dentry->d_name.name,
			image_file->f_dentry->d_name.len);

	vdfs_mark_record_dirty((struct vdfs_btree_gen_record *)record);

	/* to see a new folder in target dir we have to create new tree inode
	 * here and build a packtree in readdir function
	 */
	install_point_inode = iget_locked(sbi->sb, ino);
	if (!install_point_inode) {
		ret = -ENOMEM;
		goto exit;
	}
	if (!(install_point_inode->i_state & I_NEW))
		vdfs_init_inode(EMMCFS_I(install_point_inode));

	ret = vdfs_read_packtree_inode(install_point_inode,
			(struct emmcfs_cattree_key *)record->key);

	vdfs_release_record((struct vdfs_btree_gen_record *)record);
	EMMCFS_DEBUG_MUTEX("cattree mutex w lock un");
	mutex_w_unlock(sbi->catalog_tree->rw_tree_lock);

	if (ret)
		goto error_exit;

	unlock_new_inode(install_point_inode);
	iput(install_point_inode);
exit:
	return ret;

error_exit:
	iget_failed(install_point_inode);
	return ret;
}

int read_squashfs_image_simple(struct inode *inode, __u64 offset, __u32 length,
		void *data)
{
	int ret = 0, i;
	void *mapped_pages;
	unsigned int pages_cnt = ((unsigned int)(offset & (PAGE_SIZE - 1)) +
		length + PAGE_SIZE - 1) >> PAGE_SHIFT;

	struct page **pages =
			kzalloc(pages_cnt * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	memset(pages, 0, pages_cnt * sizeof(struct page *));
	ret = vdfs_read_or_create_pages(inode,
		(pgoff_t)(offset >> (__u64)PAGE_SHIFT), pages_cnt, pages,
			VDFS_PACKTREE_READ, 0, 0);
	if (ret) {
		kfree(pages);
		return ret;
	}
	mapped_pages = vmap(pages, pages_cnt,  VM_MAP, PAGE_KERNEL);
	if (!mapped_pages) {
		ret = -ENOMEM;
		goto exit;
	}

	offset &= (PAGE_SIZE - 1);
	memcpy(data, (char *)mapped_pages + offset, length);
	vunmap(mapped_pages);

exit:
	for (i = 0; i < (int)pages_cnt; i++)
		if (pages[i] && !IS_ERR(pages[i]))
			page_cache_release(pages[i]);
	kfree(pages);
	return ret;
}
