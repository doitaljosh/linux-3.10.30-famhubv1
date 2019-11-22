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
 * @file		ffat_addon_misc.h
 * @brief		The global configuration for FFAT
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ffat_addon_misc.h"

#include "ffat_common.h"

#include "ffat_dir.h"
#include "ffat_vol.h"
#include "ffat_main.h"
#include "ffat_file.h"
#include "ffat_share.h"

#include "ffat_addon_log.h"
#include "ffat_addon_fcc.h"
#include "ffat_addon_debug.h"

#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_MISC)

static t_boolean	_matchesWildcardMask(Vol* pVol, t_int32 dwLenWildcardMask, const t_wchar* psWildcardMask,
								t_int32 dwLenTarget, t_wchar* psTarget);

/**
 * get node name in a directory with status
 *
 * @param		pNode		: [IN] node pointer
 * @param		pRSI		: [IN/OUT] readdir result storage
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing.
 * @version		JUN-17-2009 [JeongWoo Park] Add the code to support OS specific character set
 *											If use OS specific character set, skip the removing tail dot and space
 */
FFatErr
ffat_addon_misc_readdirStat(Node* pNode, FFatReaddirStatInfo* pRSI, ComCxt* pCxt)
{
	ReaddirInfo			stRI;
	FFatErr				r;
	ReaddirFlag			dwFlag;
	t_boolean			bWC;			// check wild card
	t_int32				dwNameLen = 0;	// length of wild card
	t_wchar*			psName = NULL;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pRSI);

	dwFlag = READDIR_STAT;

#ifdef FFAT_GET_REAL_SIZE_OF_DIR
	dwFlag |= READDIR_GET_SIZE_OF_DIR;
#endif

#ifdef FFAT_VFAT_SUPPORT
	if (pRSI->psName != NULL)
	{
		dwFlag |= READDIR_LFN;
	}
#endif

	if (pRSI->psShortFileName != NULL)
	{
		dwFlag |= READDIR_SFN;
	}

	if (pRSI->psNameToSearch)
	{
		dwNameLen = FFAT_WCSLEN(pRSI->psNameToSearch);
		if ((dwNameLen <= 0) || (dwNameLen > FFAT_FILE_NAME_MAX_LENGTH ))
		{
			FFAT_LOG_PRINTF((_T("Invalid name string for search - no mame or too long\n")));
			return FFAT_EINVALID;
		}

		psName = (t_wchar*)FFAT_LOCAL_ALLOC(((FFAT_FILE_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);
		FFAT_ASSERT(psName);

		FFAT_WCSCPY(psName, pRSI->psNameToSearch);

		if ((VOL_FLAG(NODE_VOL(pNode)) & VOL_OS_SPECIFIC_CHAR) == 0)
		{
			FFATFS_RemoveTrailingDotAndBlank(psName, &dwNameLen);
		}

		// '*', "*.*" is no wild character
		if (((dwNameLen == 1) && (pRSI->psNameToSearch[0] == '*'))
			||
			((dwNameLen == 3) && (pRSI->psNameToSearch[0] == '*') &&
			(pRSI->psNameToSearch[1] == '.') && (pRSI->psNameToSearch[2] == '*')))
		{
			bWC = FFAT_FALSE;
		}
		else
		{
			bWC = FFAT_TRUE;
		}
	}
	else
	{
		bWC = FFAT_FALSE;
	}

	FFAT_INIT_RI(&stRI, pRSI->psName, pRSI->psShortFileName, 
					pRSI->dwNameLen, pRSI->dwShortFileNameLen, &pRSI->stStat, NULL);

	do
	{
		r = ffat_dir_readdir((Node*)pNode, pRSI->dwOffset, &stRI, dwFlag, pCxt);
		if (r == FFAT_OK)
		{
			// check wild card
			if (bWC == FFAT_TRUE)
			{
				FFAT_ASSERT(dwNameLen > 0);
				FFAT_ASSERT(pRSI->psNameToSearch);

				if (_matchesWildcardMask(NODE_VOL(pNode), dwNameLen, pRSI->psNameToSearch,
									stRI.dwLenLFN, stRI.psLFN) == FFAT_TRUE)
				{
					r = FFAT_OK;
					goto out;
				}

				// set to next offset
				pRSI->dwOffset = stRI.dwOffsetNext;
				stRI.dwLenLFN	= pRSI->dwNameLen;
				stRI.dwLenSFN	= pRSI->dwShortFileNameLen;
			}
			else
			{
				break;
			}
		}
		else
		{
			break;
		}
	} while(1);

out:
	if (r == FFAT_OK)
	{
		pRSI->dwOffsetNext			= stRI.dwOffsetNext;
		pRSI->dwNameLen				= stRI.dwLenLFN;
		pRSI->dwShortFileNameLen	= stRI.dwLenSFN;
	}

	FFAT_LOCAL_FREE(psName, ((FFAT_FILE_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);

	return r;
}


/**
 * unlink a node with name retrieval
 *
 * @param		pNode		: [IN] node pointer
 * @param		pRGNI		: [IN/OUT] readdir get node result storage
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-13-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_misc_readdirGetNode(Node* pNode, FFatReaddirGetNodeInfo* pRGNI, ComCxt* pCxt)
{
	ReaddirInfo			stRI;
	FFatErr				r;
	ReaddirFlag			dwFlag = READDIR_GET_NODE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pRGNI);

#ifdef FFAT_VFAT_SUPPORT
	if (pRGNI->psName)
	{
		dwFlag |= READDIR_LFN;
	}

	FFAT_INIT_RI(&stRI, pRGNI->psName, NULL , pRGNI->dwNameLen, 0, NULL, (Node*)pRGNI->pNode);
#else
	if (pRGNI->psName)
	{
		dwFlag |= READDIR_SFN;
	}

	FFAT_INIT_RI(&stRI, NULL, pRGNI->psName, 0, pRGNI->dwNameLen, NULL, (Node*)pRGNI->pNode);
#endif

	r = ffat_dir_readdir((Node*)pNode, pRGNI->dwOffset, &stRI, dwFlag, pCxt);
	if (r == FFAT_OK)
	{
		pRGNI->dwOffsetNext = stRI.dwOffsetNext;
	}

	return r;
}



/**
 * fsctl helper function for clean DIR
 *
 * This function assumes there is no opened file in the directory
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		DongYoung Seo
 * @version		SEP-13-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_misc_cleanDir(Node* pNode, ComCxt* pCxt)
{
	ReaddirInfo			stRI;
	FFatErr				r;
	ReaddirFlag			dwFlag;
	Node*				pNodeChild = NULL;
	t_int32				dwOffset = 0;
	Vol*				pVol;

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);

	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not a same one")));
		return FFAT_EXDEV;
	}

	// allocate memory for node
	pNodeChild = (Node*) FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(NULL != pNodeChild);

	ffat_node_resetNodeStruct(pNodeChild);

	dwFlag = READDIR_GET_NODE | READDIR_NO_LOCK;

	FFAT_INIT_RI(&stRI, NULL, NULL , 0, 0, NULL, pNodeChild);

	do
	{
		r = ffat_dir_readdir((Node*)pNode, dwOffset, &stRI, dwFlag, pCxt);
		if (r < 0)
		{
			break;
		}

		dwOffset = stRI.dwOffsetNext;

		if (NODE_IS_DIR(pNodeChild) == FFAT_TRUE)
		{
			// do not unlink directory, i can't !!
			continue;
		}

		if (ffat_log_isLogNode(pNodeChild) == FFAT_TRUE)
		{
			continue;
		}

		if (ffat_debug_isDubugFile(pNodeChild) == FFAT_TRUE)
		{
			continue;
		}

		r = ffat_file_unlink(pNode, pNodeChild, NODE_UNLINK_NO_LOCK, pCxt);
		FFAT_EO(r, (_T("fail to unlink a node")));

		r = ffat_node_terminateNode(pNodeChild, pCxt);
		FFAT_EO(r, (_T("fail to unlink a node")));
	} while (1);

	if (r == FFAT_ENOMOREENT)
	{
		r = FFAT_OK;
	}

out:
	FFAT_LOCAL_FREE(pNodeChild, sizeof(Node), pCxt);

	return r;
}


/**
 * unlink a node with name retrieval
 *
 * @param		pNode		: [IN] node pointer
 * @param		pRUI		: [IN/OUT] readdir unlink information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_misc_readdirUnlink(Node* pNode, FFatReaddirUnlinkInfo* pRUI, ComCxt* pCxt)
{
	ReaddirInfo			stRI;
	FFatErr				r;
	ReaddirFlag			dwFlag;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pRUI);
	FFAT_ASSERT(pRUI->pNode);

	dwFlag = READDIR_GET_NODE | READDIR_NO_LOCK;		// already got write lock

	FFAT_INIT_RI(&stRI, NULL, NULL , 0, 0, NULL, (Node*)pRUI->pNode);

	// INITIALIZE A NODE
	r = ffat_node_initNode(NODE_VOL(pNode), pNode, NODE_C(pNode), (Node*)pRUI->pNode, FFAT_FALSE, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to init node")));
		goto out;
	}

	pRUI->dwOffsetNext = pRUI->dwOffset;

re:
	r = ffat_node_terminateNode((Node*)pRUI->pNode, pCxt);
	FFAT_ER(r, (_T("Fail to termiante a node")));

	r = ffat_dir_readdir((Node*)pNode, pRUI->dwOffsetNext, &stRI, dwFlag, pCxt);
	if (r == FFAT_OK)
	{
		pRUI->dwOffsetNext = stRI.dwOffsetNext;
	}
	else
	{
		goto out;
	}

	// check mode
	if ((pRUI->dwMode & pNode->stDE.bAttr) && (NODE_IS_FILE((Node*)pRUI->pNode) == FFAT_TRUE))
	{
		r = ffat_file_unlink(pNode, (Node*)pRUI->pNode, NODE_UNLINK_NONE, pCxt);
		FFAT_EO(r, (_T("Fail To unlink a file")));
	}
	else
	{
		goto re;
	}

out:
	r = ffat_node_terminateNode((Node*)pRUI->pNode, pCxt);
	FFAT_ER(r, (_T("Fail to termiante a node")));

	return r;
}


/**
 * secure deallocate
 * Call cluster init function for not used sector 
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwCluster	: [IN] start to delete
 * @param		dwCount		: [IN] cluster count to delete
 * @param		pCxt		: [IN] context of current operation
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to cached I/O
 */
FFatErr
ffat_addon_misc_secureDeallocate(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	t_uint32		dwStartCluster;	// start cluster
	t_uint32		dwPrevCluster;	// previous cluster
	t_uint32		dwNextCluster;	// next cluster
	t_int32			dwContCount;	// contiguous count
	FFatErr			r;

	if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	dwNextCluster = dwCluster;

	if (dwCount == 0)
	{
		dwCount = VI_CC(VOL_VI(pVol));
	}

	do
	{
		dwContCount = 1;
		dwPrevCluster = dwNextCluster;

		if (FFATFS_IS_EOF(VOL_VI(pVol), dwPrevCluster) == FFAT_TRUE)
		{
			break;
		}

		do 
		{
			r = FFATFS_GetNextCluster(VOL_VI(pVol), dwPrevCluster, &dwNextCluster, pCxt);
			FFAT_ER(r, (_T("fail to get next cluster")));

			if (FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) == FFAT_TRUE)
			{
				break;
			}

			if ((dwPrevCluster + 1) == dwNextCluster)
			{
				dwContCount++;
			}
			else
			{
				break;
			}

			dwPrevCluster = dwNextCluster;

		} while (--dwCount > 0);

		dwStartCluster = dwPrevCluster - dwContCount + 1;

		r = ffat_initCluster(pVol, NULL, dwStartCluster, dwContCount,
						FFAT_CACHE_SYNC, pCxt);
		FFAT_ER(r, (_T("fail to init cluster")));
	} while (dwCount > 0);

	return FFAT_OK;
}


#ifdef FFAT_VFAT_SUPPORT
	/**
	* get short file name from long file name or vice versa
	*
	* @param		pGSLN			: [IN/OUT] FFatGetShortLongName structure pointer
	* @param		dwCmd			: [IN] flag for get short name or get long name
	 * @param		pCxt			: [IN] context of current operation
	* @return		FFAT_OK			: success 
	* @return		Negative		: error
	* @author		GwangOk Go
	* @version		NOV-28-2007 [GwangOk Go] First Writing.
	* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
	*											- Case sensitive
	*/
	FFatErr
	ffat_addon_misc_getShortLongName(FFatGetShortLongName* pGSLN,
										FFatFSCtlCmd dwCmd, ComCxt* pCxt)
	{
		FFatErr				r;
		ReaddirInfo			stRI;
		FFatNodeStatus		stStat;

		Node*				pParentNode;
		t_wchar*			pTempName;
		t_uint32			dwOffset = 0;

		FFAT_ASSERT(pGSLN);
		FFAT_ASSERT(pGSLN->pParent);
		FFAT_ASSERT(pGSLN->psShortName);
		FFAT_ASSERT(pGSLN->psLongName);
		FFAT_ASSERT(dwCmd == FFAT_FSCTL_GET_SHORT_NAME || dwCmd == FFAT_FSCTL_GET_LONG_NAME);

		pParentNode = (Node*)pGSLN->pParent;

		if (NODE_IS_ROOT(pParentNode) == FFAT_FALSE)
		{
			dwOffset = FAT_DE_SIZE << 1;	// skip "." & ".."
		}

		stRI.pNodeStatus	= &stStat;
		stRI.pNode			= NULL;

		if (dwCmd == FFAT_FSCTL_GET_SHORT_NAME)
		{
			FFAT_ASSERT(pGSLN->dwShortNameLen >= (FAT_DE_SFN_MAX_LENGTH + 1));

			pTempName = (t_wchar*) FFAT_LOCAL_ALLOC((FFAT_FILE_NAME_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
			FFAT_ASSERT(pTempName != NULL);

			stRI.psLFN			= pTempName;
			stRI.psSFN			= pGSLN->psShortName;
		}
		else
		{
			FFAT_ASSERT(pGSLN->dwLongNameLen >= FFAT_FILE_NAME_MAX_LENGTH + 1);

			pTempName = (t_wchar*) FFAT_LOCAL_ALLOC((FAT_DE_SFN_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
			FFAT_ASSERT(pTempName != NULL);

			stRI.psLFN			= pGSLN->psLongName;
			stRI.psSFN			= pTempName; 
		}

		while (1)
		{
			stRI.dwLenLFN		= pGSLN->dwLongNameLen;
			stRI.dwLenSFN		= pGSLN->dwShortNameLen;

			r = ffat_dir_readdir(pParentNode, dwOffset, &stRI, (READDIR_STAT | READDIR_LFN | READDIR_SFN), pCxt);
			if (r < 0)
			{
				break;
			}

			if ((dwCmd == FFAT_FSCTL_GET_SHORT_NAME) &&
				(((VOL_FLAG(NODE_VOL(pParentNode)) & VOL_CASE_SENSITIVE) == 0)
				? (FFAT_WCSICMP(pGSLN->psLongName, pTempName) == 0)
				: (FFAT_WCSCMP(pGSLN->psLongName, pTempName) == 0)))
			{
				pGSLN->dwLongNameLen = stRI.dwLenLFN;
				pGSLN->dwShortNameLen = stRI.dwLenSFN;
				break;
			}
			else if ((dwCmd == FFAT_FSCTL_GET_LONG_NAME) &&
				(((VOL_FLAG(NODE_VOL(pParentNode)) & VOL_CASE_SENSITIVE) == 0)
				? (FFAT_WCSICMP(pGSLN->psShortName, pTempName) == 0)
				: (FFAT_WCSCMP(pGSLN->psShortName, pTempName) == 0)))
			{
				pGSLN->dwLongNameLen = stRI.dwLenLFN;
				pGSLN->dwShortNameLen = stRI.dwLenSFN;
				break; 
			}

			dwOffset = stRI.dwOffsetNext;
		}

		if (dwCmd == FFAT_FSCTL_GET_SHORT_NAME)
		{
			FFAT_LOCAL_FREE(pTempName, (FFAT_FILE_NAME_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
		}
		else
		{
			FFAT_LOCAL_FREE(pTempName, (FAT_DE_SFN_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
		}

		return r;
	}
#endif	// #ifdef FFAT_VFAT_SUPPORT


/**
 * check the FAT sector is wholly free or not
 *
 * @param		pVol			: [IN] Volume
 * @param		dwSectorNo		: [IN] FAT Sector Number, 1st FAT only
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK, FFAT_FALSE		: not free
 * @return		FFAT_OK1, FFAT_TRUE		: free
 * @return		Negative		: error
 * @author		DongYoung Seo
 * @version		JUN-16-2008 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_misc_isFreeFATSector(Vol* pVol, t_uint32 dwSectorNo, ComCxt* pCxt)
{
	t_uint32		dwFCC;
	FFatErr			r;

	if (dwSectorNo == VOL_FFS(pVol))
	{
		// NEVER BE FREE
		return FFAT_FALSE;
	}

	r = ffat_fcc_getFCCOfSector(pVol, dwSectorNo, &dwFCC, pCxt);
	FFAT_ER(r, (_T("fail to get Free Cluster Count")));

	if (r == FFAT_DONE)
	{
		if (dwFCC == (t_uint32)VOL_CCPFS(pVol))
		{
			return FFAT_TRUE;
		}

		return FFAT_FALSE;
	}

	// FCC is not activated
	r = FFATFS_GetFCCOfSector(VOL_VI(pVol), dwSectorNo, &dwFCC, pCxt);
	FFAT_ER(r, (_T("fail to get Free Cluster Count")));

	if (dwFCC == (t_uint32)VOL_CCPFS(pVol))
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


//=============================================================================
//
//	static function
//


// Determine whether <psTarget> matches <psWildcardMask>; '?' and '*' are
// valid wildcards.
// The '*' quantifier is a 0 to <infinity> character wildcard.
// The '?' quantifier is a 0 or 1 character wildcard.
static t_boolean
_matchesWildcardMask(Vol* pVol, t_int32 dwLenWildcardMask, const t_wchar* psWildcardMask,
						t_int32 dwLenTarget, t_wchar* psTarget)
{
	while (dwLenWildcardMask && dwLenTarget)
	{
		if (*psWildcardMask == L'?')
		{
			// skip current target character
			dwLenTarget--;
			psTarget++;
			dwLenWildcardMask--;
			psWildcardMask++;
			continue;
		}

		if (*psWildcardMask == L'*')
		{
			psWildcardMask++;
			if (--dwLenWildcardMask)
			{
				while (dwLenTarget)
				{
					if (_matchesWildcardMask(pVol, dwLenWildcardMask, psWildcardMask,
									dwLenTarget--, psTarget++) == FFAT_TRUE)
					{
						return FFAT_TRUE;
					}
				}

				return FFAT_FALSE;
			}

			return FFAT_TRUE;
		}
		// test for equality
		else if ((*psWildcardMask) != (*psTarget))
		{
			if ((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) ||
				(FFAT_TOWLOWER(*psWildcardMask) != FFAT_TOWLOWER(*psTarget)))
			{
				return FFAT_FALSE;
			}
		}

		dwLenWildcardMask--;
		psWildcardMask++;
		dwLenTarget--;
		psTarget++;
	}

	// target matches wild card mask, succeed
	if (!dwLenWildcardMask && !dwLenTarget)
	{
		return FFAT_TRUE;
	}

	// wild card mask has been spent and target has characters remaining, fail
	if (!dwLenWildcardMask)
	{
		return FFAT_FALSE;
	}

	// target has been spent; succeed only if wild card characters remain
	while (dwLenWildcardMask--)
	{
		if (*psWildcardMask != L'*' && *psWildcardMask != L'?')
		{
			return FFAT_FALSE;
		}

		psWildcardMask++;
	}

	return FFAT_TRUE;
}

