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
 * @file		oam_linux.c
 * @brief		This file includes OS abstracion APIs.
 * @version		RFS_3.0.0_b047_RTM
 * @see			none
 * @author		hayeong.kim@samsung.com
 */

#include "ns_oam_defs.h"
#include "linux_util.h"
#include "ess_debug.h"
#include "rfs_linux.h"

#include <linux/slab.h>
#include <linux/vmalloc.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_OAM)

#ifndef TRUE
#define TRUE	(1L)
#endif

#ifndef FALSE
#define FALSE	(0L)
#endif


/* local variable definition */
static const unsigned int g_dwDays[] =
{
	0,		31,		59,		90,		120,	151,	181,	212,
	243,	273,	304,	334,	0,		0,		0,		0
};

#define SEC_PER_MIN			(60)            /* 60 secs / min */
#define MIN_PER_HR			(60)            /* 60 mins / hour */
#define SEC_PER_HR			(3600)          /* 3600 secs / hour */
#define HR_PER_DAY			(24)            /* 24 hours / day */
#define DAY_PER_YR			(365)           /* 365 days / year */
#define SEC_PER_DAY			(60 * 60 * 24)  /* 86400 secs / day */
#define DAY_PER_10YR		(365 * 10 + 2)  /* 3650 days / 10years */
#define SEC_PER_10YR		DAY_PER_10YR * SEC_PER_DAY      /* 10 years -> 315532800 secs */
//#define MIN_DATE                SEC_PER_10YR
#define MIN_DATE			0

#define LEAP_DAYS(yr)   ((yr + 3) >> 2)  /* leap-year days during yr years */
#define LEAP_YEAR(yr)   ((yr & 3) == 0) /* true if yr is the leap year */

/******************************************************************************/
/* Internal Function                                                          */
/******************************************************************************/
/**
 * convert linux time (sec) to OAM time
 *
 * @param pLxTime       [in] linux time
 * @param pTime         [out] OAM time structure
 * @return      void
 * linux time start from 1970.01.01. But Linux glue handles times after 1980.01.01
 */
void
_LinuxTimeToOamTime(
	IN	PLINUX_TIMESPEC		pLxTime,
	OUT	POAM_TIME			pTime)
{
	LINUX_TIME		dwTimeSec;
	unsigned int	dwDay = 0;
	unsigned int	dwMonth = 0;
	unsigned int	dwYear = 0;
	u64				ns = 0;

	extern struct timezone sys_tz;

	dwTimeSec = pLxTime->tv_sec;

	/* set to GMT time */
	dwTimeSec -= (sys_tz.tz_minuteswest * SEC_PER_MIN);

	/* start from 1980 */
	/*if (dwTimeSec < MIN_DATE)
		dwTimeSec = MIN_DATE;*/

	/* set value in milli seconds */
	pTime->wMilliseconds = 0;
	ns = pLxTime->tv_nsec;

	while (unlikely(ns >= NSEC_PER_MSEC))
	{
		ns -= NSEC_PER_MSEC;
		pTime->wMilliseconds++;
	}

	/* set the minimum value of date */
	pTime->wSecond = (unsigned short) (dwTimeSec % SEC_PER_MIN);
	pTime->wMinute = (unsigned short) ((dwTimeSec / SEC_PER_MIN) % MIN_PER_HR);
	pTime->wHour = (unsigned short) ((dwTimeSec / SEC_PER_HR) % HR_PER_DAY);

	/* get the days & years */
	dwDay = (unsigned int) ((dwTimeSec - MIN_DATE) / SEC_PER_DAY); /* start from 1970 */
	dwYear = (unsigned int) (dwDay / DAY_PER_YR);

	/* re-organize the year & day by the leap-years */
	if ((LEAP_DAYS(dwYear) + (dwYear * DAY_PER_YR)) > dwDay)
	{
		dwYear--;
	}
	dwDay -= (LEAP_DAYS(dwYear) + (dwYear * DAY_PER_YR));

	/* find the month & day */
	if ((dwDay == g_dwDays[2]) && LEAP_YEAR(dwYear))
	{
		dwMonth = 2;
	}
	else
	{
		if (LEAP_YEAR(dwYear) && (dwDay > g_dwDays[2]))
		{
			dwDay--;
		}
		for (dwMonth = 0; dwMonth < 12; dwMonth++)
		{
			if (g_dwDays[dwMonth] > dwDay)
			{
				break;
			}
		}
	}

	pTime->wDay = dwDay - g_dwDays[dwMonth - 1] + 1;
	pTime->wMonth = dwMonth;
	pTime->wYear = dwYear + 1970; /* NativeFS time starts from 1970.1.1 */

	RFS_VMZC(0,
			("CurTime - year:%d month:%d day:%d hour:%d min:%d sec:%d",
			pTime->wYear, pTime->wMonth, pTime->wDay,
			pTime->wHour, pTime->wMinute, pTime->wSecond));
	 
	return;
}


/******************************************************************************/
/* TASK MANAGEMENT                                                            */
/******************************************************************************/

/**
 * @brief OamGetCurrentTaskId function returns the Task Id
 */
unsigned int
OamGetCurrentTaskId(void)
{
	return LINUX_g_CurTask->pid;
}


/**
 * @brief OamGetSystemTime function returns the system time
 */
void
OamGetSystemTime(
	OUT POAM_TIME		pTime)
{
	struct timespec stLxTime = CURRENT_TIME;

	RFS_ASSERT(pTime);

	_LinuxTimeToOamTime(&stLxTime, pTime);

	return;
}


/**
 * @brief Get current time in milli-seconds
 */
unsigned int
OamGetMilliSec(void)
{
	struct timespec stLxTime = CURRENT_TIME;
	return (unsigned int) stLxTime.tv_sec;
}

/******************************************************************************/
/* USER ID                                                                    */
/******************************************************************************/
/**
 * @brief OamGetUid function returns the user id in linux
 */
unsigned int
OamGetUid(void)
{
	return LINUX_g_CurUid;
}

/**
 * @brief OamGetGid function returns the group id in linux 
 */
unsigned int
OamGetGid(void)
{
	return LINUX_g_CurGid;
}


/******************************************************************************/
/* TASK SYNCHRONIZATION                                                       */
/******************************************************************************/
/**
 * @brief OamCreateSemaphore function creates and returns a semaphore object.
 */
unsigned int
OamCreateSemaphore(
	IN OAM_SEMAPHORE*	pSemaphore,
	IN unsigned int		dwCount)
{
	RFS_ASSERT(pSemaphore);

	sema_init(pSemaphore, dwCount);

	return TRUE;
}


/**
 * @brief OamDestroySemaphore function destroys the semaphore
 * and frees it from the system.
 */
void
OamDestroySemaphore(
	IN OAM_SEMAPHORE* pSemaphore)
{
	/*  do nothing in linux */
	return;
}


/**
 * @brief OamGetSemaphore function gets an exclusive rights to the semaphore.
 */
int
OamGetSemaphore(
	IN OAM_SEMAPHORE* pSemaphore)
{
	RFS_ASSERT(pSemaphore);

	down(pSemaphore);

	return 0;
}


/**
 * @brief OamTryGetSemaphore function tries to get semaphore
 * and returns immediately
 * regardless of whether or not the request can be satisfied.
 */
unsigned int
OamTryGetSemaphore(
	IN OAM_SEMAPHORE* pSemaphore)
{
	int	result;
	RFS_ASSERT(pSemaphore);

	result = down_trylock(pSemaphore);
	if (!result)
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}


/**
 * @brief OamPutSemaphore function releases the semaphore.
 */
int
OamPutSemaphore(
	IN OAM_SEMAPHORE* pSemaphore)
{
	RFS_ASSERT(pSemaphore);

	up(pSemaphore);

	return 0;
}

/******************************************************************************/
/* MISCELLANEOUS RUNTIME LIBRARY                                              */
/******************************************************************************/
/* OamCopyMemory function copies specified amount of data from pSrc to pDest. */
void
OamCopyMemory(
	IN void*			pDest,
	IN const void*		pSrc,
	IN unsigned int		dwBytes)
{
	memcpy(pDest, pSrc, (size_t) dwBytes);
}

/* OamFillMemory function fills memory spaces with a specified value. */
void
OamFillMemory(
	IN void*			pDest,
	IN unsigned char	value,
	IN unsigned int		dwBytes)
{
	memset(pDest, (int) value, (size_t) dwBytes);
}

/* OamCompareMemory function compare memory spaces with a specified value. */
int
OamCompareMemory(
	IN const void*		pBuf1,
	IN const void*		pBuf2,
	IN unsigned int		dwBytes)
{
	return memcmp(pBuf1, pBuf2, (size_t) dwBytes);
}

unsigned int	
OamInitMemory(void)
{
	/* This is useless function for linux */
	return TRUE;
}

unsigned int
OamTerminateMemory(void)
{
	/* This is useless function for linux */
	return TRUE;
}

#define SIZE 0x8000 
void*
OamDbgAllocateMemory(
	IN unsigned int		dwSize,
	IN const char*		filename,
	IN unsigned int		lineno)
{
	void *nPtr;
	RFS_DMZC(1, ("[kmalloc] allocate memory of %u", dwSize));

	if( dwSize > SIZE ) 
		nPtr = vmalloc(dwSize);
	else
		nPtr = kmalloc((size_t) dwSize, GFP_NOFS);
	
	if( nPtr == NULL )
	{
		printk( "OamDbgAllocateMemory() allocation fail : ");
		if( dwSize > SIZE )
			printk( "vmalloc case....\n");	
		else
			printk( "kmalloc case....\n");	
	}
	
	return nPtr;
}

void
OamDbgFreeMemory(
	IN void*			pMemory)
{ 
	RFS_ASSERT(pMemory);

	RFS_DMZC(1, ("[kfree] release memory of %p", pMemory));

	if (((u32)pMemory >= VMALLOC_START) && ((u32)pMemory <= VMALLOC_END))
		vfree(pMemory);
	else
		kfree(pMemory);
}

void*
OamAllocateMemory(
	IN unsigned int		dwSize)
{
	void *nPtr;

	if( dwSize > SIZE )
		nPtr = vmalloc(dwSize);
	else
		nPtr = kmalloc((size_t) dwSize, GFP_NOFS);
	
	if( nPtr == NULL )
	{
		printk( "OamAllocateMemory() allocation fail : ");
		if( dwSize > SIZE )
			printk( "vmalloc case....\n");	
		else
			printk( "kmalloc case....\n");	
	}
	
	return nPtr;
}


void
OamFreeMemory(
	IN void*			pMemory)
{ 
	RFS_ASSERT(pMemory);
	if (((u32)pMemory >= VMALLOC_START) && ((u32)pMemory <= VMALLOC_END))
		vfree(pMemory);
	else
		kfree(pMemory);
}


/******************************************************************************/
/* MISCELLANEOUS RUNTIME LIBRARY                                              */
/******************************************************************************/
/**
 * @brief OamAtomicIncrement function increments the value of the variable
 * passed as the argument and returns the its value incremented.
 */
unsigned int
OamAtomicIncrement(
	IN volatile unsigned int*	pValue)
{
	RFS_ASSERT(pValue);

	/*
	 * atomic_t is like the following:
	 * 	typedef struct { volatile int counter; } atomic_t
	 */

	atomic_inc((atomic_t *)pValue);

	return *pValue;
}


/**
 * @brief OamAtomicDecrement function decrements the value of the variable
 * passed as the argument and returns the its value incremented.
 */
unsigned int
OamAtomicDecrement(
	IN volatile unsigned int*	pValue)
{
	RFS_ASSERT(pValue);

	atomic_dec((atomic_t *)pValue);

	return *pValue;
}

/**
 * @brief OamGetOffsetFromUtc function returns offset from UTC
 */
int
OamGetOffsetFromUtc(void)
{
	extern struct timezone sys_tz;

	return -(sys_tz.tz_minuteswest);
}

/*
 * Define symbols
 */
#include <linux/module.h>

EXPORT_SYMBOL(OamDbgAllocateMemory);
EXPORT_SYMBOL(OamDbgFreeMemory);
EXPORT_SYMBOL(OamAllocateMemory);
EXPORT_SYMBOL(OamFreeMemory);
EXPORT_SYMBOL(OamGetOffsetFromUtc);

// end of file
