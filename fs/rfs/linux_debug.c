/*
 * RFS 3.0 Developed by Flash Software Group.
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
* @file		linux_debug.c
* @brief	Define debug MACROs and functions
* @author	Kwon Moon Sang
* @author	Hayoung Kim <hayeong.kim@samsung.com>
* @date		2008/08/26
* @remark		
* @version	RFS_3.0.0_b047_RTM
* @note
*			26-AUG-2008 [Moonsang Kwon]: First writing
*			19-SEP-2008 [Moonsang Kwon]: Add new functions
*				1. _RFSGetLastError() returns the last error number.
*				2. _RFSGetThreadId() returns current thread's id.
*				3. _RFSLockConsole() gets lock for the console.
*				4. _RFSUnlockConsole() releases lock for the console.
*			15-JUN-2009 [Hayoung Kim]: define EXPORT_SYMBOL
*			15-JUN-2009 [Hayoung Kim]: Remove following functions
*				1. _RFSGetLastError() returns the last error number
*				2. Functions for conversing UNICODE string
*					:_RFSMbsToWcsC(), _RFSMbsToWcs()
*/

#include "linux_debug.h"
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
gnRFS_DebugZoneMask = RFS_DZM_DEFAULT | RFS_DZM_TEST | RFS_DZM_GLUE | RFS_DZM_NESTLE | RFS_DZM_NATIVEFS;

/**
 * Return current global debug zone mask for this package.
 * @return	Return current RFS's debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark	This mask is used to determine whether to show the debug
 *			message or not.
 */
unsigned int
_RFSGetDebugZoneMask(void)
{
	// return current debug zone mask
	//return (gnRFS_DebugZoneMask & ~(eRFS_DZM_OAM | eRFS_DZM_BCACHE));
	//return (eRFS_DZM_VOL | eRFS_DZM_FILE | eRFS_DZM_VNODE);

#ifndef RFS_RELEASE

#ifdef CONFIG_RFS_FS_DEBUG_MSG_VOL
	gnRFS_DebugZoneMask |= eRFS_DZM_VOL;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_VOL;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_VNODE
	gnRFS_DebugZoneMask |= eRFS_DZM_VNODE;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_VNODE;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_DIR
	gnRFS_DebugZoneMask |= eRFS_DZM_DIR;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_DIR;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_FILE
	gnRFS_DebugZoneMask |= eRFS_DZM_FILE;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_FILE;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_BCACHE
	gnRFS_DebugZoneMask |= eRFS_DZM_BCACHE;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_BCACHE;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_XATTR
	gnRFS_DebugZoneMask |= eRFS_DZM_XATTR;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_XATTR;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_OAM
	gnRFS_DebugZoneMask |= eRFS_DZM_OAM;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_OAM;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_DEV
	gnRFS_DebugZoneMask |= eRFS_DZM_DEV;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_DEV;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_API
	gnRFS_DebugZoneMask |= eRFS_DZM_API;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_API;
#endif
#ifdef CONFIG_RFS_FS_DEBUG_MSG_BASE
	gnRFS_DebugZoneMask |= eRFS_DZM_BASE;
#else
	gnRFS_DebugZoneMask &= ~eRFS_DZM_BASE;
#endif

#endif /* ifndef RFS_RELEASE */

	return gnRFS_DebugZoneMask;

}

EXPORT_SYMBOL(_RFSGetDebugZoneMask);

/**
 * Set current global debug zone mask.
 * @param	[in] nNewRFSDebugZoneMask New RFS debug zone mask
 * @return	Return RFS's new debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark
 */
unsigned int
_RFSSetDebugZoneMask(unsigned int nNewRFSDebugZoneMask)
{
	// replace and return global debug zone mask
	gnRFS_DebugZoneMask = (nNewRFSDebugZoneMask | eRFS_DZM_ALWAYS);
	return gnRFS_DebugZoneMask;
}

#if (ENABLE_UNIX < 1)
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
_RFSGetStrRchrT(PTSTR ptszString, TCHAR tCh)
{
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
}
#endif

/**
 * When some critical error occurs, we call this endless loop function.
 * @return	None
 * @author	Kwon Moon Sang
 * @version 2008/09/09 Initial version.
 * @remark	Make a break point here so that when some critical error occurs,
 *          you can inspect the call stack, and so on.
 */
void
_RFSEnterEndlessLoop(void)
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
_RFSEnterDebugBreak(void)
{
	OamDbgBreak();
}
EXPORT_SYMBOL(_RFSEnterDebugBreak);

/**
 * CONF: return current time in second.
 * @return	Current time in second.
 * @author	Kwon Moon Sang
 * @version 2008/09/09 Initial version
 * @remark	Return current time in second. You need to port this functioni for your programming environment.
 */
#include <linux/time.h>
unsigned int
_RFSGetTimeSec(void)
{
	unsigned int	uTimeInSecond = 0;

	uTimeInSecond = (unsigned int) get_seconds();

	return uTimeInSecond;
}
EXPORT_SYMBOL(_RFSGetTimeSec);

#if (ENABLE_MULTI_THREAD >= 1)

/**
 * CONF: Define semaphore for console access control.
 */

#if ENABLE_LINUX_KERNEL
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
_RFSLockConsole(void)
{
#if ENABLE_LINUX_KERNEL
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

EXPORT_SYMBOL(_RFSLockConsole);

/**
 * CONF: Implement console unlocking function.
 * @return	None.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version
 * @remark	Get the console lock. Implementation depends on your system.
 */
void 
_RFSUnlockConsole(void)
{
#if ENABLE_LINUX_KERNEL
	up(&Semaphore);
#else
	#error "You should define your own semaphore unlock function here!!!"
#endif
	return;
}

EXPORT_SYMBOL(_RFSUnlockConsole);

/**
 * CONF: Implement console unlocking function.
 * @return	Current thread id.
 * @author	Kwon Moon Sang
 * @version 2008/09/19 Initial version
 * @remark	Get the console lock. Implementation depends on your system.
 */
THREAD_ID_TYPE 
_RFSGetThreadId(void)
{
#if ENABLE_LINUX_KERNEL
	return current->pid;
#else
	#error "You should define your own thread id returning function here!!!"
	return 0;
#endif
}
EXPORT_SYMBOL(_RFSGetThreadId);

#endif // end of #if (ENABLE_MULTI_THREAD >= 1)
