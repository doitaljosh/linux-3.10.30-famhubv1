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
 * @file		ffat_share.c
 * @brief		common moudle for FFAT_CORE, FFAT_ADDON
 * @author		Seo Dong Young(dy76.seo@samsung.com)
 * @version		DEC-18-2007 [DongYoung Seo] First writing
 * @see			None
 */


//*****************************************************************/
//
// FFAT SHARE는 FFAT과 ADDON에서 공통으로 사용되는 함수가 구현된다.
//
//*****************************************************************/

// header - ESS_BASE
#include "ess_math.h"

// header - FFAT
#include "ffat_config.h"
#include "ffat_al.h"

#include "ffat_common.h"
#include "ffat_vol.h"
#include "ffat_main.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffatfs_api.h"

#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_SHARE)

// static functions
static t_int32	_readWritePartialCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster,
						t_int32 dwOffset, t_int32 dwSize, t_int8* pBuff, FFatCacheFlag dwFlag,
						FFatCacheInfo* pCI, t_boolean bRead, ComCxt* pCxt);
static FFatErr	_initPartialSector(Vol* pVol, t_uint32 dwSector, t_int32 dwStartOffset,
						t_int32 dwSize, t_int8* pBuff, FFatCacheFlag dwFlag, FFatCacheInfo* pCI);

typedef t_int32 (*_PFN_IO)(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
							FFatCacheFlag dwFlag, FFatCacheInfo* pCI);

typedef t_int32	(*_PFN_IOVS)(FFatVS* pVS, FFatCacheFlag dwFlag, FFatCacheInfo* pCI);

static const _PFN_IO _stIO[2] =
							{
								ffat_al_writeSector,
								ffat_al_readSector
							};

// check the value of FFAT_TRUE and FFAT_FALSE
#if (FFAT_TRUE != 1)
	#error	"Invalid FFAT_TRUE value, it should be 1"
#endif

#if (FFAT_FALSE != 0)
	#error	"Invalid FFAT_FALSE value, it should be 0"
#endif

#define _CHECK_CACHE(_vol, _s, _c)		((void)0)
#define _CHECK_CACHE_VS()				((void)0)

// debug begin
#ifdef FFAT_DEBUG
	// below to definition must be used carefully. it takes much time to checking them
	#undef _CHECK_LAST_CLUSTER		// disable last cluster of node checking 
	#undef _CHECK_PAL				// disable PAL of node checking

	#undef _CHECK_CACHE
	#undef _CHECK_CACHE_VS
	#define _CHECK_CACHE(_pV, _dwS, _dwC)	_checkCacheConsistency(_pV, _dwS, _dwC)

	static void	_checkCacheConsistency(Vol* pVol, t_uint32 dwSector, t_int32 dwCount);
#endif
// debug end


/**
* Cluster에 data를 read/write 한다. (user data 전용)
*
* 주의 !!!
* 반드시 cluster 단위로 사용하여야 한다.
*
* @param		pVol			: [IN] volume pointer
* @param		pNode			: [IN] node pointer, may be NULL
* @param		dwCluster		: [IN] cluster number
* @param		pBuff			: [IN] buffer pointer
* @param		dwCount			: [IN] cluster count to write
* @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
* @param		dwFlag			: [IN] cache flag
* @param		pCI				: [IN] cache information for IO request
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo 
* @version		DEC-19-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_readWriteCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster, t_int8* pBuff,
							t_int32 dwCount, t_boolean bRead, FFatCacheFlag dwFlag,
							ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	t_int32			dwSN;		// sector number
	t_int32			dwSC;		// sector count
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT((bRead == FFAT_TRUE) || (bRead == FFAT_FALSE));

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		r = FFATFS_ReadWriteCluster(VOL_VI(pVol), dwCluster, pBuff, dwCount, bRead,
							dwFlag, &stCI, pCxt);
		IF_UK (r != dwCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to read write cluster")));
			FFAT_ASSERT(r < 0);
			return r;
		}
	}
	else
	{
		dwSN = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);
		dwSC = dwCount << VOL_SPCB(pVol);

		_CHECK_CACHE(pVol, dwSN, dwSC);
		r = _stIO[bRead](dwSN, pBuff, dwSC, dwFlag, &stCI);
		IF_UK (r  != dwSC)
		{
			FFAT_LOG_PRINTF((_T("IO error")));
			return FFAT_EIO;
		}
	}

	return dwCount;
}


/**
 * pVC를 이용하여 data read/write를 수행한다.
 *
 * 이 함수를 사용하기 전에 반드시 cluster를 할당해 두어야 한다.
 *
 * @param		pVol		: [IN] vol pointer
 * @param		pNode		: [IN] node pointer, may be NULL
 * @param		dwOffset	: [IN] write start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwCluster	: [IN] cluster number
 * @param		bRead		: [IN] FFAT_TRUE : read, FFAT_FALSE ; write
 * @param		dwFlag		: [IN] flags for cache IO
 * @param		pCxt		: [IN] context of current operation
 * @return		0 or above	: read/write size in byte
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-24-2006 [DongYoung Seo] First Writing
 */
t_int32
ffat_readWritePartialCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster,
								t_int32 dwOffset, t_int32 dwSize, t_int8* pBuff,
								t_boolean bRead, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;

	FFAT_ASSERT(dwSize > 0);

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));
	
	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		return FFATFS_ReadWritePartialCluster(VOL_VI(pVol), dwCluster, dwOffset,
						dwSize, pBuff, bRead, dwFlag, &stCI, pCxt, FFAT_FALSE);	
	}
	else
	{
		return _readWritePartialCluster(pVol, pNode, dwCluster, dwOffset,
						dwSize, pBuff, dwFlag, &stCI, bRead, pCxt);
	}
}


/**
 * Initialize clusters (use DIRECT I/O)
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwCluster	: [IN] cluster information
 * @param		dwCount		: [IN] cluster count
 * @param		dwFlag		: [IN] cache flag
 * @param		pCxt		: [IN] context of current operation
 * @return		positive	: initialized cluster count
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		DEC-19-2007 [DongYoung Seo] move from FFATFS
 * @version		FEB-21-2009 [DongYoung Seo] always use direct I/O for initialization
 */
FFatErr
ffat_initCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster, t_int32 dwCount,
						FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwSector;				// sector number
	t_int32			dwSectorCount;			// sector count per a write
	t_int32			dwTotalSectorCount;
	t_int8*			pBuff = NULL;
	FFatCacheInfo	stCI;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwCount > 0);

	// add direct i/o flag
	dwFlag |= FFAT_CACHE_DIRECT_IO;

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		r = FFATFS_InitCluster(VOL_VI(pVol), dwCluster, dwCount , NULL, 0,
						dwFlag, &stCI, pCxt);
		FFAT_EO(r, (_T("Fail to init cluster for meta-data")));

		goto out;
	}

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(FFAT_CLUSTER_INIT_BUFF_SIZE, pCxt);
	FFAT_ASSERT(pBuff != NULL);

	FFAT_MEMSET(pBuff, 0x00, FFAT_CLUSTER_INIT_BUFF_SIZE);

	FFAT_ASSERT((FFAT_CLUSTER_INIT_BUFF_SIZE & VOL_SSM(pVol)) == 0);
	dwSectorCount	= FFAT_CLUSTER_INIT_BUFF_SIZE >> VOL_SSB(pVol);

	dwTotalSectorCount = dwCount * VOL_SPC(pVol);
	dwSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);

	dwFlag |= FFAT_CACHE_NOREAD;

	do
	{
		if (dwTotalSectorCount < dwSectorCount)
		{
			dwSectorCount = dwTotalSectorCount;
		}

		_CHECK_CACHE(pVol, dwSector, dwSectorCount);

		r = ffat_al_writeSector(dwSector, pBuff, dwSectorCount, dwFlag, &stCI);
		IF_UK (r != dwSectorCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to init cluster")));
			r = FFAT_EIO;
			goto out;
		}

		dwTotalSectorCount	-= dwSectorCount;
		dwSector			+= dwSectorCount;
	} while (dwTotalSectorCount > 0);

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, FFAT_CLUSTER_INIT_BUFF_SIZE, pCxt);

	if (r == FFAT_OK)
	{
		return dwCount;
	}

	return r;
}


/**
 * 하나의 cluster에 대해 일부 영역의 초기화를 수행한다.
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pNode			: [IN] node pointer, may be NULL
 * @param		dwCluster		: [IN] cluster number
 * @param		dwStartOffset	: [IN] start offset
 *										cluster의 크기를 초과해서는 안된다.
 * @param		dwSize			: [IN] initialization size in byte
 *										cluster size를 초과할 경우에는 무시된다.
 * @param		dwFlag			: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_initPartialCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster,
				t_int32 dwStartOffset, t_int32 dwSize, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwSector;
	t_int32			dwSectorCount;		// sector count per a write
	t_uint32		dwEndSector;		// init end sector number
	t_int32			dwTotalSectorCount;
	t_int32			dwBuffSize;		// memory allocation size
	t_int8*			pBuff;
	t_int32			dwEndOffset;
	FFatCacheInfo	stCI;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset < VOL_CS(pVol));

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));

	if (dwStartOffset > VOL_CS(pVol))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter, start/end offset")));
		return FFAT_EINVALID;
	}

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		return FFATFS_InitPartialCluster(VOL_VI(pVol), dwCluster, dwStartOffset,
						dwSize, dwFlag, &stCI, pCxt);
	}

	dwEndOffset = dwStartOffset + dwSize - 1;

	// adjust end offset
	if (dwEndOffset >= VOL_CS(pVol))
	{
		dwSize = VOL_CS(pVol) - dwStartOffset;
		dwEndOffset = VOL_CS(pVol) - 1;
	}

	dwBuffSize	= ESS_GET_MIN(FFAT_CLUSTER_INIT_BUFF_SIZE, VOL_CS(pVol));
	pBuff		= (t_int8*)FFAT_LOCAL_ALLOC(dwBuffSize, pCxt);
	FFAT_ASSERT(pBuff != NULL);

	dwBuffSize = dwBuffSize & (~VOL_SSM(pVol));
	dwSectorCount = dwBuffSize >> VOL_SSB(pVol);		// 한번에 write 할 sector의 수를 설정

	dwSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);

	// get end sector number
	dwEndSector = dwSector + (dwEndOffset >> VOL_SSB(pVol));

	// get first sector
	dwSector = dwSector + (dwStartOffset >> VOL_SSB(pVol));

	dwTotalSectorCount = dwEndSector - dwSector + 1;

	// Initialize first sector
	if (dwStartOffset & VOL_SSM(pVol))
	{
		r = _initPartialSector(pVol, dwSector, (dwStartOffset & VOL_SSM(pVol)), 
							dwSize, pBuff, dwFlag, &stCI);
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
		if ((dwEndOffset + 1) & VOL_SSM(pVol))
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
			FFAT_MEMSET(pBuff, 0x00, dwTotalSectorCount << VOL_SSB(pVol));
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

			_CHECK_CACHE(pVol, dwSector, dwSectorCount);

			r = ffat_al_writeSector(dwSector, pBuff, dwSectorCount, dwFlag, &stCI);
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
	if ((dwEndOffset + 1) & VOL_SSM(pVol))
	{
		r = _initPartialSector(pVol, dwEndSector, 0, ((dwEndOffset & VOL_SSM(pVol)) + 1), 
								pBuff, dwFlag, &stCI);
		FFAT_EO(r, (_T("fail to init partial sector")));
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, dwBuffSize, pCxt);

	return r;
}


/**
* read write full sectors
*
* @param		pVol			: [IN] volume pointer
* @param		pNode			: [IN] node pointer, may be NULL
* @param		dwSector		: [IN] sector number
* @param		dwCount			: [IN] sector count
*										size 가 sector 크기를 초과할 경우 무시 된다.
* @param		pBuff			: [IN] read/write data storage
* @param		dwFlag			: [IN] cache flag
* @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
* @param		pCxt			: [IN] context of current operation
* @return		positive		: read sector count
* @return		negative		: error
* @author		DongYoung Seo
* @history		DEc-19-2007 [DongYoung Seo] First write
*/
FFatErr
ffat_readWriteSectors(Vol* pVol, Node* pNode, t_uint32 dwSector, t_int32 dwCount,
					t_int8* pBuff, FFatCacheFlag dwFlag, t_boolean bRead, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(pBuff);

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		return FFATFS_ReadWriteSectors(VOL_VI(pVol), dwSector, dwCount, pBuff,
						dwFlag, &stCI, bRead, pCxt);
	}

	_CHECK_CACHE(pVol, dwSector, dwCount);

	r = _stIO[bRead](dwSector, pBuff, dwCount, dwFlag, &stCI);
	IF_UK (r != dwCount)
	{
		FFAT_LOG_PRINTF((_T("Fail to read/write cluster")));
		return FFAT_EIO;
	}

	return dwCount;
}



/**
 * 하나의 sector에 대해 일부 영역의 read/write를 수행한다.
 * read/write for a partial sector
 *
 * @param		pVol			: [IN] volume information
 * @param		pNode			: [IN] node information
 * @param		dwSector		: [IN] sector number
 * @param		dwOffset		: [IN] I/O start offset
 * @param		dwSize			: [IN] I/O size in byte
 *										size 가 sector 크기를 초과할 경우 무시 된다.
 * @param		pBuff			: [IN] read/write data storage
 * @param		dwFlag			: [IN] cache flag
 * @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
 * @param		pCxt			: [IN] context of current operation
 * @return		positive		: read size
 * @return		negative		: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @history		MAR-02-2007 [DongYoung Seo] Add memory free check routine
 */
FFatErr
ffat_readWritePartialSector(Vol* pVol, Node* pNode, t_uint32 dwSector, t_int32 dwOffset,
					t_int32 dwSize, t_int8* pBuff, FFatCacheFlag dwFlag,
					t_boolean bRead, ComCxt* pCxt)
{
	t_int8*			pReadBuff = NULL;
	FFatCacheInfo	stCI;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwSize >= 0);

	// sector size adjustment
	if ((dwOffset + dwSize - 1) >= VOL_SS(pVol))
	{
		dwSize = VOL_SS(pVol) - dwOffset;
	}

	FFAT_INIT_CI(&stCI, pNode, VOL_DEV(pVol));

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		return FFATFS_ReadWritePartialSector(VOL_VI(pVol), dwSector, dwOffset, dwSize,
							pBuff, dwFlag, &stCI, bRead, pCxt);
	}

	_CHECK_CACHE(pVol, dwSector, 1);

	pReadBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pReadBuff != NULL);

	r = ffat_al_readSector(dwSector, pReadBuff, 1, dwFlag, &stCI);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to read a sector")));
		r = FFAT_EIO;
		goto out;
	}

	if (bRead == FFAT_TRUE)
	{
		FFAT_MEMCPY(pBuff, (pReadBuff + dwOffset), dwSize);
	}
	else
	{
		FFAT_MEMCPY((pReadBuff + dwOffset), pBuff, dwSize);

		r = ffat_al_writeSector(dwSector, pReadBuff, 1, dwFlag, &stCI);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to read a sector")));
			r = FFAT_EIO;
			goto out;
		}
	}

	r = dwSize;

out:
	FFAT_LOCAL_FREE(pReadBuff, VOL_SS(pVol), pCxt);

	return r;
}


/**
* write directory entry at any cluster
*
* write 이전에 반드시 충분한 cluster가 확보 되어야 한다.
*
* @param		pVol		: [IN] volume information
* @param		dwCluster	: [IN] the first cluster, May be 0(there is no information)
*								FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
 *								dwClusterOfOffset must be a value cluster when this variables is 0
* @param		dwClusterOfOffset
*							: [IN] Cluster of Offset, May be 0(there is no information)
*								FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
*								dwCluster must be a value cluster when this variables is 0
* @param		dwOffset	: [IN] write start offset from dwCluster
* @param		pBuff		: [IN] write data
* @param		dwSize		: [IN] write size in byte
* @param		dwFlag		: [IN] cache flag
* @param		pNode		: [IN] node pointer for file level flush, may be NULL
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: SUCCESS
* @return		else		: FAIL
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		DEC-22-2006 [DongYoung Seo] First Writing
*/
FFatErr
ffat_writeDEs(Vol* pVol, t_uint32 dwCluster, t_uint32 dwClusterOfOffset, t_int32 dwOffset,
				t_int8* pBuff, t_int32 dwSize, FFatCacheFlag dwFlag, Node* pNode, ComCxt* pCxt)
{
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT((dwSize % FAT_DE_SIZE) == 0);
	FFAT_ASSERT((dwOffset % FAT_DE_SIZE) == 0);

	if (dwCluster != FFATFS_FAT16_ROOT_CLUSTER)
	{
		if (dwClusterOfOffset != 0)
		{
			if (dwClusterOfOffset != FFATFS_FAT16_ROOT_CLUSTER)
			{
				FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwClusterOfOffset) == FFAT_TRUE);
				dwCluster	= dwClusterOfOffset;
				dwOffset	= dwOffset & VOL_CSM(pVol);
			}
			else
			{
				FFAT_ASSERT(dwClusterOfOffset == VOL_RC(pVol));
				FFAT_ASSERT(VOL_IS_FAT16(pVol) == FFAT_TRUE);
				dwCluster	= dwClusterOfOffset;
			}
		}

		FFAT_ASSERT((dwClusterOfOffset == 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE) : FFAT_TRUE);
	}
	else if (dwCluster == 0)
	{
		FFAT_ASSERT(dwClusterOfOffset != 0);
		dwCluster	= dwClusterOfOffset;
		dwOffset	= dwOffset & VOL_CSM(pVol);
	}

	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (VOL_IS_FAT16(pVol) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (VOL_RC(pVol) == dwCluster) : (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE));

	r = FFATFS_WriteDE(VOL_VI(pVol), dwCluster, dwOffset, pBuff, dwSize,
					dwFlag, pNode, pCxt);
	FFAT_EO(r, (_T("Fail to write directory entry")));

out:
	return r;
}


/**
* delete directory entries
*
*
* @param		pVol			: [IN] volume information
* @param		dwCluster		: [IN] first write cluster
*									FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
*									may be 0 - dwClusterOfOffset must not be 0
* @param		dwOffset		: [IN] write start offset
*									from dwCluster
* @param		dwClusterOfOffset: [IN] cluster of dwOffset, may be 0
* @param		dwCount			: [IN] delete entry count
* @param		bInteligent		: [IN] flag for looking up deletion mark for efficiency.deletion mark
*							FFAT_TRUE : check directory entry to set it 0x00(FAT_DE_END_OF_DIR)
*							FFAT_FALS : write 0xE5(FAT_DE_FREE) at the head of entry
* @param		dwFlag			: [IN] cache flag
* @param		pNode			: [IN] node pointer for file level flush, may be NULL
* @param		pCxt			: [IN] context of current operation 
* @return		FFAT_OK			: Success
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		JAN-05-2009 [DongYoung Seo] First Writing
* @version		JAN-07-2009 [JeongWoo Park] consider that pNode can be NULL
*/
FFatErr
ffat_deleteDEs(Vol* pVol, t_uint32 dwCluster, t_int32 dwOffset, t_uint32 dwClusterOfOffset,
				t_int32 dwCount, t_boolean bIntelligent,
				FFatCacheFlag dwFlag, Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwCluster || dwClusterOfOffset);
	FFAT_ASSERT(dwCount);
	FFAT_ASSERT((bIntelligent == FFAT_FALSE) || (bIntelligent == FFAT_TRUE));

	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (VOL_IS_FAT16(pVol) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (VOL_RC(pVol) == dwCluster) : (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE));

	if (dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(VOL_IS_FAT16(pVol) == FFAT_TRUE);

		r = FFATFS_DeleteDE(VOL_VI(pVol), dwCluster, dwOffset, dwCount,
					bIntelligent, dwFlag, pNode, pCxt);
	}
	else
	{
		if (dwClusterOfOffset)
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwClusterOfOffset) == FFAT_TRUE);

			r = FFATFS_DeleteDE(VOL_VI(pVol), dwClusterOfOffset,
						(dwOffset & VOL_CSM(pVol)),
						dwCount, bIntelligent, dwFlag, pNode, pCxt);
		}
		else
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

			r = FFATFS_DeleteDE(VOL_VI(pVol), dwCluster,
						dwOffset, dwCount, bIntelligent, dwFlag, pNode, pCxt);
		}
	}

	return r;
}


//=============================================================================
//
//	STATIC Functions
//


/**
* 하나의 cluster에 대해 일부 영역의 read를 수행한다.
*
* Caution !!!
* End offset이 cluster size를 넘어설 경우에는 이후 부분은 무시된다.
* 
* @param		pVol			: [IN] volume pointer
* @param		pNode			: [IN] node pointer
* @param		dwCluster		: [IN] cluster number
* @param		dwStartOffset	: [IN] start offset
* @param		dwSize			: [IN] read write size
* @param		pBuff			: [IN/OUT] read data storage
* @param		dwFlag			: [IN] cache flag
* @param		bRead			: [IN]	FFAT_TRUE : read
*										FFAT_FALSE: write
* @param		pCxt			: [IN] context of current operation
* @return		0 or above		: read / write size in byte
* @return		else			: error
* @author		DongYoung Seo 
* @version		AUG-23-2006 [DongYoung Seo] First Writing.
*/
static t_int32
_readWritePartialCluster(Vol* pVol, Node* pNode, t_uint32 dwCluster, 
						t_int32 dwStartOffset, t_int32 dwSize,
						t_int8* pBuff, FFatCacheFlag dwFlag, 
						FFatCacheInfo* pCI, t_boolean bRead, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwSector;
	t_uint32		dwStartSector;		// init start sector number
	t_uint32		dwEndSector;		// init end sector number
	t_int32			dwTotalSectorCount;
	t_int32			dwEndOffset;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwStartOffset >= 0);
	FFAT_ASSERT(dwSize >= 0);
	FFAT_ASSERT(pCI);

  // 2010.07.14_chunum.kong_Fix the bug that error code is added about Macro of Invalid cluster check, cluster 0 is returned at sector value.
  // Problem Broken BPB at RFS
	IF_UK (FFATFS_IsValidCluster(VOL_VI(pVol), dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster number")));
		return FFAT_EFAT;
	}
	
	IF_UK (dwStartOffset > VOL_CS(pVol))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter, start/end offset")));
		return FFAT_EINVALID;
	}

	dwEndOffset = dwStartOffset + dwSize - 1;

	// adjust end offset
	if (dwEndOffset >= VOL_CS(pVol))
	{
		dwEndOffset = VOL_CS(pVol) - 1;
		dwSize = dwEndOffset - dwStartOffset + 1;
	}

	dwSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);

	// get end sector number
	dwEndSector = dwSector + (dwEndOffset >> VOL_SSB(pVol));

	// get first sector
	dwStartSector = dwSector + (dwStartOffset >> VOL_SSB(pVol));

	dwTotalSectorCount = dwEndSector - dwStartSector + 1;

	// read/write first sector
	if (dwStartOffset & VOL_SSM(pVol))
	{
		r = ffat_readWritePartialSector(pVol, pNode, dwStartSector,
					(dwStartOffset & VOL_SSM(pVol)), dwSize, pBuff, dwFlag, bRead, pCxt);
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

	if ((dwEndOffset + 1) & VOL_SSM(pVol))
	{
		// 마지막 sector에 대해서는 partial write를 해야 하므로 1 감소
		dwTotalSectorCount--;
	}

	if (dwTotalSectorCount > 0)
	{
		_CHECK_CACHE(pVol, dwStartSector, dwTotalSectorCount);
		r = _stIO[bRead](dwStartSector, pBuff, dwTotalSectorCount, dwFlag, pCI);
		IF_UK (r != dwTotalSectorCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to init cluster")));
			r = FFAT_EIO;
			goto out;
		}

		dwStartSector += dwTotalSectorCount;
		pBuff += (dwTotalSectorCount << VOL_SSB(pVol));	// buffer pointer 증가
	}

	// read/write last sector
	if ((dwEndOffset + 1) & VOL_SSM(pVol))
	{
		FFAT_ASSERT(dwStartSector == dwEndSector);

		r = ffat_readWritePartialSector(pVol, pNode, dwStartSector, 
					0, ((dwEndOffset & VOL_SSM(pVol)) + 1), pBuff, dwFlag, bRead, pCxt);
		FFAT_EO(r, (_T("fail to init partial sector")));
	}

	r = dwSize;

out:
	return r;
}


/**
 * 하나의 sector에 대해 일부 영역의 초기화를 수행한다.
 *
 * @param		pVol			: [IN] volume pointer
 * @param		dwSector		: [IN] sector number
 * @param		dwStartOffset	: [IN] start offset
 * @param		dwSize			: [IN] size in byte
 *										sector의 크기를 초과할 경우에는 초과된 부분은 무시된다.
 * @param		pBuff			: [IN] buffer pointer
 *										meta data update가 아닌경우는
 *										반드시 sector size 보다 큰 buffer를 전달해야 한다.
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		DEc-19-2007 [DongYoung Seo] copy from FFATFS
*/
static FFatErr
_initPartialSector(Vol* pVol, t_uint32 dwSector, t_int32 dwStartOffset,
				t_int32 dwSize, t_int8* pBuff, FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCI);

	if ((dwStartOffset + dwSize) > VOL_SS(pVol))
	{
		dwSize = VOL_SS(pVol) - dwStartOffset;
	}

	FFAT_ASSERT(pBuff);

	_CHECK_CACHE(pVol, dwSector, 1);

	r = ffat_al_readSector(dwSector, pBuff, 1, dwFlag, pCI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read a sector")));
		return FFAT_EIO;
	}

	FFAT_MEMSET(pBuff + dwStartOffset, 0x00, dwSize);

	r = ffat_al_writeSector(dwSector, pBuff, 1, dwFlag | FFAT_CACHE_DIRTY, pCI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read a sector")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}


// debug begin

#ifdef FFAT_DEBUG

	/**
	 * print cluster chain for debug purpose
	 *
	 * @param		pVol		: [IN] Volume Pointer
	 * @param		dwCluster	: [IN] start cluster number
	 * @author		DongYoung Seo
	 * @version		OCT-23-2008 [DongYoung Seo] First Writing.
	 */
	void
	ffat_share_printClusterChain(Vol* pVol, t_uint32 dwCluster, ComCxt* pCxt)
	{
		FFatErr			r;
		t_uint32		dwCur;
		t_uint32		dwNext;
		FFAT_ASSERT(pVol);

		dwNext = dwCluster;

		FFAT_PRINT_VERBOSE((_T("FAT Chain : %d"), dwCluster));
		do
		{
			dwCur = dwNext;
			r = FFATFS_GetNextCluster(VOL_VI(pVol), dwCur, &dwNext, pCxt);
			if (r < 0)
			{
				FFAT_PRINT_VERBOSE((_T("Fail to get next cluster")));
			}
			FFAT_PRINT_VERBOSE((_T("->%d"), dwNext));
		} while (FFATFS_IS_EOF(VOL_VI(pVol), dwNext) == FFAT_FALSE);

		FFAT_PRINT_VERBOSE((_T("->EOF")));
		return;
	}


	/**
	 * check pNode->stPAL validity
	 *	debug purpose function.
	 *
	 * @param		pNode		: [IN] Node Pointer
	 * @param		pCxt		: [IN] current context
	 * @author		DongYoung Seo
	 * @version		OCT-28-2008 [DongYoung Seo] First Writing.
	 * @version		DEC-12-2008 [GwangOk Go] change LastLocation into PAL
	 */
	FFatErr
	ffat_share_checkNodePAL(Node* pNode, ComCxt* pCxt)
	{
	#ifdef _CHECK_PAL
		FFatErr			r;
		t_uint32		dwCluster;
		Vol*			pVol;
		NodePAL		stPAL;

		FFAT_ASSERT(pNode);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (pNode->dwLastCluster == 0) : FFAT_TRUE);

		ffat_node_getPAL(pNode, &stPAL);

		FFAT_ASSERT((NODE_C(pNode) == 0) ? (stPAL.dwOffset == FFAT_NO_OFFSET) : FFAT_TRUE);
		FFAT_ASSERT((stPAL.dwOffset == FFAT_NO_OFFSET) ? ((stPAL.dwCluster == 0) && (stPAL.dwCount == 0)) : FFAT_TRUE);
		FFAT_ASSERT((stPAL.dwOffset != FFAT_NO_OFFSET) ? ((stPAL.dwCluster != 0) && (stPAL.dwCount != 0)) : FFAT_TRUE);
		FFAT_ASSERT((stPAL.dwCluster) ? ((stPAL.dwOffset & VOL_CSM(NODE_VOL(pNode))) == 0) : FFAT_TRUE);

		if ((stPAL.dwCluster == 0) || (stPAL.dwCount == 0) || (stPAL.dwCluster == FFAT_NO_OFFSET))
		{
			// no information
			return FFAT_OK;
		}

		pVol = NODE_VOL(pNode);

		r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), NODE_C(pNode), stPAL.dwOffset,
								&dwCluster, pCxt);
		FFAT_ASSERT(r == FFAT_OK);

		FFAT_ASSERT(dwCluster == stPAL.dwCluster);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), stPAL.dwCluster) == FFAT_TRUE);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), (stPAL.dwCluster + stPAL.dwCount - 1)) == FFAT_TRUE);
	#else
		NodePAL		stPAL;

		FFAT_ASSERT(pNode);

		ffat_node_getPAL(pNode, &stPAL);

		FFAT_ASSERT((NODE_C(pNode) == 0) ? (pNode->dwLastCluster == 0) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (stPAL.dwOffset == FFAT_NO_OFFSET) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (stPAL.dwCluster == 0) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (stPAL.dwCount == 0) : FFAT_TRUE);
		FFAT_ASSERT((stPAL.dwCluster) ? ((stPAL.dwOffset & VOL_CSM(NODE_VOL(pNode))) == 0) : FFAT_TRUE);
	#endif
		return FFAT_OK;
	}


	/**
	* check pNode->dwLastcluster validity
	*	debug purpose function.
	*
	* @param		pNode		: [IN] Node Pointer
	* @param		pCxt		: [IN] current context
	* @author		DongYoung Seo
	* @version		OCT-28-2008 [DongYoung Seo] First Writing.
	*/
	FFatErr
	ffat_share_checkNodeLastClusterInfo(Node* pNode, ComCxt* pCxt)
	{
	#ifdef _CHECK_LAST_CLUSTER
		FFatErr			r;
		t_uint32		dwLastCluster;
		t_uint32		dwCount;
		Vol*			pVol;

		FFAT_ASSERT(pNode);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (pNode->dwLastCluster == 0) : FFAT_TRUE);

		if (pNode->dwLastCluster == 0)
		{
			// NO INFORMATION
			return FFAT_OK;
		}

		pVol = NODE_VOL(pNode);

		r = ffat_misc_getLastCluster(pVol, NODE_C(pNode), &dwLastCluster, &dwCount, pCxt);
		FFAT_ASSERT(r == FFAT_OK);

		FFAT_ASSERT(pNode->dwLastCluster == dwLastCluster);
	#else
		FFAT_ASSERT(pNode);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (pNode->dwLastCluster == 0) : FFAT_TRUE);
	#endif
		return FFAT_OK;
	}


	/**
	* check input name and name in the directory entry
	*	debug purpose function.
	*
	* @param		pNode		: [IN] Node Pointer
	* @param		psName		: [IN] original name
	* @param		pDE			: [IN] pDE
	* @param		pCxt		: [IN] current context
	* @author		DongYoung Seo
	* @version		NOV-03-2008 [DongYoung Seo] First Writing.
	* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
	*											- Case sensitive
	*/
	FFatErr
	ffat_share_checkDE(Node* pNode, t_wchar* psName, FatDeSFN* pDE, ComCxt* pCxt)
	{
		static t_wchar	psTempName[(FFAT_NAME_MAX_LENGTH + 1) * 2];
											// temporary name storage to generate name from DE
		static t_wchar	psAdjustedName[(FFAT_NAME_MAX_LENGTH + 1) * 2];
											// adjusted name from FAT filesystem
		static FatDeSFN	stDE[FAT_DE_COUNT_MAX];
											// Temporary buffer for DE storage
		t_int32			dwDeCount;			// directory entry count
		FatNameType		dwNameType;			// type of name
		t_int32			dwNameLen = 0;			// length of name
		t_int32			dwNamePartLen;		// length of name part
		t_int32			dwExtPartLen;		// length of extension part
		t_int32			dwSfnNameSize;		// size of short file name
		FFatErr			r;
		Vol*			pVol;

		FFAT_ASSERT(pNode);
		pVol = NODE_VOL(pNode);

		FFAT_WCSCPY(psAdjustedName, psName);

		// generate adjusted name for FAT filesystem
		r = FFATFS_AdjustNameToFatFormat(VOL_VI(pVol), psAdjustedName, &dwNameLen, &dwNamePartLen, &dwExtPartLen,
								&dwSfnNameSize, &dwNameType, &stDE[0]);
		FFAT_ASSERT(r == FFAT_OK);

//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT  
		  if (dwNameLen != pNode->wNameLen)
		  {
			   FFAT_PRINT_PANIC((_T("Length of Name for Name/DE:%d/%d\n"), pNode->wNameLen, dwNameLen));
			   FFAT_ASSERT(0);
		  }
#else
		  if (dwNameLen < pNode->wNameLen)
		  {
			   FFAT_PRINT_PANIC((_T("Length of Name for Name/DE:%d/%d\n"), pNode->wNameLen, dwNameLen));
			   FFAT_ASSERT(0);
		  }
#endif

		if (pDE[0].bAttr == FFAT_ATTR_LONG_NAME)
		{
			// This is LFN
			dwDeCount = (pDE[0].sName[0] & (~FAT_DE_LAST_LFNE_MASK)) + 1;
			FFAT_ASSERT((dwNameType & FAT_NAME_SFN) == 0);
		}
		else
		{
			dwDeCount = 1;
			FFAT_ASSERT(dwNameType & FAT_NAME_SFN);
		}

		// check Name
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT  
		  r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), pDE, dwDeCount,
			 psTempName, &dwNameLen, FAT_GEN_NAME_LFN);
#else
		  r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), pDE, dwDeCount,
			 psTempName, &dwNameLen, FAT_GEN_NAME_SFN);
#endif 
		FFAT_ASSERT(r == FFAT_OK);

//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT  
		  if (((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
		   ? (FFAT_WCSICMP(psAdjustedName, psTempName) != 0)
		   : (FFAT_WCSCMP(psAdjustedName, psTempName) != 0))
		  {
			   FFAT_PRINT_VERBOSE((_T("Input Name : %s\n"), ffat_debug_w2a(psAdjustedName, pVol)));
			   FFAT_PRINT_VERBOSE((_T("Name from DE: %s\n"), ffat_debug_w2a(psTempName, pVol)));
			   FFAT_ASSERT(0);
		  }
#endif 
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT  
		  if (dwNameLen != pNode->wNameLen)
		  {
			   FFAT_PRINT_PANIC((_T("Length of Name for Name/DE:%d/%d\n"), pNode->wNameLen, dwNameLen));
			   FFAT_ASSERT(0);
		  }
#else
		  if (dwNameLen < pNode->wNameLen)
		  {
			   FFAT_PRINT_PANIC((_T("Length of Name for Name/DE:%d/%d\n"), pNode->wNameLen, dwNameLen));
			   FFAT_ASSERT(0);
		  }
#endif

		return FFAT_OK;
	}


	/**
	* check Directory Entry information on the Node Structure
	*
	* @param		pNode		: [IN] Node Pointer
	* @param		FFAT_OK		: success
	* @author		DongYoung Seo
	* @version		19-DEC--2008 [DongYoung Seo] First Writing
	*/
	FFatErr
	ffat_share_checkNodeDeInfo(Node* pNode)
	{
		if (VOL_IS_FAT32(NODE_VOL(pNode)) == FFAT_TRUE)
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeStartCluster) == FFAT_TRUE);
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeEndCluster) == FFAT_TRUE);
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeClusterSFNE) == FFAT_TRUE);
		}
		else
		{
			FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == FFATFS_FAT16_ROOT_CLUSTER);

			if (NODE_COP(pNode) == VOL_RC(NODE_VOL(pNode)))
			{
#if 0
				// below ASSERT can not true for hidden area root node
				FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeStartCluster);
				FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeEndCluster);
				FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeClusterSFNE);
#endif
				FFAT_ASSERT(ffat_addon_checkNodeDeInfo(pNode) == FFAT_TRUE);
			}
			else
			{
				FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeStartCluster) == FFAT_TRUE);
				FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeEndCluster) == FFAT_TRUE);
				FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pNode->stDeInfo.dwDeClusterSFNE) == FFAT_TRUE);
			}
		}

		return FFAT_OK;
	}


	/**
	* check a cluster is the destination dwOffset apart from dwStartcluster
	*
	* @param		pVol				: [IN] volume pointer
	* @param		dwStartCluster		: [IN] start cluster number
	* @param		dwOffset			: [IN] offset 
	* @param		dwClusterOfOffset	: [IN] destination cluster for the offset from dwStartCluster
	* @param		pCxt				: [IN] current context
	* @param		FFAT_OK		: success
	* @author		DongYoung Seo
	* @version		19-DEC--2008 [DongYoung Seo] First Writing
	*/
	FFatErr
	ffat_share_checkClusterOfOffset(Vol* pVol, t_uint32 dwStartCluster,
								t_uint32 dwOffset, t_uint32 dwClusterOfOffset, ComCxt* pCxt)
	{
		FFatErr			r;
		t_uint32		dwCluster;			// destination cluster

		r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), dwStartCluster, dwOffset, &dwCluster, pCxt);
		FFAT_ASSERT(r == FFAT_OK);

		FFAT_ASSERT(dwCluster == dwClusterOfOffset);

		return FFAT_OK;
	}


	//=========================================================================
	//
	//	STATIC FUNCTIONS
	//

	/**
	* check the sector is in the FFATFS cache on dirty state or not.
	*
	* @param		pVol		: [IN] Volume Pointer
	* @param		dwSector	: [IN] start sector number
	* @param		dwCount		: [IN] count of sector
	* @author		DongYoung Seo
	* @version		???-??-???? [DongYoung Seo] First Writing.
	*/
	static void
	_checkCacheConsistency(Vol* pVol, t_uint32 dwSector, t_int32 dwCount)
	{
		t_uint32	dwSectorNo;
		FFatErr		r;

		while (dwCount--)
		{
			dwSectorNo = dwSector + dwCount;
			r = FFATFS_FSCtl(FAT_FSCTL_IS_DIRTY_SECTOR_IN_CACHE, VOL_VI(pVol), &dwSectorNo, NULL);

			FFAT_ASSERTP(r == FFAT_FALSE, (_T("normal data sector is in the FFATFS cache!!")));
		}
	}

#endif
// debug end

