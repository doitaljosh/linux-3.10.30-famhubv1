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
 * @file		ffat_addon_extended_attribute.c
 * @brief		Extended attribute Module for FFAT
 * @author		InHwan Choi (inhwan.choi@samsung.com)
 * @version		NOV-15-2007 [InHwan Choi] First writing
 * @version		JUN-23-2008 [GwangOk Go] Remove limitation of total X-ATTR size
 * @see			None
 */ 

#include "ess_math.h"

#include "ffat_types.h"
#include "ffat_common.h"
#include "ffat_misc.h"

#include "ffat_node.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffatfs_api.h"
#include "ffatfs_types.h"
#include "ffatfs_misc.h"

#include "ffat_addon_xattr.h"
#include "ffat_addon_api.h"
#include "ffat_addon_log.h"
#include "ffat_addon_types_internal.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_XATTR)

#define _IS_ACTIVATED_EA(_pVol)		((VOL_FLAG(_pVol) & VOL_ADDON_XATTR) ? FFAT_TRUE : FFAT_FALSE)
#define _NODE_EA_CLUSTER(_pNode)	(NODE_ADDON(_pNode)->stEA.dwEAFirstCluster)		// get first cluster of EA
#define _NODE_EA_TOTALSIZE(_pNode)	(NODE_ADDON(_pNode)->stEA.dwTotalSize)		// get total size of EA

#define _INIT_EA_CLUSTER	(1)		// no information of EA cluster 
#define _NO_EA_CLUSTER		(0)		// no EA	(Don't change this value, log module use 0 as no EA cluster)


/*
	Extended Attribute(EA) design principal

	1. EA는 Linux에서 file에 사용자가 원하는 정보를 담는 Extended Attribute 의 약자이다.
	2. EA cluster는 EA를 저장하는 cluster를 말한다.
	3. EA entry는 한개의 EA가 저장되는 단위를 말한다.
	4. EA entry는 EA의 명칭이 저장되는 name과 그 명칭의 내용의 저장되는 value로 나뉜다.
	5. 하나의 file에 대한 EA 용량은 한 sector를 넘지 못한다. value는 128Byte를 넘지 못한다.
*/

// prototype for static functions
static FFatErr	_getEAMain(Node* pNode, EAMain* pEAMain, t_uint32* pudwCluster, FFatVC* pVC, ComCxt* pCxt);
static FFatErr	_createEA(Node* pNode, FFatVC* pVC, EAEntry* pEAEntry, FFatXAttrInfo* pEAInfo, ComCxt* pCxt);
static FFatErr	_compactEA(Node* pNode, FFatVC* pVCOld, EAMain* pEAMain, ComCxt* pCxt);

static FFatErr	_lookupEAEntry(Node* pNode, EAMain* pstEAMain, FFatXAttrInfo* pEAInfo,
								t_uint32 udwNameSize, FFatVC* pVC, EAEntry* pEAEntry,
								t_uint32* pudwCurOffset, ComCxt* pCxt);
static FFatErr	_getEAList(Node* pNode, FFatVC* pVC, t_uint32 udwValidCount, t_int8* psList,
								t_uint32 udwSize, t_uint32* pudwListSize, ComCxt* pCxt);

static FFatErr	_readEAMain(Node* pNode, EAMain* pEAMain, ComCxt* pCxt);
static FFatErr	_writeEAMain(Node* pNode, t_uint32 udwCluster, EAMain* pEAMain,
								FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_writeEAEntry(Node* pNode, FFatVC* pVC, t_uint32 udwCurOffset, FFatXAttrInfo* pEAInfo,
								EAEntry* pEAEntry, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_getEANameSize(FFatXAttrInfo* pEAInfo, t_uint32* pudwNameSize);

static FFatErr	_getRootEAFirstCluster(Vol* pVol, t_uint32* pdwRootEACluster, ComCxt* pCxt);
static FFatErr	_setRootEAFirstCluster(Vol* pVol, t_uint32 dwRootEACluster,
								FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static void		_updateNode(Node* pNode, t_uint32 dwFirstCluster, t_uint32 dwTotalSize);


#ifdef FFAT_BIG_ENDIAN
	static void	_boEAMain(EAMain* pEAMain);
	static void	_boEAEntry(EAEntry* pEAEntry);
#endif

// defines
#define _EA_BYTE_ALIGN				4
#define _EA_BYTE_ALIGN_MASK			(_EA_BYTE_ALIGN - 1)

#define _EA_TEMP_BUFF_SIZE			(FFAT_TEMP_BUFF_MAX_SIZE >> 1)

//!< get byte aligned address
#define _EA_BYTE_ALIGN_SIZE(_size)	(((_size) + _EA_BYTE_ALIGN_MASK) & (~_EA_BYTE_ALIGN_MASK))

typedef struct
{
	FFatXAttrNSID		dwNSID;
	t_uint8				dwLength;
	t_int8*				szPrefix;
} _XAttrNameSpace;

static const _XAttrNameSpace _pNameSpaceTable[] =
{
	{FFAT_XATTR_NS_USER,				FFAT_XATTR_USER_PREFIX_LEN,			FFAT_XATTR_USER_PREFIX},
	{FFAT_XATTR_ID_POSIX_ACL_ACCESS,	FFAT_XATTR_POSIX_ACL_ACCESS_LEN,	FFAT_XATTR_POSIX_ACL_ACCESS},
	{FFAT_XATTR_ID_POSIX_ACL_DEFAULT,	FFAT_XATTR_POSIX_ACL_DEFAULT_LEN,	FFAT_XATTR_POSIX_ACL_DEFAULT},
	{FFAT_XATTR_NS_TRUSTED,				FFAT_XATTR_TRUSTED_PREFIX_LEN,		FFAT_XATTR_TRUSTED_PREFIX},
	{FFAT_XATTR_NS_SECURITY,			FFAT_XATTR_SECURITY_PREFIX_LEN,		FFAT_XATTR_SECURITY_PREFIX}
};


/**
 * if mount flag includes FFAT_MOUNT_XATTR, add VOL_ADDON_XATTR to volume flag
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwFlag		: [IN] mount flag
 * @param		pCxt		: [IN] Context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing
 */
FFatErr
ffat_ea_mount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFAT_ASSERT(pVol);

	if (dwFlag & FFAT_MOUNT_XATTR)
	{
		VOL_FLAG(pVol) |= VOL_ADDON_XATTR;
	}

	return FFAT_OK;
}


/**
 * set an extended attribute value
 * extended attribute의 name, value를 추가하거나 변경한다.
 * 변경하는 경우 기존 X-ATTR를 지우고 list의 마지막에 추가한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pEAInfo		: [IN] extended attribute info
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		JUN-27-2008 [GwangOk Go] Remove limitation of total X-ATTR size
 * @version		DEC-04-2008 [JeongWoo Park] bug fix about the sequence of core lock / unlock
 *											Add the roll-back code
 * @version		DEC-19-2008 [JeongWoo Park] support root EA
 * @version		FEB-04-2009 [JeongWoo Park] edit for error value keeping
 * @version		MAR-18-2009 [DongYoung Seo] add EA cluster count update code
 */
FFatErr
ffat_ea_set(Node* pNode, FFatXAttrInfo* pEAInfo, ComCxt* pCxt)
{
	FFatErr			r;
	FFatErr			rErr;
	FFatErr			rLookup;
	Vol*			pVol;
	FFatVC			stVC;
	FFatVC			stVCNew;

	EAMain			stEAMain;
	EAEntry			stEAEntryNew;
	EAEntry			stEAEntryOld;

	t_uint32		udwNameSize;
	t_uint32		udwEntrySize;
	t_uint32		udwDelOffset = 0;	// offset of entry to be deleted
	t_uint32		udwInsOffset = 0;	// offset of entry to be inserted
	t_uint32		udwNewClusterCnt = 0;
	t_uint32		udwOrgUsedSpace;
	t_uint32		udwOrgEOC;

	t_uint32		udwIOSize;

	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;

	t_boolean		bCoreLocked = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// *** CHECK Parameters
	IF_UK (_IS_ACTIVATED_EA(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("The volume is not mounted to use EA")));
		return FFAT_ENOXATTR;
	}

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		return FFAT_EACCESS;
	}

	IF_UK ((pEAInfo->psName == NULL) || (pEAInfo->pValue == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (pEAInfo->dwSize > FFAT_EA_VALUE_SIZE_MAX)
	{
		FFAT_LOG_PRINTF((_T("Invalid size")));
		return FFAT_EINVALID;
	}

	IF_UK (pEAInfo->dwSetFlag & ~(FFAT_XATTR_SET_FLAG_CREATE | FFAT_XATTR_SET_FLAG_REPLACE))
	{
		FFAT_LOG_PRINTF((_T("Invalid flag")));
		return FFAT_EINVALID;
	}

	// check name size
	r = _getEANameSize(pEAInfo, &udwNameSize);
	FFAT_ER(r, (_T("Invalid name size")));

	VC_INIT(&stVC, 0);
	VC_INIT(&stVCNew, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	stVCNew.pVCE = NULL;	// 추가 cluster 할당여부 확인으로 사용

	udwEntrySize = sizeof(EAEntry) + udwNameSize + pEAInfo->dwSize;

	stEAEntryNew.ubTypeFlag		= (t_uint8)EA_ENTRY_USED;
	stEAEntryNew.ubNameSpaceID	= (t_uint8)pEAInfo->dwNSID;
	stEAEntryNew.udwEntryLength	= _EA_BYTE_ALIGN_SIZE(udwEntrySize);
	stEAEntryNew.uwNameSize		= (t_uint16)udwNameSize;	// byte size
	stEAEntryNew.udwValueSize	= pEAInfo->dwSize;

	// *** get EAMain & vectored cluster of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	if (r == FFAT_ENOXATTR)
	{
		IF_UK (pEAInfo->dwSetFlag & FFAT_XATTR_SET_FLAG_REPLACE)
		{
			// replace flag인 경우 ea cluster가 없으면 에러
			FFAT_LOG_PRINTF((_T("no extended attribute")));
			r = FFAT_ENOXATTR;
			goto out;
		}

		// EA가 없으면 새로 만듬
		r = _createEA(pNode, &stVC, &stEAEntryNew, pEAInfo, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to create extended attribute cluster")));
		}

		// _createEACluster() 에서 다 처리되었음으로 그냥 리턴
		goto out;
	}
	else IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to get extended attribute cluster information")));
		goto out;
	}

	FFAT_ASSERT(stEAMain.uwSig == EA_SIG);
	FFAT_ASSERT(stEAMain.uwValidCount <= FFAT_EA_ATTR_NUMBER_MAX);
	FFAT_ASSERT(ESS_MATH_CDB(stEAMain.udwTotalSpace, VOL_CS(pVol), VOL_CSB(pVol)) == VC_CC(&stVC));

	// *** Do compaction EA if EA is over threshold.
	if ((stEAMain.udwTotalSpace + stEAEntryNew.udwEntryLength) > EA_COMPACTION_THRESHOLD)
	{
		// 1개의 entry length(stEAEntryNew.udwEntryLength)가 한계가 있기 때문에
		// 다음 조건과 같이 invalid EA가 존재하여야 함.
		FFAT_ASSERT(stEAMain.udwUsedSpace < stEAMain.udwTotalSpace);
		
		// threshold 이상이면 compaction을 수행하여 공간 확보
		r = _compactEA(pNode, &stVC, &stEAMain, pCxt);
		FFAT_EO(r, (_T("fail to compact X-ATTR cluster")));

		FFAT_ASSERT((stEAMain.udwTotalSpace + stEAEntryNew.udwEntryLength) < EA_COMPACTION_THRESHOLD);
		FFAT_ASSERT(stEAMain.udwTotalSpace == stEAMain.udwUsedSpace);
	}

	udwOrgUsedSpace	= stEAMain.udwUsedSpace;

	// *** Find the ea entry with matching condition and find free area
	rLookup = _lookupEAEntry(pNode, &stEAMain, pEAInfo, udwNameSize, &stVC,
							&stEAEntryOld, &udwDelOffset, pCxt);
	if (rLookup == FFAT_OK)
	{
		// If CREATE flag and the entry is already exist, then return error.
		IF_UK (pEAInfo->dwSetFlag & FFAT_XATTR_SET_FLAG_CREATE)
		{
			FFAT_LOG_PRINTF((_T("already exist extended attribute")));
			r = FFAT_EEXIST;
			goto out;
		}

		udwOrgUsedSpace -= stEAEntryOld.udwEntryLength;
	}
	else if (rLookup == FFAT_ENOENT)
	{
		// If REPLACE flag and the entry is not exist, then return error.
		IF_UK (pEAInfo->dwSetFlag & FFAT_XATTR_SET_FLAG_REPLACE)
		{
			FFAT_LOG_PRINTF((_T("no extended attribute")));
			r = FFAT_ENOXATTR;
			goto out;
		}

		// IF EA number is at maximum, then return error.
		IF_UK (stEAMain.uwValidCount == FFAT_EA_ATTR_NUMBER_MAX)
		{
			FFAT_LOG_PRINTF((_T("too many extended attribute")));
			r = FFAT_EFULLXATTR;
			goto out;
		}
	}
	else
	{
		// other error
		r = rLookup;
		goto out;
	}

	// list maximum 확인
	if ((udwOrgUsedSpace + stEAEntryNew.udwEntryLength) > FFAT_EA_LIST_SIZE_MAX)
	{
		FFAT_LOG_PRINTF((_T("there is insufficient space for extended attribute")));
		r = FFAT_ENOSPC;
		goto out;
	}

	// new entry 추가후 필요한 cluster count 계산
	udwNewClusterCnt = ESS_MATH_CDB((stEAMain.udwTotalSpace + stEAEntryNew.udwEntryLength),
									VOL_CS(pVol), VOL_CSB(pVol));

	// lock ADDON for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	bCoreLocked = FFAT_TRUE;

	// *** 추가로 cluster가 필요한 경우 할당
	if (udwNewClusterCnt > VC_CC(&stVC))
	{
		t_uint32	udwRequiredCnt;
		t_uint32	udwFreeCount;

		// allocate memory for vectored cluster information
		stVCNew.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
		FFAT_ASSERT(stVCNew.pVCE);

		stVCNew.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

		udwRequiredCnt = udwNewClusterCnt - VC_CC(&stVC);

		// get free clusters
		r = ffat_misc_getFreeClusters(pNode, udwRequiredCnt, &stVCNew, 0, &udwFreeCount,
										FAT_ALLOCATE_NONE, pCxt);
		FFAT_EO(r, (_T("fail to get free clusters")));

		FFAT_ASSERT(udwRequiredCnt == udwFreeCount);
		FFAT_ASSERT(udwFreeCount == VC_CC(&stVCNew));
		FFAT_ASSERT(VC_VEC(&stVCNew) >= 1);
	}

	if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// the insert position is last of EA
	udwInsOffset = stEAMain.udwTotalSpace;

	// *** write log
	r = ffat_log_setEA(pNode, &stVC, &stVCNew, udwDelOffset, udwInsOffset, &stEAMain,
						&stEAEntryOld, &dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write log")), out_undo_fc);

	// store original EOC for undo
	udwOrgEOC = VC_LC(&stVC);

	// *** Make chine for new clusters
	if (stVCNew.pVCE != NULL)
	{
		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_MAKE_CHAIN_BEFORE);

		// make cluster chain
		r = ffat_misc_makeClusterChainVC(pNode, VC_LC(&stVC), &stVCNew, FAT_UPDATE_NONE, dwCacheFlag, pCxt);
		FFAT_EOTO(r, (_T("fail to make cluster chain")), out_undo_log);

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_MAKE_CHAIN_AFTER);

		FFAT_ASSERT(VC_IS_FULL(&stVC) == FFAT_FALSE);

		// merge VC
		if (VC_IS_FULL(&stVC) == FFAT_FALSE)
		{
			ffat_com_mergeVC(&stVC, &stVCNew);
		}

		FFAT_ASSERT(udwNewClusterCnt == VC_CC(&stVC));
	}

	// *** Discard original EA entry as DELETE
	if (rLookup == FFAT_OK)
	{
		// 동일 name의 entry가 존재하는 경우
		// 기존의 attriubte에 delete mark 후 마지막에 entry 추가
		FFAT_ASSERT(stEAEntryOld.ubTypeFlag & EA_ENTRY_USED);

		stEAEntryOld.ubTypeFlag = EA_ENTRY_DELETE;

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_MARK_DELETE_BEFORE);

		// write type (delete mark)
		r = ffat_node_readWriteInit(pNode, udwDelOffset, (t_int8*)&(stEAEntryOld.ubTypeFlag),
								EA_TYPE_FLAG_SIZE, &stVC, &udwIOSize, dwCacheFlag,
								FFAT_RW_WRITE, pCxt);
		IF_UK ((r < 0) || (udwIOSize != EA_TYPE_FLAG_SIZE))
		{
			FFAT_LOG_PRINTF((_T("fail to write X-ATTR type")));
			r = FFAT_EIO;
			goto out_undo_fat;
		}

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_MARK_DELETE_AFTER);

		stEAMain.udwUsedSpace -= stEAEntryOld.udwEntryLength;
		stEAMain.uwValidCount--;
	}

	stEAMain.udwTotalSpace += stEAEntryNew.udwEntryLength;
	stEAMain.udwUsedSpace += stEAEntryNew.udwEntryLength;
	stEAMain.uwValidCount++;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_WRITE_ENTRY_BEFORE);

	// *** Write new EAEntry & name & value
	r = _writeEAEntry(pNode, &stVC, udwInsOffset, pEAInfo, &stEAEntryNew, dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write ea")), out_undo_discard);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_WRITE_ENTRY_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_WRITE_MAIN_BEFORE);

	// *** Write EAMain
	r = _writeEAMain(pNode, VC_FC(&stVC), &stEAMain, dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write ea main")), out_undo_new);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_EA_WRITE_MAIN_AFTER);

	// set EA information
	_updateNode(pNode, VC_FC(&stVC), stEAMain.udwTotalSpace);

out:
	IF_LK (bCoreLocked == FFAT_TRUE)
	{
		// lock CORE for free cluster sync
		r |= ffat_core_unlock(pCxt);
	}

	FFAT_LOCAL_FREE(stVCNew.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);

	return r;


//*** FAIL 처리부 (Run-time error correction)
out_undo_new:
	FFAT_ASSERT(stEAEntryNew.ubTypeFlag & EA_ENTRY_USED);

	stEAEntryNew.ubTypeFlag = EA_ENTRY_DELETE;

	// write type (DELETE mark)
	rErr = ffat_node_readWriteInit(pNode, udwInsOffset, (t_int8*)&(stEAEntryNew.ubTypeFlag),
									EA_TYPE_FLAG_SIZE, &stVC, &udwIOSize, (dwCacheFlag | FFAT_CACHE_FORCE),
									FFAT_RW_WRITE, pCxt);
	IF_UK ((rErr < 0) || (udwIOSize != EA_TYPE_FLAG_SIZE))
	{
		FFAT_LOG_PRINTF((_T("fail to roll back of new EA entry")));
		r |= rErr;
	}
out_undo_discard:
	if (rLookup == FFAT_OK)
	{
		FFAT_ASSERT(stEAEntryOld.ubTypeFlag & EA_ENTRY_DELETE);

		stEAEntryOld.ubTypeFlag = EA_ENTRY_USED;

		// write type (USED mark)
		rErr = ffat_node_readWriteInit(pNode, udwDelOffset, (t_int8*)&(stEAEntryOld.ubTypeFlag),
										EA_TYPE_FLAG_SIZE, &stVC, &udwIOSize, (dwCacheFlag | FFAT_CACHE_FORCE),
										FFAT_RW_WRITE, pCxt);
		IF_UK ((rErr < 0) || (udwIOSize != EA_TYPE_FLAG_SIZE))
		{
			FFAT_LOG_PRINTF((_T("fail to roll back of original EA entry")));
			r |= rErr;
		}
	}

out_undo_fat:
	if (stVCNew.pVCE != NULL)
	{
		// deallocate cluster
		rErr = ffat_misc_deallocateCluster(pNode, udwOrgEOC, VC_FC(&stVCNew), 0, &stVCNew, FAT_UPDATE_DE_NONE,
										(dwCacheFlag | FFAT_CACHE_FORCE), pCxt);
		IF_UK (rErr < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to roll back allocated FAT chain.")));
			r |= rErr;
		}

		VC_VEC(&stVCNew) = 0;	// deallocateCluster에서 add free cluster를 수행 -> add free cluster할 필요없음
	}

out_undo_log:
	FFAT_ASSERT(FFAT_IS_SUCCESS(r) == FFAT_FALSE);
	rErr = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_EA_SET, pNode, pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back for log.")));
		r |= rErr;
	}

out_undo_fc:
	if (VC_VEC(&stVCNew) != 0)
	{
		// add free cluster to FCC
		rErr = ffat_fcc_addFreeClustersVC(pVol, &stVCNew, pCxt);
		IF_UK (rErr < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to add free clusters to FCC")));
			r |= rErr;
		}
	}

	goto out;
}


/**
 * get an extended attribute value
 * extended attribute의 name을 받아서 찾아 value와 size를 반환한다.
 * size가 0일 경우 extended attribute의 name에 해당하는 value의 size를 반환한다.
 *
 * @param		pNode			: [IN] node pointer
 * @param		pEAInfo			: [IN/OUT] extended attribute info
 * @param		pudwValueSize	: [OUT] size of extended attribute value
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		JUN-27-2008 [GwangOk Go] Remove limitation of total X-ATTR size
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 */
FFatErr
ffat_ea_get(Node* pNode, FFatXAttrInfo* pEAInfo, t_uint32* pudwValueSize, ComCxt* pCxt)
{
	FFatErr 		r;
	Vol*			pVol;
	FFatVC			stVC;
	EAMain			stEAMain;
	EAEntry			stEAEntry;
	t_uint32		udwNameSize;
	t_uint32		udwCurOffset;
	t_uint32		udwIOSize;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pudwValueSize);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	IF_UK (_IS_ACTIVATED_EA(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("The volume is not mounted to use EA")));
		return FFAT_ENOXATTR;
	}

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		return FFAT_EACCESS;
	}

	IF_UK ((pEAInfo->dwSize != 0) && (pEAInfo->pValue == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	// check name size
	r = _getEANameSize(pEAInfo, &udwNameSize);
	FFAT_ER(r, (_T("Invalid name size")));

	VC_INIT(&stVC, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// get vectored cluster & EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	FFAT_EO(r, (_T("fail to get extended attribute cluster information")));

	FFAT_ASSERT(stEAMain.uwSig == EA_SIG);
	FFAT_ASSERT(stEAMain.uwValidCount <= FFAT_EA_ATTR_NUMBER_MAX);
	FFAT_ASSERT(ESS_MATH_CDB(stEAMain.udwTotalSpace, VOL_CS(pVol), VOL_CSB(pVol)) == VC_CC(&stVC));

	// attribute를 찾는다.
	r = _lookupEAEntry(pNode, &stEAMain, pEAInfo, udwNameSize, &stVC,
						&stEAEntry, &udwCurOffset, pCxt);
	if (r == FFAT_ENOENT)
	{
		r = FFAT_ENOXATTR;
	}
	FFAT_EO(r, (_T("fail to search entry")));

	if (pEAInfo->dwSize == 0)
	{
		*pudwValueSize = stEAEntry.udwValueSize;
		r = FFAT_OK;
		goto out;
	}

	// input buffer size 확인
	if ((t_uint32)pEAInfo->dwSize < stEAEntry.udwValueSize)
	{
		r = FFAT_ERANGE;
		FFAT_LOG_PRINTF((_T("Too small buffer for EA value")));
		goto out;
	}

	udwCurOffset += (sizeof(EAEntry) + stEAEntry.uwNameSize);

	// get current value
	r = ffat_node_readWriteInit(pNode, udwCurOffset, pEAInfo->pValue, stEAEntry.udwValueSize,
								&stVC, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
	IF_UK ((r < 0) || (udwIOSize != stEAEntry.udwValueSize))
	{
		FFAT_LOG_PRINTF((_T("fail to read X-ATTR value")));
		r = FFAT_EIO;
		goto out;
	}

	*pudwValueSize = stEAEntry.udwValueSize;

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	return r;
}


/**
 * delete an extended attribute
 * 지우려는 extended attribute의 info에 delete mark를 한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pEAInfo		: [IN] extended attribute info
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		JUN-27-2008 [GwangOk Go] Remove limitation of total X-ATTR size
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 * @version		FEB-04-2009 [JeongWoo Park] edit for error value keeping
 */
FFatErr
ffat_ea_delete(Node* pNode, FFatXAttrInfo* pEAInfo, ComCxt* pCxt)
{
	FFatErr			r;
	FFatErr			rErr;
	FFatVC			stVC;
	EAMain			stEAMain;
	EAEntry			stEAEntry;
	t_uint32		udwNameSize;
	t_uint32		udwFirstCluster;
	t_uint32		udwCurOffset;
	t_uint32		udwIOSize;
	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("The volume is not mounted to use EA")));
		return FFAT_ENOXATTR;
	}

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		return FFAT_EACCESS;
	}

	// check name size
	r = _getEANameSize(pEAInfo, &udwNameSize);
	FFAT_ER(r, (_T("Invalid name size")));

	VC_INIT(&stVC, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// get vectored cluster & EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	FFAT_EO(r, (_T("fail to get extended attribute cluster information")));

	udwFirstCluster = VC_FC(&stVC);

	FFAT_ASSERT(stEAMain.uwSig == EA_SIG);
	FFAT_ASSERT(stEAMain.uwValidCount <= FFAT_EA_ATTR_NUMBER_MAX);
	FFAT_ASSERT(ESS_MATH_CDB(stEAMain.udwTotalSpace, VOL_CS(NODE_VOL(pNode)), VOL_CSB(NODE_VOL(pNode))) == VC_CC(&stVC));

	// attribute를 찾는다.
	r = _lookupEAEntry(pNode, &stEAMain, pEAInfo, udwNameSize, &stVC,
						&stEAEntry, &udwCurOffset, pCxt);
	FFAT_EO(r, (_T("fail to search entry")));

	FFAT_ASSERT(stEAEntry.ubTypeFlag & EA_ENTRY_USED);

	if (VOL_IS_SYNC_META(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// write log
	r = ffat_log_deleteEA(pNode, udwFirstCluster, udwCurOffset, &stEAMain, &stEAEntry, &dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_DELETE_EA_MARK_DELETE_BEFORE);

	// write type
	stEAEntry.ubTypeFlag = EA_ENTRY_DELETE;

	r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)&(stEAEntry.ubTypeFlag),
								EA_TYPE_FLAG_SIZE, &stVC, &udwIOSize, dwCacheFlag, FFAT_RW_WRITE, pCxt);
	IF_UK ((r < 0) || (udwIOSize != EA_TYPE_FLAG_SIZE))
	{
		FFAT_LOG_PRINTF((_T("fail to write X-ATTR type")));
		r = FFAT_EIO;
		goto out_undo_log;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_DELETE_EA_MARK_DELETE_AFTER);

	// write EAMain
	stEAMain.udwUsedSpace -= stEAEntry.udwEntryLength;
	stEAMain.uwValidCount--;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_DELETE_EA_WRITE_MAIN_BEFORE);

	r = _writeEAMain(pNode, udwFirstCluster, &stEAMain, dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write ea main")), out_undo_discard);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_DELETE_EA_WRITE_MAIN_AFTER);
	
out:
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	return r;

// Run-time error correction
out_undo_discard:
	FFAT_ASSERT(stEAEntry.ubTypeFlag & EA_ENTRY_DELETE);

	stEAEntry.ubTypeFlag = EA_ENTRY_USED;

	// write type (USED mark)
	rErr = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)&(stEAEntry.ubTypeFlag),
									EA_TYPE_FLAG_SIZE, &stVC, &udwIOSize, (dwCacheFlag | FFAT_CACHE_FORCE),
									FFAT_RW_WRITE, pCxt);
	IF_UK ((rErr < 0) || (udwIOSize != EA_TYPE_FLAG_SIZE))
	{
		FFAT_LOG_PRINTF((_T("fail to roll back of original EA entry")));
		r |= rErr;
	}

out_undo_log:
	FFAT_ASSERT(FFAT_IS_SUCCESS(r) == FFAT_FALSE);
	rErr = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_EA_DELETE, pNode, pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back for log.")));
		r |= rErr;
	}

	goto out;
}


/**
 * list extended attribute names
 * extended attribute name들을 담은 buffer와 extended attribute name들의 사이즈 합을 반환한다.
 * input buffer의 size가 0이면 extended attribute name들의 사이즈 합을 반환한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pEAInfo		: [IN/OUT] extended attribute info
 * @param		pudwListSize: [IN/OUT] size of X-ATTR name list
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JUN-27-2008 [GwangOk Go] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 */
FFatErr
ffat_ea_list(Node* pNode, FFatXAttrInfo* pEAInfo, t_uint32* pudwListSize, ComCxt* pCxt)
{
	FFatErr 		r;
	FFatVC			stVC;
	EAMain			stEAMain;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pudwListSize);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("The volume is not mounted to use EA")));
		return FFAT_ENOXATTR;
	}

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		return FFAT_EACCESS;
	}

	IF_UK (pEAInfo->dwSize < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid size")));
		return FFAT_EINVALID;
	}

	IF_UK ((pEAInfo->dwSize != 0) && (pEAInfo->psName == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	VC_INIT(&stVC, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// get vectored cluster & EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	if (r == FFAT_ENOXATTR)
	{
		*pudwListSize = 0;
		r = FFAT_OK;
		goto out;
	}
	else if (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to get extended attribute cluster information")));
		goto out;
	}

	FFAT_ASSERT(stEAMain.uwSig == EA_SIG);
	FFAT_ASSERT(stEAMain.uwValidCount <= FFAT_EA_ATTR_NUMBER_MAX);
	FFAT_ASSERT(ESS_MATH_CDB(stEAMain.udwTotalSpace, VOL_CS(NODE_VOL(pNode)), VOL_CSB(NODE_VOL(pNode))) == VC_CC(&stVC));

	if (pEAInfo->dwSize == 0)
	{
		// get only list size
		r = _getEAList(pNode, &stVC, stEAMain.uwValidCount, NULL, 0, pudwListSize, pCxt);
	}
	else
	{
		// get list & list size
		r = _getEAList(pNode, &stVC, stEAMain.uwValidCount, pEAInfo->psName, pEAInfo->dwSize, pudwListSize, pCxt);
	}

	FFAT_EO(r, (_T("fail to list X-ATTR")));

out:
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	return r;
}


/**
 * destroy extended attribute cluster
 * 해당 file이 삭제가 되면 extended cluster 또한 함께 삭제 되어야 한다.
 * FAT chain만 free로 만들면 된다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK 	: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 * @version		MAR-16-2009 [DongYoung Seo] bug fix: FLASH00020924
 *											add FFAT_CACHE_SYNC flag for ffat_misc_deallocateCluster()
 *											cluster deallocation must be write through operation.
 *											(open unlink entry for EA will be deleted just after this operation)
 * @version		MAR-18-2009 [DongYoung Seo] add Node initialization code after deallocate
 */
FFatErr
ffat_ea_deallocate(Node* pNode, ComCxt* pCxt)
{
	FFatErr			r;
	FFatVC			stVC;
	EAMain			stEAMain;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_ENOXATTR;
	}

	// root can not be deleted
	FFAT_ASSERT (NODE_IS_ROOT(pNode) != FFAT_TRUE);

	VC_INIT(&stVC, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// get vectored cluster of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	IF_UK (r != FFAT_OK)
	{
		goto out;
	}

	// 해당 FAT chain을 free시킨다.
	r = ffat_misc_deallocateCluster(pNode, 0, VC_FC(&stVC), 0, &stVC,
									FAT_ALLOCATE_NONE, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to deallocate cluster")));

	// initializes EA information
	ffat_ea_initNode(pNode);

out:
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	return r;
}


/**
 * set extended attribute entry's true creat time
 * extended attribute cluster에 저장되어 있는 진짜 create time을 수정한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [INOUT] status pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-23-2007 [InHwan Choi] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 */ 
FFatErr
ffat_ea_setStatus(Node* pNode, FFatNodeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr 		r;
	EAMain			stEAMain;
	t_uint32		udwFirstCluster;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_ENOXATTR;
	}

	// get first cluster & EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, &udwFirstCluster, NULL, pCxt);
	if (r < 0)
	{
		FFAT_ASSERT(r == FFAT_ENOXATTR);
		return r;
	}

	// set create time
	stEAMain.uwCrtDate = (t_uint16)((pStatus->dwCTime) >> 16);
	stEAMain.uwCrtTime = (t_uint16)((pStatus->dwCTime) & 0xFFFF);

	// write EAMain
	r = _writeEAMain(pNode, udwFirstCluster, &stEAMain, FFAT_CACHE_SYNC, pCxt);
	FFAT_ER(r, (_T("fail to write ea main")));

	// update pStatus for Core update SFNE
	pStatus->dwCTime = udwFirstCluster;

	return FFAT_OK;
}


/**
 * get extended attribute entry's true creat time
 * extended attribute cluster에 저장되어 있는 진짜 create time을 반환한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pDE			: [IN] extended attribute info
 * @param		wCrtDate	: [IN/OUT] extended attribute info
 * @param		wCrtTime	: [IN/OUT] extended attribute info
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK 	: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-23-2007 [InHwan Choi] First Writing.
 * @version		DEC-22-2008 [JeongWoo Park] support root EA
 */
FFatErr
ffat_ea_getCreateTime(Node* pNode, FatDeSFN* pDE, t_uint16* puwCrtDate,
					t_uint16* puwCrtTime, ComCxt* pCxt)
{
	FFatErr 		r;
	EAMain			stEAMain;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(puwCrtDate);
	FFAT_ASSERT(puwCrtTime);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_ENOXATTR;
	}

	// get EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, NULL, pCxt);
	if (r < 0)
	{
		FFAT_ASSERT(r == FFAT_ENOXATTR);
		return r;
	}

	*puwCrtDate = stEAMain.uwCrtDate;
	*puwCrtTime = stEAMain.uwCrtTime;

	return FFAT_OK;
}


/**
 * get allocated size for EA
 *
 * @param		pNode		: [IN] node pointer
 * @param		pdwSize		: [IN/OUT] allocated size for  EA
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK 	: success
 *								0 : there is no EA
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		MAR-18-2009 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_ea_getAllocSize(Node* pNode, t_uint32* pdwSize, ComCxt* pCxt)
{
	FFatErr			r;
	EAMain			stEAMain;			// Main information for EA
	t_uint32		dwTotalSize;		// total size of EA

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwSize);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	*pdwSize = 0;

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		// get EAMain of extended attribute
		r = _getEAMain(pNode, &stEAMain, NULL, NULL, pCxt);
		if (r == FFAT_OK)
		{
			dwTotalSize = _NODE_EA_TOTALSIZE(pNode);
			FFAT_ASSERT(dwTotalSize >= sizeof(EAMain));

			*pdwSize = (dwTotalSize + VOL_CS(NODE_VOL(pNode)) -1) & (~VOL_CSM(NODE_VOL(pNode)));
			FFAT_ASSERT(*pdwSize >= (t_uint32)VOL_CS(NODE_VOL(pNode)));
		}
	}

	return FFAT_OK;
}


/**
 * Does node have extended attribute entry?
 *
 * @param		pNode		: [IN] node pointer
 * @param		pudwCluster	: [OUT] EA cluster no of node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-26-2007 [InHwan Choi] First Writing.
 */
FFatErr
ffat_ea_getEAFirstCluster(Node* pNode, t_uint32* pudwCluster, ComCxt* pCxt)
{
	EAMain	stEAMain;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(&pNode->stDE);
	FFAT_ASSERT(pudwCluster);
	FFAT_ASSERT(pCxt);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_ENOXATTR;
	}

	return _getEAMain(pNode, &stEAMain, pudwCluster, NULL, pCxt);
}


/**
* rename시 extended attribute 관련 update를 수행
*
* @param		pNodeSrc	: [IN] source node pointer
* @param		pNodeDes	: [IN] destination node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		JeongWoo Park
* @version		DEC-22-2008 [JeongWoo Park] First Writing
*/
void
ffat_ea_renameEA(Node* pNodeSrc, Node* pNodeDes)
{
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDes);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeSrc) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeDes) == FFAT_FALSE);

	if (_IS_ACTIVATED_EA(NODE_VOL(pNodeDes)) == FFAT_FALSE)
	{
		// volume에 extended Attribute가 설정되어 있지 않은 경우
		return;
	}

	if ((pNodeSrc->stDE.bNTRes & ADDON_SFNE_MARK_XATTR) == 0)
	{
		//FFAT_ASSERT(0);
		return;
	}

	// mark extended attribute on NTRes of SFNE
	pNodeDes->stDE.bNTRes |= ADDON_SFNE_MARK_XATTR;

	return;
}


/**
* init node for EA
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		JeongWoo Park
* @version		DEC-19-2008 [JeongWoo Park] First Writing.
* @version		MAR-18-2009 [DongYoung Seo] make a sub-rouine _updatNode()
*/
FFatErr
ffat_ea_initNode(Node* pNode)
{
	// set as need to calculate the EA start cluster
	_updateNode(pNode, _INIT_EA_CLUSTER, 0);

	return FFAT_OK;
}


/**
* read EA first cluster for Root
*
* @param		pVol				: [IN] Volume structure
* @param		pdwRootEACluster	: [OUT] first cluster of Root EA
* @param		pCxt				: [IN] context of current operation
* @return		FFAT_OK				: success
* @return		negative			: fail
* @author		JeongWoo Park
* @version		DEC-19-2008 [JeongWoo Park] First Writing
*/
FFatErr
ffat_ea_getRootEAFirstCluster(Vol* pVol, t_uint32* pdwRootEACluster, ComCxt* pCxt)
{
	return _getRootEAFirstCluster(pVol, pdwRootEACluster, pCxt);
}


/**
* write EA first cluster for Root
*
* @param		pVol			: [IN] Volume structure
* @param		dwRootEACluster	: [IN] first cluster of Root EA
*										If 0, erase Root EA
* @param		dwCacheFlag		: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		negative		: fail
* @author		JeongWoo Park
* @version		DEC-19-2008 [JeongWoo Park] First Writing
*/
FFatErr
ffat_ea_setRootEAFirstCluster(Vol* pVol, t_uint32 dwRootEACluster,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	return _setRootEAFirstCluster(pVol, dwRootEACluster, dwCacheFlag, pCxt);
}


/**
* check the validity of the extended attribute
* for CHKDSK tool
*
* @param		pNode		: [IN] node pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK 	: success
* @return		negative	: fail
* @author		JeongWoo Park
* @version		DEC-29-2008 [JeongWoo Park] first writing
*/
FFatErr
ffat_ea_checkEA(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;
	FFatVC		stVC;
	EAMain		stEAMain;
	EAEntry		stEAEntry;
	t_uint32	udwCurOffset;
	t_uint32	udwRemainSize;
	t_uint32	udwUsedSize;
	t_uint32	udwValidCount;
	t_uint32	udwIOSize;
	t_boolean	bFoundPosixAccess = FFAT_FALSE;
	t_boolean	bFoundPosixDefault = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	IF_UK (_IS_ACTIVATED_EA(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		FFAT_ASSERT(0);
		return FFAT_ENOXATTR;
	}

	VC_INIT(&stVC, 0);

	// allocate memory for vectored cluster information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	stVC.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// get vectored cluster & EAMain of extended attribute
	r = _getEAMain(pNode, &stEAMain, NULL, &stVC, pCxt);
	if (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to get extended attribute cluster information")));
		goto out;
	}

	if (stEAMain.uwValidCount > FFAT_EA_ATTR_NUMBER_MAX)
	{
		FFAT_LOG_PRINTF((_T("EA valid count is over the FFAT_EA_ATTR_NUMBER_MAX")));
		r = FFAT_EFAT;
		goto out;
	}

	if (ESS_MATH_CDB(stEAMain.udwTotalSpace, VOL_CS(NODE_VOL(pNode)), VOL_CSB(NODE_VOL(pNode))) != VC_CC(&stVC))
	{
		FFAT_LOG_PRINTF((_T("EA Total space size is wrong")));
		r = FFAT_EFAT;
		goto out;
	}

	// check each EA entry
	udwCurOffset	= sizeof(EAMain);
	udwRemainSize	= stEAMain.udwTotalSpace - udwCurOffset;
	udwUsedSize		= udwCurOffset;
	udwValidCount	= 0;

	while (udwRemainSize > 0)
	{
		// read current EAEntry
		r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)&stEAEntry, sizeof(EAEntry),
									&stVC, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
		IF_UK ((r < 0) || (udwIOSize != sizeof(EAEntry)))
		{
			FFAT_LOG_PRINTF((_T("fail to read X-ATTR entry")));
			r = FFAT_EIO;
			goto out;
		}

#ifdef FFAT_BIG_ENDIAN
		_boEAEntry(&stEAEntry);
#endif

		FFAT_ASSERT((stEAEntry.ubTypeFlag & EA_ENTRY_USED) || (stEAEntry.ubTypeFlag & EA_ENTRY_DELETE));

		// delete mark를 확인한다. && name 길이를 비교한다.
		if ((stEAEntry.ubTypeFlag & EA_ENTRY_DELETE) == 0)
		{
			if (stEAEntry.ubNameSpaceID == FFAT_XATTR_ID_POSIX_ACL_ACCESS)
			{
				if (stEAEntry.uwNameSize != 0)
				{
					FFAT_LOG_PRINTF((_T("EA POSIX name size is wrong")));
					r = FFAT_EFAT;
					goto out;
				}

				if (bFoundPosixAccess == FFAT_TRUE)
				{
					FFAT_LOG_PRINTF((_T("POSIX_ACL_ACCESS is mutiply existed.")));
					r = FFAT_EFAT;
					goto out;
				}

				bFoundPosixAccess = FFAT_TRUE;
			}
			else if (stEAEntry.ubNameSpaceID == FFAT_XATTR_ID_POSIX_ACL_DEFAULT)
			{
				if (stEAEntry.uwNameSize != 0)
				{
					FFAT_LOG_PRINTF((_T("EA POSIX name size is wrong")));
					r = FFAT_EFAT;
					goto out;
				}

				if (bFoundPosixDefault == FFAT_TRUE)
				{
					FFAT_LOG_PRINTF((_T("POSIX_ACL_DEFAULT is mutiply existed.")));
					r = FFAT_EFAT;
					goto out;
				}

				bFoundPosixDefault = FFAT_TRUE;
			}

			udwUsedSize += stEAEntry.udwEntryLength;
			udwValidCount++;
		}

		udwCurOffset	+= stEAEntry.udwEntryLength;
		udwRemainSize	-= stEAEntry.udwEntryLength;
	}

	if (udwUsedSize != stEAMain.udwUsedSpace)
	{
		FFAT_LOG_PRINTF((_T("EA used space size is wrong")));
		r = FFAT_EFAT;
		goto out;
	}

	if (udwCurOffset != stEAMain.udwTotalSpace)
	{
		FFAT_LOG_PRINTF((_T("EA total space size is wrong")));
		r = FFAT_EFAT;
		goto out;
	}

	if (udwValidCount != stEAMain.uwValidCount)
	{
		FFAT_LOG_PRINTF((_T("EA valid count is wrong")));
		r = FFAT_EFAT;
		goto out;
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVC.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT((r >= 0) || (r == FFAT_EIO));

	return r;
}


/**
 * undo extended attribute set (for power off recovery)
 *
 * @param		pVol				: [IN] volume pointer
 * @param		udwFirstCluster		: [IN] first cluster of extended attribute
 * @param		udwDelOffset		: [IN] offset of current to be deleted
 * @param		udwInsOffset		: [IN] offset of current to be inserted
 * @param		pEAMain				: [IN] EAMain
 * @param		pEAEntry			: [IN] EAEntry
 * @param		pCxt				: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-13-2008 [GwangOk Go] : First Writing
 * @version		DEC-10-2008 [JeongWoo Park] : edit sequence of recovery and
 *											 Add the consideration about the case of power-off before cluster allocation.
 * @version		MAR-03-2009 [DongYoung Seo]: remove assert when there is not exist cluster for the offset
 *											There cluster may not exist when power off just after log write
 * @version		MAR-16-2009 [JeongWoo Park] : Bug fix - FLASH00020832
 *												All operation of the log recovery must be write-through.
 *												(write the USED/DELETE mark with write-through)
 */
FFatErr
ffat_ea_undoSetEA(Vol* pVol, t_uint32 udwFirstCluster, t_uint32 udwDelOffset,
				t_uint32 udwInsOffset, EAMain* pEAMain, EAEntry* pEAEntry, ComCxt* pCxt)
{
	FFatErr			r;
	FatVolInfo*		pVI;
	FFatCacheInfo	stCI;
	t_uint32		udwCluster;
	t_uint8			ubTypeFlag;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(udwInsOffset != 0);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pEAEntry);
	FFAT_ASSERT(pCxt);

	pVI = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

#ifdef FFAT_BIG_ENDIAN
	_boEAMain(pEAMain);
#endif

	// *** 1) write original EAMain
	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
	r = FFATFS_ReadWritePartialCluster(pVI, udwFirstCluster, 0, sizeof(EAMain),
										(t_int8*)pEAMain, FFAT_FALSE, FFAT_CACHE_SYNC, &stCI, pCxt, FFAT_FALSE);
	IF_UK (r != sizeof(EAMain))
	{
		FFAT_LOG_PRINTF((_T("fail to write EAMain")));
		r = FFAT_EIO;
		goto out;
	}

	// *** 2) recover original EA entry as USED
	if (udwDelOffset != 0)
	{
		FFAT_ASSERT(pEAEntry->ubTypeFlag & EA_ENTRY_USED);
		FFAT_ASSERT((pEAEntry->ubTypeFlag & EA_ENTRY_DELETE) == 0);

		// get udwDelCluster from udwDelOffset
		r = FFATFS_GetClusterOfOffset(pVI, udwFirstCluster, udwDelOffset, &udwCluster, pCxt);
		FFAT_ASSERT(FFATFS_IS_EOF(pVI, udwCluster) == FFAT_FALSE);
		FFAT_EO(r, (_T("fail to get cluster of original EA entry")));

		// remark the type as USED (DELETE -> USED) 
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		r = FFATFS_ReadWritePartialCluster(pVI, udwCluster, (udwDelOffset & VOL_CSM(pVol)),
											EA_TYPE_FLAG_SIZE, (t_int8*)&(pEAEntry->ubTypeFlag),
											FFAT_FALSE, FFAT_CACHE_SYNC, &stCI, pCxt, FFAT_FALSE);
		IF_UK (r != EA_TYPE_FLAG_SIZE)
		{
			FFAT_LOG_PRINTF((_T("fail to write X-ATTR type")));
			r = FFAT_EIO;
			goto out;
		}
	}

	// *** 3) discard new EA entry as DELETED
	// get udwInsCluster from udwInsOffset
	r = FFATFS_GetClusterOfOffset(pVI, udwFirstCluster, udwInsOffset,
									&udwCluster, pCxt);
	if (r == FFAT_EFAT)
	{
		// If cluster is not allocated before power-off,
		// FFAT_EFAT can be returned by FFATFS_GetClusterOfOffset().
		// In this case. there isn't new EA entry, POR is all-done.
		// just return OK. [By STORM]

		r = FFAT_OK;
		goto out;
	}
	else
	{
		FFAT_EO(r, (_T("fail to get cluster of new EA entry")));
	}

	if (FFATFS_IS_EOF(pVI, udwCluster) == FFAT_FALSE)
	{
		ubTypeFlag = EA_ENTRY_DELETE;

		// write type
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		r = FFATFS_ReadWritePartialCluster(pVI, udwCluster, (udwInsOffset & VOL_CSM(pVol)),
											EA_TYPE_FLAG_SIZE, (t_int8*)&ubTypeFlag,
											FFAT_FALSE, FFAT_CACHE_SYNC, &stCI, pCxt, FFAT_FALSE);
		IF_UK (r != EA_TYPE_FLAG_SIZE)
		{
			FFAT_LOG_PRINTF((_T("fail to write X-ATTR type")));
			r = FFAT_EIO;
			goto out;
		}
	}
// debug begin
	else
	{
		// nothing to do.
		// there is no cluster for the offset - power off before FAT update
	}
// debug end

	r = FFAT_OK;

out:
	return r;
}


/**
 * redo extended attribute deletion (for power off recovery)
 *
 * @param		pVol				: [IN] volume pointer
 * @param		udwFirstCluster		: [IN] first cluster of extended attribute
 * @param		udwDelOffset		: [IN] offset of current to be deleted
 * @param		pEAMain				: [IN] EAMain
 * @param		pEAEntry			: [IN] EAEntry
 * @param		pCxt				: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] : First Writing
 * @version		DEC-10-2008 [JeongWoo Park] : Bug fix - EAMain has old value, so recalculate valid count, used space
 * @version		MAR-16-2009 [JeongWoo Park] : Bug fix - FLASH00020832
 *												All operation of the log recovery must be write-through.
 *												(write the delete mark with write-through)
 */
FFatErr
ffat_ea_redoDeleteEA(Vol* pVol, t_uint32 udwFirstCluster, t_uint32 udwDelOffset,
					EAMain* pEAMain, EAEntry* pEAEntry, ComCxt* pCxt)
{
	FFatErr			r;
	FatVolInfo*		pVI;
	FFatCacheInfo	stCI;
	t_uint32		udwDelCluster;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pEAEntry);
	FFAT_ASSERT(pCxt);

	pVI = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	// get udwDelCluster from udwDelOffset
	r = FFATFS_GetClusterOfOffset(pVI, udwFirstCluster, udwDelOffset, &udwDelCluster, pCxt);
	FFAT_ASSERT(FFATFS_IS_EOF(pVI, udwDelCluster) == FFAT_FALSE);
	FFAT_EO(r, (_T("fail to get cluster of type")));

#ifdef FFAT_BIG_ENDIAN
	_boEAEntry(pEAEntry);
#endif

	// write type
	FFAT_ASSERT(pEAEntry->ubTypeFlag == EA_ENTRY_USED);
	FFAT_ASSERT((pEAEntry->ubTypeFlag & EA_ENTRY_DELETE) == 0);

	pEAEntry->ubTypeFlag = EA_ENTRY_DELETE;
	
	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
	r = FFATFS_ReadWritePartialCluster(pVI, udwDelCluster, (udwDelOffset & VOL_CSM(pVol)),
										EA_TYPE_FLAG_SIZE, (t_int8*)&(pEAEntry->ubTypeFlag),
										FFAT_FALSE, FFAT_CACHE_SYNC, &stCI, pCxt, FFAT_FALSE);
	IF_UK (r != EA_TYPE_FLAG_SIZE)
	{
		FFAT_LOG_PRINTF((_T("fail to write X-ATTR type")));
		r = FFAT_EIO;
		goto out;
	}

	// write EAMain
	FFAT_ASSERT(pEAMain->uwValidCount > 0);
	FFAT_ASSERT(pEAMain->udwUsedSpace > pEAEntry->udwEntryLength);

	pEAMain->udwUsedSpace -= pEAEntry->udwEntryLength;
	pEAMain->uwValidCount--;

#ifdef FFAT_BIG_ENDIAN
	_boEAMain(pEAMain);
#endif

	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
	r = FFATFS_ReadWritePartialCluster(pVI, udwFirstCluster, 0, sizeof(EAMain),
									(t_int8*)pEAMain, FFAT_FALSE, FFAT_CACHE_SYNC, &stCI, pCxt,FFAT_FALSE);
	IF_UK (r != sizeof(EAMain))
	{
		FFAT_LOG_PRINTF((_T("fail to write EAMain")));
		r = FFAT_EIO;
		goto out;
	}

	r = FFAT_OK;

out:
	return r;
}


////////////// static functions //////////////


/**
 * get EA Main and the first cluster which extended attributes is stored
 * If pVC is not NULL, gather clusters
 *
 * @param		pNode		: [IN] node pointer
 * @param		pEAMain		: [OUT] EAMain
 * @param		pudwCluster	: [OUT] extended attribute cluster number (may be NULL)
 * @param		pVC			: [OUT] vectored clusters of EA (may be NULL)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK 	: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		JUN-27-2008 [GwangOk Go] Remove limitation of total X-ATTR size
 * @version		DEC-04-2008 [JeongWoo Park] rename function name & remove unnecessary code
 * @version		MAR-18-2009 [DongYoung Seo] add EA cluster count update code
 */
static FFatErr
_getEAMain(Node* pNode, EAMain* pEAMain, t_uint32* pudwCluster, FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr		r;
	t_uint32	dwEAFirstCluster;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pCxt);

	// check XATTR mark in NTRes of DE
	if ((NODE_IS_ROOT(pNode) == FFAT_FALSE) &&
		((pNode->stDE.bNTRes & ADDON_SFNE_MARK_XATTR) == 0))
	{
		return FFAT_ENOXATTR;
	}

	// check whether calculate EA first cluster
	if (_NODE_EA_CLUSTER(pNode) == _INIT_EA_CLUSTER)
	{
		IF_LK (NODE_IS_ROOT(pNode) == FFAT_FALSE)
		{
			dwEAFirstCluster = (t_uint32)((FFAT_BO_UINT16(pNode->stDE.wCrtDate) << 16) + (FFAT_BO_UINT16(pNode->stDE.wCrtTime)));
		}
		else
		{
			// Retrieve Root EA first cluster
			r = _getRootEAFirstCluster(NODE_VOL(pNode), &dwEAFirstCluster, pCxt);
			FFAT_ER(r, (_T("fail to read extended attr of Root")));
		}

		// update NODE
		_NODE_EA_CLUSTER(pNode) = dwEAFirstCluster;
	}
	else
	{
		dwEAFirstCluster = _NODE_EA_CLUSTER(pNode);

		FFAT_ASSERT((NODE_IS_ROOT(pNode) == FFAT_TRUE) ? FFAT_TRUE : 
			(dwEAFirstCluster == (t_uint32)((FFAT_BO_UINT16(pNode->stDE.wCrtDate) << 16) + (FFAT_BO_UINT16(pNode->stDE.wCrtTime)))));
	}

	// If no EA, then return
	if (dwEAFirstCluster == _NO_EA_CLUSTER)
	{
		FFAT_ASSERT(_NODE_EA_TOTALSIZE(pNode) == 0);
		r =  FFAT_ENOXATTR;
		goto out;
	}

	FFAT_ASSERT(FFATFS_IsValidCluster(NODE_VI(pNode), dwEAFirstCluster) == FFAT_TRUE);

	// read EAMain
	r = _readEAMain(pNode, pEAMain, pCxt);
	FFAT_ER(r, (_T("fail to get ea main")));

	// check the validity of EAMain
	if (pEAMain->uwSig != EA_SIG)
	{
		// init EA first cluster
		ffat_ea_initNode(pNode);
		r = FFAT_EFAT;
		goto out;
	}

	FFAT_ASSERT(pEAMain->uwValidCount <= FFAT_EA_ATTR_NUMBER_MAX);

	// get vectored cluster of extended attribute
	if (pVC != NULL)
	{
		r = ffat_misc_getVectoredCluster(NODE_VOL(pNode), NULL, dwEAFirstCluster,
										FFAT_NO_OFFSET, 0, pVC, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get vectored cluster")));

		FFAT_ASSERT(dwEAFirstCluster == VC_FC(pVC));
	}

	// set count of EA
	_updateNode(pNode, dwEAFirstCluster, pEAMain->udwTotalSpace);

	// set first cluster of EA
	if (pudwCluster != NULL)
	{
		*pudwCluster = dwEAFirstCluster;
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * create extended attribute area (EAMain & EA clusters)
 *
 * @param		pNode		: [IN] node pointer 
 * @param		pVC			: [OUT] vectored clusters of EA
 * @param		pEAEntry	: [IN] entry of extended attribute
 * @param		pEAInfo		: [IN] extended attribute info
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK 	: success
 * @return		negative	: fail
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 * @version		DEC-04-2008 [JeongWoo Park] rename function &
 *											bug fix about the sequence of core lock / unlock
 * @version		DEC-19-2008 [JeongWoo Park] support Root EA
 * @version		FEB-04-2009 [JeongWoo Park] edit for error value keeping
 * @version		MAR-18-2009 [DongYoung Seo] add EA cluster count update code
 */
static FFatErr
_createEA(Node* pNode, FFatVC* pVC, EAEntry* pEAEntry, FFatXAttrInfo* pEAInfo, ComCxt* pCxt)
{
	FFatErr				r;
	FFatErr				rErr;
	Vol*				pVol;

	EAMain				stEAMain;						// main structure for EA

	t_uint32			dwNewClusters;
	t_uint32			dwFreeCount;
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;

	t_uint32			udwCreateSize;					// used size for EA Info (Main + Entry)
	t_uint32			udwFirstCluster;				// first cluster of EA

	t_boolean			bCoreLocked = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pEAEntry);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pCxt);

	pVol = NODE_VOL(pNode);

	FFAT_ASSERT(VC_CC(pVC) == 0);
	FFAT_ASSERT(VC_VEC(pVC) == 0);

	FFAT_ASSERT(pEAEntry->udwValueSize == (t_uint32)pEAInfo->dwSize);

	udwCreateSize	= sizeof(EAMain) + pEAEntry->udwEntryLength;
	dwNewClusters	= ESS_MATH_CDB(udwCreateSize, VOL_CS(pVol), VOL_CSB(pVol));

	// lock ADDON for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	bCoreLocked = FFAT_TRUE;

	// get free clusters
	r = ffat_misc_getFreeClusters(pNode, dwNewClusters, pVC, 0, &dwFreeCount, FAT_ALLOCATE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to get free clusters")));

	FFAT_ASSERT(dwNewClusters == dwFreeCount);
	FFAT_ASSERT(dwFreeCount == VC_CC(pVC));
	FFAT_ASSERT(VC_VEC(pVC) >= 1);

	udwFirstCluster = VC_FC(pVC);

	if ((VOL_IS_SYNC_META(pVol) == FFAT_TRUE) ||
		(NODE_IS_ROOT(pNode) == FFAT_TRUE))
	{
		// volume is sync mode
		// if root, it need to update BPB as sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// write log
	r = ffat_log_createEA(pNode, pVC, &dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write log")), out_undo_fc);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_MAKE_CHAIN_BEFORE);

	// make cluster chain
	r = ffat_misc_makeClusterChainVC(pNode, 0, pVC, FAT_UPDATE_NONE, dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to allocate cluster")), out_undo_log);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_MAKE_CHAIN_AFTER);

	// set EAMain
	stEAMain.uwSig			= EA_SIG;
	stEAMain.uwCrtTime		= pNode->stDE.wCrtTime;
	stEAMain.uwCrtDate		= pNode->stDE.wCrtDate;
	stEAMain.udwTotalSpace	= udwCreateSize;
	stEAMain.udwUsedSpace	= udwCreateSize;
	stEAMain.uwValidCount	= 1;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_WRITE_MAIN_BEFORE);

	// write EAMain
	r = _writeEAMain(pNode, udwFirstCluster, &stEAMain, dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write ea main")));
		goto out_undo_fat;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_WRITE_MAIN_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_WRITE_ENTRY_BEFORE);

	// write EAEntry & name & value
	r = _writeEAEntry(pNode, pVC, sizeof(EAMain), pEAInfo, pEAEntry, dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write EAEntry")));
		goto out_undo_fat;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_WRITE_ENTRY_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_DE_UPDATE_BEFORE);

	// record EA first cluster
	if (NODE_IS_ROOT(pNode) == FFAT_FALSE)
	{
		// pNode SFN의 CrTime과 CrDate에 cluster번호를 적는다.
		pNode->stDE.wCrtDate = FFAT_BO_UINT16((t_uint16)(udwFirstCluster >> 16));
		pNode->stDE.wCrtTime = FFAT_BO_UINT16((t_uint16)(udwFirstCluster & 0xFFFF));

		// pNode SFN의 CrTime_tenth 에 EA flag를 적는다.
		pNode->stDE.bNTRes |= ADDON_SFNE_MARK_XATTR;

		// directory entry를 update한다.
		r = ffat_node_updateSFNE(pNode, 0, 0, 0, (FAT_UPDATE_DE_WRITE_DE | FAT_UPDATE_DE_NEED_WRITE),
								dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to update directory entry for extended attribute cluster information.")));
			goto out_undo_fat;
		}
	}
	else
	{
		// EA cluster of root is at BPB
		r = _setRootEAFirstCluster(NODE_VOL(pNode), udwFirstCluster, dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to update BPB for root extended attribute cluster information.")));
			goto out_undo_fat;
		}
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_CREATE_EA_DE_UPDATE_AFTER);

	// update NODE
	_updateNode(pNode, udwFirstCluster, stEAMain.udwTotalSpace);

out:
	IF_LK (bCoreLocked == FFAT_TRUE)
	{
		// lock CORE for free cluster sync
		r |= ffat_core_unlock(pCxt);
	}

	return r;

//*** FAIL 처리부
out_undo_fat:
	// deallocate cluster
	rErr = ffat_misc_deallocateCluster(pNode, 0, udwFirstCluster, 0, NULL,
										FAT_UPDATE_DE_NONE, (dwCacheFlag | FFAT_CACHE_FORCE), pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back allocated FAT chain.")));
		r |= rErr;
	}

	VC_VEC(pVC) = 0;

out_undo_log:
	FFAT_ASSERT(FFAT_IS_SUCCESS(r) == FFAT_FALSE);
	rErr = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_EA_CREATE, pNode, pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back for log.")));
		r |= rErr;
	}

out_undo_fc:
	if (VC_VEC(pVC) != 0)
	{
		// add free cluster to FCC
		rErr = ffat_fcc_addFreeClustersVC(pVol, pVC, pCxt);
		IF_UK (rErr < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to add free clusters to FCC")));
			r |= rErr;
		}
	}

	goto out;
}


/**
 * compact extended attributes
 * 
 * @param		pNode			: [IN] node pointer
 * @param		pVCOld			: [IN] vectored cluster
 * @param		pEAMain			: [IN] EAMain
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-05-2008 [GwangOk Go] First Writing.
 * @version		DEC-04-2008 [JeongWoo Park] bug fix about the sequence of core lock / unlock
 * @version		DEC-19-2008 [JeongWoo Park] support Root EA
 * @version		FEB-04-2009 [JeongWoo Park] edit for error value keeping
 * @version		MAR-18-2009 [DongYoung Seo] add EA cluster count update code
 */
static FFatErr
_compactEA(Node* pNode, FFatVC* pVCOld, EAMain* pEAMain, ComCxt* pCxt)
{
	//===========================================
	// 
	// Please write comment to local variables !!!, dyseo, 20081209
	//

	FFatErr		r;
	FFatErr		rErr;
	Vol*		pVol;
	FFatVC		stVCNew;

	t_uint32	udwNewClusterCnt;
	t_uint32	udwFreeCount;
	t_uint32	udwValidCount;
	t_uint32	udwCurOffsetOld;
	t_uint32	udwCurOffsetNew;
	
	EAEntry		stEAEntryCur;
	
	t_int8*		pBuff;

	t_uint32	udwFirstClusterOld;
	t_uint32	udwFirstClusterNew;
	
	t_uint32	udwRemainSize;
	t_uint32	udwCopySize;

	t_uint32	udwIOSize;

	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;

	t_boolean		bCoreLocked = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVCOld);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pCxt);

	pVol = NODE_VOL(pNode);

	VC_INIT(&stVCNew, 0);

	// allocate memory for vectored cluster information
	stVCNew.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(EA_VCE_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVCNew.pVCE);

	stVCNew.dwTotalEntryCount = EA_VCE_BUFF_SIZE / sizeof(FFatVCE);

	// allocate temporary buffer for read/write
	pBuff = FFAT_LOCAL_ALLOC(_EA_TEMP_BUFF_SIZE, pCxt);
	FFAT_ASSERT(pBuff);

	udwNewClusterCnt = ESS_MATH_CDB(pEAMain->udwUsedSpace, VOL_CS(pVol), VOL_CSB(pVol));

	// lock ADDON for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	bCoreLocked = FFAT_TRUE;

	// get free clusters
	r = ffat_misc_getFreeClusters(pNode, udwNewClusterCnt, &stVCNew, 0, &udwFreeCount, FAT_ALLOCATE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to get free clusters for Compaction EA")));

	FFAT_ASSERT(udwNewClusterCnt == udwFreeCount);

	if ((VOL_IS_SYNC_META(pVol) == FFAT_TRUE) ||
		(NODE_IS_ROOT(pNode) == FFAT_TRUE))
	{
		// volume is sync mode
		// if root, it need to update BPB as sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// write log
	r = ffat_log_compactEA(pNode, pVCOld, &stVCNew, &dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to write log")), out_undo_fc);

	udwFirstClusterOld = VC_FC(pVCOld);
	udwFirstClusterNew = VC_FC(&stVCNew);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_MAKE_CHAIN_BEFORE);

	// cluster를 allocation한다.
	r = ffat_misc_makeClusterChainVC(pNode, 0, &stVCNew, FAT_UPDATE_NONE, dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to allocate cluster")), out_undo_log);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_MAKE_CHAIN_AFTER);

	pEAMain->udwTotalSpace = pEAMain->udwUsedSpace;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DATA_WRITE_BEFORE);

	// write new EAMain
	r = _writeEAMain(pNode, udwFirstClusterNew, pEAMain, dwCacheFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write ea main")));
		goto out_undo_fat;
	}

	udwValidCount = pEAMain->uwValidCount;

	udwCurOffsetOld = sizeof(EAMain);
	udwCurOffsetNew = sizeof(EAMain);

	// loop을 돌면서 pList에 name의 저장한다.
	while (udwValidCount > 0)
	{
		// read current EAEntry
		r = ffat_node_readWriteInit(pNode, udwCurOffsetOld, (t_int8*)&stEAEntryCur, sizeof(EAEntry),
								pVCOld, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
		IF_UK ((r < 0) || (udwIOSize != sizeof(EAEntry)))
		{
			FFAT_LOG_PRINTF((_T("fail to read X-ATTR entry")));
			r = FFAT_EIO;
			goto out_undo_fat;
		}

#ifdef FFAT_BIG_ENDIAN
		_boEAEntry(&stEAEntryCur);
#endif

		FFAT_ASSERT((stEAEntryCur.ubTypeFlag & EA_ENTRY_USED) || (stEAEntryCur.ubTypeFlag & EA_ENTRY_DELETE));

		// delete mark를 확인한다. && name 길이를 비교한다.
		if ((stEAEntryCur.ubTypeFlag & EA_ENTRY_DELETE) == 0)
		{
			udwRemainSize = stEAEntryCur.udwEntryLength;

			while (udwRemainSize > 0)
			{
				if (udwRemainSize < _EA_TEMP_BUFF_SIZE)
				{
					udwCopySize = udwRemainSize;
				}
				else
				{
					udwCopySize = _EA_TEMP_BUFF_SIZE;
				}

				// read old X-ATTR data
				r = ffat_node_readWriteInit(pNode, udwCurOffsetOld, pBuff, udwCopySize,
										pVCOld, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
				IF_UK ((r < 0) || (udwIOSize != udwCopySize))
				{
					FFAT_LOG_PRINTF((_T("fail to read X-ATTR data")));
					r = FFAT_EIO;
					goto out_undo_fat;
				}

				// write new X-ATTR data
				r = ffat_node_readWriteInit(pNode, udwCurOffsetNew, pBuff, udwCopySize,
										&stVCNew, &udwIOSize, dwCacheFlag, FFAT_RW_WRITE, pCxt);
				IF_UK ((r < 0) || (udwIOSize != udwCopySize))
				{
					FFAT_LOG_PRINTF((_T("fail to read X-ATTR data")));
					r = FFAT_EIO;
					goto out_undo_fat;
				}

				udwCurOffsetOld += udwCopySize;
				udwCurOffsetNew += udwCopySize;
				udwRemainSize -= udwCopySize;
			}

			udwValidCount--;
		}
		else
		{
			udwCurOffsetOld += stEAEntryCur.udwEntryLength;
		}
	}

	FFAT_ASSERT(pEAMain->udwTotalSpace == udwCurOffsetNew);
	FFAT_ASSERT(udwValidCount == 0);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DATA_WRITE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DE_UPDATE_BEFORE);

	// Record EA first cluster
	if (NODE_IS_ROOT(pNode) == FFAT_FALSE)
	{
		// pNode SFN의 CrTime과 CrDate에 new cluster번호를 적는다.
		pNode->stDE.wCrtDate = FFAT_BO_UINT16((t_uint16)(udwFirstClusterNew >> 16));
		pNode->stDE.wCrtTime = FFAT_BO_UINT16((t_uint16)(udwFirstClusterNew & 0xFFFF));

		FFAT_ASSERT(pNode->stDE.bNTRes & ADDON_SFNE_MARK_XATTR);

		// directory entry를 update한다.(Atime, MTime도 같이)
		r = ffat_node_updateSFNE(pNode, 0, 0, 0,
								(FAT_UPDATE_DE_MTIME | FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_WRITE_DE | FAT_UPDATE_DE_NEED_WRITE),
								dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to update directory entry for extended attribute cluster information.")));
			goto out_undo_fat;
		}
	}
	else
	{
		// EA cluster of root is at BPB
		r = _setRootEAFirstCluster(NODE_VOL(pNode), udwFirstClusterNew, dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to update BPB for root extended attribute cluster information.")));
			goto out_undo_fat;
		}
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DE_UPDATE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DEALLOCATE_CLUSTER_BEFORE);

	// deallocate old clusters
	r = ffat_misc_deallocateCluster(pNode, 0, udwFirstClusterOld, 0, pVCOld,
					FAT_UPDATE_DE_NONE, (dwCacheFlag | FFAT_CACHE_FORCE), pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to deallocat the old EA FAT chain.")));
		goto out_undo_de;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_COMPACT_EA_DEALLOCATE_CLUSTER_AFTER);

	// copy vectored cluster
	pVCOld->dwTotalEntryCount	= stVCNew.dwTotalEntryCount;
	pVCOld->dwClusterOffset		= stVCNew.dwClusterOffset;
	pVCOld->dwTotalClusterCount	= stVCNew.dwTotalClusterCount;
	pVCOld->dwValidEntryCount	= stVCNew.dwValidEntryCount;

	FFAT_MEMCPY(pVCOld->pVCE, stVCNew.pVCE, EA_VCE_BUFF_SIZE);

	// update NODE
	_updateNode(pNode, udwFirstClusterNew, pEAMain->udwTotalSpace);

out:
	IF_LK (bCoreLocked == FFAT_TRUE)
	{
		// lock CORE for free cluster sync
		r |= ffat_core_unlock(pCxt);
	}

	FFAT_LOCAL_FREE(pBuff, _EA_TEMP_BUFF_SIZE, pCxt);
	FFAT_LOCAL_FREE(stVCNew.pVCE, EA_VCE_BUFF_SIZE, pCxt);
	return r;

// FAIL 처리부
out_undo_de:
	// pNode SFN의 CrTime과 CrDate에 old cluster번호를 적는다.
	// write Old Cluster number at CreationTime and Creation Data on SFNE
	pNode->stDE.wCrtDate = FFAT_BO_UINT16((t_uint16)(udwFirstClusterOld >> 16));
	pNode->stDE.wCrtTime = FFAT_BO_UINT16((t_uint16)(udwFirstClusterOld & 0xFFFF));

	// directory entry를 update한다.
	rErr = ffat_node_updateSFNE(pNode, 0, 0, 0, (FAT_UPDATE_DE_WRITE_DE | FAT_UPDATE_DE_NEED_WRITE), dwCacheFlag, pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to update directory entry for extended attribute cluster information.")));
		r |= rErr;
	}

out_undo_fat:
	// deallocate cluster
	rErr = ffat_misc_deallocateCluster(pNode, 0, udwFirstClusterNew, 0, NULL,
					FAT_UPDATE_DE_NONE, (dwCacheFlag | FFAT_CACHE_FORCE), pCxt);
	if (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back allocated FAT chain.")));
		r |= rErr;
	}

	VC_VEC(&stVCNew) = 0;

out_undo_log:
	FFAT_ASSERT(FFAT_IS_SUCCESS(r) == FFAT_FALSE);
	rErr = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_EA_COMPACTION, pNode, pCxt);
	IF_UK (rErr < 0)
	{
		FFAT_LOG_PRINTF((_T("Fail to roll back for log.")));
		r |= rErr;
	}

out_undo_fc:
	if (VC_VEC(&stVCNew) != 0)
	{
		// add free cluster to FCC
		rErr = ffat_fcc_addFreeClustersVC(pVol, &stVCNew, pCxt);
		IF_UK (rErr < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to add free clusters to FCC")));
			r |= rErr;
		}
	}

	goto out;
}


/**
 * lookup EAEntry
 * 
 * @param		pNode			: [IN] node pointer
 * @param		pstEAMain		: [IN] extended attribute main
 * @param		pEAInfo			: [IN] extended attribute info
 * @param		udwNameSize		: [IN] name size to find
 * @param		pVC				: [IN] vectored cluster
 * @param		pEAEntryOut		: [OUT] entry of found attribute
 * @param		pudwCurOffset	: [OUT] offset of found attribute
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-05-2008 [GwangOk Go] First Writing.
 * @version		DEC-09-2008 [JeongWoo Park] modify this to lookup the end of EA by parameter "pudwFreeOffset".
 * @version		DEC-29-2008 [JeongWoo Park] remove the parameter "pudwFreeOffset". the insert position is end of EA.
 */
static FFatErr
_lookupEAEntry(Node* pNode, EAMain* pstEAMain, FFatXAttrInfo* pEAInfo,
				t_uint32 udwNameSize, FFatVC* pVC, EAEntry* pEAEntryOut,
				t_uint32* pudwCurOffset, ComCxt* pCxt)
{
	FFatErr		r;
	t_int8*		pNameBuf = NULL;
	t_uint32	udwCurOffset;
	t_uint32	udwValidCount;
	t_uint32	udwIOSize;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pstEAMain);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pEAEntryOut);
	FFAT_ASSERT(pudwCurOffset);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT((udwNameSize == 0) ? ((pEAInfo->dwNSID == FFAT_XATTR_ID_POSIX_ACL_ACCESS) || (pEAInfo->dwNSID == FFAT_XATTR_ID_POSIX_ACL_DEFAULT)) : FFAT_TRUE);

	udwValidCount = pstEAMain->uwValidCount;
	udwCurOffset = sizeof(EAMain);

	if (pstEAMain->uwValidCount == 0)
	{
		// There is no valid EA entry
		return FFAT_ENOENT;
	}

	if (udwNameSize > 0)
	{
		pNameBuf = FFAT_LOCAL_ALLOC(FFAT_EA_NAME_SIZE_MAX, pCxt);
		FFAT_ASSERT(pNameBuf);
	}

	// *** lookup the entry which is match with psName. if found, return the pudwCurOffset.
	while (udwValidCount > 0)
	{
		// get current entry
		r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)pEAEntryOut, sizeof(EAEntry),
									pVC, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
		IF_UK ((r < 0) || (udwIOSize != sizeof(EAEntry)))
		{
			FFAT_LOG_PRINTF((_T("fail to read X-ATTR entry")));
			r = FFAT_EIO;
			goto out;
		}

#ifdef FFAT_BIG_ENDIAN
		_boEAEntry(pEAEntryOut);
#endif

		FFAT_ASSERT((pEAEntryOut->ubTypeFlag  & EA_ENTRY_USED) || (pEAEntryOut->ubTypeFlag & EA_ENTRY_DELETE));

		// Compare the condition(NSID, name size, name) if the entry is valid
		if ((pEAEntryOut->ubTypeFlag & EA_ENTRY_DELETE) == 0)
		{
			udwValidCount--;

			// *** (1) compare NSID
			if (pEAInfo->dwNSID != pEAEntryOut->ubNameSpaceID)
			{
				goto next;
			}

			// *** (2) compare name size
			if (udwNameSize != pEAEntryOut->uwNameSize)
			{
				goto next;
			}

			// *** (3) compare name
			if (udwNameSize == 0)
			{
				// POSIX_ACL_ACCESS / POSIX_ACL_DEFAULT
				// we found it !!
				*pudwCurOffset = udwCurOffset;
				r = FFAT_OK;
				goto out;
			}
			else
			{
				// USER / TRUSTED / SECURITY
				FFAT_ASSERT(pNameBuf);

				// get current name
				r = ffat_node_readWriteInit(pNode, udwCurOffset + sizeof(EAEntry), pNameBuf, udwNameSize,
											pVC, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
				IF_UK ((r < 0) || (udwIOSize != udwNameSize))
				{
					FFAT_LOG_PRINTF((_T("fail to read X-ATTR entry")));
					r = FFAT_EIO;
					goto out;
				}

				// compare name string
				if (FFAT_STRNCMP((char*)pEAInfo->psName, (char*)pNameBuf, udwNameSize) == 0)
				{
					// we found it !!
					*pudwCurOffset = udwCurOffset;
					r = FFAT_OK;
					goto out;
				}
			}
		} // end of if ((pEAEntry->ubTypeFlag & EA_ENTRY_DELETE) == 0)

next:
		udwCurOffset += pEAEntryOut->udwEntryLength;
	}
	
	r = FFAT_ENOENT;

out:
	if (pNameBuf != NULL)
	{
		FFAT_LOCAL_FREE(pNameBuf, FFAT_EA_NAME_SIZE_MAX, pCxt);
	}
	
	return r;
}


/**
 * get name list & size of attributes
 * if buffer is NULL, get just the size
 * 
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] vectored cluster
 * @param		udwValidCount	: [IN] count of attributes
 * @param		psList			: [IN/OUT] buffer to store name of attributes
 * @param		udwSize			: [IN] size of buffer to store name of attributes
 * @param		pudwListSize	: [OUT] name size of attributes
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_getEAList(Node* pNode, FFatVC* pVC, t_uint32 udwValidCount, t_int8* psList,
			t_uint32 udwSize, t_uint32* pudwListSize, ComCxt* pCxt)
{
	FFatErr			r;
	EAEntry			stEAEntry;
	t_uint32		udwCurOffset;
	t_int32			dwPrefixSize;
	t_uint32		udwTotalSize;
	t_uint32		udwIOSize;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pudwListSize);
	FFAT_ASSERT(pCxt);

	*pudwListSize = 0;
	udwTotalSize = 0;

	if (udwValidCount == 0)
	{
		return FFAT_OK;
	}

	udwCurOffset = sizeof(EAMain);

	// loop을 돌면서 pList에 name의 저장한다.
	while (udwValidCount > 0)
	{
		// read current EAEntry
		r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)&stEAEntry, sizeof(EAEntry),
								pVC, &udwIOSize, FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
		IF_UK ((r < 0) || (udwIOSize != sizeof(EAEntry)))
		{
			FFAT_LOG_PRINTF((_T("fail to read X-ATTR entry")));
			return FFAT_EIO;
		}

#ifdef FFAT_BIG_ENDIAN
		_boEAEntry(&stEAEntry);
#endif

		FFAT_ASSERT((stEAEntry.ubTypeFlag  & EA_ENTRY_USED) || (stEAEntry.ubTypeFlag & EA_ENTRY_DELETE));

		// delete mark를 확인한다. && name 길이를 비교한다.
		if ((stEAEntry.ubTypeFlag & EA_ENTRY_DELETE) == 0)
		{
			dwPrefixSize = _pNameSpaceTable[stEAEntry.ubNameSpaceID - 1].dwLength;

			FFAT_ASSERT((stEAEntry.uwNameSize == 0) ? 
				((stEAEntry.ubNameSpaceID == FFAT_XATTR_ID_POSIX_ACL_ACCESS) || (stEAEntry.ubNameSpaceID == FFAT_XATTR_ID_POSIX_ACL_DEFAULT)) 
				: FFAT_TRUE);

			if (psList != NULL)
			{
				FFAT_ASSERT(udwSize != 0);

				if (udwSize < (udwTotalSize + dwPrefixSize + stEAEntry.uwNameSize + 1))	// including NULL
				{
					FFAT_LOG_PRINTF((_T("the size of list buffer is too small")));
					return FFAT_ERANGE;
				}

				FFAT_MEMCPY(psList, _pNameSpaceTable[stEAEntry.ubNameSpaceID - 1].szPrefix, dwPrefixSize);
				psList += dwPrefixSize;

				if (stEAEntry.uwNameSize != 0)
				{
					// get current name
					r = ffat_node_readWriteInit(pNode, (udwCurOffset + sizeof(EAEntry)), psList,
												(t_uint32)stEAEntry.uwNameSize, pVC, &udwIOSize,
												FFAT_CACHE_NONE, FFAT_RW_READ, pCxt);
					IF_UK ((r < 0) || (udwIOSize != (t_uint32)(stEAEntry.uwNameSize)))
					{
						FFAT_LOG_PRINTF((_T("fail to read X-ATTR name")));
						return FFAT_EIO;
					}

					psList += stEAEntry.uwNameSize;
				}

				*psList++ = '\0';					// including NULL
			}

			udwTotalSize += (dwPrefixSize + stEAEntry.uwNameSize + 1);	// including NULL
			udwValidCount--;
		}

		udwCurOffset += stEAEntry.udwEntryLength;
	}

	*pudwListSize = udwTotalSize;

	return FFAT_OK;
}


/**
 * read EAMain
 * 
 * @param		pNode			: [IN] node pointer
 * @param		pEAMain			: [OUT] EAMain
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_readEAMain(Node* pNode, EAMain* pEAMain, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pCxt);

	// read EAMain
	r = ffat_readWritePartialCluster(NODE_VOL(pNode), pNode, _NODE_EA_CLUSTER(pNode),
									0, sizeof(EAMain), (t_int8*)pEAMain,
									FFAT_TRUE, FFAT_CACHE_NONE, pCxt);
	IF_UK (r != sizeof(EAMain))
	{
		FFAT_LOG_PRINTF((_T("Fail to read EAMain")));
		return FFAT_EIO;
	}

#ifdef FFAT_BIG_ENDIAN
	_boEAMain(pEAMain);
#endif

	return FFAT_OK;
}


/**
 * write EAMain
 * 
 * @param		pNode			: [IN] node pointer
 * @param		udwCluster		: [IN] cluster to write
 * @param		pEAMain			: [IN] EAMain
 * @param		dwCacheFlag		: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_writeEAMain(Node* pNode, t_uint32 udwCluster, EAMain* pEAMain, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pCxt);

#ifdef FFAT_BIG_ENDIAN
	_boEAMain(pEAMain);
#endif

	// write EAMain
	r = ffat_readWritePartialCluster(NODE_VOL(pNode), pNode, udwCluster, 0, sizeof(EAMain),
									(t_int8*)pEAMain, FFAT_FALSE, dwCacheFlag, pCxt);
	IF_UK (r != sizeof(EAMain))
	{
		FFAT_LOG_PRINTF((_T("Fail to write EAMain")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}


/**
 * write EAEntry & Name & Value
 * 
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] vectored cluster
 * @param		udwCurOffset	: [IN] offset to write
 * @param		pEAInfo			: [IN] EAInfo
 * @param		pEAEntry		: [IN] EAEntry
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_writeEAEntry(Node* pNode, FFatVC* pVC, t_uint32 udwCurOffset, FFatXAttrInfo* pEAInfo,
				EAEntry* pEAEntry, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_int32			dwNameSize;
	t_uint32		udwIOSize;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pEAInfo);
	FFAT_ASSERT(pEAEntry);
	FFAT_ASSERT(pCxt);

	dwNameSize = pEAEntry->uwNameSize;	// backup for byte ordering

#ifdef FFAT_BIG_ENDIAN
	_boEAEntry(pEAEntry);
#endif

	// write EAEntry
	r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)pEAEntry, sizeof(EAEntry),
								pVC, &udwIOSize, dwCacheFlag, FFAT_RW_WRITE, pCxt);
	IF_UK ((r < 0) || (udwIOSize != sizeof(EAEntry)))
	{
		r = FFAT_EIO;
		FFAT_LOG_PRINTF((_T("fail to write X-ATTR entry info")));
		goto out;
	}

	udwCurOffset += sizeof(EAEntry);

	// write name (start at first cluster)
	r = ffat_node_readWriteInit(pNode, udwCurOffset, (t_int8*)pEAInfo->psName, (t_uint32)dwNameSize,
								pVC, &udwIOSize, dwCacheFlag, FFAT_RW_WRITE, pCxt);
	IF_UK ((r < 0) || (udwIOSize != (t_uint32)dwNameSize))
	{
		r = FFAT_EIO;
		FFAT_LOG_PRINTF((_T("fail to write X-ATTR name")));
		goto out;
	}

	udwCurOffset += dwNameSize;

	// write value (may not start at first cluster)
	r = ffat_node_readWriteInit(pNode, udwCurOffset, pEAInfo->pValue, (t_uint32)pEAInfo->dwSize,
							pVC, &udwIOSize, dwCacheFlag, FFAT_RW_WRITE, pCxt);
	IF_UK ((r < 0) || (udwIOSize != (t_uint32)(pEAInfo->dwSize)))
	{
		r = FFAT_EIO;
		FFAT_LOG_PRINTF((_T("fail to write X-ATTR value")));
		goto out;
	}

	r = FFAT_OK;

out:
	return r;
}

/**
* check EA name size with  NSID in pEAInfo, return name size
* 
* @param		pEAInfo			: [IN] EA info
* @param		pudwNameSize	: [OUT] name size of EA
* @return		FFAT_OK 		: success
* @return		negative		: fail
* @author		JeongWoo Park
* @version		AUG-06-2008 [JeongWoo Park] First Writing.
*/
static FFatErr
_getEANameSize(FFatXAttrInfo* pEAInfo, t_uint32* pudwNameSize)
{
	t_uint32	udwNameSize;

	FFAT_ASSERT(pEAInfo->psName);
	udwNameSize = FFAT_STRLEN(pEAInfo->psName);		// byte size

	IF_UK (udwNameSize > FFAT_EA_NAME_SIZE_MAX)
	{
		FFAT_LOG_PRINTF((_T("too long name")));
		return FFAT_EINVALID;
	}

	if (udwNameSize == 0)
	{
		if ((pEAInfo->dwNSID != FFAT_XATTR_ID_POSIX_ACL_ACCESS) &&
			(pEAInfo->dwNSID != FFAT_XATTR_ID_POSIX_ACL_DEFAULT))
		{
			FFAT_LOG_PRINTF((_T("Invalid parameter")));
			return FFAT_EINVALID;
		}
	}
	else
	{
		if ((pEAInfo->dwNSID != FFAT_XATTR_NS_USER) &&
			(pEAInfo->dwNSID != FFAT_XATTR_NS_TRUSTED) && (pEAInfo->dwNSID != FFAT_XATTR_NS_SECURITY))
		{
			FFAT_LOG_PRINTF((_T("Invalid parameter")));
			return FFAT_EINVALID;
		}
	}

	*pudwNameSize = udwNameSize;

	return FFAT_OK;
}

/**
* read EA first cluster for Root
*
* @param		pVol				: [IN] Volume structure
* @param		pdwRootEACluster	: [OUT] first cluster of Root EA
* @param		pCxt				: [IN] context of current operation
* @return		FFAT_OK				: success
* @return		negative			: fail
* @author		JeongWoo Park
* @version		DEC-19-2008 [JeongWoo Park] First Writing
*/
static FFatErr
_getRootEAFirstCluster(Vol* pVol, t_uint32* pdwRootEACluster, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	ExtendedAttr	stEA;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwRootEACluster);
	FFAT_ASSERT(pCxt);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	// read the extended attribute of root from BPB
	r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_EA_OFFSET,
						sizeof(ExtendedAttr), (t_int8*)&stEA,
						(FFAT_CACHE_DATA_FS | FFAT_CACHE_DIRECT_IO | FFAT_CACHE_META_IO),
						&stCI, FFAT_TRUE, pCxt);
	if (r != sizeof(ExtendedAttr))
	{
		FFAT_LOG_PRINTF((_T("fail to read root EA first cluster from BPB")));
		return FFAT_EIO;
	}

	// check validity of root EA
	if (FFAT_STRNCMP((char*)stEA.szSignature, FFAT_EA_SIGNATURE, FFAT_EA_SIGNATURE_SIZE) == 0)
	{
		*pdwRootEACluster = FFAT_BO_UINT32(stEA.dwEAFirstCluster);
	}
	else
	{
		// root has no EA
		*pdwRootEACluster = _NO_EA_CLUSTER;
	}

	return FFAT_OK;
}

/**
* write EA first cluster for Root
*
* @param		pVol			: [IN] Volume structure
* @param		dwRootEACluster	: [IN] first cluster of Root EA
*										If 0, erase Root EA
* @param		dwCacheFlag		: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		negative		: fail
* @author		JeongWoo Park
* @version		DEC-19-2008 [JeongWoo Park] First Writing
* @version		MAR-18-2009 [DongYoung Seo] add EA cluster count update code
*/
static FFatErr
_setRootEAFirstCluster(Vol* pVol, t_uint32 dwRootEACluster,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	ExtendedAttr	stEA;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(dwCacheFlag & FFAT_CACHE_SYNC);	// now only support sync mode for BPB update

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	// generate Extended attribute for root
	if (dwRootEACluster == 0)
	{
		// if 0, erase EA area
		FFAT_MEMSET(stEA.szSignature, 0x00, FFAT_EA_SIGNATURE_SIZE);
		stEA.dwEAFirstCluster = 0;
	}
	else
	{
		FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), dwRootEACluster) == FFAT_TRUE);

		FFAT_MEMCPY(stEA.szSignature, FFAT_EA_SIGNATURE, FFAT_EA_SIGNATURE_SIZE);
		stEA.dwEAFirstCluster = FFAT_BO_UINT32(dwRootEACluster);
	}

	// write the extended attribute of root from BPB
	r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_EA_OFFSET,
										sizeof(ExtendedAttr), (t_int8*)&stEA,
										(dwCacheFlag | FFAT_CACHE_DATA_FS),
										&stCI, FFAT_FALSE, pCxt);
	if (r != sizeof(ExtendedAttr))
	{
		FFAT_LOG_PRINTF((_T("fail to write root EA first cluster to BPB")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}


/**
* update EA information for a node
*
* @param		pNode			: [IN] node pointer
* @param		dwFirstCluster	: [IN] first cluster of EA
* @param		dwTotalSize		: [IN] total size of EA (in byte)
* @author		DongYoung Seo
* @version		MAR-18-2009 [DongYoung Seo] First Writing.
*/
static void
_updateNode(Node* pNode, t_uint32 dwFirstCluster, t_uint32 dwTotalSize)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_VOL(pNode));

	// set as need to calculate the EA start cluster
	_NODE_EA_CLUSTER(pNode)		= dwFirstCluster;
	_NODE_EA_TOTALSIZE(pNode)	= dwTotalSize;

	FFAT_ASSERT(dwFirstCluster <= VOL_LCN(NODE_VOL(pNode)));
	FFAT_ASSERT((FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), dwFirstCluster) == FFAT_TRUE) ? (dwTotalSize >= sizeof(EAMain)) : FFAT_TRUE);

	return;
}


#ifdef FFAT_BIG_ENDIAN
	/**
	 * change the byte order of EAMain structure
	 *
	 * @param		pEAMain		: [IN] EAMain pointer
	 * @return		FFAT_OK		: success
	 * @return		negative	: fail
	 * @author		GwangOk Go
	 * @version		AUG-06-2008 [GwangOk Go] First Writing.
	 */
	static void
	_boEAMain(EAMain* pEAMain)
	{
		FFAT_ASSERT(pEAMain);

		pEAMain->uwSig			= FFAT_BO_UINT16(pEAMain->uwSig);
		pEAMain->uwCrtTime		= FFAT_BO_UINT16(pEAMain->uwCrtTime);
		pEAMain->uwCrtDate		= FFAT_BO_UINT16(pEAMain->uwCrtDate);
		pEAMain->uwValidCount	= FFAT_BO_UINT16(pEAMain->uwValidCount);
		pEAMain->udwTotalSpace	= FFAT_BO_UINT32(pEAMain->udwTotalSpace);
		pEAMain->udwUsedSpace	= FFAT_BO_UINT32(pEAMain->udwUsedSpace);
	}

	/**
	 * change the byte order of EAEntry structure
	 *
	 * @param		pEAEntry	: [IN] EAEntry pointer
	 * @return		FFAT_OK		: success
	 * @return		negative	: fail
	 * @author		GwangOk Go
	 * @version		AUG-06-2008 [GwangOk Go] First Writing.
	 */
	static void
	_boEAEntry(EAEntry* pEAEntry)
	{
		FFAT_ASSERT(pEAEntry);

		pEAEntry->uwNameSize		= FFAT_BO_UINT16(pEAEntry->uwNameSize);
		pEAEntry->udwEntryLength	= FFAT_BO_UINT32(pEAEntry->udwEntryLength);
		pEAEntry->udwValueSize		= FFAT_BO_UINT32(pEAEntry->udwValueSize);
	}
#endif
