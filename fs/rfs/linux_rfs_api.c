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
 * @file        linux_rfs_api.c
 * @brief       This file defines operation sets for Linux VFS.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */


#include "rfs_linux.h"
#include "linux_vnode.h"
#include "linux_volume.h"
#include "linux_dir.h"
#include "linux_file.h"
#ifdef CONFIG_RFS_FS_XATTR
#include "linux_xattr.h"
#endif

#include <linux/mpage.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_API)

/*
 * Function prototypes
 */
static inline LINUX_ERROR 	LxReadPage(
								PLINUX_FILE				pFile,
								PLINUX_PAGE				pPage);
static inline LINUX_ERROR 	LxWritePage(
								PLINUX_PAGE				pPage,
								PLINUX_WB_CTL			pWbc);
static inline LINUX_ERROR 	LxReadPages(
								PLINUX_FILE				pFile,
								PLINUX_ADDRESS_SPACE	pMapping,
								PLINUX_LIST				pPages,
								unsigned int			dwNrPages);
static inline LINUX_ERROR	LxWritePages(
								PLINUX_ADDRESS_SPACE	pMapping,
								PLINUX_WB_CTL			pWbc);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static inline int			LxWriteBegin(
								PLINUX_FILE				pFile,
								PLINUX_ADDRESS_SPACE	pMapping,
								LINUX_OFFSET			llOffset,
								unsigned 				dwLen,
								unsigned 				dwFlags,
								PLINUX_PAGE 			*ppPage,
								void 					**ppFsData);
static inline int 	LxWriteEnd(
								PLINUX_FILE				pFile,
								PLINUX_ADDRESS_SPACE 	pMapping,
								LINUX_OFFSET 			llOffset,
								unsigned 				dwLen,
								unsigned 				dwCopied,
								PLINUX_PAGE 			pPage,
								void 					*pFsData);
#else
static inline LINUX_ERROR	LxPrepareWrite(
								PLINUX_FILE				pFile,
								PLINUX_PAGE 			pPage,
								unsigned int 			from,
								unsigned int 			to);
static inline LINUX_ERROR 	LxCommitWrite(
								PLINUX_FILE				pFile,
								PLINUX_PAGE 			pPage,
								unsigned int 			from,
								unsigned int 			to);
#endif	/* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static inline LINUX_SSIZE_T	LxFileAioRead(
								PLINUX_KIOCB		pKiocb, 
								PLINUX_CIOVEC		pIovec,
								unsigned long		nSegs,
								LINUX_OFFSET		llPos);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
static int LxDentryHashI(
				PLINUX_DENTRY_CONST	pDentry,
				PLINUX_INODE_CONST	pInode,
				PLINUX_QSTR		pQstr);
#else
static int LxDentryHashI(
				PLINUX_DENTRY   pDentry,
				PLINUX_QSTR	pQstr);
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
static int LxDentryCmpI(
				IN	PLINUX_DENTRY_CONST	pParent,
				IO	PLINUX_INODE_CONST	pInode,
				IN	PLINUX_DENTRY_CONST	pDentry,
				IO	PLINUX_INODE_CONST	pInode1,
				IN      unsigned int		length, 
				IN      const char		*str,   
				IN	PLINUX_QSTR_CONST	name);
#else
static int LxDentryCmpI(
				PLINUX_DENTRY	pDentry,
				PLINUX_QSTR	pStrA,
				PLINUX_QSTR	pStrB);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
static int LxDentryRevalidateI(
						PLINUX_DENTRY	pDentry,
						PLINUX_NAMEIDATA	pNd);
#else
static int LxDentryRevalidateI(
						PLINUX_DENTRY	pDentry,
						unsigned int	dwFlag);
#endif

/******************************************************************************/
/* Linux VFS Operation Vectors                                                */
/******************************************************************************/

/*
 * super operations
 */
LINUX_SUPER_OPS g_stLinuxSuperOps =
{
	.alloc_inode    = LxAllocateInode,
	.destroy_inode  = LxDestroyInode,
	.write_inode    = VnodeWriteInode,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.evict_inode    = VnodeEvictInode,
#else
	.delete_inode   = VnodeDeleteInode,
	.clear_inode	= VnodeClearInode,
#endif
	.put_super      = VolUnmountVolume,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	.write_super    = VolWriteVolume,
#endif
	.statfs         = VolGetVolumeInformation,
	.remount_fs     = VolRemountVolume,
	.show_options	= VolShowOptions,
};


/*
 * address space operations
 */
LINUX_ADDRESS_SPACE_OPS g_stLinuxAddrOps =
{
	.readpage		= LxReadPage,
	.writepage		= LxWritePage,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 38)
	.sync_page      = LINUX_BlockSyncPage,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	.write_begin	= LxWriteBegin,
	.write_end		= LxWriteEnd,
#else
	.prepare_write	= LxPrepareWrite,
	.commit_write	= LxCommitWrite,
#endif
	.direct_IO		= FileDirectRW,
	.readpages		= LxReadPages,
	.writepages		= LxWritePages,
};


/*
 * file & inode operations for directory
 */
LINUX_FILE_OPS g_stLinuxDirOps =
{
	.read			= LINUX_GenericReadDir,
	.readdir		= DirReadDirectory,
	.fsync          = FileSync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.unlocked_ioctl		= FileIoctl,
#else
	.ioctl			= FileIoctl,
#endif
};

LINUX_INODE_OPS g_stLinuxDirInodeOps =
{
	.create			= DirCreateFile,
	.lookup			= DirLookup,
	.unlink			= DirDelete,
	.mkdir			= DirCreateDirectory,
	.rmdir			= DirDelete,
	.rename			= DirRename,
	.permission		= FilePermission,
	.setattr		= FileSetAttribute,
#ifdef CONFIG_RFS_FS_SPECIAL
	.symlink		= DirSymbolicLink,
	.mknod			= DirCreateNode,
#endif
#ifdef CONFIG_RFS_FS_XATTR
	.setxattr		= LINUX_GenericSetXattr,
	.getxattr		= LINUX_GenericGetXattr,
	.listxattr		= XattrListNames,
	.removexattr	= LINUX_GenericRemoveXattr,
#endif
};


/*
 * file & inode operations for file
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
	#define fsReadFile		LINUX_DoSyncRead
#else
	#define fsReadFile		LINUX_GenericFileRead
#endif

LINUX_FILE_OPS g_stLinuxFileOps =
{
	.open			= FileOpen,
	.llseek			= FileSeek,
#ifdef CONFIG_RFS_FS_SYNC_ON_CLOSE
	.flush			= FileFlush,
#endif
	.release		= FileClose,
	.read			= fsReadFile,
	.write          = FileWrite,
	.mmap           = LINUX_GenericFileMmap,
	.fsync          = FileSync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.unlocked_ioctl		= FileIoctl,
#else
	.ioctl			= FileIoctl,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
	.splice_read	= LINUX_GenericSpliceRead,
	.splice_write	= LINUX_GenericSpliceWrite,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
	.sendfile		= LINUX_GenericSendFile,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
	.aio_read		= LxFileAioRead,
	.aio_write		= FileAioWrite,
#endif
};

LINUX_INODE_OPS g_stLinuxFileInodeOps =
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	.truncate       = FileTruncate,
#endif
	.permission     = FilePermission,
	.setattr        = FileSetAttribute,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
	//.fallocate		= FileAllocate,
#endif
#ifdef CONFIG_RFS_FS_XATTR
	.setxattr		= LINUX_GenericSetXattr,
	.getxattr		= LINUX_GenericGetXattr,
	.listxattr		= XattrListNames,
	.removexattr	= LINUX_GenericRemoveXattr,
#endif
};

/*
 * inode operations for symlink
 */
LINUX_INODE_OPS g_stLinuxSymlinkInodeOps =
{
	.readlink       = LINUX_GenericReadLink,
	.follow_link    = FileFollowLink,
	.put_link       = FilePutLink,
#ifdef CONFIG_RFS_FS_XATTR
	.setxattr		= LINUX_GenericSetXattr,
	.getxattr		= LINUX_GenericGetXattr,
	.listxattr		= XattrListNames,
	.removexattr	= LINUX_GenericRemoveXattr,
#endif
	.setattr        = FileSetAttribute,
};

/*
 * inode operations for symlink
 */
LINUX_INODE_OPS g_stLinuxSpecialInodeOps = 
{
	.permission		= FilePermission,
	.setattr		= FileSetAttribute,
#ifdef CONFIG_RFS_FS_XATTR
	.setxattr		= LINUX_GenericSetXattr,
	.getxattr		= LINUX_GenericGetXattr,
	.listxattr		= XattrListNames,
	.removexattr	= LINUX_GenericRemoveXattr,
#endif
};

/*
 * dentry operations for case-insensitive
 */
LINUX_DENTRY_OPS g_stLinuxDentryOpsI =
{
	.d_revalidate = LxDentryRevalidateI,
	.d_hash		= LxDentryHashI,
	.d_compare	= LxDentryCmpI,
};

/******************************************************************************/
/* Define Operations                                                          */
/******************************************************************************/

/*
 * address space operations
 */
#define fsGetBlock		FileGetBlock

/**
 * @brief		read a specified page
 * @param[in]	file		file to read
 * @param[in]	page		page to read
 * @return		return 0 on success
 */
static inline LINUX_ERROR 
LxReadPage(
	IN	PLINUX_FILE		pFile, 
	IN	PLINUX_PAGE		pPage)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR nDebug;

		nDebug = LINUX_MpageReadpage(pPage, fsGetBlock);

		LNX_VMZ(("mpage_readpage (ino: %lu): index %lu, result %d",
					pFile->f_mapping->host->i_ino,
					pPage->index,
					nDebug));
		return nDebug;
	}
#else
	return LINUX_MpageReadpage(pPage, fsGetBlock);
#endif
}

/**
 * @brief		write a specified page
 * @param[in]	page	to write page
 * @param[in]	wbc		writeback control	
 * @return		return 0 on success, errno on failure
 */
static inline LINUX_ERROR 
LxWritePage(
	IN	PLINUX_PAGE		pPage, 
	IN	PLINUX_WB_CTL	pWbc)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR nDebug;

		nDebug = LINUX_BlockWriteFullPage(pPage, fsGetBlock, pWbc);

		LNX_VMZ(("block_write_full_page (ino: %lu): index %lu, result %d",
					pPage->mapping->host->i_ino,
					pPage->index,
					nDebug));

		return nDebug;
	}
#else
	return LINUX_BlockWriteFullPage(pPage, fsGetBlock, pWbc);
#endif
}

/**
 * @brief		read multiple pages
 * @param[in]	file		file to read
 * @param[in]	mapping     address space to read
 * @param[in]	pages       page list to read	
 * @param[in]	nr_pages	number of pages 
 * @return		return 0 on success, errno on failure
 */
static inline LINUX_ERROR 
LxReadPages(
	IN	PLINUX_FILE				pFile, 
	IN	PLINUX_ADDRESS_SPACE	pMapping,
	IN	PLINUX_LIST				pPages, 
	IN	unsigned int			dwNrPages)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR		nDebug = 0;

		nDebug = LINUX_MpageReadpages(pMapping, pPages, dwNrPages, fsGetBlock);

		LNX_VMZ(("mpage_readpages (ino: %lu): result %d",
					pMapping->host->i_ino, nDebug));
		return nDebug;
	}
#else
	return LINUX_MpageReadpages(pMapping, pPages, dwNrPages, fsGetBlock);
#endif
}

/**
 * @brief		write multiple pages
 * @param[in]	mapping		address space to write
 * @param[in]	wbc         writeback_control	
 * @return		return 0 on success, errno on failure
 */
static inline LINUX_ERROR 
LxWritePages(
	IN	PLINUX_ADDRESS_SPACE	pMapping, 
	IN	PLINUX_WB_CTL			pWbc)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR		nDebug = 0;
	
		nDebug = LINUX_MpageWritePages(pMapping, pWbc, fsGetBlock);

		LNX_VMZ(("mpage_writepages (ino: %lu): result %d",
					pMapping->host->i_ino, nDebug));

		return nDebug;
	}
#else
	return LINUX_MpageWritePages(pMapping, pWbc, fsGetBlock);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
/**
 * @brief		read some partial page to write rest page
 *
 * @param[in]	pFile 		to read file
 * @param[in]	pMapping	address mapping of file to read
 * @param[in]	llOffset	start position in file (pos)
 * @param[in]	dwLen		bytes counts to prepare in page 
 * @param[in]	dwFlags		flags
 * @param[in]	ppPage		address of page to be read
 * @param[in]	ppFsData	
 * @return		return 0 on success, errno on failure
 *
 * This function requires addtional code saving inode->i_size because there is
 * case when inode->i_size is chagned after LINUX_ContPrepareWrite.  
 */
static inline int
LxWriteBegin(
	PLINUX_FILE pFile,
	PLINUX_ADDRESS_SPACE pMapping,
	LINUX_OFFSET llOffset,
	unsigned dwLen, /* unsigned -> unsigned int */
	unsigned dwFlags,
	PLINUX_PAGE *ppPage,
	void **ppFsData)
{
	PLINUX_INODE	pInode = pMapping->host;

	*ppPage = NULL;

#ifdef TFS5_DEBUG
	{
		LINUX_ERROR		nDebug = 0;
	
		nDebug = LINUX_ContWriteBegin(pFile,
								pMapping,
								llOffset,
								dwLen,
								dwFlags,
								ppPage,
								ppFsData,
								fsGetBlock,
								&(VnodeGetVnodeFromInode(pInode)->llMappedSize));
	
		LNX_IMZ(("write begin (ino: %lu):"
					" size %lld, offset %lld, len %u, mappedS %lld, result %d",
					pMapping->host->i_ino,
					pMapping->host->i_size,
					llOffset, dwLen,
					VnodeGetVnodeFromInode(pInode)->llMappedSize,
					nDebug));

		return nDebug;
	}
#else
	{
		LINUX_ERROR		nRet;

		nRet = LINUX_ContWriteBegin(pFile,
				pMapping,
				llOffset,
				dwLen,
				dwFlags,
				ppPage,
				ppFsData,
				fsGetBlock,
				&(VnodeGetVnodeFromInode(pInode)->llMappedSize));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
		if (unlikely(nRet) && *ppPage == NULL) {
			LINUX_OFFSET llISize = pMapping->host->i_size;
			if (llOffset + dwLen > llISize)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
				LINUX_Vmtruncate(pMapping->host, llISize);
#else
				LINUX_TruncatePagecache(pInode, llOffset + dwLen, llISize);
				FileTruncate(pInode);
#endif
		}
#endif

		return nRet;
	}
#endif
}

/**
 * @brief	write a specified page
 *
 * @param[in]	pFile 		to read file
 * @param[in]	pMapping	address mapping of file to read
 * @param[in]	llOffset	start position in file (pos)
 * @param[in]	dwLen		bytes counts to prepare in page 
 * @param[in]	dwCopied	bytes of copied data for write
 * @param[in]	pPage		address of page to write
 * @param[in]	pFsData	
 * @return		return 0 on success, errno on failure
 */
static inline int
LxWriteEnd(
	PLINUX_FILE pFile,
	PLINUX_ADDRESS_SPACE pMapping,
	LINUX_OFFSET llOffset,
	unsigned dwLen,
	unsigned dwCopied,
	PLINUX_PAGE pPage,
	void *pFsData)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR		nDebug = 0;

		nDebug = LINUX_GenericWriteEnd(pFile,
								pMapping,
								llOffset,
								dwLen,
								dwCopied,
								pPage,
								pFsData);

		LNX_IMZ(("generic_write_end (ino: %lu):"
					" size %lld, offset %lld, len %u, result %d",
					pMapping->host->i_ino,
					pMapping->host->i_size, llOffset, dwLen, nDebug));

		return nDebug;
	}
#else

	return LINUX_GenericWriteEnd(pFile,
								pMapping,
								llOffset,
								dwLen,
								dwCopied,
								pPage,
								pFsData);
#endif
}

#else
/**
 * @brief		read some partial page to write rest page
 *
 * @param[in]	pFile		to read file
 * @param[in]	pPage		specified page to read
 * @param[in]	dwFrom		start position in page
 * @param[in]	dwTo			bytes counts to prepare in page 
 * @return		return 0 on success, errno on failure
 *
 * This function requires addtional code saving inode->i_size because there is
 * case when inode->i_size is chagned after LINUX_ContPrepareWrite.  
 */
static inline LINUX_ERROR 
LxPrepareWrite(
	IN	PLINUX_FILE		pFile, 
	IN	PLINUX_PAGE		pPage, 
	IN	unsigned int	dwFrom, 
	IN	unsigned int	dwTo)
{
	PLINUX_INODE	pInode = pPage->mapping->host;

	/* 
	 * RFS do not allow to allocate additional cluster during prepare_write.
	 * All required clusters are allocated before call prepare_write.
	 */
	
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR		nDebug = 0;

		nDebug = LINUX_ContPrepareWrite(pPage, dwFrom, dwTo, fsGetBlock,
				&(VnodeGetVnodeFromInode(pInode)->llMappedSize));

		LNX_VMZ(("prepare write (ino: %lu): index %lu from %u to %u, result %d",
					pInode->i_ino,
					pPage->index, dwFrom, dwTo, nDebug));

		return nDebug;
	}
#else
	return LINUX_ContPrepareWrite(pPage, dwFrom, dwTo, fsGetBlock,
			&(VnodeGetVnodeFromInode(pInode)->llMappedSize));
#endif
}

/**
 * @brief		write a specified page
 *
 * @param[in]	pFile		to write file
 * @param[in]	pPage		page descriptor
 * @param[in]	dwFrom		start position in page		
 * @param[in]	dwTo			end position in page	
 * @return		return 0 on success, errno on failure
 */
static inline LINUX_ERROR 
LxCommitWrite(
	IN	PLINUX_FILE		pFile, 
	IN	PLINUX_PAGE		pPage, 
	IN	unsigned int	dwFrom, 
	IN	unsigned int	dwTo)
{
#ifdef TFS5_DEBUG
	{
		LINUX_ERROR nDebug;

		nDebug = LINUX_GenericCommitWrite(pFile, pPage, dwFrom, dwTo);

		LNX_VMZ(("commmit write (ino: %lu):"
					" index %lu from %u to %u, file size %llu, result %d",
					pFile->mapping->host->i_ino,
					pPage->index, dwFrom, dwTo,
					pFile->f_dentry->d_inode->i_size,
					nDebug));

		return nDebug;
	}
#else
	return LINUX_GenericCommitWrite(pFile, pPage, dwFrom, dwTo);
#endif
}

#endif	/* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static inline LINUX_SSIZE_T
LxFileAioRead(
		IN	PLINUX_KIOCB		pKiocb, 
		IN	PLINUX_CIOVEC		pIovec,
		IN	unsigned long		nSegs,
		IN	LINUX_OFFSET		llPos)
{
/* #ifdef TFS5_DEBUG */
#if (RFS_DL_INFO <= RFS_DEBUG_LEVEL) && (RFS_DL_INFO_ENABLE >= 1)
	PLINUX_FILE		pFile = pKiocb->ki_filp;
	PLINUX_INODE	pInode = pFile->f_mapping->host;
	LINUX_SSIZE_T	szRetVal = 0;

	LNX_IMZ(("[in] FILE aio_read i_ino %lu size %Lu pos %Lu count %u %s",
				pInode->i_ino,
				LINUX_InodeReadSize(pInode),
				llPos, pKiocb->ki_left,
				(pFile->f_flags & LINUX_O_DIRECT)? "(O_DIRECT)": ""));

	szRetVal = LINUX_GenericFileAioRead(pKiocb, pIovec, nSegs, llPos);

	LNX_IMZ(("[out] FILE aio read end, %d pid: ret %lld, pos %Lu, count %u",
				LINUX_g_CurTask->pid,
				(long long) szRetVal, llPos, pKiocb->ki_left));

	return szRetVal;
#else
	return LINUX_GenericFileAioRead(pKiocb, pIovec, nSegs, llPos);
#endif
}
#endif /* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) */

#include <linux/dcache.h>

static int
LxDentryHashI(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	IN 	PLINUX_DENTRY_CONST	pDentry,
	IO	PLINUX_INODE_CONST	PInode,
#else
	IN	PLINUX_DENTRY	pDentry,
#endif
	IO	PLINUX_QSTR	pQstr)
{
	PLINUX_SUPER		pSb = pDentry->d_inode->i_sb;
	PLINUX_NLS_TABLE	pNlsIo = NULL;
	const unsigned char *pName;
	unsigned int dwLen;
	unsigned long dwHash;

	if (unlikely(NULL == RFS_SB_VCB(pSb)))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	pNlsIo = RFS_SB_VCB(pSb)->pNlsTableIo;
	if (unlikely(NULL == pNlsIo))
	{
		LNX_CMZ(("NLS Table isn't set"));
		RFS_ASSERT(pNlsIo);
		return -EFAULT;
	}

	pName = pQstr->name;
	dwLen = pQstr->len;
	dwHash = LINUX_InitNameHash();

	while (dwLen--)
	{
		dwHash = LINUX_PartialNameHash(LINUX_NlsToUpper(pNlsIo, *pName++), dwHash);
	}

	pQstr->hash = LINUX_EndNameHash(dwHash);

	return 0;
}

static int
LxDentryCmpI(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	IN      PLINUX_DENTRY_CONST	pParent,
	IO	PLINUX_INODE_CONST	pInode,
	IN	PLINUX_DENTRY_CONST	pDentry,
	IO	PLINUX_INODE_CONST	pInode1,
	IN	unsigned int		length,
	IN	const char		*str,
	IN	PLINUX_QSTR_CONST	name)
#else
	IN	PLINUX_DENTRY		pDentry,
	IN	PLINUX_QSTR		pStrA,
	IN	PLINUX_QSTR		pStrB)
#endif
{
	PLINUX_SUPER		pSb = pDentry->d_inode->i_sb;
	PLINUX_NLS_TABLE	pNlsIo = NULL;

	if (unlikely(NULL == RFS_SB_VCB(pSb)))
	{
		LNX_CMZ(("VCB isn't initialized"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	pNlsIo = RFS_SB_VCB(pSb)->pNlsTableIo;
	if (unlikely(NULL == pNlsIo))
	{
		LNX_CMZ(("NLS Table isn't set"));
		RFS_ASSERT(pNlsIo);
		return -EFAULT;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	if (length == name->len)
	{
		if (0 == LINUX_NlsStrniCmp(pNlsIo, str, name->name, length))
		{
			LNX_DMZ(("(dentry)%s and (input)%s is the same", str, name->name));
			return 0; //match
		}
	}
#else
	if (pStrA->len == pStrB->len)
	{
		if (0 == LINUX_NlsStrniCmp(pNlsIo, pStrA->name, pStrB->name, pStrA->len))
		{
			LNX_DMZ(("(dentry)%s and (input)%s is the same", pStrA->name, pStrB->name));
			return 0; //match
		}
	}
#endif

	return 1; //not match
}

static int
LxDentryRevalidateI(
	IN	PLINUX_DENTRY	pDentry,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	IN	PLINUX_NAMEIDATA	pNd)
#else
	IN	unsigned int		dwFlag)
#endif
{
	/*
	 * During creat, drop the dentry.
	 * d_lookup() sometimes finds the dentry of different name from specified name by user,
	 * because d_compare() finds names by case-insensitive.
	 * But we have to create a file with case sensitive name which is inputed by user.
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	if (!pNd)
		return 0;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	if (pNd->flags & LOOKUP_RCU)
#else
	if (dwFlag & LOOKUP_RCU)
#endif
		return -ECHILD;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0) 
	if ((pNd->flags & LOOKUP_CREATE)
			&& !(pNd->flags & (LOOKUP_CONTINUE | LOOKUP_PARENT)))
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	if (((pNd->flags & LOOKUP_CREATE))
			&& !(pNd->flags &  LOOKUP_PARENT))
#else
	if (((dwFlag & LOOKUP_CREATE))
			&& !(dwFlag &  LOOKUP_PARENT))
#endif
#endif        
	{
		LNX_DMZ(("revalidate"));
		return 0; // invalidate the dentry
	}

	return 1;
}

#ifdef RFS_CLUSTER_CHANGE_NOTIFY
FP_CLUSTER_USAGE_NOTIFY *gfpClusterUsageNotify = NULL;

void
rfs_register_cluster_usage_notify(FP_CLUSTER_USAGE_NOTIFY *pFunc)
{
	if (pFunc)
	{
		LNX_DMZ(("Registering callback of volume usage notify %p", pFunc));

		gfpClusterUsageNotify = pFunc;
	}
}
EXPORT_SYMBOL(rfs_register_cluster_usage_notify);
#endif

// end of file
