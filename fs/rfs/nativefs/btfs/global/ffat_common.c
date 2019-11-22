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
 * @file		ffat_common.c
 * @brief		common moudle for FFAT_CORE and FFAT_FATFS
 * @author		Seo Dong Young(dy76.seo@samsung.com)
 * @version		JUL-14-2006 [DongYoung Seo] First writing
 * @see			None
 */


//*****************************************************************/
//
// FFATCOMMON은 FFAT과 FFATFS에서 공통으로 사용되는 함수가 구현된다.
//
//*****************************************************************/

// header - ESS_BASE

// header - FFAT
#include "ffat_config.h"
#include "ffat_al.h"

#include "ffat_common.h"
#include "ffat_vol.h"
#include "ffat_main.h"

// header - Abstraction module

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_GLOBAL_COMMON)

//#define _DEBUG_LOCK
//#define _DEBUG_MEM

// definition
#ifdef FFAT_LITTLE_ENDIAN
	#define		_MAKEWORD(first, second)	(((second) << 8) + (first))
#else
	#define		_MAKEWORD(first, second)	(((first) << 8) + (second))
#endif

//=============================================================================
//
//	Atomic Module
//


// ATOMIC LOCK IS USED FOR INTERNAL CONSISTENCY 
//	THIS LOCK MUST BE USED FOR A SHORT TIME.
#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	// caution!!.
	// ATOMIC_LOCK and UNLOCK are used for a short time lock. This is not a MUTEX !!.
	// Do not use nested lock or unlock.
	// use lock -> unlock -> lock -> unlock ... !!
	#define	_ATOMIC_LOCK(_r)	do	\
								{	\
									_r = ffat_al_getLock(_LOCK_ATOMIC());	\
									IF_UK (_r < 0)	\
									{	\
										return _r;	\
									}	\
								} while (0)

	#define	_ATOMIC_UNLOCK(_r)	do	\
								{	\
									_r = ffat_al_putLock(_LOCK_ATOMIC());	\
									IF_UK (_r < 0)	\
									{	\
										return _r;	\
									}	\
								} while (0)
	#define	_ATOMIC_LOCK_VOID(_r)	_r = ffat_al_getLock(_LOCK_ATOMIC())
	#define	_ATOMIC_UNLOCK_VOID(_r)	_r = ffat_al_putLock(_LOCK_ATOMIC())
#else
	#define		_ATOMIC_LOCK(_r)
	#define		_ATOMIC_UNLOCK(_r)
	#define		_ATOMIC_LOCK_VOID(_r)
	#define		_ATOMIC_UNLOCK_VOID(_r)
#endif

// lock for Atomic operation and lock module
#define _LOCK_ATOMIC()			((FFatLock*)_pLockAtomic)
#define _LOCK_ATOMIC_PTR()		((FFatLock**)&_pLockAtomic)

//static FFatLock* _pLockAtomic;
static t_int32*	_pLockAtomic;


//=============================================================================
// PSTACK MODULE

#define _PSTACK_MAIN()		((EssPStackMain*)&_stPStackMain)

//static EssPStackMain _stPStackMain;
static t_int32		_stPStackMain[sizeof(EssPStackMain)/sizeof(t_int32)];
static t_int32*		_pBuffPStack[FFAT_PSTACK_COUNT];

#if (FFAT_LOCK_TYPE == FFAT_LOCK_SINGLE)
	static t_int8*		_gpPStackForCxt = NULL;
#endif

//=============================================================================

#define _STATISTIC_GET_PSTACK_INC
#define _STATISTIC_RELEASE_PSTACK_INC
#define _STATISTIC_LOCALALLOC_INC
#define _STATISTIC_LOCALFREE_INC
#define _STATISTIC_GETREADLOCK_INC
#define _STATISTIC_PUTREADLOCK_INC
#define _STATISTIC_GETWRITELOCK_INC
#define _STATISTIC_PUTWRITELOCK_INC
#define _STATISTIC_GETAFREELOCK_INC
#define _STATISTIC_RELEASEALOCK_INC

#define _STATISTIC_GETAFREELOCK		0		// get a free lock count
#define _STATISTIC_RELEASEALOCK		0		// release a lock count
#define _STATISTIC_PRINT

#define _CXT_INIT_LOCK
#define _CXT_CHECK_LOCK

// debug begin
#ifdef FFAT_DEBUG
	// file system status - this is development purpose, not for retail

	#define _DEBUG_MAX_FUNCTION_NAME_LEN		48

	typedef struct
	{
		t_int32		dwMemUsage;			// current memory usage in byte
		t_int32		dwMaxMemUsage;		// max memory usage in byte
		t_uint32	dwAllocCount;		// memory allocation count
		t_uint32	dwFreeCount;		// memory free count
		t_int32		dwMemCheck;			// variable for memory check
		t_int8		psFuncName[_DEBUG_MAX_FUNCTION_NAME_LEN];
										// max memory use function name
		t_int8		psCurFuncName[_DEBUG_MAX_FUNCTION_NAME_LEN];
										// current use function name
	} _MemStatus;

	typedef struct
	{
		t_int32		dwStackUsage;		// current stack usage 
		t_int32		dwMaxStackUsage;	// mas stack usage in byte 
		t_int8		psFuncName[_DEBUG_MAX_FUNCTION_NAME_LEN];
										// max memory use function name
		t_int8		psCurFuncName[_DEBUG_MAX_FUNCTION_NAME_LEN];
										// current use function name
	} _StackStatus;

	typedef struct
	{
		t_uint32		dwGetPStack;		// get PSTACK count
		t_uint32		dwReleasePStack;	// release PSTACK count
		t_uint32		dwLocalAlloc;		// local memory alloc count
		t_uint32		dwLocalFree;		// local memory free count
		t_uint32		dwGetReadLock;
		t_uint32		dwPutReadLock;
		t_uint32		dwGetWriteLock;
		t_uint32		dwPutWriteLock;
		t_uint32		dwGetFreeLock;		// get a free lock
		t_uint32		dwReleaseLock;		// release a lock
	} _Statistics;

	typedef struct
	{
		_MemStatus			stMemStatus;		// memory usage
		_StackStatus		stStackStatus;		// stack usage

		_Statistics		stLockStatistics;		// call statistics
	} _Status;

	static _Status			_stStatus;

	#define _STATISTIC()		(&_stStatus.stLockStatistics)

	#undef _STATISTIC_GET_PSTACK_INC
	#undef _STATISTIC_RELEASE_PSTACK_INC
	#undef _STATISTIC_LOCALALLOC_INC
	#undef _STATISTIC_LOCALFREE_INC
	#undef _STATISTIC_GETREADLOCK_INC
	#undef _STATISTIC_PUTREADLOCK_INC
	#undef _STATISTIC_GETWRITELOCK_INC
	#undef _STATISTIC_PUTWRITELOCK_INC
	#undef _STATISTIC_GETAFREELOCK_INC
	#undef _STATISTIC_RELEASEALOCK_INC
	#undef _STATISTIC_GETAFREELOCK				// get a free lock count
	#undef _STATISTIC_RELEASEALOCK				// release a lock count

	#undef _STATISTIC_PRINT

	#define _STATISTIC_GET_PSTACK_INC			_STATISTIC()->dwGetPStack++;
	#define _STATISTIC_RELEASE_PSTACK_INC		_STATISTIC()->dwReleasePStack++;
	#define _STATISTIC_LOCALALLOC_INC			_STATISTIC()->dwLocalAlloc++;
	#define _STATISTIC_LOCALFREE_INC			_STATISTIC()->dwLocalFree++;
	#define _STATISTIC_GETREADLOCK_INC			_STATISTIC()->dwGetReadLock++;
	#define _STATISTIC_PUTREADLOCK_INC			_STATISTIC()->dwPutReadLock++;
	#define _STATISTIC_GETWRITELOCK_INC			_STATISTIC()->dwGetWriteLock++;
	#define _STATISTIC_PUTWRITELOCK_INC			_STATISTIC()->dwPutWriteLock++;
	#define _STATISTIC_GETAFREELOCK_INC			++_STATISTIC()->dwGetFreeLock;
	#define _STATISTIC_RELEASEALOCK_INC			++_STATISTIC()->dwReleaseLock;
	#define _STATISTIC_GETAFREELOCK				_STATISTIC()->dwGetFreeLock
	#define _STATISTIC_RELEASEALOCK				_STATISTIC()->dwReleaseLock

	#define _STATISTIC_PRINT	_printStatistics();

	static void			_printStatistics(void);

	// initializes lock count
	#undef _CXT_INIT_LOCK
	#define _CXT_INIT_LOCK		pCxt->dwLockCount = pCxt->dwUnlockCount = 0;
	#undef _CXT_CHECK_LOCK
	#define _CXT_CHECK_LOCK		FFAT_ASSERT(pCxt->dwLockCount == pCxt->dwUnlockCount);
#endif

#ifdef _DEBUG_LOCK
	#ifndef FFAT_DEBUG
		#error "_DEBUG_LOCK configuration must be used with FFAT_DEBUG"
	#endif

	#define FFAT_DEBUG_LOCK_PRINTF	FFAT_PRINT_DEBUG((_T("[LOCK (%d)] "), FFAT_DEBUG_GET_TASK_ID())); FFAT_PRINT_DEBUG
#else
	#define FFAT_DEBUG_LOCK_PRINTF(_msg)
#endif


// memory debug part
#ifdef _DEBUG_MEM
	#ifndef FFAT_DEBUG
		#error "_DEBUG_MEM configuration must be used with FFAT_DEBUG"
	#endif

	#define FFAT_DEBUG_MEM_PRINTF	FFAT_PRINT_DEBUG((_T("[MEM (%d)] "), GetCurrentThreadId())); FFAT_PRINT_DEBUG
#else
	#define FFAT_DEBUG_MEM_PRINTF(_msg)
#endif

#ifdef FFAT_DEBUG
	#define _DEBUG_STATUS_MEM_INIT()		_statusMemInit()
	#define _DEBUG_STATUS_MEM_TERMINATE()	_statusMemTerminate()
	#define _DEBUG_STATUS_MEM_ALLOC(_p, _s)	_statusMemAlloc(_p, _s);
	#define _DEBUG_STATUS_MEM_FREE(_s)		_statusMemFree(_s);

	// memory and stack status module is for development
	static void		_statusMemInit(void);
	static void		_statusMemTerminate(void);
	static void		_statusMemAlloc(void* pPtr, t_int32 dwSize);
	static void		_statusMemFree(t_int32 dwSize);
	static void		_statusMemPrint(void);
#else
	#define _DEBUG_STATUS_MEM_INIT()
	#define _DEBUG_STATUS_MEM_TERMINATE()
	#define _DEBUG_STATUS_MEM_ALLOC(_p, _s)
	#define _DEBUG_STATUS_MEM_FREE(_s)
#endif

// debug end


/**
 * This function Initialize FFatCom module
 *
 * @return		FFAT_OK	: Success
 * @return		else	: error
 * @author		DongYoung Seo
 * @version		JUL-20-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_common_init(void)
{
	FFatErr		r;

	r = ffat_atomic_init();
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to initialize FFatAtomic ")));
		return r;
	}

	r = ffat_common_initMem();
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to initialize memory ")));
		return r;
	}

	return FFAT_OK;
}


/**
 * This function terminates FFatCom module
 *
 * @return		FFAT_OK	: Success
 * @return		else	: error
 * @author		DongYoung Seo
 * @version		JUL-20-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_common_terminate(void)
{
	FFatErr		r;

	r = ffat_common_terminateMem();
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to terminate memory ")));
		return r;
	}

	r = ffat_atomic_terminate();
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to terminate FFatAtomic ")));
		return r;
	}

	_STATISTIC_PRINT

	return FFAT_OK;
}


//======================================================================
//
//	Memory Allocation / Deallocation module
//

/**
 * Initialize common memory 
 *
 * This function initializes PStack for common memory usage 
 * such as FFatfsCache module
 *
 * It do not anything on dynamic memory allocation.
 *
 * @return		FFAT_OK	: success
 * @return		else	: error
 * @author		DongYoung Seo
 * @version		JUL-25-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_common_initMem(void)
{
	FFatErr		r;
	t_int32		i;

	_DEBUG_STATUS_MEM_INIT();

	// initialize _pBuffPStack
	FFAT_MEMSET(_pBuffPStack, 0x00 ,sizeof(_pBuffPStack));
	FFAT_MEMSET(_PSTACK_MAIN(), 0x00, sizeof(EssPStackMain));

	r = EssPStack_InitMain(_PSTACK_MAIN());
	IF_UK (r != ESS_OK)
	{
		FFAT_LOG_PRINTF((_T("Fail to init PStack Main")));
		return FFAT_EINIT;
	}

#ifdef FFAT_DYNAMIC_ALLOC
	for (i = 0; i < FFAT_PSTACK_COUNT; i++)
	{
		_pBuffPStack[i] = NULL;
	}

#if (FFAT_LOCK_TYPE == FFAT_LOCK_SINGLE)
	_gpPStackForCxt = NULL;
	_gpPStackForCxt = FFAT_MALLOC(FFAT_LOCAL_PSTACK_SIZE, ESS_MALLOC_IO);
	IF_UK (_gpPStackForCxt == NULL)
	{
		FFAT_LOG_PRINTF((_T("Fail to allocate memory for PStack")));
		return FFAT_ENOMEM;
	}
#endif
#else

	// create several PSTACK only for multiple lock state
	FFAT_DEBUG_PRINTF((_T("PSTACK SIZE for volume : %d \n"), FFAT_LOCAL_PSTACK_SIZE));

	for (i = 0; i < FFAT_PSTACK_COUNT; i++)
	{
		_pBuffPStack[i] = FFAT_MALLOC(FFAT_LOCAL_PSTACK_SIZE, ESS_MALLOC_IO);
		IF_UK (_pBuffPStack[i] == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for PSTACK")));
			return FFAT_ENOMEM;
		}

		r = EssPStack_Add(_PSTACK_MAIN(), (t_int8*)_pBuffPStack[i], FFAT_LOCAL_PSTACK_SIZE);
		IF_UK (r != ESS_OK)
		{
			return FFAT_EINIT;
		}
	}
#endif

	return FFAT_OK;
}


/**
 * terminate common memory 
 *
 * This function terminates PStack for common memory usage 
 *
 * @return		FFAT_OK	: success
 * @return		else	: error
 * @author		DongYoung Seo
 * @version		DEC-18-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_common_terminateMem(void)
{

#ifndef FFAT_DYNAMIC_ALLOC
	// release memory for PStack
	t_int32		i;

	for (i = 0; i < FFAT_PSTACK_COUNT; i++)
	{
		ffat_common_freeMem(_pBuffPStack[i], FFAT_LOCAL_PSTACK_SIZE);
	}
#else
#if (FFAT_LOCK_TYPE == FFAT_LOCK_SINGLE)
	FFAT_FREE(_gpPStackForCxt, FFAT_LOCAL_PSTACK_SIZE);
	_gpPStackForCxt = NULL;
#endif
#endif

	_DEBUG_STATUS_MEM_TERMINATE();

	return FFAT_OK;
}


/**
 * This function allocates memory from OSAL
 *
 * @param		dwSize	: allocation size
 * @param		pCxt	: context pointer
 * @return		NULL	: fail to allocation
 * @return		else	: available memory pointer
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
void*
ffat_common_allocMem(t_int32 dwSize, EssMallocFlag dwFlag)
{
	void*	pPtr;

	pPtr = ffat_al_allocMem(dwSize, dwFlag);

	_DEBUG_STATUS_MEM_ALLOC(pPtr, dwSize);

	return pPtr;
}


/**
 * This function free memory from OSAL
 *
 * @param		p		: memory pointer
 * @param		dwSize	: size of allocated memory
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
void
ffat_common_freeMem(void* p, t_int32 dwSize)
{
	ffat_al_freeMem(p, dwSize);

	_DEBUG_STATUS_MEM_FREE(dwSize);
}


/**
 * This function allocates memory from PSTACK
 *
 * @param		dwSize	: allocation size
 * @param		pCxt	: context pointer
 * @return		NULL	: fail to allocation
 * @return		else	: available memory pointer
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
void*
ffat_common_localMalloc(t_int32 dwSize, ComCxt* pCxt)
{
	_STATISTIC_LOCALALLOC_INC

	FFAT_ASSERT(pCxt);
	return EssPStack_Alloc(_PSTACK_MAIN(), pCxt->pPStack, dwSize);
}


/**
 * This function free memory to PSTACK
 *
 * @param		p		: memory pointer
 * @param		dwSize	: allocation size
 * @param		pCxt	: context pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
void
ffat_common_localFree(void* p, t_int32 dwSize, ComCxt* pCxt)
{
	_STATISTIC_LOCALFREE_INC

	if (p)
	{
		EssPStack_Free(_PSTACK_MAIN(), pCxt->pPStack, p, dwSize);
	}

	return;
}


/**
 * This function return a free PStack
 *
 * @param		dwSize		: allocation size
 * @param		ppPStack	: PSTACK storage pointer
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOMEM	: There is no free PSTACK
 * @author		DongYoung Seo
 * @version		DEC-05-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_pstack_getFree(t_int32 dwSize, void** ppPStack)
{
	FFatErr		r;

	_STATISTIC_GET_PSTACK_INC

	FFAT_ASSERT(ppPStack);

#ifdef FFAT_DYNAMIC_ALLOC

	*ppPStack = NULL;

	r = FFAT_OK;

#else

	_ATOMIC_LOCK(r);

	*ppPStack = EssPStack_GetFree(_PSTACK_MAIN(), dwSize);

	_ATOMIC_UNLOCK(r);

	r = *ppPStack ? FFAT_OK : FFAT_ENOMEM;

#endif

	return r;
}


/**
* This function release a PStack
*
* @param		pPStack	: PStack pointer
* @return		void
* @author		DongYoung Seo
* @version		DEC-05-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_pstack_release(void* pPStack)
{
#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
#endif

	_ATOMIC_LOCK(r);

	_STATISTIC_RELEASE_PSTACK_INC

	EssPStack_Release(_PSTACK_MAIN(), pPStack);

	_ATOMIC_UNLOCK(r);

	return FFAT_OK;
}

//
//	End of Memory Allocation / Deallocation module
//
//======================================================================



//======================================================================
//
//	FFatAtomic module
//


/**
 * This function initializes FFatAtomic module
 *
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
 * @version		JAN-15-2008 [DongYoung Seo] always get a lock for atomic 
 *										for spin lock support
*/
FFatErr
ffat_atomic_init(void)
{
	_pLockAtomic = NULL;

	// create a lock for FFatAtomic
	return ffat_al_getFreeLock(_LOCK_ATOMIC_PTR());
}


/**
 * This function initializes FFatAtomic module
 *
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
 * @version		JAN-15-2008 [DongYoung Seo] always release atomic lock for atomic 
 *									for spin lock support
*/
FFatErr
ffat_atomic_terminate(void)
{
	// delete a lock for FFatAtomic
	return ffat_al_releaseLock(_LOCK_ATOMIC_PTR());
}


/**
 * This function increase value of a variable
 *
 * @param		pVal	: variable pointer
 * @return		value after increase
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_atomic32
ffat_atomic_inc32(t_atomic32* pVal)
{
	t_atomic32	dwRet;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
#endif

	_ATOMIC_LOCK(r);

	(*pVal)++;
	dwRet = *pVal;

	_ATOMIC_UNLOCK(r);

	return dwRet;
}


/**
 * This function decrease value of variable
 *
 * @param		pVal	: variable pointer
 * @return		value after decrease
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_atomic32
ffat_atomic_dec32(t_atomic32* pVal)
{
	t_atomic32		dwRet;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
#endif

	_ATOMIC_LOCK(r);

	(*pVal)--;
	dwRet = *pVal;

	_ATOMIC_UNLOCK(r);

	return dwRet;
}


/**
 * This function increase value of a variable
 *
 * @param		pVal	: variable pointer
 * @return		value after increase
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_atomic16
ffat_atomic_inc16(t_atomic16* pVal)
{
	t_atomic16	wRet;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
#endif

	_ATOMIC_LOCK_VOID(r);		// ignore error

	(*pVal) = (*pVal) + 1;
	wRet = *pVal;

	_ATOMIC_UNLOCK_VOID(r);

	return wRet;
}


/**
 * This function decrease value of variable
 *
 * @param		pVal	: variable pointer
 * @return		value after decrease
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_atomic16
ffat_atomic_dec16(t_atomic16* pVal)
{
	t_atomic16		wRet;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
#endif

	_ATOMIC_LOCK_VOID(r);		// ignore error

	(*pVal)--;
	wRet = *pVal;

	_ATOMIC_UNLOCK_VOID(r);		// ignore error

	return wRet;
}

//
//	End of FFatAtomic module
//
//======================================================================


//======================================================================
//
//	String module
//
/**
 * 입력된 length 만큼 만 대/소 문자를 구별하지 않고 string을 비교한다.
 *
 * @param		wcs1		: string 1
 * @param		wcs2		: string 2
 * @param		dwLen		: compare character count
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_common_wcsicmp(const t_wchar* wcs1, const t_wchar* wcs2, t_int32 dwLen)
{
	t_wchar		f, l, i;

	f = l = 0;

	i = 0;

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) /* 2-byte aligned */
	{
		do
		{
			if (*wcs1 != *wcs2)
			{
				f = FFAT_TOWLOWER((t_wchar)(*wcs1));
				l = FFAT_TOWLOWER((t_wchar)(*wcs2));
			}
			else
			{
				f = l = *wcs1;
			}

			wcs1++; wcs2++;

			if (i++ >= dwLen)
			{
				// same
				return 0;
			}
		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}
	else /* not-aligned */
	{
		t_uint8*	pc1 = (t_uint8*) wcs1;
		t_uint8*	pc2 = (t_uint8*) wcs2;

		t_wchar		wc1, wc2;

		do
		{
			// we don't have to worry about following two local variables. because,
			// they are optimized by the compiler and excluded from the final binary.
			wc1 = (t_wchar) _MAKEWORD(*pc1, *(pc1 + 1));
			wc2 = (t_wchar) _MAKEWORD(*pc2, *(pc2 + 1));

			// convert them to lower case
			if (wc1 != wc2)
			{
				f = FFAT_TOWLOWER(wc1);
				l = FFAT_TOWLOWER(wc2);
			}
			else
			{
				f = l = wc1;
			}

			pc1 += 2;
			pc2 += 2;

			if (i++ >= dwLen)
			{
				// same
				return 0;
			}

		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}

	return (t_int32)(f - l);
}


/**
 * check if the two strings are same (case-insensitive)
 *
 * @param		s1		: first string
 * @param		s2		: second string
 * @return		1		: They are different
 * @return		0		: They are same
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
t_int32
ffat_common_stricmp(const t_int8* s1, const t_int8* s2)
{
	t_uint8		c1, c2;

	FFAT_ASSERT(s1);
	FFAT_ASSERT(s2);

	while (*s1 != '\0')
	{
		c2 = *s2++;
		if (c2 == '\0')
		{
			return 1;
		}
		c1 = *s1++;

		if (c1 == c2)
		{
			continue;
		}

		c1 = (t_uint8)FFAT_TOLOWER(c1);
		c2 = (t_uint8)FFAT_TOLOWER(c2);

		if (c1 != c2)
		{
			return 1;
		}
	}

	if (*s2 != '\0')
	{
		return 1;
	}

	return 0;
}


/**
 * 대/소 문자를 구별하지 않고 string을 비교한다.
 * 단, string1 은 대문자로 입력된다.
 *
 * @param		wcs1		: string 1 (uppercase string)
 * @param		wcs2		: string 2
 * @return		void
 * @author		GwangOk Go
 * @version		DEV-18-2007 [GwangOk Go] Modify ess_wcsicmp()
 */
FFatErr
ffat_common_wcsucmp(const t_wchar* wcs1, const t_wchar* wcs2)
{
	t_wchar		f, l;

	f = l = 0;

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) /* 2-byte aligned */
	{
		do
		{
			if (*wcs1 != *wcs2)
			{
				f = *wcs1;
				l = FFAT_TOWUPPER((t_wchar)(*wcs2));
			}
			else
			{
				f = l = *wcs1;
			}

			wcs1++;
			wcs2++;

		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}
	else /* not-aligned */
	{
		t_uint8*	pc1 = (t_uint8*) wcs1;
		t_uint8*	pc2 = (t_uint8*) wcs2;

		t_wchar		wc1, wc2;

		do
		{
			// we don't have to worry about following two local variables. because,
			// they are optimized by the compiler and excluded from the final binary.
			wc1 = (t_wchar) _MAKEWORD(*pc1, *(pc1 + 1));
			wc2 = (t_wchar) _MAKEWORD(*pc2, *(pc2 + 1));

			// convert them to lower case
			if (wc1 != wc2)
			{
				f = wc1;
				l = FFAT_TOWUPPER(wc2);
			}
			else
			{
				f = l = wc1;
			}

			pc1 += 2;
			pc2 += 2;

		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}

	return (t_int32)(f - l);
}


/**
 * 대/소 문자를 구별하지 않고 string을 비교한다.
 * 단, string1 은 대문자로 입력된다.
 *
 * @param		wcs1		: string 1 (uppercase string)
 * @param		wcs2		: string 2
 * @return		void
 * @author		GwangOk Go
 * @version		DEV-18-2007 [GwangOk Go] Modify ess_wcsicmp()
 */
FFatErr
ffat_common_wcsnucmp(const t_wchar* wcs1, const t_wchar* wcs2, t_int32 dwLen)
{
	t_wchar		f, l, i;

	f = l = i = 0;

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) /* 2-byte aligned */
	{
		do
		{
			if (*wcs1 != *wcs2)
			{
				f = *wcs1;
				l = FFAT_TOWUPPER((t_wchar)(*wcs2));
			}
			else
			{
				f = l = *wcs1;
			}

			wcs1++;
			wcs2++;

			if (i++ >= dwLen)
			{
				// same
				return 0;
			}

		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}
	else /* not-aligned */
	{
		t_uint8*	pc1 = (t_uint8*) wcs1;
		t_uint8*	pc2 = (t_uint8*) wcs2;

		t_wchar		wc1, wc2;

		do
		{
			// we don't have to worry about following two local variables. because,
			// they are optimized by the compiler and excluded from the final binary.
			wc1 = (t_wchar) _MAKEWORD(*pc1, *(pc1 + 1));
			wc2 = (t_wchar) _MAKEWORD(*pc2, *(pc2 + 1));

			// convert them to lower case
			if (wc1 != wc2)
			{
				f = wc1;
				l = FFAT_TOWUPPER(wc2);
			}
			else
			{
				f = l = wc1;
			}

			pc1 += 2;
			pc2 += 2;

			if (i++ >= dwLen)
			{
				// same
				return 0;
			}

		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}

	return (t_int32)(f - l);
}


/**
 * wide character string을 대문자로 변환한다.
 *
 * @param		pDesWcs		: [OUT] output string
 * @param		pSrcWcs		: [IN] input string
 * @return		void
 * @author		GwangOk Go
 * @version		DEC-18-2007 [GwangOk Go] First Writing.
 */
void
ffat_common_towupper_str(t_wchar* pDesWcs, const t_wchar* pSrcWcs)
{
	while (1) 
	{
		*pDesWcs = FFAT_TOWUPPER(*pSrcWcs);

		if (*pSrcWcs == '\0')
		{
			return;
		}

		pDesWcs++;
		pSrcWcs++;
	}
}


//
//	End of String module
//
//======================================================================


//======================================================================
//
//	Lock Management module
//


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
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_getFreeLock(FFatLock** ppLock)
{
	FFatErr		r;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		rr;
#endif

	_ATOMIC_LOCK(rr);

	r = ffat_al_getFreeLock(ppLock);

	_ATOMIC_UNLOCK(rr);

	_STATISTIC_GETAFREELOCK_INC
	FFAT_DEBUG_LOCK_PRINTF((_T("get a free lock:0x%X, get/release count:%d/%d\n"), *ppLock, _STATISTIC_GETAFREELOCK, _STATISTIC_RELEASEALOCK));

	return r;
}


/**
 * release a lock and add it to free list
 *
 *
 * @param		ppLock	: lock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_releaseLock(FFatLock** ppLock)
{
	FFatErr		r;

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		rr;
#endif

	if (*ppLock == NULL)
	{
		return FFAT_OK;
	}

	_STATISTIC_RELEASEALOCK_INC
	FFAT_DEBUG_LOCK_PRINTF((_T("release a lock:0x%X, get/release count:%d/%d\n"), *ppLock, _STATISTIC_GETAFREELOCK, _STATISTIC_RELEASEALOCK));

	_ATOMIC_LOCK(rr);

	r = ffat_al_releaseLock(ppLock);

	_ATOMIC_UNLOCK(rr);

	return r;
}


/**
 * This function initializes a RW lock
 *
 * CAUTION : this function is not safe on multi-thread environment
 *
 * @param		pRWLock			: pointer of lock storage
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EPANIC		: fail to create a lock
 * @author		DongYoung Seo
 * @version		DEC-04-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_initRWLock(ComRWLock* pRWLock)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pRWLock);

	if (pRWLock->pLock == NULL)
	{
		r = FFAT_GET_FREE_LOCK(&(pRWLock->pLock));
		FFAT_EO(r, (_T("fail to get a free lock")));

		FFAT_ASSERT(pRWLock->pLockRW == NULL);

		r = FFAT_GET_FREE_LOCK(&(pRWLock->pLockRW));
		IF_UK (r < 0)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get a free lock")));
			r |= FFAT_RELEASE_LOCK(&pRWLock->pLock);
			goto out;
		}

		pRWLock->dwRefCount = 0;
	}

// debug begin
#ifdef FFAT_DEBUG
	#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
		if (r == FFAT_OK)
		{
			FFAT_ASSERT(pRWLock->pLock);
			FFAT_ASSERT(pRWLock->pLockRW);
		}
		else
		{
			FFAT_ASSERT(pRWLock->pLock == NULL);
			FFAT_ASSERT(pRWLock->pLockRW == NULL);
		}
	#endif
#endif
// debug end
out:
	return r;
}


/**
 * terminate a RW lock and add it to free list
 *
 *
 * @param		pRWLock	: lock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		DEC-04-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_terminateRWLock(ComRWLock* pRWLock)
{
	FFatErr		r;

	FFAT_ASSERT(pRWLock->dwRefCount == 0);

	r = FFAT_RELEASE_LOCK(&pRWLock->pLock);
	FFAT_ER(r, (_T("fail to release a lock")));

	pRWLock->pLock = NULL;

	r = FFAT_RELEASE_LOCK(&pRWLock->pLockRW);
	FFAT_ER(r, (_T("fail to release a lock")));

	pRWLock->pLockRW = NULL;

	return r;
}


/**
 * get read lock
 *
 *
 * @param		pRWLock	: RWLock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		DEC-03-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_rwlock_getReadLock(ComRWLock* pRWLock)
{
	FFatErr		r;

	if (pRWLock->pLock == NULL)
	{
		return FFAT_OK;
	}

	_STATISTIC_GETREADLOCK_INC

	r = FFAT_LOCK_GET(pRWLock->pLock);
	FFAT_ER(r, (_T("fail to get lock")));

	FFAT_ASSERT(pRWLock->dwRefCount >= 0);

	// check Ref Count
	if (pRWLock->dwRefCount == 0)
	{
		r = FFAT_LOCK_GET(pRWLock->pLockRW);	// get read lock
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to get lock")));
			goto out;
		}
	}

	pRWLock->dwRefCount++;

	FFAT_DEBUG_LOCK_PRINTF((_T("get read lock:0x%X, ref:%d\n"), pRWLock->pLockRW, pRWLock->dwRefCount));

out:
	r |= FFAT_LOCK_PUT(pRWLock->pLock);

	return r;
}


/**
 * put read lock
 *
 *
 * @param		pRWLock	: RWLock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		DEC-03-2007 [DongYoung Seo] First Writing.
 * @version		MAR-31-2009 [DongYoung Seo] release common lock after putting fail for RW lock
 */
FFatErr
ffat_rwlock_putReadLock(ComRWLock* pRWLock)
{
	FFatErr		r;

	if (pRWLock->pLock == NULL)
	{
		return FFAT_OK;
	}

	_STATISTIC_PUTREADLOCK_INC

	r = FFAT_LOCK_GET(pRWLock->pLock);
	FFAT_ER(r, (_T("fail to get lock")));

	pRWLock->dwRefCount--;

	FFAT_ASSERT(pRWLock->dwRefCount >= 0);

	// check Ref Count
	if (pRWLock->dwRefCount == 0)
	{
		r = FFAT_LOCK_PUT(pRWLock->pLockRW);	// get read lock
		FFAT_EO(r, (_T("fail to lock")));
	}

	FFAT_DEBUG_LOCK_PRINTF((_T("put read lock:0x%X, ref:%d\n"), pRWLock->pLockRW, pRWLock->dwRefCount));

out:
	r |= FFAT_LOCK_PUT(pRWLock->pLock);

	return r;
}


/**
 * get write lock
 *
 *
 * @param		pRWLock	: RWLock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		DEC-03-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_rwlock_getWriteLock(ComRWLock* pRWLock)
{
	FFatErr		r;

	if (pRWLock->pLockRW == NULL)
	{
		return FFAT_OK;
	}

	_STATISTIC_GETWRITELOCK_INC

	r = ffat_al_getLock(pRWLock->pLockRW);	// get read write lock
	FFAT_ER(r, (_T("fail to get write lock")));

	FFAT_ASSERT(pRWLock->dwRefCount == 0);	// reference count should be 0

	FFAT_DEBUG_LOCK_PRINTF((_T("get write lock:0x%X, ref:%d\n"), pRWLock->pLockRW, pRWLock->dwRefCount));

	return FFAT_OK;
}


/**
 * put write lock
 *
 *
 * @param		pRWLock	: RWLock structure pointer
 * @return		FFAT_OK	: success
 * @author		DongYoung Seo
 * @version		DEC-03-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_rwlock_putWriteLock(ComRWLock* pRWLock)
{
	FFatErr		r;

	FFAT_ASSERT(pRWLock->dwRefCount == 0);

	if (pRWLock->pLock == NULL)
	{
		return FFAT_OK;
	}

	_STATISTIC_PUTWRITELOCK_INC

	r = ffat_al_putLock(pRWLock->pLockRW);	// get read lock
	FFAT_ER(r, (_T("fail to put write lock")));

	FFAT_DEBUG_LOCK_PRINTF((_T("put write lock:0x%X, ref:%d\n"), pRWLock->pLockRW, pRWLock->dwRefCount));

	return FFAT_OK;
}


/**
 * Get atomic lock
 *
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		APR-17-2008 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_getAtomic(void)
{
#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;

	_ATOMIC_LOCK(r);
#endif

	return FFAT_OK;
}


/**
 * Put atomic lock
 *
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		APR-17-2008 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_putAtomic(void)
{
#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	FFatErr		r;
	_ATOMIC_UNLOCK(r);
#endif

	return FFAT_OK;
}


/**
 * Get spin lock
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EPANIC		: Fail to get lock
 * @author		DongYoung Seo
 * @version		JAN-15-2009 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_getSpin(void)
{
	return ffat_al_getLock(_LOCK_ATOMIC());
}


/**
 * Put spin lock
 *
 * @return		FFAT_OK			: success
 * @return		FFAT_EPANIC		: Fail to get lock
 * @author		DongYount Seo
 * @version		JAN-15-2009 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_lock_putSpin(void)
{
	return ffat_al_putLock(_LOCK_ATOMIC());
}


//
//	End of Lock Management module
//
//======================================================================


//======================================================================
//
//	Context module
//


/**
 * initializes and creates a context
 *
 *
 * @param		pCxt		: ComCxt structure pointer
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOMEM	: not enough memory
 * @author		DongYoung Seo
 * @version		DEC-12-2007 [DongYoung Seo] First Writing.
 */
FFatErr	
ffat_cxt_create(ComCxt *pCxt)
{

#ifdef FFAT_DYNAMIC_ALLOC

	t_int8*		pBuff;
	FFatErr		r;

	FFAT_ASSERT(pCxt);

#if (FFAT_LOCK_TYPE == FFAT_LOCK_SINGLE)
	pBuff = _gpPStackForCxt;
#else
	pBuff = FFAT_MALLOC(FFAT_LOCAL_PSTACK_SIZE, ESS_MALLOC_IO);
#endif
	IF_UK (pBuff == NULL)
	{
		FFAT_LOG_PRINTF((_T("Fail to allocate memory for PStack")));
		return FFAT_ENOMEM;
	}

	FFAT_ASSERT(((t_uint32)pBuff & FFAT_MEM_ALIGN_MASK) == 0);

	r = ESSPStack_initPStack(&pCxt->pPStack, pBuff, FFAT_LOCAL_PSTACK_SIZE);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to init PSTack")));
		FFAT_ASSERT(0);
		return FFAT_ENOMEM;
	}

	pCxt->dwFlag		= COM_FLAG_NONE;
	pCxt->dwLockCore	= 0;
	pCxt->dwLockAddon	= 0;

	_CXT_INIT_LOCK

	return FFAT_OK;

#else

	pCxt->dwFlag		= COM_FLAG_NONE;
	pCxt->dwLockCore	= 0;
	pCxt->dwLockAddon	= 0;

	_CXT_INIT_LOCK

	return ffat_pstack_getFree(FFAT_PSTACK_SIZE, (void**)&pCxt->pPStack);

#endif
}


/**
 * release a context
 *
 *
 * @param		pCxt		: ComCxt structure pointer
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOMEM	: not enough memory
 * @author		DongYoung Seo
 * @version		DEC-13-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_cxt_delete(ComCxt *pCxt)
{
	FFAT_ASSERT(pCxt);

#ifdef FFAT_DYNAMIC_ALLOC

	FFAT_ASSERT(pCxt->dwLockCore == 0);
	FFAT_ASSERT(pCxt->dwLockAddon == 0);

// debug begin
#ifdef FFAT_DEBUG
	FFAT_ASSERT(pCxt->dwLockCount == pCxt->dwLockCount);
#endif
// debug end
#if (FFAT_LOCK_TYPE == FFAT_LOCK_SINGLE)
	// DO NOTHING
#else
	FFAT_FREE(pCxt->pPStack, FFAT_LOCAL_PSTACK_SIZE);
#endif

	_CXT_CHECK_LOCK

	return FFAT_OK;
#else

	_CXT_CHECK_LOCK

	return ffat_pstack_release(pCxt->pPStack);

#endif
}


//
//	End of Context module
//
//======================================================================


//======================================================================
//
//	Utility functions
//

/**
 * lookup a specified offset from pVC
 *
 * pVC에서 dwOffset에 해당하는 cluster와 index를 저장하여 return 한다.
 *
 *
 * @param		dwOffset	: [IN] lookup offset
 * @param		pVC			: [IN] Fat vectored cluster
 * @param		pdwIndex	: [OUT] index storage
 *								찾지 못했을경우 *pdwIndex의 값은 변경되지 않는다.
 * @param		pdwCluster	: [OUT] next cluster number
 *								찾지 못했을경우 *pdwCluster의 값은 변경되지 않는다.
 * @param		dwCSB		: [IN] cluster size in bit count
 * @return		FFAT_OK			: lookup success
 * @return		FFAT_ENOENT		: lookup fail
 * @return		FFAT_EINVALID	: invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-24-2006 [DongYoung Seo] First Writing
 * @version		DEC-19-2007 [DongYoung Seo] move from FFATFS to Global
 */
FFatErr
ffat_com_lookupOffsetInVC(t_uint32 dwOffset, FFatVC* pVC, t_int32* pdwIndex,
							t_uint32* pdwCluster, t_int32 dwCSB)
{
	t_int32		dwIndex = 0;
	t_uint32	dwClusterCount;

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwIndex);
	FFAT_ASSERT(pdwCluster);

	if (dwOffset < pVC->dwClusterOffset)
	{
		// offset이 더 작을 경우는 정보가 없음.
		return FFAT_ENOENT;
	}

	if (VC_IS_EMPTY(pVC) == FFAT_TRUE)
	{
		// no information at pVC
		return FFAT_ENOENT;
	}

	dwOffset = dwOffset - pVC->dwClusterOffset;
	dwClusterCount = dwOffset >> dwCSB;

	do
	{
		if (dwClusterCount < pVC->pVCE[dwIndex].dwCount)
		{
			// we got it
			*pdwIndex = dwIndex;
			*pdwCluster = pVC->pVCE[dwIndex].dwCluster + dwClusterCount;

			return FFAT_OK;
		}

		dwClusterCount -= pVC->pVCE[dwIndex].dwCount;

		dwIndex++;
	} while(dwIndex < pVC->dwValidEntryCount);

	return FFAT_ENOENT;
}


/**
 * lookup a specified cluster from pVC
 *
 * @param		dwCluster	: [IN] cluster number to lookup
 * @param		pVC			: [IN] Fat vectored cluster
 * @param		pdwIndex	: [IN] index storage
 * @return		FFAT_OK		: lookup success
 * @return		FFAT_ENOENT : lookup fail
 * @return		FFAT_EINVALID : invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_com_lookupClusterInVC(t_uint32 dwCluster, FFatVC* pVC, t_int32* pdwIndex)
{
	t_int32		i;

	FFAT_ASSERT(pdwIndex);

	if (pVC == NULL)
	{
		return FFAT_ENOENT;
	}

	for (i = 0; i < pVC->dwValidEntryCount; i++)
	{
		FFAT_ASSERT(pVC->pVCE[i].dwCount > 0);

		if ((dwCluster >= pVC->pVCE[i].dwCluster) &&
			(dwCluster < pVC->pVCE[i].dwCluster + pVC->pVCE[i].dwCount))
		{
			// we got it ~~, 찾았다아~~
			*pdwIndex = i;

			return FFAT_OK;
		}
	}
	
	return FFAT_ENOENT;
}


/**
 * lookup a specified cluster from pVC
 *
 * @param		dwCluster	: [IN] cluster number to lookup
 * @param		pVC			: [IN] Fat vectored cluster
 * @param		pdwIndex	: [IN] index storage
 *								찾지 못했을경우 *pdwIndex의 값은 변경되지 않는다.
 * @param		pdwCluster	: [IN] next cluster number
 *								찾지 못했을경우 *pdwCluster의 값은 변경되지 않는다.
 * @return		FFAT_OK		: lookup success
 * @return		FFAT_ENOENT : lookup fail
 * @return		FFAT_EINVALID : invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-24-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_com_lookupNextClusterInVC(t_uint32 dwCluster, FFatVC* pVC,
								t_int32* pdwIndex, t_uint32* pdwCluster)
{
	t_uint32	dwLast;
	t_int32		dwIndex;
	FFatErr		r;

	FFAT_ASSERT(pdwIndex);
	FFAT_ASSERT(pdwCluster);

	r = ffat_com_lookupClusterInVC(dwCluster, pVC, &dwIndex);
	if (r == FFAT_ENOENT)
	{
		return r;
	}

	// 일단 dwCluster는 찾았다. 다음 cluster만 찾으면 된다. 
	// 현재 cluster가 dwIndex의 마지막 cluster인지 확인
	dwLast = pVC->pVCE[dwIndex].dwCluster + pVC->pVCE[dwIndex].dwCount - 1;
	if (dwCluster == dwLast)
	{
		dwIndex++;
		if (dwIndex >= pVC->dwValidEntryCount)
		{
			// 아쉽다.. 바로 다음인데...
			return FFAT_ENOENT;
		}

		*pdwIndex = dwIndex;
		*pdwCluster = pVC->pVCE[dwIndex].dwCluster;
	}
	else
	{
		*pdwIndex = dwIndex;
		*pdwCluster = dwCluster + 1;
	}

	FFAT_ASSERT(*pdwCluster >= 2);

	return FFAT_OK;
}



/**
* merge one FFatVC to another FFatVC
* this functions does not care cluster offset
*
* @param		pVC_To		: [IN/OUT] merge target(base)
* @param		pVC_From	: [IN] merge source
* @return		void
* @author		DongYoung Seo
* @version		AUG-21-2006 [DongYoung Seo] First Writing.
* @version		SEP-03-2009 [JW Park] Add the check code whether pVC_From is no value
*/
void
ffat_com_mergeVC(FFatVC* pVC_To, FFatVC* pVC_From)
{
	t_uint32	dwLastCluster;
	t_int32		dwCurEntry;
	t_int32		dwCurEntryFrom = 0;

	FFAT_ASSERT(pVC_To);
	FFAT_ASSERT(pVC_From);

	FFAT_ASSERT(VC_TEC(pVC_To) >= VC_VEC(pVC_To));
	FFAT_ASSERT(VC_TEC(pVC_From) >= VC_VEC(pVC_From));

	if (VC_VEC(pVC_From) == 0)
	{
		// DO NOTHING
		return;
	}

	if (pVC_To->dwValidEntryCount == 0)
	{
		// pVC_To에 정보가 하나도 없을 경우
		dwCurEntry		= 0;
		dwLastCluster	= 0;
		pVC_To->pVCE[0].dwCluster	= 0;
		pVC_To->pVCE[0].dwCount	= 0;
		pVC_To->dwTotalClusterCount = 0;
	}
	else
	{
		dwCurEntry		= pVC_To->dwValidEntryCount - 1;
		dwLastCluster	= pVC_To->pVCE[dwCurEntry].dwCluster + pVC_To->pVCE[dwCurEntry].dwCount - 1;

		// set contiguous cluster information
		if ((dwLastCluster + 1) == pVC_From->pVCE[dwCurEntryFrom].dwCluster)
		{
			pVC_To->pVCE[dwCurEntry].dwCount	+= pVC_From->pVCE[dwCurEntryFrom].dwCount;
			pVC_To->dwTotalClusterCount			+= pVC_From->pVCE[dwCurEntryFrom].dwCount;
			dwCurEntryFrom++;
		}

		dwCurEntry++;
	}

	// merge them
	while ((dwCurEntry < pVC_To->dwTotalEntryCount) && 
		(dwCurEntryFrom < pVC_From->dwValidEntryCount))
	{
		pVC_To->pVCE[dwCurEntry].dwCluster	= pVC_From->pVCE[dwCurEntryFrom].dwCluster;
		pVC_To->pVCE[dwCurEntry].dwCount	= pVC_From->pVCE[dwCurEntryFrom].dwCount;
		pVC_To->dwTotalClusterCount			+= pVC_From->pVCE[dwCurEntryFrom].dwCount;

		dwCurEntry++;
		dwCurEntryFrom++;
	}

	pVC_To->dwValidEntryCount = dwCurEntry;

	return;
}



/**
* align(merge) last contiguous entries
*
* @param		pVC		: [IN/OUT] target VC
* @return		void
* @author		DongYoung Seo
* @version		JUN-01-2007 [DongYoung Seo] First Writing.
*/
void
ffat_com_alignVC(FFatVC* pVC)
{
	FFatVCE*	pCur;
	FFatVCE*	pPrev;
	t_int32		i;

	FFAT_ASSERT(pVC);

	if (VC_VEC(pVC) < 2)
	{
		return;
	}

	for (i = (VC_VEC(pVC) - 1); i > 0; i--)
	{
		pCur	= &pVC->pVCE[i];
		pPrev	= &pVC->pVCE[i - 1];
		
		if ((pPrev->dwCluster + pPrev->dwCount) == pCur->dwCluster)
		{
			// let's merge
			pPrev->dwCount += pCur->dwCount;

			pVC->dwValidEntryCount--;
			FFAT_ASSERT(pVC->dwValidEntryCount >= 0);
		}
		else if ((pCur->dwCluster + pCur->dwCount) == pPrev->dwCluster)
		{
			// let's merge
			pPrev->dwCluster	= pCur->dwCluster;
			pPrev->dwCount		+= pCur->dwCount;

			pVC->dwValidEntryCount--;
			FFAT_ASSERT(pVC->dwValidEntryCount >= 0);
		}
		else
		{
			break;
		}
	}
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
void ffat_common_cluster_change_notify(t_int32 dwFreeCount, t_int32 dwTotalCount, t_int32 dwClustersize,void* pDevice)
{	

	ffat_al_cluster_notify(dwFreeCount, dwTotalCount, dwClustersize, pDevice);

}


//
//	End of Utility functions
//
//======================================================================


// debug begin

//======================================================================
//
//	filesystem status module
//

#ifdef FFAT_DEBUG

	/**
	 * This function initializes memory usage variable for debug.
	 *
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_statusMemInit(void)
	{
		FFAT_MEMSET(&_stStatus.stMemStatus, 0x00, sizeof(_MemStatus));
		FFAT_DEBUG_MEM_PRINTF((_T("Initialized")));
	}


	/**
	 * This function prints memory usages and terminate
	 *
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_statusMemTerminate(void)
	{
		FFAT_DEBUG_PRINTF((_T("Max Memory Usage/Alloc/Free : %d/%d/%d \n"),
							_stStatus.stMemStatus.dwMaxMemUsage,
							_stStatus.stMemStatus.dwAllocCount,
							_stStatus.stMemStatus.dwFreeCount));
		if (_stStatus.stMemStatus.dwAllocCount != _stStatus.stMemStatus.dwFreeCount)
		{
			FFAT_DEBUG_PRINTF((_T("There is some allocated memory current(%d)!!, alloc/free :%d/%d \n"),
								_stStatus.stMemStatus.dwMemUsage, 
								_stStatus.stMemStatus.dwAllocCount,
								_stStatus.stMemStatus.dwFreeCount));
		}

		FFAT_DEBUG_MEM_PRINTF((_T("Terminated")));
	}


	/**
	 * This function update memory allocation status
	 *
	 * @param		pPtr		: allocated memory pointer, may be NULL
	 * @param		dwSize		: size of memory
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_statusMemAlloc(void* pPtr, t_int32 dwSize)
	{
		if (pPtr == NULL)
		{
			// memory allocation fail
			return;
		}

		_stStatus.stMemStatus.dwMemUsage += dwSize;
		_stStatus.stMemStatus.dwAllocCount++;

		if (_stStatus.stMemStatus.dwMemUsage > _stStatus.stMemStatus.dwMaxMemUsage)
		{
			_stStatus.stMemStatus.dwMaxMemUsage = _stStatus.stMemStatus.dwMemUsage;
			FFAT_STRNCPY(_stStatus.stMemStatus.psFuncName, 
							_stStatus.stMemStatus.psCurFuncName,
							sizeof(_stStatus.stMemStatus.psFuncName));
			FFAT_PRINT_DEBUG((_T("Max memory usage : %d, func : %s \n"),
							_stStatus.stMemStatus.dwMaxMemUsage,
							_stStatus.stMemStatus.psFuncName));
		}

		FFAT_DEBUG_MEM_PRINTF((_T("ALLOC, PTR/Size:0x%X/%d"), pPtr, dwSize));
	}


	/**
	 * This function update memory free status
	 *
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_statusMemFree(t_int32 dwSize)
	{
		_stStatus.stMemStatus.dwMemUsage -= dwSize;
		_stStatus.stMemStatus.dwFreeCount++;
		
		FFAT_ASSERT(_stStatus.stMemStatus.dwMemUsage >= 0);

		FFAT_DEBUG_MEM_PRINTF((_T("Free, Size:%d"), dwSize));

		return;
	}


	// memory check begin
	void
	ffat_com_debugMemCheckBegin(const char* psFuncName)
	{
		_stStatus.stMemStatus.dwMemCheck = _stStatus.stMemStatus.dwMemUsage;
		FFAT_STRNCPY(_stStatus.stMemStatus.psCurFuncName, psFuncName, sizeof(_stStatus.stMemStatus.psCurFuncName));
		return;
	}


	// end of memory check
	void
	ffat_com_debugMemCheckEnd(void)
	{
#ifdef FFAT_DYNAMIC_ALLOC
		// do not check memory when it is dynamic allocation.
		// some module may allocate memory for it's own data structure and not release it.
#else
		if (_stStatus.stMemStatus.dwMemCheck != _stStatus.stMemStatus.dwMemUsage)
		{
			FFAT_PRINT_DEBUG((_T("There is some memory leakage !!!")));
			FFAT_ASSERT(0);
		}
#endif
		return;
	}


	/**
	 * This function update memory allocation/free status
	 *
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_statusMemPrint(void)
	{
		FFAT_PRINT_DEBUG((_T("Memory Status =============== \n")));
		FFAT_PRINT_DEBUG((_T("dwMaxMemUsage        :%d \n"), _stStatus.stMemStatus.dwMaxMemUsage));
		FFAT_PRINT_DEBUG((_T("dwAllocCount         :%d \n"), _stStatus.stMemStatus.dwAllocCount));
		FFAT_PRINT_DEBUG((_T("dwFreeCount          :%d \n"), _stStatus.stMemStatus.dwFreeCount));
		FFAT_PRINT_DEBUG((_T("Max memory use func  :%s \n"), _stStatus.stMemStatus.psFuncName));
		return;
	}


	/**
	 * This function print function call status
	 *
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_printStatistics(void)
	{
		FFAT_PRINT_DEBUG((_T("============================================================\n")));
		FFAT_PRINT_DEBUG((_T("=======     COMMON        STATICS     =======================\n")));
		FFAT_PRINT_DEBUG((_T("============================================================\n")));

		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Get PSTACK Count: ", 		_STATISTIC()->dwGetPStack));
		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Release PSTACK Count: ", 	_STATISTIC()->dwReleasePStack));

		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Local Alloc Count: ", 	_STATISTIC()->dwLocalAlloc));
		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Local Free Count: ", 		_STATISTIC()->dwLocalFree));

		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Get Read Lock Count: ",	_STATISTIC()->dwGetReadLock));
		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Put Read Lock Count: ", 	_STATISTIC()->dwPutReadLock));

		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Get Write Lock Count: ",	_STATISTIC()->dwGetWriteLock));
		FFAT_PRINT_DEBUG((_T(" %30s %d\n"), "Put Write Lock Count: ",	_STATISTIC()->dwPutWriteLock));
		FFAT_PRINT_DEBUG((_T("============================================================\n")));

		_statusMemPrint();
	}
#endif
// debug end

//
//	End of filesystem status module
//
//======================================================================


// debug begin
//======================================================================
//
//	start of debug module
//

#ifdef FFAT_DEBUG
	// convert UNICODE string to ASCII(MULTIBYTE) string
	//	Max String Length is (FFAT_SYMLINK_MAX_PATH_LEN + 1) * 2
	t_int8*
	ffat_debug_w2a(t_wchar* psStr, void* pVol)
	{
		static t_int8		psTemp[(FFAT_SYMLINK_MAX_PATH_LEN + 1) * 2];

		FFAT_WCSTOMBS((char*)psTemp, sizeof(psTemp), psStr, (FFAT_WCSLEN(psStr) + 1), VI_DEV(VOL_VI((Vol*)pVol)));	// ignore error

		return psTemp;
	}

	// convert UNICODE string to ASCII(MULTIBYTE) string
	//	this function is used to printf 2 strings
	//	Max String Length is (FFAT_SYMLINK_MAX_PATH_LEN + 1) * 2
	t_int8*
	ffat_debug_w2a_2nd(t_wchar* psStr, void* pVol)
	{
		static t_int8	psTemp[(FFAT_SYMLINK_MAX_PATH_LEN + 1) * 2];

		FFAT_WCSTOMBS((char*)psTemp, sizeof(psTemp), psStr, (FFAT_WCSLEN(psStr) + 1), VI_DEV(VOL_VI((Vol*)pVol)));	// ignore error

		return psTemp;
	}

	/**
	* return a hex character for the byte
	* debug purpose
	*
	* @param		bValue		: [IN] value to be converted
	* @return		void
	* @author		DongYoung Seo
	* @version		NOV-03-2008 [DongYoung Seo] First Writing.
	*/
	static char
	_getHexChar(t_uint8 bValue)
	{
		FFAT_ASSERT(bValue <= 0x0F);

		if (bValue >= 0x0A)
		{
			return (bValue - 0x0A + 'A');
		}
		else
		{
			return (bValue + '0');
		}
	}

	/**
	* return hex string for the byte
	* debug purpose
	*
	* @param		bValue		: [IN] value to be converted
	* @return		void
	* @author		DongYoung Seo
	* @version		NOV-03-2008 [DongYoung Seo] First Writing.
	*/
	static char*
	_getHexString(t_uint8 bValue)
	{
		static char psStr[4];

		FFAT_MEMSET(psStr, 0x00, sizeof(psStr));

		// get 1st character
		psStr[0] = _getHexChar(bValue >> 4);
		psStr[1] = _getHexChar(bValue & 0x0F);

		return psStr;
	}

	/**
	* print a buffer for debug purpose
	*
	* @param		pBuffer		: [IN] buffer pointer to be printed
	* @param		dwOffset	: [IN] print start offset
	* @param		dwSize		: [IN] size of byte to be printed
	* @return		void
	* @author		DongYoung Seo
	* @version		JUN-01-2007 [DongYoung Seo] First Writing.
	*/
	FFatErr
	ffat_debug_printBuffer(t_int8* pBuffer, t_int32 dwOffset, t_int32 dwSize)
	{
		t_int32			i;
		t_int32			j;
		char			pPrintStr[64];		// temporary buffer

		FFAT_PRINT_VERBOSE((_T("memory dump begin (ptr/offset/size:0x%X, 0x%X, 0x%X=============="), (t_uint32)pBuffer, dwOffset, dwSize));

		for (i = dwOffset; i < (dwOffset + dwSize); /*None*/)
		{
			FFAT_MEMSET(pPrintStr, 0x00, sizeof(pBuffer));

			pPrintStr[0] = '0';
			pPrintStr[1] = 'x';

			if ((dwOffset + dwSize) >= 0x100)
			{
				FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), _getHexString((t_uint8)(i >> 8)));
			}

			FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), _getHexString((t_uint8)(i & 0xFF)));
			FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), ": ");

			for (j = 0; j < 16; j++)
			{
				if (((i + j) < dwOffset) || ((i + j) >= (dwOffset + dwSize)))
				{
					FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), "00 ");
				}
				else
				{
					// we must not use sprintf()
					FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), _getHexString(pBuffer[i + j]));
					FFAT_STRCPY((pPrintStr + FFAT_STRLEN(pPrintStr)), " ");
				}
			}

			i = ((i + j) & (~0x0F));

			FFAT_PRINT_VERBOSE((_T("%s"), pPrintStr));
		}

		FFAT_PRINT_VERBOSE((_T("memory dump End (ptr/offset/size:0xx%X, 0x%X, 0x%X=============="), (t_uint32)pBuffer, dwOffset, dwSize));

		return FFAT_OK;
	}
#endif


//
//	End of debug module
//
//======================================================================
// debug end

