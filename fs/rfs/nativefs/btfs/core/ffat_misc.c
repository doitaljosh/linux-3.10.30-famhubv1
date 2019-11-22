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
 * @file		ffat_misc.c
 * @brief		This file contains miscellaneus functions. common function CORE and ADDON
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ffat_common.h"

#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffatfs_api.h"

#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_MISC)

/**
 * get cluster number of offset
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwOffset		: [IN] offset for the cluster
 * @param		pdwCluster		: [OUT] cluster number storage
 *									할당된 cluster가 없을경우 0을 저장한다.
 * @param		pdwClusterPrev	: [OUT] previous cluster number
 *									may be NULL
 *									0 : there is no previous cluster or can not found the cluster
 * @param		pPAL			: [OUT] previous access location
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
 * @version		DEC-12-2008 [GwangOk Go] add previous access location
 */
FFatErr
ffat_misc_getClusterOfOffset(Node* pNode, t_uint32 dwOffset, t_uint32* pdwCluster,
							NodePAL* pPAL, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	t_uint32		dwNewOffset;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCluster);
	FFAT_ASSERT(pNode->dwFlag & NODE_INIT);

	pVol			= NODE_VOL(pNode);
	*pdwCluster		= NODE_C(pNode);
	dwNewOffset		= 0;

	IF_UK (NODE_C(pNode) == 0)
	{
		FFAT_ASSERT(dwOffset == 0);
		return FFAT_OK;
	}

	if (dwOffset < (t_uint32)VOL_CS(pVol))
	{
		// offset is within first cluster
		return FFAT_OK;
	}
	else
	{
		NodePAL		stPAL;

		ffat_node_getPAL(pNode, &stPAL);

		if ((stPAL.dwOffset != FFAT_NO_OFFSET) &&
			(dwOffset >= stPAL.dwOffset) &&
			(dwOffset < (stPAL.dwOffset + (stPAL.dwCount << VOL_CSB(pVol)))))
		{
			// offset is within previous access location
			*pdwCluster = stPAL.dwCluster + ((dwOffset - stPAL.dwOffset) >> VOL_CSB(pVol));

			return FFAT_OK;
		}
	}

	// root directory for FAT16
	if (NODE_C(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(VOL_IS_FAT16(pVol) == FFAT_TRUE);

		return FFAT_OK;
	}

	// lookup cluster from ADDON module.
	r = ffat_addon_getClusterOfOffset(pNode, dwOffset, pdwCluster, &dwNewOffset, pPAL, pCxt);
	if (r == FFAT_DONE)
	{
		// we get cluster for dwOffset
		return FFAT_OK;
	}

	FFAT_ASSERT(dwOffset >= dwNewOffset);

	// get cluster from FFATFS
	r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), *pdwCluster, (dwOffset - dwNewOffset),
						pdwCluster, pCxt);
	FFAT_EO(r, (_T("fail to get cluster of offset ")));

out:
	return r;
}


/**
 * get next cluster number
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwCluster		: [IN] cluster
 * @param		pdwNextCluster	: [OUT] next cluster storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		negative		: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_misc_getNextCluster(Node* pNode, t_uint32 dwCluster,
							t_uint32* pdwNextCluster, ComCxt* pCxt)
{
	FFatErr		r;

	r = ffat_addon_getNextCluster(pNode, dwCluster, pdwNextCluster);
	FFAT_ER(r, (_T("fail to get next cluster")));

	if (r == FFAT_DONE)
	{
		return FFAT_OK;
	}

	return FFATFS_GetNextCluster(NODE_VI(pNode), dwCluster, pdwNextCluster, pCxt);
}


/**
 * Get the last cluster
 *
 * @param		pVol			: [IN] volume pointer
 * @param		dwCluster		: [IN] current cluster
 * @param		pdwLastCluster	: [OUT] last cluster storage
 *									dwCluster가 last cluster이 경우 dwCluster가 저장된다.
 * @param		pdwCount		: [OUT] cluster count to the last cluster
 *									may be NULL
 *									dwCluster 가 last cluster 일 경우 0 이 저장된다.
 *									dwCluster is not included. (0 when dwCluster is the last cluster)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_EFAT	: cluster chain is corrupted
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_misc_getLastCluster(Vol* pVol, t_uint32 dwCluster, t_uint32* pdwLastCluster,
							t_uint32* pdwCount, ComCxt* pCxt)
{
	// Node의 마지막 cluster 정보를 구한다.
	// 현재는 ADDON module에서 처리할 내용이 없으므로 구현하지 않는다.

	return FFATFS_GetLastCluster(VOL_VI(pVol), dwCluster, pdwLastCluster, pdwCount, pCxt);
}


/**
 * get a free cluster for directory
 *
 * directory 용으로 사용할 free cluster를 할당 받는다.
 * 현재는 directory에 대한 cluster 관리 정책이 없어 file과 동일 함.
 *
 * @param		pNode			: [IN] node pointer
 * @param		pstVC			: [IN] Vector clusters
 * @param		dwClusterCount	: [IN] cluster count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @history		DEC-29-2007 [InHwan Choi] refactoring FCC
 * @history		FEB-28-2009 [JeongWoo Park] edit to get multiple free clusters
 */
FFatErr
ffat_misc_getFreeClusterForDir(Node* pNode, FFatVC* pstVC, t_uint32 dwClusterCount, ComCxt* pCxt)
{
	t_uint32			dwFreeCount = 0;
	FFatErr				r;
	FatAllocateFlag		dwAllocFlag = FAT_ALLOCATE_DIR;

	r = ffat_misc_getFreeClusters(pNode, dwClusterCount, pstVC, 0,
									&dwFreeCount, dwAllocFlag, pCxt);
	FFAT_ER(r, (_T("fail to get a free cluster")));

	FFAT_ASSERT(pstVC->dwTotalClusterCount == dwClusterCount);
	FFAT_ASSERT(dwClusterCount == dwFreeCount);

	return r;
}

/**
 * get free clusters 
 * ffat_fcc_getFreeClusters()에서 free cluster를 삭제하였으므로
 * 성공후 다른곳에서 error 발생시 ffat_fcc_addFreeClustersVC()를 해주어야 함
 * (free cluster가 dirty였을수 있으므로 반드시 add 해주어야 함)
 *
 * how to know really there is not enough free clusters ?
 *	return value is FFAT_ENOSPC and *pdwFreeCount is 0
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN] vectored cluster data for free, never be NULL
 * @param		dwHint			: [IN/OUT] free cluster hint
 * @param		pdwFreeCount	: [IN] stored free cluster count at pVC
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		dwAllocFlag		: [IN] flag for allocation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: Not enough free cluster on the volume
 *									or not enough free entry at pVC (pdwFreeCount is updated)
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @history		DEC-29-2007 [InHwan Choi] refactoring FCC
 * @history		SEP-17-2008 [DongYoung Seo] update assert sentence.
 *										sometimes VC_CC(pVC) != dwFreeCount
 * @history		NOV-04-2008 [DongYoung Seo] add *pdwFreeCount initialization code
 * @history		FEB-15-2009 [DongYoung Seo] store 0 to *pdwFreeCount when the volume does not have enough free clusters
 */
FFatErr
ffat_misc_getFreeClusters(Node* pNode, t_uint32 dwCount, FFatVC* pVC,
							t_uint32 dwHint, t_uint32* pdwFreeCount,
							FatAllocateFlag dwAllocFlag, ComCxt* pCxt)
{
	t_uint32		dwFreeCount = 0;		// free cluster count
	FFatErr			r;

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(VC_VEC(pVC) == 0);
	FFAT_ASSERT(VC_CC(pVC) == 0);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(pdwFreeCount);

	// initialize *pdwFreeCount
	*pdwFreeCount = 0;

	// get free cluster from ADDON module
	r = ffat_addon_getFreeClusters(pNode, dwCount, pVC, dwHint, &dwFreeCount, dwAllocFlag, pCxt);
	if ((r == FFAT_DONE) || (r == FFAT_ENOSPC))
	{
		FFAT_ASSERT((dwFreeCount != 0) ? (VC_CC(pVC) == dwFreeCount) : FFAT_TRUE);
		FFAT_ASSERT((r == FFAT_DONE) ? (VC_CC(pVC) == dwCount) : FFAT_TRUE);
		FFAT_ASSERT((r == FFAT_DONE) ? (dwFreeCount == dwCount) : FFAT_TRUE);
		FFAT_ASSERT((r == FFAT_ENOSPC) ? ((VC_IS_FULL(pVC) == FFAT_TRUE) || (dwFreeCount == 0)) : FFAT_TRUE);
		*pdwFreeCount = dwFreeCount;

		IF_LK (r == FFAT_DONE)
		{
			r = FFAT_OK;
		}

		goto out;
	}
	else if (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get free cluster from FCC & FFATFS.")));
		goto out;
	}

	FFAT_ASSERT(r == FFAT_OK);
	FFAT_ASSERT(dwFreeCount == 0);			// ADDON does not return any free cluster

	// get free clusters from FFATFS
	r = FFATFS_GetFreeClusters(NODE_VI(pNode), (dwCount - dwFreeCount), pVC,
						dwHint, FFAT_FALSE, pCxt);
	if (r < 0)
	{
		if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
		{
			*pdwFreeCount = VC_CC(pVC);
			FFAT_PRINT_INFO((_T("FFATFS or there is not enough free entry at pVC")));
		}
		else
		{
			FFAT_PRINT_INFO((_T("fail to get free cluster from FFATFS or there is not enough free cluster")));
		}
	}
	else
	{
		*pdwFreeCount = VC_CC(pVC);
		r = FFAT_OK;
	}

out:
	FFAT_ASSERT(((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_FALSE)) ? (*pdwFreeCount == 0) : FFAT_TRUE);
	FFAT_ASSERT(((r == FFAT_ENOSPC) && (*pdwFreeCount != 0)) ? (VC_IS_FULL(pVC) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT(((r == FFAT_ENOSPC) && (*pdwFreeCount != 0)) ? (VC_CC(pVC) == *pdwFreeCount) : FFAT_TRUE);
	return r;
}


/**
 * allocate new clusters
 *
 * 주의 allocation 된 cluster의 정보는 pVC에 추가되어 저장된다.
 *		It does not initializes pVC
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwEOC		: [IN] end of file cluster 
 *								New cluster는 여기에 연결이 된다.
 *								필요한 cluster 번호는 첫번째 cluster 이며
 *								나머지는 optional 이다.
 *								pVC->dwClusterCount should be 0 when this is 0
 * @param		dwCount		: [IN] cluster count to allocate
 * @param		pVC			: [IN/OUT] cluster information, should not be NULL
 * @param		pLastVCE	: [OUT] last vectored cluster entry (may be NULL)
 * @param		dwFAFlag	: [IN] cluster count in pdwClusters
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 * @history		DEC-29-2007 [InHwan Choi] refactoring FCC
 * @history		AUG-16-2006 [DongYoung Seo] correct stVC_Temp.dwTotalEntryCount routine.
 *									use "/ sizeof(FFatVCE)" instead of "/ sizeof(FFatVC)"
 * @history		DEC-28-2009 [JW Park] fix the bug that wrong reference of pVC_Temp memory after free it.
 *								Bug effect: 1) Corrupt FAT table 2) Logical read sector fail by over-range.
 */
FFatErr
ffat_misc_allocateCluster(Node* pNode, t_uint32 dwEOC, t_uint32 dwCount,
						FFatVC* pVC, FFatVCE* pLastVCE, FatAllocateFlag dwFAFlag,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	t_uint32		dwRestCount;
	t_uint32		dwAllocatedCount;
	t_int32 		dwIndex;
	t_uint32		dwFirstAlloc = 0;	// the first allocated cluster number
	FFatVC			stVC_Temp;			// temporary FVC 
	FFatVC*			pVC_Temp;
	t_uint32		dwOriginalEOC;		// to store the original EOC

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(pVC);

	// When EOC is 0 then pFVC should be empty.
	FFAT_ASSERT((dwEOC == 0) ? (pVC->dwValidEntryCount == 0) : FFAT_TRUE);

	dwOriginalEOC = dwEOC;

	pVol = NODE_VOL(pNode);

	pVC_Temp = &stVC_Temp;

	pVC_Temp->pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(pVC_Temp->pVCE);

	dwRestCount = dwCount;

	stVC_Temp.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

	do
	{
		// initialize Fat Vectored Cluster Temp
		VC_INIT(pVC_Temp, 0);
		pVC_Temp->pVCE[0].dwCluster		= 0;
		pVC_Temp->pVCE[0].dwCount		= 0;

		r = ffat_misc_getFreeClusters(pNode, dwRestCount, pVC_Temp, dwEOC,
							&dwAllocatedCount, FAT_ALLOCATE_NONE, pCxt);
		if (r < 0)
		{
			if (r == FFAT_ENOSPC)
			{
				if (dwAllocatedCount == 0)
				{
					// THERE IS NOT ENOUGH FREE SPACE
					goto out;
				}
			}
			else
			{
				FFAT_LOG_PRINTF((_T("fail to get free cluster")));
				goto out;
			}
		}

		FFAT_ASSERT(dwRestCount >= dwAllocatedCount);
		FFAT_ASSERT(VC_VEC(pVC_Temp) <= VC_TEC(pVC_Temp));
		FFAT_ASSERT((VC_CC(pVC_Temp) == dwAllocatedCount) || (VC_VEC(pVC_Temp) == VC_TEC(pVC_Temp)));
		FFAT_ASSERT(dwAllocatedCount > 0);

		r = ffat_misc_makeClusterChainVC(pNode, dwEOC, pVC_Temp, FAT_UPDATE_NONE, dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			VC_VEC(pVC_Temp) = 0;	// error 처리시 마지막 get free cluster를 add free cluster 하기 위함
			FFAT_PRINT_DEBUG((_T("fail to make cluster chain")));
			goto out;
		}

		if (dwFAFlag & FAT_ALLOCATE_INIT_CLUSTER)
		{
			for (dwIndex = (VC_VEC(pVC_Temp) - 1) ; dwIndex >= 0 ; dwIndex--)
			{				
				r = ffat_initCluster(pVol, pNode, pVC_Temp->pVCE[dwIndex].dwCluster,
									pVC_Temp->pVCE[dwIndex].dwCount, dwCacheFlag, pCxt);
				FFAT_ER(r, (_T("Fail to initialize cluster")));
			}
		}

// debug begin
#ifdef FFAT_DEBUG
		if (dwEOC > 0)
		{
			t_uint32	dwNextCluster;

			r = ffat_misc_getNextCluster(pNode, dwEOC, &dwNextCluster, pCxt);
			FFAT_ER(r, (_T("fail to get next cluster")));

			FFAT_ASSERT(FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) == FFAT_FALSE);
			FFAT_ASSERT(VC_FC(pVC_Temp) == dwNextCluster);
		}
#endif
// debug end

		if (dwFirstAlloc == 0)
		{
			FFAT_ASSERT(VC_FC(pVC_Temp) > 0);
			dwFirstAlloc = VC_FC(pVC_Temp);
		}

		// _merge cluster chain
		if (VC_IS_FULL(pVC) == FFAT_FALSE)
		{
			ffat_com_mergeVC(pVC, pVC_Temp);
		}
		
		FFAT_ASSERT(pVC_Temp->dwValidEntryCount > 0);

		dwEOC		= VC_LC(pVC_Temp);
		dwRestCount	-= dwAllocatedCount;
	
	} while(dwRestCount > 0);

	if (VC_IS_EMPTY(pVC) == FFAT_FALSE)
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_LC(pVC)) == FFAT_TRUE);
		VI_SET_FCH(VOL_VI(pVol), (VC_LC(pVC) + 1));
	}

	if (pLastVCE)
	{
		pLastVCE->dwCluster	= pVC_Temp->pVCE[VC_LEI(pVC_Temp)].dwCluster;
		pLastVCE->dwCount	= pVC_Temp->pVCE[VC_LEI(pVC_Temp)].dwCount;
	}

	FFAT_ASSERT(VC_CC(pVC) > 0);

out:

	// 오류가 발생 하였을 경우 
	// allocation을 해제 하여야 한다.
	IF_UK (r < 0)
	{
		if (dwFirstAlloc != 0)
		{
			// [BUG FIX : 2008-11-18] Input the dwOriginalEOC To make the original EOC can hold EOC mark.
			ffat_misc_deallocateCluster(pNode, dwOriginalEOC, dwFirstAlloc, 0,
										NULL, (dwFAFlag | FAT_DEALLOCATE_FORCE),
										(dwCacheFlag | FFAT_CACHE_FORCE), pCxt);

			if ((VC_VEC(pVC_Temp) != 0) && (VC_FC(pVC_Temp) != dwFirstAlloc))
			{
				// add free cluster which be gotten last to FCC
				r |= ffat_addon_addFreeClustersVC(pVol, pVC_Temp, pCxt);
			}
		}
	}

	// [BUG FIX : 2009-12-28] free the memory after reference.
	FFAT_LOCAL_FREE(pVC_Temp->pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	return r;
}


/**
 * allocate clusters 
 *
 * pVC에 있는 모든 cluster를 free 한다.
 * dwPrevEOF에는 EOC mark를 기록한다.
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwNewEOC		: [IN] new end of chain
 *										dwPrevEOF는 EOC mark가 기록된다.
 *										May be 0 (if this is 0 dwFirstCluster should not be 0)
 * @param		dwFirstCluster	: [IN] first cluster to be deallocated
 *										0일 경우는 dwPrevEOF에서 next cluster를 구한다.
 *										May be 0 (if this is 0 dwPrevEOF should not be 0)
 * @param		dwCount			: [IN] cluster count to allocate
 *										0일 경우는 dwFirstCluster이후의 모든 cluster를 free 한다.
 * @param		pVC				: [IN] cluster information
 *										cluster 정보가 저장되어 있다.
 *										주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 * @param		dwFAFlag		: [IN] flag for cluster deallocation
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 * @version		SEP-08-2008 [DongYoung Seo] remove assert for check dwFirstCluster is 0 or not.
 *									it may be zero after ffat_addon_deallocateCluster()
 *									when ADDON do nothing.
 */
FFatErr
ffat_misc_deallocateCluster(Node* pNode, t_uint32 dwNewEOC,
							t_uint32 dwFirstCluster, t_int32 dwCount,
							FFatVC* pVC, FatAllocateFlag dwFAFlag,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FatAllocate		stAlloc;
	t_uint32		dwDeallocCount;
	FFatErr			r;

	dwDeallocCount = 0;

	r = ffat_addon_deallocateCluster(pNode, dwNewEOC, &dwFirstCluster, dwCount,
					&dwDeallocCount, pVC, dwFAFlag, dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("fail to deallocate clusters with ADDON module")));
	if (r == FFAT_DONE)
	{
		// all cluster deallocation is done 
		goto addon_done;
	}

	stAlloc.dwCount			= dwCount;	// allocated cluster count를 deallocation 해야할 cluster의 수로 설정한다.
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= dwNewEOC;
	stAlloc.dwFirstCluster	= dwFirstCluster;
	stAlloc.dwLastCluster	= 0;
	stAlloc.pVC				= pVC;

	r = FFATFS_DeallocateCluster(NODE_VI(pNode), dwCount, &stAlloc,
					&dwDeallocCount, &dwFirstCluster, dwFAFlag, dwCacheFlag, pNode, pCxt);
	FFAT_ER(r, (_T("fail to deallocate cluster")));

	// We should sync cluster deallocation to avoid cache mismatch
	// between FFAT cache and VFS cache
	// The cluster may be reused for a file.
	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		// sync FFATFS cache
		r = FFATFS_SyncVol(NODE_VI(pNode), FFAT_CACHE_SYNC, pCxt);
		FFAT_ER(r, (_T("Fail to sync volume for directory removal")));
	}

addon_done:
	r = ffat_addon_afterDeallocateCluster(pNode, pVC, dwFirstCluster, dwDeallocCount, pCxt);
	FFAT_ER(r, (_T("fail operation after deallocation on ADDON module ")));

	return r;
}


/**
* Make cluster chain with FVC
*
* @param		pNode			: [IN] Node pointer
* @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
*										may be 0 ==> no previous cluster
* @param		pFVC			: [IN] Vectored Cluster Information
* @param		dwFUFlag		: [IN] flags for FAT update
* @param		dwCacheFlag		: [IN] flag for cache operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo
* @version		NOV-15-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_misc_makeClusterChain(Node* pNode, t_uint32 dwPrevEOF, t_int32 dwClusterCount,
						t_uint32* pdwClusters, FatUpdateFlag dwFUFlag,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = FFATFS_MakeClusterChain(NODE_VI(pNode), dwPrevEOF, dwClusterCount,
								pdwClusters, dwFUFlag, dwCacheFlag, pNode, pCxt);
	FFAT_ER(r, (_T("fail to make cluster chain")));

	// give FAT update information to ADDON
	r = ffat_addon_afterMakeClusterChain(pNode, dwPrevEOF, dwClusterCount, pdwClusters, dwFUFlag, dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("makeClusterChainAfter Operation Failed ")));

	ffat_misc_decFreeClusterCount(pNode, dwClusterCount, pCxt);

	return r;
}


/**
* Make cluster chain with VC
*
* @param		pNode			: [IN] Node pointer
* @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
*										may be 0 ==> no previous cluster
* @param		pVC				: [IN] Vectored Cluster Information
* @param		dwFUFlag		: [IN] flags for FAT update
* @param		dwCacheFlag		: [IN] flag for cache operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo
* @version		NOV-15-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_misc_makeClusterChainVC(Node* pNode, t_uint32 dwPrevEOF, FFatVC* pVC,
								FatUpdateFlag dwFUFlag, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = FFATFS_MakeClusterChainVC(NODE_VI(pNode), dwPrevEOF, pVC, dwFUFlag,
					dwCacheFlag, pNode, pCxt);
	FFAT_ER(r, (_T("fail to make cluster chain")));

	// send FAT update information to ADDON
	r = ffat_addon_afterMakeClusterChainVC(pNode, dwPrevEOF, pVC, dwFUFlag, dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("makeClusterChainVCAfter Operation Failed ")));

	ffat_misc_decFreeClusterCount(pNode, VC_CC(pVC), pCxt);

	return r;
}


/**
 * get cluster information.
 *
 * vector 형태로 cluster의 정보를 구한다.
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 *
 * 주의
 * pVC의 내용은 초기화 하지 않는다.
 * 정보를 추가해서 저장한다.
 * 단, dwOffset을 사용할때 pVC는 초기화되어 있어야 한다
 * 
 * @param		pVol			: [IN] volume pointer
 * @param		pNode			: [IN] node pointer (in case ADDON is not used, set NULL)
 * @param		dwCluster		: [IN] start cluster number (may be 0. if 0, dwOffset must be valid.)
 * @param		dwOffset		: [IN] start offset (if dwCluster is 0, this is used. if not, ignored)
 * @param		dwCount			: [IN] cluster count
 *									0 : fill until pVC is full or till EOF
 * @param		pVC				: [OUT] vectored cluster information
 * @param		pPAL			: [OUT] previous access location
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		OCT-14-2008 [GwangOk Go] add parameter dwOffset
 */
FFatErr
ffat_misc_getVectoredCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster, t_uint32 dwOffset,
							t_uint32 dwCount, FFatVC* pVC, NodePAL* pPAL, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT((dwCluster == 0) ? (VC_IS_EMPTY(pVC) == FFAT_TRUE) : FFAT_TRUE);

	if ((dwCluster == 0) && (dwCount != 0))
	{
		NodePAL		stPAL;
		t_uint32	dwOffsetIndexReq;
		t_uint32	dwOffsetIndexPAL;

		// pVC must be initialized

		FFAT_ASSERT(VC_O(pVC) == (dwOffset & (~VOL_CSM(pVol))));

		ffat_node_getPAL(pNode, &stPAL);

		dwOffsetIndexReq = dwOffset >> VOL_CSB(pVol);
		dwOffsetIndexPAL = stPAL.dwOffset >> VOL_CSB(pVol);

		if ((stPAL.dwOffset != FFAT_NO_OFFSET) &&
			(dwOffset >= stPAL.dwOffset) &&
			((dwOffsetIndexReq + dwCount) <= (dwOffsetIndexPAL + stPAL.dwCount)))
		{
			FFAT_ASSERT(dwOffsetIndexReq >= dwOffsetIndexPAL);

			// clusters are within previous access location
			pVC->pVCE[0].dwCluster	= stPAL.dwCluster + dwOffsetIndexReq - dwOffsetIndexPAL;
			pVC->pVCE[0].dwCount	= dwCount;

			VC_CC(pVC)	= dwCount;
			VC_VEC(pVC)	= 1;

			return FFAT_OK;
		}
	}

	// ADDON module may fill partial cluster information
	r = ffat_addon_getVectoredCluster(pNode, dwCluster, dwOffset, dwCount, pVC,
					&dwCluster, &dwCount, pPAL, pCxt);
	if (r == FFAT_DONE)
	{
		return FFAT_OK;
	}

	if ((dwCluster == 0) && (VC_CC(pVC) == 0))
	{
		// there is no start cluster. get cluster of offset.
		r = ffat_misc_getClusterOfOffset(pNode, dwOffset, &dwCluster, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get cluster of offset ")));
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

	// get rest cluster information from FFAT
	return FFATFS_GetVectoredCluster(VOL_VI(pVol), dwCluster, dwCount,
					pVC, FFAT_FALSE, pCxt);
}


/**
* notification for decrease free cluster count
*
* @param		pNode		: [IN] node pointer
* @param		dwCount		: [IN] cluster count
* @return		FFAT_OK		: success
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Remove the code calling fat_addon_decFreeClusterCount
*/
void
ffat_misc_decFreeClusterCount(Node* pNode, t_uint32 dwCount, ComCxt* pCxt)
{
	FFAT_ASSERT(pNode);

	FFATFS_DecFreeClusterCount(NODE_VI(pNode), dwCount, pCxt);

	return;
}


/**
* Flush cache for a volume
* 
* flush ==> sync + discard
*
* @param		pVol	: [IN] volume pointer
* @param		FFAT_OK			: Sector IO Success
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_misc_flushVol(Vol* pVol, ComCxt* pCxt)
{
	FFatErr				r;
	FFatCacheInfo		stCI;				// cache info for buffer cache

	FFAT_ASSERT(pVol);

	// Flush all cache on volume volume
	r = FFATFS_FlushVol(VOL_VI(pVol), FFAT_CACHE_NONE, pCxt);
	FFAT_ER(r, (_T("fail to flush volume")));

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	// flush all cache 
	r = ffat_al_syncDev(FFAT_CACHE_SYNC | FFAT_CACHE_DISCARD, &stCI);
	FFAT_ER(r, (_T("fail to sync buffer cache")));

	return FFAT_OK;
}


/**
* Sector IO toward logical device
* 
* @param		pVol		: [IN] volume pointer
* @param		pLDevIO		: [IN] Logical device IO information
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK			: Sector IO Success
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh]	Add the code that does ldevIO directly if addon does not support it
*/
FFatErr
ffat_misc_ldevIO(Vol* pVol, FFatLDevIO* pLDevIO, ComCxt* pCxt)
{
	FFatErr		r;
	FFatCacheInfo	stCI;
	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));
	
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLDevIO);

	if ((pLDevIO->dwSectorNo + pLDevIO->dwCount) > VOL_SC(pVol))
	{
		FFAT_LOG_PRINTF((_T("Invalid Sector Count")));
		return FFAT_EINVALID;
	}

	if (pLDevIO->dwFlag > FFAT_IO_ERASE_SECTOR) 
	{
		FFAT_LOG_PRINTF((_T("Invalid IO type")));
		return FFAT_EINVALID;
	}

	r = ffat_misc_flushVol(pVol, pCxt);
	FFAT_EO(r, (_T("fail to flush caches")));

	r = ffat_addon_ldevIO(pVol, pLDevIO, pCxt);
	if (r < 0)
	{
		FFAT_LOG_PRINTF((_T("LDev IO Error or not support request")));
	}

	if (r == FFAT_ENOSUPPORT)
	{
		if (pLDevIO->dwFlag == FFAT_IO_READ_SECTOR)
		{
			r = ffat_al_readSector(pLDevIO->dwSectorNo, pLDevIO->pBuff, pLDevIO->dwCount, FFAT_CACHE_DIRECT_IO, &stCI);
			if (r > 0)
			{
				r = FFAT_OK;
			}
		}
		else if (pLDevIO->dwFlag == FFAT_IO_WRITE_SECTOR)
		{
			r = ffat_al_writeSector(pLDevIO->dwSectorNo, pLDevIO->pBuff, pLDevIO->dwCount, FFAT_CACHE_DIRECT_IO, &stCI);
			if (r > 0)
			{
				r = FFAT_OK;
			}
		}
		else
		{
			FFAT_ASSERT(0);
		}
	}
	else if (r == FFAT_DONE)
	{
		r = FFAT_OK;
	}

out:
	return r;
}
