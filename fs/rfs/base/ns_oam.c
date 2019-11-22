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
 * @brief	Implementation of the run-time library routines for oam interfaces.
 * @author	ByungJune Song (byungjune.song@samsung.com)
 * @author	InHwan Choi (inhwan.choi@samsung.com)
 * @file	ns_oam.c
 */

#include "ns_misc.h"


/******************************************************************************/
/* DEFINE                                                                     */
/******************************************************************************/
#define NSD_FILE_ZONE_MASK		(eNSD_DZM_BASE)

#ifdef TFS5_DEBUG
	#define DBG_SEM
#endif


/******************************************************************************/
/* Task Management                                                            */
/******************************************************************************/
/** @brief RtlGetCurrentTaskId function returns the Task Id 
*	@return	void
*/
unsigned int
RtlGetCurrentTaskId(void)
{
	return OamGetCurrentTaskId();
}

/******************************************************************************/
/* USER ID                                                                    */
/******************************************************************************/
/** @brief	OamGetUid function returns the user id in linux 
*	@return	UID	- stored in vnode
*/
unsigned int
RtlGetUid(void)
{
	return OamGetUid();
}

/** @brief OamGetGid function returns the group id in linux 
*	@return	GID	- stored in vnode
*/
unsigned int
RtlGetGid(void)
{
	return OamGetGid();
}

/******************************************************************************/
/* TASK SYNCHRONIZATION                                                       */
/******************************************************************************/
#if defined(CONFIG_RTOS) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)
	#ifdef TFS5_DEBUG
	static int g_nRwSemaphore	= 0;
	#endif /*TFS5_DEBUG*/
#endif /*defined(CONFIG_RTOS) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)*/

/** @brief		RtlCreateGate function creates and returns a Gate object. 
*	@param[in]	pGate	
*	@param[in]	dwCount	
*	@return		unsigned int	1 : Success, 0 : FAIL
*/
unsigned int
RtlCreateGate(
	IN GATE*			pGate,
	IN unsigned int		dwCount)
{
	NSD_AS(pGate);

	pGate->pSem = RtlAllocMem(sizeof(OAM_SEMAPHORE));
	RtlFillMem(pGate->pSem, 0, sizeof(OAM_SEMAPHORE));

#ifdef TFS5_DEBUG
	pGate->bLocked	= FALSE;
	pGate->dwOwnerId = OAM_INVALID_TASK_ID;
#endif

	return	OamCreateSemaphore(pGate->pSem, dwCount);
}


/** @brief RtlDestroyGate function destroys the Gate and frees it from the system. 
*	@param[in]	pGate	
*	@return		void
*/
void
RtlDestroyGate(
	IN GATE*	pGate)
{
	NSD_AS(pGate);

#ifdef TFS5_DEBUG
	pGate->bLocked	= FALSE;
	pGate->dwOwnerId = OAM_INVALID_TASK_ID;
#endif

	OamDestroySemaphore(pGate->pSem);
	RtlFreeMem(pGate->pSem);
}


/** @brief RtlEnterGate function gets an exclusive rights to the Gate. 
*	@param[in]	pGate	
*	@return		void
*/
void
RtlEnterGate(
	IN GATE*	pGate)
{
	NSD_AS(pGate);

	OamGetSemaphore(pGate->pSem);
	#ifdef TFS5_DEBUG
	pGate->bLocked = TRUE;
	pGate->dwOwnerId = RtlGetCurrentTaskId();
#ifdef DBG_SEM
	NSD_VMZ((_T("Get Gate --> Current SemID [%d], bLocked [%d]\n"), pGate->dwOwnerId, pGate->bLocked));
	NSD_VMZ((_T("Current Task ID : %d\n"), RtlGetCurrentTaskId()));
#endif
#endif // TFS5_DEBUG

}


/** @brief RtlExitGate function releases the Gate. 
*	@param[in]	pGate	
*	@return		void
*/
void
RtlExitGate(
	IN GATE*	pGate)
{
	NSD_AS(pGate);

#ifdef DBG_SEM
	NSD_VMZ((_T("Put Gate [before] --> Current SemID [%d], bLocked [%d]\n"), pGate->dwOwnerId, pGate->bLocked));
#endif
#ifdef TFS5_DEBUG
	pGate->bLocked = FALSE;
	pGate->dwOwnerId = OAM_INVALID_TASK_ID;
#endif
	OamPutSemaphore(pGate->pSem);
#ifdef DBG_SEM
	NSD_VMZ((_T("Put Gate [after] --> Current SemID [%d], bLocked [%d]\n"), pGate->dwOwnerId, pGate->bLocked));
#endif
}


/** @brief		RtlCreateSemaphore function creates and returns a semaphore object. 
*	@param[in]	pSemaphore	
*	@param[in]	dwCount	
*	@return		unsigned int	1 : Success, 0 : FAIL
*/
unsigned int
RtlCreateSemaphore(
	IN SEMAPHORE*		pSemaphore,
	IN unsigned int		dwCount)
{
	NSD_AS(pSemaphore);
	
	pSemaphore->pSem = RtlAllocMem(sizeof(OAM_SEMAPHORE));
	RtlFillMem(pSemaphore->pSem, 0, sizeof(OAM_SEMAPHORE));

#ifdef TFS5_DEBUG
	pSemaphore->bLocked	= FALSE;
	pSemaphore->dwOwnerId = OAM_INVALID_TASK_ID;
#endif

	return	OamCreateSemaphore(pSemaphore->pSem, dwCount);
}


/** @brief RtlDestroySemaphore function destroys the semaphore and frees it from the system. 
*	@param[in]	pSemaphore	
*	@return		void
*/
void
RtlDestroySemaphore(
	IN SEMAPHORE*	pSemaphore)
{
	NSD_AS(pSemaphore);

#ifdef TFS5_DEBUG
	pSemaphore->bLocked	= FALSE;
	pSemaphore->dwOwnerId = OAM_INVALID_TASK_ID;
#endif

	OamDestroySemaphore(pSemaphore->pSem);
	RtlFreeMem(pSemaphore->pSem);
}


/** @brief RtlGetSemaphore function gets an exclusive rights to the semaphore. 
*	@param[in]	pSemaphore	
*	@return		void
*/
unsigned int
RtlGetSemaphore(
	IN SEMAPHORE*	pSemaphore)
{
	unsigned int dwRet;

	NSD_AS(pSemaphore);

	dwRet = OamGetSemaphore(pSemaphore->pSem);

#ifdef TFS5_DEBUG
	pSemaphore->bLocked = TRUE;
	pSemaphore->dwOwnerId = RtlGetCurrentTaskId();
#ifdef DBG_SEM
	NSD_VMZ((_T("Get Semaphore --> Current SemID [%d], bLocked [%d]\n"), pSemaphore->dwOwnerId, pSemaphore->bLocked));
	NSD_VMZ((_T("Current Task ID : %d\n"), RtlGetCurrentTaskId()));
#endif
#endif // TFS5_DEBUG

	return dwRet;
}


/** @brief RtlTryGetSemaphore function tries to get semaphore and returns immediately regardless of whether or not the request can be satisfied. 
*	@param[in]	pSemaphore	
*	@return		void
*/
unsigned int
RtlTryGetSemaphore(
	IN SEMAPHORE*	pSemaphore)
{
	unsigned int result;

	NSD_AS(pSemaphore);

	result = OamTryGetSemaphore(pSemaphore->pSem);
#ifdef TFS5_DEBUG
	if (result)
	{
		pSemaphore->bLocked = TRUE;
		pSemaphore->dwOwnerId = RtlGetCurrentTaskId();
	}
#endif
	return result;
}


/** @brief RtlPutSemaphore function releases the semaphore. 
*	@param[in]	pSemaphore	
*	@return		void
*/
unsigned int
RtlPutSemaphore(
	IN SEMAPHORE*	pSemaphore)
{
	unsigned int dwRet;
	NSD_AS(pSemaphore);

#ifdef DBG_SEM
	NSD_VMZ((_T("Put Semaphore [before] --> Current SemID [%d], bLocked [%d]\n"), pSemaphore->dwOwnerId, pSemaphore->bLocked));
#endif
#ifdef TFS5_DEBUG
	pSemaphore->bLocked = FALSE;
	pSemaphore->dwOwnerId = OAM_INVALID_TASK_ID;
#endif

	dwRet = OamPutSemaphore(pSemaphore->pSem);

#ifdef DBG_SEM
	NSD_VMZ((_T("Put Semaphore [after] --> Current SemID [%d], bLocked [%d]\n"), pSemaphore->dwOwnerId, pSemaphore->bLocked));
#endif

	return dwRet;
}


#ifdef TFS5_DEBUG

	BOOL
	RtlOwnSemaphore(
		IN SEMAPHORE*	pSemaphore)
	{
		#ifdef DBG_SEM
			NSD_VMZ((_T("Check if the object is locked - RtlOwnSemaphore")));
			NSD_VMZ((_T("pSemaphore->bLocked : %d\n"), pSemaphore->bLocked));
			NSD_VMZ((_T("pSemaphore->dwOwnerId : %d\n"), pSemaphore->dwOwnerId));
		#endif
		if (pSemaphore->bLocked)
		{	
		#ifdef DBG_SEM
				NSD_VMZ((_T("Semaphore is preempted by [%d] task, and Current Task Id : %d\n"), pSemaphore->dwOwnerId, RtlGetCurrentTaskId()));
		#endif
			return pSemaphore->dwOwnerId == RtlGetCurrentTaskId();
		}

		return FALSE;
	}

	BOOL
	RtlIsLocked(
		IN SEMAPHORE* pSemaphore)
	{
		return pSemaphore->bLocked;
	}

#endif /*TFS5_DEBUG*/


#if defined(CONFIG_RTOS) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)
/** @brief		creates and returns a rw semaphore object. 
*	@param[in]	pRwSemaphore	
*	@return		unsigned int	1 : Success, 0 : FAIL
*/
unsigned int
RtlCreateRwSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
	if (!RtlCreateSemaphore(&pRwSemaphore->bsRW, 1))
	{
		return FALSE;
	}

	if (!RtlCreateSemaphore(&pRwSemaphore->bsPreemption, 1))
	{
		RtlDestroySemaphore(&pRwSemaphore->bsRW);
		return FALSE;
	}

	pRwSemaphore->dwReadCount = 0;

#ifdef TFS5_DEBUG
	g_nRwSemaphore++;
	pRwSemaphore->bWriteLocked = FALSE;
	pRwSemaphore->dwWriteOwnerId = OAM_INVALID_TASK_ID;
#endif

	return TRUE;
}


/** @brief		destroy a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		void
 */
void
RtlDestroyRwSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
	RtlDestroySemaphore(&pRwSemaphore->bsRW);
	RtlDestroySemaphore(&pRwSemaphore->bsPreemption);

#ifdef TFS5_DEBUG
	g_nRwSemaphore--;
	pRwSemaphore->bWriteLocked = FALSE;
	pRwSemaphore->dwWriteOwnerId = OAM_INVALID_TASK_ID;
#endif
}


/** @brief		read down a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		void
 */
void
RtlGetReadSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
	RtlGetSemaphore(&pRwSemaphore->bsPreemption);

	if (pRwSemaphore->dwReadCount == 0)
	{
		RtlGetSemaphore(&pRwSemaphore->bsRW);
	}

	pRwSemaphore->dwReadCount++;

	RtlPutSemaphore(&pRwSemaphore->bsPreemption);
}


/** @brief		read up a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		void
 */
void
RtlPutReadSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
	RtlGetSemaphore(&pRwSemaphore->bsPreemption);
	
	pRwSemaphore->dwReadCount--;
	if (pRwSemaphore->dwReadCount == 0)
	{
		RtlPutSemaphore(&pRwSemaphore->bsRW);
	}

	RtlPutSemaphore(&pRwSemaphore->bsPreemption);
}


/** @brief		write down a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		void
 */
void
RtlGetWriteSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
#ifdef TFS5_DEBUG
	pRwSemaphore->bWriteLocked = TRUE;
	pRwSemaphore->dwWriteOwnerId = RtlGetCurrentTaskId();
#endif

	RtlGetSemaphore(&pRwSemaphore->bsRW);
}


/** @brief		try to write down a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		result
 */
unsigned int
RtlTryGetWriteSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{

	unsigned int dwRes;

	dwRes = RtlTryGetSemaphore(&pRwSemaphore->bsRW);
#ifdef TFS5_DEBUG
	if (dwRes)
	{
		pRwSemaphore->bWriteLocked = TRUE;
		pRwSemaphore->dwWriteOwnerId = RtlGetCurrentTaskId();
	}
#endif
	return dwRes;
}


/** @brief		write up a rw semaphore object. 
 *	@param[in]	pRwSemaphore	
 *	@return		void
 */
void
RtlPutWriteSemaphore(
	IN RW_SEMAPHORE*	pRwSemaphore)
{
#ifdef TFS5_DEBUG
	pRwSemaphore->bWriteLocked = FALSE;
	pRwSemaphore->dwWriteOwnerId = OAM_INVALID_TASK_ID;
#endif

	RtlPutSemaphore(&pRwSemaphore->bsRW);
}


#ifdef TFS5_DEBUG
BOOL
RtlOwnWriteSemaphore(
	IN RW_SEMAPHORE* pRwSemaphore)
{
	if (pRwSemaphore->bWriteLocked)
	{
		return pRwSemaphore->dwWriteOwnerId == RtlGetCurrentTaskId();
	}

	return FALSE;
}
#endif /*TFS5_DEBUG*/

#endif /*defined(CONFIG_RTOS) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)*/


/** @brief OamAtomicIncrement function increments the value of the variable
 * passed as the argument and returns the its value incremented. 
 *	@param[in]		pValue	
 *  @return			result
 */
unsigned int
RtlAtomicIncrement(
	IN volatile unsigned int* pValue)
{
	NSD_AS(pValue);
	return OamAtomicIncrement(pValue);
}


/** @brief OamAtomicDecrement function decrements the value of the variable
 * passed as the argument and returns the its value incremented.
 *	@param[in]		pValue	
 *  @return			result
 */
unsigned int
RtlAtomicDecrement(
	IN volatile unsigned int* pValue)
{
	NSD_AS(pValue);
	return OamAtomicDecrement(pValue);
}


/******************************************************************************/
/* Time																		  */
/******************************************************************************/
/** @brief		RtlGetCurSystim function retrieves the current local date and time.
*	@param[out]	pTime	Pointer to a SYSTEMTIME structure to receive the current local date and time. 
*	@return		void
*/
void
RtlGetCurSysTime(
	OUT	PSYS_TIME pTime)
{
	OAM_TIME	SysTime;
	
	NSD_AS(pTime);
	OamGetSystemTime(&SysTime);
	
	pTime->wYear			= SysTime.wYear;
	pTime->wMonth			= SysTime.wMonth;
	pTime->wDay				= SysTime.wDay;
	pTime->wDayOfWeek		= SysTime.wDayOfWeek;
	pTime->wHour			= SysTime.wHour;
	pTime->wMinute			= SysTime.wMinute;
	pTime->wSecond			= SysTime.wSecond;
	pTime->wMilliseconds	= SysTime.wMilliseconds;
}


/** @brief		RtlSysTimeToCompTime function convert SYS_TIME type to COMP_TIME type
*	@param[in]	pSysTime	Pointer to a SYSTEMTIME structure
*	@param[out]	pCompTime	Pointer to a COMPTIME structure
*	@return		void
*/
void
RtlSysTimeToCompTime(
	 IN		PSYS_TIME		pSysTime,
	 OUT	PCOMP_TIMESPEC	pCompTime)
{
	NSD_AS( pSysTime );
	NSD_AS( pCompTime );
	
	pCompTime->dwDate = MAKE_DATE( pSysTime->wYear, pSysTime->wMonth, pSysTime->wDay );
	pCompTime->dwTime = MAKE_TIME( pSysTime->wHour, pSysTime->wMinute, pSysTime->wSecond, pSysTime->wMilliseconds );
}


/** @brief		RtlCompTimeToSysTime function convert COMP_TIME type to SYS_TIME type 
*	@param[in]	pCompTime	Pointer to a COMPTIME structure
*	@param[in]	wMilliSec	Number of milliseconds
*	@param[out]	pSysTime	Pointer to a SYSTEMTIME structure
*	@return		void
*/
void
RtlCompTimeToSysTime(
	 IN		PCOMP_TIMESPEC		pCompTime,
	 OUT	PSYS_TIME			pSysTime)
{
	NSD_AS(pCompTime);
	NSD_AS(pSysTime);

	pSysTime->wHour				= (unsigned short)TIME_HOUR(pCompTime->dwTime);
	pSysTime->wMinute			= (unsigned short)TIME_MIN(pCompTime->dwTime);
	pSysTime->wSecond			= (unsigned short)TIME_SEC(pCompTime->dwTime);
	pSysTime->wMilliseconds		= (unsigned short)TIME_MILISEC(pCompTime->dwTime);

	pSysTime->wYear				= (unsigned short)DATE_YEAR(pCompTime->dwDate);
	pSysTime->wMonth			= (unsigned short)DATE_MONTH(pCompTime->dwDate);
	pSysTime->wDay				= (unsigned short)DATE_DAY(pCompTime->dwDate);
	pSysTime->wDayOfWeek		= (unsigned short)0;
}


/** @brief		RtlGetMilliSec function gets current time in millisec
 *  @return		milli sec.
 */
unsigned int
RtlGetMilliSec(void)
{
	return OamGetMilliSec();
}

/**
 * @brief		Get UTC minute from GMT (e.g. Korea +540 (min))
 * @return		UTC minute from GMT
 */
int
RtlGetOffsetFromUtc(void)
{
	return OamGetOffsetFromUtc();
}

/******************************************************************************/
/* Memory																	  */
/******************************************************************************/

/** @brief		Copies bytes between buffers
*	@param[out]	pDest	New Buffer
*	@param[in]	pSrc	Buffer to copy from
*	@param[in]	dwBytes	Number of charaters to copy
*	@return		The value of dest
*/
void
RtlCopyMem(
	OUT	void*			pDest,
	IN	const void*		pSrc,
	IN	unsigned int	dwBytes)
{
	NSD_AS(pDest);
	OamCopyMemory(pDest, pSrc, dwBytes);
}


/** @brief		Sets buffers to a specified character. 
*	@param[out]	pDest	New Buffer
*	@param[in]	value	Buffer to copy from
*	@param[in]	dwBytes	Number of charaters to copy
*	@return		The value of dest
*/
void
RtlFillMem(
	OUT	void*			pDest,
	IN	unsigned char	value,
	IN	unsigned int	dwBytes)
{
	NSD_AS(pDest);
	OamFillMemory(pDest, value, dwBytes);
}


/** @brief		Compare characters in two buffers.
*	@param[in]	pBuf1	First buffer.
*	@param[in]	pBuf2	Second buffer.
*	@param[in]	dwBytes	Number of characters 
*	@return		The return value indicates the relationship between the buffers.
*	@par output
*	@li		returned value < 0 : buf1 less than buf2
*	@li		returned value = 0 : buf1 identical to buf2
*	@li		returned value > 0 : buf1 greater than buf2
*/
int
RtlCmpMem(
	IN const void*		pBuf1,
	IN const void*		pBuf2,
	IN unsigned int			dwBytes)
{
	NSD_AS(pBuf1);
	NSD_AS(pBuf2);
	return OamCompareMemory(pBuf1, pBuf2, dwBytes);
}


/** @brief		Initialize the memory 
 *  @return		result 
 */
unsigned int
RtlInitMemory(void)
{
	return OamInitMemory();
}


/** @brief		Terminate the memory 
 *  @return		result 
 */
unsigned int
RtlTerminateMemory(void)
{
	return OamTerminateMemory();
}
