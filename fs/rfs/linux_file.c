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
 * @file        linux_file.c
 * @brief       This file includes file operations for Linux VFS.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_file.h"
#include "linux_fcb.h" /* for FcbGetVnode, linux_vnode.h, linux_vcb.h */

#include <linux/aio.h>
#include <linux/writeback.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_FILE)


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/

/**
 * @brief		check additional permission of inode
 * @param[in]	pVnode     pVnode
 * @param[in]	dwPerm     permission
 * @return		
 */
FERROR
FileCheckNativePermission(
	IN	PVNODE				pVnode, 
	IN	OPERATION_MODE		dwPerm)
{
	FERROR	nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pVnodeOps, -ENOSYS);

	if (pVnode->pVnodeOps->pfnPermission)
	{
		nErr = pVnode->pVnodeOps->pfnPermission(pVnode, dwPerm);
		if (nErr)
		{
			LNX_EMZ(("NativeFS Permission fails (nErr : 0x%08x)"
					": additional permission is not allowed", -nErr));
		}
	}
	return nErr;
}


/**
 * @brief		Write and wait upon the last page for inode
 * @param[in]	inode		inode pointer
 * @param[in]	llOffset	to write
 * @return		zero on success, negative value on failure
 * @remarks
 *
 * This is a data integrity operation for a combination of
 * zerofill and direct IO write
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
#define start	range_start
#define end		range_end
#endif
static LINUX_ERROR 
LxSyncLastPage(
	IN	PLINUX_INODE	pInode, 
	IN	LINUX_OFFSET	llOffset)
{
	LINUX_OFFSET		llStartOffset = (llOffset - 1) & LINUX_PAGE_CACHE_MASK;
	PLINUX_ADDRESS_SPACE	pMapping = pInode->i_mapping;

	LINUX_WB_CTL		stWbc =
	{
		.sync_mode  	= LINUX_WB_SYNC_ALL,
		.start   		= llStartOffset,
		.end  			= llStartOffset + LINUX_PAGE_SIZE - 1,
	};

	/*
	 * Note: There's race condition. We don't use page cache operation
	 * directly.
	 */
	return pMapping->a_ops->writepages(pMapping, &stWbc);
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
#undef start
#undef end
#endif


/**
 * @brief		Allocate blocks for write
 * @param[io]	pVnode		Vnode pointer
 * @param[io]	pFile		File descriptor
 * @param[io]	nCount		number of bytes to write
 * @param[io]	pPos		offset in file
 * @return      zero on success, errno on failure
 */
LINUX_SSIZE_T
_FileWriteAllocateBlocks__lock(
	IO	PVNODE				pVnode,
	IN	PLINUX_FILE			pFile,
	IN	LINUX_SIZE_T		nCount, 
	IO	LINUX_OFFSET		llPos)
{
	OPERATION_MODE			dwPerm = 0;
	LINUX_OFFSET			llNewPos = 0;
	LINUX_SSIZE_T			nLinuxError = 0;
	FERROR					nErr = FERROR_NO_ERROR;

	/* 
	 * 1. calculate the resulted size after writing
	 * 2. if the size is larger than current vnode's size,
	 * 3. expand clusters to the size - just allocate clusters (pfnExpandCluster)
	 * 3-1. glue should get inode lock before calling pfnExpandCluster
	 * 3-2. glue should release inode lock after pfnExpandCluster
	 */


	/*
	 * calculate the expected size after writing data
	 */
	if (pFile->f_flags & LINUX_O_APPEND)
	{
		/*
		 * If set O_APPEND flag,
		 * Linux VFS will move current position to the end of file
		 * before write operation (VFS's generic write)
		 */
		llNewPos = LINUX_InodeReadSize(&pVnode->stLxInode) + (LINUX_OFFSET) nCount;
		dwPerm |= (OP_APPEND | OP_RESIZE);
	}
	else
	{
		llNewPos = llPos + (LINUX_OFFSET) nCount;

		if (llNewPos > LINUX_InodeReadSize(&pVnode->stLxInode))
		{
			dwPerm |= OP_RESIZE;
		}
	}

	/*
	 * check for native permission
	 */
	nErr = FileCheckNativePermission(pVnode, dwPerm);
	if (nErr)
	{
		LNX_EMZ(("Not permitted by NativeFS(0x%x)", dwPerm));

		nLinuxError = (LINUX_SSIZE_T) RtlLinuxError(nErr);
		goto out;
	}

	/*
	 * enlarge the size of file (not change i_size)
	 */
	if (dwPerm & OP_RESIZE)
	{
		LNX_DMZ(("expand clusters (to %llu)", (unsigned long long)llNewPos));

		nErr = pVnode->pVnodeOps->pfnExpandClusters(pVnode, llNewPos);
		if (nErr != FERROR_NO_ERROR)
		{
			if (nErr != FERROR_NO_FREE_SPACE)
			{
				LNX_EMZ(("[out] NativeFS ExpandClusters fails "
							"(nErr: 0x%08x, errno: %d): pos %llu",
							-nErr, RtlLinuxError(nErr),
							(unsigned long long)llNewPos));
			}

			nLinuxError = (LINUX_SSIZE_T) RtlLinuxError(nErr);
			goto out;
		}
	}

out:

	return nLinuxError;
}

/******************************************************************************/
/* Functions for Linux VFS                                                    */
/******************************************************************************/

/*
 * file management apis (file/inode_operations for file)
 */

/**
 * @brief		open file (file_operations: open)
 * @param[in]	pInode		inode structure
 * @param[in]	pFile		file structure
 * @return		zero on success, negative value on failure 
 *
 * vfs calls f_ops->open without any locks
 */
LINUX_ERROR
FileOpen(
	IN	PLINUX_INODE		pInode, 
	IN	PLINUX_FILE			pFile)
{
	PFILE_CONTROL_BLOCK		pFcb = (PFILE_CONTROL_BLOCK) pFile;
	PVNODE					pVnode;
	FERROR					nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pInode, -EINVAL);
	LNX_ASSERT_ARG(pFile, -EINVAL);

	/* get vnode of 'struct inode' */
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pVnodeOps, -ENOSYS);

	LNX_IMZ(("[in] FILE open i_ino %lu size %Lu flag(0%o) %s",
				pInode->i_ino,
				LINUX_InodeReadSize(pInode),
				pFile->f_flags,
				(pFile->f_dentry)? (char*)pFile->f_dentry->d_name.name: ""));

	/* call native open operation */
	if (pVnode->pVnodeOps->pfnOpen)
	{
		nErr = pVnode->pVnodeOps->pfnOpen(pFcb);
		if (nErr)
		{
			LNX_EMZ(("NativeFS Open fails (nErr: 0x%08x): %s",
					-nErr,
					pFile->f_dentry->d_name.name));
		}
		LNX_DMZ(("FILE open with native, %d pid", LINUX_g_CurTask->pid));
	}
	else
	{
		LNX_DMZ(("FILE open without native %016llx, %d pid",
				pVnode->llVnodeID, LINUX_g_CurTask->pid));
	}

	return RtlLinuxError(nErr);
}

/**
 * @brief		close file (file_operations: release)
 * @param[in]	pInode		inode structure
 * @param[in]	pFile		file structure
 * @return		zero on success, negative value on failure
 */
LINUX_ERROR
FileClose(
	IN	PLINUX_INODE		pInode, 
	IN	PLINUX_FILE			pFile)
{
	PFILE_CONTROL_BLOCK		pFcb = (PFILE_CONTROL_BLOCK) pFile;
	PVNODE					pVnode;
	FERROR					nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pInode, -EINVAL);
	LNX_ASSERT_ARG(pFile, -EINVAL);

	/* get vnode of 'struct inode' */
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pFileOps, -ENOSYS);

	LNX_IMZ(("[in] FILE close i_ino %lu %s",
				pInode->i_ino,
				(pFile->f_dentry)? (char*) pFile->f_dentry->d_name.name:""));

	/* call native close operation */
	if (pVnode->pFileOps->pfnClose)
	{
		nErr = pVnode->pFileOps->pfnClose(pFcb);
		if (nErr)
		{
			LNX_EMZ(("NativeFS Close fails (nErr: 0x%08x)", -nErr));
		}
	}
	else
	{
		LNX_DMZ(("FILE close without native 0x%llx, %d pid",
				pVnode->llVnodeID, LINUX_g_CurTask->pid));
	}

	return RtlLinuxError(nErr);
}


#ifdef CONFIG_RFS_FS_SYNC_ON_CLOSE
/**
 * @brief		flush in-core data and metadata
 * @param[in]	pFile	file descriptor
 * @param[in]	pId		owner id (?)
 * @return		zero on success
 */
LINUX_ERROR
FileFlush(
	IN	PLINUX_FILE		pFile,
	IN	LINUX_FL_OWNER	pId)
{
	/* 20091026
	 *
	 * If you want to guarantee that user data is flushed before syncing metadata,
	 * you should confirms I/O result here.
	 */

	/*
	 * sync flag guarantees the write should be synchronous.
	 * But write_inode of RFS 3.0(BTFS) always writes data & metadata synchoronously.
	 */
	return LINUX_WriteInodeNow(pFile->f_mapping->host, 1);
}
#endif

/**
 * @brief		move file offset to new position
 * @param[in]	pFile		file structure
 * @param[in]	llOffset	new position to move by seek operation
 * @param[in]	nFlagSeek	seek operation
 * @return		new offset on success, or errno
 */
LINUX_OFFSET
FileSeek(
	IO	PLINUX_FILE			pFile, 
	IN	LINUX_OFFSET		llOffset, 
	IN	int					nFlagSeek)
{
	PFILE_CONTROL_BLOCK		pFcb = (PFILE_CONTROL_BLOCK) pFile;
	PVNODE					pVnode = NULL;
	FERROR					nErr = FERROR_NO_ERROR;
	LINUX_ERROR				dwLinuxError = 0;

	LNX_ASSERT_ARG(pFile, -EINVAL);

	pVnode = FcbGetVnode(pFcb);
	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pFileOps, -ENOSYS);

	LNX_IMZ(("[in] FILE seek to %lld", llOffset));
	/*
	 * 'struct file' has no lock
	 *  linux vfs(default_llseek) gets big lock(lock_kernel(): spin lock)
	 */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 38)
	/* Get the big kernel lock */
	lock_kernel();
#else
        mutex_lock(&pFile->f_dentry->d_inode->i_mutex);
#endif

	switch (nFlagSeek) 
	{
		case LINUX_SEEK_END:
			llOffset += LINUX_InodeReadSize(pFile->f_dentry->d_inode);
			break;
		case LINUX_SEEK_CUR:
			llOffset += pFile->f_pos;
			break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)                
		case LINUX_SEEK_DATA:
			if (llOffset >= LINUX_InodeReadSize(pFile->f_dentry->d_inode))
			{       
				mutex_unlock(&pFile->f_dentry->d_inode->i_mutex);
				return -ENXIO;
			}
			break;
		case LINUX_SEEK_HOLE:
			if (llOffset >= LINUX_InodeReadSize(pFile->f_dentry->d_inode))
			{   
				mutex_unlock(&pFile->f_dentry->d_inode->i_mutex);
				return -ENXIO;
			}
			llOffset = LINUX_InodeReadSize(pFile->f_dentry->d_inode);
			break;
#endif 
	}


	if (llOffset < 0)
	{
		LNX_VMZ(("offset is invalid"));
		dwLinuxError = -EINVAL;
	}
	else if (llOffset != pFile->f_pos)
	{
		/* valid offset */
		if (pVnode->pFileOps->pfnSeekFile)
		{
			nErr = pVnode->pFileOps->pfnSeekFile(pFcb, (FILE_OFFSET) llOffset);
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("NativeFS SeekFile fails (nErr: 0x%08x)", -nErr));
			}
			dwLinuxError = RtlLinuxError(nErr);
		}
		else
		{
			LNX_VMZ(("No Native interface for SeekFile"));
		}
	}

	if (0 == dwLinuxError)
	{
		/* update file->p_pos */
		FcbSetOffset(pFcb, llOffset);
	}
	else
	{
		/* set error */
		llOffset = (LINUX_OFFSET) dwLinuxError;
	}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 38)
	/* Release the big kernel lock */
	unlock_kernel();
#else
        mutex_unlock(&pFile->f_dentry->d_inode->i_mutex);
#endif

	LNX_IMZ(("[out] FILE seek end, %d pid", LINUX_g_CurTask->pid));

	return llOffset;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
/**
 * @brief		write up to count bytes to file at speicified position
 * @param[in]	pKiocb		kiocb pointer
 * @param[in]	pIovec		io vector pointer
 * @param[in]	nSegs		number of segments 
 * @param[in]	llPos		offset in file
 * @return      write bytes on success, errno on failure
 */
LINUX_SSIZE_T
FileAioWrite(
	IN	PLINUX_KIOCB		pKiocb, 
	IN	PLINUX_CIOVEC		pIovec,
	IN	unsigned long		nSegs,
	IN	LINUX_OFFSET		llPos)
{
	PVNODE					pVnode = NULL;
	PLINUX_FILE				pFile = pKiocb->ki_filp;
	LINUX_SIZE_T			nCount = 0;
	LINUX_SSIZE_T			nLinuxError = 0;
	
	/*
	 * check the count
	 * VFS verify that the count is not negative : rw_verify_area()
	 */
	nCount = pKiocb->ki_left; /* remaining count for write */
	if (nCount == 0)
	{
		LNX_VMZ(("ncount is 0"));
		return 0;
	}

	LNX_ASSERT_ARG(pFile, -EINVAL);

	/* get vnode of 'struct file' */
	pVnode = FcbGetVnode((PFILE_CONTROL_BLOCK)pFile);

	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pVnodeOps, -ENOSYS);
	LNX_ASSERT_ARG(pVnode->pVnodeOps->pfnExpandClusters, -ENOSYS);

	LNX_IMZ(("[in] FILE aio write i_ino(%lu) vnodeID(%llx) pos(%Lu) count(%u) %s",
				pVnode->stLxInode.i_ino, pVnode->llVnodeID,
				llPos, nCount,
				(pFile->f_flags & LINUX_O_APPEND)? "(O_APPEND)": ""));

	/* allocate clusters if offset + count is larger than current size */
	nLinuxError = _FileWriteAllocateBlocks__lock(pVnode, pFile, nCount, llPos);
	if (nLinuxError)
	{
		LNX_EMZ(("Allocating clusters fails i_ino %lu ret %d",
					pVnode->stLxInode.i_ino, nLinuxError));
		goto out;
	}

	/* generic write */
	nLinuxError = LINUX_GenericFileAioWrite(pKiocb, pIovec, nSegs, llPos);

	if (nLinuxError < 0 && (nLinuxError != -ENOSPC))
	{
		LNX_SMZ(("generic_file_aio_write (ino: %lu): result %d",
					pVnode->stLxInode.i_ino, nLinuxError));
	}

out:
	LNX_IMZ(("[out] FILE aio write end, %d pid: ret %lld, pos %Lu, count %u",
				LINUX_g_CurTask->pid,
				(long long) nLinuxError, llPos, nCount));

	return nLinuxError;
}
#endif

/**
 * @brief		write up to count bytes to file at speicified position
 * @param[io]	pFile		file
 * @param[io]	pBuf		buffer pointer
 * @param[io]	nCount		number of bytes to write
 * @param[io]	pPos		offset in file
 * @return      write bytes on success, errno on failure
 */
LINUX_SSIZE_T
FileWrite(
	IO	PLINUX_FILE			pFile, 
	IN	const char*			pBuf, 
	IN	LINUX_SIZE_T		nCount, 
	IO	LINUX_OFFSET*		pPos)
{
	PVNODE					pVnode = NULL;
	LINUX_SSIZE_T			nLinuxError = 0;

	/*
	 * check the count
	 * VFS verify that the count is not negative : rw_verify_area()
	 */
	if (nCount == 0)
	{
		LNX_VMZ(("ncount is 0"));
		return 0;
	}

	LNX_ASSERT_ARG(pFile, -EINVAL);
	LNX_ASSERT_ARG(pPos, -EINVAL);

	/* get vnode of 'struct file' */
	pVnode = FcbGetVnode((PFILE_CONTROL_BLOCK)pFile);

	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(pVnode->pVnodeOps, -ENOSYS);
	LNX_ASSERT_ARG(pVnode->pVnodeOps->pfnExpandClusters, -ENOSYS);

	LNX_IMZ(("[in] FILE write i_ino %lu size %Lu pos %Lu count %u MappedS %Lu %s",
				pVnode->stLxInode.i_ino,
				pVnode->stLxInode.i_size,
				*pPos, nCount,
				pVnode->llMappedSize,
				(pFile->f_flags & LINUX_O_APPEND)? "(O_APPEND)": ""));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
	nLinuxError = LINUX_DoSyncWrite(pFile, pBuf, nCount, pPos);
	/*
	 * do_sync_write will call aio_write : FileAioWrite()
	 */

#else /* #if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19) */

	/* allocate clusters if offset + count is larger than current size */
	nLinuxError = _FileWriteAllocateBlocks__lock(pVnode, pFile, nCount, *pPos);
	if (nLinuxError)
	{
		LNX_EMZ(("Allocating clusters fails i_ino %lu ret %d",
					pVnode->stLxInode.i_ino, nLinuxError));
		goto out;
	}

	/* generic write */
	nLinuxError = LINUX_GenericFileWrite(pFile, pBuf, nCount, pPos);

	if (nLinuxError < 0 && (nLinuxError != -ENOSPC))
	{
		LNX_SMZ(("generic_file_write (ino: %lu): result %d",
					pVnode->stLxInode.i_ino, nLinuxError));
	}

out:
#endif /* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) */

	LNX_IMZ(("[out] FILE write i_ino %lu end, %d pid: i_size: %Lu, ret %lld, count %u MappedS %Lu",
				pVnode->stLxInode.i_ino,
				LINUX_g_CurTask->pid,
				pVnode->stLxInode.i_size,
				(long long) nLinuxError, nCount,
				pVnode->llMappedSize));

	return nLinuxError;
}


/**
 * @brief		flush all dirty buffers of inode include data and meta data
 * @param[in]	pFile         file pointer
 * @param[in]	pDentry       dentry pointer
 * @param[in]	nDataSync     flag
 * @return		0 on success, errno on failure
 */
LINUX_ERROR
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
FileSync(
	IN	PLINUX_FILE		pFile,
	IN	LINUX_OFFSET		start,
	IN	LINUX_OFFSET		end, 
	IN	int			nDataSync)   
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34)
FileSync(
	IN	PLINUX_FILE		pFile, 
	IN	int			nDataSync)
#else
FileSync(
	IN	PLINUX_FILE		pFile, 
	IN	PLINUX_DENTRY		pDentry, 
	IN	int			nDataSync)
#endif
{
	int	nLinuxError = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34)
	LNX_ASSERT_ARG(pFile->f_path.dentry->d_inode, -EINVAL);
	LNX_IMZ(("[in] FILE sync i_ino %lu , %d pid",
	pFile->f_path.dentry->d_inode->i_ino, LINUX_g_CurTask->pid));
#else
	LNX_ASSERT_ARG(pDentry, -EINVAL);
	LNX_ASSERT_ARG(pDentry->d_inode, -EINVAL);
	LNX_IMZ(("[in] FILE sync i_ino %lu , %d pid",
				pDentry->d_inode->i_ino, LINUX_g_CurTask->pid));
#endif

	/*
	 * Metadata Sync
	 * VFS sync operation (do_fsync) writes dirty pages of file  before
	 * calling f_op->fsync(this function) and confirms I/O result after
	 * this function.
	 */

	/* 20091026
	 *	(The policy of user data is changed. Not guarantee -> Guarantee)
	 *
	 * If you want to guarantee that user data is flushed before syncing metadata,
	 * you should confirms I/O result here.
	 */

	/*
	 * sync flag guarantees the write should be synchronous.
	 * But write_inode of RFS 3.0(BTFS) always writes data & metadata synchoronously.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	nLinuxError = filemap_write_and_wait_range(pFile->f_path.dentry->d_inode->i_mapping, start, end);
        
	if (nLinuxError)
	{
		return nLinuxError;
	}

	LINUX_Lock(&(pFile->f_path.dentry->d_inode)->i_mutex);
#endif 
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34)
	nLinuxError = LINUX_WriteInodeNow(pFile->f_path.dentry->d_inode, 1);
#else
	nLinuxError = LINUX_WriteInodeNow(pDentry->d_inode, 1);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)         
	LINUX_Unlock(&(pFile->f_path.dentry->d_inode)->i_mutex);
#endif     

	LNX_IMZ(("[out] FILE sync end, %d pid", LINUX_g_CurTask->pid));

	return nLinuxError;
}

/**
 * @brief		check a permission of inode
 * @param[in]	pInode			inode
 * @param[in]	dwLxOpMode		file mode to check
 * @param[in]	pNd				nameidata
 * @return		0 on success, errno on failure
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27)
LINUX_ERROR
FilePermission(
	IN	PLINUX_INODE		pInode, 
	IN	int			dwLxOpMode, //mode mask for checking
	IN	PLINUX_NAMEIDATA	pNd)
#elif ((LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)))
/* #if KERNEL_VERSION(2,6,27) <= LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38) || LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)*/
LINUX_ERROR
FilePermission(
	IN      PLINUX_INODE            pInode,
	IN      int			dwLxOpMode)
#else
/* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38) */
LINUX_ERROR
FilePermission(
	IN	PLINUX_INODE		pInode,
	IN	int			dwLxOpMode,
	IN	unsigned int		flags)
#endif
{
	PVNODE				pVnode = NULL;
	LINUX_ERROR			dwLinuxError = 0;

	LNX_ASSERT_ARG(pInode, -EINVAL);

	if ((dwLxOpMode & MAY_WRITE) && LINUX_IS_RDONLY(pInode))
	{
		LNX_EMZ(("vfs permission error ROFS"));
		return (-EROFS);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
       if (dwLxOpMode  & MAY_NOT_BLOCK)
               return -ECHILD;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	if (flags & IPERM_FLAG_RCU)
		return -ECHILD;
#endif

	/* check NativeFS's Permission */
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* call Native's permission if exist */
	if (pVnode->pVnodeOps && pVnode->pVnodeOps->pfnPermission)
	{
		unsigned int	dwOpMode = 0;
		FERROR			nErr = FERROR_NO_ERROR;

		/* set operation mode */
		if (dwLxOpMode & MAY_WRITE)
			dwOpMode |= OP_WRITE;
		if (dwLxOpMode & MAY_READ)
			dwOpMode |= OP_READ;
		if (dwLxOpMode & MAY_EXEC)
			dwOpMode |= OP_EXEC;

		/* if need RWX permission, call pfnPermission */
		if (dwOpMode != 0) 
		{
			nErr = pVnode->pVnodeOps->pfnPermission(pVnode, dwOpMode);
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("NativeFS Permission fails for 0%o (nErr : 0x%08x)",
							-nErr, dwLxOpMode));
				return RtlLinuxError(nErr);
			}
		}
	}

	/* check for Linux VFS permission */
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)))
	dwLinuxError = LINUX_GenericPermission(pInode, dwLxOpMode, NULL);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)))
	dwLinuxError = LINUX_GenericPermission(pInode, dwLxOpMode, flags, NULL);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
	dwLinuxError = LINUX_GenericPermission(pInode, dwLxOpMode);
#else
	dwLinuxError = LINUX_VfsPermission(pInode, dwLxOpMode);
#endif
	if (dwLinuxError)
	{
		LNX_SMZ(("vfs permission error %d (i_mode:0%o mask:0x%x)",
					dwLinuxError,
					(unsigned int)pInode->i_mode,
					dwLxOpMode));
	}

	return dwLinuxError;
}

/* check for read only file, not regular file or a file forbided to change size */
#define IS_RESIZABLE(pInode)									\
((LINUX_IS_RDONLY(pInode) || !LINUX_IS_REG(pInode->i_mode) ||	\
  LINUX_IS_IMMUTABLE(pInode) || LINUX_IS_APPEND(pInode)) ? 0 : 1)

/**
 * @brief		change size of file
 * @param[in]	pVnode	vnode for change
 * @param[io]	pAttr	attributes to set
 * @return		0 on success, errno on failure
 */
static LINUX_ERROR
_FileResizeOps(PVNODE pVnode, PLINUX_ATTR pAttr)
{
	FERROR			nErr = FERROR_NO_ERROR;
	PLINUX_INODE	pInode = NULL;

	LNX_ASSERT_ARG(pAttr, -EINVAL);
	LNX_ASSERT_ARG(pVnode, -EINVAL);
	pInode = &(pVnode->stLxInode);

	if (!IS_RESIZABLE(pInode))
	{
		/* check it 
		   : read only file, not regular file, forbided to change size */
		LNX_SMZ(("Resizing is not permitted"));
		return -EPERM;
	}
	else if (pAttr->ia_size > pInode->i_sb->s_maxbytes)
	{
		/* check the maximum file size */

		LNX_SMZ(("truncate beyond the volume"));

		return -EFBIG;
	}
	else if (NULL == pVnode->pVnodeOps->pfnSetFileSize)
	{
		/* check operation */

		LNX_CMZ(("No Native interface for changing size"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/* change inode's size : treat only the expand case */
	if (pAttr->ia_size > LINUX_InodeReadSize(pInode))
	{
		/*
		 * ## change size
		 * bFillZero flag is FALSE.
		 * If TRUE,
		 * NativeFS always inititlizes NULL contents after expanding a file.
		 * Otherwise,
		 * NativeFS calls Nestle's zerofill function depends on vnode flag.
		 * pfnSetFileSize will update mtime, size and mark dirty flag.
		 */
		nErr = pVnode->pVnodeOps->pfnSetFileSize(pVnode, pAttr->ia_size, FALSE);

		if (nErr != FERROR_NO_ERROR)
		{
			/* fail to change size */
			if (nErr != FERROR_NO_FREE_SPACE)
			{
				LNX_EMZ(("NativeFS SetFileSize fails (nErr: 0x%08x)"
							": size %lld -> %lld",
							-nErr,
							LINUX_InodeReadSize(pInode),
							pAttr->ia_size));
			}

			/*
			 * Linux VFS allocates and maps new pages,
			 *  before calling i_op->setattr for truncating a file.
			 * When expanding a file, you should shrink the allocated pages.
			 */
			LINUX_TruncInodePages(&pInode->i_data, LINUX_InodeReadSize(pInode));

			return RtlLinuxError(nErr);
		}

		LNX_VMZ(("NativeFS SetFileSize %lld to %lld(!= llMappedSize)",
				LINUX_InodeReadSize(pInode),
				pAttr->ia_size));

		/* update inode's size and i_blocks (numof allocated blocks) */
		LINUX_InodeWriteSize(pInode, pAttr->ia_size);
		/* nativefs set i_blocks
		LxSetBlocks(pInode, pAttr->ia_size);
		*/

		/* after truncating a file, mtime need to be updated */
		pAttr->ia_valid |= LINUX_ATTR_MTIME;

		/* do not need to change size */
		pAttr->ia_valid &= ~LINUX_ATTR_SIZE;
	}

	return 0;
}

/** 
 * @brief		check requested mode is valid at SETATTR mode
 * @param[in]	pVnode	vnode for change
 * @param[in]	pAttr	attributes to set
 * @return		0 on not-change-mode, 1 on change-mode, errno on failure
 */
static inline LINUX_ERROR
_FileCheckAttrForSETATTR(PVNODE pVnode, PLINUX_ATTR pAttr)
{
	/*
	 * wMaskedPerm is the masked mode to be changed
	 * which just deal with 'rwx' field
	 * i_mode of inode can be only changed to the masked permission
	 */
	LINUX_MODE		wFsMask, wMaskedPerm;
	PLINUX_INODE	pInode = NULL;

	LNX_ASSERT_ARG(pAttr, -EINVAL);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	if (pAttr->ia_mode & (LINUX_S_ISUID | LINUX_S_ISGID | LINUX_S_ISVTX))
	{
		/* don't allow change of SetUID, SetGID, Sticky bits */
		LNX_EMZ(("SetUID, SetGID, Sticky bits of file and directory can not be changed"));
		return -EPERM;
	}

	if (!(pAttr->ia_valid & LINUX_ATTR_MODE))
	{
		/* no change of mode */
		return 0;
	}

	pInode = &(pVnode->stLxInode);
	if (LINUX_IS_DIR(pInode->i_mode))
		wFsMask = pVnode->pVcb->stExtOpt.wFsDmask;
	else
		wFsMask = pVnode->pVcb->stExtOpt.wFsFmask;

	wMaskedPerm = (pAttr->ia_mode & ACL_RWX) & ~wFsMask;

	 /* 'r-x' field of i_mode and the masked permission should be the same */
	if ((wMaskedPerm & (ACL_READS | ACL_EXECUTES)) != (pInode->i_mode & (ACL_READS | ACL_EXECUTES)))
		/* no change of mode */
		return 0;

	/*
	 * All '-w-' bit of i_mode to be changed should be disabled. (to Read-only mode)
	 * or '-w-' bit of i_mode to be changed should be 'Bit not' with '-w-' bits of the mask
	 *
	 * For example, if the mask is '-------w-', (it always disables the other's w bit.)
	 * the masked permission to be changed should be '-w--w----' or '---------'
	 */
	if ((wMaskedPerm & ACL_WRITES) && ((wMaskedPerm & ACL_WRITES) != (~wFsMask & ACL_WRITES)))
		/* no change of mode */
		return 0;

	/* change of mode */
	return 1;
}

/**
 * @brief		change uid, gid, permission of file
 * @param[in]	pVnode	vnode for change
 * @param[io]	pAttr	attributes to set
 * @return		0 on success, errno on failure
 */
static LINUX_ERROR
_FileAttrOps(PVNODE pVnode, PLINUX_ATTR pAttr)
{
	FERROR			nErr = FERROR_NO_ERROR;
	PLINUX_INODE	pInode = NULL;

	ACL_MODE		wPerm = 0;
	unsigned int	dwUid = 0;
	unsigned int	dwGid = 0;
	BOOL			bChangeGUid = FALSE;

	LNX_ASSERT_ARG(pAttr, -EINVAL);
	LNX_ASSERT_ARG(pVnode, -EINVAL);
	pInode = &(pVnode->stLxInode);

	if (NULL == pVnode->pVnodeOps->pfnSetGuidMode)
	{
		/* check operation */
		LNX_CMZ(("No Native interface for changing gid/uid/mode"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/* change uid */
	if ((pAttr->ia_valid & LINUX_ATTR_UID) && (pAttr->ia_uid != pInode->i_uid))
	{
		dwUid = pAttr->ia_uid;
		bChangeGUid = TRUE;
	}
	else
	{
		/* do not change uid */
		dwUid = pInode->i_uid;
	}

	/* change gid */
	if ((pAttr->ia_valid & LINUX_ATTR_GID) && (pAttr->ia_gid != pInode->i_gid))
	{
		dwGid = pAttr->ia_gid;
		bChangeGUid = TRUE;
	}
	else
	{
		/* do not change gid */
		dwGid = pInode->i_gid;
	}

	/* in case of external mount */
	/* not allow to change uid/gid to be compatible with linux vfat feature */
	if (IS_VCB_SETATTR(pVnode->pVcb) && (bChangeGUid == TRUE))
	{
		/* don't allow owner and group of file and directory to be changed */
		LNX_EMZ(("owner of file and directory can not be changed"));
		return -EPERM;
	}

	/*
	 * For ACL
	 */

	/* do not change ACL for symlink */
	if (LINUX_IS_LNK(pInode->i_mode))
	{
		pAttr->ia_valid &= ~LINUX_ATTR_MODE;
	}

	/* change ACL */
	if ((pAttr->ia_valid & LINUX_ATTR_MODE) &&
		((pAttr->ia_mode & LINUX_S_IALLUGO) != (pInode->i_mode & LINUX_S_IALLUGO)))
	{
		/*
		 * external mount &&
		 * compatible with linux vfat's uid, gid, umask option
		 */
		if (IS_VCB_SETATTR(pVnode->pVcb))
		{
			LINUX_ERROR			dwLinuxError = 0;
			dwLinuxError = _FileCheckAttrForSETATTR(pVnode, pAttr);
			
			if (dwLinuxError == 0) // no change, but don't return error
			{
				pAttr->ia_valid &= ~LINUX_ATTR_MODE;
			}
			else if (dwLinuxError == 1) // change mode
			{
				/*
				 * If the permission is satisfied with conditions to change the mode,
				 *  pAttr->ia_mode will be arranged with mask and applied to new i_mode
				 */

				/* adjust mode with mask */
				if (LINUX_IS_DIR(pInode->i_mode))
					pAttr->ia_mode &= ~(pVnode->pVcb->stExtOpt.wFsDmask);
				else
					pAttr->ia_mode &= ~(pVnode->pVcb->stExtOpt.wFsFmask);

				/* the current mode and requested mode are the same. do not need to change */
				if ((pAttr->ia_mode & LINUX_S_IALLUGO) == (pInode->i_mode & LINUX_S_IALLUGO))
					pAttr->ia_valid &= ~LINUX_ATTR_MODE;
			}
			else // return error
			{
				/* not allowed operation */
				return dwLinuxError;
			}
		} 
		
		if (pAttr->ia_valid & LINUX_ATTR_MODE)
		{
			/* change mode */
			wPerm = pAttr->ia_mode & LINUX_S_IALLUGO;
			bChangeGUid = TRUE;
		}
		else
		{
			/* do not change mode */
			wPerm = pInode->i_mode & LINUX_S_IALLUGO;
		}
	}
	else
	{
		/* do not change ACL */
		wPerm = pInode->i_mode & LINUX_S_IALLUGO;
	}

	/*
	 * 2010.05.17 (dosam.kim)
	 * According to FAT spec, Root directory cannot have owner and permission.
	 * BTFS can have owner, permission in XDE in case of internal mode. (not compatible with FAT spec)
	 * In case of external mode, BTFS should not create XDE for file or directory.
	 * But current BTFS creates XDE for root directory. It SHOULD be fixed later.
	 */
	if (IS_VCB_EXTERNAL(pVnode->pVcb) && (pVnode == pVnode->pVcb->pRoot))
		bChangeGUid = FALSE;
	
	if (bChangeGUid)
	{
		LNX_VMZ(("uid: %u gid: %u perm: 0%o", dwUid, dwGid, wPerm));

		nErr = pVnode->pVnodeOps->pfnSetGuidMode(pVnode, dwUid, dwGid, wPerm);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_EMZ(("NativeFS SetGuidMode failes (nErr: 0x%08x)"
					":fail to change uid/gid/mode",
					-nErr));

			return RtlLinuxError(nErr);
		}
	}

	return 0;
}

/**
 * @brief		change time of file
 * @param[in]	pVnode	vnode for change
 * @param[io]	pAttr	attributes to set
 * @return		0 on success, errno on failure
 */
static LINUX_ERROR
_FileTimeOps(PVNODE pVnode, PLINUX_ATTR pAttr)
{
	FERROR		nErr = FERROR_NO_ERROR;

	PSYS_TIME	ptmChangeTime[2];
	SYS_TIME	tmAccessTime;	/* access time */
	SYS_TIME	tmModTime;		/* modification time */

	LNX_ASSERT_ARG(pAttr, -EINVAL);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* check operation */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnSetFileTime))
	{
		LNX_CMZ(("No Native interface for changing time"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	ptmChangeTime[0] = ptmChangeTime[1] = NULL;

	if (pAttr->ia_valid & LINUX_ATTR_ATIME)
	{
		RtlLinuxTimeToSysTime(&pAttr->ia_atime, &tmAccessTime);
		ptmChangeTime[0] = &tmAccessTime;
	}

	if (pAttr->ia_valid & LINUX_ATTR_MTIME)
	{
		RtlLinuxTimeToSysTime(&pAttr->ia_mtime, &tmModTime);
		ptmChangeTime[1] = &tmModTime;
	}

	nErr = pVnode->pVnodeOps->pfnSetFileTime(pVnode,
			NULL,
			ptmChangeTime[0],
			ptmChangeTime[1]);
	if (nErr != FERROR_NO_ERROR)
	{
		/*
		 * 2010.05.18 (dosam.kim)
		 * Root directory of BTFS is not allowed to change the time 
		 * But BTFS should take information of the time. It SHOULD be fixed later.
		 */
		if ((nErr == FERROR_ACCESS_DENIED) && (pVnode == pVnode->pVcb->pRoot))
			return 0;

		LNX_EMZ(("NativeFS SetFileTime fails (nErr: 0x%08x)"
					": change time", -nErr));

		return RtlLinuxError(nErr);
	}

		return 0;
}

/**
 * @brief		change an attribute of inode
 * @param[in]	pDentry    dentry
 * @param[io]	pAttr      new attribute to set
 * @return      0 on success, errno on failure
 *
 * it is only used for chmod, especially when read only mode be changed
 */
LINUX_ERROR
FileSetAttribute(
	IN	PLINUX_DENTRY	pDentry, 
	IO	PLINUX_ATTR		pAttr)
{
	PVNODE				pVnode = NULL;
	PLINUX_INODE		pInode = NULL;
	LINUX_ERROR			dwLinuxError = 0;
	OPERATION_MODE		dwPerm = OP_METAUPDATE;
	FERROR				nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pAttr, -EINVAL);
	LNX_ASSERT_ARG(pDentry, -EINVAL);
	LNX_ASSERT_ARG(pDentry->d_inode, -EINVAL);

	pInode = pDentry->d_inode;
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARG(pVnode, -EINVAL);

	LNX_IMZ(("[in] FILE setattr %llu, %d pid",
			pVnode->llVnodeID, LINUX_g_CurTask->pid));

	LNX_VMZ(("set %s%s%s%s%s%s",
			((pAttr->ia_valid & LINUX_ATTR_UID)? "uid,": ""),
			((pAttr->ia_valid & LINUX_ATTR_GID)? "gid,": ""),
			((pAttr->ia_valid & LINUX_ATTR_SIZE)? "size,": ""),
			((pAttr->ia_valid & LINUX_ATTR_ATIME)? "atime,": ""),
			((pAttr->ia_valid & LINUX_ATTR_MTIME)? "mtime,": ""),
			((pAttr->ia_valid & LINUX_ATTR_MODE)? "mode,": "")));

	/* check permission (LinuxVFS) */
	dwLinuxError = LINUX_InodeChangeOk(pInode, pAttr);
	if (dwLinuxError)
	{
		LNX_SMZ(("inode change ok : not permitted"));
		return dwLinuxError;
	}


	/* check NativeFS's permission */
	if (pAttr->ia_valid & LINUX_ATTR_SIZE)
	{
		if (pAttr->ia_size == LINUX_InodeReadSize(pInode))
			pAttr->ia_valid &= ~LINUX_ATTR_SIZE;
		else 
			dwPerm |= OP_RESIZE;
	}

	nErr = FileCheckNativePermission(pVnode, dwPerm);
	if (nErr)
	{
		LNX_EMZ(("Not permitted by NativeFS(0x%x)", dwPerm));

		return RtlLinuxError(nErr);
	}

	/* change size */
	if (pAttr->ia_valid & LINUX_ATTR_SIZE)
	{
		dwLinuxError = _FileResizeOps(pVnode, pAttr);
		if (dwLinuxError)
		{
			if (dwLinuxError != -ENOSPC)
			{
				LNX_EMZ(("File resize fails(err: %d)", dwLinuxError));
			}
			return dwLinuxError;
		}
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
		/* _FileResizeOps only takes care of forward truncation.
		 * Since the linux kernel 2.6.36, LINUX_InodeSetattr has chaged from
		 * inode_setattr to setattr_copy. inode_setattr used to call vmtruncate
		 * which in turn used to call the inode->i_op->truncate so the backward
		 * truncation case was taken care of. But setattr_copy does not call
		 * vmtruncate. Therefore we have to take care of backward truncation
		 * seperately
		 */
		if (pAttr->ia_size < LINUX_InodeReadSize(pInode))
		{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
			dwLinuxError = LINUX_Vmtruncate(pInode, pAttr->ia_size);
			if (dwLinuxError)
			{
				LNX_EMZ(("File resize fails (errno : %d)", dwLinuxError));
				return dwLinuxError;
			}
#else
			dwLinuxError = LINUX_InodeNewSizeOk(pInode, pAttr->ia_size);
			if (dwLinuxError)
			{
				LNX_EMZ(("File resize fails (errno : %d)", dwLinuxError));
				return dwLinuxError;
			}
			LINUX_TruncateSetSize(pInode, pAttr->ia_size);
			FileTruncate(pInode);
#endif
		}
		#endif
	}

	/* change mode, uid, gid */
	if (pAttr->ia_valid & (LINUX_ATTR_MODE | LINUX_ATTR_GID | LINUX_ATTR_UID))
	{
		dwLinuxError = _FileAttrOps(pVnode, pAttr);
		if (dwLinuxError)
		{
			LNX_EMZ(("File attribute change fails(err: %d)", dwLinuxError));
			return dwLinuxError;
		}
	}

	/* change atime or mtime */
	if (pAttr->ia_valid & (LINUX_ATTR_ATIME | LINUX_ATTR_MTIME))
	{
		dwLinuxError = _FileTimeOps(pVnode, pAttr);
		if (dwLinuxError)
		{
			LNX_EMZ(("File time update fails(err: %d)", dwLinuxError));
			return dwLinuxError;
		}
	}

	/* VFS changes inode's attribute */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	LINUX_InodeSetattr(pInode, pAttr);
	LINUX_MarkInodeDirty(pInode);
#else
	dwLinuxError = LINUX_InodeSetattr(pInode, pAttr);
	if (dwLinuxError)
	{
		LNX_SMZ(("inode_setattr fails (errno : %d)", dwLinuxError));
	}
#endif

	LNX_IMZ(("[out] FILE setattr end 0x%x, %llu, %d pid",
			pInode->i_mode, pInode->i_size, LINUX_g_CurTask->pid));

	return dwLinuxError; 
}


/**
 * @brief		truncate a file to a specified size
 * @param[in]	inode inode
 *
 * support to reduce or enlarge a file
 */
void
FileTruncate(
	IO	PLINUX_INODE	pInode)
{	
	PVNODE				pVnode = NULL;
	FERROR				nErr = FERROR_NO_ERROR;

	if (NULL == pInode)
	{
		RFS_ASSERT(0);
		return;
	}

	pVnode = VnodeGetVnodeFromInode(pInode);

	/* no interface of NativeFS */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnSetFileSize))
	{
		nErr = FERROR_NOT_SUPPORTED;
		LNX_CMZ(("No Native interface for truncating file"));
		RFS_ASSERT(0);
		return;
	}

	/* 
	 * Do nothing in sys_truncate, sys_ftruncate for expand file,
	 * because FileSetAttribute expands file size before calling
	 * vmtruncate() and being clear flag.
	 * This is called by vmtruncate() at rollback in commit_write
	 *  or sys_truncate/ftruncate for shrink file.
	 */

	LNX_IMZ(("[in] FILE truncate %llu, %d pid",
			LINUX_InodeReadSize(pInode), LINUX_g_CurTask->pid));

	/*
	 * ## change size
	 * bFillZero flag is FALSE.
	 * If TRUE,
	 * NativeFS always inititlizes NULL contents after expanding a file.
	 * Otherwise,
	 * NativeFS calls Nestle's zerofill function depends on vnode flag.
	 * pfnSetFileSize will update mtime and size.
	 */
	nErr = pVnode->pVnodeOps->pfnSetFileSize(pVnode,
			LINUX_InodeReadSize(pInode), FALSE);

	if (nErr == FERROR_NO_ERROR)
	{
		/* reset i_blocks */
		/* nativefs set i_blocks
		LxSetBlocks(pInode, LINUX_InodeReadSize(pInode));
		*/

		/* reset mapped_size */
		VnodeSetMappedSize(pVnode, LINUX_InodeReadSize(pInode));

		/* inode's mtime is updated by NativeFS */

		LNX_VMZ(("NativeFS SetFileSize to %lld(== llMappedSize)",
					LINUX_InodeReadSize(pInode)));
	}
	else
	{
		LNX_EMZ(("NativeFS SetFileSize fails (nErr : 0x%08x)"
					": truncate shrink to %lld",
					-nErr,
					LINUX_InodeReadSize(pInode)));
	}

	LNX_IMZ(("[out] FILE truncate end(nErr : 0x%08x), %d pid",
				-nErr, LINUX_g_CurTask->pid));

	return;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17)
/*
 * In linux 2.6.17 or more, the callback function in direct io is changed.
 * Now, it is used to single get block instead of multiple get blocks.
 */
#define FileGetBlocks FileGetBlock

#else   /* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) */
/**
 *  @brief		Function to translate a logical block into physical block
 *  @param[in]	pInode		inode
 *  @param[in]	nIblock		logical block number
 *  @param[in]	nMaxBlocks  dummy variable new
 *  @param[in]	pBh			buffer head pointer
 *  @param[in]	nFlagCreate	control flag
 *  @return     zero on success, negative value on failure
 *
 *  This function is only invoked by direct IO
 */
static LINUX_ERROR 
FileGetBlocks(
	IN	PLINUX_INODE	pInode, 
	IN	LINUX_SECTOR	nblock, 
	IN	unsigned long	nMaxBlocks, 
	IN	PLINUX_BUF		pBh, 
	IN	int				nFlagCreate)
{
	LINUX_ERROR		dwLinuxError;

	dwLinuxError = FileGetBlock(pInode, nblock, pBh, nFlagCreate);
	if (!dwLinuxError)
	{
		pBh->b_size = (1 << pInode->i_blkbits);
	}
	else
	{
		LNX_EMZ(("FileGetBlock fails (errno: %d)", dwLinuxError));
	}

	return dwLinuxError;
}
#endif  /* #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) */

/**
 *  @brief		function excuting direct I/O operation
 *  @param[in]	nRwFlag			I/O command
 *  @param[in]	pIocb			VFS kiocb pointer
 *  @param[in]	pIov			VFS iovc pointer
 *  @param[in]	lOffset			I/O offset
 *  @param[in]	nNumSegs		the number segments
 *  @return     written or read date size on sucess, negative value on failure
 */
LINUX_SSIZE_T 
FileDirectRW(
	IN	int					nRwFlag, 
	IN	PLINUX_KIOCB		pIocb,
	IN	PLINUX_CIOVEC		pIov, 
	IN	LINUX_OFFSET		llOffset, 
	IN	unsigned long		nNumSegs)
{
	PLINUX_INODE			pInode = NULL;
	PLINUX_SUPER			pSb = NULL;
	LINUX_SIZE_T			nRet = 0;

	pInode = pIocb->ki_filp->f_mapping->host;
	LNX_ASSERT_ARG(pInode, -EFAULT);

	pSb = pInode->i_sb;
	LNX_ASSERT_ARG(pSb, -EFAULT);

	/*
     * WARNING!!
     * Though Direct Write,
     * sometime run buffered write opeartion for filling "HOLE" created by lseek()
     */

	/* direct write operation */
	if (nRwFlag == LINUX_WRITE)
	{
		PVNODE					pVnode = NULL;
		LINUX_OFFSET			llCurPos = 0;
		LINUX_OFFSET			llOriginSize = 0;
		unsigned int			dwFilledByte = 0;
		FERROR					nErr = FERROR_NO_ERROR;

		/*
		 * WRITE
		 *
		 * If it is necessary to allocate additional blocks for append write,
		 * all blocks are allocated before calling this operation in FileWrite().
		 */

		/* get vnode */
		pVnode = VnodeGetVnodeFromInode(pInode);

		llOriginSize = LINUX_InodeReadSize(pInode);
		llCurPos = llOffset;	/* offset to start writing the file */

		LNX_IMZ(("[in] FILE dwrite size %lld, offset %lld count %u, %d pid",
				llOriginSize, llCurPos, LINUX_IovLength(pIov, nNumSegs),
				LINUX_g_CurTask->pid));

		/* fill zero from i_size to LINUX_g_CurTask offset */
		if (llCurPos > llOriginSize)
		{
			nErr = VnodeFillZero(pVnode,
								llOriginSize, 
								(llCurPos - llOriginSize),
								&dwFilledByte);
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("VnodeFillZero fails (nErr : 0x%08x)"
						": size %lld cur_pos %lld",
						-nErr,
						llOriginSize, llCurPos));

				nRet = RtlLinuxError(nErr);
				goto out;
			}

			/* update file size and sync last partial page */
			LINUX_InodeWriteSize(pInode, llCurPos);
			/* nativefs set i_blocks
			LxSetBlocks(pInode, llCurPos);
			*/

			/* update llMappedSize */
			VnodeSetMappedSize(pVnode, llCurPos);

			nRet = LxSyncLastPage(pInode, llCurPos);
			if (nRet)
			{
				LNX_EMZ(("Syncing last page fails (errno: %d)", nRet));
				goto out;
			}
		}
	}

	/* VFS direct I/O operation */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)	
	nRet = LINUX_BlockdevDirectIO(nRwFlag,
									pIocb,
									pInode,
									pSb->s_bdev,
									pIov,
									llOffset,
									nNumSegs,
									FileGetBlocks,
									NULL);
#else
	nRet = LINUX_BlockdevDirectIO(nRwFlag,
									pIocb,
									pInode,
									pIov,
									llOffset,
									nNumSegs,
									FileGetBlocks);
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	if (unlikely((nRwFlag & LINUX_WRITE) && nRet < 0)) {
		LINUX_OFFSET llISize = LINUX_InodeReadSize(pInode);
		LINUX_OFFSET llEnd = llOffset + LINUX_IovLength(pIov, nNumSegs);

		if (llEnd > llISize)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
			LINUX_Vmtruncate(pInode, llISize);
#else
			LINUX_TruncatePagecache(pInode, llEnd, llISize);
			FileTruncate(pInode);
#endif
	}
#endif

out: 

	if ((LINUX_SSIZE_T) nRet < 0)
	{
		LNX_EMZ(("[out] FILE direct %s end(%d),"
						"offset: %lld count: %u,"
						" %d pid",
						((nRwFlag == LINUX_WRITE)? "Write": "Read"),
						nRet,
						llOffset, LINUX_IovLength(pIov, nNumSegs),
						LINUX_g_CurTask->pid));
	}

	return nRet; 
}

/**
 * @brief		follow symlink path
 * @param[in]	pDentry		linux dentry pointer
 * @param[in]	pNd			linux nameidata
 * @return		pointer of resolved symlink path
 */
void*
FileFollowLink(
	IN	PLINUX_DENTRY		pDentry, 
	IN	PLINUX_NAMEIDATA	pNd)
{
	char*			pPath = NULL;
	wchar_t*		pwsPath = NULL;
	PVNODE			pVnode = NULL;
	unsigned int	dwLen;
	FERROR			nErr = FERROR_NO_ERROR;
	
	LNX_ASSERT_ARG(pDentry, ERR_PTR(-EINVAL));
	LNX_ASSERT_ARG(pNd, ERR_PTR(-EINVAL));
	LNX_ASSERT_ARG(pDentry->d_inode, ERR_PTR(-EINVAL));

	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);

	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnReadSymlink))
	{
		LNX_CMZ(("No Native interface for read symlink"));
		RFS_ASSERT(0);
		return ERR_PTR(-ENOSYS);
	}

	/* allocate memory for path to be stored */
	pwsPath = (wchar_t *) LINUX_Kmalloc(sizeof(wchar_t) * LINUX_PAGE_SIZE,
			LINUX_GFP_NOFS);
	if (!pwsPath)
	{
		LNX_EMZ(("fail to allocate memory"));
		return ERR_PTR(-ENOMEM);
	}

	pPath = (char *) LINUX_Kmalloc(LINUX_PAGE_SIZE, LINUX_GFP_NOFS);
	if (!pPath)
	{
		LNX_EMZ(("fail to allocate memory"));

		LINUX_Kfree(pwsPath);
		return ERR_PTR(-ENOMEM);
	}

	LNX_IMZ(("[in] FILE follow link, %llu file, %d pid",
			pVnode->llVnodeID, LINUX_g_CurTask->pid));

	/* read symlink's path */
	nErr = pVnode->pVnodeOps->pfnReadSymlink(pVnode, pwsPath,
					(sizeof(wchar_t) * LINUX_PAGE_SIZE), &dwLen);
	if (nErr)
	{
		LINUX_Kfree(pwsPath);
		LINUX_Kfree(pPath);

		LNX_EMZ(("NativeFS ReadSymlink fails (nErr : 0x%08x)", -nErr));

		return ERR_PTR(RtlLinuxError(nErr));
	}

	/* fixed 20090402 (because of changing of interface)
	 *
	 * pwsPath returned from pfnReadSymlink() doesn't have NULL character.
	 * But RtlConvertToMbs() detects Endof string by NULL,
	 * so it's necessary to include Null character at the end of string.
	 */
	if (dwLen < LINUX_PAGE_SIZE)
	{
		pwsPath[dwLen] = 0x00;
	}

	/* convert wchar path to multi-bytes string */
	dwLen = RtlConvertToMbs(VnodeGetVcb(pVnode), pwsPath, pPath,
			LINUX_PAGE_SIZE);
	if (dwLen <= 0)
	{
		LINUX_Kfree(pwsPath);
		LINUX_Kfree(pPath);

		LNX_EMZ(("fail to convert unicode name"));

		return ERR_PTR(-EFAULT);
	}

	LINUX_NdSetLink(pNd, pPath);

	LINUX_Kfree(pwsPath);

	LNX_IMZ(("[out] FILE follow link end, %llu file, %d pid",
			pVnode->llVnodeID, LINUX_g_CurTask->pid));

	return pPath;
}

/**
 * @brief		release symlink memory assigned by follow_link
 * @param[in]	pDentry			linux dentry pointer
 * @param[in]	pNd				linux nameidata
 * @param[out]	pSymlinkPath	buffer pointer has symlink path
 *
 */
void
FilePutLink(
	IN	PLINUX_DENTRY		pDentry, 
	IN	PLINUX_NAMEIDATA	pNd, 
	IO	void*				pSymlinkPath)
{
	/* release symlink path (multibytes) assigned by follow_link */
	if (pSymlinkPath)
	{
		LINUX_Kfree(pSymlinkPath);
	}

	return;
}

/**
 * @brief		translate index into a logical block
 * @param[in]	pInode     inode
 * @param[in]	iblock    index
 * @param[in]	pBuf buffer head pointer
 * @param[in]	create    flag whether new block will be allocated
 * @return      returns 0 on success, errno on failure
 *
 * get_block in Nestle do not allocate new cluster. just find logical block.
 * If there aren't logical block, return error.
 */
LINUX_ERROR 
FileGetBlock(
	IN	PLINUX_INODE	pInode, 
	IN	LINUX_SECTOR	dwBlock, 
	IN	PLINUX_BUF		pBuf, 
	IN	int				dwCreate)
{
	PVNODE				pVnode = NULL;
	PLINUX_SUPER		pSb = NULL;
	unsigned int		dwPhys = 0;
	unsigned int		dwContBlockCnt = 0;
	FERROR				nErr = FERROR_NO_ERROR;
	LINUX_ERROR			dwLinuxError = 0;

	LNX_ASSERT_ARG(pInode, -EINVAL);
	LNX_ASSERT_ARG(pBuf, -EINVAL);

	pSb = pInode->i_sb;
	LNX_ASSERT_ARG(pSb, -EFAULT);

	pVnode = VnodeGetVnodeFromInode(pInode);

	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnMapBlocks))
	{
		LNX_CMZ(("No Native interface for MapBlocks"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/* get physical block for logical offset(dwBlock) */
	nErr = pVnode->pVnodeOps->pfnMapBlocks(pVnode,
										(unsigned int) dwBlock,
										1,
										&dwPhys,
										&dwContBlockCnt);

// 20110121 enable the following code
/*
 * To handle pfnMapBlocks() error,
 *  while two process are accessing a single file with O_APPEND flag
 */

//#ifdef _NOT_RESOLVED_FEATURE_
	// 20091112 remove
	/*
	 * BTFS can't handle the size of Node,
	 *  if pfnExpandClusters requests clusters by the block unit.
	 */

	// 20090312 add
	if (dwCreate && (nErr == FERROR_RANGE))
	{
		LINUX_OFFSET	llNewPos = 0;

		llNewPos = ((dwBlock + 1) << pVnode->stLxInode.i_blkbits);

		LNX_DMZ(("allocate new block i_ino %lu; dwBlock %lu llNewPos %Lu",
				pInode->i_ino,
				(unsigned long) dwBlock, llNewPos));

		nErr = pVnode->pVnodeOps->pfnExpandClusters(pVnode, llNewPos);
		if (nErr != FERROR_NO_ERROR)
		{
			if (nErr != FERROR_NO_FREE_SPACE)
			{
				LNX_EMZ(("NativeFS ExpandClusters fails (nErr : 0x%08x, errno: %d) i_ino %lu"
							"\n:\tnewSize %Lu i_size %Lu pid %d",
							-nErr, RtlLinuxError(nErr),
							pInode->i_ino,
							llNewPos, pInode->i_size,
							LINUX_g_CurTask->pid));
			}

			dwLinuxError = RtlLinuxError(nErr);
			goto out;
		}
		else
		{
			nErr = pVnode->pVnodeOps->pfnMapBlocks(pVnode,
					(unsigned int) dwBlock,
					1,
					&dwPhys,
					&dwContBlockCnt);
		}
	}
//#endif

	/* fail to get physical block no. */
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS MapBlocks fails (nErr : 0x%08x) i_ino %lu"
				"\n:\tdwBlock %lu i_size %Lu pid %d %s",
				-nErr,
				pInode->i_ino,
				(unsigned long) dwBlock, pInode->i_size,
				LINUX_g_CurTask->pid,
				(dwCreate? "(alloc)":"")));
	
		if (nErr == FERROR_END_OF_FILE)
		{
			LNX_CMZ(("can't access beyond the end of file i_ino %lu"
					"\n:\tdwBlock %lu i_size %Lu",
					pInode->i_ino,
					(unsigned long) dwBlock, pInode->i_size));

			dwLinuxError = -EFAULT;
		}
		else
		{
			dwLinuxError = RtlLinuxError(nErr);
		}

		/* return error number */
		goto out;
	}

	/* check whether the physical number is behind partition size */
	if (dwPhys >= (pSb->s_bdev->bd_inode->i_size >> pSb->s_blocksize_bits))
	{
		LNX_CMZ(("Invalid Physical blk.num(%u) for Logical blk(%lu): %u i_ino %lu",
				dwPhys, (unsigned long) dwBlock,
				(unsigned int) (pSb->s_bdev->bd_inode->i_size >> pSb->s_blocksize_bits),
				pInode->i_ino));

		RFS_ASSERT(0);

		/* return error number */
		dwLinuxError = -EIO;
		goto out;
	}

	/* map physical number to buffer head */
	LINUX_MapBuffer(pBuf, pSb, dwPhys);

	/* set new buffer and update llMappedSize */
	if ((dwBlock + 1) > (pVnode->llMappedSize >> pInode->i_blkbits))
	{
		LNX_VMZ(("i_ino: %lu dwBlock:%lu i_size:%Lu set_new blks",
					pInode->i_ino,
					(unsigned long) dwBlock, pInode->i_size));

		/* Disk mapping was newly created by get_block */
		LINUX_SetBufferNew(pBuf);

		pVnode->llMappedSize += pSb->s_blocksize;
		/* nativefs set i_blocks */

		LNX_VMZ(("i_ino: %lu mapped size of %llu file is %llu",
					pInode->i_ino,
					pVnode->llVnodeID, pVnode->llMappedSize));
	}

	/* info log */
	LNX_VMZ(("i_ino %lu (block %ld ->phy %u) new mappedSize(%u):%s",
				pInode->i_ino,
				(unsigned long) dwBlock, dwPhys,
				(unsigned int)(pVnode->llMappedSize >> pSb->s_blocksize_bits),
				((NULL != pVnode->pstLxDentry)?
				 (char *)(pVnode->pstLxDentry->d_name.name): "NULL")
			));

	/* success */
	dwLinuxError = 0;

out:
	return dwLinuxError;
}

/**
 * @brief ioctl command
 * @param[in]	pInode		inode pointer
 * @param[in]	pFile		file pointer
 * @param[in]	dwCmd		ioctl command
 * @param[in]	dwArgAddr	address of user args for command
 *
 * @return		return 0 on success, or errno
 *
 * f_op->ioctl is protected by lock_kernel()/unlock_kernel()
 * VFS gets and releases kernel lock before and after f_op->ioctl in do_ioctl for inode
 * general ioctl for filp isn't protected by any lock because each task has own filp table and we don't have to be worry about race condition.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
long
FileIoctl(
       PLINUX_FILE     pFile,
       unsigned int    dwCmd,
       unsigned long   dwArgAddr)
#else
int
FileIoctl(
	PLINUX_INODE 	pInode,
	PLINUX_FILE 	pFile,
	unsigned int	dwCmd,
	unsigned long	dwArgAddr)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	PLINUX_INODE    pInode = pFile->f_path.dentry->d_inode;
#endif
	PVNODE	pVnode = NULL;
	int		dwRet = 0;

	switch (dwCmd)
	{
	/*
	 * set/clear zerofill flag for inode
	 * if set zerofill(by default), initialize clusters for truncate operation
	 */
	case RFS_IOC_SET_ZEROFILL:
	{
		int dwFlag = 0;

		if ((dwRet = LINUX_GetUser(dwFlag, (int *) dwArgAddr)) != 0)
		{ 
			LNX_SMZ(("Error(%d) in getting value from user", dwRet));
			break;
		}
		
		pVnode = VnodeGetVnodeFromInode(pInode);

		/* use Vnode's metalock to protect dwAttr */
		VnodeMetaLock(pVnode);

		/*
		 * If value is zero, set no_init_cluster flag.
		 * Otherwise, clear no_init_cluster flag.
		 */
		if (dwFlag == 0)
		{
			pVnode->dwAttr |= FILE_ATTR_NO_INIT_CLUSTER;
		}
		else
		{
			pVnode->dwAttr &= ~FILE_ATTR_NO_INIT_CLUSTER;
		}

		VnodeMetaUnlock(pVnode);

		break;
	}
	case RFS_IOC_DO_CHKDSK:
	{
		/* 20090114 need superblock lock ? -> Nativefs locks before chkdsk.  */

		dwRet = VolChkDisk(pInode->i_sb);
		if (0 != dwRet) 
		{
			LNX_CMZ(("NativeFS Integrity is broken(errno : %d)!!!", dwRet));
		}
		else
		{
			LNX_VMZ(("NativeFS Integrity is ok!!"));
		}

		break;
	}
	default:
	{
		dwRet = -ENOTTY;
		LNX_DMZ(("Unsupported command: %08x", dwCmd));
		break;
	}

	} /* end of switch */

	return dwRet;
}

// end of file
