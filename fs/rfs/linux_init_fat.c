/*
 * RFS 3.0 Developed by Flash Software Group.
 *
 * Copyright 2006-2009 by Memory Division, Samsung Electronics, Inc.,
 * San #16, Banwol-Dong, Hwasung-City, Gyeonggi-Do, Korea
 *
 * All rights reserved.
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

/**
 * @file        linux_init_fat.c
 * @brief       This file initializes rfs_fat module and registers rfs filesystem.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */


#include "rfs_linux.h"
#include "natives.h"

#include <linux/module.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_BTFS)

/*
 * filesystem type definition
 */
static LINUX_FS_TYPE g_RFS_FAT_FsType =
{
	.owner          = LINUX_THIS_MODULE,
	.name           = RFS_BTFS,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 38)
	.get_sb         = LxGetSb,
#else
	.mount		= LxMount,
#endif
	.kill_sb        = LINUX_KillBlockSuper,
	.fs_flags       = LINUX_FS_REQUIRES_DEV,
};


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>

#include "linux_version.h"

#define FAT_PROC_DIR		"rfs"
#define FAT_ENTRY_VERSION	"version"

struct proc_dir_entry *pstFATProcDir = NULL;

static int ProcEntryReadVersion(struct seq_file *m, void *v)
{
	(void)v;

	seq_printf(m, "%s\n", RFS_FS_VERSION);
	return 0;
}

static int ProcEntryOpen(struct inode *inode, struct file *file)
{
       return single_open_size(file, ProcEntryReadVersion, NULL, PAGE_SIZE);
}

static const struct file_operations RFSProcFops = {
	.open           = ProcEntryOpen,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};
#endif

/**
 * @brief		Interface function for registration BTFS
 */
static LINUX_ERROR __init 
LxInitModuleInit_FAT(void)
{
	LINUX_ERROR		dwLinuxError = 0;
	FERROR			nErr = FERROR_NO_ERROR;

	/* initialize native filesystem BTFS */
	nErr = NativefsInitialize(GetBTFS());
	if (nErr)
	{
		LNX_EMZ(("Fail to initialize NativeFS %s", RFS_BTFS));
		dwLinuxError = RtlLinuxError(nErr);
		goto exit;
	}

	/* register filesystem to linux VFS */
	dwLinuxError = LINUX_RegisterFilesystem(&g_RFS_FAT_FsType);
	if (dwLinuxError)
	{
		LNX_EMZ(("Fail to register filesystem to VFS"));
		NativefsUninitialize(GetBTFS());
		goto exit;
	}

	/*
	 * set the function to map NativeFS to Linux glue
	 * This function will be called to access nativeFS when mount BTFS
	 */
	GetNativeBTFS = GetBTFS;

#ifdef CONFIG_PROC_FS
	pstFATProcDir = proc_mkdir(FAT_PROC_DIR, NULL);
	if (pstFATProcDir)
	{
		proc_create(FAT_ENTRY_VERSION, 0, pstFATProcDir, &RFSProcFops);
	}
	else
	{
		LNX_EMZ(("Fail to create proc entry %s", FAT_PROC_DIR));
	}
#endif

exit:
	return dwLinuxError;
}

/**
 * @brief		Interface function for unregistration BTFS
 */
static void __exit 
LxInitModuleExit_FAT(void)
{
	NativefsUninitialize(GetBTFS());
	LINUX_UnregisterFilesystem(&g_RFS_FAT_FsType);
#ifdef CONFIG_PROC_FS
	if (pstFATProcDir)
	{
		remove_proc_entry(FAT_ENTRY_VERSION, pstFATProcDir);
		remove_proc_entry(FAT_PROC_DIR, NULL);
	}
#endif
}

module_init(LxInitModuleInit_FAT);
module_exit(LxInitModuleExit_FAT);

MODULE_LICENSE("Samsung, Proprietary");

// end of file
