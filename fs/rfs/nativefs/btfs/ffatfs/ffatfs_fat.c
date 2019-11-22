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
 * @file		ffatfs_fat.c
 * @brief		The FAT module for FFATFS
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"

#include "ffatfs_fat.h"
#include "ffatfs_types.h"
#include "ffatfs_cache.h"
#include "ffatfs_api.h"
#include "ffatfs_misc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_FAT)

//#define _DEBUG_FAT

// abstraction layer
#define _GET_MAIN()			((FFatfsFatMain*)&_FatMain)

// static function
static FFatErr	_makeClusterChainRollBack(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwClusterCount, 
							t_uint32* pdwClusters, FFatCacheFlag dwCacheFlag, void* pNode);
static FFatErr	_makeClusterChainRollBackVC(FatVolInfo* pVI, t_uint32 dwPrevEOF,
							t_int32 dwLastEntryIndex, t_int32 dwLastOffset,
							FFatVC* pVC, FFatCacheFlag dwCacheFlag,
							void* pNode);

static FFatErr	_deallocateClusterVC(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwLastEntryIndex, 
							t_int32 dwLastOffsets, FFatVC* pVC, t_uint32* pdwFreeCount,
							t_uint32* pdwFirstFree, FatAllocateFlag dwFAFlag,
							FFatCacheFlag dwCacheFlag, void* pNode);
static FFatErr	_deallocateCluster(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_uint32 dwCluster, 
							t_uint32* pdwFreeCount, t_uint32* pdwFirstFree,
							FatAllocateFlag dwFlag, FFatCacheFlag dwCacheFlag,
							void* pNode);
static FFatErr	_deallocateWithSC(FatVolInfo* pVI, FFatVC* pVC,
							t_uint32 dwFirstCluster, t_uint32* pdwFreeCount,
							FatAllocateFlag dwFlag, FFatCacheFlag dwCacheFlag,
							void* pNode);

static FFatErr	_buildSparseClusterMap(FatVolInfo* pVI, FFatVC* pVC,
								FatSparseCluster* pFSC, FatAllocateFlag dwFlag);
static FFatErr	_buildSparseClusterMapLv2(FatVolInfo* pVI, FFatVC* pVC,
								FatSparseCluster* pFSC, t_int32 dwIndexLv1, FatAllocateFlag dwFlag);

// for free cluster count
static FFatErr	_getFreeClusterCount(FatVolInfo* pVI, t_int8* pBuff, t_int32 dwBuffSize, t_uint32* pdwFreeCount);
static t_int32	_getFreeClusterCount16(t_int8* pBuff, t_int32 dwLastIndex);
static t_int32	_getFreeClusterCount32(t_int8* pBuff, t_int32 dwLastIndex);

// functions about VC
static void		_adjustVC(FFatVC* pVC, t_int32 dwLastEntryIndex, t_int32 dwLastOffset);


#define FFAT_DEBUG_FAT_PRINTF(_msg)
#define	FFAT_DEBUG_FAT_UPDATE(_C, _V)

// debug begin
#ifdef FFAT_DEBUG
	// debug functions
	static void	_debugCheckClusterValidity(FatVolInfo* pVI, t_uint32* pdwClusters, t_int32 dwCount);
	static void	_debugCheckClusterValidityVC(FatVolInfo* pVI, FFatVC* pVC);
	static void _debugFatUpdate(t_uint32 dwCluster, t_uint32 dwValue);

	#ifdef _DEBUG_FAT
		#undef FFAT_DEBUG_FAT_PRINTF
		//#define FFAT_DEBUG_FAT_PRINTF(_msg)		FFAT_PRINT_VERBOSE((_T("[BTFS_FAT] %s, %d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)
		#define FFAT_DEBUG_FAT_PRINTF(_msg)		_BTFS_MY_PRINTF(_T("[BTFS_FAT] ")); _BTFS_MY_PRINTF _msg
	#endif

	#undef FFAT_DEBUG_FAT_UPDATE
	#define	FFAT_DEBUG_FAT_UPDATE(_C, _V)	_debugFatUpdate(_C, _V)
#endif
// debug end

// global variables
static FFatfsFatMain	_stMain;

/**
 *	Initializes FAT module
 **/
FFatErr
ffat_fs_fat_init(void)
{
	t_int32		i;

	FFAT_MEMSET(&_stMain, 0x00, sizeof(FFatfsFatMain));

	// initialize callback entries
	for (i = 0; i < FFATFS_FAT_CALLBACK_COUNT; i++)
	{
		_stMain.pfCallBack[i] = NULL;
	}

	return FFAT_OK;
}


/**
 *	Terminate FAT module
 **/
FFatErr
ffat_fs_fat_terminate(void)
{
	return FFAT_OK;
}

/**
 * get cluster of dwOffset
 *
 * @param		pVI		: volume pointer
 * @param		dwCluster		: start cluster, offset is 0
 * @param		dwOffset		: byte offset from begin of the node
 * @param		pdwCluster		: cluster number storage
 *									*pdwCluster 0 : root directory
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		JUL-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_getClusterOfOffset(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwOffset, 
							t_uint32* pdwCluster)
{
	FFatErr		r;
	t_uint32	dwForwardClusterCount;		// cluster count to traverse

	FFAT_ASSERT(pVI);
	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (FFATFS_IS_FAT16(pVI) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? FFAT_TRUE : FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(pdwCluster);

	dwForwardClusterCount = dwOffset >> VI_CSB(pVI);

	if ((dwForwardClusterCount == 0) || (dwCluster == FFATFS_FAT16_ROOT_CLUSTER))
	{
		*pdwCluster			= dwCluster;		// target cluster is dwCluster
		return FFAT_OK;
	}

	r = ffat_fs_fat_forwardCluster(pVI, dwCluster, dwForwardClusterCount, pdwCluster);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to forward cluster")));
		return r;
	}

	return FFAT_OK;
}


/**
 * get cluster number of the next of cluster dwCluster for FAT16
 *
 * @param		pVI	: [IN] volume pointer
 * @param		dwCluster	: [IN] start cluster
 * @param		pdwCluster	: [OUT] Next Cluster number
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @return		FFAT_EFAT	: broken cluster chain
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_getNextCluster16(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32* pdwCluster)
{

	FFatfsCacheEntry*	pEntry = NULL;
	t_uint32			dwSector;
	t_uint16*			pCluster;
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVI == NULL) || (pdwCluster == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster number")));
		return FFAT_EFAT;
	}

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwCluster);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	// get FAT sector number
	dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCluster);

	r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_LOCK | FFAT_CACHE_DATA_FAT), &pEntry, pVI);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get a sector from FFATFS cache")));
		return r;
	}

	FFAT_ASSERT(pEntry);

	pCluster = (t_uint16*)(pEntry->pBuff);

	*pdwCluster = FFAT_BO_UINT16(pCluster[dwCluster & VI_CCPFS_MASK(pVI)]);

	r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_UNLOCK | FFAT_CACHE_DATA_FAT), NULL);
	IF_UK (r < 0)
	{
		return r;
	}

	return FFAT_OK;

}


/**
 * get cluster number of the next of cluster dwCluster for FAT32
 *
 * @param		pVI	: [IN] volume pointer
 * @param		dwCluster	: [IN] start cluster
 * @param		pdwCluster	: [OUT] Next Cluster number
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @return		FFAT_EFAT	: broken cluster chain
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_getNextCluster32(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32* pdwCluster)
{

	FFatfsCacheEntry*	pEntry = NULL;
	t_uint32			dwSector;
	t_uint32*			pCluster;
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVI == NULL) || (pdwCluster == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif
  // 2010.07.14_chunum.kong_Fix the bug that error code is added about Macro of Invalid cluster check, cluster 0 is returned at sector value.
  // Problem Broken BPB at RFS
	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster number")));
		return FFAT_EFAT;
	}


	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwCluster);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	// get FAT sector number
	dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCluster);

	r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_FAT | FFAT_CACHE_LOCK), &pEntry, pVI);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get a sector from FFATFS cache")));
		return r;
	}

	FFAT_ASSERT(pEntry);

	pCluster = (t_uint32*)(pEntry->pBuff);

	*pdwCluster = FFAT_BO_UINT32(pCluster[dwCluster & VI_CCPFS_MASK(pVI)]) & FAT32_MASK;

	r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DATA_FAT | FFAT_CACHE_UNLOCK), NULL);
	IF_UK (r < 0)
	{
		return r;
	}

	return FFAT_OK;
}


/**
 * forward cluster
 * dwCluster로 부터 dwCount개수 만큼 FAT chain을 따라 이동한 cluster의
 * 정보를 return 한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] start cluster
 * @param		dwCount			: [IN] forward cluster
 * @param		pdwCluster		: [OUT] cluster number storage
 *										*pdwCluster 0 : root directory
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_forwardCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwCount,
					t_uint32* pdwCluster)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(pdwCluster);

	if (dwCount == 0)
	{
		*pdwCluster = dwCluster;
		return FFAT_OK;
	}

	while (dwCount-- > 0)
	{
		// read FAT
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		FFAT_EO(r, (_T("fail to get cluster")));
	}

	*pdwCluster = dwCluster;

out:
	return r;
}


/**
 * get the last cluster
 *
 * dwCluster로 부터 마지막 cluster 까지 이동한 후 마지막 cluster 번호를
 * pdwLastCluster에 저장한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] start cluster
 * @param		pdwLastCluster	: [OUT] last cluster storage
 *									dwCluster가 last cluster이 경우 dwCluster가 저장된다.
 * @param		pdwCount		: [OUT] cluster count to the last cluster
 *									may be NULL
 *									dwCluster 가 last cluster 일 경우 0 이 저장된다.
 *									dwCluster is not included. (0 when dwCluster is the last cluster)
 * @return		FFAT_OK		: success
 * @return		FFAT_EFAT	: cluster chain is corrupted
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @version		FEB-14-2009 [DongYoung Seo] store 0 at *pdwCount for FAT16 root directory
 */
FFatErr
ffat_fs_fat_getLastCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32* pdwLastCluster,
							t_uint32* pdwCount)
{
	FFatErr		r = FFAT_OK;
	t_uint32	dwClusterPrev;
	t_uint32	dwCount = 0;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwLastCluster);

	if ((dwCluster == VI_RC(pVI)) && (FFATFS_IS_FAT16(pVI) == FFAT_TRUE))
	{
		// this is root cluster on FAT16 volume
		*pdwLastCluster = dwCluster;
		dwCount = 0;
		goto out;
	}

#ifdef FFAT_STRICT_CHECK
	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	while (1)
	{
		// read FAT
		dwClusterPrev = dwCluster;

		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		FFAT_ER(r, (_T("fail to get cluster")));

		if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
		{
			r = FFAT_OK;
			break;
		}
		else if (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
		{
			r = FFAT_EFAT;
			break;
		}

		dwCount++;
	}

	*pdwLastCluster = dwClusterPrev;

out:
	if (pdwCount)
	{
		*pdwCount = dwCount;
	}

	return r;
}


/**
 * FAT16의 EOF 인지 확인한다.
 *
 * @param		dwCluster		: [IN] cluster number
 * @return		FFAT_TRUE	: EOF
 * @return		FFAT_FALSE	: not EOF
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
t_boolean
ffat_fs_fat_isEOF16(t_uint32 dwCluster)
{
	return IS_FAT16_EOF(dwCluster);
}


/**
 * FAT32의 EOF 인지 확인한다.
 *
 * @param		dwCluster		: [IN] cluster number
 * @return		FFAT_TRUE		: EOF
 * @return		FFAT_FALSE		: not EOF
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
t_boolean
ffat_fs_fat_isEOF32(t_uint32 dwCluster)
{
	return IS_FAT32_EOF(dwCluster);
}


/**
 * get free clusters 
 *
 * free cluster를 찾아 그 정보를 array 형태로 pVC에 저장한다.
 *
 * @param		pVI	: [IN] volume pointer
 * @param		dwCount		: [IN] free cluster request count
 * @param		pVC			: [IN/OUT] free cluster storage, array
 * @param		dwHint		: [IN] free cluster hint
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOSPC	: not enough free space
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @version		OCT-22-2008 [GwangOk Go] even if not enough free cluster, return partial free cluster
 */
FFatErr
ffat_fs_fat_getFreeClusters(FatVolInfo* pVI, t_uint32 dwCount,
								FFatVC* pVC, t_uint32 dwHint, t_boolean bGetMoreCluster)
{
	FFatErr		r;
	t_uint32	dwRequiredCount;
	t_uint32	dwFreeCount;		// NOSPC error가 발생할 경우 free cluster cache를 채우기 위함.

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(dwCount > 0);

	// check hint cluster
	if (FFATFS_IS_VALID_CLUSTER(pVI, dwHint) == FFAT_FALSE)
	{
		if (FFATFS_IS_VALID_FREE_CLUSTER_HINT(pVI) == FFAT_TRUE)
		{
			dwHint = VIC_FCH(&pVI->stVolInfoCache);
		}
		else
		{
			dwHint = 2;		// set it to the first cluster
		}
	}

	// check is there enough free cluster
	if (VI_FCC(pVI) == 0)
	{
		VC_CC(pVC) = 0;
		VC_VEC(pVC) = 0;
		// not enough free cluster
		FFAT_LOG_PRINTF((_T("Not enough free cluster")));
		FFAT_DEBUG_FAT_PRINTF((_T("Free Cluster Count / required count : %d/%d \n"), VIC_FCC(&pVI->stVolInfoCache), dwCount));
		return FFAT_ENOSPC;
	}

	// free cluster lookup sequence 
	// 1. get free cluster from hint to the end of cluster
	// 2. get free cluster from 1st to (hint -1)

	dwRequiredCount = dwCount;

	// phase 1
	r = pVI->pVolOp->pfGetFreeFromTo(pVI, dwHint, VI_LCN(pVI),
									dwRequiredCount, pVC, &dwCount, bGetMoreCluster);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get free cluster ")));
		return r;
	}

	if ((dwCount >= dwRequiredCount) || (VC_VEC(pVC) == VC_TEC(pVC)))
	{
		// fully allocated or no more free space on the VC
		return FFAT_OK;
	}

	if (dwHint == 2)
	{
		// not enough free cluster
		FFAT_LOG_PRINTF((_T("Not enough free cluster")));
		FFAT_DEBUG_FAT_PRINTF((_T("Free Cluster Count / required count : %d/%d \n"), VIC_FCC(&pVI->stVolInfoCache), dwCount));

		// set new total free cluster count.
		pVI->stVolInfoCache.dwFreeClusterCount = dwCount;

		return FFAT_ENOSPC;
	}
	
	dwFreeCount		= dwCount;
	dwCount			= dwRequiredCount - dwCount;
	dwRequiredCount	= dwCount;

	// phase 2
	r = pVI->pVolOp->pfGetFreeFromTo(pVI, 2, (dwHint - 1), dwRequiredCount, pVC, &dwCount, bGetMoreCluster);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get free cluster ")));
		return r;
	}

	if (VC_VEC(pVC) == VC_TEC(pVC))
	{
		// fully allocated or no more free space on the VC
		return FFAT_OK;
	}

	if (dwRequiredCount > dwCount)
	{
		// we does not allocate enough free cluster
		FFAT_LOG_PRINTF((_T("Not enough free cluster")));
		FFAT_DEBUG_FAT_PRINTF((_T("Free Cluster Count / required count : %d/%d \n"), VIC_FCC(&pVI->stVolInfoCache), dwCount));
		FFAT_ASSERT((VI_IS_VALID_FCC(pVI) == FFAT_TRUE) ? (VI_FCC(pVI) == (dwFreeCount + dwCount)) : FFAT_TRUE);

		// set new total free cluster count.
		VI_FCC(pVI) = dwFreeCount + dwCount;

		return FFAT_ENOSPC;
	}

// debug begin
#ifdef FFAT_DEBUG
	_debugCheckClusterValidityVC(pVI, pVC);
#endif
// debug end

	return FFAT_OK;
}


/**
 * get free clusters between dwFrom and dwTo
 *
 * free cluster를 찾아 그 정보를 array 형태로 pVC에 저장한다.
 *
 * @param		pVI	: [IN] volume pointer
 * @param		dwFrom		: [IN] lookup start cluster
 * @param		dwTo		: [IN] lookup end cluster
 * @param		dwCount		: [IN] free cluster request count
 * @param		pVC			: [IN/OUT] free cluster storage, array
 * @param		dwHint		: [IN] free cluster hint
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOSPC	: not enough free clusters
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_getFreeClustersFromTo(FatVolInfo* pVI, t_uint32 dwHint, 
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwFreeCount, t_boolean bGetMoreCluster)
{
	FFatErr		r;
	t_uint32	dwRequiredCount;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(dwFrom <= dwTo);
	FFAT_ASSERT(pdwFreeCount);

	// check hint cluster
	if (FFATFS_IS_VALID_CLUSTER(pVI, dwHint) == FFAT_FALSE)
	{
		if (FFATFS_IS_VALID_FREE_CLUSTER_HINT(pVI) == FFAT_TRUE)
		{
			dwHint = VIC_FCH(&pVI->stVolInfoCache);
		}
		else
		{
			dwHint = 2;		// set it to the first cluster
		}
	}

	// check is there enough free cluster
	if (VI_FCC(pVI) < dwCount)
	{
		// not enough free cluster
		FFAT_LOG_PRINTF((_T("Not enough free cluster")));
		FFAT_DEBUG_FAT_PRINTF((_T("Free Cluster Count / required count : %d/%d \n"), VIC_FCC(&pVI->stVolInfoCache), dwCount));
		return FFAT_ENOSPC;
	}

	if (dwFrom > dwTo)
	{
		// Nothing to do
		return FFAT_OK;
	}

	// check hint validity
	if ((dwHint > dwTo) || (dwHint < dwFrom))
	{
		dwHint = dwFrom;
	}

	// free cluster lookup sequence 
	// 1. get free cluster from hint to the end of cluster
	// 2. get free cluster from 1st to (hint -1)
	dwRequiredCount = dwCount;

	// phase 1
	r = pVI->pVolOp->pfGetFreeFromTo(pVI, dwHint, dwTo,
								dwRequiredCount, pVC, &dwCount, bGetMoreCluster);
	
	FFAT_ASSERT((r < 0) && (r != FFAT_ENOSPC) ? (dwCount == 0) : FFAT_TRUE);

	*pdwFreeCount += dwCount;

	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get free cluster ")));
		return r;
	}

	if ((dwCount >= dwRequiredCount) || (VC_VEC(pVC) == VC_TEC(pVC)))
	{
		// fully allocated or no more free space on the FVC
		return FFAT_OK;
	}

	if (dwHint == 2)
	{
		// not enough free cluster
		return FFAT_ENOSPC;
	}

	dwCount			= dwRequiredCount - dwCount;
	dwRequiredCount	= dwCount;

	// phase 2
	r = pVI->pVolOp->pfGetFreeFromTo(pVI, dwFrom, (dwHint - 1),
								dwRequiredCount, pVC, &dwCount, bGetMoreCluster);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get free cluster ")));
		return r;
	}

	*pdwFreeCount += dwCount;

	if (dwRequiredCount > dwCount)
	{
		// we does not allocate all required free cluster
		r = FFAT_ENOSPC;
	}
	else
	{
		r = FFAT_OK;
	}

// debug begin
#ifdef FFAT_DEBUG
	_debugCheckClusterValidityVC(pVI, pVC);
#endif
// debug end

	return r;
}


/**
 * get free clusters count at a FAT sector
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwSector		: [IN] Fat Sector Number
 * @param		pdwFreeCount	: [OUT] found free cluster count
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 * @version		OCT-11-2008 [DongYoung Seo] ADD ASSERT to check validity of dwSector
 * @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
  */
FFatErr
ffat_fs_fat_getFreeClusterCountAt(FatVolInfo* pVI, t_uint32 dwSector, t_int32* pdwFreeCount)
{
	FFatfsCacheEntry*	pEntry = NULL;
	t_int32				dwLastIndex;
	FFatErr				r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(dwSector <= VI_LVFSFF(pVI));

	if (dwSector > VI_LFSFF(pVI))
	{
		FFAT_ASSERT(dwSector > VI_FSC(pVI));
		dwSector -= VI_FSC(pVI);
	}

	FFAT_ASSERT(dwSector <= VI_LVFSFF(pVI));

	*pdwFreeCount = 0;

	if (dwSector == VI_LVFSFF(pVI))
	{
		dwLastIndex = VI_LCN(pVI) & VI_CCPFS_MASK(pVI);
	}
	else
	{
		dwLastIndex = VI_CCPFS(pVI) - 1;
	}

	r = ffat_fs_cache_getSector(dwSector, FFAT_CACHE_DATA_FAT, &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get a sector")));

	if (FFATFS_IS_FAT32(pVI) == FFAT_TRUE)
	{
		*pdwFreeCount = _getFreeClusterCount32(pEntry->pBuff, dwLastIndex);
	}
	else
	{
		*pdwFreeCount = _getFreeClusterCount16(pEntry->pBuff, dwLastIndex);
	}

	r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
	FFAT_ER(r, (_T("fail to put sector")));

	return FFAT_OK;
}


/**
 * get free clusters from dwFrom to dwTo for FAT16
 *
 * free cluster를 찾아 그 번호를 pdwClusters에 저장한다.
 * 이 함수를 수정 할 때는 비슷한 ffat_fs_fat_getFreeFromTo32 도 같이 체크 바람
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwFrom			: [IN] lookup start cluster
 * @param		dwTo			: [IN] lookup end cluster
 * @param		dwCount			: [IN] cluster count
 * @param		pVC			: [IN/OUT] cluster storage
 * @param		pdwFreeCount	: [OUT] found free cluster count
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: no more free space on the volume
 *									no more free entry at pVC
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		MAY-08-2007 [DongYoung Seo] Add checking routine for dwTo
 * @version		JAN-21-2008 [DongYoung Seo] Add stop code when rouine meet to the request count
 */
FFatErr
ffat_fs_fat_getFreeFromTo16(FatVolInfo* pVI,t_uint32 dwFrom, t_uint32 dwTo, 
							t_uint32 dwCount, FFatVC* pVC, t_uint32* pdwFreeCount, t_boolean bGetMoreCluster)
{
	t_uint32		dwSector;
	t_uint32		i;
	t_uint16*		p16;

	t_uint32		j;
	t_uint32		dwClusterPerFatSector;
	t_uint32		dwClusterPerFatSectorMask;
	t_uint32		dwFreeCount = 0;		// free cluster count
	t_uint32		dwPrev;
	t_int32			dwCurEntry;		// current VC entry
	t_uint32		dwCurCluster;
	FFatErr			r = FFAT_OK;
	t_uint32		dwOffset;			// FAT sector offset, 2nd FAT sector에 대한 처리를 위함
	FFatCacheFlag	dwCacheFlag;

	FFatfsCacheEntry*	pEntry = NULL;

	FFAT_ASSERT(dwFrom >= 2);
	FFAT_ASSERT(dwTo <= VI_LCN(pVI));
	FFAT_ASSERT(pdwFreeCount);

	dwClusterPerFatSector		= VI_CCPFS(pVI);
	dwClusterPerFatSectorMask	= VI_CCPFS_MASK(pVI);

	if (dwFrom > dwTo)
	{
		*pdwFreeCount = 0;
		return FFAT_OK;
	}

	if (pVC->dwValidEntryCount)
	{
		dwCurEntry	= pVC->dwValidEntryCount - 1;
		dwPrev		= pVC->pVCE[dwCurEntry].dwCluster + pVC->pVCE[dwCurEntry].dwCount - 1;
	}
	else
	{
		dwCurEntry = -1;
		dwPrev = 0;
	}

	dwOffset = 0;
	dwCacheFlag = FFAT_CACHE_LOCK | FFAT_CACHE_DATA_FAT;

	for (i = dwFrom; i <= dwTo; i = i + dwClusterPerFatSector)
	{
		dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, i) + dwOffset;

		r = ffat_fs_cache_getSector(dwSector, dwCacheFlag, &pEntry, pVI);
		FFAT_ER(r, (_T("fail to get a cache for FAT sector")));

		j = i & dwClusterPerFatSectorMask;

		p16 = (t_uint16*)pEntry->pBuff;

		i = i & (~dwClusterPerFatSectorMask);

		if ((i + dwClusterPerFatSector) > dwTo)
		{
			// 마지막 cluster를 넘어서지 않게 조절.
			dwClusterPerFatSector = dwTo & dwClusterPerFatSectorMask;
			dwClusterPerFatSector += 1;		// for 문의 조건이 < 이므로 1을 더해준다.
		}

		for (/* NOTHING */ ; j < dwClusterPerFatSector; j++)
		{
			if (p16[j] == FAT16_FREE)
			{
				dwCurCluster = i + j;	// get cluster number

				FFAT_ASSERT((dwCurCluster <= dwTo) && (dwCurCluster >= dwFrom));

				if ((dwPrev + 1) == dwCurCluster)
				{
					pVC->pVCE[dwCurEntry].dwCount++;
				}
				else
				{
					// dwCurEntry never over come dwTotalEntryCount
					FFAT_ASSERT(dwCurEntry < pVC->dwTotalEntryCount);

					if ((dwCurEntry + 1) == pVC->dwTotalEntryCount)
					{
						// there is no more free area
						break;
					}

					dwCurEntry++;

					pVC->pVCE[dwCurEntry].dwCluster	= dwCurCluster;
					pVC->pVCE[dwCurEntry].dwCount	= 1;
				}

				dwPrev = dwCurCluster;

				dwFreeCount++;
				pVC->dwTotalClusterCount++;

				if (bGetMoreCluster == FFAT_FALSE)
				{
					if (dwFreeCount == dwCount)
					{
						r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
						FFAT_EO(r, (_T("fail to put sector")));

						// get got all required clusters
						goto out;
					}
				}
			}
		}

		r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
		FFAT_ER(r, (_T("fail to put sector")));

		// dwCurEntry never over come dwTotalEntryCount
		FFAT_ASSERT(dwCurEntry < VC_TEC(pVC));

		if ((dwCurEntry + 1) == VC_TEC(pVC))
		{
			// there is no more free area
			if (dwFreeCount >= dwCount)
			{
				r = FFAT_OK;
			}
			else
			{
				r = FFAT_ENOSPC;
			}

			break;
		}

		if (dwFreeCount >= dwCount)
		{
			break;
		}
	}

out:
	pVC->dwValidEntryCount = dwCurEntry + 1;

	FFAT_ASSERT(VC_TEC(pVC) >= VC_VEC(pVC));

	*pdwFreeCount = dwFreeCount;

	return r;
}


/**
 * get free clusters from dwFrom to dwTo for FAT32
 *
 * free cluster를 찾아 그 번호를 pdwClusters에 저장한다.
 * 이 함수를 수정 할 때는 비슷한 ffat_fs_fat_getFreeFromTo16 도 같이 체크 바람
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwFrom			: [IN] lookup start cluster
 * @param		dwTo			: [IN] lookup end cluster
 * @param		pVC			: [IN/OUT] free cluster storage
 * @param		dwCount			: [IN] cluster count
 * @param		pdwFreeCount	: [OUT] found free cluster count
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: no more free space on the volume
 *									no more free entry at pVC
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_getFreeFromTo32(FatVolInfo* pVI,t_uint32 dwFrom, t_uint32 dwTo, 
							t_uint32 dwCount, FFatVC* pVC,
							t_uint32* pdwFreeCount, t_boolean bGetMoreCluster)
{
	t_uint32			dwSector;
	t_uint32			i;
	t_uint32*			p32;

	t_uint32			j;
	t_uint32			dwClusterPerFatSector;
	t_uint32			dwClusterPerFatSectorMask;
	t_uint32			dwFreeCount = 0;		// free cluster count
	t_uint32			dwPrev;
	t_int32				dwCurEntry;		// current VC entry
	t_uint32			dwCurCluster;
	FFatErr				r = FFAT_OK;
	t_uint32			dwOffset;			// FAT sector offset, 2nd FAT sector에 대한 처리를 위함
	FFatCacheFlag		dwCacheFlag;


	FFatfsCacheEntry*	pEntry = NULL;

	FFAT_ASSERT(dwFrom >= 2);
	FFAT_ASSERT(dwTo <= VI_LCN(pVI));
	FFAT_ASSERT(pdwFreeCount);

	dwClusterPerFatSector		= VI_CCPFS(pVI);
	dwClusterPerFatSectorMask	= VI_CCPFS_MASK(pVI);

	if (dwFrom > dwTo)
	{
		*pdwFreeCount = 0;
		return FFAT_OK;
	}

	if (pVC->dwValidEntryCount)
	{
		dwCurEntry	= pVC->dwValidEntryCount - 1;
		dwPrev		= pVC->pVCE[dwCurEntry].dwCluster + pVC->pVCE[dwCurEntry].dwCount - 1;
	}
	else
	{
		dwCurEntry = -1;
		dwPrev = 0;
	}

	dwOffset = 0;

	dwCacheFlag = FFAT_CACHE_LOCK | FFAT_CACHE_DATA_FAT;

	for (i = dwFrom; i <= dwTo; i = i + dwClusterPerFatSector)
	{
		dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, i) + dwOffset;
	
		r = ffat_fs_cache_getSector(dwSector, dwCacheFlag, &pEntry, pVI);
		FFAT_ER(r, (_T("fail to get a cache for FAT sector")));

		j = i & dwClusterPerFatSectorMask;

		p32 = (t_uint32*)pEntry->pBuff;

		i = i & (~dwClusterPerFatSectorMask);

		// adjust dwClusterPerFatSector for dwTo.

		if ( (i + dwClusterPerFatSector) > dwTo)
		{
			// 마지막 cluster를 넘어서지 않게 조절.
			dwClusterPerFatSector = dwTo & dwClusterPerFatSectorMask;
			dwClusterPerFatSector += 1;		// for 문의 조건이 < 이므로 1을 더해준다.
		}

		for (/* NOTHING */ ; j < dwClusterPerFatSector; j++)
		{
			if ((FFAT_BO_UINT32(p32[j]) & FAT32_MASK) == FAT32_FREE)
			{
				dwCurCluster = i + j;	// get cluster number

				FFAT_ASSERT((dwCurCluster <= dwTo) && (dwCurCluster >= dwFrom));

				if ((dwPrev + 1) == dwCurCluster)
				{
					pVC->pVCE[dwCurEntry].dwCount++;
				}
				else
				{
					// dwCurEntry never over come dwTotalEntryCount
					FFAT_ASSERT(dwCurEntry < pVC->dwTotalEntryCount);

					if ((dwCurEntry + 1) == pVC->dwTotalEntryCount)
					{
						// there is no more free area
						break;
					}

					dwCurEntry++;

					pVC->pVCE[dwCurEntry].dwCluster	= dwCurCluster;
					pVC->pVCE[dwCurEntry].dwCount	= 1;
				}

				dwPrev = dwCurCluster;

				dwFreeCount++;
				pVC->dwTotalClusterCount++;		// increase total cluster count in pVC

				if (bGetMoreCluster == FFAT_FALSE)
				{
					if (dwFreeCount == dwCount)
					{
						r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
						FFAT_ER(r, (_T("fail to put sector")));
						goto out;
					}
				}
			}
		}

		r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
		FFAT_ER(r, (_T("fail to put sector")));
		
		// dwCurEntry never over come dwTotalEntryCount
		FFAT_ASSERT(dwCurEntry < VC_TEC(pVC));

		if ((dwCurEntry + 1) == VC_TEC(pVC))
		{
			// there is no more free area
			if (dwFreeCount >= dwCount)
			{
				r = FFAT_OK;
			}
			else
			{
				r = FFAT_ENOSPC;
			}

			break;
		}
		
		if (dwFreeCount >= dwCount)
		{
			break;
		}
	}

out:
	pVC->dwValidEntryCount = dwCurEntry + 1;

	FFAT_ASSERT(VC_TEC(pVC) >= VC_VEC(pVC));

	*pdwFreeCount = dwFreeCount;

	return r;
}


/**
 * update FAT area and make cluster chain for FAT16
 * Write EOC at last cluster
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		dwClusterCount	: [IN] cluster count to be updated
 * @param		pdwClusters		: [IN] cluster storage
 * @param		dwFUFlag		: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_makeClusterChain(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwClusterCount,
						t_uint32* pdwClusters, FatUpdateFlag dwFUFlag,
						FFatCacheFlag dwCacheFlag, void* pNode)
{
	FFatErr			r;
	t_int32			i;
	t_uint32		dwSector;
	t_uint32		dwPrevSector;
	t_uint32		dwCPFSMask;

	FFatfsCacheEntry*	pEntry = NULL;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwClusterCount >= 0);
	FFAT_ASSERT(pdwClusters);

	dwCacheFlag |= FFAT_CACHE_DATA_FAT;

	if (dwFUFlag & FAT_UPDATE_FORCE)
	{
		dwCacheFlag |= FFAT_CACHE_FORCE;
	}

	dwCPFSMask = VI_CCPFS_MASK(pVI);

	if (dwPrevEOF == 0)
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, pdwClusters[0]);
	}
	else
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwPrevEOF);
	}

	r = ffat_fs_cache_getSector(dwPrevSector, (dwCacheFlag | FFAT_CACHE_LOCK), &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get a sector buffer from FFATFS CACHE")));

	if (dwPrevEOF != 0)
	{
		FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwPrevEOF, pdwClusters[0], dwCPFSMask);
		FFAT_DEBUG_FAT_UPDATE(dwPrevEOF, pdwClusters[0]);
	}

// debug begin
#ifdef FFAT_DEBUG
	// check cluster validity
	_debugCheckClusterValidity(pVI, pdwClusters, dwClusterCount);
#endif
// debug end

	// update FAT area except last one.
	for (i = 0; i < dwClusterCount; i++)
	{
		dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, pdwClusters[i]);

		if (dwSector != dwPrevSector)
		{
			// set dirty
			FFATFS_CACHE_SET_DIRTY(pEntry);

			// put sector
			r = ffat_fs_cache_putSector(pVI, pEntry, (dwCacheFlag | FFAT_CACHE_DIRTY), pNode);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to put FFatfsCache entry")));
				goto roll_back;
			}
			
			dwPrevSector = dwSector;

			// get new sector buffer
			r = ffat_fs_cache_getSector(dwPrevSector, (dwCacheFlag | FFAT_CACHE_LOCK), &pEntry, pVI);
			FFAT_ER(r, (_T("fail to get a sector buffer from FFATFS CACHE")));
		}

		if (i == (dwClusterCount - 1))
		{
			FFATFS_UPDATE_FAT_BUFFER_EOC(pVI, pEntry->pBuff, pdwClusters[i], dwCPFSMask);
			FFAT_DEBUG_FAT_UPDATE( pdwClusters[i], FFATFS_IS_FAT32(pVI) ? FAT32_EOC : FAT16_EOC);

			// put sector
			r = ffat_fs_cache_putSector(pVI, pEntry, (dwCacheFlag | FFAT_CACHE_DIRTY), pNode);
			FFAT_ER(r, (_T("Fail to put FFatfsCache entry")));

			break;
		}
		else
		{
			FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, pdwClusters[i], pdwClusters[i+1], dwCPFSMask);
			FFAT_DEBUG_FAT_UPDATE(pdwClusters[i], pdwClusters[i + 1]);
		}
	}

	return FFAT_OK;

roll_back:

	// error 발생시 roll back을 수행한다.
	_makeClusterChainRollBack(pVI, dwPrevEOF, i, pdwClusters, dwCacheFlag, pNode);
	return r;
}


/**
 * update FAT area and make cluster chain with VC
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		pVC			: [IN] Vectored Cluster Information
 * @param		dwFUFlag		: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_makeClusterChainVC(FatVolInfo* pVI, t_uint32 dwPrevEOF,
					FFatVC* pVC, FatUpdateFlag dwFUFlag,
					FFatCacheFlag dwCacheFlag, void* pNode)
{
	FFatErr			r;
	t_int32			i, j;
	t_uint32		dwSector;
	t_uint32		dwPrevSector;
	t_uint32		dwCPFSMask;
	t_uint32		dwCurCluster;		// current cluster number
	t_uint32		dwNextCluster;		// next cluster number

// debug begin
#ifdef FFAT_DEBUG
	t_uint32		dwUpdatedClusterCount = 0;
#endif
// debug end

	FFatfsCacheEntry*	pEntry = NULL;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);

	if (pVC->dwValidEntryCount == 0)
	{
		return FFAT_OK;
	}

	dwCacheFlag |= FFAT_CACHE_DATA_FAT;

	if (dwFUFlag & FAT_UPDATE_FORCE)
	{
		dwCacheFlag |= FFAT_CACHE_FORCE;
	}

	dwCPFSMask = VI_CCPFS_MASK(pVI);

	if (dwPrevEOF == 0)
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, pVC->pVCE[0].dwCluster);
	}
	else
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwPrevEOF);
	}

	r = ffat_fs_cache_getSector(dwPrevSector, (dwCacheFlag | FFAT_CACHE_LOCK),
								&pEntry, pVI);
	FFAT_ER(r, (_T("fail to get a sector buffer from FFATFSCACHE")));

	if (dwPrevEOF != 0)
	{
		FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwPrevEOF, pVC->pVCE[0].dwCluster, dwCPFSMask);
		FFAT_DEBUG_FAT_UPDATE(dwPrevEOF, pVC->pVCE[0].dwCluster);
	}

// debug begin
	// check cluster validity
#ifdef FFAT_DEBUG
	_debugCheckClusterValidityVC(pVI, pVC);
#endif
// debug end

	// update FAT area except last one.
	for (i = 0; i < pVC->dwValidEntryCount; i++)
	{
		for (j = 0; j < (t_int32)pVC->pVCE[i].dwCount; j++)
		{
			dwCurCluster = pVC->pVCE[i].dwCluster + j;

			dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCurCluster);

			if (dwSector != dwPrevSector)
			{
				// set dirty
				pEntry->dwFlag |= (FFAT_CACHE_DIRTY | dwCacheFlag);
				
				// put sector
				r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DIRTY | dwCacheFlag), pNode);
				IF_UK (r < 0)
				{
					FFAT_LOG_PRINTF((_T("Fail to put FFatfsCache entry")));
					goto roll_back;
				}

				dwPrevSector = dwSector;

				// get new sector buffer
				r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_LOCK | dwCacheFlag), &pEntry, pVI);
				FFAT_ER(r, (_T("fail to get a sector buffer from FFATFSCACHE")));
			}

			if ((i == (pVC->dwValidEntryCount - 1)) &&
				(j == (t_int32)(pVC->pVCE[i].dwCount - 1)))
			{
				FFATFS_UPDATE_FAT_BUFFER_EOC(pVI, pEntry->pBuff, dwCurCluster, dwCPFSMask);
				FFAT_DEBUG_FAT_UPDATE(dwCurCluster, VI_EOC(pVI));
				// put sector
				r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DIRTY | dwCacheFlag), pNode);
				FFAT_ER(r, (_T("Fail to put FFatfsCache entry")));

// debug begin
#ifdef FFAT_DEBUG
				dwUpdatedClusterCount++;
#endif
// debug end

				break;
			}
			else
			{
				if (j == (t_int32)(pVC->pVCE[i].dwCount - 1))
				{
					dwNextCluster = pVC->pVCE[i + 1].dwCluster;
				}
				else
				{
					dwNextCluster = dwCurCluster + 1;
				}

				FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwCurCluster, dwNextCluster, dwCPFSMask);
				FFAT_DEBUG_FAT_UPDATE(dwCurCluster, dwNextCluster);
			}
// debug begin
#ifdef FFAT_DEBUG
			dwUpdatedClusterCount++;
#endif
// debug end
		}
	}

	FFAT_ASSERT((t_int32)dwUpdatedClusterCount == pVC->dwTotalClusterCount);

	return FFAT_OK;

roll_back:
	// error 발생시 roll back을 수행한다.
	_makeClusterChainRollBackVC(pVI, dwPrevEOF, i, j, pVC, 
						(dwCacheFlag | FFAT_CACHE_FORCE), pNode);

	return r;
}


/**
 * get cluster information.
 *
 * vector 형태로 cluster의 정보를 구한다.
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 *
 * 주의!!! 
 *	FFAT_OK를 return 하더라도 dwCount 만큼의 cluster 정보가 pVC에 저장되어 있지 않을 수 있다.
 *	(dwCount 이전에 EOC 를 만날 경우)
 *	저장된 cluster의 수는 반드시 pVC->dwTotalClusterCount 를 통해 확인해야 한다.
 * 
 * pVC에 valid 한 cluster 정보가 있다면 누적하여 저장한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number for VC.
 *									this is the first cluster of new entry at pVC
 * @param		dwCount			: [IN] max cluster count to fill pVC
 *									0 : fill until pVC is full or till EOF
 * @param		bGetContiguous	: [IN] if this flag is TRUE, get last VCE to contiguous cluster
 * @param		pVC				: [OUT] vectored cluster information
 * @return		FFAT_OK			: success,
 *									pVC에 dwCount 만큼의 cluster가 저장되어 있지 않을 수 있다.
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		DEC-16-2008 [GwangOk Go] add bGetContiguous flag
 */
FFatErr
ffat_fs_fat_getVectoredCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwCount,
								t_boolean bGetContiguous, FFatVC* pVC)
{
	FFatErr		r = FFAT_OK;
	t_uint32	dwPrevCluster;
	t_int32		dwIndex;		// current VCE index;
	
	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pVC->pVCE != NULL);
	FFAT_ASSERT(dwCount >= 0);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	// dwCount가 0일 경우는 메모리가 허용하는 만큼 채운다.
	if (dwCount == 0)
	{
		dwCount = VI_CC(pVI);		// max cluster count 
	}

	dwPrevCluster	= dwCluster;
	if ((VC_VEC(pVC) > 0) && (VC_LC(pVC) == (dwCluster - 1)))
	{
		// contiguous
		dwIndex			= VC_LEI(pVC);
		pVC->pVCE[dwIndex].dwCount++;
	}
	else
	{
		if (VC_VEC(pVC) >= VC_TEC(pVC))
		{
			FFAT_ASSERT(VC_VEC(pVC) == VC_TEC(pVC));
			// no more free entry
			return FFAT_OK;
		}

		dwIndex							= VC_VEC(pVC);
		pVC->pVCE[dwIndex].dwCluster	= dwCluster;
		pVC->pVCE[dwIndex].dwCount		= 1;
		pVC->dwValidEntryCount++;
	}

	pVC->dwTotalClusterCount++;

	dwCount--;

	while ((dwCount > 0) || (bGetContiguous == FFAT_TRUE))
	{
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		FFAT_ER(r, (_T("fail to get next cluster")));

		if (dwCluster == (dwPrevCluster + 1))
		{
			// contiguous cluster
			pVC->pVCE[dwIndex].dwCount++;
		}
		else
		{
			if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
			{
				// end of chain
				r = FFAT_OK;
				break;
			}
			else if (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
			{
				// invalid cluster
				r = FFAT_EFAT;
				FFAT_LOG_PRINTF((_T("invalid cluster")));
				break;
			}
			else if ((dwCount == 0) && (bGetContiguous == FFAT_TRUE))
			{
				r = FFAT_OK;
				break;
			}

			dwIndex++;

			if (dwIndex >= VC_TEC(pVC))
			{
				// there is not enough entry
				dwIndex--;
				r = FFAT_OK;
				break;
			}

			pVC->pVCE[dwIndex].dwCluster	= dwCluster;
			pVC->pVCE[dwIndex].dwCount		= 1;
		}

		dwPrevCluster = dwCluster;
		pVC->dwTotalClusterCount++;

		if (dwCount > 0)
		{
			dwCount--;
		}
	}

	pVC->dwValidEntryCount = dwIndex + 1;

	return r;
}


/**
 * deallocate clusters
 * Cluster의 할당을 해제 한다.
 *
 * backward 방식으로 해제를 수행한다.
 * TFS4 1.x 와 같은 방식으로 수행한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 *										It may not correct.
 * @param		pAlloc			: [IN] allocated cluster information
 *										It may not have all cluster number.
 * @param		pdwDeallocCount	: [IN] deallocated cluster count
 * @param		pdwFristDealloc	: [IN/OUT] the first deallocated cluster number
 *										may be NULL
 * @param		dwFAFlag		: [IN] deallocation flag
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @version		MAR-20-2009 [DongYoung Seo] add entry count for pAlloc->pVC checking code 
 *											pAllocate->pVC may not have valid information
 */
FFatErr
ffat_fs_fat_deallocateCluster(FatVolInfo* pVI, t_uint32 dwCount,
								FatAllocate* pAlloc, t_uint32* pdwDeallocCount,
								t_uint32* pdwFirstDealloc, 
								FatAllocateFlag dwFAFlag, FFatCacheFlag dwCacheFlag,
								void* pNode)
{
	t_uint32		dwFreeCount;	// deallocated cluster count
	t_uint32		dwFirstFree;
	FFatErr			r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT(pdwDeallocCount);

	FFAT_ASSERT((dwFAFlag & FAT_ALLOCATE_FORCE) ? (dwCacheFlag & FFAT_CACHE_FORCE) : FFAT_TRUE);

	// VC에 모든 cluster 정보가 포함되어 있을 경우에는 pVC로 처리한다.
	// be careful!!. this function have a relation with log recovery.
	if ((pAlloc->pVC != NULL) && (dwCount <= pAlloc->pVC->dwTotalClusterCount)
			&& (VC_VEC(pAlloc->pVC) > 0))
	{
		r = _deallocateClusterVC(pVI, pAlloc->dwPrevEOF, 0, 0, pAlloc->pVC,
					&dwFreeCount, &dwFirstFree, dwFAFlag, dwCacheFlag, pNode);
	}
	else
	{
		r = _deallocateCluster(pVI, pAlloc->dwPrevEOF, pAlloc->dwFirstCluster,
					&dwFreeCount, &dwFirstFree, dwFAFlag, dwCacheFlag, pNode);
	}
	FFAT_ER(r, (_T("Fail to deallocate clusters")));

	//20100413_sks => Change to add the cluster notification function FFATFS_INC_FREE_CLUSTER_COUNT(pVI, dwFreeCount);
	if (FFATFS_IsValidFreeClusterCount(pVI) == FFAT_TRUE)
	{
		pVI->stVolInfoCache.dwFreeClusterCount += dwFreeCount;
		FFAT_CLUSTER_CHANGE_NOTI(pVI->stVolInfoCache.dwFreeClusterCount, pVI->dwClusterCount,pVI->dwClusterSize,pVI->pDevice);
	}
	else
	{
		FFAT_CLUSTER_CHANGE_NOTI(INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,NULL);
	}

	*pdwDeallocCount += dwFreeCount;

	IF_LK (dwFirstFree != 0)
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwFirstFree) == FFAT_TRUE);
		VI_SET_FCH(pVI, dwFirstFree);
	}

	IF_LK (pdwFirstDealloc != NULL)
	{
		// set first deallocated cluster
		*pdwFirstDealloc = dwFirstFree;
	}

	// the first free cluster must be valid when deallocated cluster count is not zero
	FFAT_ASSERT(dwFreeCount > 0 ? FFATFS_IS_VALID_CLUSTER(pVI, dwFirstFree) == FFAT_TRUE : FFAT_TRUE);

	return FFAT_OK;
}


/**
 * deallocate clusters
 * Cluster의 할당을 해제 한다.
 *
 * backward 방식으로 해제를 수행한다.
 * TFS4 1.x 와 같은 방식으로 수행한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 *									It may not correct.
 * @param		pAlloc			: [IN] allocated cluster information
 *									It may not have all cluster number.
 * @param		pdwDeallocCount	: [IN] deallocated cluster count
 * @param		dwFAFlag		: [IN] deallocation flag
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @return		FFAT_OK			: success
 * @author		Soojeong Kim
 * @version		AUG-27-2007 [Soojeong Kim] First Writing.
 */
FFatErr			
ffat_fs_fat_deallocateClusterFromTo(FatVolInfo* pVI, t_uint32 dwPostEOF, t_uint32 dwCluster, t_uint32 dwCount,
									t_uint32* pdwDeallocCount, FatAllocateFlag dwFAFlag,
									FFatCacheFlag dwCacheFlag, void* pNode)
{
	FFatVC		stVC;
	FFatVCE		stVCE;
	
	FFatErr		r;

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	VC_INIT(&stVC, VC_NO_OFFSET);

	stVCE.dwCluster			= dwCluster;
	stVCE.dwCount			= dwCount;
	stVC.pVCE				= &stVCE;
	stVC.dwTotalEntryCount	= 1;
	stVC.dwValidEntryCount	= 1;
	stVC.dwTotalClusterCount = dwCount;

	r = ffat_fs_fat_freeClustersVC(pVI, &stVC, pdwDeallocCount, dwFAFlag, dwCacheFlag, pNode);
	FFAT_ER(r, (_T("fail to free clusters")));

	FFAT_ASSERT(dwCount == (*pdwDeallocCount));

	// eof 설정이 필요할 경우 설정한다.
	if (dwPostEOF != 0)
	{
		r = FFAT_FS_FAT_UPDATECLUSTER(pVI, dwPostEOF, pVI->dwEOC, dwCacheFlag, pNode);
		if (r < 0)
		{
			if ((dwFAFlag & FAT_ALLOCATE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get sector")));
				goto out;
			}
		}
	}

out:
	return r;
}

/**
 * update a cluster for FAT16
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwValue			: [IN] cluster data
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_fat_updateCluster16(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwValue,
							FFatCacheFlag dwFlag, void* pNode)
{

	FFatfsCacheEntry*	pEntry = NULL;
	t_uint32			dwSector;
	FFatErr				r;

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCluster);
	r = ffat_fs_cache_getSector(dwSector, (dwFlag | FFAT_CACHE_LOCK), &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get sector")));

	FFATFS_UPDATE_FAT16((t_uint16*)pEntry->pBuff, dwCluster, dwValue, VI_CCPFS_MASK(pVI));
	FFAT_DEBUG_FAT_UPDATE(dwCluster, dwValue);

	r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
	FFAT_ER(r, (_T("fail to put sector")));

	return FFAT_OK;
}


/**
 * update a cluster for FAT32
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwValue			: [IN] cluster data
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_fat_updateCluster32(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwValue,
							FFatCacheFlag dwFlag, void* pNode)
{

	FFatfsCacheEntry*	pEntry = NULL;
	t_uint32			dwSector;
	FFatErr				r;

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCluster);
	r = ffat_fs_cache_getSector(dwSector, dwFlag | FFAT_CACHE_LOCK, &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get sector")));

	FFATFS_UPDATE_FAT32((t_uint32*)pEntry->pBuff, dwCluster, dwValue, VI_CCPFS_MASK(pVI));
	FFAT_DEBUG_FAT_UPDATE(dwCluster, dwValue);

	r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
	FFAT_ER(r, (_T("fail to put sector")));

	return FFAT_OK;
}


/**
 * Initialize clusters
 * cluster에 대한 초기화를 수행한다.
 *
 * @param		pVI	: [IN] volume pointer
 * @param		dwCluster	: [IN] cluster information
 * @param		dwCount		: [IN] cluster count
 * @param		pBuff		: [IN] buffer pointer
 *								may be NULL when there is no buffer
 *								NULL이 아닐 경우는 반드시 0x00로 초기화된 buffer이어야 한다.
 * @param		dwSize		: [IN] buffer size
 * @param		dwFlag		: [IN] cache flag
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		FEB-15-2009 [DongYoung Seo] discard caches within initialization area
 *
 */
FFatErr
ffat_fs_fat_initCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwCount,
						t_int8* pBuff, t_int32 dwSize, 
						FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FFatErr			r;
	t_uint32		dwSector;
	t_int32			dwSectorCount;			// sector count per a write
	t_int32			dwTotalSectorCount;
	t_boolean		bAlloc = FFAT_FALSE;	// boolean for memory allocation

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(pCI);

	if ((pBuff == NULL) || (dwSize < VI_SS(pVI)))
	{
		pBuff = (t_int8*)FFAT_LOCAL_ALLOC(FFAT_CLUSTER_INIT_BUFF_SIZE, VI_CXT(pVI));
		FFAT_ASSERT(pBuff);

		dwSize = FFAT_CLUSTER_INIT_BUFF_SIZE;
		FFAT_MEMSET(pBuff, 0x00, dwSize);

		bAlloc = FFAT_TRUE;
	}

	dwSize = dwSize & (~VI_SSM(pVI));
	dwSectorCount = dwSize >> VI_SSB(pVI);

	dwTotalSectorCount = dwCount * VI_SPC(pVI);

	dwSector = FFATFS_GetFirstSectorOfCluster(pVI, dwCluster);

	dwFlag |= (FFAT_CACHE_NOREAD | FFAT_CACHE_DIRECT_IO);

	// discard all of the sectors
	do
	{
		if (dwTotalSectorCount < dwSectorCount)
		{
			dwSectorCount = dwTotalSectorCount;
		}

		r = ffat_al_writeSector(dwSector, pBuff, dwSectorCount, dwFlag, pCI);
		IF_UK (r != dwSectorCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to init cluster")));
			r = FFAT_EIO;
			goto out;
		}

		ffat_fs_cache_discard(pVI ,dwSector, dwSectorCount);

		dwTotalSectorCount	-= dwSectorCount;
		dwSector			+= dwSectorCount;
	} while (dwTotalSectorCount > 0);

	r = FFAT_OK;

out:
	if (bAlloc)
	{
		FFAT_LOCAL_FREE(pBuff, FFAT_CLUSTER_INIT_BUFF_SIZE, VI_CXT(pVI));
	}

	return r;
}


/**
 * 하나의 cluster에 대해 일부 영역의 초기화를 수행한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwStartOffset	: [IN] start offset
 *										cluster의 크기를 초과해서는 안된다.
 * @param		dwSize			: [IN] initialization size in byte
 *										cluster size를 초과할 경우에는 무시된다.
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information for IO request
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fat_initPartialCluster(FatVolInfo* pVI, t_uint32 dwCluster,
				t_int32 dwStartOffset, t_int32 dwSize, 
				FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FFatErr			r;
	t_uint32		dwSector;
	t_int32			dwSectorCount;		// sector count per a write
	t_uint32		dwEndSector;		// init end sector number
	t_int32			dwTotalSectorCount;
	t_int32			dwBuffSize;		// memory allocation size
	t_int8*			pBuff;
	t_int32			dwEndOffset;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(FFATFS_IsValidCluster(pVI, dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset < VI_CS(pVI));
	FFAT_ASSERT(pCI);

	IF_UK (dwStartOffset > VI_CS(pVI))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter, start/end offset")));
		return FFAT_EINVALID;
	}

	dwEndOffset = dwStartOffset + dwSize - 1;

	// adjust end offset
	if (dwEndOffset >= VI_CS(pVI))
	{
		dwSize = VI_CS(pVI) - dwStartOffset;
		dwEndOffset = VI_CS(pVI) - 1;
	}

	dwBuffSize = ESS_GET_MIN(FFAT_CLUSTER_INIT_BUFF_SIZE, VI_CS(pVI));
	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(dwBuffSize, VI_CXT(pVI));
	FFAT_ASSERT(pBuff != NULL);

	dwBuffSize = dwBuffSize & (~VI_SSM(pVI));
	dwSectorCount = dwBuffSize >> VI_SSB(pVI);		// 한번에 write 할 sector의 수를 설정

	dwSector = FFATFS_GetFirstSectorOfCluster(pVI, dwCluster);

	// get end sector number
	dwEndSector = dwSector + (dwEndOffset >> VI_SSB(pVI));

	// get first sector
	dwSector = dwSector + (dwStartOffset >> VI_SSB(pVI));

	dwTotalSectorCount = dwEndSector - dwSector + 1;

	// Initialize first sector
	if (dwStartOffset & VI_SSM(pVI))
	{
		r = ffat_fs_misc_initPartialSector(pVI, dwSector, 
							(dwStartOffset & VI_SSM(pVI)), 
							dwSize,
							pBuff, dwFlag, pCI);
		FFAT_EO(r, (_T("fail to init partial sector")));

		dwTotalSectorCount--;		// sector 1개 write 되었다.
		dwSector++;

		if (dwTotalSectorCount == 0)
		{
			// init done
			r = FFAT_OK;
			goto out;
		}
	}

	if (dwTotalSectorCount > 0)
	{
		if ((dwEndOffset + 1) & VI_SSM(pVI))
		{
			// 마지막 sector에 대해서는 partial write를 해야 하므로 1 감소
			dwTotalSectorCount--;
			if (dwTotalSectorCount == 0)
			{
				goto partial;
			}
		}

		if (dwTotalSectorCount < dwSectorCount)
		{
			FFAT_MEMSET(pBuff, 0x00, dwTotalSectorCount << VI_SSB(pVI));
		}
		else
		{
			FFAT_MEMSET(pBuff, 0x00, dwBuffSize);
		}

		do
		{
			if (dwTotalSectorCount < dwSectorCount)
			{
				dwSectorCount = dwTotalSectorCount;
			}

			r = ffat_al_writeSector(dwSector, pBuff, dwSectorCount, dwFlag, pCI);
			IF_UK (r != dwSectorCount)
			{
				FFAT_LOG_PRINTF((_T("Fail to init cluster")));
				r = FFAT_EIO;
				goto out;
			}

			dwTotalSectorCount -= dwSectorCount;
			dwSector += dwSectorCount;

		} while (dwTotalSectorCount > 0);
	}

partial:
	// Initialize last sector
	if ((dwEndOffset + 1) & VI_SSM(pVI))
	{
		r = ffat_fs_misc_initPartialSector(pVI, dwEndSector, 
							0, ((dwEndOffset & VI_SSM(pVI)) + 1),
							pBuff, dwFlag, pCI);
		FFAT_EO(r, (_T("fail to init partial sector")));
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, dwBuffSize, VI_CXT(pVI));

	return r;
}


/**
 * cluster에 대해 read/write를 수행한다.
 *
 * 
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwCount			: [IN] cluster count
 * @param		pBuff			: [IN/OUT] read /write data storage
 * @param		dwFlag			: [IN] cache flag
 * @param		bRead			: [IN]	FFAT_TRUE : read
 *										FFAT_FALSE: write
 * @return		positive		: read/written cluster count
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-24-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_fs_fat_readWriteCluster(FatVolInfo* pVI, t_uint32 dwCluster,
						t_int8* pBuff, t_int32 dwCount, 
						FFatCacheFlag dwFlag, FFatCacheInfo* pCI, t_boolean bRead)
{
	FFatErr		r;
	t_uint32	dwSector;
	t_int32		dwSectorCount;		// sector count per a write

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pCI);

	dwSectorCount = dwCount << VI_SPCB(pVI);

	dwSector = FFATFS_GetFirstSectorOfCluster(pVI, dwCluster);

	r = ffat_fs_misc_readWriteSectors(pVI, dwSector, dwSectorCount, pBuff, dwFlag, pCI, bRead);
	IF_UK (r != dwSectorCount)
	{
		FFAT_LOG_PRINTF((_T("Fail to read/write cluster")));
		return FFAT_EIO;
	}

	return dwCount;
}


/**
 * 하나의 cluster에 대해 일부 영역의 read를 수행한다.
 *
 * Caution !!!
 * End offset이 cluster size를 넘어설 경우에는 이후 부분은 무시된다.
 * 
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwStartOffset	: [IN] start offset
 * @param		dwSize			: [IN] read write size
 * @param		pBuff			: [IN/OUT] read data storage
 * @param		dwFlag			: [IN] cache flag
 * @param		bRead			: [IN]	FFAT_TRUE : read
 *										FFAT_FALSE: write
 * @param		sectors			: [IN] check Cluster-unaligned-Read
 * @return		0 or above		: read / write size in byte
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
t_int32
ffat_fs_fat_readWritePartialCluster(FatVolInfo* pVI, t_uint32 dwCluster,
				t_int32 dwStartOffset, t_int32 dwSize,
				t_int8* pBuff, FFatCacheFlag dwFlag, 
				FFatCacheInfo* pCI, t_boolean bRead, t_boolean sectors)
{
	FFatErr			r;
	t_uint32		dwSector;
	t_uint32		dwStartSector;		// init start sector number
	t_uint32		dwEndSector;		// init end sector number
	t_int32			dwTotalSectorCount;
	t_int32			dwEndOffset;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(FFATFS_IsValidCluster(pVI, dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwSize >= 0);
	FFAT_ASSERT(pCI);

  // 2010.07.14_chunum.kong_Fix the bug that error code is added about Macro of Invalid cluster check, cluster 0 is returned at sector value.
  // Problem Broken BPB at RFS
	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster number")));
		return FFAT_EFAT;
	}

	IF_UK (dwStartOffset > VI_CS(pVI))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter, start/end offset")));
		return FFAT_EINVALID;
	}

	dwEndOffset = dwStartOffset + dwSize - 1;

	// adjust end offset
	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
	if (!(sectors) && (dwEndOffset >= VI_CS(pVI)))
	{
		dwEndOffset = VI_CS(pVI) - 1;
		dwSize = dwEndOffset - dwStartOffset + 1;
	}

	dwSector = FFATFS_GetFirstSectorOfCluster(pVI, dwCluster);

	// get end sector number
	dwEndSector = dwSector + (dwEndOffset >> VI_SSB(pVI));

	// get first sector
	dwStartSector = dwSector + (dwStartOffset >> VI_SSB(pVI));

	dwTotalSectorCount = dwEndSector - dwStartSector + 1;

	// read/write first sector
	if (dwStartOffset & VI_SSM(pVI))
	{
		r = ffat_fs_misc_readWritePartialSector(pVI, dwStartSector, 
						(dwStartOffset & VI_SSM(pVI)), dwSize, pBuff, dwFlag, pCI, bRead);
		FFAT_EO(r, (_T("fail to init partial sector")));

		dwTotalSectorCount--;		// sector 1개 write 되었다.
		dwStartSector++;			// 다음 write sector 증가
		// buffer pointer 증가
		pBuff += r;

		if (dwTotalSectorCount == 0)
		{
			// write done
			r = dwSize;
			goto out;
		}
	}

	if ((dwEndOffset + 1) & VI_SSM(pVI))
	{
		// 마지막 sector에 대해서는 partial write를 해야 하므로 1 감소
		dwTotalSectorCount--;
	}

	if (dwTotalSectorCount > 0)
	{
		if (bRead == FFAT_TRUE)
		{
			if (dwFlag & FFAT_CACHE_DATA_META)
			{
				r = ffat_fs_cache_readSector(dwStartSector, pBuff, dwTotalSectorCount, dwFlag, pVI);
			}
			else
			{
				r = ffat_al_readSector(dwStartSector, pBuff, dwTotalSectorCount, dwFlag, pCI);
			}
		}
		else
		{
			if (dwFlag & FFAT_CACHE_DATA_META)
			{
				r = ffat_fs_cache_writeSector(dwStartSector, pBuff, dwTotalSectorCount, dwFlag, pVI, pCI->pNode);
			}
			else
			{
				r = ffat_al_writeSector(dwStartSector, pBuff, dwTotalSectorCount, dwFlag, pCI);
			}
		}

		IF_UK (r != dwTotalSectorCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to init cluster")));
			r = FFAT_EIO;
			goto out;
		}

		dwStartSector += dwTotalSectorCount;
		pBuff += (dwTotalSectorCount << VI_SSB(pVI));	// buffer pointer 증가
	}

	// read/write last sector
	if ((dwEndOffset + 1) & VI_SSM(pVI))
	{
		FFAT_ASSERT(dwStartSector == dwEndSector);

		r = ffat_fs_misc_readWritePartialSector(pVI, dwStartSector, 
							0, ((dwEndOffset & VI_SSM(pVI)) + 1),
							pBuff, dwFlag, pCI, bRead);
		FFAT_EO(r, (_T("fail to init partial sector")));
	}

	r = dwSize;

out:
	return r;
}


/**
* Check the FAT Sector is whole free or not
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] cluster number
* @param		dwFlag			: [IN] cache flag
* @return		FFAT_TRUE		: It is a free FAT Sector
* @return		FFAT_FALSE		: It is not a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
* @version		OCT-11-2008 [DongYoung Seo] ADD ASSERT to check validity of dwSector
* @version		JAN-21-2009 [DongYoung Seo] bug fix. return value is overwitten by cache_putSector().
*/
FFatErr
ffat_fs_fat_isFreeFatSector(FatVolInfo* pVI, t_uint32 dwSector, FFatCacheFlag dwFlag)
{
	FFatfsCacheEntry*	pEntry = NULL;
	t_uint32*			pBuff;
	t_int32				i;
	FFatErr				r;
	FFatErr				rr;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT((dwSector <= VI_LVFSFF(pVI)) && (dwSector >= VI_FFS(pVI)));

	dwFlag |= FFAT_CACHE_DATA_FAT;

	r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get a sector")));

	FFAT_ASSERT(pEntry);
	FFAT_ASSERT(pEntry->pBuff);
	pBuff = (t_uint32*)pEntry->pBuff;

	if (FFATFS_IS_FAT32(pVI) == FFAT_TRUE)
	{
		for (i = ((VI_SS(pVI) / sizeof(t_uint32)) - 1); i >= 0; i--)
		{
			// ignore most 4-bit in FAT32
			if ((FFAT_BO_UINT32(pBuff[i]) & FAT32_MASK) != FAT32_FREE)
			{
				r = FFAT_FALSE;
				goto out;
			}
		}
	}
	else
	{
		for (i = ((VI_SS(pVI) / sizeof(t_uint32)) - 1); i >= 0; i--)
		{
			if (pBuff[i] != FAT16_FREE)
			{
				r = FFAT_FALSE;
				goto out;
			}
		}
	}

	r = FFAT_TRUE;

out:
	if (pEntry)
	{
		rr = ffat_fs_cache_putSector(pVI, pEntry, dwFlag, NULL);
		FFAT_ER(rr, (_T("fail to put a cache entry")));
	}

	return r;
}


/**
* get the first cluster of a FAT sector
*
* @param		pVI		: [IN] volume pointer
* @param		dwSector		: [IN] FAT Sector number
* @param		pdwClusterNo	: [IN/OUT] cluster number storage
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_fat_getFirstClusterOfFatSector(FatVolInfo* pVI, t_uint32 dwSector,
										t_uint32* pdwClusterNo)
{
	FFAT_ASSERT(pdwClusterNo);
	FFAT_ASSERT(pVI);

	FFAT_ASSERT((dwSector >= VI_FFS(pVI)) && (dwSector < (VI_FFS(pVI) + VI_FSC(pVI))));

	while (dwSector > VI_LFSFF(pVI))
	{
		dwSector -= VI_FSC(pVI);
	}

	dwSector -= VI_FFS(pVI);
	*pdwClusterNo = dwSector << VI_CCPFSB(pVI);

	return FFAT_OK;
}


/**
* Get the last cluster of a FAT sector
*
* @param		pVI		: [IN] volume pointer
* @param		dwSector		: [IN] FAT Sector number
* @param		pdwClusterNo	: [IN/OUT] cluster number storage
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_fat_getLastClusterOfFatSector(FatVolInfo* pVI, t_uint32 dwSector,
									t_uint32* pdwClusterNo)
{
	FFAT_ASSERT(pdwClusterNo);
	FFAT_ASSERT(pVI);
	
	FFAT_ASSERT((dwSector >= VI_FFS(pVI)) || (dwSector < (VI_FFS(pVI) + VI_FSC(pVI))));

	while (dwSector > VI_LFSFF(pVI))
	{
		dwSector -= VI_FSC(pVI);
	}

	if (dwSector == VI_LVFSFF(pVI))
	{
		// this is the last FAT sector
		*pdwClusterNo = VI_LCN(pVI);
	}
	else
	{
		dwSector -= VI_FFS(pVI);
		*pdwClusterNo = ((dwSector + 1) << VI_CCPFSB(pVI)) - 1;
	}

	return FFAT_OK;
}


/**
* Get free cluster count for a FAT Sector
*
* @param		pVI		: [IN] volume pointer
* @param		dwSector		: [IN] a FAT Sector number
* @param		pdwClusterCount	: [IN/OUT] cluster number storage
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
* @version		OCT-11-2008 [DongYoung Seo] ADD ASSERT to check validity of dwSector
* @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
*/
FFatErr
ffat_fs_fat_getFCCOfSector(FatVolInfo* pVI, t_uint32 dwSector,
									t_uint32* pdwClusterCount)
{
	t_int8*		pBuff = NULL;
	t_int32		dwIndex;
	FFatErr		r;

	FFAT_ASSERT(pdwClusterCount);
	FFAT_ASSERT(pVI);
	FFAT_ASSERT((dwSector >= VI_FFS(pVI)) || (dwSector < (VI_FFS(pVI) + VI_FSC(pVI))));

	while (dwSector > VI_LFSFF(pVI))
	{
		dwSector -= VI_FSC(pVI);
	}

	FFAT_ASSERT(dwSector <= VI_LVFSFF(pVI));

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VI_SS(pVI), VI_CXT(pVI));
	FFAT_ASSERT(pBuff != NULL);

	// DO NOT NEED TO ADD CACHE
	r = ffat_fs_cache_readSector(dwSector, pBuff, 1, FFAT_CACHE_DATA_FAT, pVI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read sector from ffatfs cache")));
		r =  FFAT_EIO;
		goto out;
	}

	if (dwSector == VI_LVFSFF(pVI))
	{
		dwIndex = VI_LCN(pVI) & VI_CCPFS_MASK(pVI);
	}
	else
	{
		dwIndex = VI_CCPFS(pVI) - 1;
	}

	if (FFATFS_IS_FAT32(pVI) == FFAT_TRUE)
	{
		*pdwClusterCount = _getFreeClusterCount32(pBuff, dwIndex);
	}
	else
	{
		*pdwClusterCount = _getFreeClusterCount16(pBuff, dwIndex);
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, VI_SS(pVI), VI_CXT(pVI));

	return r;
}


/**
 * update volume status in pVI
 *
 * pVI에 이미 volume 정보가 있을 경우 아무런 작업을 하지 않는다.
 *
 * @param		pVI	: [IN] volume pointer
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @remark		이 함수는 statfs()에만 불린다는거 
 */
FFatErr
ffat_fs_fat_updateStatus(FatVolInfo* pVI, t_int8* pBuff, t_int32 dwSize)
{
	FFatErr		r;
	t_int8*		pInternalBuff = NULL;
	t_uint32	dwFreeCount;

	if (FFATFS_IsValidFreeClusterCount(pVI) == FFAT_TRUE)
	{
		// 기존에 statfs가 불린적이 있다면...
		// 없다면 아랫것
		return FFAT_OK;
	}

	if ((FFATFS_STATFS_BUFF_SIZE > dwSize) || (pBuff == NULL))
	{
		pInternalBuff = (t_int8*)FFAT_LOCAL_ALLOC(FFATFS_STATFS_BUFF_SIZE, VI_CXT(pVI));
		FFAT_ASSERT(pInternalBuff != NULL);

		pBuff		= pInternalBuff;
		dwSize		= FFATFS_STATFS_BUFF_SIZE;
	}

	// sync volume before update status operation
	r = ffat_fs_cache_syncVol(pVI, FFAT_CACHE_SYNC, VI_CXT(pVI));
	FFAT_EO(r, (_T("fail to sync volume")));

	dwFreeCount = 0;
	r = _getFreeClusterCount(pVI, pBuff, dwSize, &dwFreeCount);
	FFAT_EO(r, (_T("fail to get free count")));

	pVI->stVolInfoCache.dwFreeClusterCount = dwFreeCount;

out:
	FFAT_LOCAL_FREE(pInternalBuff, FFATFS_STATFS_BUFF_SIZE, VI_CXT(pVI));

	return r;
}

/**
 * Sparse Cluster 정보를 이용하여 deallocation을 수행한다.
 *
 * @param		pVI		: [IN] volume information
 * @param		pVC			: [IN] vectored cluster information, may be NULL
 * @param		dwFirstCluster	: [IN] first cluster number to be deallocated
 * @param		pdwFreeCount	: [OUT] deallocated cluster count
 * @param		dwFlag			: [IN] allocate flag
 * @param		dwCacheFlag		: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		MAR-02-2007 [DongYoung Seo] free memory before error out
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush

 */
FFatErr
ffat_fs_fat_deallocateWithSC(FatVolInfo* pVI, FFatVC* pVC, t_uint32 dwFirstCluster,
				t_uint32* pdwFreeCount, FatAllocateFlag dwFlag,
				FFatCacheFlag dwCacheFlag, void* pNode)
{
	return _deallocateWithSC(pVI, pVC, dwFirstCluster,
									pdwFreeCount, dwFlag, dwCacheFlag, pNode);
}

/**
 * Free clusters from end of the list
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 * @param		pClusters		: [IN] clusters to be free
 * @param		dwFlag			: [IN] cache flag
 *									FFAT_CACHE_SYNC : sync FAT area after free operation
 * @param		dwAllocFlag		" [IN] flags for cluster deallocation
 *									FAT_ALLOCATE_DIR : free clusters for directory
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		SEP-25-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_fat_freeClusters(FatVolInfo* pVI, t_int32 dwCount,t_uint32* pClusters, 
						FFatCacheFlag dwFlag, FatAllocateFlag dwAllocFlag, void* pNode)
{

	FFatErr				r = FFAT_OK;
	FFatfsCacheEntry*	pEntry = NULL;
	t_int32				i;
	t_uint32			dwCurCluster;
	t_uint32			dwSector;
	t_uint32			dwPrevSector = 0;
	t_uint32			dwCPFSMask;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pClusters);

	if (dwCount == 0)
	{
		// Nothing to do
		return FFAT_OK;
	}

	dwFlag		|= FFAT_CACHE_DATA_FAT;
	dwCPFSMask	= VI_CCPFS_MASK(pVI);

	// update FAT area except last one.
	for (i = (dwCount - 1); i >= 0; i--)
	{
		dwCurCluster = pClusters[i];

		dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCurCluster);

		if (dwSector != dwPrevSector)
		{
			// set dirty
			if (pEntry)
			{
				// put sector - ignore error
				r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
				if (r < 0)
				{
					FFAT_LOG_PRINTF((_T("fail to put FFATFS CACHE")));
					if ((dwFlag & FAT_ALLOCATE_FORCE) == 0)
					{
						goto out;
					}
				}
			}

			// get new sector buffer
			r = ffat_fs_cache_getSector(dwSector, dwFlag | FFAT_CACHE_LOCK, &pEntry, pVI);
			if (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to get sector from FFatfsCache")));
				if ((dwFlag & FAT_ALLOCATE_FORCE) == 0)
				{
					goto out;
				}

				continue;
			}

			dwPrevSector = dwSector;
		}

		FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwCurCluster, FAT_FREE, dwCPFSMask);
		FFAT_DEBUG_FAT_UPDATE(dwCurCluster, FAT_FREE);

		if (dwAllocFlag & FAT_DEALLOCATE_DISCARD_CACHE)
		{
			// DON'T CARE ERROR
			ffat_fs_cache_discard(pVI,
							FFATFS_GetFirstSectorOfCluster(pVI, dwCurCluster),
							VI_SPC(pVI));
		}
	}

	if (pEntry)
	{
		// release last sector
		r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DATA_FAT | FFAT_CACHE_DIRTY), pNode);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to put sector")));
			if ((dwFlag & FAT_ALLOCATE_FORCE) == 0)
			{
				goto out;
			}
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * Free clusters from end of the list
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 * @param		pClusters		: [IN] clusters to be free
 * @param		dwFlag			: [IN] cache flag
 *									FFAT_CACHE_SYNC : sync FAT area after free operation
 * @param		dwDAFlag		: [IN] flags for cluster deallocation
 *									FAT_ALLOCATE_DIR : free clusters for directory
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		Soojeong Kim
 * @version		AUG-25-2007 [Soojeong Kim] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_fat_freeClustersVC(FatVolInfo* pVI, FFatVC* pVC,
							t_uint32* pdwFreeCount, FatDeallocateFlag dwDAFlag,
							FFatCacheFlag dwCacheFlag, void* pNode)
{
	FFatErr				r;
	FFatfsCacheEntry*	pEntry = NULL;
	t_int32				i, j;
	t_uint32			dwCurCluster;
	t_uint32			dwSector;
	t_uint32			dwPrevSector = 0;
	t_uint32			dwCPFSMask;
	t_uint32			dwCount = 0;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwFreeCount);

	dwCPFSMask	= VI_CCPFS_MASK(pVI);
	dwCacheFlag	|= FFAT_CACHE_DATA_FAT;

	// update FAT area except last one.
	for (i = (VC_VEC(pVC) - 1); i >= 0; i--)
	{
		for (j = (pVC->pVCE[i].dwCount - 1); j >= 0; j--)
		{
			dwCurCluster = pVC->pVCE[i].dwCluster + j;

			dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwCurCluster);

			if (dwSector != dwPrevSector)
			{
				// set dirty
				if (pEntry)
				{
					// put sector - ignore error
					r = ffat_fs_cache_putSector(pVI, pEntry, (dwCacheFlag | FFAT_CACHE_DIRTY), pNode);
					if (r < 0)
					{
						FFAT_LOG_PRINTF((_T("fail to put FFatfsCache")));
						if ((dwDAFlag & FAT_DEALLOCATE_FORCE) == 0)
						{
							goto out;
						}
					}
				}

				// get new sector buffer
				r = ffat_fs_cache_getSector(dwSector, (dwCacheFlag | FFAT_CACHE_LOCK), &pEntry, pVI);
				if (r < 0)
				{
					FFAT_LOG_PRINTF((_T("Fail to get sector from FFatfsCache")));
					if ((dwDAFlag & FAT_DEALLOCATE_FORCE) == 0)
					{
						goto out;
					}

					continue;
				}

				dwPrevSector = dwSector;
			}

			FFAT_ASSERT(pEntry);
			FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwCurCluster, FAT_FREE, dwCPFSMask);
			FFAT_DEBUG_FAT_UPDATE(dwCurCluster, FAT_FREE);

			if (dwDAFlag & FAT_DEALLOCATE_DISCARD_CACHE)
			{
				// DON'T CARE ERROR
				ffat_fs_cache_discard(pVI, FFATFS_GetFirstSectorOfCluster(pVI, dwCurCluster),
								VI_SPC(pVI));
			}

			dwCount++;
		}
	}

	if (pEntry)
	{
		// release last sector
		r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DIRTY | dwCacheFlag), pNode);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to put sector")));
			if ((dwDAFlag & FAT_ALLOCATE_FORCE) == 0)
			{
				goto out;
			}
		}
	}

	r = FFAT_OK;
	*pdwFreeCount = dwCount;

out:
	return r;
}


//=============================================================================
//
//static functions
//
//

/**
 * get free count
 *
 * @param		pVI		: [IN] volume pointer
 * @param		pBuff			: [IN] read buffer pointer
 * @param		dwBuffSize		: [IN] read buffer size
 * @param		pdwFreeCount	: [IN] free cluster count
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 *				AUG-17-2007 [Soojeong Kim] add FCC support(Free Cluster Cache)
 * @remark		이 함수는 mount이후 최초의 statfs()에서만 불린다.
 */
static FFatErr
_getFreeClusterCount(FatVolInfo* pVI, t_int8* pBuff, t_int32 dwBuffSize, t_uint32* pdwFreeCount)
{
	typedef t_int32		(*_GET_FREE_COUNT)(t_int8* pBuff, t_int32 dwLastIndex);

	FFatErr				r;
	t_uint32			dwSector;				// current IO sector
	t_uint32			dwRestSector;			// rest sector count
	t_int32				dwSectorCount;			// sector count per a read
	t_uint32			dwTotalFreeCount = 0;	// total free cluster count

	t_int32				dwLastIndex;		// last cluster number
	t_int32				dwFatEntrySizeBit;
	
	_GET_FREE_COUNT		pfGetFreeClusterCount;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);

	dwSector		= VI_FFS(pVI);						// get first fat sector
	dwRestSector	= ESS_MATH_CDB(VI_LCN(pVI) + 1, VI_CCPFS(pVI), VI_CCPFSB(pVI));
	dwSectorCount	= dwBuffSize >> VI_SSB(pVI);		// get sector count per a read

	if (FFATFS_IS_FAT32(pVI) == FFAT_TRUE)
	{
		dwFatEntrySizeBit = 2;
		pfGetFreeClusterCount = _getFreeClusterCount32;
	}
	else
	{
		dwFatEntrySizeBit = 1;
		pfGetFreeClusterCount = _getFreeClusterCount16;
	}

	dwLastIndex = (dwSectorCount << VI_SSB(pVI)) >> dwFatEntrySizeBit; // divided by dwFatEntrySizeBit

	dwLastIndex--;		// last valid index 이기 때문에.. 뺀다.

	do
	{
		if (dwRestSector <= (t_uint32)dwSectorCount)
		{
			// 이것이 마지막 read 이다.. 그러므로 last FAT sector에 대한 점검이 필요하다.
			dwSectorCount = dwRestSector;
			dwLastIndex = (VI_CCPFS(pVI) * (dwSectorCount - 1))
							+ (VI_LCN(pVI) & VI_CCPFS_MASK(pVI));
		}

		// DO NOT NEED TO ADD CACHE
		r = ffat_fs_cache_readSector(dwSector, pBuff, dwSectorCount,
					(FFAT_CACHE_DATA_FAT | FFAT_CACHE_DIRECT_IO), pVI);
		IF_UK (r != dwSectorCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to read sector from ffatfs cache")));
			return FFAT_EIO;
		}

		dwRestSector	-= dwSectorCount;
		dwSector		+= dwSectorCount;
		dwTotalFreeCount += pfGetFreeClusterCount(pBuff, dwLastIndex);

	} while (dwRestSector > 0);

	FFAT_ASSERT(dwTotalFreeCount <= VI_CC(pVI));

	*pdwFreeCount = dwTotalFreeCount;

	return FFAT_OK;
}


/**
 * get free count for FAT16
 *
 * @param		pBuff		: [IN] read buffer pointer
 * @param		dwLastIndex	: [IN] last index for free cluster check
 * @return		free cluster count
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
static t_int32
_getFreeClusterCount16(t_int8* pBuff, t_int32 dwLastIndex)
{
	t_int32		i;
	t_int32		dwFreeCount = 0;
	t_uint16*	p16;

	p16 = (t_uint16*)pBuff;

	for (i = 0; i <= dwLastIndex; i++)
	{
		if (p16[i] == FAT16_FREE)
		{
			dwFreeCount++;
		}
	}

	return dwFreeCount;
}


/**
 * get free count for FAT16
 *
 * @param		pBuff		: [IN] read buffer pointer
 * @param		dwLastIndex	: [IN] last index for free cluster check
 * @return		free cluster count
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
static t_int32
_getFreeClusterCount32(t_int8* pBuff, t_int32 dwLastIndex)
{
	t_int32		i;
	t_int32		dwFreeCount = 0;
	t_uint32*	p32;

	p32 = (t_uint32*)pBuff;

	for (i = 0; i <= dwLastIndex; i++)
	{
		if ((FFAT_BO_UINT32(p32[i]) & FAT32_MASK) == FAT32_FREE)
		{
			dwFreeCount++;
		}
	}

	return dwFreeCount;
}


/**
 * Roll back for makeClusterChain for FAT16/32
 *
 * 이 함수는 자주 사용되지 않기 때문에 FAT16/32 공용으로 만들었음.
 * Roll Back 방법
 *	1. dwPrevEOF 에는 EOC 를 기록
 *	2. 나머지 cluster 들에는 0x00 기록
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		dwClusterCount	: [IN] cluster count to be updated
 * @param		pdwClusters		: [IN] cluster storage
 * @param		dwFUFlag		: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
* @version		SEP-24-2007 [DongYoung Seo] add dirty flag on put setctor
 */
static FFatErr
_makeClusterChainRollBack(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwClusterCount,
						t_uint32* pdwClusters, FFatCacheFlag dwCacheFlag, void* pNode)
{
	FFatErr			r;
	t_int32			i;
	t_uint32		dwSector;
	t_uint32		dwPrevSector;
	t_uint32		dwCPFSMask;

	FFatfsCacheEntry*	pEntry = NULL;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwClusterCount >= 0);
	FFAT_ASSERT(pdwClusters);
	FFAT_ASSERT(dwCacheFlag & FFAT_CACHE_DATA_FAT);

	dwCPFSMask = VI_CCPFS_MASK(pVI);

	if (dwPrevEOF == 0)
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, pdwClusters[0]);
	}
	else
	{
		dwPrevSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, dwPrevEOF);
	}

	r = ffat_fs_cache_getSector(dwPrevSector, (dwCacheFlag | FFAT_CACHE_LOCK), &pEntry, pVI);

	if (dwPrevEOF != 0)
	{
		FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, dwPrevEOF, VI_EOC(pVI), dwCPFSMask);
		FFAT_DEBUG_FAT_UPDATE(dwPrevEOF, VI_EOC(pVI));
	}

	// update FAT area except last one.
	for (i = 0; i < dwClusterCount; i++)
	{
		dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, pdwClusters[0]);

		if (dwSector != dwPrevSector)
		{
			// put sector - ignore error
			r |= ffat_fs_cache_putSector(pVI, pEntry, (dwCacheFlag | FFAT_CACHE_DIRTY), pNode);

			dwPrevSector = dwSector;

			// get new sector buffer
			r |= ffat_fs_cache_getSector(dwPrevSector, (dwCacheFlag | FFAT_CACHE_LOCK), &pEntry, pVI);
		}

		FFATFS_UPDATE_FAT_BUFFER(pVI, pEntry->pBuff, pdwClusters[i], FAT32_EOC, dwCPFSMask);
		FFAT_DEBUG_FAT_UPDATE(pdwClusters[i], FAT32_EOC);
	}

	// release last sector - ignore error
	r |= ffat_fs_cache_putSector(pVI, pEntry,
					(dwCacheFlag | FFAT_CACHE_DATA_FAT | FFAT_CACHE_DIRTY), pNode);

	r |= ffat_fs_cache_syncVol(pVI, (FFAT_CACHE_SYNC | FFAT_CACHE_FORCE), VI_CXT(pVI));

	IF_UK (r < 0)
	{
		r = FFAT_EIO;
	}

	return r;
}


/**
 * Roll back for makeClusterChain for FAT16/32
 *
 * 이 함수는 자주 사용되지 않기 때문에 FAT16/32 공용으로 만들었음.
 * Roll Back 방법
 *	1. dwPrevEOF 에는 EOC 를 기록
 *	2. 나머지 cluster 들에는 0x00 기록
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwPrevEOF			: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		dwLastEntryIndex	: [IN] last entry index on pVC->pVCE
 * @param		dwLastOffset		: [IN] count of pVC->pVC[dwLastEntryIndex].dwCount
 * @param		pVC				: [IN] vectored cluster information
 * @param		dwFUFlag			: [IN] flags for FAT update
 * @param		pNode				: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
static FFatErr
_makeClusterChainRollBackVC(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwLastEntryIndex, 
						t_int32 dwLastOffset, FFatVC* pVC, 
						FFatCacheFlag dwCacheFlag, void* pNode)
{
	FatAllocateFlag	dwAllocFlag;
	t_uint32		dwFreeCount;
	t_uint32		dwFirstFree;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);

	dwAllocFlag = FAT_ALLOCATE_FORCE;
	dwCacheFlag |= FFAT_CACHE_FORCE;

	return _deallocateClusterVC(pVI, dwPrevEOF, dwLastEntryIndex, dwLastOffset,
					pVC, &dwFreeCount, &dwFirstFree, dwAllocFlag, dwCacheFlag, pNode);
}


/**
 * cluster deallocation with VC(Fat Vectored Cluster)
 *
 * 이 함수는 자주 사용되지 않기 때문에 FAT16/32 공용으로 만들었음.
 *	1. dwPrevEOF 에는 EOC 를 기록
 *	2. 나머지 cluster 들에는 0x00 기록
 *
 *	dwLastEntryIndex와 dwLastOffset이 모두 0일 경우에는 정보가 없는것으로 간주한다.
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwPrevEOF			: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		dwLastEntryIndex	: [IN] last entry index on pVC->pVCE
 * @param		dwLastOffset		: [IN] count of pVC->pVC[dwLastEntryIndex].dwCount
 * @param		pVC				: [IN/OUT] deallocation 이후에는 이 변수의 내용이 변경 될 수 있다.
 *										deallocation을 위한 buffer로 사용한다.
										// 초기에는 모두/일부의 cluster 정보가 포함되어 있다.
 * @param		pdwFreeCount		: [OUT] deallocated 된 cluster의 수
 * @param		pdwFirstFree		: [OUT] first deallocated cluster number
 * @param		dwFAFlag			: [IN] flags for FAT update
 * @param		dwCacheFlag			: [IN] flags for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @history		DEC_11-2007 [InHwan Choi] apply to open unlink
 */
static FFatErr
_deallocateClusterVC(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwLastEntryIndex, 
						t_int32 dwLastOffset, FFatVC* pVC, 
						t_uint32* pdwFreeCount, t_uint32* pdwFirstFree,
						FatAllocateFlag dwFAFlag, FFatCacheFlag dwCacheFlag,
						void* pNode)
{
	FFatErr			r;
	t_uint32		dwFreeCount = 0;
	t_uint32		dwTemp;				// temporary variable

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(pdwFirstFree);

	FFAT_ASSERT(VC_VEC(pVC) > 0);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, pVC->pVCE[0].dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(pVC->pVCE[0].dwCount >= 1);

	*pdwFreeCount = 0;

	// mirroring 설정
	dwCacheFlag |= FFAT_CACHE_DATA_FAT;

	FFAT_ASSERT((dwFAFlag & FAT_ALLOCATE_FORCE) ? (dwCacheFlag & FFAT_CACHE_FORCE) : FFAT_TRUE);

	// check the cluster is valid 
	// get last valid VC entry
	_adjustVC(pVC, dwLastEntryIndex, dwLastOffset);

	*pdwFirstFree = VC_FC(pVC);	// set first cluster

	// get last cluster number on VC
	dwTemp = VC_LC(pVC);

	r = pVI->pVolOp->pfGetNextCluster(pVI, dwTemp, &dwTemp);
	FFAT_EO(r, (_T("fail to get last cluster")));

	// FatAllocateFlag가 FAT_DEALLOCATE_FORCE이면 dwTemp가 BAD여도 EOF로 간주한다. 
	if (pVI->pVolOp->pfIsEOF(dwTemp) == FFAT_TRUE)
	{
		// 더이상의 CLUSTER 정보가 없다.
		// 한번에 해결한다.
		r = ffat_fs_fat_freeClustersVC(pVI, pVC, pdwFreeCount, dwFAFlag, dwCacheFlag, pNode);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to deallocate clusters")));
			if ((dwFAFlag & FAT_DEALLOCATE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get sector")));
				goto out;
			}
		}
	}
	else if (dwTemp == 0)
	{
		if (dwFAFlag & FAT_DEALLOCATE_FORCE)
		{
			r = ffat_fs_fat_freeClustersVC(pVI, pVC, pdwFreeCount, dwFAFlag, dwCacheFlag, pNode);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to deallocate clusters")));
			}
		}
		else
		{
			FFAT_ASSERT(0);
			// 음.. 뭔가 정상이 아닌데...
			// FAT이 깨진 경우이다.. 지우려고 한게 지워져있으니 성공처리
			goto write_eof;
		}
	}
	else if (FFATFS_IS_BAD(pVI, dwTemp) == FFAT_TRUE)
	{
		FFAT_ASSERT(dwFAFlag & FAT_DEALLOCATE_FORCE);

		r = ffat_fs_fat_freeClustersVC(pVI, pVC, pdwFreeCount, dwFAFlag, dwCacheFlag, pNode);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to deallocate clusters")));
		}
	}
	else
	{
		// 추가적인 cluster 정보가 있으므로 backward cluster deallocation을 수행하기 위해서는 
		// cluster chain 정보를 수집하여야 한다.

		// sparse cluster를 이용하여 deallocation을 수행한다.
		r = _deallocateWithSC(pVI, NULL, dwTemp, &dwFreeCount, dwFAFlag, dwCacheFlag, pNode);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to deallocate clusters")));
			if ((dwFAFlag & FAT_ALLOCATE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get sector")));
				goto out;
			}
		}

		*pdwFreeCount += dwFreeCount;

		// deallocate cluster at pVC
		r = ffat_fs_fat_freeClustersVC(pVI, pVC, &dwFreeCount, dwFAFlag, dwCacheFlag, pNode);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to deallocate clusters")));
			if ((dwFAFlag & FAT_DEALLOCATE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get sector")));
				goto out;
			}
		}

		*pdwFreeCount += dwFreeCount;
	}

write_eof:
	// eof 설정이 필요할 경우 설정한다.
	if (dwPrevEOF != 0)
	{
		r = FFAT_FS_FAT_UPDATECLUSTER(pVI, dwPrevEOF, pVI->dwEOC, dwCacheFlag, pNode);
		if (r < 0)
		{
			if ((dwFAFlag & FAT_ALLOCATE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get sector")));
				goto out;
			}
		}
	}

out:
	return r;
}


/**
 * backward cluster deallocation
 *
 *	1. dwPrevEOF 에는 EOC 를 기록
 *	2. 나머지 cluster 들에는 0x00 기록
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *									may be 0 ==> no previous cluster
 * @param		dwCluster		: [IN] the first cluster to be deallocated
 * @param		pdwFreeCount	: [OUT] deallocated cluster count
 * @param		pdwFirstFree	: [OUT] first deallocated cluster number
 * @param		dwFAFlag		: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @history		FEB-21-2006 [DongYoung Seo] set first cluster for de-allocate when cluster allocation is failed.
 */
static FFatErr
_deallocateCluster(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_uint32 dwCluster, 
					t_uint32* pdwFreeCount, t_uint32* pdwFirstFree,
					FatAllocateFlag dwFAFlag, FFatCacheFlag dwCacheFlag,
					void* pNode)
{
	FFatErr			r = FFAT_OK;
	FFatVC	stVC;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(pdwFirstFree);

	stVC.pVCE	= NULL;

	dwCacheFlag |= FFAT_CACHE_DATA_FAT;

	FFAT_ASSERT((dwFAFlag & FAT_ALLOCATE_FORCE) ? (dwCacheFlag & FFAT_CACHE_FORCE) : FFAT_TRUE);

	if (dwCluster == 0) 
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwPrevEOF) == FFAT_TRUE);

		// first cluster 정보가 없다 !!
		// dwPrevEOF로 부터 시작 cluster 정보를 구해낸다.
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwPrevEOF, &dwCluster);
		FFAT_ER(r, (_T("Fail to get the first unlinking cluster")));

		if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
		{
			*pdwFreeCount = 0;
			*pdwFirstFree = 0;
			goto write_eof;
		}

		*pdwFirstFree = dwCluster;
	}
	else
	{
		*pdwFirstFree = dwCluster;
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	// deallocate cluster
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, VI_CXT(pVI));
	FFAT_ASSERT(stVC.pVCE != NULL);

	stVC.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
	stVC.dwValidEntryCount		= 1;
	stVC.pVCE[0].dwCluster		= dwCluster;
	stVC.pVCE[0].dwCount		= 1;
	stVC.dwTotalClusterCount	= 1;

	r = _deallocateClusterVC(pVI, 0, 0, 1, &stVC, pdwFreeCount, pdwFirstFree, dwFAFlag, dwCacheFlag, pNode);
	FFAT_EO(r, (_T("fail to deallocate cluster")));

write_eof:
	if (dwPrevEOF != 0)
	{
		r = FFAT_FS_FAT_UPDATECLUSTER(pVI, dwPrevEOF, pVI->dwEOC, dwCacheFlag, pNode);
	}

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, VI_CXT(pVI));

	return r;
}


/**
 * VC의 valid index 정보를 update 한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		pVC			: [IN] vectored cluster information
 * @param		dwLastEntryIndex: [IN] deallocated cluster count storage
 * @param		dwLastOffset	: [IN] flags for FAT update
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
static void
_adjustVC(FFatVC* pVC, t_int32 dwLastEntryIndex, t_int32 dwLastOffset)
{
	FFAT_ASSERT(pVC);

	// get index and offset
	if ((dwLastEntryIndex == 0) && (dwLastOffset == 0))
	{
		// last entry 정보가 없으므로 그냥 return
		return;
	}
	else
	{
		// pVC의 index 정보를 dwLastEntryIndex와 dwLastOffset
		pVC->dwValidEntryCount = dwLastEntryIndex + 1;
		pVC->pVCE[dwLastEntryIndex].dwCount = dwLastOffset;
	}

	return;
}


/**
 * Sparse Cluster 정보를 이용하여 deallocation을 수행한다.
 *
 * @param		pVI		: [IN] volume information
 * @param		pVC			: [IN] vectored cluster information, may be NULL
 * @param		dwFirstCluster	: [IN] first cluster number to be deallocated
 * @param		pdwFreeCount	: [OUT] deallocated cluster count
 * @param		dwFlag			: [IN] allocate flag
 * @param		dwCacheFlag		: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		MAR-02-2007 [DongYoung Seo] free memory before error out
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush

 */
static FFatErr
_deallocateWithSC(FatVolInfo* pVI, FFatVC* pVC, t_uint32 dwFirstCluster,
				t_uint32* pdwFreeCount, FatAllocateFlag dwFlag,
				FFatCacheFlag dwCacheFlag, void* pNode)
{
	FatSparseCluster*	pFSC = NULL;			// Fat sparse cluster
	FFatVC	stVC;
	t_int32				dwSC2Count;
	t_uint32			dwCount;
	FFatErr				r;
	t_int32				i, j;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC || (FFATFS_IS_VALID_CLUSTER(pVI, dwFirstCluster) == FFAT_TRUE));
	FFAT_ASSERT(pdwFreeCount);

	*pdwFreeCount = 0;
	stVC.pVCE = NULL;

	pFSC = (FatSparseCluster*)FFAT_LOCAL_ALLOC(sizeof(FatSparseCluster), VI_CXT(pVI));
	FFAT_ASSERT(pFSC != NULL);

	if (pVC == NULL)
	{
		stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, VI_CXT(pVI));
		FFAT_ASSERT(stVC.pVCE != NULL);

		pVC = &stVC;
		VC_INIT(pVC, VC_NO_OFFSET);

		pVC->dwTotalEntryCount	= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
		pVC->dwValidEntryCount	= 1;
		pVC->pVCE[0].dwCluster	= dwFirstCluster;
		pVC->pVCE[0].dwCount	= 1;
	}

	r = _buildSparseClusterMap(pVI, pVC, pFSC, dwFlag);
	FFAT_EO(r, (_T("fail to build sparse cluster map")));

	// do first deallocation with pVC
	// 마지막 부분은 pVC에 저장되어 있다.
	r = ffat_fs_fat_freeClustersVC(pVI, pVC, &dwCount, dwFlag, dwCacheFlag, pNode);
	FFAT_EO(r, (_T("fail to free clusters")));

	*pdwFreeCount += dwCount;

	// do deallocation
	for (i = (pFSC->dwSC1Count - 1); i >= 0 ; i--)
	{
		if (i != (pFSC->dwSC1Count - 1))
		{
			// not a first chance
			dwSC2Count = FFATFS_SPARSE_CLUSTER_COUNT_LV2;

			r = _buildSparseClusterMapLv2(pVI, pVC, pFSC, i, dwFlag);
			FFAT_EO(r, (_T("fail to build sparce cluster map")));

			FFAT_ASSERT(dwSC2Count == pFSC->dwSC2Count);

			// do first deallocation with pVC
			// 마지막 부분은 pVC에 저장되어 있다.
			r = ffat_fs_fat_freeClustersVC(pVI, pVC, &dwCount, dwFlag, dwCacheFlag, pNode);
			FFAT_EO(r, (_T("fail to free clusters")));

			*pdwFreeCount += dwCount;
		}

		dwSC2Count = pFSC->dwSC2Count - 1;
		if (dwSC2Count == 0)
		{
			continue;
		}

		for (j = (dwSC2Count - 1); j >= 0; j--)
		{
			pVC->dwValidEntryCount = 0;
			r = ffat_fs_fat_getVectoredCluster(pVI, pFSC->pdwSC2[j], 0, FFAT_FALSE, pVC);
			FFAT_EO(r, (_T("fail to get vectored cluster information")));

			r = ffat_fs_fat_freeClustersVC(pVI, pVC, &dwCount, dwFlag, dwCacheFlag, pNode);
			FFAT_EO(r, (_T("fail to free clsuters")));

			*pdwFreeCount += dwCount;
		}
	}

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, VI_CXT(pVI));
	FFAT_LOCAL_FREE(pFSC, sizeof(FatSparseCluster), VI_CXT(pVI));

	return r;
}


/**
 * deallocatoin을 위한 cluster map을 생성한다.
 * level 1/2 와 pVC 모두에 대한 정보를 채운다.
 *
 * start cluster는 pVC->pVCE[0].dwCluster 가 된다.
 *
 * @param		pVI	: [IN] volume information
 * @param		pVC		: [IN] vectored cluster information
 * @param		pFSC		: [IN/OUT] sparse cluster information
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_buildSparseClusterMap(FatVolInfo* pVI, FFatVC* pVC,
						FatSparseCluster* pFSC, FatAllocateFlag dwFlag)
{

	t_int32		i;			// index for SC1
	t_int32		j = 0;		// index for SC2
	t_uint32	dwCluster;
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pFSC);

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, pVC->pVCE[0].dwCluster) == FFAT_TRUE);

	pFSC->dwSC1Count = pFSC->dwSC2Count = 1;

	// set first cluster
	dwCluster = VC_FC(pVC);

	// set first cluster to pFSC
	for (i = 0; i < FFATFS_SPARSE_CLUSTER_COUNT_LV1; i++)
	{
		// Sparse Cluster Level1
		pFSC->pdwSC1[i] = dwCluster;
		pFSC->pdwSC2[0] = pFSC->pdwSC1[i];

		j = 0;
		do
		{
			VC_INIT(pVC, VC_NO_OFFSET);		// don't care of dwClusterOffset

			// Sparse Cluster Data Level2
			r = ffat_fs_fat_getVectoredCluster(pVI, dwCluster, 0, FFAT_FALSE, pVC);
			if (r < 0)
			{
				if ((dwFlag & FAT_DEALLOCATE_FORCE) == 0)
				{
					FFAT_ER(r, (_T("fail to get vectored cluster")));
				}
				dwCluster = pVI->dwEOC;
			}
			else
			{
				FFAT_ASSERT(VC_VEC(pVC) > 0);
				dwCluster = VC_LC(pVC);
				r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
				FFAT_ER(r, (_T("fail to get next cluster")));
			}

			if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
			{
				goto out;
			}

			j++;		// increase SC2 index
			pFSC->pdwSC2[j] = dwCluster;
			pVC->dwValidEntryCount = 0;
		} while (j < FFATFS_SPARSE_CLUSTER_COUNT_LV2);
	}

out:
	pFSC->dwSC1Count	= i + 1;
	pFSC->dwSC2Count	= j + 1;

	FFAT_ASSERT(pFSC->dwSC1Count > 0);
	FFAT_ASSERT(pFSC->dwSC2Count > 0);

	return FFAT_OK;
}


/**
 * deallocatoin을 위한 cluster map을 생성한다.
 * level 2 에 대한 정보를 채운다.
 * Level 1의 값은 수정하지 않는다.
 *
 * start cluster는 pVC->pVCE[0].dwCluster 가 된다.
 *
 * @param		pVI	: [IN] volume information
 * @param		pVC		: [IN] vectored cluster information
 * @param		pFSC		: [IN/OUT] sparse cluster information
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_buildSparseClusterMapLv2(FatVolInfo* pVI, FFatVC* pVC,
						FatSparseCluster* pFSC, t_int32 dwIndexLv1, FatAllocateFlag dwFlag)
{

	t_int32		j = 0;		// index for SC2
	t_uint32	dwCluster;
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pFSC);

	pFSC->dwSC2Count = 1;

	pVC->dwTotalClusterCount	= 0;
	pVC->dwValidEntryCount		= 1;
	pVC->pVCE[0].dwCluster		= pFSC->pdwSC1[dwIndexLv1];
	pVC->pVCE[0].dwCount		= 1;

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, pVC->pVCE[0].dwCluster) == FFAT_TRUE);

	// set first cluster
	dwCluster = VC_FC(pVC);

	// set first cluster to pFSC
	pFSC->pdwSC2[0] =dwCluster;
	j = 0;

	do
	{
		VC_INIT(pVC, VC_NO_OFFSET);		// don't care of dwClusterOffset

		// Sparse Cluster Data Level2
		r = ffat_fs_fat_getVectoredCluster(pVI, dwCluster, 0, FFAT_FALSE, pVC);
		if (r < 0)
		{
			if ((dwFlag & FAT_DEALLOCATE_FORCE) == 0)
			{
				FFAT_ER(r, (_T("fail to get vectored cluster")));
			}
			dwCluster = pVI->dwEOC;
		}
		else
		{
			FFAT_ASSERT(VC_VEC(pVC) > 0);
			dwCluster = VC_LC(pVC);
			r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
			FFAT_ER(r, (_T("fail to get next cluster")));
		}

		if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
		{
			goto out;
		}

		if (j == FFATFS_SPARSE_CLUSTER_COUNT_LV2 - 1)
		{
			goto out;
		}

		j++;		// increase SC2 index
		pFSC->pdwSC2[j] = dwCluster;
	} while (j < FFATFS_SPARSE_CLUSTER_COUNT_LV2);

out:
	pFSC->dwSC2Count	= j + 1;

	FFAT_ASSERT(pFSC->dwSC2Count > 0);

	return FFAT_OK;
}


// debug begin
//=============================================================================
//
//	DEBUG AREA
//	Debug function only
//

#ifdef FFAT_DEBUG

	/**
	 * 입력된 cluster 들이 valid 한지를 검사한다.
	 *
	 * 이 함수는 debug 용으로 release에서는 제외 된다.
	 * Roll Back 방법
	 *	1. dwPrevEOF 에는 EOC 를 기록
	 *	2. 나머지 cluster 들에는 0x00 기록
	 *
	 * @param		pVI	: [IN] volume pointer
	 * @param		pVC		: [IN] cluster information
	 * @return		FFAT_OK			: success
	 * @return		else			: error
	 * @author		DongYoung Seo
	 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_debugCheckClusterValidityVC(FatVolInfo* pVI, FFatVC* pVC)
	{
		t_int32		i, j;
		t_uint32	dwTotalClusterCount = 0;

		for (i = 0; i < VC_VEC(pVC); i++)
		{
			FFAT_ASSERT(pVC->pVCE[i].dwCount > 0);

			for (j = 0; j < (t_int32)pVC->pVCE[i].dwCount; j++)
			{
				if (FFATFS_IS_VALID_CLUSTER(pVI, (pVC->pVCE[i].dwCluster + j)) == FFAT_TRUE)
				{
					dwTotalClusterCount++;
					continue;
				}

				FFAT_ASSERT(0);
			}
		}

		FFAT_ASSERT(dwTotalClusterCount == VC_CC(pVC));

		return;
	}


	/**
	 * 입력된 cluster 들이 valid 한지를 검사한다.
	 *
	 * 이 함수는 debug 용으로 release에서는 제외 된다.
	 * Roll Back 방법
	 *	1. dwPrevEOF 에는 EOC 를 기록
	 *	2. 나머지 cluster 들에는 0x00 기록
	 *
	 * @param		pVI	: [IN] volume pointer
	 * @param		pdwClusters	: [IN] cluster information
	 * @param		dwCount		: [IN] cluster count in pdwClusters
	 * @return		FFAT_OK			: success
	 * @return		else			: error
	 * @author		DongYoung Seo
	 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_debugCheckClusterValidity(FatVolInfo* pVI, t_uint32* pdwClusters, t_int32 dwCount)
	{
		t_int32		i;

		for (i = 0; i < dwCount; i++)
		{
			if (FFATFS_IS_VALID_CLUSTER(pVI, pdwClusters[i]) == FFAT_TRUE)
			{
				continue;
			}

			FFAT_DEBUG_PRINTF((_T("Cluster validity check error !!, pdwCluster[i]/i:%d/%d \n"), pdwClusters[i], i));
			FFAT_ASSERT(0);
		}

		return;
	}


	static void 
	_debugFatUpdate(t_uint32 dwCluster, t_uint32 dwValue)
	{
		FFAT_DEBUG_FAT_PRINTF((_T("Update %d->%d (0x%X->0x%X) \n"), dwCluster, dwValue, dwCluster, dwValue));
	}

	FFatErr
	ffat_fs_fat_changeFAT32FatTable(FatVolInfo* pVI)
	{
		FFatfsCacheEntry*	pEntry;
		t_uint32			dwSector, i;
		FFatErr				r;
		t_uint32*			pCluster;
		t_uint32			dwValue;

		if (FFATFS_IS_FAT16(pVI) == FFAT_TRUE)
		{
			return FFAT_ENOSUPPORT;
		}

		for(i = 2; i < pVI->dwClusterCount+2; i++)
		{
			dwSector = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVI, i);
			r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_FAT | FFAT_CACHE_LOCK), &pEntry, pVI);
			FFAT_ER(r, (_T("fail to get sector")));

			pCluster = (t_uint32*)(pEntry->pBuff);
			dwValue = FFAT_BO_UINT32(pCluster[i & VI_CCPFS_MASK(pVI)]) & FAT32_MASK;

			dwValue = dwValue | ((FFAT_RAND()%17) << 28);

			FFATFS_UPDATE_FAT32((t_uint32*)pEntry->pBuff, i, dwValue, VI_CCPFS_MASK(pVI));
			FFAT_DEBUG_FAT_UPDATE(i, dwValue);

			r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_DATA_FAT | FFAT_CACHE_DIRTY), NULL);
			FFAT_ER(r, (_T("fail to put sector")));
		}

		return FFAT_OK;
	}

#endif
// debug end

