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
 * @file		ffatfs_main.c
 * @brief		The file implements main module for FFatfs
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version	  	JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

//***********************************************************************/
//
// FFatfs의 전체적인 관리를 하는 module이다.
// Initialization/terminate/lock 등의 operation을 수행한다.
//
// NOTICE !!
// All FFATFS functions are not re-entrant.
//
//***********************************************************************/
//
// This module manages whole FFatfs.
// operation list : Initialization/terminate/lock/etc..
//
//***********************************************************************/

// includes
#include "ess_types.h"
#include "ess_math.h"

#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_common.h"
#include "ffat_al.h"

#include "ffatfs_config.h"
#include "ffatfs_main.h"
#include "ffatfs_cache.h"
#include "ffatfs_fat.h"
#include "ffatfs_de.h"

// debug begin
#include "ess_debug.h"
// debug end

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_MAIN)

#undef BTFS_DEBUG

//#define _MAIN_DEBUG

#define _MAIN()				((FFatfsMainInfo*)(&_stFFatfsMain))

// global(external) variables
//FFatfsMainInfo		gstFFatfsMain;
static t_int32			_stFFatfsMain[sizeof(FFatfsMainInfo) / sizeof(t_int32)];

const FatVolOperation		gstVolOp16 =	// volume operation functions for FAT16
							{
								ffat_fs_fat_getNextCluster16,		// get next cluster
								ffat_fs_fat_isEOF16,				// check EOF
								ffat_fs_fat_getFreeFromTo16,		// get free cluster
								ffat_fs_fat_updateCluster16,		// update cluster
							};

const FatVolOperation		gstVolOp32 =	// volume operation functions for FAT32
							{
								ffat_fs_fat_getNextCluster32,		// get next cluster
								ffat_fs_fat_isEOF32,				// check EOF
								ffat_fs_fat_getFreeFromTo32,		// get free cluster
								ffat_fs_fat_updateCluster32,		// update cluster
							};

// debug begin
#define		FFAT_DEBUG_MAIN_LOCK_PRINTF(_msg)

#ifdef FFAT_DEBUG
	static FFatErr	_debugFSCtl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2);
	static FFatErr	_debugFSCtlIVIC(FatVolInfo* pVolInfo);
	static FFatErr	_debugIsDirtySectorInCache(FatVolInfo* pVolInfo, t_uint32 dwSector);

	#undef FFAT_DEBUG_MAIN_LOCK_PRINTF

	#ifdef _MAIN_DEBUG
		#define		FFAT_DEBUG_MAIN_LOCK_PRINTF		FFAT_DEBUG_PRINTF("[FFATFS_MAIN_LOCK] "); FFAT_DEBUG_PRINTF
	#else
		#define		FFAT_DEBUG_MAIN_LOCK_PRINTF(_msg)
	#endif
#endif
// debug end


/**
 * This function initializes FFatfs module
 *
 * @param		bForce				: force initialization.
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREAD	: FFATFS already initialized
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_init(t_boolean bForce)
{
	ComCxt		stCxt;			// a temporary context
	t_int32		r;

	FFAT_ASSERTP(sizeof(_stFFatfsMain) == sizeof(FFatfsMainInfo), (_T("Incorrect _stFFatfsMain size")));

	if ((FFATFS_IS_INITIALIZED() == FFAT_TRUE) && (bForce == FFAT_FALSE))
	{
		FFAT_LOG_PRINTF((_T("FFatfs is already initialized")));
		return FFAT_EINIT_ALREADY;
	}

	// initialize FFatfs main
	FFAT_MEMSET(&_stFFatfsMain, 0x00, sizeof(_stFFatfsMain));
	FFAT_MEMSET(&stCxt, 0x00, sizeof(ComCxt));

	// create a lock for FFATFS
	r = FFATFS_LOCK_GET_FREE(&_MAIN()->pLock);
	IF_UK (r != FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("fail to create a lock for gstFFatfsMain")));
		return r;
	}

	// lock it
	r = FFATFS_LOCK(&stCxt);
	FFAT_ER(r, (_T("fail to lock ffat_fs")));

	// Initialize time stamp, do not move this code after cache initialization
	_MAIN()->wTimeStamp = 1;

	// initialize FFatfsCache
	r = ffat_fs_cache_init();
	FFAT_ER(r, (_T("fail to initialize FFatfsCache")));

	// initialize FFatfsFast
	r = ffat_fs_fat_init();
	FFAT_ER(r, (_T("fail to initialize FFatfsFat")));

	// initialize FFatfsDE
	r = ffat_fs_de_init();
	FFAT_ER(r, (_T("fail to initialize FFatfsFat")));

	// set initialized
	_MAIN()->dwFlag |= FFATFS_MAIN_INIT;

	// unlock FFATFS
	// lock it
	r = FFATFS_UNLOCK(&stCxt);
	FFAT_ER(r, (_T("fail to unlock FFATFS main")));

	return FFAT_OK;
}


/**
 * This function terminates FFatfs module
 *
 * @param		pCxt		: context
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_terminate(void)
{
	FFatErr		r;
	ComCxt		stCxt;			// a temporary context

	// check initialization state

	if (FFATFS_IS_INITIALIZED() == FFAT_FALSE)
	{
		return FFAT_EINIT;
	}

	FFAT_MEMSET(&stCxt, 0x00, sizeof(ComCxt));

	// LOCK ffatfs
	r = FFATFS_LOCK(&stCxt);

	// sync all crated cache
	ffat_fs_cache_syncAll(FFAT_CACHE_FORCE, &stCxt);

	// init Call Back Function
	r |= ffat_fs_cache_terminate();
	r |= ffat_fs_fat_terminate();

	// unlock
	r |= FFATFS_UNLOCK(&stCxt);

	_MAIN()->dwFlag = FFATFS_MAIN_NONE;

	if (r < 0)
	{
		if (&_MAIN()->pLock)
		{
			// delete lock
			r |= FFAT_LOCK_RELEASE(&_MAIN()->pLock);
		}
	}

	return r;
}


/**
 * This function initializes FatVolInfo 
 *
 * @param		pVolInfo		: volume information
 * @param		dwSectorSize	: sector size of volume
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_initVolInfo(FatVolInfo* pVolInfo)
{

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVolInfo == NULL) || (EssMath_IsPowerOfTwo(pVolInfo->dwSectorSize) == 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(EssMath_IsPowerOfTwo(pVolInfo->dwSectorSize));

	ESS_DLIST_INIT(&pVolInfo->dlFatCacheDirty);

	// set default FAT cache
	pVolInfo->pFatCache = ffat_fs_cache_getCache(pVolInfo->dwSectorSize);
	IF_UK (pVolInfo->pFatCache == NULL)
	{
		FFAT_LOG_PRINTF((_T("Not supported sector size")));
		FFAT_DEBUG_PRINTF((_T("There is no cache for sector size %d"), pVolInfo->dwSectorSize));
		return FFAT_ENOSUPPORT;
	}

	// init cluster cache
	VIC_INIT(VI_VIC(pVolInfo));
	return FFAT_OK;
}


/**
 * FFATFS control
 *
 * lock이 필요할 경우 각각의 command를 처리하는 부분에서 수행하도록 한다.
 *
 * @param		dwCmd		: filesystem control command
 * @param		pParam0		: parameter 0
 * @param		pParam1		: parameter 1
 * @param		pParam2		: parameter 2
 * @author		DongYoung Seo
 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_fsctl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2)
{
// debug begin
#ifdef FFAT_DEBUG
	if (dwCmd & FAT_FSCTL_DEBUG_BASE)
	{
		return _debugFSCtl(dwCmd, pParam0, pParam1, pParam2);
	}
#endif
// debug end

	return FFAT_ENOSUPPORT;
}


#if (FFATFS_LOCK_TYPE != FFATFS_LOCK_NONE)
	/**
	 * lock FFATFS
	 *
	 * @param		pCxt			: [IN] context of current operation
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EINVALID	: invalid parameter
	 * @return		FFAT_EPANIC		: fail
	 * @author		DongYoung Seo
	 * @version		DEC-17-2007 [DongYoung Seo] First Writing.
	 */
	FFatErr
	ffat_fs_getLock(ComCxt* pCxt)
	{
		FFatErr		r = FFAT_OK;

		FFAT_ASSERT(pCxt);

	#if (FFATFS_LOCK_TYPE == FFATFS_LOCK_SINGLE)
		if ((pCxt->dwFlag & COM_FLAG_FFATFS_LOCKED) == 0)
		{
			r = ffat_al_getLock(_MAIN()->pLock);
			if (r == FFAT_OK)
			{
				pCxt->dwFlag |= COM_FLAG_FFATFS_LOCKED;
				_MAIN()->wRefCount++;
			}

			FFAT_DEBUG_MAIN_LOCK_PRINTF((_T("Get, ref:%d"), _MAIN()->wRefCount));
		}
		else
		{
			_MAIN()->wRefCount++;
			FFAT_DEBUG_MAIN_LOCK_PRINTF((_T("Re-Enter, ref:%d"), _MAIN()->wRefCount));
		}
	#endif

		return r;
	}


	/**
	 * unlock FFATFS
	 *
	 * @param		pCxt			: [IN] context of current operation
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EINVALID	: invalid parameter
	 * @return		FFAT_EPANIC		: fail
	 * @author		DongYoung Seo
	 * @version		DEC-17-2007 [DongYoung Seo] First Writing.
	 */
	FFatErr
	ffat_fs_putLock(ComCxt* pCxt)
	{
		FFatErr		r = FFAT_OK;

		FFAT_ASSERT(pCxt);

	#if (FFATFS_LOCK_TYPE == FFATFS_LOCK_SINGLE)
		if (pCxt->dwFlag & COM_FLAG_FFATFS_LOCKED)
		{
			_MAIN()->wRefCount--;

			if (_MAIN()->wRefCount == 0)
			{
				FFAT_DEBUG_MAIN_LOCK_PRINTF((_T("Put, ref:%d"), _MAIN()->wRefCount));

				r = ffat_al_putLock(_MAIN()->pLock);
				if (r == FFAT_OK)
				{
					pCxt->dwFlag &= (~COM_FLAG_FFATFS_LOCKED);
				}
			}
			else
			{
				FFAT_DEBUG_MAIN_LOCK_PRINTF((_T("Out, ref:%d"), _MAIN()->wRefCount));
			}
		}
	#endif

		return r;
	}
#endif	// end of #if !(FFATFS_LOCK_TYPE == FFATFS_LOCK_NONE)

/**
 * check ffatfs initialization 
 *
 * @return		FFAT_TRUE	: initialized
 * @return		FFAT_FALSE	: not initialized
 * @author		DongYoung Seo
 * @version		DEC-18-2007 [DongYoung Seo] First Writing.
 */
t_boolean
ffat_fs_isInitialized(void)
{
	return	(_MAIN()->dwFlag & FFATFS_MAIN_INIT) ? FFAT_TRUE : FFAT_FALSE;
}


/**
 * Increase ffatfs main time stamp
 *
 * @author		DongYoung Seo
 * @version		DEC-18-2007 [DongYoung Seo] First Writing.
 */
void
ffat_fs_incTimeStamp(void)
{
	_MAIN()->wTimeStamp++;
}


/**
 * return ffatfs main time stamp
 *
 * @author		DongYoung Seo
 * @version		DEC-18-2007 [DongYoung Seo] First Writing.
 */
t_uint16
ffat_fs_getTimeStamp(void)
{
	return _MAIN()->wTimeStamp;
}


// debug begin
#ifdef FFAT_DEBUG
	// ============================================================================
	//
	//	DEBUG PART
	//

	/**
	 * FFATFS control for debug
	 *
	 * @param		dwCmd		: filesystem control command
	 * @param		pParam0		: parameter 0
	 * @param		pParam1		: parameter 1
	 * @param		pParam2		: parameter 2
	 * @author		DongYoung Seo
	 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugFSCtl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2)
	{
		if (dwCmd == FAT_FSCTL_IVIC)
		{
			return _debugFSCtlIVIC((FatVolInfo*)pParam0);
		}
		else if (dwCmd == FAT_FSCTL_IS_DIRTY_SECTOR_IN_CACHE)
		{
			return _debugIsDirtySectorInCache((FatVolInfo*)pParam0, *(t_uint32*)pParam1);
		}

		return FFAT_ENOSUPPORT;
	}


	/**
	 * invalidate volume information cache
	 *
	 * @param		pVolInfo	: volume pointer
	 * @author		DongYoung Seo
	 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugFSCtlIVIC(FatVolInfo* pVolInfo)
	{

	#ifdef FFAT_STRICT_CHECK
		IF_UK (pVolInfo == NULL)
		{
			FFAT_LOG_PRINTF((_T("Invalid parameter")));
			return FFAT_EINVALID;
		}
	#endif
		
		FFAT_ASSERT(pVolInfo);

		VIC_INIT(VI_VIC(pVolInfo));

		return FFAT_OK;
	}



	/**
	 * check a dirty sector is in the cache
	 *
	 * @param		pVolInfo	: volume pointer
	 * @param		dwSector	: sector number
	 * @author		DongYoung Seo
	 * @version		DEC-19-2007 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugIsDirtySectorInCache(FatVolInfo* pVolInfo, t_uint32 dwSector)
	{
		FFatfsCacheEntry*	pEntry;

		FFAT_ASSERT(pVolInfo);

		pEntry = ffat_fs_cache_lookupSector(pVolInfo, dwSector);
		if (pEntry)
		{
			if (pEntry->dwFlag & FFAT_CACHE_DIRTY)
			{
				return FFAT_TRUE;
			}
		}

		return FFAT_FALSE;
	}


	//
	// End of debug part
	//
	// ============================================================================
#endif
// debug end

