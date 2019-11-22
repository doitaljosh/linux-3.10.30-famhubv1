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
 * @file		ffatfs_cache.c
 * @brief		cache module for FFATFS, this cache manages metadata for FATFS
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

//*****************************************************************/
//
// FFatfsCache는 FFatfs에서 접근하는 영역에 대한 caching을 위한 module이다.
// FFatfs는 FAT filesystem의 FAT과 directory entry에 대한 access만을 수행하며
// FFatfs는 이 두 영역만을 cache에 저장한다
//
// FFatfs는 최소 1개의 FFatfsCache가 있어야 한다.
// 이 cache는 defult cache로 사용이 되며 크기는 최소 2개 sector 이상이 된다.
// (사용자의 설정에 따라 크기 변경이 가능하다.)
//
// 각각의 volume별로 독립적인 cache가 사용이 가능하도록 
// FFatfsVolInfo 에는 cache를 위한 pointer를 가지고 있으며
// 따로 할당 되지 않을 경우 default cache를 사용하게 된다.
//
// 여러개의 cache에 대한 정보를 관리하기 위하여 FFatfsCacheMain 구조체가 있으며
// 여기에는 FFatfs에 생성된 모든 FFatfsCache 들이 등록된다.
//
//*****************************************************************/
//
// FFatfsCache module is a cache for area that is accessed by FFatfs
// FFatfs access FAT and DE area of FAT filesystem.
// So FFatfs just manage area for this two area.
//
// FFatfs must have one cache at least.
// This cache is called default cache, the size is minimum 2 sector
// (Cache size is configurable)
//
// FFatfsCache manages a pointer a FFatfsVolInfo to provide volume level cache.
// If a volume does not have it's own cache it uses default cache
//
// FFatfsCacheMain structure is to manage several caches
// It contains all created caches for FFatfs
//
//*****************************************************************/

// header
// header - ESS_BASE
#include "ess_math.h"
#include "ess_bitmap.h"
#include "ess_hash.h"

// header - FFAT_CORE
#include "ffat_types.h"
#include "ffat_al.h"

#include "ffat_common.h"

// header - FFAT_FATFS
#include "ffatfs_config.h"
#include "ffatfs_main.h"
#include "ffatfs_cache.h"
#include "ffatfs_misc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_CACHE)

//#define _DEBUG_CACHE

#define _BUFF_SIZE_FOR_SYNC		(FFAT_TEMP_BUFF_MAX_SIZE / 2)			// temporary buffer size for _syncEntries()

#if (_BUFF_SIZE_FOR_SYNC < 32)
	#error "too small buffer size for sync!!"
#endif

// abstraction layer
static FFatfsCacheMain		_stCacheMain;		//!< FFatfs cache

#define _CACHE_SIG			((t_uint8)0xF0)		// signature for Cache structure

// defines
// pE : entry pointer
// pC : cache pointer
// dwS : sector number
// hash	: hash value

#define _IS_DIRTY(_pE)			(((_pE)->dwFlag & FFAT_CACHE_DIRTY) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_LOCKED(_pE)			(((_pE)->dwFlag & FFAT_CACHE_LOCK) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_SHARED(_pE)			(((_pE)->dwFlag & FFAT_CACHE_SHARED) ? FFAT_TRUE : FFAT_FALSE)

#define _SET_FREE(_pE)			(_pE)->dwFlag = FFAT_CACHE_FREE		// set free entry
#define _SET_DIRTY(_pE)			FFATFS_CACHE_SET_DIRTY(_pE)			// set dirty flag
#define _SET_CLEAN(_pE)			(_pE)->dwFlag &= ~FFAT_CACHE_DIRTY	// set clean flag
#define _SET_LOCK(_pE)			(_pE)->dwFlag |= FFAT_CACHE_LOCK	// set lock flag
#define _CLEAN_LOCK(_pE)		(_pE)->dwFlag &= ~FFAT_CACHE_LOCK	// clean lock flag
#define _CLEAN_SYNC(_pE)		(_pE)->dwFlag &= ~FFAT_CACHE_SYNC	// clean sync flag
#define _SET_SHARED(_pE)		(_pE)->dwFlag |= FFAT_CACHE_SHARED	// set shared flag
#define _CLEAN_SHARED(_pE)		(_pE)->dwFlag &= ~FFAT_CACHE_SHARED	// clean shared flag

#define _SET_NODE(_pE, _pN)		(_pE)->pNode = _pN				// set node pointer
#define _CLEAN_NODE(_pE)		(_pE)->pNode = NULL				// clean node pointer

// add an entry to free list
#define _ADD_TO_FREE(_pC, _pE)		\
				ESS_HASH_ADD_TO_FREE(&(_pC)->stHash, &(_pE)->stHashEntry)

// move an entry to free list
#define _MOVE_TO_FREE(_pC, _pE)		\
				do {				\
					ESS_HASH_MOVE_TO_FREE(&(_pC)->stHash, &(_pE)->stHashEntry);\
					ESS_LRU_REMOVE_INIT(&(_pE)->stLruEntry);	\
				} while (0)

// move to head of clean list
#define _REMOVE_FROM_DIRTY(_pE)		\
				do {				\
						_SET_CLEAN(_pE);									\
						ESS_DLIST_DEL_INIT(&(_pE)->dlDirty);			\
				} while (0)

// move to head of dirty list
#define _MOVE_TO_DIRTY(_pC, _pE)	\
				do {				\
					_SET_DIRTY(pEntry);													\
					ESS_DLIST_MOVE_HEAD(&(_pC)->dlDirty, &(_pE)->dlDirty);	\
				} while (0)

// update Hash List
#define _UPDATE_HASH(_pC, _dwH, _pE)		\
				ESS_HASH_MOVE_TO_HEAD(&(_pC)->stHash, _dwH, &(_pE)->stHashEntry)

// update LRU list
#define	_UPDATE_LRU(_pC, _pE)				\
				ESS_LRU_MOVE_TO_HEAD(&(_pC)->stLru, &(_pE)->stLruEntry)

// discard entry
#define _ENTRY_DISCARD(_pC, _pE)	do {									\
										/* move to free list */				\
										(_pE)->dwFlag	= FFAT_CACHE_NONE;	\
										_SET_CLEAN(_pE);					\
										_REMOVE_FROM_DIRTY(_pE);			\
										_MOVE_TO_FREE(_pC, _pE);			\
										/* remove from volume list */		\
										_DETACH_FROM_VOLUME((_pE));			\
										(_pE)->pVolInfo = NULL;				\
									} while (0)

// get hash value. (bucket number)
#define _GET_HASH(_dwSector)		(t_int32)((_dwSector) & FFATFS_CACHE_HASH_MASK)

// attach an entry to volume
#define _ATTACH_TO_VOLUME(_pVolInfo, _pE)		\
									ESS_DLIST_MOVE_HEAD((&(_pVolInfo)->dlFatCacheDirty), &(_pE)->stDListVol)
// detach an entry from volume
#define _DETACH_FROM_VOLUME(_pE)	ESS_DLIST_DEL_INIT(&(_pE)->stDListVol)


#define _DIRTY_LIST(_pC)			(&((_pC)->dlDirty))

// local variables
static PFN_HASH_CMP		_pfCacheEntryCmp = NULL;

// static functions
static t_int32	_readSectorFromCache(t_uint32 dwSector, t_int8* pBuff, t_uint32 dwLastSector,
							FatVolInfo* pVolInfo, t_uint8* pBitmap);
static t_int32	_readSectorWithBitmap(t_uint32 dwSector, t_int8* pBuff, t_uint32 dwLastSector,
							FFatCacheFlag dwFlag, FatVolInfo* pVolInfo, t_uint8* pBitmap);
static FFatfsCacheEntry*	
				_lookupSector(FatVolInfo* pVI, t_uint32 dwSector);
static FFatErr	_addNewSectors(FatVolInfo* pVolInfo, t_uint32 dwStartSector, 
							t_int8* pBuff, t_int32 dwCount, FFatCacheFlag dwFlag);
static FFatErr	_getFreeEntry(FatVolInfo* pVolInfo, FFatfsCacheEntry** ppFreeEntry);

static FFatErr	_initCache(FFatfsCacheInfo* pCache, t_int8* pBuff, t_int32 dwSize,
							FatVolInfo* pVolInfo, t_int32 dwSectorSize,
							FFatfsCacheInfoFlag wFlag);
static FFatfsCacheInfo*	_getCache(t_int32 dwSectorSize);
static FFatfsCacheInfo*	_lookupCache(t_int32 dwSectorSize);
static FFatErr			_removeCache(FFatfsCacheInfo* pCache);

static FFatErr			_autoAddCache(t_int32 dwSectorSize);
static FFatErr			_addCache(t_int8* pBuff, t_int32 dwSize, t_int32 dwSectorSize,
									FFatfsCacheInfoFlag wFlag);
static FFatErr			_syncEntries(FFatfsCacheInfo* pCache, FatVolInfo* pVolInfo,
								FFatfsCacheEntry** ppEntry,
								t_int32 dwCount, FFatCacheFlag dwFlag, ComCxt* pCxt);

typedef struct
{
	t_uint32	dwSector;
	FatVolInfo*	pVolInfo;
} _CacheCmp;

#ifdef _DEBUG_CACHE
	#define FFAT_DEBUG_CACHE_PRINTF		FFAT_PRINT_VERBOSE((_T("[FATCACHE] %s()/%d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE

	#ifdef FFAT_DEBUG
		static void	_debugCheckCacheValidity(FatVolInfo* pVolInfo, t_uint32 dwSectorNo);
	#else
		#define _debugCheckCacheValidity(pVolInfo, dwSectorNo)
	#endif
#else
	#define FFAT_DEBUG_CACHE_PRINTF(_msg)
	#define _debugCheckCacheValidity(pVolInfo, dwSectorNo)
#endif

#define _STATISTICS_ENTRYCOMPARE
#define _STATISTICS_SYNCVOL
#define _STATISTICS_SYNCNODE
#define _STATISTICS_SYNC
#define _STATISTICS_SYNCALL
#define _STATISTICS_SYNCENTRY
#define _STATISTICS_FLUSHVOL
#define _STATISTICS_READSECTOR
#define _STATISTICS_WRITESECTOR
#define _STATISTICS_GETSECTOR
#define _STATISTICS_PUTSECTOR
#define _STATISTICS_DISCARD
#define _STATISTICS_CALLBACK
#define _STATISTICS_UPDATE
#define _STATISTICS_LOOKUPSECTOR
#define _STATISTICS_ADDNEWSECTOR
#define _STATISTICS_GETFREEENTRY
#define _STATISTICS_HIT
#define _STATISTICS_MISS
#define _STATISTICS_PRINT

#define _DEBUG_CHECK_CACHE_COUNT(_a)		((void)0)

// debug begin
#ifdef FFAT_DEBUG
	// statistics
	#undef _STATISTICS_ENTRYCOMPARE
	#undef _STATISTICS_SYNCVOL
	#undef _STATISTICS_SYNCNODE
	#undef _STATISTICS_SYNC
	#undef _STATISTICS_SYNCALL
	#undef _STATISTICS_SYNCENTRY
	#undef _STATISTICS_FLUSHVOL
	#undef _STATISTICS_READSECTOR
	#undef _STATISTICS_WRITESECTOR
	#undef _STATISTICS_GETSECTOR
	#undef _STATISTICS_PUTSECTOR
	#undef _STATISTICS_DISCARD
	#undef _STATISTICS_CALLBACK
	#undef _STATISTICS_UPDATE
	#undef _STATISTICS_LOOKUPSECTOR
	#undef _STATISTICS_ADDNEWSECTOR
	#undef _STATISTICS_GETFREEENTRY
	#undef _STATISTICS_HIT
	#undef _STATISTICS_MISS
	#undef _STATISTICS_PRINT

	#undef _DEBUG_CHECK_CACHE_COUNT

	typedef struct
	{
		t_uint32 dwEntryCompare;
		t_uint32 dwSyncVol;
		t_uint32 dwSyncNode;
		t_uint32 dwSync;
		t_uint32 dwSyncAll;
		t_uint32 dwSyncEntry;
		t_uint32 dwFlushVol;
		t_uint32 dwReadSector;
		t_uint32 dwWriteSector;
		t_uint32 dwGetSector;
		t_uint32 dwPutSector;
		t_uint32 dwDiscard;
		t_uint32 dwCallback;
		t_uint32 dwUpdate;
		t_uint32 dwLookupSector;
		t_uint32 dwAddNewSector;
		t_uint32 dwGetFreeEntry;
		t_uint32 dwHit;
		t_uint32 dwMiss;
	} _Statistic;

	#define _STATISTICS()		((_Statistic*)&_stStatistics)

	//static _MainDebug	_stMainDebug;
	static t_int32 _stStatistics[sizeof(_Statistic) / sizeof(t_int32)];

	#define _STATISTICS_ENTRYCOMPARE	_STATISTICS()->dwEntryCompare++;
	#define _STATISTICS_SYNCVOL			_STATISTICS()->dwSyncVol++;
	#define _STATISTICS_SYNCNODE		_STATISTICS()->dwSyncNode++;
	#define _STATISTICS_SYNC			_STATISTICS()->dwSync++;
	#define _STATISTICS_SYNCALL			_STATISTICS()->dwSyncAll++;
	#define _STATISTICS_SYNCENTRY		_STATISTICS()->dwSyncEntry++;
	#define _STATISTICS_FLUSHVOL		_STATISTICS()->dwFlushVol++;
	#define _STATISTICS_READSECTOR		_STATISTICS()->dwReadSector++;
	#define _STATISTICS_WRITESECTOR		_STATISTICS()->dwWriteSector++;
	#define _STATISTICS_GETSECTOR		_STATISTICS()->dwGetSector++;
	#define _STATISTICS_PUTSECTOR		_STATISTICS()->dwPutSector++;
	#define _STATISTICS_DISCARD			_STATISTICS()->dwDiscard++;
	#define _STATISTICS_CALLBACK		_STATISTICS()->dwCallback++;
	#define _STATISTICS_UPDATE			_STATISTICS()->dwUpdate++;
	#define _STATISTICS_LOOKUPSECTOR	_STATISTICS()->dwLookupSector++;
	#define _STATISTICS_ADDNEWSECTOR	_STATISTICS()->dwAddNewSector++;
	#define _STATISTICS_GETFREEENTRY	_STATISTICS()->dwGetFreeEntry++;
	#define _STATISTICS_HIT				_STATISTICS()->dwHit++;
	#define _STATISTICS_MISS			_STATISTICS()->dwMiss++;

	#define _STATISTICS_PRINT			_statisticsPrint();
	static void _statisticsPrint(void);

	#define _DEBUG_CHECK_CACHE_COUNT(_pVI)		((void)0)

	#if 0
		#define _DEBUG_CHECK_CACHE_COUNT(_pVI)	\
					do{										\
						t_int32			dwFreeCount;		\
						t_int32			dwLRUCount;			\
						if (_pVI != NULL)					\
						{									\
							dwLRUCount = EssDList_Count(&(_pVI)->pFatCache->stLru.stDListLru);	\
							dwFreeCount = EssDList_Count(&(_pVI)->pFatCache->stHash.stDListFree);	\
							if ((dwLRUCount + dwFreeCount) != 57)											\
							{																				\
								FFAT_DEBUG_PRINTF("LRU/FREE:%d/%d\n", dwLRUCount, dwFreeCount);				\
								FFAT_ASSERT((dwLRUCount + dwFreeCount) == 57);								\
							}																				\
						}																					\
					} while (0)
	#endif
#endif
// debug end


/**
 * This function initializes FFatfsCache
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid configuration
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
 * @version		MAR-24-2009 [DongYoung Seo] BUG FIX- increase reference count of default cache
 *												to be freed on termination
*/
FFatErr
ffat_fs_cache_init(void)
{
	t_int32				i;
	FFatErr				r;
	FFatfsCacheInfo*	pCache;

	_stCacheMain.wCachecount = 0;
	ESS_DLIST_INIT(&_stCacheMain.dlCaches);

	// check bucket count
	IF_UK (EssMath_IsPowerOfTwo(FFATFS_CACHE_HASH_BUCKET_COUNT) == 0)
	{
		FFAT_LOG_PRINTF((_T("FFATFS_CACHE_HASH_BUCKET_COUNT is not power of 2")));
		return FFAT_EINVALID;
	}

	_pfCacheEntryCmp = ffat_fs_cache_entryCompare;

	// initialize callback entries
	for (i = 0; i < FFATFS_CACHE_CALLBACK_COUNT; i++)
	{
		_stCacheMain.pfCallBack[i] = NULL;
	}

	for (i = 0; i < FFATFS_MAX_CACHE_COUNT; i++)
	{
		_stCacheMain.pCacheBuff[i] = NULL;
	}

	r = _autoAddCache(ffat_al_getConfig()->dwSectorSize);
	IF_LK (r == FFAT_OK)
	{
		pCache = _lookupCache(ffat_al_getConfig()->dwSectorSize);
		pCache->wRefCount++;
	}

	return r;
}


/**
 * This function terminateFFatfsCache
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid configuration
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing
*/
FFatErr
ffat_fs_cache_terminate(void)
{
	FFatfsCacheInfo*	pCurCache;
	FFatfsCacheInfo*	pNextCache;

	ESS_DLIST_FOR_EACH_ENTRY_SAFE(FFatfsCacheInfo, pCurCache, pNextCache, &_stCacheMain.dlCaches, dlCache)
	{
		FFAT_ASSERT(pCurCache->bSig == _CACHE_SIG);
		if ((pCurCache->wFlag & FFATFS_CIFLAG_USER) == 0)
		{
			FFAT_FREE(pCurCache, ffat_al_getConfig()->dwFFatfsCacheSize);
		}
	}

	FFAT_MEMSET(&_stCacheMain, 0x00, sizeof(FFatfsCacheMain));

	_STATISTICS_PRINT

	return FFAT_OK;
}


/**
 * This function lookup a cache that size is dwSectorSize
 *
 * @param		wSectorSize	: sector size in byte
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
*/
FFatfsCacheInfo*
ffat_fs_cache_getCache(t_int32 dwSectorSize)
{
	FFatfsCacheInfo*	pCache;

	pCache = _getCache(dwSectorSize);

#ifdef FFATFS_AUTO_CACHE_ADD_REMOVE
	if (pCache == NULL)
	{
		_autoAddCache(dwSectorSize);
		pCache = _getCache(dwSectorSize);
	}
#endif

	return pCache;
}


/**
 * This function lookup a cache that size is dwSectorSize
 *
 * @param		pCache	: cache information
 * @return		void
 * @author		DongYoung Seo
 * @version		MAY-21-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_cache_putCache(FFatfsCacheInfo* pCache)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pCache);
	FFAT_ASSERT(pCache->wRefCount > 0);

	pCache->wRefCount--;

	if (pCache->wRefCount == 0)
	{
		// release memory for this cache
		r = _removeCache(pCache);
	}

	return r;
}


/**
 * This function checks cache existence for dwSectorSize
 *
 * @param		dwSectorSize	: [IN]sector size in byte
 * @param		ppBuff			: [OUT] storage of buffer pointer
 *									maybe NULL
 * @return		FFAT_TRUE		: the cache is exist
 * @return		FFAT_FALSE		: the cache is not exist
 * @author		DongYoung Seo
 * @version		MAY-21-2008 [DongYoung Seo] First Writing.
 * @version		JAN-06-2009 [DongYoung Seo] add ppBuff to return pointer of buffer
*/
t_boolean
ffat_fs_cache_checkCache(t_int32 dwSectorSize, t_int8** ppBuff)
{
	FFatfsCacheInfo*	pCache;

	pCache = _lookupCache(dwSectorSize);
	if (pCache == NULL)
	{
		return FFAT_FALSE;
	}

	if (ppBuff)
	{
		*ppBuff = (t_int8*)pCache;
	}

	return FFAT_TRUE;
}


/**
 * This function compares two entries
 *
 * @param		pTarget			: pointer of sector number storage
 * @param		pHashEntry		: a hash entry
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_boolean
ffat_fs_cache_entryCompare(void* pTarget, EssHashEntry* pHashEntry)
{
	FFatfsCacheEntry*	pEntry;
	_CacheCmp*			pCmp;

	FFAT_ASSERT(pTarget);
	FFAT_ASSERT(pHashEntry);

	_STATISTICS_ENTRYCOMPARE

	pCmp	= (_CacheCmp*)pTarget;
	pEntry = ESS_GET_ENTRY(pHashEntry, FFatfsCacheEntry, stHashEntry);

	if ((pCmp->dwSector == pEntry->dwSectorNo)
		&& (pCmp->pVolInfo->wTimeStamp == pEntry->wTimeStamp))
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/**
 * This function add a new cache to cache list and increase cache count.
 *
 * @param		pBuff			: buffer for cache
 * @param		dwSize			: cache size in byte
 * @param		dwSectorSize	: sector size for cache operation
 * @return		void
 * @author		DongYoung Seo
 * @version		JAN-14-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_addCache(t_int8* pBuff, t_int32 dwSize, t_int32 dwSectorSize)
{
	if (ffat_fs_cache_checkCache(dwSectorSize, NULL) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("A proper cache is already added")));
		return FFAT_EBUSY;
	}

	return _addCache(pBuff, dwSize, dwSectorSize, FFATFS_CIFLAG_USER);
}


/**
 * This function remove a cache from cache list and decrease cache count.
 *
 * @param		pBuff			: buffer pointer for cache
 * @param		pCxt			: context
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		JAN-14-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_removeCache(t_int8* pBuff, ComCxt* pCxt)
{
	FFatfsCacheInfo*	pCache;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pBuff == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	pBuff = (t_int8*)FFAT_GET_ALIGNED_ADDR(pBuff);
	pCache = (FFatfsCacheInfo*)pBuff;

	IF_UK (pCache->bSig != _CACHE_SIG)
	{
		FFAT_LOG_PRINTF((_T("Invalid signature")));
		return FFAT_EINVALID;
	}

	IF_UK(pCache->wRefCount > 0)
	{
		FFAT_LOG_PRINTF((_T("Current cache is used for some volume!!")));
		return FFAT_EBUSY;
	}

	// sync it, do not check error
	ffat_fs_cache_sync(pCache, FFAT_CACHE_SYNC, pCxt);

	return _removeCache(pCache);
}


/**
 * This function sync a volume
 *
 *
 * @param		pVolInfo	: volume pointer
 * @param		dwFlag		: sync flag
 *								Valid flag : 
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_syncVol(FatVolInfo* pVolInfo, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	EssDList*			pDListCur;
	FFatfsCacheEntry**	ppEntries;			// cache entry storage
	t_int32				dwCount;			// count of entry
	t_int32				dwMaxEntryCount;		// total entry count

	FFatErr				r = FFAT_OK;

	_STATISTICS_SYNCVOL

	FFAT_ASSERT(pVolInfo);

	// check dirty cache list
	if (ESS_DLIST_IS_EMPTY(&pVolInfo->dlFatCacheDirty))
	{
		return FFAT_OK;
	}

	ppEntries = (FFatfsCacheEntry**)FFAT_LOCAL_ALLOC(_BUFF_SIZE_FOR_SYNC, pCxt);
	FFAT_ASSERT(ppEntries != NULL);

	dwMaxEntryCount = _BUFF_SIZE_FOR_SYNC / sizeof(FFatfsCacheEntry*);

	do
	{
		// get entries
		pDListCur = &pVolInfo->dlFatCacheDirty;
		for (dwCount = 0; dwCount < dwMaxEntryCount; dwCount++)
		{
			pDListCur = ESS_DLIST_GET_NEXT(pDListCur);
			if (pDListCur == &pVolInfo->dlFatCacheDirty)
			{
				// no more dirty entry
				break;
			}

			ppEntries[dwCount] = ESS_GET_ENTRY(pDListCur, FFatfsCacheEntry, stDListVol);

			FFAT_ASSERT(ppEntries[dwCount]->pVolInfo == pVolInfo);
			FFAT_ASSERT(ppEntries[dwCount]->dwFlag & FFAT_CACHE_DIRTY);
		}

		r = _syncEntries(VI_CACHE(pVolInfo), pVolInfo, ppEntries, dwCount, dwFlag, pCxt);
		FFAT_EO(r, (_T("fail to sync entries")));

		if (ESS_DLIST_IS_EMPTY(&pVolInfo->dlFatCacheDirty))
		{
			// no more dirty entry
			break;
		}
	} while(1);

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(ppEntries, _BUFF_SIZE_FOR_SYNC, pCxt);
	return r;
}


/**
* This function sync caches for a node
*
* @param		pVolInfo	: volume pointer
* @param		pNode		: node pointer
*								FFATFS does not know about the Node structure
*								It just distinguish it with a pointer value.
* @param		dwFlag		: cache flag
*								FFAT_CACHE_SYNC
* @return		FFAT_OK		: success
* @return		else		: error
* @author		DongYoung Seo
* @version		NOV-28-2007 [DongYoung Seo] First Writing.
* @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
*/
FFatErr
ffat_fs_cache_syncNode(FatVolInfo* pVolInfo, void* pNode, FFatCacheFlag dwFlag)
{
	EssDList*			pDListCur;
	FFatfsCacheEntry**	ppEntries;			// cache entry storage
	FFatfsCacheEntry*	pEntry;				// current entry
	t_int32				dwCount;			// count of entry
	t_int32				dwMaxEntryCount;	// total entry count

	FFatErr				r = FFAT_OK;

	FFAT_ASSERT(pVolInfo);

	_STATISTICS_SYNCNODE

	// check dirty cache list
	if (ESS_DLIST_IS_EMPTY(&pVolInfo->dlFatCacheDirty))
	{
		return FFAT_OK;
	}

	ppEntries = (FFatfsCacheEntry**)FFAT_LOCAL_ALLOC(_BUFF_SIZE_FOR_SYNC, VI_CXT(pVolInfo));
	FFAT_ASSERT(ppEntries != NULL);

	dwMaxEntryCount = _BUFF_SIZE_FOR_SYNC / sizeof(FFatfsCacheEntry*);

	do
	{
		// get entries
		pDListCur = &pVolInfo->dlFatCacheDirty;
		for (dwCount = 0; dwCount < dwMaxEntryCount; /* None */)
		{
			pDListCur = ESS_DLIST_GET_NEXT(pDListCur);
			if (pDListCur == &pVolInfo->dlFatCacheDirty)
			{
				// no more dirty entry
				break;
			}

			pEntry = ESS_GET_ENTRY(pDListCur, FFatfsCacheEntry, stDListVol);

			FFAT_ASSERT(pEntry->pVolInfo == pVolInfo);
			FFAT_ASSERT(pEntry->dwFlag & FFAT_CACHE_DIRTY);

			if ((pEntry->pNode == pNode) || (_IS_SHARED(pEntry) == FFAT_TRUE))
			{
				ppEntries[dwCount] = pEntry;
				dwCount++;
			}
		}

		if (dwCount == 0)
		{
			// there is no dirty entry for this node
			break;
		}

		r = _syncEntries(VI_CACHE(pVolInfo), pVolInfo, ppEntries, dwCount, dwFlag, VI_CXT(pVolInfo));
		FFAT_EO(r, (_T("fail to sync entries")));

		_DEBUG_CHECK_CACHE_COUNT(pVolInfo);

		if (ESS_DLIST_IS_EMPTY(&pVolInfo->dlFatCacheDirty))
		{
			// no more dirty entry
			break;
		}
	} while(1);

	r = FFAT_OK;

out:
	FFAT_ASSERT((dwFlag & FFAT_CACHE_FORCE) ? r == FFAT_OK : FFAT_TRUE);

	FFAT_LOCAL_FREE(ppEntries, _BUFF_SIZE_FOR_SYNC, VI_CXT(pVolInfo));
	return r;
}


/**
 * This function sync a cache
 *
 * @param		pVolInfo	: [IN] Volume information
 * @param		pCache		: [IN] a cache
 * @param		dwFlag		: [IN] sync flag
 * @param		pCxt		: [IN] cUrrent context
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_sync(FFatfsCacheInfo* pCache, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	EssDList*			pDListHead;			// head of dirty list
	EssDList*			pDListNext;			// DLIST for the first dirty entry
	FFatfsCacheEntry*	pEntry;

	FFatErr				r;

	FFAT_ASSERT(pCache);

	_STATISTICS_SYNC

	do
	{
		// check dirty cache list
		if (ESS_DLIST_IS_EMPTY(_DIRTY_LIST(pCache)))
		{
			return FFAT_OK;
		}

		pDListHead	= _DIRTY_LIST(pCache);

		// get first dirty entry
		pDListNext	= ESS_DLIST_GET_NEXT(pDListHead);
		FFAT_ASSERT(pDListNext != pDListHead);

		pEntry		= ESS_GET_ENTRY(pDListNext, FFatfsCacheEntry, dlDirty);

		r = ffat_fs_cache_syncVol(pEntry->pVolInfo, dwFlag, pCxt);
		if (r < 0)
		{
			// force flag check
			if ((dwFlag & FFAT_CACHE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to sync an entry")));
				goto out;
			}
		}
	} while (1);

	r = FFAT_OK;

out:
	return r;
}


/**
 * This function sync all caches
 *
 * @param		dwFlag	: sync flag
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_syncAll(FFatCacheFlag dwFlag, ComCxt* pCxt)
{

	FFatfsCacheInfo*	pCache;
	EssDList*			pDListCur;
	FFatErr				r;

	_STATISTICS_SYNCALL

	ESS_DLIST_FOR_EACH(pDListCur, &(_stCacheMain.dlCaches))
	{
		pCache = ESS_DLIST_GET_ENTRY(pDListCur, FFatfsCacheInfo, dlCache);
		r = ffat_fs_cache_sync(pCache, dwFlag, pCxt);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to sync a cache !!")));
			goto out;
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * This function sync an entry
 *
 * @param		pCache		: cache structure
 * @param		pVolInfo	: Volume Information
 * @param		pEntry		: a cache entry to be syncs
 * @param		dwFlag		: Cache flag
 *								FFAT_CACHE_DISCARD	: just discard it
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
 */
FFatErr
ffat_fs_cache_syncEntry(FFatfsCacheInfo* pCache, struct _FatVolInfo* pVolInfo,
							FFatfsCacheEntry* pEntry, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	FFatErr			r = FFAT_OK;
	t_uint32		dwSector;
	t_int32			dwFatCount;	// for Fat mirroring
	t_int32			i;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pCache);
	FFAT_ASSERT(pEntry);

	_STATISTICS_SYNCENTRY

	if (_IS_DIRTY(pEntry) == FFAT_FALSE)
	{
		goto out;
	}

	// check time stamp
	IF_UK (((pVolInfo != NULL) && (pEntry->wTimeStamp != pVolInfo->wTimeStamp))
			|| (pEntry->wTimeStamp != pEntry->pVolInfo->wTimeStamp))
	{
		_MOVE_TO_FREE(pCache, pEntry);

		// remove dirty flag
		_SET_CLEAN(pEntry);
		_CLEAN_NODE(pEntry);
		_CLEAN_SHARED(pEntry);

		// move entry to clean list
		_REMOVE_FROM_DIRTY(pEntry);

		// remove from volume, do not delete(disconnect) from volume
		// just delete !!. (head for volume is already removed)
		ESS_DLIST_INIT(&pEntry->stDListVol);

		return FFAT_OK;
	}

	dwFlag |= (FFAT_CACHE_SYNC | pEntry->dwFlag);

	dwSector = pEntry->dwSectorNo;
	if (dwFlag & FFAT_CACHE_DATA_FAT)
	{
		dwFatCount = VI_FC(pEntry->pVolInfo);
	}
	else
	{
		dwFatCount = 1;
	}

	if (dwFlag & FFAT_CACHE_SYNC_CALLBACK)
	{
		// callback v2.0.2
		for (i = 0; i < FFATFS_CACHE_CALLBACK_COUNT; i++)
		{
			if (_stCacheMain.pfCallBack[i])
			{
				r = _stCacheMain.pfCallBack[i](pEntry->pVolInfo, pEntry->dwSectorNo,
							pEntry->dwFlag, pCxt);
				FFAT_ER(r, (_T("fail on callback routine")));
			}
		}
	}

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pEntry->pVolInfo));

re:
	FFAT_DEBUG_CACHE_PRINTF((_T("SYNC entry(0x%x), sec no:%d\n"), (t_uint32)pEntry, dwSector));

	r = ffat_al_writeSector(dwSector, pEntry->pBuff, 1, 
					(dwFlag | FFAT_CACHE_SYNC | FFAT_CACHE_META_IO), &stCI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to write a sector")));
		return FFAT_EIO;
	}

	if ((dwFlag & FFAT_CACHE_DATA_FAT) && (VI_FLAG(pVolInfo) & VI_FLAG_FAT_MIRROR))
	{
		// check mirror
		dwFatCount--;
		if (dwFatCount > 0)
		{
			dwSector += VI_FSC(pEntry->pVolInfo);
			goto re;
		}
	}

out:
	if (dwFlag & FFAT_CACHE_DISCARD)
	{
		FFAT_DEBUG_CACHE_PRINTF((_T("DISCARD entry(0x%x), sec no:%d\n"), (t_uint32)pEntry, pEntry->dwSectorNo));

		_DEBUG_CHECK_CACHE_COUNT(pEntry->pVolInfo);

		FFAT_ASSERT(pEntry->pVolInfo ? (pEntry->wTimeStamp == pEntry->pVolInfo->wTimeStamp) : FFAT_TRUE);
		_ENTRY_DISCARD(pCache, pEntry);

		return FFAT_OK;
	}

	// remove dirty flag
	_SET_CLEAN(pEntry);
	_CLEAN_NODE(pEntry);
	_CLEAN_SHARED(pEntry);

	// move entry to clean list
	_REMOVE_FROM_DIRTY(pEntry);
	_DETACH_FROM_VOLUME(pEntry);

	return FFAT_OK;
}


/**
 * This function flushes a volume, (remove all cache entry for the volume)
 *
 *
 * @param		pVolInfo	: volume pointer
 * @param		dwFlag		: sync flag
 *								Valid flag : 
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_cache_flushVol(FatVolInfo* pVolInfo, FFatCacheFlag dwFlag)
{
	EssDList*			pDListCur;
	EssDList*			pDListNext;
	EssDList*			pDListHead;
	FFatfsCacheEntry*	pEntry;
	FFatfsCacheInfo*	pCache;
	EssLruEntry*		pLruEntry;

	FFatErr				r;

	_STATISTICS_SYNC

	pCache		= pVolInfo->pFatCache;

	FFAT_ASSERT(pCache);

	pDListHead	= &pVolInfo->pFatCache->stLru.stDListLru;

	dwFlag |= FFAT_CACHE_SYNC | FFAT_CACHE_DISCARD | FFAT_CACHE_FORCE;		// discard it

	ESS_DLIST_FOR_EACH_SAFE(pDListCur, pDListNext, pDListHead)
	{
		pLruEntry	= ESS_GET_ENTRY(pDListCur, EssLruEntry, stDListLru);
		pEntry		= ESS_GET_ENTRY(pLruEntry, FFatfsCacheEntry,stLruEntry);

		if (pEntry->pVolInfo != pVolInfo)
		{
			continue;
		}

		r = ffat_fs_cache_syncEntry(pCache, pVolInfo, pEntry, dwFlag, VI_CXT(pVolInfo));
		if (r < 0)
		{
			// force flag check
			if ((dwFlag & FFAT_CACHE_FORCE) == 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to sync an entry")));
				goto out;
			}
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * This function read sectors from FFatfsCache
 *
 * @param		dwSector	: read start sector number
 * @param		pBuff		: buffer (storage)
 * @param		dwCount		: sector count
 * @param		dwFlag		: cache flag
								FFAT_CACHE_DATA_FAT
								FFAT_CACHE_DATA_DE
								FFAT_CACHE_DATA_META
								FFAT_CACHE_LOCK
								FFAT_CACHE_DATA_USER
								FFAT_CACHE_DIRECT_IO
 * @param		pVolInfo	: volume information
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
								FFAT_ENOMEM
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version		MAY-09-2007 [DongYoung Seo] add 1 byte to pLocalSectorBitmap for safety
 */
t_int32
ffat_fs_cache_readSector(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
						FFatCacheFlag dwFlag, FatVolInfo* pVolInfo)
{
	t_uint8*	pSectorBitmap;
	t_int32		dwReadCount;
	t_int32		dwRestCount = 0;		// rest sector count
	t_uint8		pLocalSectorBitmap[FFATFS_CACHE_SECTOR_BITMAP_LOCAL_BYTE + 1];
										// do not remove byte 1
	t_int32		dwSectorBitmapCount;
	t_uint32	dwLastSector;			// last read sector number
	t_int32		dwValidBitmapArea;		// valid byte area of bitmap
	FFatErr		r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	_STATISTICS_READSECTOR

	FFAT_ASSERT(dwSector + dwCount <= pVolInfo->dwSectorCount);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pVolInfo->pFatCache);
	FFAT_ASSERT(dwCount > 0);

	if (dwCount <= FFATFS_CACHE_SECTOR_BITMAP_LOCAL_COUNT)
	{
		pSectorBitmap		= pLocalSectorBitmap;
		dwSectorBitmapCount = FFATFS_CACHE_SECTOR_BITMAP_LOCAL_COUNT;
	}
	else
	{
		pSectorBitmap = (t_uint8*)FFAT_LOCAL_ALLOC((FFATFS_CACHE_SECTOR_BITMAP_BYTE + 1), VI_CXT(pVolInfo));
		FFAT_ASSERT(pSectorBitmap != NULL);
		dwSectorBitmapCount	= FFATFS_CACHE_SECTOR_BITMAP_COUNT;
	}

	dwRestCount	= dwCount;
	dwReadCount	= 0;

	do
	{
		// new read start sector
		dwSector += dwReadCount;
		dwLastSector = dwSector + ESS_GET_MIN(dwRestCount, dwSectorBitmapCount) - 1;

		dwValidBitmapArea = ESS_MATH_CDB((dwLastSector - dwSector + 1), 8, 3);

		// set memory to all 1,
		FFAT_MEMSET(pSectorBitmap, 0xFF, dwValidBitmapArea);

		// 1. read sectors from current cache with bitmap updating
		r = _readSectorFromCache(dwSector, pBuff, dwLastSector,
								pVolInfo, pSectorBitmap);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to read sector from cache")));
			goto free_and_out;
		}

		dwRestCount -= r;
		FFAT_ASSERT(dwRestCount >= 0);
		dwReadCount = r;

		if (dwRestCount == 0)
		{
			break;
		}

		// 2. read rest sectors
		r = _readSectorWithBitmap(dwSector, pBuff, dwLastSector,
								dwFlag, pVolInfo, pSectorBitmap);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to read rest sector with cache")));
			goto free_and_out;
		}

		dwRestCount -= r;
		FFAT_ASSERT(dwRestCount >= 0);
		dwReadCount += r;

		//increase buffer pointer
		pBuff += (dwReadCount << VI_SSB(pVolInfo));

	} while (dwRestCount > 0);

	// 3. return
	r = dwCount;

free_and_out:
	if (dwCount > FFATFS_CACHE_SECTOR_BITMAP_LOCAL_COUNT)
	{
		FFAT_LOCAL_FREE(pSectorBitmap, (FFATFS_CACHE_SECTOR_BITMAP_BYTE + 1), VI_CXT(pVolInfo));
	}

	return r;
}


/**
 * This function read sectors from FFatfsCache
 *
 * @param		dwSector	: read start sector number
 * @param		pBuff		: buffer (storage)
 * @param		dwCount		: sector count
 * @param		dwFlag		: cache flag
								FFAT_CACHE_DATA_FAT
								FFAT_CACHE_DATA_DE
								FFAT_CACHE_DATA_META
								FFAT_CACHE_LOCK
								FFAT_CACHE_DATA_USER
								FFAT_CACHE_DIRECT_IO
 * @param		pVI			: [IN] volume information
 * @param		pNode		: [IN] node pointer
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
								FFAT_ENOMEM
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @version		FEB-15-2009 [DongYoung Seo] optimize write routine
 *										remove partial sector write when flag is FFAT_CACHE_DIRECT_IO
 */
t_int32
ffat_fs_cache_writeSector(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
						FFatCacheFlag dwFlag, FatVolInfo* pVI, void* pNode)
{
	FFatfsCacheEntry*	pEntry = NULL;
	t_int32				i;
	FFatErr				r;
	FFatCacheInfo		stCI;

	FFAT_ASSERT(pVI);

	_STATISTICS_WRITESECTOR

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVI));

	if ((dwFlag & FFAT_CACHE_DIRECT_IO) == FFAT_CACHE_DIRECT_IO)
	{
		// write first
		r = ffat_al_writeSector(dwSector, pBuff, dwCount, 
						(dwFlag | FFAT_CACHE_DIRECT_IO), &stCI);
		if (r != dwCount)
		{
			FFAT_PRINT_INFO((_T("Fail to write sectors")));

			FFAT_ASSERT(r < 0);
			return r;
		}

		dwFlag &= (~FFAT_CACHE_DIRTY);

		r = ffat_fs_cache_update(pVI, dwSector, pBuff, dwCount, dwFlag);
		FFAT_ER(r, (_T("Fail to UPDATE CACHE")));
	}
	else
	{
		for (i = 0; i < dwCount; i++)
			{
			// no need to read from device
			r = ffat_fs_cache_getSector((dwSector + i), (dwFlag | FFAT_CACHE_NOREAD), &pEntry, pVI);
			FFAT_ER(r, (_T("Fail to get a sector from cache")));

			FFAT_MEMCPY(pEntry->pBuff, (pBuff + (i << VI_SSB(pVI))), VI_SS(pVI));

			r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
			FFAT_ER(r, (_T("fail to put a sector")));
		}
	}

	return dwCount;
}


/**
 * This function gets FFATFS cache entry for a sector
 *
 * @param		dwSector	: read start sector number
 *								FAT 영역일 경우에 sector 번호는 dwFlag의 FFAT_CACHE_XXX_2ND_FAT 에 따라
 *								변경 될 수 있다.
 * @param		dwFlag		: cache flag
								FFAT_CACHE_DATA_FAT
								FFAT_CACHE_DATA_DE
								FFAT_CACHE_DATA_META
								FFAT_CACHE_LOCK
								FFAT_CACHE_DATA_USER
								FFAT_CACHE_DIRECT_IO
 * @param		ppEntry		: [OUT] Entry pointer
 * @param		pVI			: [IN] volume information
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
								FFAT_ENOMEM
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
 */
FFatErr
ffat_fs_cache_getSector(t_uint32 dwSector, FFatCacheFlag dwFlag,
							FFatfsCacheEntry** ppEntry, FatVolInfo* pVI)
{
	FFatfsCacheInfo*	pCache;
	FFatCacheInfo		stCI;
	FFatErr				r;
	FFatfsCacheEntry*	pEntry;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

#ifdef FFAT_STRICT_CHECK
	IF_UK ((ppEntry == NULL) || (pVI == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(ppEntry);
	FFAT_ASSERT(pVI);

	_STATISTICS_GETSECTOR

	pCache	= pVI->pFatCache;

	pEntry = _lookupSector(pVI, dwSector);
	if (pEntry)
	{
		FFAT_ASSERT(_IS_LOCKED(pEntry) == FFAT_FALSE);

		(pEntry)->dwFlag |= (dwFlag | FFAT_CACHE_LOCK);
		*ppEntry = pEntry;

		_DEBUG_CHECK_CACHE_COUNT(pVI);
		_debugCheckCacheValidity(pVI, pEntry->dwSectorNo);
	}
	else
	{
		// get an cache entry
		r = _getFreeEntry(pVI, &pEntry);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to get a free ffatfs cache entry")));
			FFAT_ASSERT(0);
			return r;
		}

		FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVI));

		pEntry->dwFlag		= dwFlag;
		pEntry->dwSectorNo	= dwSector;

		if ((dwFlag & FFAT_CACHE_NOREAD) == 0)
		{
			// read sector with the cache
			r = ffat_al_readSector(dwSector, (pEntry)->pBuff, 1, 
								(dwFlag | FFAT_CACHE_META_IO), &stCI);
			IF_UK (r != 1)
			{
				FFAT_LOG_PRINTF((_T("Fail to read sector")));

				_DEBUG_CHECK_CACHE_COUNT(pVI);

				// discard entry and add it to free list
				FFAT_ASSERT(pEntry->wTimeStamp == pEntry->pVolInfo->wTimeStamp);
				_ENTRY_DISCARD(pVI->pFatCache, pEntry);
				return FFAT_EIO;
			}
		}

		// update hash list
		_UPDATE_HASH(pCache, _GET_HASH(pEntry->dwSectorNo), pEntry);

		_DEBUG_CHECK_CACHE_COUNT(pVI);

		// assert for detached state 
		FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(pEntry)->stDListVol) == FFAT_TRUE);

		*ppEntry = pEntry;

		FFAT_DEBUG_CACHE_PRINTF((_T("New Sector Add(0x%x), sec no:%d\n"), (t_uint32)pEntry, pEntry->dwSectorNo));
	}

	FFAT_ASSERT(pEntry->pVolInfo->pFatCache->wSectorSize == VI_SS(pVI));
	FFAT_ASSERT(pEntry->wTimeStamp == pEntry->pVolInfo->wTimeStamp);
	FFAT_ASSERT(pEntry->wTimeStamp == pVI->wTimeStamp);

	return FFAT_OK;
}


/**
 * This function releases(put) FFATFS cache entry for a sector
 *
 * @param		pVolInfo	: [IN] Volume information
 * @param		pEntry		: [IN] an entry to sync
 * @param		dwFlag		: [IN] cache flag
 *								FFAT_CACHE_SYNC		: Sync it
 *								FFAT_CACHE_UNLOCK
 *								FFAT_CACHE_DIRTY : set dirty and move to dirty list
 * @param		pNode		: [IN] Node Pointer
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_cache_putSector(FatVolInfo* pVolInfo, FFatfsCacheEntry* pEntry, FFatCacheFlag dwFlag, void* pNode)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pEntry);
	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pVolInfo == pEntry->pVolInfo);

	_STATISTICS_PUTSECTOR

	pEntry->dwFlag |= dwFlag;

	if (pEntry->dwFlag & FFAT_CACHE_DIRTY)
	{
		_SET_DIRTY(pEntry);

		// move to dirty list
		_MOVE_TO_DIRTY(pVolInfo->pFatCache, pEntry);
		_ATTACH_TO_VOLUME(pVolInfo, pEntry);

		if ((pNode != NULL) && (_IS_SHARED(pEntry) == FFAT_FALSE))
		{
			pEntry->pNode = pNode;
			_SET_NODE(pEntry, pNode);
		}
	}

	if (pEntry->dwFlag & FFAT_CACHE_SYNC)
	{
		r = ffat_fs_cache_syncEntry(VI_CACHE(pVolInfo), pVolInfo, pEntry, pEntry->dwFlag, VI_CXT(pVolInfo));
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to sync an entry")));
		}

		_CLEAN_SYNC(pEntry);
	}
	else if (pNode != NULL)
	{
		if (pEntry->pNode != pNode)
		{
			_SET_SHARED(pEntry);
		}

		_SET_NODE(pEntry, pNode);
	}

	if (pEntry->dwFlag & FFAT_CACHE_LOCK)
	{
		_CLEAN_LOCK(pEntry);
	}

	return r;
}


/**
 * This function discard cache entries at FFatfsCache
 *
 * @param		pVolInfo	: volume information
 * @param		dwSector	: discard start sector number
 * @param		dwCount		: sector count
 * @author		DongYoung Seo
 * @version		OCT-10-2006 [DongYoung Seo] First Writing.
 */
void
ffat_fs_cache_discard(FatVolInfo* pVolInfo, t_uint32 dwSector, t_int32 dwCount)
{
	FFatfsCacheEntry*	pEntry;
	t_int32				i;

	FFAT_ASSERT(pVolInfo);

	_STATISTICS_DISCARD

	for (i = 0; i < dwCount; i++)
	{
		pEntry = _lookupSector(pVolInfo, (dwSector + i));
		if (pEntry == NULL)
		{
			continue;
		}

		FFAT_ASSERT(pEntry->pVolInfo->pFatCache->wSectorSize == VI_SS(pVolInfo));
		FFAT_DEBUG_CACHE_PRINTF((_T("%s, lookup success and discard (%d) \n"), __FUNCTION__, pEntry->dwSectorNo));

		FFAT_ASSERT(pEntry->wTimeStamp == pEntry->pVolInfo->wTimeStamp);
		_ENTRY_DISCARD(pVolInfo->pFatCache, pEntry);

#ifdef FFAT_DEBUG
		FFAT_ASSERT(_lookupSector(pVolInfo, (dwSector + i)) == NULL);
#endif
	}
}


/**
* This function (un)register callback function for cache operation
*
* @param		pFN		: function pointer for callback function
* @param		bReg	: FFAT_TRUE : register, FFAT_FALSE: un-register
* @return		void
* @author		DongYoung Seo
* @version		NOV-05-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_cache_callback(PFN_CACHE_CALLBACK pFN, t_boolean bReg)
{
	t_int32		i;

	_STATISTICS_CALLBACK

	if (bReg == FFAT_TRUE)
	{
		// get free entry
		for (i = 0; i < FFATFS_CACHE_CALLBACK_COUNT; i++)
		{
			if (_stCacheMain.pfCallBack[i] == NULL)
			{
				// this is free entry
				_stCacheMain.pfCallBack[i] = pFN;
				break;
			}
		}

		if (i >= FFATFS_CACHE_CALLBACK_COUNT)
		{
			FFAT_LOG_PRINTF((_T("No more free cache callback entry")));
			return FFAT_ENOENT;
		}
	}
	else
	{
		for (i = 0; i < FFATFS_CACHE_CALLBACK_COUNT; i++)
		{
			if (_stCacheMain.pfCallBack[i] == pFN)
			{
				_stCacheMain.pfCallBack[i] = NULL;
				break;
			}
		}
	}

	return FFAT_OK;
}


/**
 * update caches only sectors that are exist at the cache
 * 
 * @param		pVI				: [IN] volume information
 * @param		dwStartSector	: [IN] start sector number
 * @param		pBuff			: [IN] buffer (storage)
 * @param		dwCount			: [IN] sector count
 * @param		dwFlag			: [IN] cache flag
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		DEC-19-2007 [DongYoung Seo] First Writing.
 * @version		FEB-17-2009 [DongYoung Seo] change dirty flag setting routine.
 *											set dirty flag when dwFlag has dirty attribute.
 */
FFatErr
ffat_fs_cache_update(FatVolInfo* pVI, t_uint32 dwStartSector,
				t_int8* pBuff, t_int32 dwCount, FFatCacheFlag dwFlag)
{
	FFatfsCacheInfo*	pCache;
	FFatfsCacheEntry*	pEntry;
	t_int32				i;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwCount > 0);

	_STATISTICS_UPDATE

	pCache	= VI_CACHE(pVI);

	for (i = 0; i < dwCount; i++)
	{
		pEntry = _lookupSector(pVI, (dwStartSector + i));
		if (pEntry == NULL)
		{
			continue;
		}

		// copy data to entry
		pEntry->dwFlag		= dwFlag;		// dwFlag has dirty flag.

		// copy data to buffer
		FFAT_MEMCPY(pEntry->pBuff, (pBuff + (i << pCache->bSectorSizeBits)), pCache->wSectorSize);

		if (dwFlag & FFAT_CACHE_DIRTY)
		{
			// move to dirty list
			_MOVE_TO_DIRTY(pCache, pEntry);
			_ATTACH_TO_VOLUME(pEntry->pVolInfo, pEntry);
		}
		else
		{
			_REMOVE_FROM_DIRTY(pEntry);
			_DETACH_FROM_VOLUME(pEntry);
		}

		FFAT_DEBUG_CACHE_PRINTF((_T("cache entry update (%d) \n"), pEntry->dwSectorNo));
	}

	return FFAT_OK;
}


//=============================================================================
//
// Static Functions
//
//

/**
 * This function lookup a sector from cache
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwSector	: [IN] sector number
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] First Writing.
 * @version		FEB-17-2009 [DongYoung Seo] add an ASSERT
 * @version		MAY-13-2009 [DongYoung Seo] remove redundnat HASH update code.
 */
static FFatfsCacheEntry*
_lookupSector(FatVolInfo* pVI, t_uint32 dwSector)
{
	EssHashEntry*		pHashEntry;
	FFatfsCacheEntry*	pEntry;
	_CacheCmp			stCmp;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	_STATISTICS_LOOKUPSECTOR

	stCmp.dwSector	= dwSector;
	stCmp.pVolInfo	= pVI;

	pHashEntry = ESS_HASH_LOOKUP(&(VI_CACHE(pVI)->stHash), _GET_HASH(dwSector),
									&stCmp, _pfCacheEntryCmp);
	if (pHashEntry == NULL)
	{
		_STATISTICS_MISS

		// the sector is not in the cache
		return	NULL;
	}

	_DEBUG_CHECK_CACHE_COUNT(pVI);
	_debugCheckCacheValidity(pVI, dwSector);

	_STATISTICS_HIT

	pEntry	= ESS_GET_ENTRY(pHashEntry, FFatfsCacheEntry, stHashEntry);

	_UPDATE_LRU(VI_CACHE(pVI), pEntry);

	FFAT_ASSERT(pEntry->pVolInfo == pVI);

	return pEntry;
}


//==========================================================
// Internal functions


/**
 * This function read sectors that is stored at the cache
 * and update bitmap to 0
 *
 * @param		dwSector		: read start sector number,
 * @param		pBuff			: buffer (storage)
 * @param		dwLastSector	: last sector number
 * @param		pVolInfo	: volume information
 * @param		pBitmap		: bitmap pointer
 *								bit 0 : read sector 
 *								bit 1 : do not read sector
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
								FFAT_ENOMEM
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version		MAY-14-2007 [DongYoung Seo] Remove incorrect assert
 */
static t_int32
_readSectorFromCache(t_uint32 dwSector, t_int8* pBuff, t_uint32 dwLastSector,
						FatVolInfo* pVolInfo, t_uint8 *pBitmap)
{
	FFatfsCacheInfo*	pCache;
	FFatfsCacheEntry*	pEntry;				//!< cache entry pointer
	t_uint32			i;
	t_int32				dwReadSectorCount = 0;
	t_uint32			dwCount;

	FFAT_ASSERT(dwLastSector <= pVolInfo->dwSectorCount);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pVolInfo->pFatCache);
	FFAT_ASSERT((dwLastSector - dwSector + 1) > 0);
	FFAT_ASSERT((dwLastSector - dwSector + 1) <= FFATFS_CACHE_SECTOR_BITMAP_COUNT);

	pCache = pVolInfo->pFatCache;

	dwCount = dwLastSector - dwSector + 1;

	for (i = 0; i < dwCount; i++)
	{
		pEntry = _lookupSector(pVolInfo, (dwSector + i));
		if (pEntry)
		{
			FFAT_DEBUG_CACHE_PRINTF((_T("%s, lookup success (%d) \n"), __FUNCTION__, pEntry->dwSectorNo));

			FFAT_MEMCPY(pBuff + (i << pCache->bSectorSizeBits), pEntry->pBuff, pCache->wSectorSize);

			FFAT_ASSERTP(ESS_BITMAP_IS_SET(pBitmap, i) == 1, (_T("duplicated sector read")));

			ESS_BITMAP_CLEAR(pBitmap, i);

			dwReadSectorCount++;
		}
	}

	return dwReadSectorCount;
}


/**
 * Block Device로 부터 sector read를 수행한다.
 * read의 대상이 되는 sectos는 bitmap의 1인 sector에 한정한다.
 * (bitmap에서 0인 sector 들은 cache에 존재하지 않는것을 보장해야한다.)
 * read 후 cache에 저장한다
 *
 * This function read sectors block device.
 * It read sectors according to the bitmap,
 * This functions read sector when the bit is 0.
 * Read sectors are stored both buffer and cache.
 *
 * @param		dwSector	: read start sector number
 * @param		pBuff		: buffer (storage)
 * @param		dwLastSector: last read sector number
 * @param		dwFlag		: cache flag
 *								FFAT_CACHE_DATA_FAT
 *								FFAT_CACHE_DATA_DE
 *								FFAT_CACHE_DATA_META
 *								FFAT_CACHE_LOCK
 *								FFAT_CACHE_DATA_USER
 *								FFAT_CACHE_DIRECT_IO
 * @param		pVolInfo	: volume information
 * @param		pBitmap		: bitmap pointer
 *								bit 1 : read
 *								bit 0 : do not read
 * @return		0 or above	: read sector count
 * @return		Negative	: error number
 *								FFAT_ENOMEM
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] First Writing.
 * @version		MAY-14-2007 [DongYoung Seo] Remove incorrect assert
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
 */
static t_int32
_readSectorWithBitmap(t_uint32 dwSector, t_int8* pBuff, t_uint32 dwLastSector,
						FFatCacheFlag dwFlag, FatVolInfo* pVolInfo, t_uint8 *pBitmap)
{
	FFatfsCacheInfo*	pCache;
	FFatCacheInfo		stCI;
	t_uint32			i;
	t_int32				r;
	t_int32				dwContSectors;
	t_int32				dwReadSectorBase = 0;
	t_int32				dwReadSectorCount = 0;
	t_int8*				pCurBuff;
	t_uint32			dwCount;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(dwLastSector <= pVolInfo->dwSectorCount);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pVolInfo->pFatCache);
	FFAT_ASSERT((dwLastSector - dwSector + 1) > 0);
	FFAT_ASSERT((dwLastSector - dwSector + 1) <= FFATFS_CACHE_SECTOR_BITMAP_COUNT);

	pCache = pVolInfo->pFatCache;

	dwCount = dwLastSector - dwSector + 1;

	// fill cache information
	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	for (i = 0; i < dwCount; i++)
	{
		if (ESS_BITMAP_IS_CLEAR(pBitmap, i) == FFAT_TRUE)
		{
			// ignore already read sectors.
			continue;
		}

		dwReadSectorBase = i;

		// ignore already read sectors.
		while (ESS_BITMAP_IS_SET(pBitmap, i) == FFAT_TRUE)
		{
			i++;

			if (i >= dwCount)
			{
				break;
			}
		}

		i--;	// i가 이미 증가 되었으므로 다시 감소 시킨다.

		dwContSectors = i - dwReadSectorBase + 1;	// get contiguous sector count

		if (dwContSectors == 0)
		{
			continue;
		}

		pCurBuff = pBuff + (dwReadSectorBase << pCache->bSectorSizeBits);
		r = ffat_al_readSector((dwSector + dwReadSectorBase), pCurBuff, dwContSectors,
						(dwFlag | FFAT_CACHE_META_IO), &stCI);
		IF_UK (r != dwContSectors)
		{
			FFAT_LOG_PRINTF((_T("Fail to read sectors from buffer cache")));
			if (r < 0)
			{
				return r;
			}
			else
			{
				return FFAT_EIO;
			}
		}

		if (dwContSectors < FFATFS_CACHE_BYPASS_TRIGGER)
		{
			// add sectors to cache
			r = _addNewSectors(pVolInfo, (dwSector + dwReadSectorBase), pCurBuff, dwContSectors, dwFlag);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to add sectors to FFatfsCache ")));
				return r;
			}
		}

		dwReadSectorCount += dwContSectors;
	}

	return dwReadSectorCount;
}


/**
 * sector들을 FFatfsCache에 추가한다.
 * 
 * This function add sector buffers to cache
 *
 * @param		pVolInfo		: [IN] volume information
 * @param		dwStartSector	: [IN] start sector number
 * @param		pBuff			: [IN] buffer (storage)
 * @param		dwCount			: [IN] sector count
 * @param		dwFlag			: [IN] cache flag
 *									FFAT_CACHE_DATA_FAT
 *									FFAT_CACHE_DATA_DE
 *									FFAT_CACHE_DATA_META
 *									FFAT_CACHE_LOCK
 *									FFAT_CACHE_DATA_USER
 *									FFAT_CACHE_DIRECT_IO
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JUL-19-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_addNewSectors(FatVolInfo* pVolInfo, t_uint32 dwStartSector, 
				t_int8* pBuff, t_int32 dwCount, FFatCacheFlag dwFlag)
{
	FFatfsCacheInfo*	pCache;
	FFatfsCacheEntry*	pFreeEntry;
	FFatErr				r;
	t_int32				i;

	_STATISTICS_ADDNEWSECTOR

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwCount > 0);

	if (dwCount > FFATFS_CACHE_BYPASS_TRIGGER)
	{
		return FFAT_OK;
	}

	if (dwFlag & FFAT_CACHE_DIRECT_IO)
	{
		return FFAT_OK;
	}

	pCache	= VI_CACHE(pVolInfo);

	// remove dirty bit
	dwFlag = dwFlag & ~FFAT_CACHE_DIRTY;

	for (i = 0; i < dwCount; i++)
	{
		r = _getFreeEntry(pVolInfo, &pFreeEntry);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("BY PASS FFATFS CACHING OPERATION, NOT ENOUGH MEMORY")));
			FFAT_DEBUG_PRINTF((_T("We can not reach here !!")));
			ESS_ASSERT(0);
			return FFAT_OK;
		}

		// copy data to entry
		pFreeEntry->dwFlag		= dwFlag;		// dwFlag has dirty flag.
		pFreeEntry->dwSectorNo	= dwStartSector + i;

		// copy data to buffer
		FFAT_MEMCPY(pFreeEntry->pBuff, (pBuff + (i << pCache->bSectorSizeBits)), pCache->wSectorSize);

		_UPDATE_HASH(pCache, _GET_HASH(pFreeEntry->dwSectorNo), pFreeEntry);
		_UPDATE_LRU(pCache, pFreeEntry);

		FFAT_DEBUG_CACHE_PRINTF((_T("%s, ADD new sector (%d) \n"), __FUNCTION__, pFreeEntry->dwSectorNo));
	}

	return FFAT_OK;
}


/**
 * free cache entry를 return 한다.
 * free entry가 없을경우 LRU 정책에 의해 free entry를 구한다.
 * 
 * This function returns a free cache entry.
 * It eliminate a least recently used entry from cache when there is no free entry.
 *
 * @param		pVolInfo	: volume information
 * @param		ppFreeEntry	: Free Entry storage
 * @return		NULL	: programming error
 * @return		else	: success
 *
 * @author		DongYoung Seo
 * @version		JUL-19-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_getFreeEntry(FatVolInfo* pVolInfo, FFatfsCacheEntry** ppFreeEntry)
{
	EssHashEntry*		pHashEntry;
	EssLruEntry*		pLruEntry;
	FFatfsCacheEntry*	pEntry;
	FFatErr				r;
	FFatfsCacheInfo*	pCache;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	ESS_ASSERT(pVolInfo);
	ESS_ASSERT(ppFreeEntry);

	_STATISTICS_GETFREEENTRY

	pCache = VI_CACHE(pVolInfo);

	// get a free entry from EssHashLru
	pHashEntry = ESS_HASH_GET_FREE(&pCache->stHash);
	if (pHashEntry == NULL)
	{
		// there is no free entry, get a least recently used entry from LRU
		pLruEntry = ESS_LRU_GET_TAIL(&pCache->stLru);
		FFAT_ASSERT(pLruEntry);
		pEntry = ESS_GET_ENTRY(pLruEntry, FFatfsCacheEntry, stLruEntry);

		FFAT_ASSERT(_IS_LOCKED(pEntry) == FFAT_FALSE);

		// sync entry if it is a dirty one.
		r = ffat_fs_cache_syncEntry(pCache, pEntry->pVolInfo, pEntry, pEntry->dwFlag, VI_CXT(pVolInfo));
		if (r < 0)
		{
			return r;
		}

		FFAT_DEBUG_CACHE_PRINTF((_T("Get a free entry, entry for sector %d is chosen as victim \n"), pEntry->dwSectorNo));
	}
	else
	{
		pEntry = ESS_GET_ENTRY(pHashEntry, FFatfsCacheEntry, stHashEntry);
	}

	pEntry->pVolInfo	= pVolInfo;				// set volume pointer
	pEntry->wTimeStamp	= pVolInfo->wTimeStamp;	// set time stamp

	_CLEAN_NODE(pEntry);						// clean node entry
	pEntry->dwFlag = FFAT_CACHE_FREE;

	// move to LRU head
	_UPDATE_LRU(pCache, pEntry);

	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&pEntry->stDListVol) == FFAT_TRUE);
	FFAT_ASSERT((pEntry->dwFlag & FFAT_CACHE_DIRTY) == 0);
	FFAT_ASSERT((pEntry->dwFlag & FFAT_CACHE_LOCK) == 0);

	*ppFreeEntry = pEntry;

	return FFAT_OK;
}


/**
 * This function initializes FFatfsCacheInfo
 *	1. initialize cache info structure
 *	2. initialize cache entries
 *	3. add to main cache list
 *
 * @param		pCache			: cache information structure
 * @param		pBuff			: buffer for cache
 * @param		dwSize			: cache size in byte
 * @param		pVolInfo		: volume information, may be NULL
 * @param		dwSectorSize	: sector size for cache operation
 *									should be power of 512
 * @param		wFlag			: flag for new cache
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREADY	: FFATFS already initialized
 * @author		DongYoung Seo
 * @version		JUL-12-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_initCache(FFatfsCacheInfo* pCache, t_int8* pBuff, 
				t_int32 dwSize, FatVolInfo* pVolInfo,
				t_int32 dwSectorSize, FFatfsCacheInfoFlag wFlag)
{
	FFatfsCacheEntry*	pEntry;
	t_int8*				pTemp;
	t_int8*				pBuffSector;
	EssDList*			pHashTable;
	t_int32				dwSectorBuffCount;
	t_int32				i;
	FFatErr				r;
	
#ifdef FFAT_STRICT_CHECK
	IF_UK ((pCache == NULL) || (pBuff == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter !!")));
		return FFAT_EINVALID;
	}
#endif

	if ((t_uint)pBuff & FFAT_MEM_ALIGN_MASK)
	{
		FFAT_LOG_PRINTF((_T("Buffer pointer is not aligned")));

		// adjust unaligned buffer pointer
		pTemp = pBuff;
		pBuff = (t_int8*)FFAT_GET_ALIGNED_ADDR(pBuff);
		dwSize -= (t_int32)(pBuff - pTemp);
	}

	pHashTable	= (EssDList*)pBuff;
	pBuff		= (t_int8*)(pHashTable + FFATFS_CACHE_HASH_BUCKET_COUNT);
	dwSize		= dwSize - (FFATFS_CACHE_HASH_BUCKET_COUNT * sizeof(EssDList));

	// assign cache
	// get count of cache
	dwSectorBuffCount = dwSize / (dwSectorSize + sizeof (FFatfsCacheEntry));

	IF_UK (dwSectorBuffCount < FFATFS_CACHE_SIZE_MIN)
	{
		FFAT_LOG_PRINTF((_T("Too small FFatfsCache size")));
		FFAT_DEBUG_PRINTF((_T("Minimum cache size is over : %d"), (FFAT_SECTOR_SIZE + sizeof (FFatfsCacheEntry) * FFATFS_CACHE_SIZE_MIN)));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}

	// init Hash LRU
	r = ESS_HASH_INIT(&pCache->stHash, pHashTable, FFATFS_CACHE_HASH_BUCKET_COUNT);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to init HASH LRU for FFastfsCacheInfo")));
		return r;
	}

	r = ESS_LRU_INIT(&pCache->stLru);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to init HASH LRU for FFastfsCacheInfo")));
		return r;
	}

	if (pVolInfo != NULL)
	{
		IF_UK (VI_SS(pVolInfo) != dwSectorSize)
		{
			FFAT_LOG_PRINTF((_T("Invalid sector size")));
			return FFAT_EINVALID;
		}
	}

	IF_UK (EssMath_IsPowerOfTwo(dwSectorSize) == ESS_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid sector size")));
		return FFAT_EINVALID;
	}

	// set hash mask
	pCache->wSectorSize		= (t_uint16)dwSectorSize;
	pCache->bSectorSizeBits	= (t_uint8)EssMath_Log2(dwSectorSize);

	pEntry = (FFatfsCacheEntry*)pBuff;
	pBuffSector = (t_int8*)(pEntry + dwSectorBuffCount);

	for (i = 0; i < dwSectorBuffCount; i++)
	{
		// check buffer range for pEntry
		FFAT_ASSERT((FFAT_GET_ADDR(pEntry) + sizeof(pEntry)) <= (FFAT_GET_ADDR(pBuff) + dwSize));
		// check buffer range for pBuffSector
		FFAT_ASSERT((FFAT_GET_ADDR(pBuffSector) + FFAT_SECTOR_SIZE) < (FFAT_GET_ADDR(pBuff) + dwSize));

		ESS_LRU_INIT_ENTRY(&pEntry->stLruEntry);
		ESS_HASH_INIT_ENTRY(&pEntry->stHashEntry);
		ESS_DLIST_INIT(&pEntry->dlDirty);
		ESS_DLIST_INIT(&pEntry->stDListVol);

		_ADD_TO_FREE(pCache, pEntry);

		pEntry->pBuff		= pBuffSector;
		pEntry->dwFlag		= FFAT_CACHE_FREE;

		pEntry++;
		pBuffSector += pCache->wSectorSize;
	}

	ESS_DLIST_INIT(_DIRTY_LIST(pCache));

	// add new cache to cache list
	ESS_DLIST_ADD_TAIL(&_stCacheMain.dlCaches, &pCache->dlCache);
	_stCacheMain.wCachecount++;

	pCache->bSig		= _CACHE_SIG;
	pCache->wRefCount	= 0;
	pCache->wFlag		= wFlag;

	return FFAT_OK;
}



/**
 * This function lookup a cache that size is dwSectorSize
 *
 * @param		wSectorSize	: sector size in byte
 * @return		void
 * @author		DongYoung Seo
 * @version		MAY-21-2007 [DongYoung Seo] First Writing.
*/
static FFatfsCacheInfo*
_lookupCache(t_int32 dwSectorSize)
{
	FFatfsCacheInfo*	pCache;
	EssDList*			pPos;

	ESS_DLIST_FOR_EACH(pPos, &_stCacheMain.dlCaches)
	{
		pCache = ESS_DLIST_GET_ENTRY(pPos, FFatfsCacheInfo, dlCache);
		if (pCache->wSectorSize == (t_uint16)dwSectorSize)
		{
			return pCache;
		}
	}
	return NULL;
}


/**
 * This function lookup a cache that size is dwSectorSize
 *
 * @param		wSectorSize	: sector size in byte
 * @return		void
 * @author		DongYoung Seo
 * @version		DEC-18-2006 [DongYoung Seo] First Writing.
*/
static FFatfsCacheInfo*
_getCache(t_int32 dwSectorSize)
{
	FFatfsCacheInfo*	pCache;

	pCache = _lookupCache(dwSectorSize);
	if (pCache)
	{
		pCache->wRefCount++;			// increase reference count
	}

	return pCache;
}


/**
* This function add a new cache (it uses dynamic memory allocation)
*
* @return		void
* @author		DongYoung Seo
* @version		DEC-18-2006 [DongYoung Seo] First Writing.
*/
static FFatErr
_autoAddCache(t_int32 dwSectorSize)
{
	// add new cache with dynamic alloc
	t_int32				i;
	FFatErr				r = FFAT_ENOMEM;
	t_int32				dwSize;

	for (i = 0; i < FFATFS_MAX_CACHE_COUNT; i++)
	{
		if (_stCacheMain.pCacheBuff[i])
		{
			continue;
		}

		// get memory for default FAT cache
		dwSize	= ffat_al_getConfig()->dwFFatfsCacheSize;		// buffer
		_stCacheMain.pCacheBuff[i] = (t_int8*)FFAT_MALLOC(dwSize, ESS_MALLOC_IO);

		r = _addCache(_stCacheMain.pCacheBuff[i], dwSize,
						dwSectorSize, FFATFS_CIFLAG_AUTO_ADD);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to add cache")));
			FFAT_FREE(_stCacheMain.pCacheBuff[i], dwSize);
			_stCacheMain.pCacheBuff[i] = NULL;
		}

		break;
	}

	return r;
}


/**
* This function add a new cache to cache list and increase cache count.
*
* @param		pBuff			: buffer for cache
* @param		dwSize			: cache size in byte
* @param		dwSectorSize	: sector size for cache operation
* @param		wFlag			: flag for cache operation
* @return		void
* @author		DongYoung Seo
* @version		JAN-14-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_addCache(t_int8* pBuff, t_int32 dwSize, t_int32 dwSectorSize, FFatfsCacheInfoFlag wFlag)
{
	FFatfsCacheInfo* pCache;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pBuff == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	dwSize	-= (t_int32) FFAT_GET_ALIGN_OFFSET(pBuff);
	pBuff	= (t_int8*)FFAT_GET_ALIGNED_ADDR(pBuff);

	if (dwSize < (t_int32)(dwSectorSize + sizeof(FFatfsCacheInfo)))
	{
		FFAT_LOG_PRINTF((_T("Not enough memory")));
		return FFAT_ENOMEM;
	}

	pCache	= (FFatfsCacheInfo*)pBuff;
	pBuff	+= sizeof(FFatfsCacheInfo);
	dwSize	-= sizeof(FFatfsCacheInfo);

	return _initCache(pCache, pBuff, dwSize, NULL, dwSectorSize, wFlag);
}


/**
* This function remove a cache from cache list
*
* @param		pCache			: cache information
* @return		void
* @author		DongYoung Seo
* @version		MAY-21-2008 [DongYoung Seo] First Writing.
* @version		AUG-08-2008 [DongYoung Seo] bug fix, clean pCacheBuff
* @version		JAN-06-2009 [DongYoung Seo] remove cache even if that is created automatically or user created
*/
static FFatErr
_removeCache(FFatfsCacheInfo* pCache)
{
	t_int32			i;

	FFAT_ASSERT(pCache);
	FFAT_ASSERT(pCache->wRefCount == 0);			// check cache busy state
	FFAT_ASSERT(pCache->bSig == _CACHE_SIG);

	ESS_DLIST_DEL(pCache->dlCache.pPrev, pCache->dlCache.pNext);
	_stCacheMain.wCachecount--;

	if (pCache->wFlag & FFATFS_CIFLAG_AUTO_ADD)
	{
		for (i = 0; i <FFATFS_MAX_CACHE_COUNT; i++)
		{
			if (_stCacheMain.pCacheBuff[i] == (t_int8*)pCache)
			{
				_stCacheMain.pCacheBuff[i] = NULL;
				break;
			}
		}
	}

	if (pCache->wFlag & FFATFS_CIFLAG_AUTO_ADD)
	{
		// Free memory
		FFAT_FREE(pCache, ffat_al_getConfig()->dwFFatfsCacheSize);
	}

	return FFAT_OK;
}


/**
 * This function sync several entry
 * all entries must be in same volume
 *
 * @param		pCache		: [IN] cache structure
 * @param		pVolInfo	: [IN] volume information
 * @param		ppEntry		: [IN] storage of cache entries
 * @param		dwFlag		: [IN] Cache flag
 *								FFAT_CACHE_DISCARD	: just discard it
 * @param		pCxt		: [IN] BTFS context of current task
 * @return		FFAT_OK		: success
 * @return		FFAT_EIO	: I/O error
 * @return		else		: refer to the ffat_errno.h
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
 * @version		JUN-02-2008 [DongYoung Seo] adapt vectored I/O
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
 */
static FFatErr
_syncEntries(FFatfsCacheInfo* pCache, FatVolInfo* pVolInfo, FFatfsCacheEntry** ppEntry,
				t_int32 dwCount, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	FFatErr			r = FFAT_OK;
	t_int32			dwStartIndex;		// current start index for ppEntry for sync
	t_int32			dwCurIndex;			// current index of ppEntry
	t_int32			i;
	t_int32			j;
	FFatVS			stVS;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pCache);
	FFAT_ASSERT(ppEntry);
	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(dwCount > 0);

	stVS.dwTotalEntryCount = _BUFF_SIZE_FOR_SYNC / sizeof(FFatVSE);
	stVS.pVSE = (FFatVSE*)FFAT_LOCAL_ALLOC(_BUFF_SIZE_FOR_SYNC, pCxt);
	FFAT_ASSERT(stVS.pVSE != NULL);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	i				= 0;

	do
	{
		stVS.dwValidEntryCount = 0;

		dwStartIndex	= i;			// Initializes start index

		for (/* None */; i < dwCount; i++)
		{
			FFAT_ASSERT(pVolInfo == ppEntry[i]->pVolInfo);

			FFAT_ASSERT(_IS_DIRTY(ppEntry[i]) == FFAT_TRUE);

			// check time stamp
			IF_UK (ppEntry[i]->wTimeStamp != pVolInfo->wTimeStamp)
			{
				continue;
			}

			// check is there any free space for new data
			if (stVS.dwValidEntryCount >= stVS.dwTotalEntryCount)
			{
				// no more free entry
				break;
			}

			if (ppEntry[i]->dwFlag & FFAT_CACHE_SYNC_CALLBACK)
			{
				// callback
				for (j = 0; j < FFATFS_CACHE_CALLBACK_COUNT; j++)
				{
					if (_stCacheMain.pfCallBack[j])
					{
						r = _stCacheMain.pfCallBack[j](pVolInfo, ppEntry[i]->dwSectorNo,
									ppEntry[i]->dwFlag, pCxt);
						if ((r < 0) && ((dwFlag & FFAT_CACHE_FORCE) == 0))
						{
							FFAT_DEBUG_PRINTF((_T("fail on callback routine")));
							goto out;
						}
					}
				}
			}

			stVS.pVSE[stVS.dwValidEntryCount].dwSector	= ppEntry[i]->dwSectorNo;
			stVS.pVSE[stVS.dwValidEntryCount].dwCount	= 1;
			stVS.pVSE[stVS.dwValidEntryCount].pBuff		= ppEntry[i]->pBuff;

			FFAT_ASSERT(ppEntry[i]->pBuff != NULL);

			// for FAT mirroring
			if ((ppEntry[i]->dwFlag & FFAT_CACHE_DATA_FAT)
					&& (VI_FLAG(pVolInfo) & VI_FLAG_FAT_MIRROR))
			{
				if ((stVS.dwValidEntryCount + (t_int16)VI_FC(pVolInfo)) >= stVS.dwTotalEntryCount)
				{
					i--;
					break;
				}

				// add another FAT sector
				for (j = 1; j < (t_int32)VI_FC(pVolInfo); j++)
				{
					stVS.dwValidEntryCount++;

					stVS.pVSE[stVS.dwValidEntryCount].dwSector	= ppEntry[i]->dwSectorNo + VI_FSC(pVolInfo) * j;
					stVS.pVSE[stVS.dwValidEntryCount].dwCount	= 1;
					stVS.pVSE[stVS.dwValidEntryCount].pBuff		= ppEntry[i]->pBuff;
				}
			}

			stVS.dwValidEntryCount++;
		}

		if (stVS.dwValidEntryCount == 0)
		{
			// no more data to sync
			FFAT_ASSERT(i == dwCount);
			break;
		}

		FFAT_ASSERT(stVS.dwValidEntryCount > 0);

		// sync
		r = ffat_al_writeSectorVS(&stVS,
					(dwFlag | FFAT_CACHE_DATA_META | FFAT_CACHE_SYNC | FFAT_CACHE_META_IO), &stCI);
		if ((r < 0) && ((dwFlag & FFAT_CACHE_FORCE) == 0))
		{
			FFAT_DEBUG_PRINTF((_T("fail to write data to block device")));
			goto out;
		}

		// post operation for sync
		for (dwCurIndex = dwStartIndex; dwCurIndex < i; dwCurIndex++)
		{
			if (dwCurIndex == dwCount)
			{
				// no more entry
				break;
			}

			// check time stamp
			IF_UK (ppEntry[dwCurIndex]->wTimeStamp != pVolInfo->wTimeStamp)
			{
				_MOVE_TO_FREE(VI_CACHE(pVolInfo), ppEntry[dwCurIndex]);
				continue;
			}

			FFAT_ASSERT(ppEntry[dwCurIndex]->wTimeStamp == pVolInfo->wTimeStamp);

			if (dwFlag & FFAT_CACHE_DISCARD)
			{
				_DEBUG_CHECK_CACHE_COUNT(ppEntry[dwCurIndex]->pVolInfo);
				_ENTRY_DISCARD(VI_CACHE(pVolInfo), ppEntry[dwCurIndex]);
				continue;
			}
			else
			{
				_CLEAN_NODE(ppEntry[dwCurIndex]);
				_CLEAN_SHARED(ppEntry[dwCurIndex]);

				// move entry to clean list
				_REMOVE_FROM_DIRTY(ppEntry[dwCurIndex]);
				_DETACH_FROM_VOLUME(ppEntry[dwCurIndex]);
			}
		}
	} while (1);

	r = FFAT_OK;

out:

	FFAT_ASSERT((dwFlag & FFAT_CACHE_FORCE) ? r == FFAT_OK : FFAT_TRUE);

	FFAT_LOCAL_FREE(stVS.pVSE, _BUFF_SIZE_FOR_SYNC, pCxt);

	return r;
}

// debug begin

//==============================================================================//
//
//	DEBUG PART
//

#ifdef FFAT_DEBUG

	// check the sector is in the ffatfs cache
	FFatfsCacheEntry*
	ffat_fs_cache_lookupSector(FatVolInfo* pVolInfo, t_uint32 dwSector)
	{
		return _lookupSector(pVolInfo, dwSector);
	}


	#ifdef _DEBUG_CACHE
		static void
		_debugCheckCacheValidity(FatVolInfo* pVolInfo, t_uint32 dwSectorNo)
		{
			EssDList*		pos;
			EssDList*				pHead;
			EssHashEntry*		pHashEntry;
			EssLruEntry*		pLruEntry;
			FFatfsCacheEntry*	pCacheEntry;
			t_int32				dwHashVal;
			_CacheCmp			stCmp;
			t_int32				dwCount = 0;

			stCmp.dwSector	= dwSectorNo;
			stCmp.pVolInfo	= pVolInfo;

			dwHashVal = _GET_HASH(dwSectorNo);
			pHead = &pVolInfo->pFatCache->stHash.pHashTable[dwHashVal];

			ESS_DLIST_FOR_EACH(pos, pHead)
			{
				pHashEntry = ESS_GET_ENTRY(pos, EssHashEntry, stDListHash);
				if (ffat_fs_cache_entryCompare(&stCmp, (EssHashEntry*)pHashEntry) == FFAT_TRUE)
				{
					dwCount++;
					FFAT_ASSERT(dwCount < 2);
				}

				pCacheEntry	= ESS_GET_ENTRY(pHashEntry, FFatfsCacheEntry, stHashEntry);
				//FFAT_ASSERT(pCacheEntry->dwSectorNo <= pVolInfo->dwSectorCount);
			}

			// check LRU
			pHead = &pVolInfo->pFatCache->stLru.stDListLru;

			dwCount = 0;

			ESS_DLIST_FOR_EACH(pos, pHead)
			{
				pLruEntry	= ESS_GET_ENTRY(pos, EssLruEntry, stDListLru);
				pCacheEntry	= ESS_GET_ENTRY(pLruEntry, FFatfsCacheEntry, stLruEntry);
				if (pCacheEntry->dwSectorNo == dwSectorNo)
				{
					if (pCacheEntry->pVolInfo->wTimeStamp == pVolInfo->wTimeStamp)
					{
						dwCount++;
						FFAT_ASSERT(dwCount < 2);
						FFAT_ASSERT(pCacheEntry->dwSectorNo < pVolInfo->dwSectorCount);
					}
				}
			}
		}
	#endif


	static void
	_statisticsPrint(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======    FFATFS CACHE STATISTICS   =======================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "EntryCompare : ",	_STATISTICS()->dwEntryCompare));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "SyncVol : ", 		_STATISTICS()->dwSyncVol));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "SyncNode : ",		_STATISTICS()->dwSyncNode));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Sync : ", 		_STATISTICS()->dwSync));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "SyncAll : ", 		_STATISTICS()->dwSyncAll));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "SyncEntry : ", 	_STATISTICS()->dwSyncEntry));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FlushVol : ",		_STATISTICS()->dwFlushVol));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "ReadSector : ", 	_STATISTICS()->dwReadSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "WriteSector : ", 	_STATISTICS()->dwWriteSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "GetSector : ", 	_STATISTICS()->dwGetSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "PutSector : ", 	_STATISTICS()->dwPutSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Discard : ", 		_STATISTICS()->dwDiscard));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Callback : ", 	_STATISTICS()->dwCallback));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Update : ", 		_STATISTICS()->dwUpdate));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "LookupSector : ",	_STATISTICS()->dwLookupSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "AddNewSector : ",	_STATISTICS()->dwAddNewSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "GetFreeEntry : ",	_STATISTICS()->dwGetFreeEntry));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Hit : ", 			_STATISTICS()->dwHit));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Miss : ", 		_STATISTICS()->dwMiss));
		if ((_STATISTICS()->dwHit + _STATISTICS()->dwMiss) > 0)
		{
			FFAT_DEBUG_PRINTF((_T(" %30s %d.0 %%\n"), "Hit Ratio",	((_STATISTICS()->dwHit * 100 ) / (_STATISTICS()->dwHit + _STATISTICS()->dwMiss))));
		}

		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
	}

#endif

//
//	END OF DEBUG PART
//
//==============================================================================//

// debug end
