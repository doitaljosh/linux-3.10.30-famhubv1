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
* @file		linux_util.c
* @brief	Implementation of linux utility
* @version	RFS_3.0.0_b047_RTM
* @author	hayeong.kim@samsung.com
*/

#include <linux/time.h>
#include <linux/string.h>
#include "ns_types.h"
#include "ns_misc.h"
#include "linux_util.h"

#undef NSD_FILE_ZONE_MASK
#define NSD_FILE_ZONE_MASK		(eNSD_DZM_BASE)

/* local variable definition */
static const unsigned int g_dwDays[] =
{
	0,  31,  59,  90, 120, 151, 181, 212,
	243, 273, 304, 334,   0,   0,   0,   0
};


#define SEC_PER_MIN		(60)			/* 60 secs / min */
#define MIN_PER_HR		(60)            /* 60 mins / hour */
#define SEC_PER_HR		(3600)          /* 3600 secs / hour */
#define HR_PER_DAY		(24)            /* 24 hours / day */
#define DAY_PER_YR		(365)           /* 365 days / year */
#define SEC_PER_DAY		(60 * 60 * 24)  /* 86400 secs / day */
#define DAY_PER_10YR    (365 * 10 + 2)  /* 3650 days / 10years */
#define SEC_PER_10YR    DAY_PER_10YR * SEC_PER_DAY      /* 10 years -> 315532800 secs */
//#define MIN_DATE		SEC_PER_10YR
#define MIN_DATE		0

#define LEAP_DAYS(yr)   ((yr + 3) >> 2)  /* leap-year days during yr years */
#define LEAP_YEAR(yr)   ((yr & 3) == 0) /* true if yr is the leap year */

/* error table */
#define ERR_TABLE_MAX   0x0025

static const int aNsToLinuxErrorTable[ERR_TABLE_MAX]=
{
													/* index */
	0,			-ENOMEM,	-EIO,		-EINVAL,	/* 00 .. 03 */
	-EINVAL,	-ELOOP,		-ENOSYS,	-EFAULT,	/* 04 .. 07 */
	-ENODEV,	-EBUSY,		-EBADF,		-EFAULT,	/* 08 .. 0B */
	-EEXIST,	-ENOENT,	-ENOENT,	-EACCES,	/* 0C .. 0F */
	-ENAMETOOLONG,-EISDIR,	-ENODEV,	-ENOTDIR,	/* 10 .. 13 */
	-EMFILE,	-EROFS,		-ENOTEMPTY,	-EACCES,	/* 14 .. 17 */
	-ENOSPC,	-EFAULT,	-EFAULT,	-EFAULT,	/* 18 .. 1B */
	-EFAULT,	-ENOSPC,	-ENOSPC,	-ENODATA,	/* 1C .. 1F */
	-ERANGE,	-EACCES,	-EPERM,		-EFAULT,	/* 20 .. 23 */
	-ENOENT,										/* 24 .. 23 */
};

/**
 * @brief		convert Nestle error number to linux errno(asm/errno.h)
 * @param[in]	dwErr	Nestle error number
 * @return		linux errno (negative)
 */
LINUX_ERROR
RtlLinuxError(
	IN	FERROR		dwErr)
{
	int dwIndex;

	NSD_ASSERT(dwErr <= 0);

	dwIndex = -dwErr;

	if (dwIndex < ERR_TABLE_MAX)
	{
        	return aNsToLinuxErrorTable[dwIndex];
	}

	return -EFAULT;
}

/**
 * @brief		convert linux errno to Nestle error number
 * @param[in]	dwLinuxError	errno in Linux
 * @return		Nestle error number
 */
FERROR
RtlNestleError(
	IN	LINUX_ERROR		dwLinuxError)
{
	// [TODO] : rearrange the order of error case accoding to the possibility
	switch (-dwLinuxError)
	{
	case    0:
	        return FERROR_NO_ERROR;
	case    EPERM:
	        return FERROR_NOT_SUPPORTED;
	case    ENOMEM:
	        return FERROR_INSUFFICIENT_MEMORY;
	case    EIO:
	        return FERROR_IO_ERROR;
	case    EINVAL:
	        return FERROR_INVALID;
	case    ENOSYS:
	        return FERROR_NOT_SUPPORTED;
	case    EFAULT:
			return FERROR_INVALID;
	case    ENODEV:
	        return FERROR_DEVICE_NOT_FOUND;
	case    EBUSY:
	        return FERROR_BUSY;
	case    ENOENT:
	        return FERROR_NO_MORE_ENTRIES	;
	case    EBADF:
	        return FERROR_INVALID_HANDLE;
    case    EEXIST:
	        return FERROR_ALREADY_EXISTS;
	case    EACCES:
	        return FERROR_ACCESS_DENIED;
	case    ENAMETOOLONG:
	        return FERROR_NAME_TOO_LONG;
	case    EISDIR:
	        return FERROR_NOT_A_FILE;
	case    ENOTDIR:
	        return FERROR_NOT_A_DIRECTORY;
	case    EMFILE:
	        return FERROR_TOO_MANY_OPEN_FILES;
	case    EROFS:
	        return FERROR_READONLY_FS;
	case    ENOTEMPTY:
	        return FERROR_NOT_EMPTY;
	case    ENOSPC:
	        return FERROR_NO_FREE_SPACE;
	default:
			return FERROR_IO_ERROR;
	}

	return dwLinuxError;
}

/**
 * @brief		convert linux time (sec) to Nestle typed time
 * @param[in]	pLxTime	linux system time
 * @param[out]	pTime	Nestle time structure
 * @return		void
 */
void
RtlLinuxTimeToSysTime(
	IN	PLINUX_TIMESPEC		pLxTime, 
	OUT	PSYS_TIME			pTime)
{
	LINUX_TIME		dwTimeSec;
	unsigned int	dwDay = 0;
	unsigned int	dwMonth = 0;
	unsigned int	dwYear = 0;
	u64				ns = 0;

	dwTimeSec = pLxTime->tv_sec - (long)(sys_tz.tz_minuteswest * 60);

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

	/* set the minimum value */
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
	pTime->wYear = dwYear + 1970; /* Nativefs time starts from 1970.1.1 */

	NSD_VMZC(0,
			("year:%d month:%d day:%d hour:%d min:%d sec:%d",
			pTime->wYear, pTime->wMonth, pTime->wDay,
			pTime->wHour, pTime->wMinute, pTime->wSecond));

	return;
}

/**
 * @brief		convert wide-character to multibyte char 
 * @param[in]	pVcb		volume control
 * @param[in]	cWideChar	wide-char
 * @param[out]	pMultiByteChar	multibyte char
 * @param[in]	cbMultiByte		size of MultiByteChar (in bytes)
 * @return		the length of resulting char or -1
 */
int
RtlConvertToMb(
	IN	PVOLUME_CONTROL_BLOCK	pVcb, 
	IN	wchar_t					cWideChar, 
	OUT	char*					pMultiByteChar,
	IN	unsigned int			cbMultiByte)
{
	PLINUX_NLS_TABLE	pNls;
	int					cLen;
	char				aString[LINUX_NLS_MAX_CHARSET_SIZE];

	NSD_ASSERT(pVcb->pNlsTableIo || pMultiByteChar);

	pNls = pVcb->pNlsTableIo;

	cLen = pNls->uni2char(cWideChar, aString, LINUX_NLS_MAX_CHARSET_SIZE);
	if (cLen <= 0)
	{
		/* invalid character or buffer */
		NSD_EMZC(TRUE, (_T("invalid wide character(0x%C)"), cWideChar));
		return -1;
	}
	else if (cLen > cbMultiByte)
	{
		/* invalid buffer size */
		NSD_EMZC(TRUE, (_T("buffer size is smaller than string(%d > %d)"),
					cLen, cbMultiByte));
		return -1;
	}

	RtlCopyMem(pMultiByteChar, (const void*) aString, (unsigned int) cLen);

	return cLen;
}

/**
 * @brief		convert wide-character string to multibyte string (nNameLen: includeing null)
 * @param[in]	pVcb
 * @param[in]	pWideCharStr	widechar string
 * @param[out]	pMultiByteStr	multibyte string
 * @param[in]	cbMultiByte		maximum length of multibytes memory space
 * @return		the length of resulting name
 */
int
RtlConvertToMbs(
	IN	PVOLUME_CONTROL_BLOCK	pVcb, 
	IN	const wchar_t*			pWideCharStr, 
	OUT	char*					pMultiByteStr, 
	IN	unsigned int			cbMultiByte)
{
	int					cLen = 0;
	int					dwStringIdx = 0, dwIdxWsz = 0;
	const wchar_t*		pUname = pWideCharStr;
	char				aString[LINUX_NLS_MAX_CHARSET_SIZE];
	PLINUX_NLS_TABLE	pNls;

	NSD_ASSERT(pVcb->pNlsTableIo || pWideCharStr || pMultiByteStr);

	pNls = pVcb->pNlsTableIo;

	NSD_VMZC(TRUE, ("cbMultiByte: %u", cbMultiByte));

	for (dwStringIdx = 0; dwStringIdx < (cbMultiByte - 1); dwIdxWsz++)
	{
		if (pUname[dwIdxWsz] == 0x0000)
		{
			/* end-of-WideString */
			NSD_VMZC(TRUE, ("end-of-WCs(idx: %d)", dwIdxWsz));
			break;
		}

		cLen = pNls->uni2char(pUname[dwIdxWsz], aString, LINUX_NLS_MAX_CHARSET_SIZE);
		if (cLen <= 0)
		{
			NSD_EMZC(TRUE, ("uni2char Fails(%d: EINVAL)", cLen));
			/* Invalid character */
			cLen = -EINVAL;
			break;
		}
		else
		{
			if (dwStringIdx + cLen > (cbMultiByte - 1))
			{
				/* valid but out-of-memory */
				NSD_VMZC(TRUE,
						("out-of-memory(mbs idx: %d, cLen: %d, boundary: %d)",
							dwStringIdx, cLen, cbMultiByte));
				break;
			}
			
			RtlCopyMem(&pMultiByteStr[dwStringIdx],
					(const void*) aString, (unsigned int) cLen);
		}

		dwStringIdx += cLen;
	}

	pMultiByteStr[dwStringIdx] = '\0';

	NSD_VMZC(TRUE, ("End-of converting WC->MB(cb: %d, cLen: %d): %s",
				dwStringIdx, cLen, pMultiByteStr));

	return (cLen < 0)? -EINVAL: dwStringIdx;
}

/**
 * @brief	convert multi-character to wide char 
 * @param[in]	pVcb		volume control
 * @param[out]	pWideChar	memory for wchar
 * @param[in]	pMultiByteStr multibyte string
 * @param[in] 	cbMultiByte	size of MultiByteStr (in bytes)
 * @return	the length of resulting char or -1(error)
 */
int
RtlConvertToWc(
	IN	PVOLUME_CONTROL_BLOCK	pVcb, 
	OUT	wchar_t*				pWideChar, 
	IN	const char*				pMultiByteStr,
	IN	unsigned int			cbMultiByte)
{
	PLINUX_NLS_TABLE	pNls;
	int					cLen;

	NSD_ASSERT(pVcb->pNlsTableIo || pWideChar || pMultiByteStr);

	pNls = pVcb->pNlsTableIo;

	cLen = pNls->char2uni(pMultiByteStr, cbMultiByte, pWideChar);
	if (cLen < 0) 
	{
		/* invalid char or buffer */
		NSD_EMZC(TRUE, (_T("invalid multibyte character(0x%s)"), pMultiByteStr));
		return -1;
	} 

    return 1;
}

/**
 * @brief	convert multibyte string to wide-character string (nNameLen: includeing null)
 * @param[in] pVcb			volume control block
 * @param[out] pWideCharStr	resulting unicode name
 * @param[in] cchWideChar	char count of widechar string
 * @param[in] pMultiByteStr	multibyte string
 * @param[in] cbMultiByte	byte size of multibyte string
 * @return	count of char in resulting wchar string
 */
int
RtlConvertToWcs(
	IN	PVOLUME_CONTROL_BLOCK	pVcb, 
	OUT	wchar_t*				pWideCharStr,
	IN	unsigned int			cchWideChar,
	IN	const char*				pMultiByteStr, 
	IN	unsigned int			cbMultiByte)
{
	PLINUX_NLS_TABLE	pNls;
	int					cLen = 0;
	int					dwIdxStr = 0;
	unsigned int		uniLen = 0;

	NSD_ASSERT(pVcb->pNlsTableIo || pWideCharStr || pMultiByteStr);
	pNls = pVcb->pNlsTableIo;

	NSD_VMZC(TRUE, ("cchWideChar: %u, cbMultiByte: %u (%s)", cchWideChar, cbMultiByte, pMultiByteStr));

	for (dwIdxStr = 0, uniLen = 0; uniLen < (cchWideChar - 1); uniLen++)
	{
		wchar_t tmpWchar;
		unsigned char *tmpMbStr = NULL;

		if (pMultiByteStr[dwIdxStr] == '\0')
		{
			/* end-of-MultibyteStr */
			NSD_VMZC(TRUE, ("end-of-MBs(idx: %d)", dwIdxStr));
			break;
		}

		tmpMbStr = (unsigned char *)&pMultiByteStr[dwIdxStr];

		cLen = pNls->char2uni(tmpMbStr, (cbMultiByte - dwIdxStr), &tmpWchar);
		if (cLen <= 0)
		{
			NSD_EMZC(TRUE, ("char2uni Fails(%d: EINVAL)", cLen));
			/* invalid char or buffer */
			cLen = -EINVAL;
			break;
		}
		else
		{
			if (tmpWchar == 0x00)
			{
				/* Null char */
				NSD_VMZC(TRUE, ("end-of string(NULL is converted)"));
				break;
			}
			else
			{
				pWideCharStr[uniLen] = tmpWchar;
			}

			dwIdxStr += cLen;
		}
	}

	pWideCharStr[uniLen] = 0x0000;

	NSD_VMZC(TRUE, ("End-of converting MB->WC(cch: %d, cLen: %d)", uniLen, cLen));

	/* return the length of name: it should be smaller than cchWideChar */
	return (cLen < 0)? -EINVAL: uniLen;
}

/**
 * @brief	allocate memory for unicode string & convert cstring(multi-bytes) to unicode(wide-char) string
 *
 * @param[in] pVcb		Volume control block
 * @param[in] pMbsName	multibytes string
 * @param[in] cbMbsLen	byte size of multibyte string
 * @param[out] ppwszName	unicode string pointer
 * @return		count of char in wchar string on success or errno on failure
 * @remarks		ppwszName is allocated here. This should be free after use.
 */
int
RtlGetWcsName(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	const char*				pMbsName,
	IN	unsigned int			cbMbsLen,
	OUT	wchar_t**				ppwszName)
{
	int	dwRet = 0;
	int dwLimitLen = LINUX_PATH_MAX;	/* linux/limits.h */
	wchar_t*	pwszTmp = NULL;

	NSD_ASSERT(*ppwszName == NULL);

	/* allocate memory for wchar string */
	pwszTmp = (wchar_t *)
		LINUX_Kmalloc((sizeof(wchar_t) * dwLimitLen), LINUX_GFP_NOFS);
	if (!pwszTmp)
	{
		NSD_PMZC(TRUE, (_T("fail to get free memory")));
		return -ENOMEM;
	}

	/* convert mbyte string to wchar string */
	dwRet = RtlConvertToWcs(pVcb, pwszTmp, dwLimitLen, pMbsName, cbMbsLen);
	if ((dwRet > 0) && (dwRet < dwLimitLen))
	{
		/* success */
		*ppwszName = pwszTmp;

		return dwRet;
	}

	if (dwRet <= 0)
	{
		NSD_EMZC(TRUE, (_T("Name is invalid or Codepage of Name is not equal to Codepage of Filesystem.")));
		
		dwRet = -EINVAL;
	}
	else
	{
		/* this code will not be touched */
		NSD_EMZC(TRUE, (_T("Name too long")));

		dwRet = -ENAMETOOLONG;

		/* FIXME 2009.01.12 assert? -> fixed 20090120 */
		NSD_ASSERT(0);
	}

	LINUX_Kfree((pwszTmp));
	return dwRet;
}

/**
 * @brief	release memory allocated for wchar string(name)
 *
 * @param[in] ppwszName		address of wchar string
 */
void
RtlPutWcsName(wchar_t **ppwszName)
{
	if (*ppwszName)
	{
		LINUX_Kfree(*ppwszName);
		*ppwszName = NULL;
	}
}


/*
 * Export Symbols for Linux module
 */
#include <linux/module.h>

EXPORT_SYMBOL(RtlLinuxError);

#include "ess_bitmap.h"
EXPORT_SYMBOL(EssBitmap_GetCountOfBitOne);
EXPORT_SYMBOL(EssBitmap_GetLowestBitZero);
EXPORT_SYMBOL(EssBitmap_GetLowestBitOne);

#include "ess_debug.h"
EXPORT_SYMBOL(ESS_DebugInit);
EXPORT_SYMBOL(ESS_DebugNullPrintf);

#include "ess_dlist.h"
EXPORT_SYMBOL(EssDList_Count);

#include "ess_hash.h"
EXPORT_SYMBOL(EssHash_Init);
EXPORT_SYMBOL(EssHash_Lookup);
EXPORT_SYMBOL(EssHash_GetFree);

#include "ess_list.h"
EXPORT_SYMBOL(EssList_Count);

#include "ess_lru.h"
EXPORT_SYMBOL(EssLru_Init);
EXPORT_SYMBOL(EssLru_GetTail);

#include "ess_math.h"
EXPORT_SYMBOL(EssMath_Rand);
EXPORT_SYMBOL(EssMath_IsPowerOfTwo);
EXPORT_SYMBOL(EssMath_Log2);

#include "ess_pstack.h"
EXPORT_SYMBOL(EssPStack_Init);
EXPORT_SYMBOL(EssPStack_InitMain);
EXPORT_SYMBOL(EssPStack_Release);
EXPORT_SYMBOL(EssPStack_Free);
EXPORT_SYMBOL(EssPStack_Alloc);
EXPORT_SYMBOL(EssPStack_GetFree);
EXPORT_SYMBOL(EssPStack_Add);

EXPORT_SYMBOL(ESSPStack_initPStack);

#include "ess_rbtree2.h"
EXPORT_SYMBOL(EssRBTree2_Delete);
EXPORT_SYMBOL(EssRBTree2_Lookup);
EXPORT_SYMBOL(EssRBTree2_IsEmpty);
EXPORT_SYMBOL(EssRBTree2_Init);
EXPORT_SYMBOL(EssRBTree2_LookupSmallerApproximate);
EXPORT_SYMBOL(EssRBTree2_LookupBiggerApproximate);
EXPORT_SYMBOL(EssRBTree2_Insert);

#include "ns_misc.h"
EXPORT_SYMBOL(RtlMbsToWcs);
EXPORT_SYMBOL(RtlWcsToMbs);
EXPORT_SYMBOL(RtlWcToMb);
EXPORT_SYMBOL(RtlWcsLen);
EXPORT_SYMBOL(RtlWcsCpy);
EXPORT_SYMBOL(RtlWcsNCpy);
EXPORT_SYMBOL(RtlWcsICmp);
EXPORT_SYMBOL(RtlWcsCmp);
EXPORT_SYMBOL(RtlWcsNCmp);
EXPORT_SYMBOL(RtlToLower);
EXPORT_SYMBOL(RtlToUpper);
EXPORT_SYMBOL(RtlGetLowerWideChar);
EXPORT_SYMBOL(RtlGetUpperWideChar);
EXPORT_SYMBOL(RtlStrNCmp);
EXPORT_SYMBOL(RtlStrNCpy);
EXPORT_SYMBOL(RtlStrCmp);
EXPORT_SYMBOL(RtlStrCpy);
EXPORT_SYMBOL(RtlStrLen);
EXPORT_SYMBOL(RtlFillMem);
EXPORT_SYMBOL(RtlCopyMem);
EXPORT_SYMBOL(RtlCmpMem);
EXPORT_SYMBOL(RtlIsLeadByte);
EXPORT_SYMBOL(RtlDestroySemaphore);
EXPORT_SYMBOL(RtlPutSemaphore);
EXPORT_SYMBOL(RtlGetSemaphore);
EXPORT_SYMBOL(RtlCreateSemaphore);

EXPORT_SYMBOL(RtlGetCurSysTime);

#include "nsd_debug.h"

EXPORT_SYMBOL(_NsdEnterDebugBreak);
EXPORT_SYMBOL(_NsdEnterEndlessLoop);
EXPORT_SYMBOL(_NsdGetTimeSec);
EXPORT_SYMBOL(_NsdGetDebugZoneMask);
EXPORT_SYMBOL(_NsdGetLastError);
EXPORT_SYMBOL(_NsdGetThreadId);
EXPORT_SYMBOL(_NsdLockConsole);
EXPORT_SYMBOL(_NsdUnlockConsole);

//end of file
