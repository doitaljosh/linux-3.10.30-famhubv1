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
 * @file		ffat_node.c
 * @brief		The file implements node module3y
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

// includes

#include "ess_types.h"
#include "ess_math.h"
#include "ess_bitmap.h"

#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_misc.h"

#include "ffat_common.h"

#include "ffat_vol.h"
#include "ffat_node.h"
#include "ffat_main.h"
#include "ffat_misc.h"
#include "ffat_file.h"
#include "ffat_dir.h"
#include "ffat_share.h"

#include "ffatfs_api.h"

#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_NODE)

//#define _DEBUG_NODE

// static functions
static FFatErr	_lookupHelperForGetNodeDe(Node* pNodeParent, t_int32 dwOffset,
							FatNameType dwNameType, t_int32 dwNameLen, FatGetNodeDe* pGetNodeDe);
static FFatErr	_lookupForCreate(Node* pNodeParent, t_wchar* psUpperName, t_int32 dwNameLen,
							Node* pNode, FatGetNodeDe* pNodeDE, FFatLookupFlag dwLookupFlag,
							NodeNumericTail* pNumericTail, ComCxt* pCxt);
static FFatErr	_updateNumericTail(FatGetNodeDe* pNodeDE, Node* pNode,
							NodeNumericTail* pNumericTail);
static FFatErr	_updateFreeDe(FatGetNodeDe* pNodeDE, t_int32* pdwPrevDeCluster,
							t_int32* pdwPrevDeOffset, t_int32 dwEntryCount,
							Node* pNode, ComCxt* pCxt);
static FFatErr	_updateFreeDeLast(FatGetNodeDe* pNodeDE, 
							t_int32 dwPrevDeCluster, t_int32 dwPrevDeOffset, 
							t_int32 dwEntryCount, Node* pNode, ComCxt* pCxt);
#if 0	// not used (when creating exist node, nestle use truncate)
static FFatErr	_createExistNode(Node* pNode, FFatCreateFlag dwFlag, ComCxt* pCxt);
#endif

static FFatErr	_renameCheckParameter(Node* pNodeSrcParent, Node* pNodeSrc,
							Node* pNodeDesParent, Node* pNodeDes, t_wchar* psName);
static FFatErr	_renameUpdateDEs(Node* pNodeSrcParent, Node* pNodeSrc, Node* pNodeDesParent,
							Node* pNodeNew, t_wchar* psName,
							t_uint32* pdwClustersDE, t_int32 dwClusterCountDE,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt);

static FFatErr	_getNodeName(Node* pNode, t_wchar* psName, t_int32* pdwLen, ComCxt* pCxt);

// read write data
static FFatErr	_readWriteHeadTail(Node* pNode, t_uint32 dwOffset, t_int8* pBuff,
								t_uint32 dwSize, FFatVC* pVC, t_uint32* pdwSizeDone,
								FFatRWFlag bRWFalg, FFatCacheFlag dwFlag, ComCxt* pCxt);
static FFatErr	_readWriteBody(Node* pNode, t_uint32 dwOffset, t_int8* pBuff,
								t_uint32 dwSize, FFatVC* pVC, t_uint32* pdwSizeDone,
								FFatRWFlag bRWFalg, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
static FFatErr _readWriteWhole(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
								FFatVC* pVC, t_uint32* pdwSizeDone, t_boolean bRead, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
static t_int32 _readWriteSectors(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
			t_uint32 dwCluster, t_boolean bRead, FFatCacheFlag dwFlag, ComCxt* pCxt);

static FFatErr	_deleteDE(Node* pNode, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static void		__Itoa(t_int32 dwValue, t_int8* pStr);

#ifdef FFAT_CMP_NAME_MULTIBYTE
	static t_int32	_lookupForCreateCompareMultiByte(Node* pNodeChild, t_wchar* psCurName,
								char** ppsUpperNameMB, t_wchar* psUpperName,
								t_int32 dwNameLen, ComCxt* pCxt, t_boolean bFree);
	static FFatErr	_lookupCmpNameMultiByte(Node* pNodeParent, t_wchar* psUpperName,
								t_int32 dwNameLen, Node* pNodeChild, FatGetNodeDe* pNodeDE,
								FFatLookupFlag dwLookupFlag, ComCxt* pCxt);
#endif

#ifdef _DEBUG_NODE
	#define FFAT_DEBUG_NODE_PRINTF		FFAT_PRINT_VERBOSE((_T("[NODE,TID(%d)] %s()/%d"), FFAT_DEBUG_GET_TASK_ID(), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE
#else
	#define FFAT_DEBUG_NODE_PRINTF(_msg)
#endif

/**
 * This function initializes FFatNode module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_node_init(void)
{
	// nothing to do

	return FFAT_OK;
}


/**
 * This function terminates FFatNode module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_node_terminate(void)
{
	// nothing to do

	return FFAT_OK;
}


/**
 * Initialize a node
 * 1. Initialize member variable
 * 2. get a free lock
 *
 * Node에 대한 사용을 종료할 경우에는 lock를 release 하기 위해
 * 반드시 ffat_node_terminateNode()를 호출해야한다.
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pNodeParent		: [IN] parent node pointer, May be NULL
 * @param		dwParentCluster	: cluster of parent
 * @param		pNodeChild		: child node pointer
 * @param		bGetLock		:	FFAT_TRUE => assign new lock, for lookup
 *									FFAT_FALSE => Do not assign lock
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_node_initNode(Vol* pVol, Node* pNodeParent, t_uint32 dwParentCluster,
					Node* pNodeChild, t_boolean bGetLock, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeChild);

	FFAT_MEMSET(pNodeChild, 0x00, sizeof(Node));

	if (pNodeParent != NULL)
	{
		IF_UK (VOL_TS(pVol) != NODE_TS(pNodeParent))
		{
			FFAT_LOG_PRINTF((_T("Invalid parent node")));
			return FFAT_EACCESS;
		}

		FFAT_ASSERT(NODE_C(pNodeParent) == dwParentCluster);

		FFAT_ASSERT((dwParentCluster == FFATFS_FAT16_ROOT_CLUSTER) ? 
			(NODE_IS_ROOT(pNodeParent) == FFAT_TRUE) : (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwParentCluster) == FFAT_TRUE));
	}

	FFAT_ASSERT((dwParentCluster == FFATFS_FAT16_ROOT_CLUSTER) ? 
					(dwParentCluster == VOL_RC(pVol)) : (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwParentCluster) == FFAT_TRUE));
	FFAT_ASSERT((VOL_IS_FAT32(pVol) == FFAT_TRUE) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwParentCluster) == FFAT_TRUE) : FFAT_TRUE);

	pNodeChild->dwClusterOfParent	= dwParentCluster;
	pNodeChild->wTimeStamp			= VOL_TS(pVol);

	pNodeChild->pVol = pVol;

	r = ffat_addon_initNode(pVol, pNodeParent, pNodeChild, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to init child node for ADDON")));
		return r;
	}

	if (bGetLock == FFAT_TRUE)
	{
		r = ffat_lock_initRWLock(&pNodeChild->stRWLock);
		FFAT_EO(r, (_T("fail to get a lock for node")));
	}
	else
	{
		FFAT_ASSERT(pNodeChild->stRWLock.pLock == NULL);
		FFAT_ASSERT(pNodeChild->stRWLock.pLockRW == NULL);
	}

	pNodeChild->wRefCount	= 0;
	pNodeChild->wSig		= NODE_SIG;		// set signature

	pNodeChild->pChildNode	= NULL;			// initialize child node
	pNodeChild->pInode		= NULL;			// initialize INODE

	ffat_node_initPAL(pNodeChild);

	NODE_SET_INIT(pNodeChild);

	r = FFAT_OK;

out:
	IF_UK (r != FFAT_OK)
	{
		ffat_addon_terminateNode(pNodeChild, pCxt);		// ignore error
	}

	return r;
}


/**
 * terminates a Node structure usage
 * it release all resource for the node (lock, buffer etc.)
 * Do not initialize information for NODE.
 *
 * @param		pNodeChild	: [IN/OUT] child node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_node_terminateNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);

	FFAT_ASSERT(NODE_IS_INIT(pNode) == FFAT_TRUE);

	r = ffat_addon_terminateNode(pNode, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to terminate node on ADDON module")));
		return r;
	}

	// reset signature
	pNode->wSig		= NODE_SIG_RESET;

	r = ffat_lock_terminateRWLock(&pNode->stRWLock);
	FFAT_ER(r, (_T("fail to release lock")));

	return FFAT_OK;
}


/**
 * Initialize root node
 *
 * @param		pVol		: volume pointer
 * @param		pNode		: Node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: Success
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 * @version		JUN-30-2009 [JeongWoo Park] set time of root as mount time.
 */
FFatErr
ffat_node_initRootNode(struct _Vol* pVol, Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);

	r = ffat_node_initNode(pVol, NULL, VOL_RC(pVol), pNode, FFAT_TRUE, pCxt);
	IF_UK (r < 0)
	{
		return r;
	}

	pNode->dwFlag		|= NODE_ROOT_DIR | FFAT_NODE_DIR | NODE_VALID;
	pNode->stDE.bAttr	|= FFAT_NODE_DIR;	// set directory flag

	pNode->dwCluster	= VOL_RC(pVol);

	// root node start cluster
	// root node start offset
	pNode->stDeInfo.dwDeStartCluster	= NODE_C(pNode);
	pNode->stDeInfo.dwDeStartOffset		= 0x01;

	// set time of root as mount time
	FFATFS_SetDeTime(&pNode->stDE, FAT_UPDATE_DE_ALL_TIME, NULL);

	return FFAT_OK;
}


/**
 * get node status information from short file name entry
 *
 * @param		pNode		: [IN] Node pointer
 * @param		pDE			: [IN] Short File Name entry pointer
 * @param		dwDeCluster	: [IN] DE가 있는 cluster number  (parent first cluster가 아님)
 * @param		dwDeOffset	: [IN] DE offset from the parent first cluster
 * @param		pStatus		: [OUT] Node status
 * @author		DongYoung Seo
 * @version		AUG-09-2006 [DongYoung Seo] First Writing.
 * @version		NOV-04-2008 [DongYoung Seo] shift left 16 bit for access date
 * @version		MAR-18-2009 [DongYoung Seo] add parameter pVol, add pStatus->dwAllocSize
 * @version		OCT-20-2009 [JW Park] Add the consideration about dirty-size state of node
 */
FFatErr
ffat_node_getStatusFromDe(Node* pNode, FatDeSFN* pDE, t_uint32 dwDeCluster, t_uint32 dwDeOffset,
							FFatNodeStatus* pStatus)
{
	Vol* pVol;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pStatus);

	pVol = NODE_VOL(pNode);

	pStatus->dwAttr			= FFATFS_GetDeAttr(pDE);
	pStatus->dwCluster		= FFATFS_GetDeCluster(VOL_VI(pVol), pDE);
	pStatus->dwIno1			= dwDeCluster;
	pStatus->dwIno2			= dwDeOffset;

	pStatus->dwSize			= NODE_S(pNode);
	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(pDE)) : FFAT_TRUE);
	
	pStatus->dwATime		= FFATFS_GetDeADate(pDE) <<16;
	pStatus->dwMTime		= (FFATFS_GetDeWDate(pDE) << 16) | FFATFS_GetDeWTime(pDE);
	pStatus->dwCTime		= (FFATFS_GetDeCDate(pDE) << 16) | FFATFS_GetDeCTime(pDE);
	pStatus->dwCTimeTenth	= FFATFS_GetDeCTimeTenth(pDE);

	// set allocated size
	pStatus->dwAllocSize	= (pStatus->dwSize + VOL_CS(pVol) - 1) & (~VOL_CSM(pVol));

	return FFAT_OK;
}


/**
 * lookup a node in a directory
 *
 * NODE가 존재할 경우 pNodeChild의 정보를 update 하고
 * NODE_INIT flag를 설정한다.
 *
 * @param		pNodeParent		: [IN] Parent node
 * @param		pNodeChild		: [IN/OUT] Child node information storage
 * @param		psName			: [IN] node name
 * @param		dwOffset		: [IN] lookup start offset
 * @param		dwFlag			: [IN] lookup flag
 * @param		pAddonInfo		: [IN] info of ADDON node (may be NULL)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ETOOLONG	: too long name
 * @author		DongYoung Seo
 * @version		AUG-09-2006 [DongYoung Seo] First Writing
 * @version		JAN-18-2008 [GwangOk Go] Lookup improvement
 * @version		JAN-18-2008 [GwangOk Go] remove lock release routine for child
 *										lookup does not assign new lock for child node
 * @version		JAN-30-2009 [JeongWoo Park] support opened-rmdir for parent directory
 * @version		MAY-13-2009 [DongYoung Seo] Support Name compare with multi-byte(CQID:FLASH00021814)
 * @version		JUN-18-2009 [JeongWoo Park] Add the code to support OS specific naming rule
 *											- Case sensitive / OS character set
 */
FFatErr
ffat_node_lookup(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, t_int32 dwOffset,
					FFatLookupFlag dwFlag, void* pAddonInfo, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;
	t_int32				dwNameLen = 0;			// name character count
	t_int32				dwNamePartLen = 0;
	t_int32				dwExtPartLen = 0;
	t_int32				dwSfnNameSize = 0;
	FatNameType			dwNameType;				// name type
	FatGetNodeDe		stGetNodeDE;
	NodeNumericTail*	pNumericTail = NULL;	// Numeric tail의 정보 수집을 위한변수

#ifdef FFAT_VFAT_SUPPORT
	t_wchar*			psConvertedName = NULL;	// name storage for Upper case if volume is mounted with case insensitive
#else
	t_wchar				psConvertedName[FAT_DE_SFN_MAX_LENGTH + 1]; // name(8) + dot(1) + ext.(3) + NULL(1)
#endif

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNodeParent == NULL) || (pNodeChild == NULL) || (psName == NULL) ||
		(NODE_VOL(pNodeParent) == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(NODE_IS_VALID(pNodeParent) == FFAT_TRUE);

	pVol = NODE_VOL(pNodeParent);
	stGetNodeDE.pDE = NULL;

	// lock node
	// lock child
	if (NODE_IS_VALID(pNodeChild) == FFAT_TRUE)
	{
		FFAT_ASSERT(dwFlag & FFAT_LOOKUP_FOR_RENAME);

		// initializes node flag
		pNodeChild->dwFlag &= ~(NODE_NAME_SFN | NODE_NAME_NUMERIC_TAIL | NODE_ROOT_DIR);
	}
	else
	{
		r = ffat_node_initNode(pVol, pNodeParent, NODE_C(pNodeParent), pNodeChild, FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("fail to init child node")));
	}

	if ((dwFlag & FFAT_LOOKUP_NO_LOCK) == 0)
	{
		// lock parent
		r = NODE_GET_READ_LOCK(pNodeParent);
		FFAT_EOTO(r, (_T("Fail to lock parent")), out_parent);

		r = VOL_GET_READ_LOCK(pVol);
		FFAT_EOTO(r, (_T("fail to get read lock")), out_vol);
	}

	VOL_INC_REFCOUNT(pVol);

	// check time stamp between volume and parent node
	// already is done at ffat_node_initNode
	IF_UK (NODE_IS_DIR(pNodeParent) != FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Parent is not a directory !!")));
		r = FFAT_ENOTDIR;
		goto out;
	}

	// check the parent directory is in open-unlinked state
	// LINUX may unlink a directory in open state and also request read operation
	// if opened-unlink state, there is no child
	IF_UK (NODE_IS_OPEN_UNLINK(pNodeParent) == FFAT_TRUE)
	{
		r = FFAT_ENOENT;
		goto out;
	}
	FFAT_ASSERT(NODE_IS_UNLINK(pNodeParent) == FFAT_FALSE);	// the node with only unlinked state can not be here

	// generate short file name
	r = FFATFS_AdjustNameToFatFormat(VOL_VI(pVol), psName, &dwNameLen, &dwNamePartLen, &dwExtPartLen,
							&dwSfnNameSize, &dwNameType, &pNodeChild->stDE);
	FFAT_EO(r, (_T("invalid name for FAT file system")));

	// allocate memory for directory entry
	stGetNodeDE.pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(stGetNodeDE.pDE != NULL);

#ifdef FFAT_VFAT_SUPPORT
	if (dwNameType & FAT_NAME_SFN)
	{
		// node 가 sfn임을 설정한다.
		pNodeChild->dwFlag |= NODE_NAME_SFN;
	}
	else
	{
		pNodeChild->stDE.bNTRes &= ~FAT_DE_SFN_ALL_LOWER;		// set SFN to upper character
	}

	if (dwNameType & FAT_NAME_NUMERIC_TAIL)
	{
		pNodeChild->dwFlag |= NODE_NAME_NUMERIC_TAIL;
	}

	if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME)) &&
		(pNodeChild->dwFlag & NODE_NAME_NUMERIC_TAIL))
	{
		FFAT_ASSERT((pNodeChild->dwFlag & NODE_NAME_SFN) == 0);

		pNumericTail = (NodeNumericTail*)FFAT_LOCAL_ALLOC(sizeof(NodeNumericTail), pCxt);
		FFAT_ASSERT(pNumericTail != NULL);

		// initialize NumericTail
		ffat_node_initNumericTail(pNumericTail, 0);
	}

	if ((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
	{
		// allocate memory for upper name
		psConvertedName = FFAT_LOCAL_ALLOC(((dwNameLen + 1) * sizeof(t_wchar)), pCxt);
		FFAT_ASSERT(psConvertedName != NULL);

		FFAT_TOWUPPER_STR(psConvertedName, psName);
	}
	else
	{
		// Use 'psName' directly if volume is mounted with case sensitive
		psConvertedName = psName;
	}
	
#endif

#ifndef FFAT_VFAT_SUPPORT
	r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), &pNodeChild->stDE, 1, psConvertedName, &dwNameLen, FAT_GEN_NAME_SFN);
	FFAT_EO(r, (_T("fail to generate name from DE")));

	FFAT_ASSERT(dwNameType & FAT_NAME_SFN);
	FFAT_ASSERT((dwNameType & FAT_NAME_NUMERIC_TAIL) == 0);
	FFAT_ASSERT((dwNameType & FAT_NAME_LFN_CHAR) == 0);

	pNodeChild->dwFlag |= NODE_NAME_SFN;
#endif

	pNodeChild->wNameLen			= (t_int16)dwNameLen;
	pNodeChild->wNamePartLen		= (t_int16)dwNamePartLen;	// set character count of name part of long filename
	pNodeChild->bExtPartLen			= (t_uint8)dwExtPartLen;	// set character count of extension part of long filename
	pNodeChild->bSfnNameSize		= (t_uint8)dwSfnNameSize;	// set byte count of name part of short filename

	// initialize stGetNodeDE structure
	r = _lookupHelperForGetNodeDe(pNodeParent, dwOffset, dwNameType, dwNameLen, &stGetNodeDE);
	FFAT_EO(r, (_T("fail go init GetNodeDe ")));

	pNodeChild->stDeInfo.dwDeCount	= stGetNodeDE.dwTargetEntryCount;

	// lookup with ADDON module
	r = ffat_addon_lookup(pNodeParent, pNodeChild, psConvertedName, dwNameLen,
						dwFlag, &stGetNodeDE, pNumericTail, pCxt);
	if (r < 0)
	{
		goto out;
	}
	else if (r == FFAT_DONE)
	{
		// lookup이 ADDON module에서 처리됨.
		goto lookup_done;
	}

	FFAT_ASSERT(stGetNodeDE.dwTargetEntryCount >= 1);

	if (dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE))
	{
		pNodeChild->stDeInfo.dwFreeCount	= 0;
		pNodeChild->stDeInfo.dwFreeCluster	= 0;
		pNodeChild->stDeInfo.dwFreeOffset	= 0;

		r = _lookupForCreate(pNodeParent, psConvertedName, dwNameLen,
							pNodeChild, &stGetNodeDE, dwFlag, pNumericTail, pCxt);
		if (r < 0)
		{
			goto out;
		}
	}
	else
	{
#ifndef FFAT_CMP_NAME_MULTIBYTE
		stGetNodeDE.psName				= psConvertedName;
		stGetNodeDE.dwNameLen			= dwNameLen;
		stGetNodeDE.psShortName			= pNodeChild->stDE.sName;
		stGetNodeDE.bExactOffset		= FFAT_FALSE;
		stGetNodeDE.dwClusterOfOffset	= 0;

		// get directory entry
		r = ffat_dir_getDirEntry(pVol, pNodeParent, &stGetNodeDE, FFAT_TRUE, FFAT_TRUE, pCxt);
		if (r < 0)
		{
			// Node를 찾지 못했거나 에러 발생.
			if (r == FFAT_EEOF)
			{
				r = FFAT_ENOENT;
			}
			else if (r == FFAT_EXDE)
			{
				r = FFAT_EFAT;
			}

			goto out;
		}
		
		// LFN only일 경우 SFN만 리턴될 수 없다.
		FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (stGetNodeDE.dwEntryCount > 1));
#else
		r = _lookupCmpNameMultiByte(pNodeParent, psConvertedName, dwNameLen,
							pNodeChild, &stGetNodeDE, dwFlag, pCxt);
		if (r < 0)
		{
			goto out;
		}
#endif
		FFAT_ASSERT(r == FFAT_OK);
	}

lookup_done:
	// here we found one node, and we have DE for target node.
	// fill node information
	ffat_node_fillNodeInfo(pNodeChild, &stGetNodeDE, pAddonInfo);

	// set node valid flag
	NODE_SET_VALID(pNodeChild);

	// give new node information to ADDON module
	r = ffat_addon_afterLookup(pNodeParent, pNodeChild, dwFlag, pCxt);

	FFAT_ASSERT(NODE_S(pNodeChild) == FFATFS_GetDeSize(NODE_DE(pNodeChild)));
	FFAT_ASSERT(NODE_C(pNodeChild) == FFATFS_GetDeCluster(VOL_VI(pVol), NODE_DE(pNodeChild)));

out:
	if (dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME))
	{
		if ((dwFlag & FFAT_LOOKUP_SET_CHILD) && (r == FFAT_ENOENT) &&
			(VOL_IS_RDONLY(pVol) == FFAT_FALSE))
		{
			// set child node pointer of parent node
			pNodeParent->pChildNode = pNodeChild;
		}
	}

#ifdef FFAT_VFAT_SUPPORT
	// free memory for upper name
	if (psConvertedName != psName)
	{
		FFAT_LOCAL_FREE(psConvertedName, ((dwNameLen + 1) * sizeof(t_wchar)), pCxt);
	}
#endif

	// free memory for numeric tail
	FFAT_LOCAL_FREE(pNumericTail, sizeof(NodeNumericTail), pCxt);

	// free memory for directory entry
	FFAT_LOCAL_FREE(stGetNodeDE.pDE, VOL_MSD(pVol), pCxt);

	VOL_DEC_REFCOUNT(pVol);

	if ((dwFlag & FFAT_LOOKUP_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(pVol);

out_vol:
		r |= NODE_PUT_READ_LOCK(pNodeParent);
	}

out_parent:
	// terminate node

	return r;
}


/**
* open a node
*
* Assign a lock for node
*
* @param		pNode		: [IN] node pointer
* @param		pINode		: [IN] correspondent INODE Pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_EINVALID	: Invalid parameter
* @author		DongYoung Seo
* @version		AUG-14-2006 [DongYoung Seo] First Writing
*/
FFatErr
ffat_node_open(Node* pNode, void* pInode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = ffat_lock_initRWLock(&pNode->stRWLock);
	FFAT_ER(r, (_T("fail get a lock")));

	r = NODE_GET_WRITE_LOCK(pNode);
	FFAT_EO(r, (_T("fail to get write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	FFAT_ASSERT(NODE_IS_ROOT(pNode) ? FFAT_TRUE : NODE_RC(pNode) >= 0);

	NODE_INC_REF_COUNT(pNode);

	FFAT_ASSERT(NODE_INODE(pNode) ? NODE_INODE(pNode) == pInode : FFAT_TRUE);

	NODE_INODE(pNode) = pInode;

	NODE_SET_OPEN(pNode);

	FFAT_DEBUG_NODE_PRINTF((_T("Open %s, ref:%d\n"), pNode->stDE.sName, NODE_RC(pNode)));

	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_WRITE_LOCK(pNode);

out:
	IF_UK (r < 0)
	{
		ffat_lock_terminateRWLock(&pNode->stRWLock);
	}

	return r;
}


/**
 * close a node
 *
 * this function is called by close() or destroy()
 *
 * on close(), NC flag should have FFAT_NODE_CLOSE_SYNC to sync meta-data & user-data
 * on destroy(), NC flag should have FFAT_NODE_CLOSE_RELEASE_RESOURCE to release resources of node
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwNCFlag	: [IN] flag for node close
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing
 * @version		JUN-19-2009 [GwangOk Go] readjust NC flag & merge root close
 */
FFatErr
ffat_node_close(Node* pNode, FFatNodeCloseFlag dwNCFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_VOL(pNode));
	FFAT_ASSERT((NODE_IS_ROOT(pNode) == FFAT_TRUE) ? (NODE_IS_UNLINK(pNode) == FFAT_FALSE) : FFAT_TRUE);
	FFAT_ASSERT((NODE_IS_ROOT(pNode) == FFAT_TRUE) ? (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE) : FFAT_TRUE);

	if (pNode->wSig == NODE_SIG_RESET)
	{
		// DO NOTHING
		return FFAT_OK;
	}

	// in case of close root directory
	//	if close by user operation, NC flag should not have FFAT_NODE_CLOSE_RELEASE_RESOURCE
	//	if close by u-mount, NC flag should have FFAT_NODE_CLOSE_RELEASE_RESOURCE

	r = NODE_GET_WRITE_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	if (dwNCFlag & FFAT_NODE_CLOSE_SYNC)
	{
		r = ffat_node_sync(pNode, NODE_S(pNode), NODE_SYNC_NO_LOCK, pCxt);
		FFAT_EO(r, (_T("fail to sync a node")));
	}

	if (dwNCFlag & FFAT_NODE_CLOSE_DEC_REFERENCE)
	{
		FFAT_ASSERT(NODE_RC(pNode) > 0);
		FFAT_ASSERT(NODE_IS_OPEN(pNode) == FFAT_TRUE);

		NODE_DEC_REF_COUNT(pNode);

		if (NODE_RC(pNode) == 0)
		{
			NODE_CLEAR_OPEN(pNode);
		}
	}

	if ((dwNCFlag & FFAT_NODE_CLOSE_RELEASE_RESOURCE) == 0)
	{
		r = FFAT_OK;
		goto out;
	}

	FFAT_ASSERT(NODE_IS_OPEN(pNode) == FFAT_FALSE);
	FFAT_ASSERT(NODE_RC(pNode) == 0);

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	FFAT_ASSERT(NODE_IS_ROOT(pNode) ? FFAT_TRUE : NODE_RC(pNode) == 0);

	r = ffat_addon_afterCloseNode(pNode, pCxt);
	FFAT_EO(r, (_T("Fail to close after operation for a node on ADDON module")));

	NODE_CLEAR_OPEN(pNode);

	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
	r |= NODE_PUT_WRITE_LOCK(pNode);

	r |= ffat_node_terminateNode(pNode, pCxt);

	r = FFAT_OK;

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	FFAT_DEBUG_NODE_PRINTF((_T("Close %s, ref:%d\n"), pNode->stDE.sName, NODE_RC(pNode)));

	return r;

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_WRITE_LOCK(pNode);

	return r;
}


/**
 * Create a node
 *
 * node를 create 하고 create된 node의 정보를 return 한다.
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNodeChild	: [IN/OUT] child node pointer
 * @param		psName		: [IN] node name
 * @param		dwFlag		: [IN] node creation flag
 * @param		pAddonInfo	: [IN] info of ADDON node (may be NULL)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume, child is root node
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 * @history		DEC-06-2007 [InHwan Choi] apply to open unlink
 * @history		JAN-29-2009 [DongYoung Seo] bug fix, CQID:FLASH00019961
 *								overwrite error value while calling the ffat_addon_afterCreate()
 * @version		JAN-30-2009 [JeongWoo Park] Add assert for checking the parent.
 */
FFatErr
ffat_node_create(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, FFatCreateFlag dwFlag,
				void* pAddonInfo, ComCxt* pCxt)
{
	FFatErr		r;
	Vol*		pVol;
	FatDeSFN*	pDE = NULL;
	t_int32		dwLFNE_Count;							// LFN Entry Count
	t_int8		bCheckSum = 0;
	t_uint32	dwCluster = 0;								// cluster for directory
	t_int32		dwClusterCountDE = 0;							// cluster count for directory entry write
	t_uint32	pdwClustersDE[NODE_MAX_CLUSTER_FOR_CREATE];	// clusters for directory entry write

	// for log recovery
	FFatCacheFlag	dwCacheFlag	= FFAT_CACHE_NONE;		// flag for cache operation
	FatUpdateFlag	dwFUFlag	= FAT_UPDATE_NONE;		// flag for FAT update operation

	// for add-on check
	t_boolean	bAddonInvoked = FFAT_FALSE;				// boolean to check ADDON module is invoked or not.

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pAddonInfo);
	FFAT_ASSERT(pCxt);

	FFAT_ASSERT(NODE_VOL(pNodeParent));
	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_VALID(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_UNLINK(pNodeParent) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeParent) == FFAT_FALSE);

	// lookup success한 node를 다시 create할 수 없음
	// 존재한 file을 다시 creat하는 경우는 nestle에서 truncate로 처리
	FFAT_ASSERT(NODE_IS_VALID(pNodeChild) == FFAT_FALSE);

	pVol = NODE_VOL(pNodeParent);

	if ((dwFlag & FFAT_CREATE_NO_LOCK) == 0)
	{
		// lock parent node
		r = NODE_GET_WRITE_LOCK(pNodeParent);
		FFAT_ER(r, (_T("fail to get parent write lock")));

		r = VOL_GET_READ_LOCK(pVol);
		FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);
	}

	FFAT_ASSERT(NODE_IS_UNLINK(pNodeChild) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeChild) == FFAT_FALSE);

	VOL_INC_REFCOUNT(pVol);

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNodeParent) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not same")));
		r = FFAT_EXDEV;
		goto out;
	}

	if (VOL_IS_RDONLY(pVol) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("read only volume !!")));
		r = FFAT_EROFS;
		goto out;
	}

	// check lookup flag
	if (dwFlag & FFAT_CREATE_LOOKUP)
	{
		// create전 lookup을 해야할 경우는 lookup을 수행한다.
		// 이때는 lookup에 대한 lock을 하지 않는다.
		r = ffat_node_lookup(pNodeParent, pNodeChild, psName, 0,
					(FFAT_LOOKUP_NO_LOCK | FFAT_LOOKUP_FOR_CREATE), pAddonInfo, pCxt);
		if ((r < 0) && (r != FFAT_ENOENT))
		{
			FFAT_LOG_PRINTF((_T("There is error while lookup a node")));
			goto out;
		}
	}

#if 0	// not used (when creating exist node, nestle use truncate)
	// valid node 일 경우 lookup이 이미 수행되었고 node가 존재함을 의미한다.
	if (NODE_IS_VALID(pNodeChild) == FFAT_TRUE)
	{
		// check it is root node
		if (NODE_IS_ROOT(pNodeChild) == FFAT_TRUE)
		{
			// This is root node !!!
			FFAT_LOG_PRINTF((_T("This is a root node !!!")));
			r = FFAT_EACCESS;
			goto out;
		}

		// There is a valid node !!!
		// and the child node information is stored at pNodeChild.
		r = _createExistNode(pNodeChild, dwFlag, pCxt);
		FFAT_EO(r, (_T("fail to create an exist node")));

		// check node name
		// rename it, (대/소 문자만 다른 경우를 처리하기 위함)
		r = ffat_node_rename(pNodeParent, pNodeChild, pNodeParent, pNodeChild,
						psName, FFAT_RENAME_NO_LOCK, pCxt);
		goto out;
	}
#endif

	// the node is not exist
	// we can create it.

	// allocate memory for directory entry
	pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(pDE != NULL);

#ifdef FFAT_VFAT_SUPPORT
	if (pNodeChild->dwFlag & NODE_NAME_SFN)
	{
		dwLFNE_Count = 0;
	}
	else
	{
		// max name length check
		if (dwFlag & FFAT_CREATE_ATTR_DIR)
		{
			if (pNodeChild->wNameLen > FFAT_DIR_NAME_MAX_LENGTH)
			{
				FFAT_LOG_PRINTF((_T("Too long name for directory")));
				r = FFAT_ETOOLONG;
				goto out;
			}
		}
		else
		{
			if (pNodeChild->wNameLen > FFAT_FILE_NAME_MAX_LENGTH)
			{
				FFAT_LOG_PRINTF((_T("Too long name for FILE")));
				r = FFAT_ETOOLONG;
				goto out;
			}
		}

		bCheckSum = FFATFS_GetCheckSum(&pNodeChild->stDE);

		// generate directory entry
		r = FFATFS_GenLFNE(psName, pNodeChild->wNameLen, (FatDeLFN*)pDE, &dwLFNE_Count, bCheckSum);
		FFAT_EO(r, (_T("fail to generate long file name entry")));
	}
#else
	FFAT_ASSERT(pNodeChild->dwFlag & NODE_NAME_SFN);
	dwLFNE_Count = 0;
#endif

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EXPAND_PARENT_BEFORE);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_EXPAND_PARENT_BEFORE);

	// lock CORE for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock CORE")));

	// expand parent directory to get clusters for directory entry write when there is not enough space
	r = ffat_node_createExpandParent(pNodeParent, pNodeChild,
					&dwClusterCountDE, pdwClustersDE, NODE_MAX_CLUSTER_FOR_CREATE, pCxt);
	FFAT_EO(r, (_T("fail to expand parent directory")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EXPAND_PARENT_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_EXPAND_PARENT_AFTER);

	// if it is a directory, allocate a new cluster.
	// NOTICE!. do not move this code to before _createExpandParent()
	if (dwFlag & FFAT_CREATE_ATTR_DIR)
	{
		FFatVC	stVC;
		FFatVCE	stVCEntry;

		VC_INIT(&stVC, VC_NO_OFFSET);

		stVC.dwTotalEntryCount	= 1;
		stVC.pVCE				= &stVCEntry;

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_GET_FREE_CLUSTER_BEFORE);

		r = ffat_misc_getFreeClusterForDir(pNodeChild, &stVC, 1, pCxt);
		FFAT_EO(r, (_T("fail to get a free cluster for directory")));

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_GET_FREE_CLUSTER_AFTER);
		FFAT_DEBUG_LRT_CHECK(FFAT_EIO_TEST_WRITE_DATA_BEFORE);

		FFAT_ASSERT(VC_CC(&stVC) == 1);
		
		dwCluster = VC_FC(&stVC);
		pNodeChild->dwCluster = dwCluster;
	}
	else
	{
		pNodeChild->dwCluster = 0;
	}

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_UPDATE_BEFORE);

	r = ffat_node_updateSFNE(pNodeChild, 0, (t_uint8)(dwFlag & FFAT_CREATE_ATTR_MASK),
						pNodeChild->dwCluster, FAT_UPDATE_DE_ALL, dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to update SFNE")));

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_UPDATE_AFTER);

	if (dwFlag & FFAT_CREATE_ATTR_DIR)
	{
		// Directory일 경우는 할당받은 cluster를 초기화 한다.
		r = ffat_dir_initCluster(pNodeParent, pNodeChild, pCxt);
		FFAT_EO(r, (_T("fail to init cluster for directory")));
	}

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_LOG_BEFORE);

	if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// pDE : memory for directory entry, LFNE is stored, but SFNE is not stored
	// if support RFS mechanism about POSIX ATTR, SFNE is modified in ADDON module
	r = ffat_addon_create(pNodeParent, pNodeChild, psName, pDE, bCheckSum,
							pAddonInfo, dwFlag, &dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to create a node with add-on module")));
	bAddonInvoked = FFAT_TRUE;

	// copy SFN to pDE
	FFAT_MEMCPY(&pDE[dwLFNE_Count], &pNodeChild->stDE, sizeof(FatDeSFN));

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_LOG_AFTER);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_DE_UPDATE_BEFORE);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_DE_UPDATE_BEFORE);
	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_WRITEDE_BEFORE);

	FFAT_ASSERT(ffat_share_checkDE(pNodeChild, psName, pDE, pCxt) == FFAT_OK);

	// set Cluster and Offset for DE
	pNodeChild->stDeInfo.dwDeEndCluster		= pdwClustersDE[dwClusterCountDE - 1];
	pNodeChild->stDeInfo.dwDeEndOffset		= pNodeChild->stDeInfo.dwDeStartOffset 
								+ ((pNodeChild->stDeInfo.dwDeCount - 1) << FAT_DE_SIZE_BITS);
	pNodeChild->stDeInfo.dwDeClusterSFNE	= pNodeChild->stDeInfo.dwDeEndCluster;
	pNodeChild->stDeInfo.dwDeOffsetSFNE		= pNodeChild->stDeInfo.dwDeEndOffset;

	r = ffat_node_writeDEs(pNodeParent, pNodeChild, pDE, pNodeChild->stDeInfo.dwDeCount,
					pdwClustersDE, dwClusterCountDE, (dwCacheFlag | FFAT_CACHE_DATA_DE), pCxt);
	FFAT_EO(r, (_T("fail to write directory entries")));

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_MKDIR_WRITEDE_AFTER);

	if (NODE_C(pNodeChild) != 0)
	{
		dwFUFlag = VOL_GET_FAT_UPDATE_FLAG(NODE_VOL(pNodeChild));

		r = ffat_misc_makeClusterChain(pNodeChild, 0, 1, &dwCluster, dwFUFlag, dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to update cluster")));
			// delete directory entry
			_deleteDE(pNodeChild, dwCacheFlag, pCxt);		// ignore error
			goto out;
		}
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_DE_UPDATE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_MKDIR_DE_UPDATE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_EIO_TEST_WRITE_METADATA_AFTER);

	NODE_SET_VALID(pNodeChild);

out:
	IF_LK (bAddonInvoked == FFAT_TRUE)
	{
		r |= ffat_addon_afterCreate(pNodeParent, pNodeChild, psName, pdwClustersDE,
							dwClusterCountDE, FFAT_IS_SUCCESS(r), pCxt);
	}

	IF_LK ((r < 0) && (dwCluster != 0))
	{
		// add free cluster which be gotten last to FCC
		r |= ffat_addon_addFreeClusters(pVol, dwCluster, 1, pCxt);
	}

	// unlock CORE for free cluster sync
	r |= ffat_core_unlock(pCxt);

	FFAT_LOCAL_FREE(pDE, VOL_MSD(pVol), pCxt);

	VOL_DEC_REFCOUNT(pVol);

	if ((dwFlag & FFAT_CREATE_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(pVol);

out_vol:
		r |= NODE_PUT_WRITE_LOCK(pNodeParent);
	}

	return r;
}


/**
 * write/update a SFNE for a node
 * 
 * Node의 Short File Name Entry 를 update write 한다.
 * 만약 Node가 open unlink 상태라면 DE를 wrtie하지 않는다.
 *
 * This will set/clean the dirty-size flag of node
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwSize			: [IN] node size
 * @param		bAttr			: [IN] node attribute
 * @param		dwCluster		: [IN] node cluster
 * @param		dwDUFlag		: [IN] flag for De update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @history		DEC-05-2007 [InHwan Choi] apply to open unlink
 * @history		SEP-10-2008 [DongYoung Seo] change FatDeUpdateFlag operation.
 *											NO_WRITE Flag is removed, default is WRITE DE
 * @history		APR-13-2009 [JW Park] Add the code for FAT_UPDATE_DE_FORCE to skip IO error
 * @history		OCT-20-2009 [JW Park] Add the code for consideration of dirty-size stat of node
 */
FFatErr
ffat_node_updateSFNE(Node* pNode, t_uint32 dwSize, t_uint8 bAttr,
						t_uint32 dwCluster, FatDeUpdateFlag dwDUFlag,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	FatDeSFN		stSFN_Backup;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_FALSE);

	pVol	= NODE_VOL(pNode);

	dwCacheFlag |= FFAT_CACHE_DATA_DE;

	if ((dwDUFlag & FAT_UPDATE_DE_WRITE_DE) && (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE))
	{
		// write DE를 하는 경우만 backup
		FFAT_MEMCPY(&stSFN_Backup, &pNode->stDE, sizeof(FatDeSFN));
	}

	if (dwDUFlag & FAT_UPDATE_DE_SIZE)
	{
		// mark dirty bit if it begins to dirty-size state
		if (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
		{
			pNode->stDE.bNTRes |= FAT_DE_DIRTY_SIZE;
			NODE_SET_DIRTY_SIZE(pNode);
		}

		// update size if it is not dirty-size state or 
		// dwDUFlag indicate to remove the dirty-size state
		if ((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ||
			(dwDUFlag & FAT_UPDATE_REMOVE_DIRTY))
		{
			FFATFS_SetDeSize(&pNode->stDE, dwSize);
			pNode->stDE.bNTRes &= ~FAT_DE_DIRTY_SIZE;
		}
	}
	else if (dwDUFlag & FAT_UPDATE_REMOVE_DIRTY)
	{
		// if there is no size information, use the size of node
		FFATFS_SetDeSize(&pNode->stDE, NODE_S(pNode));
		pNode->stDE.bNTRes &= ~FAT_DE_DIRTY_SIZE;
	}
	else if (dwDUFlag & FAT_UPDATE_ONLY_DE_SIZE)
	{
		// only update DE size, not remove dirty-size state.
		FFAT_ASSERT(NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE);

		FFATFS_SetDeSize(&pNode->stDE, dwSize);
	}

	// debug begin
	FFAT_ASSERT(((NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE) && ((dwDUFlag & FAT_UPDATE_REMOVE_DIRTY) == 0))
		? ((pNode->stDE.bNTRes & FAT_DE_DIRTY_SIZE) != 0)
		: ((pNode->stDE.bNTRes & FAT_DE_DIRTY_SIZE) == 0));
	// debug end

	if (dwDUFlag & FAT_UPDATE_DE_ATTR)
	{
		FFATFS_SetDeAttr(&pNode->stDE, bAttr);
	}

	if (dwDUFlag & FAT_UPDATE_DE_ALL_TIME)
	{
		FFATFS_SetDeTime(&pNode->stDE, dwDUFlag, NULL);
	}

	if (dwDUFlag & FAT_UPDATE_DE_CLUSTER)
	{
		FFATFS_SetDeCluster(&pNode->stDE, dwCluster);
	}

	// update SFNE name type
	if ((pNode->dwFlag & NODE_NAME_SFN) == 0)
	{
		pNode->stDE.bNTRes &= ~FAT_DE_SFN_ALL_LOWER;		// set SFN to upper character
	}

	if ((dwDUFlag & FAT_UPDATE_DE_WRITE_DE) == 0)
	{
		return FFAT_OK;
	}

	// Do not write directory entry in following case
	// 1) if node is open unlink state,
	// 2) if node is dirty state not begin state (for the performance)
	//    without FAT_UPDATE_REMOVE_DIRTY or FAT_UPDATE_DE_NEED_WRITE flag.
	if ((NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE)
		&&
		((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ||
		(NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE) ||
		(dwDUFlag & (FAT_UPDATE_REMOVE_DIRTY | FAT_UPDATE_DE_NEED_WRITE)))
		)
	{
		FFAT_ASSERT((pNode->stDeInfo.dwDeEndCluster != 0) ? (pNode->stDeInfo.dwDeClusterSFNE != 0) : FFAT_TRUE);

		// write DE
		// Extended DE가 있을경우 SFNDE의 위치는 'pNode->stDeInfo.dwDeEndOffset - 32' 임
		r = ffat_writeDEs(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeClusterSFNE,
			pNode->stDeInfo.dwDeOffsetSFNE ,
			(t_int8*)&pNode->stDE, FAT_DE_SIZE, dwCacheFlag, pNode, pCxt);
		IF_UK ((r < 0) && ((dwDUFlag & FAT_UPDATE_DE_FORCE) == 0))
		{
			FFAT_LOG_PRINTF((_T("Fail to write directory entry")));

			// restore SFNE
			FFAT_MEMCPY(&pNode->stDE, &stSFN_Backup, sizeof(FatDeSFN));
			return FFAT_EIO;
		}

		FFAT_DEBUG_NODE_PRINTF((_T("UPDATE SFNE, NodePtr/NodeCluster/NodeSize:0x%X/0x%X/%d\n"), pNode, FFATFS_GetDeCluster(VOL_VI(pVol), NODE_DE(pNode)), FFATFS_GetDeSize(NODE_DE(pNode))));
	}

	if (dwDUFlag & FAT_UPDATE_DE_SIZE)
	{
		pNode->dwSize = dwSize;
	}

	if (dwDUFlag & FAT_UPDATE_DE_CLUSTER)
	{
		pNode->dwCluster = dwCluster;
	}

	// remove the dirty-size-begin flag after write DE
	if (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
	{
		NODE_CLEAR_DIRTY_SIZE_BEGIN(pNode);
	}

	// remove the dirty-size-rdonly flag after write DE
	if (NODE_IS_DIRTY_SIZE_RDONLY(pNode) == FFAT_TRUE)
	{
		NODE_CLEAR_DIRTY_SIZE_RDONLY(pNode);
	}
	
	// if the dirty bit is removed
	if (dwDUFlag & FAT_UPDATE_REMOVE_DIRTY)
	{
		NODE_CLEAR_DIRTY_SIZE(pNode);
	}

	return FFAT_OK;
}


/**
 * set status of a node
 *
 * It can change attribute, last access date, write date/time, creat data/time
 * (Others will be ignored)
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [IN] node information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume, node is root
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @history		FEB-28-2007 [DongYoung Seo] Add root node check
 * @history		DEC-23-2008 [JeongWoo Park] support setstatus of EA / symlink 
 */
FFatErr
ffat_node_setStatus(Node* pNode, FFatNodeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr				r;
	FatDeSFN			stSFN;		// for SFN backup
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;
	t_boolean			bAddonInvoked = FFAT_FALSE;
								// boolean to check ADDON module is invoked or not.
	FFatNodeStatus		stStatusInternal;
								// temporal buffer for pStatus

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNode == NULL) || (pStatus == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	r = NODE_GET_WRITE_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get write lock for a node")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get read lock for a volume")), out_vol);

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not same")));
		r = FFAT_EXDEV;
		goto out;
	}

	if (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("This volume is mounted with read-only flag")));
		r = FFAT_EROFS;
		goto out;
	}

	// check root
	if (NODE_IS_ROOT(pNode) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("This is root node")));
		r = FFAT_EACCESS;
		goto out;
	}

	if (((NODE_IS_DIR(pNode) == FFAT_TRUE) ? FFAT_TRUE : FFAT_FALSE)
			!= (((pStatus->dwAttr) & FFAT_ATTR_DIR) ? FFAT_TRUE : FFAT_FALSE))
	{
		FFAT_LOG_PRINTF((_T("Invalid attribute, can not change file to dir, dir to file")));
		r = FFAT_EINVALID;
		goto out;
	}

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

	if (VOL_IS_SYNC_META(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// ADDON will be edit the Status, use the another buffer
	FFAT_MEMCPY(&stStatusInternal, pStatus, sizeof(FFatNodeStatus));

	r = ffat_addon_setStatus(pNode, &stStatusInternal, &dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to set status on ADDON module")));
	bAddonInvoked = FFAT_TRUE;

	// backup current DE information
	FFAT_MEMCPY(&stSFN, &pNode->stDE, FAT_DE_SIZE);

	pNode->stDE.wCrtDate		= FFAT_BO_UINT16((t_uint16)(stStatusInternal.dwCTime >> 16));
	pNode->stDE.wCrtTime		= FFAT_BO_UINT16((t_uint16)(stStatusInternal.dwCTime & 0xFFFF));
	pNode->stDE.wWrtDate		= FFAT_BO_UINT16((t_uint16)(stStatusInternal.dwMTime >> 16));
	pNode->stDE.wWrtTime		= FFAT_BO_UINT16((t_uint16)(stStatusInternal.dwMTime & 0xFFFF));
	pNode->stDE.wLstAccDate		= FFAT_BO_UINT16((t_uint16)(stStatusInternal.dwATime >> 16));
	pNode->stDE.bCrtTimeTenth	= (t_uint8)stStatusInternal.dwCTimeTenth;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_STATUS_DE_UPDATE_BEFORE);

	r = ffat_node_updateSFNE(pNode, 0, (t_uint8)(stStatusInternal.dwAttr & FFAT_ATTR_MASK), 0, 
							(FAT_UPDATE_DE_ATTR | FAT_UPDATE_DE_WRITE_DE | FAT_UPDATE_DE_NEED_WRITE), dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to update short file name entry")));
		// restore original entry
		FFAT_MEMCPY(&pNode->stDE, &stSFN, FAT_DE_SIZE);
		goto out;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_STATUS_DE_UPDATE_AFTER);

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

	r = FFAT_OK;

out:
	IF_LK (bAddonInvoked == FFAT_TRUE)
	{
		r |= ffat_addon_afterSetStatus(pNode, FFAT_IS_SUCCESS(r), pCxt);
	}

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_WRITE_LOCK(pNode);

	return r;
}


/**
 * get status of a node
 *
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [IN/OUT] node information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_node_getStatus(Node* pNode, FFatNodeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNode == NULL) || (pStatus == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	r = NODE_GET_READ_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get node read lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = ffat_addon_getStatusFromDe(pNode, &pNode->stDE, pNode->stDeInfo.dwDeStartCluster,
						pNode->stDeInfo.dwDeStartOffset, pStatus, pCxt);
	if (r == FFAT_DONE)
	{
		r = FFAT_OK;
	}
	else
	{
		r = ffat_node_getStatusFromDe(pNode, &pNode->stDE, pNode->stDeInfo.dwDeStartCluster,
						pNode->stDeInfo.dwDeStartOffset, pStatus);
	}

	FFAT_EO(r, (_T("fail to get node status")));

#ifdef FFAT_GET_REAL_SIZE_OF_DIR
	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		r = ffat_dir_getSize(pNode, &pStatus->dwSize, pCxt);
		FFAT_EO(r, "fail to get size of directory");
		// Bug fix 2012050039: added by SISO- to update allocation size, according to the size being calculated
		pStatus->dwAllocSize += pStatus->dwSize;
	}
#endif

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
out_vol:

	r |= NODE_PUT_READ_LOCK(pNode);

	return r;
}


/**
 * sync a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwSizeToBe	: [IN] size to be (size of VNODE)
 * @param		dwFlag		: [IN] flags for sync node
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		NOV-16-2007 [DongYoung Seo] Add timestamp checking
 * @version		OCT-20-2009 [JW Park] Add the consideration about dirty-size state of node
 * @version		DEC-01-2009 [JW Park] Add the consideration about shrink during sync
 */
FFatErr
ffat_node_sync(Node* pNode, t_uint32 dwSizeToBe, NodeSyncFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		FFAT_ASSERT(pNode);
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_RDONLY(pNode) == FFAT_FALSE);

	if ((dwFlag & NODE_SYNC_NO_LOCK) == 0)
	{
		r = NODE_GET_WRITE_LOCK(pNode);
		FFAT_ER(r, (_T(" fail to get node write lock")));

		r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to get volume read lock")));
			goto out_vol;
		}
	}

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	if (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		// not to do for read only volume
		r = FFAT_OK;
		goto out;
	}

	// CHECK TIME STAMP
	// check time stamp between volume and parent node
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE ? NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)) : FFAT_TRUE);

	// remove // when you need to do something before syncNode
	//r = ffat_addon_syncNode();
	//FFAT_EO(r, (_T("fail to sync a node on ADDON module")));

	// If node is dirty-size state, remove the dirty bit and update the size of DE case by case.
	// This is transaction safe operation (only write 32byte)
	if ((NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE) &&
		(NODE_IS_UNLINK(pNode) == FFAT_FALSE) &&
		((dwFlag & NODE_SYNC_NO_WRITE_DE) == 0))
	{
		FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);

		IF_UK (NODE_S(pNode) > dwSizeToBe)
		{
			// During synchronizing, write/expand operation is performed by another thread
			// after dwSizeToBe was set in Nestle.

			// In this case, all of user data is not synchronized.			
			// Remain dirty-size state, update only size of DE.

			r = ffat_node_updateSFNE(pNode, dwSizeToBe, 0, 0,
									(FAT_UPDATE_ONLY_DE_SIZE | FAT_UPDATE_DE_NEED_WRITE | FAT_UPDATE_DE_WRITE_DE),
									FFAT_CACHE_SYNC, pCxt);

			FFAT_EO(r, (_T("fail to update dirty DE of the node")));
		}
		else
		{
			// During synchronizing, shrink operation or no operation is performed by another thread
			// after dwSizeToBe was set in Nestle.

			// In this case, all of user data is synchronized.
			// Remove the dirty-size state.

			r = ffat_node_updateSFNE(pNode, NODE_S(pNode), 0, 0,
									(FAT_UPDATE_DE_SIZE | FAT_UPDATE_REMOVE_DIRTY | FAT_UPDATE_DE_WRITE_DE),
									FFAT_CACHE_SYNC, pCxt);

			FFAT_EO(r, (_T("fail to update dirty DE of the node")));

			FFAT_ASSERT(NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode)));
		}
	}

	r = FFATFS_SyncNode(NODE_VI(pNode), pNode, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync a node")));

	r = ffat_addon_afterSyncNode(pNode, pCxt);
	FFAT_EO(r, (_T("fail to sync a node on ADDON module")));
	
	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE ? NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)) : FFAT_TRUE);

out:

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	if ((dwFlag & NODE_SYNC_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
	}

out_vol:
	if ((dwFlag & NODE_SYNC_NO_LOCK) == 0)
	{
		r |= NODE_PUT_WRITE_LOCK(pNode);
	}

	return r;
}


/**
 * rename a node
 *
 * it can not rename to another volume
 * it can not rename a file to a directory or a directory to a file.
 * it can rename a directory to another exist directory when it does not have any node in it.
 *
 * rename 중 power off가 될 경우, destination node의 대 소 문자는 변경된 상태로 남아 있을 수 있다..
 *
 *	Linux does not know whether node is in open state or not
 *	So Linux glue always set FFAT_RENAME_TARGET_OPENED flag.
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node
 * @param		pNodeDesParent	: [IN] parent node of destination(target)
 * @param		pNodeDes		: [IN] destination node
 * @param		psName			: [IN] target node name
 * @param		dwFlag			: [IN] rename flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOSUPPORT	: no support rename between different volumes
 * @return		FFAT_EXDEV		: media ejected (time stamp checking error)
 * @return		FFAT_ENOTDIR	: parent is not a directory
 * @return		FFAT_EACCESS	: volume is mounted with read-only flag
 * @author		DongYoung Seo
 * @version		28-AUG-2006 [DongYoung Seo] First Writing.
 * @version		15-DEC-2006 [SungWoo Jo] Added updating the address(..) of source node
 * @version		10-SEP-200? [DongYoung Seo] update access data
 * @version		09-DEC-2008 [DongYoung Seo] add pNodeNew to support open rename
 * @version		06-JAN-2008 [DongYoung Seo] remove reference code for XDE
 * @version		11-FEB-2009 [GwangOk Go] update renamed node info on pNodeSrc (delete pNodeNew)
 * @version		26-OCT-2009 [JW Park] Add the consideration code about dirty-size pNodeSrc.
 *										In this case, pNodeNew also becomes dirty-size node.
 */
FFatErr
ffat_node_rename(Node* pNodeSrcParent, Node* pNodeSrc, Node* pNodeDesParent, Node* pNodeDes,
				t_wchar* psName, FFatRenameFlag dwFlag, ComCxt* pCxt)
{
	t_boolean		bLockDes = FFAT_FALSE;
	FFatErr			r;
	t_wchar*		psSrcName = NULL;
	t_int32			dwSrcLen;							// source node name length
	Vol*			pVol;
	t_boolean		bSame = FFAT_FALSE;					// same node or not.
	t_boolean		bLockDesParent = FFAT_FALSE;		// parent of destination node is locked

	// for log
	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;		// cache flag
	FatAllocateFlag dwAllocFlag = FAT_ALLOCATE_NONE;
	t_int32			dwClusterCountDE = 0;				// cluster count for directory entry write
	t_uint32		pdwClustersDE[NODE_MAX_CLUSTER_FOR_CREATE];
														// clusters for directory entry write

	t_boolean		bAddonInvoked = FFAT_FALSE;
											// boolean to check ADDON module is invoked or not.

	Node*			pNodeNew;				// temporary node for renamed node info

	// check 사항이 많아서 따로 분리함.
	// 다른 volume인지에 대한체크도 여기서 수행된다.
	r = _renameCheckParameter(pNodeSrcParent, pNodeSrc, pNodeDesParent, pNodeDes, psName);
	FFAT_ER(r, (_T("invalid parameter")));

	FFAT_ASSERT(((dwFlag & FFAT_RENAME_TARGET_OPENED) == 0) ? (NODE_IS_OPEN(pNodeDes) == FFAT_FALSE) : FFAT_TRUE);
	FFAT_ASSERT((NODE_IS_OPEN(pNodeDes) == FFAT_TRUE) ? (dwFlag & FFAT_RENAME_TARGET_OPENED) : FFAT_TRUE);

	// lock sequence
	//	1. lock SRC 
	//	2. lock parent of Src
	//	3. lock Des -- if it is a valid node
	//	4. lock parent of Des
	pVol = NODE_VOL(pNodeSrcParent);

	if ((dwFlag & FFAT_RENAME_NO_LOCK) == 0)
	{
		r = NODE_GET_WRITE_LOCK(pNodeSrc);
		FFAT_ER(r, (_T("fail to get write lock for source node")));

		r = NODE_GET_WRITE_LOCK(pNodeSrcParent);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to get write lock for parent of source node")));
			goto out_src;
		}

		if (NODE_IS_VALID(pNodeDes) == FFAT_TRUE)
		{
			// valid 이지만 lock이 없는 경우는 lookup만 수행된 경우이다.
			if (ffat_node_isSameNode(pNodeSrc, pNodeDes) == FFAT_FALSE)
			{
				r = NODE_GET_WRITE_LOCK(pNodeDes);
				IF_UK (r < 0)
				{
					FFAT_LOG_PRINTF((_T("Fail to lock destination node")));
					goto out_src_parent;
				}
				bLockDes = FFAT_TRUE;
			}
			else
			{
				bLockDes = FFAT_FALSE;
			}
		}

		if (ffat_node_isSameNode(pNodeSrcParent, pNodeDesParent) == FFAT_FALSE)
		{
			r = NODE_GET_WRITE_LOCK(pNodeDesParent);
			if (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to lock parent destination node")));
				goto out_des;
			}
			bLockDesParent = FFAT_TRUE;
		}
		else
		{
			bLockDesParent = FFAT_FALSE;
		}

		r = VOL_GET_READ_LOCK(pVol);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to get vol read lock")));
			goto out_des_parent;
		}
	}
	// END OF LOCK

	VOL_INC_REFCOUNT(pVol);

	pNodeNew = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNodeNew != NULL);

	// lookup flag가 있을 경우 lookup을 수행한다.
	if (dwFlag & FFAT_RENAME_LOOKUP)
	{
		r = ffat_node_lookup(pNodeDesParent, pNodeDes, psName, 0,
						(FFAT_LOOKUP_NO_LOCK | FFAT_LOOKUP_FOR_RENAME), NULL, pCxt);
		if ((r != FFAT_OK) && (r != FFAT_ENOENT))
		{
			FFAT_LOG_PRINTF((_T("There is some error while lookup operation")));
			goto out;
		}
	}

	// source와 destination은 모두 directory 이거나 모두 file이어야 한다.
	if (NODE_IS_DIR(pNodeSrc) == FFAT_TRUE)
	{
		// source node is a directory
		if (NODE_IS_VALID(pNodeDes) == FFAT_TRUE)
		{
			if (NODE_IS_DIR(pNodeDes) != FFAT_TRUE)
			{
				FFAT_LOG_PRINTF((_T("destination is not a directory (source is a directory)")));
				r = FFAT_ENOTDIR;
				goto out;
			}
		}
	}
	else
	{
		// source node is a file
		if ((NODE_IS_VALID(pNodeDes) == FFAT_TRUE) && 
			(NODE_IS_FILE(pNodeDes) != FFAT_TRUE))
		{
			FFAT_LOG_PRINTF((_T("destination is not a file (source is a file)")));
			r = FFAT_EISDIR;
			goto out;
		}
	}

	// 1. 같은 디렉터리의 같은 파일로의 변경인지 확인
	if (ffat_node_isSameNode(pNodeSrc, pNodeDes) == FFAT_TRUE)
	{
		// allocate memory for source node name
		psSrcName = (t_wchar*) FFAT_LOCAL_ALLOC((FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
		FFAT_ASSERT(psSrcName != NULL);

		r = _getNodeName(pNodeSrc, psSrcName, &dwSrcLen, pCxt);
		FFAT_EO(r, (_T("fail to get node name")));

		FFAT_ASSERT(dwSrcLen == (t_int32)pNodeSrc->wNameLen);

		// 이름이 같을 경우 대소문자가 같은지 비교한다.
		// check case of name when both name are same one.
		if (dwSrcLen == pNodeDes->wNameLen)
		{
			if (FFAT_WCSCMP(psSrcName, psName) == 0)
			{
				// 음.. 같은 이름이자나... 바꿀 필요 없음.
				// it's totally same name, no need to rename.
				r = FFAT_OK;
				goto out_same_name;
			}
		}

		// 같으면 좋을텐데..  같지 않으므로 실제 operation 시작~~.
		// let's go for renaming.
		bSame = FFAT_TRUE;
	}
	else
	{
		bSame = FFAT_FALSE;

		if ((NODE_IS_VALID(pNodeDes) == FFAT_TRUE) &&
			(NODE_IS_DIR(pNodeDes) == FFAT_TRUE))
		{
			// 서로 다른 node일 경우, 대상이 DIRECTORY이면 비어 있어야 한다.
			r = ffat_dir_isEmpty(pNodeDes, pCxt);
			if (r != FFAT_TRUE)
			{
				if (r == FFAT_FALSE)
				{
					r = FFAT_ENOTEMPTY;
				}
				// else ==> there is some error
				goto out;
			}
		}
	}

	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_RDONLY(pNodeSrc) == FFAT_FALSE);
	FFAT_ASSERT((NODE_IS_VALID(pNodeDes) == FFAT_TRUE) ? (NODE_IS_DIRTY_SIZE_RDONLY(pNodeDes) == FFAT_FALSE) : FFAT_TRUE);

	if ((bSame == FFAT_TRUE) || (NODE_IS_VALID(pNodeDes) == FFAT_TRUE))
	{
		// destination node가 존재하거나 src와 같은 node일 경우
		// (same node) or (target node is an exist node) 
		FFAT_MEMCPY(pNodeNew, pNodeDes, sizeof(Node));

		// do not have free DE information so let's lookup
		FFAT_MEMSET(&pNodeNew->stDeInfo, 0x00, sizeof(NodeDeInfo));

		// lookup free directory entry for destination
		r = ffat_node_lookup(pNodeDesParent, pNodeNew, psName, 0,
						(FFAT_LOOKUP_NO_LOCK | FFAT_LOOKUP_FREE_DE | FFAT_LOOKUP_FOR_RENAME),
						NULL, pCxt);
		if (r != FFAT_ENOENT)
		{
			FFAT_LOG_PRINTF((_T("fail to lookup free directory entry")));
			goto out;
		}
	}
	else
	{
		// use the free space information of pNodeDes directly.
		// no need to lookup for free DE
		FFAT_MEMCPY(pNodeNew, pNodeDes, sizeof(Node));
		pNodeDes = NULL;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_EXPAND_PARENT_BEFORE);

	// lock CORE for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock CORE")));

	// get cluster information to write new directory entry
	r = ffat_node_createExpandParent(pNodeDesParent, pNodeNew, &dwClusterCountDE, 
					pdwClustersDE, NODE_MAX_CLUSTER_FOR_CREATE, pCxt);
	IF_LK (r < 0)
	{
		r |= ffat_core_unlock(pCxt);
		goto out;
	}

	// unlock CORE for free cluster sync
	r = ffat_core_unlock(pCxt);
	FFAT_EO(r, (_T("fail to lock CORE")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_EXPAND_PARENT_AFTER);

	// fill pNodeNewDes information 
	NODE_C(pNodeNew)	= NODE_C(pNodeSrc);
	NODE_S(pNodeNew)	= NODE_S(pNodeSrc);
	pNodeNew->dwLastCluster	= pNodeSrc->dwLastCluster;
	pNodeNew->pInode		= pNodeSrc->pInode;
	pNodeNew->wRefCount		= pNodeSrc->wRefCount;

	if (NODE_IS_OPEN(pNodeSrc) == FFAT_TRUE)
	{
		NODE_SET_OPEN(pNodeNew);
	}
	else
	{
		NODE_CLEAR_OPEN(pNodeNew);
	}

	// if pNodeSrc is dirty-size node, adjust the pNodeNew as dirty-size
	if (NODE_IS_DIRTY_SIZE(pNodeSrc) == FFAT_TRUE)
	{
		pNodeNew->stDE.bNTRes |= FAT_DE_DIRTY_SIZE;
		NODE_SET_DIRTY_SIZE(pNodeNew);

		// If pNodeSrc is dirty-size-rdonly, set dirty-size-rdonly state of pNodeNew.
		// After re-mount of read-only volume, the pNodeSrc can be dirty-size-rdonly
		// that is needed to recovery ahead of expand/write.
		IF_UK (NODE_IS_DIRTY_SIZE_RDONLY(pNodeSrc) == FFAT_TRUE)
		{
			NODE_SET_DIRTY_SIZE_RDONLY(pNodeNew);
		}
	}

	// copy previous access location
	FFAT_MEMCPY(&pNodeNew->stPAL, &pNodeSrc->stPAL, sizeof(NodePAL));

	// NAME, NTRes 부분을 제외한 나머지 부분을 Source node에서 복사해 온다.
	pNodeNew->stDE.bAttr = pNodeSrc->stDE.bAttr;

	FFAT_ASSERT(13 == (t_int32)&((FatDeSFN*)0)->bCrtTimeTenth);
	FFAT_MEMCPY(&pNodeNew->stDE.bCrtTimeTenth, &pNodeSrc->stDE.bCrtTimeTenth, 
					(FAT_DE_SIZE - (t_int32)&(((FatDeSFN*)0)->bCrtTimeTenth)));

	FFAT_MEMCPY(&pNodeNew->stAddon, &pNodeSrc->stAddon, sizeof(FFatAddonNode));

	if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// 실제 rename 이 수행되어야 할 부분이다.
	r = ffat_addon_rename(pNodeSrcParent, pNodeSrc, pNodeDesParent,
					pNodeDes, pNodeNew, psName, dwFlag, &dwCacheFlag, pCxt);
	if ((r == FFAT_DONE) || (r < 0))
	{
		if (r == FFAT_DONE)
		{
			bAddonInvoked = FFAT_TRUE;
			r = FFAT_OK;
		}

		goto out;
	}

	bAddonInvoked = FFAT_TRUE;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_DELETE_SRC_DE_BEFORE);

	FFAT_ASSERT(NODE_C(pNodeSrcParent) == NODE_COP(pNodeSrc));

	// delete DE of Source node, SRC Node의 directory entry를 삭제한다.
	r = ffat_deleteDEs(pVol, NODE_COP(pNodeSrc), pNodeSrc->stDeInfo.dwDeStartOffset,
						pNodeSrc->stDeInfo.dwDeStartCluster, pNodeSrc->stDeInfo.dwDeCount,
						FFAT_FALSE, (FFAT_CACHE_DATA_DE | dwCacheFlag), pNodeSrc, pCxt);
	FFAT_EO(r, (_T("fail to delete source node")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_DELETE_SRC_DE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_DELETE_DES_DE_BEFORE);

	if ((bSame == FFAT_FALSE) && (pNodeDes != NULL))
	{
		FFAT_ASSERT(NODE_C(pNodeDesParent) == NODE_COP(pNodeDes));

		// delete old destination node's directory entry
		// delete DE of Source node, SRC Node의 directory entry를 삭제한다.
		r = ffat_deleteDEs(pVol, NODE_COP(pNodeDes), pNodeDes->stDeInfo.dwDeStartOffset,
						pNodeDes->stDeInfo.dwDeStartCluster, pNodeDes->stDeInfo.dwDeCount,
						FFAT_FALSE, (FFAT_CACHE_DATA_DE | dwCacheFlag), pNodeDes, pCxt);
		FFAT_EO(r, (_T("fail to delete old destination node")));

		// deallocate clusters for destination
		if ((NODE_C(pNodeDes) != 0) & ((dwFlag & FFAT_RENAME_TARGET_OPENED) == 0))
		{
			r = ffat_misc_deallocateCluster(pNodeDes, 0, NODE_C(pNodeDes), 0, NULL,
							dwAllocFlag, dwCacheFlag, pCxt);
			FFAT_EO(r, (_T("fail to deallocate cluster for destination node")));
		}
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_DELETE_DES_DE_AFTER);

	r = _renameUpdateDEs(pNodeSrcParent, pNodeSrc, pNodeDesParent, pNodeNew,
					psName, pdwClustersDE, dwClusterCountDE, dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("Fail to update DEs for rename")));

out:
	IF_LK (bAddonInvoked == FFAT_TRUE)
	{
		r |= ffat_addon_afterRename(pNodeSrcParent, pNodeSrc, pNodeDesParent, pNodeDes,
						pNodeNew, psName, dwFlag, FFAT_IS_SUCCESS(r),
						pdwClustersDE, dwClusterCountDE, pCxt);
		if (r >= 0)
		{
			NODE_SET_VALID(pNodeNew);
		}
	}

	IF_LK (r == FFAT_OK)
	{
		if ((bSame == FFAT_FALSE) && (pNodeDes != NULL))
		{
			if (dwFlag & FFAT_RENAME_TARGET_OPENED)
			{
				NODE_SET_OPEN_UNLINK(pNodeDes);
			}

			NODE_SET_UNLINK(pNodeDes);
		}

		ffat_node_sync(pNodeSrc, NODE_S(pNodeSrc), (NODE_SYNC_NO_LOCK | NODE_SYNC_NO_WRITE_DE), pCxt);		// ignore error

		FFAT_MEMCPY(pNodeSrc, pNodeNew, sizeof(Node));
	}

out_same_name:
	VOL_DEC_REFCOUNT(pVol);

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNodeSrc) == FFAT_FALSE) ? (NODE_S(pNodeSrc) == FFATFS_GetDeSize(NODE_DE(pNodeSrc))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNodeSrc) == FFATFS_GetDeCluster(VOL_VI(pVol), NODE_DE(pNodeSrc)));

	FFAT_LOCAL_FREE(psSrcName, (FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar), pCxt);
	FFAT_LOCAL_FREE(pNodeNew, sizeof(Node), pCxt);

	if ((dwFlag & FFAT_RENAME_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNodeDesParent));
	}

out_des_parent:
	if ((dwFlag & FFAT_RENAME_NO_LOCK) == 0)
	{
		if (bLockDesParent == FFAT_TRUE)
		{
			r |= NODE_PUT_WRITE_LOCK(pNodeDesParent);
		}

out_des:
		if (bLockDes == FFAT_TRUE)
		{
			r |= NODE_PUT_WRITE_LOCK(pNodeDes);
		}

out_src_parent:
		r |= NODE_PUT_WRITE_LOCK(pNodeSrcParent);

out_src:
		r |= NODE_PUT_WRITE_LOCK(pNodeSrc);
	}

	return r;
}


/**
 * node를 삭제한다.
 * 만약 open되어 있는 file/directory 이라면 DE만 삭제하고 FAT은 deallocation하지 않는다.
 *
 * @param		pNodeParent	: [IN] parent node pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 * @param		dwNUFlag	: [IN] unlink flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: SUCCESS
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @history		DEC-04-2007 [InHwan Choi] open unlink
 * @version		NOV-12-2008 [DongYoung Seo] support open unlink for directory
 * @version		MAR-20-2009 [DongYoung Seo] add NODE_UNLINK_SYNC check routine
 */
FFatErr
ffat_node_unlink(Node* pNodeParent, Node* pNode, NodeUnlinkFlag dwNUFlag, ComCxt* pCxt)
{
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;		// flag for cache operation
	FatAllocateFlag		dwAllocFlag = FAT_ALLOCATE_NONE;	// allocate flag
	t_boolean			bAddonInvoked = FFAT_FALSE;			// boolean to check ADDON module is invoked or not.
	FFatErr				r;
	FFatVC				stVC;								// vectored cluster storage

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_UNLINK(pNode) == FFAT_FALSE);
	FFAT_ASSERT((NODE_IS_OPEN(pNode) == FFAT_TRUE) ? (dwNUFlag & NODE_UNLINK_OPEN) : FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_RDONLY(pNode) == FFAT_FALSE);

	FFAT_DEBUG_NODE_PRINTF((_T("Unlink Parent/Node/Node_C:0x%X/0x%X/0X%x\n"), pNodeParent, pNode, NODE_C(pNode)));

	stVC.pVCE = NULL;

	if ((dwNUFlag & NODE_UNLINK_NO_LOCK) == 0)
	{
		// lock child node
		r = NODE_GET_WRITE_LOCK(pNode);
		FFAT_ER(r, (_T("fail to get node write lock")));

		if (pNodeParent != NULL)
		{
			r = NODE_GET_WRITE_LOCK(pNodeParent);
			FFAT_EOTO(r, (_T("fail to get node write lock")), out_child);
		}

		r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
		FFAT_EOTO(r, (_T("fail to get vol read lock")), out_vol);
	}

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	// CHECK TIME STAMP
	// check time stamp between volume and parent node
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	if (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("read only volume !!")));
		r = FFAT_EROFS;
		goto out;
	}

	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		// check root
		if (NODE_IS_ROOT(pNode) == FFAT_TRUE)
		{
			FFAT_LOG_PRINTF((_T("This is root node")));
			r = FFAT_EACCESS;
			goto out;
		}

		r = ffat_dir_isEmpty(pNode, pCxt);
		FFAT_EO(r, (_T("fail to check directory empty state")));

		if (r == FFAT_FALSE)
		{
			FFAT_LOG_PRINTF((_T("the directory is not empty")));
			r = FFAT_ENOTEMPTY;
			goto out;
		}
	}

	// get memory for VCE
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE != NULL);

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

	stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
	VC_INIT(&stVC, VC_NO_OFFSET);

	if ((VOL_IS_SYNC_META(NODE_VOL(pNode)) == FFAT_TRUE) || (dwNUFlag & NODE_UNLINK_SYNC))
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	r = ffat_addon_unlink(pNodeParent, pNode, &stVC, dwNUFlag, &dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to unlink on ADDON module")));

	bAddonInvoked = FFAT_TRUE;

	if (r == FFAT_DONE)
	{
		r = FFAT_OK;
		goto out;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_UNLINK_DEALLOCATE_CLUSTER_BEFORE);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RMDIR_DEALLOCATE_CLUSTER_BEFORE);

	// do not use size of node to check is there assigned cluster 
	if ((NODE_C(pNode) != 0) && ((dwNUFlag & NODE_UNLINK_OPEN) == 0))
	{
		if (dwNUFlag & NODE_UNLINK_SECURE)
		{
			dwAllocFlag |= FAT_ALLOCATE_SECURE;
		}

		if (dwNUFlag & NODE_UNLINK_DISCARD_CACHE)
		{
			dwAllocFlag |= FAT_DEALLOCATE_DISCARD_CACHE;
		}

		r = ffat_misc_deallocateCluster(pNode, 0, NODE_C(pNode), 0,
						&stVC, dwAllocFlag, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to deallocate cluster")));
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_UNLINK_DEALLOCATE_CLUSTER_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RMDIR_DEALLOCATE_CLUSTER_AFTER);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_UNLINK_DELETE_DE_BEFORE);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RMDIR_DELETE_DE_BEFORE);

	r = _deleteDE(pNode, dwCacheFlag, pCxt);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_UNLINK_DELETE_DE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RMDIR_DELETE_DE_AFTER);

out:
	IF_LK (bAddonInvoked == FFAT_TRUE)
	{
		r |= ffat_addon_afterUnlink(pNodeParent, pNode, dwNUFlag, FFAT_IS_SUCCESS(r), pCxt);
	}

	IF_LK (r == FFAT_OK)
	{
		if (dwNUFlag & NODE_UNLINK_OPEN)
		{
			NODE_SET_OPEN_UNLINK(pNode);		// set open unlinked stat
		}

		NODE_SET_UNLINK(pNode);					// set unlinked state
	}

	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	if ((dwNUFlag & NODE_UNLINK_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
	}

out_vol:
	if ((pNodeParent != NULL) && ((dwNUFlag & NODE_UNLINK_NO_LOCK) == 0))
	{
		r |= NODE_PUT_WRITE_LOCK(pNodeParent);
	}

out_child:
	if ((dwNUFlag & NODE_UNLINK_NO_LOCK) == 0)
	{
		r |= NODE_PUT_WRITE_LOCK(pNode);
	}

	return r;
}


/**
 * open unlink 후 close될때 호출된다.
 * FAT을 deallocate한다.
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: SUCCESS
 * @return		else		: error
 * @author		InHwan Choi
 * @version		DEC-04-2007 [InHwan Choi] First Writing.
 * @version		NOV-12-2008 [DongYoung Seo] support open unlink for directory
 * @version		APR-29-2009 [JeongWoo Park] Add the code to call
 *											ffat_addon_unlinkOpenUnlinkedNode()
 *											before deallocation FAT chain.
 */
FFatErr
ffat_node_unlinkOpenUnlinked(Node* pNode, ComCxt* pCxt)
{
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;			// flag for cache operation
	FatAllocateFlag		dwAllocFlag = FAT_ALLOCATE_NONE;		// allocate flag
	FFatErr				r;
	FFatVC				stVC;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (NODE_IS_VALID(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("invalid node ")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE);

	stVC.pVCE = NULL;	// for free

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	// CHECK TIME STAMP
	// check time stamp between volume and parent node
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	if (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("read only volume !!")));
		r = FFAT_EROFS;
		goto out;
	}

	// open unlink log를 삭제한다.
	r = ffat_addon_unlinkOpenUnlinkedNode(pNode, pCxt);
	FFAT_EO(r, (_T("fail to delete node on ADDON module")));

	if (NODE_C(pNode) != 0)
	{
		// get memory for VCE
		stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
		FFAT_ASSERT(stVC.pVCE != NULL);

		stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
		VC_INIT(&stVC, VC_NO_OFFSET);

		// get vectored cluster from offset 0
		// 성능 향상을 위해 VC정보를 모아서 deallocate 함
		r = ffat_misc_getVectoredCluster(NODE_VOL(pNode), pNode, NODE_C(pNode),
							0, 0, &stVC, NULL, pCxt);
		FFAT_EO(r, (_T("fail to get vectored cluster")));

		// FAT을 deallocation한다.
		r = ffat_misc_deallocateCluster(pNode, 0, NODE_C(pNode), 0, &stVC, 
								dwAllocFlag, (dwCacheFlag | FFAT_CACHE_SYNC), pCxt);
		FFAT_EO(r, (_T("fail to deallocate cluster")));
	}

	// open unlink log를 삭제한다.
	r = ffat_addon_afterUnlinkOpenUnlinkedNode(pNode, pCxt);
	FFAT_EO(r, (_T("fail to (after) delete node on ADDON module")));

	pNode->dwFlag &= ~NODE_OPEN_UNLINK;

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	return r;
}


/**
 * fill pNode information from directory entry
 *
 * @param		pNode		: [IN/OUT] node information storage
 * @param		pNodeDE		: [IN] node information with directory entry.
 * @param		pAddonInfo	: [IN/OUT] buffer of ADDON node (may be NULL)
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-09-2006 [DongYoung Seo] First Writing
 * @version		JAN-06-2008 [DongYoung Seo] add updating code for offset of SFNE
 * @version		OCT-22-2009 [JW Park] Add the code to check dirty-size state
 */
void 
ffat_node_fillNodeInfo(Node* pNode, FatGetNodeDe* pNodeDE, void* pAddonInfo)
{
	FatDeSFN*	pDeSFN;

	FFAT_ASSERT(pNode);


	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pNodeDE->pDE);

	pDeSFN = &pNodeDE->pDE[pNodeDE->dwEntryCount - 1];

	// fill node offset
	pNode->dwFlag		|= pDeSFN->bAttr;
	pNode->dwCluster	= FFATFS_GetDeCluster(NODE_VI(pNode), pDeSFN);
	pNode->dwSize		= FFATFS_GetDeSize(pDeSFN);

	FFAT_MEMCPY(&pNode->stDE, pDeSFN, FAT_DE_SIZE);

	// If SFN has dirty bit, set dirty-size state of node.
	IF_UK (pDeSFN->bNTRes & FAT_DE_DIRTY_SIZE)
	{
		NODE_SET_DIRTY_SIZE(pNode);

		// If volume is read-only, set dirty-size-rdonly state of node.
		IF_UK (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
		{
			NODE_SET_DIRTY_SIZE_RDONLY(pNode);
		}
	}

	pNode->stDeInfo.dwDeStartCluster	= pNodeDE->dwDeStartCluster;
	pNode->stDeInfo.dwDeStartOffset		= pNodeDE->dwDeStartOffset;
	pNode->stDeInfo.dwDeEndCluster		= pNodeDE->dwDeEndCluster;
	pNode->stDeInfo.dwDeEndOffset		= pNodeDE->dwDeEndOffset;
	pNode->stDeInfo.dwDeCount			= pNodeDE->dwTotalEntryCount;
	pNode->stDeInfo.dwDeClusterSFNE		= pNodeDE->dwDeSfnCluster;
	pNode->stDeInfo.dwDeOffsetSFNE		= pNodeDE->dwDeSfnOffset;

	ffat_addon_fillNodeInfo(pNode, pNodeDE, pAddonInfo);

	FFAT_ASSERT(ffat_share_checkNodeDeInfo(pNode) == FFAT_OK);

	return;
}


/**
 * check the two node is a same node.
 *
 *
 * @param		pNode1	: [IN] node pointer
 * @param		pNode2	: [IN] node name storage
 * @author		DongYoung Seo
 * @version		AUG-30-2006 [DongYoung Seo] First Writing.
 */
t_boolean FFAT_FASTCALL
ffat_node_isSameNode(Node* pNode1, Node* pNode2)
{

	FFAT_ASSERT(pNode1);
	FFAT_ASSERT(pNode2);

	if (pNode1 == pNode2)
	{
		return FFAT_TRUE;
	}

	if ((pNode1->stDeInfo.dwDeStartOffset != pNode2->stDeInfo.dwDeStartOffset) ||		// FOR DE
		(pNode1->stDeInfo.dwDeStartCluster != pNode2->stDeInfo.dwDeStartCluster) ||		// FOR DE
		(NODE_C(pNode1) != NODE_C(pNode2)) ||											// for cluster
		(NODE_COP(pNode1) != NODE_COP(pNode2)) ||										// for parent
		(NODE_TS(pNode1) != NODE_TS(pNode2)))											// for volume
	{
		return FFAT_FALSE;
	}
	
	return FFAT_TRUE;
}


/**
 * create symlink node & write symlink info
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNodeChild	: [IN/OUT] child node pointer
 * @param		psName		: [IN] node name
 * @param		psPath		: [IN] target path
 * @param		dwFlag		: [IN] node creation flag
 * @param		pAddonInfo	: [IN] info of ADDON node (may be NULL)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-27-2007 [DongYoung Seo] modify for Multi-thread.
 *											Move from FFatMain
 * @version		JAN-30-2009 [JeongWoo Park] Add assert for checking the parent.
 */
FFatErr
ffat_node_createSymlink(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, t_wchar* psPath,
						FFatCreateFlag dwFlag, void* pAddonInfo, ComCxt* pCxt)
{
	FFatErr			r;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT(pAddonInfo);
	FFAT_ASSERT(pCxt);

	FFAT_ASSERT(NODE_VOL(pNodeParent));
	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_VALID(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_UNLINK(pNodeParent) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeParent) == FFAT_FALSE);

	// lookup success한 node를 다시 create할 수 없음
	// 존재한 file을 다시 creat하는 경우는 nestle에서 truncate로 처리
	FFAT_ASSERT(NODE_IS_VALID(pNodeChild) == FFAT_FALSE);

	if ((dwFlag & FFAT_CREATE_NO_LOCK) == 0)
	{
		// lock parent node
		r = NODE_GET_WRITE_LOCK(pNodeParent);
		FFAT_ER(r, (_T("fail to get parent write lock")));

		r = VOL_GET_READ_LOCK(NODE_VOL(pNodeParent));
		FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);
	}

	VOL_INC_REFCOUNT(NODE_VOL(pNodeParent));

	// lock CORE for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock CORE")));

	r = ffat_addon_createSymlink(pNodeParent, pNodeChild, psName, psPath,
						(dwFlag & (~FFAT_CREATE_ATTR_DIR)), pAddonInfo, pCxt);

	VOL_DEC_REFCOUNT(NODE_VOL(pNodeParent));

	// unlock CORE for free cluster sync
	r |= ffat_core_unlock(pCxt);

out:
	if ((dwFlag & FFAT_CREATE_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNodeParent));
out_vol:
		r |= NODE_PUT_WRITE_LOCK(pNodeParent);
	}

	return r;
}


/**
 * read symlink info & get target path
 *
 * @param		pNode		: [IN] node pointer
 * @param		psPath		: [OUT] target path
 * @param		dwLen		: [IN] length of psPath, in character count
 * @param		pdwLen		: [OUT] count of character stored at psPath
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-27-2007 [DongYoung Seo] modify for Multi-thread.
 *											Move from FFatMain
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 */
FFatErr
ffat_node_readSymlink(Node* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pdwLen);

	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	if (NODE_IS_FILE(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Node is directory")));
		return FFAT_EISDIR;
	}

	r = NODE_GET_READ_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get node read lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = ffat_addon_readSymlink(pNode, psPath, dwLen, pdwLen, pCxt);

	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_READ_LOCK(pNode);

	return r;
}


/**
 * check node is symlink
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-27-2007 [DongYoung Seo] Move from FFatMain
 */
FFatErr
ffat_node_isSymlink(Node* pNode)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	if (NODE_IS_FILE(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Node is directory")));
		return FFAT_EISDIR;
	}

	return ffat_addon_isSymlink(pNode);
}


/**
 * 1. get clusters for directory entry write 
 * 2. expand directory size when there is not enough space
 * 3. update directory entry information for write
 *
 * @param	pNodeParent		: [IN] parent node pointer
 * @param	pNodeChild		: [IN] child node pointer
 * @param	pdwClusterCount	: [OUT] cluster count in pdwClustersDE
 * @param	pdwClustersDE	: [OUT] cluster for directory entry write
 * @param	dwCount			: [IN] storage count of pdwClustersDE,
 *								should be NODE_MAX_CLUSTER_FOR_CREATE
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_EDIRFULL	: no more free space on the directory
 * @return	else			: error
 * @author	DongYoung Seo
 * @version	13-SEP-2006 [DongYoung Seo] First Writing
 * @version	02-DEC-2008 [DongYoung Seo] bug fix, change cluster count calculation routine.
 *										there is an error on calculating end offset
 * @version	28-FEB-2009 [JeongWoo Park] change directory expanding routine.
 */
FFatErr
ffat_node_createExpandParent(Node* pNodeParent, Node* pNodeChild, t_int32* pdwClusterCount,
						t_uint32* pdwClustersDE, t_uint32 dwCount, ComCxt* pCxt)
{
	t_uint32	dwClusterCountDE;			// cluster count at pdwClustersDE
	t_uint32	i;
	FFatErr		r = FFAT_OK;
	Vol*		pVol;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(pdwClusterCount);
	FFAT_ASSERT(pdwClustersDE);
	FFAT_ASSERT(dwCount == NODE_MAX_CLUSTER_FOR_CREATE);

	pVol = NODE_VOL(pNodeParent);

	if (pNodeChild->stDeInfo.dwFreeCount == 0)
	{
		// FAT12/16의 root일 경우는.... 확장 불가임...
		if ((NODE_IS_ROOT(pNodeParent) == FFAT_TRUE) &&
			(VOL_IS_FAT16(pVol) == FFAT_TRUE))
		{
			FFAT_PRINT_DEBUG((_T("there is not enough free space on directory")));
			r = FFAT_EDIRFULL;
			goto out;
		}

		// we do not have enough free DE
		// expand parent directory for free directory entry
		FFAT_ASSERT(pNodeChild->stDeInfo.dwLastDeOffset >= 0);
		pNodeChild->stDeInfo.dwDeStartOffset = pNodeChild->stDeInfo.dwLastDeOffset + FAT_DE_SIZE;

		pNodeChild->stDeInfo.dwDeEndOffset = pNodeChild->stDeInfo.dwDeStartOffset +
							((pNodeChild->stDeInfo.dwDeCount - 1) << FAT_DE_SIZE_BITS);

		// check maximum directory size (2MB)
		if (pNodeChild->stDeInfo.dwDeEndOffset > ((FAT_DE_MAX - 1) * FAT_DE_SIZE))
		{
			FFAT_PRINT_DEBUG((_T("Too big directory size")));
			r = FFAT_EDIRFULL;
			goto out;
		}

		FFAT_ASSERT(pNodeChild->stDeInfo.dwDeEndOffset >= pNodeChild->stDeInfo.dwDeStartOffset);

		// cluster count to write(existed cluster + new allocated cluster)
		dwClusterCountDE = (pNodeChild->stDeInfo.dwDeEndOffset >> VOL_CSB(pVol))
							- (pNodeChild->stDeInfo.dwDeStartOffset >> VOL_CSB(pVol))
							+ 1;

		FFAT_ASSERT(dwClusterCountDE <= NODE_MAX_CLUSTER_FOR_CREATE);
		FFAT_ASSERT(dwClusterCountDE > 0);
		FFAT_ASSERT(pNodeChild->stDeInfo.dwLastDeCluster >= 2);

		// expand dir
		if (((pNodeChild->stDeInfo.dwDeStartOffset & VOL_CSM(pVol)) == 0) &&
			(pNodeChild->stDeInfo.dwDeStartOffset > 0))
		{
			// only need the new clusters
			r = ffat_dir_expand(pNodeParent, dwClusterCountDE,
								pNodeChild->stDeInfo.dwLastDeCluster, &pdwClustersDE[0], pCxt);
		}
		else
		{
			// need both the last cluster and the new clusters
			pdwClustersDE[0] = pNodeChild->stDeInfo.dwLastDeCluster;
			r = ffat_dir_expand(pNodeParent, (dwClusterCountDE - 1),
								pNodeChild->stDeInfo.dwLastDeCluster, &pdwClustersDE[1], pCxt);
		}
		FFAT_EO(r, (_T("fail to expand parent")));

		pNodeChild->stDeInfo.dwDeStartCluster = pdwClustersDE[0];

		FFAT_ASSERT(pNodeChild->stDeInfo.dwDeStartOffset == (t_uint32)(pNodeChild->stDeInfo.dwLastDeOffset + FAT_DE_SIZE));
	}
	else
	{
		pNodeChild->stDeInfo.dwDeStartOffset	= pNodeChild->stDeInfo.dwFreeOffset;
		pNodeChild->stDeInfo.dwDeStartCluster	= pNodeChild->stDeInfo.dwFreeCluster;

		pNodeChild->stDeInfo.dwDeEndOffset	= pNodeChild->stDeInfo.dwDeStartOffset
									+ ((pNodeChild->stDeInfo.dwDeCount - 1) << FAT_DE_SIZE_BITS);

		pdwClustersDE[0] = pNodeChild->stDeInfo.dwDeStartCluster;

		FFAT_ASSERT(pNodeChild->stDeInfo.dwDeEndOffset >= pNodeChild->stDeInfo.dwDeStartOffset);

		dwClusterCountDE = (pNodeChild->stDeInfo.dwDeEndOffset >> VOL_CSB(pVol))
							- (pNodeChild->stDeInfo.dwDeStartOffset >> VOL_CSB(pVol))
							+ 1;

		FFAT_ASSERT(dwClusterCountDE <= 3);
		FFAT_ASSERT(dwClusterCountDE > 0);

		// assert 경우, NODE_MAX_CLUSTER_FOR_CREATE 조정
		FFAT_ASSERT(dwClusterCountDE <= dwCount);
		
		if (dwClusterCountDE > 1)
		{
			if (FFATFS_FAT16_ROOT_CLUSTER == pdwClustersDE[0])
			{
				FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

				pdwClustersDE[1] = pdwClustersDE[2] = FFATFS_FAT16_ROOT_CLUSTER;
			}
			else
			{
				// write 할 cluster의 정보를 수집한다.
				for (i = 0; i < (dwClusterCountDE - 1); i++)
				{
					r = FFATFS_GetNextCluster(NODE_VI(pNodeParent), pdwClustersDE[i],
									&pdwClustersDE[i + 1], pCxt);
					FFAT_EO(r, (_T("fail to get next cluster")));

					FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(NODE_VI(pNodeParent), pdwClustersDE[i + 1]) == FFAT_TRUE);
				}
			}
		}
	}

	*pdwClusterCount = dwClusterCountDE;

	FFAT_ASSERT(*pdwClusterCount > 0);

out:
	return r;
}


//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
/**
 * pVC를 이용하여 data read/write/init를 수행한다.
 *
 * 이 함수를 사용하기 전에 반드시 cluster를 할당해 두어야 한다.
 * 단, read/write/init size가 할당된 cluster의 크기보다 큰 경우 partial read/write가 된다. (cluster 단위)
 *
 * cluster 에 대한 write 만을 수행한다.
 * FAT16의 root directory에 대한 write는 _writeOnFAT16RootDir() 을 이용하라.
 *
 * init은 write 시작 offset이 file size보다 큰 경우 처리하기 위함이다. (0x00로 초기화)
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] read/write/init start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] read/write/init size
 * @param		pVC			: [IN] cluster information for write
 * @param		pdwSizeDone	: [OUT] read/write/init size is done(may be NULL)
 * @param		dwFlag		: [IN] cache flag
 * @param		bRWFalg		: [IN] read/write/init flag
 * @param		pCxt		: [IN] context of current operation
 * @return		0 or above	: read/write/init size in byte
 * @return		negative	: error number
 * @author		DongYoung Seo
 * @version		AUG-18-2006 [DongYoung Seo] First Writing
 * @version		OCT-22-2008 [GwangOk Go] if not enough allocated cluster, write to end of chain
 * @version		OCT-24-2008 [GwangOk Go] add init operation. remove _initNode() at ffat_file.c
 * @version		OCT-28-2008 [DongYoung Seo] add no space checking routine
 *									in case of write offset is over file size and there is not enough free cluter for write start offset
 *									_readWriteHeadTail() will return 0, 
 *									i add checking routine and return FFAT_ENOSPC
 *									(And also modifi _write() for conrresponding deallocation)
 * @version		OCT-28-2008 [DongYoung Seo] add checking code for cluster validity of write offset
 * @version		MAY-27-2009 [JeongWoo Park] Change the paramter to support 4GB (dwSize -> t_uint32)
 *											separate the return value and IO successed size.
 */
FFatErr
ffat_node_readWriteInit(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_uint32 dwSize, 
						FFatVC* pVC, t_uint32* pdwSizeDone, FFatCacheFlag dwFlag,
						FFatRWFlag bRWFalg, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;
	Vol*		pVol;
	t_uint32	dwRequestSize;
	t_uint32	dwSizeDone;

	FFAT_ASSERT(pVC);

	IF_UK (dwSize == 0)
	{
		if (pdwSizeDone != NULL)
		{
			*pdwSizeDone = 0;
		}

		return FFAT_OK;
	}

	pVol = NODE_VOL(pNode);
	dwRequestSize = dwSize;

	// there is three step for read/write/init operation.
	// head, body, tail
	// 위 head/body/tail은 cluster 단위로 처리한다.

 	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
	// 예외 : _readWriteWhole( read 단위가 작고 align이 안되어 있는 경우는 전체를 read, 
	 // step1 : _readWriteHead or _readWriteWhole
	if (dwOffset & VOL_CSM(pVol))
	{
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		if(((dwFlag & FFAT_CACHE_DIRECT_IO) && (bRWFalg == FFAT_RW_WRITE)))
		{
			//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
			r = _readWriteWhole(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, FFAT_FALSE, dwFlag, pCxt);				
			FFAT_ER(r, (_T("fail to read/write head")));
			if (dwSizeDone == 0)
			{
				r = _readWriteHeadTail(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, bRWFalg, dwFlag, pCxt);
				FFAT_ER(r, (_T("fail to read/write head")));
			}
		}
		else if ((bRWFalg == FFAT_RW_READ) && (dwSize <= (32*1024)))
		{
			//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
			r = _readWriteWhole(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, FFAT_TRUE, dwFlag, pCxt);				
			FFAT_ER(r, (_T("fail to read/write head")));				
			if (dwSizeDone == 0)
			{
				r = _readWriteHeadTail(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, bRWFalg, dwFlag, pCxt);
				FFAT_ER(r, (_T("fail to read/write head")));
			}
		}
		else
		{
			// offset이 cluster align이 되지 않은 경우 head가 있음
			// init (NO cache_direct_io) / none (NO cache_direct_io) / write (NO cache_direct_io)  
			r = _readWriteHeadTail(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, bRWFalg, dwFlag, pCxt);
			FFAT_ER(r, (_T("fail to read/write head")));
		}

		if (dwSizeDone == 0)
		{
			// there is no more allocated cluster
			// return FFAT_ENOSPC to notify there is no free cluster
			return FFAT_ENOSPC;
		}

		dwSize -= dwSizeDone;
		if (dwSize == 0)
		{
			// all done
			goto out;
		}
		dwOffset += dwSizeDone;
		pBuff += dwSizeDone;
	}

	FFAT_ASSERT((dwOffset & VOL_CSM(pVol)) == 0);

	// step2 : _readWriteBody
	if (dwSize >= (t_uint32)VOL_CS(pVol))
	{
		t_uint32	dwAlignedSize = dwSize & (~VOL_CSM(pVol));

		// size가 cluster size보다 큰 경우 body가 있음

		r = _readWriteBody(pNode, dwOffset, pBuff, dwAlignedSize, pVC, &dwSizeDone, bRWFalg, dwFlag, pCxt);
		FFAT_ER(r, (_T("fail to read/write body")));

		FFAT_ASSERT((dwSizeDone & VOL_CSM(pVol)) == 0);

		dwSize -= dwSizeDone;

		if ((dwSize == 0) || (dwSizeDone < dwAlignedSize))
		{
			// all done or cluster가 부족한 경우
			goto out;
		}

		dwOffset += dwSizeDone;
		pBuff += dwSizeDone;
	}

	FFAT_ASSERT((dwOffset & VOL_CSM(pVol)) == 0);

	// step3 : _readWriteTail
	r = _readWriteHeadTail(pNode, dwOffset, pBuff, dwSize, pVC, &dwSizeDone, bRWFalg, dwFlag, pCxt);
	FFAT_ER(r, (_T("fail to read/write tail")));

	if ((dwSizeDone == 0) && (dwRequestSize == dwSize))
	{
		// nothing is written
		// no free space
		return FFAT_ENOSPC;
	}

	FFAT_ASSERT((dwSize == dwSizeDone) || (dwSizeDone == 0));
	dwSize -= dwSizeDone;

out:
	// return with size is done.
	if (pdwSizeDone != NULL)
	{
		*pdwSizeDone = (dwRequestSize - dwSize);
	}

	return FFAT_OK;
}


/**
 * write new directory entries
 *
 * write에 필요한 cluster는 미리 할당되어 있어야 한다.
 * write에 필요한 cluster는 모두 pdwClustersDE에 저장되어 있다.
 *
 * @param		pNodeParent		: [IN] node pointer
 * @param		pNodeChild		: [IN] child node pointer
 * @param		pDE				: [IN] directory entry storage
 * @param		dwEntryCount	: [IN] directory entry count
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-18-2006 [DongYoung Seo] First Writing
 * @version		JAN-05-2008 [DongYoung Seo] remove DE info at pNodechild updating routine
 *									The purpose of this function is write DE only
 * @version		JAN-06-2009 [DongYOung Seo] remove XDE relative code
 */
FFatErr
ffat_node_writeDEs(Node* pNodeParent, Node* pNodeChild, FatDeSFN* pDE, t_int32 dwEntryCount,
					t_uint32* pdwClustersDE, t_int32 dwClusterCountDE,
					FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	FFatVC			stVC;
	FFatVCE			stEntry[4];
	t_int32			i;				// index of stVC
	t_int32			j;				// index of pdwClustersDE

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(dwEntryCount >= 0);
	FFAT_ASSERT(dwEntryCount <= (VOL_MSD(NODE_VOL(pNodeParent)) / FAT_DE_SIZE));
	FFAT_ASSERT(pdwClustersDE);
	FFAT_ASSERT(dwClusterCountDE <= 4);
	FFAT_ASSERT(dwClusterCountDE > 0);
	FFAT_ASSERT(NODE_C(pNodeParent) == NODE_COP(pNodeChild));
	FFAT_ASSERT(dwCacheFlag & FFAT_CACHE_DATA_DE);

	FFAT_DEBUG_NODE_PRINTF((_T("Write DEs, Node/Cluster/Offset/Count:0x%X/0x%X/0x%X/%d\n"), 
			(t_uint32)pNodeChild, NODE_COP(pNodeChild), pNodeChild->stDeInfo.dwDeStartOffset, pNodeChild->stDeInfo.dwDeCount));

	stVC.dwTotalEntryCount		= 4;
	stVC.dwClusterOffset		= pNodeChild->stDeInfo.dwDeStartOffset & (~VOL_CSM(NODE_VOL(pNodeParent)));
	stVC.dwTotalClusterCount	= dwClusterCountDE;
	stVC.pVCE					= &stEntry[0];

	stVC.pVCE[0].dwCluster		= pdwClustersDE[0];
	stVC.pVCE[0].dwCount		= 1;
	stVC.dwValidEntryCount		= 1;
	i = 0;
	j = 1;

	for (/*None*/; j < dwClusterCountDE; j++)
	{
		if ((stVC.pVCE[i].dwCluster + 1) == pdwClustersDE[j])
		{
			stVC.pVCE[i].dwCount++;
		}
		else
		{
			i++;
			stVC.pVCE[i].dwCluster	= pdwClustersDE[j];
			stVC.pVCE[i].dwCount	= 1;
			stVC.dwValidEntryCount++;
		}
	}

	// file pointer
	r = ffat_dir_write(pNodeParent, (t_uint32)pNodeChild->stDeInfo.dwDeStartOffset,
					(t_int8*)pDE, (dwEntryCount << FAT_DE_SIZE_BITS), &stVC,
					dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("fail to write directory")));
	FFAT_ASSERT(r == (dwEntryCount << FAT_DE_SIZE_BITS));

	return FFAT_OK;
}


/**
* reset node structure
*	This function is used for optimal initialization.
*	do not need to initialize all of the node structure to 0x00
*
* @param		pNode			: pointer of node
* @author		DongYount Seo
* @version		SEP-10-2008 [DongYoung Seo] First Writing.
*/
void
ffat_node_resetNodeStruct(Node* pNode)
{
	FFAT_ASSERT(pNode);

	((Node*)pNode)->dwFlag = NODE_FLAG_NONE;		// No information

	return;
}


/**
 * initializes previous access location information for a node
 *
 * @param		pNode			: pointer of node
 * @author		DongYount Seo
 * @version		NOV-24-2008 [DongYoung Seo] First Writing.
 * @version		DEC-12-2008 [GwangOk Go] change LastLocation into PAL
 */
void
ffat_node_initPAL(Node* pNode)
{
	FFAT_ASSERT(pNode);

	pNode->stPAL.dwOffset	= FFAT_NO_OFFSET;
	pNode->stPAL.dwCluster	= 0;
	pNode->stPAL.dwCount	= 0;
}


/**
 * set previous access location information for a node
 *
 * @param		pNode			: [IN] pointer of node
 * @param		pPAL			: [IN] previous access location
 * @author		DongYount Seo
 * @version		NOV-24-2008 [DongYoung Seo] First Writing.
 * @version		DEC-12-2008 [GwangOk Go] change LastLocation into PAL
 * @version		JUN-09-2009 [GwangOk Go] consider dwNewLastOffset do not overflow
 */
void
ffat_node_setPAL(Node* pNode, NodePAL* pPAL)
{
	t_uint32		dwOldLastOffset;	// last offset of old PAL
	t_uint32		dwNewLastOffset;	// last offset of new PAL
	Vol*			pVol;
	FFatErr			r;

	FFAT_ASSERT(pNode);

	if (pPAL->dwOffset == FFAT_NO_OFFSET)
	{
		// no info
		return;
	}

	pVol = NODE_VOL(pNode);

	FFAT_ASSERT((pPAL->dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pPAL->dwCluster) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((pPAL->dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), (pPAL->dwCluster + pPAL->dwCount - 1)) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((pPAL->dwCluster != 0) ? (pPAL->dwOffset != FFAT_NO_OFFSET) : FFAT_TRUE);

	r = ffat_lock_getAtomic();		// don't care error, this is very short term

	if (pNode->stPAL.dwOffset == FFAT_NO_OFFSET)
	{
		// no old info
		goto out_new;
	}

	FFAT_ASSERT((pPAL->dwOffset & VOL_CSM(pVol)) == 0);
	FFAT_ASSERT(pPAL->dwOffset <= (0xFFFFFFFF - VOL_CS(pVol) + 1));
	FFAT_ASSERT((pNode->stPAL.dwOffset & VOL_CSM(pVol)) == 0);

	FFAT_ASSERT(pNode->stPAL.dwCount > 0);
	FFAT_ASSERT(pPAL->dwCount > 0);

	dwOldLastOffset = pNode->stPAL.dwOffset + (pNode->stPAL.dwCount << VOL_CSB(pVol)) - 1;
	dwNewLastOffset = pPAL->dwOffset + (pPAL->dwCount << VOL_CSB(pVol)) - 1;

	FFAT_ASSERT((t_int32)(dwOldLastOffset & VOL_CSM(pVol)) == VOL_CSM(pVol));
	FFAT_ASSERT((t_int32)(dwNewLastOffset & VOL_CSM(pVol)) == VOL_CSM(pVol));
	FFAT_ASSERT(dwNewLastOffset >= (t_uint32)VOL_CSM(pVol));
	FFAT_ASSERT(dwNewLastOffset > pPAL->dwOffset);

	if (((pPAL->dwOffset <= pNode->stPAL.dwOffset) && (dwNewLastOffset >= dwOldLastOffset)) ||	// old is included in new
		((pPAL->dwOffset - 1) > dwOldLastOffset) ||			// new is greater than old & not adjacent
		(dwNewLastOffset < (pNode->stPAL.dwOffset - 1)))		// new is less than old & not adjacent
	{
		goto out_new;
	}
	else if ((pPAL->dwOffset >= pNode->stPAL.dwOffset) && dwNewLastOffset <= dwOldLastOffset)
	{
		// new is included in old
		goto out;
	}
	else if (((pPAL->dwOffset >= pNode->stPAL.dwOffset) && ((pPAL->dwOffset - 1) < dwOldLastOffset)) ||	// overlapped
		(((pPAL->dwOffset - 1) == dwOldLastOffset) && (pPAL->dwCluster == (pNode->stPAL.dwCluster + pNode->stPAL.dwCount))))	// new is greater than old & adjacent
	{
		// expand backward (->)
		pNode->stPAL.dwCount	= (dwNewLastOffset - pNode->stPAL.dwOffset + 1) >> VOL_CSB(pVol);

		goto out;
	}
	else if (((dwNewLastOffset > (pNode->stPAL.dwOffset - 1)) && (dwNewLastOffset <= dwOldLastOffset)) ||	// overlapped
		((dwNewLastOffset == (pNode->stPAL.dwOffset - 1)) && ((pPAL->dwCluster + pPAL->dwCount) == pNode->stPAL.dwCluster)))	// new is less than old & adjacent
	{
		// expand forward (<-)
		pNode->stPAL.dwOffset	= pPAL->dwOffset;
		pNode->stPAL.dwCluster	= pPAL->dwCluster;
		pNode->stPAL.dwCount	= (dwOldLastOffset - pPAL->dwOffset + 1) >> VOL_CSB(pVol);

		goto out;
	}

out_new:
	// do not use memcpy. performance get worse in Linux. in other OS?
	pNode->stPAL.dwOffset	= pPAL->dwOffset;
	pNode->stPAL.dwCluster	= pPAL->dwCluster;
	pNode->stPAL.dwCount	= pPAL->dwCount;

out:
	if (r == FFAT_OK)
	{
		ffat_lock_putAtomic();		// don't care error, this is very short term
	}

	return;
}


/**
 * get previous access location information for a node
 *
 * @param		pNode			: [IN] pointer of node
 * @param		pPAL			: [OUT] previous access location
 * @author		DongYount Seo
 * @version		NOV-24-2008 [DongYoung Seo] First Writing.
 * @version		DEC-12-2008 [GwangOk Go] change LastLocation into PAL
 */
void
ffat_node_getPAL(Node* pNode, NodePAL* pPAL)
{
	FFatErr			r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pPAL);

	r = ffat_lock_getAtomic();		// don't care error, this is very short term

	// do not use memcpy. performance get worse in Linux. in other OS?
	pPAL->dwOffset	= pNode->stPAL.dwOffset;
	pPAL->dwCluster	= pNode->stPAL.dwCluster;
	pPAL->dwCount	= pNode->stPAL.dwCount;

	FFAT_ASSERT((pPAL->dwOffset != FFAT_NO_OFFSET) ? ((pPAL->dwOffset & VOL_CSM(NODE_VOL(pNode))) == 0) : FFAT_TRUE);
	FFAT_ASSERT((pPAL->dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pPAL->dwCluster) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((pPAL->dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), (pPAL->dwCluster + pPAL->dwCount - 1)) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((pPAL->dwCluster != 0) ? (pPAL->dwOffset != FFAT_NO_OFFSET) : FFAT_TRUE);

	if (r == FFAT_OK)
	{
		ffat_lock_putAtomic();		// don't care error, this is very short term
	}

	return;
}

FFatErr
ffat_node_GetGUIDFromNode(Node* pNode, void* pstXDEInfo)
{
	return ffat_addon_GetGUIDFromNode(pNode, pstXDEInfo);
}


/**
 * check node access permission
 * must not be directory
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwFlag			: [IN] node access flag
 * @author		DongYount Seo
 * @version		JAN-21-2009 [GwangOk Go] First Writing.
 */
FFatErr
ffat_node_isAccessible(Node* pNode, NodeAccessFlag dwFlag)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_FALSE);

	return ffat_addon_isAccessable(pNode, dwFlag);
}


//=============================================================================
//
//	static functions
//


/**
 * pVC를 이용하여 data read/write/init를 수행한다.
 *
 * cluster의 일부를 write 한다. 
 * 만약 dwOffset이 cluster align에 맞다면 아무런 역할을 수행하지 않는다.
 *
 * cluster가 할당되어 있지 않을 수 있음 -> return 0
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] read/write/init start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] read/write/init size
 *								dwOffset + dwSize가 cluster의 크기를 넘을 경우
 *								넘는 부분에 대해서는 write를 수행하지 않는다.
 * @param		pVC			: [IN] Fat Vectored Cluster
 * @param		pdwSizeDone	: [OUT] IO size is done
 * @param		bRWFalg		: [IN] read/write/init flag
 * @param		dwFlag		: cache flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: no error
 * @return		negative	: error number
 * @author		DongYoung Seo
 * @version		AUG-24-2006 [DongYoung Seo] First Writing
 * @version		OCT-22-2008 [GwangOk Go] if not enough allocated cluster, write nothing
 * @version		OCT-24-2008 [GwangOk Go] add init operation
 * @version		May-27-2009 [JeongWoo Park] Change the dwSize to t_uint32
 *											Add the code to support 4GB
 */
static FFatErr
_readWriteHeadTail(Node* pNode, t_uint32 dwOffset, t_int8* pBuff,
					t_uint32 dwSize, FFatVC* pVC, t_uint32* pdwSizeDone,
					FFatRWFlag bRWFalg, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	Vol*			pVol;
	t_uint32		dwCluster;
	t_int32			dwIndex;
	FFatErr			r;
	t_uint32		dwStartOffset; // start 0ffset in cluster

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwSizeDone);
	FFAT_ASSERT(((bRWFalg == FFAT_RW_READ) || (bRWFalg == FFAT_RW_WRITE)) ? (pBuff != NULL) : FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// get cluster of dwOffset
	r = ffat_com_lookupOffsetInVC(dwOffset, pVC, &dwIndex, &dwCluster, VOL_CSB(pVol));
	if (r == FFAT_ENOENT)
	{
		// there is no cluster information of the offset
		r = ffat_misc_getClusterOfOffset(pNode, dwOffset, &dwCluster, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get last cluster")));

		if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_FALSE)
		{
			FFAT_ASSERT(NODE_C(pNode) == 0);
			FFAT_ASSERT(VC_O(pVC) == 0);

			// get cluster from FFATFS
			r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), VC_FC(pVC), dwOffset, &dwCluster, pCxt);
			FFAT_ER(r, (_T("fail to get cluster of offset ")));
		}
	}

	FFAT_ASSERT(r >= 0);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

	// 이제 실제 operation 시작~~
	dwStartOffset = dwOffset & VOL_CSM(pVol);

	FFAT_ASSERT((dwStartOffset + dwSize) >= dwStartOffset); // can not happened overflow at here.

	// adjust dwSize
	if ((dwStartOffset + dwSize) > (t_uint32)VOL_CS(pVol))
	{
		dwSize = VOL_CS(pVol) - dwStartOffset;
	}

	switch (bRWFalg)
	{
		case FFAT_RW_READ:
			r = ffat_readWritePartialCluster(pVol, pNode, dwCluster, (t_int32)dwStartOffset, (t_int32)dwSize,
											pBuff, FFAT_TRUE, dwFlag, pCxt);
			break;
		case FFAT_RW_WRITE:
			r = ffat_readWritePartialCluster(pVol, pNode, dwCluster, (t_int32)dwStartOffset, (t_int32)dwSize,
											pBuff, FFAT_FALSE, dwFlag, pCxt);
			break;
		case FFAT_RW_INIT:
			r = ffat_initPartialCluster(pVol, pNode, dwCluster, (t_int32)dwStartOffset, (t_int32)dwSize, dwFlag, pCxt);
			if (r == FFAT_OK)
			{
				r = (t_int32)dwSize;
			}
			break;
		default:
			FFAT_ASSERT(0);
		break;
	}
	
	FFAT_ASSERT(dwSize == (t_uint32)r);

	IF_LK (r >= 0)
	{
		*pdwSizeDone = (t_uint32)r;
		r = FFAT_OK;
	}

	return r;
}


/**
 * pVC를 이용하여 data read/write/init를 수행한다.
 *
 * cluster전체를 read/write/init 한다.
 * 만약 dwOffset이 cluster align에 맞다면 아무런 역할을 수행하지 않는다.
 *
 * 이 함수를 사용하기 전에 반드시 cluster를 할당해 두어야 한다.
 * 단, read/write size가 할당된 cluster의 크기보다 큰 경우 partial read/write/init가 된다. (cluster 단위)
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] read/write/init start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] read/write/init size
 *								dwOffset + dwSize가 cluster의 크기를 넘을 경우
 *								넘는 부분에 대해서는 write를 수행하지 않는다.
 * @param		pVC			: [IN] Fat Vectored Cluster
 * @param		pdwSizeDone	: [OUT] IO size is done
 * @param		bRWFalg		: [IN] read/write/init flag
 * @param		dwFlag		: [IN] flags for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: no error
 * @return		negative	: error number
 * @author		DongYoung Seo
 * @version		AUG-24-2006 [DongYoung Seo] First Writing
 * @version		OCT-22-2008 [GwangOk Go] if not enough allocated cluster, write end of chain
 * @version		OCT-24-2008 [GwangOk Go] add init operation
 * @version		OCT-28-2008 [DongYoung Seo] add code for last location updating 
 *								when there is no additional clusters at pVC 
 *									and that is all of the clusters for the node
 * @version		FEB-15-2009 [DongYoung Seo] pass cache flag for ffat_initCluster()
 * @version		APR-13-2009 [JeongWoo Park] modify to do not edit original VC
 * @version		May-27-2009 [JeongWoo Park] Change the dwSize to t_uint32
 *											Add the code to support 4GB
 */
static FFatErr
_readWriteBody(Node* pNode, t_uint32 dwOffset, t_int8* pBuff,
				t_uint32 dwSize, FFatVC* pOriVC, t_uint32* pdwSizeDone,
				FFatRWFlag bRWFalg, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	Vol*			pVol;
	t_uint32		dwCluster;
	t_int32			dwIndex;
	t_uint32		dwCount;
	t_uint32		dwRWCount = 0;		// read write cluster count
	t_uint32		dwRWOffset = dwOffset;
	FFatVC			stVC;				// temporary VC
	FFatErr			r;
	FFatVC*			pVC;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pOriVC);
	FFAT_ASSERT(pdwSizeDone);
	FFAT_ASSERT(((bRWFalg == FFAT_RW_READ) || (bRWFalg == FFAT_RW_WRITE)) ? (pBuff != NULL) : FFAT_TRUE);
	FFAT_ASSERT((dwOffset & VOL_CSM(NODE_VOL(pNode))) == 0);
	FFAT_ASSERT(dwSize >= (t_uint32)VOL_CS(NODE_VOL(pNode)));
	FFAT_ASSERT((dwSize & VOL_CSM(NODE_VOL(pNode))) == 0);

	pVol = NODE_VOL(pNode);

	pVC = pOriVC;
	stVC.pVCE = NULL;

	dwCount = dwSize >> VOL_CSB(pVol);

	// get cluster of dwOffset
	r = ffat_com_lookupOffsetInVC(dwOffset, pVC, &dwIndex, &dwCluster, VOL_CSB(pVol));
	if (r == FFAT_ENOENT)
	{
		// 찾지 못했다.. 그럼... 실제로 찾아야지..

		// allocate memory for Vectored Cluster Information
		stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
		FFAT_ASSERT(stVC.pVCE != NULL);

		stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
		pVC = &stVC;

		VC_INIT(pVC, (dwOffset & (~VOL_CSM(pVol))));

		r = ffat_misc_getVectoredCluster(pVol, pNode, 0, dwOffset, dwCount, pVC, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get vectored cluster info")));

		dwIndex = 0;
		dwCluster = VC_FC(pVC);
	}

	FFAT_ASSERT(r >= 0);

re:
	// check for partial entry write
	// get read write cluster count
	dwRWCount = (pVC->pVCE[dwIndex].dwCluster + pVC->pVCE[dwIndex].dwCount) - dwCluster;

	if (dwRWCount > dwCount)
	{
		dwRWCount = dwCount;	// adjust cluster count
	}

	switch (bRWFalg)
	{
		case FFAT_RW_READ:
			r = ffat_readWriteCluster(pVol, pNode, dwCluster, pBuff, dwRWCount,
								FFAT_TRUE, dwCacheFlag, pCxt);
			break;
		case FFAT_RW_WRITE:
			r = ffat_readWriteCluster(pVol, pNode, dwCluster, pBuff, dwRWCount,
								FFAT_FALSE, dwCacheFlag, pCxt);
			break;
		case FFAT_RW_INIT:
			r = ffat_initCluster(pVol, pNode, dwCluster, dwRWCount, dwCacheFlag, pCxt);
			break;
		default:
			FFAT_ASSERT(0);
			break;
	}

	FFAT_ER(r, (_T("fail to read/write/init clusters")));

	dwCount -= dwRWCount;		// decrease rest count

	dwRWOffset += (dwRWCount << VOL_CSB(pVol));		// increase offset

	dwIndex++;

	pBuff += (dwRWCount << VOL_CSB(pVol));

	while (dwCount > 0)
	{
		if (dwIndex >= VC_VEC(pVC))
		{
			// last cluster 정보는 VC 초기화전에 해주어야 함
			dwCluster = VC_LC(pVC);

			if (stVC.pVCE == NULL)
			{
				// allocate memory for Vectored Cluster Information
				stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
				FFAT_ASSERT(stVC.pVCE != NULL);

				stVC.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
				pVC = &stVC;
			}

			// 다시 cluster 정보를 수집한다. dwCluster is the last cluster number
			VC_INIT(pVC, dwRWOffset);

			r = ffat_misc_getNextCluster(pNode, dwCluster, &dwCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));

			IF_UK (FFATFS_IS_EOF(VOL_VI(pVol), dwCluster) == FFAT_TRUE)
			{
				// Corrupted FAT Chain
				FFAT_ASSERT(0);

				// no more cluster chain
				// request size가 cluster chain보다 크다
				r = FFAT_EFAT;
				goto out;
			}

			r = ffat_misc_getVectoredCluster(pVol, pNode, dwCluster, FFAT_NO_OFFSET,
											dwCount, pVC, NULL, pCxt);
			FFAT_EO(r, (_T("fail to get vectored cluster information")));
			
			dwIndex = 0;
			goto re;
		}

		FFAT_ASSERT(dwIndex > 0);	// Never be zero

		if (dwCount < pVC->pVCE[dwIndex].dwCount)
		{
			dwRWCount = dwCount;
		}
		else
		{
			dwRWCount = pVC->pVCE[dwIndex].dwCount;
		}

		dwCluster = pVC->pVCE[dwIndex].dwCluster;

		switch (bRWFalg)
		{
			case FFAT_RW_READ:
				r = ffat_readWriteCluster(pVol, pNode, dwCluster, pBuff, dwRWCount,
								FFAT_TRUE, dwCacheFlag, pCxt);
				break;
			case FFAT_RW_WRITE:
				r = ffat_readWriteCluster(pVol, pNode, dwCluster, pBuff, dwRWCount,
								FFAT_FALSE, dwCacheFlag, pCxt);
				break;
			case FFAT_RW_INIT:
				r = ffat_initCluster(pVol, pNode, dwCluster, dwRWCount, dwCacheFlag, pCxt);
				break;
			default:
				FFAT_ASSERT(0);
				break;
		}

		FFAT_EO(r, (_T("fail to read/write/init clusters")));

		dwCount		-= dwRWCount;
		dwRWOffset	+= (dwRWCount << VOL_CSB(pVol));	// increase RW offset

		pBuff += (dwRWCount << VOL_CSB(pVol));
		dwIndex++;
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), (dwCluster + dwRWCount - 1)) == FFAT_TRUE);

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	IF_LK (r >= FFAT_OK)
	{
		// 성공한 크기만큼 반환
		*pdwSizeDone = (dwSize - (dwCount << VOL_CSB(pVol)));
		return FFAT_OK;
	}
	else
	{
		return r;
	}
}


//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
/**
 * pVC를 이용하여 data read/write를 수행한다. 성능을 위해서 head, body , tail 로 나누지 않고 한번에 i/o 를 수행한다.
 *
 * 이 함수를 사용하기 전에 반드시 cluster를 할당해 두어야 한다.
 *
 * @param		pNode		: [IN] node poiter
 *								pNode->stCurFP 가 update 된다.
 * @param		dwOffset	: [IN] write start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		pVC		    : [IN] free vectored cluster info
 * @param		pdwSizeDone	: [OUT] IO size is done
 * @param  bRead  : [IN] FFAT_TRUE : read, FFAT_FALSE ; write
 * @param		dwCacheFlag	: [IN] flags for cache IO
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: no error
 * @return		negative	: error number
 * @author		KYUNGSIK SONG
 * @version		JULY-01-2009 [KYUNGSIK SONG] First Writing
 */
// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
static FFatErr
_readWriteWhole(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
			FFatVC* pVC, t_uint32* pdwSizeDone, t_boolean bRead, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	Vol*		pVol;
	t_uint32	dwCluster;
	t_int32		dwIndex;
	t_int32		dwCount;
	t_int32		dwRWCount = 0;		// read write cluster count
	FFatErr		r;
	t_uint32	dwLastOffset;
	FFatVC		stFVC;

	FFAT_ASSERT(pNode);
	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pBuff);

	pVol = NODE_VOL(pNode);
	stFVC.pVCE = NULL;

	// get required cluster count for read
	dwLastOffset = (dwOffset & VOL_CSM(pVol)) + dwSize - 1;
	
	if((!(dwLastOffset & VOL_CSM(pVol))) && bRead)
	{
		*pdwSizeDone = 0;	
		return FFAT_OK;
	}

	// read에 필요한 total cluster의 수를 구한다.
	dwCount	= (t_int32)ESS_MATH_CDB((dwLastOffset + 1), VOL_CS(pVol), VOL_CSB(pVol));

	// get cluster of dwOffset
	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	r = ffat_com_lookupOffsetInVC(dwOffset, pVC, &dwIndex, &dwCluster, VOL_CSB(pVol));

	if (r == FFAT_ENOENT)
	{
		// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
		// flow 상 이런경우는 생기지 않는다...  혹시나 생길까 ?
		// 발생가능하다. when write stat offset is over file size
		// FFAT_ASSERT(0);

		// 찾지 못했다.. 그럼... 실제로 찾아야지..
		FFAT_ASSERT((VC_CC(pVC) == 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), NODE_C(pNode)) == FFAT_TRUE) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (VC_CC(pVC) > 0) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? (VC_O(pVC) !=  VC_NO_OFFSET) : FFAT_TRUE);
		FFAT_ASSERT((NODE_C(pNode) == 0) ? ((VC_O(pVC) & VOL_CSM(pVol)) == 0) : FFAT_TRUE);

		if ((VC_CC(pVC) > 0) && (VC_O(pVC) < dwOffset))
		{
			FFAT_ASSERT(dwOffset > VC_LCO(pVC, pVol));
			FFAT_ASSERT((VC_O(pVC) & VOL_CSM(pVol)) == 0);
			FFAT_ASSERT((dwOffset - VC_LCO(pVC, pVol)) >= (t_uint32)VOL_CS(pVol));

			r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), VC_LC(pVC),
				(dwOffset - VC_LCO(pVC, pVol)), &dwCluster, pCxt);
			FFAT_ER(r, (_T("fail to get cluster for an offset")));
		}
		else
		{
			FFAT_ASSERT(NODE_C(pNode) > 0);
			r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), pNode->dwCluster, dwOffset,
				&dwCluster, pCxt);
				FFAT_ER(r, (_T("fail to get cluster for an offset")));
		}

		pVC = &stFVC;			// change pFVC to avoid change of original cluster chain

		FFAT_ASSERT(stFVC.pVCE == NULL);

		stFVC.pVCE = (FFatVCE*) FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
		FFAT_ASSERT(stFVC.pVCE);
		stFVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

		// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
		VC_INIT(pVC, (dwOffset & (~VOL_CSM(pVol))));
		
		// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
		// Attention : pPAL 
		r = ffat_misc_getVectoredCluster(pVol, pNode, dwCluster, dwOffset, dwCount, pVC, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get vectored cluster info")));

		// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
		r = ffat_com_lookupOffsetInVC(dwOffset, pVC, &dwIndex, &dwCluster, VOL_CSB(pVol));
		FFAT_ER(r, (_T("impossible error , why ? i don't know !!!")));
	}

	FFAT_ASSERT(r >= 0);

	// check for partial entry write
	// get read write cluster count
	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	dwRWCount = (pVC->pVCE[dwIndex].dwCluster + pVC->pVCE[dwIndex].dwCount) - dwCluster;
	
	if (dwRWCount < dwCount)
	{
		*pdwSizeDone = 0;	
		goto out;
	}	

	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	r = _readWriteSectors(pNode, dwOffset, pBuff, dwSize, dwCluster, bRead, dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("fail to read/write clusters")));
	if(r == 0)
	{
		*pdwSizeDone = 0;
		goto out;
	}

out: 
	if (stFVC.pVCE)
	{
		FFAT_LOCAL_FREE(stFVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	}

	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	// 2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read_Modified
	if ( r < 0 )
	{
		return r;
	}
	else if ( pdwSizeDone == 0 )
	{
		return FFAT_OK;
	}
	else 
	{
		*pdwSizeDone = (t_uint32)r;
		return FFAT_OK;
	}	
}

//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read 
/**
 * sectoer 단위 data read/write를 수행한다. 성능을 위해서 head, body , tail 로 나누지 않고 한번에 i/o 를 수행할 때 쓰인다.
 *
 * 이 함수를 사용하기 전에 반드시 cluster를 할당해 두어야 한다.[Cluster가 size만큼은 연속적으로 할당됨을 가정함]
 *
 * @param		pNode		: [IN] node poiter
 *								pNode->stCurFP 가 update 된다.
 * @param		dwOffset	: [IN] write start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwCluster	: [IN] cluster number for read/write
 * @param		bRead		: [IN] FFAT_TRUE : read, FFAT_FALSE ; write
 * @param		dwFlag	: [IN] flags for cache IO
 * @param		pCxt		: [IN] context of current operation
 * @return		0 or above	: read/write size in byte
 * @return		else		: error
 * @author		KYUNGSIK SONG
 * @version		JULY-01-2009 [KYUNGSIK SONG] First Writing
 */
static t_int32
_readWriteSectors(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
			t_uint32 dwCluster, t_boolean bRead, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	t_uint32		dwSector;
	t_uint32		dwStartSector;		// init start sector number
	t_uint32		dwEndSector;		// init end sector number
	t_int32			dwTotalSectorCount;
	t_int32			dwEndOffset;
	FFatErr			r;
	Vol*			pVol;
	t_int8*			pTempBuf = NULL;
	
	FFAT_ASSERT(dwSize > 0);

	pVol = NODE_VOL(pNode);
	
	dwEndOffset = (dwOffset & VOL_CSM(NODE_VOL(pNode))) + dwSize - 1;
	
	dwSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);
	
	dwEndSector = dwSector + (dwEndOffset >> VI_SSB(VOL_VI(pVol)));
	
	// get first sector
	dwStartSector = dwSector + ((dwOffset & VI_CSM(VOL_VI(pVol))) >> VI_SSB(VOL_VI(pVol)));

	dwTotalSectorCount = dwEndSector - dwStartSector + 1;	

	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read	
	FFAT_INIT_CI(&stCI, pNode, VI_DEV(NODE_VI(pNode)));
	
	if (bRead)
	{
  	//2010.0406@shinho.oh_bug_fix_malloc	
		pTempBuf = (t_int8*)FFAT_LOCAL_ALLOC(dwTotalSectorCount * VI_SS(VOL_VI(pVol)), pCxt);
		if (pTempBuf == NULL)
		{
			return 0;
		}
		
		r = FFATFS_ReadWriteSectors(NODE_VI(pNode), dwStartSector, dwTotalSectorCount, pTempBuf,
									dwFlag, &stCI, bRead, pCxt);
		if (r < 0)
		{
    	//2010.0406@shinho.oh_bug_fix_malloc	
			FFAT_LOCAL_FREE(pTempBuf, dwTotalSectorCount * VI_SS(VOL_VI(pVol)), pCxt);
			return r;
		}
		else
		{
			FFAT_MEMCPY(pBuff, pTempBuf + (dwOffset & VI_SSM(VOL_VI(pVol))), dwSize);
    	//2010.0406@shinho.oh_bug_fix_malloc				
			FFAT_LOCAL_FREE(pTempBuf, dwTotalSectorCount * VI_SS(VOL_VI(pVol)), pCxt);
			return dwSize;
		}
	}
	else
	{
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		return FFATFS_ReadWritePartialCluster(NODE_VI(pNode), dwCluster, (dwOffset & VOL_CSM(NODE_VOL(pNode))), dwSize,
								pBuff, bRead, dwFlag, &stCI, pCxt, FFAT_TRUE);
		
	}
}






#if 0	// not used (when creating exist node, nestle use truncate)
/**
 * create an exist node
 * it checks flags
 *	if the flag is FFAT_EXCL : it return FFAT_EEXIST
 *	if the node is a directory it return FFAT_EEXIST
 *	if the node is 
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwFlag		: [IN] node creation flag
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing
 */
static FFatErr
_createExistNode(Node* pNode, FFatCreateFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_DEBUG_NODE_PRINTF(_T("Create an exist node \n"));

	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		// child node가 존재할때 
		// 이 node가 directory 이면,
		return FFAT_EEXIST;
	}
	else
	{
		// this is a file
		if (dwFlag & FFAT_CREATE_ATTR_DIR)
		{
			FFAT_LOG_PRINTF((_T("this is not a file")));
			return FFAT_EEXIST;
		}
	}

	if (dwFlag & FFAT_CREATE_EXCL)
	{
		// child node가 존재할때
		// FFAT_CREATE_EXCL flag가 있을 경우
		// so return FFAT_EEXIST
		return FFAT_EEXIST;
	}

	// target is exist and the target is a node, we need to truncate it
	if (pNode->dwSize != 0)
	{
		// node의 크기가 0이 아닐 경우 node를 0byte로 truncation 한다.
		// get a lock for child node
		r = ffat_node_open(pNode, NODE_INODE(pNode), pCxt);
		FFAT_ER(r, (_T("fail to open child node")));

		r = ffat_file_changeSize(pNode, 0, (FFAT_CHANGE_SIZE_NO_LOCK | FFAT_CHANGE_SIZE_FOR_CREATE),
						FFAT_CACHE_NONE, pCxt);
		IF_UK (r < 0)
		{
			// fail to truncate a node to size 0
			FFAT_LOG_PRINTF((_T("fail to change node size to 0")));

			// Close node
			ffat_node_close(pNode, FFAT_NODE_CLOSE_NO_LOG, pCxt);
			return r;
		}

		r = ffat_node_close(pNode, FFAT_NODE_CLOSE_NO_LOG, pCxt);
		FFAT_ER(r, (_T("fail to close a node")));
		// creation done.
	}

	return FFAT_OK;
}
#endif


/**
 * lookup node with gathering information for creation
 *
 * there is two additional operation for creation
 * 1. get ~xxx for short file name entry
 * 2. free directory entry lookup for new node
 *=========================================================
 * create를 위하여 Node를 찾는다.
 * node creation을 위한 operation이 추가된다.
 *	1. ~xxx 찾기 (Short file name에 들어갈 숫자가 생성된다.)
 *	2. 빈 DE 영역 찾기
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		psConvertedName	: [IN] node name to lookup
 *									upper case if volume is mounted as case insensitive
 * @param		dwNameLen		: [IN] hint offset
 * @param		pNode			: [IN/OUT] node pointer.
 *									free directory entry 정보가 저장됨.
 * @param		pNodeDE			: [IN/OUT] node directory entry storage
 * @param		dwLookupFlag	: [IN] flag for lookup
 * @param		pNumericTail	: [IN] buffer for numeric tail
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-11-2006 [DongYoung Seo] First Writing
 * @version		MAY-08-2007 [DongYoung Seo] modify next numeric tail candidate 
 *									to the next number of current maximum value
 * @version		JAN-18-2008 [GwangOk Go] Lookup improvement
 * @version		DEC-13-2008 [DongYoung Seo] [bug fix] some times SFN does not be compared even if name length is short.
 *									when entry count is 2 and partial name is not same.
 * @version		MAY-13-2009 [DongYoung Seo] Support Name compare with multi-byte(CQID:FLASH00021814)
 * @version		JUN-18-2009 [JeongWoo Park] Add the code to support OS specific naming rule
 *											- Case sensitive / OS character set
 */
static FFatErr
_lookupForCreate(Node* pNodeParent, t_wchar* psConvertedName, t_int32 dwNameLen,
					Node* pNodeChild, FatGetNodeDe* pNodeDE, FFatLookupFlag dwLookupFlag,
					NodeNumericTail* pNumericTail, ComCxt* pCxt)
{
	FFatErr				r;
	t_wchar*			psCurName = NULL;			// current node name storage
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
	t_int32				dwCurNameLen;				// current node name length
#endif
	t_int32				dwPrevDeCluster;			// previous entry end cluster 
													// free entry의 정보를 수집하기 위해 사용함.
	t_int32				dwPrevDeOffset;				// previous entry end offset
													// free entry의 정보를 수집하기 위해 사용함.
	t_int32				dwPrevDeEndCluster;			// previous entry last cluster for directory truncation
	t_int32				dwRequiredEntryCount;		// Node 생성에 필요한 entry count
	Vol*				pVol;

	// for log recovery
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_SYNC;

#ifndef FFAT_CMP_NAME_MULTIBYTE
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
	t_wchar				wc;							// a temporary buffer for wide character
#endif
#else
	// for FFAT_CMP_NAME_MULTIBYTE
	t_int8*				psUpperNameMB = NULL;		// converted upper named to Multi-byte
#endif

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(psConvertedName);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pNodeChild);

	pVol = NODE_VOL(pNodeParent);

	// allocate memory for node name
	psCurName = (t_wchar*)FFAT_LOCAL_ALLOC(FFAT_NAME_BUFF_SIZE, pCxt);
	FFAT_ASSERT(psCurName != NULL);

	// set entry count
	if ((VOL_FLAG(pVol) & VOL_ADDON_XDE) == 0)
	{
		// Extended DE를 사용하지 않을 경우
		dwRequiredEntryCount = pNodeDE->dwTargetEntryCount;
	}
	else
	{
		// Extended DE를 사용할 경우
		dwRequiredEntryCount = pNodeDE->dwTargetEntryCount + 1;
	}

	// create 일 경우에는 빈 entry의 정보를 수집할 필요가 있다.
	// 그러므로 dwTargetEntryCount를 0으로 설정하여 모든 entry를 조사한다.
	pNodeDE->dwTargetEntryCount = 0;
	pNodeDE->psName				= NULL;
	pNodeDE->bExactOffset		= FFAT_FALSE;
	pNodeDE->dwClusterOfOffset	= 0;

	if (pNodeDE->dwOffset != 0)
	{
		// start offset is valid
		dwPrevDeOffset	= pNodeDE->dwOffset - FAT_DE_SIZE;

		if (dwPrevDeOffset & FAT_DE_SIZE_MASK)
		{
			// align offset by DE size
			dwPrevDeOffset = (dwPrevDeOffset & (~FAT_DE_SIZE_MASK)) + FAT_DE_SIZE;
		}

		if (pNodeDE->dwClusterOfOffset != 0)
		{
			// start cluster of valid
			dwPrevDeCluster = pNodeDE->dwClusterOfOffset;
		}
		else
		{
			FFAT_ASSERT(NODE_C(pNodeParent) != 0);

			r = ffat_misc_getClusterOfOffset(pNodeParent, dwPrevDeOffset,
										(t_uint32*)&dwPrevDeCluster, NULL, pCxt);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to get near cluster")));
				goto out;
			}
		}
	}
	else
	{
re:		// re for numeric tail
		// initialize previous information
		dwPrevDeCluster	= pNodeDE->dwCluster;

		if (NODE_IS_ROOT(pNodeParent) == FFAT_TRUE)
		{
			dwPrevDeOffset	= -1;			// there is no entry
		}
		else
		{
			dwPrevDeOffset	= FAT_DE_SIZE;	// set it to the offset for .. entry 
		}
	}

	dwPrevDeEndCluster = dwPrevDeCluster;

	do
	{
		// get directory entry
		r = ffat_dir_getDirEntry(pVol, pNodeParent, pNodeDE, FFAT_FALSE, FFAT_TRUE, pCxt);
		if (r < 0)
		{
			// Node를 찾지 못했거나 에러 발생.
			// when there is no element that name is psName
			// or there is some error

			// the node is not exist
			if (r == FFAT_EEOF)
			{
				// update free DE area
				if (pNodeChild->stDeInfo.dwFreeCount == 0)
				{
					// EEOF 까지 왔지만 FREE DE 정보를 찾지 못했다.
					r = _updateFreeDeLast(pNodeDE, dwPrevDeCluster, dwPrevDeOffset,
												dwRequiredEntryCount, pNodeChild, pCxt);
					FFAT_EO(r, (_T("Fail to update information for node creation")));
				}
				else
				{
					// we can truncate the directory
#ifdef FFAT_TRUNCATE_DIR
					if (FFATFS_FAT16_ROOT_CLUSTER == NODE_C(pNodeParent))
					{
						// nothing to do.
						FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);
					}
					else
					{
						// do not check error
						ffat_addon_truncateDir(pNodeParent, dwPrevDeEndCluster,
											dwPrevDeOffset, &dwCacheFlag, pCxt);
					}
#endif
				}

				if (pNodeChild->dwFlag & NODE_NAME_NUMERIC_TAIL)
				{
					FFAT_ASSERT((pNodeChild->dwFlag & NODE_NAME_SFN) == 0);

					// update numeric tail
					r = ffat_node_insertNumericTail(pNodeChild, pNumericTail);
					IF_UK (r < 0)
					{
						FFAT_LOG_PRINTF((_T("Fail to insert numeric tail")));
						goto out;
					}

					if (r == FFAT_OK1)
					{
						// need to find a new numeric tail
						// initialize NumericTail
						ffat_node_initNumericTail(pNumericTail, pNumericTail->wMax + 1);

						// reset lookup start offset
						pNodeDE->dwOffset			= 0;
						pNodeDE->dwClusterOfOffset	= NODE_C(pNodeParent);

						goto re;
					}
				}

				r = FFAT_ENOENT;
			}
			else if (r == FFAT_EXDE)
			{
				r = FFAT_EFAT;
			}

			goto out;
		}

		// LFN only일 경우 SFN만 리턴될 수 없다.
		FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (pNodeDE->dwEntryCount > 1));

		if (dwLookupFlag & FFAT_LOOKUP_FREE_DE)
		{
			// do not compare with any name
			goto next;
		}

		// check volume label
		IF_UK (pNodeDE->dwEntryCount == 1)
		{
			FFAT_ASSERT(pNodeDE->pDE[0].bAttr != FFAT_ATTR_LONG_NAME);

			if (pNodeDE->pDE[0].bAttr & FFAT_ATTR_VOLUME)
			{
				// ignore volume label
				FFAT_ASSERT(NODE_IS_ROOT(pNodeParent) == FFAT_TRUE);
				goto next;
			}
		}

#ifdef FFAT_VFAT_SUPPORT
	#ifndef FFAT_CMP_NAME_MULTIBYTE
		if ((dwRequiredEntryCount == pNodeDE->dwTotalEntryCount) &&
				(pNodeDE->dwEntryCount >= 2))
		{
			// compare long file name
			// QUIZ !!! -- Why did I use memcpy ?.
			FFAT_MEMCPY(&wc, (((FatDeLFN*)(&pNodeDE->pDE[pNodeDE->dwEntryCount - 2]))->sName1),
							sizeof(t_wchar));

			wc = FFAT_BO_UINT16(wc);

			if ((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
			{
				wc = FFAT_TOWUPPER(wc);
			}
			
			if (psConvertedName[0] != wc)
			{
				// 1st character is a different character.
				goto sfn;
			}

			// get long file name
			r = FFATFS_GenNameFromDirEntry(NODE_VI(pNodeChild), pNodeDE->pDE,
							pNodeDE->dwEntryCount, psCurName, &dwCurNameLen, FAT_GEN_NAME_LFN);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to generate name from DE")));
				goto out;
			}

			// check name length
			if (dwNameLen == dwCurNameLen)
			{
				t_int32	i;

				// 마지막 문자가 다를 확률이 높기에 확장자를 제외한 name 부분의 
				// 마지막 3개의 문자부터 비교해보고 같으면 나머지를 비교한다
				FFAT_ASSERT(dwNameLen >= pNodeChild->wNamePartLen);

				for (i = pNodeChild->wNamePartLen; (i > 0) && (i > (pNodeChild->wNamePartLen - 3));	i--)
				{
					wc = psCurName[i - 1];
					if ((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
					{
						wc = FFAT_TOWUPPER(wc);
					}

					if (psConvertedName[i - 1] != wc)
					{
						goto sfn;
					}
				}

				// 모든 string을 비교해 본다.
				if (((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
					? (FFAT_WCSUCMP(psConvertedName, psCurName) == 0)
					: (FFAT_WCSCMP(psConvertedName, psCurName) == 0))
				{
					// we found it !!
					break;
				}
			}
		}

	#else	// #ifndef FFAT_CMP_NAME_MULTIBYTE

		if (
			(pNodeDE->dwEntryCount >= 2) &&
				((dwRequiredEntryCount == pNodeDE->dwTotalEntryCount) ||
				(dwRequiredEntryCount  == 1))
			)
		{
			// compare long file name
			// get long file name
			r = FFATFS_GenNameFromDirEntry(NODE_VI(pNodeChild), pNodeDE->pDE,
							pNodeDE->dwEntryCount, psCurName, &dwCurNameLen, FAT_GEN_NAME_LFN);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to generate name from DE")));
				goto out;
			}

			// check name length
			// compare name with multi-byte
			if (_lookupForCreateCompareMultiByte(pNodeChild, psCurName,
							&psUpperNameMB, psConvertedName, dwNameLen, pCxt, FFAT_FALSE) == 0)
			{
				// we found it
				break;
			}
		}

		goto sfn;
	#endif

#else
		FFAT_ASSERT(pNodeDE->dwEntryCount == 1);
#endif

//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT
sfn:
		FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (pNodeDE->dwEntryCount >= 2));

		// LFN only일 경우 SFN은 비교할 필요 없음
		if (((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) &&
			(dwNameLen <= FAT_DE_SFN_MAX_LENGTH))
		{
			// Short file name도 비교를 해봐야 한다.
			// get short file name
			r = FFATFS_GenNameFromDirEntry(NODE_VI(pNodeChild), pNodeDE->pDE, pNodeDE->dwEntryCount,
										psCurName, &dwCurNameLen, FAT_GEN_NAME_SFN);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to generate name from DE")));
				goto out;
			}

		#ifndef FFAT_CMP_NAME_MULTIBYTE
			if (dwNameLen != dwCurNameLen)
			{
				// short file name 도 다르다.
				goto next;
			}

			// 모든 string을 비교해 본다.
			if (FFAT_WCSUCMP(psConvertedName, psCurName) == 0)
			{
				// we found it !!
				break;
			}
		#else
			if (dwNameLen == dwCurNameLen)
			{
				// 모든 string을 비교해 본다.
				if (FFAT_WCSUCMP(psConvertedName, psCurName) == 0)
				{
					// we found it !!
					break;
				}
			}

			// compare name with multi-byte
			if (_lookupForCreateCompareMultiByte(pNodeChild, psCurName,
							&psUpperNameMB, psConvertedName, dwNameLen, pCxt, FFAT_FALSE) == 0)
			{
				// we found it
				break;
			}
		#endif
		}
#endif

next:
		// update free DE information
		if (pNodeChild->stDeInfo.dwFreeCount == 0)
		{
			// free entry의 정보가 없을 경우에만 수행
			r = _updateFreeDe(pNodeDE, &dwPrevDeCluster, &dwPrevDeOffset,
								dwRequiredEntryCount, pNodeChild, pCxt);
			FFAT_EO(r, (_T("Fail to update free de information")));
		}

		if (pNodeChild->dwFlag & NODE_NAME_NUMERIC_TAIL)
		{
			FFAT_ASSERT((pNodeChild->dwFlag & NODE_NAME_SFN) == 0);

			r = _updateNumericTail(pNodeDE, pNodeChild, pNumericTail);
			FFAT_EO(r, (_T("Fail to update information for node creation")));
		}

		pNodeDE->dwOffset = pNodeDE->dwDeEndOffset + FAT_DE_SIZE;
		if (pNodeDE->dwOffset & VOL_CSM(pVol))
		{
			pNodeDE->dwClusterOfOffset	= pNodeDE->dwDeEndCluster;
		}
		else
		{
			pNodeDE->dwClusterOfOffset	= 0;
		}

		dwPrevDeEndCluster		= pNodeDE->dwDeEndCluster;
	} while (1);

	r = FFAT_OK;

out:
#ifdef FFAT_CMP_NAME_MULTIBYTE
	// release allocated memory
	_lookupForCreateCompareMultiByte(pNodeChild, psCurName, &psUpperNameMB,
					psConvertedName, dwNameLen, pCxt, FFAT_TRUE);
#endif

	// free memory for name
	FFAT_LOCAL_FREE(psCurName, FFAT_NAME_BUFF_SIZE, pCxt);

	return r;
}


/**
 * creation을 위한 lookup에서 numeric tail 정보를 update 한다.
 *
 * @param		pNodeDE			: [IN] Directory Entry information
 * @param		pdwPrevDeCluster: [OUT] :cluster of previous directory entry
 * @param		pdwPrevDeOffset	: [IN/OUT] in : end offset of previous node
											out : current node end offset
 * @param		dwEntryCount	: [IN] minimum free entry count.
 * @param		pNode			: [IN] Node pointer
 * @param		pNumericTail	: [IN/OUT] numeric tail information
 * @author		DongYoung Seo
 * @version		AUG-11-2006 [DongYoung Seo] First Writing
 */
static FFatErr
_updateNumericTail(FatGetNodeDe* pNodeDE, Node* pNode, NodeNumericTail* pNumericTail)
{
	t_int32		dwNumericTail;
	FatDeSFN*	pDeSFN;

	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNumericTail);

	// update numeric tail

	// create flag가 있을 경우이므로 pNode의 stDE에는 SFN 이 채워져 있다.
	// if the first character is not same, pass it.
	// if the first character of extension is not a same one, pass it too.
	pDeSFN = &pNodeDE->pDE[pNodeDE->dwEntryCount - 1];
	if ((FFAT_TOUPPER(pDeSFN->sName[0]) != pNode->stDE.sName[0]) ||
		(FFAT_TOUPPER(pDeSFN->sName[FAT_SFN_NAME_PART_LEN]) != pNode->stDE.sName[FAT_SFN_NAME_PART_LEN]))
	{
		return FFAT_OK;
	}

	// get numeric tail
	dwNumericTail = FFATFS_GetNumericTail(pDeSFN);
	if (dwNumericTail == 0)
	{
		// there is no numeric tail
		return FFAT_OK;
	}

	// update NodeNumericTail structure

	// check random value
	if (pNumericTail->wRand1 == dwNumericTail) 
	{
		pNumericTail->wRand1 = 0;
	}

	if (pNumericTail->wRand2 == dwNumericTail) 
	{
		pNumericTail->wRand2 = 0;
	}

	if ((dwNumericTail < pNumericTail->wMin) || 
		(dwNumericTail > pNumericTail->wMax))
	{
		return FFAT_OK;
	}

	// update bitmap
	ESS_BITMAP_SET(pNumericTail->pBitmap, (dwNumericTail - pNumericTail->wMin));

	pNumericTail->wBottom	= ESS_GET_MIN(pNumericTail->wBottom, (t_int16)dwNumericTail);
	pNumericTail->wTop		= ESS_GET_MAX(pNumericTail->wTop, (t_int16)dwNumericTail);

	return FFAT_OK;
}


/**
 * free directory entry 정보를 update 한다.
 *
 *
 * @param		pNodeDE				: [IN] Directory Entry information
 * @param		pdwPrevDeCluster	: [IN/OUT] in : end offset of previous node
											out : current node end offset
 * @param		pdwPrevDeOffset		: [IN/OUT] previous directory entry offset
 *										-1 : there is no previous offset
 * @param		dwEntryCount		: [IN] minimum free entry count.
 * @param		pCxt				: [IN] context of current operation
 * @param		pNode				: [IN] Node pointer
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 */
static FFatErr
_updateFreeDe(FatGetNodeDe* pNodeDE, 
				t_int32* pdwPrevDeCluster, t_int32* pdwPrevDeOffset,
				t_int32 dwEntryCount, Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;
	t_int32		dwFreeCount;				// free entry count
	t_int32		dwStartClusterOffset;		// start cluster offset
	t_int32		dwEndClusterOffset;			// end cluster offset

	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pdwPrevDeCluster);
	FFAT_ASSERT(pdwPrevDeOffset);
	FFAT_ASSERT(pNode);

	if (*pdwPrevDeOffset < 0)
	{
		// there is no DE
		dwFreeCount = pNodeDE->dwDeStartOffset;
	}
	else
	{
		dwFreeCount = pNodeDE->dwDeStartOffset - (*pdwPrevDeOffset + FAT_DE_SIZE);
	}

	dwFreeCount = dwFreeCount / FAT_DE_SIZE;		// compiler가 잘해줄것임. ^^; 

	if (dwFreeCount >= dwEntryCount)
	{
		// there is enough free entry
		if (NODE_COP(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
		{
			// root directory 일 경우는 root 임을 설정함.
			pNode->stDeInfo.dwFreeCluster	= FFATFS_FAT16_ROOT_CLUSTER;
		}
		else
		{
			pNode->stDeInfo.dwFreeCluster	= *pdwPrevDeCluster;
		}

		if (*pdwPrevDeOffset < 0)
		{
			pNode->stDeInfo.dwFreeOffset	= 0;
		}
		else
		{
			pNode->stDeInfo.dwFreeOffset	= (*pdwPrevDeOffset + FAT_DE_SIZE);
		}

		pNode->stDeInfo.dwFreeCount		= dwFreeCount;

		// check free offset is on next cluster
		if (((pNode->stDeInfo.dwFreeOffset & VOL_CSM(NODE_VOL(pNode))) == 0) &&
			(pNode->stDeInfo.dwFreeOffset != 0))
		{
			// On my god.. it is on the next cluster
			// we must get the next cluster number

			// check root directory
			if (NODE_COP(pNode) != FFATFS_FAT16_ROOT_CLUSTER)
			{
				FFAT_ASSERT(pNodeDE->dwCluster != FFATFS_FAT16_ROOT_CLUSTER);

				dwStartClusterOffset = ESS_MATH_CDB((pNode->stDeInfo.dwFreeOffset + 1),
											VOL_CS(NODE_VOL(pNode)), VOL_CSB(NODE_VOL(pNode)));
				dwEndClusterOffset = ESS_MATH_CDB((pNodeDE->dwDeEndOffset + 1), 
											VOL_CS(NODE_VOL(pNode)), VOL_CSB(NODE_VOL(pNode)));

				// first try. end offset is on the next cluster ?
				if (dwStartClusterOffset == dwEndClusterOffset)
				{
					// Great!! they are on the same cluster.. 
					pNode->stDeInfo.dwFreeCluster = pNodeDE->dwDeEndCluster;
				}
				else
				{
					r = ffat_misc_getNextCluster(pNode, *pdwPrevDeCluster,
										&pNode->stDeInfo.dwFreeCluster, pCxt);
					IF_UK (r < 0)
					{
						FFAT_LOG_PRINTF((_T("fail to get next cluster")));
						return r;
					}
				}
			}
		}
	}

	*pdwPrevDeCluster	= pNodeDE->dwDeEndCluster;
	*pdwPrevDeOffset	= pNodeDE->dwDeEndOffset;

	return FFAT_OK;
}


/**
 * 마지막 DE를 읽은 이후의 나머지 free directory entry 정보를 update 한다.
 *
 *
 * @param		pNodeDE			: [IN] Directory Entry information
 * @param		dwPrevDeCluster	: [IN] Previous DE cluster
 * @param		dwPrevDeOffset	: [IN] Previous DE offset
 *										-1 : there is no DE on the directory
 *											root only
 * @param		dwEntryCount	: [IN] minimum free entry count.
 * @param		pNode			: [IN] Node pointer
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 */
static FFatErr
_updateFreeDeLast(FatGetNodeDe* pNodeDE, 
				t_int32 dwPrevDeCluster, t_int32 dwPrevDeOffset,
				t_int32 dwEntryCount, Node* pNode, ComCxt* pCxt)
{
	FFatErr			r;
	t_int32			dwFreeCount;		// free entry count
	t_int32			dwDeEndOffset;		// end offset of free DE
	t_uint32		dwCluster;
	FatVolInfo*		pVolInfo;
	t_int32			i;

	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_VOL(pNode));

	// free entry의 정보가 없을 경우에만 수행
	pVolInfo	= NODE_VI(pNode);

	if (NODE_COP(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
	{
		dwDeEndOffset = (VI_REC(pVolInfo) - 1) << FAT_DE_SIZE_BITS;	// last DE offset
	}
	else
	{
		dwCluster		= dwPrevDeCluster;

		if (dwPrevDeOffset < 0)
		{
			dwDeEndOffset = VI_CS(pVolInfo) - FAT_DE_SIZE;
		}
		else
		{
			dwDeEndOffset	= (dwPrevDeOffset + VI_CS(pVolInfo)) & (~VI_CSM(pVolInfo));
			dwDeEndOffset	-= FAT_DE_SIZE;	// this is the last DE offset on the cluster
		}

		// 3개의 cluster 정도면 모든 길이의 node이름 저장이 가능함.
		i = 3;

		// get last DE offset
		do 
		{
			r = FFATFS_GetNextCluster(pVolInfo, dwCluster, &dwCluster, pCxt);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to get next cluster")));
				return r;
			}

			if (FFATFS_IS_EOF(pVolInfo, dwCluster) == FFAT_TRUE)
			{
				break;
			}

			dwDeEndOffset += VI_CS(pVolInfo);

		} while (i--);
	}

	if (pNodeDE->dwOffset < FAT_DE_SIZE)
	{
		// it must be root directory
		// DE에 Valid한 게 없는 것이므로 cluster 전체가 free entry임, +1을 해줘야 함 [BUG FIX By STORM / 20080415]
		dwFreeCount = dwDeEndOffset + FAT_DE_SIZE;
		FFAT_ASSERT(((dwFreeCount & VI_CSM(pVolInfo)) == 0) || (dwFreeCount == (VI_REC(pVolInfo) << FAT_DE_SIZE_BITS)));
	}
	else
	{
		dwFreeCount = dwDeEndOffset - dwPrevDeOffset;	// this comes from upper line
	}

	dwFreeCount = dwFreeCount >> FAT_DE_SIZE_BITS;		// (/ FAT_DE_SIZE)

	if (dwFreeCount >= dwEntryCount)
	{
		// root 일 경우 처음 node에 대한 처리.
		if (pNodeDE->dwOffset == 0)
		{
			// there is no node in the root directory
			pNode->stDeInfo.dwFreeOffset	= 0;
		}
		else
		{
			pNode->stDeInfo.dwFreeOffset	= dwPrevDeOffset + FAT_DE_SIZE;
		}

		// there is enough free entry
		if (NODE_COP(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
		{
			// root directory 일 경우는 root 임을 설정함.
			pNode->stDeInfo.dwFreeCluster	= FFATFS_FAT16_ROOT_CLUSTER;
		}
		else
		{
			pNode->stDeInfo.dwFreeCluster	= dwPrevDeCluster;

			if (((pNode->stDeInfo.dwFreeOffset & VI_CSM(pVolInfo)) == 0) && 
				(pNode->stDeInfo.dwFreeOffset != 0))
			{
				// next cluster is the first free cluster
				r = FFATFS_GetNextCluster(pVolInfo, dwPrevDeCluster,
								&pNode->stDeInfo.dwFreeCluster, pCxt);
				IF_UK (r < 0)
				{
					FFAT_LOG_PRINTF((_T("fail to get next cluster")));
					return r;
				}
			}

			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVolInfo, pNode->stDeInfo.dwFreeCluster) == FFAT_TRUE);
		}

		pNode->stDeInfo.dwFreeCount		= dwFreeCount;
	}

	// set last DE cluster and offset
	pNode->stDeInfo.dwLastDeCluster = dwPrevDeCluster;
	pNode->stDeInfo.dwLastDeOffset	= dwPrevDeOffset;

	return FFAT_OK;
}


/**
 * lookup helper function for FatGetNodeDe structure initialization
 *
 * initialization item.
 *	1. lookup start cluster and offset
 *	2. lookup entry count
 *
 * @param		pNodeParent	: [IN] Parent node
 * @param		dwOffset	: [IN] lookup start offset
 * @param		dwFlag		: [IN] flag for lookup
 * @param		dwNameType	: type of name, refer to FFatNameType 
 * @param		dwNameLen	: type of name, refer to FFatNameType 
 * @param		pGetNodeDe	: [OUT] node DE information structure for lookup operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-09-2006 [DongYoung Seo] First Writing
 */
static FFatErr
_lookupHelperForGetNodeDe(Node* pNodeParent, t_int32 dwOffset,
			FatNameType dwNameType, t_int32 dwNameLen, FatGetNodeDe* pNodeDE)
{
	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeDE);

	pNodeDE->dwCluster			= pNodeParent->dwCluster;
	pNodeDE->dwClusterOfOffset	= 0;
	pNodeDE->dwOffset			= dwOffset;

#ifdef FFAT_VFAT_SUPPORT
	if (dwNameType & FAT_NAME_SFN)
	{
		pNodeDE->dwTargetEntryCount	= 1;	//set entry count to 1
	}
	else
	{
		pNodeDE->dwTargetEntryCount	= ESS_MATH_CD(dwNameLen, FAT_LFN_NAME_CHAR) + 1;
	}
#else
	FFAT_ASSERT(dwNameType & FAT_NAME_SFN);
	pNodeDE->dwTargetEntryCount	= 1;
#endif

	FFAT_ASSERT(pNodeDE->dwTargetEntryCount >= 1);

	return FFAT_OK;
}


/**
 * numeric tail 정보를 저장하기 위한 구조체를 초기화 한다.
 *
 * @param		pNumericTail	: [IN/OUT] numeric tail structure pointer
 * @param		wBase			: [IN] numeric tail number base
 * @return		void
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-11-2006 [DongYoung Seo] First Writing
 */
void
ffat_node_initNumericTail(NodeNumericTail* pNumericTail, t_uint16 wBase)
{
	t_int32		dwTemp;

	FFAT_ASSERT(pNumericTail);

	// initialize bitmap
	FFAT_MEMSET(pNumericTail->pBitmap, 0x00, FFAT_NUMERIC_TAIL_BUFF);

	pNumericTail->wMin		= wBase;
	pNumericTail->wMax		= wBase + FFAT_NUMERIC_TAIL_MAX_INDEX - 1;
	pNumericTail->wRand1	= (t_uint16)FFAT_RAND();
	pNumericTail->wRand2	= (t_uint16)FFAT_RAND();
	pNumericTail->wBottom	= pNumericTail->wMax;
	pNumericTail->wTop		= 0;

	if ((pNumericTail->wMax + 1) >= FAT_DE_MAX)
	{
		// Max 이상일 경우 random 값은 사용할 필요가 없다.
		pNumericTail->wRand1 = pNumericTail->wRand2 = 0;
	}

	if (pNumericTail->wRand1 < pNumericTail->wMax)
	{
		dwTemp = pNumericTail->wMax + pNumericTail->wRand1;
		if (dwTemp >= FAT_DE_MAX)
		{
			pNumericTail->wRand1 = pNumericTail->wMax + 1;
		}
		else
		{
			pNumericTail->wRand1 = (t_uint16)dwTemp;
		}
	}

	if (pNumericTail->wRand2 < pNumericTail->wMax)
	{
		dwTemp = pNumericTail->wMax + pNumericTail->wRand2;
		if (dwTemp >= FAT_DE_MAX)
		{
			pNumericTail->wRand2 = pNumericTail->wMax + 2;
		}
		else
		{
			pNumericTail->wRand2 = (t_uint16)dwTemp;
		}
	}

	return;
}


/**
 * insert numeric tail at pNode->stDE
 *
 * @param		pNode		: [IN/OUT] node pointer
 * @param		pNT			: [IN] numeric tail information
 * @return		FFAT_OK		: success
 * @return		FFAT_OK1	: there is no free numeric tail
 * @return		FFAT_ENOSPC	: too big numeric tail
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 * @version		MAR-24-2009 [DongYoung Seo] add maximum numeric tail limitation code(FLASH00020850)
 */
FFatErr
ffat_node_insertNumericTail(Node* pNode, NodeNumericTail* pNT)
{
	t_int32		dwNumericTail;
	t_int32		dwLen;
	t_int32		dwLast;
	t_int8		psName[12];	// 임시로 ~xxx를 저장할 buffer
	t_int32		i;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNT);

	// ~0은 사용하지 않는다.
	if (pNT->wMin == 0)
	{
		ESS_BITMAP_SET(pNT->pBitmap, 0);
	}

	// 1. 가장 작은 ~xxx 를 구한다.
	dwNumericTail = EssBitmap_GetLowestBitZero((t_uint8*)pNT->pBitmap, FFAT_NUMERIC_TAIL_BUFF);
	if (dwNumericTail < 0)
	{
		FFAT_ASSERT(dwNumericTail == ESS_ENOENT);

#ifdef FFAT_MAX_NUMERIC_TAIL
		// we do not permit numeric tail over FFAT_MAX_NUMERIC_TAIL
		FFAT_PRINT_DEBUG((_T("Too big number for numeric tail")));
		return FFAT_ENOSPC;
#else
		// there is no free bit
		// check first and second random variable
		if (pNT->wRand1 != 0)
		{
			dwNumericTail = (t_int32)pNT->wRand1;
		}
		else if (pNT->wRand2 != 0)
		{
			dwNumericTail = (t_int32)pNT->wRand2;
		}
		else
		{
			// need to find a new numeric tail
			return FFAT_OK1;
		}
#endif
	}

	FFAT_ASSERT(dwNumericTail >= 0);
	FFAT_ASSERT(dwNumericTail <= FAT_DE_MAX);

	dwNumericTail = pNT->wMin + dwNumericTail;

#ifdef FFAT_MAX_NUMERIC_TAIL
	// we do not permit numeric tail over FFAT_MAX_NUMERIC_TAIL
	if (dwNumericTail > FFAT_MAX_NUMERIC_TAIL)
	{
		FFAT_PRINT_DEBUG((_T("Too big number for numeric tail")));
		return FFAT_ENOSPC;
	}
#endif

	// 2. 구했으니 SFNE 에 붙인다.
	// 일단 ~xxx 를 준비한다.
	psName[0] = '~';
	__Itoa(dwNumericTail, (psName + 1));
	dwLen = (t_int32)FFAT_STRLEN(psName);

	FFAT_DEBUG_NODE_PRINTF((_T("New NumericTail:%d, Name :%s\n"), dwNumericTail, psName));

	// 3. 이제 넣을 position을 찾는다.
	// 주의 DBCS의 경우 DBC의 일부를 자르지 않도록 주의한다.
	for (i = 7; i > 0; i--)
	{
		if (pNode->stDE.sName[i] != ' ')
		{
			break;
		}
	}

	i++;

	if ((i + dwLen) <= FAT_SFN_NAME_PART_LEN)
	{
		// 찾았다~~~. 그냥 뒤에 붙이면 된다.
	}
	else
	{
		dwLast = FAT_SFN_NAME_PART_LEN - dwLen;
		for (i = 0; i < dwLast; i++)
		{
			FFAT_ASSERT(NODE_VOL(pNode));
			FFAT_ASSERT(NODE_VI(pNode));

			// DBC 인지 체크
			if (FFAT_ISLEADBYTE(pNode->stDE.sName[i], VI_DEV(NODE_VI(pNode))))
			{
				if ((i + 1) >= dwLast)
				{
					break;
				}

				i++;
			}
		}
	}

	// 3. i위치에 넣으면 된다.
	FFAT_MEMCPY(&pNode->stDE.sName[i], psName, dwLen);

	// 4. ' '를 채워야 하나?
	if ((i + dwLen) < FAT_SFN_NAME_PART_LEN)
	{
		FFAT_MEMSET(&pNode->stDE.sName[(i + dwLen)], 0x20, FAT_SFN_NAME_PART_LEN - (i + dwLen));
	}

	// 5. 자알 들어갔다.
	return FFAT_OK;
}


/**
 * function parameter checker for ffat_node_rename()
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node
 * @param		pNodeDesParent	: [IN] parent node of destination(target)
 * @param		pNodeDes		: [IN] destination node (May be NULL)
 * @param		psName			: [IN] target node name
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOSUPPORT	: no support rename between different volumes
 * @return		FFAT_EXDEV		: media ejected (time stamp checking error)
 * @return		FFAT_ENOTDIR	: parent is not a directory
 * @return		FFAT_EACCESS	: volume is mounted with read-only flag
 * @author		DongYoung Seo
 * @version		AUG-30-2006 [DongYoung Seo] First Writing.
 * @version		FEB-11-2009 [GwangOk Go] update renamed node info on pNodeSrc (delete pNodeNew)
 */
static FFatErr
_renameCheckParameter(Node* pNodeSrcParent, Node* pNodeSrc,
						Node* pNodeDesParent, Node* pNodeDes, t_wchar* psName)
{
#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNodeSrcParent == NULL) || (pNodeSrc == NULL) ||
			(pNodeDesParent == NULL) || (pNodeDes == NULL) || (psName == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK ((NODE_IS_VALID(pNodeSrcParent) != FFAT_TRUE) || 
			(NODE_IS_VALID(pNodeSrc) != FFAT_TRUE) ||
			(NODE_IS_VALID(pNodeDesParent) != FFAT_TRUE))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNodeSrcParent);
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDesParent);
	FFAT_ASSERT(pNodeDes);
	FFAT_ASSERT(psName);

	FFAT_ASSERT(NODE_IS_VALID(pNodeSrcParent) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_VALID(pNodeSrc) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_VALID(pNodeDesParent) == FFAT_TRUE);

	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeSrcParent) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeSrc) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeDesParent) == FFAT_FALSE);

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNodeSrc) == FFAT_FALSE) ? (NODE_S(pNodeSrc) == FFATFS_GetDeSize(NODE_DE(pNodeSrc))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNodeSrc) == FFATFS_GetDeCluster(NODE_VI(pNodeSrc), NODE_DE(pNodeSrc)));
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeSrc) == FFAT_FALSE);

	FFAT_ASSERT(((NODE_IS_VALID(pNodeDes) == FFAT_TRUE) && (NODE_IS_DIRTY_SIZE(pNodeDes) == FFAT_FALSE)) ? (NODE_S(pNodeDes) == FFATFS_GetDeSize(NODE_DE(pNodeDes))) : FFAT_TRUE);
	FFAT_ASSERT((NODE_IS_VALID(pNodeDes) == FFAT_TRUE) ? (NODE_C(pNodeDes) == FFATFS_GetDeCluster(NODE_VI(pNodeDes), NODE_DE(pNodeDes))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNodeDes) == FFAT_FALSE);

	// check err 1
	// volume is same or not
	IF_UK (NODE_VOL(pNodeSrcParent)->wTimeStamp != NODE_VOL(pNodeDesParent)->wTimeStamp)
	{
		FFAT_LOG_PRINTF((_T("no support rename between different volumes !!")));
		return FFAT_ENOSUPPORT;
	}

	// check volume and child
	IF_UK ((NODE_VOL(pNodeSrcParent)->wTimeStamp != NODE_TS(pNodeSrcParent)) ||
			(NODE_VOL(pNodeSrcParent)->wTimeStamp != NODE_TS(pNodeDesParent)) ||
			(NODE_VOL(pNodeSrcParent)->wTimeStamp != NODE_TS(pNodeSrc)))
	{
		FFAT_LOG_PRINTF((_T("an ejected file or parent directory")));
		return FFAT_EXDEV;
	}

	// Parent가 directory 인지 검사
	IF_UK ((NODE_IS_DIR(pNodeSrcParent) != FFAT_TRUE) ||
			(NODE_IS_DIR(pNodeDesParent) != FFAT_TRUE))
	{
		FFAT_LOG_PRINTF((_T("parent node is not a DIR")));
		return FFAT_ENOTDIR;
	}

	if (NODE_IS_VALID(pNodeDes) == FFAT_TRUE)
	{
		// destination이 valid node 일경우 time stamp를 비교한다.
		IF_UK (NODE_TS(pNodeDesParent) != NODE_TS(pNodeDes))
		{
			FFAT_LOG_PRINTF((_T("an ejected destination node")));
			return FFAT_EXDEV;
		}
	}

	if (VOL_IS_RDONLY(NODE_VOL(pNodeSrcParent)) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("This is a read only volume")));
		return FFAT_EROFS;
	}

	return FFAT_OK;
}


/**
 * get name of a node
 *
 * pNode의 stDeInfo 로 부터 node의 이름과 길이를 구한다.
 *
 * @param		pNode	: [IN] node pointer
 * @param		psName	: [OUT] node name storage
 * @param		pdwLen	: [OUT] node name length (character count)
 * @param		pCxt	: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-30-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_getNodeName(Node* pNode, t_wchar* psName, t_int32* pdwLen, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	FatGetNodeDe	stNodeDE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psName);

	FFAT_ASSERT(pNode->stDeInfo.dwDeCount > 0);

	pVol		= NODE_VOL(pNode);

	stNodeDE.pDE = (FatDeSFN*) FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(stNodeDE.pDE != NULL);

	stNodeDE.dwCluster			= NODE_COP(pNode);
	stNodeDE.dwOffset			= pNode->stDeInfo.dwDeStartOffset;
	stNodeDE.dwClusterOfOffset	= pNode->stDeInfo.dwDeStartCluster;
	stNodeDE.dwTargetEntryCount	= 0;
	stNodeDE.psName				= NULL;
	stNodeDE.bExactOffset		= FFAT_TRUE;

	r = ffat_dir_getDirEntry(pVol, NULL, &stNodeDE, FFAT_TRUE, FFAT_TRUE, pCxt);
	FFAT_ASSERT(r == FFAT_OK);
	FFAT_EO(r, (_T("fail to get node ")));

	FFAT_ASSERT(stNodeDE.dwDeStartOffset == pNode->stDeInfo.dwDeStartOffset);
	FFAT_ASSERT(stNodeDE.dwDeEndOffset == pNode->stDeInfo.dwDeEndOffset);
	FFAT_ASSERT(stNodeDE.dwTotalEntryCount == pNode->stDeInfo.dwDeCount);
	// LFN only일 경우 SFN만 리턴될 수 없다.
	FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (stNodeDE.dwEntryCount > 1));

	// get file name
#ifdef FFAT_VFAT_SUPPORT
		r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), stNodeDE.pDE, stNodeDE.dwEntryCount,
						psName, pdwLen, FAT_GEN_NAME_LFN);
#else
		r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), stNodeDE.pDE, stNodeDE.dwEntryCount,
					psName, pdwLen, FAT_GEN_NAME_SFN);
#endif
	FFAT_EO(r, (_T("fail to generate name from DE")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stNodeDE.pDE, VOL_MSD(pVol), pCxt);

	return r;
}


/**
* this function is a sub function for ffat_node_rename to update Directory Entries
*
* @param		pNodeSrcParent	: [IN] parent node of Source
* @param		pNodeSrc		: [IN] source node
* @param		pNodeDesParent	: [IN] parent node of destination(target)
* @param		pNodeNew		: [IN/OUT] Newly created node after rename
* @param		psName			: [IN] target node name
* @param		pdwClustersDE	: [IN] Clusters for DE writing
* @param		dwClusterCountDE	: [IN] cluster count in pdwClustersDE array
* @param		dwCacheFlag		: [IN] flag for cache operation
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo
* @version		10-DEC-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_renameUpdateDEs(Node* pNodeSrcParent, Node* pNodeSrc, Node* pNodeDesParent, Node* pNodeNew,
				t_wchar* psName, t_uint32* pdwClustersDE, t_int32 dwClusterCountDE,
				FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	FatDeSFN*		pDE = NULL;			// directory entry pointer
	t_int32			dwLFNE_Count = 0;					// log file name entry count
	t_uint8			bCheckSum = 0;
	Vol*			pVol;

	FFAT_ASSERT(pNodeSrcParent);
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDesParent);
	FFAT_ASSERT(pNodeNew);
	FFAT_ASSERT(pdwClustersDE);
	FFAT_ASSERT(dwClusterCountDE > 0);

	pVol = NODE_VOL(pNodeSrcParent);

	// write new destination node's directory entry
	// allocate memory for directory entry
	pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(pDE != NULL);

#ifdef FFAT_VFAT_SUPPORT
	if (pNodeNew->dwFlag & NODE_NAME_SFN)
	{
		dwLFNE_Count = 0;
	}
	else
	{
		bCheckSum = FFATFS_GetCheckSum(&pNodeNew->stDE);

		// generate directory entry
		r = FFATFS_GenLFNE(psName, pNodeNew->wNameLen, (FatDeLFN*)pDE, &dwLFNE_Count, bCheckSum);
		FFAT_EO(r, (_T("fail to generate long file name entry")));
	}
#else
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
	FFAT_ASSERT(pNodeNew->dwFlag & NODE_NAME_SFN);
	dwLFNE_Count = 0;
#endif

	// update directory entry
	ffat_addon_renameUpdateDE(pNodeSrc, pNodeNew, pDE, bCheckSum, pCxt);

	// copy SFN to pDE
	FFAT_MEMCPY(&pDE[dwLFNE_Count], &pNodeNew->stDE, sizeof(FatDeSFN));

	// update access date
	r = ffat_node_updateSFNE(pNodeNew, 0, 0, 0, FAT_UPDATE_DE_ATIME, FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to update Access Time")));

	// set Cluster and Offset for DE
	pNodeNew->stDeInfo.dwDeEndCluster		= pdwClustersDE[dwClusterCountDE - 1];
	pNodeNew->stDeInfo.dwDeClusterSFNE		= pNodeNew->stDeInfo.dwDeEndCluster;
	pNodeNew->stDeInfo.dwDeOffsetSFNE		= pNodeNew->stDeInfo.dwDeEndOffset;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_WRITE_NEW_DE_BEFORE);

	r = ffat_node_writeDEs(pNodeDesParent, pNodeNew, pDE, pNodeNew->stDeInfo.dwDeCount,
				pdwClustersDE, dwClusterCountDE, (dwCacheFlag | FFAT_CACHE_DATA_DE), pCxt);
	FFAT_EO(r, (_T("fail to write directory entries")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_WRITE_NEW_DE_AFTER);

	// nikke add
	if ((NODE_C(pNodeSrcParent) != NODE_C(pNodeDesParent)) && (NODE_IS_DIR(pNodeSrc) == FFAT_TRUE))
	{
		// update ".."
		FFAT_ASSERT((FFATFS_GetDeCluster(VOL_VI(pVol), &pNodeSrc->stDE)) > 0);

		r = ffat_readWritePartialCluster(pVol, NULL, NODE_C(pNodeSrc), 0x20,
						sizeof(FatDeSFN), (t_int8*)pDE, FFAT_TRUE,
						FFAT_CACHE_DATA_DE, pCxt);
		FFAT_EO(r, (_T("fail to read directory entry for \"..\"")));

		// debug begin
#ifdef FFAT_DEBUG
		FFAT_ASSERT((pDE->sName[0] == '.') && (pDE->sName[1] == '.'));
		FFAT_ASSERT(FFATFS_GetDeCluster(VOL_VI(pVol), pDE) ? 
					FFATFS_GetDeCluster(VOL_VI(pVol), pDE) == NODE_C(pNodeSrcParent) :
						VOL_IS_FAT32(pVol) ? (VOL_RC(pVol) == NODE_C(pNodeSrcParent)) :
							(FFATFS_FAT16_ROOT_CLUSTER == NODE_C(pNodeSrcParent)));
#endif
		// debug end

		// 0 means root DIR.
		if (NODE_C(pNodeDesParent) == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

			r = FFATFS_SetDeCluster(pDE, 0);
		}
		else
		{
			r = FFATFS_SetDeCluster(pDE, NODE_C(pNodeDesParent));
		}
		FFAT_EO(r, (_T("fail to update directory entries of src")));

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_UPDATE_DOTDOT_BEFORE);

		r = ffat_writeDEs(pVol, 0, FFATFS_GetDeCluster(VOL_VI(pVol), &pNodeSrc->stDE),
						0x20, (t_int8*)pDE, sizeof(FatDeSFN),
						(dwCacheFlag | (FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC)), pNodeSrc, pCxt);
		FFAT_EO(r, (_T("fail to update directory entries of src")));

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_RENAME_UPDATE_DOTDOT_AFTER);
	}
	// nikke add end

out:
	FFAT_LOCAL_FREE(pDE, VOL_MSD(pVol), pCxt);
	return r;
}


/**
* delete directory entries for a node
*
* @param		pVol			: [IN] volume pointer
* @param		dwFlag			: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		NOV-19-2007 [DongYoung Seo] First Writing,
*/
static FFatErr
_deleteDE(Node* pNode, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFAT_DEBUG_NODE_PRINTF((_T("DeleteDE of Node/Cluster/Offset/Count:0x%X/0x%X/0x%X/0x%X/%d\n"),
							(t_uint32)pNode, NODE_COP(pNode), pNode->stDeInfo.dwDeStartOffset, pNode->stDeInfo.dwDeCount));

	return ffat_deleteDEs(NODE_VOL(pNode), NODE_COP(pNode), 
					pNode->stDeInfo.dwDeStartOffset, pNode->stDeInfo.dwDeStartCluster,
					pNode->stDeInfo.dwDeCount, FFAT_TRUE, dwCacheFlag, pNode, pCxt);
}


/**
* convert a positive integer to ASCII string
*
* @param		dwValue			: [IN] positive integer value
* @param		pStr			: [OUT] string storage
* @return		FFAT_OK			: success
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		DEC-20-2007 [DongYoung Seo] First Writing,
*/
static void
__Itoa(t_int32 dwValue, t_int8* pStr)
{
	t_int8*		p;			// pointer
	t_int8*		pFirst;		// first digit
	t_int8		bTemp;		// temporary variable
	t_uint32	dwDigit;	// a digit

	FFAT_ASSERT(dwValue > 0);
	FFAT_ASSERT(pStr);

	pFirst = p = pStr;

	do 
	{
		dwDigit = dwValue % 10;
		dwValue /= 10;
		*p++ = (t_int8) (dwDigit + '0');
	} while (dwValue > 0);

	*p-- = '\0';

	do 
	{
		bTemp	= *p;
		*p		= *pFirst;
		*pFirst	= bTemp;
		p--;
		pFirst++;
	} while (pFirst < p);
}


#ifdef FFAT_CMP_NAME_MULTIBYTE
	/**
	* This is a sub function for lookup operation (_lookupForCreate() )
	* This function is used when user want to compare string with multi-byte (FFAT_CMP_NAME_MULTIBYTE)
	* This function allocate memory for multi-byte converted upper string
	*	when *ppsUpperNameMB is NULL
	*	and convert psUpperName and string to multi-byte 
	*
	* @param		pNodeChild			: [IN] child node pointer
	* @param		psCurName			: [IN] current name
	* @param		ppsUpperNameMB		: [IN/out] converted (or will be converted) name to Multi-Byte
	* @param		psUpperName			: [IN] original upper case name(name to lookup)
	* @param		dwNameLen			: [IN] length of upper name == length of psCurName
	* @param		pCxt				: [IN] current context
	* @param		bFree				: [IN] FFAT_TRUE : free allocated memory and return
	* @return		0					: both strings are equal
	* @return		else				: both strings are NOT equal
	* @author		DongYoung Seo (dy76.seo@samsung.com)
	* @version		MAY-12-2009 [DongYoung Seo] First Writing,
	* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
	*											- Case sensitive
	*/
	static t_int32
	_lookupForCreateCompareMultiByte(Node* pNodeChild, t_wchar* psCurName, char** ppsUpperNameMB,
					t_wchar* psUpperName, t_int32 dwNameLen, ComCxt* pCxt, t_boolean bFree)
	{
		char*		psCurNameMB = NULL;			// buffer pointer for Multi-byte string
		t_int32		r;

		FFAT_ASSERT(pNodeChild);
		FFAT_ASSERT(psCurName);
		FFAT_ASSERT(ppsUpperNameMB);
		FFAT_ASSERT(psUpperName);
		FFAT_ASSERT(pCxt);

		if (bFree == FFAT_TRUE)
		{
			// free allocated memory
			FFAT_LOCAL_FREE(*ppsUpperNameMB, ((FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);
			return FFAT_OK;
		}

		// compare name only when some character of the string is '_'
		if ((pNodeChild->dwFlag & NODE_NAME_UNDERSCORE) == 0)
		{
			return -1;			// return not same
		}

		if (*ppsUpperNameMB == NULL)
		{
			// this is the first time
			// allocate memory and convert string to multi-byte
			*ppsUpperNameMB = FFAT_LOCAL_ALLOC(((FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);
			FFAT_ASSERT(*ppsUpperNameMB);

			*ppsUpperNameMB[0] = '\0';	// to avoid incorrect comparison after failure of MCSTOMBS

			FFAT_WCSTOMBS(*ppsUpperNameMB, (FFAT_NAME_MAX_LENGTH + 1), psUpperName,
							dwNameLen, NODE_VOL(pNodeChild));
		}

		psCurNameMB = FFAT_LOCAL_ALLOC(((FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);
		FFAT_ASSERT(psCurNameMB);

		psCurNameMB[0] = '\0';			// to avoid incorrect comparison after failure of MCSTOMBS

		FFAT_WCSTOMBS(psCurNameMB, (FFAT_NAME_MAX_LENGTH + 1), psCurName,
						dwNameLen, NODE_VOL(pNodeChild));

		r = ((VOL_FLAG(NODE_VOL(pNodeChild)) & VOL_CASE_SENSITIVE) == 0)
			? (FFAT_STRICMP(psCurNameMB, *ppsUpperNameMB))
			: (FFAT_STRCMP(psCurNameMB, *ppsUpperNameMB));

		FFAT_LOCAL_FREE(psCurNameMB, ((FFAT_NAME_MAX_LENGTH + 1) * sizeof(t_wchar)), pCxt);

		return r;
	}


	/**
	* lookup node with comparing name multi-byte
	* (not for creating)
	*
	* @param		pNodeParent	: [IN] parent node pointer
	* @param		psUpperName	: [IN] node uppercase name to lookup
	* @param		dwNameLen	: [IN] hint offset
	* @param		pNode		: [IN/OUT] node pointer.
	*								free directory entry 정보가 저장됨.
	* @param		pNodeDE		: [IN/OUT] node directory entry storage
	* @param		dwLookupFlag: [IN] flag for lookup
	* @param		pCxt		: [IN] context of current operation
	* @author		DongYoung Seo
	* @version		MAY-12-200I [DongYoung Seo] First Writing
	*/
	static FFatErr
	_lookupCmpNameMultiByte(Node* pNodeParent, t_wchar* psUpperName, t_int32 dwNameLen,
					Node* pNodeChild, FatGetNodeDe* pNodeDE, FFatLookupFlag dwLookupFlag,
					ComCxt* pCxt)
	{
		FFatErr			r;

		FFAT_ASSERT(pNodeParent);
		FFAT_ASSERT(psUpperName);
		FFAT_ASSERT(dwNameLen > 0);
		FFAT_ASSERT(pNodeChild);
		FFAT_ASSERT(pNodeDE);
		FFAT_ASSERT((dwLookupFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) == 0);

		// check under score
		if ((pNodeChild->dwFlag & NODE_NAME_UNDERSCORE) == 0)
		{
			pNodeDE->psName				= psUpperName;
			pNodeDE->dwNameLen			= dwNameLen;
			pNodeDE->psShortName			= pNodeChild->stDE.sName;
			pNodeDE->bExactOffset		= FFAT_FALSE;
			pNodeDE->dwClusterOfOffset	= 0;

			// get directory entry
			r = ffat_dir_getDirEntry(NODE_VOL(pNodeParent), pNodeParent,
								pNodeDE, FFAT_TRUE, FFAT_TRUE, pCxt);
			if (r < 0)
			{
				// Node를 찾지 못했거나 에러 발생.
				if (r == FFAT_EEOF)
				{
					r = FFAT_ENOENT;
				}
				else if (r == FFAT_EXDE)
				{
					r = FFAT_EFAT;
				}
			}

			// LFN only일 경우 SFN만 리턴될 수 없다.
			FFAT_ASSERT(((r < 0) || ((VOL_FLAG(NODE_VOL(pNodeParent)) & VOL_LFN_ONLY) == 0)) ? FFAT_TRUE : (pNodeDE->dwEntryCount > 1));
		}
		else
		{
			// use lookup for create withount numerictail and free DE checking
			pNodeChild->dwFlag &= ~NODE_NAME_NUMERIC_TAIL;

			pNodeChild->stDeInfo.dwFreeCount	= 1;
			pNodeChild->stDeInfo.dwFreeCluster	= 0;
			pNodeChild->stDeInfo.dwFreeOffset	= 0;

			r = _lookupForCreate(pNodeParent, psUpperName, dwNameLen,
							pNodeChild, pNodeDE, dwLookupFlag, NULL, pCxt);
		}

		return r;
	}

#endif		// end of #ifdef FFAT_CMP_NAME_MULTIBYTE

