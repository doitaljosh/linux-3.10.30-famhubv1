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
 * @file		ffat_addon_spfile.c
 * @brief		Special File
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First writing
 * @version		JUN-17-2009 [GwangOk Go] rename symlink into spfile
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"
#include "ffat_node.h"
#include "ffat_vol.h"
#include "ffat_main.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffat_addon_api.h"
#include "ffat_addon_spfile.h"
#include "ffat_addon_log.h"
#include "ffat_addon_xde.h"
#include "ffat_addon_types_internal.h"

#include "ffatfs_api.h"

// defines
#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_SYMLINK)

#define _SYMLINK_CRT_TIME_TENTH		0xD7
#define _SYMLINK_SIG_1				0xDFA78699
#define _SYMLINK_SIG_2				0x83B18ACD
#define _SYMLINK_SIG_1_LEN			4
#define _SYMLINK_SIG_2_LEN			4
#define _SYMLINK_CHECK_SUM_LEN		4
#define _SYMLINK_TIME_TENTH_LEN		1
#define _SYMLINK_PADDING_LEN		1
#define _SYMLINK_PATH_LEN_LEN		2
#define _SYMLINK_INFO_LEN			(sizeof(SymlinkInfo1) + sizeof(SymlinkInfo2))
#define _SYMLINK_PATH_POS			sizeof(SymlinkInfo1)
#define _SYMLINK_MAX_PATH_LEN		FFAT_SYMLINK_MAX_PATH_LEN

#define _FIFO_CRT_TIME_TENTH		0xE8
#define _SOCKET_CRT_TIME_TENTH		0xF9


//!< get byte aligned size of symlink
#define _SYMLINK_BYTE_ALIGN				4
#define _SYMLINK_BYTE_ALIGN_MASK		(_SYMLINK_BYTE_ALIGN - 1)
#define _SYMLINK_BYTE_ALIGN_SIZE(_size)	(((_size) + _SYMLINK_BYTE_ALIGN_MASK) & (~_SYMLINK_BYTE_ALIGN_MASK))

// typedefs
typedef struct _SymlinkInfo1
{
	t_int32		dwSignature1;
	t_uint8		bCrtTimeTenth;
	t_uint8		bPadding;
	t_uint16	wPathLen;		// length of wPathlen, character count
} SymlinkInfo1;

typedef struct _SymlinkInfo2
{
	t_uint32	dwCheckSum;
	t_int32		dwSignature2;
} SymlinkInfo2;

// function declarations
static FFatErr	_constructSymlinkInfo(Vol* pVol, t_wchar* psPath, t_int32 dwPathLen, FatDeSFN* pDE, t_int8* pBuff);
static FFatErr	_getPathAndTimeTenth(Node* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen, 
									t_uint8* pbCrtTimeTenth, ComCxt* pCxt);
static t_uint32	_getTargetPathCheckSum(t_wchar* psPath, t_int32 dwPathLen);
static FFatErr	_writeCrtTimeTenth(Node* pNode, t_uint8 bCrtTimeTenth, ComCxt* pCxt);

static FFatErr	_isSymlink(Node* pNode, t_uint8* pbCrtTimeTenth, t_boolean* pbSymlink, ComCxt* pCxt);


/**
 * initialize Special File
 *
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-07-2007 [GwangOk Go] First Writing.
 * @version		JUN-17-2009 [GwangOk Go] rename symlink into spfile 
 */
FFatErr
ffat_spfile_init(void)
{
	FFAT_ASSERT(_SYMLINK_MAX_PATH_LEN <= 4096);		// linux maximum path length is 4K
	
	return FFAT_OK;
}


/**
 * if mount flag includes FFAT_MOUNT_SPECIAL_FILES, add VOL_ADDON_SPECIAL_FILES to volume flag
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwFlag		: [IN] mount flag
 * @return		void
 * @author		GwangOk Go
 * @version		AUG-14-2008 [GwangOk Go] First Writing
 * @version		JUN-17-2009 [GwangOk Go] rename symlink into spfile
 */
void
ffat_spfile_mount(Vol* pVol, FFatMountFlag dwFlag)
{
	FFAT_ASSERT(pVol);

	if (dwFlag & FFAT_MOUNT_SPECIAL_FILES)
	{
		VOL_FLAG(pVol) |= VOL_ADDON_SPECIAL_FILES;
	}

	return;
}


/**
 * create symlink node & write symlink info
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNodeChild	: [IN] child node pointer
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
 * @version		OCT-12-2008 [DongYoung Seo] initialize stVC before first error checking.
 *								because "out:" routine check members of stVC
 * @version		OCT-12-2008 [DongYoung Seo] change core unlock error checking routine
 *								it makes infinite loop.
 *								do not use FFAT_EO, to check fail - it is already in the "out" routine
 * @version		OCT-31-2008 [DongYoung Seo] do not unlock core on out routine when error occurred before core locking
 * @version		JAN-06-2009 [DongYoung Seo] add updating code for offset for SFNE 
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		JUL-31-2009 [JeongWoo Park] Add consideration of 4byte align of symlink structure
 *											the mis-aligned address of structure can make compile error with some platform.
 */
FFatErr
ffat_spfile_createSymlink(Node* pNodeParent, Node* pNodeChild, t_wchar* psName,
						t_wchar* psPath, FFatCreateFlag dwFlag, void* pAddonInfo, ComCxt* pCxt)
{
	FFatErr		r;
	Vol*		pVol;
	FatDeSFN*	pDE = NULL;
	t_int32		dwLFNE_Count;								// LFN Entry Count
	t_uint8		bCheckSum = 0;
	t_int32		dwClusterCountDE;							// cluster count for directory entry write
	t_uint32	pdwClustersDE[NODE_MAX_CLUSTER_FOR_CREATE];	// clusters for directory entry write

	t_int8*		pBuff = NULL;				// buffer for symlink info
	t_int32		dwPathLen;					// target path length (character count)
	t_uint32	dwSymInfoLen;				// byte length of symlink info
	t_uint32	dwSymInfoLenDone;			// byte length of symlink info
	t_uint32	dwInfoClusterCount;			// cluster count for symlink info
	t_uint32	dwFreeClusterCount;
	t_boolean	bCoreLocked = FFAT_FALSE;	// boolean to check core lock or unlock checking
	FFatVC		stVC;

	// for log recovery
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;

	// check parameter
	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT((dwFlag & FFAT_CREATE_ATTR_DIR) == 0);
	FFAT_ASSERT(NODE_IS_VALID(pNodeChild) == FFAT_FALSE);

	pVol = NODE_VOL(pNodeParent);
	FFAT_ASSERT(pVol);

	if ((VOL_FLAG(pVol) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return FFAT_ENOSUPPORT;
	}

	stVC.pVCE = NULL;
	VC_INIT(&stVC, VC_NO_OFFSET);

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

#if 0	// not used
	if (NODE_IS_VALID(pNodeChild) == FFAT_TRUE)
	{
		// valid node일 경우 node가 존재함을 의미
		FFAT_LOG_PRINTF((_T("already exist node")));
		r = FFAT_EEXIST;
		goto out;
	}
#endif

	// allocate memory for directory entry
	pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(pDE);

#ifdef FFAT_VFAT_SUPPORT
	if (pNodeChild->dwFlag & NODE_NAME_SFN)
	{
		dwLFNE_Count = 0;
	}
	else
	{
		// max name length check
		if (pNodeChild->wNameLen > FFAT_FILE_NAME_MAX_LENGTH)
		{
			FFAT_LOG_PRINTF((_T("Too long name for FILE")));
			r = FFAT_ETOOLONG;
			goto out;
		}

		bCheckSum = FFATFS_GetCheckSum(&pNodeChild->stDE);

		// generate directory entry
		r = FFATFS_GenLFNE(psName, pNodeChild->wNameLen, (FatDeLFN*)pDE, &dwLFNE_Count, bCheckSum);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to generate long file name entry")));
			goto out;
		}
	}
#else
	FFAT_ASSERT(pNodeChild->dwFlag & NODE_NAME_SFN);
	dwLFNE_Count = 0;
#endif

	FFAT_ASSERT((VOL_FLAG(pVol) & VOL_ADDON_XDE) ? (pNodeChild->stDeInfo.dwDeCount == dwLFNE_Count + 2) : (pNodeChild->stDeInfo.dwDeCount == dwLFNE_Count + 1));

	ffat_xde_create(pNodeChild, pDE, bCheckSum, (XDEInfo*)pAddonInfo);		// void return

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_EXPAND_PARENT_BEFORE);

	// lock ADDON for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock CORE")));

	bCoreLocked = FFAT_TRUE;

	// expand parent directory to get clusters for directory entry write when there is not enough space
	r = ffat_node_createExpandParent(pNodeParent, pNodeChild, &dwClusterCountDE,
							pdwClustersDE, NODE_MAX_CLUSTER_FOR_CREATE, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to expand parent directory")));
		goto out;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_EXPAND_PARENT_AFTER);

	// check target path length
	dwPathLen = FFAT_WCSLEN(psPath);
	if (dwPathLen > _SYMLINK_MAX_PATH_LEN)
	{
		FFAT_LOG_PRINTF((_T("target path is too long")));
		r = FFAT_EINVALID;
		goto out;
	}

	// calculate byte length of symlink file
	dwSymInfoLen = _SYMLINK_BYTE_ALIGN_SIZE((dwPathLen + 1) * sizeof(t_wchar)) + _SYMLINK_INFO_LEN;

	// calculate cluster count for symlink info
	dwInfoClusterCount = ESS_MATH_CDB(dwSymInfoLen, VOL_CS(pVol), VOL_CSB(pVol));

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	// set VC parameter
	stVC.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
	// other members of stVC is already initialize at the being of this function.

	// get free clusters
	r = ffat_misc_getFreeClusters(pNodeChild, dwInfoClusterCount, &stVC, 0,
						&dwFreeClusterCount, FAT_ALLOCATE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to get free clusters")));

	FFAT_ASSERT(dwInfoClusterCount == dwFreeClusterCount);
	FFAT_ASSERT(dwFreeClusterCount == VC_CC(&stVC));

	// set offset. this is the first cluster .
	stVC.dwClusterOffset = 0;

	// update SFNE (no write)
	r = ffat_node_updateSFNE(pNodeChild, 0, (t_uint8)(dwFlag & FFAT_CREATE_ATTR_MASK),
							VC_FC(&stVC), FAT_UPDATE_DE_ALL, dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to update SFNE")));

	// allocate memory for Symlink Info
	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(dwSymInfoLen, pCxt);
	FFAT_ASSERT(pBuff);

	// construct symlink info
	_constructSymlinkInfo(pVol, psPath, dwPathLen, &pNodeChild->stDE, pBuff);

	pNodeChild->stDE.bCrtTimeTenth = _SYMLINK_CRT_TIME_TENTH;

	pNodeChild->stDE.dwFileSize = dwSymInfoLen;

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_CREATE_SYMLINK_LOG_BEFORE);

	if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag		|= FFAT_CACHE_SYNC;
	}

	// write log for createSymlink
	r = ffat_log_createSymlink(pNodeChild, psName, &stVC, &dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to log createSymlink")));
		goto out_after_create;
	}

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_CREATE_SYMLINK_LOG_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_ALLOCATE_CLUSTER_BEFORE);

	r = ffat_misc_makeClusterChainVC(pNodeChild, 0, &stVC, FAT_UPDATE_NONE,
						(dwCacheFlag | FFAT_CACHE_DATA_FAT), pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to make cluster chain")));
		goto out_after_create;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_ALLOCATE_CLUSTER_AFTER);

	// copy SFN to pDE
	FFAT_MEMCPY(&pDE[dwLFNE_Count], &pNodeChild->stDE, sizeof(FatDeSFN));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_WRITE_SYMLINK_INFO_BEFORE);

	// write symlink info
	r = ffat_node_readWriteInit(pNodeChild, 0, pBuff, dwSymInfoLen, &stVC, &dwSymInfoLenDone,
								(FFAT_CACHE_SYNC | FFAT_CACHE_DATA_FS), FFAT_RW_WRITE, pCxt);
	IF_UK ((r < 0) || (dwSymInfoLenDone != dwSymInfoLen))
	{
		FFAT_LOG_PRINTF((_T("Fail to write symlink info")));
		r = FFAT_EIO;
		goto out_dealloc_fat;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_WRITE_SYMLINK_INFO_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_WRITE_DE_BEFORE);

	FFAT_ASSERT(ffat_share_checkDE(pNodeChild, psName, pDE, pCxt) == FFAT_OK);

	// set Cluster and Offset for DE
	pNodeChild->stDeInfo.dwDeEndCluster		= pdwClustersDE[dwClusterCountDE - 1];
	pNodeChild->stDeInfo.dwDeEndOffset		= pNodeChild->stDeInfo.dwDeStartOffset 
								+ ((pNodeChild->stDeInfo.dwDeCount - 1) << FAT_DE_SIZE_BITS);
	pNodeChild->stDeInfo.dwDeClusterSFNE	= pNodeChild->stDeInfo.dwDeEndCluster;
	pNodeChild->stDeInfo.dwDeOffsetSFNE		= pNodeChild->stDeInfo.dwDeEndOffset;

	// write directory entries
	r = ffat_node_writeDEs(pNodeParent, pNodeChild, pDE, pNodeChild->stDeInfo.dwDeCount,
						pdwClustersDE, dwClusterCountDE, (dwCacheFlag | FFAT_CACHE_DATA_DE),
						pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write directory entries")));
		goto out_dealloc_fat;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_SYMLINK_WRITE_DE_AFTER);

	NODE_S(pNodeChild) = dwSymInfoLen;
	NODE_C(pNodeChild) = VC_FC(&stVC);

	// set valid
	NODE_SET_VALID(pNodeChild);

	// set symlink flag
	NODE_ADDON_FLAG(pNodeChild) |= ADDON_NODE_SYMLINK;

out_dealloc_fat:
	IF_UK (r < 0)
	{
		t_uint32		dwDeallocCount;
		FatAllocate		stAlloc;

		stAlloc.dwCount = VC_CC(&stVC);
		stAlloc.dwHintCluster = 0;
		stAlloc.dwPrevEOF = 0;
		stAlloc.dwFirstCluster = VC_FC(&stVC);
		stAlloc.dwLastCluster = VC_LC(&stVC);
		stAlloc.pVC = &stVC;

		FFATFS_DeallocateCluster(VOL_VI(pVol), dwInfoClusterCount, &stAlloc, &dwDeallocCount,
								NULL, FAT_ALLOCATE_FORCE, 
								(dwCacheFlag | FFAT_CACHE_DATA_FAT | FFAT_CACHE_FORCE),
								pNodeParent, pCxt);

		VC_VEC(&stVC) = 0;	// deallocateCluster에서 add free cluster를 수행 -> add free cluster할 필요없음
	}

out_after_create:
	r |= ffat_addon_afterCreate(pNodeParent, pNodeChild, psName,
						pdwClustersDE, dwClusterCountDE, FFAT_IS_SUCCESS(r), pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to after operation for create")));
	}

	FFAT_LOCAL_FREE(pBuff, dwSymInfoLen, pCxt);
	
out:
	IF_UK (r < 0)
	{
		if (VC_VEC(&stVC) != 0)
		{
			// add free cluster to FCC
			r |= ffat_fcc_addFreeClustersVC(pVol, &stVC, pCxt);
		}
	}

	if (bCoreLocked == FFAT_TRUE)
	{
		// unlock ADDON for free cluster sync
		r |= ffat_core_unlock(pCxt);
	}

	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_LOCAL_FREE(pDE, VOL_MSD(pVol), pCxt);

	return r;
}


/**
 * read symlink info & get target path / CrtTimeTenth
 *
 * @param		pNode			: [IN] node pointer
 * @param		psPath			: [OUT] target path
 * @param		dwLen			: [IN] length of psPath, in character count
 * @param		pdwLen			: [OUT] count of character stored at psPath
 * @param		pbCrtTimeTenth	: [IN/OUT] creation time tenth (may be NULL)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSUPPORT	: volume does not support symlink
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EINVALID	: this is not symbolic link
 * @return		FFAT_EIO		: io error while reading symlink info
 * @return		else			: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] add parameter CreatDate/Time.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		FEB-04-2009 [GwangOk Go] make _getPathAndTimeTenth()
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 */
FFatErr
ffat_spfile_readSymlink(Node* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen,
					t_uint8* pbCrtTimeTenth, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pdwLen);

	pVol = NODE_VOL(pNode);
	FFAT_ASSERT(pVol);

	if ((VOL_FLAG(pVol) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return FFAT_ENOSUPPORT;
	}

	VOL_INC_REFCOUNT(pVol);

	// check time stamp between volume and node
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	IF_UK (SYMLINK_IS_SYMLINK(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("this is not symbolic link")));
		r = FFAT_EINVALID;
		goto out;
	}

	// check node size
	FFAT_ASSERT(NODE_S(pNode) > 0);

	// get target path & CrtTimeTenth
	r = _getPathAndTimeTenth(pNode, psPath, dwLen, pdwLen, pbCrtTimeTenth, pCxt);
	IF_UK (r == FFAT_OK1)
	{
		// symlink가 아닌 경우
		r = FFAT_EINVALID;
	}

out:
	VOL_DEC_REFCOUNT(pVol);

	return r;
}


/**
 * create special file (except for symlink)
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwCreateFlag	: [IN] create flag
 * @return		void
 * @author		GwangOk Go
 * @version		JUN-17-2009 [GwangOk Go] First Writing.
 */
void
ffat_spfile_create(Node* pNode, FFatCreateFlag dwCreateFlag)
{
	FFAT_ASSERT(pNode);

	if ((VOL_FLAG(NODE_VOL(pNode)) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return;
	}

	if (dwCreateFlag & FFAT_CREATE_FIFO)
	{
		FFAT_ASSERT((dwCreateFlag & FFAT_CREATE_ATTR_DIR) == 0);

		// set fifo mark on creation time tenth
		pNode->stDE.bCrtTimeTenth = _FIFO_CRT_TIME_TENTH;

		// set fifo flag
		NODE_ADDON_FLAG(pNode) |= ADDON_NODE_FIFO;
	}
	else if (dwCreateFlag & FFAT_CREATE_SOCKET)
	{
		FFAT_ASSERT((dwCreateFlag & FFAT_CREATE_ATTR_DIR) == 0);

		// set socket mark on creation time tenth
		pNode->stDE.bCrtTimeTenth = _SOCKET_CRT_TIME_TENTH;

		// set socket flag
		NODE_ADDON_FLAG(pNode) |= ADDON_NODE_SOCKET;
	}

	return;
}


/**
 * after lookup and readdir, if node is special file (symlink, fifo, socket),
 * set flag & (in case of symlink) get CrtTimeTenth
 *
 * @param		pNode		: [IN] node pointer
 * @param		pbCrtTimeTenth	: [IN/OUT] creation time tenth (may be NULL)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		FEB-04-2009 [GwangOk Go] merge with ffat_symlink_getStatus()
 * @version		JUN-17-2009 [GwangOk Go] add fifo, socket
 */
FFatErr
ffat_spfile_afterLookup(Node* pNode, t_uint8* pbCrtTimeTenth, ComCxt* pCxt)
{
	FFatErr		r;
	t_boolean	bSymlink;

	FFAT_ASSERT(pNode);

	if ((VOL_FLAG(NODE_VOL(pNode)) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return FFAT_OK;
	}

	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		return FFAT_OK;
	}

	// check creation time tenth
	switch (pNode->stDE.bCrtTimeTenth)
	{
	case _SYMLINK_CRT_TIME_TENTH:
		r  = _isSymlink(pNode, pbCrtTimeTenth, &bSymlink, pCxt);
		FFAT_ER(r, (_T("fail to get free clusters")));

		if (bSymlink == FFAT_TRUE)
		{
			// set symlink flag
			NODE_ADDON_FLAG(pNode) |= ADDON_NODE_SYMLINK;
		}
		break;

	case _FIFO_CRT_TIME_TENTH:
		// set fifo flag
		NODE_ADDON_FLAG(pNode) |= ADDON_NODE_FIFO;
		break;

	case _SOCKET_CRT_TIME_TENTH:
		// set socket flag
		NODE_ADDON_FLAG(pNode) |= ADDON_NODE_SOCKET;
		break;
	}

	return FFAT_OK;
}


/**
 * in case of special file rename, set mark & flag 
 *
 * @param		pNodeSrc	: [IN] source node pointer
 * @param		pNodeDes	: [IN] destination node pointer
 * @return		void
 * @author		GwangOk Go
 * @version		DEC-07-2007 [GwangOk Go] First Writing.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		JUN-17-2009 [GwangOk Go] add fifo, socket
 */
void
ffat_spfile_rename(Node* pNodeSrc, Node* pNodeDes)
{
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDes);

	if ((VOL_FLAG(NODE_VOL(pNodeSrc)) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return;
	}

	switch (NODE_ADDON_FLAG(pNodeSrc) & ADDON_NODE_SPECIAL_FILES)
	{
	case ADDON_NODE_SYMLINK:

		FFAT_ASSERT(pNodeSrc->stDE.bCrtTimeTenth == _SYMLINK_CRT_TIME_TENTH);

		// set symlink mark on creation time tenth
		pNodeDes->stDE.bCrtTimeTenth = _SYMLINK_CRT_TIME_TENTH;

		// set symlink flag
		NODE_ADDON_FLAG(pNodeDes) |= ADDON_NODE_SYMLINK;
		break;

	case ADDON_NODE_FIFO:

		FFAT_ASSERT(pNodeSrc->stDE.bCrtTimeTenth == _FIFO_CRT_TIME_TENTH);

		// set fifo mark on creation time tenth
		pNodeDes->stDE.bCrtTimeTenth = _FIFO_CRT_TIME_TENTH;

		// set fifo flag
		NODE_ADDON_FLAG(pNodeDes) |= ADDON_NODE_FIFO;
		break;

	case ADDON_NODE_SOCKET:

		FFAT_ASSERT(pNodeSrc->stDE.bCrtTimeTenth == _SOCKET_CRT_TIME_TENTH);

		// set socket mark on creation time tenth
		pNodeDes->stDE.bCrtTimeTenth = _SOCKET_CRT_TIME_TENTH;

		// set socket flag
		NODE_ADDON_FLAG(pNodeDes) |= ADDON_NODE_SOCKET;
		break;
	}

	return;
}


/**
 * Before set status,
 * set special file flag on creation time of updated DE and
 * (in case of symlink) write creation time tenth in symlink info
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [INOUT] status pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-11-2007 [GwangOk Go] First Writing.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		JUN-17-2009 [GwangOk Go] add fifo, socket
 */
FFatErr
ffat_spfile_setStatus(Node* pNode, FFatNodeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	if ((VOL_FLAG(NODE_VOL(pNode)) & VOL_ADDON_SPECIAL_FILES) == 0)
	{
		// volume이 special file을 지원하지 않는 경우
		return FFAT_OK;
	}

	switch (NODE_ADDON_FLAG(pNode) & ADDON_NODE_SPECIAL_FILES)
	{
	case ADDON_NODE_SYMLINK:
		
		FFAT_ASSERT(pNode->stDE.bCrtTimeTenth == _SYMLINK_CRT_TIME_TENTH);

// debug begin
#ifdef FFAT_DEBUG
		{
			t_boolean	bSymlink;
			r = _isSymlink(pNode, NULL, &bSymlink, pCxt);
			if (r == FFAT_OK)
			{
				FFAT_ASSERT(bSymlink == FFAT_TRUE);
			}	
		}
#endif
// debug end

		// write created time tenth
		r = _writeCrtTimeTenth(pNode, (t_uint8)pStatus->dwCTimeTenth, pCxt);
		IF_LK (r == FFAT_OK)
		{
			// update pStatus for Core update SFNE
			pStatus->dwCTimeTenth = _SYMLINK_CRT_TIME_TENTH;
		}
		break;

	case ADDON_NODE_FIFO:

		FFAT_ASSERT(pNode->stDE.bCrtTimeTenth == _FIFO_CRT_TIME_TENTH);

		// set fifo mark on creation time tenth
		pStatus->dwCTimeTenth = _FIFO_CRT_TIME_TENTH;
		break;

	case ADDON_NODE_SOCKET:

		FFAT_ASSERT(pNode->stDE.bCrtTimeTenth == _SOCKET_CRT_TIME_TENTH);

		// set socket mark on creation time tenth
		pStatus->dwCTimeTenth = _SOCKET_CRT_TIME_TENTH;
		break;
	}

	return r;
}


//=============================================================================
//
//	static function
//


/**
 * construct symlink info
 *
 * @param		pVol		: [IN] volume pointer
 * @param		psPath		: [IN] target path
 * @param		dwPathLen	: [IN] target path length
 * @param		pDE			: [IN] directory entry
 * @param		pBuff		: [OUT] symlink info
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		JUL-31-2009 [JeongWoo Park] Add consideration of 4byte align of symlink structure
 *											the mis-aligned address of structure can make compile error with some platform.
 */
static FFatErr
_constructSymlinkInfo(Vol* pVol, t_wchar* psPath, t_int32 dwPathLen, FatDeSFN* pDE, t_int8* pBuff)
{
	t_int32			dwPathUniLen;
	t_int8*			pTemp1;

	SymlinkInfo1*	pSymlinkInfo1;
	SymlinkInfo2*	pSymlinkInfo2;

#ifdef FFAT_BIG_ENDIAN
	t_int32			dwCnt;
	t_wchar*		pTemp2;
#endif

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT((dwPathLen >= 0) && (dwPathLen <= _SYMLINK_MAX_PATH_LEN));
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pBuff);

	pSymlinkInfo1 = (SymlinkInfo1*)pBuff;

	pSymlinkInfo1->dwSignature1 = FFAT_BO_INT32(_SYMLINK_SIG_1);
	pSymlinkInfo1->bCrtTimeTenth = pDE->bCrtTimeTenth;
	pSymlinkInfo1->bPadding = 0x0;
	pSymlinkInfo1->wPathLen = FFAT_BO_UINT16((t_uint16)dwPathLen);

	pTemp1 = pBuff + _SYMLINK_PATH_POS;

	dwPathUniLen = dwPathLen * sizeof(t_wchar);

	// copy target path
	FFAT_MEMCPY(pTemp1, (t_int8*)psPath, dwPathUniLen);
	FFAT_MEMSET((pTemp1 + dwPathUniLen), 0x00, sizeof(t_wchar));

#ifdef FFAT_BIG_ENDIAN
	pTemp2 = (t_wchar*)pTemp1;

	for (dwCnt = 0; dwCnt < dwPathLen; dwCnt++)
	{
		pTemp2[dwCnt] = FFAT_BO_UINT16(pTemp2[dwCnt]);
	}
#endif

	pSymlinkInfo2 = (SymlinkInfo2*)(pTemp1 + _SYMLINK_BYTE_ALIGN_SIZE(dwPathUniLen + sizeof(t_wchar)));

	pSymlinkInfo2->dwCheckSum = FFAT_BO_UINT32(_getTargetPathCheckSum(psPath, dwPathLen));
	pSymlinkInfo2->dwSignature2 = FFAT_BO_INT32(_SYMLINK_SIG_2);

	return FFAT_OK;
}


/**
 * read symlink info & get target path / CrtTimeTenth
 *
 * @param		pNode			: [IN] node pointer
 * @param		psPath			: [OUT] target path
 * @param		dwLen			: [IN] length of psPath, in character count
 * @param		pdwLen			: [OUT] count of character stored at psPath
 * @param		pbCrtTimeTenth	: [IN/OUT] creation time tenth (may be NULL)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EIO		: io error while reading symlink info
 * @return		FFAT_OK1		: this is not symbolic link
 * @return		FFAT_ENOEME		: psPath is too small for symlink storage
 * @return		else			: error
 * @author		GwangOk Go
 * @version		FEB-04-2009 [GwangOk Go] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] add parameter CreatDate/Time.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		FEB-04-2009 [GwangOk Go] make _getPathAndTimeTenth()
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 * @version		JUL-31-2009 [JeongWoo Park] Add consideration of 4byte align of symlink structure
 *											the mis-aligned address of structure can make compile error with some platform.
 */
FFatErr
_getPathAndTimeTenth(Node* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen,
						t_uint8* pbCrtTimeTenth, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;
	t_int8*				pBuff = NULL;
	t_uint32			dwClusterCount;

	SymlinkInfo1*		pSymlinkInfo1;
	SymlinkInfo2*		pSymlinkInfo2;
	FFatVC				stVC;
	t_uint32			dwIOSize;

#ifdef FFAT_BIG_ENDIAN
	t_int32			dwCnt;
#endif

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pdwLen);

	pVol = NODE_VOL(pNode);
	FFAT_ASSERT(pVol);

	// check node size
	FFAT_ASSERT(NODE_S(pNode) > 0);

	// calculate cluster count for symlink info
	dwClusterCount = ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));

	// allocate memory for symlink Info
	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(NODE_S(pNode), pCxt);
	FFAT_ASSERT(pBuff);
	
	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
	stVC.dwValidEntryCount		= 0;
	stVC.dwTotalClusterCount	= 0;
	stVC.dwClusterOffset		= 0;

	FFAT_ASSERT(pNode->dwCluster != 0);

	// get vectored cluster information from offset 0
	r = ffat_misc_getVectoredCluster(pVol, pNode, NODE_C(pNode), 0, dwClusterCount,
						&stVC, NULL, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to get vectored cluster")));
		goto out;
	}

	// check cluster count
	FFAT_ASSERT(dwClusterCount <= VC_CC(&stVC));

	// read symlink info
	r = ffat_node_readWriteInit(pNode, 0, pBuff, NODE_S(pNode), &stVC, &dwIOSize,
						(FFAT_CACHE_SYNC | FFAT_CACHE_DATA_FS), FFAT_RW_READ, pCxt);
	IF_UK ((r < 0) || (NODE_S(pNode) != dwIOSize))
	{
		FFAT_LOG_PRINTF((_T("Fail to read symlink info")));
		r = FFAT_EIO;
		goto out;
	}

	pSymlinkInfo1 = (SymlinkInfo1*)pBuff;

	// check signature1
	if (FFAT_BO_INT32(pSymlinkInfo1->dwSignature1) != _SYMLINK_SIG_1)
	{
		FFAT_LOG_PRINTF((_T("Signature1 is wrong")));
		r = FFAT_OK1;
		goto out;
	}

	FFAT_ASSERT(pSymlinkInfo1->wPathLen <= _SYMLINK_MAX_PATH_LEN);

	*pdwLen = (t_int32)FFAT_BO_UINT16(pSymlinkInfo1->wPathLen);

	// check path length
	if (_SYMLINK_BYTE_ALIGN_SIZE((*pdwLen + 1) * sizeof(t_wchar)) != (t_int32)(NODE_S(pNode) - _SYMLINK_INFO_LEN))
	{
		FFAT_LOG_PRINTF((_T("Target path length is wrong")));
		r = FFAT_OK1;
		goto out;
	}

	pSymlinkInfo2 = (SymlinkInfo2*)(pBuff + NODE_S(pNode) - _SYMLINK_CHECK_SUM_LEN - _SYMLINK_SIG_2_LEN);

	// check signature2
	if (FFAT_BO_INT32(pSymlinkInfo2->dwSignature2) != _SYMLINK_SIG_2)
	{
		FFAT_LOG_PRINTF((_T("Signature2 is wrong")));
		r = FFAT_OK1;
		goto out;
	}

	if (*pdwLen > dwLen)
	{
		FFAT_PRINT_DEBUG((_T("Not enough memory")));
		r = FFAT_ENOMEM;
		goto out;
	}

	FFAT_MEMCPY((t_int8*)psPath, (pBuff + _SYMLINK_PATH_POS), (*pdwLen * sizeof(t_wchar)));
	if (dwLen > *pdwLen)
	{
		psPath[*pdwLen] = 0x00;		// add NULL for customer friendly
	}

#ifdef FFAT_BIG_ENDIAN
	for (dwCnt = 0; dwCnt < pSymlinkInfo1->wPathLen; dwCnt++)
	{
		psPath[dwCnt] = FFAT_BO_UINT16(psPath[dwCnt]);
	}
#endif

	// check checksum
	if (FFAT_BO_INT32(pSymlinkInfo2->dwCheckSum) != FFAT_BO_UINT32(_getTargetPathCheckSum(psPath, pSymlinkInfo1->wPathLen)))
	{
		FFAT_LOG_PRINTF((_T("Check sum is wrong")));
		r = FFAT_OK1;
		goto out;
	}

	// return Create time tenth
	if (pbCrtTimeTenth != NULL)
	{
		*pbCrtTimeTenth = pSymlinkInfo1->bCrtTimeTenth;
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_LOCAL_FREE(pBuff, NODE_S(pNode), pCxt);

	return r;
}


/**
 * get check sum for target path
 *
 * @param		psPath		: [IN] target path
 * @param		dwPathLen	: [IN] target path length
 * @return		t_uint32	: check sum for target path
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 */
t_uint32
_getTargetPathCheckSum(t_wchar* psPath, t_int32 dwPathLen)
{
	t_uint32	dwHashValue = 0;
	t_int32		dwCnt = 0;
	t_uint8*	pTemp = (t_uint8*)psPath;

	FFAT_ASSERT(psPath);
	FFAT_ASSERT(dwPathLen >= 0 && dwPathLen <= _SYMLINK_MAX_PATH_LEN);

	for (dwCnt = 0; dwCnt < dwPathLen; pTemp += 2, dwCnt++)
	{
		dwHashValue += *pTemp + (*(pTemp + 1) << 8);
		dwHashValue -= (dwHashValue << 13) | (dwHashValue >> 19);
	}

	return dwHashValue;
}


/**
 * write creation time tenth on symlink info
 *
 * @param		pNode			: [IN] node pointer
 * @param		bCrtTimeTenth	: [IN] creation time tenth
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EIO		: I/O error occur while writing CrtTimeTenth
 * @author		GwangOk Go
 * @version		DEC-11-2007 [GwangOk Go] First Writing.
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 */
FFatErr
_writeCrtTimeTenth(Node* pNode, t_uint8 bCrtTimeTenth, ComCxt* pCxt)
{
	FFatErr			r;
	FatVolInfo*		pVolInfo;

	t_int32			dwSector;

	FFAT_ASSERT(pNode);

	pVolInfo = NODE_VI(pNode);

	FFAT_ASSERT(pVolInfo);

	FFAT_ASSERT(FFATFS_IsValidCluster(pVolInfo, NODE_C(pNode)) == FFAT_TRUE);

	// get first sector of symlink info
	dwSector = FFATFS_GetFirstSectorOfCluster(pVolInfo, NODE_C(pNode));

	// write creation time tenth
	r = ffat_readWritePartialSector(NODE_VOL(pNode), pNode, dwSector,
						_SYMLINK_SIG_1_LEN, _SYMLINK_TIME_TENTH_LEN,
						(t_int8*)&bCrtTimeTenth, (FFAT_CACHE_SYNC | FFAT_CACHE_DATA_FS), FFAT_FALSE, pCxt);
	IF_UK (r != _SYMLINK_TIME_TENTH_LEN)
	{
		FFAT_LOG_PRINTF((_T("Fail to write creation time tenth on symlink info")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}


/**
 * check whether node is symlink
 * if node is symlink, get CrtTimeTenth
 *
 * @param		pNode		: [IN] node pointer
 * @param		pbCrtTimeTenth	: [IN/OUT] creation time tenth (may be NULL)
 * @param		pbSymlink	: [OUT] boolean flag representing node is symlink
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		JUN-17-2009 [GwangOk Go] First Writing.
 * @version		JUL-31-2009 [JeongWoo Park] Add consideration of 4byte align of symlink structure
 *											the mis-aligned address of structure can make compile error with some platform.
 */
FFatErr
_isSymlink(Node* pNode, t_uint8* pbCrtTimeTenth, t_boolean* pbSymlink, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;
	t_wchar*	psPath;
	t_int32		dwPathLen;	// byte length including NULL
	t_int32		dwBuffLen;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pCxt);

	*pbSymlink = FFAT_FALSE;
	
	if (NODE_S(pNode) <= _SYMLINK_INFO_LEN)
	{
		// size is too short
		goto out;
	}

	if (NODE_S(pNode) >
		(_SYMLINK_INFO_LEN + (_SYMLINK_BYTE_ALIGN_SIZE((_SYMLINK_MAX_PATH_LEN + 1) * sizeof(t_wchar)))))
	{
		// size is too long.
		goto out;
	}

	dwPathLen = NODE_S(pNode) - _SYMLINK_INFO_LEN;

	// allocate memory for path
	psPath = (t_wchar*)FFAT_LOCAL_ALLOC(dwPathLen, pCxt);
	FFAT_ASSERT(psPath);

	dwBuffLen = dwPathLen / sizeof(t_wchar);

	// get target path & CrtTimeTenth
	// lookup후 symbolic link인지 판단하기 위해 symlink info를 모두 읽어야 한다 (check sum 확인을 위해)
	// 이후 readSymlink에서 또 symlink info를 읽기 때문에 overhead가 있다
	// 추후 성능문제가 생길시 수정 (check sum을 확인하지 않도록?)
	r = _getPathAndTimeTenth(pNode, psPath, dwBuffLen, &dwBuffLen, pbCrtTimeTenth, pCxt);

	if (r == FFAT_OK)
	{
		// it is symilnk
		*pbSymlink = FFAT_TRUE;
	}
	else if (r == FFAT_OK1)
	{
		// it is not symlink
		r = FFAT_OK;
	}

	FFAT_LOCAL_FREE(psPath, dwPathLen, pCxt);

out:
	return r;
}
