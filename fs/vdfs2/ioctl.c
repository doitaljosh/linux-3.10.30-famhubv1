/**
 * @file        fs/emmcfs/ioctl.c
 * @brief       IOCTL support for eMMCFS.
 * @author      TODO
 * @date        TODO
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file contains ioctl handler function.
 * IOCTL (an abbreviation of input/output control) is a system call for
 * device-specific input/output operations and other operations which cannot be
 * expressed by regular system calls
 *
 * @see         TODO: documents
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include "emmcfs.h"
#include "packtree.h"
#include <linux/mount.h>
#include <linux/version.h>
#include <linux/file.h>
#include <../fs/internal.h>

static int mnt_writers_increment(struct vfsmount *mnt)
{
	int ret = 0;

	preempt_disable();
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
#ifdef CONFIG_SMP
	this_cpu_inc(mnt->mnt_pcp->mnt_writers);
#else
	mnt->mnt_writers++;
#endif
#endif
	/*
	 * The store to mnt_inc_writers must be visible before we pass
	 * MNT_WRITE_HOLD loop below, so that the slowpath can see our
	 * incremented count after it has set MNT_WRITE_HOLD.
	 */
	smp_mb();
	while (mnt->mnt_flags & MNT_WRITE_HOLD)
		cpu_relax();
	/*
	 * After the slowpath clears MNT_WRITE_HOLD, mnt_is_readonly will
	 * be set to match its requirements. So we must not load that until
	 * MNT_WRITE_HOLD is cleared.
	 */
	smp_rmb();
	if ((mnt->mnt_flags & MNT_READONLY) &&
		(mnt->mnt_sb->s_flags & MS_RDONLY)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
#ifdef CONFIG_SMP
		this_cpu_dec(mnt->mnt_pcp->mnt_writers);
#else
		mnt->mnt_writers--;
#endif
#endif
		ret = -EROFS;
		goto out;
	}
out:
	preempt_enable();
	return ret;
}
/**
 * @brief	give up write access to a mount.
 * @param [in]	mnt	the mount on which to give up write access.
 * @return	none.
 */
static void mnt_writers_decrement(struct vfsmount *mnt)
{
	preempt_disable();
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
#ifdef CONFIG_SMP
	this_cpu_dec(mnt->mnt_pcp->mnt_writers);
#else
	mnt->mnt_writers--;
#endif
#endif
	preempt_enable();
}

/**
 * @brief	ioctl (an abbreviation of input/output control) is a system
 *		call for device-specific input/output operations and other
 *		 operations which cannot be expressed by regular system calls
 * @param [in]	filp	File pointer.
 * @param [in]	cmd	IOCTL command.
 * @param [in]	arg	IOCTL command arguments.
 * @return		0 if success, error code otherwise.
 */
long emmcfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int flags;
	struct inode *inode = filp->f_dentry->d_inode;
	int rc = 0;
	struct vdfs_sb_info *sbi =
		((struct super_block *)inode->i_sb)->s_fs_info;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = 0;
		vdfs_get_vfs_inode_flags(inode);
		if (EMMCFS_I(inode)->flags & (1 << VDFS_IMMUTABLE))
			flags |= FS_IMMUTABLE_FL;
		return put_user(flags & FS_FL_USER_VISIBLE, (int __user *) arg);
	case FS_IOC_SETFLAGS:
		rc = mnt_writers_increment(filp->f_path.mnt);
		if (rc)
			return rc;

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
		if (!is_owner_or_cap(inode)) {
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) ||\
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
		if (!inode_owner_or_capable(inode)) {
#else
	BUILD_BUG();
#endif
			rc = -EACCES;
			goto mnt_drop_write_exit;
		}

		if (get_user(flags, (int __user *) arg)) {
			rc = -EFAULT;
			goto mnt_drop_write_exit;
		}

		mutex_lock(&inode->i_mutex);

		/*
		 * The IMMUTABLE flag can only be changed by the relevant
		 * capability.
		 */
		if ((flags & FS_IMMUTABLE_FL) &&
			!capable(CAP_LINUX_IMMUTABLE)) {
			rc = -EPERM;
			goto unlock_inode_exit;
		}

		/* don't silently ignore unsupported flags */
		if (flags & ~FS_IMMUTABLE_FL) {
			rc = -EOPNOTSUPP;
			goto unlock_inode_exit;
		}

		vdfs_start_transaction(sbi);
		if (flags & FS_IMMUTABLE_FL)
			EMMCFS_I(inode)->flags |= (1 << VDFS_IMMUTABLE);
		else
			EMMCFS_I(inode)->flags &= ~(1 << VDFS_IMMUTABLE);
		vdfs_set_vfs_inode_flags(inode);
		inode->i_ctime = emmcfs_current_time(inode);
		mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
		emmcfs_write_inode_to_bnode(inode);
		mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
		vdfs_stop_transaction(sbi);

unlock_inode_exit:
		mutex_unlock(&inode->i_mutex);
mnt_drop_write_exit:
		mnt_writers_decrement(filp->f_path.mnt);
		return rc;
	case VDFS_IOC_GET_OPEN_COUNT:
		return put_user(atomic_read(&(EMMCFS_I(inode)->open_count)),
			(int __user *) arg);

	case VDFS_IOC_GET_PARENT_ID:
		return put_user(EMMCFS_I(inode)->parent_id,
				(__u64 __user *) arg);

	default:
		return -ENOTTY;
	}
}

static void vdfs_delete_list_item(struct list_head *list, int value)
{
	struct list_head *cur, *tmp;
	struct vdfs_int_container *pa;
	list_for_each_safe(cur, tmp, list) {
		pa = list_entry(cur, struct vdfs_int_container, list);
		if (pa->value == value) {
			list_del(&pa->list);
			kfree(pa);
		}
	}
}

void vdfs_clear_list(struct list_head *list)
{
	struct list_head *cur, *tmp;
	struct vdfs_int_container *pa;
	list_for_each_safe(cur, tmp, list) {
		pa = list_entry(cur, struct vdfs_int_container, list);
		list_del(&pa->list);
		kfree(pa);
	}
}

static void vdfs_add_list_item(struct list_head *list, int value)
{
	struct list_head *cur, *tmp;
	int count = 0;
	struct vdfs_int_container *pa;
	/* check if value already exists */
	list_for_each_safe(cur, tmp, list) {
		pa = list_entry(cur, struct vdfs_int_container, list);
		if (pa->value == value) {
			count++;
			break;
		}
	}
	/* if no value in the list, add it */
	if (count == 0) {
again:
		pa = kzalloc(sizeof(struct vdfs_int_container), GFP_KERNEL);
		if (!pa)
			goto again;
		pa->value = value;
		list_add(&pa->list, list);
	}
}
/* check permission if it's needed do sleep.
 * return 0 if it has permissions,
 *	1 if it has no permissions
 * */
int check_permissions(struct vdfs_sb_info *sbi)
{
	int ret  = 0;
	if (!list_empty(&sbi->high_priority.high_priority_tasks)) {
		struct list_head *cur, *tmp;
		struct vdfs_int_container *pa;
		int count = 0;
		struct list_head *list =
				&sbi->high_priority.high_priority_tasks;
		mutex_lock(&sbi->high_priority.task_list_lock);
		/* search current process in access list */
		list_for_each_safe(cur, tmp, list) {
			pa = list_entry(cur, struct vdfs_int_container, list);
			if ((pa->value == current->pid) ||
				(pa->value == current->real_parent->pid)) {
				count++;
				break;
			}
		}
		mutex_unlock(&sbi->high_priority.task_list_lock);
		if (count == 0) {
			if (wait_for_completion_interruptible_timeout(
				&sbi->high_priority.high_priority_done, 5000)
				== -ERESTARTSYS)
				ret = 1;
		}
	}
	return ret;
}
void init_high_priority(struct vdfs_high_priority *high_priority)
{
	INIT_LIST_HEAD(&high_priority->high_priority_tasks);
	mutex_init(&high_priority->task_list_lock);
	init_completion(&high_priority->high_priority_done);
}

void destroy_high_priority(struct vdfs_high_priority *high_priority)
{
	vdfs_clear_list(&high_priority->high_priority_tasks);
	complete_all(&high_priority->high_priority_done);
}

static void clear_files_rw_mode(struct super_block *sb)
{
	struct file *f;

retry:
	do_file_list_for_each_entry(sb, f) {
		struct vfsmount *mnt;
		if (!S_ISREG(f->f_path.dentry->d_inode->i_mode))
			continue;
		if (!file_count(f))
			continue;
		if (!(f->f_mode & FMODE_WRITE))
			continue;
		spin_lock(&f->f_lock);
		f->f_mode &= ~FMODE_WRITE;
		spin_unlock(&f->f_lock);
		if (file_check_writeable(f) != 0)
			continue;
		file_release_write(f);
		mnt = mntget(f->f_path.mnt);
		if (!mnt)
			goto retry;
		mnt_drop_write(mnt);
		mntput(mnt);
		goto retry;
	} while_file_list_for_each_entry;
}

static int force_ro(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	if (sb->s_frozen != SB_UNFROZEN)
#else
	if (sb->s_writers.frozen != SB_UNFROZEN)
#endif
		return -EBUSY;

	shrink_dcache_sb(sb);
	sync_filesystem(sb);

	clear_files_rw_mode(sb);

	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | MS_RDONLY;

	invalidate_bdev(sb->s_bdev);
	return 0;
}

/**
 * @brief	ioctl (an abbreviation of input/output control) is a system
 *		call for device-specific input/output operations and other
 *		operations which cannot be expressed by regular system calls
 * @param [in]	filp	File pointer.
 * @param [in]	cmd	IOCTL command.
 * @param [in]	arg	IOCTL command arguments.
 * @return		0 if success, error code otherwise.
 */
long vdfs_dir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct inode *inode = filp->f_dentry->d_inode;
	struct vdfs_sb_info *sbi =
		((struct super_block *)inode->i_sb)->s_fs_info;
	struct super_block *sb = (struct super_block *)inode->i_sb;
	switch (cmd) {
	case VDFS_IOC_FORCE_RO:
		if (test_option(sbi, FORCE_RO))
			return -EINVAL;
		down_write(&sb->s_umount);
		if (sb->s_root && sb->s_bdev && !(sb->s_flags & MS_RDONLY))
			force_ro(sb);

		up_write(&sb->s_umount);
		set_option(sbi, FORCE_RO);
		break;
	case VDFS_IOC_GRAB2PARENT:
		mutex_lock(&sbi->high_priority.task_list_lock);
		/* if it's first high priority process */
		if (list_empty(&sbi->high_priority.high_priority_tasks))
			INIT_COMPLETION(sbi->high_priority.
					high_priority_done);
		/* add current process to list */
		vdfs_add_list_item(&sbi->high_priority.
				high_priority_tasks, current->
				real_parent->pid);
		mutex_unlock(&sbi->high_priority.task_list_lock);
		break;
	case VDFS_IOC_RELEASE2PARENT:
		mutex_lock(&sbi->high_priority.task_list_lock);
		vdfs_delete_list_item(&sbi->high_priority.
				high_priority_tasks, current->
				real_parent->pid);
		if (list_empty(&sbi->high_priority.high_priority_tasks))
			complete_all(&sbi->high_priority.
					high_priority_done);
		mutex_unlock(&sbi->high_priority.task_list_lock);
		break;
	case VDFS_IOC_GRAB:
		mutex_lock(&sbi->high_priority.task_list_lock);
		/* if it's first high priority process */
		if (list_empty(&sbi->high_priority.high_priority_tasks))
			INIT_COMPLETION(sbi->high_priority.
					high_priority_done);
		/* add current process to list */
		vdfs_add_list_item(&sbi->high_priority.
				high_priority_tasks, current->pid);
		mutex_unlock(&sbi->high_priority.task_list_lock);
		break;
	case VDFS_IOC_RELEASE:
		mutex_lock(&sbi->high_priority.task_list_lock);
		vdfs_delete_list_item(&sbi->high_priority.
				high_priority_tasks, current->pid);
		if (list_empty(&sbi->high_priority.high_priority_tasks))
			complete_all(&sbi->high_priority.
					high_priority_done);
		mutex_unlock(&sbi->high_priority.task_list_lock);
		break;
	case VDFS_IOC_RESET:
		mutex_lock(&sbi->high_priority.task_list_lock);
		vdfs_clear_list(&sbi->high_priority.
				high_priority_tasks);
		complete_all(&sbi->high_priority.
					high_priority_done);
		mutex_unlock(&sbi->high_priority.task_list_lock);
		break;
	case VDFS_IOC_INSTALL: {
		struct ioctl_install_params input_data;
		struct inode *image_inode;
		struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);
#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33)
		struct file *f;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
		struct fd f;
#endif

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (copy_from_user(&input_data,
			(struct ioctl_install_params __user *)arg,
			sizeof(input_data))) {
			return -EFAULT;
		}
		if (memcmp(input_data.packtree_layout_version,
				VDFS_PACK_METADATA_VERSION,
				strlen(VDFS_PACK_METADATA_VERSION))) {
			printk(KERN_ERR "Packtree layout mismatch:\n"
				"Image file had been expanded by"
				" previous version of install.vdfs utility.");
			return -EINVAL;
		}

#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33)
		f = fget(input_data.image_fd);
		if (!f)
			return -EBADF;
		image_inode = f->f_dentry->d_inode;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
		f = fdget(input_data.image_fd);
		if (!f.file)
			return -EBADF;
		image_inode = f.file->f_dentry->d_inode;
#endif


		ret = mnt_writers_increment(filp->f_path.mnt);
		if (ret)
			return ret;

		vdfs_start_transaction(sbi);

#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33)
		ret = vdfs_install_packtree(filp, f, &input_data);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
		ret = vdfs_install_packtree(filp, f.file, &input_data);
#endif

		if (!ret) {
			mutex_lock(&inode_info->truncate_mutex);
			inode->i_size++;
			sbi->files_count += input_data.pmc.inodes_cnt;
			inode->i_mtime = emmcfs_current_time(inode);
			ret = emmcfs_write_inode_to_bnode(inode);
			mutex_unlock(&inode_info->truncate_mutex);

			mutex_lock(&EMMCFS_I(image_inode)->truncate_mutex);
			EMMCFS_I(image_inode)->flags |= (1 << VDFS_IMMUTABLE);
			vdfs_set_vfs_inode_flags(image_inode);
			image_inode->i_ctime = emmcfs_current_time(image_inode);
			emmcfs_write_inode_to_bnode(image_inode);
			mutex_unlock(&EMMCFS_I(image_inode)->truncate_mutex);
		}

		vdfs_stop_transaction(sbi);
		mnt_writers_decrement(filp->f_path.mnt);
#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
	LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33)
		fput(f);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
		fdput(f);
#endif
		return ret;
	}
	case VDFS_IOC_UNINSTALL: {
		struct ioctl_uninstall_params data;
		int open_count;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (VDFS_IS_READONLY(sb))
			return -EROFS;

		if (copy_from_user(&data,
			(struct ioctl_uninstall_params __user *)arg,
			sizeof(data))) {
			ret = -EFAULT;
			return ret;
		}

		if (EMMCFS_I(inode)->ptree.tree_info) {
			open_count = atomic_read(&(EMMCFS_I(inode)->ptree.
						tree_info->open_count));

			if (open_count != 0)
				return -EBUSY;
		}

		ret = mnt_writers_increment(filp->f_path.mnt);
		if (ret)
			return ret;

		vdfs_start_transaction(sbi);

		ret = vdfs_uninstall_packtree(inode, &data);

		vdfs_stop_transaction(sbi);
		mnt_writers_decrement(filp->f_path.mnt);
		return ret;
	}
	case VDFS_IOC_GET_INODE_TYPE:
		return put_user(EMMCFS_I(inode)->record_type,
				(__u8 __user *) arg);

	case VDFS_IOC_GET_PARENT_ID:
		return put_user(EMMCFS_I(inode)->parent_id,
				(__u64 __user *) arg);

	default:
		return -EINVAL;

	}

	return ret;
}



