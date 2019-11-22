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
 * @file		ffat_addon_chkdsk.c
 * @brief		Check disk
 * @author		Zhang Qing
 * @version		SEP-19-2006 [Zhang Qing] First writing
 * @version		JAN-13-2010 [ChunUm Kong] Modifying comment (English/Korean)
 * @see			None
 */

#include "ess_types.h"
#include "ess_bitmap.h"
#include "ess_math.h"
#include "ess_debug.h"

#include "ffat_common.h"

#include "ffat_api.h"
#include "ffat_al.h"
#include "ffat_node.h"
#include "ffat_dir.h"
#include "ffat_main.h"
#include "ffat_share.h"
#include "ffatfs_api.h"

#include "ffat_addon_chkdsk.h"
#include "ffat_addon_hpa.h"
#include "ffat_addon_types_internal.h"
#include "ffat_addon_xattr.h"

// #define _CHKDSK_PRINT_CLUSTER_CHAIN
#if defined(FFAT_DEBUG) && !defined(BPB_CORRUPT_ON_CHKDSK_ERROR)
	#define	BPB_CORRUPT_ON_CHKDSK_ERROR
#endif


#define _CHKDSK_MEM_SIZE			(8 * 1024)		//!< maximum 8k memory usage during chkdsk
#define _CHKDSK_MEM_SIZE_IN_BIT		(3 + 10)		//!< bit number of _CHKDSK_MEM_SIZE

#define _HT_BUCKET_SIZE				(128)			//!< hash table size for storing first clusters of un-finished chains
#define _HT_BUCKET_SIZE_MASK		(0x007F)		//!< hash table size mask, used for get hash value
#define _HT_ENTRY_SIZE				(_HT_BUCKET_SIZE * 2)

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_CHKDSK)

#define _SET_UPDATED()			(_bUpdated = FFAT_TRUE)
#define _IS_UPDATED()			(_bUpdated)
#define _PNODE(_p)				((Node*)(_p))
#define _PFATGETNODEDE(_p)		((FatGetNodeDe*)(_p))
#define _CHKDSKRANGE(_p)		((ChkdskRange*)(_p))
#define _PESSHASH(_p)			((EssHash*)(_p))
#define _PESSDLIST(_p)			((EssDList*)(_p))
#define _PHASHCLUSTERENTRY(_p)	((_HashClusterEntry*)(_p))

#define _RANGE_START	(_CHKDSKRANGE(_gpChkRange)->dwRangeStart)
#define _RANGE_END		(_CHKDSKRANGE(_gpChkRange)->dwRangeEnd)

#define _HASH_VALUE(dwCluster) ((dwCluster) & _HT_BUCKET_SIZE_MASK)

// dwF	: chkdsk flag
#define _NEED_REPAIR(_dwF)	(((_dwF) & (FFAT_CHKDSK_REPAIR_INTERACTIVE | FFAT_CHKDSK_REPAIR)) ? FFAT_TRUE : FFAT_FALSE)
#define _SHOW_MSG(_dwF)		((_dwF) & (FFAT_CHKDSK_SHOW))
#define _IS_CHECK_ONLY(_dw)	(((_dw) & (FFAT_CHKDSK_CHECK_ONLY)) ? FFAT_TRUE : FFAT_FALSE)

#define _CHKDSK_PRINT_VERBOSE(msg, _dwFlag)	if (_SHOW_MSG(_dwFlag)) { \
												FFAT_PRINT_VERBOSE(msg); \
											}
#define _CHKDSK_PRINT_ERROR(msg)			FFAT_PRINT_CRITICAL(msg);
#define _CHKDSK_PRINT_CRITICAL(msg)			FFAT_PRINT_CRITICAL(msg);

#define _CLUSTER_HT_BUCKET_SIZE			(128)			//!< hash table size for storing clusters
#define _CLUSTER_HT_BUCKET_SIZE_MASK	(0x007F)		//!< hash table size mask, used for get hash value
#define _CLUSTER_HT_ENTRY_SIZE			(_CLUSTER_HT_BUCKET_SIZE * 2) //!< hash table entry size

#define _CHKDSK_DIR_STACK_ENTRY_COUNT	(32)			//!< chkdsk dir stack entry count(must be 2^n)

// types
typedef struct
{
	t_uint32 dwRangeStart;
	t_uint32 dwRangeEnd;
} ChkdskRange;

typedef struct _HashClusterEntry
{
	EssHashEntry	stListHash;
	t_uint32		dwCluster;
} _HashClusterEntry;

typedef struct _ChkDirStackEntry
{
	t_uint32	dwParentCluster;	// cluster of parent
	t_uint32	dwCluster;			// cluster of itself
	t_uint32	dwOffset;			// byte offset from dwCluster
} ChkDirStackEntry;

typedef struct _ChkDirStack
{
	EssDList			stDList;
	ChkDirStackEntry	pStackEntry[_CHKDSK_DIR_STACK_ENTRY_COUNT];
} ChkDirStack;

typedef struct _ChkDirStackMgr
{
	EssDList			stStackHead;
	t_uint32			dwSP;
} ChkDirStackMgr;

static t_boolean		_bUpdated			= FFAT_FALSE;	// check volume update status

static t_uint8*			_gpMap				= NULL;
static Node*			_gpNode				= NULL;		// Node*
static t_int32*			_gpNodeDE			= NULL;		// FatGetNodeDe*
static t_wchar*			_gsCurName			= NULL;		// current dir/file name
static char*			_gsCurName8			= NULL;

static t_int32*			_gpChkRange			= NULL;		// ChkdskRange

static t_int32*			_gpUnFinishedChain	= NULL;		// EssHash*
static t_int32*			_gpHashTable		= NULL;		// hash table for storing first clusters of un-finished chains
														// EssDList*
static t_int32*			_gpEntries			= NULL;		// used for storing all hash table entries
														// _HashClusterEntry*

static t_uint32			_gdwHTEntryCount	= 0;		// number of hash table entries
static t_uint32			_gdwUFCRangeEnd	= 0;		// [0, gdwUFCRangeEnd]: area that unfinished chain records

static ChkDirStackMgr	_gstDirStackMgr;

// static functions
static FFatErr		_chkdskInit(Vol* pVol, ComCxt* pCxt);
static FFatErr		_chkdskRelease(Vol* pVol, ComCxt* pCxt);
static FFatErr		_mark_set(t_uint32 dwClusterNumber, FFatChkdskFlag dwFlag);
static FFatErr		_clusterHashInit(EssHash* pClusterHash, EssDList* pHashTable,
							_HashClusterEntry* pEntries, t_int32 dwBucketSize,
							t_int32 dwEntrySize);
static FFatErr		_clusterHashAdd(EssHash* pClusterHash, 
							t_uint32 dwClusterNumber, t_int32 dwHashVal);
static t_boolean	_clusterHashHasEntry(EssHash* pClusterHash, 
							t_uint32 dwClusterNumber, t_int32 dwHashVal);
static FFatErr		_clusterHashRemove(EssHash* pClusterHash, 
							t_uint32 dwClusterNumber, t_int32 dwHashVal);
static FFatErr		_checkDir(Vol* pVol, t_uint32 dwCluster, t_uint32 dwParentCluster,
							FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_checkFAT(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_checkPathLen(Node* pNode, t_uint32 dwLFNLen, FFatChkdskFlag dwFlag);
static t_boolean	_markChainIsNeeded(Vol* pVol, t_uint32 dwStartCluster);
static FFatErr		_markChain(Vol* pVol, t_uint32 dwStartCluster, t_uint32* pdwClusterCount,
						t_boolean* pbAllMarked, FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_checkOpenUnlinked(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_markClustersHPA(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_checkEA(Node* pNode, FFatChkdskFlag dwFlag, ComCxt* pCxt);
static FFatErr		_chkDirStackInit(void);
static FFatErr		_chkDirStackTerminate(void);
static FFatErr		_chkDirStackPush(t_uint32 dwParentCluster, t_uint32 dwCluster, t_uint32 dwOffset);
static FFatErr		_chkDirStackPop(t_uint32* pdwParentCluster, t_uint32* pdwCluster, t_uint32* pdwOffset);


/** 
 * ffat_addon_chkdsk check the state of file system
 * 
 * @param		pVol		: [IN] volume pointer
 * @param		dwFlag		: [IN] flag for CHKDSK
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		Zhang Qing
 * @version		SEP-24-2006 [Zhang Qing] First Writing.
 * @version		26-NOV-2008 [DongYoung Seo] add cluster checking code for open unlinked node
 * @version		26-NOV-2008 [JeongWoo Park] FFAT_CHKDSK_REPAIR for read-only volume will
 *											return FFAT_EACCESS instead of ASSERT
 */
FFatErr
ffat_addon_chkdsk(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;
	t_uint32	i;
	t_uint32	dwTimes;				//checking times
	t_uint32	dwRootCluster;
	t_uint32	dwRootParentCluster;
	FFAT_ASSERT(pVol);

	if (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_PRINT_VERBOSE((_T("volume is not ready!")));
		return FFAT_EACCESS;
	}

	r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
	IF_UK (r < 0)
	{
		FFAT_PRINT_VERBOSE((_T("Fail to sync FCC")));
		return r;
	}

	// check busy state
	if (VOL_IS_BUSY(pVol) == FFAT_TRUE)
	{
		FFAT_PRINT_DEBUG((_T("the volume is in busy state")));
	}

	if ((VOL_IS_RDONLY(pVol) == FFAT_TRUE) &&
		(dwFlag & (FFAT_CHKDSK_REPAIR | FFAT_CHKDSK_REPAIR_INTERACTIVE)))
	{
		FFAT_PRINT_VERBOSE((_T("FFAT_CHKDSK_REPAIR | FFAT_CHKDSK_REPAIR_INTERACTIVE can not be used for read-only volume")));
		return FFAT_EROFS;
	}

	r = _chkdskInit(pVol, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	dwTimes = ESS_MATH_CDB(VOL_CC(pVol), (_CHKDSK_MEM_SIZE << 3),
								(_CHKDSK_MEM_SIZE_IN_BIT + 3));

	for (i = 0; i < dwTimes; i++)
	{
		FFAT_MEMSET(_gpMap, 0x00, _CHKDSK_MEM_SIZE);

		_RANGE_START	= i << (_CHKDSK_MEM_SIZE_IN_BIT + 3);
		_RANGE_END		= ((i + 1) << (_CHKDSK_MEM_SIZE_IN_BIT + 3)) - 1;

		if (_RANGE_END > VOL_LCN(pVol))
		{
			_RANGE_END = VOL_LCN(pVol);
		}

		// mark cluster 0 and 1
		_mark_set(0, dwFlag);
		_mark_set(1, dwFlag);

		r = _markClustersHPA(pVol, dwFlag, pCxt);
		FFAT_EO(r, (_T("Fail to mark clsuters for HPA")));

		_CHKDSK_PRINT_VERBOSE((_T("<<< Start checking Volume >>>\n")), dwFlag);
		
		dwRootCluster		= NODE_C(VOL_ROOT(pVol));
		dwRootParentCluster	= NODE_COP(VOL_ROOT(pVol));

		r = _checkDir(pVol, dwRootCluster, dwRootParentCluster, dwFlag, pCxt);
		if (r < 0)
		{
			FFAT_EO(r, (_T("there is some error on the disk or fail to repair it")));
		}

		if (ffat_hpa_isActivated(pVol) == FFAT_TRUE)
		{
			HPAInfo*	pInfo;
			pInfo = VOL_ADDON(pVol)->pHPA;
			
			_CHKDSK_PRINT_VERBOSE((_T("<<< Start checking HPA >>>\n")), dwFlag);
			r = _checkDir(pVol, pInfo->dwRootCluster, dwRootParentCluster, dwFlag, pCxt);
			if (r < 0)
			{
				FFAT_EO(r, (_T("fail to check HPA")));
			}
		}

		// we can check open-unlinked cluster for only LOG-ON
		if (FFAT_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE)
		{
			// update clusters for open unlinked nodes
			r = _checkOpenUnlinked(pVol, dwFlag, pCxt);
			FFAT_EO(r, (_T("Fail to check open unlinked node")));
		}

		// check FAT table
		r = _checkFAT(pVol, dwFlag, pCxt);
		FFAT_EO(r, (_T("fail to check FAT")));
	}

#ifdef FFAT_DEBUG
	// check HPA
	ffat_hpa_checkTCCOfHPA(pVol, pCxt);
#endif

out:
	
	_chkdskRelease(pVol, pCxt);

	if ((_IS_UPDATED() == FFAT_TRUE) && 
			(ffat_vol_sync(pVol, FFAT_FALSE, pCxt) < 0))
	{
		FFAT_PRINT_DEBUG((_T("failed to synchronize volume!")));
	}

	if (r != FFAT_OK)
	{
#ifdef BPB_CORRUPT_ON_CHKDSK_ERROR
		if ((VOL_IS_RDONLY(pVol) == FFAT_FALSE) && 
			(FFAT_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE))
		{
			t_int8*	pBuffTemp = NULL;
			FFatErr	rr;

			pBuffTemp = FFAT_LOCAL_ALLOC(pVol->stDevInfo.dwDevSectorSize, pCxt);
			FFAT_ASSERT(pBuffTemp != NULL);

			rr = ffat_readWriteSectors(pVol, NULL, 0, 1, pBuffTemp,
										(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC),
										FFAT_TRUE, pCxt);
			if (rr != 1)
			{
				FFAT_LOCAL_FREE(pBuffTemp, pVol->stDevInfo.dwDevSectorSize, pCxt);
				FFAT_PRINT_DEBUG((_T("BPB Read is failed!")));
				return r;
			}

			pBuffTemp[510] = 0xA5;
			pBuffTemp[511] = 0x5A;

			rr = ffat_readWriteSectors(pVol, NULL, 0, 1, pBuffTemp,
										(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC),
										FFAT_FALSE, pCxt);
			if (rr != 1)
			{
				FFAT_LOCAL_FREE(pBuffTemp, pVol->stDevInfo.dwDevSectorSize, pCxt);
				FFAT_PRINT_DEBUG((_T("BPB Overwrite is failed")));
				return r;
			}

			FFAT_LOCAL_FREE(pBuffTemp, pVol->stDevInfo.dwDevSectorSize, pCxt);
		}
#endif
		FFAT_PRINT_DEBUG((_T("chkdsk completed - disk has some problem!")));
	}
	else
	{
		FFAT_PRINT_DEBUG((_T("chkdsk completed - disk has no problem.\n")));
	}

	return r;
}


//=============================================================================
//
//	STATIC FUNCTIONS
//


/** 
* check cluster chain for open unlinked node
* 
* @param		pVol		: [IN] volume pointer
* @param		dwFlag		: [IN] flag for CHKDSK
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		else		: error
* @author		DongYoung Seo
* @version		27-NOV-2008 [DongYoung Seo] First Writing
*/
static FFatErr
_checkOpenUnlinked(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	t_int32		dwIndex;				// index for open unlink
	t_uint32	dwCluster;				// cluster number storage for open unlink
	t_boolean	bAllMarked;
	t_uint32	dwClusterCount;			// count of cluster
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	dwIndex = -1;
	do
	{
		dwIndex++;

		r = ffat_log_getClusterOfOUEntry(pVol, dwIndex, &dwCluster, pCxt);
		if (r < 0)
		{
			if (r == FFAT_ENOENT)
			{
				continue;
			}
			else if (r == FFAT_EINVALID)
			{
				// this is end of entry, no more entry
				break;
			}
			else
			{
				// I/O ERROR
				FFAT_PRINT_DEBUG((_T("Fail to get cluster information from log module for open unlink")));
				goto out;
			}
		}

		FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

		// check the open unlinked cluster chain
		if (_markChainIsNeeded(pVol, dwCluster) == FFAT_TRUE)
		{
			_CHKDSK_PRINT_VERBOSE((_T(">> Checking open unlinked cluster :%d\n"), dwCluster), dwFlag);

			dwClusterCount = 0;

			r = _markChain(pVol, dwCluster, &dwClusterCount, &bAllMarked, dwFlag, pCxt);
			if ((r == FFAT_OK) && (bAllMarked == FFAT_TRUE))
			{
				_CHKDSK_PRINT_VERBOSE((_T("-- Finished open unlinked cluster:%d\n"), dwCluster), dwFlag);
			}

			if (r < 0)
			{
				r = FFAT_ENOSUPPORT;
				_CHKDSK_PRINT_ERROR((_T("Failed to mark FAT chain bitmap!\n")));
				goto out;
			}
		}
	} while(1);

	FFAT_ASSERT((dwIndex == (LOG_OPEN_UNLINK_ENTRY_SLOT * LOG_OPEN_UNLINK_SLOT)) || (dwIndex == 0));

	r = FFAT_OK;

out:
	return r;
}


/** 
* _hash_release releases all allocated memory related to hash table
* 
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr 
_hash_release(Vol* pVol, ComCxt* pCxt)
{
	FFAT_LOCAL_FREE(_gpEntries, (sizeof(_HashClusterEntry) * _HT_ENTRY_SIZE), pCxt);
	_gpEntries = NULL;

	FFAT_LOCAL_FREE(_gpHashTable, (sizeof(EssDList) * _HT_BUCKET_SIZE), pCxt);
	_gpHashTable = NULL;

	FFAT_LOCAL_FREE(_gpUnFinishedChain, sizeof(EssHash), pCxt);
	_gpUnFinishedChain = NULL;

	return FFAT_OK;
}


/** 
* _hash_init initializes memory allocation related to hash table 
* 
* @param	pVol		: [IN] volume pointer
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success
* @return	FFAT_ENOMEM	: no enough memory
* @author	Zhang Qing
* @version	SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr 
_hash_init(Vol* pVol, ComCxt* pCxt)
{
	FFatErr 	r;

	_gpUnFinishedChain = (t_int32 *) FFAT_LOCAL_ALLOC(sizeof(EssHash), pCxt);
	FFAT_ASSERT(_gpUnFinishedChain != NULL);

	_gpHashTable = (t_int32*) FFAT_LOCAL_ALLOC((sizeof(EssDList) * _HT_BUCKET_SIZE), pCxt);
	FFAT_ASSERT(_gpHashTable != NULL);

	_gpEntries = (t_int32*)FFAT_LOCAL_ALLOC((sizeof(_HashClusterEntry) * _HT_ENTRY_SIZE), pCxt);
	FFAT_ASSERT(_gpEntries != NULL);

	r = _clusterHashInit(_PESSHASH(_gpUnFinishedChain), 
		_PESSDLIST(_gpHashTable), _PHASHCLUSTERENTRY(_gpEntries), 
		_HT_BUCKET_SIZE, _HT_ENTRY_SIZE);
	if (r < 0)
	{
		goto out;
	}

	_gdwHTEntryCount = 0;

	r = FFAT_OK;

out:
	if (r != FFAT_OK)
	{
		_hash_release(pVol, pCxt);
	}

	return r;
}


/** 
* _mark_is_set checks whether a cluster is marked or not
* 
* @param dwClusterNumber 	: [IN] the cluster to be checked
* @param dwFlag 			: [IN] chkdsk flag
*  
* @return FFAT_FALSE : the cluster is not marked
* @return FFAT_TRUE  : the cluster is marked
* @author Zhang Qing
* @version SEP-24-2006 [Zhang Qing] First Writing.
*/
static t_boolean
_mark_is_set(t_uint32 dwClusterNumber)
{
	t_uint32 dwConvertedCN; //converted cluster number

	if ((dwClusterNumber < _RANGE_START) || (dwClusterNumber > _RANGE_END))
	{
		return FFAT_FALSE;
	}

	dwConvertedCN = dwClusterNumber - _RANGE_START;
	if (ESS_BITMAP_IS_SET(_gpMap, dwConvertedCN))
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/** 
* @brief _mark_set marks a cluster
* 
* @param	dwCluster	: [IN] the cluster to be marked
* @param	dwFlag		: [IN] chkdsk flag
* 
* @return	FFAT_OK		: dwCluster can be marked, or already be marked.
* @return	FFAT_OK1	: dwCluster need not be marked.
* @return	FFAT_EFAT	: dwCluster is duplicated.
* @author	Zhang Qing
* @version	SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_mark_set(t_uint32 dwCluster, FFatChkdskFlag dwFlag)
{
	t_uint32 dwConvertedCN; //converted cluster number

#ifdef _CHKDSK_PRINT_CLUSTER_CHAIN
	_CHKDSK_PRINT_VERBOSE((_T("-%d"), dwCluster), dwFlag);
#endif

	if (dwCluster > _RANGE_END)
	{
		return FFAT_OK1;
	}

	if (dwCluster < _RANGE_START)
	{
		return FFAT_OK;
	}

	IF_UK (_mark_is_set(dwCluster))
	{
		_CHKDSK_PRINT_VERBOSE((_T("duplicated FAT chain value: %d\n"), dwCluster), dwFlag);
		FFAT_ASSERT(0);
		return FFAT_EFAT;
	}

	dwConvertedCN = dwCluster - _RANGE_START;
	ESS_BITMAP_SET(_gpMap, dwConvertedCN);
	return FFAT_OK;
}


/**
* _markChainIsNeeded checks the marking operation at a FAT chain whether it is needed or not
*
* @param		dwStartCluster : [IN] start cluster of a FAT chain
*
* @return		FFAT_FALSE : not need to mark a FAT chain
* @return		FFAT_TRUE  : need to mark a FAT chain
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static t_boolean
_markChainIsNeeded(Vol* pVol, t_uint32 dwStartCluster)
{
	t_boolean	bNeedMark;

	// check we need to mark the chain or not.
	// Mark chain when
	// 1. dwStartCluster >= _RANGE_START
	// 2. If unfinished hash table is full, dwStartCluster >= area that unfinished chain records
	// 3. dwStartCluster is in unfinished hash table

	// 0. To root cluster of FAT16, do not mark chain
	// since there is no FAT chain to root of FAT16
	if ((VOL_IS_FAT16(pVol) == FFAT_TRUE) && (VI_RC(VOL_VI(pVol)) == dwStartCluster))
	{
		return FFAT_FALSE;
	}

	bNeedMark = FFAT_FALSE;

	if (dwStartCluster >= _RANGE_START)
	{
		bNeedMark = FFAT_TRUE;
	}
	else
	{
		// 2. if (hash table is full) && (dwStartCluster > hash table range end)
		if ((_gdwHTEntryCount >= _HT_ENTRY_SIZE)
			&& (dwStartCluster > _gdwUFCRangeEnd))
		{
			bNeedMark = FFAT_TRUE;
		}
		else
		{
			// 3. we can use hash table to check when hash table is not full or dwStartCluster <= hash table range end
			// if (dwStartCluster is in hash table)
			if (_clusterHashHasEntry(_PESSHASH(_gpUnFinishedChain), dwStartCluster, _HASH_VALUE(dwStartCluster)) )
			{
				bNeedMark = FFAT_TRUE;
			}
		}
	}

	return bNeedMark;
}


/**
* _manage_fat_chain manages a FAT chain after marking operation. 
* Put the start cluster of FAT chain in hash table if the FAT chain is not fully marked,
* and re-check it in the following round.
* 
* @param pVol 				: [IN] volume pointer
* @param dwStartCluster 	: [IN] start cluster of a FAT chain
* @param bAllMarked 		: [IN] CHKDSK flag
* 
* @return FFAT_OK		: success
* @author Zhang Qing
* @version SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_manage_fat_chain(t_uint32 dwStartCluster, t_boolean bAllMarked)
{
	FFatErr r;

	if (dwStartCluster > _RANGE_END) 
	{
		//This chain must be marked in next round, return directly.
		return FFAT_OK;
	}

	//if (all clusters in chain are been really marked)
	if (bAllMarked == FFAT_TRUE)
	{
		//remove dwStartCluster from hash table if it is in hash table since chain is all-marked.
		r = _clusterHashRemove(_PESSHASH(_gpUnFinishedChain), 
			dwStartCluster, _HASH_VALUE(dwStartCluster));
		if ( (r == FFAT_OK) && (_gdwHTEntryCount < _HT_ENTRY_SIZE) )
		{
			(_gdwHTEntryCount) -= 1;
		}
		//else: If UnFinishedChain hash table have already been full
		//let it go, nothing can do since some UnFinishedChain information may already be lost
		//UnFinishedChain hash table cannot be trusted any more
	}
	else
	{
		//if (hash table is not full) and (not all clusters in chain are been really marked)
		if (_gdwHTEntryCount < _HT_ENTRY_SIZE)
		{
			//add dwStartCluster to hash table
			_clusterHashAdd(_PESSHASH(_gpUnFinishedChain), dwStartCluster,
				_HASH_VALUE(dwStartCluster)); 
			(_gdwHTEntryCount) += 1;
			if (_gdwHTEntryCount >= _HT_ENTRY_SIZE) //hash table is full
			{
				//set _RANGE_START - 1 as maximum sector in hash table
				//[BUG FIX 20100126] consider the case that _RANGE_START is 0.
				if (_RANGE_START > 0)
				{
					_gdwUFCRangeEnd = _RANGE_START - 1;
				}
				else
				{
					_gdwUFCRangeEnd = 0;
				}
			}
		}
	}

	return FFAT_OK;
}


/** 
* _markChain marks a FAT chain started from dwStartCluster
* 
* @param	pVol 				: [IN] volume pointer
* @param	dwStartCluster 		: [IN] start cluster of a FAT chain
* @param	pdwClusterCount		: [IN/OUT] As input, it means the expected 
*									number of clusters in FAT chain. 
*									If it is 0, do not care it. As output,
*									 it means the real number of clusters in FAT chain.
* @param pbAllMarked 			: [OUT] All clusters in the FAT chain are marked or not
* @param dwFlag 				: [IN] chkdsk flag
* @param		pCxt			: [IN] context of current operation
* @return 		FFAT_OK		: success
* @return 		FFAT_EIO	: IO error
* @return 		FFAT_EFAT	: the real number of clusters is not equal to the expected number of clusters in FAT chain 
*
* @author Zhang Qing
* @version SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_markChain(Vol* pVol, t_uint32 dwStartCluster, t_uint32* pdwClusterCount,
			t_boolean* pbAllMarked, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	t_int32		r;
	t_uint32	dwSavedStartCluster;
	t_uint32	dwNextCluster;
	t_uint32	dwCurrentClusterCount;
	t_boolean	bAllMarked;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pdwClusterCount != NULL);
	FFAT_ASSERT(pbAllMarked != NULL);

	dwSavedStartCluster = dwStartCluster;
	bAllMarked = FFAT_TRUE;

#ifdef FFAT_DEBUG
	_CHKDSK_PRINT_VERBOSE((_T("	- cluster chain :%d\n"), dwStartCluster), dwFlag);
#endif

	dwCurrentClusterCount = 0;
	r = _mark_set(dwStartCluster, dwFlag);
	if (r < 0)
	{
		return r;
	}
	if (r == FFAT_OK1)
	{
		bAllMarked = FFAT_FALSE;
	}

	dwCurrentClusterCount++;

	do
	{
		r = FFATFS_GetNextCluster(VOL_VI(pVol), dwStartCluster, &dwNextCluster, pCxt);
		IF_UK (r < 0)
		{
			_CHKDSK_PRINT_VERBOSE((_T("Failed to read FAT table!\n")), dwFlag);
			return r;
		}

		if (FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster))
		{
			if ((*pdwClusterCount != 0) && (dwCurrentClusterCount != *pdwClusterCount))
			{
				FFAT_ASSERT(dwCurrentClusterCount < *pdwClusterCount);
				_CHKDSK_PRINT_ERROR((_T("Broken FAT chain. Size is smaller than expected! \
										[EOF: %d => %d, ScannedCount: %d, ExpectedCount: %d]\n"),
										dwStartCluster, dwNextCluster,
										dwCurrentClusterCount, *pdwClusterCount));
				*pdwClusterCount = dwCurrentClusterCount;
				return FFAT_EFAT;
			}
			break;
		}

		if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwNextCluster) == FFAT_FALSE)
		{
			_CHKDSK_PRINT_VERBOSE((_T("Invalid cluster no %d\n"), dwNextCluster), dwFlag);
			if (*pdwClusterCount == 0)
			{
				return FFAT_EFAT;
			}
			else
			{
				FFAT_ASSERT(dwCurrentClusterCount < *pdwClusterCount);
				_CHKDSK_PRINT_ERROR((_T("Broken FAT chain. Size is smaller than expected! \
										[BAD: %d => %d, ScannedCount: %d, ExpectedCount: %d]\n"),
										dwStartCluster, dwNextCluster,
										dwCurrentClusterCount, *pdwClusterCount));

				*pdwClusterCount = dwCurrentClusterCount;
				if(dwNextCluster == 0)
				{
					(*pdwClusterCount)--;
				}
				return FFAT_EFAT;
			}
		}

		r = _mark_set(dwNextCluster, dwFlag);
		if (r < 0)
		{
			return FFAT_EFAT;
		}

		if (r == FFAT_OK1)
		{
			bAllMarked = FFAT_FALSE;
		}

		dwCurrentClusterCount++;

		if ((*pdwClusterCount != 0) && (dwCurrentClusterCount > *pdwClusterCount))
		{
			_CHKDSK_PRINT_ERROR((_T("Broken FAT chain. Size is larger than expected!\
									[More: %d => %d, ScannedCount: %d, ExpectedCount: %d]\n"),
									dwStartCluster, dwNextCluster,
									dwCurrentClusterCount, *pdwClusterCount));
			*pdwClusterCount = dwCurrentClusterCount;
			return FFAT_EFAT;
		}

		dwStartCluster = dwNextCluster;

	} while (1);

	r = _manage_fat_chain(dwSavedStartCluster, bAllMarked);
	if (r < 0)
	{
		return FFAT_EFAT;
	}

	*pbAllMarked = bAllMarked;

	return FFAT_OK;
}


/** 
* _convertPathName converts the path name from t_wchar* to t_int8*
* 
* @param		pVol 			: [IN] volume pointer
* @param		 psPathName8	: [OUT] the result of coverted pathname
* @param		psPathName16	: [IN] the input pathname
* @return		FFAT_OK : success
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_convertPathName(Vol* pVol, char* psPathName8, t_wchar* psPathName16)
{
	t_uint32	dwPathLen;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_VI(pVol));
	//	FFAT_ASSERT(VI_DEV(VOL_VI(pVol)));	// @NEED_REVIEW_GO
	FFAT_ASSERT(psPathName8 != NULL);
	FFAT_ASSERT(psPathName16 != NULL);

	dwPathLen = FFAT_WCSLEN(psPathName16);
	FFAT_WCSTOMBS(psPathName8, FFAT_PATH_NAME_MAX_LENGTH, psPathName16, dwPathLen, VI_DEV(VOL_VI(pVol)));	// ignore error

	if (dwPathLen == 0)
	{
		psPathName8[0] = '/';
		dwPathLen++;
	}
	psPathName8[dwPathLen] = 0;

	return FFAT_OK;
}


/** 
* _check_file_truncate truncate a file when file size is not matched 
* 
* @param pVol 				: [IN] volume pointer
* @param pNode 			    : [IN] Node* for directory entry
* @param dwClusterCount 	: [IN] the number of clusters of file
* @param dwFlag 			: [IN] chkdsk flag
* 
* @return FFAT_OK 		: truncate file success
* @return FFAT_EFAT	    : truncate file failed
* @author Zhang Qing
* @version SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_check_file_truncate(Vol* pVol, Node* pNode, t_uint32 dwClusterCount, FFatChkdskFlag dwFlag)
{
	FFatErr			r;
	t_uint32		dwSize;
	t_uint32		dwCommand;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pNode != NULL);

	if (dwFlag & FFAT_CHKDSK_REPAIR_INTERACTIVE)
	{
		FFAT_ASSERTP(0, (_T("[CHKDSK] File size error error !!")));
		_CHKDSK_PRINT_CRITICAL(((_T("Correct it (file_truncate)?(y/n)"))));
		dwCommand = FFAT_GETCHAR();
		if ((dwCommand != 'y') && (dwCommand != 'Y'))
		{
			return FFAT_OK;
		}
	}

	dwSize = dwClusterCount << VOL_CSB(pVol);
	r = FFAT_ChangeSize((FFatNode*)pNode, dwSize, FFAT_CHANGE_SIZE_NO_LOCK);
	IF_UK (r < 0)
	{
		return r;
	}

	return FFAT_OK;
}


/** 
* _check_file checks a file
* 
* @param		pVol  		: [IN] volume pointer
* @param		pNode 		: [IN] Node* for directory entry
* @param		dwFlag 		: [IN] chkdsk flag
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_PANIC	: repare file failed
* @author Zhang Qing
* @version SEP-24-2006 [Zhang Qing] First Writing.
* @version OCT-22-2009 [JW Park] Add the consideration about dirty-sized node.
*/
static FFatErr
_check_file(Vol* pVol, Node* pNode, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_boolean		bAllMarked;
	t_uint32 		dwStartCluster;
	t_uint32 		dwSize;
	t_uint32		dwClusterCount = 0;
	t_uint32		dwNextCluster;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pNode != NULL);

	dwStartCluster = NODE_C(pNode);
	dwSize = NODE_S(pNode);

	if (dwStartCluster == 0)
	{
		_CHKDSK_PRINT_VERBOSE((_T("	- checking FILE clusters: no clusters\n")), dwFlag);
		if (dwSize == 0)
		{
			r = FFAT_OK;
		}
		else //dwSize != 0
		{
			dwClusterCount = 0;
			r = FFAT_EFAT;
		}
	}
	else //dwStartCluster >= 2
	{
		r = FFATFS_GetNextCluster(VOL_VI(pVol), dwStartCluster, &dwNextCluster, pCxt);
		if (r < 0)
		{
			_CHKDSK_PRINT_VERBOSE((_T("Failed to read FAT table!\n")), dwFlag);
			return r;
		}

		if (((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) && (dwSize == 0))
			||
			(dwNextCluster == FAT_FREE))
		{
			dwClusterCount = (dwNextCluster == FAT_FREE) ? 0 : 1;
			r = FFAT_EFAT;
		}
		else
		{
			// check the cluster chain
			if (_markChainIsNeeded(pVol, dwStartCluster) == FFAT_FALSE)
			{
				r = FFAT_OK;
			}
			else
			{
				_CHKDSK_PRINT_VERBOSE((_T("	- checking FILE clusters: start cluster(%d)\n"), dwStartCluster), dwFlag);

				if (NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE)
				{
					dwClusterCount = ESS_MATH_CDB(dwSize, VOL_CS(pVol), VOL_CSB(pVol));
				}
				else
				{
					// If node is dirty-size, there is no information about cluster count.
					dwClusterCount = 0;
				}
				
				r = _markChain(pVol, dwStartCluster, &dwClusterCount, &bAllMarked, dwFlag, pCxt);
				if (r != FFAT_OK)
				{
					r = FFAT_EFAT;
				}
			}
		}
	}

	// repair if r == FFAT_EFAT
	// do nothing if r == FFAT_PANIC
	if (r == FFAT_EFAT)
	{
		_CHKDSK_PRINT_ERROR((_T("FILE does not have exact clusters for the size \n")));
		_CHKDSK_PRINT_ERROR((_T("name:%s, fileSize:%d, ScannedClusterCount:%d \n"), _gsCurName8, NODE_S(pNode), dwClusterCount));

		if (!_NEED_REPAIR(dwFlag))
		{
			r = FFAT_EFAT;
		}
		else if(_IS_CHECK_ONLY(dwFlag) == FFAT_TRUE)
		{
			r = FFAT_EFAT;
		}
		else
		{
			r = _check_file_truncate(pVol, pNode, dwClusterCount, dwFlag);
			if (r < 0)
			{
				return r;
			}
			else
			{
				return FFAT_OK;
			}
		}
	}

	return r;
}


/** 
* _checkDir_dotdot checks '..' directory entry
* 
* @param		pVol			: [IN] volume pointer 
* @param		pNodeDE 		: [OUT] output FatGetNodeDe
* @param		dwParentCluster : [IN] the parent cluster of directory entry
* @param		dwFlag 			: [IN] chkdsk flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_PANIC	: repair dot dot directory entry failed
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_checkDir_dotdot(Vol* pVol, FatGetNodeDe* pNodeDE, t_uint32 dwParentCluster,
					FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwCommand;
	FatDeSFN*		pDE;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pNodeDE != NULL);

	pDE = &_PFATGETNODEDE(_gpNodeDE)->pDE[_PFATGETNODEDE(_gpNodeDE)->dwEntryCount - 1];

	// parent is root directory
	if (VI_RC(VOL_VI(pVol)) == dwParentCluster)
	{
		if (FFATFS_GetDeCluster(VOL_VI(pVol), pDE) == 0)
		{
			return FFAT_OK;
		}
	}

	if (FFATFS_GetDeCluster(VOL_VI(pVol), pDE) == dwParentCluster)
	{
		return FFAT_OK;
	}

	_CHKDSK_PRINT_ERROR((_T("Invalid parent directory information for \"..\"\n")));
	_CHKDSK_PRINT_ERROR((_T("De Cluster/Real Parent Cluster :%d/%d\n"), FFATFS_GetDeCluster(VOL_VI(pVol), pDE), dwParentCluster));

	if (_NEED_REPAIR(dwFlag))
	{
		if (dwFlag & FFAT_CHKDSK_REPAIR_INTERACTIVE)
		{
			FFAT_ASSERTP(0, (_T("[CHKDSK] dot dot error !!")));
			_CHKDSK_PRINT_CRITICAL((_T("Correct it (dir_dotdot)?(y/n):")));
			dwCommand = FFAT_GETCHAR();
			if ((dwCommand != 'y') && (dwCommand != 'Y'))
			{
				return FFAT_OK;
			}
		}

		// repairing.
		if (VI_RC(VOL_VI(pVol)) == dwParentCluster) //parent is root
		{
			FFATFS_SetDeCluster(pDE, 0);
		}
		else
		{
			FFATFS_SetDeCluster(pDE, dwParentCluster);
		}

		r = ffat_writeDEs(pVol, pNodeDE->dwCluster, pNodeDE->dwDeStartCluster,
						pNodeDE->dwDeStartOffset, (t_int8*)pNodeDE->pDE, FAT_DE_SIZE,
						FFAT_CACHE_DATA_DE, NULL, pCxt);
		IF_UK (r < 0)
		{
			FFAT_PRINT_DEBUG((_T("failed to fix dot dot entry\n")));
			return r;
		}

		_SET_UPDATED();
	}
	else if (_IS_CHECK_ONLY(dwFlag) == FFAT_TRUE)
	{
		return FFAT_EFAT;
	}

	return FFAT_OK;
}


/** 
 * _check_de check a directory entry
 * 
 * @param	pVol			: [IN] volume pointer
 * @param	pNode			: [IN] Node* for directory entry
 * @param	dwFlag			: [IN] chkdsk flag
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK		: success
 * @return	negative	: fail
 * @author	Zhang Qing
 * @version	SEP-24-2006 [Zhang Qing] First Writing.
 * @version	JAN-05-2008 [DongYoung Seo] change DE deletion routine to ffat_deleteDEs()
 * @version	MAR-02-2009 [GwangOk Go] add check routine for XDE
 */
static FFatErr
_check_de(Vol* pVol, Node* pNode, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwCommand;
	NodeDeInfo*		pDeInfo;
	t_uint32		dwNextCluster;
	t_boolean		bValid;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pNode != NULL);

	if (_PFATGETNODEDE(_gpNodeDE)->dwTotalEntryCount == 1)
	{
		// if entry count is '1', check extended directory entry
		r = ffat_xde_getXDE(pVol, _PFATGETNODEDE(_gpNodeDE), pCxt);
		if (r == FFAT_OK1)
		{
			_CHKDSK_PRINT_ERROR((_T("This extended directory entry doesn't have a short directory entry\n")));

			// no support repair
			r = FFAT_EFAT;
			goto out;
		}
	}

	//ToDo
	if (pNode->dwCluster == 0)
	{
		return FFAT_OK;
	}

	bValid = FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNode->dwCluster);

	//delete an invalid DE for directory
	if (NODE_IS_DIR(pNode))
	{
		r = FFATFS_GetNextCluster(VOL_VI(pVol), pNode->dwCluster, &dwNextCluster, pCxt);
		IF_UK (r < 0)
		{
			_CHKDSK_PRINT_VERBOSE((_T("Failed to read FAT table!\n")), dwFlag);
			return r;
		}
		bValid = (bValid && (dwNextCluster != FAT_FREE));
	}

	//check DE cluster number
	if (!bValid)
	{
		_CHKDSK_PRINT_ERROR((_T("Invalid directory entry's cluster %d\n"), pNode->dwCluster));
		if (_NEED_REPAIR(dwFlag) == FFAT_FALSE)
		{
			r = FFAT_EFAT;
			goto out;
		}
		else if(_IS_CHECK_ONLY(dwFlag) == FFAT_TRUE)
		{
			r = FFAT_EFAT;
			goto out;
		}
		else
		{
			if (dwFlag & FFAT_CHKDSK_REPAIR_INTERACTIVE)
			{
				_CHKDSK_PRINT_CRITICAL((_T("Remove the directory entry?(y/n)")));
				dwCommand = FFAT_GETCHAR();
				if ((dwCommand != 'y') && (dwCommand != 'Y'))
				{
					return FFAT_OK;
				}
			}

			//repair
			pDeInfo = &pNode->stDeInfo;
			r = ffat_deleteDEs(pVol, 0, pDeInfo->dwDeStartOffset,
								pDeInfo->dwDeStartCluster,
								pDeInfo->dwDeCount, FFAT_TRUE,
								FFAT_CACHE_DATA_DE, NULL, pCxt);
			IF_LK (r >= 0)
			{
				r = FFAT_OK1;
			}
			else
			{
				FFAT_PRINT_DEBUG((_T("Failed to remove the directory entry\n")));
				r = FFAT_EFAT;
			}

			_SET_UPDATED();
			goto out;
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/** 
* _getDeFromClusterOffset get Node and FatGetNodeDe struct for a directory entry
* 
* @param		pVol		: [IN] volume pointer
* @param		dwCluster	: [IN] start cluster
* @param		pdwOffset	: [IN/OUT] offset from start cluster, which is the start point of a directory entry
* @param		pNode		: [OUT] output Node* 
* @param		pNodeDE		: [OUT] output FatGetNodeDe
* @param		 dwFlag		: [IN] chkdsk flag
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_getDeFromClusterOffset(Vol* pVol, t_uint32 dwCluster, t_uint32* pdwOffset, 
						Node* pNode, FatGetNodeDe* pNodeDE, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;

	FFAT_ASSERT(pVol != NULL);
	FFAT_ASSERT(pdwOffset != NULL);
	FFAT_ASSERT(pNode != NULL);
	FFAT_ASSERT(pNodeDE != NULL);

	if (*pdwOffset > (FAT_DE_MAX * FAT_DE_SIZE))
	{
		_CHKDSK_PRINT_VERBOSE((_T("Too big directory")), dwFlag);
		r = FFAT_EFAT;
		goto out;
	}

	pNodeDE->dwCluster			= dwCluster;
	pNodeDE->dwOffset			= *pdwOffset;
	pNodeDE->dwTargetEntryCount	= 0;
	pNodeDE->psName				= NULL;
	pNodeDE->bExactOffset		= FFAT_FALSE;
	pNodeDE->dwClusterOfOffset	= 0;

	r = ffat_dir_getDirEntry(pVol, NULL, pNodeDE, FFAT_TRUE, FFAT_FALSE, pCxt);
	if (r < 0)
	{
		if (r == FFAT_EEOF)
		{
			r = FFAT_ENOENT;
		}
		else
		{
			_CHKDSK_PRINT_VERBOSE((_T("Fail to read directory entry\n")), dwFlag);
		}
		goto out;
	}

	*pdwOffset = pNodeDE->dwDeEndOffset + FAT_DE_SIZE;

	r = ffat_node_initNode(pVol, NULL, dwCluster, pNode, FFAT_FALSE, pCxt);
	IF_UK (r < 0)
	{
		_CHKDSK_PRINT_VERBOSE((_T("Fail to init node\n")), dwFlag);
		goto out;
	}

	// release lock now.
	r = ffat_lock_terminateRWLock(&pNode->stRWLock);
	FFAT_EO(r, (_T("fail to release lock")));

	ffat_node_fillNodeInfo(pNode, pNodeDE, NULL);
	NODE_SET_VALID(pNode);

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		_CHKDSK_PRINT_VERBOSE((_T("Time stamp is not correct, incorrect volume\n")), dwFlag);
		r = FFAT_EXDEV;
		goto out;
	}

	r = FFAT_OK;

out:

	return r;
}

/** 
* _chkDirStackInit initialize the gstDirStackMgr structure
* 
* @param	None
* @return	FFAT_OK			: success
* @return	negative		: fail
* @author	JeongWoo Park
* @version	MAY-18-2009 [JeongWoo Park] First writing.
*/
static FFatErr
_chkDirStackInit(void)
{
	_gstDirStackMgr.dwSP	= 0;
	ESS_DLIST_INIT(&(_gstDirStackMgr.stStackHead));

	return FFAT_OK;
}

/** 
* _chkDirStackTerminate deallocate the dynamic allocated memory.
* 
* @param	None
* @return	FFAT_OK			: success
* @return	negative		: fail
* @author	JeongWoo Park
* @version	MAY-18-2009 [JeongWoo Park] First writing.
*/
static FFatErr
_chkDirStackTerminate(void)
{
	if (ESS_DLIST_IS_EMPTY(&(_gstDirStackMgr.stStackHead)) == FFAT_FALSE)
	{
		ChkDirStack*	pDirStack;
		ChkDirStack*	pDirStackTemp;

		ESS_DLLIST_FOR_EACH_ENTRY_PREV_SAFE(ChkDirStack, pDirStack, pDirStackTemp, &(_gstDirStackMgr.stStackHead), stDList)
		{
			ESS_DLIST_DEL_INIT(&(pDirStack->stDList));
			FFAT_FREE(pDirStack, sizeof(ChkDirStack));
		}
	}

	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(_gstDirStackMgr.stStackHead)) == FFAT_TRUE);

	return FFAT_OK;
}

/** 
* _chkDirStackPush push the stack data for iterative.
* 
* @param	dwParentCluster	: [IN] parent cluster of directory
* @param	dwCluster		: [IN] cluster of directory
* @param	dwOffset		: [IN] offset in directory
* @return	FFAT_OK			: success
* @return	negative		: fail
* @author	JeongWoo Park
* @version	MAY-18-2009 [JeongWoo Park] First writing.
*/
static FFatErr
_chkDirStackPush(t_uint32 dwParentCluster, t_uint32 dwCluster, t_uint32 dwOffset)
{
	FFatErr			r = FFAT_OK;
	ChkDirStack*	pDirStack = NULL;
	t_uint32		dwIndex;

	dwIndex = _gstDirStackMgr.dwSP & (_CHKDSK_DIR_STACK_ENTRY_COUNT - 1);

	if (dwIndex != 0)
	{
		FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(_gstDirStackMgr.stStackHead)) == FFAT_FALSE);
		
		pDirStack = ESS_DLIST_GET_ENTRY(ESS_DLIST_GET_TAIL(&(_gstDirStackMgr.stStackHead)), ChkDirStack, stDList);
	}
	else
	{
		// we need more memory
		pDirStack = (ChkDirStack*)FFAT_MALLOC(sizeof(ChkDirStack), ESS_MALLOC_NONE);
		if (pDirStack == NULL)
		{
			_CHKDSK_PRINT_ERROR((_T("fail to allocate memory for chkdsk\n")));
			r = FFAT_ENOSUPPORT;
			goto out;
		}

		ESS_DLIST_ADD_TAIL(&(_gstDirStackMgr.stStackHead), &(pDirStack->stDList));
	}

	pDirStack->pStackEntry[dwIndex].dwParentCluster	= dwParentCluster;
	pDirStack->pStackEntry[dwIndex].dwCluster		= dwCluster;
	pDirStack->pStackEntry[dwIndex].dwOffset		= dwOffset;

	_gstDirStackMgr.dwSP++;

out:
	return r;
}

/** 
* _chkDirStackPop pop the stack data for iterative.
* 
* @param	pdwParentCluster	: [OUT] parent cluster of directory
* @param	pdwCluster			: [OUT] cluster of directory
* @param	pdwOffset			: [OUT] offset in directory
* @return	FFAT_OK				: success
* @return	negative			: fail
* @author	JeongWoo Park
* @version	MAY-18-2009 [JeongWoo Park] First writing.
*/
static FFatErr
_chkDirStackPop(t_uint32* pdwParentCluster, t_uint32* pdwCluster, t_uint32* pdwOffset)
{
	ChkDirStack*	pDirStack = NULL;
	t_uint32		dwIndex;

	FFAT_ASSERT(_gstDirStackMgr.dwSP > 0);
	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(_gstDirStackMgr.stStackHead)) == FFAT_FALSE);

	_gstDirStackMgr.dwSP--;

	dwIndex = _gstDirStackMgr.dwSP & (_CHKDSK_DIR_STACK_ENTRY_COUNT - 1);

	pDirStack = ESS_DLIST_GET_ENTRY(ESS_DLIST_GET_TAIL(&(_gstDirStackMgr.stStackHead)), ChkDirStack, stDList);

	*pdwParentCluster	= pDirStack->pStackEntry[dwIndex].dwParentCluster;
	*pdwCluster			= pDirStack->pStackEntry[dwIndex].dwCluster;
	*pdwOffset			= pDirStack->pStackEntry[dwIndex].dwOffset;

	if (dwIndex == 0)
	{
		ESS_DLIST_DEL_INIT(&(pDirStack->stDList));
		FFAT_FREE(pDirStack, sizeof(ChkDirStack));
	}

	return FFAT_OK;
}

/** 
 * _checkDir check the state for a directory including all files and sub-directory under the directory in DFS way
 * 
 * @param	pVol			: [IN] volume pointer
 * @param	dwCluster		: [IN] start cluster of directory
 * @param	dwParentCluster : [IN] parent cluster of directory
 * @param	dwFlag			: [IN] chkdsk flag
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	negative		: fail
 * @author	Zhang Qing
 * @version	SEP-24-2006 [Zhang Qing] First Writing.
 * @version	DEC-31-2008 [JeongWoo Park] Add check routine for EA & root EA.
 * @version	MAR-02-2009 [GwangOk Go]    Add check routine for XDE
 * @version	MAR-31-2009 [JeongWoo Park] Change the function as iterative from recursive
 * @version	MAY-13-2009 [JeongWoo Park] remove the restriction of directory depth by using dynamic malloc
 */
static FFatErr
_checkDir(Vol* pVol, t_uint32 dwCluster, t_uint32 dwParentCluster,
			FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwOffset = 0;
	t_uint32		dwLFNLen;
	FatDeSFN*		pDE;
	t_uint32		dwClusterCount;
	t_boolean		bAllMarked;
	
	FFAT_ASSERT(pVol != NULL);

	_chkDirStackInit();

again:
	while(1)
	{
		// find valid DE from offset, with NODE
		// [NOTICE] By internally, this function checks the validity of the XDE,
		//			if the XDE is corrupted, this will return FFAT_EPROG [by STORM : 20081223]
		r = _getDeFromClusterOffset(pVol, dwCluster, &dwOffset, _PNODE(_gpNode),
						_PFATGETNODEDE(_gpNodeDE), dwFlag, pCxt);
		if (r == FFAT_ENOENT)
		{
			break;
		}
		else if (r == FFAT_EXDE)
		{
			_CHKDSK_PRINT_ERROR((_T("This node doesn't have an extended directory entry: ParentCluster/Offset(%d/%d), ClusterOfOffset(%d)\n"),
								dwCluster,
								_PFATGETNODEDE(_gpNodeDE)->dwDeStartOffset,
								_PFATGETNODEDE(_gpNodeDE)->dwDeStartCluster));

			// no support repair
			goto out;
		}
		else if (r < 0)
		{
			goto out;
		}

		_CHKDSK_PRINT_VERBOSE((_T(" >> Checking DE: ParentCluster/Offset(%d/%d), ClusterOfOffset(%d)\n"),
								dwCluster,
								_PFATGETNODEDE(_gpNodeDE)->dwDeStartOffset,
								_PFATGETNODEDE(_gpNodeDE)->dwDeStartCluster),
								dwFlag);

		r = _check_de(pVol, _PNODE(_gpNode), dwFlag, pCxt);
		if (r < 0)
		{
			goto out;
		}
		else if(r == FFAT_OK1)
		{
			continue;
		}

#ifdef FFAT_VFAT_SUPPORT
		r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), _PFATGETNODEDE(_gpNodeDE)->pDE,
						_PFATGETNODEDE(_gpNodeDE)->dwEntryCount,
						_gsCurName, (t_int32*)&dwLFNLen, FAT_GEN_NAME_LFN);
#else
		r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), _PFATGETNODEDE(_gpNodeDE)->pDE,
						_PFATGETNODEDE(_gpNodeDE)->dwEntryCount,
						_gsCurName, (t_int32*)&dwLFNLen, FAT_GEN_NAME_SFN);
#endif
		if (r < 0)
		{
			_CHKDSK_PRINT_VERBOSE((_T("fail to geneterate LFN from DE")), dwFlag);
			goto out;
		}

		_convertPathName(pVol, _gsCurName8, _gsCurName);
		_CHKDSK_PRINT_VERBOSE((_T("	- name: %s\n"), _gsCurName8), dwFlag);

		r = _checkPathLen(_PNODE(_gpNode), dwLFNLen, dwFlag);
		if (r < 0)
		{
			goto out;
		}

		r = _checkEA(_PNODE(_gpNode), dwFlag, pCxt);
		if (r < 0)
		{
			goto out;
		}
		
		if (NODE_IS_FILE(_PNODE(_gpNode)))
		{
			r = _check_file(pVol, _PNODE(_gpNode), dwFlag, pCxt);
		}
		else if (NODE_IS_DIR(_PNODE(_gpNode)))
		{
			pDE = &_PFATGETNODEDE(_gpNodeDE)->pDE[_PFATGETNODEDE(_gpNodeDE)->dwEntryCount - 1];
			//skip dot
			if (FFAT_MEMCMP(pDE->sName, FAT_DE_DOT, FAT_SFN_NAME_CHAR) == 0)
			{
				// none
			}
			else if (FFAT_MEMCMP(pDE->sName, FAT_DE_DOTDOT, FAT_SFN_NAME_CHAR) == 0) //dot dot
			{
				r = _checkDir_dotdot(pVol, _PFATGETNODEDE(_gpNodeDE),
									dwParentCluster, dwFlag, pCxt);
			}
			else //directory
			{
				r = _chkDirStackPush(dwParentCluster, dwCluster, dwOffset);
				if (r < 0)
				{
					_CHKDSK_PRINT_ERROR((_T("too deep directory depth : depth(> %d)\n"), _gstDirStackMgr.dwSP));
					goto out;
				}

				dwParentCluster	= dwCluster;
				dwCluster		= _PNODE(_gpNode)->dwCluster;
				dwOffset		= 0;

				goto again;
			}
		}

		if (r < 0)
		{
			goto out;
		}
	}

	FFAT_ASSERT(r == FFAT_ENOENT);

	// check root EA
	if (dwCluster == NODE_C(VOL_ROOT(pVol)))
	{
		r = _checkEA(VOL_ROOT(pVol), dwFlag, pCxt);
		if (r < 0)
		{
			goto out;
		}
	}

	// Check FAT chain for dwCluster
	if (_markChainIsNeeded(pVol, dwCluster) == FFAT_TRUE)
	{
		_CHKDSK_PRINT_VERBOSE((_T("  = checking DIR clusters: start cluster(%d)\n"), dwCluster), dwFlag);

		dwClusterCount = 0;
		r = _markChain(pVol, dwCluster, &dwClusterCount, &bAllMarked, dwFlag, pCxt);
		if (r < 0)
		{
			r = FFAT_ENOSUPPORT;
			_CHKDSK_PRINT_ERROR((_T("Failed to mark FAT chain bitmap!\n")));
			goto out;
		}
	}

	if (_gstDirStackMgr.dwSP > 0)
	{
		r = _chkDirStackPop(&dwParentCluster, &dwCluster, &dwOffset);
		if (r < 0)
		{
			goto out;
		}

		goto again;
	}

	r = FFAT_OK;

out:
	_chkDirStackTerminate();

	return r;
}


/** 
* _checkFAT check unused clusters in FAT
* 
* @param		pVol		: [IN] volume pointer
* @param		dwFlag		: [IN] chkdsk flag
* 
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
* @history		DEC-10-2007 [InHwan Choi] apply to extended attribute
* @history		MAR-31-2009 [JeongWoo Park] edit to return error if the lost cluster is found with SHOW flag
*/
static FFatErr
_checkFAT(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;
	t_uint32	dwIdx;
	t_uint32	dwValue;
	t_uint32	dwCommand;
	t_uint32	bFixAll;
	t_boolean	bErr = FFAT_FALSE;

	_CHKDSK_PRINT_VERBOSE((_T(">> Checking unused FAT entries!\n")), dwFlag);

	bFixAll = FFAT_FALSE;

	for (dwIdx = _RANGE_START; dwIdx <= _RANGE_END; dwIdx++)
	{
		if (_mark_is_set(dwIdx))
		{
			continue;
		}

		r = FFATFS_GetNextCluster(VOL_VI(pVol), dwIdx, &dwValue, pCxt);
		IF_UK (r < 0)
		{
			FFAT_PRINT_DEBUG((_T("Failed to read FAT table!")));
			goto out;
		}

		if (dwValue == FAT_FREE)
		{
			continue;
		}

		_CHKDSK_PRINT_ERROR((_T("Unused FAT number = 0x%x (%d)\n"), dwIdx, dwIdx));

		if (_NEED_REPAIR(dwFlag))
		{
			if ((dwFlag & FFAT_CHKDSK_REPAIR_INTERACTIVE) && (bFixAll == FFAT_FALSE))
			{
				//[CHKDSK] FAT error !!
				_CHKDSK_PRINT_CRITICAL((_T("Correct it (FAT)?(y/n/a)")));
				dwCommand = FFAT_GETCHAR();
				if ((dwCommand != 'y') && (dwCommand != 'Y') && (dwCommand != 'a') && (dwCommand != 'A'))
				{
					continue;
				}
				if ((dwCommand == 'a') || (dwCommand == 'A'))
				{
					bFixAll = FFAT_TRUE;
				}
			}

			// we should clean it.
			r = FFATFS_FreeClusters(VOL_VI(pVol), 1, &dwIdx, FFAT_CACHE_NONE,
				FAT_ALLOCATE_NONE, NULL, pCxt);
			IF_UK (r < 0)
			{
				FFAT_PRINT_DEBUG((_T("failed to update FAT log!")));
				goto out;
			}

			_SET_UPDATED();
		}
		else if(_IS_CHECK_ONLY(dwFlag) == FFAT_TRUE)
		{
			r = FFAT_EFAT;
			goto out;
		}
		else
		{
			// enuCHKDSK_SHOW flag can be here
			bErr = FFAT_TRUE;
		}
	}

	_CHKDSK_PRINT_VERBOSE((_T("-- Finished checking unused FAT entries!\n")), dwFlag);

	r = FFAT_OK;
	
	IF_UK (bErr == FFAT_TRUE)
	{
		r = FFAT_EFAT;
	}

out:
	return r;
}


/** 
* _chkdskInit init memory allocation for chkdsk
* 
* @param		pVol		: [IN] volume pointer 
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_ENOMEM : not enough memory
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_chkdskInit(Vol* pVol, ComCxt* pCxt)
{
	FFatErr		r;
	r = FFAT_ENOMEM;

	_gsCurName = (t_wchar*)FFAT_LOCAL_ALLOC(FFAT_NAME_BUFF_SIZE, pCxt);
	FFAT_ASSERT(_gsCurName != NULL);

	_gsCurName8 = (char*)FFAT_LOCAL_ALLOC(FFAT_NAME_BUFF_SIZE, pCxt);
	FFAT_ASSERT(_gsCurName8 != NULL);

	_gpNode = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(_gpNode != NULL);

	ffat_node_resetNodeStruct(_gpNode);

	_gpNodeDE = (t_int32*)FFAT_LOCAL_ALLOC(sizeof(FatGetNodeDe), pCxt);
	FFAT_ASSERT(_gpNodeDE != NULL);

	_PFATGETNODEDE(_gpNodeDE)->pDE = FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(_PFATGETNODEDE(_gpNodeDE)->pDE != NULL);

	_gpChkRange = (t_int32*)FFAT_LOCAL_ALLOC(sizeof(ChkdskRange), pCxt);
	FFAT_ASSERT(_gpChkRange != NULL);

	_gdwHTEntryCount = 0;
	_gdwUFCRangeEnd = 0;

	_gpMap = (t_uint8 *) FFAT_LOCAL_ALLOC(_CHKDSK_MEM_SIZE, pCxt);
	FFAT_ASSERT(_gpMap != NULL);

	FFAT_MEMSET(_gpMap, 0x00, _CHKDSK_MEM_SIZE);

	r = _hash_init(pVol, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}


	r = FFAT_OK;

out:
	IF_UK (r != FFAT_OK)
	{
		FFAT_PRINT_DEBUG((_T("failed to allocate memory for bitmap!")));
		_chkdskRelease(pVol, pCxt);
	}
	return r;
}


/** 
* _chkdskRelease releases all allocated memory
* 
* @param		pVol		: [IN] volume pointer 
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK : success
* @author		Zhang Qing
* @version		SEP-24-2006 [Zhang Qing] First Writing.
*/

static FFatErr 
_chkdskRelease(Vol* pVol, ComCxt* pCxt)
{
	_hash_release(pVol, pCxt);

	FFAT_LOCAL_FREE(_gpMap, _CHKDSK_MEM_SIZE, pCxt);
	_gpMap = NULL;

	FFAT_LOCAL_FREE(_gpChkRange, sizeof(ChkdskRange), pCxt);
	_gpChkRange = NULL;

	FFAT_LOCAL_FREE(_PFATGETNODEDE(_gpNodeDE)->pDE, VOL_MSD(pVol), pCxt);

	FFAT_LOCAL_FREE(_gpNodeDE, sizeof(FatGetNodeDe), pCxt);
	_gpNodeDE = NULL;

	FFAT_LOCAL_FREE(_gpNode, sizeof(Node), pCxt);
	_gpNode = NULL;

	FFAT_LOCAL_FREE(_gsCurName8, FFAT_NAME_BUFF_SIZE, pCxt);
	_gsCurName8 = NULL;

	FFAT_LOCAL_FREE(_gsCurName, FFAT_NAME_BUFF_SIZE, pCxt);
	_gsCurName = NULL;

	return FFAT_OK;
}


/** 
* _hash_init initializes memory allocation related to hash table, 
* 
* @param	pVol		: [IN] volume pointer
* 
* @return	FFAT_OK		: success
* @return	FFAT_ENOMEM	: no enough memory
* @author	Zhang Qing
* @version	SEP-24-2006 [Zhang Qing] First Writing.
*/

FFatErr 
_clusterHashInit(EssHash* pClusterHash, EssDList* pHashTable, _HashClusterEntry* pEntries,
				t_int32 dwBucketSize, t_int32 dwEntrySize)
{
	FFatErr 	r;
	t_int32 	i;

	r = ESS_HASH_INIT(pClusterHash, pHashTable, dwBucketSize);
	IF_UK (r < 0)
	{
		FFAT_PRINT_DEBUG((_T("Fail to init HASH TABLE")));
		return r;
	}

	for (i = 0; i < dwEntrySize; i++)
	{
		ESS_HASH_INIT_ENTRY(&pEntries[i].stListHash);
		ESS_HASH_ADD_TO_FREE(pClusterHash, (EssHashEntry*)&pEntries[i]);
	}	

	return FFAT_OK;
}


/** 
* _hash_add adds a cluster to HASH TABLE
* 
* @param pVol	 			: [IN] volume pointer
* @param dwClusterNumber 	: [IN] cluster to be added
* 
* @return		FFAT_OK		: success
* @author Zhang Qing
* @version Sep-24-2006 [Zhang Qing] First Writing.
*/
FFatErr 
_clusterHashAdd(EssHash* pClusterHash, t_uint32 dwClusterNumber, t_int32 dwHashVal)
{
	EssDList*	pFree;

	if (ESS_DLIST_IS_EMPTY(&pClusterHash->stDListFree))
	{
		FFAT_PRINT_DEBUG((_T("Fail to get memory for cluster entry")));
		return FFAT_ENOMEM;
	}

	pFree = ESS_DLIST_GET_NEXT(&pClusterHash->stDListFree);
	ESS_DLIST_DEL(pFree->pPrev, pFree->pNext);

	((_HashClusterEntry *)pFree)->dwCluster = dwClusterNumber;

	ESS_HASH_ADD(pClusterHash, dwHashVal, (EssHashEntry *)pFree);

	return FFAT_OK;
}

/** 
* _hash_cmp compares 2 HASH TABLE entries, whether they have the same cluster number or not.
* It is used by _hash_lookup
* @param	pTarget 	: [IN] the target HASH TABLE entry to be compared
* @param	pCurrent 	: [IN] the current HASH TABLE entry 
* 
* @return	FFAT_TRUE	: 2 HASH TABLE entries have the same cluster number
* @return	FFAT_FALSE	: 2 HASH TABLE entries have the different cluster number
* @author	Zhang Qing
* @version	Sep-24-2006 [Zhang Qing] First Writing.
*/

static t_boolean 
_clusterHashCmp(void* pTarget, EssHashEntry* pCurrent)
{
	if (((_HashClusterEntry*)pTarget)->dwCluster == ((_HashClusterEntry*)pCurrent)->dwCluster)
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}

/** 
* _hash_lookup lookups a cluster in HASH TABLE
* 
* @param	dwClusterNumber : [IN] the cluster number to be looked up
* 
* @return	NULL		: cannot find the cluster in HASH TABLE
* @return	not NULL	: the pointer of HASH TABLE entry which has value dwClusterNumber
* @author	Zhang Qing
* @version	Sep-24-2006 [Zhang Qing] First Writing.
*/
static _HashClusterEntry*
_clusterHashLookup(EssHash* pClusterHash, t_uint32 dwClusterNumber, t_int32 dwHashVal)
{
	_HashClusterEntry* pEntry;
	_HashClusterEntry stTarget;

	ESS_HASH_INIT_ENTRY(&stTarget.stListHash);

	stTarget.dwCluster = dwClusterNumber;

	pEntry = (_HashClusterEntry*)ESS_HASH_LOOKUP(pClusterHash, 
				dwHashVal, &stTarget, (PFN_HASH_CMP)_clusterHashCmp);

	return pEntry;
}

/** 
* _hash_has_entry checks HASH TABLE contains entry which has value dwClusterNumber 
* 
* @param	dwClusterNumber : [IN] the cluster number to be checked
* 
* @return	FFAT_TRUE  : HASH TABLE contains the entry
* @return	FFAT_FALSE : HASH TABLE does not contain the entry
* @author	Zhang Qing
* @version	SEP-24-2006 [Zhang Qing] First Writing.
*/

static t_boolean
_clusterHashHasEntry(EssHash* pClusterHash, t_uint32 dwClusterNumber, t_int32 dwHashVal)
{
	_HashClusterEntry* pEntry;

	pEntry = _clusterHashLookup(pClusterHash, dwClusterNumber, dwHashVal);

	if (pEntry != NULL)
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/** 
* _hash_remove removes a entry from HASH TABLE. The entry has value dwClusterNumber
* 
* @param	pVol			: [IN] volume pointer
* @param	dwClusterNumber	: [IN] cluster to be removed
* @return	FFAT_OK	: remove success
* @author	Zhang Qing
* @version	SEP-24-2006 [Zhang Qing] First Writing.
*/
static FFatErr
_clusterHashRemove(EssHash* pClusterHash, t_uint32 dwClusterNumber, t_int32 dwHashVal)
{
	_HashClusterEntry* pEntry;

	pEntry = _clusterHashLookup(pClusterHash, dwClusterNumber, dwHashVal);
	if (pEntry != NULL)
	{
		ESS_HASH_MOVE_TO_FREE(pClusterHash, (EssHashEntry*)pEntry);
		return FFAT_OK;
	}

	return FFAT_ENOENT;
}


/** 
* Mark Cluster Chain for Hidden Protected Area
*
* @param	pVol		: [IN] volume pointer
* @param	dwFlag		: [IN] flag for chkdsk
* @param	pCxt		: [IN] current Context
* @return	FFAT_OK		: success
* @return	else		: error
* @author	DongYoung Seo
* @version	01-DEC-2008 [DongYoung Seo] First Writing.
* @version	Aug-29-2009 [SangYoon Oh] Add the code to mark Cluster bitmap clusters
*/

static FFatErr
_markClustersHPA(Vol* pVol, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	HPAInfo*	pInfo;
	t_uint32	dwCount;
	t_boolean	bAllMarked;
	FFatErr		r;

	//Cluster Bitmap
	dwCount = ffat_hpa_getClusterBitmapSize(pVol);
	if (dwCount == 0)
	{
		return FFAT_OK;
	}

	dwCount = ESS_MATH_CDB(dwCount, VOL_CS(pVol), VOL_CSB(pVol));

	pInfo = VOL_ADDON(pVol)->pHPA;

	r = _markChain(pVol, pInfo->dwCMCluster, &dwCount, &bAllMarked, dwFlag, pCxt);
	FFAT_ER(r, (_T("fail to mark chain")));

	//HPA Bitmap
	dwCount = ffat_hpa_getBitmapSize(pVol);
	if (dwCount == 0)
	{
		return FFAT_OK;
	}

	dwCount = ESS_MATH_CDB(dwCount, VOL_CS(pVol), VOL_CSB(pVol));

	r = _markChain(pVol, pInfo->dwFSMCluster, &dwCount, &bAllMarked, dwFlag, pCxt);
	FFAT_ER(r, (_T("fail to mark chain")));

	dwCount = 1;

	//HPA Info
	r = _markChain(pVol, pInfo->dwInfoCluster, &dwCount, &bAllMarked, dwFlag, pCxt);
	FFAT_ER(r, (_T("fail to mark chain")));

	return FFAT_OK;
}


/** 
* check length of name and path
* 
* @param		pNode			: [IN] volume pointer
* @param		dwLFNLen		: [IN] context of current operation
* @param		dwFlag  		: [IN] chkdsk flag
* @return		FFAT_OK			: success
* @return		negative		: fail
* @author
* @version		13-DEC-2008 [DongYoung Seo] bug fix on the path length checking routine
* @version		13-MAY-2009 [JeongWoo Park] remove the check code about max path length
*											no more keep the path string
*/
static FFatErr
_checkPathLen(Node* pNode, t_uint32 dwLFNLen, FFatChkdskFlag dwFlag)
{
	FFatErr			r;

	if (NODE_IS_FILE(pNode))
	{
		if (dwLFNLen > FFAT_FILE_NAME_MAX_LENGTH)
		{
			r = FFAT_ETOOLONG;
			goto out;
		}
	}
	else if (NODE_IS_DIR(pNode))
	{
		if (dwLFNLen > FFAT_DIR_NAME_MAX_LENGTH)
		{
			r = FFAT_ETOOLONG;
			goto out;
		}
	}

	r = FFAT_OK;

out:
	if (r != FFAT_OK)
	{
		_CHKDSK_PRINT_ERROR((_T("the name is too long: length %d\n"), dwLFNLen));
	}

	return r;
}


/** 
* _checkEA check the extended attribute
* 
* @param	pNode			: [IN] volume pointer
* @param	dwFlag			: [IN] chkdsk flag
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
* @return	negative		: fail
* @author	JeongWoo Park
* @version	DEC-31-2008 [JeongWoo Park] First Writing.
*/
static FFatErr
_checkEA(Node* pNode, FFatChkdskFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;
	t_uint32	udwEACluster;
	t_uint32	dwClusterCount;
	Vol*		pVol;
	t_boolean	bAllMarked;

	pVol = NODE_VOL(pNode);

	r = ffat_ea_getEAFirstCluster(pNode, &udwEACluster, pCxt);
	if (r == FFAT_OK)
	{
		FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), udwEACluster) == FFAT_TRUE);

		if (FFATFS_IsValidCluster(VOL_VI(pVol), udwEACluster) == FFAT_TRUE)
		{
			// check the XATTR cluster chain
			IF_LK (_markChainIsNeeded(pVol, udwEACluster) == FFAT_TRUE)
			{
				_CHKDSK_PRINT_VERBOSE((_T("	- checking XATTR clusters: start cluster(%d)\n"), udwEACluster), dwFlag);

				dwClusterCount = 0;
				r = _markChain(pVol, udwEACluster, &dwClusterCount, &bAllMarked, dwFlag, pCxt);
				IF_UK (r < 0)
				{
					r = FFAT_ENOSUPPORT;
					_CHKDSK_PRINT_ERROR((_T("Failed to mark FAT chain bitmap!\n")));
					goto out;
				}
				
				r = ffat_ea_checkEA(pNode, pCxt);
				IF_UK (r < 0)
				{
					_CHKDSK_PRINT_VERBOSE((_T("Failed to check XATTR entries \n")), dwFlag);
					goto out;
				}
			}
		}
	}
	else if (r == FFAT_ENOXATTR)
	{
		r = FFAT_OK;
	}

out:
	return r;
}


//=============================================================================

