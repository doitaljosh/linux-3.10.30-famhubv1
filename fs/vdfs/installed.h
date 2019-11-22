#ifndef INSTALLED_H_
#define INSTALLED_H_




struct inode *vdfs_ro_image_installation(struct vdfs_sb_info *sbi,
		struct file *parent_dir, unsigned long arg);

struct installed_info *vdfs_get_installed_tree(struct inode *root_inode);

int vdfs_fill_image_root(struct vdfs_cattree_record *record,
		struct inode *inode);

void vdfs_destroy_installed_list(struct vdfs_sb_info *sbi);

int vdfs_remove_ro_image(struct file *install_dir);
#endif
