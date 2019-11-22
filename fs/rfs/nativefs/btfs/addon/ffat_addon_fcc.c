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
* @file			ffat_addon_fcc.c
* @brief		Free Cluster Cache
* @author		DongYoung Seo(dy76.seo@samsung.com)
* @version		OCT-31-2006 [DongYoung Seo] First writing
* @version		JAN-14-2010 [ChunUm Kong]	Modifying comment (English/Korean)
* @see			None
*/

#include "ffat_types.h"
#include "ffat_common.h"
#include "ess_bitmap.h"

#include "ffat_vol.h"
#include "ffat_node.h"
#include "ffat_misc.h"

#include "ffat_addon_types_internal.h"
#include "ffat_addon_fcc.h"

#include "ess_rbtree2.h"
#include "ess_math.h"

#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_FCC)

//#define _FCC_DEBUG
//#define _FCC_STATISTICS

// define
// volume info ADDON free cluster cache
#define _FCC(_pVol) 						((VOL_ADDON(_pVol))->pFCC)

// check FCC is activated or not
#define _IS_ACTIVATED(_pVol)				(_FCC(_pVol) ? FFAT_TRUE : FFAT_FALSE)

#define _IS_FULL(_Main) 					((ESS_DLIST_IS_EMPTY(_FCCMAIN_LF(_Main)) == ESS_TRUE) ? FFAT_TRUE : FFAT_FALSE)

#define _FCCMAIN_LF(_Main)					(&((_Main)->dlFree))					//!< get free list
#define _FCCMAIN_LV(_Main)					(&((_Main)->dlValid))					//!< get valid list

#define _FCC_TC(FCC_VI)						(&(FCC_VI)->stRBTreeClusterHead)		//!< get cluster tree
#define _FCC_TCC(FCC_VI) 					(&(FCC_VI)->stRBTreeContCountHead)		//!< get count tree
#define _FCC_LD(FCC_VI)						(&(FCC_VI)->dlDirtyList)				//!< get dirty list

#define _INC_FREE_CLUSTER_COUNT(_FCC_VI, _dwCount)	((_FCC_VI)->dwFreeClusterCount += _dwCount)
#define _DEC_FREE_CLUSTER_COUNT(_FCC_VI, _dwCount)	((_FCC_VI)->dwFreeClusterCount -= _dwCount)

#define _FCCE_SET_CLUSTER(_pFCCE, _dwCluster)	((_pFCCE)->stNodeCluster.dwRBNodeKey = _dwCluster)
#define _FCCE_SET_COUNT(_pFCCE, _dwCount)		((_pFCCE)->stNodeCount.dwRBNodeKey = _dwCount)

#define _FCCE_NC(_pFCCE) 						(&((_pFCCE)->stNodeCluster))
#define _FCCE_CLUSTER(_pFCCE)					((_pFCCE)->stNodeCluster.dwRBNodeKey)
#define _FCCE_NCC(_pFCCE)						(&((_pFCCE)->stNodeCount))
#define _FCCE_COUNT(_pFCCE)						((_pFCCE)->stNodeCount.dwRBNodeKey)

#define _FCCE_LAST_CLUSTER(_pFCCE)				((_FCCE_CLUSTER(_pFCCE)) + (_FCCE_COUNT(_pFCCE)) - (1))

#define _FCCE_LL(_pFCCE) 						(&((_pFCCE)->dlList))
#define _FCCE_LD(_pFCCE) 						(&((_pFCCE)->dlDirty))
#define _FCCE_IS_DIRTY(_pFCCE)					(ESS_DLIST_IS_EMPTY(_FCCE_LD(_pFCCE)) ? FFAT_FALSE : FFAT_TRUE)

// dwCluster ~ dwCount is basis
// 1. dwCluster�������� dwCluster���� ������ LEFT, ������ RIGHT�̴�.
// [en] as dwCluster is standard, go left if less than dwCluster and go right if more than dwCluster
// 2. _IS_INCLUDED, _IS_INCLUDING�� _IS_EQUAL�� �����Ѵ�.
// [en] _IS_INCLUDED and _IS_INCLUDING is included _IS_EQUAL

// _IS_EQUAL			:	|------------|			-> dwCluster ~ dwCount
//							|------------|			-> pEntry
#define _IS_EQUAL(_pFCCE, _dwCluster, _dwCount)		(( (_FCCE_CLUSTER(_pFCCE) == (_dwCluster))						\
													&& (_FCCE_COUNT(_pFCCE) == (_dwCount)) ) ? FFAT_TRUE : FFAT_FALSE)

// _IS_LEFT_OVERLAP 	:		|------------|		-> dwCluster ~ dwCount
//							|------------|			-> pEntry
#define _IS_LEFT_OVERLAP(_pFCCE, _dwCluster, _dwCount)	(( (_FCCE_CLUSTER(_pFCCE) < (_dwCluster)) &&			\
														(_FCCE_LAST_CLUSTER(_pFCCE) >= (_dwCluster)) &&						\
														(_FCCE_LAST_CLUSTER(_pFCCE) < ((_dwCluster) + (_dwCount) - 1)) )	\
														? FFAT_TRUE : FFAT_FALSE)

// _IS_RIGHT_OVERLAP	:	|-------------| 		-> dwCluster ~ dwCount
//								|------------|		-> pEntry
#define _IS_RIGHT_OVERLAP(_pFCCE, _dwCluster, _dwCount)		(( (_FCCE_CLUSTER(_pFCCE) > (_dwCluster)) && 		\
															(_FCCE_CLUSTER(_pFCCE) <= ((_dwCluster) + (_dwCount) - 1)) &&			\
															(_FCCE_LAST_CLUSTER(_pFCCE) > ((_dwCluster) + (_dwCount) - 1)) )		\
															? FFAT_TRUE : FFAT_FALSE)

// _IS_LEFT_OVERLAP �Ǵ� _IS_RIGHT_OVERLAP
#define _IS_OVERLAP(_pFCCE, _dwCluster, _dwCount)	(( (_IS_LEFT_OVERLAP(_pFCCE, _dwCluster, _dwCount)) ||		\
													(_IS_RIGHT_OVERLAP(_pFCCE, _dwCluster, _dwCount)) ) 		\
													? FFAT_TRUE : FFAT_FALSE)

// _IS_INCLUDED 		 :		|-----| 			-> dwCluster ~ dwCount
//							|-------------| 		-> pEntry
#define _IS_INCLUDED(_pFCCE, _dwCluster, _dwCount)	(( (_FCCE_CLUSTER(_pFCCE) <= (_dwCluster)) &&				\
													((_FCCE_CLUSTER(_pFCCE) + _FCCE_COUNT(_pFCCE)) >= ((_dwCluster) + (_dwCount))) )		\
													? FFAT_TRUE : FFAT_FALSE)

// _IS_INCLUDING		 :	|-------------| 		-> dwCluster ~ dwCount
//								|-----| 			-> pEntry
#define _IS_INCLUDING(_pFCCE, _dwCluster, _dwCount)		(( (_FCCE_CLUSTER(_pFCCE) >= (_dwCluster)) &&		\
														((_FCCE_CLUSTER(_pFCCE) + _FCCE_COUNT(_pFCCE)) <= ((_dwCluster) + (_dwCount))) )	\
														? FFAT_TRUE : FFAT_FALSE)

// _IS_LEFT_LIKED		 :			|------|		-> dwCluster ~ dwCount
//							|------|				-> pEntry
#define _IS_LEFT_LIKED(_pFCCE, _dwCluster, _dwCount)	((_FCCE_CLUSTER(_pFCCE) + _FCCE_COUNT(_pFCCE) ==			\
														(_dwCluster)) ? FFAT_TRUE : FFAT_FALSE)

// _IS_RIGHT_LIKED		 :	|------|				-> dwCluster ~ dwCount
//									|------|		-> pEntry
#define _IS_RIGHT_LIKED(_pFCCE, _dwCluster, _dwCount)	(((_dwCluster) + (_dwCount) ==							\
														_FCCE_CLUSTER(_pFCCE)) ? FFAT_TRUE : FFAT_FALSE)


// Internal types
typedef struct
{
	Vol*			pVol;				//!< volume info
	EssRBNode2 		stNodeCluster; 		//!< RB-Tree Node for cluster number
	EssRBNode2 		stNodeCount;		//!< RB-Tree Node for contiguous cluster count

	EssDList		dlList;				//!< if this is not an empty list ==> free entry
	//!< if this is empty list ==> valid entry
	EssDList		dlDirty;			//!< list for entry which was not update in FAT(in Fat, it is still used)
} _FCCEntry;

typedef struct
{
	FCCVolInfo*		pFCCVI;			//!< buffer for FCC Info
	EssList			slFreeFCCVI;	//!< storage for free FCC Info

	EssDList		dlFree;			//!< free entry list
	EssDList		dlValid;		//!< valid entry list
} _FCCMain;

// Internal static functions
static FFatErr	_initFCCVI(FCCVolInfo* pFCCVI);

static FFatErr	_addClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount,
							t_boolean bDirty, t_boolean bForce, ComCxt* pCxt);
static FFatErr	_addAndDeallocateClustersVC(Vol* pVol, FFatVC* pVC,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_getClusters(Vol* pVol, t_uint32 dwLookupHint, t_uint32 dwCount,
							t_uint32* pdwFreeCluster, t_uint32* pdwFreeCount, ComCxt* pCxt);
static FFatErr	_getClustersFromTo(Vol* pVol, t_uint32 dwHint, t_uint32 dwFrom,
							t_uint32 dwTo, t_uint32 dwCount, t_uint32* pdwFreeCluster,
							t_uint32* pdwFreeCount, ComCxt* pCxt);
static FFatErr	_removeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount,
							ComCxt* pCxt);
static FFatErr	_updateEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32	 dwCluster,
							t_uint32 dwCount, t_boolean bForce, ComCxt* pCxt);
static FFatErr	_removeEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32 dwCluster,
							t_uint32 dwCount, ComCxt* pCxt);
static FFatErr	_addEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32 dwCluster,
							t_uint32 dwCount, ComCxt* pCxt);
static FFatErr	_getFreeEntry(Vol* pVol, _FCCEntry** ppFreeEntry, t_boolean bForce,
							t_uint32 dwCount, ComCxt* pCxt);
static t_int32	_getFreeClusterCount16(t_uint32 dwCluster, t_int8* pBuff,
							t_int32 dwLastIndex, Vol* pVol, ComCxt* pCxt);
static t_int32	_getFreeClusterCount32(t_uint32 dwCluster, t_int8* pBuff,
							t_int32 dwLastIndex, Vol* pVol, ComCxt* pCxt);
static FFatErr	_getFreeClusterCount(Vol* pVol, t_int8* pBuff, t_int32 dwBuffSize,
							t_uint32* pdwFreeCount, ComCxt* pCxt);

// debug begin
#ifdef _FCC_DEBUG
	static FFatErr		_checkTree(Vol* pVol);
	static FFatErr		_checkVC(Vol* pVol, FFatVC* pVC);
	static FFatErr		_removeConfirm(Vol* pVol, t_uint32	dwCluster, t_uint32 dwCount);
	static FFatErr		_addConfirm(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount);
#endif
// debug end

static _FCCMain*		_pFCCMain;

#ifdef FFAT_DYNAMIC_ALLOC
	static FCCVolInfo*	_getFreeFCCVIDynamic(void);
	static FFatErr		_releaseFCCVIDynamic(FCCVolInfo* pFCCI);

	#define	_INIT_FCCVI_STORAGE()			FFAT_OK
	#define	_TERMINATE_FCCVI_STORAGE()
	#define	_GET_FREE_FCCVI()				_getFreeFCCVIDynamic()
	#define	_RELEASE_FCCVI(_pFCCI)			_releaseFCCVIDynamic(_pFCCI)
#else
	static FFatErr		_initFCCVIStorageStatic(void);
	static void			_terminateFCCVIStorageStatic(void);
	static FCCVolInfo*	_getFreeFCCVIStatic(void);
	static FFatErr		_releaseFCCVIStatic(FCCVolInfo* pFCCVI);

	#define	_INIT_FCCVI_STORAGE			_initFCCVIStorageStatic
	#define	_TERMINATE_FCCVI_STORAGE	_terminateFCCVIStorageStatic
	#define	_GET_FREE_FCCVI				_getFreeFCCVIStatic
	#define	_RELEASE_FCCVI				_releaseFCCVIStatic
#endif

#ifdef _FCC_DEBUG
	#define		FFAT_DEBUG_FCC_PRINTF(_msg)		FFAT_PRINT_VERBOSE((_T("BTFS_FCC, %s()"), __FUNCTION__)); FFAT_PRINT_VERBOSE(_msg)
#else
	#define		FFAT_DEBUG_FCC_PRINTF(_msg)
#endif

// debug begin
#ifdef _FCC_STATISTICS
	typedef struct
	{
		t_uint32		dwFCCHit;		// GFS Hit Count
		t_uint32		dwFCCMiss;		// GFS Miss Count
		t_uint32		dwFCCReplace;	// GFS Entry Replace Count

		t_uint32		dwFccInit;
		t_uint32		dwFccerminate;
		t_uint32		dwFccMount;
		t_uint32		dwFccSync;
		t_uint32		dwFccUmount;
		t_uint32		dwFccAddFreeClustersFromTo;
		t_uint32		dwFccSyncVol;
		t_uint32		dwFccDeallocateCluster;
		t_uint32		dwFccGetFreeClusters;
		t_uint32		dwFccGetFreeClustersFromTo;
		t_uint32		dwFccGetFCCOfSector;
		t_uint32		dwFccGetVolumeStatus;
		t_uint32		dwFccRemoveFreeClusters;
		t_uint32		dwFccRemoveFreeClustersVC;
		t_uint32		dwFccAddFreeClusters;
		t_uint32		dwFccAddFreeClustersVC;

		t_uint32		dwFcc_getClusters;
		t_uint32		dwFcc_getClustersFromTo;
		t_uint32		dwFcc_addAndDeallocateClustersVC;
		t_uint32		dwFcc_addClusters;
		t_uint32		dwFcc_getFreeEntry;
		t_uint32		dwFcc_removeClustersVC;
		t_uint32		dwFcc_removeClusters;
		t_uint32		dwFcc_removeEntry;
		t_uint32		dwFcc_updateEntry;
		t_uint32		dwFcc_addEntry;
		t_uint32		dwFcc_getFreeClusterCount16;
		t_uint32		dwFcc_getFreeClusterCount32;
		t_uint32		dwFcc_getFreeClusterCount;
		t_uint32		dwFcc_initFCCVI;
		t_uint32		dwFcc_initFCCVIStorageStatic;
		t_uint32		dwFcc_terminateFCCVIStorageStatic;
		t_uint32		dwFcc_getFreeFCCVIStatic;
		t_uint32		dwFcc_releaseFCCVIStatic;

	} _debugStatisc;

	#define _STATISTIC()		((_debugStatisc*)&_stStatics)

	static _debugStatisc	_stStatics;
	//static t_int32 _stStatics[sizeof(_debugStatisc) / sizeof(t_int32)];

	#define _STATISTICS_FCC_HIT			_STATISTIC()->dwFCCHit++;
	#define _STATISTICS_FCC_MISS		_STATISTIC()->dwFCCMiss++;
	#define _STATISTICS_FCC_REPLACE		_STATISTIC()->dwFCCReplace++;

	#define _STATISTIC_FCC_INIT						_STATISTIC()->dwFccInit++;
	#define _STATISTIC_FCC_TERMINATE				_STATISTIC()->dwFccerminate++;
	#define _STATISTIC_FCC_MOUNT					_STATISTIC()->dwFccMount++;
	#define _STATISTIC_FCC_SYNC						_STATISTIC()->dwFccSync++;
	#define _STATISTIC_FCC_UMOUNT					_STATISTIC()->dwFccUmount++;
	#define _STATISTIC_FCC_ADDFREECLUSTERSFROMTO	_STATISTIC()->dwFccAddFreeClustersFromTo++;
	#define _STATISTIC_FCC_SYNCVOL					_STATISTIC()->dwFccSyncVol++;
	#define _STATISTIC_FCC_DEALLOCATECLUSTER		_STATISTIC()->dwFccDeallocateCluster++;
	#define _STATISTIC_FCC_GETFREECLUSTERS			_STATISTIC()->dwFccGetFreeClusters++;
	#define _STATISTIC_FCC_GETFREECLUSTERSFROMTO	_STATISTIC()->dwFccGetFreeClustersFromTo++;
	#define _STATISTIC_FCC_GETFCCOFSECTOR			_STATISTIC()->dwFccGetFCCOfSector++;
	#define _STATISTIC_FCC_GETVOLUMESTATUS			_STATISTIC()->dwFccGetVolumeStatus++;
	#define _STATISTIC_FCC_REMOVEFREECLUSTERS		_STATISTIC()->dwFccRemoveFreeClusters++;
	#define _STATISTIC_FCC_REMOVEFREECLUSTERSVC		_STATISTIC()->dwFccRemoveFreeClustersVC++;
	#define _STATISTIC_FCC_ADDFREECLUSTERS			_STATISTIC()->dwFccAddFreeClusters++;
	#define _STATISTIC_FCC_ADDFREECLUSTERSVC		_STATISTIC()->dwFccAddFreeClustersVC++;

	#define _STATISTIC_FCC_GETCLUSTERS					_STATISTIC()->dwFcc_getClusters++;
	#define _STATISTIC_FCC_GETCLUSTERSFROMTO			_STATISTIC()->dwFcc_getClustersFromTo++;
	#define _STATISTIC_FCC_ADDANDDEALLOCATECLUSTERVC	_STATISTIC()->dwFcc_addAndDeallocateClustersVC++;
	#define _STATISTIC_FCC_ADDCLUSTERS					_STATISTIC()->dwFcc_addClusters++;
	#define _STATISTIC_FCC_GETFREEENTRY					_STATISTIC()->dwFcc_getFreeEntry++;
	#define _STATISTIC_FCC_REMOVECLUSTERSVC				_STATISTIC()->dwFcc_removeClustersVC++;
	#define _STATISTIC_FCC_REMOVECLUSTERS				_STATISTIC()->dwFcc_removeClusters++;
	#define _STATISTIC_FCC_REMOVEENTRY					_STATISTIC()->dwFcc_removeEntry++;
	#define _STATISTIC_FCC_UPDATEENTRY					_STATISTIC()->dwFcc_updateEntry++;
	#define _STATISTIC_FCC_ADDENTRY						_STATISTIC()->dwFcc_addEntry++;
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT16		_STATISTIC()->dwFcc_getFreeClusterCount16++;
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT32		_STATISTIC()->dwFcc_getFreeClusterCount32++;
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT			_STATISTIC()->dwFcc_getFreeClusterCount++;
	#define _STATISTIC_FCC_INITFCCVI					_STATISTIC()->dwFcc_initFCCVI++;
	#define _STATISTIC_FCC_INITFCCVISTORAGESTATIC		_STATISTIC()->dwFcc_initFCCVIStorageStatic++;
	#define _STATISTIC_FCC_TERMINATEFCCVISTORAGESTATIC	_STATISTIC()->dwFcc_terminateFCCVIStorageStatic++;
	#define _STATISTIC_FCC_GETFREEFCCVISTATIC			_STATISTIC()->dwFcc_getFreeFCCVIStatic++;
	#define _STATISTIC_FCC_RELEASEFCCVISTATIC			_STATISTIC()->dwFcc_releaseFCCVIStatic++;

	#define _STATISTIC_INIT				FFAT_MEMSET(&_stStatics, 0x00, sizeof(_stStatics));
	#define _STATISTIC_PRINT			_printStatistics();

	static void			_printStatistics(void);
#else
	#define _STATISTICS_FCC_HIT
	#define _STATISTICS_FCC_MISS
	#define _STATISTICS_FCC_REPLACE
	#define _STATISTIC_INIT
	#define _STATISTIC_PRINT

	#define _STATISTIC_FCC_INIT
	#define _STATISTIC_FCC_TERMINATE
	#define _STATISTIC_FCC_MOUNT
	#define _STATISTIC_FCC_SYNC
	#define _STATISTIC_FCC_UMOUNT
	#define _STATISTIC_FCC_ADDFREECLUSTERSFROMTO
	#define _STATISTIC_FCC_SYNCVOL
	#define _STATISTIC_FCC_DEALLOCATECLUSTER
	#define _STATISTIC_FCC_GETFREECLUSTERS
	#define _STATISTIC_FCC_GETFREECLUSTERSFROMTO
	#define _STATISTIC_FCC_GETFCCOFSECTOR
	#define _STATISTIC_FCC_GETVOLUMESTATUS
	#define _STATISTIC_FCC_REMOVEFREECLUSTERS
	#define _STATISTIC_FCC_REMOVEFREECLUSTERSVC
	#define _STATISTIC_FCC_ADDFREECLUSTERS
	#define _STATISTIC_FCC_ADDFREECLUSTERSVC

	#define _STATISTIC_FCC_GETCLUSTERS
	#define _STATISTIC_FCC_GETCLUSTERSFROMTO
	#define _STATISTIC_FCC_ADDANDDEALLOCATECLUSTERVC
	#define _STATISTIC_FCC_ADDCLUSTERS
	#define _STATISTIC_FCC_GETFREEENTRY
	#define _STATISTIC_FCC_REMOVECLUSTERSVC
	#define _STATISTIC_FCC_REMOVECLUSTERS
	#define _STATISTIC_FCC_REMOVEENTRY
	#define _STATISTIC_FCC_UPDATEENTRY
	#define _STATISTIC_FCC_ADDENTRY
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT16
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT32
	#define _STATISTIC_FCC_GETFREECLUSTERCOUNT
	#define _STATISTIC_FCC_INITFCCVI
	#define _STATISTIC_FCC_INITFCCVISTORAGESTATIC
	#define _STATISTIC_FCC_TERMINATEFCCVISTORAGESTATIC
	#define _STATISTIC_FCC_GETFREEFCCVISTATIC
	#define _STATISTIC_FCC_RELEASEFCCVISTATIC
#endif
// debug end


/**
* initialize free cluster cache
*
* @return		FFAT_OK 	: success
* @return		else		: error
* @author		DongYoung Seo
* @version		OCT-31-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fcc_init(void)
{
	t_int8*		pBuff;
	t_int32		dwSize;
	_FCCEntry*	pEntry;
	t_int32		dwEntryCount;
	t_int32		i;
	FFatErr		r;

	_STATISTIC_INIT
	_STATISTIC_FCC_INIT

	FFAT_ASSERT(FFAT_ADDON_FCC_MEM_SIZE >= FFAT_FCC_BUFF_MIN);

	pBuff = (t_int8*)FFAT_MALLOC(FFAT_ADDON_FCC_MEM_SIZE, ESS_MALLOC_NONE);
	if (pBuff == NULL)
	{
		r = FFAT_ENOMEM;
		FFAT_LOG_PRINTF((_T("fail to allocate memory for FCC")));
		goto out;
	}

	FFAT_MEMSET(pBuff, 0, FFAT_ADDON_FCC_MEM_SIZE);

	// allocate memory for Vol Log info
	_pFCCMain = (_FCCMain*)pBuff;
	FFAT_MEMSET(_pFCCMain, 0x00, sizeof(_FCCMain));

	ESS_DLIST_INIT(_FCCMAIN_LF(_pFCCMain));
	ESS_DLIST_INIT(_FCCMAIN_LV(_pFCCMain));

	// get buffer size for _FCCEntry
	dwSize = FFAT_ADDON_FCC_MEM_SIZE - sizeof(_FCCMain);

	dwEntryCount = dwSize / sizeof(_FCCEntry);

	pEntry = (_FCCEntry*)(pBuff + sizeof(_FCCMain));

	for (i = 0; i < dwEntryCount; i++)
	{
		ESS_DLIST_INIT(_FCCE_LD(pEntry));

		ESS_DLIST_INIT(_FCCE_LL(pEntry));
		ESS_DLIST_ADD_HEAD(_FCCMAIN_LF(_pFCCMain), _FCCE_LL(pEntry));

		_FCCE_SET_CLUSTER(pEntry, 0);
		_FCCE_SET_COUNT(pEntry, 0);
		
		pEntry->pVol = NULL;

		pEntry = (_FCCEntry*)(pEntry + 1);
	}

	r = _INIT_FCCVI_STORAGE();
	FFAT_EO(r, (_T("fail to init FCC infomration storage")));

out:
	if (r != FFAT_OK)
	{
		// free memory
		if ((pBuff) && (_pFCCMain->pFCCVI))
		{
			_TERMINATE_FCCVI_STORAGE();
		}

		FFAT_FREE(_pFCCMain, FFAT_ADDON_FCC_MEM_SIZE);
	}

	return r;
}


/**
* Init volume for free cluster cache
*
* @param		pVol		: [IN] volume pointer
* @return		FFAT_OK 	: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		OCT-31-2006 [DongYoung Seo]		First Writing.
*/
FFatErr
ffat_fcc_mount(Vol* pVol)
{
	FFatErr		r;

	_STATISTIC_FCC_MOUNT

	FFAT_ASSERT(pVol);

	if (pVol->dwFlag & VOL_RDONLY)
	{
		_FCC(pVol) = NULL;
		return FFAT_OK;
	}

	_FCC(pVol) = _GET_FREE_FCCVI();
	IF_UK (_FCC(pVol) == NULL)
	{
		return FFAT_ENOENT;
	}

	r = _initFCCVI(_FCC(pVol));
	if (r != FFAT_OK)
	{
		_RELEASE_FCCVI(_FCC(pVol));
		_FCC(pVol) = NULL;
	}

	return r;
}


/**
* release(disable) free cluster cache, when un-mounting
* (It does not free memory)
*
* @param		pVol		: [IN] volume pointer
* @param		dwFlag		: [IN] flag for un-mount operation
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK 	: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		OCT-31-2006 [DongYoung Seo] First Writing.
* @version		NOV-27-2006 [DongYoung Seo] Add dwFlag to check force un-mount
*/
FFatErr
ffat_fcc_umount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FCCVolInfo* 	pFCC;
	_FCCEntry*		pEntry;
	EssRBNode2* 	pNode;
	FFatCacheFlag	dwCFlag;
	FFatErr 		r;

	_STATISTIC_FCC_UMOUNT

	FFAT_ASSERT(pVol);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	pFCC = _FCC(pVol);

	if (dwFlag & FFAT_UMOUNT_FORCE)
	{
		dwCFlag = FFAT_CACHE_FORCE | FFAT_CACHE_SYNC;
	}
	else
	{
		dwCFlag = FFAT_CACHE_SYNC;
	}

	// Don't care error (Here is un-mounting routine)
	r = ffat_fcc_syncVol(pVol, dwCFlag, pCxt);
	if ((r < 0) && ((dwFlag & FFAT_UMOUNT_FORCE) == 0))
	{
		return r;
	}

	do
	{
		pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), 0);
		if (pNode == NULL)
		{
			break;
		}
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		FFAT_ASSERT(pEntry->pVol == pVol);
		
		r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, (dwFlag & FFAT_UMOUNT_FORCE), pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));

	}while(1);

	pFCC->dwFreeClusterCount = 0;

	ESS_DLIST_INIT(_FCC_LD(pFCC));

	_RELEASE_FCCVI(pFCC);

	(VOL_ADDON(pVol))->pFCC = NULL;

	return FFAT_OK;
}


/**
* Initializes FCC
*
* @return		FFAT_OK 	: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		SEP-27-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fcc_terminate(void)
{
	_STATISTIC_FCC_TERMINATE

	if (_pFCCMain)
	{
		_TERMINATE_FCCVI_STORAGE();

		FFAT_FREE(_pFCCMain, FFAT_ADDON_FCC_MEM_SIZE);
		_pFCCMain = NULL;
	}

	_STATISTIC_PRINT

	return FFAT_OK; 
}


/**
* sync FCC's dirty FCCEs(Free Cluster Cache Entry) 
*
* @param		pVol			: [IN] volume pointer
* @param		dwCFlag 		: [IN] Flag for Cache I/O
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: success
* @return		FFAT_EINVALID	: invalid parameter
* @return		negative		: fail
* @author		Soojeong Kim
* @version		AUG-22-2007 [Soojeong Kim]	 First Writing.
* @version		NOV-22-2007 [DongYoung Seo]  Add cache flag
*/
FFatErr
ffat_fcc_syncVol(Vol* pVol, FFatCacheFlag dwCFlag, ComCxt* pCxt)
{
	FCCVolInfo* 		pFCC;
	_FCCEntry*			pFCCE;
	_FCCEntry*			pNext;
	FFatVC				stVC_Temp;
	t_uint32			dwDeallocatedCount = 0;
	FatDeallocateFlag	dwDAFlag;			// deallocate flag
	FFatErr 			r;

	_STATISTIC_FCC_SYNCVOL

	FFAT_ASSERT(pVol);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	pFCC = _FCC(pVol);

	FFAT_DEBUG_FCC_PRINTF((_T("pVol:0x%X\n"), (t_uint32)pVol));

	if (ESS_DLIST_IS_EMPTY(_FCC_LD(pFCC)))
	{
		FFAT_DEBUG_FCC_PRINTF((_T("pVol:0x%X - No dirty entry\n"), (t_uint32)pVol));
		return FFAT_OK;
	}

	dwCFlag |= (FFAT_CACHE_SYNC | FFAT_CACHE_DATA_FAT); 	// add sync flag

	if (dwCFlag & FFAT_CACHE_FORCE)
	{
		dwDAFlag = FAT_DEALLOCATE_FORCE;
	}
	else
	{
		dwDAFlag = FAT_ALLOCATE_NONE;
	}

	VC_INIT(&stVC_Temp, VC_NO_OFFSET);

	stVC_Temp.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT (stVC_Temp.pVCE != NULL);

	stVC_Temp.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
	stVC_Temp.pVCE[0].dwCluster	= 0;
	stVC_Temp.pVCE[0].dwCount	= 0;

	// Dirty list�� cluster number�� sort�Ǿ�� �Ѵ�!!!
	// [en] Dirty list must be sorted by cluster number
	ESS_DLIST_FOR_EACH_ENTRY_SAFE(_FCCEntry, pFCCE, pNext, _FCC_LD(pFCC), dlDirty)
	{
		stVC_Temp.pVCE[VC_VEC(&stVC_Temp)].dwCluster	= _FCCE_CLUSTER(pFCCE);
		stVC_Temp.pVCE[VC_VEC(&stVC_Temp)].dwCount		= _FCCE_COUNT(pFCCE);
		stVC_Temp.dwTotalClusterCount					+= _FCCE_COUNT(pFCCE);

		// �������� �̰��� FAT���� free�� ��ϵǾ� �ִ�
		// [en] from now on, this is recording as free at FAT 
		// delete from used���� ���� �ʿ�� ����
		// [en] it needs not delete at deleting from used
		FFAT_DEBUG_FCC_PRINTF((_T("pVol:0x%X - dirty entry, Cluster/Count/%d/%d\n"), (t_uint32)pVol, _FCCE_CLUSTER(pFCCE), _FCCE_COUNT(pFCCE)));

		stVC_Temp.dwValidEntryCount++;

		if (VC_IS_FULL(&stVC_Temp) == FFAT_TRUE)
		{
			r = FFATFS_FreeClusterVC(VOL_VI(pVol), &stVC_Temp,
							&dwDeallocatedCount, dwDAFlag, dwCFlag, NULL, pCxt);
			FFAT_EO(r, (_T("Fail to free clusters")));

			FFAT_ASSERT (dwDeallocatedCount == stVC_Temp.dwTotalClusterCount);

			VC_INIT(&stVC_Temp, 0);
			dwDeallocatedCount = 0;
		}

		ESS_DLIST_DEL_INIT(_FCCE_LD(pFCCE));
	}

	if (VC_IS_EMPTY(&stVC_Temp) == FFAT_FALSE)
	{
		r = FFATFS_FreeClusterVC(VOL_VI(pVol), &stVC_Temp,
						&dwDeallocatedCount, dwDAFlag, dwCFlag, NULL, pCxt);
		FFAT_EO(r, (_T("fail to free clusters")));

		FFAT_ASSERT (dwDeallocatedCount == stVC_Temp.dwTotalClusterCount);
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVC_Temp.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	return r;
}


/**
* deallocate new clusters 
*
* fast unlink �� ���� deallocate�Ҷ� FCC�� cluster������ �߰��ϰ�
* ���� deallocate�� ���� �ʴ´�.
* [en] in case of deallocation, cluster information is added at FCC for fast unlink 
*      and it does not execute real deallocation
* ��, FCC�� free entry�� ������� ������ FCC�� �߰����� �ʰ� deallocate�Ѵ�.
* [en] Merely, in case no free entry of FCC, it does not add at FCC and executes deallocation
* ���� ! dwCount�� ���� 0���� ���´�. deallocate�� EOF�� ������ ���� ��ӵȴ�.
* [en] Attention! dwCount is usually 0. deallocation keeps going by meeting EOF
*
* @param		pNode			: [IN] node pointer
* @param		dwNewEOC		: [IN] new EOC
*									New cluster�� ���⿡ ������ �ȴ�.
*								[en] New cluster is connected here
*									�ʿ��� cluster ��ȣ�� ù��° cluster �̸�
*									�������� optional �̴�.
*								[en] needing cluster number is first cluster and reminder is optional
* @param		pdwFirstCluster	: [IN] first cluster number to be deallocated
*								  [OUT] first deallocated cluster number
*									may not have valid information
*									dwNewEOC must be a valid cluster 
*									when this value is not a valid cluster
* @param		dwCount 		: [IN] cluster count to deallocate
* @param		pdwDeallocCount : [OUT] deallocate�� cluster count
* @param		pVC				: [IN/OUT] cluster information
*									may be NULL
* @param		dwFAFlag		: [IN] FAT allocation flag
* @param		dwCacheFlag 	: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: FCC is not active state
* @return		FFAT_DONE		: success
* @return		else			: error
* @author		Soojeong Kim
* @version		AUG-17-2007 [Soojeong Kim]	First Writing.
* @version		DEC-27-2007 [InHwan Choi]	FCC refactoring
* @version		JUL-03-2008 [DongYoung Seo] update pdwFirstCluster implicitly.
* @version		SEP-02-2008 [DongYoung Seo] remove spare cluster deallocation routine
*									to add all deallocated clusters to FCC
* @version		SEP-03-2008 [DongYoung Seo] remove FFAT_FS_FAT_UPDATECLUSTER
* @version		SEP-08-2008 [DongYoung Seo] add routine for sync mode deallocation.
*									it must be deallocated backward.
* @version		JUN-08-2009 [JeongWoo Park] remove the wrong AST code about VC_VEC(pVC) > 0.
*									it can be happened at the mounted volume with NO_LOG | SYNC_META
*/
FFatErr
ffat_fcc_deallocateCluster(Node* pNode, t_uint32 dwNewEOC,
							t_uint32* pdwFirstCluster, t_uint32 dwCount,
							t_uint32* pdwDeallocCount, FFatVC* pVC,
							FatAllocateFlag dwFAFlag, FFatCacheFlag dwCacheFlag,
							ComCxt* pCxt)
{
	FFatVC		stVC;
	Vol*		pVol;
	t_uint32	dwFirstFree = 0;
	t_uint32	dwNextCluster;
	t_uint32	dwFreeCount;			// total free count
	FFatErr		r = FFAT_OK;
	t_int32		i;
	
	_STATISTIC_FCC_DEALLOCATECLUSTER

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwFirstCluster);
	FFAT_ASSERT(pdwDeallocCount);
	FFAT_ASSERT(*pdwDeallocCount == 0);

	pVol = NODE_VOL(pNode);

	stVC.pVCE = NULL;

	// get free cluster
	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// free cluster cache is not activated
		return FFAT_OK;
	}

	FFAT_DEBUG_FCC_PRINTF((_T("pNode/NODE_C/dwNewEOC/dwCount:0x%X/%d/%d/%d\n"), (t_uint32)pNode, NODE_C(pNode), dwNewEOC, dwCount));

	// check sync flag on dwCacheFlag
	// the cluster deallocation routine must be free cluster from end of chain to the head
	// FCC does not consider this case and just add pVC only with clean mode
	if (dwCacheFlag & FFAT_CACHE_SYNC)
	{
		if ((pVC == NULL) ||
			(VC_VEC(pVC) == 0))
		{
			// DO nothing
			return FFAT_OK;
		}
		
		FFAT_ASSERT(VC_CC(pVC) > 0);

// debug begin
#ifdef FFAT_DEBUG
		if (VC_IS_FULL(pVC) == FFAT_FALSE)
		{
				t_uint32		dwTempCluster;

				// THE LAST CLUSTER MUST BE EOC
				r = ffat_misc_getNextCluster(pNode, VC_LC(pVC), &dwTempCluster, pCxt);
				FFAT_ER(r, (_T("fail to get next cluster")));

				FFAT_ASSERT(FFATFS_IS_EOF(VOL_VI(pVol), dwTempCluster) == FFAT_TRUE);
		}
#endif
// debug end

		for (i = 0; i < VC_VEC(pVC); i++)
		{
			r = _addClusters(pVol, pVC->pVCE[i].dwCluster, pVC->pVCE[i].dwCount,
							FFAT_FALSE, FFAT_FALSE, pCxt);
			if (r == FFAT_ENOENT)
			{
				// no more free entry at FCC
				return FFAT_OK;
			}
			else if (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to add clusters to FCC")));
				return r;
			}
		}

		return FFAT_OK;
	}

	IF_UK (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		dwCacheFlag |= FFAT_CACHE_DATA_DE;		// set DE flag
	}

	dwFreeCount		= 0;						// initialize

	// deallocate, if pVC have valid information
	// pVC�� deallocate�� ������ �ִٸ� ó���Ѵ�.
	// [en] it is processed if there is deallocating information at pVC
	// EOF�� ������ return�Ѵ�.
	// [en] it is returned if it meets EOF
	if ((pVC != NULL) && (VC_CC(pVC) > 0))
	{
		FFAT_ASSERT(pVC->pVCE);
		FFAT_ASSERT(VC_VEC(pVC) > 0);
		FFAT_ASSERT(*pdwFirstCluster == VC_FC(pVC));

		// get next cluster of the last cluster to check there is some rest clusters
		r = ffat_misc_getNextCluster(pNode, VC_LC(pVC), &dwNextCluster, pCxt);
		FFAT_EO(r, (_T("fail to get the next cluster")));

		r = _addAndDeallocateClustersVC(NODE_VOL(pNode), pVC, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to deallocate clusters")));

		dwFreeCount = VC_CC(pVC);

		// deallocation complete!!
		dwFirstFree = VC_FC(pVC);
	}
	else
	{
		if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwNewEOC) == FFAT_TRUE)
		{
			r = ffat_misc_getNextCluster(pNode, dwNewEOC, &dwNextCluster, pCxt);
			FFAT_EO(r, (_T("fail to get the next cluster")));
		}
		else
		{
			dwNextCluster = *pdwFirstCluster;
		}
	}

	if(FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) == FFAT_TRUE)
	{
		goto out;
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwNextCluster) == FFAT_TRUE);

	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE != NULL);

	stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE /  sizeof(FFatVCE);

	do
	{
		// get free cluster from FCC
		VC_INIT(&stVC, VC_NO_OFFSET);
		stVC.pVCE[0].dwCluster	= 0;
		stVC.pVCE[0].dwCount	= 0;

		r = FFATFS_GetVectoredCluster(NODE_VI(pNode), dwNextCluster, 0, &stVC, FFAT_FALSE, pCxt);
		FFAT_EO(r, (_T("Fail to get vectored cluster.")));

		FFAT_ASSERT((VC_CC(&stVC) > 0) && (VC_VEC(&stVC) > 0));
		FFAT_ASSERT(dwNextCluster == VC_FC(&stVC));

		// check the last cluster is the real EOC
		r = ffat_misc_getNextCluster(pNode, VC_LC(&stVC), &dwNextCluster, pCxt);
		FFAT_EO(r, (_T("fail to get the next cluster")));

		r = _addAndDeallocateClustersVC(NODE_VOL(pNode), &stVC, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to deallocate clusters")));

		dwFreeCount += VC_CC(&stVC);

		if (dwFirstFree == 0)
		{
			dwFirstFree = VC_FC(&stVC);
		}

		if(FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) == FFAT_TRUE)
		{
			// done
			break;
		}
	} while (1);

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	IF_LK (r >= 0)
	{
		FFATFS_IncFreeClusterCount(NODE_VI(pNode), dwFreeCount, pCxt);

		if (dwNewEOC != 0)
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwNewEOC) == FFAT_TRUE);
			r = FFATFS_UpdateCluster(NODE_VI(pNode), dwNewEOC, NODE_VI(pNode)->dwEOC, dwCacheFlag, pNode, pCxt);
			if (r < 0)
			{
				if ((dwFAFlag & FAT_ALLOCATE_FORCE) == 0)
				{
					FFAT_DEBUG_PRINTF((_T("fail to get sector")));
					goto out;
				}
			}
		}

		if (dwFirstFree != 0)
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwFirstFree) == FFAT_TRUE);
			VI_SET_FCH(NODE_VI(pNode), dwFirstFree);
		}

		*pdwDeallocCount	= dwFreeCount;
		*pdwFirstCluster	= dwFirstFree;		// first deallocated cluster number

		r = FFAT_DONE;
	}

	return r;
}


/**
 * get free clusters
 *
 *	free cluster�� ������ FCC�� ���ؼ��� ���� �� �ִ�.
 * [en] free cluster can be got through only FCC
 *	���� FCC�� ���ϴ� �� ��ŭ �������� FCC�� �߰� ���� ��´�.
 * [en] if the number of free cluster in FCC is not enough, 
 *      free cluster is got after adding free cluster at FCC
 *	�� �Լ��� ȣ��� ��, pVC�� ����ִٰ� �����Ѵ�.
 * [en] assume that pVC is empty when this function is invoked
 *	VC�� full�� �ɶ��� FFAT_ENOSPC �� return�Ѵ�.
 * [en] FFAT_ENOSPC returns when VC is full
 *
 * @param		pNode				: [IN] node pointer
 * @param		dwPrevEOF			: [IN] end of file cluster 
 *										New cluster�� ���⿡ ������ �ȴ�.
 *									[en] New cluster is connected here
 *										�ʿ��� cluster ��ȣ�� ù��° cluster �̸�
 *										�������� optional �̴�.
 *									[en] needing cluster number is first cluster and reminder is optional
 * @param		dwCount 			: [IN] cluster count to allocate
 * @param		pVC					: [IN/OUT] cluster information never be NULL
 *										must be an empty one.
 * @param		dwHint				: [IN] lookup hint
 * @param		pdwFreeCount		: [OUT] count of free clusters added to pVC
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		pCxt				: [IN] context of current operation
 * @return		FFAT_OK 			: FCC is not active state
 * @return		FFAT_DONE			: dwCount ��ŭ�� cluster�� �����.
 *									[en] cluster is got as many as dwCount
 * @return		FFAT_ENOSPC 		: pVC�� �� �����Ȱų� dwCount��ŭ ���� ���ߴ�. pdwFreeCount�� ��ȯ�Ѵ�.
 *									[en] pVC is full or it is not got as many as dwCount. pdwFreeCount returns
 *										or not enough free entry at pVC (pdwFreeCount is updated)
 * @return		else				: error
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 * @history		AUG-28-2007 [Soojeong Kim]	Second Writing.
 * @history		DEC-29-2007 [InHwan Choi]	FCC refactoring.
 * @history		AUG-26-2007 [DongYoung Seo] add checking routine for pVC full
 *										return FFAT_ENOSPC when pVC is full or there is no free cluster
 * @history		OCT-09-2008 [GwangOk Go]	do not add free cluster again after get free cluster
 * @history		OCT-22-2008 [GwangOk Go]	even if not enough free cluster, return partial free cluster
 * @history		OCT-28-2008 [DongYoung Seo] sync FCC before free cluster requesting to FFATFS.
 *											to guarantee free cluster count consistency between FFATFS and FCC
* @history		NOV-06-2008 [DongYoung Seo] update  *pdwFreeCount when FCC does not have enough free clusters
 */
FFatErr
ffat_fcc_getFreeClusters(Node* pNode, t_uint32 dwCount, FFatVC* pVC,
							t_uint32 dwHint, t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	FCCVolInfo*		pFCC;
	FFatErr			r;
	FFatErr			rr;
	Vol*			pVol;
	t_uint32		dwFreeCluster;
	t_uint32		dwCurFreeCount;		// current free cluster count
	t_uint32		dwTotalFreeCount;	// total free cluster count
	t_int32			dwCurEntry = 0;
	t_int32			dwIndex;

// debug begin
#ifdef _FCC_STATISTICS
	t_boolean		bCacheMiss = FFAT_FALSE;
#endif
// debug end

	_STATISTIC_FCC_GETFREECLUSTERS

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pVC->pVCE);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT((VC_VEC(pVC) == 0) && (VC_CC(pVC) == 0));

	pVol = NODE_VOL(pNode);
	pFCC = _FCC(pVol);

	// get free cluster
	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// free cluster cache is not activated
		return FFAT_OK;
	}

	dwTotalFreeCount	= 0;
	*pdwFreeCount		= 0;

	// FCC�� free cluster count�� ���ϴ� ��ŭ �������� FAT�� �о� FCC�� ä�� �ִ´�.
	// [en] when free cluster count is not enough as many as it want, 
	//      free cluster count is filled at FCC by reading FAT
	if (pFCC->dwFreeClusterCount < dwCount)
	{
// debug begin
#ifdef _FCC_STATISTICS
		bCacheMiss = FFAT_TRUE;
#endif
// debug end

		// sync volume
		//	do not remove sync volume
		//	FFATFS update Free cluster count when it can not get the requested count of free clusters.
		//	But some of free clusters are may be at FCC with dirty state.
		//	So FCC must be synchronized to make clean state of volume
		r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
		FFAT_EO(r, (_T("fail to sync dirty Entry")));

		// get free cluster from FFATFS
		r = FFATFS_GetFreeClusters(VOL_VI(pVol), dwCount, pVC, dwHint, FFAT_TRUE, pCxt);
		if (r < 0)
		{
			if (r == FFAT_ENOSPC)
			{
				// FVC is full or there is not enough free clusters

				// remove free cluster information from FCC for free cluster information consistency
				// remove free clusters from FCC
				for (dwIndex = 0 ; dwIndex < VC_VEC(pVC); dwIndex++)
				{
					rr = _removeClusters(pVol, pVC->pVCE[dwIndex].dwCluster,
										pVC->pVCE[dwIndex].dwCount, pCxt);
					FFAT_ER(rr, (_T("Fail to remove cluster in FCC.")));
				}

				if (VC_IS_EMPTY(pVC) == FFAT_FALSE)
				{
					FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC)) == FFAT_TRUE);
					VI_SET_FCH(NODE_VI(pNode), (VC_LC(pVC) + 1));
				}
			}
// debug begin
			else
			{
				FFAT_DEBUG_PRINTF((_T("Fail to get Free clusters")));
			}
// debug end
			goto out;
		}

		FFAT_ASSERT(VC_CC(pVC) >= dwCount);

		// ADD ADDITIONAL CLUSTERS TO FCC
		if (VC_CC(pVC) > dwCount)
		{
			dwCurFreeCount = VC_CC(pVC);
			dwIndex = VC_LEI(pVC);

			// remove clusters from FCC
			do
			{
				FFAT_ASSERT(dwCurFreeCount >= pVC->pVCE[dwIndex].dwCount);

				dwCurFreeCount -= pVC->pVCE[dwIndex].dwCount;		// decrease cluster count
				if (dwCurFreeCount < dwCount)
				{
					VC_CC(pVC) = dwCount;							// set cluster count

					dwFreeCluster	= pVC->pVCE[dwIndex].dwCluster + (dwCount - dwCurFreeCount);
					dwCount			= (dwCurFreeCount + pVC->pVCE[dwIndex].dwCount) - VC_CC(pVC);

					if (dwCount > 0)
					{
						// add partial entry
						r = _addClusters(pVol, dwFreeCluster, dwCount, FFAT_FALSE, FFAT_TRUE, pCxt);
						FFAT_ER(r, (_T("Fail to add cluster in FCC.")));
					}

					FFAT_ASSERT(pVC->pVCE[dwIndex].dwCount > dwCount);

					pVC->pVCE[dwIndex].dwCount = pVC->pVCE[dwIndex].dwCount - dwCount;
					VC_VEC(pVC) = dwIndex + 1;
					break;
				}
				else if (dwCurFreeCount == dwCount)
				{
					VC_VEC(pVC) = dwIndex;
					VC_CC(pVC) = dwCount;			// set cluster count
					break;
				}

				// add whole entry
				r = _addClusters(pVol, pVC->pVCE[dwIndex].dwCluster,
									pVC->pVCE[dwIndex].dwCount, FFAT_FALSE, FFAT_TRUE, pCxt);
				FFAT_ER(r, (_T("Fail to add cluster in FCC.")));
			} while (--dwIndex >= 0);

			FFAT_ASSERT(dwIndex >= 0);
		}

		// remove clusters from FCC
		for (dwIndex = 0 ; dwIndex < VC_VEC(pVC); dwIndex++)
		{
			r = _removeClusters(pVol, pVC->pVCE[dwIndex].dwCluster,
							pVC->pVCE[dwIndex].dwCount, pCxt);
			FFAT_ER(r, (_T("Fail to remove cluster in FCC.")));
		}

		r = FFAT_DONE;
		goto out;
	}

	// get free cluster from FCC
	dwFreeCluster = dwHint;
	dwCurFreeCount = 0;

	// get clusters
	while (dwCount > 0)
	{
		FFAT_ASSERT(pFCC->dwFreeClusterCount >= dwCount);
		FFAT_ASSERT(dwFreeCluster + dwCurFreeCount <= VOL_LCN(pVol));
	
		// FCC���� dwCount��ŭ free cluster�� �ݵ�� �־�� �Ѵ�.
		// [en] free cluster in FCC must exist as many as dwCount
		r = _getClusters(pVol, dwHint, dwCount, &dwFreeCluster, &dwCurFreeCount, pCxt);
		IF_UK (r < 0)
		{
			FFAT_ASSERT(0);
			FFAT_DEBUG_FCC_PRINTF(_T("Fail to get free cluster from FCC, There is critical problem in FCC algorism\n"));
			goto out;
		}

		FFAT_ASSERT(dwCurFreeCount > 0);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwFreeCluster) == FFAT_TRUE);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), (dwFreeCluster + dwCurFreeCount - 1)) == FFAT_TRUE);

		pVC->dwValidEntryCount++;
		pVC->pVCE[dwCurEntry].dwCluster	= dwFreeCluster;
		pVC->pVCE[dwCurEntry].dwCount	= dwCurFreeCount;
		pVC->dwTotalClusterCount		+= dwCurFreeCount;

		dwCurEntry++;
		dwTotalFreeCount += dwCurFreeCount;
		dwCount			 -= dwCurFreeCount;

		// VC is full, return
		if (VC_IS_FULL(pVC) == FFAT_TRUE)
		{
			// no more free entry
			break;
		}

		FFAT_ASSERT(dwFreeCluster + dwCurFreeCount - 1 <= VOL_LCN(pVol));

		if (dwFreeCluster + dwCurFreeCount == VOL_LCN(pVol))
		{
			dwHint = 2;
		}
		else
		{
			dwHint = dwFreeCluster + dwCurFreeCount;
		}
	}

	if (VC_IS_EMPTY(pVC) == FFAT_FALSE)
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_LC(pVC)) == FFAT_TRUE);
		VI_SET_FCH(VOL_VI(pVol), (VC_LC(pVC) + 1));
	}

	if (dwCount == 0)
	{
		r = FFAT_DONE;
	}
	else
	{
		r = FFAT_ENOSPC;
	}

// debug begin
#ifdef _FCC_STATISTICS
	if (bCacheMiss == FFAT_TRUE)
	{
		_STATISTICS_FCC_MISS
	}
	else
	{
		_STATISTICS_FCC_HIT
	}
#endif
	
#ifdef _FCC_DEBUG
	_checkVC(pVol, pVC);
#endif
// debug end

out:
	if ((r >= 0) ||
		((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE)))
	{
		*pdwFreeCount = VC_CC(pVC);			// update free count
	}

	return r;
}


/**
* get free new clusters in specific range
*
* Ư�� ������ free cluster�� ã���ش�.
* [en] free cluster in specific range is found 
* FCC�ȿ� free cluster�� ����ѵ� Ư�� ����(dwFrom, dwTo)�� free cluster �� ������ �� �ִ�.
* [en] free cluster in FCC is enough, however free cluster in specific range(dwFrom, dwTo) can not be enough
* �� ���, FCC���� ������ �ʰ� FFATFS���� ���Ѵ�.
* [en] in this case, it can be got at FFATFS instead of FCC 
* �� �Լ��� �ݺ��ؼ� ȣ���� �� ������ pVC�ȿ� ������ ���� ���� �� �ڷ� append�Ѵ�.
* [en] this function can be invoked repeatably and in case of information in pVC, it is appended behind
* ���� dwFrom, dwTo�� �ٲ��� �ʰ� �ݺ��ؼ� ȣ���ϸ� ���� ������ free cluster������ ä���� �� ������ �����ؾ� �Ѵ�.
* [en] therefore, if dwTo is not changed and it is invoked repeatably, 
*      pay attention because free cluster information of same content can be filled 
*
* @param		pVol				: [IN] volume info
* @param		dwCount 			: [IN] cluster count to allocate
* @param		pVC					: [IN/OUT] cluster information It can be append
* @param		pdwAllocatedCount	: [OUT] Allocate �� ���� return �Ѵ�.
*									[en] [OUT] the allocating number returns
* @param		dwFlag				: [IN] cluster count in pdwClusters
* @param		pCxt				: [IN] context of current operation
* @return		FFAT_OK 			: FCC is not active state
* @return		FFAT_DONE			: dwCount ��ŭ�� cluster�� �����.
*									[en] cluster is got as many as dwCount 
* @return		FFAT_ENOSPC 		: pVC�� �� �����Ȱų�, dwCount��ŭ ���� ���ߴ�.pdwAllocatedCount�� ��ȯ�Ѵ�.
*									[en] pVC is full or it is not got as many as dwCount. pdwAllocatedCount returns
* @return		else				: error
* @author		DongYoung Seo
* @version		AUG-28-2007 [Soojeong Kim]	First Writing.
* @history		JAN-07-2008 [InHwan Choi]	FCC refactoring
* @history		OCT-12-2008 [DongYoung Seo] use FFATFS only when there is not enough free clusters on the FCC
* @history		OCT-27-2008 [DongYoung Seo] remove current free cluster count checking routine is over than requested count
										to avoid free clusters infomration mis-match between FCC and FFATFS
*/
FFatErr
ffat_fcc_getFreeClustersFromTo(Vol* pVol, t_uint32 dwHint, t_uint32 dwFrom,
								t_uint32 dwTo, t_uint32 dwCount,
								FFatVC* pVC, t_uint32* pdwAllocatedCount, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;
	t_uint32		dwFreeCluster;
	t_uint32		dwFreeCount;
	t_int32			dwCurEntry;
	t_boolean		bGetFFATFS = FFAT_FALSE;
	t_uint32		dwTotalCount;
	t_int32			dwSavedValidIndex;
	t_uint32		dwSavedTotalCount;
	t_uint32		dwSavecLastCluster;
	t_uint32		dwSavedLastCount;
	t_uint32		dwConnectedCluster = 0;
	t_uint32		dwConnectedCount = 0;

// debug begin
#ifdef _FCC_STATISTICS
	t_boolean		bCacheMiss = FFAT_FALSE;
#endif
// debug end

	_STATISTIC_FCC_GETFREECLUSTERSFROMTO

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pVC->pVCE);
	FFAT_ASSERT(pdwAllocatedCount);
	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT(dwTo <= VOL_LCN(pVol));

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// free cluster cache is not activated
		return FFAT_OK;
	}

	*pdwAllocatedCount = 0;

	if(VC_IS_FULL(pVC) == FFAT_TRUE)
	{
		// there is no free entry
		return FFAT_ENOSPC;
	}

	dwFreeCluster = dwHint;
	dwFreeCount = 0;

	dwSavedTotalCount = VC_CC(pVC);
	dwSavedValidIndex = VC_VEC(pVC);

	dwCurEntry = VC_VEC(pVC);

	dwTotalCount = dwCount;

	if (dwCurEntry > 0)
	{
		dwSavecLastCluster = VC_LC(pVC);
		dwSavedLastCount =	pVC->pVCE[VC_LEI(pVC)].dwCount;
		FFAT_ASSERT((dwSavecLastCluster > 0) && (dwSavedLastCount > 0));
	}
	else
	{
		dwSavecLastCluster = 0;
		dwSavedLastCount = 0;
	}

	// get clusters
	while (dwTotalCount > 0)
	{
		r = _getClustersFromTo(pVol, (dwFreeCluster + dwFreeCount), dwFrom, dwTo,
						dwTotalCount, &dwFreeCluster, &dwFreeCount, pCxt);
		if (r < 0)
		{
			// ���ϴ� ������ free cluster�� ���� ���� FFATFS���� ���� ã�ƾ� �Ѵ�.
			// [en] in case of no free cluster in wanted area, it is found itself at FFATFS
			if (r == FFAT_ENOENT)
			{
				bGetFFATFS = FFAT_TRUE;
				break;
			}

			FFAT_ASSERT(0);
			FFAT_DEBUG_PRINTF((_T("Fail to get free cluster from FCC, There is critical problem in FCC algorithm")));
			goto out;
		}

		FFAT_ASSERT(dwFreeCount > 0);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwFreeCluster) == FFAT_TRUE);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), (dwFreeCluster + dwFreeCount - 1)) == FFAT_TRUE);

		if ((dwSavecLastCluster + 1) == dwFreeCluster)
		{
			FFAT_ASSERT(dwCurEntry > 0);
			FFAT_ASSERT((dwCurEntry - 1) == VC_LEI(pVC));
			
			pVC->pVCE[VC_LEI(pVC)].dwCount += dwFreeCount;

			FFAT_ASSERT((dwConnectedCluster == 0) && (dwConnectedCount == 0));
			dwConnectedCluster = dwFreeCluster;
			dwConnectedCount = dwFreeCount;
		}
		else
		{
			pVC->pVCE[dwCurEntry].dwCluster = dwFreeCluster;
			pVC->pVCE[dwCurEntry].dwCount = dwFreeCount;
			VC_VEC(pVC)++;
			dwCurEntry++;
		}

		VC_CC(pVC)			+= dwFreeCount;
		*pdwAllocatedCount	+= dwFreeCount;
		dwTotalCount		-= dwFreeCount;

		// VC is full, return
		if (VC_VEC(pVC) == VC_TEC(pVC))
		{
			break;
		}
		FFAT_ASSERT((VC_VEC(pVC) > 0) && (VC_CC(pVC) > 0));
	}

	if (bGetFFATFS)
	{
		VC_VEC(pVC) = dwSavedValidIndex;
		VC_CC(pVC) -= *pdwAllocatedCount;

		if (dwSavedLastCount > 0)
		{
			FFAT_ASSERT(dwSavedLastCount > 0);
			pVC->pVCE[VC_LEI(pVC)].dwCount = dwSavedLastCount;
		}

		dwFreeCount = 0;

		FFAT_ASSERT(VC_IS_FULL(pVC) == FFAT_FALSE);

		// get free cluster from FFATFS 
		r = FFATFS_GetFreeClustersFromTo(VOL_VI(pVol), dwHint, dwFrom, dwTo, dwCount, 
			pVC, &dwFreeCount, FFAT_FALSE, pCxt);
		IF_UK ((r < 0) && (r != FFAT_ENOSPC))
		{
			FFAT_LOG_PRINTF((_T("Fail to get free cluster from FFATFS.")));
			goto out;
		}
		else if (r == FFAT_OK)
		{
			FFAT_ASSERT(dwCount == dwFreeCount);
		}
		else
		{
			FFAT_ASSERT(r == FFAT_ENOSPC);
		}

		*pdwAllocatedCount = dwFreeCount;
	}

	if (VC_IS_EMPTY(pVC) != FFAT_TRUE)
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_LC(pVC)) == FFAT_TRUE);
		VI_SET_FCH(VOL_VI(pVol), (VC_LC(pVC) + 1));
	}

	FFAT_ASSERT(dwCount >= *pdwAllocatedCount);

	if (dwCount == *pdwAllocatedCount)
	{
		FFAT_ASSERT(dwCount + dwSavedTotalCount == VC_CC(pVC));
		r = FFAT_DONE;
	}
	else IF_LK (dwCount > *pdwAllocatedCount)
	{
		r = FFAT_ENOSPC;
	}
	else
	{
		FFAT_ASSERT(0);
	}

// debug begin
#ifdef _FCC_STATISTICS
	if (bCacheMiss == FFAT_TRUE)
	{
		_STATISTICS_FCC_MISS
	}
	else
	{
		_STATISTICS_FCC_HIT
	}
#endif

#ifdef _FCC_DEBUG
	_checkVC(pVol, pVC);
#endif
// debug end

out:
	return r;
}


/**
* get free cluster count at FAT sector
*
* Ư�� FAT sector�� free cluster�� ����� �˷��ش�.
* [en] announces that the number of free cluster in specific range
* free cluster������ ���鼭 ã�� free cluster�� FCC �� �ִ´�. 
* [en] free cluster to be found puts at FCC, counting the number of free clusters
* FCC�� free entry�� ������ ���� �ʴ´�.
* [en] it does not put if there is no free entry of FCC
*
* @param		pVol			: [IN] volume info
* @param		dwSector		: [IN] FAT sector
* @param		pdwFCC			: [OUT] free cluster count in FAT sector
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: FCC is not active state
* @return		FFAT_DONE		: success
* @return		else			: error
* @author		Soojeong Kim
* @version		AUG-28-2007 [Soojeong Kim]	First Writing.
* @version		OCT-11-2008 [DongYoung Seo] ADD ASSERT to check validity of dwSector
* @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine

*/
FFatErr
ffat_fcc_getFCCOfSector(Vol* pVol, t_uint32 dwSector, t_uint32* pdwFCC, ComCxt* pCxt)
{
	FFatErr			r;
	FatVolInfo*		pVolInfo;
	t_int8*			pBuff;
	t_uint32		dwCluster;			// start cluster number
	t_int32			dwIndex;
	FFatCacheInfo	stCI;				// Cache information for block device I/O
	FCCVolInfo*		pFCC;				// FCC
	EssRBNode2*		pNode;				// a node pointer of RBTree
	_FCCEntry*		pEntry = NULL;		// FCC Entry

	_STATISTIC_FCC_GETFCCOFSECTOR

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwFCC);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(dwSector <= VOL_LVFSFF(pVol));

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	pVolInfo	= VOL_VI(pVol);			// get VolInfo
	pFCC		= _FCC(pVol);			// get FCC

	// find first cluster number in FAT sector
	dwCluster = FFATFS_GET_FIRST_CLUSTER_ON_SECTOR(pVolInfo, dwSector);

	// check the sector is already at the FCC
	pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), dwCluster);
	if (pNode != NULL)
	{
		// there is an entry for this cluster
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);
		if (_IS_INCLUDED(pEntry, dwCluster, VOL_CCPFS(pVol)) == FFAT_TRUE)
		{
			// they are in the FCC and whole FAT Sector is free
			*pdwFCC = VOL_CCPFS(pVol);
			FFAT_DEBUG_FCC_PRINTF((_T("ffat_fcc_getFCCOfSector() pVol/Sector/FreeClusterCount:0x%X/%d/%d\n"), (t_uint32)pVol, dwSector, *pdwFCC));
			return FFAT_DONE;
		}

		// do not care the last FAT sector, it must not be totally free FAT area.
	}

	// Sync for dirty entry : [TODO] instead of syncVol(), implement the syncSector()
	r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
	FFAT_ER(r, (_T("fail to sync FCC")));

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VI_SS(pVolInfo), pCxt);
	FFAT_ASSERT (pBuff != NULL);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	r = FFATFS_ReadWriteSectors(pVolInfo, dwSector, 1, pBuff, FFAT_CACHE_DATA_FAT,
						&stCI, FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to read sector from FFATFS cache")));
		r =  FFAT_EIO;
		goto out;
	}

	if (dwSector == VI_LVFSFF(pVolInfo))
	{
		dwIndex = VI_LCN(pVolInfo) & VI_CCPFS_MASK(pVolInfo);
	}
	else
	{
		FFAT_ASSERT(dwSector <= VI_LVFSFF(pVolInfo));
		dwIndex = VI_CCPFS(pVolInfo) - 1;
	}

	if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		*pdwFCC = _getFreeClusterCount32(dwCluster, pBuff, dwIndex, pVol, pCxt);
	}
	else
	{
		*pdwFCC = _getFreeClusterCount16(dwCluster, pBuff, dwIndex, pVol, pCxt);
	}

	FFAT_DEBUG_FCC_PRINTF((_T("ffat_fcc_getFCCOfSector() pVol/Sector/FreeClusterCount:0x%X/%d/%d\n"), (t_uint32)pVol, dwSector, *pdwFCC));

	r = FFAT_DONE;

out:
	FFAT_LOCAL_FREE(pBuff, VI_SS(pVolInfo), pCxt);

	return r;
}


/**
* get status of a volume
*
* volume�� free cluster������ ���Ѵ�. 
* [en] the number of volume's free cluster can be got
* statfs�� ����Ǹ� ���� �ѹ��� �����Ѵ�.
* [en] in case of statfs, it executed only one time at first
* @param		pVol		: [IN] volume pointer
* @param		pBuff		: [IN] buffer pointer, may be NULL
* @param		dwSize		: [IN] size of buffer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_DONE	: volume status gather operation success
* @return		FFAT_OK 	: just ok, nothing is done.
* @return		else		: error
* @author		DongYoung Seo
* @version		AUG-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fcc_getVolumeStatus(Vol* pVol, t_int8* pBuff, t_int32 dwSize, ComCxt* pCxt)
{
	t_int8* 	pInternalBuff = NULL;
	t_uint32	dwFreeCount;
	FFatErr 	r;

	FFAT_ASSERT(pVol);

	_STATISTIC_FCC_GETVOLUMESTATUS

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (VOL_IS_VALID_FCC(pVol) == FFAT_TRUE)
	{
		return FFAT_DONE;
	}

	if ((FFATFS_STATFS_BUFF_SIZE > dwSize) || (pBuff == NULL))
	{
		pInternalBuff = (t_int8*)FFAT_LOCAL_ALLOC(FFATFS_STATFS_BUFF_SIZE, pCxt);
		FFAT_ASSERT (pInternalBuff != NULL);

		pBuff		= pInternalBuff;
		dwSize		= FFATFS_STATFS_BUFF_SIZE;
	}

	// sync FCC dirty information before update status operation
	r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to sync dirty Entry")));

	// sync volume before update status operation
	r = FFATFS_SyncVol(VOL_VI(pVol), FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync volume")));

	dwFreeCount = 0;
	r = _getFreeClusterCount(pVol, pBuff, dwSize, &dwFreeCount, pCxt);
	FFAT_EO(r, (_T("fail to get free count")));

	VOL_FCC(pVol)	= dwFreeCount;
	r = FFAT_DONE;

out:
	FFAT_LOCAL_FREE(pInternalBuff, FFATFS_STATFS_BUFF_SIZE, pCxt);

	return r;
}


/**
* add free clusters to FCC
*
* FCC��cluster������ �߰��Ѵ�.
* [en] cluster information is added at FCC
*
* @param		pVol			: [IN] Volume pointer
* @param		dwCluster		: [IN] cluster number
* @param		dwCount			: [IN] cluster count
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: add free clusters to FCC
* @return		FFAT_ENOENT		: FCC is full, It's OK
* @return		else			: fail
* @author		InHwan Choi
* @version		JAN-20-2008 [InHwan Choi]	First Writing.
* @version		FEB-04-2009 [JeongWoo Park] Change return value from FFAT_DONE -> FFAT_OK.
*											FFAT_DONE is unnecessary return value.
*/
FFatErr
ffat_fcc_addFreeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	FFatErr 			r;

	_STATISTIC_FCC_ADDFREECLUSTERS

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{ 
		return FFAT_OK;
	}

	r = _addClusters(pVol, dwCluster, dwCount, FFAT_FALSE, FFAT_FALSE, pCxt);
	IF_UK ((r < 0) && (r != FFAT_ENOENT))
	{
		return r;
	}

	return FFAT_OK;
}


/**
 * add free clusters to FCC
 *
 * FCC�� vectored ������ cluster������ �߰��Ѵ�.
 * [en] cluster information of vectored type is added at FCC
 * get free cluster �� make cluster chain ���� error �߻��� ��� (FCC�� full�ϼ� ����)
 * [en] it is used if error occur, after getting free cluster or before making cluster chain.
 *      ( FCC is not possible to full status. ) 
 *
 * @param		pVol			: [IN] Volume pointer
 * @param		pVC				: [IN] cluster storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: add free clusters to FCC
 * @return		else			: fail
 * @author		GwangOk Go
 * @version		OCT-09-2008 [GwangOk Go]	First Writing.
 */
FFatErr
ffat_fcc_addFreeClustersVC(Vol* pVol, FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr 		r;
	t_int32 		dwIndex;

	_STATISTIC_FCC_ADDFREECLUSTERSVC

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pVC->dwValidEntryCount > 0);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

// debug begin
#ifdef _FCC_DEBUG
	_checkVC(pVol, pVC);
#endif
// debug end

	FFAT_ASSERT(VC_VEC(pVC) > 0);

	for (dwIndex = 0; dwIndex < VC_VEC(pVC); dwIndex++)
	{
		FFAT_ASSERT(pVC->pVCE[dwIndex].dwCount > 0);

		// dirty ���θ� �˼� ������ ������ dirty list�� �߰� ��Ŵ
		// [en] it is added unconditionally at dirty list, because it does not know whether is dirty or not 
		r = _addClusters(pVol, pVC->pVCE[dwIndex].dwCluster, pVC->pVCE[dwIndex].dwCount,
						FFAT_TRUE, FFAT_TRUE, pCxt);
		FFAT_ASSERT(r != FFAT_ENOENT);
		FFAT_ER(r, (_T("fail to add Cluster to FCC")));
	}

	return FFAT_OK;
}


// debug begin
#ifdef FFAT_DEBUG
	/**
	* add free cluster of a part of volume
	*
	* debug�뵵�� ���ȴ�. Ư�� ������ free cluster�� FCC�� �ִ´�.
	* [en] it is used by debug, free cluster in specific area puts at FCC
	*
	* @param		pVol			: [IN] volume pointer
	* @param		dwStartVolume	: [IN] start volume range (%)
	* @param		dwEndVolume 	: [IN] end volume range (%)
	* @param		pCxt			: [IN] context of current operation
	* @return		FFAT_OK 		: success
	* @return		else			: error
	* @author		Soojeong Kim
	* @version		SEP-04-2006 [Soojeong Kim]	First Writing.
	* @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
	*/
	FFatErr
	ffat_fcc_addFreeClustersFromTo(Vol* pVol, t_uint32 dwStartVolume, t_uint32 dwEndVolume, ComCxt* pCxt)
	{
		FFatErr 	r;
		t_uint32	dwStartFatSector;
		t_uint32	dwEndFatSector;
		t_uint32	dwTotalFatSector;
		t_uint32	dwFCC;
		t_uint32	i;

		_STATISTIC_FCC_ADDFREECLUSTERSFROMTO

		FFAT_ASSERT (pVol != NULL);

		if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
		{
			return FFAT_OK;
		}

		if ((dwStartVolume >= dwEndVolume) || (dwStartVolume >= 100))
		{
			FFAT_DEBUG_PRINTF((_T("Wrong Range, nothiNg to do\n")));
			return FFAT_OK;
		}

		dwTotalFatSector	= VOL_LVFSFF(pVol) - VOL_FFS(pVol) + 1;

		dwStartFatSector = VOL_FFS(pVol) + (dwTotalFatSector * dwStartVolume / 100);		// WHY USE DIVIDER ??
		dwEndFatSector = VOL_FFS(pVol) + (dwTotalFatSector * dwEndVolume / 100);			// WHY USE DIVIDER ??

		if (dwEndFatSector > VOL_LVFSFF(pVol))
		{
			dwEndFatSector = VOL_LVFSFF(pVol);
		}

		for (i = dwEndFatSector ; i >= dwStartFatSector ; i++)
		{
			r =  ffat_fcc_getFCCOfSector(pVol, i, &dwFCC, pCxt);
			if (r < 0)
			{
				IF_UK (r != FFAT_ENOENT)
				{
					FFAT_DEBUG_PRINTF((_T("fail to add FC to FCC\n")));
					return r;
				}
			}
		}

		return FFAT_OK;
	}
#endif
// debug end

//=============================================================================
//
//	Static Functions
//


/**
* get free cluster from FCC
*
* FCC���� dwHint������ free cluster�� ã�� ��ȯ�Ѵ�. 
* [en] free cluster finds and returns after checking dwHint at FCC
* ����� free cluster�� �ִٰ� �����Ѵ�. �ݵ�� �����ؾ� �Ѵ�.
* [en] assume that there is enough free cluster. it must success
*
* @param		pVol				: [IN] volume pointer
* @param		dwHint				: [IN] cluster number to get cluster
* @param		dwCount 			: [IN] cluster count
* @param		pdwFreeCluster		: [OUT] free cluster number storage
* @param		pdwFreeCount		: [OUT] cluster count storage
* @param		pCxt				: [IN] context of current operation
* @return		NULL				: there is no free entry 
* @return		else				: valid entry pointer
* @author		DongYoung Seo
* @version		NOV-02-2006 [DongYoung Seo] First Writing.
* @history		AUG-27-2007 [Soojeong Kim]	Second Writing.
* @history		JAN-02-2008 [InHwan Choi]	FCC refactoring.
*/
static FFatErr
_getClusters(Vol* pVol, t_uint32 dwHint, t_uint32 dwCount, t_uint32* pdwFreeCluster,
			 t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	FCCVolInfo* 	pFCC;
	EssRBNode2* 	pNode;
	_FCCEntry*		pEntry;
	FFatErr 		r;

	_STATISTIC_FCC_GETCLUSTERS

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwFreeCluster);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);

	pFCC = _FCC(pVol);
	pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwHint);
	if (pNode == NULL)
	{
		pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), 0);
		if (pNode == NULL)
		{
			FFAT_ASSERT(EssRBTree2_IsEmpty(_FCC_TC(pFCC)) == FFAT_TRUE);
			return FFAT_ENOENT;
		}
		else
		{
			pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);
		}
	}
	else
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);
	}

	*pdwFreeCluster = _FCCE_CLUSTER(pEntry);

	FFAT_ASSERT(pEntry->pVol == pVol);

	// ���ϴ� ũ�⺸�� Ŭ ��� dwCount �̿��� �κ��� ���ܵд�.
	// [en] in case size is bigger than wanted size, the part except for dwCount is leaved
	if (_FCCE_COUNT(pEntry) > dwCount)
	{
		r = _updateEntry(pVol, pEntry, (_FCCE_CLUSTER(pEntry) + dwCount), 
					(_FCCE_COUNT(pEntry) - dwCount), FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));

		*pdwFreeCount = dwCount;
	}
	else  /* (_FCCE_COUNT(pEntry) <= dwCount) */
	{
		r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));

		*pdwFreeCount = _FCCE_COUNT(pEntry);
	}

	return FFAT_OK;
}


/**
* get free cluster from FCC
*
* entry�� ã�� ���
* [en] how to find entry 
* 1. dwHint�� ���ӵǸ鼭 dwCount�� �����ϴ� entry
* [en] 1. entry is satisfied dwCount continuing with dwHint
* 2. dwFrom +dwCount�� �����ϰų� ��ġ�� entry
* [en] 2. 
* 3. dwFrom ~ dwTo ���̿� �ִ� entry
* [en] 3. 
*
* @param		pVol				: [IN] volume pointer
* @param		dwHint				: [IN] cluster number to get cluster
* @param		dwFrom				: [IN] cluster range 
* @param		dwTo				: [IN] cluster range
* @param		dwCount 			: [IN] cluster count
* @param		dwFreeClsuter		: [OUT]gotten free cluster number
* @param		dwFreeClunt 		: [OUT]gotten free cluster count
* @param		pCxt				: [IN] context of current operation
* @return		FFAT_OK 			: success
* @return		FFAT_ENOENT 		: There is no valid cluster in input range
* @return		else				: fail
* @author		InHwan Choi
* @version		JAN-02-2008 [InHwan Choi]	First Writing.
* @version		JAN-02-2008 [DongYoung Seo] remove pbDirty parameter
*/
static FFatErr
_getClustersFromTo(Vol* pVol, t_uint32 dwHint, t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount, 
						 t_uint32* pdwFreeCluster, t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	FCCVolInfo* 	pFCC;
	EssRBNode2* 	pNode;
	_FCCEntry*		pEntry;
	FFatErr 		r;

	_STATISTIC_FCC_GETCLUSTERSFROMTO

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwFreeCluster);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(dwFrom <= dwTo);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwHint) == FFAT_TRUE);

	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);

	pFCC = _FCC(pVol);

	// dwHint�� ��ȿ���� ������ dwFrom���� ã�´�.
	// [en] 
	if ((dwHint < dwFrom) || (dwHint > dwTo))
	{
		dwHint = dwFrom;
	}

	if (dwCount > (dwTo - dwFrom + 1))
	{
		dwCount = dwTo - dwFrom + 1;
	}

	// 1st : Hint ���� ū pEntry�� ã�´�.
	// [en] 
	pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwHint);	
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		FFAT_ASSERT(_FCCE_CLUSTER(pEntry) >=  dwHint);

		// dwFrom ~ dwTo ���� �ȿ��� dwCount���� ũ�ų� ���� �� Ȯ���Ѵ�.
		// [en] 
		if (((_FCCE_CLUSTER(pEntry) + dwCount - 1) <= dwTo) && (_FCCE_COUNT(pEntry) >= dwCount))
		{
			// found !!, best case
			*pdwFreeCluster = _FCCE_CLUSTER(pEntry);
			*pdwFreeCount = dwCount;
			goto found;
		}
	}

	// 2nd : dwFrom���� + dwCount �� �����ϰų� ���� �ִ� pEntry�� ã�´�.
	// [en] 
	pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), dwFrom);
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		// dwFrom + dwCount�� ������ �����ϴ°�?
		// [en] 
		if(_IS_INCLUDED(pEntry, dwFrom, dwCount))
		{
			*pdwFreeCluster = dwFrom;
			*pdwFreeCount = dwCount;
			goto found;
		}
		else if(_IS_LEFT_OVERLAP(pEntry, dwFrom, dwCount))
		{
			FFAT_ASSERT((_FCCE_LAST_CLUSTER(pEntry) + 1) > dwFrom);
			*pdwFreeCluster = dwFrom;
			*pdwFreeCount = (_FCCE_LAST_CLUSTER(pEntry) + 1 - dwFrom);
			goto found;
		}
	}
	
	// 3rd : dwFrom���� dwTo ������ �ִ� pEntry �� ã�´�.
	// [en] 
	pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwFrom);
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		if (_FCCE_CLUSTER(pEntry) <= dwTo)
		{
			*pdwFreeCluster = _FCCE_CLUSTER(pEntry);

			if(_FCCE_LAST_CLUSTER(pEntry) <= dwTo)
			{
				if((_FCCE_CLUSTER(pEntry) + dwCount - 1) <= dwTo)
				{
					*pdwFreeCount = (_FCCE_COUNT(pEntry) > dwCount) ? dwCount : _FCCE_COUNT(pEntry);	
					goto found;
				}
				else
				{
					FFAT_ASSERT((dwTo + 1) > _FCCE_CLUSTER(pEntry));
					*pdwFreeCount = _FCCE_COUNT(pEntry);
					goto found;
				}
			}
			else
			{
				if((_FCCE_CLUSTER(pEntry) + dwCount -1) <= dwTo)
				{
					*pdwFreeCount = dwCount;
					goto found;
				}
				else
				{
					FFAT_ASSERT((dwTo + 1) > _FCCE_CLUSTER(pEntry));
					*pdwFreeCount = (dwTo + 1 - _FCCE_CLUSTER(pEntry));
					goto found;
				}
			}
		}
	}

	return FFAT_ENOENT;

found:

	FFAT_ASSERT((dwFrom <= *pdwFreeCluster) && (dwTo >= (*pdwFreeCluster + *pdwFreeCount - 1)));
	FFAT_ASSERT(*pdwFreeCount <= _FCCE_COUNT(pEntry));

	// ã�� entry���� ���� cluster ������ �����Ѵ�.
	// [en] 
	r = _removeEntry(pVol, pEntry, *pdwFreeCluster, *pdwFreeCount, pCxt);
	FFAT_ER(r, (_T("fail to remove form FCCE")));

	FFAT_ASSERT(*pdwFreeCount > 0);

// debug begin
#ifdef _FCC_DEBUG
	_checkTree(pVol);
#endif
// debug end

	return r;
}


/**
* Add free clusters to FCC
*
* FCC�� VC������ cluster������ �߰��Ѵ�.
* // [en] 
* FCC�� free entry�� �������� ������ �߰����� �ʰ� deallocate �Ѵ�.
* // [en] 
* add cluster�� deallocate�� backward�� �Ѵ�.
* // [en] 
*
* @param		pVol			: [IN] Volume pointer
* @param		pVC				: [IN] cluster information
*										storage for free clusters 
*										may be NULL.
* @param		dwCacheFlag		: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo
* @version		NOV-02-2006 [DongYoung Seo] First Writing.
* @history		DEC-28-2007 [InHwan Choi]	FCC refactoring.
* @history		SEP-02-2008 [DongYoung Seo] add clusters after deallocation.
* @history		SEP-02-2008 [DongYoung Seo] add checking routine for dirty and force
*/
FFatErr
_addAndDeallocateClustersVC(Vol* pVol, FFatVC* pVC, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	t_int32			i;
	t_uint32		dwCluster;			// cluster number to be stored
	t_uint32		dwCount;			// cluster count
	t_uint32		dwTotal;
	t_uint32		dwDeallocatedCount;
	FatAllocateFlag	dwFAFlag = FAT_ALLOCATE_NONE;
	t_uint32		dwSector;			// sector number
	FFatErr			r;
	t_boolean		bDirty;				// dirty flag
	t_boolean		bForce;				// force flag

	_STATISTIC_FCC_ADDANDDEALLOCATECLUSTERVC

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(_IS_ACTIVATED(pVol));

	dwTotal = VC_CC(pVC);

	if (dwCacheFlag & FFAT_CACHE_DATA_DE)
	{
		dwFAFlag |= FAT_DEALLOCATE_DISCARD_CACHE;
	}

	// set force flag
	if (dwCacheFlag & FFAT_CACHE_FORCE)
	{
		bForce = FFAT_TRUE;
	}
	else
	{
		bForce = FFAT_FALSE;
	}

	// set dirty flag
	if (dwCacheFlag & FFAT_CACHE_SYNC)
	{
		bDirty = FFAT_FALSE;
	}
	else
	{
		bDirty = FFAT_TRUE;
	}

	for (i = VC_VEC(pVC); i > 0; i--)
	{
		dwCluster	= pVC->pVCE[i - 1].dwCluster;
		dwCount		= pVC->pVCE[i - 1].dwCount;

		FFAT_ASSERT((dwCount > 0) && (dwCluster > 0));

		r = _addClusters(pVol, dwCluster, dwCount, bDirty, bForce, pCxt);
		if (r == FFAT_ENOENT)
		{
			dwCacheFlag |= FFAT_CACHE_SYNC;
			bDirty = FFAT_FALSE;
		}
		else if ((dwCacheFlag & FFAT_CACHE_FORCE) == 0)
		{
			FFAT_ER(r, (_T("fail to add free cluster")));
		}

		if (dwCacheFlag & (FFAT_CACHE_SYNC | FFAT_CACHE_DIRECT_IO))
		{
			r = FFATFS_DeallocateClusterFromTo(VOL_VI(pVol), 0, dwCluster, dwCount,
												&dwDeallocatedCount, dwFAFlag,
												(dwCacheFlag | FFAT_CACHE_SYNC), NULL, pCxt);
			if (r < 0)
			{
				FFAT_DEBUG_PRINTF((_T("fail to add cluster")));

				// remove added clusters
				_removeClusters(pVol, dwCluster, dwCount, pCxt);

				if ((dwCacheFlag & FFAT_CACHE_FORCE) == 0)
				{
					return r;
				}
			}
			FFAT_ASSERT(dwCount == dwDeallocatedCount);
		}

		if (dwFAFlag & FAT_DEALLOCATE_DISCARD_CACHE)
		{
			// Quiz. why discard cached sector ?
			dwSector = FFATFS_GET_SECTOR_OF_CLUSTER(VOL_VI(pVol), dwCluster, 0);
			r = FFATFS_DiscardCache(VOL_VI(pVol), dwSector, (dwCount << VOL_SPCB(pVol)), pCxt);
			if (r < 0)
			{
				FFAT_DEBUG_PRINTF((_T("fail to discard FFATFS Cache")));
				if ((dwCacheFlag & FFAT_CACHE_FORCE) == 0)
				{
					return r;
				}
			}
		}

		dwTotal -= dwCount;
	}

	FFAT_ASSERT(dwTotal == 0);

	return FFAT_DONE;
}


/**
* add clusters to FCC
*
* FCC �� free cluster �� �߰��Ѵ�.
* FCC�� free entry�� �������� bForce�� ���� ������ free cluster�� �߰����� ������ �����Ѵ�.
* 
* @param		pVol			: [IN] volume pointer
* @param		dwCluster		: [IN] free cluster number
* @param		dwCount			: [IN] contiguous cluster count
* @param		bDirty			: [IN] set FCC dirty flag
* @param		bForce			: [IN] get entry by force option
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		FFAT_ENOENT		: there is no more free entry.
* @return		negative		: fail
* @author		DongYoung Seo
* @version		NOV-02-2006 [DongYoung Seo] First Writing.
* @history		JAN-02-2008 [InHwan Choi] FCC refactoring
*/
static FFatErr
_addClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, t_boolean bDirty,
				t_boolean bForce, ComCxt* pCxt)
{
	FCCVolInfo*		pFCC;
	_FCCEntry*		pAddEntry = NULL;
	_FCCEntry*		pEntry = NULL;
	EssRBNode2*		pNode;
	FFatErr 		r;
	t_uint32		dwClusterTemp;
	t_uint32		dwCountTemp;

	_STATISTIC_FCC_ADDCLUSTERS

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwCount > 0);

	pFCC = _FCC(pVol);

	FFAT_DEBUG_FCC_PRINTF((_T("_addClusters() dwCluster/dwCount/bDirty:%d/%d/%d\n"), dwCluster, dwCount, bDirty));

	// 1st step
	// �߰��Ϸ��� cluster������ ��ġ�ϰų� cluster������ �����ϴ� entry�� ã�´�.
	pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), dwCluster);
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		// ��ġ�ϰų� �����Ҷ��� �̹� FCC�� �����Ƿ� �߰����� �ʴ´�.
		if (_IS_EQUAL(pEntry, dwCluster, dwCount))
		{
			// do nothing
			// check dirty/clean
			FFAT_ASSERT((bDirty == FFAT_TRUE) ? (_FCCE_IS_DIRTY(pEntry) == FFAT_TRUE) : (_FCCE_IS_DIRTY(pEntry) == FFAT_FALSE));
			return FFAT_OK;
		}
		else if(_IS_INCLUDED(pEntry, dwCluster, dwCount))
		{
			FFAT_ASSERT(dwCluster >= _FCCE_CLUSTER(pEntry));
			FFAT_ASSERT((dwCluster + dwCount - 1) <= _FCCE_LAST_CLUSTER(pEntry));
			FFAT_ASSERT((bDirty == FFAT_TRUE) ? (_FCCE_IS_DIRTY(pEntry) == FFAT_TRUE) : (_FCCE_IS_DIRTY(pEntry) == FFAT_FALSE));
			return FFAT_OK;
		}
	}

	// 2nd step
	// make new FCC entry
	// �߰��Ϸ��� cluster������ ��ġ�ϰų� cluster�� �����ϴ� entry�� �����Ƿ� ���ο� entry�� FCC�� �߰��ؾ� �Ѵ�.
	// free entry�� �ϳ� �����´�. ���� valid ������ �ʴ�.
	r = _getFreeEntry(pVol, &pAddEntry, bForce, dwCount, pCxt);
	FFAT_ER(r, (_T("Fail to get free entry from FCC")));

	// 3rd step
	// remove cluster with new cluster information
	// ���� ���� �߰��� cluster ������ ��ġ�ų� ���ԵǴ� entry�� ������ �����Ѵ�.
	// ����! 2nd step �� 3rd step�� ��ġ�� �ٲٸ� �ȵȴ�.
	// bForce�� FFAT_FALSE �϶� _getFreeEntry�� ������ �� �ִ�.
	r = _removeClusters(pVol, dwCluster, dwCount, pCxt);
	FFAT_ER(r, (_T("Fail to remove cluster")));

	FFAT_ASSERT(pAddEntry->pVol == pVol);

	// 4th step
	// add New entry
	// ���� �߰��� cluster ������ FCC�� �߰��Ѵ�. ��ġ�� entry�� 3rd step���� �����Ǿ���.
	r = _addEntry(pVol, pAddEntry, dwCluster, dwCount, pCxt);
	FFAT_ER(r, (_T("Fail to add Entry to RBtree")));

	if (bDirty)
	{
		ESS_DLIST_MOVE_HEAD(_FCC_LD(pFCC), _FCCE_LD(pAddEntry));
		FFAT_DEBUG_FCC_PRINTF((_T("_addClusters() set entry dirty, entry/cluster/count:0x%X/%d/%d\n"), (t_uint32)pAddEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)));
	}

	// 5th step 
	// search and connect left entry
	// ���� ���� �߰��� cluster ������ ������ entry�� cluster�� ���ӵȴٸ� �̾��� �ʿ䰡 �ִ�.
	pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), _FCCE_CLUSTER(pAddEntry) - 1); 
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		// �߰��� cluster ���� ���� cluster�� �����ΰ�?
		if (_IS_LEFT_LIKED(pEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)))
		{
			FFAT_ASSERT(_IS_OVERLAP(pEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)) == FFAT_FALSE);
			FFAT_ASSERT(_IS_INCLUDED(pEntry,  _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)) == FFAT_FALSE);

			dwClusterTemp	= _FCCE_CLUSTER(pEntry);
			dwCountTemp		= _FCCE_COUNT(pEntry);

			FFAT_ASSERT(pEntry->pVol == pVol);

			// delete pEntry
			// ã�� entry�� �����Ѵ�.
			r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, FFAT_FALSE, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));

			FFAT_ASSERT(dwClusterTemp + dwCountTemp == _FCCE_CLUSTER(pAddEntry));

			// expand pAddEntry
			// �߰��� cluster ������ ���� entry�� �߰��Ѵ�.
			r = _updateEntry(pVol, pAddEntry, dwClusterTemp, (_FCCE_COUNT(pAddEntry) + dwCountTemp), FFAT_FALSE, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));
		}
	}

	// search and connect right entry
	pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), (_FCCE_CLUSTER(pAddEntry) + _FCCE_COUNT(pAddEntry)));
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		if (_IS_RIGHT_LIKED(pEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)))
		{
			FFAT_ASSERT(_IS_OVERLAP(pEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)) == FFAT_FALSE);
			FFAT_ASSERT(_IS_INCLUDED(pEntry, _FCCE_CLUSTER(pAddEntry), _FCCE_COUNT(pAddEntry)) == FFAT_FALSE);

			dwClusterTemp = _FCCE_CLUSTER(pEntry);
			dwCountTemp = _FCCE_COUNT(pEntry);

			FFAT_ASSERT(pEntry->pVol == pVol);
			
			// delete pEntry
			r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, FFAT_FALSE, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));

			FFAT_ASSERT((_FCCE_CLUSTER(pAddEntry) + _FCCE_COUNT(pAddEntry)) == dwClusterTemp);

			// expand pAddEntry
			r = _updateEntry(pVol, pAddEntry, _FCCE_CLUSTER(pAddEntry),
						(_FCCE_COUNT(pAddEntry) + dwCountTemp), FFAT_FALSE, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));
		}
	}

// debug begin
#ifdef _FCC_DEBUG
	_checkTree(pVol);
	_addConfirm(pVol, dwCluster, dwCount);
#endif
// debug end

	return FFAT_OK;
}


/**
* get a free FCC entry 
*
* FCC�� ���� free entry�� �����´�.
* free entry�� �������� bForce�� ���ο� ���� old entry�� sync��Ų�� free entry�� �����.
* free �� ã�� �����ϱ⸸ �Ѵ�. valid entry�� �������� �ʴ´�.
*
* @param		pVol			: [IN] volume pointer
* @param		ppFreeEntry 	: [OUT] free entry pointer
* @param		bForce			: [IN] get free entry even though there is no free entry 
* @param		dwCount			: [IN] count for new entry,
*										to add entry that has may clusters
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_ENOENT 	: there is no free entry
* @return		else			: valid entry pointer
* @author		DongYoung Seo
* @version		NOV-02-2006 [DongYoung Seo] First Writing.
* @history		AUG-27-2007 [Soojeong Kim] Second Writing.
* @history		JAN-02-2008 [InHwan Choi] FCC refactoring
*/
static FFatErr
_getFreeEntry(Vol* pVol, _FCCEntry** ppFreeEntry, t_boolean bForce,
				t_uint32 dwCount, ComCxt* pCxt)
{
	_FCCEntry*		pEntry;
	EssDList*		pDList;
	FFatErr			r;

	_STATISTIC_FCC_GETFREEENTRY

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppFreeEntry);

	// is there free RBNODE?
	// free entry�� ����?
	if (_IS_FULL(_pFCCMain) == FFAT_TRUE)
	{
		// get oldest node
		pDList = ESS_DLIST_GET_TAIL(_FCCMAIN_LV(_pFCCMain));
		pEntry = ESS_GET_ENTRY(pDList, _FCCEntry, dlList);

		FFAT_ASSERT(pEntry);
		FFAT_ASSERT(pEntry->pVol != NULL);

		if ((bForce == FFAT_FALSE) && (_FCCE_COUNT(pEntry) > dwCount))
		{
			// there is no more smallest entry
			return FFAT_ENOENT;
		}

		// FCC���� �����Ѵ�.
		r = _updateEntry(pEntry->pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));

		_STATISTICS_FCC_REPLACE
	}

	// get free node from FCC main
	pDList = ESS_DLIST_GET_TAIL(_FCCMAIN_LF(_pFCCMain));
	pEntry = ESS_GET_ENTRY(pDList, _FCCEntry, dlList);

	FFAT_ASSERT(_FCCE_IS_DIRTY(pEntry) == FFAT_FALSE);

	pEntry->pVol = pVol;

	*ppFreeEntry = pEntry;

	return FFAT_OK;
}


/**
* remove free cluster entry 
*
* FCC���� cluster������ �����Ѵ�. cluster�� deallocate�Ҷ� ȣ��ȴ�.
* 
* @param		pVol			: [IN] start Cluster number
* @param		dwCluster		: [IN] cluster number
* @param		dwCount			: [IN] cluster count
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: mission complete
* @return		else			: error
* @author		InHwan Choi
* @version		JAN-04-2008 [InHwan Choi] First Writing.
* @version		OCT-28-2008 [DongYoung Seo] add cluster count checking code before tree search
*/
static FFatErr
_removeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	FCCVolInfo*		pFCC;
	_FCCEntry*		pEntry;
	EssRBNode2*		pNode;
	t_uint32		dwHint;
	FFatErr			r;

	_STATISTIC_FCC_REMOVECLUSTERS

	pFCC = _FCC(pVol);

	FFAT_DEBUG_FCC_PRINTF((_T("_removeClusters() dwCluster/dwCount:%d/%d\n"), dwCluster, dwCount));

	if (pFCC->dwFreeClusterCount == 0)
	{
		// nothing to do
		return FFAT_OK;
	}

	// 1st step
	// remove overlapping entry at input range
	// cluster������ ��ġ�ϰų� ��ġ�� entry�� ã�´�.
	pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), dwCluster);
	if (pNode != NULL)
	{
		pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

		FFAT_ASSERT(_IS_RIGHT_OVERLAP(pEntry, dwCluster, dwCount) == FFAT_FALSE);

		// cluster ������ ã�� entry�� ��ġ�ϰų�, ������ ���ԵǴ� ���� �����ϰ� return�Ѵ�.
		if (_IS_INCLUDED(pEntry, dwCluster, dwCount))
		{
			r = _removeEntry(pVol, pEntry, dwCluster, dwCount, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));

			goto out;
		}
		// ��ġ�� ���� ��ġ�� �κи� �����Ѵ�.
		else if (_IS_LEFT_OVERLAP(pEntry, dwCluster, dwCount))
		{
			FFAT_ASSERT(dwCluster > _FCCE_CLUSTER(pEntry));

			// remove pEntry
			r = _removeEntry(pVol, pEntry, dwCluster, dwCount, pCxt);
			FFAT_ER(r, (_T("Fail to update FCC entry.")));
		}
	}

	dwHint = dwCluster;

	// 2nd step
	// remove including entry with input range
	// cluster ���� ���� �ȿ� �ִ� entry���� ã�� �����Ѵ�.
	do
	{
		pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwHint);
		if (pNode != NULL)
		{
			pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

			FFAT_ASSERT(_IS_EQUAL(pEntry, dwCluster, dwCount) == FFAT_FALSE);
			FFAT_ASSERT(_IS_INCLUDED(pEntry, dwCluster, dwCount) == FFAT_FALSE);
			FFAT_ASSERT(_IS_LEFT_OVERLAP(pEntry, dwCluster, dwCount) == FFAT_FALSE);

			// cluster ������ ���Եǰų� ��ġ�� entry�� �����Ѵ�.
			if (_IS_RIGHT_OVERLAP(pEntry, dwCluster, dwCount) || _IS_INCLUDING(pEntry, dwCluster, dwCount))
			{
				// remove pEntry
				r = _removeEntry(pVol, pEntry, dwCluster, dwCount, pCxt);
				FFAT_ER(r, (_T("Fail to update FCC entry.")));

				dwHint++;
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
	} while(dwHint < (dwCluster + dwCount));

out :

// debug begin
#ifdef _FCC_DEBUG
	_checkTree(pVol);
	_removeConfirm(pVol, dwCluster, dwCount);
#endif
// debug end

	return FFAT_OK;
}


/**
* remove cluster from FCC
*
* pEntry���� cluster������ ��ġ�� �κ��� �����Ѵ�.
* ���� pEntry�� cluster������ ������ ���Եȴٸ�(case 4) pEntry�� cluster���� ������ ���� �ΰ��� entry�� ������ �ȴ�.
* pEntry�� cluster������ ��ġ�ϴ� ���� case 3���� ó���ȴ�.
* 
* case 1 :		|-------------| -> dwCluster ~ dwCount
*			|------------|		-> pEntry
*
* case 2 :	|-------------| 	-> dwCluster ~ dwCount
*				|------------|	-> pEntry
*
* case 3 :	|-------------| 	-> dwCluster ~ dwCount
*			   |-------|		-> pEntry
*
* case 4 :	   |-------|		-> dwCluster ~ dwCount
*			|-------------| 	-> pEntry
*
* @param		pVol			: [IN] volume pointer
* @param		pEntry			: [IN] target entry
* @param		dwCluster		: [IN] cluster number for remove
* @param		dwCount 		: [IN] cluster count for remove
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: success
* @return		else			: error
* @author		InHwan Choi
* @version		DEC-29-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_removeEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32 dwCluster,
				t_uint32 dwCount, ComCxt* pCxt)
{
	FCCVolInfo*		pFCC;
	_FCCEntry*		pNewEntry;
	FFatErr 		r;
	t_uint32		dwCountTemp;

	_STATISTIC_FCC_REMOVEENTRY

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwCount > 0);	
	FFAT_ASSERT(_IS_OVERLAP(pEntry, dwCluster, dwCount) || _IS_INCLUDED(pEntry, dwCluster, dwCount) || _IS_INCLUDING(pEntry, dwCluster, dwCount));

	pFCC = _FCC(pVol);

	FFAT_ASSERT(pEntry->pVol == pVol);

	FFAT_ASSERT(pFCC == _FCC(pEntry->pVol));

	FFAT_DEBUG_FCC_PRINTF((_T("_removeEntry(0x%X) dwCluster/dwCount:%d/%d\n"), (t_uint32)pEntry, dwCluster, dwCount));

	// case 1 :
	if (_IS_LEFT_OVERLAP(pEntry, dwCluster, dwCount))
	{
		FFAT_ASSERT(dwCluster > _FCCE_CLUSTER(pEntry));
		r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), (dwCluster - _FCCE_CLUSTER(pEntry)), FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));
	}

	// case 2 :
	else if (_IS_RIGHT_OVERLAP(pEntry, dwCluster, dwCount))
	{
		FFAT_ASSERT(_FCCE_LAST_CLUSTER(pEntry) > (dwCluster + dwCount - 1));
		r = _updateEntry(pVol, pEntry, (dwCluster + dwCount),  
						(_FCCE_LAST_CLUSTER(pEntry) - (dwCluster + dwCount - 1)), FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));
	}

	// case 3 :
	else if (_IS_INCLUDING(pEntry, dwCluster, dwCount))
	{
		// �׳� �����Ѵ�, ��ġ�ϴ� ��쵵 ��������.
		r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry), 0, FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));
	}

	// case 4 :
	else if (_IS_INCLUDED(pEntry, dwCluster, dwCount))
	{
		dwCountTemp = _FCCE_LAST_CLUSTER(pEntry);
			
		// �Էµ� cluster���� ���� entry�� udpate�Ѵ�.
		FFAT_ASSERT(dwCluster >= _FCCE_CLUSTER(pEntry));
		r = _updateEntry(pVol, pEntry, _FCCE_CLUSTER(pEntry),
						(dwCluster - _FCCE_CLUSTER(pEntry)), FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("Fail to update FCC entry.")));

		// add New entry
		FFAT_ASSERT(dwCountTemp >= (dwCluster + dwCount - 1));

		if (dwCountTemp > (dwCluster + dwCount - 1))
		{
			// get new count for new entry.
			dwCountTemp = (dwCountTemp - (dwCluster + dwCount - 1));

			// �Էµ� cluster���� ���� entry�� ���� ������ �Ѵ�.
			// make new FCC entry
			r = _getFreeEntry(pVol, &pNewEntry, FFAT_TRUE, dwCountTemp, pCxt);
			FFAT_ER(r, (_T("Fail to get free Entry from FCC")));

			r = _addEntry(pVol, pNewEntry, (dwCluster + dwCount), dwCountTemp, pCxt);
			FFAT_ER(r, (_T("Fail to add Entry to RBtree")));

			if (_FCCE_IS_DIRTY(pEntry) == FFAT_TRUE)
			{
				ESS_DLIST_MOVE_HEAD(_FCC_LD(pFCC), _FCCE_LD(pNewEntry));
			}
		}
	}
	else
	{
		// never reach here!
		FFAT_ASSERT(0);
	}

	return FFAT_OK;
}


/**
* update cluster from FCC
*
* pEntry�� dwCluster�� dwCount�� update�Ѵ�.
* dwCount�� 0�϶��� �����ϴ� �ǹ̷� ���ȴ�.
*
* @param		pVol			: [IN] volume pointer
* @param		pEntry			: [IN] FCC entry
* @param		dwCluster		: [OUT] free cluster number for update
* @param		dwCount 		: [OUT] cluster count for update
*										0 : this entry will be deleted
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: success
* @return		else			: error
* @author		InHwan Choi
* @version		DEC-29-2007 [InHwan Choi] First Writing.
* @version		OCT-13-2008 [DongYoung Seo] recover code for dirty entry checking
*									for power off recovery
*/
static FFatErr
_updateEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32 dwCluster, t_uint32 dwCount,
					t_boolean bForce, ComCxt* pCxt)
{
	FCCVolInfo* 	pEntryFCC;
	t_uint32		dwDeallocatedCount;
	FFatErr 		r;

	_STATISTIC_FCC_UPDATEENTRY

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

	FFAT_ASSERT(pEntry->pVol != NULL);
	FFAT_ASSERT(pEntry->pVol == pVol);

	pEntryFCC = _FCC(pVol);

	FFAT_DEBUG_FCC_PRINTF((_T("_updateEntry() pEntry/pEntry_Cluster/pEntry_Count/dwCluster/dwCount/bDirty:0x%X/%d/%d/%d/%d/%d\n"), (t_uint32)pEntry, _FCCE_CLUSTER(pEntry), _FCCE_COUNT(pEntry), dwCluster, dwCount, _FCCE_IS_DIRTY(pEntry)));

	// �ϴ� �����.
	r = EssRBTree2_Delete(_FCC_TC(pEntryFCC), _FCCE_NC(pEntry));
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster")));
		FFAT_ASSERT(0);
		return FFAT_EPROG;
	}

	r = EssRBTree2_Delete(_FCC_TCC(pEntryFCC), _FCCE_NCC(pEntry));
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster")));
		FFAT_ASSERT(0);
		return FFAT_EPROG;
	}

	_DEC_FREE_CLUSTER_COUNT(pEntryFCC, _FCCE_COUNT(pEntry));

	if (_FCCE_IS_DIRTY(pEntry) == FFAT_TRUE)
	{
		r = FFATFS_DeallocateClusterFromTo(VOL_VI(pVol), 0,
						_FCCE_CLUSTER(pEntry), _FCCE_COUNT(pEntry),
						&dwDeallocatedCount, FAT_DEALLOCATE_FORCE, FFAT_CACHE_SYNC, NULL, pCxt);
		IF_UK ((r < 0) && (bForce == FFAT_TRUE))
		{
			FFAT_LOG_PRINTF((_T("fail to deallocate cluster")));
			return r;
		}

		ESS_DLIST_DEL_INIT(_FCCE_LD(pEntry));
		
		FFAT_ASSERT(_FCCE_COUNT(pEntry) == dwDeallocatedCount);
	}

	// dwCount�� 0�ϰ��� �����ȴ�
	if (dwCount > 0)
	{
		FFAT_ASSERT(_FCC(pVol) == _FCC(pEntry->pVol));

		r = _addEntry(pVol, pEntry, dwCluster, dwCount, pCxt);
		FFAT_ER(r, (_T("Fail to add entry to FCC")));
	}
	else
	{
		FFAT_ASSERT(dwCount == 0);
	
		ESS_DLIST_DEL_INIT(_FCCE_LD(pEntry));
		ESS_DLIST_MOVE_TAIL(_FCCMAIN_LF(_pFCCMain), _FCCE_LL(pEntry));	// move to free list

		pEntry->pVol = NULL;
	}

	return FFAT_OK;
}


/**
* add cluster from FCC
*
* FCC�� entry�� �߰��Ѵ�.
*
* @param		pVol			: [IN] volume pointer
* @param		pEntry			: [IN] FCC entry
* @param		dwCluster		: [OUT] free cluster number for update
* @param		dwCount 		: [OUT] cluster count for update
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: success
* @return		else			: error
* @author		InHwan Choi
* @version		DEC-29-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_addEntry(Vol* pVol, _FCCEntry* pEntry, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	FCCVolInfo* 	pFCC;
	FFatErr 		r;

	_STATISTIC_FCC_ADDENTRY

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);
	FFAT_ASSERT(pEntry);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);
	FFAT_ASSERT(dwCount > 0);

	pFCC = _FCC(pVol);

	FFAT_ASSERT(pFCC == _FCC(pEntry->pVol));

	FFAT_DEBUG_FCC_PRINTF((_T("_addEntry(0x%X) dwCluster/dwCount/Dirty:%d/%d/%d\n"), (t_uint32)pEntry, dwCluster, dwCount, _FCCE_IS_DIRTY(pEntry)));

	_FCCE_SET_CLUSTER(pEntry, dwCluster);
	_FCCE_SET_COUNT(pEntry, dwCount);

	r = EssRBTree2_Insert(_FCC_TC(pFCC), _FCCE_NC(pEntry));
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster\n")));
		return FFAT_EPROG;
	}
	
	r = EssRBTree2_Insert(_FCC_TCC(pFCC), _FCCE_NCC(pEntry));
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster")));
		return FFAT_EPROG;
	}

	// move to valid list
	ESS_DLIST_MOVE_HEAD(_FCCMAIN_LV(_pFCCMain), _FCCE_LL(pEntry));
	_INC_FREE_CLUSTER_COUNT(pFCC, _FCCE_COUNT(pEntry));

	return FFAT_OK;
}


/**
* get free count for FAT16
*
* buffer���� free cluster�� ã�� ������ ����.
* ã�� free cluster�� FCC�� �߰��Ѵ�.
*
* @param		dwCluster		: [IN] start Cluster number
*									this value is useless if bNotifyFreeCluster is false, and then it is may 0
* @param		pBuff			: [IN] read buffer pointer
* @param		dwLastIndex 	: [IN] last index for free cluster check
* @param		pVolInfo		: [IN] volume information
*									if this value is not NULL, call back is needed
* @param		pCxt			: [IN] context of current operation
* @return		free cluster count
* @author		DongYoung Seo
* @version		AUG-28-2006 [DongYoung Seo] First Writing.
* @history		AUG-17-2007 [Soojeong Kim] add FCC support.
*/
static t_int32
_getFreeClusterCount16(t_uint32 dwCluster, t_int8* pBuff, t_int32 dwLastIndex, Vol* pVol, ComCxt* pCxt)
{
	t_int32 	i;
	t_int32 	dwFreeCount = 0;
	t_uint16*	p16;
	t_uint32	dwCount = 0;
	t_int32 	dwPreCluster = 0;
	t_uint32	dwStartCluster;

	_STATISTIC_FCC_GETFREECLUSTERCOUNT16

	dwPreCluster = 0;

	p16 = (t_uint16*)pBuff;

	for (i = 0; i <= dwLastIndex; i++)
	{
		if (p16[i] == FAT16_FREE)
		{
			dwFreeCount++;
			if ((dwPreCluster + 1) == i)
			{
				dwCount++;
				dwPreCluster = i;
			}
			else if (dwCount != 0)
			{
				dwStartCluster = dwCluster + dwPreCluster - dwCount + 1;

				// never mind about fail!!
				_addClusters(pVol, dwStartCluster, dwCount, FFAT_FALSE, FFAT_FALSE, pCxt);

				// �ٽ� ���� 
				dwCount = 1;
				dwPreCluster = i;
			}
			else
			{
				dwCount++;
				dwPreCluster = i;
			}
		}
	}

	if (dwCount != 0)
	{
		dwStartCluster = dwCluster + dwPreCluster - dwCount + 1;
		// never mind about fail!!
		_addClusters(pVol, dwStartCluster, dwCount, FFAT_FALSE, FFAT_FALSE, pCxt);
	}

	return dwFreeCount;
}


/**
* get free count for FAT32
*
* @param		dwCluster		: [IN] start Cluster number
*									this value is useless if bNotifyFreeCluster is false, and then it is may 0
* @param		pBuff			: [IN] read buffer pointer
* @param		dwLastIndex 	: [IN] last index for free cluster check
* @param		pCxt			: [IN] context of current operation
* @return		free cluster count
* @author		DongYoung Seo
* @version		AUG-28-2006 [DongYoung Seo] First Writing.
*/
static t_int32
_getFreeClusterCount32(t_uint32 dwCluster, t_int8* pBuff, t_int32 dwLastIndex, Vol* pVol, ComCxt* pCxt)
{
	t_int32 	i;
	t_int32 	dwFreeCount = 0;
	t_uint32*	p32;
	t_uint32	dwCount = 0;
	t_int32 	dwPreCluster = 0;
	t_uint32	dwStartCluster;

	_STATISTIC_FCC_GETFREECLUSTERCOUNT32

	dwPreCluster = 0;

	p32 = (t_uint32*)pBuff;

	for (i = 0; i <= dwLastIndex; i++)
	{
		if ((FFAT_BO_UINT32(p32[i]) & FAT32_MASK) == FAT32_FREE)
		{
			dwFreeCount++;
			if ((dwPreCluster + 1) == i)
			{
				dwCount++;
				dwPreCluster = i;
			}
			else if (dwCount != 0)
			{
				dwStartCluster = dwCluster + dwPreCluster - dwCount + 1;

				// never mind about fail!!
				_addClusters(pVol, dwStartCluster, dwCount, FFAT_FALSE, FFAT_FALSE, pCxt); 

				// �ٽ� ���� 
				dwCount = 1;
				dwPreCluster = i;
			}
			else
			{
				dwCount++;
				dwPreCluster = i;
			}
		}
	}

	if (dwCount != 0)
	{
		dwStartCluster = dwCluster + dwPreCluster - dwCount + 1;
		// never mind about fail!!
		_addClusters(pVol, dwStartCluster, dwCount, FFAT_FALSE, FFAT_FALSE, pCxt);
	}

	return dwFreeCount;
}


/**
* get free count
*
* ffatfs ���� ȣ��ÿ��� ����ȴ�.
*
* @param		pVol			: [IN] volume
* @param		pBuff			: [IN] read buffer pointer
* @param		dwBuffSize		: [IN] buffer size
* @param		pdwFreeCount	: [OUT] return value
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK 		: free cluster count
* @return		else			: error
* @author		Soojeong Kim
* @version		AUG-17-2007 [Soojeong Kim] First Writing.
*/
static FFatErr
_getFreeClusterCount(Vol* pVol, t_int8* pBuff, t_int32 dwBuffSize,
						t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	FFatErr				r;
	FatVolInfo*			pVolInfo;
	t_uint32			dwCluster;				// start cluster number
	t_uint32			dwSector;				// current IO sector
	t_uint32			dwRestSector;			// rest sector count
	t_int32				dwSectorCount;			// sector count per a read
	t_uint32			dwTotalFreeCount = 0;	// total free cluster count
	t_int32				dwLastIndex;			// last cluster number
	FFatCacheInfo		stCI;

	_STATISTIC_FCC_GETFREECLUSTERCOUNT

	FFAT_ASSERT(pBuff);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	dwSector		= VI_FFS(pVolInfo); 					// get first fat sector
	dwRestSector	= ESS_MATH_CDB(VI_LCN(pVolInfo) + 1, VI_CCPFS(pVolInfo), VI_CCPFSB(pVolInfo));
	dwSectorCount	= dwBuffSize >> VI_SSB(pVolInfo);		// get sector count per a read

	if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		dwLastIndex = (dwSectorCount << VI_SSB(pVolInfo)) >> 2; // divided by dwFatEntrySizeBit
	}
	else
	{
		dwLastIndex = (dwSectorCount << VI_SSB(pVolInfo)) >> 1;
	}

	dwLastIndex--;		// last valid index �̱� ������.. ����.

	do
	{
		if (dwRestSector <= (t_uint32)dwSectorCount)
		{
			// �̰��� ������ read �̴�.. �׷��Ƿ� last FAT sector�� ���� ������ �ʿ��ϴ�.
			dwSectorCount	= dwRestSector;
			dwLastIndex		= (VI_CCPFS(pVolInfo) * (dwSectorCount - 1))
								+ (VI_LCN(pVolInfo) & VI_CCPFS_MASK(pVolInfo));
		}

		// find first cluster number in FAT sector
		dwCluster = FFATFS_GET_FIRST_CLUSTER_ON_SECTOR(pVolInfo, dwSector);

		// DO NOT NEED TO ADD CACHE
		r = FFATFS_ReadWriteSectors(pVolInfo, dwSector, dwSectorCount, pBuff,
							FFAT_CACHE_DATA_FAT, &stCI, FFAT_TRUE, pCxt);

		IF_UK (r != dwSectorCount)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to read sector from FFATFS cache\n")));
			return FFAT_EIO;
		}

		dwRestSector	-= dwSectorCount;
		dwSector		+= dwSectorCount;

		// get free cluster count and add it to FCC
		if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
		{
			dwTotalFreeCount += _getFreeClusterCount32(dwCluster, pBuff, dwLastIndex, pVol, pCxt);
		}
		else
		{
			dwTotalFreeCount += _getFreeClusterCount16(dwCluster, pBuff, dwLastIndex, pVol, pCxt);
		}
	} while (dwRestSector > 0);

	FFAT_ASSERT(dwTotalFreeCount <= VI_CC(pVolInfo));

	*pdwFreeCount = dwTotalFreeCount;

	FFAT_DEBUG_FCC_PRINTF((_T("_getFreeClusterCount() pVol/FreeClusterCount:0x%X/%d\n"), (t_uint32)pVol, dwTotalFreeCount));

	return FFAT_OK;
}


/**
 * initialize a FCC Volume Info storage
 * 
 * @param		pFCCVI			: pointer of a FCC Volume Info structure
 * @return		FFAT_OK			: Success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
 */
static FFatErr
_initFCCVI(FCCVolInfo* pFCCVI)
{
	FFatErr		r;

	_STATISTIC_FCC_INITFCCVI

	r = EssRBTree2_Init(&pFCCVI->stRBTreeClusterHead);
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster\n")));
		return FFAT_EPROG;
	}

	r = EssRBTree2_Init(&pFCCVI->stRBTreeContCountHead);
	if (r != ESS_OK)
	{
		FFAT_DEBUG_PRINTF((_T("Fail to init tree for cluster")));
		return FFAT_EPROG;
	}

	ESS_DLIST_INIT(&pFCCVI->dlDirtyList);

	pFCCVI->dwFreeClusterCount = 0;

	return FFAT_OK;
}


#ifdef FFAT_DYNAMIC_ALLOC

	/**
	 * get a free FCC Info
	 * 
	 * @return		NULL			: there is no free volume storage, fail to get/put lock
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static FCCVolInfo*
	_getFreeFCCVIDynamic(void)
	{
		return (FCCVolInfo*)FFAT_MALLOC(sizeof(FCCVolInfo), ESS_MALLOC_NONE);
	}


	/**
	 * release a free FCC Info and add it to free list
	 * 
	 * @param		pFCCI			: pointer of a FCCInfo
	 * @return		FFAT_OK			: success
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_releaseFCCVIDynamic(FCCVolInfo* pFCCVI)
	{
		FFAT_ASSERT(pFCCVI);
		FFAT_FREE(pFCCVI, sizeof(FCCVolInfo));

		return FFAT_OK;
	}

#else

	/**
	 * initializes FCC Volume Info storage
	 *	allocates memory for FCC Info
	 * 
	 * @return		FFAT_OK			: Success
	 * @return		FFAT_ENOEMEM	: Not enough memory
	 * @return		else			: general error, refer to the ffat_errno.h
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_initFCCVIStorageStatic(void)
	{
		t_int32		i;

		_STATISTIC_FCC_INITFCCVISTORAGESTATIC

		_pFCCMain->pFCCVI = (FCCVolInfo*)FFAT_MALLOC((sizeof(FCCVolInfo) * FFAT_MAX_VOL_COUNT), ESS_MALLOC_NONE);
		IF_UK (_pFCCMain->pFCCVI == NULL)
		{
			return FFAT_ENOMEM;
		}

		ESS_LIST_INIT(&_pFCCMain->slFreeFCCVI);

		for (i = 0; i < FFAT_MAX_VOL_COUNT; i++)
		{
			ESS_LIST_INIT(&_pFCCMain->pFCCVI[i].slFree);
			ESS_LIST_ADD_HEAD(&_pFCCMain->slFreeFCCVI, &_pFCCMain->pFCCVI[i].slFree);
		}

		return FFAT_OK;
	}


	/**
	 * releases memory for FCC
	 * 
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static void
	_terminateFCCVIStorageStatic(void)
	{
		_STATISTIC_FCC_TERMINATEFCCVISTORAGESTATIC

		IF_UK (_pFCCMain->pFCCVI)
		{
			FFAT_ASSERT(EssList_Count(&_pFCCMain->slFreeFCCVI) == FFAT_MAX_VOL_COUNT);
			FFAT_FREE(_pFCCMain->pFCCVI, (sizeof(FCCVolInfo) * FFAT_MAX_VOL_COUNT));
		}
	}


	/**
	 * get a free FCC Info
	 * 
	 * @return		NULL			: there is no free volume Volume info 
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static FCCVolInfo*
	_getFreeFCCVIStatic(void)
	{
		FCCVolInfo*		pFCCVI = NULL;
		EssList*		pList;

		_STATISTIC_FCC_GETFREEFCCVISTATIC

		IF_UK (ESS_LIST_IS_EMPTY(&_pFCCMain->slFreeFCCVI) == ESS_TRUE)
		{
			FFAT_ASSERT(0);
			// No more free volume structure
			// need to increase FFAT_VOL_COUNT_MAX
			goto out;
		}

		pList = ESS_LIST_GET_HEAD(&_pFCCMain->slFreeFCCVI);
		ESS_LIST_DEL(&_pFCCMain->slFreeFCCVI, pList->pNext);

		pFCCVI = ESS_GET_ENTRY(pList, FCCVolInfo, slFree);

	out:
		return pFCCVI;
	}


	/**
	 * release a free FCC Volume Info and add it to free list
	 * 
	 * @param		pFCCVI			: pointer of a FCCInfo
	 * @return		FFAT_OK			: success
	 * @author		DongYoung Seo
	 * @version		26-MAR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_releaseFCCVIStatic(FCCVolInfo* pFCCVI)
	{
		_STATISTIC_FCC_RELEASEFCCVISTATIC

		FFAT_ASSERT(pFCCVI);

		ESS_LIST_ADD_HEAD(&_pFCCMain->slFreeFCCVI, &pFCCVI->slFree);

		return FFAT_OK;
	}

#endif	// end of #ifdef FFAT_DYNAMIC_ALLOC


// debug begin
//=============================================================================
//
//	DEBUG PART
//

#define FFAT_CHECK_MEM_SIZE 		(32 * 1024) 	//!< maximum 8k memory usage during chkdsk

#ifdef _FCC_DEBUG
	/**
	* check overlapping cluster from each node in RB tree 
	* RBtree���� cluster���� �ߺ��Ǿ� �ִ��� �˻��Ѵ�.
	*
	* @param		pVol		: [IN] volume pointer
	* @return		FFAT_OK 	: success
	* @return		negative	: fail
	* @author		InHwan Choi
	* @version		JAN-02-2008 [InHwan Choi] First Writing.
	*/
	static FFatErr
	_checkTree(Vol* pVol)
	{
		t_uint32			i;
		FCCVolInfo*			pFCC;
		_FCCEntry*			pEntry;
		EssRBNode2*			pNode;
		t_uint32			dwLookupPoint = 0;
		t_uint32			dwLastCluster = 0;
		static t_uint8		pMap[FFAT_CHECK_MEM_SIZE];
		t_uint32			dwTotalClusterCount = 0;

		pFCC = _FCC(pVol);

		FFAT_MEMSET(pMap, 0x00, FFAT_CHECK_MEM_SIZE * sizeof(t_uint8));

		do
		{
			pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwLookupPoint);
			if (pNode != NULL)
			{
				pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

				if (_FCCE_CLUSTER(pEntry) != dwLastCluster)
				{
					for(i = _FCCE_CLUSTER(pEntry); i <= _FCCE_LAST_CLUSTER(pEntry); i++)
					{
						if (ESS_BITMAP_IS_SET(pMap, i))
						{
							FFAT_ASSERTP(0, (_T("RBTREE has node that is overlapping each other !!")));
						}
						ESS_BITMAP_SET(pMap,i);
					}
					dwLastCluster = _FCCE_CLUSTER(pEntry);

					dwTotalClusterCount += _FCCE_COUNT(pEntry);
				}
				dwLookupPoint++;
			}
			else
			{
				break;
			}
		} while(1);

		FFAT_ASSERT(pFCC->dwFreeClusterCount == dwTotalClusterCount);
		
		return FFAT_OK;
	}


	/**
	* check overlapping cluster from each FFatVCE in VC
	* VC entry���� ���� ��ġ�� cluster�� �ִ��� �˻��Ѵ�.
	*
	* @param		pVol		: [IN] volume pointer
	* @param		pVC			: [IN] cluster storage
	* @return		FFAT_OK 	: success
	* @return		negative	: fail
	* @author		InHwan Choi
	* @version		JAN-02-2008 [InHwan Choi] First Writing.
	*/
	static FFatErr
	_checkVC(Vol* pVol, FFatVC* pVC)
	{
		t_int32				dwVCIndex;
		t_uint32			dwMapIndex;
		t_uint32			dwCluster;
		t_uint32			dwCount;
		static t_uint8		pMap[FFAT_CHECK_MEM_SIZE];

		FFAT_MEMSET(pMap, 0x00, FFAT_CHECK_MEM_SIZE * sizeof(t_uint8));

		for (dwVCIndex = 0 ; dwVCIndex < VC_VEC(pVC); dwVCIndex++)
		{
			dwCluster	= pVC->pVCE[dwVCIndex].dwCluster;
			dwCount		= pVC->pVCE[dwVCIndex].dwCount;

			for(dwMapIndex = 0; dwMapIndex < dwCount; dwMapIndex++)
			{
				if (ESS_BITMAP_IS_SET(pMap, (dwCluster + dwMapIndex)))
				{
					FFAT_ASSERTP(0, (_T("FFatVCE is corrupt !!")));
				}
				ESS_BITMAP_SET(pMap, (dwCluster + dwMapIndex));
			}

			if (dwVCIndex < (VC_VEC(pVC) - 1))
			{
				FFAT_ASSERT((VC_LCE(pVC, dwVCIndex) + 1) != (pVC->pVCE[dwVCIndex + 1].dwCluster));
			}
		}

		return FFAT_OK;
	}


	/**
	* search and confirm removed cluster information in RB tree 
	* FCC���� cluster ������ ����ڿ� ã�ƺ��� ��¥ ������ Ȯ���Ѵ�.
	*
	* @param		pVol		: [IN] volume pointer
	* @param		dwCluster	: [IN] removed cluster
	* @param		dwCluster	: [IN] removed cluster count
	* @return		FFAT_OK 	: success
	* @return		negative	: fail
	* @author		InHwan Choi
	* @version		JAN-02-2008 [InHwan Choi] First Writing.
	*/
	static FFatErr
	_removeConfirm(Vol* pVol, t_uint32	dwCluster, t_uint32 dwCount)
	{
		FCCVolInfo* 	pFCC;
		_FCCEntry*		pEntry;
		EssRBNode2* 	pNode;
		t_uint32		dwLookupPoint = 0;
		t_uint32		dwLastCluster = 0;

		pFCC = _FCC(pVol);

		do
		{
			pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwLookupPoint);
			if (pNode != NULL)
			{
				pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

				if (_FCCE_CLUSTER(pEntry) != dwLastCluster)
				{
					FFAT_ASSERT(_IS_INCLUDED(pEntry, dwCluster, dwCount) == FFAT_FALSE);
					FFAT_ASSERT(_IS_INCLUDING(pEntry, dwCluster, dwCount) == FFAT_FALSE);
					FFAT_ASSERT(_IS_OVERLAP(pEntry, dwCluster, dwCount) == FFAT_FALSE);
					dwLastCluster = _FCCE_CLUSTER(pEntry);
				}

				dwLookupPoint++;
			}
			else
			{
				break;
			}
		}while(1);
		
		return FFAT_OK;
	}


	/**
	* search and confirm added cluster information in RB tree 
	* cluster�� �߰��Ѵ��� ���� �ִ��� �˻��Ѵ�.
	*
	* @param		pVol		: [IN] volume pointer
	* @param		dwCluster	: [IN] removed cluster
	* @param		dwCluster	: [IN] removed cluster count
	* @return		FFAT_OK 	: success
	* @return		negative	: fail
	* @author		InHwan Choi
	* @version		JAN-02-2008 [InHwan Choi] First Writing.

	*/
	static FFatErr
	_addConfirm(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount)
	{
		FCCVolInfo* 	pFCC;
		_FCCEntry*		pEntry; 
		EssRBNode2* 	pNode;

		pFCC = _FCC(pVol);

		pNode = EssRBTree2_LookupSmallerApproximate(_FCC_TC(pFCC), dwCluster);	
		if (pNode != NULL)
		{
			pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

			if (_IS_INCLUDED(pEntry, dwCluster, dwCount))
			{
				return FFAT_OK;
			}
		}

		pNode = EssRBTree2_LookupBiggerApproximate(_FCC_TC(pFCC), dwCluster);	
		if (pNode != NULL)
		{
			pEntry = ESS_GET_ENTRY(pNode, _FCCEntry, stNodeCluster);

			if(_IS_INCLUDED(pEntry, dwCluster, dwCount))
			{
				return FFAT_OK;
			}
		}

		FFAT_ASSERT(0);
		
		return FFAT_OK;
	}
#endif

#ifdef _FCC_STATISTICS
	static void
	_printStatistics(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======       FCC        STATICS     =======================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FCC HIT Count: ",			_STATISTIC()->dwFCCHit));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FCC MISS Count: ",			_STATISTIC()->dwFCCMiss));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FCC ENTRY REPLACE Count: ",_STATISTIC()->dwFCCReplace));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
	}
#endif
// debug end

