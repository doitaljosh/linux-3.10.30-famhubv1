/*
 * BTFS(Beyond The FAT fileSystem) Developed by Flash Software Group.
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
 * @file		ffat_dir.c
 * @brief		Thils file implements dir module
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

// includes
#include "ess_types.h"
#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_common.h"
#include "ffat_misc.h"

#include "ffat_vol.h"
#include "ffat_node.h"
#include "ffat_main.h"
#include "ffat_dir.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffatfs_types.h"
#include "ffatfs_api.h"
#include "ffatfs_de.h"

#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_DIR)

// static function
#ifdef FFAT_VFAT_SUPPORT
	static FFatErr _findLFNEsOfSFNE(Node* pNode, FatGetNodeDe* pNodeDE,
							t_uint32* pdwStartOffsetOfLFNE, t_uint32* pdwStartClusterOfLFNE,
							ComCxt* pCxt);
#endif // end of #ifdef FFAT_VFAT_SUPPORT

/**
 * This function initializes FFatDir module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_dir_init(void)
{
	/* nothing to do */

	return FFAT_OK;
}


/**
 * This function terminates FFatDir module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_dir_terminate(void)
{
	/* nothing to do */

	return FFAT_OK;
}


/**
 * Read an entry in a directory
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] entry lookup start offset
 * @param		pRI			: [IN/OUT] readdir information\
 * @param		dwFlag		: [IN] readdir flag
 *									READDIR_GET_NODE : build a node, 
 *														Caution!!. Node must be terminated after use
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: readdir success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOMOREENT	: no more entry
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		JUL-25-2006 [DongYoung Seo] First Writing.
 * @version		JAN-30-2009 [JeongWoo Park] support opened-unlink for directory.
 * @version		FEB-12-2009 [JeongWoo Park] add the code to scan previous LFNEs for SFNE.
 */
FFatErr
ffat_dir_readdir(Node* pNode, t_uint32 dwOffset, ReaddirInfo* pRI,
					ReaddirFlag dwFlag, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;
	FatGetNodeDe		stGetNodeDE;		// node directory entry.
	t_uint32			dwSize;				// size of a directory
	Node*				pNodeChild = NULL;	// child node for READDIR_STAT

#ifdef FFAT_STRICT_CHECK
	// check parameter
	IF_UK ((pNode == NULL) || (pRI == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// check directory is in open-unlinked state
	// LINUX may unlink a directory in open state and also request read operation
	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		return FFAT_ENOMOREENT;
	}
	FFAT_ASSERT(NODE_IS_UNLINK(pNode) == FFAT_FALSE);	// the node with only unlinked state can not be here

	if ((dwFlag & READDIR_NO_LOCK) == 0)
	{
		// lock node
		r = NODE_GET_READ_LOCK(pNode);
		FFAT_ER(r, (_T("fail to get node read lock")));

		r = VOL_GET_READ_LOCK(pVol);
		FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);
	}

	stGetNodeDE.pDE = NULL;	// initialize memory pointer for directory entry

	IF_UK (dwOffset > (FAT_DE_MAX * FAT_DE_SIZE))
	{
		FFAT_LOG_PRINTF((_T("Too big offset")));
		r = FFAT_EINVALID;
		goto out;
	}

	// the node is directory or not
	IF_UK (NODE_IS_DIR(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a directory ")));
		r = FFAT_ENOTDIR;
		goto out;
	}

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	VOL_INC_REFCOUNT(pVol);

	stGetNodeDE.pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(stGetNodeDE.pDE);

	// initialize structure for node information
	stGetNodeDE.dwCluster			= NODE_C(pNode);
	stGetNodeDE.dwOffset			= dwOffset;
	stGetNodeDE.dwClusterOfOffset	= 0;

	stGetNodeDE.dwTargetEntryCount	= 0;
	stGetNodeDE.psName				= NULL;
	stGetNodeDE.dwNameLen			= 0;
	stGetNodeDE.psShortName			= NULL;
	stGetNodeDE.bExactOffset		= FFAT_FALSE;

//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
re:
#endif
	// get directory entry from the offset
	r = ffat_dir_getDirEntry(pVol, pNode, &stGetNodeDE, FFAT_TRUE, FFAT_TRUE, pCxt);
	if (r < 0)
	{
// debug begin
#ifdef FFAT_DEBUG
		if (r != FFAT_EEOF)
		{
			FFAT_LOG_PRINTF((_T("fail to read directory entry")));
		}
#endif
// debug end
		if (r == FFAT_EXDE)
		{
			r = FFAT_EFAT;
		}

		goto out;
	}

#ifdef FFAT_VFAT_SUPPORT
	if (dwFlag & READDIR_LFN)
	{
		if ((pRI->psLFN == NULL) || (pRI->dwLenLFN < (FFAT_FILE_NAME_MAX_LENGTH + 1)))
		{
			FFAT_LOG_PRINTF((_T("invalid LFN string pointer")));
			r = FFAT_EINVALID;
			goto out;
		}

		if (stGetNodeDE.dwEntryCount == 1)
		{
			t_uint32	dwStartClusterOfLFNE = 0;
			t_uint32	dwStartOffsetOfLFNE = 0;

			// try to find previous LFNE in case that only SFNE is found.
			r = _findLFNEsOfSFNE(pNode, &stGetNodeDE,
								&dwStartOffsetOfLFNE, &dwStartClusterOfLFNE, pCxt);
			if (r == FFAT_ENOENT)
			{
				// no LFNEs, just SFNE
				goto get_name;
			}
			FFAT_EO(r, (_T("fail to find the LFNEs for SFNE")));

			// we found LFNEs, retry with new start offset
			stGetNodeDE.dwOffset			= dwStartOffsetOfLFNE;
			stGetNodeDE.dwClusterOfOffset	= dwStartClusterOfLFNE;
			goto re;
		}
		else
		{
get_name:
			r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), stGetNodeDE.pDE,
							stGetNodeDE.dwEntryCount,
							pRI->psLFN, &pRI->dwLenLFN, FAT_GEN_NAME_LFN);
			FFAT_EO(r, (_T("fail to generate LFN from DE")));
		}
	}
#endif

	if (dwFlag & READDIR_SFN)
	{
		if ((pRI->psSFN == NULL) || (pRI->dwLenSFN < (FAT_DE_SFN_MAX_LENGTH + 1)))
		{
			FFAT_LOG_PRINTF((_T("invalid SFN string pointer")));
			r = FFAT_EINVALID;
			goto out;
		}

		r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), stGetNodeDE.pDE,
						stGetNodeDE.dwEntryCount,
						pRI->psSFN, &pRI->dwLenSFN, FAT_GEN_NAME_SFN);
		FFAT_EO(r, (_T("fail to generate SFN from DE")));
	}

	if (dwFlag & READDIR_STAT)
	{
		if (pRI->pNodeStatus == NULL)
		{
			FFAT_LOG_PRINTF((_T("invalid node status storage pointer")));
			r = FFAT_EINVALID;
			goto out;
		}

		pNodeChild = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
		FFAT_ASSERT(pNodeChild);

		r = ffat_node_initNode(pVol, pNode, NODE_C(pNode), pNodeChild, FFAT_FALSE, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to init node")));
			goto out;
		}

		ffat_node_fillNodeInfo(pNodeChild, &stGetNodeDE, NULL);

		NODE_SET_VALID(pNodeChild);

		r = ffat_addon_getStatusFromDe(pNodeChild, &stGetNodeDE.pDE[stGetNodeDE.dwEntryCount - 1],
						stGetNodeDE.dwDeStartCluster,
						stGetNodeDE.dwDeStartOffset, pRI->pNodeStatus, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to get node status")));
			goto out;
		}
		else if (r == FFAT_DONE)
		{
			r = FFAT_OK;
		}
		else
		{
			r = ffat_node_getStatusFromDe(pNodeChild, &stGetNodeDE.pDE[stGetNodeDE.dwEntryCount - 1],
							stGetNodeDE.dwDeStartCluster,
							stGetNodeDE.dwDeStartOffset, pRI->pNodeStatus);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get node status")));
				goto out;
			}
		}

		if ((dwFlag & READDIR_GET_SIZE_OF_DIR) && (NODE_IS_DIR(pNodeChild) == FFAT_TRUE) )
		{
			r = ffat_dir_getSize(pNodeChild, &dwSize, pCxt);
			FFAT_EO(r, (_T("Fail to get size of directory")));

			pRI->pNodeStatus->dwSize = dwSize;
		}

		r = ffat_node_terminateNode(pNodeChild, pCxt);
	}

	if (dwFlag & READDIR_GET_NODE)
	{
		r = ffat_node_initNode(pVol, pNode, NODE_C(pNode), pRI->pNode, FFAT_FALSE, pCxt);
		FFAT_EO(r, (_T("fail to init node")));

		// this node nodes not needs any lock so release it
		r = ffat_lock_terminateRWLock(&pRI->pNode->stRWLock);
		FFAT_EO(r, (_T("fail to release lock")));

		ffat_node_fillNodeInfo(pRI->pNode, &stGetNodeDE, NULL);

		NODE_SET_VALID(pRI->pNode);
	}

	// update next read point
	pRI->dwOffsetNext	= stGetNodeDE.dwDeEndOffset + FAT_DE_SIZE;

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pNodeChild, sizeof(Node), pCxt);
	FFAT_LOCAL_FREE(stGetNodeDE.pDE, VOL_MSD(pVol), pCxt);

	VOL_DEC_REFCOUNT(pVol);

	if (r == FFAT_EEOF)
	{
		// END OF File일 경우 FFAT_ENOMOREENT 를 return 한다.
		r = FFAT_ENOMOREENT;
	}

	if ((dwFlag & READDIR_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(pVol);

out_vol:
		// unlock node
		r |= NODE_PUT_READ_LOCK(pNode);
	}

	return r;
}


/**
 * expand parent directory
 *
 * dwLastCluster 부터 dwClusterCount 만큼의 cluster를 확보한다.
 * pdwClusters에는 dwLastCluster 이후부터 저장이 된다.
 * 한번에 확장 가능한 갯수는 최대 3개 이다.
 *
 * @param		pNode				: [IN] node pointer(directory)
 * @param		dwNewClusterCount	: [IN] expand new cluster count
 *										최대 3개의 cluster가 저장된다.
 * @param		dwLastCluster		: [IN] the last cluster that has valid DE
 *										if 0 : there is no cluster information
 * @param		pdwClusters			: [OUT] cluster storage
 * @param		pCxt				: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 * @version		FEB-17-2009 [DongYoung Seo] use DIRECT I/O for cluster initialization
 * @version		FEB-21-2009 [DongYoung Seo] change meaning of dwLastCluster
 *											the last cluster  that has valid DE
 * @version		FEB-21-2009 [DongYoung Seo] remove per cluster allocation.
 *											update FAT in a time.
 * @version		FEB-28-2009 [JeongWoo Park] modify cluster allocation routine for bug fix(CQ.20637).
 *											and change policy that only new cluster will be store in pdwCluster
 */
FFatErr
ffat_dir_expand(Node* pNode, t_int32 dwNewClusterCount, t_uint32 dwLastCluster,
				t_uint32* pdwClusters, ComCxt* pCxt)
{
	FatUpdateFlag	dwFatUpdateFlag;
	Vol*			pVol;
	FFatErr			r;
	t_int32			i;
	t_uint32		dwCluster;						// temporary cluster storage
	t_uint32		dwPrevEOF;						// previous EOC
	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;
	FFatVC			stVC;							// vector cluster information
	FFatVCE			stVCEntry[NODE_MAX_CLUSTER_FOR_CREATE];	// vector cluster entry
	t_int32			dwEntryIndex;					// entry index in VC
	t_uint32		dwClusterIndex;					// cluster index in VC entry

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(dwNewClusterCount > 0);
	FFAT_ASSERT(dwNewClusterCount <= NODE_MAX_CLUSTER_FOR_CREATE);		// 4 is the maximum cluster count for a file name
	FFAT_ASSERT((NODE_IS_ROOT(pNode) == FFAT_TRUE) ? (VOL_IS_FAT32(NODE_VOL(pNode)) == FFAT_TRUE) : FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	if (dwLastCluster == 0)
	{
		r = ffat_misc_getLastCluster(pVol, NODE_C(pNode), &dwLastCluster, NULL, pCxt);
		FFAT_ER(r,  (_T("fail to get last cluster")));
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwLastCluster) == FFAT_TRUE);

	dwCluster = dwLastCluster;
	dwPrevEOF = dwLastCluster;

	// traverse and check existing clusters
	for (i = 0; i < dwNewClusterCount; i++)
	{
		r = FFATFS_GetNextCluster(VOL_VI(pVol), dwCluster, &dwCluster, pCxt);
		FFAT_ER(r, (_T("fail to get next cluster")));

		if (FFATFS_IS_EOF(NODE_VI(pNode), dwCluster) == FFAT_TRUE)
		{
			break;
		}

		pdwClusters[i]	= dwCluster;
		dwPrevEOF		= dwCluster;
	}

	if (i == dwNewClusterCount)
	{
		// we found all cluster to need
		return FFAT_OK;
	}
	
	dwFatUpdateFlag = VOL_GET_FAT_UPDATE_FLAG(pVol);

	// here. we need to write a new log for directory expansion
	// new log type for directory expansion.

	// log for expand dir
	r = ffat_addon_expandDir(pNode, dwPrevEOF, &dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to expand directory on ADDON")));

	IF_UK (r == FFAT_DONE)
	{
		r = FFAT_OK;
		goto out;
	}

	dwNewClusterCount = dwNewClusterCount - i;

	VC_INIT(&stVC, VC_NO_OFFSET);

	stVC.dwTotalEntryCount	= NODE_MAX_CLUSTER_FOR_CREATE;
	stVC.pVCE				= stVCEntry;

	r = ffat_misc_getFreeClusterForDir(pNode, &stVC, dwNewClusterCount, pCxt);
	FFAT_EO(r, (_T("fail to get free cluster for directory ")));

	FFAT_ASSERT(VC_CC(&stVC) == (t_uint32)dwNewClusterCount);

	for (dwEntryIndex = 0; dwEntryIndex < VC_VEC(&stVC); dwEntryIndex++)
	{
		// Init the cluster
		r = ffat_initCluster(pVol, NULL, stVCEntry[dwEntryIndex].dwCluster,
							stVCEntry[dwEntryIndex].dwCount,
							(dwCacheFlag | FFAT_CACHE_DATA_DE), pCxt);
		FFAT_EO(r, (_T("Fail to initialize cluster")));

		// record the new cluster in pdwClusters
		for (dwClusterIndex = 0; dwClusterIndex < stVCEntry[dwEntryIndex].dwCount; dwClusterIndex++)
		{
			FFAT_ASSERT(i < NODE_MAX_CLUSTER_FOR_CREATE);
			pdwClusters[i] = stVCEntry[dwEntryIndex].dwCluster + dwClusterIndex;
			i++;
		}
	}

	r = ffat_misc_makeClusterChainVC(pNode, dwPrevEOF, &stVC, dwFatUpdateFlag, dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_PRINT_DEBUG((_T("fail to make cluster chain")));
		goto out;
	}

out:
	IF_UK (r < 0)
	{
		// undo cluster allocation, don't care error
		ffat_misc_deallocateCluster(pNode, dwPrevEOF, 0, 0, NULL,
									FAT_ALLOCATE_NONE, FFAT_CACHE_FORCE, pCxt);
	}

	return r;
}


/**
 * directory를 삭제한다.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNode			: [IN] removed node pointer
 * @param		dwFlag			: [IN] flag for unlink operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume, try to unlink root node
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ENOTDIR	: this is not a directory
 * @return		FFAT_ENOTEMPTY	: not an empty directory
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-13-2007 [DongYoung Seo] add sync flag on calling ffat_node_close().
 *											to data inconsistency on cache module.
 */
FFatErr
ffat_dir_remove(Node* pNodeParent, Node* pNode, NodeUnlinkFlag dwNUFlag, ComCxt* pCxt)
{
	FFatErr			r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
	
	IF_UK (NODE_IS_VALID(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("invalid node ")));
		return FFAT_EINVALID;
	}
#endif

	IF_UK (NODE_IS_DIR((Node*)pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_ENOTDIR;
		goto out;
	}

	r = ffat_node_unlink(pNodeParent, pNode, (dwNUFlag | NODE_UNLINK_DISCARD_CACHE), pCxt);
	FFAT_EO(r, (_T("fail to unlink node")));

out:
	return r;
}


/**
 * directory 내에 file이나 다른 directory가 있는지 판단한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_TRUE	: Empty
 * @return		FFAT_FALSE	: Not empty
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_dir_isEmpty(Node* pNode, ComCxt* pCxt)
{

	FFatErr			r;
	ReaddirInfo		stRI;
	const t_int32	dwBuffSize = (FAT_DE_SFN_MAX_LENGTH + 1) * sizeof(t_wchar);

	stRI.psSFN	= (t_wchar*)FFAT_LOCAL_ALLOC(dwBuffSize, pCxt);
	FFAT_ASSERT(stRI.psSFN);

	stRI.dwLenSFN	= FAT_DE_SFN_MAX_LENGTH + 1;

	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE);

	r = ffat_dir_readdir(pNode, sizeof(FatDeSFN) * 2, &stRI, READDIR_NO_LOCK, pCxt);
	if (r == FFAT_OK)
	{
		r = FFAT_FALSE;
	}
	else if (r == FFAT_ENOMOREENT)
	{
		r = FFAT_TRUE;
	}
	
	FFAT_LOCAL_FREE(stRI.psSFN, dwBuffSize, pCxt);

	return r;
}


/**
 * initialize a cluster for directory
 * this function initialize a cluster and then write dot and dot dot.
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNode		: [IN] node pointer
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-04-2006 [DongYoung Seo] First Writing.
 * @version		FEB-17-2009 [DongYoung Seo] use DIRECT I/O for cluster initialization
 * @version		MAR-26-2009 [DongYoung Seo] ADD meta io flag on cache flag
 */
FFatErr
ffat_dir_initCluster(Node* pNodeParent, Node* pNode, ComCxt* pCxt)
{
	FFatErr			r;
	t_int32			dwSectorCount;
	t_int8*			pBuff = NULL;
	t_int32			dwWriteSector;
	Vol*			pVol;
	FatDeSFN*		pDE;
	t_boolean		bFirst = FFAT_TRUE;		// First write 
	FFatCacheFlag	dwFlag = FFAT_CACHE_DATA_DE | FFAT_CACHE_DIRECT_IO | FFAT_CACHE_META_IO;
											// Quiz. why do i set sync flag ?
											// what will happen without sync ?
	t_uint32		dwOffset ;				// sector init offset

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNode->dwCluster));

	dwSectorCount = pVol->stVolInfo.wSectorPerCluster;

	pBuff = FFAT_LOCAL_ALLOC(FFAT_SECTOR_SIZE_MAX, pCxt);
	FFAT_ASSERT(pBuff != NULL);

	dwWriteSector = FFAT_SECTOR_SIZE_MAX >> VOL_SSB(pVol);

	FFAT_MEMSET(pBuff, 0x00, (dwWriteSector << VOL_SSB(pVol)));

	pDE = (FatDeSFN*)pBuff;

	// generate dot
	FFAT_MEMCPY(pDE, &pNode->stDE, sizeof(FatDeSFN));
	FFAT_MEMCPY(pDE[0].sName, FAT_DE_DOT, FAT_SFN_NAME_CHAR);

	FFAT_MEMCPY(&pDE[1], &pNodeParent->stDE, sizeof(FatDeSFN));
	FFAT_MEMCPY(pDE[1].sName, FAT_DE_DOTDOT, FAT_SFN_NAME_CHAR);

	pDE[1].bNTRes = 0;

	if ((NODE_IS_ROOT(pNodeParent) == FFAT_TRUE) &&
		(VOL_IS_FAT16(pVol) == FFAT_TRUE))
	{
		FFAT_ASSERT(pDE[1].wFstClusHi == 0);
		FFAT_ASSERT(pDE[1].wFstClusLo == 0);
		pDE[1].wFstClusHi = 0;
		pDE[1].wFstClusLo = 0;
	}
	else
	{
		FFAT_ASSERT(pDE[1].wFstClusHi == pNodeParent->stDE.wFstClusHi);
		FFAT_ASSERT(pDE[1].wFstClusLo == pNodeParent->stDE.wFstClusLo);
		pDE[1].wFstClusHi = pNodeParent->stDE.wFstClusHi;
		pDE[1].wFstClusLo = pNodeParent->stDE.wFstClusLo;
	}

	dwOffset = 0;

	do
	{
		if (dwSectorCount < dwWriteSector)
		{
			dwWriteSector = dwSectorCount;
		}

		r = ffat_readWritePartialCluster(pVol, NULL, pNode->dwCluster,
					dwOffset, (dwWriteSector << VOL_SSB(pVol)), pBuff, FFAT_FALSE,
					dwFlag, pCxt);
		IF_UK (r !=  (dwWriteSector << VOL_SSB(pVol)))
		{
			FFAT_LOG_PRINTF((_T("fail to initialize a cluster")));
			r = FFAT_EIO;
			goto out;
		}

		dwSectorCount	-= dwWriteSector;
		dwOffset		+= (dwWriteSector << VOL_SSB(pVol));

		if (bFirst == FFAT_TRUE)
		{
			FFAT_MEMSET(pBuff, 0x00, sizeof(FatDeSFN) * 2);
			bFirst = FFAT_FALSE;
		}
	} while (dwSectorCount > 0);

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, FFAT_SECTOR_SIZE_MAX, pCxt);

	return r;
}





/**
 * write data to a directory
 * directory 에 대한 write를 수행한다.. !!!
 *
 * 반드시 충분한 cluster가 확보된 상태에서 사용되어야 한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		pVC			: [IN] vectored cluster data
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_dir_write(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
				FFatVC* pVC, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);
	FFAT_ASSERT(dwCacheFlag & FFAT_CACHE_DATA_DE);
	FFAT_ASSERT(dwSize > 0);

	pVol = NODE_VOL(pNode);

	// FAT16의 root directory 일 경우는 따로 처리
	if ((NODE_IS_ROOT(pNode) == FFAT_TRUE) &&
		(VOL_IS_FAT16(pVol) == FFAT_TRUE))
	{
		// write on root directory
		r = FFATFS_ReadWriteOnRoot(VOL_VI(pVol), dwOffset, pBuff, dwSize,
						dwCacheFlag, FFAT_FALSE, pNode, pCxt);
		FFAT_ER(r, (_T("fail to write data on FAT16 root directory")));
	}
	else
	{
		// write directory entry
		r = ffat_node_readWriteInit(pNode, dwOffset, pBuff, (t_uint32)dwSize, pVC, NULL,
									dwCacheFlag, FFAT_RW_WRITE, pCxt);
		FFAT_ER(r, (_T("fail to write data to node")));
	}

	return dwSize;
}


/**
 * get directory entry for a node
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pNode			: [IN] Node Pointer, may be NULL
 * @param		pNodeDE			: [IN/OUT] directory entry information for a node.
 * @param		bIgnoreVolLabel	: [IN/OUT] boolean for ignoring volume label entry
 * @param		bCheckAddon		: [IN] boolean for checking whether found DE has only ADDON feature (ex. XDE)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success, DEs for a node is stored at pNodeDE
 * @return		FFAT_EEOF		: end of file, no more entry
 * @return		FFAT_EINVALID	: Invalid name character at directory entry
 * @return		else		: error
 * @author		GwangOk Go
 * @version		AUG-14-2008 [GwangOk Go] First Writing.
 */
FFatErr
ffat_dir_getDirEntry(Vol* pVol, Node* pNode, FatGetNodeDe* pNodeDE,
					 t_boolean bIgnoreVolLabel, t_boolean bCheckAddon, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pCxt);

	if (pNodeDE->dwClusterOfOffset == 0)
	{
		if (pNode != NULL)
		{
			r = ffat_misc_getClusterOfOffset(pNode, pNodeDE->dwOffset,
							&pNodeDE->dwClusterOfOffset, NULL, pCxt);
		}
		else
		{
			r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), pNodeDE->dwCluster,
							pNodeDE->dwOffset,&pNodeDE->dwClusterOfOffset, pCxt);
		}
		FFAT_EO(r, (_T("Fail to get cluster of offset")));

		if (FFATFS_IS_EOF(VOL_VI(pVol), pNodeDE->dwClusterOfOffset) == FFAT_TRUE)
		{
			r = FFAT_EEOF;
			goto out;
		}
	}

re:
	r = FFATFS_GetNodeDirEntry(VOL_VI(pVol), pNodeDE, pCxt);
	if (r == FFAT_OK)
	{
		// check volume label
		IF_UK ((pNodeDE->dwEntryCount == 1) && (bIgnoreVolLabel == FFAT_TRUE))
		{
			FFAT_ASSERT(pNodeDE->pDE[0].bAttr != FFAT_ATTR_LONG_NAME);

			if (pNodeDE->pDE[0].bAttr & FFAT_ATTR_VOLUME)
			{
				// ignore volume label
				if (pNodeDE->bExactOffset == FFAT_FALSE)
				{
					pNodeDE->dwOffset += FAT_DE_SIZE;
					if ((pNodeDE->dwOffset & VOL_CSM(pVol)) == 0)
					{
						pNodeDE->dwClusterOfOffset = 0;	// set unknown
					}
					goto re;
				}
				else
				{
					r = FFAT_ENOENT;
					goto out;
				}
			}
		}

		r = ffat_addon_afterGetNodeDE(pVol, pNodeDE, pCxt);
		if (r == FFAT_OK1)
		{
			FFAT_ASSERT(pNodeDE->dwTotalEntryCount == 1);

			// If FFAT_OK1 is returned,
			// it need to skip this DE because this directory entry is XDE
			// and dwTotalEntryCount must be 1
			if ((pNodeDE->bExactOffset == FFAT_FALSE) && (bCheckAddon == FFAT_TRUE))
			{
				pNodeDE->dwOffset += (pNodeDE->dwTotalEntryCount << FAT_DE_SIZE_BITS);
				if ((pNodeDE->dwOffset & VOL_CSM(pVol)) == 0)
				{
					pNodeDE->dwClusterOfOffset = 0;	// set unknown
				}
				goto re;
			}
			else
			{
				FFAT_ASSERT((bCheckAddon == FFAT_TRUE) ? 0 : 1);

				r = FFAT_OK;
				goto out;
			}
		}
	}

out:
	return r;
}


/**
* get size of a directory
*
* @param		pNode		: [IN] directory node pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK			: success, DEs for a node is stored at pNodeDE
* @return		FFAT_EINVALID	: Invalid name character at directory entry
* @return		else		: error
* @author		DongYoung Seo
* @version		NOV-05-2008 [DongYoung Seo] first write
*/
FFatErr
ffat_dir_getSize(Node* pNode, t_uint32* pdwSize, ComCxt* pCxt)
{
	t_uint32		dwLastCluster;
	t_uint32		dwCount;
	Vol*			pVol;
	FFatErr			r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwSize);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	if ((NODE_IS_ROOT(pNode) == FFAT_TRUE) && (VOL_IS_FAT16(pVol) == FFAT_TRUE))
	{
		*pdwSize = VI_RSC(VOL_VI(pVol)) << VOL_SSB(pVol);
	}
	else
	{
		FFAT_ASSERT((pNode->stDE.sName[0] != '.') ? (FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), NODE_C(pNode)) == FFAT_TRUE) : FFAT_TRUE);

		if (NODE_C(pNode) == 0)
		{
			// child directory of root
			FFAT_ASSERT((pNode->stDE.sName[0] == '.') && (pNode->stDE.sName[1] == '.'));
			FFAT_ASSERT((pNode->stDE.sName[2] == ' ') && (pNode->stDE.sName[3] == ' '));
			FFAT_ASSERT((pNode->stDE.sName[4] == ' ') && (pNode->stDE.sName[5] == ' '));
			FFAT_ASSERT((pNode->stDE.sName[6] == ' ') && (pNode->stDE.sName[7] == ' '));

			if (VOL_IS_FAT16(pVol) == FFAT_TRUE)
			{
				*pdwSize = VOL_RSC(pVol) << VOL_SSB(pVol);
			}
			else
			{
				r = ffat_misc_getLastCluster(pVol, VOL_RC(pVol), &dwLastCluster, &dwCount, pCxt);
				FFAT_ER(r, (_T("fail to get last cluster")));
				*pdwSize = (dwCount + 1) << VOL_CSB(pVol);
			}
		}
		else
		{
			r = ffat_misc_getLastCluster(pVol, NODE_C(pNode), &dwLastCluster, &dwCount, pCxt);
			FFAT_ER(r, (_T("fail to get last cluster")));

			*pdwSize = (dwCount + 1) << VOL_CSB(pVol);
		}
	}

	return FFAT_OK;
}


//================================================================================
//
//	STATIC FUNCTIONS
//

#ifdef FFAT_VFAT_SUPPORT
	/**
	* find long file name entries of short file name entry (only for ffat_dir_readdir)
	*
	* @param		pNode					: [IN] Node Pointer of Parent directory
	* @param		pNodeDE					: [IN] directory entry information for a node.
	* @param		pdwStartOffsetOfLFNE	: [OUT] start offset of LFNE
	* @param		pdwStartClusterOfLFNE	: [OUT] start cluster of LFNE
	* @param		pCxt					: [IN] context of current operation
	* @return		FFAT_OK					: success, LFNS for SFNE is found
	* @return		FFAT_ENOENT				: no LFNE for this SFNE
	* @return		else					: error
	* @author		JeongWoo Park
	* @version		FEB-11-2009 [JeongWoo Park] First Writing.
	*/
	static FFatErr
	_findLFNEsOfSFNE(Node* pNode, FatGetNodeDe* pNodeDE,
					 t_uint32* pdwStartOffsetOfLFNE, t_uint32* pdwStartClusterOfLFNE,
					 ComCxt* pCxt)
	{
		FFatErr		r;
		Vol*		pVol;
		t_uint32	dwCurOffset;		// current working offset in the parent directory entry
		t_uint32	dwCurCluster;		// current working cluster
		t_uint32	dwCurLFNECount;		// count of the LFN entries be found in current cluster
		t_uint8		bPrevLFNOrder;		// order of previous LFNE
		t_uint8		bCheckSum;			// checksum of SFN
		
		FFAT_ASSERT(pNode);
		FFAT_ASSERT(pdwStartOffsetOfLFNE);
		FFAT_ASSERT(pdwStartClusterOfLFNE);
		FFAT_ASSERT((pNodeDE) && (pNodeDE->dwEntryCount == 1));
		FFAT_ASSERT(pNodeDE->dwDeStartOffset == pNodeDE->dwDeSfnOffset);

		if (pNodeDE->dwDeStartOffset == 0)
		{
			// NOTHING TO DO, SFN is first directory entry in parent directory
			return FFAT_ENOENT;
		}

		pVol = NODE_VOL(pNode);
		
		dwCurLFNECount	= 0;
		bPrevLFNOrder	= 0;
		bCheckSum		= FFATFS_GetCheckSum(&pNodeDE->pDE[0]);

		dwCurCluster	= pNodeDE->dwDeStartCluster;
		dwCurOffset		= pNodeDE->dwDeStartOffset;	// set as the offset of SFNE
		
		// Scan the clusters
		do
		{
			// scan from the previous directory entry
			dwCurOffset -= FAT_DE_SIZE;

			// get previous cluster
			r = ffat_misc_getClusterOfOffset(pNode, dwCurOffset, &dwCurCluster, NULL, pCxt);
			FFAT_EO(r, (_T("fail to get cluster of offset")));

			r = FFATFS_FindLFNEsInCluster(VOL_VI(pVol), dwCurCluster, dwCurOffset,
										bCheckSum, &bPrevLFNOrder, &dwCurLFNECount, pCxt);
			if (r < 0)
			{
				IF_UK (r != FFAT_ENOENT)
				{
					FFAT_LOG_PRINTF((_T("fail to get LFN for SFN")));
				}
				goto out;
			}

			FFAT_ASSERT(dwCurLFNECount > 0);

			dwCurOffset	-= ((dwCurLFNECount - 1) << FAT_DE_SIZE_BITS);

			if (r == FFAT_DONE)
			{
				// Whole LFNEs is found
				*pdwStartOffsetOfLFNE	= dwCurOffset;
				*pdwStartClusterOfLFNE	= dwCurCluster;
				r = FFAT_OK;
				goto out;
			}
			else if (dwCurOffset == 0)
			{
				r = FFAT_ENOENT;
				goto out;
			}
		} while(1);

	out:
		return r;
	}
#endif // end of #ifdef FFAT_VFAT_SUPPORT

//
//	END OF STATIC FUNCTIONS
//
//=============================================================================
