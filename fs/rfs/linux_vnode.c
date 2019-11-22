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
 * @file        linux_vnode.c
 * @brief       This file includes vnode operations.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_vnode.h"
#include "linux_vcb.h"
#include "linux_file.h"

#include <linux/writeback.h>
#include <linux/mm.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_VNODE)


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/
/**
 * @brief	fill the page with zero
 * @param	pInode		inode 	 
 * @param	pPage		page pointer
 * @param	dwFrom		start offset within page 	
 * @param	dwTo		last offset within page	 
 * @return	return 0 on success, errno on failure
 */
static LINUX_ERROR
_FillZeroPage(
	IN	PLINUX_INODE	pInode, 
	IN	PLINUX_PAGE		pPage, 
	IN	unsigned int	dwFrom, 
	IN	unsigned int	dwTo)
{
	PLINUX_BUF		pBh = NULL;
	PLINUX_BUF		pHead = NULL;
	unsigned long	dwBlock = 0;
	unsigned int	dwBlockStart = 0;
	unsigned int	dwBlockEnd = 0;
	unsigned int	dwBlockSize = 0;
	unsigned int	dwAlignedZeroTo = 0;
	BOOL			bPartial = FALSE;
	char*			pKaddr = NULL;
	LINUX_ERROR		dwLinuxError = 0;

	LNX_VMZ(("VNODE fill zero page %llx vnode, %d pid",
			VnodeGetVnodeFromInode(pInode)->llVnodeID,
			LINUX_g_CurTask->pid));
	LNX_VMZ(("fill zero of page(%lu) from %u to %u", pPage->index, dwFrom, dwTo));

	/* get block size */
	dwBlockSize = (unsigned int) (1 << pInode->i_blkbits);

	/* if page don't have buffer, create empty buffers */
	if (!LINUX_PageHasBuffers(pPage))
	{
		LINUX_CreateEmptyBuffers(pPage, dwBlockSize, 0);
	}

	/* get first buffer of page: buffers are linked to each other */
	pHead = LINUX_PageBuffers(pPage);

	/* start block # */
	dwBlock = (pPage->index << (PAGE_CACHE_SHIFT - pInode->i_blkbits)); 

	/*
	 * In the first phase,
	 * we allocate buffers and map them to fill zero
	 */
	for (pBh = pHead, dwBlockStart = 0; 
			(pBh != pHead) || (!dwBlockStart);
			dwBlock++, (dwBlockStart = dwBlockEnd + 1), (pBh = pBh->b_this_page))
	{
		/*
		 * dwBlockStart:	first offset of current block in this page
		 * dwBlockEnd:		last offset of current block in this page
		 * pBh:				buffer head that contains current block
		 */

		if (pBh == NULL) 
		{
			/* I/O error */
			dwLinuxError = -EIO;
			LNX_SMZ(("can't get buffer head"));
			goto out;
		}

		dwBlockEnd = dwBlockStart + dwBlockSize - 1;

		/* current block is smaller than requested area (dwFrom, dwTo) */
		if (dwBlockEnd < dwFrom)
		{
			/* try again with next block */
			continue;
		}
		/* current block is larger than requested area (dwFrom, dwTo) */
		else if (dwBlockStart > dwTo)
		{
			/* stop getting buffer */
			break;
		}

		/* clear New bit of buffer head */
		LINUX_ClearBit(BH_New, &pBh->b_state);

		/* map new buffer head */
		dwLinuxError = FileGetBlock(pInode, (LINUX_SECTOR) dwBlock, pBh, 1);
		if (dwLinuxError) 
		{
			LNX_EMZ(("FileGetBlock fails (errno: %d)", dwLinuxError));
			goto out;
		}

		/* if block in the request area is not uptodate, read the block */
		if ((dwBlockStart < dwFrom) && (!LINUX_IsBufferUptodate(pBh)))
		{
			LINUX_LLRwBlock(LINUX_READ, 1, &pBh);
			LINUX_WaitOnBuffer(pBh);
			if (!LINUX_IsBufferUptodate(pBh))
			{
				dwLinuxError = -EIO;
				LNX_EMZ(("Fail to read buffer head for block %lu", dwBlock));
				goto out;
			}
		}
	} /* end of for-loop */

	/*
	 * In the second phase,
	 * we RtlFillMem the page with zero in the block aligned manner.
	 * If RtlFillMem is not block-aligned, hole may return garbage data.
	 */

	/* translate page into address */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	pKaddr = LINUX_KmapAtomic(pPage, LINUX_KM_USER0);
#else
	pKaddr = LINUX_KmapAtomic(pPage);
#endif
	dwAlignedZeroTo = dwTo | (dwBlockSize - 1);
	RtlFillMem((pKaddr + dwFrom), 0, dwAlignedZeroTo - dwFrom + 1);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	LINUX_KunmapAtomic(pKaddr, LINUX_KM_USER0);
#else
	LINUX_KunmapAtomic(pKaddr);
#endif

	/*
	 * In the third phase,
	 * we make the buffers uptodate and dirty
	 */
	for (pBh = pHead, dwBlockStart = 0; 
			(pBh != pHead) || (!dwBlockStart);
			(dwBlockStart = dwBlockEnd + 1), (pBh = pBh->b_this_page))
	{
		dwBlockEnd = dwBlockStart + dwBlockSize - 1;

		/* block exists in the front of the requested area (dwFrom, dwTo) */
		if (dwBlockEnd < dwFrom) 
		{
			if (!LINUX_IsBufferUptodate(pBh))
			{
				/* partially filled with zero */
				bPartial = TRUE;
			}
			continue;
		} 
		/* block exists in the back of the requested area (dwFrom, dwTo) */
		else if (dwBlockStart > dwTo) 
		{
			/* partially filled with zero */
			/* This block didn't map by FileGetBlock and not uptodate */
			bPartial = TRUE;
			break;
		}

		/* make current buffer uptodate */
		LINUX_SetBufferUptodate(pBh);
		/* make current buffer dirty -> this will be flushed */
		LINUX_MarkBufferDirty(pBh);	
	} /* end of for-loop */

	/* if all buffers of a page were filled zero */
	if (bPartial != TRUE)
	{
		/* make current page uptodate */
		LINUX_SetPageUptodate(pPage);
	}

out:
	/* to ensure cache coherency btw kernel and userspace */
	LINUX_FlushDcachePage(pPage);

	return dwLinuxError;
}


/******************************************************************************/
/* NESTLE PUBLIC API : VNODE FUNCTION                                         */
/******************************************************************************/
/**
 * @brief		Set Vnode's File size
 * @param[in]	pVnode : nestle vnode
 * @param[in]	llSize : new file size
 * @returns		void
 */
void
VnodeSetSize(
	IN	PVNODE 			pVnode,
	IN	FILE_SIZE		llSize)
{
	LNX_ASSERT_ARGV(pVnode);

	LNX_VMZ(("Name: %s i_size: %lld",
			(NULL != pVnode->pstLxDentry)?
			pVnode->pstLxDentry->d_name.name: NULL,
			(LINUX_OFFSET) llSize));

	/* change inode's size */
	LINUX_InodeWriteSize(&pVnode->stLxInode, (LINUX_OFFSET) llSize);
	/* nativefs set i_blocks
	LxSetBlocks(&pVnode->stLxInode, (LINUX_OFFSET) llSize);
	*/

	/* change vnode's llMappedSize */
	VnodeSetMappedSize(pVnode, llSize);

	LNX_VMZ(("setsize: mapped size of %llx file is %lld:%s",
			pVnode->llVnodeID, pVnode->llMappedSize,
			((NULL != pVnode->pstLxDentry)?
			 pVnode->pstLxDentry->d_name.name: NULL)
			));

	return;
}

/**
 * @brief		Set Vnode's Blocks (inode's i_blocks)
 * @param[in]	pVnode : nestle vnode
 * @param[in]	llSize : blocks to set (bytes)
 * @returns		void
 * @remarks		i_blocks means how many 512-byte blocks is allocated for the vnode.
 */
void
VnodeSetBlocks(
	IN	PVNODE		pVnode,
	IN	FILE_SIZE	llSize)
{
	LNX_VMZ(("setblock(%u: %s) %ld",
				(unsigned int) pVnode->llVnodeID,
				((NULL != pVnode->pstLxDentry)?
				 pVnode->pstLxDentry->d_name.name: NULL),
				(unsigned long) llSize));
	LxSetBlocks(&pVnode->stLxInode, (LINUX_OFFSET) llSize);
}

/**
 * @brief		Get Vnode's Blocks (inode's i_blocks)
 * @param[in]	pVnode : nestle vnode
 * @returns		number of blocks belonged to vnode
 */
unsigned int
VnodeGetBlocks(
	IN	PVNODE 			pVnode)
{
	PLINUX_INODE pInode = NULL;
	unsigned int dwBlocks;

	LNX_ASSERT_ARG(pVnode, ~0U);

	pInode = &pVnode->stLxInode;

	LINUX_SpinLock(&pInode->i_lock);
	dwBlocks = pInode->i_blocks;
	LINUX_SpinUnlock(&pInode->i_lock);

	return dwBlocks;
}

/**
 * @brief		Set Vnode's user ID
 * @param[in]	pVnode : nestle vnode
 * @param[in]	dwUid : user ID
 * @returns		void
 */
inline void
VnodeSetUid(
	IN	PVNODE 			pVnode,
	IN	unsigned int	dwUid)
{
	LNX_ASSERT_ARGV(pVnode);

	pVnode->stLxInode.i_uid = dwUid;

	return;
}

/**
 * @brief		Set Vnode's group ID
 * @param[in]	pVnode : nestle vnode
 * @param[in]	dwGid : group id of vnode
 * @returns		void
 */
inline void
VnodeSetGid(
	IN	PVNODE 			pVnode,
	IN	unsigned int	dwGid)
{
	LNX_ASSERT_ARGV(pVnode);

	LNX_VMZ(("dwGid: %u(%x)", dwGid, dwGid));

	pVnode->stLxInode.i_gid = dwGid;

	return;
}

/**
 * @brief		Set Vnode's ACL (RWX Permission)
 * @param[in]	pVnode : nestle vnode
 * @param[in]	dwAcl : RWX permission (Refer to _ACL_MODE)
 * @returns		void
 */
inline	void
VnodeSetAcl(
	IN	PVNODE 			pVnode,
	IN	unsigned short	dwAcl)
{
	LINUX_MODE lxType = 0;

	LNX_ASSERT_ARGV(pVnode);

	/* clear ACL bits */
	lxType = (pVnode->stLxInode.i_mode & ~ACL_ALL);

	pVnode->stLxInode.i_mode = lxType | ((LINUX_MODE) (dwAcl & ACL_ALL));
	/* do not need to apply umask; umask is applyed for creating new one */

	return;
}

/**
 * @brief		Set Vnode's attribute (refer to _FILE_ATTR in ns_types.h)
 * @param[in]	pVnode : nestle vnode
 * @param[in]	dwAttr : attribute of vnode
 * @returns		void
 */
void
VnodeSetAttr(
	IN	PVNODE 			pVnode,
	IN	unsigned int	dwAttr)
{
	PLINUX_INODE		pInode = NULL;

	LNX_ASSERT_ARGV(pVnode);
	pInode = &pVnode->stLxInode;

	pVnode->dwAttr = dwAttr;

	/* for directory */
	if (dwAttr & FILE_ATTR_DIRECTORY) 
	{
		pInode->i_mode |= LINUX_S_IFDIR;
		pInode->i_fop = &g_stLinuxDirOps;
		pInode->i_op = &g_stLinuxDirInodeOps;
	}
	/* for symlink */
	else if (dwAttr & FILE_ATTR_LINKED)
	{
		pInode->i_mode |= LINUX_S_IFLNK;
		pInode->i_op = &g_stLinuxSymlinkInodeOps;
		pInode->i_mapping->a_ops = &g_stLinuxAddrOps;
		pInode->i_mapping->nrpages = 0;
	}
	/* for special file: socket */
	else if (dwAttr & FILE_ATTR_SOCKET)
	{
		pInode->i_mode |= LINUX_S_IFSOCK;
		pInode->i_op = &g_stLinuxSpecialInodeOps;
		LINUX_InitSpecialInode(pInode, pInode->i_mode, 0);
	}
	/* for special file: socket */
	else if (dwAttr & FILE_ATTR_FIFO)
	{
		pInode->i_mode |= LINUX_S_IFIFO;
		pInode->i_op = &g_stLinuxSpecialInodeOps;
		LINUX_InitSpecialInode(pInode, pInode->i_mode, 0);
	}
	/*
	 * the remainer for file ( FILE_ATTR_FILE = 0x00000000 )
	 */
	else 
	{
		pInode->i_mode |= LINUX_S_IFREG;
		pInode->i_fop = &g_stLinuxFileOps;
		pInode->i_op = &g_stLinuxFileInodeOps;
		pInode->i_mapping->a_ops = &g_stLinuxAddrOps;
		pInode->i_mapping->nrpages = 0;
	}

	LNX_DMZ(("Vnode 0x%llx: uid(%x), gid(%x), mode(%o)",
			(unsigned long long)pVnode->llVnodeID,
			pVnode->stLxInode.i_uid, pVnode->stLxInode.i_gid,
			pVnode->stLxInode.i_mode));

	return;
}

/**
 * @brief		Get Vnode's times
 * @param[in]	pVnode : nestle vnode
 * @param[out]	pCreateTime	create time
 * @param[out]	pAccessTime	access time 
 * @param[out]	pUpdateTime	update time 
 * @returns		void
 */
void
VnodeGetTimes(
	IN	PVNODE 			pVnode,
	OUT	PSYS_TIME		pCreateTime,
	OUT	PSYS_TIME		pAccessTime,
	OUT	PSYS_TIME		pUpdateTime)
{
	LNX_ASSERT_ARGV(pVnode);

	/* creation time: linux inode doesn't have this field */
	if (pCreateTime)
	{
		/* convert COMP_TIMESPEC to SYS_TIME */
		pVnode->dwCreationTime.dwTime -= (long)(sys_tz.tz_minuteswest * 60);
		RtlCompTimeToSysTime(&pVnode->dwCreationTime, pCreateTime);
	}

	/*
	 * time of last access
	 * : Linux updates it when file access by execve, mknod, pipe, utime, read
	 */
	if (pAccessTime)
	{
		RtlLinuxTimeToSysTime(&(pVnode->stLxInode.i_atime), pAccessTime);
	}

	/*
	 * time of last modification
	 * Linux updates it when file modification by mknod, truncate, utime, write
	 */
	if (pUpdateTime)
	{
		RtlLinuxTimeToSysTime(&(pVnode->stLxInode.i_mtime), pUpdateTime);
	}

	return;
}


/**
 * @brief		Set Vnode's time 
 * @param[in]	pVnode		nestle vnode
 * @param[in]	pCreateTime	create time
 * @param[in]	pAccessTime	access time
 * @param[in]	pUpdateTime	update time
 * @returns		void
 */
void
VnodeSetTimes(
	IN	PVNODE 			pVnode,
	IN	PSYS_TIME		pCreateTime,
	IN	PSYS_TIME		pAccessTime,
	IN	PSYS_TIME		pUpdateTime)
{
	LNX_ASSERT_ARGV(pVnode);

	/* creation time: linux inode doesn't have this field */
	if (pCreateTime)
	{
		RtlSysTimeToCompTime(pCreateTime, &pVnode->dwCreationTime);
		pVnode->stLxInode.i_ctime.tv_sec = 
				LINUX_MakeTime(pCreateTime->wYear, pCreateTime->wMonth,
							pCreateTime->wDay, pCreateTime->wHour,
							pCreateTime->wMinute, pCreateTime->wSecond);
		/* reflect timezone : nestle time -> linux time */
		pVnode->stLxInode.i_ctime.tv_sec +=
                                (long) (sys_tz.tz_minuteswest * 60);
		pVnode->stLxInode.i_ctime.tv_nsec = 0;
	}

	/* access time */
	if (pAccessTime)
	{
		pVnode->stLxInode.i_atime.tv_sec = 
				LINUX_MakeTime(pAccessTime->wYear, pAccessTime->wMonth,
							pAccessTime->wDay, pAccessTime->wHour, 
							pAccessTime->wMinute, pAccessTime->wSecond);

		/* reflect timezone : nestle time -> linux time */
		pVnode->stLxInode.i_atime.tv_sec +=
				(long) (sys_tz.tz_minuteswest * 60);

		pVnode->stLxInode.i_atime.tv_nsec = 0;
	}

	/* modification time */
	if (pUpdateTime)
	{
		pVnode->stLxInode.i_mtime.tv_sec = 
				LINUX_MakeTime(pUpdateTime->wYear, pUpdateTime->wMonth,
							pUpdateTime->wDay, pUpdateTime->wHour, 
							pUpdateTime->wMinute, pUpdateTime->wSecond);

		/* reflect timezone : nestle time -> linux time */
		pVnode->stLxInode.i_mtime.tv_sec +=
				(long) (sys_tz.tz_minuteswest * 60);

		pVnode->stLxInode.i_mtime.tv_nsec = 0;
	}
	
	return;
}

/**
 * @brief		Change Vnode's index
 * @param[in]	pVnode		nestle vnode
 * @param[in]	llVnodeID	index to be set to Vnode
 * @returns		void
 */
void
VnodeChangeIndex(
	IN	PVNODE				pVnode, 
	IN	unsigned long long	llVnodeID)
{
	PVOLUME_CONTROL_BLOCK	pVcb = NULL;	
	unsigned long dwHash;

	LNX_ASSERT_ARGV(pVnode);

	pVcb = pVnode->pVcb;

	LNX_VMZ(("Vnode %llx -> %llx is changed",
			pVnode->llVnodeID, llVnodeID));

	/*
	 * Linux Glue assumes that
	 *  this API is called by NativeFS with VnodeLock()
     * To protect remove/insert inode to hash, locks for hash outside operation
	 */
	LINUX_RemoveInodeHash(&pVnode->stLxInode);

#ifdef _4BYTE_VNODEID
	pVnode->stLxInode.i_ino = (unsigned long) llVnodeID;
#endif

	LINUX_InsertInodeHash(&pVnode->stLxInode);

	/* lock before handling hash list */
	LINUX_SpinLock(&pVcb->spinHash);

	/* remove vnode from hash list */
	LINUX_HLIST_DEL_INIT(&pVnode->hleHash);

	/* get hash value of vnode ID */
	pVnode->llVnodeID = llVnodeID;
	dwHash = VcbHash(pVcb, llVnodeID);

	/* add vnode to hash list */
    LINUX_HLIST_ADD_HEAD(&pVnode->hleHash, (pVcb->aHashTable + dwHash));

	/* unlock after handling hash list */
	LINUX_SpinUnlock(&pVcb->spinHash);

	return;
}

/**
 * @brief		fill zero the specific area of Vnode
 * @param[in]	pVnode			nestle vnode
 * @param[in]	llStartOffset	start offset
 * @param[in]	dwByteToFill	bytes to fill zero from llStartOffset
 * @param[out]	pdwBytesToFilled bytes filled with zero after operation
 * @returns		FERROR	
 */
FERROR
VnodeFillZero(
	IN  PVNODE 					pVnode,
	IN  FILE_OFFSET				llStartOffset,
	IN  unsigned int			dwByteToFill,
	OUT	unsigned int*			pdwBytesFilled)
{
	PLINUX_INODE			pInode = NULL;
	PLINUX_ADDRESS_SPACE	pMapping = NULL;
	PLINUX_PAGE				pPage = NULL;
	LINUX_OFFSET			llCurOffset, llNextOffset;
	LINUX_OFFSET			llNewSize;
	unsigned long			nPageIndex, nFinalPageIndex, nNextPageStart;
	unsigned int			dwFrom = 0, dwTo = 0;
	LINUX_ERROR				dwLinuxError = 0;

	LNX_ASSERT_ARG(pVnode, FERROR_INVALID);

	pInode = &pVnode->stLxInode;
	pMapping = pInode->i_mapping;

	/*
	 * llCurOffset	: current logical offset in the inode
	 * llNewSize	: new size of inode after filling zero
	 * nFinalPageIndex:	page index of llNewSize
	 */
	llCurOffset = llStartOffset;
	llNewSize = llStartOffset + dwByteToFill;

	nPageIndex = 0;
	nFinalPageIndex = 
		(unsigned long) ((llNewSize - 1) >> LINUX_PAGE_CACHE_SHIFT);

	LNX_IMZ(("[in] VNODE fillzero i_ino %lu %s from %x(offset %x) to %x",
				pInode->i_ino,
				(NULL != pVnode->pstLxDentry)?
				(char*)pVnode->pstLxDentry->d_name.name: "",
				(unsigned int) llCurOffset,
				(unsigned int) (llCurOffset & (LINUX_PAGE_SIZE - 1)),
				(unsigned int) llNewSize));

	while ((nPageIndex = 
				(unsigned long) (llCurOffset >> LINUX_PAGE_CACHE_SHIFT))
			<= nFinalPageIndex)
	{
		/* get a cached page by page index */
		pPage = LINUX_GrabCachePage(pMapping, nPageIndex);
		if (!pPage)
		{
			/* Allocation of new page fails */
			LNX_SMZ(("Out of memory: grab_cache_page"));

			dwLinuxError = -ENOMEM;
			goto out;
		}

		/* calculate the area's offset to be filled with zero */
		nNextPageStart = (nPageIndex + 1) << LINUX_PAGE_CACHE_SHIFT;
		llNextOffset = (llNewSize > nNextPageStart)? nNextPageStart : llNewSize;

		dwFrom = llCurOffset & (LINUX_PAGE_SIZE - 1);
		dwTo = (llNextOffset - 1) & (LINUX_PAGE_SIZE - 1);

		/* fill some area (dwFrom, dwTo) of current page with zero */
		dwLinuxError = _FillZeroPage(pInode, pPage, dwFrom, dwTo);
		if (dwLinuxError)
		{
			LINUX_ClearPageUptodate(pPage);

			/* release current page */
			LINUX_UnlockPage(pPage);
			LINUX_PageCacheRelease(pPage);

			LNX_VMZ(("zero fill failed (Linux Err : %d)", dwLinuxError));
			goto out;
		}

		llCurOffset = nNextPageStart;

		/* release current page */
		LINUX_UnlockPage(pPage);
		LINUX_PageCacheRelease(pPage);
	} /* end of for-loop */

out:

	/* calculate the number of area filled with zero */
	if (pdwBytesFilled)
	{
		/* success to fill pages with zero */
		if (nPageIndex > nFinalPageIndex)
		{
				*pdwBytesFilled = dwByteToFill;
		}
		/* fail during while-loop */
		else
		{
			/* if (nPageIndex <= nFinalPageIndex) */
			*pdwBytesFilled = llCurOffset - llStartOffset;
			/* if zero, fail during entering while-loop */
		}
	}

	LNX_IMZ(("[out] VNODE fillzero end, i_ino %lu, %d pid (%d)",
			pInode->i_ino, LINUX_g_CurTask->pid, dwLinuxError));

	/* translate Linux error into Nestle error */
	return RtlNestleError(dwLinuxError);
}


/******************************************************************************/
/* Functions for Linux VFS                                                    */
/******************************************************************************/

/**
 * @brief		remove page cache entries and invalidate inode buffers
 * @param[in]	pInode : linux inode
 * @returns		void
 * @remark		It gets called whenever the inode is evicted, whether it has
 * 			remaining links or not.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
void
VnodeEvictInode(
	IN PLINUX_INODE		pInode)
{
	PVNODE  pVnode;
	LNX_ASSERT_ARGV(pInode);

	/* sanity check for VFS */
	if (LINUX_AtomicRead(&pInode->i_count) != 0)
	{
		LNX_CMZ(("i_count is not zero"));
		RFS_ASSERT(0);
	}

	LNX_IMZ(("[in] VNODE deleteinode i_ino %lu %d pid", pInode->i_ino, LINUX_g_CurTask->pid));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
	LINUX_TruncInodePages(&pInode->i_data, 0);
#endif
	/* delete node only in case of i_nlink is 0 */
	if (!pInode->i_nlink) {
		/* check for badness of inode */
		if (!LINUX_IsBadInode(pInode)) 
		{
			pVnode = VnodeGetVnodeFromInode(pInode);

			LNX_ASSERT_ARGV(pVnode->pVnodeOps);

			LNX_DMZ(("Delete Inode for i_ino(%lu), vnodeID(%lu), %d pid",
						(unsigned long) pInode->i_ino, (unsigned long)pVnode->llVnodeID,
						LINUX_g_CurTask->pid));

			/*
			 *  BTFS doesn't map this function for directory.
			 */

			if (!LINUX_IS_DIR(pInode->i_mode) &&
					(NULL == pVnode->pVnodeOps->pfnDeleteNode))
			{
				LNX_CMZ(("No Native interface for DeleteNode"));
				goto out;	// -ENOSYS
			}

			LNX_VMZ(("delete_inode (%s) %s (nlink %d count %d d_count %d)",
						(LINUX_IS_DIR(pInode->i_mode)? "Dir":"File"),
						((NULL != pVnode->pstLxDentry)?
						pVnode->pstLxDentry->d_name.name: NULL),
						pInode->i_nlink,
						LINUX_AtomicRead(&pInode->i_count),
						((NULL != pVnode->pstLxDentry)?
						 LINUX_AtomicRead(&pVnode->pstLxDentry->d_count): -99)
						));

			/* call native operation */
			if (pVnode->pVnodeOps->pfnDeleteNode) 
			{
				FERROR nErr = 0;

				/* delete native node */
				nErr = pVnode->pVnodeOps->pfnDeleteNode(pVnode);
				if (nErr != FERROR_NO_ERROR)
				{
					LNX_EMZ(("NativeFS DeleteNode fails (nErr: 0x%08x): %s",
							-nErr,
						((NULL != pVnode->pstLxDentry)?
						 pVnode->pstLxDentry->d_name.name: NULL)));
				}
				else
				{
					// verbose (noisy)
					LNX_VMZ(("NativeFS DeleteNode succeeds : (%s) %s (nlink %d)",
						(LINUX_IS_DIR(pInode->i_mode)? "Dir":"File"),
						((NULL != pVnode->pstLxDentry)?
						pVnode->pstLxDentry->d_name.name: NULL),
						pInode->i_nlink));
				}
			}
		}
		else
		/* bad inode */
		{
			LNX_VMZ(("DeleteNode for Bad Inode is not supported"));
		}
	}

out:
	/* invalidate the inode buffers clear */
	invalidate_inode_buffers(pInode);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	clear_inode(pInode);
#else
	/* end_writeback instead of clear inode */
	end_writeback(pInode);
#endif

        /* skip bad inode */
        if (LINUX_IsBadInode(pInode))
        {
                return;
        }

        /* get vnode */
        pVnode = VnodeGetVnodeFromInode(pInode);
        LNX_ASSERT_ARGV(pVnode->pVcb);

        /* just return if root vnode */
        if (pVnode->pVcb->pRoot == pVnode)
        {
                return;
        }

	/* release memory for Native node */
        if (pVnode->pVnodeOps && pVnode->pVnodeOps->pfnClearNode)
        {
                FERROR nErr;

                /* call native operation(ClearNode) */
                nErr = pVnode->pVnodeOps->pfnClearNode(pVnode);
                if (nErr != FERROR_NO_ERROR)
                {
                        LNX_EMZ(("NativeFS ClearNode fails(nErr: 0x%08x)", -nErr));
                }
        }
        else
        {
                LNX_CMZ(("No Native operation for clearing Node"));
                RFS_ASSERT(0);
        }

	/* remove vnode from hash list */
	VcbRemoveHash(pVnode->pVcb, pVnode);

	LNX_IMZ(("[out] VNODE evictinode end, i_ino %lu %d pid",
				pInode->i_ino, LINUX_g_CurTask->pid));
	return;
}
#else
/**
 * @brief		deallocate blocks of inode and clear inode (super_operations: delete_inode)
 * @param[in]	pInode : linux inode
 * @returns		void
 * @remark		It will be invoked at the last iput if i_nlink is zero
 */
void
VnodeDeleteInode(
	IN PLINUX_INODE		pInode)
{

	LNX_ASSERT_ARGV(pInode);

	/* sanity check for VFS */
	if (LINUX_AtomicRead(&pInode->i_count) != 0)
	{
		LNX_CMZ(("i_count is not zero"));
		RFS_ASSERT(0);
	}

	LNX_IMZ(("[in] VNODE deleteinode i_ino %lu %d pid", pInode->i_ino, LINUX_g_CurTask->pid));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
	LINUX_TruncInodePages(&pInode->i_data, 0);
#endif

	/* check for badness of inode */
	if (!LINUX_IsBadInode(pInode)) 
	{
		PVNODE  pVnode = VnodeGetVnodeFromInode(pInode);

		LNX_ASSERT_ARGV(pVnode->pVnodeOps);

		LNX_DMZ(("Delete Inode for i_ino(%lu), vnodeID(%lu), %d pid",
					(unsigned long) pInode->i_ino, (unsigned long)pVnode->llVnodeID,
					LINUX_g_CurTask->pid));

		/*
		 *  BTFS doesn't map this function for directory.
		 */

		if (!LINUX_IS_DIR(pInode->i_mode) &&
				(NULL == pVnode->pVnodeOps->pfnDeleteNode))
		{
			LNX_CMZ(("No Native interface for DeleteNode"));
			goto out;	// -ENOSYS
		}

		LNX_VMZ(("delete_inode (%s) %s (nlink %d count %d d_count %d)",
						(LINUX_IS_DIR(pInode->i_mode)? "Dir":"File"),
						((NULL != pVnode->pstLxDentry)?
						pVnode->pstLxDentry->d_name.name: NULL),
						pInode->i_nlink,
						LINUX_AtomicRead(&pInode->i_count),
						((NULL != pVnode->pstLxDentry)?
						 LINUX_AtomicRead(&pVnode->pstLxDentry->d_count): -99)
						));

		/* call native operation */
		if (pVnode->pVnodeOps->pfnDeleteNode) 
		{
			FERROR nErr = 0;

			/* delete native node */
			nErr = pVnode->pVnodeOps->pfnDeleteNode(pVnode);
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("NativeFS DeleteNode fails (nErr: 0x%08x): %s",
						-nErr,
						((NULL != pVnode->pstLxDentry)?
						 pVnode->pstLxDentry->d_name.name: NULL)));
			}
			else
			{
				// verbose (noisy)
				LNX_VMZ(("NativeFS DeleteNode succeeds : (%s) %s (nlink %d)",
						(LINUX_IS_DIR(pInode->i_mode)? "Dir":"File"),
						((NULL != pVnode->pstLxDentry)?
						pVnode->pstLxDentry->d_name.name: NULL),
						pInode->i_nlink));
			}
		}
	}
	else
	/* bad inode */
	{
		LNX_VMZ(("DeleteNode for Bad Inode is not supported"));
	}

out:
	/* make inode clear */
	LINUX_ClearInode(pInode);

	LNX_IMZ(("[out] VNODE deleteinode end, i_ino %lu %d pid",
				pInode->i_ino, LINUX_g_CurTask->pid));

	return;
}

/**
 * @brief		clear an inode and remove it from hash
 * @param[in]	pInode : linux inode
 * @returns		void
 */
void
VnodeClearInode(
	IN PLINUX_INODE		pInode)
{
	PVNODE  pVnode = NULL;

	LNX_ASSERT_ARGV(pInode);

	/* skip bad inode */
	if (LINUX_IsBadInode(pInode)) 
	{
		return;
	}

	/* 
	 * Filesystem's XXX_delete_inode() calls clear_inode()
	 * to notify Linux VFS that the inode is no longer useful.
	 * After clear_inode(), destroy_inode() is called by Linux VFS.
	 * RFS_clear_inode() just remove unused inode from hashtable.
	 * RFS_destory_inode() releases allocated memory for Vnode & native Node.
	 */

	/* get vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARGV(pVnode->pVcb);

	/* just return if root vnode */
	if (pVnode->pVcb->pRoot == pVnode)
	{
		return;
	}

	/* release memory for Native node */
	if (pVnode->pVnodeOps && pVnode->pVnodeOps->pfnClearNode)
	{
		FERROR nErr;

		/* call native operation(ClearNode) */
		nErr = pVnode->pVnodeOps->pfnClearNode(pVnode);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_EMZ(("NativeFS ClearNode fails(nErr: 0x%08x)", -nErr));
		}	
	}
	else
	{
		LNX_CMZ(("No Native operation for clearing Node"));
		RFS_ASSERT(0);
	}

	/* remove vnode from hash list */
	VcbRemoveHash(pVnode->pVcb, pVnode);

	return;
}
#endif
/**
 * @brief		write inode
 * @param[in]	pInode : linux inode
 * @param[in]	dwWait : flush option
 * @returns		return 0 on success, errno on failure	
 */
LINUX_ERROR
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
VnodeWriteInode(
	IN PLINUX_INODE		pInode,
	IN PLINUX_WRITEBACK     pWriteback)
#else
VnodeWriteInode(
	IN PLINUX_INODE		pInode,
	IN int				dwWait)
#endif
{
	PVNODE			pVnode = NULL;
	FERROR			nErr = FERROR_NO_ERROR;
	LINUX_OFFSET	i_size = 0;

	LNX_ASSERT_ARG(pInode, -EINVAL);

	/* task is allocating memory */
	if (LINUX_g_CurTask->flags & LINUX_PF_MEMALLOC)	
	{
		LNX_VMZ(("not permitted for mem-allocating task"));

		/* skip write_inode */
		return 0;
	}

	/* read-only mode */
	if (LINUX_IS_RDONLY(pInode))
	{
		LNX_VMZ(("not permitted for read-only inode"));

		/* skip write_inode */
		return 0;
	}

	/* get vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	LNX_ASSERT_ARG(pVnode, -EFAULT);

	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnSyncFile))
	{
		LNX_CMZ(("No Native interface for write inode"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/*
	 * Flush dirty data and metadata regardless of dwWait
	 * The proper procedure is to mark the buffers dirty if dwWait is not set.
	 */

	/* check for removed inode */
	if (pInode->i_nlink == 0)
	{
		LNX_VMZ(("not permitted for deleted inode"));
	
		/* skip write_inode */
		return 0;
	}

	i_size = LINUX_InodeReadSize(pInode);

	LNX_IMZ(("[in] VNODE writeinode i_ino %lu size %Lu %s",
				pInode->i_ino,
				LINUX_InodeReadSize(pInode),
				(NULL != pVnode->pstLxDentry)?
				(char*)pVnode->pstLxDentry->d_name.name: ""));

	/* 20091026
	 *	(The policy of user data is changed. Not guarantee -> Guarantee)
	 *
	 * To guarantee that user data is flushed before syncing metadata,
	 * you should confirms I/O result here.
	 */
	if (pInode->i_mapping->nrpages)
	{
		int nLinuxError = 0;

		nLinuxError = LINUX_FilemapDataWrite(pInode->i_mapping);
		if (nLinuxError == 0)
		{
			nLinuxError = LINUX_FilemapDataWait(pInode->i_mapping);
		}

		if (nLinuxError)
		{
			LNX_EMZ(("Filemap_fdatawrite/wait %s fails(ino: %lu): err %d",
						(NULL != pVnode->pstLxDentry)?
						(char*)pVnode->pstLxDentry->d_name.name: "",
						pInode->i_ino, nLinuxError));

			LNX_IMZ(("[out] VNODE write inode end %d pid (%d)", LINUX_g_CurTask->pid, nErr));
			return nLinuxError;
		}
	}

	/* call nativeFS opeartion */
	/* sync dirty buffer of file and dirty inode and update mtime */
	nErr = pVnode->pVnodeOps->pfnSyncFile(pVnode,
										i_size,
										(LINUX_IS_NOATIME(pInode)? FALSE: TRUE),
										TRUE);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS SyncFile %s fails (%lu, nErr: 0x%08x)",
								((NULL != pVnode->pstLxDentry)?
								 pVnode->pstLxDentry->d_name.name: NULL),
								pInode->i_ino, -nErr));
	}

	LNX_IMZ(("[out] VNODE write inode end %d pid (%d)", LINUX_g_CurTask->pid, nErr));

	return RtlLinuxError(nErr);
}

/******************************************************************************/
/* Functions for Linux Glue internals                                         */
/******************************************************************************/

/**
 * @brief		initialize the Vnode
 * @param[io]	pVnode	Vnode
 * @param[in]	llVnodeID	unique Index
 * @param[in]	pVcb		Volume control block pointer
 * @param[in]	pFileOps	File Operation
 * @param[in]	pVnodeOps	Node Operation
 * @returns		void
 */
void
VnodeInitialize(
	IN	PVNODE					pVnode,
	IN	unsigned long long		llVnodeID,
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	PVNODE					pParentVnode,
	IN	void*					pFileOps,
	IN	void*					pVnodeOps)
{
	PLINUX_INODE		pInode;

	LNX_ASSERT_ARGV(pVnode);
	LNX_ASSERT_ARGV(pVcb);
	LNX_ASSERT_ARGV(pFileOps);
	LNX_ASSERT_ARGV(pVnodeOps);

	/*
	 * initialize vnode
	 */
	pVnode->pVcb					= pVcb;
	pVnode->llVnodeID				= llVnodeID;
	pVnode->pVnodeOps				= pVnodeOps;
	pVnode->pFileOps				= pFileOps;
	pVnode->pNative					= NULL;
	pVnode->dwCreationTime.dwDate	= 0;
	pVnode->dwCreationTime.dwTime	= 0;

	pVnode->pstLxDentry				= NULL;

#ifdef CONFIG_RFS_FS_POSIX_ACL
	pVnode->pstLxAcl				= NULL;
	pVnode->pstLxAclDefault			= NULL;
#endif


	/* initialize semaphore for protecting vnode meta */
	LINUX_SemaInit(&pVnode->bsVnodeMetaSem, 1);

	/*
	 * initialize inode
	 */
	pInode = &pVnode->stLxInode;

	/* assign inode number to inode */
	if (llVnodeID == (unsigned long long) RFS_ROOT_INO)
	{
		pInode->i_ino = (unsigned long) RFS_ROOT_INO;
	}
	else
	{
#ifdef _4BYTE_VNODEID
		pInode->i_ino = (unsigned long) llVnodeID;
#else
		pInode->i_ino = (unsigned long) LINUX_Iunique(pVcb->pSb, RFS_ROOT_INO);
#endif
	}

	pInode->i_version				= 0;
	pInode->i_ctime = pInode->i_mtime = pInode->i_atime = LINUX_CURRENT_TIME;
	
	pInode->i_blkbits				= (unsigned int) pInode->i_sb->s_blocksize_bits;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
	pInode->i_blksize				= pInode->i_sb->s_blocksize;
#endif 

	/*
	 * Set default mode, gid, uid (temporal value).
	 * If this is called by NativeFS,
	 * NativeFS will re-set i_mode, i_uid, i_gid as the entry is stored.
	 */
	pInode->i_mode					= 0777;
	pInode->i_uid					= LINUX_g_CurFsUid;
	/* check parent's S_ISGID bit (set-group-ID bit) */
	if (pParentVnode && (pParentVnode->stLxInode.i_mode & LINUX_S_ISGID))
	{
		/* inherit parent's group ID */
		pInode->i_gid					= pParentVnode->stLxInode.i_gid;
	}
	else
	{
		/* use current process's group ID */
		pInode->i_gid					= LINUX_g_CurFsGid;
	}

	/* insert vnode and inode to hash list */
	VcbInsertHash(pVcb, pVnode);
	LINUX_InsertInodeHash(pInode);

	LNX_VMZ(("Vnode %llx is created",
			(unsigned long long)llVnodeID));
	LNX_VMZ(("\tuid(%x), gid(%x), mode(%o)",
			pInode->i_uid, pInode->i_gid, pInode->i_mode));

	return;
}

/**
 * @brief		create a new inode and return vnode containing the inode
 * @params[in]	pVcb	volume control
 * @returns		PVNODE
 */
PVNODE
VnodeCreate(
	IN	PVOLUME_CONTROL_BLOCK	pVcb)
{
	PLINUX_INODE	pInode = NULL;
	PLINUX_SUPER	pSb;

	LNX_ASSERT_ARG(pVcb, NULL);

	pSb = (struct super_block*) pVcb->pSb;
	if (pSb == NULL)
	{
		LNX_CMZ(("superblock is not mapped to vcb"));
		return NULL;
	}

	/* allocate new inode */
	pInode = LINUX_NewInode(pSb);
	if (!pInode)
	{
		LNX_EMZ(("fail to allocate new inode"));
		return NULL;
	}

	/* return vnode of inode */
	return VnodeGetVnodeFromInode(pInode);
}

/**
 * @brief		Mark vnode bad in case of error after creating Vnode
 * @param[in]	pVnode : nestle vnode
 * @returns		void
 */
void 
VnodeMakeBad(
	IN PVNODE			pVnode)
{
	LNX_ASSERT_ARGV(pVnode);

	VcbRemoveHash(pVnode->pVcb, pVnode);
	LINUX_MakeBadInode(&pVnode->stLxInode);
	LINUX_Iput(&pVnode->stLxInode);
	return;
}
/*
 * Define Symbols
 */
#include <linux/module.h>

EXPORT_SYMBOL(VnodeGetIndex);
EXPORT_SYMBOL(VnodeChangeIndex);
EXPORT_SYMBOL(VnodeGetUid);
EXPORT_SYMBOL(VnodeSetUid);
EXPORT_SYMBOL(VnodeGetGid);
EXPORT_SYMBOL(VnodeSetGid);
EXPORT_SYMBOL(VnodeGetSize);
EXPORT_SYMBOL(VnodeSetSize);
EXPORT_SYMBOL(VnodeGetBlocks);
EXPORT_SYMBOL(VnodeSetBlocks);
EXPORT_SYMBOL(VnodeGetAttr);
EXPORT_SYMBOL(VnodeSetAttr);
EXPORT_SYMBOL(VnodeIsInitCluster);
EXPORT_SYMBOL(VnodeGetLinkCnt);
EXPORT_SYMBOL(VnodeSetLinkCnt);
EXPORT_SYMBOL(VnodeGetAcl);
EXPORT_SYMBOL(VnodeSetAcl);
EXPORT_SYMBOL(VnodeGetTimes);
EXPORT_SYMBOL(VnodeSetTimes);
EXPORT_SYMBOL(VnodeGetVcb);
EXPORT_SYMBOL(VnodeGetNative);
EXPORT_SYMBOL(VnodeSetNative);
EXPORT_SYMBOL(VnodeMarkMetaDirty);
EXPORT_SYMBOL(VnodeMarkDataDirty);
EXPORT_SYMBOL(VnodeFillZero);

// end of file
