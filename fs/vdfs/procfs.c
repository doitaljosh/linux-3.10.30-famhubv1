#include "emmcfs.h"

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/pagevec.h>
#include <linux/path.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>

#define VDFS_PROC_DIR_NAME "fs/vdfs"
#define VDFS_MAX_FILE_PATH_LEN 1024
struct proc_dir_entry *vdfs_procdir_entry;

struct vdfs_proc_dir_info {
	struct proc_dir_entry *device_directory_entry;
	struct proc_dir_entry *file_paths_entry;
	int viewed;
	struct list_head files_list;
};

struct vdfs_proc_file_struct {
	char path[VDFS_MAX_FILE_PATH_LEN];
	char *printing_path;
	struct list_head list_entry;
};

void print_path(struct vdfs_proc_file_struct *vdfs_file, struct seq_file *m)
{
	if (vdfs_file->printing_path)
		seq_printf(m, "%s\n", vdfs_file->printing_path);
}

static void *vdfs_file_paths_start(struct seq_file *m, loff_t *pos)
{
	struct vdfs_sb_info *sbi;
	struct file *f;
	struct vdfs_proc_file_struct *new_entry;

	sbi = m->private;
	spin_lock(&sbi->dirty_list_lock);

	INIT_LIST_HEAD(&sbi->proc_info->files_list);

	if (list_empty(&sbi->dirty_list_head))
		return NULL;
	if (sbi->proc_info->viewed == 1)
		return NULL;


	do_file_list_for_each_entry(sbi->sb, f) {
		struct inode *inode;
		struct vdfs_sb_info *sbi;
		int dirty_pages_count = 0;
		long unsigned start_page_index = 0;
		struct pagevec pvec;

		if (f)
			inode = f->f_dentry->d_inode;
		else
			continue;
		sbi = inode->i_sb->s_fs_info;

		if (!(S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode)))
			continue;

		pagevec_init(&pvec, 0);

		dirty_pages_count = pagevec_lookup_tag(&pvec, inode->i_mapping,
			&start_page_index, PAGECACHE_TAG_DIRTY, 1);

		if (dirty_pages_count) {
			new_entry = kzalloc(sizeof(struct vdfs_proc_file_struct),
							GFP_KERNEL);
			if (!new_entry)
				return NULL;

			new_entry->printing_path = d_path(&f->f_path,
					new_entry->path,
					VDFS_MAX_FILE_PATH_LEN);

			list_add(&new_entry->list_entry, &sbi->proc_info->files_list);
		}

	} while_file_list_for_each_entry;

	return list_entry(sbi->proc_info->files_list.next,
			struct vdfs_proc_file_struct, list_entry);
}

static void *vdfs_file_paths_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct vdfs_proc_file_struct *file = v;
	struct vdfs_sb_info *sbi = m->private;

	if (list_is_last(&file->list_entry, &sbi->proc_info->files_list))
			return NULL;

	return list_entry(file->list_entry.next,
			struct vdfs_proc_file_struct, list_entry);
}

static void vdfs_file_paths_stop(struct seq_file *m, void *v)
{
	struct vdfs_sb_info *sbi;
	struct vdfs_proc_file_struct *file;
	struct vdfs_proc_file_struct *file_next;


	sbi = m->private;
	sbi->proc_info->viewed = 1;
	list_for_each_entry_safe(file, file_next, &sbi->proc_info->files_list,
			list_entry) {
		list_del(&file->list_entry);
		kfree(file);
	}
	spin_unlock(&sbi->dirty_list_lock);
}

static int vdfs_file_paths_show(struct seq_file *m, void *v)
{
	print_path(v, m);
	return 0;
}

static const struct seq_operations seq_ops = {
	.start = vdfs_file_paths_start,
	.next = vdfs_file_paths_next,
	.stop = vdfs_file_paths_stop,
	.show = vdfs_file_paths_show
};

static int vdfs_file_paths_open(struct inode *inode, struct file *file)
{
	struct seq_file *s;
	int result = seq_open(file, &seq_ops);
	struct proc_dir_entry *pde = PDE(inode);
	struct vdfs_sb_info *sbi = pde->data;

	sbi->proc_info->viewed = 0;
	s = (struct seq_file *)file->private_data;
	s->private = sbi;
	return result;
}

static const struct file_operations file_paths_fops = {
	.owner	= THIS_MODULE,
	.open	= vdfs_file_paths_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};


int vdfs_build_proc_entry(struct vdfs_sb_info *sbi)
{
	int ret;

	sbi->proc_info = kzalloc(sizeof(*sbi->proc_info), GFP_KERNEL);
	if (!sbi->proc_info)
		return -ENOMEM;

	sbi->proc_info->device_directory_entry =
		proc_mkdir(sbi->sb->s_id, vdfs_procdir_entry);
	if (!sbi->proc_info->device_directory_entry) {
		ret = -EINVAL;
		goto cannot_create_device_proc_entry;
	}

	sbi->proc_info->file_paths_entry =
		proc_create_data("dirty_files_paths", 0,
		sbi->proc_info->device_directory_entry, &file_paths_fops, sbi);
	if (!sbi->proc_info->file_paths_entry) {
		ret = -EINVAL;
		goto cannot_create_proc_entry;
	}


	return 0;

cannot_create_proc_entry:
	remove_proc_entry(sbi->sb->s_id, vdfs_procdir_entry);
cannot_create_device_proc_entry:
	kfree(sbi->proc_info);
	return ret;
}

void vdfs_destroy_proc_entry(struct vdfs_sb_info *sbi)
{
	remove_proc_entry("dirty_files_paths",
			sbi->proc_info->device_directory_entry);
	remove_proc_entry(sbi->sb->s_id, vdfs_procdir_entry);
	kfree(sbi->proc_info);
}

int vdfs_dir_init(void)
{
	vdfs_procdir_entry = proc_mkdir(VDFS_PROC_DIR_NAME, NULL);
	if (!vdfs_procdir_entry)
		return -EINVAL;

	return 0;
}

void vdfs_dir_exit(void)
{
	remove_proc_entry(VDFS_PROC_DIR_NAME, NULL);
}
