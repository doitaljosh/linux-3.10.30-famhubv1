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
 * @file		ffatfs_de.c
 * @brief		The file implement FFatDe module
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-12-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"
#include "ffat_al.h"

#include "ffatfs_de.h"
#include "ffatfs_fat.h"
#include "ffatfs_cache.h"
#include "ffatfs_main.h"
#include "ffatfs_api.h"
#include "ffatfs_misc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_DE)

// SPACE = 
//		0x20(' '), 0x09('\t'), 0x0d('\r'), 0x0a('\n'), 0x0b('\v')
#define _IS_SPACE(_x)		(((_x) > (t_wchar)0x20) ? FFAT_FALSE : \
							((((_x) == ' ') || ((_x) == '\t') || ((_x) == '\r') || ((_x) == '\n') || ((_x) == '\v')) ? FFAT_TRUE : FFAT_FALSE))
#define _IS_DIGIT(_c)		((((_c) <= '9') && ((_c) >= '0')) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_UPPER_ALPHA(_c)	((((_c) <= 'Z') && ((_c) >= 'A')) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_LOWER_ALPHA(_c)	((((_c) <= 'z') && ((_c) >= 'a')) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_ALPHA(_c)		((_IS_UPPER_ALPHA(_c) || (_IS_LOWER_ALPHA(_c)) ? FFAT_TRUE : FFAT_FALSE))

// types

// struct for ffat_fs_de_getNodeDE
typedef struct
{
	t_uint32		dwCluster;
	t_uint32		dwOffset;
	t_uint32		dwSector;
	t_boolean		bNameMatched;
	t_uint8			bOrder;
} _GetNode;


// static function
static t_boolean	_IsValidCharacter(t_wchar* psName, t_int32 dwLen);

static t_boolean	_IsValidCharacterInOS(t_wchar* psName, t_int32 dwLen);

#ifndef FFAT_NO_CHECK_RESERVED_NAME
	static t_boolean	
				_IsValidName(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen);
#endif

static FFatErr	_genShortNameForDe(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen,
								t_int32* pdwNamePartLen, t_int32* pdwExtPartLen,
								t_int32* pdwSfnNameSize, FatDeSFN* pDE,
								FatNameType* pdwNameType);
static FFatErr	_genShortNameForVolumeDe(FatVolInfo* pVI, t_wchar* psName,
								t_int32 dwLen, FatDeSFN* pDE);
static FFatErr	_convertToOemName(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen,
								t_uint8* psOut, t_int32 dwOutLen,
								t_int32* pdwConvertSize, FatNameType* pdwNameType);
static FFatErr	_convertToOemVolumeLabel(FatVolInfo* pVI, t_wchar* psName,
								t_int32 dwLen, t_uint8* psOut, t_int32 dwOutLen);

static FFatErr	_genDummyShortNameForDe(FatDeSFN* pDE, t_int32* pdwSfnNameSize);

#ifdef FFAT_VFAT_SUPPORT
	static void FFAT_FASTCALL
					_setDeSfnCaseType(FatDeSFN* pDE, FatNameType dwNamePartType,
								FatNameType dwExtPartType);
	static void		_adjustSFNCase(t_wchar* psSrc, t_wchar* psDes, t_int32 dwLen,
								t_uint8 bNTRes);
#endif

static FFatErr	_deleteOnFat16Root(FatVolInfo* pVI, t_int32 dwOffset, 
								t_int32 dwCount, t_boolean bLookupDelMark,
								FFatCacheFlag dwFlag, void* pNode);
static FFatErr	_deleteInCluster(FatVolInfo* pVI, t_uint32 dwCluster,
								t_int32 dwEntryOffset, t_int32 dwCount,
								t_boolean bLookupDelMark, FFatCacheFlag dwFlag,
								void* pNode);
static t_uint8	_getDeletionMark(FatVolInfo* pVI, t_uint32 dwSector, 
								t_int32 dwLastEntryOffset, 
								t_int32 dwEntryPerSector, FFatCacheFlag dwFlag);

static FFatErr	_getVolumeLabelEntry(FatVolInfo* pVI, FatDeSFN* pDE,
								t_uint32* pdwSector, t_uint32* pdwOffset);
static FFAT_INLINE FFatErr
				_compareStringLFNE(FatVolInfo* pVI, t_wchar* psStr, FatDeLFN* pDeLFN, t_int32 dwLen);
static FFAT_INLINE FFatErr
				_compareSFN(FatVolInfo* pVI, FatDeSFN* pDE, t_wchar* psName, t_int32 dwNameLen);

static FFatErr	_getFreeDE(FatVolInfo* pVI, t_uint32 dwCluster,
								t_uint32* pdwSector, t_uint32* pdwOffset);

#define _RSVD_NAME_COUNT		22
#define _RSVD_NAME_LEN			5

#ifndef FFAT_NO_CHECK_RESERVED_NAME
	static char	_psReservedNames[_RSVD_NAME_COUNT][_RSVD_NAME_LEN];
#endif

// debug begin
#if 0
	#define FFAT_DEBUG_DE_PRINTF		FFAT_DEBUG_PRINTF("[DE] "); FFAT_DEBUG_PRINTF
#else
	#define FFAT_DEBUG_DE_PRINTF(_msg)
#endif
// debug end


//=============================================================================
//
//	INLINE FUNCTIONS
//


/**
* This function is a sub function for ffat_de_getNodeDe() 
*	to check this entry is a valid next LFNE
* 
* @param		pNextLFN	: [IN] Next LFNE to check validity
* @param		pFirstLFN	: [IN] A first LFNE
* @param		bOrder		: [IN] order of entry
* @return		FFAT_TRUE	: This is a valid LFNE
* @return		FFAT_FALSE	: This is an  invalid LFNE
* @author		DongYoung Seo
* @version		03-DEC-2008 [DongYoung Seo] First Writing
*/
static FFAT_INLINE t_boolean
_isValidNextLFNE(FatDeLFN* pNextLFNE, FatDeLFN* pFirstLFNE, t_uint8 bOrder)
{
	FFAT_ASSERT(pNextLFNE);
	FFAT_ASSERT(pFirstLFNE);
	FFAT_ASSERT(bOrder < FAT_DE_COUNT_MAX);

	// check check sum
	if (pFirstLFNE->bChecksum != pNextLFNE->bChecksum)
	{
		FFAT_LOG_PRINTF((_T("There is corrupted directory entry")));
		// directory entry cleaning operation will be implemented here !!!
		/*   */

		return FFAT_FALSE;
	}

	// check LFNE order
	if (bOrder != pNextLFNE->bOrder)
	{
		FFAT_LOG_PRINTF((_T("LFN entry order is corrupted !!")));
		// directory entry cleaning operation will be implemented here !!!
		/*   */

		return FFAT_FALSE;
	}

	return FFAT_TRUE;
}


/**
 * compare string (part of name) with long file name entry
 * 
 * @param		pVI			: [IN] volume information
 * @param		psStr		: [IN] string pointer
 * @param		pDeLFN		: [IN] long file name entry
 * @param		dwLen		: [IN] length to compare (length of psStr)
 * @return		FFAT_TRUE	: string and LFNE match
 * @return		FFAT_FALSE	: string and LFNE do not match
 * @author		GwangOk Go
 * @version		JAN-18-2008 [GwangOk Go] First Writing
 * @version		NOV-05-2008 [DongYoung Seo] remove compile error on big-endian
 * @version		04-DEC-2008 [DongYoung Seo] change function name from _compareStringWithLFNE
 * @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
 *											- Case sensitive
 */
static FFAT_INLINE FFatErr
_compareStringLFNE(FatVolInfo* pVI, t_wchar* psStr, FatDeLFN* pDeLFN, t_int32 dwLen)
{
	t_int32		dwRemainLen = dwLen;
	t_int32		dwCompareLen;
	t_int32		dwIndex = 0;
	t_int32		dwNameLen[] = {FAT_LFN_NAME1_CHAR, FAT_LFN_NAME2_CHAR, FAT_LFN_NAME3_CHAR};
	t_int32		dwDistance[] = {1, 13, 14};
	t_uint8*	pNamePart;

#ifdef FFAT_BIG_ENDIAN
	FatDeLFN	stBigDE;
	t_wchar*	pSrc;
	t_wchar*	pDes;
#endif

	FFAT_ASSERT(psStr);
	FFAT_ASSERT(pDeLFN);
	FFAT_ASSERT((dwLen > 0) && (dwLen <= FAT_LFN_NAME_CHAR));

	// adjust byte order
#ifdef FFAT_BIG_ENDIAN
	pSrc = (t_wchar*)pDeLFN->sName1;
	pDes = (t_wchar*)stBigDE.sName1;

	for (dwIndex = 1; dwIndex <= FAT_LFN_NAME_CHAR; dwIndex++)
	{
		*pDes++ = FFAT_BO_UINT16(*pSrc++);

		if (dwIndex == FAT_LFN_NAME1_CHAR)
		{
			pSrc = (t_wchar*)pDeLFN->sName2;
			pDes = (t_wchar*)stBigDE.sName2;
		}
		else if (dwIndex == (FAT_LFN_NAME1_CHAR + FAT_LFN_NAME2_CHAR))
		{
			pSrc = (t_wchar*)pDeLFN->sName3;
			pDes = (t_wchar*)stBigDE.sName3;
		}
	}

	pDeLFN = &stBigDE;
#endif

	pNamePart = (t_uint8*)pDeLFN;

	while (1)
	{
		pNamePart += dwDistance[dwIndex];

		if (dwRemainLen < dwNameLen[dwIndex])
		{
			// 비교할 남은 길이가 LFNE의 name part 길이보다 작은 경우
			dwCompareLen = dwRemainLen;
		}
		else
		{
			dwCompareLen = dwNameLen[dwIndex];
		}

		if (((VI_FLAG(pVI) & VI_FLAG_CASE_SENSITIVE) == 0)
			? (FFAT_WCSNUCMP(psStr, (t_wchar*)pNamePart, dwCompareLen) != 0)
			: (FFAT_WCSNCMP(psStr, (t_wchar*)pNamePart, dwCompareLen) != 0))
		{
			return FFAT_FALSE;
		}

		dwRemainLen -= dwNameLen[dwIndex];

		if (dwRemainLen < 0)
		{
			// 입력된 길이만큼 비교가 완료
			// LFNE의 다음 문자가 마지막 비교한 name part 내에 있는 경우
			pNamePart = (t_uint8*)((t_wchar*)pNamePart + dwCompareLen);

			if ((*pNamePart != 0x00) || (*(pNamePart + 1) != 0x00))
			{
				// LFNE의 다음 문자가 null이 아니면 LFNE뒤에 문자가 더 있음
				// ex) "DirName"과 "DirName5"
				return FFAT_FALSE;
			}
			
			return FFAT_TRUE;
		}
		else if (dwRemainLen == 0)
		{
			// 입력된 길이만큼 비교가 완료
			// LFNE의 다음 문자가 마지막 비교한 name part 다음에 있는 경우

			if (dwLen != FAT_LFN_NAME_CHAR)
			{
				// 입력된 길이가 LFNE의 길이가 아닌 경우 (다음 문자가 있음)

				FFAT_ASSERT(dwIndex <= 1);

				pNamePart = pNamePart + dwDistance[dwIndex+1];

				if ((*pNamePart != 0x00) || (*(pNamePart + 1) != 0x00))
				{
					// LFNE의 다음 문자가 null이 아니면 LFNE뒤에 문자가 더 있음
					// ex) "LongDirName"과 "LongDirName5"
					return FFAT_FALSE;
				}
			}
// debug begin
#ifdef FFAT_DEBUG
			else
			{
				// 입력된 길이가 LFNE의 길이인 경우 (다음 문자가 없음)
				FFAT_ASSERT(dwIndex == 2);
			}
#endif
// debug end

			return FFAT_TRUE;
		}
		psStr += dwNameLen[dwIndex];

		dwIndex++;
	}

	FFAT_ASSERT(0);

	return FFAT_FALSE;
}


/**
 * compare string for name string that length is less than FAT_LFN_NAME_CHAR
 * It may regard as a short file name so FAT filesystem should check both SFNE & LFNE
 * 
 * @param		pVI			: [IN] volume information
 * @param		pDE			: [IN] a valid directory entry
 * @param		psName		: [IN] name string pointer
 * @param		dwNameLen	: [IN] length of Directory Entry
 * @return		FFAT_OK1		: match		== FFAT_TRUE
 * @return		FFAT_OK			: not match == FFAT_FALSE
 * @return		FFAT_EINVALID	: directory has invalid name character
 * @author		DongYoung Seo
 * @version		MAR-08-2008 [DongYoung Seo] First Writing
 */
static FFAT_INLINE FFatErr
_compareSFN(FatVolInfo* pVI, FatDeSFN* pDE, t_wchar* psName, t_int32 dwNameLen)
{
	t_wchar			psNameTemp[FAT_LFN_NAME_CHAR + 1];
											// temporary buffer to compare short file name
	t_int32			dwLen;				// storage for name length (length of name at pDE)
	FFatErr			r;

	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(dwNameLen <= FAT_DE_SFN_MAX_LENGTH);

	// generate name and compare with it
	r = ffat_fs_de_genShortFileName(pVI, pDE, psNameTemp, &dwLen);
	FFAT_ER(r, (_T("fail to generate SFN")));

	if ((dwNameLen == dwLen) && (FFAT_WCSUCMP(psName, psNameTemp) == 0))
	{
		return FFAT_OK1;
	}

	return FFAT_OK;
}


/**
* This function is a sub function for _getNodeDEs()
*	check and add SFNE to pNodeDE
* 
* @param		pVI			: [IN] volume information
* @param		pNodeDe		: [IN/OUT] directory entry information for a node.
* @param		pDE			: [IN] SFN Entry pointer
* @param		bOrder		: [IN] order of LFN Entry
*									0		: valid SFNE
*									not 0	: there is no enough LFNE before this entry
* @param		dwCluster	: [IN] cluster of dwOffset
* @param		dwOffset	: [IN] current offset
* @param		bFound		: [IN] FFAT_TRUE : LFN comparison is already succeeded
* @return		FFAT_OK		: This is not an entry caller want, call need to get next node
* @return		FFAT_DONE	: Found an entry
* @return		else		: ERROR
* @author		DongYoung Seo
* @version		03-DEC-2008 [DongYoung Seo] First Writing
* @version		JUN-19-2009 [JeongWoo Park] Add the code to consider about LFN only mount flag
*/
static FFAT_INLINE FFatErr
_getNodeDEsSFNE(FatVolInfo* pVI, FatGetNodeDe* pNodeDE, FatDeSFN* pDE, t_uint8 bOrder,
				t_uint32 dwCluster, t_uint32 dwOffset, t_boolean bFound)
{
	FFatErr		r = FFAT_OK;		// default is not found

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(bOrder <= FAT_DE_LFN_COUNT_MAX);
	FFAT_ASSERT((pDE->bAttr & FFAT_ATTR_LONG_NAME_MASK) != FFAT_ATTR_LONG_NAME);
	FFAT_ASSERT((bFound == FFAT_TRUE) ? (pNodeDE->dwEntryCount > 0) : FFAT_TRUE);

#ifndef FFAT_VFAT_SUPPORT
	FFAT_ASSERT(pNodeDE->dwEntryCount == 0);
#endif

	if ((VI_FLAG(pVI) & VI_FLAG_LFN_ONLY) &&
		(pNodeDE->psName != NULL) &&
		(bFound == FFAT_FALSE))
	{
		goto out;
	}

	if (bOrder != 0)
	{
		// this is corrupted DE
		// LET'S get SFN only
		pNodeDE->dwEntryCount = 0;
	}

	if (pNodeDE->dwEntryCount == 0)
	{
		if ((pDE->sName[0] == 0x20) &&
			(pDE->sName[FAT_SFN_NAME_PART_LEN] == 0x20))
		{
			// this is an empty entry
			goto out;
		}

		// just one short file name entry
		// LFNE없이 SFNE만 있는 경우.
		// set De Start Cluster and Offset
		pNodeDE->dwDeStartCluster	= dwCluster;
		pNodeDE->dwDeStartOffset	= dwOffset;
	}

	// LFN only일 경우 SFN에 대한 비교는 할 필요 없음.
	if ((pNodeDE->psName != NULL) && (bFound == FFAT_FALSE) &&
		((VI_FLAG(pVI) & VI_FLAG_LFN_ONLY) == 0))
	{
		FFAT_ASSERT(pNodeDE->dwNameLen > 0);

		FFAT_ASSERT((pNodeDE->dwTargetEntryCount == 1) ? (pNodeDE->dwNameLen <= FAT_DE_SFN_MAX_LENGTH) : FFAT_TRUE);

		if (pNodeDE->dwNameLen <= FAT_DE_SFN_MAX_LENGTH)
		{
			r = _compareSFN(pVI, pDE, pNodeDE->psName, pNodeDE->dwNameLen);
			if (r == FFAT_OK1)
			{
				// We fount it !!
				goto done;
			}

			// This is not the node we want to find
			r = FFAT_OK;
			goto out;
		}
		else
		{
			goto out;
		}
	}

	// this node has LFNE
	// we must compare check-sum to check LFNEs are for this SFNE
	if (pNodeDE->dwEntryCount > 0)
	{
		// check order, order가 0이 아닌 상태에서 SFNE가 나왔으므로
		// 이 SFNE는 지금까지의 LFNE와 관계가 없음
		if ((ffat_fs_de_genChecksum(pDE) != (((FatDeLFN*)&pNodeDE->pDE[0])->bChecksum))
			|| (bOrder != 0))
		{
			FFAT_LOG_PRINTF((_T("Invalid check sum, ignore all LFN entries")));
			FFAT_ASSERT((VI_FLAG(pVI) & VI_FLAG_LFN_ONLY) == 0);	// LFN only일 경우 SFN만 리턴될 수 없다.

			// set entry count to 0
			pNodeDE->dwEntryCount = 0;

			// just one short file name entry
			// LFNE없이 SFNE만 있는 경우.
			pNodeDE->dwDeStartCluster	= dwCluster;
			pNodeDE->dwDeStartOffset	= dwOffset;
		}
	}

done:
	r = FFAT_DONE;

	pNodeDE->dwDeEndCluster	= dwCluster;
	pNodeDE->dwDeEndOffset	= dwOffset;
	pNodeDE->dwDeSfnCluster	= dwCluster;
	pNodeDE->dwDeSfnOffset	= dwOffset;

	// copy current DE to pNodeDE
	FFAT_MEMCPY(&pNodeDE->pDE[pNodeDE->dwEntryCount], pDE, FAT_DE_SIZE);
	pNodeDE->dwEntryCount++;

out:
	FFAT_ASSERT((r == FFAT_OK) || (r == FFAT_DONE));
	return r;
}


/**
* This function is a sub function for _getNodeDEs()
*	check and add lFNE to pNodeDE
* 
* @param		pVI				: [IN] volume information
* @param		pNodeDe			: [IN/OUT] directory entry information for a node.
* @param		pDE				: [IN] SFN Entry pointer
* @param		pbOrder			: [IN/OUT] order of LFN Entry
*									0		: valid SFNE
*									not 0	: there is no enough LFNE before this entry
* @param		dwCluster		: [IN] cluster of dwOffset
* @param		dwOffset		: [IN] current offset
* @param		pbNameMatch		: [OUT] FFAT_TRUE : Whole Name is matched
* @return		FFAT_OK			: This is not an entry caller want, call need to get next node
* @return		FFAT_OK1		: bypass all DEs for current Node
* @return		else			: ERROR
* @author		DongYoung Seo
* @version		03-DEC-2008 [DongYoung Seo] First Writing
* @version		22-JAN-2009 [DongYoung Seo] Change LFNE bypass condition 
*									from ==> else if ((pNodeDE->dwTargetEntryCount <= 2) && (*pbOrder >= 2))
*									to	==> else if ((pNodeDE->dwTargetEntryCount <= 2) && (*pbOrder >= 3))
*/
static FFAT_INLINE FFatErr
_getNodeDEsLFNE(FatVolInfo* pVI, FatGetNodeDe* pNodeDE, FatDeLFN* pDE, t_uint8* pbOrder,
				t_uint32 dwCluster, t_uint32 dwOffset, t_boolean* pbNameMatch)
{
	FFatErr		r = FFAT_OK;		// default is not found
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
	t_int32		dwCompareLen = 0;	// character count for comparison
	t_int32		dwSkipLen;			// character count for skip
	t_wchar*	psName = NULL;		// name pointer for name comparison
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT((pDE->bAttr & FFAT_ATTR_LONG_NAME_MASK) == FFAT_ATTR_LONG_NAME);
	FFAT_ASSERT(pbNameMatch);
	FFAT_ASSERT(*pbNameMatch == FFAT_FALSE);

#ifndef FFAT_VFAT_SUPPORT
	FFAT_ASSERT(pNodeDE->dwEntryCount == 0);
#endif

#ifndef  FFAT_VFAT_SUPPORT
	goto out;
#endif

#ifdef FFAT_VFAT_SUPPORT
re:
	// this is one of entries for a long file name
	if (pNodeDE->dwEntryCount != 0)
	{
		IF_UK (_isValidNextLFNE(pDE, (FatDeLFN*)&pNodeDE->pDE[0], *pbOrder) == FFAT_FALSE)
		{
			if ((pDE->bOrder & FAT_DE_LAST_LFNE_MASK) != FAT_DE_LAST_LFNE_MASK)
			{
				// this is an invalid next Long File Name entry
				goto get_new_node;
			}

			pNodeDE->dwEntryCount = 0;
			goto re;
		}

		if (pNodeDE->psName != NULL)
		{
			psName			= pNodeDE->psName + (((*pbOrder) - 1) * FAT_LFN_NAME_CHAR);
			dwCompareLen	= FAT_LFN_NAME_CHAR;
		}
	}
	else
	{
		// 현재 저장된 entry의 수가 0일 경우는 첫번째 entry 이다., this is the first entry

		// check first order
		if ((pDE->bOrder & FAT_DE_LAST_LFNE_MASK) != FAT_DE_LAST_LFNE_MASK)
		{
			FFAT_LOG_PRINTF((_T("Invalid last LFN entry, corrupted DE")));
			// directory entry cleaning operation will be implemented here !!!
			goto get_new_node;
		}

		// set start cluster and offset
		pNodeDE->dwDeStartCluster	= dwCluster;
		pNodeDE->dwDeStartOffset	= dwOffset;

		FFAT_ASSERT(pNodeDE->dwEntryCount == 0);

		*pbOrder = pDE->bOrder & ~FAT_DE_LAST_LFNE_MASK;

		// check entry count is valid
		if (*pbOrder > FAT_DE_LFN_COUNT_MAX)
		{
			FFAT_LOG_PRINTF((_T("Invalid last LFN entry, corrupted..")));
			goto get_new_node;
		}

		// 찾으려고 하는 entry의 수와 일치하는지 체크
		if (pNodeDE->dwTargetEntryCount > 1)
		{
			if ((pNodeDE->dwTargetEntryCount > 2) &&
				(*pbOrder != (pNodeDE->dwTargetEntryCount - 1)))
			{
				// SFNE를 찾는 경우가 아님. 첫 DE의 order가 안맞으면 stop.
				r = FFAT_OK1;
				goto out;
			}
			else if ((pNodeDE->dwTargetEntryCount <= 2) && (*pbOrder >= 3))
			{
				// order가 1인 LFNE과 SFNE만 찾으면 됨. LFNE는 bypass.
				r = FFAT_OK1;
				goto out;
			}
		}

		if (pNodeDE->psName != NULL)
		{
			dwSkipLen		= (*pbOrder - 1) * FAT_LFN_NAME_CHAR;
			psName			= pNodeDE->psName + dwSkipLen;
			dwCompareLen	= pNodeDE->dwNameLen - dwSkipLen;
		}
	}

	// 찾으려고 하는 name의 뒷부분 비교
	if (pNodeDE->psName != NULL)
	{
		FFAT_ASSERT(pNodeDE->dwTargetEntryCount >= 1);
		FFAT_ASSERT(psName);
		//2012.05.09_anshuma.s@samsung.com_Fix the bug for IssueQA2012040045 
		/* Add check for the end of the name string that is to be looked up.
 		* In case of end of name string , goto get_new_node.
 		* In absence of this check , look up was succeeding when 
 		* the name string length was a multiple of FAT_LFN_NAME_CHAR(13), 
 		* and the name string was matching completely with part of the LFNE's of Node's DE.
		*/ 
		if (psName != NULL && *psName != '\0') 
		{
			// compare string in last DE
			if (dwCompareLen > 0)
			{
				if (_compareStringLFNE(pVI, psName, pDE, dwCompareLen) == FFAT_TRUE)
				{
					if (*pbOrder == 1)
					{
						*pbNameMatch = FFAT_TRUE;
					}
				}
				else if ((pNodeDE->dwNameLen > FAT_DE_SFN_MAX_LENGTH) ||
					(VI_FLAG(pVI) & VI_FLAG_LFN_ONLY))
				{
					goto get_new_node;
				}	
			}		
		}
		else
		{
			goto get_new_node;
		}

	}

	// copy directory entry
	FFAT_MEMCPY(&pNodeDE->pDE[pNodeDE->dwEntryCount], pDE, FAT_DE_SIZE);
	pNodeDE->dwEntryCount++;
	(*pbOrder)--;

	r = FFAT_OK;
	goto out;
#endif

//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
get_new_node:
	pNodeDE->dwEntryCount = 0;

	FFAT_ASSERT(*pbNameMatch == FFAT_FALSE);
	r = FFAT_OK1;
#endif
out:
	FFAT_ASSERT((r == FFAT_OK) || (r == FFAT_OK1));

	return r;
}


/**
 * This function is used at ffat_de_getNodeDe() 
 * Check an entry is a new node while bypassing
 * 
 * @param		pDE			: [IN] A Short File Name Entry
 * @param		pLFN		: [IN] A Long File Name Entry for check sum
 * @return		FFAT_TRUE	: This is a new node
 * @return		FFAT_FALSE	: not a new node
 * @author		DongYoung Seo
 * @version		03-DEC-2008 [DongYoung Seo] First Writing
 * @version		25-JUN-2009 [JeongWoo Park] SFN is not passed
 */
static FFAT_INLINE t_boolean
_isNewNodeWhileByPassing(FatDeSFN* pDE, FatDeLFN* pLFN)
{
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pLFN);

	if (((pDE->bAttr & FFAT_ATTR_LONG_NAME_MASK) == FFAT_ATTR_LONG_NAME) &&
		((pDE->sName[0] & FAT_DE_LAST_LFNE_MASK) != FAT_DE_LAST_LFNE_MASK))
	{
		// First LFN이 아닌 중간 LFN의 경우 Bypass
		return FFAT_FALSE;
	}

	// SFN 혹은 First LFN의 경우 Bypass 중단
	return FFAT_TRUE;

	// LFN_ONLY 지원하기 전 소스
/*
	// 찾고자 하는 조건이 맞지 않을 경우 
	// 현재 node에 대한 DirectoryEntry는 모두 통과~~

	// 현재 상태는 pNodeDe->pDE[0]에 첫번째 LFN 정보가 저장되어 있다.

	// 깨진 directory entry일 경우에 대한 조건.
	// 예) directory entry가 아래와 같은 구성되어 있을 경우 bypass 하면 안됨.
	//				first character
	//		LFNE		0x43
	//		LFNE		0x02
	//		LFNE		0x47
	//		LFNE		0x46
	//		...
	//	위와 같은 경우 0x47이 나오는 시점은 다른 Node에 대한 정보임.
	//	그러므로 이 경우를 조사하는 코드가 필요함

	if ((pDE->sName[0] & FAT_DE_LAST_LFNE_MASK) == FAT_DE_LAST_LFNE_MASK)
	{
		return FFAT_TRUE;
	}

	if ((pDE->bAttr & FFAT_ATTR_LONG_NAME_MASK) != FFAT_ATTR_LONG_NAME)
	{
		// 깨진 directory entry일 경우에 대한 조건을 추가해야함..
		// 현재의 SFNE의 CHECK_SUM이 LFN과 맞지 않을 경우.
		// 예).		LFNE	0x42
		//			LFNE	0x01
		//			SFNE	
		// 위와 같을때 SFNE의 checksum이 LFNE의 것과 다를 경우 이것은 다른 NODE로
		// 판단해야 한다.

		if (ffat_fs_de_genChecksum(pDE) != pLFN->bChecksum)
		{
			FFAT_LOG_PRINTF((_T("Invalid check sum, ignore all LFN entries")));
			return FFAT_TRUE;
		}
	}

	return FFAT_FALSE;
*/
}


/**
* This function is a sub function for ffat_de_getNodeDe()
*	get Directory Entries for a node
* 
* @param		pVI			: [IN] volume information
* @param		pNodeDe		: [IN/OUT] directory entry information for a node.
* @param		dwSector	: [IN] lookup start sector number
* @param		dwCount		: [IN] count of sector
* @param		dwCluster	: [IN] cluster of dwOffset
* @param		dwOffset	: [IN] lookup start offset in current range
*									sector of dwOffset is dwSector.
*									offset base is begin of parent
*									DE를 찾는 시작 위치는 dwOffset이며
*									찾기 위한 entry 의 시작 offset은 dwOffset & VI_SSM(pVI)로 구한다.
*									반드시 dwOffset은 dwOffset에 해당하는 sector는 dwSector 이어야된다.
* @param		pbOrder		: [IN/OUT] order of LFN Entry
* @return		FFAT_OK		: get partial entries for a node or do not found
* @return		FFAT_DONE	: Found an entry
* @return		FFAT_ENOENT	: no more entry
* @return		FFAT_EEOF	: end of chain
* @return		else		: ERROR
* @author		DongYoung Seo
* @version		03-DEC-2008 [DongYoung Seo] First Writing
* @version		25-JUN-2009 [GwangOk Go] change logic in case that pNodeDE->bExactOffset is TRUE
*/
static FFAT_INLINE FFatErr
_getNodeDEs(FatVolInfo* pVI, FatGetNodeDe* pNodeDE, t_uint32 dwSector, t_int32 dwCount,
			t_uint32 dwCluster, t_uint32 dwOffset, t_uint8* pbOrder, t_boolean* pbNameMatched)
{
	FFatErr				r;
	FFatErr				rr;						// temporary error storage
	FFatfsCacheEntry*	pEntry = NULL;			// buffer cache entry
	FatDeSFN*			pDE;					// a directory entry pointer
	t_int32				dwEntryPerSector;		// entry count per a sector
	t_int32				i;
	t_boolean			bByPass = FFAT_FALSE;	// 찾고자하는 entry가 아닐경우 bypass 설정을 함으로써

	t_uint32			dwLastSector;			// last sector number to get DEs

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pbOrder);
	FFAT_ASSERT(dwSector <= VI_SC(pVI));
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(((FFATFS_IS_FAT16(pVI) == FFAT_TRUE) && (dwCluster == VI_RC(pVI))) ? (dwCount <= VI_RSC(pVI)) : (dwCount <= VI_SPC(pVI)));
	FFAT_ASSERT((dwOffset & FAT_DE_SIZE_MASK) == 0);
	FFAT_ASSERT(pNodeDE->dwOffset < (FAT_DE_MAX * FAT_DE_SIZE));

	dwEntryPerSector	= VI_SS(pVI) >> FAT_DE_SIZE_BITS;
	dwLastSector		= dwSector + dwCount - 1;

	do
	{
		FFAT_ASSERT(dwSector <= VI_SC(pVI));

		r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
		FFAT_EO(r, (_T("fail to get sector")));

		FFAT_ASSERT(pEntry);

		// check directory entry validity
		pDE = (FatDeSFN*)pEntry->pBuff;

		for (i = ((dwOffset & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS);
				i < dwEntryPerSector; i++, dwOffset+= FAT_DE_SIZE)
		{
			if (pDE[i].sName[0] == FAT_DE_END_OF_DIR)
			{
				// no more directory entry
				// directory truncation operation will be implemented here. !!!
				/*   */
				r = FFAT_EEOF;
				goto out;
			}

			if (pDE[i].sName[0] == FAT_DE_FREE)
			{
				// this is an free entry
				goto get_new_node;
			}

			if (bByPass == FFAT_TRUE)
			{
#ifdef FFAT_VFAT_SUPPORT
				if (_isNewNodeWhileByPassing(&pDE[i], (FatDeLFN*)&pNodeDE->pDE[0]) == FFAT_TRUE)
				{
					// This is a new node
					bByPass = FFAT_FALSE;
					FFAT_ASSERT(*pbNameMatched == FFAT_FALSE);
					FFAT_ASSERT(pNodeDE->dwEntryCount == 0);
				}
				else
				{
					continue;
				}
#endif

#ifndef FFAT_VFAT_SUPPORT
				FFAT_ASSERT(0);
#endif
			}

			FFAT_ASSERT(pNodeDE->dwEntryCount < FAT_DE_COUNT_MAX);
			FFAT_ASSERT(bByPass == FFAT_FALSE);
			FFAT_ASSERT(pNodeDE->dwEntryCount >= 0);

			// check short or long
			if ((pDE[i].bAttr & FFAT_ATTR_LONG_NAME_MASK) == FFAT_ATTR_LONG_NAME)
			{
				r = _getNodeDEsLFNE(pVI, pNodeDE, (FatDeLFN*)&pDE[i], pbOrder,
									dwCluster, dwOffset, pbNameMatched);
				FFAT_ASSERT((r == FFAT_OK) || (r == FFAT_OK1));
				if (r == FFAT_OK)
				{
					// go to read next entry
					continue;
				}

				bByPass = FFAT_TRUE;
			}
			else	// short file name entry를 만났을 경우
			{
				r = _getNodeDEsSFNE(pVI, pNodeDE, &pDE[i], *pbOrder, dwCluster,
								dwOffset, *pbNameMatched);
				FFAT_ASSERT((r == FFAT_OK) || (r == FFAT_DONE));
				if (r == FFAT_DONE)
				{
					goto find;
				}
			}

get_new_node:
			// check caller want to get node of at designated offset
			if (pNodeDE->bExactOffset == FFAT_TRUE)
			{
				r = FFAT_ENOENT;
				goto out;
			}

			// get new node
			*pbOrder = 0;
			pNodeDE->dwEntryCount = 0;
			*pbNameMatched = FFAT_FALSE;

		}	/* end of for (i = ((dwCurOffset & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS);
				i < dwEntryPerSector; i++) */

		// release a sector
		r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
		FFAT_EO(r, (_T("fail to put a cache entry")));

		pEntry = NULL;
	} while (++dwSector <= dwLastSector);

	// We get partial entries
	r = FFAT_OK;
	goto out;

find:
	// update total entry count
	pNodeDE->dwTotalEntryCount = pNodeDE->dwEntryCount;
	r = FFAT_DONE;
	goto out;

out:
	if (pEntry)
	{
		// release cache
		rr = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
		FFAT_ER(rr, (_T("fail to put a cache entry")));
	}

	FFAT_ASSERT(pNodeDE->dwEntryCount <= FAT_DE_COUNT_MAX);

	return r;
}


//=============================================================================
//
//	EXTERN FUNCTIONS
//


/**
 * This function initializes FFatfsCache
 *
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		JAN-07-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fs_de_init(void)
{
#ifndef FFAT_NO_CHECK_RESERVED_NAME
	// Initializes for reserved name

	FFAT_STRCPY(_psReservedNames[0], "CON");
	FFAT_STRCPY(_psReservedNames[1], "PRN");
	FFAT_STRCPY(_psReservedNames[2], "NUL");
	FFAT_STRCPY(_psReservedNames[3], "AUX");
	FFAT_STRCPY(_psReservedNames[4], "COM1");
	FFAT_STRCPY(_psReservedNames[5], "COM2");
	FFAT_STRCPY(_psReservedNames[6], "COM3");
	FFAT_STRCPY(_psReservedNames[7], "COM4");
	FFAT_STRCPY(_psReservedNames[8], "COM5");
	FFAT_STRCPY(_psReservedNames[9], "COM6");
	FFAT_STRCPY(_psReservedNames[10], "COM7");
	FFAT_STRCPY(_psReservedNames[11], "COM8");
	FFAT_STRCPY(_psReservedNames[12], "COM9");
	FFAT_STRCPY(_psReservedNames[13], "LPT1");
	FFAT_STRCPY(_psReservedNames[14], "LPT2");
	FFAT_STRCPY(_psReservedNames[15], "LPT3");
	FFAT_STRCPY(_psReservedNames[16], "LPT4");
	FFAT_STRCPY(_psReservedNames[17], "LPT5");
	FFAT_STRCPY(_psReservedNames[18], "LPT6");
	FFAT_STRCPY(_psReservedNames[19], "LPT7");
	FFAT_STRCPY(_psReservedNames[20], "LPT8");
	FFAT_STRCPY(_psReservedNames[21], "LPT9");
#endif

	return FFAT_OK;
}


/**
 * Directory Entry의 시간 정보를 update 한다.
 *
 * @param		pDE		: SFN entry
 * @param		dwFlag	: update flag
 * @param		pTime	: time information.
 *							NULL일 경우에는 system time을 얻어서 처리한다.
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 * @version		NOV-04-2000 [DongYoung Seo] add year validity checking code
 *											the year must be stored over 1980
 */
FFatErr
ffat_fs_de_setDeTime(FatDeSFN* pDE, FatDeUpdateFlag dwFlag, FFatTime* pTime)
{
	FFatTime	stTime;
	t_uint16	wDate, wTime;
	t_uint8		bTimeTenth;
	FFatErr		r;

	FFAT_ASSERT(pDE);

	if (pTime == NULL)
	{
		r = FFAT_LOCALTIME(&stTime);
		FFAT_EO(r, (_T("fail to get time")));
	}
	else
	{
		FFAT_MEMCPY(&stTime, pTime, sizeof(FFatTime));
	}

	wDate = (t_uint16)(stTime.tm_mday);
	wDate |= ((stTime.tm_mon + 1) << 5);
	// CHECK YEAR VALIDITY
	if (stTime.tm_year >= 70)
	{
		wDate |= ((stTime.tm_year + 1900 - 1970) << 9);
	}

	wTime = (t_uint16)(stTime.tm_sec >> 1);
	wTime |= (stTime.tm_min << 5);
	wTime |= (stTime.tm_hour << 11);

	bTimeTenth = (t_uint8)((stTime.tm_sec & 0x01) * 100 + stTime.tm_msec / 10);

	if (dwFlag & FAT_UPDATE_DE_ATIME)
	{
		pDE->wLstAccDate = FFAT_BO_UINT16(wDate);
	}

	if (dwFlag & FAT_UPDATE_DE_CTIME)
	{
		pDE->wCrtDate		= FFAT_BO_UINT16(wDate);
		pDE->wCrtTime		= FFAT_BO_UINT16(wTime);
		pDE->bCrtTimeTenth	= bTimeTenth;
	}

	if (dwFlag & FAT_UPDATE_DE_MTIME)
	{
		pDE->wWrtDate = FFAT_BO_UINT16(wDate);
		pDE->wWrtTime = FFAT_BO_UINT16(wTime);
	}

	r = FFAT_OK;
out:
	return r;
}


/**
 * Directory Entry의 시간 정보를 update 한다.
 *
 * @param		pDE			: SFN entry
 * @param		dwSize		: node size
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_de_setDeSize(FatDeSFN* pDE, t_uint32 dwSize)
{
	FFAT_ASSERT(pDE);

	pDE->dwFileSize = FFAT_BO_UINT32(dwSize);

	return FFAT_OK;
}


/**
 * Directory Entry의 cluster 정보를 update 한다.
 *
 * @param		pDE			: SFN entry
 * @param		dwCluster	: cluster number
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_de_setDeCluster(FatDeSFN* pDE, t_uint32 dwCluster)
{
	FFAT_ASSERT(pDE);

	pDE->wFstClusHi = FFAT_BO_UINT16((t_uint16)(dwCluster >> 16));
	pDE->wFstClusLo = FFAT_BO_UINT16((t_uint16)(dwCluster & 0xFFFF));

	return FFAT_OK;
}


/**
 * update attribute at directory entry
 * Directory Entry의 속성 정보를 update 한다.
 *
 * @param		pDE		: SFN entry
 * @param		bAttr	: attribute 
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_de_setDeAttr(FatDeSFN* pDE, t_uint8 bAttr)
{
	FFAT_ASSERT(pDE);

	pDE->bAttr = bAttr & FFAT_ATTR_MASK;

	return FFAT_OK;
}


/**
 * get directory entry for a node
 *
 *
 * @param		pVI	: [IN] volume pointer
 * @param		pNodeDe		: [IN/OUT] directory entry information for a node.
 * @return		FFAT_OK		: success
 * @return		FFAT_EEOF	: end of file. there is no more entry
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 * @version		MAR-07-2007 [DongYoung Seo] bug fix, check short file name entry 
 *													when target entry count is 1 or 2
 * @version		SEP-28-2008 [DongYoung Seo] change volume flag checking routine.
											from '=' t o '&'
 * @version		NOV-19-2008 [DongYoung Seo] remove volume name ignore routine
 *											it is a node that has volume name
 */
FFatErr
ffat_fs_de_getNodeDE(FatVolInfo* pVI, FatGetNodeDe* pNodeDE)
{
	FFatErr				r;
	t_uint32			dwCluster;		// lookup cluster, both start and current
	t_uint32			dwOffset;		// lookup offset, both start and current,
										// NOTICE!!! ==> offset base is parent first cluster
	t_uint32			dwSector;		// read sector number
	t_int32				dwCount;		// count of sector
	t_uint8				bOrder = 0;		// LFN entry order
	t_boolean			bNameMatched = FFAT_FALSE;
										// 이름으로 검색하는 경우 찾았는지 여부를 나타내는 flag

#ifdef FFAT_STRICT_CHECK
	if (FFATFS_IS_VALID_CLUSTER(pVI, pNodeDE->dwCluster) == FFAT_FALSE)
	{
		IF_UK (FFATFS_IS_FAT32(pVI) == FFAT_TRUE)
		{
			FFAT_LOG_PRINTF((_T("Invalid parent start cluster")));
			return FFAT_EINVALID;
		}
		// FAT12/16에서의 pNodeDe->dwParentCluster 가 FFATFS_FAT16_ROOT_CLUSTER(1)일 경우는 root 임을 의미한다.
	}

	if (FFATFS_IS_VALID_CLUSTER(pVI, pNodeDE->dwClusterOfOffset) == FFAT_FALSE)
	{
		IF_UK (pNodeDE->dwClusterOfOffset > 1)
		{
			FFAT_LOG_PRINTF((_T("Invalid lookup start")));
			return FFAT_EINVALID;
		}
		// dwStartCluster가 0,1 일경우는 hint 정보가 없음을 의미한다.
	}

	IF_UK (pNodeDE->pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid directory entry storage pointer")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pNodeDE->pDE);
	FFAT_ASSERT(pNodeDE->dwOffset < (FAT_DE_MAX * FAT_DE_SIZE));
	FFAT_ASSERT((pNodeDE->dwOffset & FAT_DE_SIZE_MASK) == 0);
	FFAT_ASSERT((pNodeDE->dwClusterOfOffset > 1) ? (FFATFS_IS_VALID_CLUSTER(pVI, pNodeDE->dwCluster) == FFAT_TRUE) : FFAT_TRUE);

// debug begin
#ifdef FFAT_DEBUG
	if ((pNodeDE->psName != NULL) && (pNodeDE->dwTargetEntryCount != 0))
	{
		FFAT_ASSERT(pNodeDE->psShortName);

		if (pNodeDE->dwTargetEntryCount == 1)
		{
			FFAT_ASSERT(pNodeDE->dwNameLen <= FAT_DE_SFN_MAX_LENGTH);
		}
		else if (pNodeDE->dwTargetEntryCount >= 2)
		{
			FFAT_ASSERT(pNodeDE->dwTargetEntryCount == (ESS_MATH_CD(pNodeDE->dwNameLen, FAT_LFN_NAME_CHAR) + 1));
		}
	}

	//check pNodeDE->dwClusterOfOffset
	{
		t_uint32	dwCluster;

		if (pNodeDE->dwClusterOfOffset)
		{
			r = ffat_fs_fat_getClusterOfOffset(pVI, pNodeDE->dwCluster, pNodeDE->dwOffset, &dwCluster);
			FFAT_ASSERT(r == FFAT_OK);
			FFAT_ASSERT(pNodeDE->dwClusterOfOffset == dwCluster);
		}
	}
#endif
// debug end

	// for normal directory

	// forward cluster
	if (pNodeDE->dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
		dwCluster = FFATFS_FAT16_ROOT_CLUSTER;
	}
	else
	{
		if (pNodeDE->dwClusterOfOffset < 2)
		{
			r = ffat_fs_fat_getClusterOfOffset(pVI, pNodeDE->dwCluster,
							pNodeDE->dwOffset, &dwCluster);
			FFAT_EO(r, (_T("fail to forward cluster for directory entry read")));

			pNodeDE->dwClusterOfOffset = dwCluster;
		}

		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, pNodeDE->dwClusterOfOffset) == FFAT_TRUE);

		// check offset
		dwCluster	= pNodeDE->dwClusterOfOffset;
	}

	// get directory entry lookup start offset in a cluster
	dwOffset = pNodeDE->dwOffset;
	pNodeDE->dwDeStartOffset = dwOffset;

	pNodeDE->dwTotalEntryCount = pNodeDE->dwEntryCount = 0;

	// get directory entry information
	do
	{
		if (pNodeDE->dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			// FAT12/16 root directory
			dwCount		= ((dwOffset & (~VI_SSM(pVI))) >> VI_SSB(pVI));
			dwSector	= VI_FRS(pVI) + dwCount;
			dwCount		= VI_RSC(pVI) - dwCount;

			if (dwCount <= 0)
			{
				FFAT_ASSERT(dwCount == 0);
				// No more cluster chain
				r = FFAT_EEOF;
				break;
			}

			FFAT_ASSERT(dwSector < (VI_FRS(pVI) + VI_RSC(pVI)));
		}
		else
		{
			if (pVI->pVolOp->pfIsEOF(dwCluster) == FFAT_TRUE)
			{
				// No more cluster chain
				r = FFAT_EEOF;
				break;
			}

			dwCount		= ((dwOffset & VI_CSM(pVI)) >> VI_SSB(pVI));
			// get lookup start sector
			dwSector	= FFATFS_GET_SECTOR_OF_CLUSTER(pVI, dwCluster, 0) + dwCount;
			dwCount		= VI_SPC(pVI) - dwCount;
		}

		FFAT_ASSERT(dwCount > 0);
		FFAT_ASSERT((dwOffset & FAT_DE_SIZE_MASK) == 0);

		// Get Directory Entry of a node
		r = _getNodeDEs(pVI, pNodeDE, dwSector, dwCount, dwCluster, dwOffset,
							&bOrder, &bNameMatched);
		if (r == FFAT_DONE)
		{
			// complete
			r = FFAT_OK;
			break;
		}
		else if (r < 0)
		{
			//fail to get DE for a node
			goto out;
		}

		if (pNodeDE->dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			// this is FAT16 end of directory
			r = FFAT_EEOF;
			break;
		}

		// get next cluster and update offset
		// if there is no other cluster return FFAT_ENOENT
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		if (r < 0)
		{
// debug begin
#ifdef FFAT_DEBUG
			if (r != FFAT_EEOF)
			{
				FFAT_LOG_PRINTF((_T("Fail to get next cluster ")));
			}
#endif
// debug end
			goto out;
		}

		dwOffset = (dwOffset + VI_CS(pVI)) & (~VI_CSM(pVI));
	} while (1);

out:
	return r;
}


/**
 * get a DirectoryEntry at dwOffset from dwCluster
 *
 * @param		
 * @param		pDE				: [IN] SFN entry
 * @return		FFAT_OK			: SUCCESS
 * @return		FFAT_EINVALID	: Invalid area
 *									out of root directory
 *									out of cluster chain
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_de_getDE(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwOffset,
					FatDeSFN* pDE, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwSector;		// Sector of dwOffset
	FFatfsCacheEntry*	pEntry = NULL;			// buffer cache entry

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pCxt);

	if (dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);

		if ((dwOffset >> FAT_DE_SIZE_BITS) >= VI_REC(pVI))
		{
			// Invalid area
			return FFAT_EINVALID;
		}

		dwSector = VI_FRS(pVI) + (dwOffset >> VI_SSB(pVI));

		FFAT_ASSERT(dwSector <= VI_LRS(pVI));
	}
	else
	{
		r = ffat_fs_fat_getClusterOfOffset(pVI, dwCluster, dwOffset, &dwCluster);
		FFAT_ER(r, (_T("fail to get cluster of offset")));

		if (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
		{
			return FFAT_EINVALID;
		}

		dwOffset = (dwOffset & VI_CSM(pVI));		// get offset in cluster
		dwSector = FFATFS_GET_SECTOR_OF_CLUSTER(pVI, dwCluster, dwOffset);
	}

	dwOffset = (dwOffset & VI_SSM(pVI));	// offset in sector

	r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
	FFAT_ER(r, (_T("fail to get sector")));

	FFAT_MEMCPY(pDE, pEntry->pBuff, FAT_DE_SIZE);

	r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
	FFAT_ER(r, (_T("fail to put a cache entry")));

	return FFAT_OK;
}


/**
 * generates checksum from SFN
 *
 * @param		pDE			: [IN] SFN entry
 * @return		check sum	: 
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
t_uint8
ffat_fs_de_genChecksum(FatDeSFN* pDE)
{
	t_uint8		bSum;
	t_uint8*	cP;
	t_int32		i;

	FFAT_ASSERT(pDE != NULL);

	FFAT_ASSERTP(pDE->sName[0] != FAT_DE_FREE, (_T("first character of short file name entry invalid")));

	bSum = 0;
	cP = (t_uint8*)pDE->sName;

	for (i = 11; i != 0; i--)
	{
		bSum = (t_uint8)( ((bSum & 0x01) ? 0x80 : 0) + (bSum >> 1) + (*cP++) );
	}

	return bSum;
}


#ifdef FFAT_VFAT_SUPPORT
	/**
	 * generates long file name from directory entry
	 *
	 * @param		pVI	: [IN] volume information
	 * @param		pDE			: [IN] LFN entry
	 * @param		dwEntryCount: [IN] Entry count in pDE, 
	 *								This is total Entry count (LFN + SFN)
	 * @param		psName		: [OUT] Name string storage
	 *								Storage size should be 256 characters or more.
	 * @param		pdwLen		: [OUT] Name length
	 * @return		FFAT_EINVALID	: directory has invalid name character
	 * @author		DongYoung Seo (dy76.seo@samsung.com)
	 * @version		AUG-08-2006 [DongYoung Seo]: First Writing.
	 * @history		FEB-25-2007 [DongYoung Seo]: modify last entry copy routine
	 */
	FFatErr
	ffat_fs_de_genLongFileName(FatVolInfo* pVI, FatDeLFN* pDE, t_int32 dwEntryCount,
								t_wchar* psName, t_int32* pdwLen)

	{
	#ifdef FFAT_BIG_ENDIAN
		t_int32		i;
		t_int32		dwLastEntry;	// the last entry of LFNE
	#endif

		t_int32		dwCurIndex;		// current index
		t_wchar*	psNameBuf;

		FFAT_ASSERT(pDE);
		FFAT_ASSERT(dwEntryCount > 0);
		FFAT_ASSERT(dwEntryCount < 22);
		FFAT_ASSERT(psName);
		FFAT_ASSERT(pdwLen);

		if (dwEntryCount == 1)
		{
			// just create a short file name entry
			return ffat_fs_de_genShortFileName(pVI, (FatDeSFN*)pDE, psName, pdwLen);
		}

		// check entry count validity
		IF_UK ((pDE[0].bOrder & (~FAT_DE_LAST_LFNE_MASK)) != (dwEntryCount - 1))
		{
			FFAT_LOG_PRINTF((_T("Invalid entry count ")));
			FFAT_ASSERT(0);
			return FFAT_EINVALID;
		}

		psNameBuf = psName;
		dwCurIndex = dwEntryCount - 2;	// 0 base, for LFN

		while (dwCurIndex >= 0)
		{
			if ((dwCurIndex == 0) && (dwEntryCount == FAT_DE_LFN_COUNT_MAX + 1))
			{
				// copy characters at last entry carefully.

				// copy 1-5 characters
				FFAT_MEMCPY(psNameBuf, pDE[dwCurIndex].sName1, FAT_LFN_NAME1_CHAR * 2);
				psNameBuf += FAT_LFN_NAME1_CHAR;	// increase string pointer

				// copy 3 characters on 6-11th character
				FFAT_MEMCPY(psNameBuf, pDE[dwCurIndex].sName2, 3 * 2);
				psNameBuf += 3;			// increase string pointer

				*psNameBuf = 0x0000;
			}
			else
			{
				FFAT_ASSERT(pDE[dwCurIndex].bAttr == FFAT_ATTR_LONG_NAME);

				// copy 1-5 characters
				FFAT_MEMCPY(psNameBuf, pDE[dwCurIndex].sName1, FAT_LFN_NAME1_CHAR * 2);
				psNameBuf += FAT_LFN_NAME1_CHAR;	// increase string pointer

				// copy 6-11 characters
				FFAT_MEMCPY(psNameBuf, pDE[dwCurIndex].sName2, FAT_LFN_NAME2_CHAR * 2);
				psNameBuf += FAT_LFN_NAME2_CHAR;	// increase string pointer

				// copy 12-13 characters
				FFAT_MEMCPY(psNameBuf, pDE[dwCurIndex].sName3, FAT_LFN_NAME3_CHAR * 2);
				psNameBuf += FAT_LFN_NAME3_CHAR;	// increase string pointer
			}

			dwCurIndex--;
		}

		// adjust byte order
	#ifdef FFAT_BIG_ENDIAN
		dwLastEntry = dwEntryCount - 2;

		for (i = 0; i <= dwLastEntry; i++)
		{
			FFAT_ASSERT(i < FAT_DE_LFN_COUNT_MAX);

			dwCurIndex = i * FAT_LFN_NAME_CHAR;

			// unfold loop for performance
			psName[dwCurIndex + 0] = FFAT_BO_UINT16(psName[dwCurIndex + 0]);
			psName[dwCurIndex + 1] = FFAT_BO_UINT16(psName[dwCurIndex + 1]);
			psName[dwCurIndex + 2] = FFAT_BO_UINT16(psName[dwCurIndex + 2]);
			psName[dwCurIndex + 3] = FFAT_BO_UINT16(psName[dwCurIndex + 3]);
			psName[dwCurIndex + 4] = FFAT_BO_UINT16(psName[dwCurIndex + 4]);
			psName[dwCurIndex + 5] = FFAT_BO_UINT16(psName[dwCurIndex + 5]);
			psName[dwCurIndex + 6] = FFAT_BO_UINT16(psName[dwCurIndex + 6]);
			psName[dwCurIndex + 7] = FFAT_BO_UINT16(psName[dwCurIndex + 7]);
			psName[dwCurIndex + 8] = FFAT_BO_UINT16(psName[dwCurIndex + 8]);

			if (i < (FAT_DE_LFN_COUNT_MAX - 1))
			{
				// QUIZ(KKAKA), Why it does not convert last 4 char?

				psName[dwCurIndex + 9] = FFAT_BO_UINT16(psName[dwCurIndex + 9]);
				psName[dwCurIndex + 10] = FFAT_BO_UINT16(psName[dwCurIndex + 10]);
				psName[dwCurIndex + 11] = FFAT_BO_UINT16(psName[dwCurIndex + 11]);
				psName[dwCurIndex + 12] = FFAT_BO_UINT16(psName[dwCurIndex + 12]);
			}
		}
	#endif

		// use current index as a temporary string length storage
		// to accelerate FFAT_WCSLEN()

		dwCurIndex = (dwEntryCount - 2) * FAT_LFN_NAME_CHAR;
		if (dwEntryCount != FAT_DE_LFN_COUNT_MAX + 1)
		{
			// add NULL next to the last character.
			psName[dwCurIndex + FAT_LFN_NAME_CHAR] = 0x0000;

			// Quiz : Why does not write NULL when dwEntry count is FAT_DE_COUNT_MAX ??
		}

		*pdwLen = FFAT_WCSLEN(psName + dwCurIndex) + dwCurIndex; 

		FFAT_ASSERT(*pdwLen <= FFAT_NAME_MAX_LENGTH);

		return FFAT_OK;
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT


/**
 * generates short file name from directory entry
 *
 * @param		pVI	: [IN] volume information
 * @param		pDE			: [IN] SFN entry
 * @param		psName		: [OUT] Name string storage
 *								Storage size should be 256 characters or more.
 * @param		pdwLen		: [OUT] Name length
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid name character at pDE
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing., comes from TFS4 1.6
 */
FFatErr
ffat_fs_de_genShortFileName(FatVolInfo* pVI, FatDeSFN* pDE,
								t_wchar* psName, t_int32* pdwLen)
{
	t_uint32	i;
	t_uint32	j;
	t_uint8		szBuffer[16] = {0, }; // name(8) + dot(1) + ext.(3) + NULL(1)
	t_int32		r;

	FFAT_ASSERTP(pDE != NULL, (_T("pDE cannot be NULL")));
	FFAT_ASSERTP(psName != NULL, (_T("pOemName cannot be NULL")));
	FFAT_ASSERT(pdwLen);

	if (pDE->bAttr == FFAT_ATTR_VOLUME)
	{
		r = ffat_fs_de_genShortVolumeLabel(pVI, pDE, psName);
		FFAT_ER(r, (_T("Fail to generate volume name")));

		*pdwLen = FFAT_WCSLEN(psName);

		return FFAT_OK;
	}

	// copy the first 8 characters (name)
	for (i = 0; i < FAT_SFN_NAME_PART_LEN; i++)
	{
		if (pDE->sName[i] == 0x20) // 0x20(white space)
		{
			break;
		}
		szBuffer[i] = pDE->sName[i];
	} // end of for-loop copying the name

	// check for KANJI
	if (szBuffer[0] == FAT_DE_CHAR_FOR_KANJI)
	{
		szBuffer[0] = FAT_DE_FREE;
	}

	if (pDE->sName[8] != 0x20) // have an extension?
	{
		FFAT_ASSERTP(i != 0, (_T("corrupted directory entry - this cannot be happen")));

		// append a dot.
		szBuffer[i++] = '.';

		// copy the last 3 characters (extension)
		for (j = 0; j < FAT_SFN_EXT_PART_LEN; j++)
		{
			if (pDE->sName[FAT_SFN_NAME_PART_LEN + j] == 0x20) // 0x20(white space)
			{
				break;
			}

			szBuffer[i++] = pDE->sName[FAT_SFN_NAME_PART_LEN + j];
		} // end of for-loop copying the extension
	} // end of if - have an extension?

	FFAT_ASSERTP(i < sizeof(szBuffer), (_T("something goes wrong!")));
	FFAT_ASSERTP(i != 0, (_T("corrupted directory entry - this cannot happen")));

	// terminate the string.
	szBuffer[i] = 0x00;

	// finally, convert it to UNICODE
	r = FFAT_MBSTOWCS(psName, (FAT_DE_SFN_MAX_LENGTH + 1), (const char*) szBuffer, (i + 1), VI_DEV(pVI));
	if (r <= 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to convert name (MB->WC)")));
		return FFAT_EINVALID;
	}

	*pdwLen = FFAT_WCSLEN(psName);

	FFAT_ASSERT(*pdwLen <= FAT_DE_SFN_MAX_LENGTH);

#ifdef FFAT_VFAT_SUPPORT
	_adjustSFNCase(psName, psName, *pdwLen, (pDE->bNTRes & FAT_DE_SFN_ALL_LOWER));	// mask case bit
#endif

	return FFAT_OK;
}


/**
* generates short file name for volume name from directory entry
*
* @param		pVI	: [IN] volume information
* @param		pDE			: [IN] SFN entry
* @param		psName		: [OUT] Name string storage
*								Storage size should be 256 characters or more.
* @return		check sum	: 
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		JAN-14-2007 [DongYoung Seo] First Writing., comes from TFS4 1.6
*/
FFatErr
ffat_fs_de_genShortVolumeLabel(FatVolInfo* pVI, FatDeSFN* pDE, t_wchar* psName)
{

	t_int32		i;
	t_uint8		szBuffer[16]; // name(8) + dot(1) + ext.(3) + NULL(1)

	FFAT_ASSERTP(pDE != NULL, (_T("pDE cannot be NULL")));
	FFAT_ASSERTP(psName != NULL, (_T("pOemName cannot be NULL")));

	FFAT_MEMCPY(szBuffer, pDE->sName, FAT_SFN_NAME_CHAR);

	for(i = (FAT_SFN_NAME_CHAR - 1); i >= 0; i--)
	{
		if (szBuffer[i] != 0x20)
		{
			break;
		}
	}
	
	IF_UK (i < 0)
	{
		FFAT_LOG_PRINTF((_T("there is no valid character ")));
		return FFAT_EINVALID;
	}

	// check for KANJI
	if (szBuffer[0] == FAT_DE_CHAR_FOR_KANJI)
	{
		szBuffer[0] = FAT_DE_FREE;
	}

	i++;

	FFAT_ASSERTP(i < sizeof(szBuffer), (_T("something goes wrong!")));
	FFAT_ASSERTP(i != 0, (_T("corrupted directory entry - this cannot happen")));

	// terminate the string.
	szBuffer[i] = 0x00;

	// finally, convert it to UNICODE
	FFAT_MBSTOWCS(psName, (FAT_DE_SFN_MAX_LENGTH + 1), (const char*)szBuffer, (i + 1), VI_DEV(pVI));

#ifdef FFAT_VFAT_SUPPORT
	_adjustSFNCase(psName, psName, FFAT_WCSLEN(psName), pDE->bNTRes);
#endif

	return FFAT_OK;
}


/**
 * adjust name to FAT filesystem format
 *
 * 입력된 이름을 FAT filesystem에서 사용할 수 있는지 점검한다.
 * 이름 변경 내용 
 *		1. 이름의 끝부분에 있는 '.'과 ' ' 를 제거한다.
 *		2. file 이름으로 사용할 수 있는 문자가 있는지 확인한다.
 *
 * 이름의 변경이 있을 경우에는 변경된 length를 다시 구하지 않도록
 * pdwLen에 저장하여 return 한다.
 *
 * pDE가 NULL이 아닐 경우는 Short File Name Entry의 name 부분에
 * multi-byte로 변환된 이름을 저장하여 return 한다.
 *
 * @param		pVI		: [IN] volume information
 * @param		psName			: [IN] Name string, this string will be modified
 * @param		pdwLen			: [IN/OUT] character count
 *									[IN] character count before modification
 *											if 0 : there is no length information
 *									[OUT] character count after modification
 * @param		pdwNamePartLen	: [OUT] character count of name part of long filename
 * @param		pdwExtPartLen	: [OUT] character count of extension part of long filename
 * @param		pdwSfnNameSize	: [OUT] byte size of name part of short filename
 * @param		pdwNameType		: [OUT] Name type
 * @param		pDE				: [IN/OUT] short file name entry
 *									[IN] storage pointer
 *									[OUT] generated short file name
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_ETOOLONG	: too long name
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing., comes from TFS4 1.6
 * @version		MAR-23-2009 [DongYoung Seo] add error checking code for ffat_fs_de_removeTrailingDotAndBlank()
 *											remove VFAT SUPPORT checking for ffat_fs_de_removeTrailingDotAndBlank()
 * @version		JUN-17-2009 [JeongWoo Park] Add the code to support OS specific character set
 */
FFatErr
ffat_fs_de_adjustNameToFatFormat(FatVolInfo* pVI, t_wchar* psName, t_int32* pdwLen, 
						t_int32* pdwNamePartLen, t_int32* pdwExtPartLen, t_int32* pdwSfnNameSize,
						FatNameType* pdwNameType, FatDeSFN* pDE)
{
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pdwLen);
	FFAT_ASSERT(pdwNamePartLen);
	FFAT_ASSERT(pdwExtPartLen);
	FFAT_ASSERT(pdwSfnNameSize);
	FFAT_ASSERT(pdwNameType);
	FFAT_ASSERT(pDE);

	// remove trailing dot and space
	if ((VI_FLAG(pVI) & VI_FLAG_OS_SPECIFIC_CHAR) == 0)
	{
		r = ffat_fs_de_removeTrailingDotAndBlank(psName, pdwLen);
		FFAT_EO(r, (_T("Fail to check trailing dot and blank")));
	}
	else
	{
		if (*pdwLen == 0)
		{
			*pdwLen = FFAT_WCSLEN(psName);
		}
	}


	// check name string length
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT  
	 IF_UK (*pdwLen > FFAT_NAME_MAX_LENGTH)
	 {
		  FFAT_LOG_PRINTF((_T("Too long name string")));
		  r = FFAT_ETOOLONG;
		  goto out;
	 }
#else
	  IF_UK (*pdwLen > FAT_DE_SFN_MAX_LENGTH) //FAT_DE_SFN_MAX_LENGTH : 12, short file name의최대길이(dot 포함, Trailing NULL 제외)
	 {
		  FFAT_LOG_PRINTF((_T("Too long name string")));
		  r = FFAT_ETOOLONG;
		  goto out;
	 }
#endif

	IF_UK (*pdwLen == 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid string length")));
		return FFAT_EINVALID;
	}

	// check name validity
	if ((VI_FLAG(pVI) & VI_FLAG_OS_SPECIFIC_CHAR) == 0)
	{
		r = _IsValidCharacter(psName, *pdwLen);
	}
	else
	{
		r = _IsValidCharacterInOS(psName, *pdwLen);
	}
	
	IF_UK (r == FFAT_FALSE)
	{
		r = FFAT_EINVALID;
		FFAT_LOG_PRINTF((_T("invalid char is in the name")));
		goto out;
	}

#ifndef FFAT_NO_CHECK_RESERVED_NAME
	// check name validity
	if ((*pdwLen == 3) || (*pdwLen == 4))
	{
		r = _IsValidName(pVI, psName, *pdwLen);
		IF_UK (r == FFAT_FALSE)
		{
			r = FFAT_EINVALID;
			FFAT_LOG_PRINTF((_T("invalid char is in the name")));
			goto out;
		}
	}
#endif

	// generate name for directory entry
	// Directory entry에 들어갈 수 있는 short file name을 생성한다.
	r = _genShortNameForDe(pVI, psName, *pdwLen, pdwNamePartLen, pdwExtPartLen,
							pdwSfnNameSize, pDE, pdwNameType);
	FFAT_EO(r, (_T("fail to generate short file name for directory entry")));

#ifndef FFAT_VFAT_SUPPORT
	FFAT_ASSERT(*pdwNameType & FAT_NAME_SFN);
	FFAT_ASSERT((VI_FLAG(pVI) & VI_FLAG_LFN_ONLY) == 0);
#endif

	FFAT_ASSERT(*pdwLen <= FFAT_NAME_MAX_LENGTH);

	if (VI_FLAG(pVI) & VI_FLAG_LFN_ONLY)
	{
		// If LFN is only used, do not use SFN & numeric tail
		*pdwNameType |= FAT_NAME_LFN_CHAR;
		*pdwNameType &= (~FAT_NAME_SFN);
		*pdwNameType &= (~FAT_NAME_NUMERIC_TAIL);

		// generate Dummy SFN
		_genDummyShortNameForDe(pDE, pdwSfnNameSize);
	}

	r = FFAT_OK;

	FFAT_ASSERT(*pdwLen <= FFAT_NAME_MAX_LENGTH);
	FFAT_ASSERT(*pdwNamePartLen <= FFAT_NAME_MAX_LENGTH);
	FFAT_ASSERT(*pdwExtPartLen <= FFAT_NAME_MAX_LENGTH);
	FFAT_ASSERT(*pdwSfnNameSize <= FFAT_NAME_MAX_LENGTH);

out:
	return r;
}


/**
* check name is valid for volume name
*
* 입력된 이름을 FAT filesystem의 volume name으로 사용할 수 있는지 점검한다.
* Caution.
*	volume name은 SFNE에 저장되는 name과 제약 조건이 다르다.
*	- 사용될수 없는 character는 동일하다.
*	- '.'은 사용 될 수 없다.
*	- ' '(blank)를 사용 할 수 있다.
*	- 대문자로만 저장이 된다.
*
* pDE가 NULL이 아닐 경우는 Short File Name Entry의 name 부분에
* multi-byte로 변환된 이름을 저장하여 return 한다.
*
* @param		psName			: [IN] Name string, this string will be modified
* @return		FFAT_TRUE		: success
* @return		FFAT_EINVALID	: Invalid volume name
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		JAN-14-2007 [DongYoung Seo] First Writing
*/
FFatErr
ffat_fs_de_isValidVolumeLabel(t_wchar* psName)
{
	t_int32		i;
	t_int32		dwLen;

	FFAT_ASSERT(psName);

	dwLen = FFAT_WCSLEN(psName);

	IF_UK ((dwLen <= 0) || (dwLen > FAT_SFN_NAME_CHAR))
	{
		FFAT_LOG_PRINTF((_T("Invalid string length")));
		return FFAT_EINVALID;
	}

	// check is the string has Dot
	for (i = 0; i < dwLen; i++)
	{
		IF_UK (FFAT_IS_VALID_FOR_SFN(psName[i]) == FFAT_FALSE)
		{
			FFAT_LOG_PRINTF((_T("Invalid character for volume name")));
			FFAT_DEBUG_PRINTF((_T("'%c' is not a valid character for volume name"), (t_int8)psName[i]));
			return FFAT_EINVALID;
		}

		IF_UK (psName[i] == '.')
		{
			FFAT_LOG_PRINTF((_T("Invalid character(.) for volume name")));
			return FFAT_EINVALID;
		}
	}

	return FFAT_TRUE;
}


/**
 * adjust name to FAT filesystem volume format
 *
 * 입력된 이름을 FAT filesystem의 volume name으로 사용할 수 있는지 점검한다.
 * Caution.
 *	volume name은 SFNE에 저장되는 name과 제약 조건이 다르다.
 *	- 사용될수 없는 character는 동일하다.
 *	- '.'은 사용 될 수 없다.
 *	- ' '(blank)를 사용 할 수 있다.
 *	- 대문자로만 저장이 된다.
 *
 * pDE가 NULL이 아닐 경우는 Short File Name Entry의 name 부분에
 * multi-byte로 변환된 이름을 저장하여 return 한다.
 *
 * @param		pVI	: [IN] volume information
 * @param		psName		: [IN] Name string, this string will be modified
 * @param		dwLen		: [IN] Length of psName (character count)
 * @param		pDE			: [IN/OUT] short file name entry
 *								[IN] storage pointer, may be NULL
 *								[OUT] generated short file name
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_ETOOLONG	: too long name
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		JAN-14-2007 [DongYoung Seo] First Writing
 */
FFatErr
ffat_fs_de_adjustNameToVolumeLabel(FatVolInfo* pVI, t_wchar* psName,
									t_int32 dwLen, FatDeSFN* pDE)
{
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pDE);

#ifdef FFAT_STRICT_CHECK
	IF_UK ((dwLen <= 0) || (dwLen > FAT_SFN_NAME_CHAR))
	{
		FFAT_LOG_PRINTF((_T("Invalid string length")));
		return FFAT_EINVALID;
	}
#endif

	r = ffat_fs_de_isValidVolumeLabel(psName);
	if (r != FFAT_TRUE)
	{
		FFAT_PRINT_DEBUG((_T("invalid name for FAT filesystem")));
		goto out;
	}

	// generate name for directory entry
	// Directory entry에 들어갈 수 있는 short file name을 생성한다.
	r = _genShortNameForVolumeDe(pVI, psName, dwLen, pDE);
	FFAT_EO(r, (_T("fail to generate short file name for directory entry")));

	r = FFAT_OK;

out:
	return r;
}


/**
 * Retrieve numeric tail from SFN Entry
 * SFN에서 Numeric Tail을 추출하여 return한다.
 *
 * @param		pDE			: [IN] short file name entry
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-15-2006 [DongYoung Seo] First Writing., 
 */
t_int32
ffat_fs_de_getNumericTail(FatDeSFN* pDE)
{
	t_int32		dwNumericTail;
	t_int32		i;

	FFAT_ASSERT(pDE);

	for (i = 7; i > 0; i--)
	{
		if (pDE->sName[i] == '~')
		{
			break;
		}

		if (pDE->sName[i] == 0x20)
		{
			continue;
		}

		if (_IS_DIGIT(pDE->sName[i]) == FFAT_FALSE)
		{
			// there is a character that is not a digit.
			// so there is no numeric tail
			return 0;
		}
	}

	if (i == 0)
	{
		// there is no numeric tail
		return 0;
	}

	dwNumericTail = 0;

	// here we guarantee the character after index i are all digit.
	i++;
	for ( /* None */; i < 8; i++)
	{
		if (pDE->sName[i] == 0x20)
		{
			break;
		}
		dwNumericTail = (dwNumericTail * 10) + (pDE->sName[i] - '0');
	}

	return dwNumericTail;
}

#ifdef FFAT_VFAT_SUPPORT
	/**
	 * generate LFNE from name
	 *
	 * @param		psName			: [IN] Name string
	 * @param		wNameLen		: [IN] name length
	 * @param		pDE				: [IN] directory entry
	 * @param		pdwEntryCount	: [IN] directory entry count
	 * @param		bCheckSum		: [IN] check sum
	 * @return		check sum
	 * @author		DongYoung Seo (dy76.seo@samsung.com)
	 * @version		AUG-16-2006 [DongYoung Seo] First Writing., 
	 */
	FFatErr
	ffat_fs_de_genLFNE(t_wchar* psName, t_int16 wNameLen, FatDeLFN* pDE, 
						t_int32* pdwEntryCount, t_uint8 bCheckSum)
	{
		t_int32		dwEntryCount;
		t_int32		i;
		t_uint8		bOrder = 1;		// entry order
		t_int16		wCur = 0;		// current character position
		t_wchar		psLast[FAT_LFN_NAME_CHAR + 1];

		FFAT_ASSERT(psName);
		FFAT_ASSERT(wNameLen > 0);
		FFAT_ASSERT(wNameLen <= FFAT_NAME_MAX_LENGTH);
		FFAT_ASSERT(pDE);

		dwEntryCount = EssMath_CeilingDivide(wNameLen, FAT_LFN_NAME_CHAR);
		*pdwEntryCount = dwEntryCount;

		for (i = (dwEntryCount - 1); i >= 0; i--)
		{
			pDE[i].bOrder		= bOrder++;
			pDE[i].bAttr		= FFAT_ATTR_LONG_NAME;
			pDE[i].bLongType	= 0x00;
			pDE[i].bChecksum	= bCheckSum;
			pDE[i].wFstClusLo	= 0x0000;

			if (i == 0)
			{
				// the last entry
				pDE[0].bOrder	|= FAT_DE_LAST_LFNE_MASK;
				FFAT_MEMSET(psLast, 0xFF, FAT_LFN_NAME_CHAR * sizeof(t_wchar));
				FFAT_WCSNCPY(psLast, &psName[wCur], (wNameLen - wCur));

				if ((wNameLen - wCur) < FAT_LFN_NAME_CHAR)
				{
					psLast[wNameLen - wCur] = 0x0000;
				}

				FFAT_ASSERT((wNameLen - wCur) <= FAT_LFN_NAME_CHAR);
				wCur = 0;

				FFAT_MEMCPY(pDE[i].sName1, &psLast[wCur], FAT_LFN_NAME1_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME1_CHAR;
				FFAT_MEMCPY(pDE[i].sName2, &psLast[wCur], FAT_LFN_NAME2_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME2_CHAR;
				FFAT_MEMCPY(pDE[i].sName3, &psLast[wCur], FAT_LFN_NAME3_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME3_CHAR;
			}
			else
			{
				FFAT_MEMCPY(pDE[i].sName1, &psName[wCur], FAT_LFN_NAME1_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME1_CHAR;
				FFAT_MEMCPY(pDE[i].sName2, &psName[wCur], FAT_LFN_NAME2_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME2_CHAR;
				FFAT_MEMCPY(pDE[i].sName3, &psName[wCur], FAT_LFN_NAME3_CHAR * sizeof(t_wchar));
				wCur += FAT_LFN_NAME3_CHAR;
			}
		}

		// adjust byte order
	#ifdef FFAT_BIG_ENDIAN
		for (i = 0; i < dwEntryCount; i++)
		{
			// unfold loop for performance
			FFAT_MEMCPY(psLast, pDE[i].sName1, FAT_LFN_NAME1_CHAR * sizeof(t_wchar));
			psLast[0] = FFAT_BO_UINT16(psLast[0]);
			psLast[1] = FFAT_BO_UINT16(psLast[1]);
			psLast[2] = FFAT_BO_UINT16(psLast[2]);
			psLast[3] = FFAT_BO_UINT16(psLast[3]);
			psLast[4] = FFAT_BO_UINT16(psLast[4]);
			FFAT_MEMCPY(pDE[i].sName1, psLast, FAT_LFN_NAME1_CHAR * sizeof(t_wchar));

			FFAT_MEMCPY(psLast, pDE[i].sName2, FAT_LFN_NAME2_CHAR * sizeof(t_wchar));
			psLast[0] = FFAT_BO_UINT16(psLast[0]);
			psLast[1] = FFAT_BO_UINT16(psLast[1]);
			psLast[2] = FFAT_BO_UINT16(psLast[2]);
			psLast[3] = FFAT_BO_UINT16(psLast[3]);
			psLast[4] = FFAT_BO_UINT16(psLast[4]);
			psLast[5] = FFAT_BO_UINT16(psLast[5]);
			FFAT_MEMCPY(pDE[i].sName2, psLast, FAT_LFN_NAME2_CHAR * sizeof(t_wchar));

			FFAT_MEMCPY(psLast, pDE[i].sName3, FAT_LFN_NAME3_CHAR * sizeof(t_wchar));
			psLast[0] = FFAT_BO_UINT16(psLast[0]);
			psLast[1] = FFAT_BO_UINT16(psLast[1]);
			FFAT_MEMCPY(pDE[i].sName3, psLast, FAT_LFN_NAME3_CHAR * sizeof(t_wchar));
		}
	#endif

		return FFAT_OK;
	}

	/**
	* find the long file name entries in cluster before SFNE
	*
	*
	* @param	pVI					: [IN] volume pointer
	* @param	dwCluster			: [IN] cluster number
	* @param	dwStartOffset		: [IN] start offset to scan in cluster
	*									This is the relative offset from the start cluster of parent directory.
	* @param	bCheckSum			: [IN] checksum of SFNE
	* @param	pbPrevLFNOrder		: [IN/OUT] previous order of LFNE
	* @param	pdwFoundLFNECount	: [OUT] count of the founded LFNE in current cluster
	*									(internally initialized to 0)
	* @param	pCxt				: [IN] context of current operation
	* @return	FFAT_OK				: LFN is found but partial LFNE set. it needs to keep scanning previous clusters
	* @return	FFAT_DONE			: LFN is found fully.
	* @return	FFAT_ENOENT			: LFN is not found
	* @return	else				: error
	* @author	JeongWoo Park
	* @version	FEB-12-2009 [JeongWoo Park] First Writing.
	*/
	FFatErr
	ffat_fs_de_findLFNEsInCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwStartOffset,
								t_uint8 bCheckSum, t_uint8* pbPrevLFNOrder,
								t_uint32* pdwFoundLFNECount, ComCxt* pCxt)
	{
		FFatErr				r;
		t_uint32			dwFirstSectorofCluster;	// the first sector of dwCluster
		t_uint32			dwCurSector;			// the current working cluster
		t_uint32			dwCurOffsetInCluster;	// the current working offset in dwCurCluster
		t_int32				dwDEIndexInSector;		// the DE index in sector
		FatDeLFN*			pLFNE;					// LFNE pointer
		FFatfsCacheEntry*	pEntry = NULL;			// cache entry

		FFAT_ASSERT(pVI);
		FFAT_ASSERT(pbPrevLFNOrder);
		FFAT_ASSERT(pdwFoundLFNECount);

		*pdwFoundLFNECount = 0;

		if (dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
			dwFirstSectorofCluster	= VI_FRS(pVI);
			dwCurOffsetInCluster	= dwStartOffset;
		}
		else
		{
			dwFirstSectorofCluster	= FFATFS_GET_FIRST_SECTOR(pVI, dwCluster);
			dwCurOffsetInCluster	= dwStartOffset & VI_CSM(pVI);
		}

		dwCurSector = dwFirstSectorofCluster + (dwCurOffsetInCluster >> VI_SSB(pVI));
		dwDEIndexInSector = (dwCurOffsetInCluster & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS;

		do
		{
			r = ffat_fs_cache_getSector(dwCurSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
			FFAT_EO(r, (_T("fail to get sector from ffatfs cache")));

			do
			{
				pLFNE = (FatDeLFN*)(pEntry->pBuff) + dwDEIndexInSector;
				
				// Check validity of LFN
				//	1) CHECKSUM 2) ORDER 3) LFN Attribute 4) FREE
				if ((pLFNE->bChecksum != bCheckSum) ||
					((pLFNE->bOrder & ~FAT_DE_LAST_LFNE_MASK) != *pbPrevLFNOrder + 1) ||
					((pLFNE->bAttr & FFAT_ATTR_LONG_NAME_MASK) != FFAT_ATTR_LONG_NAME) ||
					(pLFNE->bOrder == FAT_DE_FREE))
				{
					r = FFAT_ENOENT;
					goto out;
				}

				// update order, LFNECount
				(*pbPrevLFNOrder)++;
				(*pdwFoundLFNECount)++;

				if ((pLFNE->bOrder & FAT_DE_LAST_LFNE_MASK) == FAT_DE_LAST_LFNE_MASK)
				{
					// found last LFNE, no need to scan any more
					r = FFAT_DONE;
					goto out;
				}

				dwDEIndexInSector--;
			} while(dwDEIndexInSector >= 0);
			
			r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
			FFAT_EO(r, (_T("fail to release a ffatfs cache entry")));

			pEntry = NULL;

			dwDEIndexInSector = (VI_SS(pVI) >> FAT_DE_SIZE_BITS) - 1;
			dwCurSector--;
		} while(dwCurSector >= dwFirstSectorofCluster);

		// need to scan the previous cluster
		r = FFAT_OK;

	out:
		if (pEntry != NULL)
		{
			FFatErr rr;

			rr = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
			IF_UK(rr < 0)
			{
				FFAT_PRINT_DEBUG((_T("fail to release a ffatfs cache entry")));
				r |= rr;
			}
		}

		return r;
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT


/*
 * generate SFNE from name
 *
 * @param		pVI		: [IN] volume information
 * @param		psName			: [IN] Name string
 * @param		dwNameLen		: [IN] name length
 * @param		pDE				: [IN] directory entry
 * @return		FFAT_OK			: Success
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		SETP-05-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_fs_de_genSFNE(FatVolInfo* pVI, t_wchar* psName, t_int32 dwNameLen, FatDeSFN* pDE)
{
	t_int32		dwNamePartLen;
	t_int32		dwExtPartLen;
	t_int32		dwSfnNameSize;
	FatNameType	dwNameType;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pDE);

	return _genShortNameForDe(pVI, psName, dwNameLen, &dwNamePartLen, &dwExtPartLen,
							&dwSfnNameSize, pDE, &dwNameType);
}


/**
 * FAT16의 root directory에 data를 write 한다.
 *
 * 주의 !!!
 * 반드시 FAT16의 root directory에만 사용하여야한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwOffset		: [IN] IO start offset
 * @param		pBuff			: [IN] buffer for read/write
 * @param		dwSize			: [IN] IO size in byte
 * @param		dwFlag			: [IN] cache flag
 * @param		bRead			: [IN]	FFAT_FALSE	: Write
 *										FFAT_TRUE	: Read
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		0 or above		: read/write size in byte
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
t_int32
ffat_fs_de_readWriteOnFat16Root(FatVolInfo* pVI, t_int32 dwOffset, t_int8* pBuff,
				t_int32 dwSize, FFatCacheFlag dwFlag, t_boolean bRead, void* pNode)
{
	FFatErr			r;
	t_uint32		dwSector;		// IO sector number
	t_int32			dwCount;		// IO sector count
	FFatCacheInfo	stCI;
	t_int32			dwIOSize;		// IO size

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwOffset >= 0);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwSize > 0);
	FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_DE);

	dwIOSize = dwSize;

	// check the last offset is in root directory
	dwSector = VI_FRS(pVI) + ((dwOffset + dwIOSize - 1) >> VI_SSB(pVI));

	IF_UK (dwSector > VI_LRS(pVI))
	{
		FFAT_LOG_PRINTF((_T("write is over root directory size")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVI));

	dwSector = VI_FRS(pVI) + (dwOffset >> VI_SSB(pVI));

	// read/write partial head
	if (dwOffset & VI_SSM(pVI))
	{
		r = ffat_fs_misc_readWritePartialSector(pVI, dwSector, (dwOffset & VI_SSM(pVI)),
						dwIOSize, pBuff, dwFlag, &stCI, bRead);
		FFAT_ER(r, (_T("fail to read write root directory")));

		dwIOSize	-= r;
		pBuff		+= r;
		dwOffset	+= r;
		dwSector++;

		if (dwIOSize == 0)
		{
			goto out;
		}
	}

	FFAT_ASSERT((dwOffset & VI_SSM(pVI)) == 0);

	if (dwIOSize > VI_SS(pVI))
	{
		dwCount = dwIOSize >> VI_SSB(pVI);

		// read/write body
		if (bRead == FFAT_TRUE)
		{
			r = ffat_fs_cache_readSector(dwSector, pBuff, dwCount, dwFlag, pVI);
		}
		else
		{
			r = ffat_fs_cache_writeSector(dwSector, pBuff, dwCount, dwFlag, pVI, pNode);
		}

		IF_UK (r != dwCount)
		{
			FFAT_LOG_PRINTF((_T("Fail to read/write root directory ")));
			return FFAT_EIO;
		}

		dwIOSize	-= (dwCount << VI_SSB(pVI));
		pBuff		+= (dwCount << VI_SSB(pVI));
		dwOffset	+= (dwCount << VI_SSB(pVI));
		dwSector	+= dwCount;

		if (dwIOSize == 0)
		{
			goto out;
		}
	}

	// read/write partial tail
	FFAT_ASSERT((dwOffset & VI_SSM(pVI)) == 0);
	FFAT_ASSERT(dwIOSize <= VI_SS(pVI));

	r = ffat_fs_misc_readWritePartialSector(pVI, dwSector, (dwOffset & VI_SSM(pVI)), 
						dwIOSize, pBuff, dwFlag, &stCI, bRead);
	FFAT_ER(r, (_T("fail to read write root directory")));
	dwIOSize -= r;

out:
	FFAT_ASSERT(dwIOSize == 0);

	return dwSize;

}


/**
 * write directory entry
 *
 * write 이전에 반드시 충분한 cluster가 확보 되어야 한다.
 *
 * @param		pVI	: [IN] volume information
 * @param		dwCluster	: [IN] first write cluster
 *									FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
 * @param		dwOffset	: [IN] write start offset from dwCluster
 * @param		pBuff		: [IN] write data
 * @param		dwSize		: [IN] write size in byte
 * @param		dwFlag		: [IN] cache flag
 * @param		pNode		: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		FFAT_EIO		: I/O error
 * @return		FFAT_EINVALID	: invalid parameter
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-28-2006 [DongYoung Seo] First Writing
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @version		DEC-20-2008 [DongYoung Seo] change function operation to use offset over cluster size
 */
FFatErr
ffat_fs_de_write(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwOffset, t_int8* pBuff,
				t_int32 dwSize, FFatCacheFlag dwFlag, void* pNode)
{
	FFatCacheInfo	stCI;
	FFatErr			r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwCluster > 0);

	if (dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);

		r = ffat_fs_de_readWriteOnFat16Root(pVI, dwOffset, pBuff, dwSize,
							dwFlag, FFAT_FALSE, pNode);
		IF_UK (r != dwSize)
		{
			FFAT_LOG_PRINTF((_T("fail to write directory entry")));
			return FFAT_EIO;
		}

		return FFAT_OK;
	}

#ifdef FFAT_STRICT_CHECK
	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) != FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster ")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	if (dwOffset >= VI_CS(pVI))
	{
		r = ffat_fs_fat_getClusterOfOffset(pVI, dwCluster, dwOffset, &dwCluster);
		FFAT_ER(r, (_T("Fail to get cluster of offset")));

		dwOffset = dwOffset & VI_CSM(pVI);
	}

	FFAT_ASSERT(dwOffset < VI_CS(pVI));

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVI));

	do
	{
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		r = ffat_fs_fat_readWritePartialCluster(pVI, dwCluster, dwOffset, dwSize,
						pBuff, dwFlag, &stCI, FFAT_FALSE, FFAT_FALSE);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to write partial cluster")));
			return FFAT_EIO;
		}

		dwSize	-= r;

		if (dwSize <= 0)
		{
			FFAT_ASSERT(dwSize == 0);
			break;
		}

		pBuff	+= r;
		FFAT_ASSERT((((dwOffset + r) & VI_CSM(pVI)) == 0) || (dwSize == 0));
		FFAT_ASSERT(r > 0);
		//dwOffset += r;
		dwOffset = 0;

		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		FFAT_ER(r, (_T("fail to get next cluster")));

	} while (dwSize > 0);

	return FFAT_OK;
}


/**
 * delete directory entry
 *
 *
 * @param		pVI		: [IN] volume information
 * @param		dwCluster		: [IN] first write cluster
 *									FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
 * @param		dwOffset		: [IN] write start offset
 * @param		dwCount			: [IN] delete entry count
 * @param		bLookupDelMark	: [IN] flag for looking up deletion mark for efficiency.deletion mark
										FFAT_TRUE : check directory entry to set it 0x00(FAT_DE_END_OF_DIR)
										FFAT_FALS : write 0xE5(FAT_DE_FREE) at the head of entry
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-31-2006 [DongYoung Seo] First Writing,
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
ffat_fs_de_delete(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwOffset,
									t_int32 dwCount, t_boolean bLookupDelMark,
									FFatCacheFlag dwFlag, void* pNode)
{
	FFatErr		r;
	t_int32		dwEntryOffset;		// entry offset in a cluster
	t_int32		dwEntryPerCluster;	// DE count per a cluster
	t_int32		dwDeleteCount;		// current delete count

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwCluster > 0);

	if (dwCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);

		r = _deleteOnFat16Root(pVI, dwOffset, dwCount, bLookupDelMark, dwFlag, pNode);
		FFAT_ER(r, (_T("fail to write directory entry")));

		return FFAT_OK;
	}

#ifdef FFAT_STRICT_CHECK
	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) != FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster ")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	FFAT_DEBUG_DE_PRINTF((_T("delete DE, cluster/offset/count:%d/%d/%d"), dwCluster, dwOffset, dwCount));

	if (dwOffset >= VI_CS(pVI))
	{
		r = ffat_fs_fat_getClusterOfOffset(pVI, dwCluster, dwOffset, &dwCluster);
		FFAT_ER(r, (_T("Fail to get cluster of offset")));

		dwOffset = dwOffset & VI_CSM(pVI);
	}

	FFAT_ASSERT(dwOffset < VI_CS(pVI));

	dwEntryOffset		= (dwOffset & VI_CSM(pVI)) >> FAT_DE_SIZE_BITS;
	dwEntryPerCluster	= VI_CS(pVI) >> FAT_DE_SIZE_BITS;

	dwDeleteCount		= dwEntryPerCluster - dwEntryOffset;

	do
	{
		// adjust delete count
		if (dwCount < dwDeleteCount)
		{
			dwDeleteCount = dwCount;
		}

		// delete entry in a cluster
		r = _deleteInCluster(pVI, dwCluster, (dwEntryOffset & (dwEntryPerCluster - 1)), 
								dwDeleteCount, bLookupDelMark, dwFlag, pNode);
		FFAT_ER(r, (_T("fail to delete DE In a cluster")));

		dwCount -= dwDeleteCount;

		if (dwCount == 0)
		{
			break;
		}

		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCluster, &dwCluster);
		FFAT_ER(r, (_T("fail to get next cluster")));

		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

		dwEntryOffset = 0;
		dwDeleteCount = dwEntryPerCluster;

	} while (1);

	return FFAT_OK;
}


/**
 * get volume name
 *
 * This function just update volume name entry on root directory.
 * It does not update boot sector, boot sector is updated on FFATFS_BS module
 *
 * @param		pVI		: [IN] volume information
 * @param		psVolLabel		: [OUT] volume name storage
 * @param		dwVolLabelLen	: [IN] character count that can be stored at psVolLabel
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @return		FFAT_ENOMEM		: Not enough memory
 * @return		FFAT_ENOENT		: There is no volume name on root directory
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_fs_de_getVolLabel(FatVolInfo* pVI, t_wchar* psVolLabel, t_int32 dwVolLabelLen)
{
	t_uint32	dwSector = 0;	// sector number for volume name
	t_uint32	dwOffset = 0;	// offset for volume name
	FatDeSFN*	pDE = NULL;
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psVolLabel);

	IF_UK (dwVolLabelLen < (FAT_SFN_NAME_CHAR + 1))
	{
		FFAT_LOG_PRINTF((_T("To small name buffer size for Volume name")));
		return FFAT_EINVALID;
	}

	FFAT_MEMSET(psVolLabel, 0x00, (FAT_SFN_NAME_CHAR + 1) * sizeof(t_wchar));

	pDE = (FatDeSFN*) FFAT_LOCAL_ALLOC(sizeof(FatDeSFN), VI_CXT(pVI));
	FFAT_ASSERT(pDE != NULL);

	// lookup directory entry for volume name.
	r = _getVolumeLabelEntry(pVI, pDE, &dwSector, &dwOffset);
	if (r == FFAT_ENOENT)
	{
		goto out;
	}

	r = ffat_fs_de_genShortVolumeLabel(pVI, pDE, psVolLabel);
	FFAT_EO(r, (_T("fail to get volume name from directory entry")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pDE, sizeof(FatDeSFN), VI_CXT(pVI));

	return r;
}


/**
 * set volume name to psVolLabel
 *
 * @param		pVI		: [IN] volume information
 * @param		psVolLabel		: [IN] new volume name
 * @param		bFormat			: [IN] flag for format routine
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 *									Invalid volume name
 * @return		FFAT_EIO		: IO error
 * @return		FFAT_ENOMEM		: Not enough memory
 * @return		FFAT_ENOENT		: There is no volume name on root directory
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 * @version		NOV-21-2008 [DongYoung Seo] add flag for format
 * @version		NOV-30-2009 [JW Park] add the initialization code of pDE
 */
FFatErr
ffat_fs_de_setVolLabel(FatVolInfo* pVI, t_wchar* psVolLabel, t_boolean bFormat)
{
	t_uint32	dwSector = 0;	// sector number for volume name
	t_uint32	dwOffset = 0;	// offset for volume name
	FatDeSFN*	pDE = NULL;
	t_wchar*	psVolLabelTemp = NULL;
	t_int32		dwLen;			// length of name
	t_int8*		pmbVolLabel;		// multi byte volume name
	t_int8*		pBuff = NULL;
	FFatErr		r;

	FFatfsCacheEntry*	pEntry = NULL;		// buffer cache entry

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psVolLabel);

	dwLen = FFAT_WCSLEN(psVolLabel);
	IF_UK ((dwLen <= 0) || (dwLen > FAT_SFN_NAME_CHAR))
	{
		FFAT_LOG_PRINTF((_T("volume name length is wrong")));
		r = FFAT_EINVALID;
		goto out;
	}

	pBuff = FFAT_LOCAL_ALLOC(VI_SS(pVI), VI_CXT(pVI));
	FFAT_ASSERT(pBuff != NULL);

	pDE = (FatDeSFN*)pBuff;

	// temporally pDe is used for multi-byte name length checking
	pmbVolLabel = (t_int8*)pDE;
	r = FFAT_WCSTOMBS((char*)pmbVolLabel, sizeof(FatDeSFN), psVolLabel, (dwLen + 1), VI_DEV(pVI));
	if (r < 0)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to convert name (WCS->MBS)")));
		r = FFAT_EINVALID;
		goto out;
	}

	IF_UK (FFAT_STRLEN(pmbVolLabel) > FAT_SFN_NAME_CHAR)
	{
		FFAT_PRINT_DEBUG((_T("Too long volume name")));
		r = FFAT_EINVALID;
		goto out;
	}

	if (bFormat == FFAT_TRUE)
	{
		FFAT_MEMSET(pDE, 0x00, VI_SS(pVI));
	}
	else
	{
		// lookup directory entry for volume name.
		r = _getVolumeLabelEntry(pVI, pDE, &dwSector, &dwOffset);
		if (r < 0)
		{
			if (r == FFAT_ENOENT)
			{
				// initialize pDE
				FFAT_MEMSET(pDE, 0x00, sizeof(FatDeSFN));

				r = _getFreeDE(pVI, VI_RC(pVI), &dwSector, &dwOffset);
			}
			else
			{
				FFAT_PRINT_DEBUG((_T("Fail to get volume name entry")));
			}

			FFAT_EO(r, (_T("Fail to get free space for volume label")));
		}
	}

	psVolLabelTemp = (t_wchar*) FFAT_LOCAL_ALLOC((FAT_SFN_NAME_CHAR + 1) * sizeof(t_wchar), VI_CXT(pVI));
	FFAT_ASSERT(psVolLabelTemp != NULL);

	FFAT_WCSCPY(psVolLabelTemp, psVolLabel);

	r = ffat_fs_de_adjustNameToVolumeLabel(pVI, psVolLabelTemp, dwLen, pDE);
	FFAT_EO(r, (_T("invalid name for FAT filesystem")));

	pDE->bAttr		= FFAT_ATTR_VOLUME;

	r = ffat_fs_de_setDeTime(pDE, FAT_UPDATE_DE_ALL_TIME, NULL);
	FFAT_EO(r, (_T("fail to update time for root de")));

	if (bFormat == FFAT_TRUE)
	{
		// release a sector
		r = ffat_ldev_writeSector(VI_DEV(pVI), VI_FRS(pVI), pBuff, 1);
		FFAT_EO(r, (_T("fail to write volume name")));
	}
	else
	{
		// write directory entry
		r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
		FFAT_EO(r, (_T("fail to get sector")));

		// update DE
		FFAT_MEMCPY((pEntry->pBuff + dwOffset), pDE, sizeof(FatDeSFN));

		// release a sector
		r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_UNLOCK | FFAT_CACHE_DIRTY | FFAT_CACHE_SYNC), NULL);
		FFAT_EO(r, (_T("fail to put a cache entry")));
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(psVolLabelTemp, ((FAT_SFN_NAME_CHAR + 1) * sizeof(t_wchar)), VI_CXT(pVI));
	FFAT_LOCAL_FREE(pBuff, VI_SS(pVI), VI_CXT(pVI));

	return r;
}


/**
 * set volume name to psVolLabel
 *
 * @param		pVI		: [IN] volume information
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 *									Invalid volume name
 * @return		FFAT_EIO		: IO error
 * @return		FFAT_ENOMEM		: Not enough memory
 * @return		FFAT_ENOENT		: There is no volume name on root directory
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 * @version		NOV-21-2008 [DongYoung Seo] add flag for format
 */
FFatErr
ffat_fs_de_removeVolLabel(FatVolInfo* pVI)
{
	t_uint32	dwSector = 0;	// sector number for volume name
	t_uint32	dwOffset = 0;	// offset for volume name
	FatDeSFN*	pDE = NULL;
	FatDeSFN	stDE;
	FFatErr		r;

	FFatfsCacheEntry*	pEntry = NULL;		// buffer cache entry

	FFAT_ASSERT(pVI);

	// lookup directory entry for volume name.
	r = _getVolumeLabelEntry(pVI, &stDE, &dwSector, &dwOffset);
	if (r < 0)
	{
		if (r == FFAT_ENOENT)
		{
			r = FFAT_OK;
		}
		else
		{
			FFAT_PRINT_DEBUG((_T("Fail to get volume name entry")));
		}

		goto out;
	}

	// write directory entry
	r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
	FFAT_EO(r, (_T("fail to get sector")));

	pDE = (FatDeSFN*)(pEntry->pBuff + dwOffset);
	pDE->sName[0] = FAT_DE_FREE;

	// release a sector
	r = ffat_fs_cache_putSector(pVI, pEntry, (FFAT_CACHE_UNLOCK | FFAT_CACHE_DIRTY | FFAT_CACHE_SYNC), NULL);
	FFAT_EO(r, (_T("fail to put a cache entry")));

	r = FFAT_OK;

out:
	return r;
}


/**
* adjust name to FAT filesystem format
*
* @param		psName		: [IN] Name string, this string will be modified
* @param		pdwLen		: [IN/OUT] character count
*								[IN] character count before modification
*										if 0 : there is no length information
*								[OUT] character count after adjustment
* @return		FFAT_OK			: success
* @return		FFAT_EINVALID	: Invalid name
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		AUG-09-2006 [DongYoung Seo] First Writing.
* @version		SEP-19-2008 [DongYoung Seo] change static function to extern.
* @version		MAR-23-2009 [DongYoung Seo] add FFAT_ALLOW_TRAILING_SPACE_AND_DOT
*/
FFatErr
ffat_fs_de_removeTrailingDotAndBlank(t_wchar* psName, t_int32* pdwLen)
{
	t_int32		dwLen;

	FFAT_ASSERT(psName != NULL);
	FFAT_ASSERT(pdwLen);
	FFAT_ASSERT(*pdwLen >= 0);

	dwLen = *pdwLen;

	if (dwLen == 0)
	{
		dwLen = FFAT_WCSLEN(psName);
	}

#ifdef FFAT_ALLOW_TRAILING_SPACE_AND_DOT
	// this is FAT Spec.
	while ((dwLen > 0) &&
		((psName[dwLen - 1] == '.') ||
		(_IS_SPACE(psName[dwLen - 1]) == FFAT_TRUE)))
	{
		dwLen--;
	}
#else
	// this is Linux Requirement
	while ((dwLen > 0) &&
		((psName[dwLen - 1] == '.') ||
		(_IS_SPACE(psName[dwLen - 1]) == FFAT_TRUE)))
	{
		FFAT_PRINT_DEBUG((_T("Name has invalid character - trailing dot or space")));
		return FFAT_EINVALID;
	}
#endif

	psName[dwLen] = '\0';
	*pdwLen = dwLen;

	FFAT_ASSERTP(dwLen >= 0, (_T("string length can not be under 0")));

	return FFAT_OK;
}


//=============================================================================
//
// Static function
//

/**
 * generate short file name for directory entry
 *
 * 입력된 string으로 부터 short file name을 생성하여 directory entry에 저장한다.
 * 이때 ~xxx 와 같은 numeric tail을 붙지 않는다.
 * short file name 생성 과정에서 이름이 LFN인지 SFN인지 검사한다.
 *
 * @param		pVI		: [IN] volume information
 * @param		psName			: [IN] Name string, this string will be modified
 * @param		dwLen			: [IN] character count
 * @param		pdwNamePartLen	: [OUT] character count of name part of long filename
 * @param		pdwExtPartLen	: [OUT] character count of extension part of long filename
 * @param		pdwSfnNameSize	: [OUT] byte size of name part of short filename
 * @param		pDE				: [OUT] directory entry pointer,
 *									생성된 short file name이 저장된다.
 * @param		pdwNameType		: [OUT] name type storage
 *									LFN인지 SFN인지 저장하여 return 한다.
 * @return		FFAT_OK			: conversion success
 * @return		FFAT_EINVALID	: invalid name
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 * @version		JUN-17-2009 [JeongWoo Park] Add the code to support OS specific character set
 *											- consider tail dot.
 */
static FFatErr
_genShortNameForDe(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen,
					t_int32* pdwNamePartLen, t_int32* pdwExtPartLen,
					t_int32* pdwSfnNameSize, FatDeSFN* pDE, FatNameType* pdwNameType)
{
	FFatErr		r;
	t_int32		i;
	t_int32		dwLastPeoride;		// last period position
	t_int32		dwConvertSize;

	FatNameType	dwNamePart	= FAT_NAME_LOWER;			// name part type
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
	FatNameType	dwExtPart	= FAT_NAME_LOWER;			// extension part type
#endif	
	
	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pdwNamePartLen);
	FFAT_ASSERT(pdwExtPartLen);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pdwNameType);

#ifdef FFAT_VFAT_SUPPORT
	if (dwLen > FAT_DE_SFN_MAX_LENGTH)
	{
		*pdwNameType = 0;
	}
	else
	{
		// 초기 설정은 short file name, short file name이 아닌 조건이 있을 경우 LFN 설정
		*pdwNameType = FAT_NAME_SFN;
	}
#else
	*pdwNameType = FAT_NAME_SFN;
#endif

	i = dwLen;

	// skip the tail dot if use OS specific character
	// The last period(dot) is not in tail dot.
	if (VI_FLAG(pVI) & VI_FLAG_OS_SPECIFIC_CHAR)
	{
		for (/* None */; i > 0; i--)
		{
			if (psName[i - 1] != '.')
			{
				break;
			}
		}

		// '.','..' is invalid
		if ((i == 0) && (dwLen <= 2))
		{
			FFAT_LOG_PRINTF((_T("invalid name like '.', '..'")));
			return FFAT_EINVALID;
		}
	}

	// find the last period from the last character
	for (/* None */; i > 0; i--)
	{
		if (psName[i - 1] == '.')
		{
			break;
		}
	}

	dwLastPeoride = i;

	// 일단 이름의 모든 character는 대문자로 구성됨을 표시
	pDE->bNTRes = FAT_DE_SFN_ALL_UPPER;

	if (dwLastPeoride == 0)
	{
		// name에 .이 없는 경우
		r = _convertToOemName(pVI, psName, dwLen, pDE->sName, FAT_SFN_NAME_PART_LEN,
								&dwConvertSize, pdwNameType);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Invalid name string,  there is no character except '.' or ' ' !!")));
			return r;
		}

		FFAT_MEMSET(&pDE->sName[FAT_SFN_NAME_PART_LEN], 0x20, FAT_SFN_EXT_PART_LEN);

		*pdwNamePartLen	= dwLen;
		*pdwExtPartLen	= 0;
		*pdwSfnNameSize	= dwConvertSize;

#ifdef FFAT_VFAT_SUPPORT
		dwNamePart = *pdwNameType;

		_setDeSfnCaseType(pDE, dwNamePart, dwExtPart);
#else
		if (*pdwNamePartLen > FAT_SFN_NAME_PART_LEN)
		{
			*pdwNamePartLen = FAT_SFN_NAME_PART_LEN;
		}
#endif
	}
	else if (dwLastPeoride == 1)
	{
		// 이름이 .으로 시작하고 이후에 다시 '.'이 없을 경우
#ifdef FFAT_VFAT_SUPPORT
		// SFNE에는 실제 이름의 extension 부분이 name part에 저장된다.
		r = _convertToOemName(pVI, psName, dwLen, pDE->sName,
								FAT_SFN_NAME_PART_LEN, &dwConvertSize, pdwNameType);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Invalid name string,  there is no character except '.' or ' ' !!")));
			return r;
		}

		FFAT_MEMSET(&pDE->sName[FAT_SFN_NAME_PART_LEN], 0x20, FAT_SFN_EXT_PART_LEN);

		// short name이 아님을 설정한다.
		*pdwNameType &= (~FAT_NAME_SFN);

		*pdwNamePartLen	= dwLen;
		*pdwExtPartLen	= 0;
		*pdwSfnNameSize	= dwConvertSize;
#else
		FFAT_LOG_PRINTF((_T("invalid char is in the name")));
		return FFAT_EINVALID;
#endif
	}
	else
	{
		// check name part is all dot
		if (VI_FLAG(pVI) & VI_FLAG_OS_SPECIFIC_CHAR)
		{
			for (i = dwLastPeoride - 2; i >= 0; i--)
			{
				if (psName[i] != '.')
				{
					break;
				}
			}

			if (i < 0)
			{
				// If all name part is all dot, then extension part become name part.
				// If OS_SPECIFIC_CHAR is on, '.' will be store as '_' in SFN by _convertToOemName()
				// FAT 호환 버전과 동일하게 계산되기 위함.(DEC 등에서 이를 가정하고 있음)
				goto move_ext_to_name;
			}
		}

		// name과 ext 모두 존재하는 경우.
		r = _convertToOemName(pVI, psName, (dwLastPeoride - 1), pDE->sName,
							FAT_SFN_NAME_PART_LEN, &dwConvertSize, pdwNameType);
		if (r == FFAT_EINVALID)
		{
move_ext_to_name:
#ifdef FFAT_VFAT_SUPPORT
			// extension 부분을 name part에 저장한다.

			r = _convertToOemName(pVI, &psName[dwLastPeoride], (dwLen - dwLastPeoride),
								pDE->sName, FAT_SFN_NAME_PART_LEN, &dwConvertSize, pdwNameType);
			IF_UK (r == FFAT_EINVALID)
			{
				// extension part 역시 '.'과 ' '만 있는 경우 invalid return
				return FFAT_EINVALID;
			}

			FFAT_MEMSET(&pDE->sName[FAT_SFN_NAME_PART_LEN], 0x20, FAT_SFN_EXT_PART_LEN);

			// short name이 아님을 설정한다.
			*pdwNameType &= (~FAT_NAME_SFN);

			*pdwNamePartLen	= dwLen;
			*pdwExtPartLen	= 0;
			*pdwSfnNameSize	= dwConvertSize;
#else
			FFAT_LOG_PRINTF((_T("invalid char is in the name")));
			return FFAT_EINVALID;
#endif
		}
		else
		{
			dwNamePart = *pdwNameType;
			*pdwSfnNameSize	= dwConvertSize;

			r = _convertToOemName(pVI, &psName[dwLastPeoride], (dwLen - dwLastPeoride),
								&pDE->sName[FAT_SFN_NAME_PART_LEN], FAT_SFN_EXT_PART_LEN, &dwConvertSize, pdwNameType);

			*pdwNamePartLen	= dwLastPeoride - 1;
			*pdwExtPartLen	= dwLen - dwLastPeoride;

#ifdef FFAT_VFAT_SUPPORT
			dwExtPart = *pdwNameType;

			_setDeSfnCaseType(pDE, dwNamePart, dwExtPart);
#else
			if (r < 0)
			{
				return r;
			}

			if (*pdwNamePartLen > FAT_SFN_NAME_PART_LEN)
			{
				*pdwNamePartLen = FAT_SFN_NAME_PART_LEN;
			}

			if (*pdwExtPartLen > FAT_SFN_EXT_PART_LEN)
			{
				*pdwExtPartLen = FAT_SFN_EXT_PART_LEN;
			}
#endif
		}
	}

	// adjust for KANJI, first character 0xE5 should be changed to 0x05
	if (pDE->sName[0] == FAT_DE_FREE)
	{
		pDE->sName[0] = FAT_DE_CHAR_FOR_KANJI;
	}

	return FFAT_OK;
}

/**
 * generate short file name for volume directory entry
 *
 * 입력된 string으로 부터 short file name을 생성하여 directory entry에 저장한다.
 *
 * @param		pVI	: [IN] volume information
 * @param		psName		: [IN] Name string, this string will be modified
 * @param		dwLen		: [IN] character count
 * @param		pDE			: [OUT] directory entry pointer,
 *									생성된 short file name이 저장된다.
 * @return		FFAT_OK			: conversion success
 * @return		FFAT_EINVALID	: invalid name
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_genShortNameForVolumeDe(FatVolInfo* pVI, t_wchar* psName,
							t_int32 dwLen, FatDeSFN* pDE)
{
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pDE);

	// 일단 이름의 모든 character는 대문자로 구성됨을 표시
	pDE->bNTRes = FAT_DE_SFN_ALL_UPPER;

	r = _convertToOemVolumeLabel(pVI, psName, dwLen, pDE->sName,
								(FAT_SFN_NAME_PART_LEN + FAT_SFN_EXT_PART_LEN));
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid name string,  there is no character except '.' or ' ' !!")));
		return r;
	}

	// adjust for KANJI, first character 0xE5 should be changed to 0x05
	if (pDE->sName[0] == FAT_DE_FREE)
	{
		pDE->sName[0] = FAT_DE_CHAR_FOR_KANJI;
	}

	return FFAT_OK;
}

/**
 * Convert Unicode string to Multi-byte string
 * 
 * 입력된 string을 MB로 변환하여 psOutput에 저장한다.
 * short file name entry의 name 부분에 저장하기 위해 사용된다.
 *
 * name과 extension 모두에 사용할 수 있도록 input string의 length와
 * output string의 length 모두를 입력 받는다.
 * 변형이 불가능하거나 0x80 이상인 값이 있을 경우에는 LFN임을 표시한다.
 *
 * 오늘이 2006년의 말복이다.. 빨리 끝내고 한그릇(?)해야겠다... ^^
 *
 * Today is DEC-26th-2007, :)
 *
 * @param		pVI	: [IN] volume information
 * @param		psName		: [IN] Name string
 * @param		dwLen		: [IN] character count
 *								file name 부분을 생성할 경우 
 *								'.'을 제외한 부분까지의 length를 입력한다.
 * @param		psOut		: [OUT] MB로 변환된 OEM name이 저장될 storage
 * @param		dwOutLen	: [IN] output string의 buffer size in byte.
 *								길이는 8 또는 3만 허용된다.
 *								8 : NAME PART
 *								3 : EXT PART
 * @param		pdwConvertSize	: [OUT] byte size of converted string
 * @param		pdwNameType	: [OUT] name type storage
 *								LFN인지 SFN인지 저장하여 return 한다.
 * @return		FFAT_OK			: convert success, used upper & lower character 
 * @return		FFAT_OK1		: all characters are upper character
 * @return		FFAT_OK2		: all characters are lower character
 * @return		FFAT_EINVALID	: there is only '.' and ' '

 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 * @version		APR-16-2007 [DongYoung Seo] Add LFN condition when the name part length is over 8
 * @version		JUN-18-2009 [JeongWoo Park] Add the code to support OS specific character set
 *											- change the dot and space as '_' for SFN.
 */
static FFatErr
_convertToOemName(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen, t_uint8* psOut, 
					t_int32 dwOutLen, t_int32* pdwConvertSize, FatNameType* pdwNameType)
{

	t_int8		pStr[4];		// 2 byte이면 충분하지만 어차피 4byte가 할당되니 4를 할당받는다.
	t_int32		dwCur;			// current multi-byte character length
	t_int32		dwSum;			// total converted byte
	t_wchar		psNewName[FAT_SFN_NAME_PART_LEN];	// new name string pointer
	t_int32		i;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
#ifdef FFAT_VFAT_SUPPORT
	FFAT_ASSERT(dwLen > 0);
#endif
	FFAT_ASSERT(psOut);
	FFAT_ASSERT((dwOutLen == FAT_SFN_NAME_PART_LEN) || (dwOutLen == FAT_SFN_EXT_PART_LEN));
	FFAT_ASSERT(pdwNameType);
	FFAT_ASSERT(dwOutLen > 0);

	dwSum = 0;
	i = 0;

	FFAT_MEMSET(psNewName, 0x00, (FAT_SFN_NAME_PART_LEN * sizeof(t_wchar)));

	dwCur = 0;

#ifdef FFAT_VFAT_SUPPORT
	// lower와 upper 부분을 제거한다.
	*pdwNameType &= ~(FAT_NAME_LOWER | FAT_NAME_UPPER);
#endif

	// 이름의 '.'과 space를 제거한다.
	for (i = 0; i < dwLen; i++)
	{
		if ((psName[i] == '.') || (psName[i] == ' '))
		{
			if ((VI_FLAG(pVI) & VI_FLAG_OS_SPECIFIC_CHAR) == 0)
			{
#ifdef FFAT_VFAT_SUPPORT
				*pdwNameType &= (~FAT_NAME_SFN);
				*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
				continue;
#else
				FFAT_LOG_PRINTF((_T("invalid char is in the name")));
				return FFAT_EINVALID;
#endif
			}
			else
			{
				// if OS specific character set is used, then change the space and dot as '_'
				psNewName[dwCur++] = (t_wchar)'_';
			}
		}
		else
		{
			psNewName[dwCur++] = psName[i];
		}

		if (dwCur >= dwOutLen)
		{
			break;
		}
	}

#ifdef FFAT_VFAT_SUPPORT
	if (dwCur == dwOutLen)
	{
		// 여분의 문자에 대한 체크
		if (dwCur <= dwLen)
		{
			// 이 경우 name part가 8자, ext part가 3자 이상인지 체크해야한다.
			if (dwCur == 8)
			{
				// name part
				if ((psName[8] != 0x0000) && (dwLen > 8))
				{
					if ((psName[8] != '.') || (dwLen > 8))
					{
						// name 의 8자 이후가 .이 아니면 SFN이 아니다.
						*pdwNameType &= (~FAT_NAME_SFN);
						*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
					}
				}
			}
			else
			{
				FFAT_ASSERT(dwCur == 3);
				if (psName[dwCur] != 0x00)
				{
					// ext 의 3자 이후가 NULL이 아니면 SFN이 아니다.
					*pdwNameType &= (~FAT_NAME_SFN);
					*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
				}
			}
		}
		else
		{
			FFAT_ASSERT(0);
		}
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT

	dwLen = dwCur;

	for (i = 0; i < dwLen; i++)
	{
		dwCur = FFAT_WCTOMB((char*)pStr, 4, psNewName[i], VI_DEV(pVI));

#ifdef FFAT_VFAT_SUPPORT
		if (*pdwNameType & FAT_NAME_SFN)		// short file name일 경우에만
		{
			if (dwCur > 1)
			{
				// long file name
				*pdwNameType &= (~FAT_NAME_SFN);
			}

			if (_IS_SPACE(psNewName[i]) == FFAT_TRUE)
			{
				// long file name
				*pdwNameType &= (~FAT_NAME_SFN);
				continue;
			}

			// 0x80 보다 클 경우에는 LFN을 생성 하여야 한다.
			if (psNewName[i] & 0xFF80)
			{
				// long file name
				*pdwNameType &= (~FAT_NAME_SFN);
				*pdwNameType |= FAT_NAME_DBCS;
			}
		}

		if (dwCur <= 0)
		{
			// conversion fail
			dwCur = 1;
			pStr[0] = '_';		// 변환이 실패했을 경우 '_'를 저장한다.
			*pdwNameType |= FAT_NAME_UNKNOWN;
			*pdwNameType &= (~FAT_NAME_SFN);
			*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
		}

		// 이름 부분에 '.'이 있을 경우에는 통과한다. 단 이 경우에도 LFN을 만들어야 한다.
		if (psNewName[i] == '.')
		{
			*pdwNameType &= (~FAT_NAME_SFN);
			continue;
		}
#endif	// end of #ifdef FFAT_VFAT_SUPPORT

		if (dwCur == 1)
		{
#ifdef FFAT_VFAT_SUPPORT
			// LFN에는 사용될 수 있지만 SFN에는 사용될 수 없는 문자 체크
			// '[' ']' ';' ',' '+' '=' 
			if (FFAT_IS_VALID_FOR_SFN(pStr[0]) == FFAT_FALSE)
			{
				pStr[0] = '_';
				*pdwNameType |= FAT_NAME_LFN_CHAR;
				*pdwNameType &= (~FAT_NAME_SFN);
				*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
			}
#endif	// end of #ifdef FFAT_VFAT_SUPPORT

			// ASCII 일 경우 대문자로 변환하여 저장한다.
			psOut[dwSum] = (t_uint8)FFAT_TOUPPER(pStr[0]);

#ifdef FFAT_VFAT_SUPPORT
			// ASCII 일 경우 대문자로 변환하여 저장한다.
			psOut[dwSum] = (t_uint8)FFAT_TOUPPER(pStr[0]);
			if (psOut[dwSum] != pStr[0])
			{
				if (_IS_UPPER_ALPHA(psOut[dwSum]) == FFAT_TRUE)
				{
					// there is lower character
					*pdwNameType |= FAT_NAME_LOWER;
				}
			}
			else
			{
				// ALPHABET 일 경우에만 대문자가 있음을 설정함.
				if (_IS_UPPER_ALPHA(psOut[dwSum]) == FFAT_TRUE)
				{
					*pdwNameType |= FAT_NAME_UPPER;
				}
			}
#endif	// end of #ifdef FFAT_VFAT_SUPPORT
		}
		else
		{
			FFAT_MEMCPY(&psOut[dwSum], pStr, dwCur);
		}

		if ((dwCur + dwSum) > dwOutLen)
		{
#ifdef FFAT_VFAT_SUPPORT
			*pdwNameType |= FAT_NAME_NUMERIC_TAIL;
#endif
			break;
		}

		dwSum += dwCur;
	}

	*pdwConvertSize = dwSum;

#ifdef FFAT_VFAT_SUPPORT
	IF_UK (dwSum == 0)
	{
		// 이름이 '.'으로만 구성되어 있을 경우 invalid name으로 처리한다.
		return FFAT_EINVALID;
	}

	if (*pdwNameType & FAT_NAME_SFN)
	{
		if (i < dwLen)
		{
			// 아직 남은 CHARACTER가 있을 경우 LFN 이다.
			*pdwNameType &= (~FAT_NAME_SFN);
		}
	}
#endif

	// fill rest area to 0x20 (space, blank, ' ')
	if (dwSum != dwOutLen)
	{
		FFAT_MEMSET(&psOut[dwSum], 0x20, dwOutLen - dwSum);
	}

#ifdef FFAT_VFAT_SUPPORT
	// 대 소문자가 섞여 있다... 그럼 SFN이 아니다.
	if ((*pdwNameType & FAT_NAME_LOWER) && (*pdwNameType & FAT_NAME_UPPER))
	{
		*pdwNameType &= (~FAT_NAME_SFN);
	}
#endif

	return FFAT_OK;
}


/**
 * Convert Unicode string to Multi-byte string for volume name
 * 
 * 입력된 string을 MB로 변환하여 psOutput에 저장한다.
 * short file name entry의 name 부분에 저장하기 위해 사용된다.
 *
 * name과 extension 모두에 사용할 수 있도록 input string의 length와
 * output string의 length 모두를 입력 받는다.
 *
 * @param		pVI	: [IN] volume information
 * @param		psName		: [IN] Name string
 * @param		dwLen		: [IN] character count
 *								file name 부분을 생성할 경우 
 *								'.'을 제외한 부분까지의 length를 입력한다.
 * @param		psOut		: [OUT] MB로 변환된 OEM name이 저장될 storage
 * @param		dwOutLen	: [IN] output string의 buffer size in byte.
 *								길이는 8 또는 3만 허용된다.
 *								8 : NAME PART
 *								3 : EXT PART
 * @return		FFAT_OK			: convert success, used upper & lower character 
 * @return		FFAT_OK1		: all characters are upper character
 * @return		FFAT_OK2		: all characters are lower character
 * @return		FFAT_EINVALID	: there is only '.' and ' '

 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 * @version		MAY-18-2009 [JeongWoo Park] fix the bug about the length check.
 */
static FFatErr
_convertToOemVolumeLabel(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen, t_uint8* psOut, t_int32 dwOutLen)
{
	t_int8		pStr[4];		// 2 byte이면 충분하지만 어차피 4byte가 할당되니 4를 할당받는다.
	t_int32		dwCur;			// current multi-byte character length
	t_int32		dwSum;			// total converted byte
	t_int32		i;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(psOut);
	FFAT_ASSERT(dwOutLen == (FAT_SFN_NAME_PART_LEN + FAT_SFN_EXT_PART_LEN));
	
	dwSum = 0;
	i = 0;

	dwCur = 0;

	for (i = 0; i < dwLen; i++)
	{
		// 이름 부분에 '.'이 있을 경우 잘못된 이름임.
		IF_UK (psName[i] == '.')
		{
			// this is an Invalid name
			FFAT_LOG_PRINTF((_T("Invalid character(.) in name string")));
			return FFAT_EINVALID;
		}

		dwCur = FFAT_WCTOMB((char*)pStr, 4, psName[i], VI_DEV(pVI));
		if (dwCur <= 0)
		{
			// conversion fail
			dwCur = 1;
			pStr[0] = '_';		// 변환이 실패했을 경우 '_'를 저장한다.
		}

		if (dwCur == 1)
		{
			// LFN에는 사용될 수 있지만 SFN에는 사용될 수 없는 문자 체크
			// '[' ']' ';' ',' '+' '=' 
			if (FFAT_IS_VALID_FOR_SFN(pStr[0]) == FFAT_FALSE)
			{
				pStr[0] = '_';
			}

			// ASCII 일 경우 대문자로 변환하여 저장한다.
			psOut[dwSum] = (t_uint8)FFAT_TOUPPER(pStr[0]);
		}
		else
		{
			FFAT_MEMCPY(&psOut[dwSum], pStr, dwCur);
		}

		dwSum += dwCur;

		if (dwSum >= dwOutLen)
		{
			break;
		}
	}

	FFAT_ASSERT((dwSum > 0) && (dwSum <= FAT_SFN_NAME_PART_LEN + FAT_SFN_EXT_PART_LEN));

	// fill rest area to 0x20 (space, blank, ' ')
	if (dwSum < dwOutLen)
	{
		FFAT_MEMSET(&psOut[dwSum], 0x20, dwOutLen - dwSum);
	}

	return FFAT_OK;
}

/**
* generate dummy short file name for LFN only environment
*
* Dummy Name은 첫 두바이트 space, NULL이고,
* 확장자의 는 '/', NULL, NULL로 설정한 뒤 나머지는 random
*
* @param		pDE				: [OUT] directory entry pointer,
*									생성된 dummy short file name이 저장된다.
* @param		pdwSfnNameSize	: [OUT] byte size of name part of short filename
* @return		FFAT_OK			: success
* @return		FFAT_EINVALID	: invalid name
* @author		JeongWoo Park
* @version		JUL-22-2009 [JeongWoo Park] First Writing.
*/
static FFatErr
_genDummyShortNameForDe(FatDeSFN* pDE, t_int32* pdwSfnNameSize)
{
	t_uint32 dwRand = (t_uint32)FFAT_RAND();

	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pdwSfnNameSize);

	// 0th Character => space
	pDE->sName[0] = (t_uint8)' ';

	// 1st Character => NULL
	pDE->sName[1] = (t_uint8)0;

	// 2nd Character => Random
	pDE->sName[2] = (t_uint8)((dwRand) & 0x7F);

	// 3rd Character => Random
	pDE->sName[3] = (t_uint8)((dwRand >> 5) & 0x7F);

	// 4th Character => Random
	pDE->sName[4] = (t_uint8)((dwRand >> 10) & 0x7F);

	// 5th Character => Random
	pDE->sName[5] = (t_uint8)((dwRand >> 15) & 0x7F);

	// 6th Character => Random
	pDE->sName[6] = (t_uint8)((dwRand >> 20) & 0x7F);

	// 7th Character => Random
	pDE->sName[7] = (t_uint8)((dwRand >> 24) & 0x7F);


	// 1st Character of EXT => '/'
	pDE->sName[8] = (t_uint8)'/';

	// 2nd Character of EXT => NULL
	pDE->sName[9] = (t_uint8)0;

	// 3nd Character of EXT => NULL
	pDE->sName[10] = (t_uint8)0;

	*pdwSfnNameSize = FAT_SFN_NAME_PART_LEN;

	return FFAT_OK;
}


/**
 * Check the name character is valid by FAT spec
 *
 * @param		psName		: [IN] Name string
 * @param		dwLen		: [IN] character count
 * @return		FFAT_TRUE	: valid name
 * @return		FFAT_FALSE	: invalid name
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 * @version		NOV-20-2007 [GwangOk Go] Support VFAT On/Off
 *										 Change function name from _IsValidLongFileName()
 * @version		JUN-17-2009 [JeongWoo Park] edit the macro call at the VFAT On/Off
 *										By using character validity table, the SFN invalid character set
 *										includes the LFN invalid character set.
 */
static t_boolean
_IsValidCharacter(t_wchar* psName, t_int32 dwLen)
{
	t_int32		i;

	FFAT_ASSERT(psName != NULL);
	FFAT_ASSERT(dwLen > 0);

	for (i = 0; i < dwLen; i++)
	{
#ifdef FFAT_VFAT_SUPPORT
		if (FFAT_IS_VALID_CHAR_FOR_LFN(psName[i]) == FFAT_FALSE)
#else
		if (FFAT_IS_VALID_FOR_SFN(psName[i]) == FFAT_FALSE)
#endif
		{
			return FFAT_FALSE;
		}
	}

	return FFAT_TRUE;
}

/**
* Check the name character is valid by OS Specific
*
* @param		psName		: [IN] Name string
* @param		dwLen		: [IN] character count
* @return		FFAT_TRUE	: valid name
* @return		FFAT_FALSE	: invalid name
* @author		JeongWoo Park
* @version		JUN-17-2009 [JeongWoo Park] First Writing.
*/
static t_boolean
_IsValidCharacterInOS(t_wchar* psName, t_int32 dwLen)
{
	t_int32		i;

	FFAT_ASSERT(psName != NULL);
	FFAT_ASSERT(dwLen > 0);

	for (i = 0; i < dwLen; i++)
	{
#ifdef FFAT_VFAT_SUPPORT
		if (FFAT_IS_VALID_CHAR_FOR_LFN_IN_OS(psName[i]) == FFAT_FALSE)
#else
		if (FFAT_IS_VALID_FOR_SFN_IN_OS(psName[i]) == FFAT_FALSE)
#endif
		{
			return FFAT_FALSE;
		}
	}

	return FFAT_TRUE;
}


#ifndef FFAT_NO_CHECK_RESERVED_NAME
	/**
	* Check the name is a valid name for a node
	*
	* @param		pVI	: [IN] volume information
	* @param		psName		: [IN] Name string
	* @return		FFAT_TRUE	: valid name
	* @return		FFAT_FALSE	: invalid name
	* @author		DongYoung Seo (dy76.seo@samsung.com)
	* @version		AUG-09-2006 [DongYoung Seo] First Writing.
	* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
	*											- Case sensitive
	*/
	static t_boolean
	_IsValidName(FatVolInfo* pVI, t_wchar* psName, t_int32 dwLen)
	{
		t_int32		i;
		t_uint8		pmbName[8];
		t_int32		dwLenMB;
		FFatErr		r;

		FFAT_ASSERT(pVI);
		FFAT_ASSERT(psName != NULL);

		r = FFAT_WCSTOMBS((t_int8*)pmbName, sizeof(pmbName), psName, dwLen, VI_DEV(pVI));
		if (r < 0)
		{
			return FFAT_FALSE;
		}

		dwLenMB = (t_int32)FFAT_STRLEN((char*)pmbName);
		if ((dwLenMB != 3) && (dwLenMB != 4))
		{
			return FFAT_TRUE;
		}

		i = 0;

		for (i = 0; i < _RSVD_NAME_COUNT; i++)
		{
			if (((VI_FLAG(pVI) & VI_FLAG_CASE_SENSITIVE) == 0)
				? (FFAT_STRICMP((const t_int8*)_psReservedNames[i], (const t_int8*)pmbName) == 0)
				: (FFAT_STRCMP((const t_int8*)_psReservedNames[i], (const t_int8*)pmbName) == 0))
			{
				return FFAT_FALSE;
			}
		}

		return FFAT_TRUE;
	}
#endif


#ifdef FFAT_VFAT_SUPPORT
	/**
	 * SFNE의 NTRes 부분에 SFNE의 name type을 설정한다.
	 *
	 * MS에서 spec과 다르게 사용하고 있는 부분이다.(좋지않다.)
	 * 그리고 좀 꼬이게 사용하고 있기도 하다.. (더 좋지 않다..)
	 * 자세히 보면 알 수 있다. 정답은 ? 
	 *  답 : (code review 하시는 분이 update 하세요.)
	 * 
	 * 아래 네개의 값중 하나를 사용해야 한다.
	 * FAT_DE_SFN_NAME_UPPER, FAT_DE_SFN_EXT_UPPER,
	 * FAT_DE_SFN_ALL_UPPER, FAT_DE_SFN_ALL_LOWER
	 *
	 * @param		pDE					: [IN] SFNE
	 * @param		dwNamePartType		: [IN] name part character type
	 * @param		dwExtPartType		: [IN] extension part character type
	 * @return		void
	 * @author		DongYoung Seo (dy76.seo@samsung.com)
	 * @version		AUG-30-2006 [DongYoung Seo] First Writing.
	 */
	static void FFAT_FASTCALL
	_setDeSfnCaseType(FatDeSFN* pDE, FatNameType dwNamePartType, FatNameType dwExtPartType)
	{
		// do masking
		dwNamePartType &= (FAT_NAME_LOWER | FAT_NAME_UPPER);
		dwExtPartType &= (FAT_NAME_LOWER | FAT_NAME_UPPER);

		pDE->bNTRes = 0x00;	// 초기화 시킴.

		switch (dwNamePartType)
		{
			case (FAT_NAME_LOWER | FAT_NAME_UPPER) :
						// 대 소문자가 섞여 있으므로  UPPER 로 설정
			case FAT_NAME_UPPER :
						pDE->bNTRes |= FAT_DE_SFN_NAME_UPPER;
						break;

	//		case FAT_NAME_LOWER :
						// lower는 아무것도 안한다.
			default:
						break;
		}

		switch (dwExtPartType)
		{
			case (FAT_NAME_LOWER | FAT_NAME_UPPER) :
						// 대 소문자가 섞여 있으므로  UPPER 로 설정
			case FAT_NAME_UPPER :
						pDE->bNTRes |= FAT_DE_SFN_EXT_UPPER;
						break;

	//		case FAT_NAME_LOWER :
						// 역시 lower는 아무것도 안한다.
			default:
						break;
		}

		if (pDE->bNTRes == FAT_DE_SFN_ALL_UPPER)
		{
			pDE->bNTRes = FAT_DE_SFN_ALL_LOWER;
		}
		else if (pDE->bNTRes == FAT_DE_SFN_ALL_LOWER)
		{
			pDE->bNTRes = FAT_DE_SFN_ALL_UPPER;
		}

		return;
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT


/**
 * FAT16의 root directory의 DE를 delete한다.
 *
 * 주의 !!!
 * 반드시 FAT16의 root directory에만 사용하여야한다.
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwOffset		: [IN] IO start offset
 * @param		dwCount			: [IN] delete entry count
 * @param		bLookupDelMark	: [IN] flag for looking up deletion mark for efficiency.deletion mark
										FFAT_TRUE : check directory entry to set it 0x00(FAT_DE_END_OF_DIR)
										FFAT_FALS : write 0xE5(FAT_DE_FREE) at the head of entry
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
static FFatErr
_deleteOnFat16Root(FatVolInfo* pVI, t_int32 dwOffset, 
							t_int32 dwCount, t_boolean bLookupDelMark,
							FFatCacheFlag dwFlag, void* pNode)
{

	FFatErr				r;
	t_uint32			dwSector;			// IO sector number
	t_uint32			dwLastSector;		// last IO sector number
	t_int32				dwEntryOffset;			// entry offset
	t_int32				dwLastEntryOffset;			// entry offset
	FFatfsCacheEntry*	pEntry = NULL;
	FatDeSFN*			pDE;
	t_int32				dwEntryPerSector;	// De count per a sector
	t_uint8				bDelMark;
	t_int32				i;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwOffset >= 0);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_META);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_FAT) == 0);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_DE);

	if (dwCount == 0)
	{
		return FFAT_OK;
	}

	// check the last offset is in root directory
	dwSector = VI_FRS(pVI) + (dwOffset >> VI_SSB(pVI));

#ifdef FFAT_STRICT_CHECK
	// last delete sector가 root directory 영역 이내 인지 확인
	IF_UK (((dwOffset >> FAT_DE_SIZE_BITS) + dwCount) > VI_REC(pVI))
	{
		FFAT_LOG_PRINTF((_T("Programming error !!!, delete entry is at over root size !!!")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}
#endif

	dwEntryOffset		= (dwOffset & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS;
	// offset은 sector에 포함될 수 있는 directory entry의 크기보다 클 수는 없다.
	FFAT_ASSERT(dwEntryOffset <= (VI_SS(pVI) >> FAT_DE_SIZE_BITS));

	dwEntryPerSector	= VI_SS(pVI) >> FAT_DE_SIZE_BITS;

	// deletion mark 설정 시작 ==========================
	// Check the las entry + 1 is FAT_DE_END_OF_DIR(0x00)
	// 그렇다면 현재 삭제하는 entry에도 0x00를 설정하는것이 향후 성능에 좋은 영향을 미친다.
	dwLastSector	= dwSector + (((dwEntryOffset + dwCount) << FAT_DE_SIZE_BITS) >> VI_SSB(pVI));
	// last entry offset
	dwLastEntryOffset	= (((dwEntryOffset + dwCount - 1) << FAT_DE_SIZE_BITS) & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS;

	if (bLookupDelMark == FFAT_TRUE)
	{
		bDelMark = _getDeletionMark(pVI, dwLastSector, dwLastEntryOffset, dwEntryPerSector, dwFlag);
	}
	else
	{
		bDelMark = FAT_DE_FREE;
	}
	// deletion mark 끝 ==========================

	do
	{
		r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVI);
		FFAT_ER(r, (_T("fail to get sector from ffatfs caceh")));

		pDE = (FatDeSFN*)pEntry->pBuff;

		for (i = dwEntryOffset; i < dwEntryPerSector; i++)
		{
			pDE[i].sName[0] = bDelMark;
			dwCount--;

			if (0 == dwCount)
			{
				break;
			}
		}

		dwEntryOffset = 0;
		dwSector++;
		r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
		FFAT_ER(r, (_T("fail to release a ffatfs cache entry")));
	} while (dwCount > 0);

	FFAT_ASSERT(dwCount == 0);
	return FFAT_OK;
}

/**
 * delete directory entries in a cluster
 * 한 cluster내에서의 DE를 delete한다
 *
 * 주의 !!!
 * 반드시 한 cluster 내에서만 사용해야 한다.
 * cluster 크기를 넘어서는 경우에는 에러 발생
 *
 * @param		pVI		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwEntryOffset	: [IN] Entry Offset (not byte) in a cluster
 * @param		dwCount			: [IN] delete entry count
 * @param		bLookupDelMark	: [IN] flag for looking up deletion mark for efficiency.deletion mark
										FFAT_TRUE : check directory entry to set it 0x00(FAT_DE_END_OF_DIR)
										FFAT_FALS : write 0xE5(FAT_DE_FREE) at the head of entry
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @return		FFAT_OK			: read/write size in byte
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
static FFatErr
_deleteInCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwEntryOffset,
				t_int32 dwCount, t_boolean bLookupDelMark,
				FFatCacheFlag dwFlag, void* pNode)
{
	FFatErr				r;
	t_uint32			dwSector;			// IO sector number
	t_uint32			dwLastSector;		// last IO Sector number
	FFatfsCacheEntry*	pEntry = NULL;
	FatDeSFN*			pDE;
	t_int32				dwEntryPerSector;	// De count per a sector
	t_uint8				bDelMark;			// mark for DE deletion
	t_int32				dwLastEntryOffset;	// last entry offset
	t_int32				i;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwEntryOffset >= 0);
	FFAT_ASSERT(dwEntryOffset <= (VI_CS(pVI) >> FAT_DE_SIZE_BITS));
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_META);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_FAT) == 0);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_DE);

	if (dwCount == 0)
	{
		return FFAT_OK;
	}

#ifdef FFAT_STRICT_CHECK
	// last delete sector가 root directory 영역 이내 인지 확인
	IF_UK (((dwEntryOffset + dwCount) << FAT_DE_SIZE_BITS) > VI_CS(pVI))
	{
		FFAT_LOG_PRINTF((_T("Programming error !!!, delete entry is at over cluster size !!!")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}
#endif

	// offset은 cluster ector에 포함될 수 있는 directory entry의 크기보다 클 수는 없다.
	FFAT_ASSERT(dwEntryOffset <= (VI_CS(pVI) >> FAT_DE_SIZE_BITS));
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);

	// get IO offset
	dwSector			= FFATFS_GET_FIRST_SECTOR(pVI, dwCluster);
	dwSector			+= ((dwEntryOffset << FAT_DE_SIZE_BITS) >> VI_SSB(pVI));

	dwEntryPerSector	= VI_SS(pVI) >> FAT_DE_SIZE_BITS;

	// deletion mark 설정 시작 ==========================
	// Check the las entry + 1 is FAT_DE_END_OF_DIR(0x00)
	// 그렇다면 현재 삭제하는 entry에도 0x00를 설정하는것이 향후 성능에 좋은 영향을 미친다.
	dwLastSector	= dwSector + (((dwEntryOffset + dwCount - 1) << FAT_DE_SIZE_BITS) >> VI_SSB(pVI));

	// last entry offset
	dwLastEntryOffset	= (((dwEntryOffset + dwCount - 1) << FAT_DE_SIZE_BITS) & VI_CSM(pVI)) >> FAT_DE_SIZE_BITS;
	if (bLookupDelMark == FFAT_TRUE)
	{
		bDelMark = _getDeletionMark(pVI, dwLastSector, dwLastEntryOffset, dwEntryPerSector, dwFlag);
	}
	else
	{
		bDelMark = FAT_DE_FREE;
	}
	// deletion mark 끝 ==========================

	dwLastSector = FFATFS_GET_FIRST_SECTOR(pVI, dwCluster) + VI_SPC(pVI);	// get (last sector + 1)of this cluster
	// et entry offset in a sector 

	dwEntryOffset = dwEntryOffset & (dwEntryPerSector - 1);

	do
	{
		r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVI);
		FFAT_ER(r, (_T("fail to get sector from ffatfs cache")));

		pDE = (FatDeSFN*)pEntry->pBuff;
		
		for (i = dwEntryOffset; i < dwEntryPerSector; i++)
		{
			pDE[i].sName[0] = bDelMark;
			dwCount--;

			if (0 == dwCount)
			{
				break;
			}
		}

		dwEntryOffset = 0;
		dwSector++;
		r = ffat_fs_cache_putSector(pVI, pEntry, (dwFlag | FFAT_CACHE_DIRTY), pNode);
		FFAT_ER(r, (_T("fail to release a ffatfs cache entry")));

	} while ((dwCount > 0) && (dwSector < dwLastSector));

	FFAT_ASSERT(dwCount == 0);
	return FFAT_OK;
}


/**
 * (dwLastEntryOffset + 1) 번째 entry가 dwSector 내에 있을 경우
 * 1st byte를 검사하여 deletion mark를 return한다.
 *
 * directory entry 삭제시 일반적으로는 0xE5를 사용하지만
 * 더이상의 entry가 없을 경우 0x00를 설정하는 것이 더 효율적이다.
 *
 * 이 function의 에러는 모두 무시된다. error 일 경우는 0xE5를 return 한다.
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwSector			: [IN] sector number
 * @param		dwLastEntryOffset	: [IN] last entry offset 
 * @param		dwEntryPerSector	: [IN] DE count per a sector
 * @param		dwFlag				: [IN] cache flag
 * @return		FFAT_OK			: read/write size in byte
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
static t_uint8
_getDeletionMark(FatVolInfo* pVI, t_uint32 dwSector, 
					t_int32 dwLastEntryOffset, 
					t_int32 dwEntryPerSector, FFatCacheFlag dwFlag)
{

	FFatErr				r;
	FFatfsCacheEntry*	pEntry = NULL;
	t_uint8				bDelMark;
	FatDeSFN*			pDE;

	FFAT_ASSERT(pVI);

	// CACHE에 미리 읽어 두는 것이기에 cache의 크기상 성능상의 문제가 없다.
	if ((dwLastEntryOffset + 1) < dwEntryPerSector)	// 다음 sector에 있을 경우는 읽지 않는다.. 
	{											// 이유 ? 사용되지 않기 때문에 + cluster lookup을 해야해서..
		r = ffat_fs_cache_getSector(dwSector, dwFlag, &pEntry, pVI);
		FFAT_EO(r, (_T("fail to get sector from FFATFS caceh")));

		pDE = (FatDeSFN*)pEntry->pBuff;

		if (pDE[(dwLastEntryOffset + 1)].sName[0] == FAT_DE_END_OF_DIR)
		{
			bDelMark = FAT_DE_END_OF_DIR;	// 더이상 entry가 없음으로 표시
		}
		else
		{
			bDelMark = FAT_DE_FREE;			// 그냥 free entry로 표시.
		}

		r = ffat_fs_cache_putSector(pVI, pEntry, dwFlag, NULL);
		if (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to release a FFATFS cache entry")));
		}
	}
	else
	{
		return FAT_DE_FREE;
	}
	// deletion mark 끝 ==========================

	return bDelMark;

out:
	return FAT_DE_FREE;
}


#ifdef FFAT_VFAT_SUPPORT
	/**
	 * SFN 의 이름 부분의 대/소문자를 설정한다.
	 * 
	 *
	 * @param		psSrc			: [IN] source name string pointer
	 * @param		psDes			: [IN/OUT] destination name string pointer
	 * @param		dwLen			: [IN] length of source
	 * @param		bNTRes			: [IN] NTRes entry at SFNE
	 * @return		void
	 * @author		DongYoung Seo 
	 * @version		FEB-26-2007 [DongYoung Seo] First Writing.
	 */
	static void
	_adjustSFNCase(t_wchar* psSrc, t_wchar* psDes, t_int32 dwLen, t_uint8 bNTRes)
	{
		t_int32		i;
		FFAT_ASSERT(psSrc);
		FFAT_ASSERT(psDes);
		FFAT_ASSERT(dwLen <= FAT_DE_SFN_MAX_LENGTH);

		switch (bNTRes)
		{
			case FAT_DE_SFN_ALL_UPPER :
				// nothing to do
				break;

			case FAT_DE_SFN_ALL_LOWER :
				// lower characters
				for (i = 0; i < dwLen; i++)
				{
					psDes[i] = FFAT_TOWLOWER(psSrc[i]);
				}
				break;

			case FAT_DE_SFN_NAME_UPPER :
				// left => upper
				for (i = 0; i < dwLen; i++)
				{
					if (psSrc[i] == (t_wchar)'.')
					{
						break;
					}
				}

				for (/*NONE*/; i < dwLen; i++)
				{
					psDes[i] = FFAT_TOWLOWER(psSrc[i]);
				}

				break;

			case FAT_DE_SFN_EXT_UPPER :
				// left => lower
				for (i = 0; i < dwLen; i++)
				{
					if (psSrc[i] == (t_wchar)'.')
					{
						break;
					}
					psDes[i] = (t_uint8)FFAT_TOLOWER(psSrc[i]);
				}
				break;

			default:
				break;
		}

		return;
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT


/**
 * get entry for volume name on root directory
 * 
 * @param		pVI	: [IN] Volume Info pointer
 * @param		pDeVolLabel	: [OUT] storage for volume name entry
 * @param		pdwSector	: [OUT] sector number storage for volume name entry
 * @param		pdwOffset	: [OUT] offset(in a sector) storage for volume name entry
 * @return		FFAT_OK		: lookup success
 * @return		FFAT_EIO	: I/O error
 * @return		FFAT_NOENT	: there is no entry for volume name
 * @author		DongYoung Seo 
 * @version		SEP-05-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_getVolumeLabelEntry(FatVolInfo* pVI, FatDeSFN* pDeVolLabel, t_uint32* pdwSector, t_uint32* pdwOffset)
{
	t_uint32			dwCurCluster;		// lookup cluster, both start and current
	t_uint32			dwCurOffset;		// lookup offset, both start and current,
	FFatfsCacheEntry*	pEntry = NULL;		// buffer cache entry
	FatDeSFN*			pDe;

	t_uint32			dwSector;			// read sector number
	t_uint32			dwLastSector;		// last sector number
	t_int32				dwEntryPerSector;	// entry count per a sector
	t_int32				i;
	FFatErr				r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pDeVolLabel);
	FFAT_ASSERT(pdwSector);
	FFAT_ASSERT(pdwOffset);

	if (FFATFS_IS_FAT16(pVI) == FFAT_TRUE)
	{
		dwCurCluster = FFATFS_FAT16_ROOT_CLUSTER;
	}
	else
	{
		dwCurCluster = pVI->dwRootCluster;
	}

	// get entry count per a sector.
	dwEntryPerSector = VI_SS(pVI) >> FAT_DE_SIZE_BITS;

	dwCurOffset = 0;	// set current offset to 0

	do
	{
		if (FFATFS_IS_FAT16(pVI) == FFAT_TRUE)
		{
			// FAT12/16 root directory
			dwSector		= VI_FRS(pVI);
			dwLastSector	= VI_FRS(pVI) + VI_RSC(pVI);	// end of root sector + 1

			FFAT_ASSERT(dwSector <= dwLastSector);
		}
		else
		{
			// normal directory
			dwSector		= FFATFS_GET_SECTOR_OF_CLUSTER(pVI, dwCurCluster, 0);
			dwLastSector	= dwSector + VI_SPC(pVI);	// last sector + 1
		}

		for (/* Nothing */ ; dwSector < dwLastSector; dwSector++)
		{
			FFAT_ASSERT(dwSector <= VI_SC(pVI));

			r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
			FFAT_EO(r, (_T("fail to get sector")));

			FFAT_ASSERT(pEntry);

			// check directory entry validity
			pDe = (FatDeSFN*)pEntry->pBuff;

			for (i = ((dwCurOffset & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS);
					i < dwEntryPerSector; 
					i++, dwCurOffset += FAT_DE_SIZE)
			{
				if (pDe[i].sName[0] == FAT_DE_END_OF_DIR)
				{
					r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_DATA_DE, NULL);
					FFAT_ER(r, (_T("fail to put sector")));
					return FFAT_ENOENT;
				}

				if (pDe[i].sName[0] == FAT_DE_FREE)
				{
					// this is an free entry
					continue;
				}

				if (pDe[i].bAttr != FFAT_ATTR_VOLUME)
				{
					continue;
				}

				// we get volume name entry
				FFAT_MEMCPY(pDeVolLabel, &pDe[i], FAT_DE_SIZE);

				*pdwSector	= dwSector;					// set sector number for volume name entry
				*pdwOffset	= i << FAT_DE_SIZE_BITS;	// set byte offset for volume name entry

				// release a sector
				r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
				FFAT_EO(r, (_T("fail to put a cache entry")));
				
				r = FFAT_OK;
				goto out;
			}

			// release a sector
			r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
			FFAT_EO(r, (_T("fail to put a cache entry")));
		}

		if (FFATFS_IS_FAT16(pVI) == FFAT_TRUE)
		{
			// this is FAT16 end of directory
			r = FFAT_ENOENT;
			break;
		}

		// get next cluster and update offset
		// if there is no other cluster return FFAT_ENOENT
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCurCluster, &dwCurCluster);
		if (r < 0)
		{
			if (r != FFAT_EEOF)
			{
				FFAT_LOG_PRINTF((_T("Fail to get next cluster ")));
				r = FFAT_EFAT;
			}
			else
			{
				r = FFAT_ENOENT;
			}

			goto out;
		}

		if (pVI->pVolOp->pfIsEOF(dwCurCluster) == FFAT_TRUE)
		{
			break;
		}

	} while (1);

	r = FFAT_ENOENT;
out:
	return r;
}


/**
* get a free entry from dwCluster
* 
* @param		pVI			: [IN] Volume Info
* @param		dwCluster	: [IN] start cluster number
* @param		pdwSector	: [OUT] storage for sector number
* @param		pdwOffset	: [OUT] storage for free directory offset in the sector
* @return		FFAT_OK		: lookup success
* @return		FFAT_EIO	: I/O error
* @return		FFAT_NOENT	: there is no free entry
* @author		DongYoung Seo 
* @version		NOV-19-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_getFreeDE(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32* pdwSector, t_uint32* pdwOffset)
{
	t_uint32				dwCurCluster;		// current cluster number
	t_uint32				dwCurOffset;		// current offset
	FFatfsCacheEntry*		pEntry = NULL;		// cache entry pointer
	t_uint32				dwSector;			// current sector
	t_uint32				dwLastSector;		// last sector number for lookup
	t_int32					dwEntryPerSector;	// entries per a sector
	FatDeSFN*				pDE;				// a directory entry pointer
	t_int32					i;
	FFatErr					r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwSector);
	FFAT_ASSERT(pdwOffset);

	dwCurCluster			= dwCluster;
	dwEntryPerSector		= VI_SS(pVI) >> FAT_DE_SIZE_BITS;
	dwCurOffset				= 0;

	FFAT_ASSERT(dwEntryPerSector >= 16);		// minimum sector size 512

	do
	{
		if (dwCurCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);

			// FAT12/16 root directory
			dwSector		= VI_FRS(pVI);
			dwLastSector	= VI_FRS(pVI) + VI_RSC(pVI);	// end of root sector + 1

			FFAT_ASSERT(dwSector <= dwLastSector);
		}
		else
		{
			// normal directory
			dwSector		= FFATFS_GET_SECTOR_OF_CLUSTER(pVI, dwCurCluster, 0);
			dwLastSector	= dwSector + VI_SPC(pVI);	// last sector + 1
		}

		for (/* Nothing */ ; dwSector < dwLastSector; dwSector++)
		{
			FFAT_ASSERT(dwSector <= VI_SC(pVI));

			r = ffat_fs_cache_getSector(dwSector, (FFAT_CACHE_DATA_DE | FFAT_CACHE_LOCK), &pEntry, pVI);
			FFAT_EO(r, (_T("fail to get sector")));

			FFAT_ASSERT(pEntry);

			// check directory entry validity
			pDE = (FatDeSFN*)pEntry->pBuff;

			for (i = ((dwCurOffset & VI_SSM(pVI)) >> FAT_DE_SIZE_BITS);
					i < dwEntryPerSector; i++, dwCurOffset += FAT_DE_SIZE)
			{
				if ((pDE[i].sName[0] != FAT_DE_FREE) && (pDE[i].sName[0] != FAT_DE_END_OF_DIR))
				{
					continue;
				}

				// this is an free entry
				*pdwSector	= dwSector;
				*pdwOffset	= i << FAT_DE_SIZE_BITS;

				// release a sector
				r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
				FFAT_EO(r, (_T("fail to put a cache entry")));

				r = FFAT_OK;
				goto out;
			}

			// release a sector
			r = ffat_fs_cache_putSector(pVI, pEntry, FFAT_CACHE_UNLOCK, NULL);
			FFAT_EO(r, (_T("fail to put a cache entry")));
		}

		if (dwCurCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
			// this is FAT16 end of directory
			r = FFAT_ENOENT;
			break;
		}

		// get next cluster and update offset
		// if there is no other cluster return FFAT_ENOENT
		r = pVI->pVolOp->pfGetNextCluster(pVI, dwCurCluster, &dwCurCluster);
		if (r < 0)
		{
			if (r != FFAT_EEOF)
			{
				FFAT_LOG_PRINTF((_T("Fail to get next cluster ")));
				r = FFAT_EFAT;
			}
			else
			{
				r = FFAT_ENOENT;
			}

			goto out;
		}

		if (pVI->pVolOp->pfIsEOF(dwCurCluster) == FFAT_TRUE)
		{
			break;
		}

	} while (1);

	r = FFAT_ENOENT;
out:
	return r;
}

