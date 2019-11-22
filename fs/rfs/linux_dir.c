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
 * @file        linux_dir.c
 * @brief       This file includes directory operations for Linux VFS.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_dir.h"

#include "linux_vnode.h"
#include "linux_fcb.h"
#include "linux_xattr.h"

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_DIR)


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/

/*
 * complete the initialization of new inode, after NativeFS's initialization
 */
static LINUX_ERROR
_DirCompleteCreateInode(
	IO	PVNODE			pParentVnode,
	IO	PVNODE			pVnode,
	IO	PLINUX_DENTRY	pDentry,
	IN	LINUX_MODE		dwMode)
{
	PLINUX_INODE	pInode = &pVnode->stLxInode;
	LINUX_ERROR		dwLinuxError = 0;		

	LNX_ASSERT_ARG(pInode, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);

	/*
	 * initialize selinux of new inode
	 */
	dwLinuxError = XattrInitSecurity(pInode, &(pParentVnode->stLxInode),
					 &pDentry->d_name);

	if (dwLinuxError)
	{
		LNX_SMZ(("Xattr Security initializing fails(%d)", dwLinuxError));
		goto out;
	}

	/* set group ID & sticky bit */
	
	/* FIXME 2009.01.19 is it right? -> yes! */
	pInode->i_mode |= (dwMode & (LINUX_S_ISGID | LINUX_S_ISVTX));

	/* update parent's mtime : 20090313 fix */
	pParentVnode->stLxInode.i_mtime =
		pParentVnode->stLxInode.i_atime = LINUX_CURRENT_TIME;
	LINUX_MarkInodeDirty(&pParentVnode->stLxInode);
	
	/* fill in inode information for a dentry; turns negative dentry into valid one */

	LINUX_DInstantiate(pDentry, pInode);

	/* for debug */
	pVnode->pstLxDentry = pDentry;

out:
	return dwLinuxError;
}

static inline PLINUX_DENTRY
_DirCompleteFindInode(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	PLINUX_INODE	pInode,
	IN	PLINUX_DENTRY	pDentry)
{
	PLINUX_DENTRY	pAlias = NULL;
	PLINUX_DENTRY	pRtnDentry = NULL;

	LNX_ASSERT_ARG(pVcb, NULL);
	LNX_ASSERT_ARG(pDentry, NULL);

	if (pInode == NULL)
		goto set_dentry;

	/*
	 * When filesystem compares file's names by "case-insensitive",
	 * multiple dentries could be mapped to one inode.
	 * In this case, this filesystem allows only latest dentry
	 * to be mapped to the inode to avoid confusion of names.
	 */
	pAlias = LINUX_DFindAlias(pInode);
	if (pAlias && VcbIsAliasDentry(pVcb))
	{
		/* if alias dentry is found, invalidate it */
		if (0 != LINUX_DInvalidate(pAlias))
		{
			/*
			 * When fails to invalidate alias dentry,
			 * it returns alias dentry instead of new dentry.
			 * It could be possible
			 * if inode is directory in use or if alias is unhashed dentry.
			 */
			LINUX_Iput(pInode);
			pRtnDentry = pAlias;

			goto out;
		}
		else
		{
			LNX_IMZ(("%s is invalidated, new one is %s", pAlias->d_name.name, pDentry->d_name.name));
			LINUX_Dput(pAlias);
		}
	}

set_dentry:
	/* splice a disconnected dentry into the tree if one exists */
	pDentry->d_op = pVcb->pSb->s_root->d_op;
	pRtnDentry = LINUX_DSpliceAlias(pInode, pDentry);
	if (pRtnDentry)
	{
		LNX_DMZ(("got return"));
		pRtnDentry->d_op = pVcb->pSb->s_root->d_op;
	}

out:
	return pRtnDentry;
}

/*
 * Set uid, gid, acl to create file or directory
 * @param pParent		parent directory
 * @param pdwUID		user ID
 * @param pdwGID		group ID
 * @param pdwMode		mode of inode
 * @param dwFileType	type of file
 */
static int
_DirGetLinuxIDs(
	IN	PVNODE			pParent, 
	OUT	unsigned int*	pdwUID, 
	OUT	unsigned int*	pdwGID, 
	OUT	unsigned int*	pdwMode, 
	IN	FILE_ATTR		dwFileType)
{
	LNX_ASSERT_ARG(pParent, -EINVAL);
	LNX_ASSERT_ARG(pdwUID, -EINVAL);
	LNX_ASSERT_ARG(pdwGID, -EINVAL);

	/* set effective user ID of process */
	*pdwUID = LINUX_g_CurFsUid;


	/* set group ID */
	if (pParent->stLxInode.i_mode & LINUX_S_ISGID)
	{
		/* inherit group ID from parent */
		*pdwGID = VnodeGetGid(pParent);
		if (dwFileType == FILE_ATTR_DIRECTORY)
		{
			LNX_ASSERT_ARG(pdwMode, -EINVAL);
			*pdwMode |= LINUX_S_ISGID;
		}
	}
	else
	{
		/* inherit group ID from effective group ID of process */
		*pdwGID = LINUX_g_CurFsGid;
	}

	/* in case of external mount */
	/* set uid, gid and permission of new file or directory to be compatible with linux vfat feature */
	if (IS_VCB_SETATTR(pParent->pVcb))
	{
		/* clear all bit field except file attribute */
		*pdwMode &= LINUX_S_IFMT;

		/*
		 * permission is set by Filesystem.
		 * mode entered from user is ignored like VFAT operation.
		 * and mode of file is decided by mask which is mount option.
		 * mode of inode is masked by fmask or dmask except symbolic link
		 * mode of symbolic link has always ACL_RWX.
		 */
		if (dwFileType == FILE_ATTR_LINKED)
			*pdwMode |= ACL_RWX;
		else if (dwFileType == FILE_ATTR_DIRECTORY)
			*pdwMode |= (ACL_RWX & ~pParent->pVcb->stExtOpt.wFsDmask);
		else
			*pdwMode |= (ACL_RWX & ~pParent->pVcb->stExtOpt.wFsFmask);

		*pdwUID = pParent->pVcb->stExtOpt.dwFsUid;

		/*
		 * SetGID is ignored for gid of parent directory like VFAT operation.
		 * and gid is set by 'gid' mount option.
		 */
		*pdwGID = pParent->pVcb->stExtOpt.dwFsGid;
	}

	return 0;
}

/* 2009.06.08: new function for creating entry */
/**
 * @brief		create a new entry and Vnode (file, directory, node)
 * @param[io]	pDir		parent directory
 * @param[io]	pDentry		dentry of new file or directory
 * @param[in]	dwMode		create mode of new file or directory
 * @param[in]	dwFileType	type for creating entry: file, directory or any special file
 * @param[in]	dwDev		if special file, device id (for device node)
 * @param[in]	pSymName	if symlink file, target name (for symlink path)
 * @return		zero on success, or errno
 */
static LINUX_ERROR
_DirCreateInternal(
	IN	PVNODE			pParent,
	IO	PLINUX_DENTRY	pDentry,
	IN	int				dwMode,
	IN	FILE_ATTR		dwFileType,
	IN	LINUX_DEV_T		dwDev,
	IN	const char*		pSymName)
{
	FERROR				nErr = FERROR_NO_ERROR;

	PVNODE				pVnode = NULL;
	wchar_t*			pwszName = NULL;
	wchar_t*			pwszTargetName = NULL;
	unsigned int		dwUid = 0, dwGid = 0;
	int					dwLen;
	int					dwTargetLen;
	LINUX_ERROR			dwLinuxError = 0;
	FILE_ATTR			dwFileAttr = dwFileType;

	if ((dwFileType != FILE_ATTR_DIRECTORY) && (dwFileType != FILE_ATTR_FILE) &&
			(dwFileType != FILE_ATTR_FIFO) && (dwFileType != FILE_ATTR_SOCKET) &&
			(dwFileType != FILE_ATTR_LINKED))
	{
		LNX_EMZ(("Wrong Attribute Type: 0x%x", dwFileType));
		return -EINVAL;
	}

	/* convert new name to unicode */
	dwLen = RtlGetWcsName(pParent->pVcb, pDentry->d_name.name,
			pDentry->d_name.len, &pwszName);
	if (dwLen < 0) 
	{
		LNX_EMZ(("Fail to convert name %s", pDentry->d_name.name));
		return dwLen;
	}

	/* set user ID, group ID, attribute, Acl */
	_DirGetLinuxIDs(pParent, &dwUid, &dwGid, &dwMode, dwFileType);

	/* file attribute */
	dwFileAttr |= ((dwMode & ACL_WRITES)? 0: FILE_ATTR_READONLY);

	/* create entry and vnode of new directory */
	if (dwFileType == FILE_ATTR_DIRECTORY)
	{
		nErr = pParent->pVnodeOps->pfnCreateDirectory(pParent, 
												pwszName, 
												dwLen, 
												dwFileAttr,
												dwUid, 
												dwGid, 
												(dwMode & ACL_ALL),
												&pVnode);
	}
	/* create entry and vnode of new symlink */
	else if (dwFileType == FILE_ATTR_LINKED)
	{
		/* convert path of symbolic link to unicode */
		dwTargetLen = RtlGetWcsName(pParent->pVcb, pSymName,
				strlen(pSymName), &pwszTargetName);
		if (dwTargetLen < 0) 
		{
			RtlPutWcsName(&pwszName);

			LNX_EMZ(("Fail to convert name %s", pDentry->d_name.name));
			return dwTargetLen;
		}

		nErr = pParent->pVnodeOps->pfnCreateSymlink(pParent,
												pwszName, 
												dwLen,
												dwFileAttr,
												dwUid, 
												dwGid, 
												(dwMode & ACL_ALL),
												pwszTargetName,
												&pVnode);
	}
	/* create entry and vnode of new file */
	else
	{
		nErr = pParent->pVnodeOps->pfnCreateFile(pParent, 
												pwszName, 
												dwLen, 
												dwFileAttr,
												dwUid, 
												dwGid, 
												(dwMode & ACL_ALL),
												&pVnode);
	}

	/* success to create a entry */
	if (nErr == FERROR_NO_ERROR)
	{
		if (pVnode)
		{
			/* initialize new inode after setup Native node */
			dwLinuxError = _DirCompleteCreateInode(pParent, pVnode, pDentry, dwMode);
			if (dwLinuxError)
			{
				LNX_EMZ(("Inode initializing fails (errno: %d)", dwLinuxError));
				/* make bad inode */
				VnodeMakeBad(pVnode);
			}
		}
		else
		{
			/* NativeFS returns success, but Vnode is NULL */
			LNX_EMZ(("NativeFS Create%s returns NULL Vnode",
						((dwFileType == FILE_ATTR_DIRECTORY)?
						 "Directory":
						 (dwFileType == FILE_ATTR_LINKED)?
						 "Symlink": "File")));
			dwLinuxError = -EFAULT;

			/* need ASSERT here? */
		}
	}
	/* fail to create a entry */
	else 
	{
		LNX_EMZ(("NativeFS Create%s fails (nErr: 0x%08x) : %s",
					((dwFileType == FILE_ATTR_DIRECTORY)?
						 "Directory":
						 (dwFileType == FILE_ATTR_LINKED)?
						 "Symlink": "File"),
					-nErr, pDentry->d_name.name));

		if (pVnode)
		{
			/* check that nativeFS returns pVnode if error occurs */
			LNX_DMZ(("make bad inode(%s)", pDentry->d_name.name));
			VnodeMakeBad(pVnode);
		}
	
		dwLinuxError = RtlLinuxError(nErr);
	}

	RtlPutWcsName(&pwszName);

	if (pwszTargetName != NULL)
	{
		RtlPutWcsName(&pwszTargetName);
	}

	return dwLinuxError;
}



/******************************************************************************/
/* Functions for Linux VFS                                                    */
/******************************************************************************/

/*
 * directory management apis (file_operations for dir)
 */

#define FIND_ALL_ENTRIES	(wchar_t *)L"*"

/**
 * @brief		read all directory entries (file_operations : readdir)
 * @param[io]	pFile		file pointer of specified directory to read
 * @param[out]	pDirent		buffer pointer
 * @param[in]	pfFillDir	function pointer which fills dir info
 * @return		return 0 on success, errno on failure
 */
LINUX_ERROR
DirReadDirectory(
	IO	PLINUX_FILE		pFile, 
	OUT	void*			pBufDirent, 
	IN	LINUX_FILLDIR	pfFillDir)
{
	LINUX_ERROR			dwLinuxError = 0;
	FERROR				nErr = FERROR_NO_ERROR;
	PVNODE				pVnode = NULL;
	PFILE_CONTROL_BLOCK pFcb = (PFILE_CONTROL_BLOCK) pFile;
	FILE_OFFSET			llPos = FcbGetOffset(pFcb);
	PDIRECTORY_ENTRY	pstDirEntry = NULL;

	char*				pName = NULL;
	int					dwLen = 0;
	unsigned int		dwType = 0;
	unsigned long		dwID = 0;
	unsigned int		dwReadCnt = 0;

	/* get vnode of 'struct file' */
	pVnode = VnodeGetVnodeFromFile(pFile);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* check for nativeFS's operation */
	if ((NULL == pVnode->pFileOps) ||
			(NULL == pVnode->pFileOps->pfnReadDirectory))
	{
		LNX_CMZ(("No Native interface for ReadDirectory"));
		return -ENOSYS;
	}

	LNX_IMZ(("[in] DIR read dir %d pid", LINUX_g_CurTask->pid));

	/* allocate free page for unicode name */
	pName = (char *) LINUX_GetFreePage(LINUX_GFP_NOFS);
	if (!pName)
	{
		LNX_CMZ(("Fail to allocate free page(get_free_page)"));
		return -ENOMEM;
	}

	/* convert unicode string of "*" for scanning all entries */

	pstDirEntry = (PDIRECTORY_ENTRY)
		LINUX_Kmalloc(sizeof(DIRECTORY_ENTRY), LINUX_GFP_NOFS);
	LINUX_Memset(pstDirEntry, 0, sizeof(DIRECTORY_ENTRY));

	/* run loop while offset is smaller than size */
	while (1)
	{
		nErr = pVnode->pFileOps->pfnReadDirectory(pFcb,
												FIND_ALL_ENTRIES,
												pstDirEntry,
												&dwReadCnt);

		/* end-of-directory */
		if (nErr == FERROR_NO_MORE_ENTRIES)
		{
			nErr = FERROR_NO_ERROR;
			goto out;
		}
		/* unexpected error */
		else if (nErr != FERROR_NO_ERROR)
		{
			LNX_VMZ(("NativeFS ReadDirectory fails (nErr: %08x): pos %lld",
					-nErr, llPos));
			goto out;
		}

		/* set file type */
		if (pstDirEntry->dwFileAttribute & FILE_ATTR_DIRECTORY)
		{
			dwType = LINUX_DT_DIR;
		}
		else if (pstDirEntry->dwFileAttribute & FILE_ATTR_LINKED)
		{
			dwType = LINUX_DT_LNK;
		}
		else if (pstDirEntry->dwFileAttribute & FILE_ATTR_SOCKET)
		{
			dwType = LINUX_DT_SOCK;
		}
		else if (pstDirEntry->dwFileAttribute & FILE_ATTR_FIFO)
		{
			dwType = LINUX_DT_FIFO;
		}
		else
		{
			/*
			 * TODO - If NativeFS supports any other type,
			 *  you need to add type handling code here
			 */
			dwType = LINUX_DT_REG;
		}

		/* convert unicode string to multibyte string */
		dwLen = RtlConvertToMbs(VnodeGetVcb(pVnode),
				pstDirEntry->wszName, pName, LINUX_PAGE_SIZE);
		if (dwLen <= 0)
		{
			LNX_EMZ(("Unicode name of the entry is unavailable. "
						"It cannot be converted to the multibyte string."));

		//	RFS_ASSERT(0);
		}
		else
		{
		/* llVnodeID is internal ID number. readdir should get inode number. */
#ifdef	_4BYTE_VNODEID
			dwID = (unsigned long) pstDirEntry->llVnodeID;
#else
			{
				PVNODE	pTmpVnode = NULL;

				pTmpVnode = VcbFindVnode(VnodeGetVcb(pVnode),
						pstDirEntry->llVnodeID);
				if (pTmpVnode)
				{
					dwID = pTmpVnode->stLxInode.i_ino;

					VnodeRelease(pTmpVnode);
				}
				else
				{
					dwID = (unsigned long)
						LINUX_Iunique(VnodeGetVcb(pVnode)->pSb, RFS_ROOT_INO);
				}
			}
#endif

			LNX_VMZ(("read entry is %s(%d) - %lu(0x%llx), 0%o at %llu",
						pName, dwLen, dwID, pstDirEntry->llVnodeID,
						dwType, llPos));

			/* call filldir() of Linux VFS */
			if ((dwLinuxError = pfFillDir(pBufDirent, pName, dwLen,
							(LINUX_OFFSET) llPos, dwID, dwType)) < 0)
			{	
				LNX_SMZ(("Fail to filldir entry (errno: %d): "
							"name(%s) len(%d) pos(%llu) ino(%lu) type(%x)",
							dwLinuxError,
							pName, dwLen, llPos, dwID, dwType));

				/*
				 * If error happens while running pfFillDir,
				 * pBufDirent already has errno.
				 * So, this doesn't need to report error number here.
				 */
				break;	/* break while-loop */
			}
		}

		/* update file position */
		llPos += dwReadCnt;
		FcbSetOffset(pFcb, llPos);
	}

out:
	LNX_IMZ(("[out] DIR read dir end %d pid", LINUX_g_CurTask->pid));

	/* release memory */
	LINUX_FreePage((unsigned long) pName);
	LINUX_Kfree(pstDirEntry);

	return RtlLinuxError(nErr);
}


/******************************************************************************/
/* directory management apis (inode_operations for dir)                       */
/******************************************************************************/
/**
 * @brief		create a new file (inode_operations: create)
 * @param[io]	pDir		parent directory
 * @param[io]	pDentry		dentry of new file
 * @param[in]	dwMode		create mode of new file
 * @param[in]	pstNd		nameidata of new file
 * @return		zero on success, or errno
 */
LINUX_ERROR
DirCreateFile(
	IO	PLINUX_INODE		pDir,
	IO	PLINUX_DENTRY		pDentry,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	IN	int			dwMode,
#else
	IN	LINUX_UMODE		dwMode,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	IN	PLINUX_NAMEIDATA	pstNd)
#else
	IN	bool			bExcl)
#endif
{

	PVNODE					pParent = NULL;
	LINUX_ERROR 			dwLinuxError = 0;

	LNX_ASSERT_ARG(pDir, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	LNX_ASSERT_ARG(pstNd, -EINVAL);
#endif

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, -EINVAL);

	/* check for native operation */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnCreateFile))
	{
		LNX_CMZ(("No Native interface for CreateFile"));
		return (-ENOSYS);
	}

	LNX_IMZ(("[in] DIR create file \"%s\" in pDir %llx, pid %d",
			pDentry->d_name.name, pParent->llVnodeID, LINUX_g_CurTask->pid));

	dwLinuxError = _DirCreateInternal(pParent, pDentry, dwMode, FILE_ATTR_FILE, 0, NULL);

	LNX_IMZ(("[out] DIR create file end %d pid (%d)", LINUX_g_CurTask->pid, dwLinuxError));

	return dwLinuxError;
}

/**
 * @brief		lookup a inode associated with dentry (inode_operations: lookup)
 * @param[io]	pDir		inode of parent directory
 * @param[io]	pDentry		dentry for itself
 * @param[in]	pstNd		namei data structure
 * @return      return dentry object on success, errno on failure
 *
 * if inode doesn't exist, allocate new inode, fill it, and associated with dentry
 */
PLINUX_DENTRY 
DirLookup(
	IO	PLINUX_INODE		pDir, 
	IO	PLINUX_DENTRY		pDentry,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0) 
	IN	PLINUX_NAMEIDATA	pstNd)
#else
	IN	unsigned int		dwFlag)
#endif
{
	FERROR					nErr = FERROR_NO_ERROR;

	PVNODE					pParent = NULL;
	PVNODE					pVnode = NULL;
	PLINUX_DENTRY			pRtnDentry = NULL;
	wchar_t*				pwszName = NULL;
	unsigned int			dwLookupFlag = 0;
	int						dwLen;

	LNX_ASSERT_ARG(pDir, ERR_PTR(-EINVAL));
	LNX_ASSERT_ARG(pDentry, ERR_PTR(-EINVAL));
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	LNX_ASSERT_ARG(pstNd, ERR_PTR(-EINVAL));
#endif

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, ERR_PTR(-EINVAL));

	LNX_IMZ((
			"[in] DIR Lookup \"%s\" in pDir %llx, %d pid",
			pDentry->d_name.name, pParent->llVnodeID, LINUX_g_CurTask->pid));

	/* check that native have lookup function */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnLookupChild))
	{
		LNX_CMZ(("No Native interface for Lookup"));
		return ERR_PTR(-ENOSYS);
	}

	/* convert cstring to unicode string */
	dwLen = RtlGetWcsName(pParent->pVcb, pDentry->d_name.name,
			pDentry->d_name.len, &pwszName);
	if (dwLen < 0) 
	{
		LNX_EMZ(("Fail to convert name %s", pDentry->d_name.name));
		return ERR_PTR(dwLen);
	}

	/*
	 * check for length limitation
	 * : Linux VFS doesn't check this name's length.
	 * : __link_path_walk()->do_lookup()->real_lookup()
	 */
	if (dwLen > MAX_FILE_NAME_LENGTH)
	{
		LNX_VMZ(("Name is too long in lookup (%d:%s)",
					pDentry->d_name.len, pDentry->d_name.name));
		
		RtlPutWcsName(&pwszName);

		return ERR_PTR(-ENAMETOOLONG);
	}


	/* call Native's Lookup function */
	nErr = pParent->pVnodeOps->pfnLookupChild(pParent, 
											pwszName, 
											dwLen, 
											dwLookupFlag, 
											&pVnode);

	/* find the file or directory and create Vnode(inode) */
	if (nErr == FERROR_NO_ERROR)
	{
		/* found the entry */
		if (pVnode)
		{
			LNX_VMZ(("Path %s Found - Vnode %llx: "
						"uid(0x%x), gid(0x%x), mode(0%o)",
						pDentry->d_name.name,
						(unsigned long long) pVnode->llVnodeID,
						pVnode->stLxInode.i_uid, pVnode->stLxInode.i_gid,
						pVnode->stLxInode.i_mode));

			/* for debug */
			pVnode->pstLxDentry = pDentry;

			/* NOTICE:
			 *  set file's size to i_blocks
			 *  : nativefs should set i_blocks
			 */

			// 20090807 bugfix of alias dentry
			/* splice a disconnected dentry into the tree if one exists */
			pRtnDentry = _DirCompleteFindInode(pParent->pVcb, &(pVnode->stLxInode), pDentry);

			/* in case of external mount */
			if (IS_VCB_EXTERNAL(pParent->pVcb))
			{
				/* 
				 * mode of inode is masked except symbolic link
				 * mode symbolic link has always ACL_RWX
				 */
				LINUX_MODE dwMode = ACL_RWX;
				BOOL bIsWritable = (pVnode->stLxInode.i_mode & ACL_WRITES) ? TRUE : FALSE;

				/*
				 * '-w-' field mode of inode is shared in owner, group, other 
				 * which is read from ATTR_READ_ONLY attribute of Directory Entry
				 */
				if (LINUX_IS_DIR(pVnode->stLxInode.i_mode))
					dwMode &= ~((bIsWritable ? 0 : ACL_WRITES) | pVnode->pVcb->stExtOpt.wFsDmask);
				else if (!LINUX_IS_LNK(pVnode->stLxInode.i_mode))
					dwMode &= ~((bIsWritable ? 0 : ACL_WRITES) | pVnode->pVcb->stExtOpt.wFsFmask);

				pVnode->stLxInode.i_mode = (pVnode->stLxInode.i_mode & ~ACL_RWX) | dwMode;
				pVnode->stLxInode.i_uid = pVnode->pVcb->stExtOpt.dwFsUid;
				pVnode->stLxInode.i_gid = pVnode->pVcb->stExtOpt.dwFsGid;
			}
		}
		/* NativeFS returns success, but Vnode is NULL */
		else
		{
			LNX_EMZ(("NativeFS LookupChild returns NULL Vnode"));
			pRtnDentry = ERR_PTR(-EFAULT);

			RFS_ASSERT(0);
		}
	}
	/* fail to find the file or directory (not exist or unexpected error) */
	else
	{
		/* NativeFS returns failure, but Vnode is not NULL. Just release it. */
		if (pVnode)
		{
			LNX_VMZ(("Release Vnode"));
			VnodeRelease(pVnode);
		}

		/* set dentry's inode to NULL */
		pRtnDentry = _DirCompleteFindInode(pParent->pVcb, NULL, pDentry);

		/* not found - expected error */
		if (nErr == FERROR_PATH_NOT_FOUND)
		{
			LNX_VMZ(("Path %s not found(nErr: 0x%08x)",
						pDentry->d_name.name, -nErr));
		}
		/* fail to lookup directory because of unexpected error*/
		else
		{
			LNX_EMZ(("NativeFS LookupChild fails (nErr: 0x%08x) : %s",
						-nErr, pDentry->d_name.name));

			/* return error number */
			pRtnDentry = ERR_PTR(RtlLinuxError(nErr));
		}
	}

	/* release memory allocated for name conversion */
	RtlPutWcsName(&pwszName);

	LNX_IMZ(("[out] DIR lookup end, %d pid (%d)", LINUX_g_CurTask->pid,
				(int)(IS_ERR(pRtnDentry)? PTR_ERR(pRtnDentry): 0)));

	return pRtnDentry;
}


#ifdef CONFIG_RFS_FS_SPECIAL
/**
 * @brief		make a symbolic link (inode_operations: symlink)
 * @param[io]	pDir		parent directory inode
 * @param[io]	pDentry		dentry corresponding with new symlink file
 * @param[in]	pSymName	full link of target
 * @return      return 0 on success, errno on failure
 */
LINUX_ERROR
DirSymbolicLink(
	IO	PLINUX_INODE	pDir, 
	IO	PLINUX_DENTRY	pDentry, 
	IN	const char*		pSymName)
{	
	PVNODE				pParent = NULL;
	LINUX_ERROR			dwLinuxError = 0;

	LNX_ASSERT_ARG(pDir, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, -EINVAL);

	/* check for native operation */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnCreateSymlink))
	{
		LNX_CMZ(("No Native interface for CreateSymlink"));
		return (-ENOSYS);
	}

	LNX_IMZ(("[in] DIR symlink \"%s\" -> \"%s\" in pDir %llx, %d pid",
			pDentry->d_name.name, pSymName, pParent->llVnodeID,
			LINUX_g_CurTask->pid));

	dwLinuxError = _DirCreateInternal(pParent, pDentry, ACL_RWX, FILE_ATTR_LINKED, 0, pSymName);

	LNX_IMZ(("[out] DIR symlink end %d pid (%d)", LINUX_g_CurTask->pid, dwLinuxError));
	
	return dwLinuxError;
}
#endif /* #ifdef CONFIG_RFS_FS_SPECIAL */

/**
 * @brief		create a directory (inode_operations: mkdir)
 * @param[io]	pDir		parent directory
 * @param[io]	pDentry		dentry of new directory
 * @param[in]	dwMode		create mode of new directory
 * @return		zero on success, or errno
 */
LINUX_ERROR
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
DirCreateDirectory(
	IO	PLINUX_INODE	pDir, 
	IO	PLINUX_DENTRY	pDentry, 
	IN	int				dwMode)
#else
DirCreateDirectory(
	IO	PLINUX_INODE	pDir,
	IO	PLINUX_DENTRY	pDentry,
	IN	LINUX_UMODE				dwMode)
#endif
{
	PVNODE				pParent = NULL;
	LINUX_ERROR			dwLinuxError = 0;

	LNX_ASSERT_ARG(pDir, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, -EINVAL);

	/* check for native operation */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnCreateDirectory))
	{
		LNX_CMZ(("No Native interface for CreateDirectory"));
		return (-ENOSYS);
	}

	LNX_IMZ(("[in] DIR create directory \"%s\" in pDir %llx, %d pid",
			pDentry->d_name.name, pParent->llVnodeID, LINUX_g_CurTask->pid));

	dwLinuxError = _DirCreateInternal(pParent, pDentry, dwMode, FILE_ATTR_DIRECTORY, 0, NULL);

	LNX_IMZ(("[out] DIR create directory end, %d pid (%d)", LINUX_g_CurTask->pid, dwLinuxError));
	
	return dwLinuxError;	/* zero on success */
}


/**
 * @brief		remove a directory or a file 
 * @param[io]	pDir		parent directory inode
 * @param[in]	pDentry	dentry corresponding with a directory will be removed
 * @return		0 on success, errno on failure
 */
LINUX_ERROR
DirDelete(
	IO	PLINUX_INODE	pDir, 
	IN	PLINUX_DENTRY	pDentry)
{
	FERROR				nErr = FERROR_NO_ERROR;

	PVNODE				pParent = NULL;
	PVNODE				pVnode = NULL;
	wchar_t*			pwszName = NULL;
	int					dwLen = 0;
	
	LNX_ASSERT_ARG(pDir, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);
	LNX_ASSERT_ARG(pDentry->d_inode, -EINVAL);

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, -EINVAL);

	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	LNX_IMZ(("[in] DIR delete \"%s\" %llx, in pDir %llx, %d pid",
			pDentry->d_name.name, pVnode->llVnodeID, pParent->llVnodeID,
			LINUX_g_CurTask->pid));

	/* check for native operation */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnUnlink))
	{
		LNX_CMZ(("No Native interface"));
		return (-ENOSYS);
	}

	/* check for native permission for delete */
	nErr = FileCheckNativePermission(pVnode, OP_DELETE);
	if (nErr)
	{
		LNX_EMZ(("Deleting %s is not permitted by NativeFS",
				pDentry->d_name.name));
		return RtlLinuxError(nErr);
	}

	/* convert file for deletion to unicode */
	dwLen = RtlGetWcsName(pParent->pVcb, pDentry->d_name.name,
			pDentry->d_name.len, &pwszName);
	if (dwLen < 0) 
	{
		LNX_EMZ(("Fail to convert name %s", pDentry->d_name.name));
		return dwLen;
	}

	/*
	 * last parameter is 'open unlink' flag
	 * Linux filesystem can always unlink open files
	 */
	nErr = pParent->pVnodeOps->pfnUnlink(pParent, pVnode, TRUE);

	if (nErr == FERROR_NO_ERROR)
	{
		/* update parent's mtime; 20090316 fixed */
		pDir->i_mtime = pDir->i_atime = LINUX_CURRENT_TIME;
		LINUX_MarkInodeDirty(pDir);

		// verbose
		LNX_VMZ(("NativeFS Unlink : (%s) %s (nlink %d count %d d_count %d)",
					(LINUX_IS_DIR(pVnode->stLxInode.i_mode)? "Dir":"File"),
					pDentry->d_name.name,
					pVnode->stLxInode.i_nlink,
					LINUX_AtomicRead(&pVnode->stLxInode.i_count),
					LINUX_AtomicRead(&pDentry->d_count))
			   );
	}
	else if (nErr == FERROR_NOT_EMPTY)
	{
		/* not empty directory */
		LNX_VMZ(("NativeFS Unlink fails (nErr: 0x%08x) : %s",
					-nErr, pDentry->d_name.name));
	}
	else
	{
		LNX_EMZ(("NativeFS Unlink fails (nErr: 0x%08x) : %s",
					-nErr, pDentry->d_name.name));
	}

	RtlPutWcsName(&pwszName);
	pVnode->pstLxDentry = NULL;

	LNX_IMZ(("[out] DIR delete end %d pid (%d)", LINUX_g_CurTask->pid, RtlLinuxError(nErr)));

	return RtlLinuxError(nErr);
}

 
/**
 * @brief		change the name/location of a file or directory (inode_operations: rename)
 * @param[io]	pOldDir		old parent directory
 * @param[io]	pOldDentry	old dentry
 * @param[io]	pNewDir		new parent directory
 * @param[io]	pNewDentry	new dentry
 * @return		0 on success, errno on failure
 */
LINUX_ERROR
DirRename(
	IO	PLINUX_INODE	pOldDir, 
	IO	PLINUX_DENTRY	pOldDentry, 
	IO	PLINUX_INODE	pNewDir, 
	IO	PLINUX_DENTRY	pNewDentry)
{
	FERROR				nErr = FERROR_NO_ERROR;

	PVNODE				pOldParent = NULL;
	PVNODE				pNewParent = NULL;
	PVNODE				pOldVnode = NULL;
	PVNODE				pNewVnode = NULL;
	wchar_t*			pwszName = NULL;
	int					dwLen = 0;
	BOOL                bIsNewVnodeOpened = FALSE;

	LNX_ASSERT_ARG(pOldDir && pOldDentry, -EINVAL);
	LNX_ASSERT_ARG(pNewDir && pNewDentry, -EINVAL);

	/* get parent's vnode of 'struct inode' */
	pOldParent = VnodeGetVnodeFromInode(pOldDir);
	LNX_ASSERT_ARG(pOldParent, -EFAULT);

	pNewParent = VnodeGetVnodeFromInode(pNewDir);
	LNX_ASSERT_ARG(pNewParent, -EFAULT);

	/* check for native operation */
	if ((NULL == pOldParent->pVnodeOps) ||
			(NULL == pOldParent->pVnodeOps))
	{
		LNX_CMZ(("No Native interface for rename"));
		return -ENOSYS;
	}

	if (((NULL == pOldParent->pVnodeOps->pfnMove) ||
				(NULL == pNewParent->pVnodeOps->pfnUnlink))
			&& (NULL == pOldParent->pVnodeOps->pfnMove2))
	{
		LNX_CMZ(("No Native interface for rename"));
		return -ENOSYS;
	}

	/* get target's vnode of 'struct inode' */
	LNX_ASSERT_ARG(pOldDentry->d_inode, -EINVAL);
	pOldVnode = VnodeGetVnodeFromInode(pOldDentry->d_inode);
	LNX_ASSERT_ARG(pOldVnode, -EFAULT);

	pNewVnode = (pNewDentry->d_inode) ?
		VnodeGetVnodeFromInode(pNewDentry->d_inode): NULL;

	LNX_IMZ(("[in] DIR rename \"%s\" -> \"%s\", %d pid ",
				pOldDentry->d_name.name, pNewDentry->d_name.name,
				LINUX_g_CurTask->pid));

	/* check native permission */
	nErr = FileCheckNativePermission(pOldVnode, OP_METAUPDATE);
	if (nErr)
	{
		LNX_EMZ(("Updating the parent directory is not permitted"));
		return RtlLinuxError(nErr);
	}

	/* if vnode of new name exists, it will be deleted */
	if (pNewVnode)
	{
		nErr = FileCheckNativePermission(pNewVnode, OP_DELETE);
		if (nErr)
		{
			LNX_EMZ(("Deleting %s is not permitted",
					pNewDentry->d_name.name));
			return RtlLinuxError(nErr);
		}

		/* opened unlink */
		bIsNewVnodeOpened = TRUE;
	}

	/* convert new name to unicode */
	dwLen = RtlGetWcsName(pNewParent->pVcb, pNewDentry->d_name.name,
			pNewDentry->d_name.len, &pwszName);
	if (dwLen < 0) 
	{
		LNX_EMZ(("Fail to convert name %s", pNewDentry->d_name.name));
		return dwLen;
	}


	/*
	 * pfnMove2 removes old entry if target name exists
	 */
	if (pOldParent->pVnodeOps->pfnMove2)
	{
		/* if vnode of new name exist, it will be deleted by nativefs*/

		nErr = pOldParent->pVnodeOps->pfnMove2(pOldParent,
											pOldVnode,
											pNewParent,
											pNewVnode,
											pwszName,
											dwLen,
											TRUE, /* source is open */
											bIsNewVnodeOpened);
	}
	/*
	 * pfnMove doesn't remove old entry even if target name exists
	 * So, glue should remove old entry before calling pfnMove
	 */
	else
	{
		/* if vnode of new name exist, it should be deleted by glue */
		if (pNewVnode)
		{
			nErr = pNewParent->pVnodeOps->pfnUnlink(pNewParent,
													pNewVnode,
													TRUE); /* open unlink */

			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("NativeFS Unlink fails (nErr: 0x%08x): %s",
						-nErr, pNewDentry->d_name.name));

				goto out;
			}
		}

		nErr = pOldParent->pVnodeOps->pfnMove(pOldParent,
											pOldVnode,
											pNewParent,
											pwszName,
											dwLen,
											TRUE); /* source is open */
	}

	/* update parent's mtime. 20090316 fixed */
	if (nErr == FERROR_NO_ERROR)
	{
		/* update old parent for delete entry */
		pOldDir->i_mtime = LINUX_CURRENT_TIME;
		LINUX_MarkInodeDirty(pOldDir);

		/* update new parent for create entry */
		if (pOldDir != pNewDir)
		{
			pNewDir->i_mtime = LINUX_CURRENT_TIME;
			LINUX_MarkInodeDirty(pNewDir);
		}
	}
	else
	{
		LNX_EMZ(("NativeFS Move/Move2 fails (nErr: 0x%08x)"
				": %s -> %s",
				-nErr,
				pOldDentry->d_name.name, pNewDentry->d_name.name));
	}

out:
	LNX_DMZ(("[out] DIR rename end, %d pid", LINUX_g_CurTask->pid));

	RtlPutWcsName(&pwszName);

	return RtlLinuxError(nErr);
}

#ifdef CONFIG_RFS_FS_SPECIAL
/**
 * @brief		create a new node (inode_operations: mknod)
 * @param[io]	pDir		parent directory
 * @param[io]	pDentry		dentry of new file
 * @param[in]	dwMode		create mode of new file
 * @param[in]	dwDev		device number
 * @return		zero on success, or errno
 */
LINUX_ERROR
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
DirCreateNode(
	IO	PLINUX_INODE		pDir, 
	IO	PLINUX_DENTRY		pDentry, 
	IN	int					dwMode, 
	IN	LINUX_DEV_T			dwDev)
#else
DirCreateNode(
	IO	PLINUX_INODE		pDir,
	IO	PLINUX_DENTRY		pDentry,
	IN	LINUX_UMODE					dwMode,
	IN	LINUX_DEV_T			dwDev)
#endif
{
	PVNODE					pParent = NULL;
	LINUX_ERROR 			dwLinuxError = 0;
	FILE_ATTR				dwFileAttr;

	LNX_ASSERT_ARG(pDir, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);

	if (LINUX_IS_SOCK(dwMode))
	{
		dwFileAttr = FILE_ATTR_SOCKET;
	}
	else if (LINUX_IS_FIFO(dwMode))
	{
		dwFileAttr = FILE_ATTR_FIFO;
	}
	else
	{
		return -EINVAL;
	}

	if (!new_valid_dev(dwDev))
	{
		return -EINVAL;
	}

	/* get vnode of 'struct inode' */
	pParent = VnodeGetVnodeFromInode(pDir);
	LNX_ASSERT_ARG(pParent, -EINVAL);

	/* check for native operation */
	if ((NULL == pParent->pVnodeOps) || 
			(NULL == pParent->pVnodeOps->pfnCreateFile))
	{
		LNX_CMZ(("No Native interface for CreateFile"));
		return (-ENOSYS);
	}

	LNX_IMZ(("[in] DIR create node \"%s\" in pDir %llx, pid %d",
			pDentry->d_name.name, pParent->llVnodeID, LINUX_g_CurTask->pid));

	dwLinuxError = _DirCreateInternal(pParent, pDentry, dwMode, dwFileAttr, dwDev, NULL);

	LNX_IMZ(("[out] DIR create node end %d pid (%d)", LINUX_g_CurTask->pid, dwLinuxError));

	return dwLinuxError;
}
#endif /* #ifdef CONFIG_RFS_FS_SPECIAL */

// end of file
