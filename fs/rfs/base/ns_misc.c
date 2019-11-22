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
* @brief	Implementation of the run-time library routines for TFS5 and the natives.
* @author	ByungJune Song (byungjune.song@samsung.com)
* @author	InHwan Choi (inhwan.choi@samsung.com)
* @file		ns_misc.c
*/

#include "ess_upcase.h"
#include "ess_lowcase.h"

#include "ns_misc.h"

#ifdef CONFIG_LINUX
#include "linux_util.h"
#include <linux/ctype.h>
#include <linux/string.h>
#endif

#ifdef CONFIG_SYMBIAN
#include "ess_unicode_sym.h"
#include "nssym_stdlib.h"
#endif

#ifdef CONFIG_RTOS
#include "ess_unicode.h"
#endif

#ifdef CONFIG_WINCE
#include "wince_misc.h"
#endif

/******************************************************************************/
/* DEFINES																	  */
/******************************************************************************/
#define NSD_FILE_ZONE_MASK		(eNSD_DZM_BASE)

#define	CHECK_2BYTE_ALIGN		0x1
#define	WIDE_CHAR_SIZE			2

/******************************************************************************/
/* STATIC VARIABLES                                                           */
/******************************************************************************/

#define	MAX_BYTE				0xFFUL
#define	BYTE_BITS				8
#define	P_16					0xA001

#ifdef USE_CRC32
static	unsigned int	_UpdateCRC32(unsigned int nCrc16, char cCh);

#define INIT_VALUE_CRC32		0xFFFFFFFFUL
#define XOR_VALUE_CRC32			0xFFFFFFFFUL

static const unsigned long	crc_tab32[256] = {
		0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL,
		0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
		0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L,
		0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
		0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
		0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
		0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
		0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
		0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L,
		0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
		0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L,
		0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
		0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L,
		0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
		0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
		0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
		0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
		0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
		0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L,
		0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
		0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
		0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
		0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL,
		0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
		0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
		0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
		0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
		0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
		0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L,
		0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
		0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L,
		0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
		0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL,
		0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
		0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
		0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
		0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
		0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
		0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL,
		0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
		0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L,
		0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
		0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L,
		0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
		0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
		0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
		0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
		0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
		0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
		0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
		0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L,
		0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
		0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL,
		0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
		0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
		0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
		0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
		0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
		0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L,
		0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
		0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L,
		0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
		0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
		0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL
};
#endif

// random function
static int	_dwRandCurState = 0L;

/******************************************************************************/
/* Internal FUNCTIONS                                                         */
/******************************************************************************/

#ifdef USE_CRC32
static unsigned int 
_UpdateCRC32(unsigned int nCrc32, char cCh)
{
	nCrc32 = crc_tab32[(nCrc32 ^ cCh) & MAX_BYTE] ^ (nCrc32 >> BYTE_BITS);

	return nCrc32;
};


/******************************************************************************/
/* Nestle Public API: RTL FUNCTIONS                                           */
/******************************************************************************/
/**
 * @brief		calculate CRC
 * @param[in]	pwsName		input string
 * @param[in]	dwNameLen	string length
 * @return		CRC
 */

unsigned int
RtlCalculateCrc(
		IN const wchar_t*	pwsName,
		IN unsigned int		dwNameLen)
{
	unsigned int	i;
	unsigned int	nCRC, nCRCtemp;
	char			cChar;

	nCRC		= 0;
	nCRCtemp	= INIT_VALUE_CRC32;

	for (i = 0; i < dwNameLen; i++)
	{
		cChar = (char)(MAX_BYTE & pwsName[i]);
		nCRC = _UpdateCRC32(nCRCtemp, cChar);
		nCRCtemp = nCRC;
	}
	
	nCRC ^= XOR_VALUE_CRC32;
	
	return nCRC;
}

#else // simple checksum
unsigned int
RtlCalculateCrc(
		 IN const wchar_t*	pwsName,
		 IN unsigned int	dwLen)
{
	unsigned int	dwSum1 = 0xffff, dwSum2 = 0xffff;

	while (dwLen)
	{
		dwSum1 += *pwsName++;
		dwSum2 += dwSum1;
		dwLen--;
	}
	/* Second reduction step to reduce sums to 16 bits */
	dwSum1 = (dwSum1 & 0xffff) + (dwSum1 >> 16);
	dwSum2 = (dwSum2 & 0xffff) + (dwSum2 >> 16);

	return (dwSum2 << 16 | dwSum1);
}
#endif

/**
* @brief		This function checks if Value is power of 2 or not
* @param[in]	dwNum		input number
* @return		int
*/
int
RtlIsPow2(
	IN unsigned int	dwNum)
{
	return (dwNum & (dwNum - 1)) ? FALSE : TRUE;
}

/**
 * @brief		calculate power_2
 * @param[in]	dwNum		input number
 * @return		power_2
 */
unsigned int
RtlPow2(
	IN unsigned int	dwNum)
{
	#define STANDARD_BIT	0x00000001

	return (STANDARD_BIT << (unsigned int)dwNum);
}

/**
 * @brief		calculate log_2
 * @param[in]	dwNum		input number
 * @return		log_2
 */
unsigned int
RtlLog2(
	IN unsigned int dwNum)
{
	unsigned int dwResult;

	if (dwNum == 0 || (dwNum & (dwNum - 1)) != 0)
	{
		return 0;
	}

	for (dwResult = 0; dwNum != 1; dwResult++)
	{
		dwNum = dwNum >> 1;
	}

	return dwResult;
}


/**
 * @brief		get the uppercase wide char
 * @param[in]	wc		input wide char
 * @return		upppercase wide char
 */
wchar_t
RtlGetUpperWideChar(
	IN const wchar_t wc)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	if (wc == 0)
	{
		return wc;
	}

	return (wchar_t) (wc + _upcase_table[_upcase_table[wc >> 8] + (wc & 0xff)]);
#endif
#if defined(CONFIG_SYMBIAN)
	return SymRtlGetUpperWideChar(wc);
#endif
}

/**
 * @brief		get the lowercase wide char
 * @param[in]	wc		input wide char
 * @return		lowercase wide char
 */
wchar_t
RtlGetLowerWideChar(
	IN const wchar_t wc)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	if (wc == 0)
	{
		return wc;
	}

	return (wchar_t) (wc + _lowcase_table[_lowcase_table[wc >> 8] + (wc & 0xFF)]);
#endif
#if defined(CONFIG_SYMBIAN)
	return SymRtlGetLowerWideChar(wc);
#endif
}

/**
 * @brief		get the string length of wide char string
 * @param[in]	wcs		input wide char string
 * @return		length
 */
unsigned int
RtlWcsLen(
	IN	const wchar_t*	wcs)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	if (!((unsigned int)wcs & CHECK_2BYTE_ALIGN)) /* 2-byte aligned */
	{
		wchar_t*	temp = (wchar_t*) wcs;

		while (*temp++)
		{
			/* DO NOTHING */;
		}

		return ((unsigned int)(temp - wcs - 1));
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		unsigned char*	temp = (unsigned char *) wcs;

		while ((*temp) || (*(temp + 1)))
		{
			temp += WIDE_CHAR_SIZE;
		}

		return (((unsigned int)(temp - (unsigned char *) wcs)) >> 1);
	}
#endif
#if defined(CONFIG_SYMBIAN)
	return SymRtlWcsLen(wcs);
#endif
}

/**
 * @brief		compare two wide string
 * @param[in]	pwszName1	input wide char string1
 * @param[in]	pwszName2	input wide char string2
 * @return		compare result
 */
unsigned int
RtlCompareString(
	IN const wchar_t*		pwszName1,
	IN const wchar_t*		pwszName2)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	return	RtlWcsCmp(pwszName1, pwszName2);	
#endif
#if defined(CONFIG_SYMBIAN)
	return SymRtlWcsCmp(pwszName1, pwszName2);
#endif
}

/**
 * @brief		compare two wide string with case-insensitive for cchCompare length
 * @param[in]	pwszName1	input wide char string1
 * @param[in]	pwszName2	input wide char string2
 * @param[in]	cchCompare	the length to compare
 * @return		compare result
 */
unsigned int
RtlCompareStringCaseInsensitive(
	IN const wchar_t*		pwszName1,
	IN const wchar_t*		pwszName2,
	IN unsigned int			cchCompare)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	while (cchCompare && *pwszName1 && *pwszName2)
	{
		if (*pwszName1 != *pwszName2)
		{
			wchar_t wc1, wc2;

			// case insensitive comparison
			wc1 = RtlGetUpperWideChar(*pwszName1);
			wc2 = RtlGetUpperWideChar(*pwszName2);

			if (wc1 != wc2)
			{
				return (wc1 < wc2) ? -1 : 1;
			}
		}

		pwszName1++;
		pwszName2++;
		cchCompare--;
	}

	if (!cchCompare || *pwszName1 == *pwszName2)
	{
		return 0;
	}

	return (*pwszName1 < *pwszName2) ? -1 : 1;
#endif
#if defined(CONFIG_SYMBIAN)
	return SymRtlCompareStringCaseInsensitive(pwszName1, pwszName2, cchCompare);
#endif

}

/**
 * @brief		get the Uppercase wide string
 * @param[out]	pwszTarget		result string
 * @param[in]	pwszSource		input string
 * @return		void
 */
void
RtlConvertUpperString(
	OUT	wchar_t*		pwszTarget,
	IN	const wchar_t*	pwszSource)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	while (*pwszSource)
	{
		// case insensitive comparison
		*pwszTarget = RtlGetUpperWideChar(*pwszSource);
		pwszSource++;
		pwszTarget++;
	}

	// write null char
	*pwszTarget = 0x00;

	return;
#endif
#if defined(CONFIG_SYMBIAN)
	SymRtlConvertUpperString(pwszTarget, pwszSource);
#endif
}

/**
 * @brief		get the Lowercase wide string
 * @param[out]	pwszTarget		result string
 * @param[in]	pwszSource		input string
 * @return		void
 */
void
RtlConvertLowerString(
	OUT	wchar_t*		pwszTarget,
	IN	const wchar_t*	pwszSource)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	while (*pwszSource)
	{
		*pwszTarget = RtlGetLowerWideChar(*pwszSource);
		pwszSource++;
		pwszTarget++;
	}

	// write null char
	*pwszTarget = 0x00;

	return;
#endif
#if defined(CONFIG_SYMBIAN)
	SymRtlConvertLowerString(pwszTarget, pwszSource);
#endif
}

/**
 * @brief		get the Numeric number from the string
 * @param[in]	pwszSource		input string
 * @param[in]	cchSource		input string length
 * @param[out]	pdwCount		start position of numeric number
 * @return		numeric number, If numeric number not exist, return -1
 */
int
RtlGetLastNumeric(
	IN	const wchar_t*	pwszSource,
	IN	unsigned int	cchSource,
	OUT	unsigned int*	pdwCount)
{
	#define	SECOND_NUMBER	10		// 십의 자리수

	unsigned int	dwIndex, cchCount;
	unsigned int	cNumber = 0;
	unsigned int	i;

	/* string ends non-number */
	if ((pwszSource[cchSource - 1] < '0') || (pwszSource[cchSource - 1] > '9'))
	{
		*pdwCount = cchSource;
		return -1;
	}

	dwIndex = cchCount = 0;
	for (i = 0; i < cchSource; i++)
	{
		if (pwszSource[i] >= '0' && pwszSource[i] <= '9')
		{
			dwIndex = (SECOND_NUMBER * dwIndex) + pwszSource[i] - '0';
			cNumber++;
		}
		else
		{
			dwIndex = 0;
			/* previous count + the count of internal numbers + current char(1) */
			cchCount = cchCount + cNumber + 1;
			cNumber = 0;
		}
	}
	*pdwCount = cchCount;
	return dwIndex;
}


/**
 * @brief		covert multibyte string to wide string
 * @param[in]	pVcb			volume control block
 * @param[in]	pMultiByteStr	multibyte string (NULL is unacceptable)
 * @param[in]	cbMultiByte		the byte size of multibyte string to convert
 * @param[out]	pWideCharStr	widechar string (NULL is unacceptable)
 * @param[in]	cchWideChar		the char count allocated to pWideCharStr
 * @return		positive on success. "(unsigned int) -1" on failure (invalid buffer or invalid character)
 */
unsigned int	RtlMbsToWcs(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	const char*				pMultiByteStr,
	IN	unsigned int			cbMultiByte,
	OUT	wchar_t*				pWideCharStr,
	IN	unsigned int			cchWideChar)
	
{
	int		ret;

	pVcb = pVcb;

#ifdef CONFIG_LINUX
	ret = RtlConvertToWcs(pVcb, pWideCharStr, cchWideChar, pMultiByteStr, cbMultiByte);
#endif

#ifdef CONFIG_SYMBIAN
	ret = SymRtlMbsToWcs(pWideCharStr, pMultiByteStr, cchWideChar);
#endif

#ifdef CONFIG_RTOS
	#if (ENABLE_RFS_TOOLS >= 1)
		ret = OamMbsToWcs((t_uint16*)pWideCharStr, (t_uint8*)pMultiByteStr, cchWideChar, FALSE);
	#else
		// ignore cbMultiByte
		ret = UNI_MBSTOWCS((unsigned short*)pWideCharStr, pMultiByteStr, cchWideChar);
	#endif
#endif

#ifdef CONFIG_WINCE
	ret = RtlWceMbsToWcs(pWideCharStr, cchWideChar, pMultiByteStr, cbMultiByte);
#endif

	if (ret <= 0)
	{
		ret = -1;
	}

	return (unsigned int) ret;
}

/**
 * @brief		covert multibyte char to widechar char
 * @param[in]	pVcb			volume control block
 * @param[in]	pMultiByteChar	multibyte char (NULL is unacceptable)
 * @param[in]	cbMultiByte		the byte size of multibyte string to convert
 * @param[out]	pWideCharChar	widechar char (NULL is unacceptable)
 * @return		1 or 2 on success. -1 on failure (invalid buffer or invalid character)
 */
int		RtlMbToWc(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	const char*				pMultiByteChar,
	IN	unsigned int			cbMultiByte,
	OUT	wchar_t*				pWideCharChar) 

{
	int ret;

	pVcb = pVcb;

#ifdef CONFIG_LINUX
	ret = RtlConvertToWc(pVcb, pWideCharChar, pMultiByteChar, cbMultiByte);
#endif

#ifdef CONFIG_SYMBIAN
	ret = SymRtlMbToWc(pWideCharChar, pMultiByteChar, cbMultiByte);
#endif

#ifdef CONFIG_RTOS
	#if (ENABLE_RFS_TOOLS >= 1)
		ret = OamMbsToWcs((t_uint16*)pWideCharChar, (t_uint8*)pMultiByteChar, cbMultiByte, TRUE);
	#else
		ret = UNI_MBTOWC(pWideCharChar, pMultiByteChar, cbMultiByte);
	#endif
#endif

#ifdef CONFIG_WINCE
	ret = RtlWceMbsToWcs(pWideCharChar, 1, pMultiByteChar, cbMultiByte);
#endif

	if (ret <= 0)
	{
		return -1;
	}

	return ret;
}

/**
 * @brief		covert widechar string to multibyte string
 * @param[in]	pVcb			volume control block
 * @param[in]	pWideCharStr	widechar string (NULL is unacceptable)
 * @param[in]	cchWideChar		the char count of widechar string to convert
 * @param[out]	pMultiByteStr	multibyte string (NULL is unacceptable)
 * @param[in]	cbMultiByte		the byte size allocated to pMultiByteChar
 * @return		positive on success. "(unsigned int) -1" on failure (invalid buffer or invalid character)
 */
unsigned int	RtlWcsToMbs(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	const wchar_t*			pWideCharStr, 
	IN	unsigned int			cchWideChar,
	OUT	char*					pMultiByteStr,
	IN	unsigned int			cbMultiByte)
	
{
	int ret;

	pVcb = pVcb;

#ifdef CONFIG_LINUX
	ret = RtlConvertToMbs(pVcb, pWideCharStr, pMultiByteStr, cbMultiByte);
#endif

#ifdef CONFIG_SYMBIAN
	ret = SymRtlWcsToMbs(pMultiByteStr, pWideCharStr, cbMultiByte);
#endif

#ifdef CONFIG_RTOS
	#if (ENABLE_RFS_TOOLS >= 1)
		ret = OamWcsToMbs((t_int8*)pMultiByteStr, (t_uint16*)pWideCharStr, cbMultiByte, FALSE);
	#else
		// ignore cchWideChar
		ret = UNI_WCSTOMBS(pMultiByteStr, pWideCharStr, cbMultiByte);
	#endif
#endif

#ifdef CONFIG_WINCE
	ret = RtlWceWcsToMbs(pMultiByteStr, cbMultiByte, pWideCharStr, cchWideChar);
#endif

	if (ret <= 0)
	{
		ret = -1;
	}

	return (unsigned int) ret;
}

/**
 * @brief		covert widechar char to multibyte char
 * @param[in]	pVcb			volume control block
 * @param[in]	WideCharChar	widechar char to convert
 * @param[out]	pMultiByteChar	multibyte char (NULL is unacceptable)
 * @param[in]	cbMultiByte		the byte size allocated to pMultiByteChar
 * @return		1 or 2 on success. -1 on failure (invalid buffer or invalid character)
 */
int	RtlWcToMb(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	wchar_t					WideCharChar,
	OUT	char*					pMultiByteChar,
	IN	unsigned int			cbMultiByte)

{
	int ret;

	pVcb = pVcb;

#ifdef CONFIG_LINUX
	ret = RtlConvertToMb(pVcb, WideCharChar, pMultiByteChar, cbMultiByte);
#endif

#ifdef CONFIG_SYMBIAN
	ret = SymRtlWcToMb(pMultiByteChar, WideCharChar);
#endif

#ifdef CONFIG_RTOS
	#if (ENABLE_RFS_TOOLS >= 1)
		ret = OamWcsToMbs((t_int8*)pMultiByteChar, (t_uint16*)&WideCharChar, cbMultiByte, TRUE);
	#else
		// ignores cbMultiByte
		ret = UNI_WCTOMB(pMultiByteChar, WideCharChar);	
	#endif
#endif

#ifdef CONFIG_WINCE
	ret = RtlWceWcsToMbs(pMultiByteChar, cbMultiByte, &WideCharChar, 1);
#endif

	if (ret <= 0)
	{
		return -1;
	}

	return ret;
}


/**
 * @brief		copy string
 * @param[out]	psDest			dest string
 * @param[in]	psSrc			src string
 * @return		result
 * @note		[VERY RISKY]. Use of "strcpy" has been known to cause a buffer overflow when used incorrectly 
 */
char*
RtlStrCpy(
	OUT	char*			psDest,
	IN	const char*		psSrc)
{
	return strcpy(psDest, psSrc);
}

/**
 * @brief		copy string with n bytes
 * @param[out]	psDest			dest string
 * @param[in]	psSrc			src string
 * @param[in]	nNumber			number of bytes to copy
 * @return		result
 */
char*
RtlStrNCpy(
	OUT	char*			psDest,
	IN	const char*		psSrc,
	IN	int			nNumber)
{
	return strncpy((char*)psDest, (const char*)psSrc, nNumber);
}

/**
 * @brief		compare string
 * @param[in]	pS1				string1
 * @param[in]	pS2				string2
 * @return		result
 */
int
RtlStrCmp(
	IN	const char*	pS1,
	IN	const char*	pS2)
{
	return strcmp(pS1, pS2);
}

/**
 * @brief		compare string in insensitive mode
 * @param[in]	pS1				string1
 * @param[in]	pS2				string2
 * @return		result
 */
int
RtlStrICmp(
	IN	const char*	pS1,
	IN	const char*	pS2)
{
	unsigned char	c1, c2;

	while (*pS1 != '\0')
	{
		c2 = *pS2++;
		if (c2 == '\0')
		{
			return 1;
		}
		c1 = *pS1++;

		if (c1 == c2)
		{
			continue;
		}

		c1 = (unsigned char)tolower(c1);
		c2 = (unsigned char)tolower(c2);

		if (c1 != c2)
		{
			return 1;
		}
	}

	if (*pS2 != '\0')
	{
		return 1;
	}

	return 0;
}

/**
 * @brief		compare string with n bytes
 * @param[in]	pS1				string1
 * @param[in]	pS2				string2
 * @param[in]	nNumber			number of bytes to copy
 * @return		result
 */
int
RtlStrNCmp(
	IN	const char*	pS1,
	IN	const char*	pS2,
	IN	int			nNumber)
{
	return strncmp(pS1, pS2, nNumber);
}

/**
 * @brief		get the string length
 * @param[in]	pS				string
 * @return		string length
 */
int
RtlStrLen(
	IN	const char*	pS)
{
	return (int)strlen((char*)pS);
}

/**
 * @brief		cat src string to dest
 * @param[out]	psDest			dest string
 * @param[in]	psSrc			src string
 * @return		result
 * @note		[VERY RISKY]. Use of "strcat" has been known to cause a buffer overflow when used incorrectly
 */
char*
RtlStrCat(
	OUT	char*	psDest,
	IN	const char*		psSrc)
{
	return strcat(psDest, psSrc);
}

/**
 * @brief		covert to lowercase char
 * @param[in]	c			char
 * @return		lowercase char
 */
int
RtlToLower(
	IN	int	c)
{
	return tolower(c);
}

/**
 * @brief		covert to uppercase char
 * @param[in]	c			char
 * @return		uppercase char
 */
int
RtlToUpper(
	IN	int	c)
{
	return  toupper(c);
}

/**
 * @brief		check LeadByte
 * @param[in]	pVcb		volume control block
 * @param[in]	c			char
 * @return		result
 */
int
RtlIsLeadByte(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	int	c)
{
#if defined( CONFIG_LINUX) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)
	return 0;
#endif

#ifdef CONFIG_RTOS
	return UNI_ISLEADBYTE(c);
#endif
}


/**
 * @brief		get the cch 
 * @param[in]	pS				string
 * @return		string cch
 */
int
RtlStrCch(
	IN	const char*	pS)
{
#if defined( CONFIG_LINUX) || defined(CONFIG_SYMBIAN) || defined(CONFIG_WINCE)
	return 0;
#endif

#ifdef CONFIG_RTOS
	#if (ENABLE_RFS_TOOLS >= 1)
		return (int)OamMbsCch((t_uint8*)pS);
	#else
		return (int)UNI_MBSCCH((char*)pS);
	#endif
#endif
}


/**
 * @brief		copy wide string
 * @param[out]	pwsDst			dest string
 * @param[in]	pwsSrc			src string
 * @return		dest string
 */
wchar_t*
RtlWcsCpy(
	OUT	wchar_t*		pwsDst,
	IN	const wchar_t*	pwsSrc)
{
	if (!((unsigned int)pwsDst & CHECK_2BYTE_ALIGN)  && !((unsigned int)pwsSrc & CHECK_2BYTE_ALIGN)) // 2-byte aligned
	{
		wchar_t*	temp = pwsDst;

		// until a terminate-null appears
		while ((*temp++ = *pwsSrc++) != 0)
		{
			/* DO NOTHING */;
		}
	}
	else // not-aligned, byte to byte approach - slow
	{
		char*	pc1 = (char*) pwsDst;
		char*	pc2 = (char*) pwsSrc;

		do
		{
			*pc1++ = *pc2++;
			*pc1++ = *pc2++;

		} while ((*(pc2 - WIDE_CHAR_SIZE) != 0) || (*(pc2 - 1) != 0));
	}

	return(pwsDst);
}

/**
 * @brief		copy wide string with n bytes
 * @param[out]	pwsDst			dest string
 * @param[in]	pwsSrc			src string
 * @param[in]	cch				number of length to copy
 * @return		dest string
 */
wchar_t*
RtlWcsNCpy(
	OUT	wchar_t*		pwsDst,
	IN	const wchar_t*	pwsSrc,
	IN	unsigned int	cch)
{
	// <ANSI standard>
	// 1. If the length of source string is shorter than cch, then the
	//    remaining space of the destination string should be filled with
	//    zero.
	// 2. The destination string may not be terminated by NULL, if cch is
	//    equal to or less than the length of the source string.

	if (!((unsigned int)pwsDst & CHECK_2BYTE_ALIGN) && !((unsigned int)pwsSrc & CHECK_2BYTE_ALIGN)) /* 2-byte aligned */
	{
		wchar_t*	temp = pwsDst;

		while (cch && ((*temp++ = *pwsSrc++) != 0x0))
		{
			cch--;
		}
		*temp++ = 0x0;
		if (cch) // <ANSI Standard> Case #1
		{
			while (--cch)
			{
				// fill remaining spaces of the destination with zero
				*temp++ = 0;
			}
		}

		return(pwsDst);
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		char*	pc1 = (char *) pwsDst;
		char*	pc2 = (char *) pwsSrc;

		// until cch becomes zero or a terminate-null appears
		while (cch)
		{
			*pc1++ = *pc2++;
			*pc1++ = *pc2++;

			if ((*(pc2 - WIDE_CHAR_SIZE) == 0) &&
				(*(pc2 - 1) == 0))
			{
				// got the terminate-null
				break;
			}

			cch--;
		}

		if (cch) // <ANSI Standard> Case #1
		{
			while (--cch)
			{
				// fill remaining spaces of the destination with zero
				*pc1++ = 0;
				*pc1++ = 0;
			}
		}

		return (pwsDst);
	}
}

/**
 * @brief		compare wide string 
 * @param[in]	wcs1			string1
 * @param[in]	wcs2			string2
 * @return		result
 */
int
RtlWcsCmp(
	IN	const wchar_t*	wcs1,
	IN	const wchar_t*	wcs2)
{
	int		ret = 0;

	if (!((unsigned int)wcs1 & CHECK_2BYTE_ALIGN) && !((unsigned int)wcs2 & CHECK_2BYTE_ALIGN)) /* 2-byte aligned */
	{
		// until any different character appears or a terminate-null occurred
		while ((0 == (ret = (int)(*wcs1 - *wcs2))) && *wcs2)
		{
			++wcs1; ++wcs2;
		}
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		unsigned char*	pc1 = (unsigned char *) wcs1;
		unsigned char*	pc2 = (unsigned char *) wcs2;
		wchar_t		wc1;
		wchar_t		wc2;

		// until any different character appears or a terminate-null occurred
		while (wc1 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc1, *(pc1 + 1)),
			   wc2 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc2, *(pc2 + 1)),
			   (0 == (ret = (int)(wc1 - wc2))) && wc2)
		{
			pc1 += WIDE_CHAR_SIZE; pc2 += WIDE_CHAR_SIZE;
		}
	}

	return(ret);
}

/**
 * @brief		compare wide string with n bytes
 * @param[in]	wcs1			string1
 * @param[in]	wcs2			string2
 * @param[in]	cch				number of length to compare
 * @return		result
 */
int
RtlWcsNCmp(
	IN	const wchar_t*	wcs1,
	IN	const wchar_t*	wcs2,
	IN	unsigned int	cch)
{
	// if cch equals to zero, we don't have to go further.
	if (!cch)
	{
		return 0;
	}

	if (!((unsigned int)wcs1 & CHECK_2BYTE_ALIGN) && !((unsigned int)wcs2 & CHECK_2BYTE_ALIGN)) // 2-byte aligned
	{
		// until cch becomes zero or any difference is found.
		while ((--cch) && (*wcs1) && (*wcs1 == *wcs2))
		{
			wcs1++; wcs2++;
		}

		// return value should mean lexicographic relation between two different characters.
		return((int)(*wcs1 - *wcs2));
	}
	else // not-aligned, byte to byte approach - slow
	{
		unsigned char*	pc1 = (unsigned char *) wcs1;
		unsigned char*	pc2 = (unsigned char *) wcs2;
		wchar_t		wc1;
		wchar_t		wc2;

		// until cch becomes zero or any difference is found.
		while (wc1 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc1, *(pc1 + 1)),
			  wc2 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc2, *(pc2 + 1)),
			  (--cch) && (wc1) && (wc1 == wc2))
		{
			pc1 += WIDE_CHAR_SIZE; pc2 += WIDE_CHAR_SIZE;
		}

		// return value should mean lexicographic relation between two different characters.
		return ((int)(wc1 - wc2));
	}
}

/**
 * @brief		compare wide string with case insensitive
 * @param[in]	wcs1			string1
 * @param[in]	wcs2			string2
 * @return		result
 */
int
RtlWcsICmp(
	IN	const wchar_t*	wcs1,
	IN	const wchar_t*	wcs2)
{
	wchar_t		f, l;

	f = l = 0;

	if (!((unsigned int)wcs1 & CHECK_2BYTE_ALIGN) && !((unsigned int)wcs2 & CHECK_2BYTE_ALIGN)) /* 2-byte aligned */
	{
		do
		{
			if (*wcs1 != *wcs2)
			{
				f = RtlGetLowerWideChar((wchar_t)(*wcs1));
				l = RtlGetLowerWideChar((wchar_t)(*wcs2));
			}
			else
			{
				f = l = *wcs1;
			}

			wcs1++; wcs2++;
		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}
	else /* not-aligned */
	{
		unsigned char*	pc1 = (unsigned char *) wcs1;
		unsigned char*	pc2 = (unsigned char *) wcs2;

		wchar_t wc1, wc2;

		do
		{
			// we don't have to worry about following two local variables. because,
			// they are optimized by the compiler and excluded from the final binary.
			wc1 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc1, *(pc1 + 1));
			wc2 = (wchar_t) RTL_UNICODE_MAKEWORD(*pc2, *(pc2 + 1));

			// convert them to lower case
			if (wc1 != wc2)
			{
				f = RtlGetLowerWideChar(wc1);
				l = RtlGetLowerWideChar(wc2);
			}
			else
			{
				f = l = wc1;
			}

			pc1 += WIDE_CHAR_SIZE;
			pc2 += WIDE_CHAR_SIZE;
		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}

	return (int)(f - l);
}


/**
* @brief		compare wide string with case insensitive
* @param[in]	wcs1			string1
* @param[in]	wcs2			string2
* @return		result
*/
wchar_t*
RtlWcsrChar(const wchar_t* wcs, wchar_t wc)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	unsigned int count = RtlWcsLen(wcs)-1;
	if (!((unsigned int)wcs & 0x1)) // 2-byte aligned
	{
		while ( (count != 0) && ((*(wcs+count)) != wc))
		{
			count--;
		}

		if ((*(wcs+count)) == wc)
		{
			return (wchar_t *) wcs + count;
		}
	}
	else // not-aligned
	{
		char*		pc = (char *) (wcs+count);
		wchar_t		wcTemp;

		while (wcTemp = (wchar_t) RTL_UNICODE_MAKEWORD(*(pc-1), *(pc)),
				(wcTemp) && (wcTemp != wc))
		{
			pc -= WIDE_CHAR_SIZE;
			if ( (pc < (char*)wcs) || ((pc -1 ) < (char*)wcs) )
				break;
		}

		if (wc == wcTemp)
		{
			return (wchar_t *) pc;
		}
	}

	return 0;
#endif /*defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)*/

#if defined(CONFIG_SYMBIAN)
	return SymRtlWcsrChar(wcs, wc);
#endif /*defined(CONFIG_SYMBIAN)*/

}


wchar_t*
RtlWcsChar(
	IN const wchar_t*	wcs,
	IN const wchar_t		wc)
{
#if defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)
	if (!((unsigned int)wcs & CHECK_2BYTE_ALIGN)) // 2-byte aligned
	{
		while ((*wcs) && ((*wcs) != wc))
		{
			wcs++;
		}

		if ((*wcs) == wc)
		{
			return (wchar_t *) wcs;
		}
	}
	else // not-aligned
	{
		char*		pc = (char *) wcs;
		wchar_t		wcTemp;

		while (wcTemp = (wchar_t) RTL_UNICODE_MAKEWORD(*pc, *(pc + 1)),
				(wcTemp) && (wcTemp != wc))
		{
			pc += WIDE_CHAR_SIZE;
		}

		if (wc == wcTemp)
		{
			return (wchar_t *) pc;
		}
	}

	return 0;
#endif /*defined(CONFIG_RTOS) || defined(CONFIG_WINCE) || defined(CONFIG_LINUX)*/

#if defined(CONFIG_SYMBIAN)
	return SymRtlWcsChar(wcs, wc);
#endif /*defined(CONFIG_SYMBIAN)*/

}



/**
 * @brief		cat src wide string to dest
 * @param[out]	wcs1			dest string
 * @param[in]	wcs2			src string
 * @return		dest string
 */
wchar_t *
RtlWcsCat(
	OUT wchar_t*		wcs1, 
	IN	const wchar_t*	wcs2)
{
	return (wchar_t *) RtlWcsCpy((wchar_t*)wcs1 + RtlWcsLen(wcs1), (wchar_t*)wcs2);
}



/**
* @brief	This function returns a random value
* @return	a random value
*/
int
RtlMathRand(void)
{
	#define	RANDOM_VALUE_01		1103515245L
	#define	RANDOM_VALUE_02		12345L
	#define	RANDOM_VALUE_03		0x7fffffffL

	return((_dwRandCurState = (_dwRandCurState * RANDOM_VALUE_01 + RANDOM_VALUE_02)) & RANDOM_VALUE_03);
}

/**
 * @brief : do memory copy for un-aligned data
 * @param[in]		pDst		destination pointer
 * @param[in]		pSrc		source pointer
 * @param[in]		dwLen	byte length
 * @return 		void
 * @remark		this function cannot be used for more than 1KB
*/
void
RtlBinCpy(
	IN	void*		pDst, 
	IN	const void*	pSrc, 
	IN	unsigned int	dwLen)
{
	unsigned char*	cpDst;
	unsigned char*	cpSrc;

	NSD_AS(pDst != NULL);
	NSD_AS(pSrc != NULL);
	NSD_AS(dwLen <= 1024);

	cpDst = (unsigned char*) pDst;
	cpSrc = (unsigned char*) pSrc;

	while (dwLen-- > 0)
	{
		*cpDst++ = *cpSrc++;
	}
}


/******************************************************************************/
/* Performance																  */
/******************************************************************************/

#if	defined(CONFIG_PRINT_FUNC_INOUT) && (CONCURRENT_LEVEL == NO_CONCURRENCY)

#define MAX_FUNCTION_NAME	31 // coding rule

#ifdef CONFIG_ENABLE_PROFILING

typedef struct _FUNCTION_INFO
{
	char			pszFunctionName[MAX_FUNCTION_NAME];
	unsigned int	dwCallCount;
	unsigned int	dwStartTime;
	unsigned int	dwElapsedTime;
	unsigned int	dwTotalTime;
	DLIST_ENTRY(struct _FUNCTION_INFO)	dleSiblings;
} FN_INFO, *PFN_INFO;

static DLIST(FN_INFO)	g_dlFunctionList;
static SEMAPHORE		g_bsFnList;

#endif

#ifdef CONFIG_ENABLE_CALL_STACK

typedef struct _CALL_STACK
{
	char	pszFunctionName[MAX_FUNCTION_NAME];
	int		dwStackSize;
	DLIST_ENTRY(struct _CALL_STACK)	dleSiblings;
} CALL_STACK, *PCALL_STACK;

static DLIST(CALL_STACK)	g_dlFunctionCallStack;
static SEMAPHORE		g_bsCallStack;

#endif

void
RtlInit(void)
{
	RtlInitFunctionInfo();

#ifdef TFS5_DEBUG
	OamInitMemory();
#endif

}

void
RtlTerminate(void)
{

	RtlShowFunctionList();
	
	RtlDestroyFunctionInfo();

#ifdef TFS5_DEBUG
	OamTerminateMemory();
#endif

}

void
RtlInitFunctionInfo(void)
{
#ifdef CONFIG_ENABLE_CALL_STACK
	DLIST_INIT(&g_dlFunctionCallStack);
	RtlCreateSemaphore(&g_bsCallStack, 1);
#endif
#ifdef CONFIG_ENABLE_PROFILING
	DLIST_INIT(&g_dlFunctionList);
	RtlCreateSemaphore(&g_bsFnList, 1);
#endif
}

void
RtlDestroyFunctionInfo(void)
{
#ifdef CONFIG_ENABLE_PROFILING
	PFN_INFO	pFI;
#endif
#ifdef CONFIG_ENABLE_CALL_STACK
	PCALL_STACK pCS;
#endif
	
#ifdef CONFIG_ENABLE_CALL_STACK
	RtlGetSemaphore(&g_bsCallStack);

	// clear call stack info.
	while ((pCS = DLIST_GET_HEAD(&g_dlFunctionCallStack)))
	{
		DLIST_REMOVE(&g_dlFunctionCallStack, pCS, dleSiblings);
		RtlFreeMem(pCS);
	}

	RtlPutSemaphore(&g_bsCallStack);
	RtlDestroySemaphore(&g_bsCallStack);
#endif

#ifdef CONFIG_ENABLE_PROFILING
	RtlGetSemaphore(&g_bsFnList);
	
	// clear function profiling info.
	while ((pFI = DLIST_GET_HEAD(&g_dlFunctionList)))
	{
		DLIST_REMOVE(&g_dlFunctionList, pFI, dleSiblings);
		RtlFreeMem(pFI);
	}

	RtlPutSemaphore(&g_bsFnList);
	RtlDestroySemaphore(&g_bsFnList);
#endif
}

void 
RtlShowCallStack(void)
{
#ifdef CONFIG_ENABLE_CALL_STACK
	PCALL_STACK pCs;
	int			index=0;

	RtlPrintMsg("\n--------------------------------------------------------\n");
	RtlPrintMsg("\n[Call Stack]\n");
	
	DLIST_FOREACH(&g_dlFunctionCallStack, dleSiblings, pCs)
	{
		index++;
		OamPrintf("%d . %s %d(Byte)\n", index, pCs->pszFunctionName, pCs->dwStackSize);
		if (pCs == DLIST_GET_TAIL(&g_dlFunctionCallStack))
		{
			break;
		}
	}

	RtlPrintMsg("\n--------------------------------------------------------\n");
#endif
}

void
RtlResetFunctionList(void)
{
#ifdef CONFIG_ENABLE_PROFILING
	PFN_INFO	pFI;

	RtlGetSemaphore(&g_bsFnList);
	
	// clear function profiling info.
	while((pFI = DLIST_GET_HEAD(&g_dlFunctionList)))
	{
		DLIST_REMOVE(&g_dlFunctionList, pFI, dleSiblings);
		RtlFreeMem(pFI);
	}

	RtlPutSemaphore(&g_bsFnList);
#endif
}

void
RtlShowFunctionList(void)
{
#ifdef CONFIG_ENABLE_PROFILING
	PFN_INFO	pFi;
	t_int32		dwAverage;

	OamPrintf("\n---------------------------------------------------------------------");
	OamPrintf("\n%50s", "Function Information");
	OamPrintf("\n---------------------------------------------------------------------");
	OamPrintf("\n%30s %10s %10s %10s","@Function Name", "@Call Count","@Total Time","@Average Time");
	OamPrintf("\n---------------------------------------------------------------------");
	RtlGetSemaphore(&g_bsFnList);

	DLIST_FOREACH(&g_dlFunctionList, dleSiblings, pFi)
	{
		if (pFi)
		{
			if (pFi == DLIST_GET_TAIL(&g_dlFunctionList))
			{
				break;
			}

			if (pFi->dwCallCount == 0)
			{
				dwAverage = 0;
			}
			else
			{
				dwAverage = pFi->dwTotalTime / pFi->dwCallCount;
			}

			OamPrintf("\n%30s, %10d, %10d, %10d,", pFi->pszFunctionName, pFi->dwCallCount, pFi->dwTotalTime, dwAverage); 

		}
	}

	RtlPutSemaphore(&g_bsFnList);

	OamPrintf("\n---------------------------------------------------------------------");
#endif
}

#ifdef CONFIG_ENABLE_CALL_STACK
static int	g_dwStackSize = 0;
static int	g_dwAddress = 0;
static int	g_dwMaxStackSize = 0;
#endif
static int	g_dwEnterExitCount = 0;

void
RtlEnterFunction (int* pdwAddress, const char* pszFunctionName)
{
#ifdef CONFIG_ENABLE_PROFILING
	PFN_INFO	pFi;
	BOOL		bFound = FALSE;
#endif
#ifdef CONFIG_ENABLE_CALL_STACK
	PCALL_STACK pCs;
	int			dwCurrStack = 0;
#endif

	NSD_VMZ((_T("[->] %s()"), pszFunctionName));

	g_dwEnterExitCount++;
	NSD_AS(g_dwEnterExitCount > 0);

#ifdef CONFIG_ENABLE_CALL_STACK

	if (g_dwAddress == 0)
	{
		g_dwAddress = (int)&pdwAddress;
		NSD_AS(g_dwEnterExitCount == 1);
	}
	else
	{
		NSD_AS(g_dwEnterExitCount > 1);
		dwCurrStack = g_dwAddress - (int)&pdwAddress;
		NSD_AS(dwCurrStack > 0);
		g_dwStackSize += dwCurrStack;
		g_dwAddress = (int)&pdwAddress;
	}

	if (g_dwMaxStackSize < g_dwStackSize)
	{
		g_dwMaxStackSize = g_dwStackSize;
	}
	
	if (g_dwStackSize > 5*1024)
	{
		RtlShowCallStack();
		NSD_AS(0);
	}
	// insert new item to call stack.
	pCs = RtlAllocMem(sizeof(CALL_STACK));
	if (pCs)
	{
		RtlStrNCpy(pCs->pszFunctionName, pszFunctionName, sizeof(pCs->pszFunctionName));
		pCs->dwStackSize = dwCurrStack;
		RtlGetSemaphore(&g_bsCallStack);
		DLIST_ADD_TAIL(&g_dlFunctionCallStack, pCs, dleSiblings);
		RtlPutSemaphore(&g_bsCallStack);
	}
#endif

#ifdef CONFIG_ENABLE_PROFILING
	// search this item in the function information list.

	// if no exist, allocate new one.

	RtlGetSemaphore(&g_bsFnList); 

	DLIST_FOREACH(&g_dlFunctionList, dleSiblings, pFi)
	{
		if (pFi && !RtlStrCmp(pFi->pszFunctionName, pszFunctionName))
		{
			bFound = TRUE;
			break;
		}
		if (pFi == DLIST_GET_TAIL(&g_dlFunctionList))
		{
			//found the last item
			break;
		}
	}

	RtlPutSemaphore(&g_bsFnList); 
	
	if (bFound)
	{
		pFi->dwCallCount++;
		pFi->dwStartTime = RtlGetMilliSec();
	}
	else
	{
		pFi = RtlAllocMem(sizeof(FN_INFO));
		if (pFi)
		{
			RtlStrNCpy(pFi->pszFunctionName, pszFunctionName, sizeof(pFi->pszFunctionName));
			pFi->dwCallCount = 1;
			pFi->dwStartTime = RtlGetMilliSec();
			pFi->dwTotalTime = 0;
			
			RtlGetSemaphore(&g_bsFnList);
			DLIST_ADD_HEAD(&g_dlFunctionList, pFi, dleSiblings);
			RtlPutSemaphore(&g_bsFnList);
		}
	}

#endif

}

void
RtlExitFunction (const char*	pszFunctionName)
{
#ifdef CONFIG_ENABLE_PROFILING
	PFN_INFO	pFi;
	BOOL		bFound = FALSE;
#endif
#ifdef CONFIG_ENABLE_CALL_STACK
	PCALL_STACK pCs;
#endif

	NSD_VMZ((_T("[<-] %s()"), pszFunctionName));

	g_dwEnterExitCount--;
	NSD_AS(g_dwEnterExitCount >= 0);

#ifdef CONFIG_ENABLE_CALL_STACK	
	RtlGetSemaphore(&g_bsCallStack); 

	DLIST_FOREACH(&g_dlFunctionCallStack, dleSiblings, pCs)
	{
		if (pCs && !RtlStrCmp(pCs->pszFunctionName, pszFunctionName))
		{
			bFound = TRUE;
			break;
		}
		if (pCs == DLIST_GET_TAIL(&g_dlFunctionCallStack))
		{
			RtlPrintMsg("Abnormal Termination.");
			NSD_AS(0);
		}
	}
	if (bFound)
	{
		g_dwStackSize -= pCs->dwStackSize;
		g_dwAddress += pCs->dwStackSize;

		DLIST_REMOVE(&g_dlFunctionCallStack, pCs, dleSiblings);
		RtlFreeMem(pCs);
		if (DLIST_IS_EMPTY(&g_dlFunctionCallStack))
		{
			NSD_AS(g_dwEnterExitCount == 0);
			g_dwAddress = 0;
		}
		bFound = FALSE;
	}

	RtlPutSemaphore(&g_bsCallStack); 
#endif

#ifdef CONFIG_ENABLE_PROFILING
	RtlGetSemaphore(&g_bsFnList); 

	// if no exist, allocate new one.
	DLIST_FOREACH(&g_dlFunctionList, dleSiblings, pFi)
	{
		if (pFi && !RtlStrCmp(pFi->pszFunctionName, pszFunctionName))
		{
			bFound = TRUE;
			break;
		}
		if (pFi == DLIST_GET_TAIL(&g_dlFunctionList))
		{
			RtlPrintMsg("ENTER & EXIT is not mismatched.");
			NSD_AS(0);
		}
	} 
	
	RtlPutSemaphore(&g_bsFnList); 

	if (bFound)
	{
		pFi->dwElapsedTime = RtlGetMilliSec() - pFi->dwStartTime;
		pFi->dwTotalTime  += pFi->dwElapsedTime;
	}
#endif

}

#endif /*CONFIG_PRINT_FUNC_INOUT*/


