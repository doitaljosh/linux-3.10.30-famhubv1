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
 * @file		ffatfs_api.c
 * @brief		The file implements FFATFS API
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

//***********************************************************************/
//
// FFATFS API
//
//***********************************************************************/
//
// This file implements FFATFS API
// operation list : Initialization/terminate/lock/etc..
//
// Notice !!
//	1. FFATFS는 recovery 기능을 가지고 있지 않다.
//		recovery 기능은 FFAT_ADDON에서 구현이 되며 이기능은 FFAT_CORE에서 사용한다.
//	2. FFATFS의 API중 일부는 re-entrant 하지 않다.
//		Device에 대한 IO 작업을 수행하는 API들은
//		single lock을 이용하여 한번에 하나의 작업만을 수행한다.
//
//***********************************************************************/

// includes
#include "ess_types.h"
#include "ess_math.h"

#include "ffat_common.h"

#include "ffatfs_types.h"
#include "ffatfs_api.h"
#include "ffatfs_de.h"
#include "ffatfs_fat.h"
#include "ffatfs_misc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_API)

#define _STATISTIC_INIT
#define _STATISTIC_TERMINATE
#define _STATISTIC_MOUNT
#define _STATISTIC_ISVALIDBOOTSECTOR
#define _STATISTIC_SYNC
#define _STATISTIC_SYNCNODE
#define _STATISTIC_SYNCVOL
#define _STATISTIC_FLUSHVOL
#define _STATISTIC_ADDCACHE
#define _STATISTIC_REMOVECACHE
#define _STATISTIC_REGISTERCACHECALLBACK
#define _STATISTIC_CHKCACHE
#define _STATISTIC_GETCLUSTEROFOFFSET
#define _STATISTIC_GETNEXTCLUSTER
#define _STATISTIC_GETLASTCLUSTER
#define _STATISTIC_GETFREECLUSTERS
#define _STATISTIC_GETFREECLUSTERSFROMTO
#define _STATISTIC_GETFREECLUSTERCOUNTAT
#define _STATISTIC_ISFULLYOCCUPIEDFATSECTOR
#define _STATISTIC_MAKECLUSTERCHAIN
#define _STATISTIC_MAKECLUSTERCHAINVC
#define _STATISTIC_INCFREECLUSTER
#define _STATISTIC_DECFREECLUSTER
#define _STATISTIC_GETVECTOREDCLUSTER
#define _STATISTIC_ALLOCATECLUSTER
#define _STATISTIC_DEALLOCATECLUSTER
#define _STATISTIC_DEALLOCATECLUSTERFROMTO
#define _STATISTIC_FREECLUSTERS
#define _STATISTIC_UPDATECLUSTER
#define _STATISTIC_UPDATEVOLUMESTATUS
#define _STATISTIC_ISFREEFATSECTOR
#define _STATISTIC_GETFIRSTCLUSTEROFFATSECTOR
#define _STATISTIC_GETLASTCLUSTEROFFATSECTOR
#define _STATISTIC_GETFCCOFSECTOR
#define _STATISTIC_REGISTERFATCALLBACK
#define _STATISTIC_GETNODEDIRENTRY
#define _STATISTIC_GENNAMEFROMDIRENTRY
#define _STATISTIC_ADJUSTNAMETOFATFORMAT
#define _STATISTIC_REMOVETRAILINGDOTSANDBLANKS
#define _STATISTIC_GETNUMERICTAIL
#define _STATISTIC_SETDETIME
#define _STATISTIC_SETDESIZE
#define _STATISTIC_SETDEATTR
#define _STATISTIC_SETDECLUSTER
#define _STATISTIC_GETCHECKSUM
#define _STATISTIC_GENLFNE
#define _STATISTIC_GENSFNE
#define _STATISTIC_FINDLFNINCLUSTER
#define _STATISTIC_READWRITEONROOT
#define _STATISTIC_WRITEDE
#define _STATISTIC_DELETEDE
#define _STATISTIC_SETVOLUMENAME
#define _STATISTIC_GETVOLUMENAME
#define _STATISTIC_FSCTL
#define _STATISTIC_INITCLUSTER
#define _STATISTIC_INITPARTIALCLUSTER
#define _STATISTIC_READWRITECLUSTER
#define _STATISTIC_READWRITEPARTIALCLUSTER
#define _STATISTIC_READWRITESECTORS
#define _STATISTIC_READWRITEPARTIALSECTOR
#define _STATISTIC_FREECLUSTERVC
#define _STATISTIC_DEALLOCATEWITHSC
#define _STATISTIC_LOCK
#define _STATISTIC_UNLOCK
#define _STATISTIC_PRINT

// debug begin
#ifdef FFAT_DEBUG
	#undef _STATISTIC_INIT
	#undef _STATISTIC_TERMINATE
	#undef _STATISTIC_MOUNT
	#undef _STATISTIC_ISVALIDBOOTSECTOR
	#undef _STATISTIC_SYNC
	#undef _STATISTIC_SYNCNODE
	#undef _STATISTIC_SYNCVOL
	#undef _STATISTIC_FLUSHVOL
	#undef _STATISTIC_ADDCACHE
	#undef _STATISTIC_REMOVECACHE
	#undef _STATISTIC_REGISTERCACHECALLBACK
	#undef _STATISTIC_CHKCACHE
	#undef _STATISTIC_GETCLUSTEROFOFFSET
	#undef _STATISTIC_GETNEXTCLUSTER
	#undef _STATISTIC_GETLASTCLUSTER
	#undef _STATISTIC_GETFREECLUSTERS
	#undef _STATISTIC_GETFREECLUSTERSFROMTO
	#undef _STATISTIC_GETFREECLUSTERCOUNTAT
	#undef _STATISTIC_ISFULLYOCCUPIEDFATSECTOR
	#undef _STATISTIC_MAKECLUSTERCHAIN
	#undef _STATISTIC_MAKECLUSTERCHAINVC
	#undef _STATISTIC_INCFREECLUSTER
	#undef _STATISTIC_DECFREECLUSTER
	#undef _STATISTIC_GETVECTOREDCLUSTER
	#undef _STATISTIC_ALLOCATECLUSTER
	#undef _STATISTIC_DEALLOCATECLUSTER
	#undef _STATISTIC_DEALLOCATECLUSTERFROMTO
	#undef _STATISTIC_FREECLUSTERS
	#undef _STATISTIC_UPDATECLUSTER
	#undef _STATISTIC_UPDATEVOLUMESTATUS
	#undef _STATISTIC_ISFREEFATSECTOR
	#undef _STATISTIC_GETFIRSTCLUSTEROFFATSECTOR
	#undef _STATISTIC_GETLASTCLUSTEROFFATSECTOR
	#undef _STATISTIC_GETFCCOFSECTOR
	#undef _STATISTIC_REGISTERFATCALLBACK
	#undef _STATISTIC_GETNODEDIRENTRY
	#undef _STATISTIC_GENNAMEFROMDIRENTRY
	#undef _STATISTIC_ADJUSTNAMETOFATFORMAT
	#undef _STATISTIC_REMOVETRAILINGDOTSANDBLANKS
	#undef _STATISTIC_GETNUMERICTAIL
	#undef _STATISTIC_SETDETIME
	#undef _STATISTIC_SETDESIZE
	#undef _STATISTIC_SETDEATTR
	#undef _STATISTIC_SETDECLUSTER
	#undef _STATISTIC_GETCHECKSUM
	#undef _STATISTIC_GENLFNE
	#undef _STATISTIC_FINDLFNINCLUSTER
	#undef _STATISTIC_GENSFNE
	#undef _STATISTIC_READWRITEONROOT
	#undef _STATISTIC_WRITEDE
	#undef _STATISTIC_DELETEDE
	#undef _STATISTIC_SETVOLUMENAME
	#undef _STATISTIC_GETVOLUMENAME
	#undef _STATISTIC_FSCTL
	#undef _STATISTIC_INITCLUSTER
	#undef _STATISTIC_INITPARTIALCLUSTER
	#undef _STATISTIC_READWRITECLUSTER
	#undef _STATISTIC_READWRITEPARTIALCLUSTER
	#undef _STATISTIC_READWRITESECTORS
	#undef _STATISTIC_READWRITEPARTIALSECTOR
	#undef _STATISTIC_FREECLUSTERVC
	#undef _STATISTIC_DEALLOCATEWITHSC
	#undef _STATISTIC_LOCK
	#undef _STATISTIC_UNLOCK

	#define _STATISTIC_INIT						_STATISTICS()->dwInit++;	\
												FFAT_MEMSET(_stStatistics, 0x00, sizeof(_stStatistics));
	#define _STATISTIC_TERMINATE				_STATISTICS()->dwTerminate++;	_STATISTIC_PRINT;
	#define _STATISTIC_MOUNT					_STATISTICS()->dwMount++;
	#define _STATISTIC_ISVALIDBOOTSECTOR		_STATISTICS()->dwIsValidBootSector++;
	#define _STATISTIC_SYNC						_STATISTICS()->dwSync++;
	#define _STATISTIC_SYNCNODE					_STATISTICS()->dwSyncNode++;
	#define _STATISTIC_SYNCVOL					_STATISTICS()->dwSyncVol++;
	#define _STATISTIC_FLUSHVOL					_STATISTICS()->dwFlushVol++;
	#define _STATISTIC_ADDCACHE					_STATISTICS()->dwAddCache++;
	#define _STATISTIC_REMOVECACHE				_STATISTICS()->dwRemoveCache++;
	#define _STATISTIC_REGISTERCACHECALLBACK	_STATISTICS()->dwRegisterCacheCallback++;
	#define _STATISTIC_CHKCACHE					_STATISTICS()->dwChkCache++;
	#define _STATISTIC_GETCLUSTEROFOFFSET		_STATISTICS()->dwGetClusterOfOffset++;
	#define _STATISTIC_GETNEXTCLUSTER			_STATISTICS()->dwGetNextCluster++;
	#define _STATISTIC_GETLASTCLUSTER			_STATISTICS()->dwGetLastCluster++;
	#define _STATISTIC_GETFREECLUSTERS			_STATISTICS()->dwGetFreeClusters++;
	#define _STATISTIC_GETFREECLUSTERSFROMTO	_STATISTICS()->dwGetFreeClustersFromTo++;
	#define _STATISTIC_ISFULLYOCCUPIEDFATSECTOR	_STATISTICS()->dwIsFullyOccupiedFATSector++;
	#define _STATISTIC_MAKECLUSTERCHAIN			_STATISTICS()->dwMakeClusterChain++;
	#define _STATISTIC_MAKECLUSTERCHAINVC		_STATISTICS()->dwMakeClusterChainVC++;
	#define _STATISTIC_INCFREECLUSTER			_STATISTICS()->dwIncFreeCluster++;
	#define _STATISTIC_DECFREECLUSTER			_STATISTICS()->dwDecFreeCluster++;
	#define _STATISTIC_GETVECTOREDCLUSTER		_STATISTICS()->dwGetVectoredCluster++;
	#define _STATISTIC_ALLOCATECLUSTER			_STATISTICS()->dwAllocateCluster++;
	#define _STATISTIC_DEALLOCATECLUSTER		_STATISTICS()->dwDeallocateCluster++;
	#define _STATISTIC_DEALLOCATECLUSTERFROMTO	_STATISTICS()->dwDeallocateClusterFromTo++;
	#define _STATISTIC_FREECLUSTERS				_STATISTICS()->dwFreeClusters++;
	#define _STATISTIC_UPDATECLUSTER			_STATISTICS()->dwUpdateCluster++;
	#define _STATISTIC_UPDATEVOLUMESTATUS		_STATISTICS()->dwUpdateVolumeStatus++;
	#define _STATISTIC_ISFREEFATSECTOR			_STATISTICS()->dwIsFreeFatSector++;
	#define _STATISTIC_GETFIRSTCLUSTEROFFATSECTOR	_STATISTICS()->dwGetFirstClusterOfFatSector++;
	#define _STATISTIC_GETLASTCLUSTEROFFATSECTOR	_STATISTICS()->dwGetLastClusterOfFatSector++;
	#define _STATISTIC_GETFCCOFSECTOR			_STATISTICS()->dwGetFCCOfSector++;
	#define _STATISTIC_REGISTERFATCALLBACK		_STATISTICS()->dwRegisterFATCallback++;
	#define _STATISTIC_GETNODEDIRENTRY			_STATISTICS()->dwGetNodeDirEntry++;
	#define _STATISTIC_GENNAMEFROMDIRENTRY		_STATISTICS()->dwGenNameFromDirEntry++;
	#define _STATISTIC_ADJUSTNAMETOFATFORMAT	_STATISTICS()->dwAdjustNameToFatFormat++;
	#define _STATISTIC_REMOVETRAILINGDOTSANDBLANKS	_STATISTICS()->dwRemoveTrailingDotsAndBlanks++;
	#define _STATISTIC_GETNUMERICTAIL			_STATISTICS()->dwGetNumericTail++;
	#define _STATISTIC_SETDETIME				_STATISTICS()->dwSetDeTime++;
	#define _STATISTIC_SETDESIZE				_STATISTICS()->dwSetDeSize++;
	#define _STATISTIC_SETDEATTR				_STATISTICS()->dwSetDeAttr++;
	#define _STATISTIC_SETDECLUSTER				_STATISTICS()->dwSetDeCluster++;
	#define _STATISTIC_GETCHECKSUM				_STATISTICS()->dwGetCheckSum++;
	#define _STATISTIC_GENLFNE					_STATISTICS()->dwGenLFNE++;
	#define _STATISTIC_FINDLFNINCLUSTER			_STATISTICS()->dwFindLFNInCluster++;
	#define _STATISTIC_READWRITEONROOT			_STATISTICS()->dwReadWriteOnRoot++;
	#define _STATISTIC_WRITEDE					_STATISTICS()->dwWriteDE++;
	#define _STATISTIC_DELETEDE					_STATISTICS()->dwDeleteDE++;
	#define _STATISTIC_SETVOLUMENAME			_STATISTICS()->dwSetVolumeLabel++;
	#define _STATISTIC_GETVOLUMENAME			_STATISTICS()->dwGetVolumeLabel++;
	#define _STATISTIC_FSCTL					_STATISTICS()->dwFSCtl++;
	#define _STATISTIC_INITCLUSTER				_STATISTICS()->dwInitCluster++;
	#define _STATISTIC_INITPARTIALCLUSTER		_STATISTICS()->dwInitPartialCluster++;
	#define _STATISTIC_READWRITECLUSTER			_STATISTICS()->dwReadWriteCluster++;
	#define _STATISTIC_READWRITEPARTIALCLUSTER	_STATISTICS()->dwReadWritePartialCluster++;
	#define _STATISTIC_READWRITESECTORS			_STATISTICS()->dwReadWriteSectors++;
	#define _STATISTIC_READWRITEPARTIALSECTOR	_STATISTICS()->dwReadWritePartialSector++;
	#define _STATISTIC_FREECLUSTERVC			_STATISTICS()->dwFreeClusterVC++;
	#define _STATISTIC_DEALLOCATEWITHSC			_STATISTICS()->dwDeallocateWithSC++;
	#define _STATISTIC_LOCK						_STATISTICS()->dwLock++;
	#define _STATISTIC_UNLOCK					_STATISTICS()->dwUnLock++;

	typedef struct
	{
		t_uint32 dwInit;
		t_uint32 dwTerminate;
		t_uint32 dwMount;
		t_uint32 dwIsValidBootSector;
		t_uint32 dwSync;
		t_uint32 dwSyncNode;
		t_uint32 dwSyncVol;
		t_uint32 dwFlushVol;
		t_uint32 dwAddCache;
		t_uint32 dwRemoveCache;
		t_uint32 dwRegisterCacheCallback;
		t_uint32 dwChkCache;
		t_uint32 dwGetClusterOfOffset;
		t_uint32 dwGetNextCluster;
		t_uint32 dwGetLastCluster;
		t_uint32 dwGetFreeClusters;
		t_uint32 dwGetFreeClustersFromTo;
		t_uint32 dwIsFullyOccupiedFATSector;
		t_uint32 dwMakeClusterChain;
		t_uint32 dwMakeClusterChainVC;
		t_uint32 dwIncFreeCluster;
		t_uint32 dwDecFreeCluster;
		t_uint32 dwGetVectoredCluster;
		t_uint32 dwAllocateCluster;
		t_uint32 dwDeallocateCluster;
		t_uint32 dwDeallocateClusterFromTo;
		t_uint32 dwFreeClusters;
		t_uint32 dwUpdateCluster;
		t_uint32 dwUpdateVolumeStatus;
		t_uint32 dwIsFreeFatSector;
		t_uint32 dwGetFirstClusterOfFatSector;
		t_uint32 dwGetLastClusterOfFatSector;
		t_uint32 dwGetFCCOfSector;
		t_uint32 dwRegisterFATCallback;
		t_uint32 dwGetNodeDirEntry;
		t_uint32 dwGenNameFromDirEntry;
		t_uint32 dwAdjustNameToFatFormat;
		t_uint32 dwRemoveTrailingDotsAndBlanks;
		t_uint32 dwGetNumericTail;
		t_uint32 dwSetDeTime;
		t_uint32 dwSetDeSize;
		t_uint32 dwSetDeAttr;
		t_uint32 dwSetDeCluster;
		t_uint32 dwGetCheckSum;
		t_uint32 dwGenLFNE;
		t_uint32 dwFindLFNInCluster;
		t_uint32 dwReadWriteOnRoot;
		t_uint32 dwWriteDE;
		t_uint32 dwDeleteDE;
		t_uint32 dwSetVolumeLabel;
		t_uint32 dwGetVolumeLabel;
		t_uint32 dwFSCtl;
		t_uint32 dwInitCluster;
		t_uint32 dwInitPartialCluster;
		t_uint32 dwReadWriteCluster;
		t_uint32 dwReadWritePartialCluster;
		t_uint32 dwReadWriteSectors;
		t_uint32 dwReadWritePartialSector;
		t_uint32 dwFreeClusterVC;
		t_uint32 dwDeallocateWithSC;
		t_uint32 dwLock;
		t_uint32 dwUnLock;
	} _Statistic;

	#define _STATISTICS()		((_Statistic*)&_stStatistics)

	//static _MainDebug	_stMainDebug;
	static t_int32 _stStatistics[sizeof(_Statistic) / sizeof(t_int32)];

	#undef _STATISTIC_PRINT
	#define _STATISTIC_PRINT	_statisticsPrint();

	static void	_statisticsPrint(void);

#endif
// debug end


/**
 * This function initializes FFatfs module
 *
 * @param		bForce				: force initialization.
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREADY	: FFATFS already initialized
 * @return		else				: error
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_Init(t_boolean bForce)
{
	_STATISTIC_INIT
	return ffat_fs_init(bForce);
}


/**
 * This function terminates FFatfs module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_Terminate(void)
{
	_STATISTIC_TERMINATE
	return ffat_fs_terminate();
}


/**
 * This function sync caches for a volume
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwFlag		: [IN] cache flag
 *								FFAT_CACHE_SYNC
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JUL-14-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_SyncVol(FatVolInfo* pVI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_SYNCVOL

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_cache_syncVol(pVI, dwFlag, pCxt);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * This function sync caches for a node
 *
 * @param		pVI			: [IN] volume pointer
 * @param		pNode		: [IN] node pointer
 *								FFATFS does not know about the Node structure
 *								It just distinguish it with a pointer value.
 * @param		dwFlag		: [IN] cache flag
 *								FFAT_CACHE_SYNC
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_SyncNode(FatVolInfo* pVI, void* pNode, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_SYNCNODE

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_cache_syncNode(pVI, pNode, dwFlag);

	r |= FFATFS_UnLock(pCxt);

	return r;
}

/**
* This function flush all caches for a volume
*
* @param		pVI			: [IN] volume pointer
* @param		dwFlag		: [IN] cache flag
* @param		pCxt		: [IN] context of current operation
* @return		void
* @author		DongYoung Seo
* @version		May-29-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_FlushVol(FatVolInfo* pVI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_FLUSHVOL

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_cache_flushVol(pVI, dwFlag);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
* This function add a new cache to FFATFS cache
*
* @param		pBuff			: buffer for cache
* @param		dwSize			: cache size in byte
* @param		dwSectorSize	: sector size for cache operation
* @param		pCxt			: [IN] context of current operation
* @return		void
* @author		DongYoung Seo
* @version		MAR-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_AddCache(t_int8* pBuff, t_int32 dwSize, t_int32 dwSectorSize, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_ADDCACHE

	r = ffat_fs_cache_addCache(pBuff, dwSize, dwSectorSize);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
* This function remove a cache from FFATFS cache
*
* @param		pBuff			: buffer pointer for cache
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		FFAT_EINVALID	: Invalid parameter
* @author		DongYoung Seo
* @version		MAR-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_RemoveCache(t_int8* pBuff, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_REMOVECACHE

	r = ffat_fs_cache_removeCache(pBuff, pCxt);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
* This function check a proper cache exist or not for size pChkCache->dwSectorSize
*
*
* @param		pChkCache		: pointer of FFatCheckCache structure
* @param		pCxt			: [IN] context of current operation
* @param		FFAT_OK			: the proper cache does not exist
* @param		FFAT_OK1		: the proper cache is exists, and fill pChkCache->pBuffer pointer
* @param		FFAT_EINVALID	: Invalid paramter
* @param		else			: error
* @author		Soojeong Kim
* @version		AUG-09-2007 [Soojeong Kim] First Writing.
* @version		JAN-06-2009 [DongYoung Seo] change parameter to pCakCache
*/
FFatErr
FFATFS_ChkCache(FFatCheckCache* pChkCache, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	if (pChkCache == NULL)
	{
		FFAT_PRINT_DEBUG((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_CHKCACHE

	if (ffat_fs_cache_checkCache(pChkCache->dwSectorSize, &pChkCache->pBuff) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("A proper cache is already added")));
		r = FFAT_OK1;
	}
	else
	{
		r = FFAT_OK;
	}

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
* This function (un)register callback function for cache operation
*
* @param		pFN		: function pointer for callback function
* @param		bReg	: FFAT_TRUE : register, FFAT_FALSE: un-register
* @param		pCxt	: [IN] context of current operation
* @return		void
* @author		DongYoung Seo
* @version		NOV-05-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_RegisterCacheCallback(PFN_CACHE_CALLBACK pFN, t_boolean bReg, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_REGISTERCACHECALLBACK

	r = ffat_fs_cache_callback(pFN, bReg);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * This function discard cache entries at FFatfsCache
 *
 * @param		pVI			: volume information
 * @param		dwSector	: discard start sector number
 * @param		dwCount		: sector count
* @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-17-2008 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_DiscardCache(FatVolInfo* pVI, t_uint32 dwSector, t_int32 dwCount, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, FFATFS_GET_CLUSTER_OF_SECTOR(pVI, dwSector)) == FFAT_TRUE);
	FFAT_ASSERT(dwCount > 0);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_REGISTERCACHECALLBACK

	ffat_fs_cache_discard(pVI, dwSector, dwCount);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


//=============================================================================
//
// API for BOOT SECTOR
//

/**
 * mount a volume
 *
 * set volume flag
 * volume의 정보를 위한 boot sector read 등의 동작은 여기서 할 수 없다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		pLDevInfo		: [IN] logical device information
 * @param		pDev			: [IN] void type pointer for device IO
 *										block device IO를 이용할때 전달된다.
* @param		pCxt			: [IN] context of current operation
 * @param		dwFlag			: mount flag
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_Mount(FatVolInfo* pVI, FFatLDevInfo* pLDevInfo,
				void* pDev, ComCxt* pCxt, FFatMountFlag dwFlag)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pLDevInfo);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_MOUNT

	VI_SET_CXT(pVI, pCxt);

	FFAT_MEMSET(pVI, 0x00, sizeof(FatVolInfo));

	r = ffat_fs_bs_mount(pVI, pLDevInfo, pDev, pCxt, dwFlag);
	FFAT_EO(r, (_T("fail to read boot sector and retrieve volume information")));

	r = ffat_fs_initVolInfo(pVI);
	FFAT_EO(r, (_T("fail to initialize volume information")));

out:
	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * unmount a volume
 *
 * release all resource for volume
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwFlag			: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		MAY-21-2008 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_Umount(FatVolInfo* pVI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_MOUNT

	VI_SET_CXT(pVI, pCxt);

	if (VI_CACHE(pVI))
	{
		r = ffat_fs_cache_flushVol(pVI, dwFlag);
		if ((r != FFAT_OK) && ((dwFlag & FFAT_CACHE_FORCE) == 0))
		{
			FFAT_LOG_PRINTF((_T("fail to flush caches")));
			goto out;
		}

		r = ffat_fs_cache_putCache(VI_CACHE(pVI));
		FFAT_EO(r, (_T("fail to release FFATFS cache")));
	}

out:
	r |= FFATFS_UnLock(pCxt);

	return r;
}


/** 
 * retrieve boot sector info from boot sector before mount
 * (sector size, cluster size, first data sector)
 * 
 * @param		pDev			: [IN] user pointer for block device IO
 *									this parameter is used for block device IO 
 *									such as sector read or write.
 *									User can distinguish devices from this pointer.
 * @param		dwIOSize		: [IN] current I/O size
 * @param		pdwSectorSize	: [OUT] sector size storage
 * @param		pdwClusterSize	: [OUT] cluster size storage
 * @param		pdwFirstDataSector	: [OUT] first data sector storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] add cluster size
 * @version		FEB-18-2008 [GwangOk Go] add first data sector
*/
FFatErr
FFATFS_GetBSInfoFromBS(void* pDev, t_int32 dwIOSize, t_int32* pdwSectorSize,
						t_int32* pdwClusterSize, t_uint32* pdwFirstDataSector, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pdwSectorSize == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pdwSectorSize);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_MOUNT

	r = ffat_fs_bs_getBSInfoFromBS(pDev,dwIOSize, pdwSectorSize, pdwClusterSize,
									pdwFirstDataSector, pCxt);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


//=============================================================================
//
// API for FAT
//

/**
 * update volume status in pVI
 *
 * pVI에 이미 volume 정보가 있을 경우 아무런 작업을 하지 않는다.
 *
 * @param		pVI			: [IN] volume pointer
 * @param		pBuff		: [IN] buffer pointer, may be NULL
 * @param		dwSize		: [IN] size of buffer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_UpdateVolumeStatus(FatVolInfo* pVI, t_int8* pBuff, t_int32 dwSize, ComCxt* pCxt)
{

	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_UPDATEVOLUMESTATUS

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_updateStatus(pVI, pBuff, dwSize);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * get cluster of dwOffset
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] start cluster, offset is 0
 * @param		dwOffset		: byte offset from begin of the node
 * @param		pdwCluster		: cluster number storage
 *									*pdwCluster 0 : root directory
 * @param		pdwClusterPrev	: previous cluster, may be NULL
 *									0 : no previous cluster
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		JUL-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GetClusterOfOffset(FatVolInfo* pVI, t_uint32 dwCluster,
			t_uint32 dwOffset, t_uint32* pdwCluster, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pdwCluster == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}

	IF_UK ((dwCluster != FFATFS_FAT16_ROOT_CLUSTER) && 
			FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pdwCluster);
	FFAT_ASSERT((dwCluster == FFATFS_FAT16_ROOT_CLUSTER) ? (FFATFS_IS_FAT16(pVI) == FFAT_TRUE) : (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE));

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETCLUSTEROFOFFSET

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getClusterOfOffset(pVI, dwCluster, dwOffset, pdwCluster);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Get the next cluster
 *
 * @param		pVI				: volume pointer
 * @param		dwCluster		: current cluster
 * @param		pdwNextCluster	: next cluster storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-11-2006 [DongYoung Seo] First Writing.
 */
FFatErr	
FFATFS_GetNextCluster(FatVolInfo* pVI, t_uint32 dwCluster,
						t_uint32* pdwNextCluster, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pdwNextCluster == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETNEXTCLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_forwardCluster(pVI, dwCluster, 1, pdwNextCluster);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Get the last cluster
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] current cluster
 * @param		pdwLastCluster	: [OUT] last cluster storage
 *									dwCluster가 last cluster이 경우 dwCluster가 저장된다.
 * @param		pdwCount		: [OUT] cluster count to the last cluster
 *									may be NULL
 *									dwCluster 가 last cluster 일 경우 0 이 저장된다.
 *									dwCluster is not included. (0 when dwCluster is the last cluster)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EFAT	: cluster chain is corrupted
 * @return		else			: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GetLastCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32* pdwLastCluster,
						t_uint32* pdwCount, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pdwLastCluster == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETLASTCLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getLastCluster(pVI, dwCluster, pdwLastCluster, pdwCount);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * get free clusters 
 *
 * free cluster를 찾아 그 정보를 array 형태로 pdwClusters에 저장한다.
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwCount		: [IN] free cluster request count
 * @param		pVC			: [IN/OUT] free cluster storage, array
 * @param		dwHint		: [IN] free cluster hint
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GetFreeClusters(FatVolInfo* pVI, t_uint32 dwCount, FFatVC* pVC,
						t_uint32 dwHint, t_boolean bGetMoreCluster, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pVC == NULL) || (dwCount <= 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETFREECLUSTERS

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getFreeClusters(pVI, dwCount, pVC, dwHint, bGetMoreCluster);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * get free clusters 
 *
 * free cluster를 찾아 그 정보를 array 형태로 pdwClusters에 저장한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwHint			: [IN] free cluster hint
 * @param		dwFrom			: [IN] lookup start cluster
 * @param		dwTo			: [IN] lookup end cluster
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN/OUT] free cluster storage, array
 * @param		pdwFreeCount	: [OUT] found free cluster count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GetFreeClustersFromTo(FatVolInfo* pVI, t_uint32 dwHint, 
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwFreeCount, t_boolean bGetMoreCluster, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pVC == NULL) || (dwCount <= 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETFREECLUSTERSFROMTO

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getFreeClustersFromTo(pVI, dwHint, dwFrom, dwTo, dwCount, pVC, pdwFreeCount, bGetMoreCluster);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * make cluster chain and update FAT
 *
 * dwPrevEOF 로 부터 cluster chain을 생성한다.
 * Write EOC at last cluster
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *									may be 0 when there is no free cluster
 * @param		dwClusterCount	: [IN] cluster count to be updated
 * @param		pdwClusters		: [IN] cluster storage
 * @param		dwFUFlag			: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_MakeClusterChain(FatVolInfo* pVI, t_uint32 dwPrevEOF, t_int32 dwClusterCount, 
						t_uint32* pdwClusters, FatUpdateFlag dwFUFlag,
						FFatCacheFlag dwCacheFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pdwClusters == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_MAKECLUSTERCHAIN

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_makeClusterChain(pVI, dwPrevEOF, dwClusterCount,
					pdwClusters, dwFUFlag, dwCacheFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * update FAT area and make cluster chain with VC
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
 *										may be 0 ==> no previous cluster
 * @param		pVC				: [IN] Vectored Cluster Information
 * @param		dwFUFlag		: [IN] flags for FAT update
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		NOV-03-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_MakeClusterChainVC(FatVolInfo* pVI, t_uint32 dwPrevEOF, 
							FFatVC* pVC, FatUpdateFlag dwFUFlag,
							FFatCacheFlag dwCacheFlag, void* pNode,ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pVC == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_MAKECLUSTERCHAINVC

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_makeClusterChainVC(pVI, dwPrevEOF, pVC, dwFUFlag,
						dwCacheFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * increase free cluster counts
 *
 * VolInfo의 free cluster의 갯수를 증가 시킨다.
 * 주의 !!!
 * FAT 외부에서 cluster 할당을 받을 경우 반드시 이 함수를 이용하여 
 * free cluster 갯수에 대한 동기화를 수행하여야 한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCount			: [IN] free cluster count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 */
void
FFATFS_IncFreeClusterCount(FatVolInfo* pVI, t_uint32 dwCount, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pCxt);

	r = FFATFS_Lock(pCxt);
	FFAT_EO(r, (_T("Fail to get lock")));

	_STATISTIC_INCFREECLUSTER

	//20100413_sks => Change to add the cluster notification function
	if (FFATFS_IsValidFreeClusterCount(pVI) == FFAT_TRUE)
	{
		pVI->stVolInfoCache.dwFreeClusterCount += dwCount;
		FFAT_CLUSTER_CHANGE_NOTI(pVI->stVolInfoCache.dwFreeClusterCount, pVI->dwClusterCount,pVI->dwClusterSize,pVI->pDevice);
		FFAT_ASSERT(VI_CC(pVI) >= dwCount);
	}
	else
	{
		FFAT_CLUSTER_CHANGE_NOTI(INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,NULL);
	}

	r = FFATFS_UnLock(pCxt);
	FFAT_EO(r, (_T("Fail to put lock")));

out:
	// ignore error
	return;
}


/**
 * decrease free cluster counts
 *
 * VolInfo의 free cluster의 갯수를 감소
 * 주의 !!!
 * FAT 외부에서 cluster 할당을 받을 경우 반드시 이 함수를 이용하여 
 * free cluster 갯수에 대한 동기화를 수행하여야 한다.
 * 
 * @param		pVI				: [IN] volume pointer
 * @param		dwCount			: [IN] free cluster count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 */
void
FFATFS_DecFreeClusterCount(FatVolInfo* pVI, t_uint32 dwCount, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pCxt);

	r = FFATFS_Lock(pCxt);
	FFAT_EO(r, (_T("Fail to get lock")));

	_STATISTIC_DECFREECLUSTER

	//20100413_sks => Change to add the cluster notification function
	if (FFATFS_IsValidFreeClusterCount(pVI) == FFAT_TRUE)
	{
		FFAT_ASSERT(dwCount <= pVI->stVolInfoCache.dwFreeClusterCount);
		FFAT_ASSERT(VI_CC(pVI) >= dwCount);

		pVI->stVolInfoCache.dwFreeClusterCount -= dwCount;
		FFAT_CLUSTER_CHANGE_NOTI(pVI->stVolInfoCache.dwFreeClusterCount, pVI->dwClusterCount, pVI->dwClusterSize, pVI->pDevice);
	}
	else
	{
		FFAT_CLUSTER_CHANGE_NOTI(INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,INVALID_CLUSTER_COUNT,NULL);
	}

	r = FFATFS_UnLock(pCxt);
	FFAT_EO(r, (_T("Fail to put lock")));
out:
	// ignore error
	return;
}


/**
 * get cluster information.
 *
 * vector 형태로 cluster의 정보를 구한다.
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] start cluster number
 * @param		dwCount			: [IN] cluster count
 *										0 : fill until pVC is full or till EOF
 * @param		pVC				: [OUT] vectored cluster information
 * @param		bGetContiguous	: [IN] if this flag is TRUE, get last VCE to contiguous cluster
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-17-2006 [DongYoung Seo] First Writing.
 * @version		DEC-16-2008 [GwangOk Go] add bGetContiguous flag
 */
FFatErr
FFATFS_GetVectoredCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwCount,
						FFatVC* pVC, t_boolean bGetContiguous, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwCount < 0) || (pVC == NULL) ||
			(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETVECTOREDCLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getVectoredCluster(pVI, dwCluster, dwCount, bGetContiguous, pVC);

	FFAT_ASSERT(VC_VEC(pVC) <= VC_TEC(pVC));
	FFAT_ASSERT(VC_CC(pVC) > 0);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * deallocate clusters from end of the chain
 * Cluster의 할당을 해제 한다.
 * backward 방식으로 해제를 수행한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 * @param		pAlloc			: [IN] allocated cluster information
 *									deallocated 될 모든 cluster 정보가 포함되어 있지 않을 수 있다.
 *									pAlloc->pVC 가 NULL 일 수도 있다.
 *										(이때는 반드시 pAlloc->dwFirstCluster가 valid 한 값을 가져야한다.)
 * @param		pdwDeallocCount	: [IN] deallocated cluster count
 * @param		pdwFristDealloc	: [IN/OUT] the first deallocated cluster number
 *										may be NULL
 * @param		dwFAFlag		: [IN] deallocation flag
 *									FAT_DEALLOCATE_FORCE : force deallocate
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @version		JUL-03-2008 [DongYoung Seo] add parameter pdwFirstDealloc
 */
FFatErr
FFATFS_DeallocateCluster(FatVolInfo* pVI, t_int32 dwCount,
							FatAllocate* pAlloc, t_uint32* pdwDeallocCount,
							t_uint32* pdwFirstDealloc,
							FatAllocateFlag dwFAFlag, FFatCacheFlag dwCacheFlag,
							void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwCount < 0) || (pAlloc == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_DEALLOCATECLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_deallocateCluster(pVI, dwCount, pAlloc, pdwDeallocCount, 
						pdwFirstDealloc, dwFAFlag, dwCacheFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * deallocate clusters from to
 * Cluster의 할당을 해제 한다.
 * backward 방식으로 해제를 수행한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwPostEOF		: [IN] post EOF cluster may 0
 * @param		dwCluster		: [IN] first cluster to deallocate, can not be 0
 * @param		dwCount			: [IN] cluster deallocation count
 * @param		pdwDeallocCount	: [IN/OUT] deallocated cluster count
 * @param		dwFAFlag		: [IN] deallocation flag
 *									FAT_DEALLOCATE_FORCE : force deallocate
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_DeallocateClusterFromTo(FatVolInfo* pVI, t_uint32 dwPostEOF, t_uint32 dwCluster,
							t_uint32 dwCount, t_uint32* pdwDeallocCount, FatAllocateFlag dwFAFlag,
							FFatCacheFlag dwCacheFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pdwDeallocCount == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);
#endif

	// dirty cache entry for DE must be discarded when the clusters are deallocated
	FFAT_ASSERT((dwCacheFlag & FFAT_CACHE_DATA_DE) ? (dwFAFlag & FAT_DEALLOCATE_DISCARD_CACHE) : FFAT_TRUE);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_DEALLOCATECLUSTERFROMTO

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_deallocateClusterFromTo(pVI, dwPostEOF, dwCluster, dwCount,
							pdwDeallocCount, dwFAFlag, dwCacheFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Free clusters from end of the cluster
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCount			: [IN] cluster allocation count
 * @param		pClusters		: [IN] clusters to be free
 * @param		dwFlag			: [IN] cache flag
 *									FFAT_CACHE_SYNC : sync updated FAT area after free operation
 * @param		dwAllocFlag		" [IN] flags for cluster deallocation
 *									FAT_ALLOCATE_DIR : free clusters for directory
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		SEP-25-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_FreeClusters(FatVolInfo* pVI, t_int32 dwCount,t_uint32* pClusters,
					FFatCacheFlag dwFlag, FatAllocateFlag dwAllocFlag,
					void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwCount < 0) || (pClusters == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_FREECLUSTERS

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_freeClusters(pVI, dwCount, pClusters, dwFlag, dwAllocFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * update a cluster
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwValue			: [IN] cluster data
 * @param		dwFlag			: [IN] cache flag
 *									FFAT_CACHE_SYNC : sync updated FAT area after free operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		SEP-25-2006 [DongYoung Seo] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_UpdateCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwValue,
						FFatCacheFlag dwFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_FALSE))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_UPDATECLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = FFAT_FS_FAT_UPDATECLUSTER(pVI, dwCluster, dwValue, dwFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * cluster에 대한 초기화를 수행한다.
 *
 * @param		pVI			: [IN] volume pointer
 * @param		dwCluster	: [IN] cluster information
 * @param		dwCount		: [IN] cluster count
 * @param		pBuff		: [IN] buffer pointer
 *								may be NULL when there is no buffer
 *								NULL이 아닐 경우는 반드시 0x00로 초기화된 buffer이어야 한다.
 * @param		dwSize		: [IN] buffer size
 * @param		dwFlag		: [IN] cache flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_InitCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwCount,
					t_int8* pBuff, t_int32 dwSize, FFatCacheFlag dwFlag,
					FFatCacheInfo* pCI, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwCount < 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_INITCLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_initCluster(pVI, dwCluster, dwCount, pBuff, dwSize, dwFlag, pCI);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * 하나의 cluster에 대해 일부 영역의 초기화를 수행한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwStartOffset	: [IN] start offset
 * @param		dwSize			: [IN] init size in byte
 *										cluster size 초과 분에 대해서는 무시된다.
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information for IO request
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_InitPartialCluster(FatVolInfo* pVI, t_uint32 dwCluster, 
				t_int32 dwStartOffset, t_int32 dwSize, 
				FFatCacheFlag dwFlag, FFatCacheInfo* pCI, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwStartOffset < 0) || (dwSize < 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_INITPARTIALCLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_initPartialCluster(pVI, dwCluster, dwStartOffset, dwSize,dwFlag, pCI);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Cluster에 data를 read/write 한다. (user data 전용)
 *
 * 주의 !!!
 * 반드시 cluster 단위로 사용하여야 한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		pBuff			: [IN] buffer pointer
 * @param		dwCount			: [IN] cluster count to write
 * @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information for IO request
 * @param		pCxt			: [IN] context of current operation
 * @return		positive		: read/written cluster count
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
t_int32
FFATFS_ReadWriteCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_int8* pBuff,
				t_int32 dwCount, t_boolean bRead, FFatCacheFlag dwFlag,
				FFatCacheInfo* pCI, ComCxt* pCxt)
{
	FFatErr		r;
	FFatErr		rr;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pBuff == 0) || (dwCount < 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_READWRITECLUSTER

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_readWriteCluster(pVI, dwCluster, pBuff, dwCount, dwFlag, pCI, bRead);

	rr = FFATFS_UnLock(pCxt);
	IF_UK (rr < 0)
	{
		return rr;
	}

	return r;
}


/**
 * 하나의 cluster에 대해 일부 영역에 data를 write 한다.
 *
 * 주의 !!!
 * 반드시 1개의 cluster에 대해서만 사용해야 한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwStartOffset	: [IN] start offset
 * @param		dwSize			: [IN] read write size in byte
 * @param		pBuff			: [IN] buffer for read/write
 * @param		bRead			: [IN]	FFAT_TRUE : read
 *										FFAT_FALSE: write
 * @param		dwFlag			: [IN] cache flag
 * @param		pCI				: [IN] cache information for IO request
 * @param		pCxt			: [IN] context of current operation
 * @param		sectors			: [IN] check Cluster-unaligned-Read
 * @return		0 or above		: read/written byte
 * @return		else			: error
 * @author		DongYoung Seo 
 * @version		AUG-23-2006 [DongYoung Seo] First Writing.
 */
//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
t_int32
FFATFS_ReadWritePartialCluster(FatVolInfo* pVI, t_uint32 dwCluster, 
				t_int32 dwStartOffset, t_int32 dwSize, t_int8* pBuff,
				t_boolean bRead, FFatCacheFlag dwFlag, FFatCacheInfo* pCI, ComCxt* pCxt, t_boolean sectors)
{
	FFatErr		r;
	FFatErr		rr;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwStartOffset < 0) || (dwSize < 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_READWRITEPARTIALCLUSTER

	VI_SET_CXT(pVI, pCxt);
	//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
	r = ffat_fs_fat_readWritePartialCluster(pVI, dwCluster, dwStartOffset, 
						dwSize, pBuff, dwFlag, pCI, bRead, sectors);
	FFAT_EO(r, (_T("fail to read/write partial cluster")));

out:
	rr = FFATFS_UnLock(pCxt);
	IF_UK (rr < 0)
	{
		return rr;
	}

	return r;
}



/**
* Check the FAT Sector is whole free or not
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] FAT sector number
* @param		dwFlag			: [IN] cache flag
* @return		FFAT_TRUE		: It is a free FAT Sector
* @return		FFAT_FALSE		: It is not a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_IsFreeFatSector(FatVolInfo* pVI, t_uint32 dwSector,
						FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r, rr;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_ISFREEFATSECTOR

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_isFreeFatSector(pVI, dwSector, dwFlag);

	rr = FFATFS_UnLock(pCxt);
	FFAT_ER(rr, (_T("fail to unlock FFATFS")));

	return r;
}


/**
* Get the first cluster of a FAT sector
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] FAT Sector number
* @param		pdwClusterNo	: [IN/OUT] cluster number storage
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_GetFirstClusterOfFatSector(FatVolInfo* pVI, t_uint32 dwSector,
									t_uint32* pdwClusterNo)
{

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_GETFIRSTCLUSTEROFFATSECTOR

	return ffat_fs_fat_getFirstClusterOfFatSector(pVI, dwSector, pdwClusterNo);
}


/**
* Get the last cluster of a FAT sector
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] FAT Sector number
* @param		pdwClusterNo	: [IN/OUT] cluster number storage
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_GetLastClusterOfFatSector(FatVolInfo* pVI, t_uint32 dwSector, t_uint32* pdwClusterNo)
{
#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK ((dwSector < VI_FFS(pVI)) || (dwSector >= (VI_FFS(pVI) + VI_FSC(pVI))))
	{
		FFAT_LOG_PRINTF((_T("Invalid sector number")));
		FFAT_ASSERT(0);
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_GETLASTCLUSTEROFFATSECTOR

	return ffat_fs_fat_getLastClusterOfFatSector(pVI, dwSector, pdwClusterNo);
}



/**
* Get free cluster count for a FAT Sector
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] a FAT Sector number
* @param		pdwClusterCount	: [IN/OUT] cluster number storage
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: It is a free FAT Sector
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFATFS_GetFCCOfSector(FatVolInfo* pVI, t_uint32 dwSector,
						t_uint32* pdwClusterCount, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETFCCOFSECTOR

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_fat_getFCCOfSector(pVI, dwSector, pdwClusterCount);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


//=============================================================================
//
//	API FOR DIRECTORY ENTRY
//


/**
* FAT16의 root directory에 data를 write 한다.
*
* 주의 !!!
* 반드시 FAT16의 root directory에만 사용하여야한다.
*
* @param		pVI				: [IN] volume pointer
* @param		dwOffset		: [IN] IO start offset
* @param		pBuff			: [IN] buffer for read/write
* @param		dwSize			: [IN] IO size in byte
* @param		dwFlag			: [IN] cache flag
* @param		bRead			: [IN]	FFAT_TRUE : Write
*										FFAT_FALSE : Read
* @param		pNode			: [IN] node pointer for file level flush, may be NULL
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo 
* @version		AUG-23-2006 [DongYoung Seo] First Writing.
* @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
*/
FFatErr
FFATFS_ReadWriteOnRoot(FatVolInfo* pVI, t_int32 dwOffset, t_int8* pBuff,
						t_int32 dwSize, FFatCacheFlag dwFlag, t_boolean bRead,
						void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (dwOffset < 0) || (dwSize < 0) || (pBuff == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (FFATFS_IS_FAT16(pVI) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter, This function can not work on FAT32")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwOffset >= 0);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwSize > 0);
	FFAT_ASSERT(FFATFS_IS_FAT16(pVI) == FFAT_TRUE);
	FFAT_ASSERT(dwFlag & FFAT_CACHE_DATA_DE);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_READWRITEONROOT

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_de_readWriteOnFat16Root(pVI, dwOffset, pBuff, dwSize,
						dwFlag, bRead, pNode);
	IF_LK (r == dwSize)
	{
		r = FFAT_OK;
	}
	else
	{
		FFAT_ASSERT(r < 0);
		FFAT_EO(r, (_T("fail to read/write on FAT16 Root cluster ")));
	}

out:
	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * get directory entry for a node
 *
 * @param		pVI			: [IN] volume pointer
 * @param		pNodeDE		: [IN/OUT] directory entry information for a node.
 *								pNodeDE->dwStartOffset	: lookup start offset
 *								pNodeDE->dwStartCluster	: cluster of dwStartOffset, 
 *													may be 0 when there is no cluster information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success, DEs for a node is stored at pNodeDE
 * @return		FFAT_EEOF	: end of file, no more entry
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GetNodeDirEntry(FatVolInfo* pVI, FatGetNodeDe* pNodeDE, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pNodeDE == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pNodeDE);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETNODEDIRENTRY

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_de_getNodeDE(pVI, pNodeDE);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * get directory entry for a node
 *
 * THIS FUNCTION DOES NOT NEED ANY LOCK !!
 *
 * @param		pVI			: [IN] volume pointer
 * @param		pDE			: [IN] directory entry pointer
 * @param		dwEntryCount: [IN] entry count
 * @param		psName		: [IN/OUT] Name storage
 *								storage should be or over 256 chars with FAT_GEN_NAME_LFN
 *								storage should be or over 13 chars with FAT_GEN_NAME_LFN
 * @param		pdwLen		: [OUT] Name length
 * @param		dwFlag		: [IN] Name generation flag
 *								FAT_GEN_NAME_LFN : generate long file name
 *													SFNE일 경우 SFN 생성.
 *								FAT_GEN_NAME_SFN : generate short file name.
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter,
 *									directory has invalid name character
 * @return		else			: error
 * @return		FFAT_EINVALID	: invalid parameter, invalid flag
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-08-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_GenNameFromDirEntry(FatVolInfo* pVI, FatDeSFN* pDE, t_int32 dwEntryCount,
							t_wchar* psName, t_int32* pdwLen, FatGenNameFlag dwFlag)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pDE == NULL) || (dwEntryCount <= 0) || (psName == NULL) ||
		(pdwLen == NULL) || ((dwFlag & FAT_GEN_NAME_MASK) == 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pDE);
	FFAT_ASSERT(dwEntryCount);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pdwLen);
	FFAT_ASSERT(dwFlag & FAT_GEN_NAME_MASK);

	_STATISTIC_GENNAMEFROMDIRENTRY

	if (dwFlag & FAT_GEN_NAME_LFN)
	{
#ifdef FFAT_VFAT_SUPPORT
		r = ffat_fs_de_genLongFileName(pVI, (FatDeLFN*)pDE, dwEntryCount, psName, pdwLen);
#else
		r = FFAT_EINVALID;
#endif
	}
	else if (dwFlag & FAT_GEN_NAME_SFN)
	{
		r = ffat_fs_de_genShortFileName(pVI, &pDE[(dwEntryCount -1)], psName, pdwLen);
	}
	else
	{
		r = FFAT_EINVALID;
	}

	return r;
}


/**
 * adjust name to FAT filesystem format
 * 입력된 이름을 FAT filesytem에 맞는 형태로 변경한다.
 * 입력된 이름을 FAT filesystem에서 사용할 수 있는지 점검한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		psName			: [IN] Name string, this string will be modified
 * @param		pdwLen			: [IN/OUT] character count
 *									[IN] character count before modification
 *										if 0 : there is no length information
 *									[OUT] character count after modification
 * @param		pdwNamePartLen	: [OUT] character count of name part of long filename
 * @param		pdwExtPartLen	: [OUT] character count of extension part of long filename
 * @param		pdwSfnNameSize	: [OUT] byte size of name part of short filename
 * @param		pdwType			: [OUT] Name type
 * @param		pDE				: [IN/OUT] short file name entry
 *									[IN] storage pointer
 *									[OUT] generated short file name
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-09-2006 [DongYoung Seo] First Writing., comes from TFS4 1.6
 */
FFatErr
FFATFS_AdjustNameToFatFormat(FatVolInfo* pVI, t_wchar* psName, t_int32* pdwLen,
					t_int32* pdwNamePartLen, t_int32* pdwExtPartLen, t_int32* pdwSfnNameSize,
					FatNameType* pdwType, FatDeSFN* pDE)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (psName == NULL) || (pdwLen == NULL) ||
		(pdwNamePartLen == NULL) || (pdwExtPartLen == NULL) ||
		(pdwSfnNameSize == NULL) || (pdwType == NULL) || (pDE == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (*pdwLen < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid name length")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_ADJUSTNAMETOFATFORMAT

	return ffat_fs_de_adjustNameToFatFormat(pVI, psName, pdwLen,
						pdwNamePartLen, pdwExtPartLen, pdwSfnNameSize, pdwType, pDE);
}


/**
* remove trailing dots and spaces
*
* @param		psName			: [IN] Name string, this string will be modified
* @param		pdwLen			: [IN/OUT] character count
*									[IN] character count before modification
*										if 0 : there is no length information
*									[OUT] character count after modification
* @return		FFAT_OK			: success
* @return		FFAT_EINVALID	: Invalid parameter or invalid name
* @author		DongYoung Seo (dy76.seo@samsung.com)
* @version		SEP-19-2008 [DongYoung Seo] First Writing
* @version		MAR-23-2009 [DongYoung Seo] treat error for ffat_fs_de_removeTrailingDotAndBlank()
*/
FFatErr
FFATFS_RemoveTrailingDotAndBlank(t_wchar* psName, t_int32* pdwLen)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((psName == NULL) || (pdwLen == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (*pdwLen < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid name length")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_REMOVETRAILINGDOTSANDBLANKS

	return ffat_fs_de_removeTrailingDotAndBlank(psName, pdwLen);
}


/**
 * Retrieve numeric tail from SFN Entry
 * SFN에서 Numeric Tail을 추출하여 return한다.
 *
 * @param		pDE			: [IN] short file name entry
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-14-2006 [DongYoung Seo] First Writing., 
 */
t_int32
FFATFS_GetNumericTail(FatDeSFN* pDE)
{

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_GETNUMERICTAIL

	return ffat_fs_de_getNumericTail(pDE);
}


/**
 * update time at directory entry structure
 *
 * @param		pDE			: [IN/OUT] short file name entry
 * @param		dwFlag		: [IN] update flag
 * @param		pTime		: [IN] Time
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing., 
 */
FFatErr
FFATFS_SetDeTime(FatDeSFN* pDE, FatDeUpdateFlag dwFlag, FFatTime* pTime)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_SETDETIME

	return ffat_fs_de_setDeTime(pDE, dwFlag, pTime);
}


/**
 * update size at directory entry structure
 *
 * @param		pDE			: [IN/OUT] short file name entry
 * @param		dwSize		: [IN] update flag
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_SetDeSize(FatDeSFN* pDE, t_uint32 dwSize)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_SETDESIZE

	return ffat_fs_de_setDeSize(pDE, dwSize);
}


/**
 * update cluster number at directory entry structure
 *
 * @param		pDE			: [IN/OUT] short file name entry
 * @param		dwCluster	: [IN] cluster number
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_SetDeCluster(FatDeSFN* pDE, t_uint32 dwCluster)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_SETDECLUSTER

	return ffat_fs_de_setDeCluster(pDE, dwCluster);
}


/**
 * update attribute at directory entry structure
 *
 * @param		pDE			: [IN/OUT] short file name entry
 * @param		bAttr		: [IN] FAT DE Attribute
 * @return		positive	: valid numeric tail
 * @return		0			: there is no numeric tail
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-24-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_SetDeAttr(FatDeSFN* pDE, t_uint8 bAttr)
{
#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	_STATISTIC_SETDEATTR

	return ffat_fs_de_setDeAttr(pDE, bAttr);
}


/**
 * generate check-sum from SFNE
 *
 * @param		pDE			: [IN] short file name entry
 * @return		check sum
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First Writing., 
 */
t_uint8
FFATFS_GetCheckSum(FatDeSFN* pDE)
{
#ifdef FFAT_STRICT_CHECK

	//FFATFS_CHECK_INIT_RETURN();

	IF_UK (pDE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return 0;
	}
#endif

	_STATISTIC_GETCHECKSUM

	FFAT_ASSERT(pDE);

	return ffat_fs_de_genChecksum(pDE);
}


#ifdef FFAT_VFAT_SUPPORT
	/**
	 * generate LFNE from name
	 *
	 * @param		psName			: [IN] Name string
	 * @param		wNameLen		: [IN] name length
	 * @param		pDE				: [IN] directory entry
	 * @param		pdwEntryCount	: [IN/OUT] directory entry count
	 *									filled LFN entry count
	 * @param		bCheckSum		: [IN] check sum
	 * @return		check sum
	 * @author		DongYoung Seo (dy76.seo@samsung.com)
	 * @version		AUG-16-2006 [DongYoung Seo] First Writing., 
	 */
	FFatErr
	FFATFS_GenLFNE(t_wchar* psName, t_int16 wNameLen, FatDeLFN* pDE, t_int32* pdwEntryCount, t_uint8 bCheckSum)
	{

	#ifdef FFAT_STRICT_CHECK

		FFATFS_CHECK_INIT_RETURN();

		IF_UK ((psName == NULL) || (wNameLen < 0) || (wNameLen > FFAT_NAME_MAX_LENGTH) 
				|| (pDE == NULL) || (pdwEntryCount == NULL))
		{
			FFAT_LOG_PRINTF((_T("Invalid parameter")));
			return FFAT_EINVALID;
		}
	#endif

		_STATISTIC_GENLFNE

		return ffat_fs_de_genLFNE(psName, wNameLen, pDE, pdwEntryCount, bCheckSum);
	}

	/**
	* find the long file name entries in cluster before SFNE
	*
	*
	* @param	pVI					: [IN] volume pointer
	* @param	dwCluster			: [IN] cluster number
	* @param	dwStartOffset		: [IN] start offset to scan
	*									This is the relative offset from the start cluster of parent directory.
	* @param	bCheckSum			: [IN] checksum of SFNE
	* @param	pbPrevLFNOrder		: [IN/OUT] previous order of LFNE
	* @param	pdwFoundLFNECount	: [OUT] count of the founded LFNE in current cluster
	*									(internally initialized to 0)
	* @param	pCxt				: [IN] context of current operation
	* @return	FFAT_OK				: LFN is found but partial LFNE set. need to scan previous cluster also
	* @return	FFAT_DONE			: LFN is found fully.
	* @return	FFAT_ENOENT			: LFN is not found
	* @return	else				: error
	* @author	JeongWoo Park
	* @version	FEB-12-2009 [JeongWoo Park] First Writing.
	*/
	FFatErr
	FFATFS_FindLFNEsInCluster(FatVolInfo* pVI, t_uint32 dwCluster, t_uint32 dwStartOffset,
							t_uint8 bCheckSum, t_uint8* pbPrevLFNOrder, t_uint32* pdwFoundLFNECount,
							ComCxt* pCxt)
	{
		FFatErr		r;

	#ifdef FFAT_STRICT_CHECK
		FFATFS_CHECK_INIT_RETURN();

		IF_UK ((pVI == NULL) ||
				(pbPrevLFNOrder == NULL) ||
				(pdwFoundLFNECount == NULL))
		{
			FFAT_LOG_PRINTF((_T("Invalid parameter")));
			return FFAT_EINVALID;
		}
	#endif

		FFAT_ASSERT(pVI);
		FFAT_ASSERT(pbPrevLFNOrder);
		FFAT_ASSERT(pdwFoundLFNECount);

		r = FFATFS_Lock(pCxt);
		FFAT_ER(r, (_T("fail to get lock")));

		_STATISTIC_FINDLFNINCLUSTER

		VI_SET_CXT(pVI, pCxt);

		r = ffat_fs_de_findLFNEsInCluster(pVI, dwCluster, dwStartOffset, bCheckSum,
										pbPrevLFNOrder, pdwFoundLFNECount, pCxt);

		r |= FFATFS_UnLock(pCxt);

		return r;
	}
#endif	//#ifdef FFAT_VFAT_SUPPORT


/**
 * write directory entry
 *
 * write 이전에 반드시 충분한 cluster가 확보 되어야 한다.
 *
 * @param		pVI			: [IN] volume information
 * @param		dwCluster	: [IN] first write cluster
 *									FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
 * @param		dwOffset	: [IN] write start offset from dwCluster
 * @param		pBuff		: [IN] write data
 * @param		dwSize		: [IN] write size in byte
 * @param		dwFlag		: [IN] cache flag
 * @param		pNode		: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt		: [IN] context of current operation
 * @return		check sum
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-28-2006 [DongYoung Seo] First Writing
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 * @version		DEC-20-2008 [DongYoung Seo] change function operation to use offset over cluster size
 */
FFatErr
FFATFS_WriteDE(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwOffset, t_int8* pBuff,
						t_int32 dwSize, FFatCacheFlag dwFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pBuff == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwSize >= 0);
	FFAT_ASSERT(dwOffset >= 0);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_WRITEDE

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_de_write(pVI, dwCluster, dwOffset, pBuff, dwSize, dwFlag, pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * delete directory entry
 *
 *
 * @param		pVI				: [IN] volume information
 * @param		dwCluster		: [IN] first write cluster
 *									FFATFS_FAT16_ROOT_CLUSTER(1) : root directory of FAT16
 * @param		dwOffset		: [IN] write start offset
 * @param		dwCount			: [IN] delete entry count
 * @param		bLookupDelMark	: [IN] flag for looking up deletion mark for efficiency.deletion mark
										FFAT_TRUE : check directory entry to set it 0x00(FAT_DE_END_OF_DIR)
										FFAT_FALS : write 0xE5(FAT_DE_FREE) at the head of entry
 * @param		dwFlag			: [IN] cache flag
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation 
 * @return		FFAT_OK			: Success
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-31-2006 [DongYoung Seo] First Writing
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_DeleteDE(FatVolInfo* pVI, t_uint32 dwCluster, t_int32 dwOffset,
					t_int32 dwCount, t_boolean bLookupDelMark,
					FFatCacheFlag dwFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(dwCount >= 1);
	FFAT_ASSERT(dwOffset >= 0);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_DELETEDE

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_de_delete(pVI, dwCluster, dwOffset, dwCount, bLookupDelMark,
					(dwFlag | FFAT_CACHE_DATA_DE), pNode);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


//=============================================================================
//
//	Utility functions
//




// ============================================================================
//
//	MISC functions
//

/**
 * get volume name
 *
 * @param		pVI				: [IN] volume information
 * @param		psVolLabel		: [OUT] volume name storage
 * @param		dwVolLabelLen	: [IN] character count that can be stored at psVolLabel
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
FFATFS_GetVolumeLabel(FatVolInfo* pVI, t_wchar* psVolLabel, t_int32 dwVolLabelLen, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (psVolLabel == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(psVolLabel);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_GETVOLUMENAME

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_de_getVolLabel(pVI, psVolLabel, dwVolLabelLen);
	if (r == FFAT_ENOENT)
	{
		// get volume name from boot sector.
		r = ffat_fs_bs_getVolLabel(pVI, psVolLabel, dwVolLabelLen);
	}

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * set volume name to psVolLabel
 *
 * @param		pVI				: [IN] volume information
 * @param		psVolLabel		: [IN] new volume name
 *									NULL or zero length : remove volume name
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 * @version		NOV-21-2008 [DongYoung Seo] remove volume label change on boot sector
 *										Windows ignores volume label on boot sector
 */
FFatErr
FFATFS_SetVolumeLabel(FatVolInfo* pVI, t_wchar* psVolLabel, ComCxt* pCxt)
{
	FFatErr			r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_SETVOLUMENAME

	VI_SET_CXT(pVI, pCxt);

	if ((psVolLabel != NULL) && (FFAT_WCSLEN(psVolLabel) > 0))
	{
		r = ffat_fs_de_setVolLabel(pVI, psVolLabel, FFAT_FALSE);
	}
	else
	{
		r = ffat_fs_de_removeVolLabel(pVI);
	}

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Check the boot sector is valid or not
 *
 * lock이 필요할 경우 각각의 command를 처리하는 부분에서 수행하도록 한다.
 *
 * @param		pBootSector		: [IN] boot sector storage.
 * @param		FFAT_OK			: the buffer is a boot sector
 * @param		FFAT_EINVALID	: the buffer is not a boot sector
 * @author		DongYoung Seo
 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_IsValidBootSector(t_int8* pBootSector)
{

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pBootSector == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pBootSector);

	_STATISTIC_ISVALIDBOOTSECTOR

	return ffat_fs_bs_isValidBootSector(pBootSector);
}


/**
 * FFATFS control
 *
 * lock이 필요할 경우 각각의 command를 처리하는 부분에서 수행하도록 한다.
 *
 * @param		dwCmd		: filesystem control command
 * @param		pParam0		: parameter 0
 * @param		pParam1		: parameter 1
 * @param		pParam2		: parameter 2
 * @author		DongYoung Seo
 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFATFS_FSCtl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2)
{

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();
#endif

	_STATISTIC_FSCTL

	return ffat_fs_fsctl(dwCmd, pParam0, pParam1, pParam2);
}


/**
* read/write full sectors
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] sector number
* @param		dwCount			: [IN] sector count
*										size 가 sector 크기를 초과할 경우 무시 된다.
* @param		pBuff			: [IN] read/write data storage
* @param		dwFlag			: [IN] cache flag
* @param		pCI				: [IN] cache information
* @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
* @param		pCxt			: [IN] context of current operation
* @return		positive		: read sector count
* @return		negative		: error
* @author		DongYoung Seo
* @history		MAY-26-2007 [DongYoung Seo] First write
*/
t_int32
FFATFS_ReadWriteSectors(FatVolInfo* pVI, t_uint32 dwSector, t_int32 dwCount,
						t_int8* pBuff, FFatCacheFlag dwFlag, FFatCacheInfo* pCI,
						t_boolean bRead, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pBuff == NULL) || (pCI == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT(dwCount > 0);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_READWRITESECTORS

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_misc_readWriteSectors(pVI, dwSector, dwCount ,pBuff, dwFlag, pCI, bRead);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
* 하나의 sector에 대해 일부 영역의 read/write를 수행한다.
*
* @param		pVI				: [IN] volume pointer
* @param		dwSector		: [IN] sector number
* @param		dwStartOffset	: [IN] start offset
* @param		dwSize			: [IN] read write size in byte
*										size 가 sector 크기를 초과할 경우 무시 된다.
* @param		pBuff			: [IN] read/write data storage
* @param		dwFlag			: [IN] cache flag
* @param		pCI				: [IN] cache information
* @param		bRead			: [IN] FFAT_TRUE : read, FFAT_FALSE : write
* @param		pCxt			: [IN] context of current operation
* @return		positive		: read size
* @return		negative		: error
* @author		DongYoung Seo 
* @history		MAY-26-2007 [DongYoung Seo] first write
*/
t_int32
FFATFS_ReadWritePartialSector(FatVolInfo* pVI, t_uint32 dwSector, 
						t_int32 dwStartOffset, t_int32 dwSize, t_int8* pBuff, 
						FFatCacheFlag dwFlag, FFatCacheInfo* pCI,t_boolean bRead,
						ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	FFATFS_CHECK_INIT_RETURN();

	IF_UK ((pVI == NULL) || (pBuff == NULL) || (pCI == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVI);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT(dwSize > 0);

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	_STATISTIC_READWRITEPARTIALSECTOR

	VI_SET_CXT(pVI, pCxt);

	r = ffat_fs_misc_readWritePartialSector(pVI, dwSector, dwStartOffset, dwSize ,pBuff, dwFlag, pCI, bRead);

	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * deallocate clusters
 * Cluster의 할당을 해제 한다.
 * backward 방식으로 해제를 수행한다.
 *
 * @param		pVI				: [IN] volume pointer
 * @param		dwCount			: [IN] cluster deallocation count
 * @param		pAlloc			: [IN] allocated cluster information
 *									deallocated 될 모든 cluster 정보가 포함되어 있지 않을 수 있다.
 *									pAlloc->pVC 가 NULL 일 수도 있다.
 *										(이때는 반드시 pAlloc->dwFirstCluster가 valid 한 값을 가져야한다.)
 * @param		pdwDeallocCount	: [IN] deallocated cluster count
 * @param		dwFAFlag		: [IN] deallocation flag
 *									FAT_DEALLOCATE_FORCE : force deallocate
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pNode			: [IN] node pointer for file level flush, may be NULL
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		Soojeong Kim
 * @version		AUG-27-2007 [Soojeong Kim] First Writing.
 * @version		NOV-29-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_FreeClusterVC(FatVolInfo* pVI, FFatVC* pVC,
						t_uint32* pdwFreeCount, FatAllocateFlag dwFAFlag,
						FFatCacheFlag dwCacheFlag, void* pNode, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK

	FFATFS_CHECK_INIT_RETURN();

	IF_UK (pVI == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to get lock")));

	VI_SET_CXT(pVI, pCxt);

	_STATISTIC_FREECLUSTERVC

	r = ffat_fs_fat_freeClustersVC(pVI, pVC, pdwFreeCount, dwFAFlag, dwCacheFlag, pNode);
	FFAT_EO(r, (_T("fail to deallocate cluster ")));

out:
	r |= FFATFS_UnLock(pCxt);

	return r;
}


/**
 * Lock FFATFS
 *
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		else			: Error
 * @author		DongYoung Seo
 * @version		DEC-19-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_Lock(ComCxt* pCxt)
{
	_STATISTIC_LOCK

	return FFATFS_LOCK(pCxt);
}


/**
 * UnLock FFATFS
 *
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		else			: Error
 * @author		DongYoung Seo
 * @version		DEC-19-2007 [DongYoung Seo] add pNode parameter for file level flush
 */
FFatErr
FFATFS_UnLock(ComCxt* pCxt)
{
	_STATISTIC_UNLOCK

	return FFATFS_UNLOCK(pCxt);
}


// ============================================================================
//
//	Static Function Part
//

//
//	End of Static function part
//
// ============================================================================


// debug begin
// ============================================================================
//
//	DEBUG PART
//

#ifdef FFAT_DEBUG

	FFatErr
	FFATFS_ChangeFatTableFAT32(FatVolInfo* pVI)
	{
		return ffat_fs_fat_changeFAT32FatTable(pVI);
	}


	static void
	_statisticsPrint(void)
	{
		FFAT_DEBUG_PRINTF((_T(" =============================================================\n")));
		FFAT_DEBUG_PRINTF((_T(" ============    FFATFS STATISTICS   =========================\n")));
		FFAT_DEBUG_PRINTF((_T(" =============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_Init(): ", _STATISTICS()->dwInit));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_Terminate(): ", _STATISTICS()->dwTerminate));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_Mount(): ", _STATISTICS()->dwMount));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_IsValidBootSector(): ", _STATISTICS()->dwIsValidBootSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SyncNode(): ", _STATISTICS()->dwSyncNode));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SyncVol(): ", _STATISTICS()->dwSyncVol));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_FlushVol(): ", _STATISTICS()->dwFlushVol));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_AddCache(): ", _STATISTICS()->dwAddCache));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_RemoveCache(): ", _STATISTICS()->dwRemoveCache));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_RegisterCacheCallback(): ", _STATISTICS()->dwRegisterCacheCallback));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ChkCache(): ", _STATISTICS()->dwChkCache));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetClusterOfOffset(): ", _STATISTICS()->dwGetClusterOfOffset));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetNextCluster(): ", _STATISTICS()->dwGetNextCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetLastCluster(): ", _STATISTICS()->dwGetLastCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetFreeClusters(): ", _STATISTICS()->dwGetFreeClusters));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetFreeClustersFromTo(): ", _STATISTICS()->dwGetFreeClustersFromTo));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_MakeClusterChain(): ", _STATISTICS()->dwMakeClusterChain));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_MakeClusterChainVC(): ", _STATISTICS()->dwMakeClusterChainVC));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_IncFreeClusterCount(): ", _STATISTICS()->dwIncFreeCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_DecFreeClusterCount(): ", _STATISTICS()->dwDecFreeCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetVectoredCluster(): ", _STATISTICS()->dwGetVectoredCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_AllocateCluster(): ", _STATISTICS()->dwAllocateCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_DeallocateCluster(): ", _STATISTICS()->dwDeallocateCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_DeallocateClusterFromTo(): ", _STATISTICS()->dwDeallocateClusterFromTo));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_FreeClusters(): ", _STATISTICS()->dwFreeClusters));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_UpdateCluster(): ", _STATISTICS()->dwUpdateCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_UpdateVolumeStatus(): ", _STATISTICS()->dwUpdateVolumeStatus));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_IsFreeFatSector(): ", _STATISTICS()->dwIsFreeFatSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetFirstClusterOfFatSector(): ", _STATISTICS()->dwGetFirstClusterOfFatSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetLastClusterOfFatSector(): ", _STATISTICS()->dwGetLastClusterOfFatSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetFCCOfSector(): ", _STATISTICS()->dwGetFCCOfSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetNodeDirEntry(): ", _STATISTICS()->dwGetNodeDirEntry));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GenNameFromDirEntry(): ", _STATISTICS()->dwGenNameFromDirEntry));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_AdjustNameToFatFormat(): ", _STATISTICS()->dwAdjustNameToFatFormat));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetNumericTail(): ", _STATISTICS()->dwGetNumericTail));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SetDeTime(): ", _STATISTICS()->dwSetDeTime));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SetDeSize(): ", _STATISTICS()->dwSetDeSize));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SetDeAttr(): ", _STATISTICS()->dwSetDeAttr));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SetDeCluster(): ", _STATISTICS()->dwSetDeCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetCheckSum(): ", _STATISTICS()->dwGetCheckSum));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GenLFNE(): ", _STATISTICS()->dwGenLFNE));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_FindLongFileNameInCluster(): ", _STATISTICS()->dwFindLFNInCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ReadWriteOnRoot(): ", _STATISTICS()->dwReadWriteOnRoot));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_WriteDE(): ", _STATISTICS()->dwWriteDE));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_DeleteDE(): ", _STATISTICS()->dwDeleteDE));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_SetVolumeLabel(): ", _STATISTICS()->dwSetVolumeLabel));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_GetVolumeLabel(): ", _STATISTICS()->dwGetVolumeLabel));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_FSCtl(): ", _STATISTICS()->dwFSCtl));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_InitCluster(): ", _STATISTICS()->dwInitCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_InitPartialCluster(): ", _STATISTICS()->dwInitPartialCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ReadWriteCluster(): ", _STATISTICS()->dwReadWriteCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ReadWritePartialCluster(): ", _STATISTICS()->dwReadWritePartialCluster));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ReadWriteSectors(): ", _STATISTICS()->dwReadWriteSectors));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_ReadWritePartialSector(): ", _STATISTICS()->dwReadWritePartialSector));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_FreeClusterVC(): ", _STATISTICS()->dwFreeClusterVC));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_Lock(): ", _STATISTICS()->dwLock));
		FFAT_DEBUG_PRINTF((_T(" %40s %d\n"), "FFATFS_UnLock(): ", _STATISTICS()->dwUnLock));

		FFAT_DEBUG_PRINTF((_T(" =============================================================\n")));
	}

#endif /* #ifdef FFAT_DEBUG */

//
// End of debug part
//
// ============================================================================
// debug end







