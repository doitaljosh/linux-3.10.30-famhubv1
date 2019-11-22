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
 * @file        linux_bcache.c
 * @brief       This file includes APIs to NativeFS for accessing buffer cache.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_bcache.h"
#include "linux_vcb.h"
#include "linux_vnode.h"

#include <linux/bio.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_BCACHE)


/******************************************************************************/
/* Internal BCM Functions                                                     */
/******************************************************************************/
/**
 * get(read) blocks 
 * @param pVcb			VCB pointer 
 * @param dwFlag		flag for read or write
 * @param dwStartBlock	start block number
 * @param dwBlockCount	numof blocks to get
 * @param pBHs			array pointer of buffer head to get blocks
 * @return				FERROR_NO_ERROR on success
 */
static inline FERROR
_BcmGetBlocks(
	IN  PVOLUME_CONTROL_BLOCK       pVcb,
	IN	int				dwFlag,
	IN  unsigned int	dwStartBlock,
	IN  unsigned int	dwBlockCount,
	OUT PLINUX_BUF*		pBHs)
{
	PLINUX_SUPER	pSb = NULL;
	FERROR			nErr = FERROR_NO_ERROR;
	unsigned int	nCurBlock = 0;
	int 			nIndex = 0;

	LNX_ASSERT_ARG(pVcb && pVcb->pSb && pBHs, FERROR_INVALID);
	LNX_ASSERT_ARG(((dwFlag == BCM_OP_READ) || (dwFlag == BCM_OP_WRITE)),
			FERROR_INVALID);

	pSb = pVcb->pSb;

	/* sanity check */
	{
		LINUX_SECTOR	nMaxSector;

		nMaxSector = pSb->s_bdev->bd_inode->i_size >> 9;
		if (nMaxSector)
		{
			LINUX_SECTOR nSector = dwStartBlock << 
				(pSb->s_bdev->bd_inode->i_blkbits - 9);
			unsigned int dwNrSector = dwBlockCount <<
				(pSb->s_bdev->bd_inode->i_blkbits - 9);

			if ((nMaxSector < dwNrSector) ||
					(nMaxSector - dwNrSector < nSector))
			{
				LNX_CMZ(("attempt to access beyond eod : (%u, %u)\n\t\t"
							"MaxSector(%lu), Req.Sector(%lu, %u)",
							dwStartBlock, dwBlockCount,
							(unsigned long) nMaxSector, (unsigned long) nSector, dwNrSector));

				RFS_ASSERT(0);
				return FERROR_SYSTEM_PANIC;
			}
		}
	}

	/* read buffers if BCM_OP_READ, or get buffers if BCM_OP_WRITE */
	for (nCurBlock = dwStartBlock;
			nCurBlock < (dwStartBlock + dwBlockCount);
			nCurBlock++, nIndex++)
	{
		if (dwFlag == BCM_OP_READ)
		{
			/* read blocks */
			pBHs[nIndex] = LINUX_SbBread(pSb, nCurBlock);
		}
		else /* if (dwFlag == BCM_OP_WRITE) */
		{
			pBHs[nIndex] = LINUX_SbGetblk(pSb, nCurBlock);
		}

		if (pBHs[nIndex] == NULL)
		{
			/* critical bugs message for read fail*/
			LNX_CMZ(("Fail to read buffer.(blocknr: %u)", nCurBlock));

			nErr = FERROR_IO_ERROR;
			goto release_bhs;
		}
	}

	return nErr;

release_bhs:
	LNX_SMZ(("Fail to read buffer head. Release allcated bhs from %d", nIndex));

	/* release blocks */
	for (--nIndex; nIndex >= 0; nIndex--)
	{
		LINUX_Brelse(pBHs[nIndex]);
		pBHs[nIndex] = NULL;
	}

	return nErr;
}

/******************************************************************************/
/* NESTLE PUBLIC BCM FUNCTION                                                 */
/******************************************************************************/
/**
* @brief		get blocks and copy data to buffer of caller
* @param[in]	pVcb			pointer of vcb
* @param[in]	pVnode			pointer of vnode 
* @param[in]	dwStartBlock	start block number
* @param[in]	dwBlockCount	blcok count
* @param[in]	pBuff			buffer pointer
* @param[in]	dwFlag			operation flag 
* @returns		FERROR_NO_ERROR
* @remarks		
* 
*/
FERROR
BcmReadBuffer(
	IN  PVOLUME_CONTROL_BLOCK	pVcb,
	IN  PVNODE				pVnode,
	IN  unsigned int		dwStartBlock,
	IN  unsigned int		dwBlockCount,
	OUT char*				pBuff,
	IN  BCM_OP_FLAG			dwFlag)
{
	unsigned int	dwBlockSize = 0;
	PLINUX_BUF		pstOneBH;
	PLINUX_BUF 		*pBHs = NULL;
	FERROR 			nErr = FERROR_NO_ERROR;
	int				nIndex = 0;
	unsigned char 	*pCurBuff = pBuff;

	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pBuff, FERROR_INVALID);
	LNX_ASSERT_ARG(dwBlockCount, FERROR_INVALID);

	dwBlockSize = VcbGetBlockSize(pVcb);

	LNX_IMZ(("[in] BcmReadBuffer (%u, %u) pBuff: %p",
			dwStartBlock, dwBlockCount, pBuff));

	/* fixed 20090120
     * If dwBlockCount is one, BCM don't need malloc()
     * Calling malloc() depends on number of count to read
     */
	if (dwBlockCount == 1)
	{
		pBHs = &pstOneBH;
	}
	else
	{
		/* Allocate array of buffer_head's pointer */
		pBHs = (PLINUX_BUF *)
			LINUX_Kmalloc((sizeof(PLINUX_BUF) * dwBlockCount), LINUX_GFP_NOFS);
		if (NULL == pBHs)
		{
			LNX_CMZ(("Fail to allocate memory(kmalloc)"));
			return FERROR_INSUFFICIENT_MEMORY;
		}
	}

	LINUX_Memset(pBHs, 0, (sizeof(PLINUX_BUF) * dwBlockCount));

	/* Read Blocks to Linux Buffer cache */
	nErr = _BcmGetBlocks(pVcb, BCM_OP_READ, dwStartBlock, dwBlockCount, pBHs);
	if (nErr)
	{
		LNX_CMZ(("Fail to get blocks(%u, %u)", dwStartBlock, dwBlockCount));
		goto out;
	}

	/* Copy data from Linux Buffer cache to caller's Buffer */
	for (nIndex = 0; nIndex < dwBlockCount; nIndex++)
	{
		LINUX_Memcpy(pCurBuff, (char *)(pBHs[nIndex]->b_data), dwBlockSize);
		pCurBuff += dwBlockSize;

		LINUX_Brelse(pBHs[nIndex]);
	}

out:
	/* Release array of buffer_head's pointer */
	if ((dwBlockCount > 1) && pBHs)
		LINUX_Kfree(pBHs);

	return nErr;
}

/**
* @brief		write caller's buffer to I/O blocks
* @param[in]	pVcb			pointer of vcb
* @param[in]	pVnode			pointer of vnode 
* @param[in]	dwStartBlock	start block number
* @param[in]	dwBlockCount	blcok count
* @param[in]	pBuff			buffer pointer
* @param[in]	dwFlag			operation flag 
* @returns		FERROR_NO_ERROR
* @remarks		
* 
*/
FERROR
BcmWriteBuffer(
	IN  PVOLUME_CONTROL_BLOCK	pVcb,
	IN  PVNODE				pVnode,	/* WARN!! it could be NULL */
	IN  unsigned int		dwStartBlock,
	IN  unsigned int		dwBlockCount,
	IN  char*				pBuff,
	IN  BCM_OP_FLAG			dwFlag)
{
	unsigned int	dwBlockSize = 0;
	PLINUX_BUF 		*pBHs = NULL;
	FERROR 			nErr = FERROR_NO_ERROR;
	int				nIndex = 0;
	unsigned char 	*pCurBuff = pBuff;

	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pBuff, FERROR_INVALID);

	/*
	 * 1. get blocks
	 * 2. copy pBuff -> bh
	 * 3. write blocks
	 */

	LNX_IMZ(("[in] BcmWriteBuffer (%u, %u) pBuff: %p",
			dwStartBlock, dwBlockCount, pCurBuff));

	dwBlockSize = VcbGetBlockSize(pVcb);

	/* Allocate array of buffer_head's pointer */
	pBHs = (PLINUX_BUF *) LINUX_Kmalloc((sizeof(PLINUX_BUF) * dwBlockCount),
				LINUX_GFP_NOFS);
	if (NULL == pBHs)
	{
		LNX_CMZ(("Fail to allocate memory(kmalloc)"));
		return FERROR_INSUFFICIENT_MEMORY;
	}

	LINUX_Memset(pBHs, 0, (sizeof(PLINUX_BUF) * dwBlockCount));

	/* fixed 20090120
	 *  Because Nativefs handles partial write for block,
     *  it's unnecessary to read blocks before writing 
     */  
	/* Get Blocks to Linux Buffer cache */
	nErr = _BcmGetBlocks(pVcb, BCM_OP_WRITE, dwStartBlock, dwBlockCount, pBHs);
	if (nErr)
	{
		/* Fail to get blocks */
		LNX_CMZ(("Fail to get blocks(%u, %u)", dwStartBlock, dwBlockCount));

		goto out;
	}

	/* Copy data from caller's Buffer to Linux Buffer cache */
	for (nIndex = 0; nIndex < dwBlockCount; nIndex++)
	{
		LNX_VMZ(("pBHs[%d]: %p, pBHs[]->b_data: %p, pCurBuff: %p",
				nIndex, pBHs[nIndex],
				((pBHs[nIndex] != NULL)? pBHs[nIndex]->b_data: 0),
				pCurBuff));

		/* lock buffer before writing bh */
		LINUX_LockBuffer(pBHs[nIndex]);

		LINUX_Memcpy((char *)(pBHs[nIndex]->b_data), pCurBuff, dwBlockSize);
		pCurBuff += dwBlockSize;

		/* before mark_buffer_dirty, buffer should be up-to-date */
		LINUX_SetBufferUptodate(pBHs[nIndex]);

		/* unlock buffer after marking up-to-date */
		LINUX_UnlockBuffer(pBHs[nIndex]);
		LINUX_MarkBufferDirty(pBHs[nIndex]);

		/* TODO
		 * add dlist to Vnode
		 */
	}

	/*
	 * write through mode
	 *  : NativeFS(BTFS) use this function with write-through mode
	 *    for update metadata only
	 */
	if (dwFlag & BCM_OP_SYNC)
	{
		/* write out dirty buffer and wait upon in-progress I/O */

		/* Synchrounous Write
		 * If flag is WRITE_SYNC for bh,
		 * __make_request() calls __generic_unplug_device().
		 * 
		 * SideEffect: SyncWrite makes frequent small write for STL write.
         * From linux 2.6.27, it make the same effect to use
         * ll_rw_block(SWRITE_SYNC,,)
		 */
		for (nIndex = 0; nIndex < dwBlockCount; nIndex++)
		{
			PLINUX_BUF pBh = pBHs[nIndex];

			LINUX_LockBuffer(pBh);
			LINUX_ClearBufferDirty(pBh);
			pBh->b_end_io = LINUX_EndBufferWriteSync;
			LINUX_GetBH(pBh);
			LINUX_SubmitBH(LINUX_WRITE_SYNC, pBh);
		}

		for (nIndex = 0; nIndex < dwBlockCount; nIndex++)
		{
			LINUX_WaitOnBuffer(pBHs[nIndex]);
			if (!LINUX_IsBufferUptodate(pBHs[nIndex]))
			{
				/* Write Fail in D/D */
				LNX_CMZ(("Fail to sync dirty buffer (%lu)",
						(unsigned long) pBHs[nIndex]->b_blocknr));

				nErr = FERROR_IO_ERROR;

				/* don't stop this loop until the check is over */
			}
		}
	}

	/* release buffer head */
	for (nIndex = 0; nIndex < dwBlockCount; nIndex++)
	{
		LINUX_Brelse(pBHs[nIndex]);
	}

out:
	/* Release allocated memory for the array of buffer_head's pointer */
	LINUX_Kfree(pBHs);

	return nErr;
}


/**
  * @brief		write caller's buffer to I/O blocks (use vector interface)
  * @param[in]	pVcb		pointer of vcb
  * @param[in]	pVnode	pointer of vnode
  * @param[in]	pVI		vector IO information
  * @param[in]	dwFlag	operation flag
  * @returns	FERROR_NO_ERROR
  * @remarks
  *
 */
FERROR
BcmWriteBufferV(
	IN PVOLUME_CONTROL_BLOCK	pVcb,
	IN PVNODE			pVnode,
	IO PVECTOR_INFO		pVI,
	IN BCM_OP_FLAG		dwFlag)
{
	PVECTOR_ENTRY	pCurVE = NULL;
	FERROR 			nErr = FERROR_NO_ERROR;
	unsigned int	nNumEntry = 0;

	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVI, FERROR_INVALID);

	nNumEntry = pVI->dwValidEntryCount;
	pCurVE = pVI->pVE;


	for ( ; ((nNumEntry > 0) && (pCurVE != NULL)) ; nNumEntry--, pCurVE++)
	{
		nErr = BcmWriteBuffer(pVcb,
							pVnode,
							pCurVE->dwBlock,
							pCurVE->dwCount,
							pCurVE->pBuff,
							dwFlag);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_CMZ(("Fail to Write buffer (%u, %u)",
					pCurVE->dwBlock, pCurVE->dwCount));
			break;
		}
	}

	/*
	 * sanity check
	 * check whether numof real vector entries and input parameter is the same
	 */
	if ((nErr == FERROR_NO_ERROR) && (nNumEntry != 0) && (pCurVE == NULL))
	{
		LNX_CMZ(("Fail to reach the end of the Vector entry's list"));
		RFS_ASSERT(0);
		nErr = FERROR_FILESYSTEM_CORRUPT;
	}

	return nErr;
}


/**
 * @brief		read blocks (use vector interface)
 * @param[in]	pVcb		pointer of vcb
 * @param[in]	pVnode		pointer of vnode
 * @param[in]	pVI			vector IO information
 * @param[in]	dwFlag		operation flag
 * @returns		FERROR_NO_ERROR
 * @remarks
 *
 */
FERROR
BcmReadBufferV(
	IN PVOLUME_CONTROL_BLOCK	pVcb,
	IN PVNODE			pVnode,
	IO PVECTOR_INFO		pVI,
	IN BCM_OP_FLAG		dwFlag)
{
	PVECTOR_ENTRY	pCurVE = NULL;
	FERROR 			nErr = FERROR_NO_ERROR;
	unsigned int	nNumEntry = 0;

	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVI, FERROR_INVALID);

	nNumEntry = pVI->dwValidEntryCount;
	pCurVE = pVI->pVE;


	for ( ; (nNumEntry > 0) && pCurVE != NULL; nNumEntry--, pCurVE++)
	{
		nErr = BcmReadBuffer(pVcb,
							pVnode,
							pCurVE->dwBlock,
							pCurVE->dwCount,
							pCurVE->pBuff,
							dwFlag);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_CMZ(("Fail to Read buffer (%u, %u)",
					pCurVE->dwBlock, pCurVE->dwCount));
			break;
		}
	}

	/*
	 * sanity check
	 * check whether numof real vector entries and input parameter is the same
	 */
	if ((nErr == FERROR_NO_ERROR) && (nNumEntry > 0) && (pCurVE == NULL))
	{
		LNX_CMZ(("Fail to reach the end of the Vector entry's list"));
		RFS_ASSERT(0);
		nErr = FERROR_FILESYSTEM_CORRUPT;
	}

	return nErr;
}



/**
* @brief		sync buffer
* @param[in]	pVcb		pointer of vcb
* @param[in]	pVnode		pointer of vnode
* @returns		FERROR_NO_ERROR
* @remarks		
* 
*/
FERROR
BcmSyncBuffer(
	IN PVOLUME_CONTROL_BLOCK	pVcb,
	IN PVNODE				pVnode)
{
    /* fixed 20090119
     * This function is not used in linux OS.
     */

	return FERROR_NO_ERROR;
}

/**
 * @brief		discard buffer
 *				find entry and discard in pVnode list.
 *				If entry is only partially included discard range, don't discard entry but flush entry
 * It would be discard a whole entry in vnode by "BCM_OP_ALL" flag
 * @param[in]	pVcb			pointer of vcb
 * @param[in]	pVnode			pointer of vnode 
 * @param[in]	dwStartBlock	start block number
 * @param[in]	dwBlockCount	block count
 * @param[in]	dwFlag			operation flag 
 * @returns		FERROR_NO_ERROR
 * @remarks		
 * 
 */
FERROR
BcmDiscardBuffer(
		IN PVOLUME_CONTROL_BLOCK	pVcb,
		IN PVNODE				pVnode,
		IN unsigned int			dwStartBlock,
		IN unsigned int			dwBlockCount,
		IN BCM_OP_FLAG		dwFlag)
{

     /* This function is not used in linux OS.
	 * Current doesn't call this function.
	 */

	return FERROR_NO_ERROR;
}


/*
 * Define symbols
 */
#include <linux/module.h>

/*
 * The following is macro defined in ns_nativefs.h
 * These operations can be used by NativeFS and so define EXPORT_SYMBOL 
	#define NS_ReadBuffer           BcmReadBuffer
	#define NS_ReadBufferV          BcmReadBufferV
	#define NS_WriteBuffer          BcmWriteBuffer
	#define NS_WriteBufferV         BcmWriteBufferV
	#define NS_SyncBuffer           BcmSyncBuffer
	#define NS_DiscardBuffer        BcmDiscardBuffer
*/

EXPORT_SYMBOL(BcmWriteBuffer);
EXPORT_SYMBOL(BcmReadBuffer);
EXPORT_SYMBOL(BcmSyncBuffer);
EXPORT_SYMBOL(BcmDiscardBuffer);
EXPORT_SYMBOL(BcmReadBufferV);
EXPORT_SYMBOL(BcmWriteBufferV);

// end of file
