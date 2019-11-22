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
 * @file		ffat_misc.h
 * @brief		miscellaneus functions for FFAT FS
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ffat_common.h"

#include "ffatfs_main.h"
#include "ffatfs_misc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_MISC)

/**
 * 하나의 sector에 대해 일부 영역의 초기화를 수행한다.
 *
 * @param		pVolInfo		: [IN] volume pointer
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
 */
FFatErr
ffat_fs_misc_initPartialSector(FatVolInfo* pVolInfo, t_uint32 dwSector, 
					t_int32 dwStartOffset, t_int32 dwSize, 
					t_int8* pBuff, FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FFatfsCacheEntry*	pEntry = NULL;
	FFatErr		r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pCI);

	if ((dwStartOffset + dwSize) > VI_SS(pVolInfo))
	{
		dwSize = VI_SS(pVolInfo) - dwStartOffset;
	}
	
	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		// data가 meta data 영역인 경우
		r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVolInfo);
		FFAT_ER(r, (_T("fail to get sector")));

		FFAT_MEMSET(pBuff + dwStartOffset, 0x00, dwSize);

		r = ffat_fs_cache_putSector(pVolInfo, pEntry, dwFlag, pCI->pNode);
		FFAT_ER(r, (_T("fail to get sector")));
	}
	else
	{
		FFAT_ASSERT(pBuff);

		r = ffat_al_readSector(dwSector, pBuff, 1, dwFlag, pCI);
		IF_UK (r != 1)
		{
			FFAT_LOG_PRINTF((_T("Fail to read a sector")));
			return FFAT_EIO;
		}

		FFAT_MEMSET(pBuff + dwStartOffset, 0x00, dwSize);

		// ffat_fs_cache_discard(dwSector, 1, pVolInfo);

		r = ffat_al_writeSector(dwSector, pBuff, 1, dwFlag | FFAT_CACHE_DIRTY, pCI);
		IF_UK (r != 1)
		{
			FFAT_LOG_PRINTF((_T("Fail to read a sector")));
			return FFAT_EIO;
		}
	}

	return FFAT_OK;
}


/**
 * 하나의 sector에 대해 일부 영역의 read/write를 수행한다.
 *
 * @param		pVolInfo		: [IN] volume pointer
 * @param		dwSector		: [IN] sector number
 * @param		dwStartOffset	: [IN] start offset
 * @param		dwSize			: [IN] read write size in byte
 *										size 가 sector 크기를 초과할 경우 무시 된다.
 * @param		pBuff			: [IN] read/write data storage
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information
 * @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
 * @return		positive		: read size
 * @return		negative		: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @history		MAR-02-2007 [DongYoung Seo] Add memory free check routine
 */
t_int32
ffat_fs_misc_readWritePartialSector(FatVolInfo* pVolInfo, t_uint32 dwSector,
					t_int32 dwStartOffset, t_int32 dwSize, t_int8* pBuff, 
					FFatCacheFlag dwFlag, FFatCacheInfo* pCI,t_boolean bRead)
{
	t_int8*				pReadBuff = NULL;
	FFatfsCacheEntry*	pEntry = NULL;
	FFatErr				r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT(dwSize >= 0);

	// sector size adjustment
	if ((dwStartOffset + dwSize - 1) >= VI_SS(pVolInfo))
	{
		dwSize = VI_SS(pVolInfo) - dwStartOffset;
	}

	// meta data 일 경우는 FFatfsCache 이용.
	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVolInfo);
		FFAT_ER(r, (_T("fail to get sector")));

		pReadBuff = pEntry->pBuff;
	}
	else
	{
		pReadBuff = (t_int8*)FFAT_LOCAL_ALLOC(VI_SS(pVolInfo), VI_CXT(pVolInfo));
		FFAT_ASSERT(pReadBuff != NULL);

		r = ffat_al_readSector(dwSector, pReadBuff, 1, dwFlag, pCI);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to read a sector")));
			r = FFAT_EIO;
			goto out;
		}
	}

	if (bRead == FFAT_TRUE)
	{
		FFAT_MEMCPY(pBuff, (pReadBuff + dwStartOffset), dwSize);
	}
	else
	{
		FFAT_MEMCPY((pReadBuff + dwStartOffset), pBuff, dwSize);

		if ((dwFlag & FFAT_CACHE_DATA_META) == 0)
		{
			r = ffat_al_writeSector(dwSector, pReadBuff, 1, dwFlag, pCI);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to read a sector")));
				r = FFAT_EIO;
				goto out;
			}
		}
		else
		{
			dwFlag |= FFAT_CACHE_DIRTY;
		}
	}

	if (dwFlag & FFAT_CACHE_DATA_META)
	{
		r = ffat_fs_cache_putSector(pVolInfo, pEntry, dwFlag, pCI->pNode);
		FFAT_ER(r, (_T("fail to put ffatfs cache")));
	}

	r = dwSize;

out:
	if ((dwFlag & FFAT_CACHE_DATA_META) == 0)
	{
		FFAT_LOCAL_FREE(pReadBuff, VI_SS(pVolInfo), VI_CXT(pVolInfo));
	}

	return r;
}


/**
* read write full sectors
*
* @param		pVolInfo		: [IN] volume pointer
* @param		dwSector		: [IN] sector number
* @param		dwCount			: [IN] sector count
*										size 가 sector 크기를 초과할 경우 무시 된다.
* @param		pBuff			: [IN] read/write data storage
* @param		dwFlag			: [IN] cache flag
* @param		pCI				: [IN] cache information
* @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
* @return		positive		: read sector count
* @return		negative		: error
* @author		DongYoung Seo
* @history		MAY-26-2007 [DongYoung Seo] First write
*/
t_int32
ffat_fs_misc_readWriteSectors(FatVolInfo* pVolInfo, t_uint32 dwSector, t_int32 dwCount, t_int8* pBuff,
									FFatCacheFlag dwFlag, FFatCacheInfo* pCI,t_boolean bRead)
{
	FFatErr		r;

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pCI);

	if (bRead)
	{
		if (dwFlag & FFAT_CACHE_DATA_META)
		{
			r = ffat_fs_cache_readSector(dwSector, pBuff, dwCount, dwFlag, pVolInfo);
		}
		else
		{
			r = ffat_al_readSector(dwSector, pBuff, dwCount, dwFlag, pCI);
		}
	}
	else
	{
		if (dwFlag & FFAT_CACHE_DATA_META)
		{
			r = ffat_fs_cache_writeSector(dwSector, pBuff, dwCount, dwFlag, pVolInfo, pCI->pNode);
		}
		else
		{
			// ffat_fs_cache_discard(dwSector, dwSectorCount, pVolInfo);
			r = ffat_al_writeSector(dwSector, pBuff, dwCount, dwFlag, pCI);
		}
	}

	IF_UK (r != dwCount)
	{
		FFAT_LOG_PRINTF((_T("Fail to read/write cluster")));
		return FFAT_EIO;
	}

	return dwCount;
}

#ifdef FFAT_DEBUG

	/**
	 * print a buffer of DE for debug purpose
	 *
	 * @param		pDE			: [IN] buffer for DE
	 * @return		void
	 * @author		GwangOk Go
	 * @version		FEB-16-2009 [GwangOk Go] First Writing.
	 */
	void
	ffat_debug_printDE(t_int8* pDE)
	{
		t_int32     dwIndex;

		for (dwIndex = 0; dwIndex < 32; dwIndex++)
		{
			FFAT_PRINT_VERBOSE((_T("[%x][%c]"), *(pDE + dwIndex), *(pDE + dwIndex)));

			if (dwIndex == 15)
			{
				FFAT_PRINT_VERBOSE((_T("\n")));
			}
			else
			{
				FFAT_PRINT_VERBOSE((_T(",")));
			}
		}

		FFAT_PRINT_VERBOSE((_T("\n")));

		return;
	}


	/**
	 * print structure FatGetNodeDe for debug purpose
	 *
	 * @param		pNodeDe		: [IN] FatGetNodeDe structure
	 * @return		void
	 * @author		GwangOk Go
	 * @version		FEB-16-2009 [GwangOk Go] First Writing.
	 */
	void
	ffat_debug_printGetNodeDe(FatGetNodeDe* pNodeDe)
	{
		t_int32     dwIndex = 0;

		FFAT_PRINT_VERBOSE((_T("dwCluster/dwOffset/dwClusterOfOffset: %d, %d, %d\n"), pNodeDe->dwCluster, pNodeDe->dwOffset, pNodeDe->dwClusterOfOffset));
		FFAT_PRINT_VERBOSE((_T("dwDeStartCluster/dwDeStartOffset: %d, %d\n"), pNodeDe->dwDeStartCluster, pNodeDe->dwDeStartOffset));
		FFAT_PRINT_VERBOSE((_T("dwDeEndCluster/dwDeEndOffset: %d, %d\n"), pNodeDe->dwDeEndCluster, pNodeDe->dwDeEndOffset));
		FFAT_PRINT_VERBOSE((_T("dwDeSfnCluster/dwDeSfnOffset: %d, %d\n"), pNodeDe->dwDeSfnCluster, pNodeDe->dwDeSfnOffset));
		FFAT_PRINT_VERBOSE((_T("dwEntryCount/dwTotalEntryCount: %d, %d\n"), pNodeDe->dwEntryCount, pNodeDe->dwTotalEntryCount));

		FFAT_PRINT_VERBOSE((_T("dwTargetEntryCount: %d\n"), pNodeDe->dwTargetEntryCount));
		FFAT_PRINT_VERBOSE((_T("dwNameLen: %d\n"), pNodeDe->dwNameLen));
		FFAT_PRINT_VERBOSE((_T("psShortName [%s]\n"), pNodeDe->psShortName));
		FFAT_PRINT_VERBOSE((_T("bExactOffset: %d\n"), pNodeDe->bExactOffset));

		while (dwIndex < pNodeDe->dwTotalEntryCount)
		{
			FFAT_PRINT_VERBOSE((_T(">> Index [%d]\n"), dwIndex));

			ffat_debug_printDE((t_int8*)&pNodeDe->pDE[dwIndex]);

			dwIndex++;
		}

		return;
	}
#endif
