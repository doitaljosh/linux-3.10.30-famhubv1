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
 * @file		ffat_addon_hpa.c
 * @brief		Hidden Protected Area Module for FFAT
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		MAY-23-2007 [DongYoung Seo] First writing
 * @see			None
 */


/*
	HPA design principal

	1. The last cluster is HPA information
	2. The last cluster - 1 is HPA Root directory
	3. from ((last cluster - 1) - Full HPA bitmap size) to (Full HPA bitmap size) is Full HPA bitmap area
	4. from (Full HPA bitmap size) - Cluster bitmap size) to (Cluster bitmap size) is Cluster bitmap area
	4. the FAT sectors which cover clusters from the Cluster bitmap to the Last Cluster should be free to create HPA
	5. the above FAT sectors are occupied by HPA while creation.
	6. Can not rename from HPA to normal area, or from normal area to HPA

	clusters	|Cluster Bitmap      |last - 1 - Full HPA bitmap size l      last - 1    | The last|
	================================================================================================
	............|Cluster Bitmap AREA |    Full HPA BITMAP AREA        |HPA Root Directory|HPA Info |
	================================================================================================
*/

#include "ess_math.h"
#include "ess_bitmap.h"

#include "ffat_common.h"

#include "ffat_node.h"
#include "ffat_vol.h"
#include "ffat_dir.h"
#include "ffat_share.h"
#include "ffat_misc.h"
#include "ffatfs_api.h"

#include "ffat_addon_types_internal.h"
#include "ffat_addon_hpa.h"
#include "ffat_addon_misc.h"
#include "ffat_addon_fcc.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_HPA)

//#define _HPA_DEBUG			// enable debug and checking

// MACROS
#define _MAIN()					((HPAMain*)(&_HPAMain))

#define _CONFIG()				(&(ffat_al_getConfig()->stHPA))		// get HPA config
#define _INFO(_pVol)			(VOL_ADDON(_pVol)->pHPA)			// get HPA info
#define _BITMAP_FHPA(_pVol)		(_INFO(pVol)->pBitmapFullHPA)		// get Full HPA bitmap pointer
#define _BITMAP_PHPA(_pVol)		(_INFO(pVol)->pBitmapPartialHPA)	// get Partial HPA bitmap pointer
#define _FBITMAP_SIZE(_pVol)		ESS_MATH_CDB((VOL_LVFSFF(_pVol) - VOL_FFS(pVol) + 1), 8, 3)
																	// get bitmap size for a volume
																	// get byte count for bitmap
#define _CBITMAP_SIZE(_pVol)	ESS_MATH_CDB((VOL_LCN(_pVol) + 1), 8 , 3)
																	// get cluster bitmap size for a volume
																	// get byte count for cluster bitmap

#define _IS_ACTIVATED(_pVol)	(_INFO(_pVol) == NULL ? FFAT_FALSE : FFAT_TRUE)
																	// check HPA activated or not

#define _SET_NODE_HPA(_pNode)	((NODE_ADDON_FLAG(_pNode)) |= ADDON_NODE_HPA)

// check node is in HPA
#define _IS_NODE_AT_HPA(_pNode)	((NODE_ADDON_FLAG(_pNode) & ADDON_NODE_HPA) ? FFAT_TRUE: FFAT_FALSE)

// is valid total cluster count for HPA
#define _IS_VALID_HPA_TCC(_pInfo)	\
					((((_pInfo)->dwTotalClusterCount) == FFAT_FREE_CLUSTER_INVALID) ? \
									FFAT_FALSE : FFAT_TRUE)

#define _ROOT_PARENT_CLUSTER(_pVol)		NODE_C(VOL_ROOT(_pVol))
#define _ROOT_DE_START_OFFSET(_pInfo)	(t_int32)&(((HPAInfoSector*)0)->pHPARootDE)
#define _ROOT_DE_END_OFFSET(_pInfo)		(t_uint32)(_ROOT_DE_START_OFFSET(_pInfo) + \
											(((_pInfo)->dwEntryCount - 1) << FAT_DE_SIZE_BITS))

#define _INFO_FLAG(_pInfo)			((_pInfo)->dwFlag)					// flag
#define _INFO_IC(_pInfo)			((_pInfo)->dwInfoCluster)			// info cluster
#define _INFO_RC(_pInfo)			((_pInfo)->dwRootCluster)			// root cluster
#define _INFO_FSMC(_pInfo)			((_pInfo)->dwFSMCluster)			// FAT Sector Bitmap Cluster
#define _INFO_CMC(_pInfo)			((_pInfo)->dwCMCluster)				// Cluster Bitmap Cluster
#define _INFO_CMS(_pInfo)			((_pInfo)->dwCMSector)				// Cluster Bitmap Sector
#define _INFO_FCH(_pInfo)			((_pInfo)->dwFreeClusterHint)		// free cluster hint
#define _INFO_TCC(_pInfo)			((_pInfo)->dwTotalClusterCount)		// Total Cluster Count
#define _INFO_RDE(_pInfo)			((_pInfo)->pRootDe)					// Root Directory Entry
#define _INFO_REC(_pInfo)			((_pInfo)->dwEntryCount)			// Entry Count
#define _INFO_BITMAP_FHPA(_pInfo)	((_pInfo)->pBitmapFullHPA)				// Bitmap for Full HPA
#define _INFO_BITMAP_PHPA(_pInfo)	((_pInfo)->pBitmapPartialHPA)		// Bitmap for PartialHPA
#define _INFO_BITMAP_FREE(_pInfo)	((_pInfo)->pBitmapFree)				// Bitmap for Free FAT Sector
#define _INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(_pInfo)			\
									((_pInfo)->pBitmapUpdated)		// Bitmap for Free FAT Sector candidate
#define _INFO_FFSCN(_pInfo)			((_pInfo)->dwFreeFATSectorsNormal)	// Count of free FAT sectors on normal area
#define _INFO_LFHPAS(_pInfo)			((_pInfo)->dwLowestFullHPASector)			// The Lowest Full HPA Sector
#define _INFO_HFHPAS(_pInfo)		((_pInfo)->dwHighestFreeFullHPASector)		// The highest Free Full HPA Sector

// is valid Lowest Full HPA Sector 
#define _IS_VALID_LFHPAS(_pVol)		((_INFO_LFHPAS(_INFO(_pVol)) <= VOL_LVFSFF(_pVol)) ? FFAT_TRUE: FFAT_FALSE)			

// increase free FAT sector count in NORMAL area
#define _INFO_FFSCN_INC(_pInfo)		if (_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE)		\
									{												\
										_INFO_FFSCN(pInfo)++;						\
									}

// decrease free FAT sector count in NORMAL area
#define _INFO_FFSCN_DEC(_pInfo)		if (_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE)		\
									{												\
										_INFO_FFSCN(pInfo)--;						\
									}


// set/clear Full HPA bitmap
#define _SET_BITMAP_FHPA(_pVol, _dwS)	\
									ESS_BITMAP_SET(_INFO_BITMAP_FHPA(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
#define _CLEAR_BITMAP_FHPA(_pVol, _dwS)	\
									ESS_BITMAP_CLEAR(_INFO_BITMAP_FHPA(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
// set/clear Partial HPA bitmap
#define _SET_BITMAP_PHPA(_pVol, _dwS)	\
									ESS_BITMAP_SET(_INFO_BITMAP_PHPA(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
#define _CLEAR_BITMAP_PHPA(_pVol, _dwS)	\
									ESS_BITMAP_CLEAR(_INFO_BITMAP_PHPA(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
// set/clear FAT free bitmap
#define _SET_BITMAP_FREE_FOR_NORMAL_AREA(_pVol, _dwS)	\
									ESS_BITMAP_SET(_INFO_BITMAP_FREE(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
#define _CLEAR_BITMAP_FREE_FOR_NORMAL_AREA(_pVol, _dwS)	\
									ESS_BITMAP_CLEAR(_INFO_BITMAP_FREE(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
// set/clear FAT updated bitmap - for free FAT sector candidate
#define _SET_BITMAP_FREE_FAT_SECTOR_CANDIDATE(_pVol, _dwS)	\
									ESS_BITMAP_SET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))
#define _CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(_pVol, _dwS)	\
									ESS_BITMAP_CLEAR(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(_INFO(_pVol)), ((_dwS) - VOL_FFS(_pVol)))

#define VOL_CBCPFS(_pVol)			(VOL_CCPFS(pVol) >> 3)		// Cluster Bitmap Count Per FAT Sector
#define VOL_CBCPFS_MASK(_pVol)		(VOL_CBCPFS(pVol) -1)		// Cluster Bitmap Count Per FAT Sector Mask
#define VOL_CBCPS(_pVol)			(VOL_SS(_pVol) << 3)		// Cluster Bitmap Count Per Sector
#define VOL_CBCPS_MASK(_pVol)		(VOL_CBCPS(_pVol) -1)		// Cluster Bitmap Count Per Sector Mask

// cluster bitmap sector of the given cluster
#define _GET_CBITMAP_SECTOR_OF_CLUSTER(_pVol, _dwC)	(_INFO_CMS(_INFO(_pVol)) + ((_dwC >> VOL_SSB(_pVol)) >> 3))


// typedefs
typedef struct
{
	t_wchar*		psRootName;				//!< HPA root directory name
	t_int32			dwRootNameLen;			//!< the length of HPA root directory
	FFatHPAConfig*	pConf;					//!< configuration for HPA
	EssDList		dlFree;					//!< Free HPA Volume info list
} HPAMain;

// prototype for static functions
static FFatErr		_findHPA(Vol* pVol, ComCxt* pCxt);
static FFatErr		_getHPA(Vol* pVol, ComCxt* pCxt);
static t_uint32		_getInfoSectorNumber(Vol* pVol);
static FFatErr		_isValidHPAInfo(Vol* pVol, HPAInfoSector* pInfoSector, ComCxt* pCxt);
static FFatErr		_mountHPA(Vol* pVol, ComCxt* pCxt);

static FFatErr		_createHPA(Vol* pVol, ComCxt* pCxt);
static FFatErr		_writeInfoSector(Vol* pVol, HPAInfoSector* pInfoSector, ComCxt* pCxt);
static FFatErr		_createHPAUpdateFAT(Vol* pVol, Node* pNodeHPARoot, ComCxt* pCxt);

static FFatErr		_allocHPA(Vol* pVol, ComCxt* pCxt);
static FFatErr		_freeHPA(Vol* pVol, ComCxt* pCxt);

static FFatErr		_readBitmap(Vol* pVol, ComCxt* pCxt);
static FFatErr		_writeBitmap(Vol* pVol, t_uint32 dwFrom, t_uint32 dwTo, ComCxt* pCxt);
static FFatErr		_initClusterBitmap(Vol* pVol, ComCxt* pCxt);
static FFatErr		_propagateUpdateBitmap(Vol* pVol, ComCxt* pCxt);
static FFatErr		_syncUpdateBitForFatSector(Vol* pVol, t_uint32 dwFatSector, ComCxt* pCxt);

static FFatErr		_getFreeClustersNormal(Vol* pVol, t_uint32 dwCount, FFatVC* pVC,
							t_uint32 dwHint, t_uint32* pdwFreeCount, ComCxt* pCxt);
static FFatErr		_getFreeClustersHPA(Node* pNode, t_uint32 dwCount, FFatVC* pVC,
							t_uint32 dwHint, t_uint32* pdwFreeCount,
							FatAllocateFlag dwAllocFlag, ComCxt* pCxt);
static FFatErr		_getFreeClustersFromTo(Vol* pVol, t_uint32 dwHint,
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwFreeCount, ComCxt* pCxt);
static FFatErr		_getFreeClustersFromToHPA(Vol* pVol, t_uint32 dwPrevEOF,
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwAllocatedCount, t_boolean bHPA, ComCxt* pCxt);

static FFatErr		_enlargeHPA(Vol* pVol, t_uint32* pdwFatSector, ComCxt* pCxt);
static FFatErr		_releaseFreeHPASector(Vol* pVol, ComCxt* pCxt);

static FFatErr		_genHPARootDe(Vol* pVol, Node* pNodeHPARoot, ComCxt* pCxt);
static t_boolean	_isHPARoot(Node* pNode);

static FFatErr		_checkFreeFatSectorForHPACreate(Vol* pVol, ComCxt* pCxt);
static FFatErr		_updateLowestHPAFatSector(Vol* pVol);

static t_boolean	_isFATSectorFullHPA(Vol* pVol, t_uint32 dwSector);
static t_boolean	_isFATSectorPartialHPA(Vol* pVol, t_uint32 dwSector);

static void			_incTotalClusterHPA(Vol* pVol, t_uint32 dwCount, ComCxt* pCxt);
static void			_decTotalClusterHPA(Vol* pVol, t_uint32 dwCount);

static FFatErr		_updateVolumeInfo(Vol* pVol, ComCxt* pCxt);

static FFatErr		_sectorIO(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, ComCxt* pCxt);

static t_int32		_sectorIOReserved(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, FFatCacheInfo* pCI,
								FFatCacheFlag dwFlag);
static t_int32		_sectorIOFATHPA(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff);
static t_int32		_sectorIOFATNormal(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, FFatCacheInfo* pCI,
								FFatCacheFlag dwFlag, ComCxt* pCxt);
static t_int32		_sectorIOFAT(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, FFatCacheInfo* pCI,
								FFatCacheFlag dwFlag, ComCxt* pCxt);
static t_int32		_sectorIORootDir(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, 
								FFatCacheInfo* pCI, FFatCacheFlag dwFlag);
static t_int32		_sectorIOData(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
								t_int32 dwCount, t_int8* pBuff, FFatCacheInfo* pCI,
								FFatCacheFlag dwFlag, ComCxt* pCxt);

static t_int32		_sectorIOEraseWrapper(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
								FFatCacheFlag dwFlag, FFatCacheInfo* pCI);

static FFatErr		_initHPAInfoStorage(void);
static FFatErr		_terminateHPAInfoStorage(void);
static HPAInfo*		_getFreeHPAInfo(Vol* pVol);
static FFatErr		_releaseHPAInfo(Vol* pVol, HPAInfo* pInfo);

#ifdef FFAT_DYNAMIC_ALLOC
	static HPAInfo*		_getFreeHPAInfoDynamic(Vol* pVol);
	static FFatErr		_releaseHPAInfoDynamic(Vol* pVol, HPAInfo* pInfo);
#else
	static HPAInfo*		_getFreeHPAInfoStatic(void);
	static FFatErr		_releaseHPAInfoStatic(HPAInfo* pInfo);
#endif

static FFatErr		_updateUpdatedBitmapAlloc(Vol* pVol, FFatVC* pVC, t_uint32 dwClusterCount);
static FFatErr		_updateUpdatedBitmapDealloc(Vol* pVol, FFatVC* pVC, t_uint32 dwClusterCount);

static FFatErr		_getClusterBitmapSectorOfFatSector(Vol* pVol, t_uint32 dwSector, t_uint32* pdwCluster,
														t_uint32* pdwSector, t_uint32* pdwOffset);
static FFatErr		_getGetPartialHPAClusterCountOfFATSector(Vol* pVol, t_uint32 dwFatSector, t_uint32* pdwCount,
															ComCxt* pCxt);

static FFatErr		_updatePartialHPA(Vol* pVol, FFatVC* pVC, t_boolean bBitmapSet, FFatCacheFlag dwFlag, ComCxt* pCxt);

static FFatErr		_convertToPartialHPA(Vol* pVol, t_uint32 dwFatSector, ComCxt* pCxt);

// static variables
static HPAMain	_HPAMain;
//static t_int32	_HPAMain[sizeof(HPAMain) / sizeof(t_int32)];

// types for buffer cache IO
typedef t_int32		(*PFN_BC_IO)(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
								FFatCacheFlag dwFlag, FFatCacheInfo* pCI);

static PFN_BC_IO		_pfBCIO[3];			// functions pointers for fast buffer cache IO

static HPAInfo*			_pHPAInfoStorage;	// Storage for HPA Information
static EssList			_slFreeHPAInfo;		// free list for HPA Information

#define _DEBUG_CHECK_BITMAP_FREE(_pVol, _pCxt)
#define _DEBUG_CHECK_FCC(_pVol, _pCxt)

// debug begin
#ifdef FFAT_DEBUG
	#undef _DEBUG_CHECK_BITMAP_FREE
	#define _DEBUG_CHECK_BITMAP_FREE(_pVol, _pCxt)	_debugCheckBitmapFree(_pVol, pCxt);

	#undef _DEBUG_CHECK_FCC
	#define _DEBUG_CHECK_FCC(_pVol, _pCxt)			_debugCheckFCC(_pVol, pCxt);

	static t_boolean	_isClusterHPA(Vol* pVol, t_uint32 dwCluster, t_int32 dwCount, t_boolean dwExpectHPA, ComCxt* pCxt);
	static void			_debugCheckAllocation(Node* pNode, t_uint32 dwCount,
													FFatVC* pVC, ComCxt* pCxt);
	static void			_debugPrintVC(FFatVC* pVC);
	static FFatErr		_debugCheckFCC(Vol* pVol, ComCxt* pCxt);
	static FFatErr		_debugCheckBitmapFree(Vol* pVol, ComCxt* pCxt);
	static FFatErr		_debugCheckBitmap(Vol* pVol);
	static t_int32		_getFreeFATSectorCount(Vol* pVol);
	static t_uint32	_debugGetTCCOfHPA(Vol* pVol, ComCxt* pCxt);
#endif

#ifdef _HPA_DEBUG
	#define FFAT_DEBUG_HPA_PRINTF(_msg)		FFAT_DEBUG_PRINTF(_T("[HPA] ")); FFAT_DEBUG_PRINTF(_msg)
#else
	#define FFAT_DEBUG_HPA_PRINTF(_msg)
#endif



// debug end

//=============================================================================
//
//	external functions
//

/**
 * Initializes HPA
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_hpa_init(void)
{
	FFatErr		r;

	// get root name
	_MAIN()->psRootName	= _CONFIG()->psHPARootName;
	FFAT_ASSERT(_MAIN()->psRootName);

	// get root name length
	_MAIN()->dwRootNameLen = FFAT_WCSLEN(_MAIN()->psRootName);
	FFAT_ASSERT(_MAIN()->dwRootNameLen < FFAT_HPA_ROOT_NAME_MAX_LENGTH);

	_MAIN()->pConf		= _CONFIG();			// set user config

	// initialize function pointer
	FFAT_ASSERT((sizeof(_pfBCIO) / sizeof(PFN_BC_IO)) == (FFAT_IO_ERASE_SECTOR + 1));
	_pfBCIO[FFAT_IO_READ_SECTOR]	= ffat_al_readSector;
	_pfBCIO[FFAT_IO_WRITE_SECTOR]	= ffat_al_writeSector;
	_pfBCIO[FFAT_IO_ERASE_SECTOR]	= _sectorIOEraseWrapper;

	FFAT_DEBUG_HPA_PRINTF((_T("Initialized, (RootNameLen:%d)\n"), _MAIN()->dwRootNameLen));

	// allocate memory for HPA
	ESS_LIST_INIT(&_slFreeHPAInfo);

	r = _initHPAInfoStorage();
	FFAT_EO(r, (_T("fail to init HPA Storage")));

out:
	if (r != FFAT_OK)
	{
		ffat_hpa_terminate();
	}

	return r;
}


/**
* Initializes HPA
*
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		SEP-27-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_terminate(void)
{
	_terminateHPAInfoStorage();
	return FFAT_OK;
}


/**
* This function mount a volume for HPA
*
* @param		pVol		: volume pointer
* @param		dwFlag		: mount flag
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK			: Success
* @return		FFAT_EACCESS	: there is HPA but mount request without HPA flag
* @author		DongYoung Seo
* @version		JUL-26-2006 [DongYoung Seo] First Writing.
* @version		JAN-05-2007 [DongYoung Seo] Add Static Meta-data Area
* @version		MAY-13-2009 [JeongWoo Park] Add the check code for READ_ONLY | HPA_CREATE
*											in this case, mount will be success only for existed HPA
*/
FFatErr
ffat_hpa_mount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);

	if ((dwFlag & FFAT_MOUNT_HPA_MASK) == 0)
	{
		if ((dwFlag & FFAT_MOUNT_HPA_NO_CHECK) == 0)
		{
			if (_findHPA(pVol, pCxt) == FFAT_OK)
			{
				return FFAT_EACCESS;
			}
		}
#if 0
		if (_findHPA(pVol, pCxt) == FFAT_OK)
		{
			return FFAT_EACCESS;
		}
#endif
		return FFAT_OK;
	}

	if ((dwFlag & FFAT_MOUNT_RDONLY) &&
		(dwFlag & FFAT_MOUNT_HPA_CREATE))
	{
		r = _findHPA(pVol, pCxt);
		if (r == FFAT_ENOENT)
		{
			return FFAT_EROFS;
		}
		FFAT_EO(r, (_T("fail to find HPA")));
	}

	if (dwFlag & FFAT_MOUNT_HPA)
	{
		r = _mountHPA(pVol, pCxt);
		if ((r == FFAT_ENOENT) && (dwFlag & FFAT_MOUNT_HPA_CREATE))
		{
			// NO HPA ON THE VOLUME 
			r = _createHPA(pVol, pCxt);
			FFAT_EO(r, (_T("fail to create HPA")));

			// NO HPA ON THE VOLUME 
			r = _mountHPA(pVol, pCxt);
			FFAT_EO(r, (_T("fail to mount HPA")));
		}
		else
		{
			FFAT_EO(r, (_T("fail to mount HPA")));
		}
	}
	else if (dwFlag & FFAT_MOUNT_HPA_CREATE)
	{
		r = _createHPA(pVol, pCxt);
		FFAT_EO(r, (_T("fail to create HPA")));

		r = _mountHPA(pVol, pCxt);
		FFAT_EO(r, (_T("fail to mount volume with HPA")));
	}
	else if (dwFlag & FFAT_MOUNT_HPA_SHOW)
	{
		r = FFAT_ENOSUPPORT;		// not implemented yet
	}
	else IF_LK (dwFlag & FFAT_MOUNT_HPA_REMOVE)
	{
		r = FFAT_ENOSUPPORT;		// not implemented yet
	}
	else
	{
		FFAT_ASSERT(0);
	}

	r = FFAT_OK;

out:
	return r;
}


/**
* This function un-mount a volume
* 이 함수는 umount operation의 마지막에 호출이 된다.
* log recovery등 addon moudle에 포함되는 umount 관련 operation을 수행한다.
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @author		DongYoung Seo
* @version		JUL-27-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_umount(Vol* pVol, ComCxt* pCxt)
{
	// check HPA Activated
	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// release memory
	_freeHPA(pVol, pCxt);

	return FFAT_OK;
}


/**
* get status of a volume
*
* @param		pVol		: [IN] volume pointer
* @param		pStatus		: [OUT] volume information storage
* @param		pBuff		: [IN] buffer pointer, may be NULL
* @param		dwSize		: [IN] size of buffer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_DONE	: volume status gather operation success
* @return		FFAT_OK		: just ok, nothing is done.
* @return		else		: error
* @author		DongYoung Seo
* @version		AUG-28-2006 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Remove dwFreeClusterCount from HPA pStatus
*/
FFatErr
ffat_hpa_getVolumeStatus(Vol* pVol, FFatVolumeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr		r;

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		FFAT_MEMSET(&pStatus->stHPA, 0x00, sizeof(FFatVolumeStatusHPA));
		return FFAT_OK;
	}

	r = _updateVolumeInfo(pVol, pCxt);
	FFAT_ER(r, (_T("fail to update volume information")));

	FFAT_ASSERT(_INFO_FLAG(_INFO(pVol)) & HPA_VALID_FREE_BITMAP);
	FFAT_ASSERT(_INFO_FLAG(_INFO(pVol)) & HPA_VALID_UPDATE_BITMAP);

	pStatus->stHPA.dwClusterCount		= _INFO_TCC(_INFO(pVol));
	return r;
}


/**
* get maximum available clusters for a node
*
* @param		pNode		: [IN] Node Pointer
* @param		pdwCount	: [OUT] maximum available cluster count
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_DONE	: volume status gather operation success
* @return		FFAT_OK		: just ok, nothing is done.
* @return		else		: error
* @author		DongYoung Seo
* @version		OCT-01-2008 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Both normal node and HPA one return same FCC
*/
FFatErr
ffat_hpa_getAvailableClusterCountForNode(Node* pNode, t_uint32* pdwCount, ComCxt* pCxt)
{
	Vol*		pVol;
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCount);

	if (_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		// nothing to do
		return FFAT_OK;
	}

	// get available cluster count for hidden area
	pVol = NODE_VOL(pNode);
	r = _updateVolumeInfo(pVol, pCxt);
	FFAT_ER(r, (_T("fail to get volume status")));

	*pdwCount = VOL_FCC(pVol);

	FFAT_ASSERT(*pdwCount < VOL_CC(pVol));

	return FFAT_DONE;
}



/**
 * get free clusters without FAT update
 *
 * @param		pNode			: [IN] Node pointer
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwHint			: [IN] free cluster hint, lookup start cluster
 * @param		pdwFreeCount	: [IN] found free cluster count
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		dwAllocFlag		: [IN] flag for allocation, for directory/file distinguish
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_DONE		: success
 * @return		FFAT_ENOSPC		: Not enough free cluster on the volume
 *									or not enough free entry at pVC (pdwFreeCount is updated)
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_hpa_getFreeClusters(Node* pNode, t_uint32 dwCount, FFatVC* pVC, t_uint32 dwHint,
							t_uint32* pdwFreeCount, FatAllocateFlag dwAllocFlag, ComCxt* pCxt)
{
	Vol*		pVol;
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwFreeCount);

	pVol = NODE_VOL(pNode);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// nothing to do
		return FFAT_OK;
	}

	if (_IS_NODE_AT_HPA(pNode) == FFAT_FALSE)
	{
		// Check and release FREE FAT Sector for Full HPA
		r = _releaseFreeHPASector(pVol, pCxt);
		FFAT_ER(r, (_T("Fail to release Full HPA Sector")));

		r = _getFreeClustersNormal(pVol, dwCount, pVC, dwHint, pdwFreeCount, pCxt);
	}
	else
	{
		r = _getFreeClustersHPA(pNode, dwCount, pVC, dwHint, pdwFreeCount, dwAllocFlag, pCxt);
	}

	FFAT_ASSERT(r != FFAT_OK);		// never

// debug begin
#ifdef FFAT_DEBUG

	_DEBUG_CHECK_FCC(NODE_VOL(pNode), pCxt)

	if ((r == FFAT_DONE) || (r == FFAT_ENOSPC))
	{
		_debugPrintVC(pVC);
	}
#endif
// debug end

	return r;
}


/**
* After function call for making cluster chain with VC
*
* @param		pNOde			: [IN] Node pointer
* @param		pVC				: [IN] Vectored Cluster Information
* @param		dwFlag			: [IN] cache flag
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo
* @version		JUN-17-200 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Update Partial HPA for HPA node
*/
FFatErr
ffat_hpa_makeClusterChainVCAfter(Node* pNode, FFatVC* pVC, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);

	if (_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		// Nothing to do
		return FFAT_OK;
	}

	// update Updated bitmap
	r = _updateUpdatedBitmapAlloc(NODE_VOL(pNode), pVC, VC_CC(pVC));
	FFAT_ER(r, (_T("fail to update updated bitmap")));

	// update Cluster bitmap
	if (_IS_NODE_AT_HPA(pNode) == FFAT_TRUE)
	{
		r = ffat_hpa_updatePartialHPA(NODE_VOL(pNode), pNode, VC_FC(pVC), VC_CC(pVC), pVC, FFAT_TRUE, dwFlag, pCxt);
		FFAT_ER(r, (_T("fail to update HPA Cluster bitmap")));
	}
	
	//_DEBUG_CHECK_BITMAP_FREE(NODE_VOL(pNode), pCxt)


	return FFAT_OK;
}


/**
 * deallocate request for HPA.
 * It does not deallocate any cluster at here.
 *
 * just set the FAT check flag.
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwCluster		: [IN] first cluster number to deallocate
 * @param		dwClusterCount	: [IN] cluster count
 * @param		pVC				: [IN] Vectored Cluster Information
 * @param		dwFlag			: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_DONE		: dwCount 만큼의 cluster allocation success
 * @return		FFAT_OK			: de-allocation을 수행하지 않았다.
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 * @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
 * @version		Aug-29-2009 [SangYoon Oh] Update Partial HPA for HPA node
 */
FFatErr
ffat_hpa_deallocateClusters(Node* pNode, t_uint32 dwCluster, t_uint32 dwCount, FFatVC* pVC, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	Vol*		pVol;
	FFatErr		r;
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(FFATFS_IsValidCluster(NODE_VI(pNode), dwCluster) == FFAT_TRUE);
	pVol = NODE_VOL(pNode);

	FFAT_ASSERT(pNode);

	if (_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		// Nothing to do
		return FFAT_OK;
	}

	// update Cluster bitmap
	if (_IS_NODE_AT_HPA(pNode) == FFAT_TRUE)
	{
		r = ffat_hpa_updatePartialHPA(pVol, pNode, dwCluster, dwCount, pVC, FFAT_FALSE, dwFlag, pCxt);
		FFAT_ER(r, (_T("fail to update cluster bitmap")));
	}

	// reset HPA_FREE_FAT_CHECKED flag
	_INFO_FLAG(_INFO(pVol)) &= ~(HPA_FREE_FAT_CHECKED | HPA_NO_FREE_CLUSTER);
	_INFO_HFHPAS(_INFO(pVol)) = VOL_LVFSFF(pVol);
	

	_DEBUG_CHECK_FCC(pVol, pCxt)

	return FFAT_OK;
}



/**
 * update free cluster count
 *
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] cluster information
 *										storage for deallocated clusters 
 *										주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
 *										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 * @param		dwDeallocCount	: [IN] deallocated count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 * @version		Aug-29-2009 [SangYoon Oh] Remove the code that increases Free Cluster Count of HPA
 * @version		Sep-30-2009 [SangYoon Oh] Add the code that releases FREE FAT Sector for Full HPA
 * @version		Nov-26-2009 [SangYoon Oh] Add the debug code to verify the total cluster count of HPA
 */
FFatErr
ffat_hpa_afterDeallocateCluster(Node* pNode, FFatVC* pVC, t_uint32 dwDeallocCount, ComCxt* pCxt)
{
	Vol*		pVol;
	FFatErr			r;
#ifdef _HPA_DEBUG
	t_int32 a;
	t_int32 b;
#endif
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(dwDeallocCount > 0);
	pVol = NODE_VOL(pNode);
	
	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// Nothing to do
		return FFAT_OK;
	}

	// update Updated bitmap
	r = _updateUpdatedBitmapDealloc(pVol, pVC, dwDeallocCount);
	FFAT_ER(r, (_T("fail to update updated bitmap")));

	// Check and release FREE FAT Sector for Full HPA
	r = _releaseFreeHPASector(pVol, pCxt);
	FFAT_ER(r, (_T("fail to release FREE FAT Sector for Full HPA")));

	_DEBUG_CHECK_BITMAP_FREE(pVol, pCxt)

#ifdef _HPA_DEBUG
	a = _INFO_TCC(_INFO(pVol));
	b = _debugGetTCCOfHPA(pVol, pCxt);
	//FFAT_ASSERT(_IS_VALID_HPA_TCC(_INFO(pVol))? _INFO_TCC(_INFO(pVol)) == _debugGetTCCOfHPA(pVol, pCxt) : FFAT_TRUE);
	//FFAT_ASSERT(_IS_VALID_HPA_TCC(_INFO(pVol))? a == b : FFAT_TRUE);
#endif

	return FFAT_OK;
}

/**
* undo HPA Create operation
*
* @param		pVol			: [IN] volume pointer
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_undoHPACreate(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;
	t_int8*			pBuff = NULL;
	t_uint32		dwSector;

	// just erase last two FAT sectors.
	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	FFAT_MEMSET(pBuff, 0x00, VOL_SS(pVol));

	dwSector = VOL_LCN(pVol);
	r = ffat_readWriteSectors(pVol, NULL, dwSector, 1, pBuff, 
						(FFAT_CACHE_DATA_FAT | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to erase HPA")));

	r = ffat_readWriteSectors(pVol, NULL, (dwSector - 1), 1, pBuff, 
						(FFAT_CACHE_DATA_FAT | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to erase HPA")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

	return r;
}


/**
* check HPA is activated or not.
*
* @param		pVol			: [IN] volume pointer
* @return		FFAT_OK			: success
* @return		else			: error
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
t_boolean
ffat_hpa_isActivated(Vol* pVol)
{
	return _IS_ACTIVATED(pVol);
}


/**
* lookup a node HPA root node
*
* @param		pNodeParent	: [IN] Parent node
* @param		pNodeChild	: [IN/OUT] Child node, 
*								Free DE information will be updated when flag is FFAT_LOOKUP_FOR_CREATE
* @param		psName		: [IN] node name
* @param		dwLen		: [IN] name length
* @param		dwFlag		:	[IN]	: lookup flag, refer to FFatLookupFlag
* @param		pGetNodeDE	: [OUT] directory entry for node
* @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않음 or 일부에 대한 lookup을 수행함
* @return		FFAT_DONE	: ADDON module에서 lookup을 성공적으로 처리함.
* @return		FFAT_ENOENT	: target node is not exist
* @return		negative	: error 
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing
* @version		JAN-13-2009 [DongYoung Seo] change HPA root DE start cluster to cluster of info
*											change HPA root DE end cluster to cluster of info
* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
*											- Case sensitive
*/
FFatErr
ffat_hpa_lookup(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, t_int32 dwLen,
				FFatLookupFlag dwFlag, FatGetNodeDe* pGetNodeDE)
{
	HPAInfo*		pInfo;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pGetNodeDE);

	if (dwFlag & FFAT_LOOKUP_FREE_DE)
	{
		// do not anything
		// This is just lookup free entries
		return FFAT_OK;
	}

	if (NODE_IS_ROOT(pNodeParent) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (_IS_ACTIVATED(NODE_VOL(pNodeParent)) == FFAT_FALSE)
	{
		// HPA does not activated
		return FFAT_OK;
	}

	if (dwLen != _MAIN()->dwRootNameLen)
	{
		return FFAT_OK;
	}

	// check string
	if (((VOL_FLAG(NODE_VOL(pNodeParent)) & VOL_CASE_SENSITIVE) == 0)
		? (FFAT_WCSICMP(psName, _MAIN()->psRootName) != 0)
		: (FFAT_WCSCMP(psName, _MAIN()->psRootName) != 0))
	{
		return FFAT_OK;
	}

	pInfo = _INFO(NODE_VOL(pNodeParent));

	// lookup target is HPA root directory
	pGetNodeDE->dwCluster			= NODE_C(pNodeParent);
	pGetNodeDE->dwDeStartCluster	= _INFO_IC(pInfo);
	pGetNodeDE->dwDeStartOffset		= _ROOT_DE_START_OFFSET(pInfo);
	pGetNodeDE->dwDeEndCluster		= _INFO_IC(pInfo);
	pGetNodeDE->dwDeEndOffset		= _ROOT_DE_END_OFFSET(pInfo);
	pGetNodeDE->dwDeSfnCluster		= NODE_C(pNodeParent);
	FFAT_MEMCPY(pGetNodeDE->pDE, pInfo->pRootDe, (pInfo->dwEntryCount * sizeof(FatDeSFN)));
	pGetNodeDE->dwEntryCount		= pInfo->dwEntryCount;
	pGetNodeDE->dwTotalEntryCount	= pInfo->dwEntryCount;

	FFAT_ASSERT((pGetNodeDE->dwDeStartOffset & FAT_DE_SIZE_MASK) == 0);
	FFAT_ASSERT((pGetNodeDE->dwDeEndOffset & FAT_DE_SIZE_MASK) == 0);

	return FFAT_DONE;
}


/**
* add node information after lookup operation
*
* @param		pNodeParent	: [IN] Parent node
* @param		pNodeChild	: [IN] Child node information
* @param		dwFlag		: [IN] lookup flag
* @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않은
* @return		negative	: error 
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_afterLookup(Node* pNodeParent, Node* pNodeChild)
{
	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);

	if (_IS_ACTIVATED(NODE_VOL(pNodeParent)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// check parent is in HPA
	if (_IS_NODE_AT_HPA(pNodeParent) == FFAT_TRUE)
	{
		_SET_NODE_HPA(pNodeChild);
	}
	else
	{
		if (_isHPARoot(pNodeChild) == FFAT_TRUE)
		{
			_SET_NODE_HPA(pNodeChild);
			NODE_COP(pNodeChild) = _INFO_IC(_INFO(NODE_VOL(pNodeParent)));
		}
	}

	return FFAT_OK;
}



/**
* Initialize a node structure for FFAT ADDON module
* do not change value except Node.stAddon
*
* @param		pVol		: [IN] volume pointer
* @param		pNodeParent	: [IN] parent node pointer
*								It may be NULL.
* @param		pNodeChild	: [IN/OUT] child node pointer
* @return		FFAT_OK		: success
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_initNode(Node* pNodeParent, Node* pNodeChild)
{
	FFAT_ASSERT(pNodeChild);

	if (pNodeParent == NULL)
	{
		return FFAT_OK;
	}

	if (_IS_ACTIVATED(NODE_VOL(pNodeParent)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// check parent is in HPA
	if (_IS_NODE_AT_HPA(pNodeParent) == FFAT_TRUE)
	{
		_SET_NODE_HPA(pNodeChild);
	}

	return FFAT_OK;
}


/**
* this function is called before directory removal.
*
* no need to lock node
* no need to do parameter validity check
*
* @param		pNode		: [IN] node(directory) pointer
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_removeDir(Node* pNode)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);

	if (_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// check is this HPA root node
	r = _isHPARoot(pNode);
	if (r == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Can not remove HPA root directory ")));
		return FFAT_EACCESS;
	}

	return r;
}


/**
* this function is called before directory/file rename.
*
* no need to lock node
* no need to do parameter validity check
*
* @param		pNodeSrc		: [IN] Source node pointer
* @param		pNodeDesParent	: [IN] Parent of destination node 
* @param		pNodeDes		: [IN] Destination node pointer
*										may be NULL
* @param		pNodeDesNew		: [IN] New destination node pointer
* @return		FFAT_OK			: Success
* @return		FFAT_EACCESS	: source or target node is HPA root
* @return		FFAT_ENOSUPPORT	: try to rename from HPA to normal or normal to HPA
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_rename(Node* pNodeSrc, Node* pNodeDesParent, Node* pNodeDes, Node* pNodeDesNew)
{
	Node*		pDes;
	FFatErr		r;

	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDes || pNodeDesNew);

	if (_IS_ACTIVATED(NODE_VOL(pNodeSrc)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (pNodeDes)
	{
		pDes = pNodeDes;
	}
	else
	{
		pDes = pNodeDesNew;
	}

	// check is this HPA root node
	r = _isHPARoot(pNodeSrc);
	if (r == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Can not rename HPA root directory ")));
		return FFAT_EACCESS;
	}

	r = _isHPARoot(pDes);
	if (r == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Can not rename HPA root directory ")));
		return FFAT_EACCESS;
	}

	if (_IS_NODE_AT_HPA(pNodeSrc) != _IS_NODE_AT_HPA(pDes))
	{
		FFAT_LOG_PRINTF((_T("Can not rename from HPA to normal or on the contrary")));
		return FFAT_ENOSUPPORT;
	}

	if (_IS_NODE_AT_HPA(pNodeSrc) != _IS_NODE_AT_HPA(pNodeDesParent))
	{
		FFAT_LOG_PRINTF((_T("Can not rename from HPA to normal or on the contrary")));
		return FFAT_ENOSUPPORT;
	}

	return FFAT_OK;
}


/**
* Sector IO toward logical device
* 
* @param		pVol	: [IN] volume pointer
* @param		pLDevIO	: [IN] S_ector IO structure pointer
* @param		pCxt			: [IN] context of current operation
* @param		FFAT_OK			: Sector IO Success
* @param		FFAT_EINVALID	: Invalid parameter
* @param		FFAT_ENOSUPPORT	: Not supported operation
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter pCxt
*/
FFatErr
ffat_hpa_ldevIO(Vol* pVol, FFatLDevIO* pLDevIO, ComCxt* pCxt)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLDevIO);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return FFAT_ENOSUPPORT;
	}

	return _sectorIO(pVol, pLDevIO->dwFlag, pLDevIO->dwSectorNo, pLDevIO->dwCount, pLDevIO->pBuff, pCxt);
}


/**
* get bitmap size in byte
* (for chkdsk)
* 
* @param		pVol		: [IN] volume pointer
* @param		positive	: size of bitmap in byte
* @param		0			: HPA is not activated
* @author		DongYoung Seo
* @version		01-DEc-2008 [DongYoung Seo] First Writing.
*/
t_int32
ffat_hpa_getBitmapSize(Vol* pVol)
{
	FFAT_ASSERT(pVol);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return 0;
	}

	return _FBITMAP_SIZE(pVol);
}

/**
* get cluster bitmap size in byte
* (for chkdsk)
* 
* @param		pVol		: [IN] volume pointer
* @param		positive	: size of cluster bitmap in byte
* @param		0			: HPA is not activated
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/
t_int32
ffat_hpa_getClusterBitmapSize(Vol* pVol)
{
	FFAT_ASSERT(pVol);

	if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
	{
		return 0;
	}

	return _CBITMAP_SIZE(pVol);
}

/**
* this function check change status request is for HPA root directory
*
* @param		pNode		: [IN] node(directory) pointer
* @author		DongYoung Seo
* @version		APR-01-2009 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_hpa_setStatus(Node* pNode)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);

	if (_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// check is this HPA root node
	r = _isHPARoot(pNode);
	if (r == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Can not change status of HPA root directory ")));
		return FFAT_EACCESS;
	}

	return FFAT_OK;
}

/**
 * update Partial HPA
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pNode			: [IN] node pointer
 * @param		dwFirstCluster	: [IN] first cluster to be updated
 * @param		dwCount			: [IN] cluster count
 * @param		pVC				: [IN] vectored cluster storage
 * @param		bBitmapSet		: [IN] flag for setting or clearing bitmap
 * @param		dwFlag			: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: updating bitmap failed mostly due to IO error
 * @author		SangYoon Oh
 * @version		Aug-29-2009 [SangYoon Oh] First Writing.
 * @version		Sep-24-2009 [SangYoon Oh] Ignore EFAT error when the FAT chain is disconnected (CQ:25289)
 * @version		Nov-26-2009 [SangYoon Oh] Add the parameter ComCxt when calling inc/decTotalClusterHPA 
 *											to verify the total cluster count of HPA
 * @version		Dec-23-2009 [JW Park] Add the consideration about free cluster can be occurred in FAT chain
 *										with sudden-power-off recovery.
 */
FFatErr ffat_hpa_updatePartialHPA(Vol* pVol, Node* pNode, t_uint32 dwFirstCluster, t_uint32 dwCount, FFatVC* pVC,
									t_boolean bBitmapSet, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	t_uint32		dwCluster;
	FFatVC			stVC_Temp;			// temporary FVC 
	FFatVC*			pVC_Temp;
	FFatErr			r;
	FatVolInfo*		pVI;

	r = FFAT_OK;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwFirstCluster > 0);

	FFAT_ASSERT((pNode != NULL)? _IS_NODE_AT_HPA(pNode) : FFAT_TRUE);//호출 전에 HPA 여부 확인되어야 함
	
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);
	FFAT_ASSERT((pVC != NULL)? (dwFirstCluster == VC_FC(pVC)) : FFAT_TRUE);
	pVI = VOL_VI(pVol);

	pVC_Temp = &stVC_Temp;
	stVC_Temp.pVCE = NULL;
	
	if ((pVC != NULL) && (VC_CC(pVC) > 0))
	{
		//VC를 활용하여 Cluster Bitmap update
		r = _updatePartialHPA(pVol, pVC, bBitmapSet, dwFlag, pCxt);
		FFAT_ASSERT(r >=0);

		//update TCC of HPA
		if (bBitmapSet == FFAT_FALSE)
		{
			_decTotalClusterHPA(pVol, VC_CC(pVC));
		}
		else
		{
			_incTotalClusterHPA(pVol, VC_CC(pVC), pCxt);
		}

		//done
		if (VC_CC(pVC) == dwCount)
		{
			return r;
		}
	}
		
	//pVC가 NULL이거나 Full인 경우 FAT Chain을 따라가면서 나머지 VC를 처리
	if ((pVC == NULL) || (VC_IS_FULL(pVC)))
	{

		if (pVC != NULL)
		{
			r = FFATFS_GetNextCluster(pVI, VC_LC(pVC), &dwCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));

			IF_UK (FFATFS_IS_EOF(pVI, dwCluster) == FFAT_TRUE)
			{
				// no more cluster chain
				goto out;
			}
		}
		else
		{
			dwCluster = dwFirstCluster;
		}

		pVC_Temp->pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
		FFAT_ASSERT(pVC_Temp->pVCE);
		stVC_Temp.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);
		do 
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwCluster) == FFAT_TRUE);
			
			VC_INIT(pVC_Temp, FFAT_NO_OFFSET);
			r = ffat_misc_getVectoredCluster(pVol, pNode, dwCluster, FFAT_NO_OFFSET, 0, pVC_Temp, NULL, pCxt);
			//FFAT_EO(r, (_T("fail to get vectored cluster info")));
			//Ignore EFAT error when the FAT chain is disconnected (CQ:25289)
			
			r = _updatePartialHPA(pVol, pVC_Temp, bBitmapSet, dwFlag, pCxt);
			FFAT_ASSERT(r >=0);

			//update TCC of HPA
			if (bBitmapSet == FFAT_FALSE)
			{
				_decTotalClusterHPA(pVol, VC_CC(pVC_Temp));
			}
			else
			{
				_incTotalClusterHPA(pVol, VC_CC(pVC_Temp), pCxt);
			}

			dwCluster = VC_LC(pVC_Temp);

			r = FFATFS_GetNextCluster(pVI, dwCluster, &dwCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));

			IF_UK ((FFATFS_IS_EOF(pVI, dwCluster) == FFAT_TRUE) ||
				(dwCluster == 0))
			{
				// no more cluster chain
				goto out;
			}

		} while (VC_IS_FULL(pVC_Temp) == FFAT_TRUE);
	}

out:
	if (pVC_Temp->pVCE)
	{
		FFAT_LOCAL_FREE(pVC_Temp->pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	}

#ifdef FFAT_DEBUG
	// check cluster validity
	if (bBitmapSet && pVC && pNode)
	{
		_debugCheckAllocation(pNode, dwCount, pVC, pCxt);
	}
#endif

	return r;
}


// debug begin
#ifdef FFAT_DEBUG
	/**
	* This function controls FFAT HPA module
	*
	* @param		dwCmd		: [IN] filesystem control command, operation type
	* @param		pVol		: [IN] Volume Pointer
	* @author		DongYoung Seo
	* @version		JUL-21-2006 [DongYoung Seo] First Writing.
	* @version		Aug-29-2009 [SangYoon Oh] Remove the code regarding FCC of HPA
	*/
	FFatErr
	ffat_hpa_fsctl(FFatFSCtlCmd dwCmd, Vol* pVol)
	{
		HPAInfo*		pInfo;

		if (dwCmd == FFAT_FSCTL_INVALIDATE_FCCH)
		{
			if (_IS_ACTIVATED(pVol) == FFAT_TRUE)
			{
				pInfo = _INFO(pVol);

				_INFO_TCC(pInfo)	= FFAT_FREE_CLUSTER_INVALID;	// no information now
				_INFO_FLAG(pInfo)	&= (~HPA_VALID_FREE_BITMAP);
				_INFO_FLAG(pInfo)	&= (~HPA_VALID_UPDATE_BITMAP);
				_INFO_FLAG(pInfo)	&= (~HPA_FREE_FAT_CHECKED);
			}
		}

		return FFAT_OK;
	}


	/**
	* check Directory Entry information on the Node Structure
	*
	* @param		pNode		: [IN] Node Pointer
	* @return		FFAT_TRUE	: check success
	* @return		FFAT_FALSE	: incorrect node DE info
	* @author		DongYoung Seo
	* @version		13-JAN--2009 [DongYoung Seo] First Writing
	*/
	FFatErr
	ffat_hpa_checkNodeDeInfo(Node* pNode)
	{
		FFAT_ASSERT(pNode);

		if ((_IS_ACTIVATED(NODE_VOL(pNode)) == FFAT_TRUE) && (_isHPARoot(pNode) == FFAT_TRUE))
		{
			return FFAT_TRUE;
		}
		else
		{
			// below ASSERT can not true for hidden area root node
			FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeStartCluster);
			FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeEndCluster);
			FFAT_ASSERT(VOL_RC(NODE_VOL(pNode)) == pNode->stDeInfo.dwDeClusterSFNE);
		}

		return FFAT_TRUE;
	}

#endif
// debug end



//=============================================================================
//
//	static functions
//


/**
* check is there any valid HPA on the volume
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_ENOMEM	: not enough memory
* @return		FFAT_ENOENT	: HPA does not exist
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_findHPA(Vol* pVol, ComCxt* pCxt)
{
	t_uint32		dwInfoSector;
	t_int8*			pBuff = NULL;
	t_int32			r;

	FFAT_ASSERT(pVol);

	dwInfoSector = _getInfoSectorNumber(pVol);

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	r = ffat_readWriteSectors(pVol, NULL, dwInfoSector, 1, pBuff, 
						FFAT_CACHE_DATA_FS, FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		r = FFAT_EIO;
		goto out;
	}

	// check HPA information is valid or not
	r = _isValidHPAInfo(pVol, (HPAInfoSector*)pBuff, pCxt);
	if (r != FFAT_OK)
	{
		r = FFAT_ENOENT;
	}
	else
	{
		r = FFAT_OK;
	}

out:
	FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

	return r;
}


/**
* check is there any node that name is HPA root name
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a name that has same name with HPA root
* @return		FFAT_ENOMEM	: not enough memory
* @return		FFAT_ENOENT	: HPA does not exist
* @author		DongYoung Seo
* @version		JUN-06-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_lookupHPARootNode(Vol* pVol, ComCxt* pCxt)
{
	Node*			pNode = NULL;
	FFatConfig*		pConf;
	FFatErr			r;

	pConf = ffat_al_getConfig();

	pNode = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNode != NULL);

	ffat_node_resetNodeStruct(pNode);

	// find the existing log file
	r = ffat_node_lookup(VOL_ROOT(pVol), pNode, pConf->stHPA.psHPARootName, 
						0, FFAT_LOOKUP_NO_LOCK, NULL, pCxt);

	// do not check error. why ?. this is a QUIZ. Answer below with your name.

	ffat_node_terminateNode(pNode, pCxt);

	FFAT_LOCAL_FREE(pNode, sizeof(Node), pCxt);

	return r;
}


/**
* get sector number for HPA information
* That is the first sector on the last cluster
*
* @param		pVol		: [IN] volume pointer
* @param		pInfo		: [IN] pointer of HPA info
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_ENOMEM	: not enough memory
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
* @version		JUN-19-2009 [JeongWoo Park] Add the code to support OS specific naming rule
*											- Case sensitive
* @version		Aug-29-2009 [SangYoon Oh] Add the code to validate the cluster bitmap cluster
*/
static FFatErr
_isValidHPAInfo(Vol* pVol, HPAInfoSector* pInfoSector, ComCxt* pCxt)
{
	FFatHPAConfig*	pConf;
	t_wchar*		psRootName = NULL;
	t_uint32		dwNextCluster;
	t_int32			dwLen;
	FFatErr			r;

	FFAT_ASSERT(pInfoSector);

	// check first signature
	if (pInfoSector->dwSig1 != FFAT_BO_UINT32(HPA_SIG1))
	{
		return FFAT_EINVALID;
	}

	// check the second signature
	if (pInfoSector->dwSig2 != FFAT_BO_UINT32(HPA_SIG2))
	{
		return FFAT_EINVALID;
	}

	// check cluster number is valid
	if ((FFATFS_IsValidCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwFSMCluster)) == FFAT_FALSE) 
			|| (FFATFS_IsValidCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwCMCluster)) == FFAT_FALSE) 
			|| ((FFATFS_IsValidCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwHPARootCluster)) == FFAT_FALSE)))
	{
		return FFAT_EINVALID;
	}

	psRootName = (t_wchar*)FFAT_LOCAL_ALLOC(FFAT_NAME_BUFF_SIZE, pCxt);
	FFAT_ASSERT(psRootName != NULL);

	// check HPA root name
#ifdef FFAT_VFAT_SUPPORT
	r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), pInfoSector->pHPARootDE, 
								FFAT_BO_INT32(pInfoSector->dwEntryCount),
								psRootName, &dwLen, FAT_GEN_NAME_LFN);
#else
	r = FFATFS_GenNameFromDirEntry(VOL_VI(pVol), pInfoSector->pHPARootDE,
								FFAT_BO_INT32(pInfoSector->dwEntryCount),
								psRootName, &dwLen, FAT_GEN_NAME_SFN);
#endif
	FFAT_EO(r, (_T("fail to generage HPA root name")));

	pConf = _CONFIG();

	// compare name
	if (((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
		? (FFAT_WCSICMP(pConf->psHPARootName, psRootName) != 0)
		: (FFAT_WCSCMP(pConf->psHPARootName, psRootName) != 0))
	{
		r = FFAT_EINVALID;
		goto out;
	}

	// check cluster - HPA root directory
	r = FFATFS_GetNextCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwHPARootCluster),
						&dwNextCluster, pCxt);
	if ((r < 0) || (dwNextCluster == FAT_FREE))
	{
		r = FFAT_EINVALID;
		goto out;
	}

	// check cluster information - FSM root
	r = FFATFS_GetNextCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwFSMCluster),
						&dwNextCluster, pCxt);
	if ((r < 0) || (dwNextCluster == FAT_FREE))
	{
		r = FFAT_EINVALID;
		goto out;
	}

	// check cluster information - CM root
	r = FFATFS_GetNextCluster(VOL_VI(pVol), FFAT_BO_UINT32(pInfoSector->dwCMCluster),
						&dwNextCluster, pCxt);
	if ((r < 0) || (dwNextCluster == FAT_FREE))
	{
		r = FFAT_EINVALID;
		goto out;
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(psRootName, FFAT_NAME_BUFF_SIZE, pCxt);

	return r;
}


/**
* get sector number for HPA information
* That is the first sector on the last cluster
*
* @param		pVol		: [IN] volume pointer
* @return		the HPA information sector number
* @author		DongYoung Seo
* @version		MAY-24-2007 [DongYoung Seo] First Writing.
*/
static t_uint32
_getInfoSectorNumber(Vol* pVol)
{
	FFAT_ASSERT(pVol);

	return FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), VOL_LCN(pVol));
}


/**
* mount a HPA volume
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK			: Success
* @return		FFAT_EACCESS	: there is no HPA on the volume
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_mountHPA(Vol* pVol, ComCxt* pCxt)
{
	FFatErr		r;

	// allocate memory for HPA
	r = _allocHPA(pVol, pCxt);
	FFAT_ER(r, (_T("fail to allocate memory for HPA")));

	r = _getHPA(pVol, pCxt);
	FFAT_EO(r, (_T("fail to get HPA information")));

	return FFAT_OK;

out:
	_freeHPA(pVol, pCxt);
	return r;
}


/**
* creat a HPA area
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the code that creates Cluster Bitmap
*/
static FFatErr
_createHPA(Vol* pVol, ComCxt* pCxt)
{
	t_uint32		dwLastFatSector;
	HPAInfoSector*	pInfoSector = NULL;
	HPAInfo*		pInfo;
	Node*			pNodeHPARoot = NULL;
	t_int32			dwBitmapClusterCount;
	t_int32			dwClusterBitmapClusterCount;
	t_uint32			i;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_FSC(pVol) > 1);
	FFAT_ASSERT(sizeof(HPAInfoSector) <= FFAT_SECTOR_SIZE);

	if (_findHPA(pVol, pCxt) == FFAT_OK)
	{
		return FFAT_OK;
	}

	// check HPA root node is already exist on the root directory
	if (_lookupHPARootNode(pVol, pCxt) == FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("There is a node that has same name with HPA root")));
		return FFAT_EEXIST;
	}

	dwLastFatSector = VOL_LVFSFF(pVol);

	r = _checkFreeFatSectorForHPACreate(pVol, pCxt);
	FFAT_EO(r, (_T("fail to check free FAT sector")));

	// allocate memory for HPA
	r = _allocHPA(pVol, pCxt);
	FFAT_ER(r, (_T("fail to allocate memory for HPA")));

	pInfo = _INFO(pVol);

	pInfoSector = (HPAInfoSector*)FFAT_LOCAL_ALLOC(sizeof(HPAInfoSector), pCxt);
	FFAT_ASSERT(pInfoSector != NULL);

	dwBitmapClusterCount = ESS_MATH_CDB(_FBITMAP_SIZE(pVol), VOL_CS(pVol), VOL_CSB(pVol));
	dwClusterBitmapClusterCount = ESS_MATH_CDB(_CBITMAP_SIZE(pVol), VOL_CS(pVol), VOL_CSB(pVol));

	pInfo->dwInfoCluster	= VOL_LCN(pVol);
	_INFO_RC(pInfo)			= pInfo->dwInfoCluster - 1;
	_INFO_FSMC(pInfo)		= _INFO_RC(pInfo) - dwBitmapClusterCount;
	_INFO_CMC(pInfo)		= _INFO_FSMC(pInfo) - dwClusterBitmapClusterCount;

	pInfoSector->dwSig1				= FFAT_BO_UINT32(HPA_SIG1);
	pInfoSector->dwHPARootCluster	= FFAT_BO_UINT32(_INFO_RC(pInfo));
	pInfoSector->dwFSMCluster		= FFAT_BO_UINT32(_INFO_FSMC(pInfo));
	pInfoSector->dwCMCluster		= FFAT_BO_UINT32(_INFO_CMC(pInfo));
	pInfoSector->dwSig2				= FFAT_BO_UINT32(HPA_SIG2);

	FFAT_DEBUG_HPA_PRINTF((_T("dwHPARootCluster cluster : %d\n"), _INFO_RC(pInfo)));
	FFAT_DEBUG_HPA_PRINTF((_T("dwFSMCluster cluster : %d\n"), _INFO_FSMC(pInfo)));
	FFAT_DEBUG_HPA_PRINTF((_T("dwCMCluster cluster : %d\n"), _INFO_CMC(pInfo)));
	FFAT_DEBUG_HPA_PRINTF((_T("bitmap cluster count: %d\n"), dwBitmapClusterCount));
	FFAT_DEBUG_HPA_PRINTF((_T("Cluster bitmap cluster count: %d\n"), dwClusterBitmapClusterCount));

	//Cluster Bitmap 부터 LastFatSector까지 HPA Bitmap Set
	for (i = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), _INFO_CMC(pInfo)) ; i <= dwLastFatSector; i++)
	{
		_SET_BITMAP_FHPA(pVol, i);	
	}

	pNodeHPARoot = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNodeHPARoot);

	ffat_node_resetNodeStruct(pNodeHPARoot);

	r = ffat_node_initNode(pVol, VOL_ROOT(pVol), VOL_RC(pVol), pNodeHPARoot, FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to init HPA Root node")));

	NODE_C(pNodeHPARoot) = _INFO_RC(pInfo);

	r = _genHPARootDe(pVol, pNodeHPARoot, pCxt);
	IF_UK (r != FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("Fail to generate HPA Root Directory Entry")));
		goto out;
	}

	FFAT_MEMCPY(pInfoSector->pHPARootDE, pInfo->pRootDe, (sizeof(FatDeSFN) * pInfo->dwEntryCount));
	pInfoSector->dwEntryCount	= FFAT_BO_INT32(pInfo->dwEntryCount);

	r = ffat_dir_initCluster(VOL_ROOT(pVol), pNodeHPARoot, pCxt);
	FFAT_EO(r, (_T("fail initialize to root directory")));

	// write information sector
	r = _writeInfoSector(pVol, pInfoSector, pCxt);
	FFAT_EO(r, (_T("fail to write information sector")));

	// write log
	r = ffat_log_hpa(pVol, LM_LOG_HPA_CREATE, pCxt);
	FFAT_EO(r, (_T("fail to write log for HPA creation")));

	r = _createHPAUpdateFAT(pVol, pNodeHPARoot, pCxt);
	FFAT_EO(r, (_T("fail to update FAT")));

	// init HPA Bitmap cluster
	r = _writeBitmap(pVol, VOL_FFS(pVol), VOL_LVFSFF(pVol), pCxt);
	FFAT_EO(r, (_T("fail to write bitmap")));

	// init Cluster bitmap
	r = _initClusterBitmap(pVol, pCxt);

	r = ffat_vol_sync(pVol, FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to sync a volume")));

	r = FFAT_OK;
out:
	if (pNodeHPARoot)
	{
		ffat_node_terminateNode(pNodeHPARoot, pCxt);
		FFAT_LOCAL_FREE(pNodeHPARoot, sizeof(Node), pCxt);
	}

	FFAT_LOCAL_FREE(pInfoSector, sizeof(HPAInfoSector), pCxt);

	_freeHPA(pVol, pCxt);

	if (r != FFAT_OK)
	{
		// undo 
		ffat_hpa_undoHPACreate(pVol, pCxt);
	}

	return r;
}


/**
* update FAT area for HPA creation
*
* @param		pVol		: [IN] volume pointer
* @param		pInfoSector	: [IN] HPA information sector
* @param		pNodeHPARoot: [IN] HPA root node
* @param		pCxt		: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add code to update FAT of Cluster Bitmap Clusters
*/
static FFatErr
_createHPAUpdateFAT(Vol* pVol, Node* pNodeHPARoot, ComCxt* pCxt)
{
	FFatErr			r;
	FFatVC			stVC;
	FFatVCE		stVCE;
	HPAInfo*					pInfo;

	// make cluster chains for info, root directory & bitmap area
	// set info cluster
	r = FFATFS_UpdateCluster(VOL_VI(pVol), VOL_LCN(pVol), VOL_EOC(pVol),
					FFAT_CACHE_DATA_FAT, NULL, pCxt);
	FFAT_EO(r, (_T("fail to update cluster for HPA info")));

	// set cluster for HPA root directory
	r = FFATFS_UpdateCluster(VOL_VI(pVol), NODE_C(pNodeHPARoot), VOL_EOC(pVol),
					FFAT_CACHE_DATA_FAT, NULL, pCxt);
	FFAT_EO(r, (_T("fail to update cluster for HPA info")));

	// FSM
	stVC.pVCE = &stVCE;
	VC_INIT(&stVC, VC_NO_OFFSET);
	stVC.dwTotalEntryCount = 1;
	stVC.dwValidEntryCount = 1;

	pInfo = _INFO(pVol);
	FFAT_ASSERT(pInfo);

	stVC.pVCE->dwCluster	= _INFO_FSMC(pInfo);
	stVC.pVCE->dwCount		= _INFO_RC(pInfo) - _INFO_FSMC(pInfo);

	stVC.dwTotalClusterCount	= stVC.pVCE->dwCount;

	r = FFATFS_MakeClusterChainVC(VOL_VI(pVol), 0x00, &stVC, FAT_UPDATE_NONE,
					FFAT_CACHE_NONE, NULL, pCxt);
	FFAT_EO(r, (_T("fail to make cluster chain for bitmap")));
	
	// CM
	stVC.pVCE->dwCluster	= _INFO_CMC(pInfo);
	stVC.pVCE->dwCount		= _INFO_FSMC(pInfo) - _INFO_CMC(pInfo);

	stVC.dwTotalClusterCount	= stVC.pVCE->dwCount;

	r = FFATFS_MakeClusterChainVC(VOL_VI(pVol), 0x00, &stVC, FAT_UPDATE_NONE,
					FFAT_CACHE_NONE, NULL, pCxt);
	FFAT_EO(r, (_T("fail to make cluster chain for Cluster bitmap")));

	r = FFAT_OK;
out:
	return r;
}


/**
* This function allocates memory for HPA
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK		: success
* @param		FFAT_ENOMEM	: not enough memory
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Remove the limitation of HPA Bitmap Size
*/
static FFatErr
_allocHPA(Vol* pVol, ComCxt* pCxt)
{
	FFatHPAConfig*	pConf;
	AddonVol*		pAddon;
	HPAInfo*		pInfo = NULL;
	t_int32			dwBitmapSize;

	FFAT_ASSERT(pVol);

	dwBitmapSize = _FBITMAP_SIZE(pVol);
	FFAT_DEBUG_HPA_PRINTF((_T("Bitmap Size : %d byte \n"), dwBitmapSize));

	pConf = _CONFIG();

	pInfo = _getFreeHPAInfo(pVol);
	if (pInfo == NULL)
	{
		// There is no free HPA info
		return FFAT_ENOMEM;
	}

	pInfo->dwFlag = HPA_NONE;			// set no flag

	pAddon = VOL_ADDON(pVol);
	pAddon->pHPA = pInfo;

	return FFAT_OK;
}


/**
* This function free memory for HPA
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK		: success
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_freeHPA(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*	pHPA = NULL;
	FFatErr		r;

	// do not change free sequence
	FFAT_ASSERT(pVol);

	pHPA = _INFO(pVol);
	if (pHPA == NULL)
	{
		return FFAT_OK;
	}

	r = _releaseHPAInfo(pVol, pHPA);
	FFAT_ER(r, (_T("fail to release HPA Info")));

	_INFO(pVol) = NULL;	// set it to NULL

	return FFAT_OK;
}


/**
* get HPA information and fill it to pVol
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_ENOMEM	: not enough memory
* @return		FFAT_ENOENT	: HPA does not exist
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add code to get cluster Bitmap information
*/
static FFatErr
_getHPA(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	HPAInfoSector*	pInfoSector;
	t_uint32		dwInfoSector;
	t_uint32		dwClusterBitmapSector;
	t_int8*			pBuff = NULL;
	t_int32			r;

	FFAT_ASSERT(pVol);

	dwInfoSector = _getInfoSectorNumber(pVol);

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	r = ffat_readWriteSectors(pVol, NULL, dwInfoSector, 1, pBuff, 
					FFAT_CACHE_DATA_FS, FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		r = FFAT_EIO;
		goto out;
	}

	pInfoSector = (HPAInfoSector*)pBuff;

	// check HPA information is valid or not
	r = _isValidHPAInfo(pVol, pInfoSector, pCxt);
	if (r != FFAT_OK)
	{
		r = FFAT_ENOENT;
		goto out;
	}

	pInfo = _INFO(pVol);
	FFAT_ASSERT(pInfo);

	_INFO_FLAG(pInfo)	= HPA_NONE;						// no flag
	_INFO_IC(pInfo)		= VOL_LCN(pVol);
	_INFO_RC(pInfo)		= FFAT_BO_UINT32(pInfoSector->dwHPARootCluster);
	_INFO_FSMC(pInfo)	= FFAT_BO_UINT32(pInfoSector->dwFSMCluster);
	_INFO_CMC(pInfo)	= FFAT_BO_UINT32(pInfoSector->dwCMCluster);
	FFAT_GetSectorOfCluster((FFatVol*)pVol, _INFO_CMC(pInfo), &dwClusterBitmapSector);
	_INFO_CMS(pInfo)	= dwClusterBitmapSector;
	_INFO_TCC(pInfo)	= FFAT_FREE_CLUSTER_INVALID;	// no information now

	pInfo->dwEntryCount	= FFAT_BO_INT32(pInfoSector->dwEntryCount);
	FFAT_MEMCPY(pInfo->pRootDe, pInfoSector->pHPARootDE, 
							(sizeof(FatDeSFN) * pInfo->dwEntryCount));

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pInfo->dwInfoCluster) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_RC(pInfo)) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_FSMC(pInfo)) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_CMC(pInfo)) == FFAT_TRUE);

	// read bitmap
	r = _readBitmap(pVol, pCxt);
	FFAT_EO(r, (_T("Fail to read bitmap from device")));

	// FAT information
	r = _updateLowestHPAFatSector(pVol);
	FFAT_EO(r, (_T("Fail to get the lowest HPA sector")));

	// set the highest HPA Sector that has free cluster
	_INFO_HFHPAS(pInfo) = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), _INFO_IC(pInfo));

	r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), _INFO_HFHPAS(pInfo), &_INFO_FCH(pInfo));
	FFAT_EO(r, (_T("fail to get the first cluster of a FAT sector")));

	r = FFAT_OK;
out:

	FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

	return r;
}


/**
* read bitmap from bitmap cluster
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the code to build parital HPA bitmap after reading the entire cluster bitmap.
* @version		Nov-26-2009 [SangYoon Oh] Fix the bug  that falsely set the partial HPA bit for the last FAT sector 
																		which is in fact not a partial HPA but a full HPA one.[CQ:25992]
* @version		Dec-10-2009 [SangYoon Oh] Add the code to clear FHPA bitmap if it has not yet been cleared 
																		due to the previous sudden power loss
*/
static FFatErr
_readBitmap(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	t_int32			dwBitmapSize;
	t_uint32		dwClusterBitmapCluster;
	t_uint32		dwClusterBitmapSize;
	t_uint32		dwClusterBitmapPerFatSector;
	t_uint32		dwClusterBitmapPerFatSectorBit;
	t_uint32		dwClusterCount;
	t_uint32		dwCluster;
	t_uint32		dwClusterIdx;
	t_uint32		dwSector;
	FFatErr			r;
	t_int8*			pBuff;
	t_int8*			pClusterBitmapBuff;
	t_int32			dwSize;
	t_int32			dwOffset;
	t_boolean		bNeedUpdateFHPA;

	FFAT_ASSERT(pVol);
	pInfo = _INFO(pVol);
	FFAT_ASSERT(pInfo);

	pBuff = NULL;
	pClusterBitmapBuff = NULL;
	bNeedUpdateFHPA = ESS_FALSE;
	dwBitmapSize	= _FBITMAP_SIZE(pVol);
	dwClusterCount	= dwBitmapSize >> VOL_CSB(pVol);

	//1. Build Full HPA Bitmap
	if (dwClusterCount > 0)
	{
		r = ffat_readWriteCluster(pVol, NULL, _INFO_FSMC(pInfo),
					(t_int8*)_INFO_BITMAP_FHPA(pInfo), dwClusterCount,
					FFAT_TRUE, FFAT_CACHE_NONE, pCxt);
		FFAT_EO(r, (_T("Fail to read bitmap data")));
	}

	if (dwBitmapSize & VOL_CSM(pVol))
	{
		// get read cluster number
		r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), _INFO_FSMC(pInfo),
						dwBitmapSize, &dwCluster, pCxt);
		FFAT_EO(r, (_T("fail to get the last cluster for bitmap cluster")));

		pBuff = (t_int8*)(_INFO_BITMAP_FHPA(pInfo) + (dwClusterCount << VOL_CSB(pVol)));
		dwSize = dwBitmapSize & VOL_CSM(pVol);
		r = ffat_readWritePartialCluster(pVol, NULL, dwCluster, 0, dwSize,
						pBuff, FFAT_TRUE, FFAT_CACHE_NONE, pCxt);
		if (r != dwSize)
		{
			FFAT_EO(r, (_T("fail to read the last bitmap cluster")));
		}
	}

	//2. Build Partial HPA Bitmap from Cluster Bitmap
	dwClusterBitmapCluster = _INFO_CMC(pInfo);
	dwClusterBitmapSize	= _CBITMAP_SIZE(pVol);
	dwClusterCount	= dwClusterBitmapSize >> VOL_CSB(pVol);
	dwClusterBitmapPerFatSector = VOL_CBCPFS(pVol);
	dwClusterBitmapPerFatSectorBit = EssMath_Log2(dwClusterBitmapPerFatSector);
	
	pClusterBitmapBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_CS(pVol), pCxt);
	FFAT_ASSERT(pClusterBitmapBuff);

	//Body
	for (dwClusterIdx = 0; dwClusterIdx < dwClusterCount; dwClusterIdx++)
	{
		r = ffat_readWriteCluster(pVol, NULL, dwClusterBitmapCluster + dwClusterIdx, 
					pClusterBitmapBuff, 1,
					FFAT_TRUE, FFAT_CACHE_NONE, pCxt);
		FFAT_EO(r, (_T("Fail to read cluster bitmap data")));

		for (dwOffset = 0; dwOffset < VOL_CS(pVol); dwOffset += dwClusterBitmapPerFatSector)
		{
			if (0 <= EssBitmap_GetLowestBitOne((t_uint8*)(pClusterBitmapBuff + dwOffset), dwClusterBitmapPerFatSector))
			{
				dwSector = (dwClusterIdx * VOL_CS(pVol) + dwOffset) >> dwClusterBitmapPerFatSectorBit;
				ESS_BITMAP_SET(_INFO_BITMAP_PHPA(pInfo), dwSector);
			
				//Clear FHPA bitmap if it has not yet been cleared due to the previous sudden power loss
				if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FHPA(pInfo), dwSector) == ESS_TRUE)
				{
					FFAT_ASSERT(bNeedUpdateFHPA == ESS_FALSE);
					ESS_BITMAP_CLEAR(_INFO_BITMAP_FHPA(pInfo), dwSector);
					bNeedUpdateFHPA = ESS_TRUE;
				}
			}
		}			
	}

	//Head or Tail	
	if (dwClusterBitmapSize & VOL_CSM(pVol))
	{
		//Bug fix: Must Initialize buffer before reading the cluster bitmap[CQ:25992]
		FFAT_MEMSET(pClusterBitmapBuff, 0x00, VOL_CS(pVol));

		dwSize = dwClusterBitmapSize & VOL_CSM(pVol);
		r = ffat_readWritePartialCluster(pVol, NULL, dwClusterBitmapCluster + dwClusterIdx, 0, dwSize,
						pClusterBitmapBuff, FFAT_TRUE, FFAT_CACHE_NONE, pCxt);
		if (r != dwSize)
		{
			FFAT_EO(r, (_T("fail to read the last cluster bitmap data")));
		}

		for (dwOffset = 0; dwOffset < dwSize; dwOffset+= dwClusterBitmapPerFatSector)
		{
			if (0 <= EssBitmap_GetLowestBitOne((t_uint8*)(pClusterBitmapBuff + dwOffset), dwClusterBitmapPerFatSector))
			{
				dwSector = (dwClusterIdx * VOL_CS(pVol) + dwOffset) >> dwClusterBitmapPerFatSectorBit;
				ESS_BITMAP_SET(_INFO_BITMAP_PHPA(pInfo), dwSector);

				//Clear FHPA bitmap if it has not yet been cleared due to the previous sudden power loss
				if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FHPA(pInfo), dwSector) == ESS_TRUE)
				{
					FFAT_ASSERT(bNeedUpdateFHPA == ESS_FALSE);
					ESS_BITMAP_CLEAR(_INFO_BITMAP_FHPA(pInfo), dwSector);
					bNeedUpdateFHPA = ESS_TRUE;
				}
			}
		}	
	}

	if (bNeedUpdateFHPA == ESS_TRUE)
	{
			// write FHPA bitmap
			r = _writeBitmap(pVol, VOL_FFS(pVol), VOL_LVFSFF(pVol), pCxt);
			FFAT_EO(r, (_T("fail to write bitmap")));		
	}

	r = FFAT_OK;
out:
	if (pClusterBitmapBuff)
		FFAT_LOCAL_FREE(pClusterBitmapBuff, VOL_CS(pVol), pCxt);
	return r;
}


/**
* write info sector to the device
*
* @param		pVol		: [IN] volume pointer
* @param		pInfoSector	: [IN] HPA information sector data structure
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_writeInfoSector(Vol* pVol, HPAInfoSector* pInfoSector, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwInfoSector;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pInfoSector);

	// get the first cluster for bitmap cluster
	dwInfoSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), VOL_LCN(pVol));

	r = ffat_readWritePartialSector(pVol, NULL, dwInfoSector, 0, sizeof(HPAInfoSector), 
						(t_int8*)pInfoSector, (FFAT_CACHE_DATA_FS | FFAT_CACHE_SYNC),
						FFAT_FALSE, pCxt);
	if (r != sizeof(HPAInfoSector))
	{
		FFAT_LOG_PRINTF((_T("fail to write info sector")));
		if (r < 0)
		{
			return r;
		}
		else
		{
			return FFAT_EIO;
		}
	}

	return FFAT_OK;
}


/**
* write bitmap to the device
*
* @param		pVol		: [IN] volume pointer
* @param		dwFrom		: [IN] bitmap update start FAT sector number
* @param		dwTo		: [IN] bitmap update end FAT sector number
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_writeBitmap(Vol* pVol, t_uint32 dwFrom, t_uint32 dwTo, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	FFatErr			r;
	t_uint32		dwFirstSector;
	t_uint32		dwLastSector;
	t_int32			dwSectorOffset;
	t_uint32		dwFirstBitmapSector;
	t_uint32		dwLastBitmapSector;
	t_uint32		i;
	t_int8*			pBuff;

	FFAT_ASSERT(pVol);

	pInfo = _INFO(pVol);
	FFAT_ASSERT(pInfo);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_FSMC(pInfo)) == FFAT_TRUE);

	// get the first cluster for bitmap cluster
	dwFirstBitmapSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), _INFO_FSMC(pInfo));
	dwLastBitmapSector = dwFirstBitmapSector + (_FBITMAP_SIZE(pVol) >> VOL_SSB(pVol));

	dwFrom	= (dwFrom - VOL_FFS(pVol)) >> 3;	// bit -> byte
	dwTo	= (dwTo - VOL_FFS(pVol)) >> 3;	// bit -> byte

	// get the first write sector
	dwFirstSector = dwFirstBitmapSector + (dwFrom >> VOL_SSB(pVol));
	// get the last write sector
	dwLastSector = dwFirstBitmapSector + (dwTo >> VOL_SSB(pVol));
	dwSectorOffset = dwFirstSector - dwFirstBitmapSector;

	pBuff = (t_int8*)(_INFO_BITMAP_FHPA(pInfo) + (dwSectorOffset << VOL_CSB(pVol)));

	for (i = dwFirstSector; i <= dwLastSector; i++)
	{
		if (i == dwLastBitmapSector)
		{
			r = ffat_readWritePartialSector(pVol, NULL, i, 0, 
							(t_int32)((dwTo & VOL_SSM(pVol)) + 1),
							pBuff, (FFAT_CACHE_DATA_FS | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
		}
		else
		{
			r = ffat_readWriteSectors(pVol, NULL, i, 1, pBuff, 
							(FFAT_CACHE_DATA_FS | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
		}
		FFAT_ER(r, (_T("fail to write bitmap")));

		pBuff += VOL_SS(pVol);
	}

	return FFAT_OK;
}

/**
* init cluster bitmap clusters
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: successfully initiatated the cluster bitmap clusters
* @return		FFAT_EIO	: IO Error
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/
static FFatErr
_initClusterBitmap(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	FFatErr			r;
	t_uint32		dwCluster;
	t_uint32		dwCount;

	FFAT_ASSERT(pVol);

	pInfo = _INFO(pVol);
	FFAT_ASSERT(pInfo);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_FSMC(pInfo)) == FFAT_TRUE);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_CMC(pInfo)) == FFAT_TRUE);

	dwCluster = _INFO_CMC(pInfo);
	dwCount = _INFO_FSMC(pInfo) - _INFO_CMC(pInfo);
	r = ffat_initCluster(pVol, NULL, dwCluster, dwCount, 
					(FFAT_CACHE_DATA_FS | FFAT_CACHE_SYNC), pCxt);
	FFAT_ER(r, (_T("fail to init cluster bitmap")));

	return FFAT_OK;
}

/**
* generate the directory entries for HPA Root directory
*
* @param		pVol		: [IN] volume pointer
* @param		pNodeHPARoot: [IN] root node of HPA
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_genHPARootDe(Vol* pVol, Node* pNodeHPARoot, ComCxt* pCxt)
{
	FFatHPAConfig*		pConf;
	t_int32				dwNameLen;
	t_int32				dwNamePartLen;
	t_int32				dwExtPartLen;
	t_int32				dwSfnNameSize;
	FatNameType			dwNameType;
	HPAInfo*			pHPAInfo;
	FatDeUpdateFlag		dwDeUpdateFlag;
//2009.0914@chunum.kong_[VFAT_OFF]_No_use_LFN
#ifdef FFAT_VFAT_SUPPORT	
	t_int8				bCheckSum;
#endif
	t_int32				dwLFNE_Count;
	FFatErr				r;

	FFAT_ASSERT(pVol);

	pConf		= _CONFIG();
	pHPAInfo	= _INFO(pVol);

	FFAT_ASSERT(pHPAInfo);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), NODE_C(pNodeHPARoot)) == FFAT_TRUE);

	// generate DE
	dwNameLen = 0;	// i do not have string length now
	r = FFATFS_AdjustNameToFatFormat(VOL_VI(pVol), pConf->psHPARootName, &dwNameLen,
						&dwNamePartLen, &dwExtPartLen, &dwSfnNameSize, &dwNameType, NODE_DE(pNodeHPARoot));
	FFAT_EO(r, (_T("fail to adjust name to FAT format")));

	if (dwNameLen > FFAT_HPA_ROOT_NAME_MAX_LENGTH)
	{
		FFAT_LOG_PRINTF((_T("Too long root directory name")));
		r = FFAT_EINVALID;
		goto out;
	}

	if (dwNameType & FAT_NAME_SFN)
	{
		pNodeHPARoot->dwFlag |= NODE_NAME_SFN;
		dwLFNE_Count = 0;
	}
	else
	{
#ifdef FFAT_VFAT_SUPPORT
		// generate long file name entry
		bCheckSum = FFATFS_GetCheckSum(&pNodeHPARoot->stDE);
		pNodeHPARoot->wNameLen = (t_int16)dwNameLen;

		// generate directory entry
		r = FFATFS_GenLFNE(pConf->psHPARootName, pNodeHPARoot->wNameLen,
					(FatDeLFN*)(pHPAInfo->pRootDe), &dwLFNE_Count, bCheckSum);
		FFAT_EO(r, (_T("fail to generate long file name entry")));

		FFAT_ASSERT(dwLFNE_Count < FFAT_HPA_ROOT_DE_COUNT);
#else
		FFAT_ASSERT(0);
#endif
	}

	dwDeUpdateFlag = FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_ATTR | FAT_UPDATE_DE_ALL_TIME
						| FAT_UPDATE_DE_CLUSTER;

	r = ffat_node_updateSFNE(pNodeHPARoot, 0, (FFAT_ATTR_HIDDEN | FFAT_ATTR_DIR | FFAT_ATTR_SYS),
					NODE_C(pNodeHPARoot), dwDeUpdateFlag, FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to update SFNE")));

	FFAT_MEMCPY(&pHPAInfo->pRootDe[dwLFNE_Count], NODE_DE(pNodeHPARoot), sizeof(FatDeSFN));
	pHPAInfo->dwEntryCount = dwLFNE_Count + 1;

	r = FFAT_OK;

out:
	return r;
}


/**
* check the last and {the last -1}th FAT sector is free
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: there is a HPA on the volume
* @return		FFAT_EIO	: IO Error
* @author		DongYoung Seo
* @version		MAY-25-2007 [DongYoung Seo] First Writing.
* @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
* @version		Aug-29-2009 [SangYoon Oh]	Add the code to check the clusters from the cluster bitmap cluster
											to the last cluster are free
*/
static FFatErr
_checkFreeFatSectorForHPACreate(Vol* pVol, ComCxt* pCxt)
{
	t_uint32		dwLastFatSector;
	t_uint32		dwRootCluster;
	t_uint32		dwFSMC;
	t_uint32		dwCMC;
	t_uint32		i;
	FatVolInfo*		pVI;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_FSC(pVol) > 1);

	pVI = VOL_VI(pVol);
	FFAT_ASSERT(pVI);
	dwLastFatSector = VOL_LVFSFF(pVol);
	
	r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
	FFAT_ER(r, (_T("fail to sync FCC")));

	dwRootCluster	= VOL_LCN(pVol) - 1;
	dwFSMC			= dwRootCluster - ESS_MATH_CDB(_FBITMAP_SIZE(pVol), VOL_CS(pVol), VOL_CSB(pVol));
	dwCMC			= dwFSMC - ESS_MATH_CDB(_CBITMAP_SIZE(pVol), VOL_CS(pVol), VOL_CSB(pVol));

	// check clusters from the cluster bitmap cluster to the last cluster are FREE
	for (i = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwCMC); i <= dwLastFatSector; i++)
	{
		r = FFATFS_IsFreeFatSector(pVI, i, FFAT_CACHE_DATA_FAT, pCxt);
	if (r != FFAT_TRUE)
	{
		if (r == FFAT_FALSE)
		{
			FFAT_LOG_PRINTF((_T("last FAT sector is not a free one ")));
			return FFAT_ENOSPC;
		}

		FFAT_LOG_PRINTF((_T("fail to check the last cluster")));
		
		return r;
	}
	}
	return FFAT_OK;
}


/**
* check the node is HPA root node
*
* @param		pVol		: [IN] volume pointer
* @return		FFAT_TRUE	: This is a HPA root node
* @return		FFAT_FALSE	: This is not a HPA root node
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
*/
static t_boolean
_isHPARoot(Node* pNode)
{
	Vol*		pVol;
	HPAInfo*	pInfo;
	FFAT_ASSERT(pNode);

	pVol	= NODE_VOL(pNode);
	pInfo	= _INFO(pVol);

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pInfo);

	if (NODE_C(pNode) == _INFO_RC(pInfo))
	{
		FFAT_ASSERT(pNode->stDeInfo.dwDeStartCluster == _INFO_IC(pInfo));
		FFAT_ASSERT(pNode->stDeInfo.dwDeStartOffset == _ROOT_DE_START_OFFSET(pInfo));
		FFAT_ASSERT((pNode->stDeInfo.dwDeStartOffset & FAT_DE_SIZE_MASK) == 0);
		FFAT_ASSERT(pNode->stDeInfo.dwDeEndCluster == _INFO_IC(pInfo));
		FFAT_ASSERT(pNode->stDeInfo.dwDeEndOffset== _ROOT_DE_END_OFFSET(pInfo));
		FFAT_ASSERT((pNode->stDeInfo.dwDeEndOffset & FAT_DE_SIZE_MASK) == 0);
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/**
* update the lowest FAT sector for HPA.
*
* @param		pVol		: [IN] volume pointer
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add code to permit the Lowest HPA Sector having an invalid FAT sector
*/
static FFatErr
_updateLowestHPAFatSector(Vol* pVol)
{
	HPAInfo*		pInfo;
	t_int32			dwIndex = 0;
	t_int32			dwBitmapSize;

	FFAT_ASSERT(pVol);

	dwBitmapSize = _FBITMAP_SIZE(pVol);
	pInfo = _INFO(pVol);

	dwIndex = EssBitmap_GetLowestBitOne(_INFO_BITMAP_FHPA(pInfo), dwBitmapSize);
	IF_UK (dwIndex < 0)
	{
		//set it as an invalid FAT sector;
		_INFO_LFHPAS(pInfo) = VOL_LVFSFF(pVol) + 1;
		return FFAT_OK;
	}

	_INFO_LFHPAS(pInfo) = VOL_FFS(pVol) + dwIndex;

	FFAT_ASSERT(VOL_LVFSFF(pVol) >= _INFO_LFHPAS(pInfo));

	return FFAT_OK;
}


/**
* check if the given FAT sector is an HPA sector
*
* @param		pVol		: [IN] volume pointer
* @param		dwSector	: [IN] FAT sector to be checked
* @return		FFAT_TRUE	: This is an HPA sector
* @return		FFAT_FALSE	: This is not an HPA sector
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
* @version		OCT-11-2008 [DongYoung Seo] ADD ASSERT to check validity of dwSector
* @version		OCT-11-2008 [DongYoung Seo] update last valid FAT sector checking routine
*/
static t_boolean
_isFATSectorFullHPA(Vol* pVol, t_uint32 dwSector)
{
	HPAInfo*		pInfo;

	FFAT_ASSERT(pVol);

	FFAT_ASSERT(dwSector >= VOL_FFS(pVol));
	FFAT_ASSERT(dwSector <= VOL_LFS(pVol));

	if (dwSector >= (VOL_FFS(pVol) + VOL_FSC(pVol)))
	{
		FFAT_ASSERT(dwSector > VOL_FSC(pVol));
		do
		{
			dwSector -= VOL_FSC(pVol);
		} while (dwSector <= VOL_LFSFF(pVol));
	}

	// Check FAT sector over have cluster area
	if (dwSector > VOL_LVFSFF(pVol))
	{
		return FFAT_FALSE;
	}

	FFAT_ASSERT(dwSector <= VOL_LVFSFF(pVol));

	dwSector -= VOL_FFS(pVol);
	pInfo = _INFO(pVol);

	return ESS_BITMAP_IS_SET(_INFO_BITMAP_FHPA(pInfo), dwSector) == ESS_TRUE ? FFAT_TRUE : FFAT_FALSE;
}

/**
* check if the given FAT sector is a partial HPA sector
*
* @param		pVol		: [IN] volume pointer
* @param		dwSector	: [IN] FAT sector to be checked
* @return		FFAT_TRUE	: This is a partial HPA sector
* @return		FFAT_FALSE	: This is not a partial HPA sector
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/
static t_boolean
_isFATSectorPartialHPA(Vol* pVol, t_uint32 dwSector)
{
	HPAInfo*		pInfo;

	FFAT_ASSERT(pVol);

	FFAT_ASSERT(dwSector >= VOL_FFS(pVol));
	FFAT_ASSERT(dwSector <= VOL_LFS(pVol));

	if (dwSector >= (VOL_FFS(pVol) + VOL_FSC(pVol)))
	{
		FFAT_ASSERT(dwSector > VOL_FSC(pVol));
		do
		{
			dwSector -= VOL_FSC(pVol);
		} while (dwSector <= VOL_LFSFF(pVol));
	}

	// Check FAT sector over have cluster area
	if (dwSector > VOL_LVFSFF(pVol))
	{
		return FFAT_FALSE;
	}

	FFAT_ASSERT(dwSector <= VOL_LVFSFF(pVol));

	dwSector -= VOL_FFS(pVol);
	pInfo = _INFO(pVol);

	return ESS_BITMAP_IS_SET(_INFO_BITMAP_PHPA(pInfo), dwSector) == ESS_TRUE ? FFAT_TRUE : FFAT_FALSE;
}

/**
* increase HPA
* the lowest FAT sector
*
* @param		pVol		: [IN] volume pointer
* @param		pdwFatSector: [IN/OUT] new FAT sector for HPA
* @param		pCxt		: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-27-2007 [DongYoung Seo] First Writing.
* @version		JAN-13-2009 [DongYoung Seo] bug fix : add FFSCN increase / decrease code
*											for correct counting
* @version		JAN-14-2009 [JeongWoo Park] Add the code to call _syncUpdateBitForFatSector()
* @version		Aug-29-2009 [SangYoon Oh] Add the code to clear partial HPA bitmap of the enlarged HPA sector
*/
static FFatErr
_enlargeHPA(Vol* pVol, t_uint32* pdwFatSector, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		i;
	t_uint32		dwLFS;		// the last Sector of 1st FAT
	t_uint32		dwFFS;		// the first sector of 1st FAT
	HPAInfo*		pInfo;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwFatSector);

	pInfo = _INFO(pVol);

	// check HPA area from the end of the volume
	dwFFS = VOL_FFS(pVol);
	dwLFS = VOL_LVFSFF(pVol);

	for (i = dwLFS; i >= dwFFS; i--)
	{
		if (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
		{
			continue;
		}

		// check whether the FAT sector is candidate
		if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), (i - VOL_FFS(pVol))) == ESS_TRUE)
		{
			r = _syncUpdateBitForFatSector(pVol, i, pCxt);
			FFAT_ER(r, (_T("fail to sync the updated bit of FAT Sector")));
		}

		// check bitmap
		if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (i - VOL_FFS(pVol))) == ESS_TRUE)
		{
			FFAT_ASSERT(ffat_addon_misc_isFreeFATSector(pVol, i, pCxt) == FFAT_TRUE);

			// OK!, got it.
			// set bit map
			_SET_BITMAP_FHPA(pVol, i);
			
			//Full HPA sector cannot be Partial HPA sector
			_CLEAR_BITMAP_PHPA(pVol, i);

			// clear bitmap free
			_CLEAR_BITMAP_FREE_FOR_NORMAL_AREA(pVol, i);
			
			// decrease free fat sector count on normal area [NORMAL -> HPA]
			_INFO_FFSCN_DEC(pInfo);

			FFAT_ASSERT((_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE) ? (_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol)) : FFAT_TRUE);

			// write bit map
			r = _writeBitmap(pVol, i, i, pCxt);
			FFAT_ER(r, (_T("fail to write bitmap")));

			*pdwFatSector = i;

			// update the lowest HPA sector number
			r = _updateLowestHPAFatSector(pVol);
			FFAT_ER(r, (_T("Fail to get the lowest HPA sector")));

			// update the highest HPA free sector number
			if (pInfo->dwHighestFreeFullHPASector < i)
			{
				pInfo->dwHighestFreeFullHPASector = i;
			}

			FFAT_DEBUG_HPA_PRINTF((_T("HPA Extended !! New Sector:%d\n"), i));

			return FFAT_OK;
		}
	}

	return FFAT_ENOSPC;
}

/**
* convert an HPA sector to a partial HPA one
*
* @param		pVol		: [IN] volume pointer
* @param		dwFatSector: [IN] HPA FAT sector to be converted to a partial HPA
* @param		pCxt		: [IN] context of current operation
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/
static FFatErr
_convertToPartialHPA(Vol* pVol, t_uint32 dwFatSector, ComCxt* pCxt)
{
	FFatErr			r;
	HPAInfo*		pInfo;
	t_uint32		dwLFS;		// the last Sector of 1st FAT
	t_uint32		dwCluster;
	t_uint32		dwBitmapSector;
	t_uint32		dwBitmapOffset;
	t_uint32		dwClusterIdx;
	t_int8*			pBuff = NULL;
	t_int8*			pBitmapBuff = NULL;
	t_uint32		dwCCPFSMask;
	FatVolInfo*		pVI;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwFatSector);
	
	pVI = VOL_VI(pVol);
	dwLFS = VOL_LVFSFF(pVol);
	dwCCPFSMask = VI_CCPFS_MASK(pVI);

	pInfo = _INFO(pVol);
	FFAT_ASSERT(_IS_VALID_LFHPAS(pVol));

	//1. Update bitmap
	FFAT_ASSERT(_isFATSectorFullHPA(pVol, dwFatSector) == FFAT_TRUE);
	_CLEAR_BITMAP_FHPA(pVol, dwFatSector);
	
	FFAT_ASSERT(_isFATSectorPartialHPA(pVol, dwFatSector) == FFAT_FALSE);
	_SET_BITMAP_PHPA(pVol, dwFatSector);

	//2. Write Cluster map
	// FAT Table을 scan하여 기존에 Alloc된 Cluster에 해당하는 Cluster Bitmap setting
	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	pBitmapBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBitmapBuff != NULL);

	//원본 FAT Sector를 읽음
	r = ffat_readWriteSectors(pVol, NULL, dwFatSector, 1, pBuff, 
			(FFAT_CACHE_DATA_FAT), FFAT_TRUE, pCxt);
	FFAT_EO(r, (_T("fail to read FAT Sector")));

	r = FFATFS_GetFirstClusterOfFatSector(pVI, dwFatSector, &dwCluster);
	FFAT_EO(r, (_T("fail to get the first cluster of a FAT sector")));

	dwBitmapSector = _GET_CBITMAP_SECTOR_OF_CLUSTER(pVol, dwCluster);		
	dwBitmapOffset = dwCluster & VOL_CBCPS_MASK(pVol);
	
	//Bitmap Sector를 읽음
	r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBitmapBuff, 
			(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
	FFAT_EO(r, (_T("fail to read Cluster Bitmap Sector")));
	
	for (dwClusterIdx = 0; dwClusterIdx < (t_uint32)VOL_CCPFS(pVol); dwClusterIdx++)
	{
		//Cluster가 할당된 경우 Cluster Bitmap Set
		if (FFATFS_IS_FREE_CLUSTER_BUFFER(pVI, pBuff, dwClusterIdx, dwCCPFSMask) == FFAT_FALSE)
		{			
			ESS_BITMAP_SET(pBitmapBuff, dwBitmapOffset);
		}
		else
		{
			ESS_BITMAP_CLEAR(pBitmapBuff, dwBitmapOffset);
		}
		dwBitmapOffset++;
	}

	r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBitmapBuff, 
				FFAT_CACHE_DATA_FS | FFAT_CACHE_SYNC , FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to write Cluster Bitmap Sector")));

	// 3. Write HPA map
	r = _writeBitmap(pVol, dwFatSector, dwFatSector, pCxt);
	FFAT_EO(r, (_T("fail to write bitmap")));
	
	// 4. update the lowest HPA sector number
	r = _updateLowestHPAFatSector(pVol);
	FFAT_EO(r, (_T("Fail to get the lowest HPA sector")));

	FFAT_DEBUG_HPA_PRINTF((_T("HPA is converted to Partial HPA !! New Sector:%d\n"), dwFatSector));
out:
	if (pBitmapBuff)
	{
		FFAT_LOCAL_FREE(pBitmapBuff, VOL_SS(pVol), pCxt);
	}

	if (pBuff)
	{
		FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);
	}

	return FFAT_OK;

}

/**
* get Free Clusters between dwFrom and dwTo
*
* store free cluster information at pVC
*
* @param		pVol			: [IN] volume pointer
* @param		dwHint			: [IN] free cluster lookup hint 
* @param		dwFrom			: [IN] free cluster lookup start cluster
* @param		dwTo			: [IN] free cluster lookup end cluster
* @param		dwCount			: [IN] cluster allocation count
* @param		pVC				: [IN/OUT] allocated cluster information 
* @param		pdwFreeCount	: [OUT] allocate cluster count
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		FFAT_OK			: success - partially success.
*									do not gather required free clusters.unt of free clusters are successfully gathered.
* @return		FFAT_ENOSPC		: not enough free cluster
*									Not enough VC Entry
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_getFreeClustersFromTo(Vol* pVol, t_uint32 dwHint,
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	FFatErr		r;

	if (dwFrom > dwTo)
	{
		*pdwFreeCount = 0;
		return FFAT_ENOSPC;
	}

	r = ffat_fcc_getFreeClustersFromTo(pVol, dwHint, dwFrom, dwTo, dwCount, pVC, pdwFreeCount, pCxt);
	if (r == FFAT_DONE)
	{
		// ADDON module에 의해 get Free cluster가 모두 수행되었다.
		FFAT_ASSERT(dwCount == *pdwFreeCount);
		FFAT_ASSERT(*pdwFreeCount > 0);
	}
	else if (r == FFAT_ENOSPC)
	{
		FFAT_ASSERT((dwCount > *pdwFreeCount) || (VC_IS_FULL(pVC) == FFAT_TRUE));
	}
	else
	{
		FFAT_ASSERT(0);
		FFAT_LOG_PRINTF((_T("error on ADDON module")));
	}

	return r;
}


/**
* get Free clusters between dwFrom and dwTo for HPA (or Normal if bHPA is false)
*
* store allocate cluster information at pVC
* if there is not enough free space
*
* @param		pVol			: [IN] volume pointer
* @param		dwFrom			: [IN] free cluster lookup start cluster
* @param		dwTo			: [IN] free cluster lookup end cluster
* @param		dwCount			: [IN] cluster allocation count
* @param		pVC				: [IN/OUT] allocated cluster information 
* @param		pdwAllocatedCount: [OUT] allocate cluster count
* @param		bHPA			: [IN] getting clusters for HPA or normal node 
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success - partially success.
*									do not gather required free clusters.
* @return		FFAT_DONE		: required count of free clusters are successfully gathered.
* @return		FFAT_ENOSPC		: there is not enough free cluster
*									there is no room at pVC
* @return		else			: error
* @author		DongYoung Seo 
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter bHPA to identify which area requires free clusters
*/
static FFatErr
_getFreeClustersFromToHPA(Vol* pVol, t_uint32 dwHint, 
							t_uint32 dwFrom, t_uint32 dwTo, t_uint32 dwCount,
							FFatVC* pVC, t_uint32* pdwFreeCount, t_boolean bHPA, ComCxt* pCxt)
{
	t_uint32	dwFFS;
	t_uint32	dwLFS;
	t_uint32	i;
	t_uint32	dwCurFrom;
	t_uint32	dwCurTo;
	t_uint32	dwFreeCount = 0;
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwFreeCount);

	*pdwFreeCount = 0;

	if (dwCount == 0)
	{
		*pdwFreeCount = 0;
		return FFAT_OK;
	}

	if (dwFrom > dwTo)
	{
		return FFAT_ENOSPC;
	}

	if (dwHint < 2)
	{
		dwHint = 2;
	}

	if (dwFrom < 2)
	{
		dwFrom = 2;
	}

	dwFFS = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwFrom);
	dwLFS = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwTo);

	if ((bHPA == FFAT_TRUE) && (dwFFS < _INFO_LFHPAS(_INFO(pVol))))
	{
		dwFFS = _INFO_LFHPAS(_INFO(pVol));
	}

	if (dwLFS < dwFFS)
	{
		return FFAT_ENOSPC;
	}

	for (i = dwLFS; i >= dwFFS; i--)
	{
		if ((bHPA == FFAT_TRUE) && (_isFATSectorFullHPA(pVol, i)) == FFAT_FALSE)
		{
			continue;
		}
		else if ((bHPA == FFAT_FALSE) && (_isFATSectorFullHPA(pVol, i)) == FFAT_TRUE)
		{
			continue;
		}

		r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), i, &dwCurFrom);
		FFAT_EO(r, (_T("fail to get the first cluster of a cluster")));

		r = FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), i, &dwCurTo);
		FFAT_EO(r, (_T("fail to get the first cluster of a cluster")));

		if (dwCurFrom < dwFrom)
		{
			dwCurFrom = dwFrom;
		}

		if (dwCurTo > dwTo)
		{
			dwCurTo = dwTo;
		}

		r = _getFreeClustersFromTo(pVol, dwHint, dwCurFrom, dwCurTo,
								(dwCount - *pdwFreeCount), pVC, &dwFreeCount, pCxt);
		IF_LK (r == FFAT_DONE)
		{
			*pdwFreeCount += dwFreeCount;
			goto out;
		}
		else if (r < 0)
		{
			if (r == FFAT_ENOSPC) 
			{
				*pdwFreeCount += dwFreeCount;

				if (VC_IS_FULL(pVC) != FFAT_TRUE)
				{
					// let's get another free clusters
					continue;
				}
			}

			goto out;
		}

		FFAT_ASSERT(0);
	}

	r = FFAT_ENOSPC;

out:

	FFAT_ASSERT((r == FFAT_DONE) ? (dwCount == *pdwFreeCount) : FFAT_TRUE);
	FFAT_ASSERT((r == FFAT_ENOSPC) ? (dwCount > *pdwFreeCount) : FFAT_TRUE);

	return r;
}


/**
* increase total cluster count for HPA
*
* @param		pVol			: [IN] volume pointer
* @param		dwCount	: [IN] cluster count
* @param		pCxt			: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Modify the assert code to enable TCC of HPA to be equal to the CC of VOL
* @version		Nov-26-2009 [SangYoon Oh] Add the parameter ComCxt when to verify the total cluster count of HPA
 
*/
static void
_incTotalClusterHPA(Vol* pVol, t_uint32 dwCount, ComCxt* pCxt)
{
	HPAInfo*	pInfo;
#ifdef _HPA_DEBUG
	t_uint32 a;
	t_uint32 b;
#endif
	FFAT_ASSERT(pVol);


	pInfo = _INFO(pVol);

	if (_INFO_TCC(pInfo) == FFAT_FREE_CLUSTER_INVALID)
	{
		return;
	}
	
	_INFO_TCC(pInfo) += dwCount;
	FFAT_ASSERT(_INFO_TCC(pInfo) <= VOL_CC(pVol));

#ifdef _HPA_DEBUG
	FFAT_DEBUG_HPA_PRINTF((_T("Increase Total Clusters for HPA %d->%d \n"), _INFO_TCC(pInfo) - dwCount, _INFO_TCC(pInfo)));
	a = _INFO_TCC(pInfo);
	b = _debugGetTCCOfHPA(pVol, pCxt);
	FFAT_ASSERT( a ==  b);	
	//FFAT_ASSERT(_INFO_TCC(pInfo) ==  _debugGetTCCOfHPA(pVol, pCxt));	
#endif 

	return;
}


/**
* decrease total cluster count for HPA
*
* @param		pVol			: [IN] volume pointer
* @param		dwCount	: [IN] cluster count
* @param		pCxt			: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
*/
static void
_decTotalClusterHPA(Vol* pVol, t_uint32 dwCount)
{
	HPAInfo*	pInfo;

	FFAT_ASSERT(pVol);

	pInfo = _INFO(pVol);

	if (_INFO_TCC(pInfo) == FFAT_FREE_CLUSTER_INVALID)
	{
		return;
	}

	FFAT_ASSERT(_INFO_TCC(pInfo) > dwCount);

	_INFO_TCC(pInfo) -= dwCount;
#ifdef _HPA_DEBUG
	FFAT_DEBUG_HPA_PRINTF((_T("\nDecrease Total Clusters for HPA %d->%d \n"), _INFO_TCC(pInfo) + dwCount,_INFO_TCC(pInfo)));
#endif 

	return;
}

/**
* update free count of Volume and total cluster count of HPA
*
* @param		pVol		: [IN] volume pointer
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_DONE	: success and all cluster informations are updated
* @param		else		: error
* @author		DongYoung Seo
* @version		MAY-28-2007 [DongYoung Seo] First Writing.
* @version		OCT-23-2008 [DongYoung Seo] add FCC sync code before _propagateupdateBitmap().
									_propagateUpdateBitmap() may invoke FFATFS without FCC sync
* @version		JAN-14-2009 [JeongWoo Park] Add the code to call _syncUpdateBitForFatSector()
*											edit sequence of getting Free cluster count of FAT sector
* @version		Aug-29-2009 [SangYoon Oh]	Remove the code regarding FCC of HPA
											Edit the code calculating free cluster count
*/
static FFatErr
_updateVolumeInfo(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	t_uint32		i;
	t_uint32		dwFCC;				// free cluster count
	t_uint32		dwFCCTotal;		// free cluster count
	t_uint32		dwTCCHPA;			// total cluster count for HPA
	t_uint32		dwLVFSFF;			// Last Valid FAT Sector on First FAT
	t_uint32		dwFreeFATSectors;	// count of wholly free FAT sectors for Normal Area
	t_int32			dwBitmapSize;		// size of bitmap
	t_uint32		dwCount;
	FFatErr			r;

	FFAT_ASSERT(pVol);

	pInfo = _INFO(pVol);

	FFAT_ASSERT(VOL_LVFSFF(pVol) == FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), VOL_LCN(pVol)));

	if (_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE)
	{
		FFAT_ASSERT(_INFO_FLAG(pInfo) & HPA_VALID_FREE_BITMAP);
		FFAT_ASSERT(_INFO_FLAG(pInfo) & HPA_VALID_UPDATE_BITMAP);

		if (_INFO_FLAG(pInfo) & HPA_FAT_UPDATED)
		{
			r = _propagateUpdateBitmap(pVol, pCxt);
			FFAT_ER(r, (_T("fail to sync update bitmap and free bitmap")));
		}

		_DEBUG_CHECK_FCC(pVol, pCxt)
		_DEBUG_CHECK_BITMAP_FREE(pVol, pCxt)

		FFAT_ASSERT(_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol));

		return FFAT_DONE;
	}

	FFAT_ASSERT((_INFO_FLAG(pInfo) & HPA_VALID_FREE_BITMAP) == 0);
	FFAT_ASSERT((_INFO_FLAG(pInfo) & HPA_VALID_UPDATE_BITMAP) == 0);

	dwTCCHPA			= 0;
	dwLVFSFF			= VOL_LVFSFF(pVol);
	dwFreeFATSectors	= 0;
	dwFCCTotal			= 0;

	// initialize bitmap area
	dwBitmapSize = _FBITMAP_SIZE(pVol);

	FFAT_MEMSET(_INFO_BITMAP_FREE(pInfo), 0x00, dwBitmapSize);
	FFAT_MEMSET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), 0x00, dwBitmapSize);

	for (i = VOL_FFS(pVol); i <= dwLVFSFF; i++)
	{
		// get free cluster count in fat sector
		r = ffat_fcc_getFCCOfSector(pVol, i, &dwFCC, pCxt);
		FFAT_ER(r, (_T("fail to get Free Cluster Count")));
		if (r != FFAT_DONE)
		{
			// FCC is not activated
			r = FFATFS_GetFCCOfSector(VOL_VI(pVol), i, &dwFCC, pCxt);
			FFAT_ER(r, (_T("fail to get Free Cluster Count")));
		}

		dwFCCTotal += dwFCC;

		if (dwFCC == (t_uint32)VOL_CCPFS(pVol))
		{
			// this is a free FAT sector in normal
			_CLEAR_BITMAP_PHPA(pVol, i);

			if (_isFATSectorFullHPA(pVol, i) == FFAT_FALSE)
			{
				dwFreeFATSectors++;
				_SET_BITMAP_FREE_FOR_NORMAL_AREA(pVol, i);
			}
		}
		else
		{
			FFAT_ASSERT((_isFATSectorFullHPA(pVol, i) && _isFATSectorPartialHPA(pVol, i))== 0);

			//TCC of HPA 계산
			if (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
			{
				// HPA area
				FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (i - VOL_FFS(pVol))) == ESS_FALSE);
				if (i < dwLVFSFF)
				{
					dwTCCHPA += (VOL_CCPFS(pVol) - dwFCC);		// cluster count per a FAT sector
				}
				else
				{
					dwTCCHPA += (VOL_CCLFS(pVol) - dwFCC);		// cluster count on last FAT sector
				}
			}
			else if (_isFATSectorPartialHPA(pVol, i) == FFAT_TRUE)
			{
				dwCount = 0;

				//FAT Sector에 해당하는 Cluster Bitmap을 Scan하여 Set된 Cluster개수를 계산
				r = _getGetPartialHPAClusterCountOfFATSector(pVol, i, &dwCount, pCxt);
				FFAT_ASSERT(r >= 0);
				dwTCCHPA += dwCount;
			}			
		}
	}

	_INFO_TCC(pInfo)	= dwTCCHPA;
	_INFO_FFSCN(pInfo)	= dwFreeFATSectors;

	VOL_FCC(pVol)		= dwFCCTotal;

	_INFO_FLAG(pInfo) |= HPA_VALID_FREE_BITMAP;		// update free bitmap valid flag
	_INFO_FLAG(pInfo) |= HPA_VALID_UPDATE_BITMAP;	// update update bitmap valid flag
	FFAT_ASSERT(_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol));

	_DEBUG_CHECK_BITMAP_FREE(pVol, pCxt)

	return FFAT_DONE;
}

/**
* get partial HPA cluster count of a given Partial HPA FAT Sector
*
* @param		pVol		: [IN] volume pointer
* @param		dwFatSector	: [IN] FAT sector number
* @param		pdwCount	: [OUT] Partial HPA cluster count
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK		: success
* @param		else		: error
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/
static FFatErr
_getGetPartialHPAClusterCountOfFATSector(Vol* pVol, t_uint32 dwFatSector, t_uint32* pdwCount, ComCxt* pCxt)
{
	t_uint32		dwFrom;		
	t_uint32		dwBitmapSector;
	t_uint32		dwOffset;
	t_int8*			pTempBuff = NULL;
	FFatErr			r;

#ifdef _HPA_DEBUG
	t_uint32		dwNextCluster;
	t_uint32		dwClusterNo; 
	t_uint32		dwClusterIdx;
	t_uint32		dwPrevHPACluster;
	t_uint32		dwContCount;
#endif

	FFAT_ASSERT(pVol);


	//FAT Sector의 Bitmap Sector를 얻음
	r = _getClusterBitmapSectorOfFatSector(pVol, dwFatSector, &dwFrom, &dwBitmapSector, &dwOffset);
	FFAT_EO(r, (_T("Fail to get Cluster Bitmap Sector Of Fat Sector")));

	pTempBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pTempBuff != NULL);
	
	r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector,
				1, pTempBuff,
				(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
	FFAT_EO(r, (_T("Fail to read cluster bitmap data")));

	dwOffset = dwOffset >> 3;
	*pdwCount = EssBitmap_GetCountOfBitOne((t_uint8*)(pTempBuff + dwOffset), VOL_CBCPFS(pVol));

#ifdef _HPA_DEBUG
	r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), dwFatSector, &dwClusterNo);
	FFAT_ASSERT(r== 0);
	dwContCount = 0;
	dwPrevHPACluster = dwClusterNo -1;
	for (dwClusterIdx = 0; dwClusterIdx < (t_uint32)VOL_CCPFS(pVol); dwClusterIdx++)
	{
		if (ESS_BITMAP_IS_SET((t_uint8*)(pTempBuff + dwOffset), dwClusterIdx))
		{
			r = FFATFS_GetNextCluster(VOL_VI(pVol), dwClusterNo + dwClusterIdx, &dwNextCluster, pCxt);
			FFAT_ASSERT(dwNextCluster>0);
			dwPrevHPACluster = dwClusterNo + dwClusterIdx;
			dwContCount++;	
			if (dwClusterIdx == (t_uint32)(VOL_CCPFS(pVol) - 1))
			{
				FFAT_DEBUG_HPA_PRINTF(("{%d~%d}\n", dwPrevHPACluster - (dwContCount - 1), dwPrevHPACluster));
			}
		}
		else
		{
			if (dwContCount >0)
			{
				FFAT_DEBUG_HPA_PRINTF(("{%d~%d}\t", dwPrevHPACluster - (dwContCount - 1), dwPrevHPACluster));
				dwContCount = 0;
			}			
		}
	}
#endif

out:
	if (pTempBuff)
	{
		FFAT_LOCAL_FREE(pTempBuff, VOL_SS(pVol), pCxt);
	}
	return FFAT_OK;
}

/**
 * propagate FAT update information to the FAT Free Bitmap
 * this function checks all updated FAT sector to check it is free or not
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		JUN-13-2008 [DongYoung Seo] First Writing.
 * @version		JAN-14-2009 [JeongWoo Park] Add the code to call _syncUpdateBitForFatSector()
 */
static FFatErr
_propagateUpdateBitmap(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*	pInfo;
	t_int32		dwCount;		// bitmap byte count
	t_int32		dwCurCount;		// index current working set
	t_int32		dwPos;			// position of bit 1
	t_uint32	dwSectorNo;		// sector number to propagate
	FFatErr		r;
	t_uint8*	pBitmap;		// bitmap for updated area

	FFAT_ASSERT(pVol);

	pInfo = _INFO(pVol);

	FFAT_ASSERT(_INFO_FLAG(pInfo) & HPA_FAT_UPDATED);

	// get index count of bitmap32
	dwCount		= _FBITMAP_SIZE(pVol);
	pBitmap		= _INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo);

	dwCurCount	= 0;

	// check bitmap with 4byte size
	do
	{
		dwPos = EssBitmap_GetLowestBitOne(pBitmap, dwCount);
		if (dwPos < 0)
		{
			break;
		}

		dwSectorNo = VOL_FFS(pVol) + (dwCurCount << 3) + dwPos;

		FFAT_ASSERT(dwSectorNo <= VOL_LVFSFF(pVol));

		r = _syncUpdateBitForFatSector(pVol, dwSectorNo, pCxt);
		FFAT_ER(r, (_T("fail to sync the updated bit of FAT Sector")));

		dwCurCount	= dwCurCount + (dwPos >> 3);
		dwCount		= dwCount - (dwPos >> 3);
		pBitmap		= pBitmap + (dwPos >> 3);

	} while(1);

	// remove updated flag
	_INFO_FLAG(pInfo) &= (~HPA_FAT_UPDATED);

	r = FFAT_OK;

	return r;
}


/**
* sync the update information of FAT sector to the FAT Free Bitmap
*
* @param		pVol			: [IN] volume pointer
* @param		dwFatSector		: [IN] FAT sector number
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @author		JeongWoo Park
* @version		JAN-14-2009 [JeongWoo Park] first writing
*/
static FFatErr
_syncUpdateBitForFatSector(Vol* pVol, t_uint32 dwFatSector, ComCxt* pCxt)
{
	FFatErr		r;
	HPAInfo*	pInfo;

	pInfo = _INFO(pVol);

	FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), (dwFatSector - VOL_FFS(pVol))) == ESS_TRUE);

	if (_isFATSectorFullHPA(pVol, dwFatSector) == ESS_TRUE)
	{
		// HPA is no need to check whether it is free
		// just clear updated bit
		_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwFatSector);

		FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwFatSector - VOL_FFS(pVol))) == FFAT_FALSE);

		return FFAT_OK;
	}

	r = ffat_addon_misc_isFreeFATSector(pVol, dwFatSector, pCxt);
	FFAT_ER(r, (_T("fail to check Free FAT Sector")));
	if (r == FFAT_TRUE)
	{
		if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwFatSector - VOL_FFS(pVol))) == ESS_FALSE)
		{
			// we find a new FREE FAT Sector in normal
			// increase free fat sector count on normal area
			_INFO_FFSCN_INC(pInfo);

			// free bitmap is only for normal area
			// update FAT Free bitmap
			_SET_BITMAP_FREE_FOR_NORMAL_AREA(pVol, dwFatSector);
		}
	}
	else
	{
		if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwFatSector - VOL_FFS(pVol))) == ESS_TRUE)
		{
			// we find a NON-FREE FAT Sector in normal which was as the free FAT sector
			// decrease free fat sector count on normal area
			_INFO_FFSCN_DEC(pInfo);

			// free bitmap is only for normal area
			// update FAT Free bitmap
			_CLEAR_BITMAP_FREE_FOR_NORMAL_AREA(pVol, dwFatSector);
		}
	}

	// clear updated bit
	_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwFatSector);

	FFAT_ASSERT((_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE) ? (_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol)) : FFAT_TRUE);

	return FFAT_OK;
}


/**
 * get free clusters without FAT update, Normal Area
 *
 * @param		pVol			: [IN] volume pointer
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwHint			: [IN] free cluster hint, lookup start cluster
 * @param		pdwFreeCount	: [IN] found free cluster count
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		dwAllocFlag		: [IN] flag for allocation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: Not enough free cluster on the volume
 *									or not enough free entry at pVC (pdwFreeCount is updated)
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @version		Aug-29-2009 [SangYoon Oh] Modify to get free clusters from full HPA sectors if there is no free cluster on normal area
 */
static FFatErr
_getFreeClustersNormal(Vol* pVol, t_uint32 dwCount,
					FFatVC* pVC, t_uint32 dwHint, t_uint32* pdwFreeCount, ComCxt* pCxt)
{
	// cluster allocation sequence !!!
	// 1. get free from {hint} -> {the lowest HPA sector - 1}
	// 2. get free from {first cluster} -> {hint - 1}
	// 3. get free from {the lowest HPA sector + 1} -> {the end of volume}
	// 4. get free from {the lowest HPA sector} -> {the highest of HPA sector}  

	t_uint32		dwLastNormalCluster;	// the last normal cluster 
											// = {the first cluster at lowest HPA sector - 1}
	t_uint32		dwFrom;					// start cluster number
	t_uint32		dwTo;					// end cluster number
	t_uint32		dwCurFreeCount;			// free cluster count
	t_uint32		dwTotalFreeCount;		// Total free cluster count
	HPAInfo*		pInfo;
	t_uint32		dwLFS;					// the last Sector of 1st FAT
	t_uint32		i;
	FFatErr			r;

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwFreeCount);
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);

	pInfo	= _INFO(pVol);

	FFAT_ASSERT(pInfo);

	// check is there enough free cluster on the volume
	if ((VOL_IS_VALID_FCC(pVol) == FFAT_TRUE))
	{
		if (VOL_FCC(pVol) < dwCount)
		{
			// not enough free space
			return FFAT_ENOSPC;
		}
	}

	dwCurFreeCount		= 0;
	dwTotalFreeCount	= 0;

	if (_IS_VALID_LFHPAS(pVol))
	{
	r = FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), 
								(_INFO_LFHPAS(pInfo) - 1), &dwLastNormalCluster);
	FFAT_EO(r, (_T("fail to get the last cluster")));
	}
	else
	{
		dwLastNormalCluster = VOL_LCN(pVol);
	}


	dwFrom	= dwHint;
	dwTo	= dwLastNormalCluster;

	// 1. get free  from {hint} -> {the lowest HPA sector}
	// check PrevEOF cluster
	if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwHint) == FFAT_FALSE)
	{
		if (FFATFS_IS_VALID_FREE_CLUSTER_HINT(VOL_VI(pVol)) == FFAT_TRUE)
		{
			dwFrom = VIC_FCH(&(VOL_VI(pVol)->stVolInfoCache));
		}
		else
		{
			dwFrom = 2;		// set it to the first cluster
		}

		dwHint = dwFrom;
	}
	else
	{
		dwFrom = dwHint;
	}

	dwTo = dwLastNormalCluster;

	r = _getFreeClustersFromTo(pVol, dwHint, dwFrom, dwTo, (dwCount - dwTotalFreeCount),
						pVC, &dwCurFreeCount, pCxt);

	if (r != FFAT_ENOSPC)
	{
		FFAT_EO(r, (_T("Fail to allocate clusters")));
	}
	
	dwTotalFreeCount += dwCurFreeCount;
	if (dwCurFreeCount > 0)
	{
		FFAT_ASSERT(VC_VEC(pVC) > 0);
		dwHint = VC_LC(pVC);
	}
	
	if (r == FFAT_DONE)
	{
		FFAT_ASSERT(dwTotalFreeCount == dwCount);
		*pdwFreeCount = dwTotalFreeCount;
		goto out;
	}
	else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
	{
		// there is not enough free space on the pVC
		*pdwFreeCount = dwTotalFreeCount;
		goto out;
	}

	// 2. get free from {first cluster(2)} -> {hint - 1}
	if (dwFrom != 2)
	{
		if (dwFrom > dwLastNormalCluster)
		{
			dwTo = dwLastNormalCluster;
		}
		else
		{
			dwTo = dwFrom - 1;
		}

		dwFrom = 2;

		r = _getFreeClustersFromTo(pVol, dwHint, dwFrom, dwTo,
							(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, pCxt);
		if (r != FFAT_ENOSPC)
		{
			FFAT_EO(r, (_T("Fail to allocate clusters")));
		}

		dwTotalFreeCount += dwCurFreeCount;
		if (dwCurFreeCount > 0)
		{
			FFAT_ASSERT(VC_VEC(pVC) > 0);
		}

		if (r == FFAT_DONE)
		{
			FFAT_ASSERT(dwTotalFreeCount == dwCount);
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
		else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
		{
			// there is not enough free space on the pVC
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
	}

	// 3. get free {the lowest HPA sector + 1} -> {the end of volume}
	dwLFS = VOL_LVFSFF(pVol);

	for (i = (_INFO_LFHPAS(pInfo) + 1); i <= dwLFS; i++)
	{
		if (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
		{
			continue;
		}

		r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), i, &dwFrom);
		FFAT_ER(r, (_T("fail to get the first cluster of a sector")));

		r = FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), i, &dwTo);
		FFAT_ER(r, (_T("fail to get the last cluster of a sector")));

		FFAT_ASSERT(dwFrom <= dwTo);

		r = _getFreeClustersFromTo(pVol, dwHint, dwFrom, dwTo,
					(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, pCxt);
		if (r != FFAT_ENOSPC)
		{
			FFAT_EO(r, (_T("Fail to allocate clusters")));
		}

		dwTotalFreeCount += dwCurFreeCount;
		if (dwCurFreeCount > 0)
		{
			FFAT_ASSERT(VC_VEC(pVC) > 0);
			dwHint = VC_LC(pVC);
		}

		if (r == FFAT_DONE)
		{
			FFAT_ASSERT(dwTotalFreeCount == dwCount);
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
		else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
		{
			// there is not enough free space on the pVC
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
	}

	// 4. get free {the lowest HPA sector} -> {the end of volume} among HPA
	for (i = _INFO_LFHPAS(pInfo); i <= dwLFS; i++)
	{
		if (_isFATSectorFullHPA(pVol, i) == FFAT_FALSE)
		{
			continue;
		}

		r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), i, &dwFrom);
		FFAT_ER(r, (_T("fail to get the first cluster of a sector")));

		r = FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), i, &dwTo);
		FFAT_ER(r, (_T("fail to get the last cluster of a sector")));

		FFAT_ASSERT(dwFrom <= dwTo);

		r = _getFreeClustersFromTo(pVol, dwHint, dwFrom, dwTo,
					(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, pCxt);
		if (r != FFAT_ENOSPC)
		{
			FFAT_EO(r, (_T("Fail to allocate clusters")));
		}

		dwTotalFreeCount += dwCurFreeCount;
		if (dwCurFreeCount > 0)
		{
			FFAT_ASSERT(VC_VEC(pVC) > 0);
			//Full HPA -> Partial HPA로 전환
			_convertToPartialHPA(pVol, i, pCxt);				
			dwHint = VC_LC(pVC);
		}

		if (r == FFAT_DONE)
		{
			FFAT_ASSERT(dwTotalFreeCount == dwCount);
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
		else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
		{
			// there is not enough free space on the pVC
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
	}

	FFAT_ASSERT(dwTotalFreeCount <= dwCount);

	if (dwTotalFreeCount == dwCount)
	{
		*pdwFreeCount = dwTotalFreeCount;
		r = FFAT_DONE;
	}
	else
	{
		// set normal free cluster count
		if (_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE)
		{
			FFATFS_SetFreeClusterCount(VOL_VI(pVol), (dwTotalFreeCount));
		}

		r = FFAT_ENOSPC;
	}

out:
	return r;
}



/**
 * get free clusters without FAT update, HPA
 *
 * @param		pVol			: [IN] volume pointer
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwHint			: [IN] free cluster hint, lookup start cluster
 * @param		pdwFreeCount	: [IN] found free cluster count
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		dwAllocFlag		: [IN] flag for allocation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: some of free clusters are founded
 * @return		FFAT_DONE		: all of free clusters are founded
 * @return		FFAT_ENOSPC		: Not enough free cluster on the volume
 *									or not enough free entry at pVC (pdwFreeCount is updated)
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @version		Aug-29-2009 [SangYoon Oh]	Modify the code to check the validity of FCC of Vol
											Add the code to get free clusters from normal area if there is no one for HPA
 * @version		Dec-11-2009 [SangYoon Oh]	Modify the code not to update pdwFreeCount to avoid ASSERT when enlarge fails
 */
static FFatErr
_getFreeClustersHPA(Node* pNode, t_uint32 dwCount, FFatVC* pVC, t_uint32 dwHint,
						t_uint32* pdwFreeCount, FatAllocateFlag dwAllocFlag,
						ComCxt* pCxt)
{
	// free cluster lookup sequence !!!
	// 1. get free clusters from {dwFreeClusterHint} -> {last cluster of current FAT sector}
	// 2. get free clusters from {the first cluster of HPA Area} - > {dwFreeClusterHint - 1}
	// 3. try enlarge
	// 4. get free clusters from normal area

	Vol*		pVol;
	HPAInfo*	pInfo;
	t_uint32	dwFrom;
	t_uint32	dwTo;
	t_uint32	dwCurFreeCount;				// found free cluster count
	t_uint32	dwTotalFreeCount;			// total free cluster count
	t_uint32	dwSector;					// new FAT sector for HPA
	t_boolean	bEnlarged = FFAT_FALSE;		// HPA is enlarged
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(VC_VEC(pVC) == 0);
	FFAT_ASSERT(VC_CC(pVC) == 0);
	FFAT_ASSERT(pdwFreeCount);

	pVol	= NODE_VOL(pNode);
	pInfo	= _INFO(pVol);

	FFAT_ASSERT(pInfo);

	// check is there enough free cluster on the volume
	if ((VOL_IS_VALID_FCC(pVol) == FFAT_TRUE))
	{
		if (VOL_FCC(pVol) < dwCount)
		{
			// not enough free space
			return FFAT_ENOSPC;
		}
	}

	dwTotalFreeCount	= 0;

	pVC->dwValidEntryCount		= 0;	// initialize valid entry count to 0
	pVC->dwTotalClusterCount	= 0;	// initialize total cluster count

	// 1. get free cluster from {dwNextFreeCluster} -> {last cluster of current FAT sector}
	if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwHint) == FFAT_TRUE)
	{
		dwFrom = dwHint;
	}
	else
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), _INFO_FCH(pInfo)) == FFAT_TRUE);
		dwFrom = _INFO_FCH(pInfo);
		dwHint = 2;
	}

	dwTo = _INFO_CMC(pInfo) - 1;

	r = _getFreeClustersFromToHPA(pVol, dwHint, dwFrom, dwTo,
								(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, FFAT_TRUE, pCxt);
	if (r != FFAT_ENOSPC)
	{
		FFAT_EO(r, (_T("Fail to allocate cluster from HPA")));
	}

	dwTotalFreeCount += dwCurFreeCount;
	if (dwCurFreeCount > 0)
	{
		FFAT_ASSERT(VC_VEC(pVC) > 0);
		dwHint = VC_LC(pVC);
	}

	if (r == FFAT_DONE)
	{
		FFAT_ASSERT(dwTotalFreeCount == dwCount);
		*pdwFreeCount = dwTotalFreeCount;
		goto out;
	}
	else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
	{
		// there is not enough free space on the pVC
		*pdwFreeCount = dwTotalFreeCount;
		goto out;
	}

	// 2. get free cluster from {the first cluster of HPA Area} - > {dwFreeClusterHint - 1}
	if (_IS_VALID_LFHPAS(pVol))
	{
		dwTo = dwFrom - 1;

		r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), _INFO_LFHPAS(pInfo), &dwFrom);
		FFAT_EO(r, (_T("fail to get the first cluster of lowest HPA Sector")));

		r = _getFreeClustersFromToHPA(pVol, dwHint, dwFrom, dwTo,
								(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, FFAT_TRUE, pCxt);
		if (r != FFAT_ENOSPC)
		{
			FFAT_EO(r, (_T("Fail to allocate cluster from HPA")));
		}

		dwTotalFreeCount += dwCurFreeCount;
		if (dwCurFreeCount > 0)
		{
			FFAT_ASSERT(VC_VEC(pVC) > 0);
		}

		if (r == FFAT_DONE)
		{
			FFAT_ASSERT(dwTotalFreeCount == dwCount);
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
		else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
		{
			// there is not enough free space on the pVC
			*pdwFreeCount = dwTotalFreeCount;
			goto out;
		}
	}

	// 3. try enlarge
	if (!(_INFO_FLAG(_INFO(pVol)) & HPA_NO_FREE_CLUSTER)) //if some clusters exist for Full HPA
	{
		do
		{
			dwSector = 0;
			r = _enlargeHPA(pVol, &dwSector, pCxt);
			if (r < 0)
			{
					FFAT_DEBUG_HPA_PRINTF((_T("fail to enlarge HPA")));

					//mark that there is no free FAT sector for an additional FHPA sector
					_INFO_FLAG(_INFO(pVol)) |= HPA_NO_FREE_CLUSTER;

					//Must not update pdwFreeCount to avoid ASSERT
					//*pdwFreeCount = dwTotalFreeCount;
					break;
			}

			bEnlarged = FFAT_TRUE;

			r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), dwSector, &dwFrom);
			FFAT_EO(r, (_T("fail to get the first cluster of lowest HPA Sector")));

			r = FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), dwSector, &dwTo);
			FFAT_EO(r, (_T("fail to get the first cluster of lowest HPA Sector")));

			dwHint = dwFrom;

			FFAT_ASSERT((dwCount - dwTotalFreeCount) > 0);

			r = _getFreeClustersFromToHPA(pVol, dwHint, dwFrom, dwTo,
								(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, FFAT_TRUE, pCxt);
			if (r == FFAT_ENOSPC)
			{
				FFAT_ASSERT(VC_CC(pVC) < dwCount);
				if (VC_VEC(pVC) == VC_TEC(pVC))
				{
					*pdwFreeCount = dwTotalFreeCount + dwCurFreeCount;
					goto out;
				}
			}
			else
			{
				FFAT_EO(r, (_T("Fail to allocate cluster from HPA")));
			}

			FFAT_ASSERT((dwCurFreeCount > 0) && (VC_VEC(pVC) > 0));

			dwTotalFreeCount += dwCurFreeCount;

			if (dwTotalFreeCount >= dwCount)
			{
				FFAT_ASSERT(dwTotalFreeCount == dwCount);
				*pdwFreeCount = dwTotalFreeCount;
				goto out;
			}
		} while(1);
	}

	// 4. get free cluster from normal area
	dwFrom = 2;
	dwTo = _INFO_CMC(pInfo) - 1;

	r = _getFreeClustersFromToHPA(pVol, dwHint, dwFrom, dwTo,
							(dwCount - dwTotalFreeCount), pVC, &dwCurFreeCount, FFAT_FALSE, pCxt);
	if (r != FFAT_ENOSPC)
	{
		FFAT_EO(r, (_T("Fail to allocate cluster from HPA")));
	}

	dwTotalFreeCount += dwCurFreeCount;
	if (dwCurFreeCount > 0)
	{
		FFAT_ASSERT(VC_VEC(pVC) > 0);
	}

	if (r == FFAT_DONE)
	{
		FFAT_ASSERT(dwTotalFreeCount == dwCount);
		*pdwFreeCount = dwTotalFreeCount;
	}
	else if ((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC) == FFAT_TRUE))
	{
		// there is not enough free space on the pVC
		*pdwFreeCount = dwTotalFreeCount;
	}

out:
	if ((bEnlarged) && (r < 0))
	{
		// reset HPA_FREE_FAT_CHECKED flag
		_INFO_FLAG(pInfo) &= (~HPA_FREE_FAT_CHECKED);
	}

	return r;
}


/**
* check and release free HPA sector
*
* @param		pVol			: [IN] volume pointer
* @param		pCxt			: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		AUG-26-2008 [DongYoung Seo] remove The Highest Free HPA Sector update routine
*									this is release function so never o update free HPA sector.
* @version		OCT-12-2008 [DongYoung Seo] update free normal FAT sector count after free checking
* @version		JAN-14-2009 [JeongWoo Park] use the ffat_addon_misc_isFreeFATSector()
*											instead of FFATFS_isFreeFATSector()
* @version		Aug-29-2009 [SangYoon Oh] Remove the code that decreases TCC of HPA
* @version		Dec-12-2009 [SangYoon Oh] Add the code to clear the flag HPA_NO_FREE_CLUSTER when shrinking fully empty FHPA sectors
*/
static FFatErr
_releaseFreeHPASector(Vol* pVol, ComCxt* pCxt)
{
	HPAInfo*		pInfo;
	t_uint32		dwLFS;		// the Last Fat Sector
	t_uint32		i;
	t_boolean		bUpdated;	// bitmap update flag
	t_uint32		dwLow;		// the lowest updated bitmap sector
	t_uint32		dwHigh;		// the highest update bitmap sector
	FFatErr			r;

	FFAT_ASSERT(pVol);

	pInfo	= _INFO(pVol);

	if (_INFO_FLAG(pInfo) & HPA_FREE_FAT_CHECKED)
	{
		FFAT_DEBUG_HPA_PRINTF((_T("Check & Releasing Free FAT sector - do nothing\n")));
		return FFAT_OK;
	}

	// @20071106-iris :
	// for performance, FCC와 붙을 경우, release HPA를 하기 전에 항상 FCC sync를 해주어야 한다.
	// FCC sync를 자주 하면 성능이 떨어 진다.
	if (_IS_VALID_HPA_TCC(pInfo))
	{
		if (VOL_FCC(pVol) < (t_uint32)VOL_CCPFS(pVol))
		{
			FFAT_DEBUG_HPA_PRINTF((_T("There is no Free FAT sector\n")));
			return FFAT_OK;
		}
	}

	FFAT_DEBUG_HPA_PRINTF((_T("Check & Releasing Free FAT sector \n")));

	dwLFS	= VOL_LVFSFF(pVol);
	bUpdated = FFAT_FALSE;

	dwHigh	= _INFO_LFHPAS(pInfo);
	dwLow	= dwLFS;

	for (i = _INFO_LFHPAS(pInfo); i < dwLFS; i++)
	{
		if (_isFATSectorFullHPA(pVol, i) == FFAT_FALSE)
		{
			continue;
		}

		r = ffat_addon_misc_isFreeFATSector(pVol, i, pCxt);
		if (r == FFAT_TRUE)
		{
			_CLEAR_BITMAP_FHPA(pVol, i);
			bUpdated = FFAT_TRUE;

			FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (i - VOL_FFS(pVol))) == FFAT_FALSE);

			_SET_BITMAP_FREE_FOR_NORMAL_AREA(pVol, i);			// clear free bitmap, free bitmap is only for normal area
			_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, i);	// clear updated bitmap

			// increase free fat sector count on normal area [HPA --> NORMAL]
			_INFO_FFSCN_INC(pInfo);

			FFAT_ASSERT((_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE) ? (_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol)) : FFAT_TRUE);

			FFAT_DEBUG_HPA_PRINTF((_T("Bitmap is cleared for sector %d \n"), i));

			if (i <= dwLow)
			{
				dwLow = i;
			}

			if (i >= dwHigh)
			{
				dwHigh = i;
			}

			FFAT_DEBUG_HPA_PRINTF((_T("Release HPA FAT Sector:%d\n"), i));
		}

		FFAT_ER(r, (_T("fail to check free fat sector")));
	}

	if (bUpdated == FFAT_TRUE)
	{
		_INFO_FLAG(_INFO(pVol)) &= ~(HPA_NO_FREE_CLUSTER);

		r = _writeBitmap(pVol, dwLow, dwHigh, pCxt);
		FFAT_ER(r, (_T("fail to write bitmap")));

		// update the LFHPAS(The Lowest HPA Sector)
		if (dwLow <= _INFO_LFHPAS(pInfo))
		{
			FFAT_ASSERT(dwLow == _INFO_LFHPAS(pInfo));

			// update FAT information
			r = _updateLowestHPAFatSector(pVol);
			FFAT_EO(r, (_T("Fail to get the lowest HPA sector")));
		}
	}

	_INFO_FLAG(pInfo) |= HPA_FREE_FAT_CHECKED;
	

	return FFAT_OK;

out:
	return r;
}


/**
* sector IO
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		FFAT_OK			: Sector IO Success
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		MAR-26-2009 [DongYoung Seo] change cache flag from DIRECTIO to none.
*									General I/O policy is determined by cache module
* @version		Aug-29-2009 [SangYoon Oh]	Add the parameter pCxt
											Revert the cache flag to DIRECTIO due to I/O error in Nestle
*/
static FFatErr
_sectorIO(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, ComCxt* pCxt)
{
	FFatCacheInfo	stCI;
	FFatCacheFlag	dwFlag = FFAT_CACHE_DIRECT_IO;
	t_int32			dwStartSector;
	FFatErr			r;

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	dwStartSector	= dwSectorNo;
	dwCount			= dwCount;

	FFAT_DEBUG_HPA_PRINTF((_T("Sector %s start/count:%d/%d \n"), dwIOType == FFAT_IO_READ_SECTOR ? "READ" : "WRITE", dwSectorNo, dwCount));

	// PART1 READ RESERVED AREA
	r = _sectorIOReserved(pVol, dwIOType, dwStartSector, dwCount, pBuff, &stCI, dwFlag);
	FFAT_EO(r, (_T("fail to read sectors, (reserved area)")));

	FFAT_ASSERT(r <= dwCount);
	dwCount -= r;

	if (dwCount == 0)
	{
		return FFAT_DONE;
	}

	pBuff += (r << VOL_SSB(pVol));
	dwStartSector += r;

	// PART2 READ FAT AREA
	r = _sectorIOFAT(pVol, dwIOType, dwStartSector, dwCount, pBuff, &stCI, dwFlag, pCxt);
	FFAT_EO(r, (_T("fail to read sectors, (FAT area)")));

	FFAT_ASSERT(r <= dwCount);
	dwCount -= r;

	if (dwCount == 0)
	{
		return FFAT_DONE;
	}

	pBuff += (r << VOL_SSB(pVol));
	dwStartSector += r;


	// PART3 READ ROOT AREA
	r = _sectorIORootDir(pVol, dwIOType, dwStartSector, dwCount, pBuff, &stCI, dwFlag);
	FFAT_EO(r, (_T("fail to read sectors, (Root Directory area)")));

	FFAT_ASSERT(r <= dwCount);
	dwCount -= r;

	if (dwCount == 0)
	{
		return FFAT_DONE;
	}

	pBuff += (r << VOL_SSB(pVol));
	dwStartSector += r;


	// PART4 READ Data AREA
	r = _sectorIOData(pVol, dwIOType, dwStartSector, dwCount, pBuff, &stCI, dwFlag, pCxt);
	FFAT_EO(r, (_T("fail to read sectors, (reserved area)")));

	FFAT_ASSERT(dwCount == r);

	return FFAT_DONE;

out:
	return r;
}


/**
* read sectors on reserved area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		pCI			: [IN] cache info
* @param		dwFlag		: [IN] cache flag
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
static t_int32
_sectorIOReserved(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, 
					FFatCacheInfo* pCI, FFatCacheFlag dwFlag)
{
	if (dwSectorNo >= VOL_FFS(pVol))
	{
		// dwSectorNO is not in reserved area
		return 0;
	}

	dwCount = ESS_GET_MIN((t_uint32)dwCount, (VOL_FFS(pVol) - dwSectorNo));

	return _pfBCIO[dwIOType](dwSectorNo, pBuff, dwCount, dwFlag, pCI);
}


/**
* read sectors on FAT area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		dwFlag		: [IN] cache flag
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
static t_int32
_sectorIOFATHPA(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo,
				t_int32 dwCount, t_int8* pBuff)
{
	t_int32			i;
	t_int32			dwReadCount;
	t_uint32*		p32;
	t_uint32		dwBAD;

	FFAT_ASSERT(dwSectorNo >= VOL_FFS(pVol));
	FFAT_ASSERT(dwCount > 0);

	dwReadCount = 0;

	FFAT_DEBUG_HPA_PRINTF((_T("SECTOR IO FAT HPA AREA, sector/count:%d/%d \n"), dwSectorNo, dwCount));

	for (i = 0; i < dwCount; i++)
	{
		if (_isFATSectorFullHPA(pVol, dwSectorNo + i) == FFAT_TRUE)
		{
			dwReadCount++;
		}
		else
		{
			break;
		}
	}

	if (dwIOType >= FFAT_IO_WRITE_SECTOR)
	{
		// for write/erase,  do nothing, just return success
		FFAT_LOG_PRINTF((_T("WRITE/ERASE ACCESS TO HPA (FAT)")));
		return dwReadCount;
	}

	// FOR read - Fill Sectors to BAD
	p32 = (t_uint32*)pBuff;

	if (dwReadCount)
	{
		dwCount = (dwReadCount << VOL_SSB(pVol)) / 4;	// GET 4 BYTE COUNT
		if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
		{
			dwBAD = FAT32_BAD;
		}
		else
		{
			dwBAD = (FAT16_BAD << 16) | FAT16_BAD;
		}

		for (i = 0; i < dwCount; i++)
		{
			p32[i] = dwBAD;
		}
	}

	return dwReadCount;
}


/**
* read sectors on FAT area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		pCI			: [IN] cache info
* @param		dwFlag		: [IN] cache flag
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh]	Add the parameter pCxt
											Modify the code to check the partial HPA bitmap and cluster bitmap ahead of I/O
*/
static t_int32
_sectorIOFATNormal(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, 
					FFatCacheInfo* pCI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	t_int32		i;
	t_int32			dwIOCount;
	t_int32			dwCountToScan;
	t_uint32		dwPreviousSector;
	t_uint32		dwFrom;
	t_uint32		dwTo;
	t_uint32		dwBitmapSector;
	t_uint32		dwBitmapOffset;
	t_int32			dwClusterIdx;
	t_uint32		dwCCPFSMask;
	FFatErr			r = FFAT_OK;
	FatVolInfo*		pVI;
	t_int8*			pBitmapBuff;
	t_int8*			pOrgFATBuff;
	t_int8*			pFATBuff;
	t_uint32		dwOrgValue;

	FFAT_ASSERT(dwSectorNo >= VOL_FFS(pVol));
	FFAT_ASSERT(dwCount > 0);

	pOrgFATBuff = NULL;
	pBitmapBuff = NULL;
	pFATBuff = pBuff;
	dwIOCount = 0;
	pVI = VOL_VI(pVol);
	dwCCPFSMask = VI_CCPFS_MASK(pVI);	

	for (i = 0; i < dwCount; i++)
	{
		if (_isFATSectorFullHPA(pVol, dwSectorNo + i) == FFAT_FALSE)
		{
			dwIOCount++;
		}
		else
		{
			break;
		}
	}

	if (dwIOCount > 0)
	{
		FFAT_ASSERT((dwIOType == FFAT_IO_READ_SECTOR) || (dwIOType == FFAT_IO_WRITE_SECTOR));

		if (dwIOType == FFAT_IO_READ_SECTOR)
		{
			r = _pfBCIO[FFAT_IO_READ_SECTOR](dwSectorNo, pFATBuff, dwIOCount, dwFlag, pCI);
			FFAT_EO(r, (_T("fail to read FAT Sector")));
		}
		else
		{
			pOrgFATBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
			FFAT_ASSERT(pOrgFATBuff != NULL);			
		}

		pBitmapBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
		FFAT_ASSERT(pBitmapBuff != NULL);
		
		dwPreviousSector = 0;
		for (i = dwSectorNo; i < (t_int32)(dwSectorNo + dwCount); i++)
		{
			if (_isFATSectorPartialHPA(pVol, i) == FFAT_TRUE) 
			{
				r = _getClusterBitmapSectorOfFatSector(pVol, i, &dwFrom, &dwBitmapSector, &dwBitmapOffset);
				FFAT_EO(r, (_T("fail to getClusterBitmapSectorOfFatSector")));

				r = FFATFS_GetLastClusterOfFatSector(pVI, i, &dwTo);
				FFAT_EO(r, (_T("fail to GetLastClusterOfFatSector")));

				dwCountToScan = dwTo - dwFrom + 1;
				FFAT_ASSERT((dwCountToScan > 0) && dwCountToScan <= VOL_CCPFS(pVol));
				
				//Cluster Bitmap Sector가 달라진 경우에만 읽음
				if (dwBitmapSector != dwPreviousSector) 
				{
					r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBitmapBuff, 
							(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
					FFAT_EO(r, (_T("fail to read Cluster Bitmap Sector")));
					dwPreviousSector = dwBitmapSector;
				}

				if (dwIOType == FFAT_IO_READ_SECTOR)
				{
					//Filling Bad marks				
					if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
					{
						for (dwClusterIdx = 0; dwClusterIdx < dwCountToScan; dwClusterIdx++, dwBitmapOffset++)
						{
							if (ESS_BITMAP_IS_SET(pBitmapBuff, dwBitmapOffset))
							{
								FFATFS_UPDATE_FAT32((t_uint32*)(pFATBuff), dwClusterIdx, FAT32_BAD, dwCCPFSMask);
							}
						}
					}
					else
					{
						//FAT16
						for (dwClusterIdx = 0; dwClusterIdx < dwCountToScan; dwClusterIdx++, dwBitmapOffset++)
						{
							if (ESS_BITMAP_IS_SET(pBitmapBuff, dwBitmapOffset))
							{
								FFATFS_UPDATE_FAT16((t_uint16*)(pFATBuff), dwClusterIdx, FAT16_BAD, dwCCPFSMask);
							}
						}
					}
				}
				else
				{
					//Write Case
					FFAT_ASSERT(dwIOType == FFAT_IO_WRITE_SECTOR);
					
					//원본 FAT Sector를 읽음
					r = ffat_readWriteSectors(pVol, NULL, i, 1, pOrgFATBuff, 
							(FFAT_CACHE_DATA_FAT), FFAT_TRUE, pCxt);
					FFAT_EO(r, (_T("fail to read FAT Sector")));

					//Partial HPA Cluster는 보존
					if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
					{
						for (dwClusterIdx = 0; dwClusterIdx < dwCountToScan; dwClusterIdx++, dwBitmapOffset++)
						{
							if (ESS_BITMAP_IS_SET(pBitmapBuff, dwBitmapOffset))
							{
								dwOrgValue = FFAT_BO_UINT32(((t_uint32*)pOrgFATBuff)[dwClusterIdx & dwCCPFSMask]) & FAT32_MASK;
								FFATFS_UPDATE_FAT32((t_uint32*)(pFATBuff), dwClusterIdx, dwOrgValue, dwCCPFSMask);
							}
						}
					}
					else
					{
						//FAT16
						for (dwClusterIdx = 0; dwClusterIdx < dwCountToScan; dwClusterIdx++, dwBitmapOffset++)
						{
							if (ESS_BITMAP_IS_SET(pBitmapBuff, dwBitmapOffset))
							{
								dwOrgValue = FFAT_BO_UINT16(((t_uint16*)pOrgFATBuff)[dwClusterIdx & dwCCPFSMask]);
								FFATFS_UPDATE_FAT16((t_uint16*)(pFATBuff), dwClusterIdx, dwOrgValue, dwCCPFSMask);
							}
						}
					}
				}

				pFATBuff += VOL_SS(pVol);

			}
		}

		if (dwIOType == FFAT_IO_WRITE_SECTOR)
		{
			//restore the starting point of user buffer 
			pFATBuff = pBuff; 
			r = _pfBCIO[FFAT_IO_WRITE_SECTOR](dwSectorNo, pFATBuff, dwIOCount, dwFlag, pCI);
			FFAT_EO(r, (_T("fail to write FAT Sector")));
		}

	}
	else
	{
		r = 0;
	}

out:
	if (pBitmapBuff)
	{
		FFAT_LOCAL_FREE(pBitmapBuff, VOL_SS(pVol), pCxt);
	}

	if (pOrgFATBuff)
	{
		FFAT_LOCAL_FREE(pOrgFATBuff, VOL_SS(pVol), pCxt);
	}
	return r;
}


/**
* read sectors on FAT area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter pCxt
*/
static t_int32
_sectorIOFAT(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, 
				FFatCacheInfo* pCI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	t_uint32		dwLFS;
	t_int32			dwRestCount;
	FFatErr			r;

	FFAT_ASSERT(dwSectorNo >= VOL_FFS(pVol));
	FFAT_ASSERT(dwCount > 0);

	dwLFS = VOL_LFS(pVol);

	if ((dwSectorNo < VOL_FFS(pVol)) || (dwSectorNo > dwLFS))
	{
		// dwSectorNO is not in reserved area
		return 0;
	}

	dwCount = ESS_GET_MIN((t_uint32)dwCount, (dwLFS - dwSectorNo + 1));

	if (dwCount == 0)
	{
		return dwCount;
	}

	dwRestCount		= dwCount;

	do
	{
		if (_isFATSectorFullHPA(pVol, dwSectorNo) == FFAT_TRUE)
		{
			r = _sectorIOFATHPA(pVol, dwIOType, dwSectorNo, dwRestCount, pBuff);
			FFAT_ER(r, (_T("fail to read FAT area (HPA)")));
		}
		else
		{
			r = _sectorIOFATNormal(pVol, dwIOType, dwSectorNo, dwRestCount, pBuff, pCI, dwFlag, pCxt);
			FFAT_ER(r, (_T("fail to read FAT area (Normal)")));
		}

		dwSectorNo	+= r;
		dwRestCount	-= r;
		pBuff += (r << VOL_SSB(pVol));

	} while (dwRestCount > 0);

	return dwCount;
}


/**
* read sectors on Root Directory area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
static t_int32
_sectorIORootDir(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, 
				FFatCacheInfo* pCI, FFatCacheFlag dwFlag)
{
	t_uint32	dwLRS;			// last root sector

	FFAT_ASSERT(dwSectorNo > VOL_LFS(pVol));
	FFAT_ASSERT(dwCount > 0);
	if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
	{
		// request is not for this area
		return 0;
	}

	dwLRS = VOL_LRS(pVol);
	if (dwSectorNo > dwLRS)
	{
		return 0;
	}

	dwCount = ESS_GET_MIN((t_uint32)dwCount, (dwLRS - dwSectorNo + 1));
	return _pfBCIO[dwIOType](dwSectorNo, pBuff, dwCount, dwFlag, pCI);
}


/**
* read sectors on DATA area
* 
* @param		pVol		: [IN] volume pointer
* @param		dwSectorNo	: [IN] start sector number 
* @param		dwCount		: [IN] sector count
* @param		pBuff		: [IN] buffer pointer
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh]	Add the parameter pCxt
											Modify the code to check the partial HPA bitmap and cluster bitmap ahead of I/O
*/
static t_int32
_sectorIOData(Vol* pVol, FFatIOType dwIOType, t_uint32 dwSectorNo, t_int32 dwCount, t_int8* pBuff, 
				FFatCacheInfo* pCI, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	t_uint32	dwFirstCluster;
	t_uint32	dwLastCluster;
	t_uint32	dwFirstFatSector;			// first FAT Sector number for reading
	t_uint32	dwLastFatSector;		// Last FAT Sector number for reading
	t_uint32	dwFrom;
	t_uint32	dwTo;
	t_uint32	dwBitmapSector;
	t_uint32	dwOffset;
	t_uint32	dwPreviousSector;
	t_int32		dwCountToScan;
	t_uint32	i;
	FatVolInfo*	pVI;
	FFatErr		r;
	t_int8*		pBitmapBuff;

	FFAT_ASSERT(dwSectorNo >= VOL_FFS(pVol));
	FFAT_ASSERT(dwCount > 0);

	if ((dwSectorNo + dwCount) > VOL_SC(pVol))
	{
		// dwSectorNO is not in reserved area
		return 0;
	}
	pVI = VOL_VI(pVol);

	// get continuous sector count
	dwFirstCluster = FFATFS_GET_CLUSTER_OF_SECTOR(pVI, dwSectorNo);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwFirstCluster) == FFAT_TRUE);

	dwFirstFatSector = FFATFS_GetFatSectorOfCluster(pVI, dwFirstCluster);
	FFAT_ASSERT(dwFirstFatSector >= VOL_FFS(pVol));
	FFAT_ASSERT(dwFirstFatSector <= VOL_LFS(pVol));

	dwLastCluster = FFATFS_GET_CLUSTER_OF_SECTOR(pVI, (dwSectorNo + dwCount - 1));
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(pVI, dwLastCluster) == FFAT_TRUE);

	dwLastFatSector = FFATFS_GetFatSectorOfCluster(pVI, dwLastCluster);
	FFAT_ASSERT(dwLastFatSector >= VOL_FFS(pVol));
	FFAT_ASSERT(dwLastFatSector <= VOL_LFS(pVol));

	pBitmapBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBitmapBuff != NULL);

	dwPreviousSector = 0;
	// get continuous FAT Sectors
	for (i = dwFirstFatSector; i <= dwLastFatSector; i++)
	{
		IF_UK (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
		{
			FFAT_DEBUG_PRINTF((_T("READ/WRITE/ERASE ON HPA AREA")));
			r = FFAT_EIO;
			goto out;
		}
		else if (_isFATSectorPartialHPA(pVol, i) == FFAT_TRUE)
		{
			//Scan할 Cluster 범위 계산
			//여기서의 가정!
			//한 ClusterBitmap Sector는 Sector크기(512Byte) * 8개의 Cluster를 cover
			//한 FAT Sector는 CCPFS개(512Byte 섹터 기준 FAT16:256개, FAT32:128개)의 Cluster를 cover
			//따라서 전자가 후자의 배수이며 align이 맞게 됨.
			//만약 Misaligned 된다면 한 FAT Sector 안에 두 Cluster Bitmap Sector이 걸쳐 있으므로 나눠서 읽어야 하는 경우 발생

			r = FFATFS_GetFirstClusterOfFatSector(pVI, i, &dwFrom);
			FFAT_EO(r, (_T("fail to get the first cluster of a FAT sector")));

			if (dwFrom < dwFirstCluster)
			{
				dwFrom = dwFirstCluster;
			}

			r = FFATFS_GetLastClusterOfFatSector(pVI, i, &dwTo);
			FFAT_EO(r, (_T("fail to get the first cluster of a FAT sector")));

			if (dwTo > dwLastCluster)
			{
				dwTo = dwLastCluster;
			}
			
			dwCountToScan = dwTo - dwFrom + 1;
			FFAT_ASSERT((dwCountToScan > 0) && dwCountToScan <= VOL_CCPFS(pVol));

			dwBitmapSector = _GET_CBITMAP_SECTOR_OF_CLUSTER(pVol, dwFrom);
			dwOffset = dwFrom & VOL_CBCPS_MASK(pVol);
			
			//Cluster Bitmap Sector가 달라진 경우에만 읽음
			if (dwBitmapSector != dwPreviousSector) 
			{
				r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBitmapBuff, 
						(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
				FFAT_EO(r, (_T("fail to read Cluster Bitmap Sector")));
				dwPreviousSector = dwBitmapSector;
			}

			for (/*Nothing*/; dwCountToScan > 0; dwCountToScan--, dwOffset++)
			{
				if (ESS_BITMAP_IS_SET(pBitmapBuff, dwOffset))
				{
					FFAT_DEBUG_PRINTF((_T("READ/WRITE/ERASE ON HPA AREA")));
					r = FFAT_EIO;
					goto out;					
				}
			}
		}
	}

	FFAT_LOCAL_FREE(pBitmapBuff, VOL_SS(pVol), pCxt);
	return _pfBCIO[dwIOType](dwSectorNo, pBuff, dwCount, dwFlag, pCI);

out:
	if (pBitmapBuff)
	{
		FFAT_LOCAL_FREE(pBitmapBuff, VOL_SS(pVol), pCxt);
	}

	return r;
}


/**
* wrapper function for ffat_al_eraseSector();
* 
* @param		dwSector		: [IN] volume pointer
* @param		pBuff			: [IN] buffer pointer
* @param		dwCount			: [IN] sector count
* @param		dwFlag			: [IN] start sector number 
* @param		pCI				: [IN] start sector number 
* @param		0 or above		: read sector count
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
*/
static t_int32
_sectorIOEraseWrapper(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
											FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	return ffat_al_eraseSector(dwSector, dwCount, dwFlag, pCI);
}


/**
* Initializes Storage for HPA Information
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOEMEM	: Not enough memory
* @author		DongYoung Seo
* @version		MAY-21-2008 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the code to allocate memory for partial HPA bitmap 
*/
static FFatErr
_initHPAInfoStorage(void)
{

#ifdef FFAT_DYNAMIC_ALLOC

	// initialization only, no allocation
	ESS_LIST_INIT(&_slFreeHPAInfo);
	_pHPAInfoStorage = NULL;

	return FFAT_OK;

#else

	FFatErr			r = FFAT_OK;
	t_int32			i;
	HPAInfo*		pInfo;

	ESS_LIST_INIT(&_slFreeHPAInfo);

	if (_CONFIG()->wHPAVolCount == 0)
	{
		return FFAT_OK;
	}

	_pHPAInfoStorage = (HPAInfo*)FFAT_MALLOC((sizeof(HPAInfo) * _CONFIG()->wHPAVolCount), ESS_MALLOC_NONE);
	IF_UK (_pHPAInfoStorage == NULL)
	{
		r = FFAT_ENOMEM;
		goto out;
	}

	FFAT_MEMSET(_pHPAInfoStorage, 0x00, (sizeof(HPAInfo) * _CONFIG()->wHPAVolCount));

	FFAT_ASSERT(_MAIN()->pConf->wHPABitmapSize > 0);

	for (i = 0; i < _CONFIG()->wHPAVolCount; i++)
	{
		pInfo = _pHPAInfoStorage + i;
		pInfo->pBitmapFullHPA = (t_uint8*)FFAT_MALLOC(_MAIN()->pConf->wHPABitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapFullHPA == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocation memory for HPA bitmap")));
			r = FFAT_ENOMEM;
			goto out;
		}

		pInfo->pBitmapPartialHPA = (t_uint8*)FFAT_MALLOC(_MAIN()->pConf->wHPABitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapPartialHPA == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocation memory for PartialHPA bitmap")));
			r = FFAT_ENOMEM;
			goto out;
		}

		pInfo->pBitmapFree = (t_uint8*)FFAT_MALLOC(_MAIN()->pConf->wHPABitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapFree == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocation memory for Free bitmap")));
			r = FFAT_ENOMEM;
			goto out;
		}

		pInfo->pBitmapUpdated = (t_uint8*)FFAT_MALLOC(_MAIN()->pConf->wHPABitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapUpdated == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocation memory for Update bitmap")));
			r = FFAT_ENOMEM;
			goto out;
		}

		ESS_LIST_INIT(&pInfo->slFree);
		ESS_LIST_ADD_HEAD(&_slFreeHPAInfo, &pInfo->slFree);
	}

	FFAT_ASSERT(EssList_Count(&_slFreeHPAInfo) == _CONFIG()->wHPAVolCount);

out:
	IF_UK (r != FFAT_OK)
	{
		_terminateHPAInfoStorage();
	}

	return r;

#endif

}


/**
* terminates Storage for HPA Information
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOEMEM	: Not enough memory
* @author		DongYoung Seo
* @version		MAY-21-2008 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the code to free memory for partial HPA bitmap 
*/
static FFatErr
_terminateHPAInfoStorage(void)
{
#ifndef FFAT_DYNAMIC_ALLOC
	t_int32			i;
	HPAInfo*		pInfo;

	IF_UK (_pHPAInfoStorage)
	{
		// free allocated memory
		for (i = 0; i < _CONFIG()->wHPAVolCount; i++)
		{
			pInfo = _pHPAInfoStorage + i;
			FFAT_FREE(pInfo->pBitmapUpdated, _MAIN()->pConf->wHPABitmapSize);
			FFAT_FREE(pInfo->pBitmapFree, _MAIN()->pConf->wHPABitmapSize);
			FFAT_FREE(pInfo->pBitmapFullHPA, _MAIN()->pConf->wHPABitmapSize);
			FFAT_FREE(pInfo->pBitmapPartialHPA, _MAIN()->pConf->wHPABitmapSize);
		}

		FFAT_FREE(_pHPAInfoStorage, sizeof(HPAInfo));
	}
#endif

	return FFAT_OK;
}


/**
 * get a free HPA Information storage
 * 
 * @param		pVol			: [IN] volume information
 * @return		NULL			: there is no free node storage, fail to get/put lock
 * @return		else			: success
 * @author		DongYoung Seo
 * @version		21-MAY-2008 [DongYoung Seo] First Writing.
 * @version		10-NOV-2008 [DongYoung Seo] add initialization code for FREE ,UPDATED BITMAP
 * @version		Aug-29-2009 [SangYoon Oh] Add the code to initialize memory for partial HPA bitmap 
 */
static HPAInfo*
_getFreeHPAInfo(Vol* pVol)
{
	HPAInfo*		pInfo;

#ifdef FFAT_DYNAMIC_ALLOC
	pInfo = _getFreeHPAInfoDynamic(pVol);
#else
	pInfo = _getFreeHPAInfoStatic();
#endif

	FFAT_MEMSET(_INFO_BITMAP_FHPA(pInfo), 0x00, _FBITMAP_SIZE(pVol));
	FFAT_MEMSET(_INFO_BITMAP_PHPA(pInfo), 0x00, _FBITMAP_SIZE(pVol));
	FFAT_MEMSET(_INFO_BITMAP_FREE(pInfo), 0x00, _FBITMAP_SIZE(pVol));
	// 초기상태를 모르기때문에 update bit를 전부 1로 세팅하여 scan하도록 함.
	FFAT_MEMSET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), 0xFF, _FBITMAP_SIZE(pVol));
	
	// clean spare bits of updated bitmap
	if (((VOL_LVFSFF(pVol) - VOL_FFS(pVol)) & 0x07) != 0x07)
	{
		t_int32		dwBitMapLastSector;
		t_int32		i;

		dwBitMapLastSector = (_FBITMAP_SIZE(pVol) << 3) + VOL_FFS(pVol);

		for (i = VOL_LVFSFF(pVol) + 1; i < dwBitMapLastSector; i++)
		{
			ESS_BITMAP_CLEAR(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), ((i) - VOL_FFS(pVol)));
		}
	}

	return pInfo;
}


#ifdef FFAT_DYNAMIC_ALLOC
	/**
	 * get a free HPA Information storage with dynamic memory allocation
	 * 
	 * @param		pVol			: [IN] volume information
	 * @return		NULL			: there is no free node storage, fail to get/put lock
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		21-MAY-2008 [DongYoung Seo] First Writing.
	 * @version		Aug-29-2009 [SangYoon Oh] Add the code to allocate memory for partial HPA bitmap 
	 */
	static HPAInfo*
	_getFreeHPAInfoDynamic(Vol* pVol)
	{
		HPAInfo*		pInfo = NULL;
		t_int32			dwBitmapSize;

		dwBitmapSize = _FBITMAP_SIZE(pVol);
		FFAT_ASSERT(dwBitmapSize > 0);

		pInfo = (HPAInfo*)FFAT_MALLOC(sizeof(HPAInfo), ESS_MALLOC_NONE);
		IF_UK (pInfo == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for HPA Info")));
			return NULL;
		}

		FFAT_MEMSET(pInfo, 0x00, sizeof(HPAInfo));

		pInfo->pBitmapFullHPA = (t_uint8*)FFAT_MALLOC(dwBitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapFullHPA == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for HPA bitmap")));
			goto error;
		}

		pInfo->pBitmapPartialHPA = (t_uint8*)FFAT_MALLOC(dwBitmapSize, ESS_MALLOC_IO);
		IF_UK (pInfo->pBitmapPartialHPA == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for Partial HPA bitmap")));
			goto error;
		}

		pInfo->pBitmapFree = (t_uint8*)FFAT_MALLOC(dwBitmapSize, ESS_MALLOC_NONE);
		IF_UK (pInfo->pBitmapFree == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for FREE bitmap")));
			goto error;
		}

		pInfo->pBitmapUpdated = (t_uint8*)FFAT_MALLOC(dwBitmapSize, ESS_MALLOC_NONE);
		IF_UK (pInfo->pBitmapUpdated == NULL)
		{
			FFAT_LOG_PRINTF((_T("Fail to allocate memory for UPDATE bitmap")));
			goto error;
		}

		return pInfo;

	error:
		FFAT_FREE(pInfo->pBitmapUpdated, dwBitmapSize);
		FFAT_FREE(pInfo->pBitmapFree, dwBitmapSize);
		FFAT_FREE(pInfo->pBitmapPartialHPA, dwBitmapSize);
		FFAT_FREE(pInfo->pBitmapFullHPA, dwBitmapSize);
		FFAT_FREE(pInfo, sizeof(HPAInfo));
		return NULL;
	}


	/**
	 * release a HPAInfo storage and add it to free list for dynamic memory allocation
	 * 
	 * @param		pVol			: [IN] volume information
	 * @param		pInfo			: [IN] HPA Information
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EPANIC		: fail to get/put lock
	 * @author		DongYoung Seo
	 * @version		21-MAY-2008 [DongYoung Seo] First Writing.
	 * @version		Aug-29-2009 [SangYoon Oh] Add the code to free memory for partial HPA bitmap 
	 */
	static FFatErr
	_releaseHPAInfoDynamic(Vol* pVol, HPAInfo* pInfo)
	{
		FFAT_ASSERT(pVol);
		FFAT_ASSERT(pInfo);

		FFAT_FREE(pInfo->pBitmapUpdated, _FBITMAP_SIZE(pVol));
		FFAT_FREE(pInfo->pBitmapFree, _FBITMAP_SIZE(pVol));
		FFAT_FREE(pInfo->pBitmapPartialHPA, _FBITMAP_SIZE(pVol));
		FFAT_FREE(pInfo->pBitmapFullHPA, _FBITMAP_SIZE(pVol));
		FFAT_FREE(pInfo, sizeof(HPAInfo));

		return FFAT_OK;
	}


#else		// #ifndef FFAT_DYNAMIC_ALLOC


	/**
	 * get a free HPA Information storage
	 * 
	 * @return		NULL			: there is no free node storage, fail to get/put lock
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		21-MAY-2008 [DongYoung Seo] First Writing.
	 * @version		Aug-29-2009 [SangYoon Oh] Add the code for partial HPA bitmap 
	 */
	static HPAInfo*
	_getFreeHPAInfoStatic(void)
	{
		HPAInfo*		pInfo = NULL;
		EssList*		pList;
		t_uint8*		pTempHPA;
		t_uint8*		pTempPartialHPA;
		t_uint8*		pTempFree;
		t_uint8*		pTempUpdated;

		IF_UK (ESS_LIST_IS_EMPTY(&_slFreeHPAInfo) == ESS_TRUE)
		{
			FFAT_ASSERT(0);
			// No more free HPAInfo
			// need to increase FFAT_HPA_MAX_VOLUME_COUNT
			return NULL;
		}

		pList = ESS_LIST_GET_HEAD(&_slFreeHPAInfo);
		ESS_LIST_DEL(&_slFreeHPAInfo, pList->pNext);

		pInfo = ESS_GET_ENTRY(pList, HPAInfo, slFree);

		// store pointers
		pTempHPA		= pInfo->pBitmapFullHPA;
		pTempPartialHPA		= pInfo->pBitmapPartialHPA;
		pTempFree		= pInfo->pBitmapFree;
		pTempUpdated	= pInfo->pBitmapUpdated;

		// initializes storage
		FFAT_MEMSET(pInfo, 0x00, sizeof(HPAInfo));

		// restore pointers
		pInfo->pBitmapFullHPA		= pTempHPA;
		pInfo->pBitmapPartialHPA	= pTempPartialHPA;
		pInfo->pBitmapFree		= pTempFree;
		pInfo->pBitmapUpdated	= pTempUpdated;

		return pInfo;
	}

	/**
	* release a HPAInfo storage and add it to free list for static memory allocation
	* 
	* @param		pInfo			: HPA Information
	* @return		FFAT_OK			: success
	* @return		FFAT_EPANIC		: fail to get/put lock
	* @author		DongYoung Seo
	* @version		21-MAY-2008 [DongYoung Seo] First Writing.
	*/
	static FFatErr
		_releaseHPAInfoStatic(HPAInfo* pInfo)
	{
		FFAT_ASSERT(pInfo);

		ESS_LIST_ADD_HEAD(&_slFreeHPAInfo, &pInfo->slFree);

		return FFAT_OK;
	}


#endif		// end of #ifndef FFAT_DYNAMIC_ALLOC

/**
 * release a free HPA Information storage
 * 
 * @param		pVol			: [IN] volume information
 * @param		pInfo			: [IN] HPA Information
 * @return		NULL			: there is no free node storage, fail to get/put lock
 * @return		else			: success
 * @author		DongYoung Seo
 * @version		21-MAY-2008 [DongYoung Seo] First Writing.
 */
static FFatErr
_releaseHPAInfo(Vol* pVol, HPAInfo* pInfo)
{
#ifdef FFAT_DYNAMIC_ALLOC
	return _releaseHPAInfoDynamic(pVol, pInfo);
#else
	return _releaseHPAInfoStatic(pInfo);
#endif
}


/**
 * set bit for updated FAT sectors (only for cluster allocation)
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pVC				: [IN] cluster information
 *										storage for deallocated clusters 
 *										주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
 *										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 * @param		dwClusterCount	: [IN] updated cluster count
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 * @version		JAN-13-2008 [DongYoung Seo] separate function for allocate routine only
 */
static FFatErr
_updateUpdatedBitmapAlloc(Vol* pVol, FFatVC* pVC, t_uint32 dwClusterCount)
{
	HPAInfo*		pInfo;
	t_int32			dwBitmapSize;
	t_int32			i;
	t_uint32		dwCluster;			// cluster number
	t_uint32		dwCount;			// count of consecutive cluster
	t_uint32		dwSectorNo;			// FAT sector number
	t_uint32		dwTemp;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(dwClusterCount > 0);
	FFAT_ASSERT(VC_VEC(pVC) > 0);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC)) == FFAT_TRUE);

	pInfo			= _INFO(pVol);
	dwBitmapSize	= _FBITMAP_SIZE(pVol);

	if (dwClusterCount != VC_CC(pVC))
	{
		// pVC does not have all cluster information.

		// set all area
		FFAT_MEMSET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), 0xFF, dwBitmapSize);

		// clean spare bits
		if (((VOL_LVFSFF(pVol) - VOL_FFS(pVol)) & 0x07) != 0x07)
		{
			t_int32		dwBitMapLastSector;

			dwBitMapLastSector = (dwBitmapSize << 3) + VOL_FFS(pVol);

			for (i = VOL_LVFSFF(pVol) + 1; i < dwBitMapLastSector; i++)
			{
				_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, i);
			}
		}
	}

	// update bitmap
	for (i = 0; i < VC_VEC(pVC); i++)
	{
		dwCluster	= pVC->pVCE[i].dwCluster;
		dwCount		= pVC->pVCE[i].dwCount;

		while (dwCount > 0)
		{
			dwSectorNo = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwCluster);

			FFAT_ASSERT(dwSectorNo <= VOL_LVFSFF(pVol));

			if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwSectorNo - VOL_FFS(pVol))) == FFAT_TRUE)
			{
				// if the allocation is occurred in the normal free FAT sector,
				// then decrease free fat sector count on normal area
				_INFO_FFSCN_DEC(pInfo);
				_CLEAR_BITMAP_FREE_FOR_NORMAL_AREA(pVol, dwSectorNo);
			}
			
			_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwSectorNo);

			// get cluster count 
			dwTemp		= ESS_GET_MIN(dwCount, (VOL_CCPFS(pVol) - (dwCluster & VOL_CCPFS_MASK(pVol))));
			dwCount		-= dwTemp;		// decrease dwCount
			dwCluster	+= dwTemp;		// get next cluster number
		}
	}

	FFAT_ASSERT((_IS_VALID_HPA_TCC(pInfo) == FFAT_TRUE) ? (_INFO_FFSCN(pInfo) == (t_uint32)_getFreeFATSectorCount(pVol)) : FFAT_TRUE);

	_INFO_FLAG(pInfo) |= HPA_FAT_UPDATED;			// set FAT updated flag

	return FFAT_OK;
}


/**
 * set bit for updated FAT sectors (for deallocate only)
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pVC				: [IN] cluster information
 *										storage for deallocated clusters 
 *										주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
 *										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 * @param		dwClusterCount	: [IN] updated cluster count
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 * @version		JAN-13-2008 [DongYoung Seo] separate function for deallocate routine only
 */
static FFatErr
_updateUpdatedBitmapDealloc(Vol* pVol, FFatVC* pVC, t_uint32 dwClusterCount)
{
	HPAInfo*		pInfo;
	t_int32			dwBitmapSize;
	t_int32			i;
	t_uint32		dwCluster;			// cluster number
	t_uint32		dwCount;			// count of consecutive cluster
	t_uint32		dwSectorNo;			// FAT sector number
	t_uint32		dwTemp;
	t_boolean		bIsHPAFatSector;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(dwClusterCount > 0);
	FFAT_ASSERT(VC_VEC(pVC) > 0);
	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC)) == FFAT_TRUE);

	pInfo			= _INFO(pVol);
	dwBitmapSize	= _FBITMAP_SIZE(pVol);

	if (dwClusterCount != VC_CC(pVC))
	{
		// pVC does not have all cluster information.

		// set all area
		FFAT_MEMSET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), 0xFF, dwBitmapSize);

		// clean spare bits
		if (((VOL_LVFSFF(pVol) - VOL_FFS(pVol)) & 0x07) != 0x07)
		{
			t_int32		dwBitMapLastSector;

			dwBitMapLastSector = (dwBitmapSize << 3) + VOL_FFS(pVol);

			for (i = VOL_LVFSFF(pVol) + 1; i < dwBitMapLastSector; i++)
			{
				_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, i);
			}
		}
	}

	// update bitmap
	for (i = 0; i < VC_VEC(pVC); i++)
	{
		dwCluster	= pVC->pVCE[i].dwCluster;
		dwCount		= pVC->pVCE[i].dwCount;

		while (dwCount > 0)
		{
			dwSectorNo = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwCluster);

			FFAT_ASSERT(dwSectorNo <= VOL_LVFSFF(pVol));

			if (dwCluster & VOL_CCPFS_MASK(pVol)) //Head & Tail
			{
				// not aligned
				// set updated bit
				_SET_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwSectorNo);
				
			}
			else
			{
				bIsHPAFatSector = _isFATSectorFullHPA(pVol, dwSectorNo);

				// FAT sector aligned cluster information
				if (dwCount >= (t_uint32)VOL_CCPFS(pVol)) //Body
				{
					if (bIsHPAFatSector == FFAT_FALSE)
					{
						// this is normal area
						if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwSectorNo - VOL_FFS(pVol))) == ESS_FALSE)
						{
							// we find a new FREE FAT Sector in normal
							// increase free fat sector count on normal area
							_INFO_FFSCN_INC(pInfo);

							// free bitmap is only for normal area
							// update FAT Free bitmap
							_SET_BITMAP_FREE_FOR_NORMAL_AREA(pVol, dwSectorNo);
						}
						else
						{
							FFAT_ASSERT(0);
						}
					}

					FFAT_ASSERT((bIsHPAFatSector == FFAT_TRUE) ? (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (dwSectorNo - VOL_FFS(pVol))) == FFAT_FALSE) : FFAT_TRUE);

					_CLEAR_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwSectorNo);
				}
				else //aligned but partial clusters
				{
					if (bIsHPAFatSector == FFAT_FALSE)
					{
						_SET_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pVol, dwSectorNo);
					}
				}
			}

			// get cluster count 
			dwTemp		= ESS_GET_MIN(dwCount, (VOL_CCPFS(pVol) - (dwCluster & VOL_CCPFS_MASK(pVol))));
			dwCount		-= dwTemp;		// decrease dwCount
			dwCluster	+= dwTemp;		// get next cluster number
		}
	}

	_INFO_FLAG(pInfo) |= HPA_FAT_UPDATED;			// set FAT updated flag

	return FFAT_OK;
}

/**
 * update Partial HPA
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pVC				: [IN] vectored cluster storage
 * @param		bBitmapSet		: [IN] flag for setting or clearing bitmap
 * @param		dwFlag			: [IN] cache flag
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: updating bitmap failed mostly due to IO error
 * @author		SangYoon Oh
 * @version		Aug-29-2009 [SangYoon Oh] First Writing.
 * @version		Dec-15-2009 [SangYoon Oh] Modify the code to correctly clean the partial bit of a FAT sector that has no HPA cluster.
 */
static FFatErr 
_updatePartialHPA( Vol* pVol, FFatVC* pVC, t_boolean bBitmapSet, FFatCacheFlag dwFlag, ComCxt* pCxt )
{
	t_uint32		dwCluster;
	t_uint32		dwCount;
	t_uint32		dwRemainCount;
	t_uint32		dwFATSector;
	t_uint32		dwBitmapSector;
	t_uint32		dwUpdated;

	t_int8*			pBuff = NULL;
	t_uint32		dwBitmapOffset;
	t_uint32		dwBitmapPerSector;
	t_uint32		dwBitmapPerSectorMask;
	t_uint32		dwPrevBitmapSector;
	t_uint32		dwPrevFATSector;
	t_uint32		dwStartOffset;


	t_int32			i;
	FFatErr			r = FFAT_OK;

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);



	dwBitmapSector = 0;
	dwPrevBitmapSector = 0;
	dwPrevFATSector = 0;
	dwBitmapPerSector = VOL_CBCPS(pVol);
	dwBitmapPerSectorMask = dwBitmapPerSector - 1;
	dwFlag |= FFAT_CACHE_DATA_FS;

	pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	for (i = 0; i < VC_VEC(pVC); i++)
	{
		dwCluster = pVC->pVCE[i].dwCluster;
		dwRemainCount = pVC->pVCE[i].dwCount;
	
		while (dwRemainCount > 0)
		{
			dwCount	= ESS_GET_MIN(dwRemainCount, VOL_CCPFS(pVol) - (dwCluster & VOL_CCPFS_MASK(pVol)));
			dwFATSector = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), dwCluster);
			FFAT_ASSERT(dwFATSector <= VOL_LVFSFF(pVol));
			
			if (_isFATSectorFullHPA(pVol, dwFATSector) == FFAT_TRUE)
			{
				//skip current FAT Sector
				dwRemainCount	-= dwCount;		// decrease remain count
				dwCluster		+= dwCount;		// get next cluster number

			}
			else
			{
				//Update Cluster Bitmap
				dwBitmapSector = _GET_CBITMAP_SECTOR_OF_CLUSTER(pVol, dwCluster);		
				dwBitmapOffset = dwCluster & dwBitmapPerSectorMask;

				if (dwPrevBitmapSector != dwBitmapSector)
				{
					if (dwPrevBitmapSector > 0)
					{
						//이전에 기록하지 않은 bitmap이 있으면 우선 write
						r = ffat_readWriteSectors(pVol, NULL, dwPrevBitmapSector, 1, pBuff, 
									dwFlag, FFAT_FALSE, pCxt);
						FFAT_ASSERT(r > 0);
						FFAT_EO(r, (_T("fail to write Cluster Bitmap Sector")));
					}

					//The bitmap sector was changed. Read a new sector 
					r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBuff, 
							(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
					FFAT_ASSERT(r > 0);
					FFAT_EO(r, (_T("fail to read Cluster Bitmap Sector")));
					dwPrevBitmapSector = dwBitmapSector;
				}

				dwUpdated = 0;

				if (bBitmapSet)
				{
					//Set PartialHPA Bitmap
					if (dwPrevFATSector != dwFATSector)
					{
						_SET_BITMAP_PHPA(pVol, dwFATSector); //previous status를 고려하지 않고 무조건 bitmap set함
						dwPrevFATSector = dwFATSector;
					}

					//Set Cluster bitmap 
					do 
					{
#ifdef _HPA_DEBUG
						FFAT_ASSERT(ESS_BITMAP_IS_SET(pBuff, dwBitmapOffset) == FFAT_FALSE);
#endif
						ESS_BITMAP_SET(pBuff, dwBitmapOffset);
						dwUpdated++;
						dwBitmapOffset++;
						dwCount--;

					} while (dwCount > 0);
				}
				else
				{
					//Clear Partial HPA bitmap 
					dwStartOffset = (dwBitmapOffset & ~VOL_CCPFS_MASK(pVol)) >> 3;
					FFAT_ASSERT(dwStartOffset < (t_uint32)VOL_SS(pVol));
					
					//Clear Cluster bitmap
					do 
					{
#ifdef _HPA_DEBUG
						FFAT_ASSERT(ESS_BITMAP_IS_SET(pBuff, dwBitmapOffset) == FFAT_TRUE);
#endif
						ESS_BITMAP_CLEAR(pBuff, dwBitmapOffset);
						dwUpdated++;
						dwBitmapOffset++;
						dwCount--;

					} while (dwCount > 0);

					//현 FAT Sector의 cluster bitmap이 모두 clear되면 Partial hpa bitmap도 clear
					if (0 > EssBitmap_GetLowestBitOne((t_uint8*)(pBuff + dwStartOffset), VOL_CBCPFS(pVol)))
					{
						_CLEAR_BITMAP_PHPA(pVol, dwFATSector);
					}

				}

				dwCluster += dwUpdated;
				dwRemainCount -= dwUpdated;						
			}
		}
	}

	FFAT_ASSERT((dwPrevBitmapSector > 0)? (dwPrevBitmapSector == dwBitmapSector) : FFAT_TRUE);
	if (dwPrevBitmapSector > 0)
	{
		//마지막 변경 사항에 대한 sector write
		r = ffat_readWriteSectors(pVol, NULL, dwPrevBitmapSector, 1, pBuff, 
					dwFlag, FFAT_FALSE, pCxt);
		FFAT_ASSERT(r>0);
		FFAT_EO(r, (_T("fail to write Cluster Bitmap Sector")));
	}

out:

	FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);
	if (r < 0)
	{
		FFAT_ASSERT(0);
		FFAT_ER(r, (_T("fail to update cluster bitmap")));
	}
	return FFAT_OK;
}

/**
* get cluster bitmap sector and offset of a given Partial HPA FAT Sector
*
* @param		pVol		: [IN] volume pointer
* @param		dwFatSector	: [IN] FAT sector number
* @param		pdwCluster	: [OUT] first cluster of a given FAT Sector
* @param		pdwSector	: [OUT] cluster bitmap sector of a given FAT Sector
* @param		pdwOffset	: [OUT] offset from cluster bitmap sector
* @param		FFAT_OK		: success
* @param		else		: error
* @author		SangYoon Oh
* @version		Aug-29-2009 [SangYoon Oh] First Writing.
*/

static FFatErr
_getClusterBitmapSectorOfFatSector(Vol* pVol, t_uint32 dwFatSector, t_uint32* pdwCluster, t_uint32* pdwSector, t_uint32* pdwOffset)
{
	FFatErr		r;

	r = FFATFS_GetFirstClusterOfFatSector(VOL_VI(pVol), dwFatSector, pdwCluster);
	if (r < 0)
	{
		FFAT_ER(r, (_T("fail to GetFirstClusterOfFatSector")));
	}

	*pdwSector = _GET_CBITMAP_SECTOR_OF_CLUSTER(pVol, *pdwCluster);
	*pdwOffset = *pdwCluster & VOL_CBCPS_MASK(pVol);

	return FFAT_OK;
}

// debug begin
//=============================================================================
//
//	DEBUG PART
//
#ifdef FFAT_DEBUG
	/**
	* checks the allocated clusters are at HPA.
	*
	* @param		pVol			: [IN] previous end of file
	* @param		dwCluster		: [IN] cluster number
	* @param		dwCount			: [IN] cluster count to allocate
	* @return		FFAT_TRUE		: [IN] they are in the HPA
	* @return		FFAT_FALSE		: [IN] some of t he cluster is not in the HPA
	* @author		DongYoung Seo
	* @version		MAY-28-2007 [DongYoung Seo] First Writing.
	* @version		Aug-29-2009 [SangYoon Oh] Add the code to check partial HPA also
	* @version		Aug-29-2009 [SangYoon Oh] Modify the code to check whether every single cluster is HPA or not
	*/
	static t_boolean
	_isClusterHPA(Vol* pVol, t_uint32 dwCluster, t_int32 dwCount, t_boolean dwExpectHPA, ComCxt* pCxt)
	{
		t_uint32			i;
		t_uint32		dwSector;
		t_uint32		dwLastCluster;

		t_uint32		dwBitmapSector;

		t_int8*			pBuff = NULL;
		t_uint32		dwBitmapOffset;
		t_uint32		dwBitmapPerSector;
		t_uint32		dwBitmapPerSectorMask;
		t_uint32		dwPrevBitmapSector;

		FFatErr			r;

		r = (dwExpectHPA)? FFAT_TRUE : FFAT_FALSE;

		dwBitmapSector = 0;
		dwPrevBitmapSector = 0;
		dwBitmapPerSector = VOL_CBCPS(pVol);
		dwBitmapPerSectorMask = dwBitmapPerSector - 1;


		pBuff = (t_int8*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
		FFAT_ASSERT(pBuff != NULL);



		dwPrevBitmapSector = 0;
		for (i = dwCluster; i < dwCluster + dwCount; i++)
		{
			dwSector = FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), i);
			IF_UK (_isFATSectorFullHPA(pVol, dwSector) == FFAT_TRUE)
			{
				if (dwExpectHPA == FFAT_FALSE)
				{
					r = FFAT_TRUE;
					FFAT_EO(r, (_T("cluster is HPA ")));
				}

				// get the last cluster of this sector
				FFATFS_GetLastClusterOfFatSector(VOL_VI(pVol), dwSector, &dwLastCluster);

				i = dwLastCluster;
			}
			else if (_isFATSectorPartialHPA(pVol, dwSector) == FFAT_TRUE)
			{
				dwBitmapSector = _GET_CBITMAP_SECTOR_OF_CLUSTER(pVol, dwCluster);		
				dwBitmapOffset = dwCluster & dwBitmapPerSectorMask;

				if (dwPrevBitmapSector != dwBitmapSector)
				{
					//The bitmap sector was changed. Read a new sector 
					r = ffat_readWriteSectors(pVol, NULL, dwBitmapSector, 1, pBuff, 
							(FFAT_CACHE_DATA_FS), FFAT_TRUE, pCxt);
					FFAT_ASSERT(0);
					FFAT_EO(r, (_T("fail to read Cluster Bitmap Sector")));
					dwPrevBitmapSector = dwBitmapSector;
				}

				if (dwExpectHPA != ESS_BITMAP_IS_SET(pBuff, dwBitmapOffset))
				{
					r = ESS_BITMAP_IS_SET(pBuff, dwBitmapOffset);
					FFAT_EO(r, (_T("not expected cluster type ")));
				}
			}
			else
			{
				//normal
				if (dwExpectHPA)
				{
					FFAT_ASSERTP(0, (_T("cluster check error")));
					r = FFAT_FALSE;
					FFAT_EO(r, (_T("cluster is normal")));
				}
			}
		}

out:
		if (pBuff)
		{
			FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);
		}

		return r;

	}

	/**
	* checks the allocated cluster status.
	* Normal NODE should have all normal clusters
	* HPA NODE should have all HPA clusters
	*
	* @param		dwPrevEOF		: [IN] previous end of file
	* @param		dwCount			: [IN] cluster count to allocate
	* @param		pVC			: [IN/OUT] cluster information
	* @param		dwAllocatedCount : [IN] allocated cluster count
	* @author		DongYoung Seo
	* @version		MAY-28-2007 [DongYoung Seo] First Writing.
	*/
	static void
	_debugCheckAllocation(Node* pNode, t_uint32 dwCount, 
							FFatVC* pVC, ComCxt* pCxt)
	{
		t_int32			i;
		t_boolean		bRet;

		FFAT_ASSERT(pNode);
		FFAT_ASSERT(pVC);

		FFAT_ASSERT((VC_CC(pVC) > 0) ? FFAT_TRUE : (dwCount >= (VC_CC(pVC) - 1)));

		for (i = 0; i < VC_VEC(pVC); i++)
		{
			FFAT_ASSERT(pVC->pVCE[i].dwCluster + pVC->pVCE[i].dwCount - 1 < _INFO_CMC(_INFO(NODE_VOL(pNode))));

			if (_IS_NODE_AT_HPA(pNode) == FFAT_TRUE)
			{
				bRet = _isClusterHPA(NODE_VOL(pNode), pVC->pVCE[i].dwCluster, pVC->pVCE[i].dwCount, FFAT_TRUE, pCxt);
				FFAT_ASSERT(bRet == FFAT_TRUE);
			}
			else
			{
				bRet = _isClusterHPA(NODE_VOL(pNode), pVC->pVCE[i].dwCluster, pVC->pVCE[i].dwCount, FFAT_FALSE, pCxt);
				FFAT_ASSERT(bRet == FFAT_FALSE);
			}
		}

		return;
	}


	static void
	_debugPrintVC(FFatVC* pVC)
	{
#ifdef _HPA_DEBUG
		t_int32		i;
		FFAT_ASSERT(pVC);

		FFAT_DEBUG_HPA_PRINTF((_T("VC [Cluster/Count] ==> ")));
		for (i = 0; i < VC_VEC(pVC); i++)
		{
			FFAT_DEBUG_PRINTF((_T("[%d/%d]"), pVC->pVCE[i].dwCluster, pVC->pVCE[i].dwCount));
		}
		FFAT_DEBUG_PRINTF((_T("\n")));
#endif
	}


	/**
	* check free / total cluster count for HPA
	*
	* @param		pVol		: [IN] volume pointer
	* @author		DongYoung Seo
	* @version		MAY-28-2007 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugCheckFCC(Vol* pVol, ComCxt* pCxt)
	{
#if 0
		HPAInfo*		pInfo;
		t_uint32		i;
		t_uint32		dwFCC;		// free cluster count
		t_uint32		dwTCC;		// total cluster count
		t_uint32		dwTFCC;		// total free cluster count
		t_uint32		dwLVFSFF;	// the Last Valid Fat Sector on First FAT
		FFatErr			r;

		FFAT_ASSERT(pVol);

		if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
		{
			// nothing to do
			return FFAT_OK;
		}

		pInfo = _INFO(pVol);

		if (_IS_VALID_HPA_FCC(pInfo) == FFAT_FALSE)
		{
			return FFAT_OK;
		}

		r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
		FFAT_ER(r, (_T("fail to sync FCC")));

		dwTFCC		= 0;
		dwTCC		= 0;
		dwLVFSFF	= VOL_LVFSFF(pVol);

		for (i = _INFO_LFHPAS(pInfo); i <= dwLVFSFF; i++)
		{
			if (_isFATSectorFullHPA(pVol, i) == ESS_FALSE)
			{
				continue;
			}

			FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (i - VOL_FFS(pVol))) == FFAT_FALSE);

			r = FFATFS_GetFCCOfSector(VOL_VI(pVol), i, &dwFCC, pCxt);
			FFAT_ER(r, (_T("fail to get Free Cluster Count")));

			dwTFCC += dwFCC;

			if (i == dwLVFSFF)
			{
				dwTCC += VOL_CCLFS(pVol);		// cluster count on last FAT sector
			}
			else
			{
				dwTCC += VOL_CCPFS(pVol);		// cluster count per a FAT sector
			}
		}

		if (_INFO_FCC(pInfo) != dwTFCC)
		{
			FFAT_ASSERT(0);
		}
		
		if (_INFO_TCC(pInfo) != dwTCC)
		{
			FFAT_ASSERT(0);
		}

#endif	

		return FFAT_OK;
	}


	/**
	* check FAT free bitmap
	*
	* @param		pVol		: [IN] volume pointer
	* @param		pCxt		: [IN] current context
	* @author		DongYoung Seo
	* @version		JUN-16-2008 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugCheckBitmapFree(Vol* pVol, ComCxt* pCxt)
	{
		HPAInfo*		pInfo;
		t_uint32		i;
		t_uint32		dwBitIndex;
		FFatErr			r;

		FFAT_ASSERT(pVol);

		pInfo = _INFO(pVol);

		if ((_INFO_FLAG(pInfo) & HPA_VALID_FREE_BITMAP) == 0)
		{
			FFAT_ASSERT((_INFO_FLAG(pInfo) & HPA_VALID_UPDATE_BITMAP) == 0);
			return FFAT_OK;
		}

		// sync Free Cluster Cache
		r = ffat_fcc_syncVol(pVol, FFAT_CACHE_NONE, pCxt);
		FFAT_ER(r, (_T("fail to sync FCC")));

		for (i = VOL_FFS(pVol); i <= VOL_LVFSFF(pVol); i++)
		{
			dwBitIndex = i - VOL_FFS(pVol);

			if (ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo), dwBitIndex) == FFAT_TRUE)
			{
				// this is updated FAT sector
				continue;
			}

			r = FFATFS_IsFreeFatSector(VOL_VI(pVol), i, FFAT_CACHE_DATA_FAT, pCxt);
			FFAT_ASSERT(r >= 0);
			if (r == FFAT_TRUE)
			{
				if (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
				{
					FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), dwBitIndex) == FFAT_FALSE);
				}
				else
				{
					// free FAT Sector
					FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), dwBitIndex) == FFAT_TRUE);
				}
			}
			else
			{
				FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), dwBitIndex) == FFAT_FALSE);
			}
		}

		_debugCheckBitmap(pVol);

		return FFAT_OK;
	}


	/**
	* check bitmap area (rest area)
	*
	* @param		pVol		: [IN] volume pointer
	* @author		DongYoung Seo
	* @version		JUN-18-2008 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugCheckBitmap(Vol* pVol)
	{
		// check bitmap spare area is 0
		HPAInfo*	pInfo;
		t_int32		dwIndex;				// last bitmap index 
		t_int32		dwLastFATSectorOffset;	// the last FAT sector offset
		t_int32		i;

		// No spare bitmap area
		if (((VOL_LVFSFF(pVol) - VOL_FFS(pVol)) & 0x07) == 0x07)
		{
			// no spare bitmap area
			return FFAT_OK;
		}

		pInfo					= _INFO(pVol);
		dwIndex					= _FBITMAP_SIZE(pVol) - 1;
		dwLastFATSectorOffset	= VOL_LVFSFF(pVol) - VOL_FFS(pVol);

		for (i = ((dwLastFATSectorOffset & 0x07) + 1); i < 8; i++)
		{
			//check bitmap HPA
			FFAT_ASSERT(ESS_BITMAP_IS_SET(&_INFO_BITMAP_FHPA(pInfo)[dwIndex], i) == FFAT_FALSE);

			// check bitmap Free
			FFAT_ASSERT(ESS_BITMAP_IS_SET(&_INFO_BITMAP_FREE(pInfo)[dwIndex], i) == FFAT_FALSE);

			// check bitmap Updated
			FFAT_ASSERT(ESS_BITMAP_IS_SET(&_INFO_BITMAP_FREE_FAT_SECTOR_CANDIDATE(pInfo)[dwIndex], i) == FFAT_FALSE);
		}

		return FFAT_OK;
	}


	/**
	* get count of FAT sectors that is wholly free
	*
	* @param		pVol			: [IN] volume pointer
	* @return		FFAT_OK			: success
	* @author		DongYoung Seo
	* @version		JUN-16-2008 [DongYoung Seo] First Writing.
	*/
	static t_int32
		_getFreeFATSectorCount(Vol* pVol)
	{
		FFAT_ASSERT(pVol);
		FFAT_ASSERT(_IS_ACTIVATED(pVol) == FFAT_TRUE);

		return EssBitmap_GetCountOfBitOne(_INFO_BITMAP_FREE(_INFO(pVol)), _FBITMAP_SIZE(pVol));
	}

	FFatErr		ffat_hpa_checkTCCOfHPA(Vol* pVol, ComCxt* pCxt)
	{
		if (_IS_ACTIVATED(pVol) == FFAT_FALSE)
		{
			// Nothing to do
			return FFAT_OK;
		}

		return _debugGetTCCOfHPA(pVol, pCxt);
	}

	/**
	* get total count of HPA clusters
	*
	* @param		pVol			: [IN] volume pointer
	* @param		pCxt			: [IN] current context
	* @return			t_uint32		:  total count of HPA clusters
	* @author		Sang-Yoon Oh
	* @version		NOV-25-2009 [Sang-Yoon Oh] First Writing.
	*/
	static t_uint32
	_debugGetTCCOfHPA(Vol* pVol, ComCxt* pCxt)
	{
		HPAInfo*		pInfo;
		t_uint32		i;
		t_uint32		dwFCC;				// free cluster count

		t_uint32		dwLVFSFF;			// Last Valid FAT Sector on First FAT
		t_uint32		dwCount;
		FFatErr			r;
		t_uint32		dwTCCHPA;			// total cluster count for HPA
		t_uint32		dwTCCFHPA;		// total cluster count for Full HPA
		t_uint32		dwTCCPHPA;		// total cluster count for Partial HPA
		FFAT_ASSERT(pVol);

		pInfo = _INFO(pVol);
		if (_IS_VALID_HPA_TCC(pInfo) != FFAT_TRUE)
		{
			return 0;
		}

		dwTCCPHPA			= 0;
		dwTCCFHPA			= 0;
		dwTCCHPA			= 0;
		dwLVFSFF			= VOL_LVFSFF(pVol);

		for (i = VOL_FFS(pVol); i <= dwLVFSFF; i++)
		{
			// get free cluster count in fat sector
			r = ffat_fcc_getFCCOfSector(pVol, i, &dwFCC, pCxt);
			FFAT_ER(r, (_T("fail to get Free Cluster Count")));
			if (r != FFAT_DONE)
			{
				// FCC is not activated
				r = FFATFS_GetFCCOfSector(VOL_VI(pVol), i, &dwFCC, pCxt);
				FFAT_ER(r, (_T("fail to get Free Cluster Count")));
			}

			if (dwFCC != (t_uint32)VOL_CCPFS(pVol))
			{
				FFAT_ASSERT((_isFATSectorFullHPA(pVol, i) && _isFATSectorPartialHPA(pVol, i))== 0);

				//TCC of HPA 계산
				if (_isFATSectorFullHPA(pVol, i) == FFAT_TRUE)
				{
					FFAT_ASSERT(ESS_BITMAP_IS_SET(_INFO_BITMAP_FREE(pInfo), (i - VOL_FFS(pVol))) == ESS_FALSE);
					if (i < dwLVFSFF)
					{
						dwTCCHPA += (VOL_CCPFS(pVol) - dwFCC);		// cluster count per a FAT sector
						ESS_DEBUG_PRINTF("Full:%d\n", (VOL_CCPFS(pVol) - dwFCC));
						dwTCCFHPA  +=VOL_CCPFS(pVol) - dwFCC;
					}
					else
					{
						dwTCCHPA += (VOL_CCLFS(pVol) - dwFCC);		// cluster count on last FAT sector
						dwTCCFHPA  += VOL_CCLFS(pVol) - dwFCC;
					}
				}
				else if (_isFATSectorPartialHPA(pVol, i) == FFAT_TRUE)
				{
					dwCount = 0;
					//FAT Sector에 해당하는 Cluster Bitmap을 Scan하여 Set된 Cluster개수를 계산
					r = _getGetPartialHPAClusterCountOfFATSector(pVol, i, &dwCount, pCxt);
					FFAT_ASSERT(r >= 0);
					
					dwTCCHPA += dwCount;
					dwTCCPHPA += dwCount;
				}
				else
				{
					dwCount = 0;

					r = _getGetPartialHPAClusterCountOfFATSector(pVol, i, &dwCount, pCxt);
					FFAT_ASSERT(dwCount == 0);
				}
			}
		}
		FFAT_DEBUG_HPA_PRINTF(("Full HPA:%d, Partial HPA:%d, HPA Total:%d\n", dwTCCFHPA, dwTCCPHPA, dwTCCHPA));
		return dwTCCHPA;
	}
#endif	// #ifdef FFAT_DEBUG
// debug end

