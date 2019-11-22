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
 * @file        linux_volume.c
 * @brief       This file includes volume control operations and superblock operations.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "ns_misc.h"
#include "natives.h"
#include "linux_volume.h"
#include "linux_vcb.h"
#include "linux_vnode.h"

#include <linux/fs.h>
#include <linux/statfs.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_VOL)

/*
 * Macro
 */
#define VolGetVcbFromSb(pSb)	((PVOLUME_CONTROL_BLOCK)(pSb->s_fs_info))

#ifndef CONFIG_RFS_DEFAULT_IOCHARSET
#define CONFIG_RFS_DEFAULT_IOCHARSET		"cp437"
#endif

/*
 * mount option
 */
typedef unsigned int OPTION_TYPE;
enum _OPTION_TYPE 
{
	/* don't match Nestle's mount flag (Linux Glue internal) */
	OPT_IOCHAR_SET,			/* set by option */
	OPT_VFAT_OFF,			/* set by config */
	OPT_SELINUX,			/* set by config */
	OPT_INTERNAL,			/* set by option */
	OPT_EXTERNAL,			/* set by option */

	/* match Nestle's mount flag */
	OPT_LOG_OFF,			/* set by option */
	OPT_LLW,				/* set by option */
	OPT_FULL_LLW,			/* set by option */
	OPT_HPA,				/* set by config */
	OPT_XATTR,				/* set by config */
	OPT_NAME_RULE,			/* set by config */

	/* don't match Nestle's mount flag (GLUE layer of external mount) */
	OPT_SETATTR,			/* set by option */
	OPT_UID,				/* set by option */
	OPT_GID,				/* set by option */
	OPT_UMASK,				/* set by option */
	OPT_FMASK,				/* set by option */
	OPT_DMASK,				/* set by option */

	/* default mount option */
	OPT_ERASE,
	OPT_GUID,
	OPT_SPECIAL,

	OPT_PRIVATE,			/* start of private flag */
	OPT_ERR,
};

/*
 * mount option mapping table
 *
 * This table is used for parsing user's pattern to mount option.
 * To use the parser library in Linux Kernel, we defines the table of match_table_t
 */
static LINUX_MATCH_TABLE g_MountOption = 
{
	/* don't match Nestle's mount flag (Linux Glue internal) */
	{OPT_IOCHAR_SET,		"iocharset=%s"},	/* locale of io character  */
	{OPT_INTERNAL,			"internal"},		/* internally use it (not compatible with FAT spec) */
	{OPT_EXTERNAL,			"external"},		/* use it as external device (compatible with FAT spec) */

	/* match Nestle's mount flag */
	{OPT_LOG_OFF, 			"log_off"},			/* transaction off */
	{OPT_LLW,				"llw"},				/* partial lazy-log-write */
	{OPT_FULL_LLW,			"full_llw"},		/* full lazy-log-write */
	{OPT_NAME_RULE,			"check=no"},		/* breaking FAT naming rule */

	{OPT_SETATTR,			"setattr"},			/* operation like owner and authority of vfat */
	{OPT_UID,				"uid=%u"},			/* uid */
	{OPT_GID,				"gid=%u"},			/* gid */
	{OPT_UMASK,				"umask=%o"},		/* umask (fmask, dmask) */
	{OPT_FMASK,				"fmask=%o"},		/* fmask */
	{OPT_DMASK,				"dmask=%o"},		/* dmask */

	{OPT_ERR,				NULL},
};

/* Nestle mount flag's Map */
struct MountFlag
{
	OPTION_TYPE optType;
	MOUNT_FLAG	dwMntFlag;
	char		*shows;
};

/*
 * mount flag mapping table
 *
 * This table is used for parsing Internal mount option to Nestle's Mount flag
 * and parsing Nestle's Mount flag to proper string to be shown to users.
 */
static struct MountFlag stMntFlagMaps[] =
{
	/* match Nestle's mount flag */
	{OPT_LOG_OFF,			MOUNT_TRANSACTION_OFF,	",log_off"},
	{OPT_LLW,				MOUNT_LLW,				",llw"}, 			/* partial lazy-log-write */
	{OPT_FULL_LLW,			MOUNT_FULL_LLW, 		",full_llw"}, 		/* full lazy-log-write */
	{OPT_NAME_RULE,			MOUNT_ALLOW_OS_NAMING_RULE, ",check=no"},	/* breaking FAT naming rule */

	/* default option */
	{OPT_ERASE,				MOUNT_ERASE_SECTOR,		",erase_sector"},
	{OPT_GUID,				MOUNT_FILE_GUID,		",gid/uid/rwx"},
	{OPT_SPECIAL,			MOUNT_FILE_SPECIAL,		",special"},

	/* not used by mount option */
	{OPT_HPA,				MOUNT_HPA,				",hpa"},
	{OPT_XATTR,				MOUNT_FILE_XATTR,		",xattr"},

	/* Linux Glue private mount flags */
	{OPT_PRIVATE,			0xFFFFFFF0,				NULL},
	{OPT_SELINUX,			PRIVATE_VCB_SELINUX,	",selinux"},	/* enable SELinux */
	{OPT_VFAT_OFF,			PRIVATE_VCB_VFAT_OFF,	",vfat_off"},	/* disable VFAT */
	{OPT_INTERNAL,			PRIVATE_VCB_INTERNAL,	",internal"},
	{OPT_EXTERNAL,			PRIVATE_VCB_EXTERNAL,	",external"},

	{OPT_ERR,				0xFFFFFFFF,				NULL},
};

/* Linux Glue's Internal */
typedef struct _MOUNT_OPTIONS 
{
	char*				pIocharset;	// IO charset
	PLINUX_NLS_TABLE	pTableIo;	// NLS table
	MOUNT_FLAG			dwMountOpt;	// Nestle's mount option
	unsigned int		dwPrivateOpt; // private mount option in linux glue
	EXT_MOUNT_OPT 		stExtOpt;	// extended mount option for GLUE layer
} MOUNT_OPTIONS, *PMOUNT_OPTIONS;


static char g_DefaultCodePage[] = CONFIG_RFS_DEFAULT_IOCHARSET;


/******************************************************************************/
/* internal functions                                                         */
/******************************************************************************/

/**
 * @brief		initialize NLS table for iocharset
 * @param[in]	pstMntOptions		parsed mount option
 * @return		return 0 on success, nErrno on failure
 */
static inline LINUX_ERROR
_InitializeNLS(
	IN	PMOUNT_OPTIONS	pstMntOptions)
{
	LNX_ASSERT_ARG(pstMntOptions, -EINVAL);

	if (pstMntOptions->pIocharset == NULL)
	{
		pstMntOptions->pTableIo = LINUX_LoadNLS(g_DefaultCodePage);
	}
	else
	{
		pstMntOptions->pTableIo = LINUX_LoadNLS(pstMntOptions->pIocharset);
		LINUX_Kfree(pstMntOptions->pIocharset);
		pstMntOptions->pIocharset = NULL;
	}

	if (NULL == pstMntOptions->pTableIo) 
	{
		LNX_SMZ(("Fail to load Iocharset: load_nls"));
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief		parse mount option
 * @param[in]	pSb				super block
 * @param[in]	pOption			user's option string
 * @param[out]	pstMntOptions	parsed mount option
 * @return		return 0 on success, nErrno on failure
 */
static LINUX_ERROR
_ParseMountOptions(
	IN	PLINUX_SUPER	pSb, 
	IN	void*			pOptions, 
	OUT	PMOUNT_OPTIONS	pstMntOptions)
{
	LINUX_SUBSTRING		aArgs[LINUX_MAX_OPT_ARGS];
	char*				pChunkOption = NULL;
	int					nToken;
	int					dwOption;
	BOOL				dwSetup = FALSE;

	LNX_ASSERT_ARG(pSb, -EINVAL);
	LNX_ASSERT_ARG(pstMntOptions, -EINVAL);

	/* initialize */
	RtlFillMem(pstMntOptions, 0, sizeof (MOUNT_OPTIONS));

	/*
	 * default uid, gid, fmask, dmask
	 * If they have not any option, then they will be set by '0'
	 */
	pstMntOptions->stExtOpt.dwFsUid = RFS_ROOT_UID;
	pstMntOptions->stExtOpt.dwFsGid = RFS_ROOT_GID;
	pstMntOptions->stExtOpt.wFsFmask = pstMntOptions->stExtOpt.wFsDmask = LINUX_g_CurFsUmask;

	while ((pChunkOption = LINUX_StrSep((char **) &pOptions, ",")) != NULL) 
	{
		if (!(*pChunkOption))
		{
			continue;
		}

		nToken = LINUX_MatchToken(pChunkOption, g_MountOption, aArgs);

		switch (nToken)
		{
		/* NLS codepage used in IO */
		case OPT_IOCHAR_SET:
			pstMntOptions->pIocharset = LINUX_MatchStrdup(&aArgs[0]);
			if (NULL == pstMntOptions->pIocharset)
			{
				LNX_EMZ(("invalid iocharset <NULL>"));
				return -EINVAL;
			}
			break;
		case OPT_LOG_OFF:
			set_mnt_opt(pstMntOptions->dwMountOpt, TRANSACTION_OFF);
			break;
		case OPT_LLW:
			set_mnt_opt(pstMntOptions->dwMountOpt, LLW);
			break;
		case OPT_FULL_LLW:
			set_mnt_opt(pstMntOptions->dwMountOpt, FULL_LLW);
			break;
		case OPT_NAME_RULE:
			set_mnt_opt(pstMntOptions->dwMountOpt, ALLOW_OS_NAMING_RULE);
			break;
		case OPT_INTERNAL:
			set_prv_opt(pstMntOptions->dwPrivateOpt, INTERNAL);
			break;
		case OPT_EXTERNAL:
			set_prv_opt(pstMntOptions->dwPrivateOpt, EXTERNAL);
			break;
		case OPT_SETATTR:
			set_prv_opt(pstMntOptions->dwPrivateOpt, SETATTR);
			dwSetup = TRUE;
			break;
		case OPT_UID:
			if (LINUX_MatchInt(&aArgs[0], &dwOption))
			{
				LNX_EMZ(("invalid uid : %s", LINUX_MatchStrdup(&aArgs[0])));
				return -EINVAL;
			}
			pstMntOptions->stExtOpt.dwFsUid = dwOption;
			dwSetup = TRUE;
			break;
		case OPT_GID:
			if (LINUX_MatchInt(&aArgs[0], &dwOption))
			{
				LNX_EMZ(("invalid gid : %s", LINUX_MatchStrdup(&aArgs[0])));
				return -EINVAL;
			}
			pstMntOptions->stExtOpt.dwFsGid = dwOption;
			dwSetup = TRUE;
			break;
		case OPT_UMASK:
			if (LINUX_MatchOctal(&aArgs[0], &dwOption))
			{
				LNX_EMZ(("invalid umask : %s", LINUX_MatchStrdup(&aArgs[0])));
				return -EINVAL;
			}
			pstMntOptions->stExtOpt.wFsFmask = pstMntOptions->stExtOpt.wFsDmask = dwOption;
			dwSetup = TRUE;
			break;
		case OPT_FMASK:
			if (LINUX_MatchOctal(&aArgs[0], &dwOption))
			{
				LNX_EMZ(("invalid fmask : %s", LINUX_MatchStrdup(&aArgs[0])));
				return -EINVAL;
			}
			pstMntOptions->stExtOpt.wFsFmask = dwOption;
			dwSetup = TRUE;
			break;
		case OPT_DMASK:
			if (LINUX_MatchOctal(&aArgs[0], &dwOption))
			{
				LNX_EMZ(("invalid dmask : %s", LINUX_MatchStrdup(&aArgs[0])));
				return -EINVAL;
			}
			pstMntOptions->stExtOpt.wFsDmask = dwOption;
			dwSetup = TRUE;
			break;
		default:
			LNX_VMZ(("unrecognizable options: %d", nToken));
			return -EINVAL;
		}
	}

	/* If owner and authority option is not set in external mount environment,
	 * owner (uid,gid) is set in root, authority (fmask,dmask) is set in '0'
	 */
	if (dwSetup == FALSE)
		pstMntOptions->stExtOpt.wFsFmask = pstMntOptions->stExtOpt.wFsDmask = RFS_DEFAULT_PERM_MASK;

	if (test_prv_opt(pstMntOptions->dwPrivateOpt, INTERNAL) &&
			test_prv_opt(pstMntOptions->dwPrivateOpt, EXTERNAL))
	{
		DPRINTK("alternative options of internal or external\n");
		return -EINVAL;
	}

	if (dwSetup && !test_prv_opt(pstMntOptions->dwPrivateOpt, EXTERNAL))
	{
		DPRINTK("uid,gid,fmask,dmask,umask should be used with external mount option");
		return -EINVAL;
	}

	/* check and load NLS Table */
	return _InitializeNLS(pstMntOptions);
}

/**
 * @brief		make NS_MOUNT_FLAG for mount disk and update mount option
 * @param[in]	pSb				super block
 * @param[in]	pstMntOptions	parsed mount options
 * @return		zero on success
 */
static LINUX_ERROR
_MakeVcbFlag(
	IN	PLINUX_SUPER	pSb, 
	IN	PMOUNT_OPTIONS	pstMntOptions)
{
	/* VcbFlag and MntFlag are the same. use MOUNT_FLAG */
	MOUNT_FLAG	nMntFlag = 0;
	unsigned int nPrvFlag = 0;

	LNX_ASSERT_ARG(pSb, -EINVAL);
	LNX_ASSERT_ARG(pstMntOptions, -EINVAL);

	/* get parsed mount options */
	nMntFlag |= pstMntOptions->dwMountOpt;
	nPrvFlag |= pstMntOptions->dwPrivateOpt;

	/*
	 * Default mount flag
	 */
#ifdef CONFIG_RFS_FS_FAT_HPA
	/* for hidden protected area */
	set_mnt_opt(nMntFlag, HPA);
	set_mnt_opt(nMntFlag, HPA_CREATE);
#endif
#ifndef CONFIG_RFS_FS_FAT_VFAT	
	/* initialize vcb's private flag (not mount flag) */
	set_prv_opt(nPrvFlag, VFAT_OFF);
#endif
#ifdef CONFIG_RFS_FS_SPECIAL
	/* enable special file: symlink, fifo, socket (default flag) */
	set_mnt_opt(nMntFlag, FILE_SPECIAL);
#endif
#ifdef CONFIG_RFS_FS_ERASE_SECTOR
	set_mnt_opt(nMntFlag, ERASE_SECTOR);
#endif
#ifdef CONFIG_RFS_FS_PERMISSION
	/* enable uid/gid/permission */
	set_mnt_opt(nMntFlag, FILE_GUID);
#endif
#ifdef CONFIG_RFS_FS_XATTR
	/* enable extended attribute */
	set_mnt_opt(nMntFlag, FILE_XATTR);
#endif
#ifdef CONFIG_RFS_FS_SECURITY
	/* enable SELinux */
	set_prv_opt(nPrvFlag, SELINUX);
#endif
	
	/*
	 * Alternative mount flags
	 */
	/* log_off mode */
	if (test_mnt_opt(nMntFlag, TRANSACTION_OFF))
	{
		/* if log is not used, some flags should be masked for transaction off */
		clear_mnt_opt(nMntFlag, LOG_INIT);
		clear_mnt_opt(nMntFlag, LLW);
		clear_mnt_opt(nMntFlag, FULL_LLW);
	}
	/* log_on mode */
	else if (test_mnt_opt(nMntFlag, LLW) && test_mnt_opt(nMntFlag, FULL_LLW))
	{
		/* if both logging types are set, choose FULL_LLW */
		clear_mnt_opt(nMntFlag, LLW);
		set_mnt_opt(nMntFlag, FULL_LLW);
	}

	/*
	 * set internal or external volume
	 */
	if (test_prv_opt(nPrvFlag, EXTERNAL) &&
			!test_prv_opt(nPrvFlag, INTERNAL))
	{
		/* use as external volume, if 'external' is set */
		set_prv_opt(nPrvFlag, EXTERNAL);
		clear_prv_opt(nPrvFlag, INTERNAL);

		/* forbidden flag for external volume */
		clear_mnt_opt(nMntFlag, FILE_XATTR);
		clear_prv_opt(nPrvFlag, SELINUX);
		clear_mnt_opt(nMntFlag, FILE_GUID);

		/* forbid flag for breaking naming rule */
		clear_mnt_opt(nMntFlag, ALLOW_OS_NAMING_RULE);

		/* special file is permitted on external mount, though */
	}
	else
	{
		/*
		 * use as internal volume, if 'internal' is set
		 *  or 'external' isn't set
		 */
		set_prv_opt(nPrvFlag, INTERNAL);
		clear_prv_opt(nPrvFlag, EXTERNAL);
	}

	/* Linux VFS flags - readonly mode */
	if (pSb->s_flags & LINUX_MS_RDONLY)
	{
		set_mnt_opt(nMntFlag, READ_ONLY);

		/* some flags should be masked for read-only */
		clear_mnt_opt(nMntFlag, CLEAN_NAND);
		clear_mnt_opt(nMntFlag, LOG_INIT);
		/* disable log if readonly mode */
		clear_mnt_opt(nMntFlag, LLW);
		clear_mnt_opt(nMntFlag, FULL_LLW);
		set_mnt_opt(nMntFlag, TRANSACTION_OFF);
		clear_mnt_opt(nMntFlag, HPA_CREATE);
		clear_mnt_opt(nMntFlag, ERASE_SECTOR);
	}

	pstMntOptions->dwMountOpt = nMntFlag;
	pstMntOptions->dwPrivateOpt = nPrvFlag;

	return 0;
}

/******************************************************************************/
/* Nestle Private Fucnctions : volume management apis                         */
/******************************************************************************/
/**
 * @brief 		show options
 */
LINUX_ERROR
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
VolShowOptions(
	IN PLINUX_SEQ_FILE	pSeq,
	IN PLINUX_VFS_MNT	plxMnt)
#else
VolShowOptions(
	IN PLINUX_SEQ_FILE	pSeq,
	IN PLINUX_DENTRY	root)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	PLINUX_SUPER			pSb = plxMnt->mnt_sb;
#else
	PLINUX_SUPER			pSb = root->d_sb;
#endif
	PVOLUME_CONTROL_BLOCK	pVcb = NULL;
	struct MountFlag *		pstMntFlag = NULL;
	unsigned int			dwVcbFlag;

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	/* special handling */
	if ((pVcb->dwPrvFlag & PRIVATE_VCB_VFAT_OFF) != PRIVATE_VCB_VFAT_OFF)
	{
		seq_puts(pSeq, ",vfat");
	}
	else
	{
		//seq_puts(pSeq, ",vfat_off");
	}

	if (IS_VCB_EXTERNAL(pVcb))
	{
		/* compatible with linux vfat's uid, gid, umask option */
		if (IS_VCB_SETATTR(pVcb))
		{
			seq_puts(pSeq, ",setattr");
		}

		/* or apply just uid, gid, umask during lookup */
		seq_printf(pSeq, ",uid=%u", pVcb->stExtOpt.dwFsUid);
		seq_printf(pSeq, ",gid=%u", pVcb->stExtOpt.dwFsGid);
		seq_printf(pSeq, ",fmask=%04o", pVcb->stExtOpt.wFsFmask);
		seq_printf(pSeq, ",dmask=%04o", pVcb->stExtOpt.wFsDmask);
	}

	/* parse mount flags or private flags
	 * print out the proper message for show_options() */
	pstMntFlag = stMntFlagMaps;
	dwVcbFlag = pVcb->dwFlag;

	for ( ; (pstMntFlag->dwMntFlag != 0xFFFFFFFF); pstMntFlag++)
	{
		/* private flags */
		if (pstMntFlag->optType == OPT_PRIVATE)
		{
			dwVcbFlag = pVcb->dwPrvFlag;
			continue;
		}

		/* do not show some options */
		if (test_mnt_opt(pstMntFlag->dwMntFlag, ERASE_SECTOR))
			continue;
		if (test_mnt_opt(pstMntFlag->dwMntFlag, FILE_SPECIAL))
			continue;
		if (test_prv_opt(pstMntFlag->dwMntFlag, VFAT_OFF))
			continue;
		if (test_prv_opt(pstMntFlag->dwMntFlag, INTERNAL))
			continue;

		/* show options */
		if (pstMntFlag->dwMntFlag & dwVcbFlag)
		{
			seq_puts(pSeq, pstMntFlag->shows);
		}
	}

	/* show iocharset */
	seq_printf(pSeq, ",iocharset=%s", pVcb->pNlsTableIo->charset);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
/**
 * @brief		get statistics on a file system (super_operation: statfs)
 * @param[in]	pDentry	linux dentry
 * @param[out]	pStat	structure to fill stat info
 * @return      return 0 on success, nErrno on failure
 */
LINUX_ERROR
VolGetVolumeInformation(
	IN	PLINUX_DENTRY	pDentry, 
	OUT	PLINUX_KSTATFS	pStat)
#else
/**
 * @brief		get statistics on a file system (super_operation: statfs)
 * @param[in]	pSb    super block
 * @param[out]	pStat  structure to fill stat info
 * @return      return 0 on success, nErrno on failure
 */
LINUX_ERROR
VolGetVolumeInformation(
	IN	PLINUX_SUPER	pSb, 
	OUT	PLINUX_KSTATFS	pStat)
#endif
{

	PVOLUME_CONTROL_BLOCK		pVcb;
	VOLUME_INFORMATION			stVolInfo;
	FERROR						nErr = FERROR_NOT_SUPPORTED;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
	PLINUX_SUPER				pSb = NULL;

	LNX_ASSERT_ARG(pDentry, -EINVAL);
	pSb = pDentry->d_sb;
#endif

	LNX_ASSERT_ARG(pSb, -EINVAL);

	LNX_IMZ(("[in] VOL get info, %d pid", LINUX_g_CurTask->pid));

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	if ((NULL == pVcb->pVolumeOps) ||
			(NULL == pVcb->pVolumeOps->pfnGetVolumeInformation))
	{
		LNX_CMZ(("No Native interface for GetVolumeInfo"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	nErr = pVcb->pVolumeOps->pfnGetVolumeInformation(pVcb, &stVolInfo);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS GetVolumeInformation fails (nErr: 0x%08x): %d pid",
				-nErr, LINUX_g_CurTask->pid));
		return RtlLinuxError(nErr);
	}

	/*
	 * 'f_type' means the magic number of filesystem.
	 * For the details of magic number, refer to man page of statfs
	 */
	pStat->f_type = (long) stVolInfo.dwFsType;			/* magic number */
	pStat->f_bsize = (long) stVolInfo.dwClusterSize;	/* cluster size */
	pStat->f_blocks = (long) stVolInfo.dwNumClusters;
	pStat->f_bfree = (long) stVolInfo.dwNumFreeClusters;
	pStat->f_bavail = (long) stVolInfo.dwNumAvailClusters;
	pStat->f_namelen = stVolInfo.dwMaxFileNameLen;

	LNX_IMZ(("[out] VOL get info end, %d pid", LINUX_g_CurTask->pid));
	return 0;
}

/**
 * @brief		release the super block (unmount)
 * @param[in]	pSb    super block
 * @return		void
 */
void
VolUnmountVolume(
	IN	PLINUX_SUPER		pSb)
{
	PVOLUME_CONTROL_BLOCK	pVcb;
	FERROR					nErr;

	LNX_ASSERT_ARGV(pSb);

	LNX_IMZ(("[in] VOL unmount"));

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return;
	}

	/* check for unmount handler */
	if ((NULL == pVcb->pVolumeOps) ||
			(NULL == pVcb->pVolumeOps->pfnUnmountDisk))
	{
		LNX_CMZ(("No Native interface for UnmountDisk"));
		RFS_ASSERT(0);
		return;
	}

	/*
	 * release native volume
	 * : If flag is UNMOUNT_FORCE,
	 *   pfnUnmountDisk always returns FERROR_NO_ERROR
	 */
	nErr = pVcb->pVolumeOps->pfnUnmountDisk(pVcb, UNMOUNT_FORCE);

	if (nErr != FERROR_NO_ERROR)
	{
		LNX_CMZ(("UnmountDisk should succeed with UNMOUNT_FORCE flag"));
		RFS_ASSERT(0);
		return;
	}

	/*
	 * release root vnode
	 */

	/* check root inode */
	if ((NULL == pVcb->pRoot) || (NULL == pVcb->pRoot->pVnodeOps))
	{
		LNX_CMZ(("Root Inode isn't initialized"));
		RFS_ASSERT(0);

		/* release vcb and clear Vcb of SB */
		VcbDestroy(pVcb);
		return;
	}

	/* destroy root node of native filesystem */
	if (pVcb->pRoot->pVnodeOps->pfnDestroyNode)
	{
		/* it always returns FERROR_NO_ERROR */
		nErr = pVcb->pRoot->pVnodeOps->pfnDestroyNode(pVcb->pRoot);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_CMZ(("DestoryNode should succeed"));
			RFS_ASSERT(0);

			/* should return after releasing memory of root vnode and vcb */
		}
	}

	/* release memory of root vnode */
	LINUX_KmemCacheFree(g_pVnodeCache, pVcb->pRoot);
	pVcb->pRoot = NULL;
  
	/*
	 * release vcb and clear Vcb of SB
	 */
	VcbDestroy(pVcb);

	LNX_IMZ(("[out] VOL unmount end %d pid", LINUX_g_CurTask->pid));

	return;
}

/**
 * @brief		write the superblock
 * @param[in]	pSb    super block
 * @return		void
 *
 * In Current Design, BTFS doesn't set s_dirt and BTFS is the only NativeFS.
 * So, WriteVolume is never called by VFS if Linux Glue set s_dirt.
 * But Linux Glue can't decide to mark the volume dirty.
 */
void
VolWriteVolume(
	IN	PLINUX_SUPER		pSb)
{
	PVOLUME_CONTROL_BLOCK  	pVcb = NULL;
	FERROR					nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARGV(pSb);

	if (pSb->s_flags & LINUX_MS_RDONLY)
	{
		LNX_CMZ(("Volume is mounted read-only"));
		RFS_ASSERT(0);
		return;
	}

	LNX_IMZ(("[in] VOL write volume, %d pid", LINUX_g_CurTask->pid));

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return;
	}

	/* check operation */
	if ((NULL == pVcb->pVolumeOps) ||
			(NULL == pVcb->pVolumeOps->pfnWriteVolume))
	{
		LNX_CMZ(("No Native interface for WriteVolume"));
		RFS_ASSERT(0);
		return;
	}

	/* write volume of Native FS */
	nErr = pVcb->pVolumeOps->pfnWriteVolume(pVcb);
	if (nErr)
	{
		LNX_EMZ(("NativeFS WriteVolume fails (nErr: 0x%08x)", -nErr));
		goto out;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	/* clear dirty flag when NativeFS writes dirty volume successfully */
	pSb->s_dirt = 0;
#endif

	LNX_IMZ(("[out] VOL write volume end, %d pid", LINUX_g_CurTask->pid));

out:
	return;
}

/**
 *  @brief		Function to build super block structure (Mount function)
 *  @param[in]	pSb			super block pointer
 *  @param[in]	pData		pointer for an optional date
 *  @param[in]	nFlagSilent	control flag for error message
 *  @return     return 0 on success, nErrno on failure
 */
LINUX_ERROR
VolFillSuper(
	IN	PLINUX_SUPER	pSb, 
	IN	void*			pData, 
	IN	int				nFlagSilent)
{
	MOUNT_OPTIONS			stMntOptions;
	PVOLUME_CONTROL_BLOCK	pVcb = NULL;
	LINUX_ERROR				nLinuxError = 0;
	FERROR					nErr = FERROR_NO_ERROR;
	PVNODE					pRootVnode = NULL;
	MOUNT_FLAG				dwMountFlag;

	LNX_ASSERT_ARG(pSb, -EINVAL);

	/* parse mount options & load NLS Table */
	nLinuxError = _ParseMountOptions(pSb, pData, &stMntOptions);
	if (nLinuxError < 0)
	{
		LNX_EMZ(("Fail to parse options: %s (errno: %d)",
				(char *)pData, nLinuxError));
		return nLinuxError;
	}

	/* make vcb flag for mount */
	nLinuxError = _MakeVcbFlag(pSb, &stMntOptions);
	if (nLinuxError < 0)
	{
		LNX_EMZ(("Fail to parse mount flag"));
		return nLinuxError;
	}

	/* allocate & initialize vcb (basic value) */
	pVcb = VcbCreate(pSb, stMntOptions.dwMountOpt,
			stMntOptions.dwPrivateOpt, stMntOptions.pTableIo,
			&stMntOptions.stExtOpt);
	if (!pVcb)
	{
		LNX_EMZ(("VcbCreate failes - alloc memory for VCB"));
		return -ENOMEM;
	}

	/*
	 * mount request to NativeFS
	 */
	dwMountFlag = pVcb->dwFlag;

	/* NativeFS make Root Vnode during mount */
	nErr = NativefsMountDisk(pSb, pVcb, &dwMountFlag, &pRootVnode);

	/* Fail to mount the device in Native */
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS Mount fails(nErr: 0x%08x, errno: %d)",
				-nErr, RtlLinuxError(nErr)));

		/* release Vnode */
		if (pRootVnode != NULL)
		{
			VnodeRelease(pRootVnode);
		}

		/* release Vcb */
		VcbDestroy(pVcb);
	
		nLinuxError = -EINVAL;
	}
	/* Success to mount the device in Native */
	else
	{
		if (NULL == pRootVnode)
		{
			LNX_CMZ(("MountDisk didn't return RootVnode"));
			RFS_ASSERT(0);
			nLinuxError = -EFAULT;
		}
		else
		{

			if (test_mnt_opt(dwMountFlag, LLW) &&
					test_mnt_opt(dwMountFlag, FULL_LLW))
			{
				/* if both logging types are set, choose FULL_LLW */
				clear_mnt_opt(dwMountFlag, LLW);
			}

			pVcb->dwFlag = dwMountFlag;

			/* Set RootVnode to Vcb */
			nErr = VcbSetRoot(pVcb, pRootVnode);
			if (nErr != FERROR_NO_ERROR)
			{
				nLinuxError = RtlLinuxError(nErr);

				/* unmount native volume before releasing vcb memory in glue */
				nErr = pVcb->pVolumeOps->pfnUnmountDisk(pVcb, UNMOUNT_FORCE);
				if (nErr != FERROR_NO_ERROR)
				{
					LNX_CMZ(("UnmountDisk should succeed with UNMOUNT_FORCE flag"));
					RFS_ASSERT(0);
				}

				/* release vnode */
				VnodeRelease(pRootVnode);

				/* release Vcb */
				VcbDestroy(pVcb);
			}
			// 2011120021 bugfix for USE_AFTER_FREE issue by SISO
			/* 
			* VcbSetDentryOps was called in else part to avoid the USE_AFTER_FREE case.
			*/
			else
			{
				// 20090807 bugfix of alias dentry
				/* Set dentry operation
				* Basically Linux VFS compares file's name by case sensitive.
				* But some particular filesystem, such as FAT,
				* compares names by case insensitive. So, these filesystem needs
				* own dentry operation to search names by case insensitive.
				*/
				VcbSetDentryOps(pVcb);
			}
		}
	}

	return nLinuxError;	
}

/**
 * @brief		parse remount option
 * @param[in]	pOption			user's option string
 * @return		remount flag on success, or zero
 */
static REMOUNT_FLAG
_ParseRemountOptions(
	IN	void*			pOptions)
{
	LINUX_SUBSTRING		aArgs[LINUX_MAX_OPT_ARGS];
	char*				pChunkOption = NULL;
	REMOUNT_FLAG		dwRemountFlag = 0;

	while ((pChunkOption = LINUX_StrSep((char **) &pOptions, ",")) != NULL) 
	{
		int	nToken;

		if (!(*pChunkOption))
		{
			continue;
		}

		nToken = LINUX_MatchToken(pChunkOption, g_MountOption, aArgs);

		switch (nToken)
		{
		case OPT_LOG_OFF:
			set_mnt_opt(dwRemountFlag, TRANSACTION_OFF);
			break;
		case OPT_LLW:
			set_mnt_opt(dwRemountFlag, LLW);
			break;
		case OPT_FULL_LLW:
			set_mnt_opt(dwRemountFlag, FULL_LLW);
			break;
		default:
			LNX_VMZ(("ignoring other options: %d", nToken));
			break;
		}
	}

	/* log_off mode */
	if (test_mnt_opt(dwRemountFlag, TRANSACTION_OFF))
	{
		/* if log is not used, some flags should be masked for transaction off */
		clear_mnt_opt(dwRemountFlag, LLW);
		clear_mnt_opt(dwRemountFlag, FULL_LLW);
	}
	/* log_on mode */
	else if (test_mnt_opt(dwRemountFlag, LLW) && test_mnt_opt(dwRemountFlag, FULL_LLW))
	{
		/* if both logging types are set, choose FULL_LLW */
		clear_mnt_opt(dwRemountFlag, LLW);
		set_mnt_opt(dwRemountFlag, FULL_LLW);
	}

	return dwRemountFlag;
}

#define INTERNAL_REMOUNT_MASK \
	(MOUNT_TRANSACTION_OFF | MOUNT_LLW | MOUNT_FULL_LLW | MOUNT_READ_ONLY)

/**
 * @brief		allow to remount to make a writable file system readonly
 * @param[in]	pSb    super block
 * @param[io]	nFlags to chang the mount flags
 * @param[in]	data  private data
 * @return      return 0
 */
LINUX_ERROR 
VolRemountVolume(
	IN	PLINUX_SUPER	pSb, 
	IO	int*			pFlag, 
	IN	char*			pData)
{
	PVOLUME_CONTROL_BLOCK  	pVcb = NULL;
	FERROR					nErr = FERROR_NO_ERROR;
	REMOUNT_FLAG			dwRemountFlag;
	MOUNT_FLAG				dwNewMountFlag;

	LNX_ASSERT_ARG(pSb, -EINVAL);

	LNX_DMZ(("Volume Remount, %d pid", LINUX_g_CurTask->pid));

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	/* check operation */
	if ((NULL == pVcb->pVolumeOps) ||
			(NULL == pVcb->pVolumeOps->pfnRemountDisk))
	{
		LNX_CMZ(("No Native interface for RemountDisk"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	dwRemountFlag = _ParseRemountOptions(pData);
	/* Linux VFS flags - readonly mode */
	if (*pFlag & LINUX_MS_RDONLY)
	{
		set_mnt_opt(dwRemountFlag, READ_ONLY);
		set_mnt_opt(dwRemountFlag, TRANSACTION_OFF);
		clear_mnt_opt(dwRemountFlag, LLW);
		clear_mnt_opt(dwRemountFlag, FULL_LLW);
	}
	else
	{
		clear_mnt_opt(dwRemountFlag, READ_ONLY);
	}

	dwRemountFlag &= INTERNAL_REMOUNT_MASK;

	LNX_DMZ(("Remount flag: 0x%08x", dwRemountFlag));

	/* write volume of Native FS */
	nErr = pVcb->pVolumeOps->pfnRemountDisk(pVcb, &dwRemountFlag);
	if (nErr)
	{
		LINUX_ERROR		dwLinuxError = RtlLinuxError(nErr);

		LNX_EMZ(("NativeFS RemountDisk(0x%08x) fails (nErr: 0x%08x)",
					dwRemountFlag, -nErr));

		/* ENOSYS: pfnRemountDisk can't handle this remount flag */
		return (dwLinuxError == -ENOSYS)? (-EINVAL): dwLinuxError;
	}

	dwNewMountFlag = (pVcb->dwFlag & ~INTERNAL_REMOUNT_MASK) | (dwRemountFlag);

	/* Linux VFS flags - readonly mode */
	if (test_mnt_opt(dwNewMountFlag, READ_ONLY))
	{
		/* some flags should be masked for read-only */
		clear_mnt_opt(dwNewMountFlag, CLEAN_NAND);
		clear_mnt_opt(dwNewMountFlag, LOG_INIT);
		clear_mnt_opt(dwNewMountFlag, HPA_CREATE);
		clear_mnt_opt(dwNewMountFlag, ERASE_SECTOR);
	}

	if (test_mnt_opt(dwNewMountFlag, LLW) &&
			test_mnt_opt(dwNewMountFlag, FULL_LLW))
	{
		/* if both logging types are set, choose FULL_LLW */
		clear_mnt_opt(dwNewMountFlag, LLW);
	}

	pVcb->dwFlag = dwNewMountFlag;

	return 0;
}

/******************************************************************************/
/* Glue Private Fucnctions : volume utility                                   */
/******************************************************************************/
/**
 * @brief Check Disk utility
 * @param pSb		super block for check
 * @return zero on success, or errno
 */
LINUX_ERROR
VolChkDisk(
	IN	PLINUX_SUPER	pSb)
{
	PVOLUME_CONTROL_BLOCK	pVcb = NULL;
	PNATIVEFS_OPERATIONS		pNativeFS = NULL;
	CHKDSK_TYPE				dwChkFlag = 0;
	FERROR					nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pSb, -EINVAL);

	pVcb = VolGetVcbFromSb(pSb);
	if (unlikely(NULL == pVcb))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	/* get nativeFS from table */
	pNativeFS = NativefsGetNative(pSb->s_type->name);
	if (!pNativeFS)
	{
		LNX_CMZ(("Fail to get NativeFS(%s)", pSb->s_type->name));
		return -EINVAL;
	}

	if (NULL == pNativeFS->pfnChkdsk)
	{
		LNX_CMZ(("No Native interface for chkdsk"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/* Check Flag : not modify volume */
	dwChkFlag = (CHKDSK_SHOW | CHKDSK_CHECK_ONLY);

	/* chkdsk */
	nErr = pNativeFS->pfnChkdsk(pVcb, &dwChkFlag, sizeof(CHKDSK_TYPE), NULL, 0);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS Chkdsk fails (nErr : 0x%x)", -nErr));
		return RtlLinuxError(nErr);
	}

	return 0;
}


/*
 * Define symbols
 */
#include <linux/module.h>

EXPORT_SYMBOL(VolFillSuper);

// end of file
