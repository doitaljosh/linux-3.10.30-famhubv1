/*
 * netsec_lsm.c
 *
 * Copyright 2013 Samsung Electronics
 * Created by Jihun Jung <jihun32.jung@samsung.com>,
 *		Himanshu Shukla <himanshu.sh@samsung.com>
 *
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/integrity.h>
#include <linux/ima.h>
#include <linux/evm.h>
#include <linux/fsnotify.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/backing-dev.h>
#include <net/flow.h>

struct security_operations *security_ops;
EXPORT_SYMBOL(security_ops);

static int common_netsec_mmap_file(struct file *file,
				unsigned long reqprot, unsigned long prot,
				unsigned long flags)
{
	return 0;
}

static int common_netsec_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline unsigned long mmap_prot(struct file *file, unsigned long prot)
{
	/*
	 * Does we have PROT_READ and does the application expect
	 * it to imply PROT_EXEC?  If not, nothing to talk about...
	 */
	if ((prot & (PROT_READ | PROT_EXEC)) != PROT_READ)
		return prot;
	if (!(current->personality & READ_IMPLIES_EXEC))
		return prot;
	/*
	 * if that's an anonymous mapping, let it.
	 */
	if (!file)
		return prot | PROT_EXEC;
	/*
	 * ditto if it's not on noexec mount, except that on !MMU we need
	 * BDI_CAP_EXEC_MMAP (== VM_MAYEXEC) in this case
	 */
	if (!(file->f_path.mnt->mnt_flags & MNT_NOEXEC)) {
#ifndef CONFIG_MMU
		unsigned long caps = 0;
		struct address_space *mapping = file->f_mapping;
		if (mapping && mapping->backing_dev_info)
			caps = mapping->backing_dev_info->capabilities;
		if (!(caps & BDI_CAP_EXEC_MAP))
			return prot;
#endif
		return prot | PROT_EXEC;
	}
	/* anything on noexec mount won't get PROT_EXEC */
	return prot;
}

int security_mmap_file(struct file *file, unsigned long prot,
			unsigned long flags)
{
	int ret;
	ret = security_ops->mmap_file(file, prot,
			mmap_prot(file, prot), flags);
	return ret;
}

int security_bprm_check(struct linux_binprm *bprm)
{
	int ret;
	ret = security_ops->bprm_check_security(bprm);
	return ret;
}

static struct security_operations netsec_ops = {
	.mmap_file =		common_netsec_mmap_file,
	.bprm_check_security =	common_netsec_bprm_check,
};

int __init security_init(void)
{
	printk(KERN_INFO "NetSec Security Framework initialization\n");
	security_ops = &netsec_ops;
	return 0;
}
