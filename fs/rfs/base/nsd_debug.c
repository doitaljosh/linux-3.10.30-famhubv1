/**
 *     @mainpage   Nestle Layer
 *
 *     @section Intro
 *       TFS5 File system's VFS framework
 *   
 *     @MULTI_BEGIN@ @COPYRIGHT_DEFAULT
 *     @section Copyright COPYRIGHT_DEFAULT
 *            COPYRIGHT. SAMSUNG ELECTRONICS CO., LTD.
 *                                    ALL RIGHTS RESERVED
 *     Permission is hereby granted to licensees of Samsung Electronics Co., Ltd. products
 *     to use this computer program only in accordance 
 *     with the terms of the SAMSUNG FLASH MEMORY DRIVER SOFTWARE LICENSE AGREEMENT.
 *     @MULTI_END@
 *
 */

/** 
* @file		nsd_debug.c
* @brief	Define debug MACROs and functions
* @author	Kwon Moon Sang
* @date		2008/08/26
* @remark		
* @version	RFS_3.0.0_b047_RTM
* @note
*			26-AUG-2008 [Moonsang Kwon]: First writing
*			19-SEP-2008 [Moonsang Kwon]: Add new functions
*				1. _NsdGetLastError() returns the last error number.
*				2. _NsdGetThreadId() returns current thread's id.
*				3. _NsdLockConsole() gets lock for the console.
*				4. _NsdUnlockConsole() releases lock for the console.
*/

#include "nsd_debug.h"
#include "ns_oam_types.h"

#if defined(__arm)
	#include <errno.h>
#endif

#if (ENABLE_LINUX_KERNEL >= 1)
#else
	#include <stdio.h>
#endif

///< CONF: Set default Debug Zone Mask
static volatile unsigned int
gnNSD_DebugZoneMask = NSD_DZM_DEFAULT | NSD_DZM_TEST | NSD_DZM_GLUE | NSD_DZM_NESTLE | NSD_DZM_NATIVEFS;

/**
 * Return current global debug zone mask for this package.
 * @return	Return current NSD's debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark	This mask is used to determine whether to show the debug
 *			message or not.
 */
unsigned int
_NsdGetDebugZoneMask(void)
{
	// return current debug zone mask
	return gnNSD_DebugZoneMask;
}

/**
 * Set current global debug zone mask.
 * @param	[in] nNewNsdDebugZoneMask New NSD debug zone mask
 * @return	Return NSD's new debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark
 */
unsigned int
_NsdSetDebugZoneMask(unsigned int nNewNsdDebugZoneMask)
{
	// replace and return global debug zone mask
	gnNSD_DebugZoneMask = (nNewNsdDebugZoneMask | eNSD_DZM_ALWAYS);
	return gnNSD_DebugZoneMask;
}

/**
 * Find the last occurrence of tCh and return the pointer of it
 * @param	[in] ptszString	Input string from which find tCh
 * @param	[in] tCh		Character to find from ptszString
 * @return	
 * @par		ptszString Normally, it is the full path of the source file 
 * @par		tCh: Normally, it is the path separator
 * @author	Kwon Moon Sang
 * @version 2008/08/27 Initial version
 * @remark	__FILE__ represents the full path. So, printing full path makes 
 *          it difficult to analyze the log. This function just returns the
 *			starting pointer of the file name.
 */
PTCHAR	
_NsdGetStrRchrT(PTSTR ptszString, TCHAR tCh)
{
#if (ENABLE_UNIX >= 1)
	// For Linux, ptszString is just file name
	return ptszString;
#else

	PTCHAR ptCp = TEXT("NULL");	// temporary pointer
	
	while (TNULL != (*ptszString))
	{
		if ((*ptszString) == tCh)
		{
			// bingo! we got the TIFS
			ptCp = ptszString;
		}
		ptszString++;
	}

	// skip if the first char is TIFS('/') and next is not NULL.
	// IFS means internal field separator.
	if ((TIFS == (*ptCp)) && (TNULL != (*(ptCp + 1))))
	{
		// trim first '/'
		ptCp++;
	}
	return ptCp;
	
#endif

}

/**
 * When some critical error occurs, we call this endless loop function.
 * @return	None
 * @author	Kwon Moon Sang
 * @version 2008/09/09 Initial version.
 * @remark	Make a break point here so that when some critical error occurs,
 *          you can inspect the call stack, and so on.
 */
void
_NsdEnterEndlessLoop(void)
{
	unsigned int bContinue = 1;

	// temporary variables
	int i = 0;
	int nSum = 0;

	while (bContinue)
	{
		nSum += i++;	///< make a break point here for debugging
	}

	nSum = i - 1;	///< PC to exit from the above loop
}

/**
* When some assert occurs, we call this assert function.
* @return	None
* @author	Choi In Hwan
* @version 2008/09/16 Initial version.
* @remark	Make a break point here so that when some assert occurs,
*          you can inspect the call stack, and so on.
*/
void
_NsdEnterDebugBreak(void)
{
#if	ENABLE_NUCLEUS
	int			dwInput;

	OamPrintf("quit or continue? (q/c) : ");
	dwInput = OamGetChar();
	if (dwInput != 'q')
	{
		return;
	}
	_NsdEnterEndlessLoop();
#else
	OamDbgBreak();
#endif
}

/**
 * CONF: return current time in second.
 * @return	Current time in second.
 * @author	Kwon Moon Sang
 * @version 2008/09/09 Initial version
 * @remark	Return current time in second. You need to port this functioni for your programming environment.
 */
unsigned int
_NsdGetTimeSec(void)
{
	unsigned int	uTimeInSecond = 0;

	// For Win32
#if defined(_WIN32)
	unsigned int uTime = 0;

	// Get clock tick since system boot up.
	uTime = GetTickCount();

	uTimeInSecond = uTime / 1000;
#elif defined(__arm)
	// CONF: implement your own
#elif ENABLE_LINUX_KERNEL
	// noting
#elif ENABLE_SYMBIAN
	// noting
#elif ENABLE_RFS_TOOLS
	// noting
#elif ENABLE_NUCLEUS
	// noting
#else
	#error "You should define your own time function here!!!"
#endif

	return uTimeInSecond;
}

/**
 * Return latest error number.
 * @return	Latest error number.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version.
 * @remark	Return the latest error number.
 */
NSD_ERROR_TYPE
_NsdGetLastError(void)
{
#if defined(_WIN32) || (ENABLE_WINCE == 1)
	return GetLastError();
#elif ENABLE_LINUX_KERNEL
	return 0;
#elif ENABLE_SYMBIAN
	return 0;
#elif ENABLE_RFS_TOOLS
	return 0;
#elif ENABLE_NUCLEUS
	return 0;
#else
	#error "You should implement your own error number returning function!!!"
#endif
}

#if (ENABLE_UNICODE >= 1)

/** @fn const wchar_t* _NsdMbsToWcsC(const char* pszString);
 *	@brief Convert multi-byte string into wide character string
 *  @param pszString Multi-byte string
 *	@return Conversion result of pszString
 *	@remark This function is just to convert __FUNCTION__ to wide string
 *	
*/
const wchar_t* 
_NsdMbsToWcsC(const char* pszString)
{
    #define _NSD_MAXIMUM_STRING_LENTH   48
    
    static wchar_t pwszTmpBuffer[_NSD_MAXIMUM_STRING_LENTH + 2];

#if ENABLE_SYMBIAN
	SymRtlMbsToWcs(pwszTmpBuffer, pszString, _NSD_MAXIMUM_STRING_LENTH);
#else
	///< CONF: you should define your own string converter
    mbstowcs(pwszTmpBuffer, pszString, _NSD_MAXIMUM_STRING_LENTH);
#endif
    return pwszTmpBuffer;
}

/** @fn wchar_t* _NsdMbsToWcsC(const char* pszString);
 *	@brief Convert multi-byte string into wide character string
 *  @param pszString Multi-byte string
 *	@return Conversion result of pszString
 *	@remark This function is just to convert __FILE__ to wide string
 *	
*/
wchar_t* 
_NsdMbsToWcs(const char* pszString)
{
    #define _NSD_MAXIMUM_STRING_LENTH   48
    
    static wchar_t pwszTmpBuffer[_NSD_MAXIMUM_STRING_LENTH + 2];
    
#if ENABLE_SYMBIAN
	SymRtlWcsToMbs(pwszTmpBuffer, pszString, _NSD_MAXIMUM_STRING_LENTH);
#else
	///< CONF: you should define your own string converter
    mbstowcs(pwszTmpBuffer, pszString, _NSD_MAXIMUM_STRING_LENTH);
#endif

    return pwszTmpBuffer;
}
#endif

#if (ENABLE_MULTI_THREAD >= 1)

/**
 * CONF: Define semaphore for console access control.
 */

// For Win32
#if defined(_WIN32) || (ENABLE_WINCE == 1)
	static CRITICAL_SECTION	_csConsoleLock;

#elif ENABLE_LINUX_KERNEL
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
		#include <linux/semaphore.h>
	#else
		#include <asm/semaphore.h>
	#endif

	typedef struct semaphore RSEMAPHORE;
	static RSEMAPHORE 	Semaphore;

#else
	// Define your own semaphore structure here.
	#error "You should define your own static semaphore object here!!!"
#endif

/**
 * CONF: Implement console locking function.
 * @return	None.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version
 * @remark	Get the console lock. Implementation depends on your system.
 */
void 
_NsdLockConsole(void)
{
#if defined(_WIN32) || (ENABLE_WINCE == 1)
	static BOOL	bIsCreated = FALSE;
	if (!bIsCreated)
	{
		InitializeCriticalSection(&_csConsoleLock);
	}	
	EnterCriticalSection(&_csConsoleLock);

#elif ENABLE_LINUX_KERNEL
	static int bIsCreated = FALSE;
	if (!bIsCreated)
	{
		sema_init(&Semaphore, 1);
	}
	down(&Semaphore);
#else
	#error "You should define your own semaphore lock function here!!!"
#endif
	bIsCreated = TRUE;
	return;
}

/**
 * CONF: Implement console unlocking function.
 * @return	None.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version
 * @remark	Get the console lock. Implementation depends on your system.
 */
void 
_NsdUnlockConsole(void)
{
#if defined(_WIN32) || (ENABLE_WINCE == 1)
	LeaveCriticalSection(&_csConsoleLock);

#elif ENABLE_LINUX_KERNEL
	up(&Semaphore);
#else
	#error "You should define your own semaphore unlock function here!!!"
#endif
	return;
}

/**
 * CONF: Implement console unlocking function.
 * @return	Current thread id.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version
 * @remark	Get the console lock. Implementation depends on your system.
 */
THREAD_ID_TYPE 
_NsdGetThreadId(void)
{
	THREAD_ID_TYPE	uThreadId = 0;
#if defined(_WIN32) || (ENABLE_WINCE == 1)
	return GetCurrentThreadId();
#elif ENABLE_LINUX_KERNEL
	uThreadId = current->pid;
#else
	#error "You should define your own thread id returning function here!!!"
#endif
	return uThreadId;
}

#endif // end of #if (ENABLE_MULTI_THREAD >= 1)
