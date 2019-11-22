/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#include <linux/aio.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "memfd.h"
#include "util.h"

static const struct file_operations kdbus_memfd_fops;

/**
 * struct kdbus_memfile - protectable shared memory file
 * @name:		Name of the (deleted) file which shows up in
 *			/proc, used for debugging
 * @lock:		Locking
 * @fp:			Shared memory backing file
 * @sealed:		Flag if the content is writable
 */
struct kdbus_memfile {
	const char *name;
	struct mutex lock;
	struct file *fp;
	bool sealed;
};

/**
 * kdbus_is_memfd() - check if a file is one of our memfds
 * @fp:			File to check
 *
 * Return: true if the file is a memfd
 */
bool kdbus_is_memfd(const struct file *fp)
{
	return fp->f_op == &kdbus_memfd_fops;
}

/**
 * kdbus_is_memfd_sealed() - check if a memfd is protected
 * @fp:			Memfd file to check
 *
 * Return: true if the memfd is protected
 */
bool kdbus_is_memfd_sealed(const struct file *fp)
{
	struct kdbus_memfile *mf = fp->private_data;

	return mf->sealed;
}

/**
 * kdbus_memfd_size() - return the actual size of a memfd
 * @fp:			Memfd file to check
 *
 * Return: the actual size of the file in bytes
 */
u64 kdbus_memfd_size(const struct file *fp)
{
	struct kdbus_memfile *mf = fp->private_data;

	return (u64)i_size_read(file_inode(mf->fp));
}

/**
 * kdbus_memfd_new() - create and install a memfd and file descriptor
 * @name:		Name of the (deleted) file which shows up in
 *			/proc, used for debugging
 * @size:		Initial size of the file
 * @fd:			Installed file descriptor
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_memfd_new(const char *name, size_t size, int *fd)
{
	const char *shmem_name = NULL;
	const char *anon_name = NULL;
	struct kdbus_memfile *mf;
	struct file *shmemfp;
	struct file *fp;
	int f, ret;

	mf = kzalloc(sizeof(*mf), GFP_KERNEL);
	if (!mf)
		return -ENOMEM;

	mutex_init(&mf->lock);

	if (name) {
		mf->name = kstrdup(name, GFP_KERNEL);
		shmem_name = kasprintf(GFP_KERNEL,
				       KBUILD_MODNAME "-memfd:%s", name);
		anon_name = kasprintf(GFP_KERNEL,
				      "[" KBUILD_MODNAME "-memfd:%s]", name);
		if (!mf->name || !shmem_name || !anon_name) {
			ret = -ENOMEM;
			goto exit;
		}
	}

	/* allocate a new unlinked shmem file */
	shmemfp = shmem_file_setup(name ? shmem_name : KBUILD_MODNAME "-memfd",
				   size, 0);
	if (IS_ERR(shmemfp)) {
		ret = PTR_ERR(shmemfp);
		goto exit;
	}
	mf->fp = shmemfp;

	f = get_unused_fd_flags(O_CLOEXEC);
	if (f < 0) {
		ret = f;
		goto exit_shmem;
	}

	/*
	 * The anonymous exported inode ops cannot reach the otherwise
	 * invisible shmem inode. We rely on the fact that nothing else
	 * can create a new file for the shmem inode, like by opening the
	 * fd in /proc/$PID/fd/
	 */
	fp = anon_inode_getfile(name ? anon_name : "[" KBUILD_MODNAME "-memfd]",
				&kdbus_memfd_fops, mf, O_RDWR);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
		goto exit_fd;
	}

	fp->f_mode |= FMODE_LSEEK|FMODE_PREAD|FMODE_PWRITE;
	fp->f_mapping = shmemfp->f_mapping;
	fd_install((unsigned int)f, fp);

	kfree(anon_name);
	kfree(shmem_name);
	*fd = f;
	return 0;

exit_fd:
	put_unused_fd((unsigned int)f);
exit_shmem:
	fput(shmemfp);
exit:
	kfree(anon_name);
	kfree(shmem_name);
	kfree(mf->name);
	kfree(mf);
	return ret;
}

static int kdbus_memfd_release(struct inode *ignored, struct file *file)
{
	struct kdbus_memfile *mf = file->private_data;

	fput(mf->fp);
	kfree(mf->name);
	kfree(mf);
	return 0;
}

static loff_t kdbus_memfd_llseek(struct file *file, loff_t offset, int whence)
{
	struct kdbus_memfile *mf = file->private_data;
	loff_t ret;

	mutex_lock(&mf->lock);
	ret = mf->fp->f_op->llseek(mf->fp, offset, whence);
	if (ret < 0)
		goto exit;

	/* update the anonymous file */
	file->f_pos = mf->fp->f_pos;

exit:
	mutex_unlock(&mf->lock);
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
static ssize_t kdbus_memfd_readv(struct kiocb *iocb, const struct iovec *iov,
				 unsigned long iov_count, loff_t pos)
#else
static ssize_t kdbus_memfd_read_iter(struct kiocb *iocb, struct iov_iter *iter)
#endif
{
	struct kdbus_memfile *mf = iocb->ki_filp->private_data;
	ssize_t ret;

	mutex_lock(&mf->lock);
	iocb->ki_filp = mf->fp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
	ret = mf->fp->f_op->aio_read(iocb, iov, iov_count, pos);
#else
	ret = mf->fp->f_op->read_iter(iocb, iter);
#endif
	if (ret < 0)
		goto exit;

	/* update the shmem file */
	mf->fp->f_pos = iocb->ki_pos;

exit:
	mutex_unlock(&mf->lock);
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
static ssize_t kdbus_memfd_writev(struct kiocb *iocb, const struct iovec *iov,
				  unsigned long iov_count, loff_t pos)
#else
static ssize_t kdbus_memfd_write_iter(struct kiocb *iocb, struct iov_iter *iter)
#endif
{
	struct kdbus_memfile *mf = iocb->ki_filp->private_data;
	ssize_t ret;

	mutex_lock(&mf->lock);

	/* deny write access to a sealed file */
	if (mf->sealed) {
		ret = -EPERM;
		goto exit;
	}

	iocb->ki_filp = mf->fp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
	ret = mf->fp->f_op->aio_write(iocb, iov, iov_count, pos);
#else
	ret = mf->fp->f_op->write_iter(iocb, iter);
#endif
	if (ret < 0)
		goto exit;

	/* update the shmem file */
	mf->fp->f_pos = iocb->ki_pos;

exit:
	mutex_unlock(&mf->lock);
	return ret;
}

static int kdbus_memfd_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kdbus_memfile *mf = file->private_data;
	int ret = 0;

	if (vma->vm_flags & VM_WRITE) {
		size_t size;
		struct inode *inode;

		/*
		 * Deny a writable mapping to a sealed file.
		 *
		 * Avoid a deadlock and do not take mf->lock here, the call to
		 * mmap() already holds mm->mmap_sem.
		 * To protect KDBUS_CMD_MEMFD_SEAL_SET racing against us,
		 * mf->sealed is changed only with mm->mmap_sem held.
		 */
		if (mf->sealed) {
			ret = -EPERM;
			goto exit;
		}

		/*
		 * Extend the size of the shmem file to the
		 * size of the mapping
		 */
		size = (vma->vm_end - vma->vm_start) +
		       (vma->vm_pgoff << PAGE_SHIFT);
		inode = file_inode(mf->fp);
		if (size > PAGE_ALIGN(i_size_read(inode)))
			i_size_write(inode, size);
	}

	/* replace the anoymous inode file with our shmem file */
	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = get_file(mf->fp);
	ret = mf->fp->f_op->mmap(file, vma);

exit:
	return ret;
}

static long
kdbus_memfd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct kdbus_memfile *mf = file->private_data;
	long ret = 0;

	mutex_lock(&mf->lock);
	switch (cmd) {
	case KDBUS_CMD_MEMFD_SIZE_GET: {
		u64 size = (u64)i_size_read(file_inode(mf->fp));

		if (!KDBUS_IS_ALIGNED8(arg)) {
			ret = -EFAULT;
			goto exit;
		}

		if (copy_to_user(argp, &size, sizeof(__u64))) {
			ret = -EFAULT;
			goto exit;
		}
		break;
	}

	case KDBUS_CMD_MEMFD_SIZE_SET: {
		u64 size;

		if (!KDBUS_IS_ALIGNED8(arg)) {
			ret = -EFAULT;
			goto exit;
		}

		if (copy_from_user(&size, argp, sizeof(__u64))) {
			ret = -EFAULT;
			goto exit;
		}

		/* deny a writable access to a sealed file */
		if (mf->sealed) {
			if (size == (u64) i_size_read(file_inode(mf->fp)))
				ret = -EALREADY;
			else
				ret = -EPERM;
			goto exit;
		}

		if (size != (u64) i_size_read(file_inode(mf->fp)))
			ret = vfs_truncate(&mf->fp->f_path, (loff_t)size);
		break;
	}

	case KDBUS_CMD_MEMFD_SEAL_GET: {
		int __user *addr = argp;

		if (put_user(mf->sealed, addr)) {
			ret = -EFAULT;
			goto exit;
		}
		break;
	}

	case KDBUS_CMD_MEMFD_SEAL_SET: {
		struct mm_struct *mm = current->mm;

		/*
		 * Make sure we have only one single user of the file
		 * before we seal, we rely on the fact there is no
		 * any other possibly writable references to the file.
		 *
		 * Protect mmap() racing against us, take mm->mmap_sem
		 * when accessing mf->sealed.
		 */
		down_read(&mm->mmap_sem);
		/* FIXME: It is insufficient to check the back-fd, we
		 * only track mmap() here, the front fd has a refcount
		 * of 2 as soon as the task has more than one thread.
		 */
		if (file_count(mf->fp) != 1) {
			if (mf->sealed == !!argp)
				ret = -EALREADY;
			else
				ret = -ETXTBSY;
		}

		if (ret == 0)
			mf->sealed = !!argp;
		up_read(&mm->mmap_sem);
		break;
	}

	default:
		ret = -ENOTTY;
		break;
	}

exit:
	mutex_unlock(&mf->lock);
	return ret;
}

static const struct file_operations kdbus_memfd_fops = {
	.owner =		THIS_MODULE,
	.release =		kdbus_memfd_release,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
	.aio_read =		kdbus_memfd_readv,
	.aio_write =		kdbus_memfd_writev,
#else
	.read_iter =		kdbus_memfd_read_iter,
	.write_iter =		kdbus_memfd_write_iter,
#endif
	.llseek =		kdbus_memfd_llseek,
	.mmap =			kdbus_memfd_mmap,
	.unlocked_ioctl =	kdbus_memfd_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =		kdbus_memfd_ioctl,
#endif
};
