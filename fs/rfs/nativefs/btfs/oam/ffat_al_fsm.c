/*
 * TFS4 2.0 FFAT(Final FAT) filesystem Developed by Flash Planning Group.
 *
 * Copyright 2006-2007 by Memory Division, Samsung Electronics, Inc.,
 * San #16, Banwol-Ri, Taean-Eup, Hwasung-City, Gyeonggi-Do, Korea
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
#include "ess_osal.h"
#include "ess_pstack.h"
#include "ess_math.h"

// FFAT headers
#include "ffat_config.h"
#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_al.h"

//#define _DEBUG_LOCK
#define  FFAT_SFN_NAME_LEN		8

//=============================================================================
//
//	LOCK
//

#define _LOCK_COUNT			FFAT_LOCK_COUNT

#ifdef FFAT_DYNAMIC_ALLOC
	//#define _LOCKS_PER_STORAGE	(32)				// locks per a lock storage
	#define _LOCKS_PER_STORAGE		(8)					// locks per a lock storage
#else
	#define _LOCKS_PER_STORAGE		_LOCK_COUNT			// locks per a lock storage
#endif


// lock type for FFAT
typedef struct
{
	EssLock			stLock;			// lock type
	EssList			stList;			// list for free lock management

// debug begin
	t_uint32		dwLockCount;
	t_uint32		dwUnlockCount;
	t_boolean		bLocked;
// debug end
} t_lock;

// Lock Storage
typedef struct
{
	EssList			slList;							//!< list for allocated list
	t_lock			stLocks[_LOCKS_PER_STORAGE];	//!< locks
} _LockStorage;

typedef struct
{
	EssList			slLockStorage;			// list for allocated lock storage
	EssList			slFreeLocks;			// list head for free locks

// debug begin
	t_uint32		dwLockOccupyCount;		// lock occupy count
	t_uint32		dwLockReleaseCount;		// lock release count
// debug end
} t_lock_main;

// lock type casting
#define	_LOCK(_pLock)		((t_lock*)(_pLock))

// lock main
//static t_lock_main		_stLockMain;
#define _LOCK_MAIN()		((t_lock_main*)&_stLockMain)
static	t_int32				_stLockMain[sizeof(t_lock_main) / sizeof(t_int32)];


static FFatErr		_initLockStorageStatic(void);
static FFatErr		_terminateLockStorage(void);
static FFatErr		_allocLockStorageDynamic(void);

#ifdef FFAT_DYNAMIC_ALLOC

	static FFatErr		_initLockStorageDynamic(void);

	#define _INIT_LOCK_STORAGE			_initLockStorageDynamic

#else

	#define _INIT_LOCK_STORAGE			_initLockStorageStatic

#endif

//
//	END OF LOCK
//
//==============================================================================


//=============================================================================

//static FFatConfig		_stConfig;		// common information
#define _CONFIG()		((FFatConfig*)_pConfig)
static t_int32*			_pConfig;

// static functions
static void		_addLock(t_lock* pLock);
static FFatErr	_initConfigLog(void);
static FFatErr	_initConfigHPA(void);






// debug begin
#ifdef _DEBUG_LOCK
	#define FFAT_DEBUG_LOCK_PRINTF		FFAT_DEBUG_PRINTF("[OSAL_LOCK] "); FFAT_DEBUG_PRINTF
#else
	#define FFAT_DEBUG_LOCK_PRINTF(...)
#endif

static FFatErr	_dbg_lockInit(FFatLock* pLock);
// debug end

// debug begin
// memory debug part
#if 0
	#define FFAT_DEBUG_MEM_PRINTF		FFAT_DEBUG_PRINTF("[MEM] "); FFAT_DEBUG_PRINTF
#else
	#define FFAT_DEBUG_MEM_PRINTF(...)
#endif

extern FFatErr	ffat_main_fsctl(FFatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2);

#ifdef FFAT_DEBUG
	#define _GET_MEM_STATUS()		((FFatMemStatus*)&_stMemStatus)
	//static FFatMemStatus	_stMemStatus;
	static t_int32	_stMemStatus[sizeof(FFatMemStatus) / sizeof(t_int32)];
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
	if (r < 0)
	{
		FFAT_DEBUG_PRINTF("Fail to get config");
		return r;
	}

	r = ffat_al_initMem();
	if (r < 0)
	{
		FFAT_DEBUG_PRINTF("fail to initialize memory module");
		return r;
	}

	r = ffat_al_initLock();
	if (r < 0)
	{
		FFAT_DEBUG_PRINTF("Fail to create locks for FFAT");
		return r;
	}

	r = ffat_al_initBlockIO();
	if (r < 0)
	{
		FFAT_DEBUG_PRINTF("fail to init BCM module");
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
	FFatErr		r;

	r = _INIT_LOCK_STORAGE();
	if (r != FFAT_OK)
	{
		FFAT_DEBUG_PRINTF("fail to init lock storage");
		goto out;
	}

	return FFAT_OK;

out:
	ffat_al_terminateLock();

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
	FFatErr		r;

	r = _terminateLockStorage();

	return FFAT_OK;
}


/**
 * This function gets a free lock
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

	if (ESS_LIST_IS_EMPTY(&_LOCK_MAIN()->slFreeLocks) == ESS_TRUE)
	{
		FFAT_LOG_PRINTF("no more free lock");
		return FFAT_ENOMEM;
	}

	pListFree = ESS_LIST_GET_HEAD(&_LOCK_MAIN()->slFreeLocks);
	pFreeLock = ESS_GET_ENTRY(pListFree, t_lock, stList);
	*ppLock = (FFatLock*) pFreeLock;

	FFAT_ASSERT(_dbg_lockInit(*ppLock) == FFAT_OK);

	// remove from free list
	ESS_LIST_REMOVE_HEAD(&_LOCK_MAIN()->slFreeLocks);

	FFAT_DEBUG_LOCK_PRINTF("A lock is occupied(0x%X), occupy/release:%d/%d\n", *ppLock, ++_LOCK_MAIN()->dwLockOccupyCount, _LOCK_MAIN()->dwLockReleaseCount);

	return FFAT_OK;
}


/**
 * release a lock and add it to free list
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

	pFreeLock = ESS_GET_ENTRY(*ppLock, t_lock, stLock);

	FFAT_ASSERT(pFreeLock->bLocked == FFAT_FALSE);

	ESS_LIST_ADD_HEAD(&_LOCK_MAIN()->slFreeLocks, &pFreeLock->stList);

// debug begin
	FFAT_DEBUG_LOCK_PRINTF("A lock is released(0x%X), occupy/release:%d/%d(Diff:%d)\n",
					*ppLock, _LOCK_MAIN()->dwLockOccupyCount,
					++_LOCK_MAIN()->dwLockReleaseCount,
					(_LOCK_MAIN()->dwLockOccupyCount - _LOCK_MAIN()->dwLockReleaseCount));

// debug end

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
	t_uint32	r;

	FFAT_ASSERT(pLock);

//	FFAT_DEBUG_LOCK_PRINTF("lock(0x%X) request lock\n", pLock);

	r = EssOsal_GetLock(&_LOCK(pLock)->stLock);
	if (r != FFAT_OK)
	{
		FFAT_LOG_PRINTF("fail to get lock \n");
		return FFAT_EPANIC;
	}

// debug begin
	_LOCK(pLock)->bLocked = FFAT_TRUE;
//	FFAT_DEBUG_LOCK_PRINTF("get lock(0x%X) \n", pLock);
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
	t_int32		r;

	r = EssOsal_PutLock(&_LOCK(pLock)->stLock);
	if (r != FFAT_OK)
	{
		FFAT_LOG_PRINTF("fail to put a lock");
		return FFAT_EPANIC;
	}

// debug begin
	_LOCK(pLock)->bLocked = FFAT_FALSE;
//	FFAT_DEBUG_LOCK_PRINTF("put lock(0x%X) \n", pLock);
// debug end

	return FFAT_OK;
}



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
	ESS_LIST_INIT(&pLock->stList);
	ESS_LIST_ADD_HEAD(&_LOCK_MAIN()->slFreeLocks, &pLock->stList);
	FFAT_DEBUG_LOCK_PRINTF("A new lock is added(0x%X), total count:%d \n", pLock);
}


/**
* initializes lock storage for dynamic memory allocation
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_initLockStorageDynamic(void)
{
	ESS_LIST_INIT(&_LOCK_MAIN()->slLockStorage);
	ESS_LIST_INIT(&_LOCK_MAIN()->slFreeLocks);

	return FFAT_OK;
}


/**
* initializes lock storage for static memory allocation
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_initLockStorageStatic(void)
{
	FFatErr					r;
	t_int32					dwCount;

	dwCount = ESS_MATH_CD(_LOCK_COUNT, _LOCKS_PER_STORAGE);
	FFAT_ASSERT(dwCount == 1);

	ESS_LIST_INIT(&_LOCK_MAIN()->slLockStorage);
	ESS_LIST_INIT(&_LOCK_MAIN()->slFreeLocks);

	while (dwCount--)
	{
		r = _allocLockStorageDynamic();
		if (r != FFAT_OK)
		{
			FFAT_LOG_PRINTF("fail to allocate memory for Lock Storage");
			goto out;
		}
	}

	r = FFAT_OK;

out:
	IF_UK (r != FFAT_OK)
	{
		_terminateLockStorage();
	}

	return r;
}


/**
* terminates lock storage
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_terminateLockStorage(void)
{
	EssList*		pList;
	_LockStorage*	pLS;			// node head storage
	t_int32			i;

	while (ESS_LIST_IS_EMPTY(&_LOCK_MAIN()->slLockStorage) == FFAT_FALSE)
	{
		pList = ESS_LIST_GET_HEAD(&_LOCK_MAIN()->slLockStorage);
		FFAT_ASSERT(pList);

		ESS_LIST_REMOVE_HEAD(&_LOCK_MAIN()->slLockStorage);

		pLS = ESS_GET_ENTRY(pList, _LockStorage, slList);

		for (i = 0; i < _LOCKS_PER_STORAGE; i++)
		{
			EssOsal_DeleteLock(&pLS->stLocks[i].stLock);
		}

		FFAT_FREE(pLS, sizeof(_LockStorage));
	}

	ESS_LIST_INIT(&_LOCK_MAIN()->slFreeLocks);

	return FFAT_OK;
}


/**
* allocate new node head storage for dynamic memory allocation
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_allocLockStorageDynamic(void)
{
	_LockStorage*	pLS;			// node head storage
	t_int32			i = 0;
	t_int32			j;
	FFatErr			r;

	pLS = (_LockStorage*)FFAT_MALLOC(sizeof(_LockStorage), ESS_MALLOC_NONE);
	IF_UK (pLS == NULL)
	{
		r = FFAT_ENOMEM;
		goto out;
	}

	ESS_LIST_INIT(&pLS->slList);
	ESS_LIST_ADD_HEAD(&_LOCK_MAIN()->slLockStorage, &pLS->slList);

	for (i = 0; i < _LOCKS_PER_STORAGE; i++)
	{
		r = EssOsal_CreateLock(&pLS->stLocks[i].stLock, FFAT_LOCK_INIT_COUNT);
		if (r != FFAT_OK)
		{
			FFAT_LOG_PRINTF("Fail to allocate locks");
			goto out;
		}

		FFAT_DEBUG_LOCK_PRINTF("Created(0x%X) \n", &pLS->stLocks[i].stLock);
	}

	// QUIZ: why did not wrote this routin in the upper loop?
	for (i = 0; i < _LOCKS_PER_STORAGE; i++)
	{
		_addLock(&pLS->stLocks[i]);

// debug begin
		_dbg_lockInit(&pLS->stLocks[i]);
// debug end
	}

	return FFAT_OK;

out:
	if (pLS)
	{
		for (j = 0; j < i; j++)
		{
			EssOsal_DeleteLock(&pLS->stLocks[j].stLock);
			FFAT_DEBUG_LOCK_PRINTF("Deleted(0x%X) \n", &pLS->stLocks[j].stLock);
		}

		FFAT_FREE(pLS, sizeof(_LockStorage));
	}

	return r;
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
	t_int32		r;

	r = EssOsal_InitMem();
	if (r != ESS_OK)
	{
		// fail to initialize memory
		return FFAT_EINIT;
	}

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
	t_int32		r;

	r = EssOsal_TerminateMem();
	if (r != ESS_OK)
	{
		return FFAT_EPANIC;
	}

// debug begin
#ifdef FFAT_DEBUG
	ffat_main_fsctl(FFAT_FSCTL_STATUS_MEM_TERMINATE, _GET_MEM_STATUS(), NULL, NULL);
#endif
// debug end

	return FFAT_OK;
}


/**
 * This function allocates memory
 *
 * @param		dwSize		: allocation byte
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
void*
ffat_al_allocMem(t_int32 dwSize, EssMallocFlag dwMallocFlag)
{
	void*	p;

	p = EssOsal_AllocMem(dwSize, dwMallocFlag);

// debug begin
	if (p)
	{
		FFAT_DEBUG_MEM_PRINTF("Alloc Ptr/Size:0x%X, %d \n", p, dwSize);

#ifdef FFAT_DEBUG
		ffat_main_fsctl(FFAT_FSCTL_STATUS_MEM_ALLOC, _GET_MEM_STATUS(), &dwSize, NULL);
#endif
	}
// debug end

	return p;
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

	EssOsal_FreeMem(p);

// debug begin
	FFAT_DEBUG_MEM_PRINTF("Free Ptr/Size:0x%X, %d \n", p, dwSize);
#ifdef FFAT_DEBUG
	ffat_main_fsctl(FFAT_FSCTL_STATUS_MEM_FREE, _GET_MEM_STATUS(), &dwSize, NULL);
#endif
// debug end
}


// debug begin
// for memory status
void
ffat_al_memCheckBegin(const char* psFuncName)
{
	#ifdef FFAT_DEBUG
		ffat_main_fsctl(FFAT_FSCTL_STATUS_MEM_CHECK_BEGIN, _GET_MEM_STATUS(), (void*)psFuncName, NULL);
	#endif
}

void
ffat_al_memCheckEnd(void)
{
	#ifdef FFAT_DEBUG
		ffat_main_fsctl(FFAT_FSCTL_STATUS_MEM_CHECK_END, _GET_MEM_STATUS(), NULL, NULL);
	#endif
}
// debug end

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
	EssTime		stTime;
	t_int32		r;

	FFAT_ASSERT(pTime);

	r = EssOsal_LocalTime(&stTime);
	if (r != ESS_OK)
	{
		FFAT_LOG_PRINTF("Fail to get local time");
		return FFAT_EPANIC;
	}

	pTime->tm_sec	= stTime.tm_sec;
	pTime->tm_min	= stTime.tm_min;
	pTime->tm_hour	= stTime.tm_hour;
	pTime->tm_mday	= stTime.tm_mday;
	pTime->tm_mon	= stTime.tm_mon;
	pTime->tm_year	= stTime.tm_year;
	pTime->tm_wday	= stTime.tm_wday;
	pTime->tm_yday	= stTime.tm_yday;

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
 * This function print a string
 *
 * @param		pFmt	: print format 
 * @param		...		: parameters
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_printf(const char* pFmt, ...)
{
#if 1
	static char	psStr[2048];
	va_list			ap;
	t_int32			r;

	va_start(ap,pFmt);

	vsprintf(psStr, pFmt, ap);
	r = EssOsal_Printf((const t_int8*)psStr);

	va_end(ap);

	return r;
#else
	va_list		ap;
	t_int32		r;

	va_start(ap,pFmt);

	r = EssOsal_Vprintf(pFmt, ap);

	va_end(ap);

	return r;
#endif
}


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
	return EssOsal_GetChar();
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

	_pConfig= FFAT_MALLOC(sizeof(FFatConfig), ESS_MALLOC_IO);
	if (_CONFIG() == NULL)
	{
		FFAT_LOG_PRINTF("Fail to allocate memory for HPA");
		return FFAT_ENOMEM;
	}

	// FFAT
	_CONFIG()->dwSectorSize			= FFAT_SECTOR_SIZE;

	// FFATFS
	_CONFIG()->dwFFatfsCacheSize	= FFATFS_CACHE_SIZE_IN_BYTE;

	// LOG
	r = _initConfigLog();
	if (r != FFAT_OK)
	{
		return r;
	}

	r = _initConfigHPA();
	if (r != FFAT_OK)
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
	FFAT_FREE(_pConfig, sizeof(_pConfig));

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
* init configuration for LOG
*
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_initConfigLog(void)
{
	if (FFAT_STRLEN(FFAT_LOG_FILE_NAME) >= FFAT_LOG_FILE_NAME_MAX_LENGTH)
	{
		FFAT_LOG_PRINTF("too log log file name");
		return FFAT_EINVALID;
	}

	FFAT_MBSTOWCS(_CONFIG()->stLog.psFileName, FFAT_LOG_FILE_NAME, FFAT_LOG_FILE_NAME_MAX_LENGTH, NULL);

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
	if (FFAT_STRLEN(FFAT_HPA_ROOT_NAME) >= FFAT_HPA_ROOT_NAME_MAX_LENGTH)
	{
		FFAT_LOG_PRINTF("Fail to init config for HPA");
		return FFAT_EINVALID;
	}

	FFAT_MBSTOWCS(_CONFIG()->stHPA.psHPARootName, FFAT_HPA_ROOT_NAME, FFAT_HPA_ROOT_NAME_MAX_LENGTH, NULL);

#ifndef FFAT_VFAT_SUPPORT
	_CONFIG()->stHPA.psHPARootName[FFAT_SFN_NAME_LEN] = '\0';
#endif

	// set HPA volume count
	_CONFIG()->stHPA.wHPAVolCount	= FFAT_HPA_MAX_VOLUME_COUNT;
	// set HPA bitmap size
	_CONFIG()->stHPA.wHPABitmapSize	= FFAT_HPA_BITMAP_SIZE;

	return FFAT_OK;
}


// debug begin
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

	_LOCK(pLock)->bLocked			= FFAT_FALSE;
	_LOCK(pLock)->dwLockCount		= 0;
	_LOCK(pLock)->dwUnlockCount	= 0;
	
	return FFAT_OK;
}

//
//	END OF DEBUG FUNCTIONS
//
//=============================================================================
// debug end


