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
 * @file		ffat_al.c
 * @brief		Abstraction module for Lock
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */


#include "ess_types.h"
#include "ess_debug.h"
#include "ess_pstack.h"
#include "ess_base_config.h"

#include "ns_nativefs.h"

// FFAT headers
#include "ffat_config.h"
#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_al.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_VFSAL_NESTLE)

//#define _DEBUG_LOCK

#define  FFAT_SFN_NAME_LEN		8

//=============================================================================
//
//	LOCK
//

#define _LOCK_COUNT		FFAT_LOCK_COUNT

// _s : size
// _t : type (purpose)
#define _MALLOC(_s, _t)				NS_AllocateMemory(_s)
// _p : pointer
// _s : size
#define _FREE(_p, _s)				do { if (_p) NS_FreeMemory(_p); } while (0)

// dynamically allocated lock - this will be destroyed when it is released
#define _DYNAMIC_LOCK				((EssList*)0xFFFFFFFF)

// lock type for FFAT
typedef struct
{
	NS_SEMAPHORE	stLock;			// lock type
	EssList			stList;			// list for free lock management
									// dynamically created lock gets _DYNAMIC_LOCK
									// on pNext member variable
// debug begin
#ifdef FFAT_DEBUG
	t_boolean		bLocked;
	t_uint32		dwLockCount;
	t_uint32		dwUnlockCount;
#endif
// debug end
} t_lock;

typedef struct
{
	t_lock*			pLocks;						// pointer for lock array
	EssList			stListFreeLocks;			// list head for free locks
} t_lock_main;

// lock type casting
#define	_LOCK(_pLock)		((t_lock*)(_pLock))

// lock main
#define _LOCK_MAIN()		((t_lock_main*)&_stLockMain)
static	t_int32				_stLockMain[sizeof(t_lock_main) / sizeof(t_int32)];

//=============================================================================

//static FFatConfig		_stConfig;		// common information
#define _CONFIG()		((FFatConfig*)_pConfig)
static t_int32*			_pConfig;


// static functions
static void		_addLock(t_lock* pLock);
static FFatErr	_dynamicLockCreation(t_lock** ppLock);
static FFatErr	_dynamicLockDestroy(t_lock* pLock);
static FFatErr	_initConfigLog(void);
static FFatErr	_initConfigHPA(void);


#define _STATISTIC_LOCK_INIT
#define _STATISTIC_LOCK_OCCUPY
#define _STATISTIC_LOCK_RELEASE
#define _STATISTIC_LOCK_CREATION
#define _STATISTIC_LOCK_DELETION
#define _STATISTIC_LOCK_PRINT


// debug begin
#ifdef _DEBUG_LOCK
	#define FFAT_DEBUG_LOCK_PRINTF	FFAT_DEBUG_PRINTF("[LOCK (%d)] ", FFAT_DEBUG_GET_TASK_ID()); FFAT_DEBUG_PRINTF
#else
	#define FFAT_DEBUG_LOCK_PRINTF(_msg)
#endif

#ifdef FFAT_DEBUG
	#undef _STATISTIC_LOCK_INIT
	#undef _STATISTIC_LOCK_OCCUPY
	#undef _STATISTIC_LOCK_RELEASE
	#undef _STATISTIC_LOCK_CREATION
	#undef _STATISTIC_LOCK_DELETION
	#undef _STATISTIC_LOCK_PRINT

	#define _STATISTIC_LOCK_INIT			FFAT_MEMSET(_stStatisticsLock, 0x00, sizeof(_StatisticLock));
	#define _STATISTIC_LOCK_OCCUPY			_STATISTICS_LOCK()->dwLockOccupyCount++;
	#define _STATISTIC_LOCK_RELEASE			_STATISTICS_LOCK()->dwLockReleaseCount++;
	#define _STATISTIC_LOCK_CREATION		_STATISTICS_LOCK()->dwLockCreation++;
	#define _STATISTIC_LOCK_DELETION		_STATISTICS_LOCK()->dwLockDeletion++;
	#define _STATISTIC_LOCK_PRINT			_printLockStatistics();

	typedef struct
	{
		t_uint32 dwLockOccupyCount;		// get a free lock count
		t_uint32 dwLockReleaseCount;	// release lock count
		t_uint32 dwLockCreation;		// lock creation count
		t_uint32 dwLockDeletion;		// lock deletion count
	} _StatisticLock;

	#define _STATISTICS_LOCK()		((_StatisticLock*)&_stStatisticsLock)

	//static _MainDebug	_stMainDebug;
	static t_int32 _stStatisticsLock[sizeof(_StatisticLock) / sizeof(t_int32)];

	static void			_printLockStatistics(void);
	static FFatErr		_dbg_lockInit(FFatLock* pLock);
#endif

// debug end


/**
* This function initializes Abstraction Layer
*
* @return		FFAT_OK			: success
* @return		FFAT_EPANIC		: fail to initialize
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_init(void)
{
	FFatErr		r;

	// initialize common information
	r = ffat_al_initConfig();
	IF_UK (r < 0)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to get config")));
		return r;
	}

	r = ffat_al_initMem();
	IF_UK (r < 0)
	{
		FFAT_DEBUG_PRINTF((_T("fail to initialize memory module")));
		return r;
	}

	r = ffat_al_initLock();
	IF_UK (r < 0)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to create locks for FFAT")));
		return r;
	}

	r = ffat_al_initBlockIO();
	IF_UK (r < 0)
	{
		FFAT_DEBUG_PRINTF((_T("fail to init BCM module")));
		return r;
	}

	return FFAT_OK;
}


/**
* This function terminates Abstraction Layer
*
* @return		FFAT_OK			: success
* @return		FFAT_EPANIC		: fail to terminate
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_terminate(void)
{
	FFatErr		r = FFAT_OK;

	r |= ffat_al_termianteBlockIO();

	r |= ffat_al_terminateConfig();

	r |= ffat_al_terminateLock();

	r |= ffat_al_terminateMem();

	return r;
}


/**
 * This function initializes lock module
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EPANIC		: fail to create a lock
 * @author		DongYoung Seo
 * @version		JAN-09-2007 [DongYoung Seo] First Writing.
 */
FFatErr 
ffat_al_initLock(void)
{
	t_int32		i, j;
	FFatErr		r = FFAT_OK;

	_STATISTIC_LOCK_INIT

	_LOCK_MAIN()->pLocks = (t_lock*)_MALLOC((_LOCK_COUNT * sizeof(t_lock)), ESS_MALLOC_NONE);
	IF_UK (_LOCK_MAIN()->pLocks  == NULL)
	{
		FFAT_LOG_PRINTF((_T("Not enough memory for lock")));
		return FFAT_ENOMEM;
	}

	for (i = 0; i < _LOCK_COUNT; i++)
	{
		IF_UK (!NS_CreateSem(&(_LOCK_MAIN()->pLocks[i].stLock), FFAT_LOCK_INIT_COUNT))
		{
			r = FFAT_EPANIC;
			goto error;
		}

		_STATISTIC_LOCK_CREATION
		FFAT_DEBUG_LOCK_PRINTF((_T("Created(0x%X) \n"), _LOCK_MAIN()->pLocks[i].stLock));
	}

	// init list head
	ESS_LIST_INIT(&_LOCK_MAIN()->stListFreeLocks);

	// add all lock to free list
	for (i = 0; i < _LOCK_COUNT; i++)
	{
		_addLock(&(_LOCK_MAIN()->pLocks[i]));

// debug begin
#ifdef FFAT_DEBUG
		_dbg_lockInit(&(_LOCK_MAIN()->pLocks[i]));
#endif
// debug end
	}

	return FFAT_OK;

error:
	j = i;
	for (i = 0; i < j; i++)
	{
		NS_DestroySem(&(_LOCK_MAIN()->pLocks[i].stLock));
	}

	return r;
}


/**
 * This function terminates lock module
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EPANIC		: fail to create a lock
 * @author		DongYoung Seo
 * @version		JAN-10-2007 [DongYoung Seo] First Writing.
 */
FFatErr 
ffat_al_terminateLock(void)
{
	t_int32		i;

	for (i = 0; i < _LOCK_COUNT; i++)
	{
		//r = NS_DestroySem(&_stLockMain.stLocks[i].stLock);
		// why nestle does not return ERROR ??
		NS_DestroySem(&(_LOCK_MAIN()->pLocks[i].stLock));

		_STATISTIC_LOCK_DELETION
	}

	ESS_LIST_INIT(&(_LOCK_MAIN()->stListFreeLocks));

	_FREE(_LOCK_MAIN()->pLocks, (_LOCK_COUNT * sizeof(t_lock)));

	_STATISTIC_LOCK_PRINT

	return FFAT_OK;
}


/**
 * This function gets a free lock
 *
 * CAUTION : this function is not safe on multi-thread environment
 *
 * @param		ppLock			: pointer of lock storage
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EPANIC		: fail to create a lock
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_al_getFreeLock(FFatLock** ppLock)
{
	EssList*		pListFree;
	t_lock*			pFreeLock;

	FFAT_ASSERT(ppLock);

	if (*ppLock != NULL)
	{
		// it already have a lock
		return FFAT_OK;
	}

	_STATISTIC_LOCK_OCCUPY

	if (ESS_LIST_IS_EMPTY(&(_LOCK_MAIN()->stListFreeLocks)) == ESS_TRUE)
	{
		//FFAT_LOG_PRINTF((_T("no more free lock - create new dynamic lock")));
		return _dynamicLockCreation((t_lock**)ppLock);
	}

	pListFree = ESS_LIST_GET_HEAD(&(_LOCK_MAIN()->stListFreeLocks));
	pFreeLock = ESS_GET_ENTRY(pListFree, t_lock, stList);
	*ppLock = (FFatLock*) pFreeLock;

	FFAT_ASSERT(_dbg_lockInit(*ppLock) == FFAT_OK);

	// remove from free list
	ESS_LIST_REMOVE_HEAD(&(_LOCK_MAIN()->stListFreeLocks));

	FFAT_DEBUG_LOCK_PRINTF((_T("A lock is occupied(0x%X)\n"), *ppLock));

	return FFAT_OK;
}


/**
 * release a lock and add it to free list
 *
 * CAUTION : this function is not safe on multi-thread environment
 *
 * @param		ppLock	: lock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		JUL-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_al_releaseLock(FFatLock** ppLock)
{
	t_lock*		pFreeLock;

	_STATISTIC_LOCK_RELEASE

	pFreeLock = ESS_GET_ENTRY(*ppLock, t_lock, stLock);

	FFAT_ASSERT(pFreeLock->bLocked == FFAT_FALSE);

	if (pFreeLock->stList.pNext == _DYNAMIC_LOCK)
	{
		//FFAT_LOG_PRINTF((_T("destroy a dynamic lock")));
		return _dynamicLockDestroy(pFreeLock);
	}

	ESS_LIST_ADD_HEAD(&(_LOCK_MAIN()->stListFreeLocks), &pFreeLock->stList);

	FFAT_DEBUG_LOCK_PRINTF((_T("A lock is released(0x%X)\n"), *ppLock));

	*ppLock = NULL;

	return FFAT_OK;
}


/**
 * This function gets a lock
 *
 * @param		pLock			: pointer of t_lock structure
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EPANIC		: fail
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr 
ffat_al_getLock(FFatLock* pLock)
{
	FFAT_ASSERT(pLock);

	// Nestle does not return error
	NS_LockSem(&_LOCK(pLock)->stLock);

// debug begin
#ifdef FFAT_DEBUG
	_LOCK(pLock)->bLocked = FFAT_TRUE;
	FFAT_DEBUG_LOCK_PRINTF((_T("get lock(0x%X) \n"), pLock));
#endif	
// debug end

	return FFAT_OK;
}


/**
 * This function releases a lock
 *
 * @param		pLock		: pointer of t_lock structure
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EPANIC		: fail
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr 
ffat_al_putLock(FFatLock* pLock)
{
	// nestle does not return error
	NS_UnlockSem(&_LOCK(pLock)->stLock);

// debug begin
#ifdef FFAT_DEBUG
	_LOCK(pLock)->bLocked = FFAT_FALSE;
	FFAT_DEBUG_LOCK_PRINTF((_T("put lock(0x%X)\n"), pLock));
#endif
// debug end

	return FFAT_OK;
}


//===================================================================
//
//	Memory functions
//

/**
 * This function initializes FFAT memory
 *
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_al_initMem(void)
{
	return FFAT_OK;
}


/**
 * This function terminates FFAT memory
 *
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_al_terminateMem(void)
{
	return FFAT_OK;
}


/**
 * This function allocates memory
 *
 * @param		dwSize			: allocation byte
 * @dwFlag		dwMallocFlag	: memory allocation flag
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
void*
ffat_al_allocMem(t_int32 dwSize, EssMallocFlag dwMallocFlag)
{
	// check!!, nestle does not care memory allocation type

	return NS_AllocateMemory((unsigned int)dwSize);
}


/**
 * This function allocates memory
 *
 * @param		p		: memory pointer
 * @param		dwSize	: size of p
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
void
ffat_al_freeMem(void* p, t_int32 dwSize)
{
	// do nothing on windows system

	NS_FreeMemory(p);
}


//
//	End of memory functions
//
//===================================================================

//===================================================================
//
//	Time functions
//


/**
 * get local time
 *
 * @param		pTime		: pointer to the storage location for local time
 * @return		FFAT_OK		: success
 *				else		: error
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-25-2006 [DongYoung Seo] First writing
 * @see			None
 */
FFatErr
ffat_localTime(FFatTime* pTime)
{
	NS_SYS_TIME	stTime;

	FFAT_ASSERT(pTime);

	NS_GetCurSysTime(&stTime);
	// to protect entering random value(stTime.wMilliseconds)
	if(stTime.wMilliseconds<=1000)
	{
		pTime->tm_msec	= stTime.wMilliseconds;
	}
	else
	{
		pTime->tm_msec	= 0;
	}

	pTime->tm_sec	= stTime.wSecond;
	pTime->tm_min	= stTime.wMinute;
	pTime->tm_hour	= stTime.wHour;
	pTime->tm_mday	= stTime.wDay;
	pTime->tm_mon	= stTime.wMonth - 1;	/*!< month range of FFatTime is [0,11] */
	pTime->tm_year	= stTime.wYear - 1900;	/*!< year of FFatTime start from 1900 */
	pTime->tm_wday	= stTime.wDayOfWeek;
	pTime->tm_yday	= 0;
	pTime->tm_isdst	= 0;

	return FFAT_OK;
}


//
//	End of Time functions
//
//===================================================================


//===================================================================
//
//	TTY functions
//


/**
 * This function gets a character from STDIN
 *
 * @return		value of a character
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_getChar(void)
{
	// nestle does not support this
#if defined(NS_CONFIG_RTOS)
	return OamGetChar();
#else
	return 0;
#endif
}


//
//	End of TTY functions
//
//===================================================================


//===================================================================
//
//	MISC functions
//

/**
* This function initializes common information
*
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_initConfig(void)
{
	t_int32		r;

	_pConfig= _MALLOC(sizeof(FFatConfig), ESS_MALLOC_IO);
	IF_UK (_CONFIG() == NULL)
	{
		FFAT_LOG_PRINTF((_T("Fail to allocate memory for HPA")));
		return FFAT_ENOMEM;
	}

	// FFAT
	_CONFIG()->dwSectorSize			= FFAT_SECTOR_SIZE;

	// FFATFS
	_CONFIG()->dwFFatfsCacheSize	= FFATFS_CACHE_SIZE_IN_BYTE;

	// LOG
	r = _initConfigLog();
	IF_UK (r != FFAT_OK)
	{
		return r;
	}

	r = _initConfigHPA();
	IF_UK (r != FFAT_OK)
	{
		return r;
	}

	return FFAT_OK;
}


/**
* This function initializes common information
*
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_terminateConfig(void)
{
	_FREE(_pConfig, sizeof(FFatConfig));
	return FFAT_OK;
}


/**
* This function return common information
*
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
*/
FFatConfig*
ffat_al_getConfig(void)
{
	return _CONFIG();
}


//
//	End of TTY functions
//
//===================================================================


//===================================================================
//
//	Internal Static functions
//

/**
 * add a new lock to lock list
 *
 * @param		pLock	: lock structure pointer
 * @author		DongYoung Seo
 * @version		JUL-28-2006 [DongYoung Seo] First Writing.
 */
static void
_addLock(t_lock* pLock)
{
	ESS_LIST_ADD_HEAD(&(_LOCK_MAIN()->stListFreeLocks), &pLock->stList);
	FFAT_DEBUG_LOCK_PRINTF((_T("A new lock is created(0x%X), total count:%d \n"), pLock));
}


/**
* run time lock creation for dynamic memory allocation configuration
*
* @param		ppLock	: lock storage pointer
* @author		DongYoung Seo
* @version		DEc-30-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_dynamicLockCreation(t_lock** ppLock)
{
	FFAT_ASSERT(ppLock);

#ifdef FFAT_DYNAMIC_ALLOC
	*ppLock = (t_lock*)_MALLOC(sizeof(t_lock), ESS_MALLOC_NONE);
	IF_UK (*ppLock == NULL)
	{
		FFAT_LOG_PRINTF((_T("Not enough memory")));
		return FFAT_ENOMEM;
	}

	IF_UK (!NS_CreateSem(&((*ppLock)->stLock), FFAT_LOCK_INIT_COUNT))
	{
		FFAT_LOG_PRINTF((_T("fail to create lock")));
		_FREE(*ppLock, sizeof(t_lock));
		return FFAT_ENOMEM;
	}

	(*ppLock)->stList.pNext = _DYNAMIC_LOCK;

// debug begin
#ifdef FFAT_DEBUG
	_dbg_lockInit(*ppLock);
#endif
// debug end

	_STATISTIC_LOCK_CREATION

	return FFAT_OK;
#else
	return FFAT_ENOMEM;
#endif
}


/**
* run time lock deletion for dynamic memory allocation configuration
*
* @param		pLock	: lock storage pointer
* @author		DongYoung Seo
* @version		DEc-30-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_dynamicLockDestroy(t_lock* pLock)
{
	FFAT_ASSERT(pLock);
	FFAT_ASSERT(pLock->stList.pNext == _DYNAMIC_LOCK);

	NS_DestroySem(&pLock->stLock);

	_FREE(pLock, sozeof(t_lock));

	_STATISTIC_LOCK_DELETION

	return FFAT_OK;
}


/**
* init configuration for LOG
*
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_initConfigLog(void)
{
	char		psName[FFAT_LOG_FILE_NAME_MAX_LENGTH + 1];
	t_int32		i;

	// do not delete below. it checks user configuration 
	IF_UK (FFAT_STRLEN(FFAT_LOG_FILE_NAME) >= FFAT_LOG_FILE_NAME_MAX_LENGTH)
	{
		FFAT_LOG_PRINTF((_T("too log log file name")));
		return FFAT_EINVALID;
	}

	FFAT_STRCPY(psName, FFAT_LOG_FILE_NAME);

	i = 0;

	while(i < FFAT_LOG_FILE_NAME_MAX_LENGTH)
	{
		_CONFIG()->stLog.psFileName[i] = (t_wchar)psName[i];

		if (psName[i] == '\0')
		{
			break;
		}

		i++;
	}

	IF_UK (i == 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid configuration - invalid log file name")));
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/**
* init configuration for Hidden Protected Area
*
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_initConfigHPA(void)
{
	char		psName[FFAT_HPA_ROOT_NAME_MAX_LENGTH + 1];
	t_int32		i;

	// do not delete below. it checks user configuration 
	IF_UK (FFAT_STRLEN(FFAT_HPA_ROOT_NAME) >= FFAT_HPA_ROOT_NAME_MAX_LENGTH)
	{
		FFAT_LOG_PRINTF((_T("Fail to init config for HPA")));
		return FFAT_EINVALID;
	}

	FFAT_STRCPY(psName, FFAT_HPA_ROOT_NAME);

#ifndef FFAT_VFAT_SUPPORT
	psName[FFAT_SFN_NAME_LEN] = '\0';
#endif

	i = 0;

	while(i < FFAT_HPA_ROOT_NAME_MAX_LENGTH)
	{
		_CONFIG()->stHPA.psHPARootName[i] = (t_wchar)psName[i];

		if (psName[i] == '\0')
		{
			break;
		}

		i++;
	}

	IF_UK (i == 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid configuration - invalid HPA root directory name")));
		return FFAT_EINVALID;
	}

	// set HPA volume count
	_CONFIG()->stHPA.wHPAVolCount	= FFAT_HPA_MAX_VOLUME_COUNT;

	// set HPA bitmap size
	_CONFIG()->stHPA.wHPABitmapSize	= FFAT_HPA_BITMAP_SIZE;
	return FFAT_OK;
}

/**
* Notify the cluster information to application when Cluster changer is occured
*
* @param		dwFreeCount		: Free cluster count
* @param		dwTotalCount	: Total cluster count
* @param		pDevice			: VCB
* @return		void
* @author		Kyungsik Song
* @version		
*/
//20100413_sks => Change to add the cluster notification function
void
ffat_al_cluster_notify(t_int32 dwFreeCount, t_int32 dwTotalCount, t_int32 dwClustersize, void* pDevice)
{
  // do nothing on windows system
  NS_NotifyFreeBlockCount(pDevice, dwTotalCount, dwFreeCount, dwClustersize);

}


// debug begin
#ifdef FFAT_DEBUG
	//=============================================================================
	//
	//	FUNCTIONS FOR DEBUG
	//

	/**
	 * lock을 수행하는것에 대한 bug를 체크한다.
	 *
	 * lock count는 반드시 unlock count와 같아야한다.
	 *
	 * @param		pLock			: [IN] lock pointer
	 * @return		FFAT_OK			: success
	 * @author		DongYoung Seo
	 * @version		AUG-25-2006 [DongYoung Seo] First Writing.
	 */
	FFatErr
	ffat_dbg_lock(FFatLock* pLock)
	{
		if (pLock)
		{
			FFAT_ASSERT(pLock);

			_LOCK(pLock)->dwLockCount++;
			ESS_ASSERT(_LOCK(pLock)->dwLockCount == (_LOCK(pLock)->dwUnlockCount + 1));
		}

		return FFAT_OK;
	}


	/**
	 * Unlock을 수행하는것에 대한 bug를 체크한다.
	 *
	 * lock count는 반드시 unlock count와 같아야한다.
	 *
	 * @param		pLock			: [IN] lock pointer
	 * @return		FFAT_OK			: success
	 * @author		DongYoung Seo
	 * @version		AUG-25-2006 [DongYoung Seo] First Writing.
	 */
	FFatErr
	ffat_dbg_unlock(FFatLock* pLock)
	{
		if (pLock)
		{
			FFAT_ASSERT(pLock);

			_LOCK(pLock)->dwUnlockCount++;
			ESS_ASSERT(_LOCK(pLock)->dwLockCount == _LOCK(pLock)->dwUnlockCount);
		}

		return FFAT_OK;
	}


	/**
	 * lock 구조체의 debugging용 자료를 초기화 한다.
	 *
	 * @param		pLock			: [IN] lock pointer
	 * @return		FFAT_OK			: success
	 * @author		DongYoung Seo
	 * @version		AUG-25-2006 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_dbg_lockInit(FFatLock* pLock)
	{
		FFAT_ASSERT(pLock);

		_LOCK(pLock)->bLocked		= FFAT_FALSE;
		_LOCK(pLock)->dwLockCount	= 0;
		_LOCK(pLock)->dwUnlockCount	= 0;
		
		return FFAT_OK;
	}

	static void
	_printLockStatistics(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======    OSAL LOCK    STATISTICS   =======================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "LockOccupyCount : ",	_STATISTICS_LOCK()->dwLockOccupyCount));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "LockReleaseCount : ", _STATISTICS_LOCK()->dwLockReleaseCount));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "LockCreation : ",		_STATISTICS_LOCK()->dwLockCreation));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "LockDeletion : ", 	_STATISTICS_LOCK()->dwLockDeletion));
	}
#endif

//
//	END OF DEBUG FUNCTIONS
//
//=============================================================================
// debug end
