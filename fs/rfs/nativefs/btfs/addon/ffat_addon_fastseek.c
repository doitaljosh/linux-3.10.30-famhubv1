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
* @file			ffat_addon_fastseek.c
* @brief		fast seek moudle.
* @author		DongYoung Seo(dy76.seo@samsung.com)
* @version		JUL-04-2006 [DongYoung Seo] First writing
* @version		SEP-01-2007 [SungWoo Jo]	supports GFS features
* @version		FEB-02-2009 [DongYoung Seo] Remove user fast seek
* @version		JAN-14-2010 [ChunUm Kong]	Modifying comment (English/Korean)
* @see			None
*/

#include "ess_math.h"
#include "ess_bitmap.h"
#include "ess_dlist.h"

#include "ffat_common.h"

#include "ffat_errno.h"
#include "ffat_addon_types.h"

#include "ffat_node.h"
#include "ffat_vol.h"

#include "ffat_addon_fastseek.h"
#include "ffat_addon_types_internal.h"

#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_FASTSEEK)

//#define _DEBUG_GFS				// enable debug print for global fast seek

#define _GFS_HEURISTIC

// ============================================================================
//
//	a disk of gossip :)
//	why does the user fast seek not use VC ?
//		I can not figure exact memory size for a node with VC.
//		It also need search operation to get cluster of offset.
//
//	Global Fast Seek == Automatic Fast Seek.
//		It manages VC
//
//	Notation
//
//		UFS		: User Fast Seek. use buffer from user
//		GFS		: Global Fast Seek, user internal buffer
//		GFSMain	: Global Fast Seek information
//		GFSE	: Global Fast Seek Entry
//

// definitions for GFS
#define _GFS_VPE				FFAT_ADDON_GFS_VCE_PER_ENTRY		// VCE per entry
#define _GFS_VPE_MASK			(FFAT_ADDON_GFS_VCE_PER_ENTRY - 1)

// cluster chunk count that can be added to the GFS list for a node
//	on going to the target offset.
// add pass through clusters to the GFS they are not the target position.
//	0		: Maximum GFSE count for pass through
//	else	: initial pass through count.
#define _GFS_PASS_THROUGH_ADD_COUNT			(0)
#define _GFS_PASS_THROUGH_HIT_TRIGGER		100			//!< trigger count to increase pass through add count
#define _GFS_PASS_THROUGH_MISS_TRIGGER		100			//!< trigger count to decrease pass through add count

// get total cluster count
//	_pE : entry pointer
#define _GFSE_CC(_pE)			((_pE)->dwTotalClusterCount)
#define _GFSE_VEC(_pE)			((_pE)->wValidEntryCount)
#define _GFSE_LEI(_pE)			(_GFSE_VEC(_pE) - 1)

// get last cluster
//	_pE : entry pointer
#define _GFSE_LC(_pE)					(((_pE)->pVCE[_GFSE_VEC(_pE) - 1]).dwCluster + ((_pE)->pVCE[_GFSE_VEC(_pE) - 1]).dwCount - 1)

// Get last cluster offset of an entry
//	_pE : entry pointer
//	_pV : volume pointer
#define _GFSE_LCO(_pE, _pV)				((_pE)->dwOffset + ((_GFSE_CC(_pE) - 1) << VOL_CSB(_pV)))

// Get last offset of an entry
//	_pE : entry pointer
//	_pV : volume pointer
#define _GFSE_LO(_pE, _pV)				(_GFSE_LCO(_pE, _pV) + VOL_CSM(_pV))

//get GFS head for a node
//	_pN : node pointer
#define _GFS_NODE_HEAD(_pN)				(NODE_ADDON(_pN)->pGFS)

#define _GFS_IS_ACTIVATED(_pN)			(_GFS_NODE_HEAD(_pN) ? FFAT_TRUE : FFAT_FALSE)

//get GFS head list for a node
//	_pN : node pointer
#define _GFS_GET_NODE_LIST_HEAD(_pN)	(&(_GFS_NODE_HEAD(_pN)->stDListNode))

// move to free list
//	_pE	: entry pointer
#define _GFS_MOVE_TO_FREE(_pE)		do {	\
										ESS_DLIST_MOVE_HEAD(&_stGFSM.dlGFSEFree, &(_pE)->dlLru);	\
										ESS_DLIST_DEL_INIT(&(_pE)->stDListNode);	\
									} while (0)

// move to LRU HEAD
//	_pE	: entry pointer
#define _GFS_MOVE_TO_LRU_HEAD(_pE)	ESS_DLIST_MOVE_HEAD(&_stGFSM.dlGFSELru, &(_pE)->dlLru)

// check the GFSE is full or not
//	_pE : entry pointer
#define _GFSE_IS_EMPTY(_pE)			((_GFSE_VEC(_pE) == 0) ?  FFAT_TRUE : FFAT_FALSE)

// check the GFSE is full or not
//	_pE : entry pointer
#define _GFSE_IS_FULL(_pE)			((_GFSE_VEC(_pE) == FFAT_ADDON_GFS_VCE_PER_ENTRY) ?  FFAT_TRUE : FFAT_FALSE)

// check _pE is the last entry of _pN
//	_pE : entry pointer
//	_pN : node pointer
#define _GFSE_IS_LAST(_pE, _pN)		((((_pE)->stDListNode.pNext) == _GFS_GET_NODE_LIST_HEAD(_pN)) ? FFAT_TRUE : FFAT_FALSE)

// check _pE is the last entry of _pN
//	_pE : entry pointer
//	_pN : node pointer
#define _GFSE_GET_NEXT(_pE)			(ESS_GET_ENTRY(ESS_DLIST_GET_NEXT(&(_pE)->stDListNode), _GFSEntry, stDListNode))

#define _GFSN_ADD_TO_FREE(_pN)		ESS_DLIST_ADD_HEAD(&_stGFSM.dlFreeNodeHead, _GFS_GET_NODE_LIST_HEAD(_pN));

#ifdef _GFS_HEURISTIC
	#define _HEURISTIC_GFS(a)		_heuristicGFS(a)
#else
	#define _HEURISTIC_GFS(a);
#endif

#ifdef FFAT_DYNAMIC_ALLOC
	#define	_NODE_HEADS_PER_STORAGE			(32)			// node head count per a node head storage
#else
	#define _NODE_HEADS_PER_STORAGE			(FFAT_ADDON_GFS_NODE_COUNT)
#endif

// typedefs

//!< The entry information for GFS
typedef struct
{
	EssDList		dlLru;					//!< for LRU / Free List
	EssDList		stDListNode;			//!< list to be connected to a node
	t_uint32		dwOffset;				//!< byte offset for this entry
	t_uint32		dwTotalClusterCount;	//!< Total cluster count in this entry
	t_int16			wValidEntryCount;		//!< valid VCE count
	t_uint16		wDummy;					//!< dummy
	FFatVCE*		pVCE;					//!< array of VCE
} _GFSEntry;


// Global Fast Seek Node Head Storage
typedef struct
{
	EssList			slList;					//!< list for allocated list
	GFSNodeHead		stGFSNH[_NODE_HEADS_PER_STORAGE];
											//!< head array for GFS Node
} _GFSNHStorage;


//!< The main information for GFS
typedef struct
{
	EssDList		dlGFSELru;				//!< LRU list
	EssDList		dlGFSEFree;				//!< Free list
	_GFSEntry*		pGFSE;					//!< Array for GFSE,
											//!< allocated buffer pointer 

	EssList			slGFSNHS;				//!< GFS Node Head Storage List, all created node heads are here.
	EssDList		dlFreeNodeHead;			//!< free list for GFSH

	t_uint32		dwReadAhead;			//!< Read ahead cluster count
	t_int16			wPassThrough;			//!< dynamic entry add count (store pass through entry)
	t_int16			wEntryCount;			//!< GFSE count

	t_int16			wHit;					// hit count
	t_int16			wMiss;					//!< miss count
											//!< negative : miss
											//!< positive : hit
											//!< if this value reached to -100 then decreases wPassThrough
											//!< if this value reached to +100 then increases wPassThrough
} _GFSMain;

// Static Functions

// static functions for GFS
static FFatErr		_getClusterOfOffsetGFS(Node* pNode, t_uint32 dwOffset, t_uint32* pdwCluster,
								t_uint32* pdwOffset, NodePAL* pPAL, ComCxt* pCxt);
static FFatErr		_getGFSE(Node* pNode, t_uint32 dwOffset, t_uint32 dwClusterCount,
								_GFSEntry** ppEntry, t_boolean bBuild, t_uint32 dwReadAhead,
								t_boolean bGetContiguous, ComCxt* pCxt);
static FFatErr		_buildGFS(Node* pNode, t_uint32 dwOffset, t_uint32 dwRequestCount,
								_GFSEntry* pCurEntry, _GFSEntry** ppEntry,
								t_uint32 dwReadAhead, t_boolean bGetContiguous, ComCxt* pCxt);
static _GFSEntry*	_allocGFSE(Node* pNode, _GFSEntry* pEntryPrev);
static FFatErr		_resetFromGFS(Node* pNode, t_uint32 dwOffset);
static FFatErr		_getVectoredClusterGFS(Node* pNode, t_uint32 dwCount, FFatVC* pVC,
								t_uint32 dwStartOffset, t_uint32* pdwNewCluster,
								t_uint32* pdwNewCount, NodePAL* pPAL, ComCxt* pCxt);
static FFatErr		_getIndexOfOffsetGFS(Vol* pVol, _GFSEntry* pEntry,
								t_uint32 dwOffset, t_int16* pwIndex, t_uint32* pdwOffset);
static void			_getChunkGFS(FFatVC* pVC, t_int32 dwStartIndex,
								t_int32* pdwVEC, t_uint32* pdwTCC);
static _GFSEntry*	_addNewGFSE(Node* pNode, FFatVC* pVC, t_int32 dwIndex,
								t_int32 dwEntryCount, t_uint32 dwOffset, t_uint32 dwTCC,
								_GFSEntry* pPrevGFSE, ComCxt* pCxt);

static FFatErr		_getFreeGFSNodeHead(Node* pNode);
static void			_releaseGFSNodeHead(Node* pNode);

static void			_heuristicGFS(t_boolean bHit);

// global fast seek main
static _GFSMain		_stGFSM;

static FFatErr		_terminateGFSNHStorage(void);
static FFatErr		_allocGFSNHStorageDynamic(void);

#ifdef FFAT_DYNAMIC_ALLOC
	#define _INIT_GFSNH_STORAGE			_initGFSNHStorageDynamic
	static FFatErr		_initGFSNHStorageDynamic(void);
#else
	#define _INIT_GFSNH_STORAGE			_initGFSNHStorageStatic
	static FFatErr		_initGFSNHStorageStatic(void);
#endif

// for statistic
#define _STATISTICS_GFS_HIT
#define _STATISTICS_GFS_MISS
#define _STATISTICS_GFS_REPLACE
#define _STATISTIC_INIT
#define _STATISTIC_PRINT

// debug begin
#ifdef FFAT_DEBUG
	typedef struct
	{
		t_uint32		dwGFSHit;		// GFS Hit Count
		t_uint32		dwGFSMiss;		// GFS Miss Count
		t_uint32		dwGFSReplace;	// GFS Entry Replace Count
		t_uint32		dwGetGFSNH;		// count of getting GFSNH
		t_uint32		dwReleaseGFSNH;	// count of releasing GFSNH
	} _debugStatisc;

	#define _STATISTIC()		(&_stStatics)

	//static _MainDebug	_stMainDebug;
	static _debugStatisc _stStatics;

	#undef _STATISTICS_GFS_HIT
	#undef _STATISTICS_GFS_MISS
	#undef _STATISTICS_GFS_REPLACE
	#undef _STATISTIC_INIT
	#undef _STATISTIC_PRINT

	#define _STATISTICS_GFS_HIT				_stStatics.dwGFSHit++;
	#define _STATISTICS_GFS_MISS			_stStatics.dwGFSMiss++;
	#define _STATISTICS_GFS_REPLACE			_stStatics.dwGFSReplace++;
	#define _STATISTICS_GFS_GET_GFSNH		++_stStatics.dwGetGFSNH
	#define _STATISTICS_GFS_RELEASE_GFSNH	++_stStatics.dwReleaseGFSNH

	#define _STATISTIC_INIT				FFAT_MEMSET(&_stStatics, 0x00, sizeof(_debugStatisc));
	#define _STATISTIC_PRINT			_printStatistics();

	static void			_printStatistics(void);
#endif

#ifdef _DEBUG_GFS
	#define		FFAT_DEBUG_GFS_PRINTF		FFAT_PRINT_VERBOSE((_T("[GFS] %s()/%d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE
#else
	#define		FFAT_DEBUG_GFS_PRINTF(_msg)
#endif

#ifdef FFAT_DEBUG
	//#define _CHECK_ALL

	#ifdef _CHECK_ALL
		#define _CHECK_GFSE(_pN, _pE, _pCxt)		_checkGFSE(_pN, _pE, _pCxt)

		static void	_checkGFSE(Node* pNode, _GFSEntry* pEntry, ComCxt* pCxt);
	#else
		#define _CHECK_GFSE(_pN, _pE, _pC)
	#endif
#else
	#define _CHECK_GFSE(_pN, _pE, _pC)
#endif
// debug end


/**
* initialize fast seek
*
* @return		FFAT_OK		: success
* @return		else		: error
* @author		DongYoung Seo
* @version		AUG-23-2006 [DongYoung Seo] First Writing.
* @version		AUG-27-2007 [SungWoo Jo]	updates about GFS
* @version		JAN-05-2008 [DongYoung Seo] improve GFS
* @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
*/
FFatErr
ffat_fastseek_init(t_boolean bForce)
{
	t_int32				dwEntryCount;
	t_int32				i;
	_GFSEntry*			pEntry;
	FFatVCE*			pVCE;
	FFatErr				r;

	FFAT_ASSERTP(EssMath_IsPowerOfTwo(FFAT_ADDON_GFS_VCE_PER_ENTRY) == FFAT_TRUE, (_T("invalid FFAT_ADDON_GFS_VCE_PER_ENTRY config")));

	if ((_stGFSM.pGFSE != NULL) && (bForce == FFAT_FALSE))
	{
		FFAT_LOG_PRINTF((_T("global fast seek buffer should be NULL")));
		return FFAT_EINIT_ALREADY;
	}

	FFAT_MEMSET(&_stGFSM, 0x00, sizeof(_GFSMain));

	// allocates GFS
	_stGFSM.pGFSE = (_GFSEntry*)FFAT_MALLOC(FFAT_ADDON_GFS_MEM_SIZE, ESS_MALLOC_NONE);
	if (_stGFSM.pGFSE == NULL)
	{
		FFAT_LOG_PRINTF((_T("Fail to alloc memory for global fast seek (1)")));
		return FFAT_ENOMEM;
	}

	ESS_DLIST_INIT(&_stGFSM.dlGFSELru);
	ESS_DLIST_INIT(&_stGFSM.dlGFSEFree);

	dwEntryCount = FFAT_ADDON_GFS_MEM_SIZE 
					/ (sizeof(_GFSEntry) + (sizeof(FFatVCE) * _GFS_VPE));

	pEntry	= _stGFSM.pGFSE;								// set first entry pointer
	pVCE	= (FFatVCE*)(_stGFSM.pGFSE + dwEntryCount);		// set first VCE pointer 

	for (i = 0; i < dwEntryCount; i++)
	{
		ESS_DLIST_INIT(&pEntry->dlLru);
		ESS_DLIST_INIT(&pEntry->stDListNode);
		_GFS_MOVE_TO_FREE(pEntry);

		pEntry->pVCE	= pVCE;			// Vector Cluster Entry

		pEntry++;
		pVCE = pVCE + _GFS_VPE;
	}

	_stGFSM.dwReadAhead		= FFAT_ADDON_GFS_READ_AHEAD_COUNT;
	_stGFSM.wPassThrough	= _GFS_PASS_THROUGH_ADD_COUNT;
	if (_stGFSM.wPassThrough == 0)
	{
		_stGFSM.wPassThrough = (t_int16)dwEntryCount;
	}

	_stGFSM.wEntryCount		= (t_int16)dwEntryCount;

	// initializes GFS Node Head Storage
	r = _INIT_GFSNH_STORAGE();
	FFAT_EO(r, (_T("fail to init GFS Node Head Storage")));

	_STATISTIC_INIT

	r = FFAT_OK;

out:
	IF_UK (r != FFAT_OK)
	{
		_terminateGFSNHStorage();
		FFAT_FREE(_stGFSM.pGFSE, sizeof(FFAT_ADDON_GFS_MEM_SIZE));
	}

	return r;
}


/**
* This function terminates GFS
*
* @return		FFAT_OK		: success
* @author		SungWoo Jo
* @version		AUG-27-2007 [SungWoo Jo]	First Writing.
* @version		JAN-05-2008 [DongYoung Seo] improve GFS
*/
FFatErr
ffat_fastseek_terminate(void)
{
	if (_stGFSM.pGFSE)
	{
		FFAT_FREE(_stGFSM.pGFSE, FFAT_ADDON_GFS_MEM_SIZE);
		_stGFSM.pGFSE = NULL;
	}

	_terminateGFSNHStorage();

	_STATISTIC_PRINT

	return FFAT_OK;
}


/**
* This function terminates global fast seek
*
* @return		FFAT_OK		: success
* @author		SungWoo Jo
* @version		AUG-27-2007 [SungWoo Jo]	First Writing.
*/
FFatErr
ffat_fastseek_umount(Vol* pVol)
{
	// Nothing to do ~~

	return FFAT_OK;
}


/**
* Init node for fast seek
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		AUG-23-2006 [DongYoung Seo] First Writing.
* @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
*/
FFatErr
ffat_fastseek_initNode(Node* pNode)
{
	// initializes GFS Head
	NODE_ADDON(pNode)->pGFS = NULL;

	return FFAT_OK;
}


/**
* terminate node for fast seek
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo]	First Writing.
*/
FFatErr
ffat_fastseek_terminateNode(Node* pNode)
{
	FFatErr		r;

	if (_GFS_IS_ACTIVATED(pNode)  == FFAT_TRUE)
	{
		r = _resetFromGFS(pNode, 0);
		FFAT_ER(r, (_T("fail to reset GFS")));
	}

	return FFAT_OK;
}


/**
* release GFS for nodes related to rename operation
* This function is called after rename operation on success
*
* @param		pNodeSrc		: [IN] source node pointer
* @param		pNodeDes		: [IN] destination node pointer
* @param		pNodeNewDes		: [IN] new destination node pointer
*									   this node has same ADDON information with pNodeSrc
* @return		FFAT_OK			: success
* @return		negative		: fail
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo] First Writing.
* @version		AHN-06-2009 [DongYoung Seo] change function name from ffat_fastseek_rename()
*										because this is after operation for rename
*/
FFatErr
ffat_fastseek_rename(Node* pNodeSrc, Node* pNodeDes, Node* pNodeNewDes)
{
	FFatErr		r;

	if (_GFS_IS_ACTIVATED(pNodeSrc) == FFAT_TRUE)
	{
		r = ffat_fastseek_deallocateGFSE(pNodeSrc);
		FFAT_ER(r, (_T("fail to deallocate GFS for a node")));

		FFAT_ASSERT(_GFS_IS_ACTIVATED(pNodeNewDes) == FFAT_TRUE);
		// reset New destination
		_GFS_NODE_HEAD(pNodeNewDes) = NULL;
	}

	if ((pNodeDes != NULL) && (_GFS_IS_ACTIVATED(pNodeDes) == FFAT_TRUE))
	{
		r = ffat_fastseek_deallocateGFSE(pNodeDes);
		FFAT_ER(r, (_T("fail to deallocate GFS for a node")));
	}

	return FFAT_OK;
}


/**
* release GFS for nodes related to rename operation
* This function is called after rename operation on success
*
* @param		pNodeSrc		: [IN] source node pointer
* @param		pNodeDes		: [IN] destination node pointer
* @param		pNodeNewDes		: [IN] new destination node pointer
*									   this node has same ADDON information with pNodeSrc
* @return		FFAT_OK			: success
* @return		negative		: fail
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo] First Writing.
* @version		AHN-06-2009 [DongYoung Seo] change function name from ffat_fastseek_rename()
*										because this is after operation for rename
*/
FFatErr
ffat_fastseek_afterRename(Node* pNodeSrc, Node* pNodeDes, Node* pNodeNewDes)
{
	return ffat_fastseek_rename(pNodeSrc, pNodeDes, pNodeNewDes);
}


/**
* release GFSE for fast seek
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_fastseek_deallocateGFSE(Node* pNode)
{
	FFatErr		r;

	if (_GFS_IS_ACTIVATED(pNode)  == FFAT_TRUE)
	{
		r = _resetFromGFS(pNode, 0);
		FFAT_ER(r, (_T("fail to reset GFS")));
	}

	return FFAT_OK;
}


/**
* get cluster number of offset
*
* *pdwCluser is an adjacent one when the dwOffset is not same is *pdwOffset,
*
* @param		pNode			: [IN] node pointer
* @param		dwOffset		: [IN] offset for the cluster
* @param		pdwCluster		: [OUT] cluster number storage
* @param		pdwOffset		: [OUT] offset of *pdwCluster
* @param		pPAL			: [OUT] previous access location
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success but not completed.
* @return		FFAT_DONE		: success and nothing to do additional work.
* @return		negative		: error
* @author		DongYoung Seo
* @version		OCT-19-2006 [DongYoung Seo] First Writing.
* @version		AUG-29-2007 [SungWoo Jo]	Add automatic GFS allocation
* @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
*/
FFatErr
ffat_fastseek_getClusterOfOffset(Node* pNode, t_uint32 dwOffset, t_uint32* pdwCluster,
								 t_uint32* pdwOffset, NodePAL* pPAL, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCluster);

	*pdwCluster	= NODE_C(pNode);
	*pdwOffset	= 0;

	// now use GFS
	r = _getClusterOfOffsetGFS(pNode, dwOffset, pdwCluster, pdwOffset, pPAL, pCxt);
	if (r < 0)
	{
		if (r != FFAT_ENOMEM)
		{
			FFAT_ASSERT(FFATFS_GetClusterOfOffset(NODE_VI(pNode), NODE_C(pNode), dwOffset, pdwCluster, pCxt) == FFAT_OK);
			FFAT_ASSERT(FFATFS_IS_EOF(NODE_VI(pNode), *pdwCluster) == FFAT_TRUE);

			*pdwCluster	= NODE_C(pNode);
			*pdwOffset	= 0;
		}

		// we do not care any error - this is add-on module
		//	FFATFS will return Cluster Of Offset
		r = FFAT_OK;
	}

	return r;
}


/**
 * get cluster information.
 *
 * vector 형태로 cluster의 정보를 구한다.
 * [en] cluster information of vector type can be got. 
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 * [en] much cluster information can be got one time by using less memory.
 *
 * 주의 : pVC의 내용은 초기화 하지 않는다.
 * [en] Attention : contents of pVC do not initialize. 
 * 정보를 추가해서 저장한다.
 * [en] it stores information adding. 
 * 단, dwOffset을 사용할때 pVC는 초기화되어 있어야 한다
 * [en] merely, pVC must be initialized when it uses dwOffset. 
 * 
 * @param		pNode			: [IN] node pointer
 * @param		dwCount			: [IN] cluster count
 * @param		pVC				: [OUT] vectored cluster information
 * @param		dwOffset		: [IN] start offset (if *pdwNewCluster is 0, this is used. if not, ignored)
 * @param		pdwNewCluster	: [IN] if *pdwNewCluster is 0, dwOffset must be valid
 *								  [OUT]	new start cluster after operation
 * @param		pdwNewCount		: [OUT] new cluster count after operation
 * @param		pPAL			: [IN/OUT] previous access location. may be NULL,
 *									 NULL : do not gather continuous clusters
 *									 NULL : gather continuous clusters
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: partial success or FASTSEEK does not have cluster information
 * @return		FFAT_DONE		: requested operation is done.
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		OCT-20-2006 [DongYoung Seo] First Writing.
 * @version		OCT-14-2008 [GwangOk Go]	add parameter dwOffset
 * @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
 */
FFatErr
ffat_fastseek_getVectoredCluster(Node* pNode, t_uint32 dwCount, FFatVC* pVC, t_uint32 dwOffset,
					t_uint32* pdwNewCluster, t_uint32* pdwNewCount, NodePAL* pPAL, ComCxt* pCxt)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwNewCluster);
	FFAT_ASSERT(pdwNewCount);

	return _getVectoredClusterGFS(pNode, dwCount, pVC, dwOffset, pdwNewCluster,
								pdwNewCount, pPAL, pCxt);
}


/**
* reset fast seek from offset
*
* @param		pNode		: [IN] node pointer
* @param		dwOffset	: [IN] reset start offset
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		OCT-23-2006 [DongYoung Seo] First Writing.
* @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
*/
FFatErr
ffat_fastseek_resetFrom(Node* pNode, t_uint32 dwOffset)
{
	FFAT_ASSERT(pNode);

	//ffat_fastseek_release(pNode);
	if (_GFS_IS_ACTIVATED(pNode) == FFAT_TRUE)
	{
		_resetFromGFS(pNode, dwOffset);
	}

	return FFAT_OK;
}


//=============================================================================
//
//	STATIC FUNCTIONS
//


/**
* get cluster number of offset
*
* *pdwCluser is an adjacent one when the dwOffset is not same is *pdwOffset,
*
* @param		pNode			: [IN] node pointer
* @param		dwOffset		: [IN] offset for the cluster
* @param		pdwCluster		: [OUT] cluster number storage
* @param		pdwOffset		: [OUT] offset of *pdwCluster
* @param		pPAL			: [OUT] previous access location
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success but not completed.
* @return		FFAT_DONE		: success and nothing to do additional work.
* @return		negative		: error
* @author		DongYoung Seo
* @version		OCT-19-2006 [DongYoung Seo] First Writing.
* @version		AUG-29-2007 [SungWoo Jo]	Add automatic GFS allocation
* @version		JAN-07-2008 [DongYoung Seo] Rewrite for new extent base GFS
* @version		DEC-12-2008 [GwangOk Go]	Add previous access location
*/
static FFatErr
_getClusterOfOffsetGFS(Node* pNode, t_uint32 dwOffset, t_uint32* pdwCluster,
						t_uint32* pdwOffset, NodePAL* pPAL, ComCxt* pCxt)
{
	_GFSEntry*		pEntry;
	t_int16			wIndex;
	t_uint32		dwEntryOffset;
	FFatErr			r;
	t_boolean		bGetContiguous;

	FFAT_DEBUG_GFS_PRINTF((_T("Node/Offset:0x%X/%d\n"), (t_uint32)pNode, dwOffset));

	if (_GFS_IS_ACTIVATED(pNode) == FFAT_FALSE)
	{
		r = _getFreeGFSNodeHead(pNode);
		if (r != FFAT_OK)
		{
			FFAT_LOG_PRINTF((_T("Fail to get free GFS Node Head - Not enough memory")));
			return FFAT_OK;
		}
	}

	if (pPAL)
	{
		bGetContiguous = FFAT_TRUE;
	}
	else
	{
		bGetContiguous = FFAT_FALSE;
	}

	r = _getGFSE(pNode, dwOffset, 1, &pEntry, FFAT_TRUE, _stGFSM.dwReadAhead, bGetContiguous, pCxt);
	if (r == FFAT_ENOENT)
	{
		// we got adjacent entry
		r = _getGFSE(pNode, dwOffset, 1, &pEntry, FFAT_FALSE, _stGFSM.dwReadAhead, bGetContiguous, pCxt);
		FFAT_ER(r, (_T("fail to get GFSE")));

		*pdwCluster = _GFSE_LC(pEntry);
		*pdwOffset	= _GFSE_LCO(pEntry, NODE_VOL(pNode));
		return FFAT_OK;
	}
	else if (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to get GFSE")));
		FFAT_ASSERT(r == FFAT_ENOMEM ? FFAT_TRUE : FFAT_FALSE);
		return r;
	}

	FFAT_ASSERT(pEntry->dwOffset <= dwOffset);
	FFAT_ASSERT(_GFSE_LO(pEntry, NODE_VOL(pNode)) >= dwOffset);

	// get forward cluster count
	FFAT_ASSERT((((dwOffset & (~VOL_CSM(NODE_VOL(pNode)))) - pEntry->dwOffset) >> VOL_CSB(NODE_VOL(pNode))) <= _GFSE_CC(pEntry));

	r = _getIndexOfOffsetGFS(NODE_VOL(pNode), pEntry, dwOffset, &wIndex, &dwEntryOffset);
	FFAT_ASSERT(r == FFAT_OK);

	*pdwCluster = pEntry->pVCE[wIndex].dwCluster 
						+ ((dwOffset - dwEntryOffset) >> VOL_CSB(NODE_VOL(pNode)));

	// set cluster offset
	*pdwOffset	= dwOffset;

	if (pPAL)
	{
		// store previous access location
		pPAL->dwOffset	= dwEntryOffset;
		pPAL->dwCluster	= pEntry->pVCE[wIndex].dwCluster;
		pPAL->dwCount	= pEntry->pVCE[wIndex].dwCount;
	}

	FFAT_DEBUG_GFS_PRINTF((_T("Node/Offset/ClusterOfOffset:0x%X/%d/%d\n"), (t_uint32)pNode, dwOffset, *pdwCluster));

	return FFAT_DONE;
}


/**
* get entry that has the offset
*
* @param		pNode			: [IN] node pointer
* @param		dwOffset		: [IN] offset for the cluster
* @param		dwClusterCount	: [IN] Required cluster count
* @param		ppEntry			: [OUT]pointer of an entry storage that is the offset
* @param		bBuild			: [IN] flag to build FSC for not
* @param		dwReadAhead		: [IN] cluster count of read ahead
* @param		bGetContiguous	: [IN] FFAT_TRUE : get more clusters if they are continuous
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: Success
* @return		NULL			: there is no entry for the offset
* @author		DongYoung Seo
* @version		JAN-07-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_getGFSE(Node* pNode, t_uint32 dwOffset, t_uint32 dwClusterCount, _GFSEntry** ppEntry,
			t_boolean bBuild, t_uint32 dwReadAhead, t_boolean bGetContiguous, ComCxt* pCxt)
{
	_GFSEntry*		pEntry;
	_GFSEntry*		pPrevEntry = NULL;
	t_uint32		dwLastOffset;

	FFAT_ASSERT(_GFS_IS_ACTIVATED(pNode) == FFAT_TRUE);

	if (ESS_DLIST_IS_EMPTY(_GFS_GET_NODE_LIST_HEAD(pNode)) == ESS_TRUE)
	{
		if (bBuild == FFAT_TRUE)
		{
			_STATISTICS_GFS_MISS
			_HEURISTIC_GFS(FFAT_FALSE);
			return _buildGFS(pNode, dwOffset, dwClusterCount, NULL, ppEntry,
							dwReadAhead, bGetContiguous, pCxt);
		}
		else
		{
			*ppEntry = NULL;
			return FFAT_ENOENT;
		}
	}

	ESS_DLIST_FOR_EACH_ENTRY(_GFSEntry, pEntry, _GFS_GET_NODE_LIST_HEAD(pNode), stDListNode)
	{
		dwLastOffset = _GFSE_LO(pEntry, NODE_VOL(pNode));

		if ((dwLastOffset >= dwOffset) && (pEntry->dwOffset <= dwOffset))
		{
			_STATISTICS_GFS_HIT

			_HEURISTIC_GFS(FFAT_TRUE);

			_GFS_MOVE_TO_LRU_HEAD(pEntry);		// move to LRU head

			*ppEntry = pEntry;
			return FFAT_OK;
		}
		else if (dwOffset < pEntry->dwOffset)
		{
			// get previous entry and stop
			break;
		}

		pPrevEntry = pEntry;
	}

	pEntry = pPrevEntry;

	_STATISTICS_GFS_MISS
	_HEURISTIC_GFS(FFAT_FALSE);

	if (bBuild == FFAT_TRUE)
	{
		*ppEntry = NULL;
		return _buildGFS(pNode, dwOffset, dwClusterCount, pEntry, ppEntry,
						dwReadAhead, bGetContiguous, pCxt);
	}
	else
	{
		*ppEntry = pPrevEntry;		// return adjacent entry
		return FFAT_OK;
	}
}


/**
* build GFSEntry and set entry that has the offset
* 
* do not add all cluster chain to the GFS.
* just build a GFSE that includes dwOffset
*
* @param		pNode			: [IN] node pointer
* @param		dwOffset		: [IN] offset for the cluster
* @param		dwRequestCount	: [IN] Required cluster count
* @param		pCurEntry		: [IN] adjacent GFSE,
*									   may be NULL when there is no GFSE for the node
* @param		ppEntry			: [OUT]target entry storage
* @param		dwReadAhead		: [IN] read ahead cluster count
* @param		bGetContiguous	: [IN] if this flag is TRUE, get last VCE to contiguous cluster
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @return		FFAT_ENOENT		: there is no entry for the offset
* @return		else			: error
* @author		DongYoung Seo
* @version		JAN-07-2008 [DongYoung Seo] First Writing.
* @version		DEC-16-2008 [GwangOk Go]	Add bGetContiguous flag
* @version		FEB-20-2009 [DongYoung Seo] Modify required cluster count calculation routine
*										no need to add cluster count of current entry
* @version		APR-08-2009 [DongYoung Seo] Return FFAT_ENOENT when pNode does not have start cluster
*/
static FFatErr
_buildGFS(Node* pNode, t_uint32 dwOffset, t_uint32 dwRequestCount,
				_GFSEntry* pCurEntry, _GFSEntry** ppEntry, 
				t_uint32 dwReadAhead, t_boolean bGetContiguous, ComCxt* pCxt)
{
	Vol*			pVol;
	_GFSEntry*		pNewEntry = NULL;
	t_uint32		dwClusterCount;
	t_uint32		dwFirstCluster;
	t_uint32		dwLastOffset;
	t_uint32		dwNewOffset;			// offset of new entry
	t_int32			dwValidEntryCount;		// valid entry count
	FFatVC			stVC;
	t_int32			i;						// index for pCurEntry
	t_int32			dwPassThroughCount;		// GFSE pass through count
	t_uint32		dwPTCC;					// pass through cluster count
	FFatErr			r = FFAT_OK;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(ppEntry);
	FFAT_ASSERT(dwRequestCount);

	pVol = NODE_VOL(pNode);

	FFAT_DEBUG_GFS_PRINTF((_T("Enter, Node/offset/count/ReadAhead:0x%X/%d/%d/%d"), (t_uint32)pNode, dwOffset, dwRequestCount, dwReadAhead));

// debug begin
#ifdef FFAT_DEBUG
	if (pCurEntry)
	{
		// check next entry validity
		if (_GFSE_IS_LAST(pCurEntry, pNode) == FFAT_FALSE)
		{
			_GFSEntry* pNext;
			pNext = _GFSE_GET_NEXT(pCurEntry);
			FFAT_ASSERT(pNext->dwOffset > dwOffset);
		}

		FFAT_ASSERT(dwOffset >= pCurEntry->dwOffset);
	}
#endif
// debug end

	// get forward cluster count
	if (pCurEntry)
	{
		_CHECK_GFSE(pNode, pCurEntry, pCxt);
		FFAT_ASSERT(dwOffset >= pCurEntry->dwOffset);

		dwClusterCount = ESS_MATH_CDB((dwOffset - pCurEntry->dwOffset + 1), VOL_CS(pVol), VOL_CSB(pVol));
	}
	else
	{
		dwClusterCount = ESS_MATH_CDB((dwOffset + 1), VOL_CS(pVol), VOL_CSB(pVol));
	}

	// adjust count
	if (dwRequestCount > dwReadAhead)
	{
		dwClusterCount += dwRequestCount;	// add request cluster count
	}
	else
	{
		dwClusterCount += dwReadAhead;		// add read ahead clusters
	}

	FFAT_ASSERT(dwClusterCount > 0);

	FFAT_ASSERT(FFAT_ALLOC_BUFF_SIZE >= (_GFS_VPE * sizeof(FFatVCE)));

	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE);

	// initializes stVC
	VC_INIT(&stVC, VC_NO_OFFSET);
	stVC.dwTotalClusterCount	= 0;
	// align to _GFS_VPE
	stVC.dwTotalEntryCount		= (FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE)) & (~_GFS_VPE_MASK);

	if (pCurEntry)
	{
		stVC.dwClusterOffset		= pCurEntry->dwOffset;
		stVC.dwValidEntryCount		= _GFSE_VEC(pCurEntry);
		stVC.dwTotalClusterCount	= _GFSE_CC(pCurEntry);

		// COPY Valid entry to stVC
		FFAT_MEMCPY(stVC.pVCE, pCurEntry->pVCE, (_GFSE_VEC(pCurEntry) * sizeof(FFatVCE)));

		FFAT_ASSERT(stVC.dwTotalEntryCount >= _GFS_VPE);

		r = FFATFS_GetNextCluster(VOL_VI(pVol), _GFSE_LC(pCurEntry), &dwFirstCluster, pCxt);
		FFAT_EO(r, (_T("fail to get next cluster")));

		if (FFATFS_IS_EOF(VOL_VI(pVol), dwFirstCluster) == FFAT_TRUE)
		{
			*ppEntry = pCurEntry;
			r = FFAT_ENOENT;
			goto out;
		}

		FFAT_ASSERT(dwClusterCount > _GFSE_CC(pCurEntry));		// just for check

		// get vectored cluster
		r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwFirstCluster,
						(dwClusterCount - _GFSE_CC(pCurEntry)), &stVC, bGetContiguous, pCxt);
		FFAT_EO(r, (_T("fail to get vectored cluster")));

		FFAT_ASSERT(VC_CC(&stVC) > 0);
		FFAT_ASSERT(VC_VEC(&stVC) > 0);

		i = _GFSE_LEI(pCurEntry);

		FFAT_ASSERT(pCurEntry->pVCE[i].dwCluster == stVC.pVCE[i].dwCluster);

		if (pCurEntry->pVCE[i].dwCount != stVC.pVCE[i].dwCount)
		{
			// increase total cluster count
			pCurEntry->dwTotalClusterCount += stVC.pVCE[i].dwCount - pCurEntry->pVCE[i].dwCount;

			// set cluster count of last entry 
			pCurEntry->pVCE[i].dwCount = stVC.pVCE[i].dwCount;

			FFAT_DEBUG_GFS_PRINTF((_T("UpdateEntry, Entry/EntryOffset/Index/Cluster/Count:0x%X/%d/%d/%d/%d"), (t_uint32)pCurEntry, pCurEntry->dwOffset, i, pCurEntry->pVCE[i].dwCluster, pCurEntry->pVCE[i].dwCount));
		}

		i++;

		// add new clusters
		// 1. fill current entry
		for (/* nothing */; i < _GFS_VPE; i++)
		{
			if (i >= VC_VEC(&stVC))
			{
				// do we get the offset ?
				dwLastOffset = _GFSE_LO(pCurEntry, pVol);
				if (dwLastOffset >= dwOffset)
				{
					// cool !! -- hit. let's go out
					*ppEntry = pCurEntry;
					r = FFAT_OK;
				}
				else
				{
					r = FFAT_ENOENT;
				}

				_CHECK_GFSE(pNode, pCurEntry, pCxt);
				goto out;
			}

			pCurEntry->pVCE[i].dwCluster	= stVC.pVCE[i].dwCluster;
			pCurEntry->pVCE[i].dwCount		= stVC.pVCE[i].dwCount;

			pCurEntry->dwTotalClusterCount	+= pCurEntry->pVCE[i].dwCount;
			pCurEntry->wValidEntryCount++;

			FFAT_DEBUG_GFS_PRINTF((_T("AddEntry, Entry/EntryOffset/Index/Cluster/Count:0x%X/%d/%d/%d/%d"), (t_uint32)pCurEntry, pCurEntry->dwOffset, i, pCurEntry->pVCE[i].dwCluster, pCurEntry->pVCE[i].dwCount));
		}

		// do we get the offset ?
		dwLastOffset = _GFSE_LO(pCurEntry, pVol);
		if (dwLastOffset >= dwOffset)
		{
			// cool !! -- hit. let's go out
			*ppEntry = pCurEntry;
			r = FFAT_OK;

			_CHECK_GFSE(pNode, pCurEntry, pCxt);
			goto out;
		}

		dwNewOffset = dwLastOffset + 1;	// get next offset

		i = _GFS_VPE;					// set index to the next entry
	}
	else
	{
		if (NODE_C(pNode) == 0)
		{
			r = FFAT_ENOENT;
			goto out;
		}

		stVC.dwClusterOffset	= 0;
		stVC.dwValidEntryCount	= 0;
		dwNewOffset				= 0;

		// get vectored cluster
		r = FFATFS_GetVectoredCluster(VOL_VI(pVol), NODE_C(pNode), dwClusterCount, &stVC,
						bGetContiguous, pCxt);
		FFAT_EO(r, (_T("fail to get vectored cluster")));

		i = 0;			// set index to first entry
	}

	FFAT_ASSERT((dwNewOffset & VOL_CSM(pVol)) == 0);

	dwPassThroughCount	= 0;			// set pass through count.
										// some entry will be added to ....  

	// we does not get the offset in the first chunk.
	do
	{
		if (i >= VC_VEC(&stVC))
		{
			// no more data, re-read clusters
			r = FFATFS_GetNextCluster(VOL_VI(pVol), VC_LC(&stVC), &dwFirstCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));

			if (FFATFS_IS_EOF(VOL_VI(pVol), dwFirstCluster) == FFAT_TRUE)
			{
				r = FFAT_ENOENT;
				goto out;
			}

			// decrease request cluster count
			dwClusterCount -= VC_CC(&stVC);

			// reset stVC
			stVC.dwClusterOffset		= dwNewOffset;
			stVC.dwValidEntryCount		= 0;			// reset valid entry count
			stVC.dwTotalClusterCount	= 0;

			// get vectored cluster
			r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwFirstCluster, dwClusterCount,
										&stVC, bGetContiguous, pCxt);
			FFAT_EO(r, (_T("fail to get vectored cluster")));

			i = 0;
		}

		_getChunkGFS(&stVC, i, &dwValidEntryCount, &dwPTCC);
		
		// To prevent the overflow, edit calculation instead of plus. [JW.Park : 2009-06-15]
		// if ((dwNewOffset + (dwPTCC << VOL_CSB(pVol))) > dwOffset) =>
		if (dwPTCC > ((dwOffset - dwNewOffset) >> VOL_CSB(pVol)))
		{
			// we got it
			break;
		}

		if (dwPassThroughCount++ < _stGFSM.wPassThrough)
		{
			pCurEntry = _addNewGFSE(pNode, &stVC, i, dwValidEntryCount, dwNewOffset,
									dwPTCC, pCurEntry, pCxt);
			if (pCurEntry == NULL)
			{
				r = FFAT_ENOMEM;
				goto out;
			}
			FFAT_ASSERT(_GFSE_CC(pCurEntry) == dwPTCC);
		}

		dwNewOffset += (dwPTCC << VOL_CSB(pVol));
		i += dwValidEntryCount;
	} while(1);

	// ok, we got the entry in stVC
	pNewEntry = _addNewGFSE(pNode, &stVC, i, dwValidEntryCount, dwNewOffset,
						dwPTCC, pCurEntry, pCxt);
	if (pNewEntry == NULL)
	{
		r = FFAT_ENOMEM;
		goto out;
	}

	*ppEntry = pNewEntry;

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	if (r < 0)
	{
		if (pNewEntry != NULL)
		{
			// move entry to free list
			_GFS_MOVE_TO_FREE(pNewEntry);
		}
	}
// debug begin
	else
	{
		_CHECK_GFSE(pNode, pCurEntry, pCxt);
		_CHECK_GFSE(pNode, pNewEntry, pCxt);
	}
// debug end

	return r;
}


/**
* allocate and add to list
*
* @param		pNode			: [IN] node pointer
* @param		pEntryPrev		: [IN] previous entry
*								  NULL : there is no previous entry
* @return		not NULL		: pointer of an entry that is the offset
* @return		NULL			: there is no free node head
*								  == FFAT_ENOMEM
* @author		DongYoung Seo
* @version		JAN-07-2008 [DongYoung Seo] First Writing.
*/
static _GFSEntry*
_allocGFSE(Node* pNode, _GFSEntry* pEntryPrev)
{
	_GFSEntry*		pEntry;		// LRU entry
	EssDList*		pDList;		// a DLIST
	FFatErr			r;

	FFAT_ASSERT(pNode);

	if (ESS_DLIST_IS_EMPTY(&_stGFSM.dlGFSEFree) == FFAT_FALSE)
	{
		// get a GFSE from free list
		pDList = ESS_DLIST_GET_TAIL(&_stGFSM.dlGFSEFree);
		FFAT_DEBUG_GFS_PRINTF((_T("get new entry from free list:0x%X\n"), (t_uint32)ESS_GET_ENTRY(pDList, _GFSEntry, dlLru)));
	}
	else
	{
		FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&_stGFSM.dlGFSELru) == FFAT_FALSE);

		// get a GFSE from LRU list
		pDList = ESS_DLIST_GET_TAIL(&_stGFSM.dlGFSELru);

		FFAT_DEBUG_GFS_PRINTF((_T("get new entry from LRU list:0x%X\n"), (t_uint32)ESS_GET_ENTRY(pDList, _GFSEntry, dlLru)));
		_STATISTICS_GFS_REPLACE
	}

	pEntry = ESS_GET_ENTRY(pDList, _GFSEntry, dlLru);
	_GFS_MOVE_TO_LRU_HEAD(pEntry);		// move to LRU list

	if (pEntryPrev == NULL)
	{
		if (_GFS_IS_ACTIVATED(pNode) == FFAT_FALSE)
		{
			r = _getFreeGFSNodeHead(pNode);
			if (r != FFAT_OK)
			{
				FFAT_LOG_PRINTF((_T("Fail to get free GFS Node Head")));
				return NULL;
			}
		}

		ESS_DLIST_DEL_INIT(&pEntry->stDListNode);

		// insert new entry
		ESS_DLIST_ADD_HEAD(_GFS_GET_NODE_LIST_HEAD(pNode), &pEntry->stDListNode);
		FFAT_DEBUG_GFS_PRINTF((_T("add new entry at the head, Node/Entry:0x%X/0x%X\n"), (t_uint32)pNode, (t_uint32)pEntry));
	}
	else if (pEntryPrev != pEntry)
	{
		FFAT_ASSERT(pEntryPrev);

		ESS_DLIST_DEL_INIT(&pEntry->stDListNode);

		// insert new entry
		ESS_DLIST_ADD(&pEntryPrev->stDListNode, pEntryPrev->stDListNode.pNext, &pEntry->stDListNode);
		FFAT_DEBUG_GFS_PRINTF((_T("add new entry middle of list, Node/Entry:0x%X/0x%X\n"), (t_uint32)pNode, (t_uint32)pEntry));
	}
	else
	{
		// no need to move entry
	}

	FFAT_DEBUG_GFS_PRINTF((_T("a victim entry GFSE(0x%x) FirstCluster/Offset/CC:%d/%d/%d \n"), (t_uint32)pEntry, pEntry->pVCE[0].dwCluster, pEntry->dwOffset, pEntry->dwTotalClusterCount));

	// initialize entry
	pEntry->dwOffset			= 0;
	pEntry->dwTotalClusterCount	= 0;
	pEntry->wValidEntryCount	= 0;

	// set GFS Flag
	NODE_ADDON(pNode)->dwFlag |= ADDON_NODE_GFS;

	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&pEntry->stDListNode) == FFAT_FALSE);

	return pEntry;
}


/**
* reset global fast seek entries from dwOffset
*
* @param		pNode		: node pointer
* @param		dwOffset	: reset start offset
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		JAN-08-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_resetFromGFS(Node* pNode, t_uint32 dwOffset)
{
	_GFSEntry*	pEntry;			// current entry
	_GFSEntry*	pEntryNext;		// next entry
	t_uint32	dwClusterCount;	// cluster count
	t_uint32	dwEntryOffset;	// offset of entry
	t_int16		wIndex;
	t_int16		i;
	Vol*		pVol;
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(_GFS_NODE_HEAD(pNode) != NULL);

	pVol = NODE_VOL(pNode);

	FFAT_DEBUG_GFS_PRINTF((_T("Node/Offset:0x$X/%d/%d\n"), (t_uint32)pNode, dwOffset));

	ESS_DLIST_FOR_EACH_ENTRY_SAFE(_GFSEntry, pEntry, pEntryNext, _GFS_GET_NODE_LIST_HEAD(pNode), stDListNode)
	{
		if (pEntry->dwOffset >= dwOffset)
		{
			_GFS_MOVE_TO_FREE(pEntry);

			FFAT_DEBUG_GFS_PRINTF((_T("An GFSE(0x%X) is released, (added free list) Node/FirstCluster/Offset/CC:%d/%d/%d/%d \n"), (t_uint32)pEntry, (t_uint32)pNode, pEntry->pVCE[0].dwCluster, pEntry->dwOffset, pEntry->dwTotalClusterCount));
		}
		else
		{
			if (_GFSE_LCO(pEntry, pVol) < dwOffset)
			{
				continue;
			}

			// reset partial entry
			dwClusterCount	= (dwOffset - pEntry->dwOffset) >> VOL_CSB(pVol);

			r = _getIndexOfOffsetGFS(pVol, pEntry, dwOffset, &wIndex, &dwEntryOffset);
			if (r < 0)
			{
				FFAT_ASSERT(r == FFAT_ENOENT);
				continue;
			}

			dwClusterCount	= pEntry->pVCE[wIndex].dwCount
								- ((dwOffset - dwEntryOffset) >> VOL_CSB(pVol));
			FFAT_ASSERT(dwClusterCount > 0);

			pEntry->pVCE[wIndex].dwCount	-= dwClusterCount;
			pEntry->dwTotalClusterCount		-= dwClusterCount;

			for (i = (wIndex + 1); i < _GFSE_VEC(pEntry); i++)
			{
				pEntry->dwTotalClusterCount -= pEntry->pVCE[i].dwCount;
			}

			if (pEntry->pVCE[wIndex].dwCount == 0)
			{
				pEntry->wValidEntryCount = wIndex;
			}
			else
			{
				pEntry->wValidEntryCount = wIndex + 1;
			}

			if (_GFSE_CC(pEntry) == 0)
			{
				FFAT_ASSERT(pEntry->pVCE[0].dwCount == 0);
				_GFS_MOVE_TO_FREE(pEntry);
			}
		}
	}

	if (ESS_DLIST_IS_EMPTY(_GFS_GET_NODE_LIST_HEAD(pNode)) == ESS_TRUE)
	{
		NODE_ADDON(pNode)->dwFlag &= ~ADDON_NODE_GFS;
		_releaseGFSNodeHead(pNode);
	}

	return FFAT_OK;
}

/**
 * get cluster information from GFS
 *
 * vector 형태로 cluster의 정보를 구한다.
 * [en] cluster information of vector type can be got. 
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 * [en] much cluster information can be got one time by using less memory.
 *
 * 주의 : * pVC의 내용은 초기화 하지 않는다.
 * [en] Attention : contents of pVC do not initialize. 
 * 정보를 추가해서 저장한다.
 * [en] it stores information adding. 
 * 
 * @param		pNode			: [IN] node pointer
 * @param		dwCount			: [IN] cluster count
 *										0 : get clusters to end of the chain
 * @param		pVC				: [OUT]vectored cluster information
 * @param		dwStartOffset	: [IN] start offset (if *pdwNewCluster is 0, this is used. if not, ignored)
 * @param		pdwNewCluster	: [IN] if *pdwNewCluster is 0, dwOffset must be valid
 *										start offset is stored at pVC->dwClusterOffset
 *								  [OUT]	new start cluster after operation
 * @param		pdwNewCount		: [OUT] new cluster count after operation
 * @param		pPAL			: [IN/OUT] previous access location. may be NULL,
 *									 NULL : do not gather continuous clusters
 *									 NULL : gather continuous clusters
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: partial success or FASTSEEK does not have cluster information
 * @return		FFAT_DONE		: requested operation is done.
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		JAN-08-2008 [DongYoung Seo] First Writing.
 * @version		OCT-14-2008 [GwangOk Go]	Add parameter dwStartOffset
 * @version		JAN-12-2009 [GwangOk Go]	Add previous access location
 * @version		JAN-29-2009 [DongYoung Seo] Bug fix, CQID:FLASH00019938
 *											Add checking code for directory. directory does not have size
 * @version		MAY-26-2009 [JeongWoo Park] Add the code to check overflow of dwOffset
 */
static FFatErr
_getVectoredClusterGFS(Node* pNode, t_uint32 dwCount, FFatVC* pVC, t_uint32 dwStartOffset,
						t_uint32* pdwNewCluster, t_uint32* pdwNewCount, NodePAL* pPAL, ComCxt* pCxt)
{
	Vol*			pVol;
	t_uint32		dwOffset;
	_GFSEntry*		pEntry;
	t_int16			wIndex;				// entry index
	t_uint16		wIndexVC;			// index for VC
	t_uint32		dwEntryOffset;		// offset of an entry
	t_uint32		dwCurCluster;		// current cluster number
	t_uint32		dwCurCount;			// current cluster count
	t_uint32		dwClusterOffset;	// offset of cluster in an entry
	t_uint32		dwRestCount;		// rest cluster count
	FFatErr			r;
	t_boolean		bGetContiguous;		// boolean to get continuous clusters or not

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwNewCluster);
	FFAT_ASSERT(pdwNewCount);

	pVol = NODE_VOL(pNode);

	FFAT_DEBUG_GFS_PRINTF((_T("Node/Count/StartOffset/*pdwNewCluster/pVC->dwClusterOffset:0x%X/%d/%d/%d/%d\n"), (t_uint32)pNode, dwCount, dwStartOffset, *pdwNewCluster, pVC->dwClusterOffset));

	if (*pdwNewCluster != 0)
	{
		// check offset is valid
		if (pVC->dwClusterOffset & VOL_CSM(pVol))
		{
			FFAT_DEBUG_GFS_PRINTF((_T("there is no valid cluster offset")));
			// there is no valid cluster offset,
			// we can not get clusters
			FFAT_ASSERT((pVC->dwClusterOffset == VC_NO_OFFSET) ? FFAT_TRUE : FFAT_FALSE);
			return FFAT_OK;
		}

		if (VC_CC(pVC) > 0)
		{
			dwOffset = pVC->dwClusterOffset + (VC_CC(pVC) << VOL_CSB(pVol));
			wIndexVC = (t_uint16)VC_LEI(pVC);

			// check offset is over file size or overflow(4GB)
			if ((NODE_IS_FILE(pNode) == FFAT_TRUE) &&
				((dwOffset >= NODE_S(pNode)) || (dwOffset == 0)))
			{
				*pdwNewCount = 0;
				return FFAT_DONE;
			}
		}
		else
		{
			dwOffset = pVC->dwClusterOffset;
			wIndexVC = 0;
		}
	}
	else
	{
		FFAT_ASSERT(dwStartOffset != FFAT_NO_OFFSET);

		dwOffset = dwStartOffset & (~VOL_CSM(pVol));
		wIndexVC = 0;
	}

	// adjust dwCount
	if (dwCount == 0)
	{
		// get cluster to the end of chain, set it to the maximum number
		dwCount = VOL_CC(pVol);
	}

	dwRestCount = dwCount;

	// check current node has GFS Head.
	if (_GFS_IS_ACTIVATED(pNode) == FFAT_FALSE)
	{
		r = _getFreeGFSNodeHead(pNode);
		if (r != FFAT_OK)
		{
			FFAT_LOG_PRINTF((_T("Fail to get free GFS Node Head - Not enough memory\n")));
			return FFAT_OK;
		}
	}

	if (pPAL)
	{
		bGetContiguous = FFAT_TRUE;
	}
	else
	{
		bGetContiguous = FFAT_FALSE;
	}

	do
	{
		r = _getGFSE(pNode, dwOffset, dwRestCount, &pEntry, FFAT_TRUE,
					_stGFSM.dwReadAhead, bGetContiguous, pCxt);
		if (r < 0)
		{
			if ((r == FFAT_ENOENT) || (r == FFAT_ENOMEM))
			{
				FFAT_PRINT_VERBOSE((_T("there is no cluster for the offset or no entry\n")));
				return FFAT_OK;
			}
			else
			{
				FFAT_PRINT_VERBOSE((_T("fail to get GFSEntry\n")));
				return r;
			}
		}

		r = _getIndexOfOffsetGFS(pVol, pEntry, dwOffset, &wIndex, &dwEntryOffset);
		FFAT_EO(r, (_T("fail to get index of offset")));

		for (/*None*/; wIndex < _GFSE_VEC(pEntry); wIndex++)
		{
			if (dwOffset > dwEntryOffset)
			{
				dwClusterOffset	= ((dwOffset - dwEntryOffset) >> VOL_CSB(pVol));

				FFAT_ASSERT(pEntry->pVCE[wIndex].dwCount > dwClusterOffset);

				dwCurCluster	= pEntry->pVCE[wIndex].dwCluster + dwClusterOffset;
				dwCurCount		= pEntry->pVCE[wIndex].dwCount - dwClusterOffset;
			}
			else
			{
				dwCurCluster	= pEntry->pVCE[wIndex].dwCluster;
				dwCurCount		= pEntry->pVCE[wIndex].dwCount;
			}

			FFAT_DEBUG_GFS_PRINTF((_T("dwCurCluster/dwCurCount/:%d/%d\n"), dwCurCluster, dwCurCount));

			// adjust current cluster count
			// check request cluster count
			if (dwCurCount > dwRestCount)
			{
				dwCurCount = dwRestCount;
			}

			if (VC_IS_EMPTY(pVC) == FFAT_TRUE)
			{
				pVC->pVCE[wIndexVC].dwCluster	= dwCurCluster;
				pVC->pVCE[wIndexVC].dwCount		= dwCurCount;
				pVC->dwValidEntryCount++;
			}
			else
			{
				if ((VC_LC(pVC) + 1) == dwCurCluster)
				{
					// add cluster count
					pVC->pVCE[wIndexVC].dwCount += dwCurCount;
				}
				else
				{
					wIndexVC++;
					if (wIndexVC >= VC_TEC(pVC))
					{
						// no more free entry at pVC
						r = FFAT_OK;
						FFAT_ASSERT(dwRestCount > 0);

						*pdwNewCluster	= dwCurCluster;
						*pdwNewCount	= dwRestCount;

						goto out;
					}

					pVC->pVCE[wIndexVC].dwCluster	= dwCurCluster;
					pVC->pVCE[wIndexVC].dwCount		= dwCurCount;

					pVC->dwValidEntryCount++;		// increase VEC
				}
			}

			pVC->dwTotalClusterCount += dwCurCount;
			dwOffset += (dwCurCount << VOL_CSB(pVol));

			if (dwRestCount != 0)
			{
				dwRestCount -= dwCurCount;

				if (dwRestCount <= 0)
				{
					FFAT_ASSERT(dwRestCount == 0);
					r = FFAT_DONE;
					*pdwNewCount = 0;
					goto out;
				}
			}

			// check offset is over file size or overflow(4GB)
			if ((NODE_IS_FILE(pNode) == FFAT_TRUE) &&
				((dwOffset >= NODE_S(pNode)) || (dwOffset == 0)))
			{
				r = FFAT_DONE;
				*pdwNewCount = 0;
				goto out;
			}

			// increase offset of entry
			dwEntryOffset += (pEntry->pVCE[wIndex].dwCount << VOL_CSB(pVol));
		}
	} while(1);

out:
	if ((r == FFAT_DONE) && (pPAL != NULL))
	{
		// store previous access location
		pPAL->dwOffset	= dwEntryOffset;
		pPAL->dwCluster	= pEntry->pVCE[wIndex].dwCluster;
		pPAL->dwCount	= pEntry->pVCE[wIndex].dwCount;
	}

	return r;
}


/**
* get index of dwOffset
*
* @param		pVol			: [IN] volume pointer
* @param		pEntry			: [IN] GFSE pointer
* @param		dwOffset		: [IN] offset of entry to lookup
* @param		pwIndex			: [OUT]index of the entry
* @param		pdwOffset		: [OUT]byte offset of the index in entry
* @return		FFAT_OK			: partial success or FASTSEEK does not have cluster information
* @author		DongYoung Seo
* @version		JAN-08-2008 [DongYoung Seo] First Writing.
* @version		JAN-06-2009 [JeongWoo Park] optimize by reducing the shift operation in loop.
*/
static FFatErr
_getIndexOfOffsetGFS(Vol* pVol, _GFSEntry* pEntry, t_uint32 dwOffset,
						t_int16* pwIndex, t_uint32* pdwOffset)
{
	t_uint32	dwClusterIndex;
	t_uint32	dwCurIndex;

	FFAT_ASSERT(pwIndex);
	FFAT_ASSERT(pdwOffset);
	FFAT_ASSERT(_GFSE_LO(pEntry, pVol) >= dwOffset);
	FFAT_ASSERT(pEntry->dwOffset <= dwOffset);
	FFAT_ASSERT(_GFSE_VEC(pEntry) > 0);

	FFAT_DEBUG_GFS_PRINTF((_T("Entry/Offset/EntryOffset/EntryClusterCount:0x%X/%d/%d/%d\n"), (t_uint32)pEntry, dwOffset, pEntry->dwOffset, pEntry->dwTotalClusterCount));

	dwClusterIndex	= (dwOffset - pEntry->dwOffset) >> VOL_CSB(pVol);

	if (dwClusterIndex <= (_GFSE_CC(pEntry) >> 1))
	{
		// forward
		dwCurIndex = 0;
		for (*pwIndex = 0; *pwIndex < _GFSE_VEC(pEntry); (*pwIndex)++)
		{
			if (dwClusterIndex < (dwCurIndex + pEntry->pVCE[*pwIndex].dwCount))
			{
				*pdwOffset = (pEntry->dwOffset + (dwCurIndex << VOL_CSB(pVol)));
				FFAT_ASSERT(*pdwOffset >= pEntry->dwOffset); // overflow can not be happened at here
				FFAT_DEBUG_GFS_PRINTF((_T("Forward-Offset/Index:%d/%d\n"), *pdwOffset, *pwIndex));
				return FFAT_OK;
			}

			dwCurIndex += pEntry->pVCE[*pwIndex].dwCount;
		}
	}
	else
	{
		// backward
		dwCurIndex = _GFSE_CC(pEntry);
		for (*pwIndex = (_GFSE_VEC(pEntry) - 1); *pwIndex >= 0; (*pwIndex)--)
		{
			dwCurIndex -= pEntry->pVCE[*pwIndex].dwCount;

			if (dwClusterIndex >= dwCurIndex)
			{
				*pdwOffset = (pEntry->dwOffset + (dwCurIndex << VOL_CSB(pVol)));
				FFAT_ASSERT(*pdwOffset >= pEntry->dwOffset); // overflow can not be happened here
				FFAT_DEBUG_GFS_PRINTF((_T("Backward-Offset/Index:%d/%d\n"), *pdwOffset, *pwIndex));
				return FFAT_OK;
			}
		}
	}

	FFAT_DEBUG_PRINTF((_T("Never reach here !!! logic error")));
	FFAT_ASSERT(0);

	return FFAT_ENOENT;
}


/**
* get a GFS chunk from pVC
* 
* @param		dwStartIndex	: [IN] start index of pVC->pVCE
* @param		pVC				: [IN] vectored cluster
* @param		pdwVEC			: [OUT]valid entry count in current chunk
* @param		pdwTCC			: [OUT]total cluster count in this chunk
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
static void
_getChunkGFS(FFatVC* pVC, t_int32 dwStartIndex, t_int32* pdwVEC, t_uint32* pdwTCC)
{
	FFAT_ASSERT(dwStartIndex < VC_VEC(pVC));
	FFAT_ASSERT((dwStartIndex & _GFS_VPE_MASK) == 0);
	FFAT_ASSERT(pdwVEC);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwTCC);

	*pdwVEC = 0;
	*pdwTCC = 0;

	do 
	{
		*pdwTCC += pVC->pVCE[dwStartIndex].dwCount;

		dwStartIndex++;
		(*pdwVEC)++;
	} while((dwStartIndex < VC_VEC(pVC)) && ((*pdwVEC) < _GFS_VPE));

	FFAT_ASSERT(*pdwTCC > 0);
	FFAT_ASSERT(*pdwVEC > 0);

	return;
}


/**
* add new chunk to GFS
* 
* @param		pNode				: [IN] start index of pVC->pVCE
* @param		pVC					: [IN] vectored cluster
* @param		dwIndex				: [OUT]valid entry count in current chunk
* @param		dwEntryCount		: [OUT]valid entry in this chunk
* @param		dwOffset			: [OUT]offset of new entry
* @param		dwTCC				: [OUT]total cluster count 
* @param		pPrevGFSE			: [OUT]previous entry, may be NULL
* @param		pCxt				: [IN] context of current operation
* @return		not NULL			: a new GFSE pointer
* @return		NULL				: there is no free node head
*										== FFAT_ENOEMEM
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
static _GFSEntry*
_addNewGFSE(Node* pNode, FFatVC* pVC, t_int32 dwIndex, t_int32 dwEntryCount,
			t_uint32 dwOffset, t_uint32 dwTCC, _GFSEntry* pPrevGFSE, ComCxt* pCxt)
{
	_GFSEntry*	pNewEntry;
	t_int32		dwEntryIndex;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT((dwIndex & _GFS_VPE_MASK) == 0);
	FFAT_ASSERT((dwIndex + dwEntryCount) <= VC_VEC(pVC));
	
	// ok, we got the entry in stVC
	pNewEntry = _allocGFSE(pNode, pPrevGFSE);	// get New Entry
	if (pNewEntry == NULL)
	{
		return NULL;
	}

	FFAT_ASSERT(pNewEntry);

	pNewEntry->dwOffset				= dwOffset;
	pNewEntry->dwTotalClusterCount	= dwTCC;
	pNewEntry->wValidEntryCount		= (t_uint16)dwEntryCount;

	dwEntryIndex = 0;

	do
	{
		FFAT_ASSERT((dwIndex <= VC_LEI(pVC)) && (dwEntryIndex < _GFS_VPE));
		pNewEntry->pVCE[dwEntryIndex ].dwCluster	= pVC->pVCE[dwIndex].dwCluster;
		pNewEntry->pVCE[dwEntryIndex].dwCount		= pVC->pVCE[dwIndex].dwCount;

		FFAT_DEBUG_GFS_PRINTF((_T("AddEntry, Entry/EntryOffset/Index/Cluster/Count:0x%X/%d/%d/%d/%d"), (t_uint32)pNewEntry, pNewEntry->dwOffset, dwEntryIndex, pNewEntry->pVCE[dwEntryIndex].dwCluster, pNewEntry->pVCE[dwEntryIndex].dwCount));

		dwIndex++;
		dwEntryIndex ++;
	} while (dwEntryIndex < _GFSE_VEC(pNewEntry));

	_CHECK_GFSE(pNode, pNewEntry, pCxt);

	FFAT_DEBUG_GFS_PRINTF((_T("Add new GFSE(0x%x) Node/FirstCluster/Offset/CC:0x%X,%d/%d/%d \n"), (t_uint32)pNewEntry, (t_uint32)pNode, pNewEntry->pVCE[0].dwCluster, pNewEntry->dwOffset, pNewEntry->dwTotalClusterCount));

	return pNewEntry;
}


/**
* Get a free GFS Node Head
* 
* @param		pNode			: Node pointer
*
* @return		NULL			: there is no free entry
* @return		not NULL		: a free GFS Node Head
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo] First Writing.
* @version		MAY-27-2008 [DongYoung Seo] Add code for dynamic memory allocation
*/
static FFatErr
_getFreeGFSNodeHead(Node* pNode)
{
	EssDList*		pList;

#ifdef FFAT_DYNAMIC_ALLOC
	FFatErr			r;

	if (ESS_DLIST_IS_EMPTY(&_stGFSM.dlFreeNodeHead) == FFAT_TRUE)
	{
		r = _allocGFSNHStorageDynamic();
		FFAT_ER(r, (_T("fail to allocate GFSNHStorage")));
	}
#endif

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(_GFS_NODE_HEAD(pNode) == NULL);

	IF_UK (ESS_DLIST_IS_EMPTY(&_stGFSM.dlFreeNodeHead) == FFAT_TRUE)
	{
		// It can not be happened,
		// but we must control this situation by any bug in Release version
		FFAT_ASSERT(0);
		return FFAT_ENOMEM;
	}

	pList = ESS_DLIST_GET_NEXT(&_stGFSM.dlFreeNodeHead);
	ESS_DLIST_DEL_INIT(pList);

	_GFS_NODE_HEAD(pNode) = ESS_GET_ENTRY(pList, GFSNodeHead, stDListNode);

	FFAT_DEBUG_GFS_PRINTF((_T("Get a free node head:0x%X, get count:%d\n"), (t_uint32)_GFS_NODE_HEAD(pNode), _STATISTICS_GFS_GET_GFSNH));

	return FFAT_OK;
}


/**
* release GFS Node Head
* 
* @param		pNode			: [IN] node pointer
* @author		DongYoung Seo
* @version		JAN-11-2008 [DongYoung Seo] First Writing.
*/
static void
_releaseGFSNodeHead(Node* pNode)
{
	if (_GFS_IS_ACTIVATED(pNode) == FFAT_TRUE)
	{
		FFAT_DEBUG_GFS_PRINTF((_T("Release a free node head:0x%X, release count:%d\n"), (t_uint32)_GFS_NODE_HEAD(pNode), _STATISTICS_GFS_RELEASE_GFSNH));

		_GFSN_ADD_TO_FREE(pNode);
		_GFS_NODE_HEAD(pNode)	= NULL;
	}
}


/**
* GFS Heuristic module
* 
* @param		bHit		: [IN] boolean for hit/miss
* @author		DongYoung Seo
* @version		JAN-14-2008 [DongYoung Seo] First Writing.
*/
void
_heuristicGFS(t_boolean bHit)
{
	if (bHit)
	{
		_stGFSM.wHit++;
		if ((_stGFSM.wHit > _GFS_PASS_THROUGH_HIT_TRIGGER) &&
			(_stGFSM.wPassThrough < _stGFSM.wEntryCount))
		{
			_stGFSM.wPassThrough++;
			_stGFSM.wHit = 0;
		}
	}
	else
	{
		_stGFSM.wMiss++;
		if ((_stGFSM.wMiss > _GFS_PASS_THROUGH_MISS_TRIGGER) &&
			(_stGFSM.wPassThrough > 0))
		{
			_stGFSM.wPassThrough--;
			_stGFSM.wMiss = 0;
		}
	}
}


#ifdef FFAT_DYNAMIC_ALLOC
	/**
	* initializes node head storage for dynamic memory allocation
	*
	* @return		FFAT_OK			: success
	* @return		FFAT_ENOMEM		: not enough memory
	* @author		DongYoung Seo
	* @version		MAY-28-2008 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_initGFSNHStorageDynamic(void)
	{
		ESS_LIST_INIT(&_stGFSM.slGFSNHS);
		ESS_DLIST_INIT(&_stGFSM.dlFreeNodeHead);

		return FFAT_OK;
	}
#else

	/**
	* initializes node head storage for static memory allocation
	*
	* @return		FFAT_OK			: success
	* @return		FFAT_ENOMEM		: not enough memory
	* @author		DongYoung Seo
	* @version		MAY-28-2008 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_initGFSNHStorageStatic(void)
	{
		FFatErr					r;
		t_int32					dwCount;

		dwCount = ESS_MATH_CD(FFAT_ADDON_GFS_NODE_COUNT, _NODE_HEADS_PER_STORAGE);
		FFAT_ASSERT(dwCount == 1);

		ESS_LIST_INIT(&_stGFSM.slGFSNHS);
		ESS_DLIST_INIT(&_stGFSM.dlFreeNodeHead);

		while (dwCount--)
		{
			r = _allocGFSNHStorageDynamic();
			FFAT_EO(r, (_T("fail to allocate memory for GFS Node Head Storage")));
		}

		r = FFAT_OK;

	out:
		IF_UK (r != FFAT_OK)
		{
			_terminateGFSNHStorage();
		}

		return r;
	}
#endif	// end of #ifdef FFAT_DYNAMIC_ALLOC

/**
* terminates lock storage
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_terminateGFSNHStorage(void)
{
	EssList*		pList;
	_GFSNHStorage*	pGFSNHS;			// node head storage

	while (ESS_LIST_IS_EMPTY(&_stGFSM.slGFSNHS) == FFAT_FALSE)
	{
		pList = ESS_LIST_GET_HEAD(&_stGFSM.slGFSNHS);
		FFAT_ASSERT(pList);

		ESS_LIST_REMOVE_HEAD(&_stGFSM.slGFSNHS);

		pGFSNHS = ESS_GET_ENTRY(pList, _GFSNHStorage, slList);
		FFAT_FREE(pGFSNHS, sizeof(_GFSNHStorage));
	}

	return FFAT_OK;
}


/**
* allocate new node head storage for dynamic memory allocation
*
* @return		FFAT_OK			: success
* @return		FFAT_ENOMEM		: not enough memory
* @author		DongYoung Seo
* @version		MAY-28-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_allocGFSNHStorageDynamic(void)
{
	_GFSNHStorage*	pGFSNHS;			// node head storage
	t_int32			i;

	pGFSNHS = (_GFSNHStorage*)FFAT_MALLOC(sizeof(_GFSNHStorage), ESS_MALLOC_NONE);
	IF_UK (pGFSNHS == NULL)
	{
		return FFAT_ENOMEM;
		
	}

	ESS_LIST_INIT(&pGFSNHS->slList);
	ESS_LIST_ADD_HEAD(&_stGFSM.slGFSNHS, &pGFSNHS->slList);

	for (i = 0; i < _NODE_HEADS_PER_STORAGE; i++)
	{
		ESS_DLIST_INIT(&pGFSNHS->stGFSNH[i].stDListNode);
		ESS_DLIST_ADD_HEAD(&_stGFSM.dlFreeNodeHead, &pGFSNHS->stGFSNH[i].stDListNode);
	}

	return FFAT_OK;
}


// debug begin

//=============================================================================
//
//	DEBUG PART
//


#ifdef _CHECK_ALL
	/**
	* check a GFSEntry
	* 
	* check item
	*	first entry is aligned to FFAT_ADDON_GFS_VCE_PER_ENTRY
	*	cluster number and count of each entry
	*	offset of entry
	*	total cluster count
	*
	* @param		pNode			: [IN] node pointer
	* @param		pEntry			: [IN] a GFSEntry
	* @param		pCxt			: [IN] context of current operation
	* @return		not NULL		: pointer of an entry that is the offset
	* @author		DongYoung Seo
	* @version		JAN-07-2008 [DongYoung Seo] First Writing.
	*/
	static void
	_checkGFSE(Node* pNode, _GFSEntry* pEntry, ComCxt* pCxt)
	{
		Vol*			pVol;
		t_uint32		dwCurOffset;		// current byte offset
		t_uint32		dwClusterCount;		// total cluster count
		t_int32			dwCurIndex;			// current fragment index
		t_uint32		dwCluster;			// start cluster for get vectored 
		FFatVC			stVC;
		FFatVCE			stVCE;
		t_boolean		bHit;				// there is a matched offset (== pEntry->dwOffset)
		t_int32			i;
		FFatErr			r;
		t_int32			dwFreeCount;
		t_int32			dwLRUCount;

		if (pEntry == NULL)
		{
			return;
		}

		pVol = NODE_VOL(pNode);
		dwClusterCount = 0;

		// check total count
		dwFreeCount	= EssDList_Count(&_stGFSM.dlGFSEFree);
		dwLRUCount	= EssDList_Count(&_stGFSM.dlGFSELru);

		FFAT_ASSERT((dwFreeCount + dwLRUCount) == _stGFSM.wEntryCount);

		bHit = FFAT_FALSE;

		// check total cluster count
		for (i = 0; i < _GFSE_VEC(pEntry); i++)
		{
			dwClusterCount += pEntry->pVCE[i].dwCount;
		}

		FFAT_ASSERTP(dwClusterCount == _GFSE_CC(pEntry), (_T("incorrect total cluster count on a GFSE")));

		dwCurIndex		= 0;			// cluster fragment(chunk) index
		dwCurOffset		= 0;
		dwCluster		= NODE_C(pNode);

		stVC.dwTotalEntryCount	= 1;
		stVC.pVCE				= &stVCE;

		// check align and offset
		do
		{
			VC_INIT(&stVC, VC_NO_OFFSET);
			stVC.dwTotalClusterCount	= 0;
			stVC.dwValidEntryCount		= 0;

			r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwCluster, 0, &stVC, FFAT_FALSE, pCxt);
			FFAT_ASSERTP(r == FFAT_OK, (_T("fail to get vectored cluster")));

			if (pEntry->dwOffset == dwCurOffset)
			{
				bHit = FFAT_TRUE;

				// check rest area
				for (i = 0; i < _GFSE_VEC(pEntry); i++)
				{
					// check entry
					if (i == _GFSE_LEI(pEntry))
					{
						FFAT_ASSERT(stVC.pVCE[0].dwCluster == pEntry->pVCE[i].dwCluster);
						FFAT_ASSERT(stVC.pVCE[0].dwCount >= pEntry->pVCE[i].dwCount);
						return;
					}
					else
					{
						FFAT_ASSERT(stVC.pVCE[0].dwCluster == pEntry->pVCE[i].dwCluster);
						FFAT_ASSERT(stVC.pVCE[0].dwCount == pEntry->pVCE[i].dwCount);
					}

					dwCluster = VC_LC(&stVC);
					r = FFATFS_GetNextCluster(VOL_VI(pVol), dwCluster, &dwCluster, pCxt);
					FFAT_ASSERT(r == FFAT_OK);

					if (FFATFS_IS_EOF(VOL_VI(pVol), dwCluster) == FFAT_TRUE)
					{
						break;
					}

					VC_INIT(&stVC, VC_NO_OFFSET);
					stVC.dwTotalClusterCount	= 0;
					stVC.dwValidEntryCount		= 0;

					r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwCluster, 0, &stVC, FFAT_FALSE, pCxt);
					FFAT_ASSERTP(r == FFAT_OK, (_T("fail to get vectored cluster")));
				}

				// error!!
				FFAT_ASSERTP(0, (_T("invalid cluster information")));
			}

			dwCurOffset += (VC_CC(&stVC) << VOL_CSB(pVol));

			dwCluster = VC_LC(&stVC);
			r = FFATFS_GetNextCluster(VOL_VI(pVol), dwCluster, &dwCluster, pCxt);
			FFAT_ASSERT(r == FFAT_OK);

			if (FFATFS_IS_EOF(VOL_VI(pVol), dwCluster) == FFAT_TRUE)
			{
				break;
			}

		} while (1);

		// error!!
		FFAT_ASSERTP(bHit == FFAT_TRUE, (_T("invalid cluster information - there is no match")));

		return;
	}
#endif	// #ifdef _CHECK_ALL

#ifdef FFAT_DEBUG
	static void
	_printStatistics(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======       GFS        STATICS     =======================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "GFS HIT Count: ",			_STATISTIC()->dwGFSHit));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "GFS MISS Count: ",			_STATISTIC()->dwGFSMiss));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "GFS ENTRY REPLACE Count: ",	_STATISTIC()->dwGFSReplace));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
	}
#endif

//
//	DEBUG PART END
//
//=============================================================================
// debug end

