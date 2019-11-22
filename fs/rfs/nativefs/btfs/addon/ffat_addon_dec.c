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
 * @file		ffat_addon_dec.c
 * @brief		Directory Entry Cache Modlue for FFAT
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		SEP-27-2006 [DongYoung Seo] First writing
 * @version		JAN-24-2008 [GwangOk Go]	Refactoring DEC module
 * @version		OCT-05-2008 [DongYoung Seo] remove not used function 
 *												ffat_dec_enableGDEC, ffat_dec_disableGDEC
 * @version		JAN-13-2010 [ChunUm Kong]	Modifying comment (English/Korean)
 * @see			None
 */

/* Modification done on 02/03/2012 by Utkarsh Pandey to change the condition of
 * empty DECEntry check.Earlier the check included only the comparison of 
 * DECEntry->wLfnFirstChar field.Modified the check to include the comparison 
 * of all the fields of the DECEntry structure.
 */
// directory entry cache
// It manages directory entries for fast node lookup


#include "ess_math.h"
#include "ess_bitmap.h"

#include "ffat_common.h"
#include "ffat_config.h"

#include "ffat_node.h"
#include "ffat_dir.h"
#include "ffat_vol.h"
#include "ffat_misc.h"
#include "ffatfs_api.h"

#include "ffat_addon_types_internal.h"
#include "ffat_addon_dec.h"

//======================================================================
//
//		UDEC buffer 
//
//	|-------------------|
//	|                   |
//	|   DECEntries...   |
//	|                   |
//	|-------------------|
//	|   _DecInfo        |
//	|-------------------|
//	|    DECNode        |		Node Information
//	|-------------------|
//	|    UserDEC        |		user assigned buffer address, size
//	|-------------------|		User buffer PTR
//
//
//		GDEC Buffer
//
//	|-------------------|
//	|    GDEC Extend    |		Extend Grows ↓
//	|    GDEC Extend    |
//	|                   |
//	|                   |
//	|                   |
//	|                   |
//	|                   |
//	|                   |
//	|                   |
//	|-------------------|
//	|   DECEntries...   |
//	|-------------------|
//	|   _DecInfo        |
//	|-------------------|
//	|    DECNode        |
//	|-------------------|
//	|   DECEntries  ... |
//	|-------------------|
//	|   _DecInfo        |		GDEC Nodes grows ↑
//	|-------------------|
//	|    DECNode        |		Node Information
//	|-------------------|		Global DEC buffer PTR
//
//
//	GDEC Extend structure
//
//	|-------------------|
//	|                   |
//	|   DECEntries      |
//	|                   |
//	|-------------------|
//	|   DECInfo         |
//	|-------------------|


//
//======================================================================


#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_DEC)

//#define _DEC_DEBUG
//#define _DEC_HIT_MISS

#define _COUNT_GDEC_BASE_ENTRY		FFAT_GDEC_BASE_ENTRY_COUNT
#define _SIZE_GDEC_BLOCK			(sizeof(DECEntry) * _COUNT_GDEC_BASE_ENTRY)
#define _SIZE_GDEC_BASE				(sizeof(DECNode) + sizeof(_DECInfo) + _SIZE_GDEC_BLOCK)
#define _SIZE_GDEC_EXTEND			(sizeof(_DECInfo) + _SIZE_GDEC_BLOCK)

#define _COUNT_UDEC_MIN_ENTRY		50
#define _SIZE_UDEC_BLOCK(_count)	(sizeof(DECEntry) * (_count))
#define _SIZE_MIN_UDEC				(sizeof(DECNode) + sizeof(_DECInfo) + (sizeof(DECEntry) * _COUNT_UDEC_MIN_ENTRY))

#define _LEN_NAME_LENGTH			8
#define _LEN_NAME_TYPE				1
#define _LEN_NUMERIC_TAIL			23

#define _MASK_NAME_LENGTH			0xFF000000
#define _MASK_NAME_TYPE				0x00800000
#define _MASK_NUMERIC_TAIL			0x007FFFFF
#define _MASK_ENTRY_INDEX			0xFFFF
#define _MASK_FIRST_NAME_CHAR		0xFFFF

#define _DECE_IS_LFN(_pDECE)			(((_pDECE)->dwNameLength2NT & _MASK_NAME_TYPE) ? FFAT_TRUE : FFAT_FALSE)
#define _DECE_NAME_LENGTH(_pDECE)		(((_pDECE)->dwNameLength2NT & _MASK_NAME_LENGTH) >> (_LEN_NAME_TYPE + _LEN_NUMERIC_TAIL))
#define _DECE_NUMERIC_TAIL(_pDECE)		((_pDECE)->dwNameLength2NT & _MASK_NUMERIC_TAIL)
#define _DECE_ENTRY_INDEX(_pDECE)		((_pDECE)->wEntryIndex)

// check entry is volume name entry
#define _DECE_IS_VOLUME_NAME(_pDECE)	((((_pDECE)->bSfnFirstChar == 0xFF) && ((_pDECE)->bSfnLastChar == 0xFF)) ? FFAT_TRUE : FFAT_FALSE)


#define _SET_DEC_ENTRY_NL_2NT(_pDEntry, _length, _type, _tail)	\
										((_pDEntry)->dwNameLength2NT =	\
										(((_length) << (_LEN_NAME_TYPE + _LEN_NUMERIC_TAIL)) & _MASK_NAME_LENGTH) |	\
										(((_type) << _LEN_NUMERIC_TAIL) & _MASK_NAME_TYPE) |	\
										((_tail) & _MASK_NUMERIC_TAIL))

#define _NODE_ADDON_UDEC(_pNode)				(NODE_ADDON(_pNode)->pUDEC)
#define _SET_NODE_ADDON_UDEC_PTR(_pNode, _pDEC)	_NODE_ADDON_UDEC(_pNode) = (_pDEC)

#define _SET_GLOBAL_DEC(_pNode)			NODE_ADDON_FLAG(_pNode) |= ADDON_NODE_GDEC
#define _RESET_GLOBAL_DEC(_pNode)		NODE_ADDON_FLAG(_pNode) &= (~ADDON_NODE_GDEC)
#define _SET_USER_DEC(_pNode)			NODE_ADDON_FLAG(_pNode) |= ADDON_NODE_UDEC
#define _RESET_USER_DEC(_pNode)			NODE_ADDON_FLAG(_pNode) &= (~ADDON_NODE_UDEC)

#define _IS_ACTIVATED_GDEC(_pNode)		((NODE_ADDON_FLAG(_pNode) & ADDON_NODE_GDEC) ? FFAT_TRUE : FFAT_FALSE)
#define _IS_ACTIVATED_UDEC(_pNode)		((NODE_ADDON_FLAG(_pNode) & ADDON_NODE_UDEC) ? FFAT_TRUE : FFAT_FALSE)

// check whether there is no cached entry or not.
#define _IS_EMPTY_DEC_NODE(_pDNode)		(((_pDNode)->wValidEntryCount == 0) ? FFAT_TRUE : FFAT_FALSE)

// kkaka. what is first info of node?
#define _FIRST_INFO_OF_NODE(_pDNode)		((_DECInfo*)((t_uint8*)(_pDNode) + sizeof(DECNode)))
#define _FIRST_ENTRY_OF_INFO(_pDInfo)		((DECEntry*)((t_uint8*)(_pDInfo) + sizeof(_DECInfo)))
#define _LAST_ENTRY_OF_GDEC_INFO(_pDInfo)	((DECEntry*)((t_uint8*)(_pDInfo) + _SIZE_GDEC_EXTEND - sizeof(DECEntry)))

// the last entry pointer of GDEC Info in the Extend Area
#define _LAST_EXTEND_OF_GDEC(_pDEC)			((t_uint8*)(_pDEC) + FFAT_GDEC_MEM_SIZE - _SIZE_GDEC_EXTEND)

// debug begin
#ifdef _DEC_DEBUG
	static FFatErr		_dec_checkDECNodeLastDECEntry(void);
#endif

#ifdef FFAT_DEBUG
	#define _DEC_NODE_SIG			0x45444F4E		// signature for DECNode. inverse of 'NODE'
	#define _DEC_INFO_SIG			0x4F464E49		// signature for DECInfo. inverse of 'INFO'
#endif
// debug end

typedef signed int	_AddonDECType;
enum __AddonDECType
{
	ADDON_DEC_NONE				= 0x00000000,		// none
	ADDON_DEC_GLOBAL			= 0x00000001,		// Global DEC
	ADDON_DEC_USER				= 0x00000002,		// User DEC

	ADDON_DEC_DUMMY				= 0x7FFFFFFF
};


//!< cache info for several DECEntry
typedef struct __DECInfo
{
// debug begin
#ifdef FFAT_DEBUG
	t_int32			dwSig;
#endif
// debug end
	t_int16		wValidEntryCount;	//!< valid entry count in pEntry
									//!< -1 : free DECInfo
	t_uint16	wLastEntryIndex;	//!< index of t he the last entry in current DEC chunk
									//!< _MASK_ENTRY_INDEX : 0xFFFF

	EssDList	stListDECInfo;		//!< linked to stHeadDECInfo of DECNode
} _DECInfo;


//!< The main information for DEC
typedef struct __GlobalDEC
{
	EssDList		stHeadDECNode;			//!< Head of DECNode List, managed by LRU
	t_uint8*		pBuffer;				//!< buffer for Global DEC
	t_uint8*		pLastBase;				//!< last base address (address of first DEC node in base area)
	t_uint8*		pFirstExtend;			//!< first extend address (address of last DEC info in extend area)
	t_int32			dwDECNodeCount;			//!< ???
	t_boolean		bUseGDEC;				//!< flag to use GDEC or not
											//!< kkaka. it also can be removed.
											//!<	    disable when pBuffer is NULL
} _GlobalDEC;


// static functions
static FFatErr		_gdec_init(void);
static void			_gdec_terminate(void);

static void			_initDECNode(Vol* pVol, DECNode* pDECNode);
static void			_initDECInfo(_DECInfo* pDECInfo, t_int32 dwBlockSize);

static FFatErr		_gdec_allocateGDEC(Node* pNode, DECNode** ppDECNode);
static void			_gdec_resetGDECNode(DECNode* pDECNode);

static DECNode*		_gdec_getFreeGDECNode(void);
static _DECInfo*	_gdec_expandGDECNode(DECNode** ppDECNode);
static _DECInfo*	_gdec_getFreeExtend(DECNode** ppDECNode);
static void			_gdec_setLastBase(void);
static void			_gdec_setFirstExtend(void);

static FFatErr		_lookupAndBuild(Node* pNodeParent, Node* pNodeChild, DECNode* pDECNode,
								t_int32 dwBlockEntryCount, t_wchar* psName, t_int32 dwLen,
								FFatLookupFlag dwFlag, FatGetNodeDe* pNodeDE,
								NodeNumericTail* pNumericTail, ComCxt* pCxt);
static FFatErr		_insertEntry(Node* pNodeParent, DECNode** ppDECNode, t_int32 dwBlockEntryCount,
								t_wchar* psName, t_int32 dwNameLen, t_int32 dwNamaPartLen,
								t_int32 dwSfnNameSize, t_int32 dwEntryIndex,
								FatDeSFN* pDeSFN, t_boolean bLFN, DECEntry** ppDECEntry);
static void			_storeEntry(Vol* pVol, DECEntry* pDECEntry, t_wchar* psName, t_int32 dwNameLen,
								t_int32 dwNamePartLen, t_int32 dwSfnNameSize, t_int32 dwEntryIndex,
								FatDeSFN* pDESFN, t_boolean bLFN);

static DECEntry*	_getLastDECEntry(_DECInfo* pDECInfo, t_int32 dwStartIndex);
static DECNode*		_gdec_getDECNodeByLastEntry(DECEntry* pLastDECEntry);

static FFatErr		_dec_updateFreeDe(Node* pNode, FFatLookupFlag dwFlag, t_int32 dwFreeStartIndex,
								t_int32 dwCurStartIndex, t_int32 dwEntryCount, ComCxt* pCxt);
static FFatErr		_dec_updateFreeDeLast(Node* pNode, t_int32 dwFreeStartIndex,
								t_int32 dwEntryCount, ComCxt* pCxt);
static FFatErr		_dec_updateNumericTail(Node* pNode, FFatLookupFlag dwFlag,
								NodeNumericTail* pNumericTail, DECEntry* pDECE, FatDeSFN* pDE);

static _AddonDECType	_getDECNodeAndEntryCount(Node* pNode, DECNode** ppDECNode, t_int32* pdwBlockEntryCount);
static void				_moveDECEntries(DECEntry* pDesEntry, DECEntry* pSrcEntry, t_int32 dwEntryCount);

static DECNode*		_gdec_getDECNode(Node* pNode);

static t_int32		_getNamePartLen(Vol* pVol, t_wchar* psName, t_int32 dwNameLen);
static t_int32		_getSfnNameSize(Vol* pVol, t_uint8* pName);

// static variables
static _GlobalDEC	_gGlobalDEC;

#define		FFAT_DEBUG_DEC_PRINTF(_msg)

// debug begin
#ifdef _DEC_HIT_MISS
	#define _DEC_HIT				_dwHit++;
	#define _DEC_MISS				_dwMiss++;
	#define _DEC_PRINT_HIT_MISS		FFAT_DEBUG_DEC_PRINTF(_T("[DEC] hit/miss:%d/%d\n", _dwHit, _dwMiss));
	static t_uint32					_dwHit = 0;
	static t_uint32					_dwMiss = 0;

	#undef		FFAT_DEBUG_DEC_PRINTF
	#define		FFAT_DEBUG_DEC_PRINTF(_msg)		FFAT_PRINT_VERBOSE((_T("BTFS_DEC, %s()/%d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)
#endif
// debug end


//=============================================================================
//
//	External Functions
//


/**
 * init DEC module
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 * @version		JAN-18-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_init(void)
{
	FFAT_ASSERT((_SIZE_GDEC_BLOCK % 2) == 0);

	return _gdec_init();
}


/**
 * terminate DEC module
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 * @version		JAN-18-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_terminate(void)
{
	if (_gGlobalDEC.bUseGDEC == FFAT_TRUE)
	{
		_gdec_terminate();
	}

	return FFAT_OK;
}


/**
 * mount volume for directory entry cache
 *
 * @param		pVol		: [IN] volume pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 * @version		JAN-18-2008 [GwangOk Go]	Refactoring DEC module
 * @version		MAY-15-2009 [GwangOk Go]	when mounting, reset DEC of volume
 */
FFatErr
ffat_dec_mount(Vol* pVol)
{
	FFAT_ASSERT(pVol);

	// log recovery시 mismatch될 수 있으므로 reset 해줌
	// [en] reset executes because it can be to mismatch at log recovery.
	return ffat_dec_umount(pVol);
}


/**
 * unmount volume for directory entry cache
 *
 * @param		pVol		: [IN] volume pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 * @version		JAN-18-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_umount(Vol* pVol)
{
	DECNode*	pDECNode;
	DECNode*	pDECNodeTemp;

	FFAT_ASSERT(pVol);

	if (_gGlobalDEC.bUseGDEC == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// volume에 속하는 DECNode를 reset
	// [en] DECNode including volume is reset
	ESS_DLIST_FOR_EACH_ENTRY_SAFE(DECNode, pDECNode, pDECNodeTemp, &(_gGlobalDEC.stHeadDECNode), stListDECNode)
	{
		if (pDECNode->pVol == pVol)
		{
			_gdec_resetGDECNode(pDECNode);
		}
	}

	return FFAT_OK;
}


/**
 * Init node for directory entry cache
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		SEP-27-2006 [DongYoung Seo] First Writing.
 * @version		JAN-18-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_initNode(Node* pNode)
{
	FFAT_ASSERT(pNode);

	_SET_NODE_ADDON_UDEC_PTR(pNode, NULL);		// init user DEC

	// reset DEC related flags
	NODE_ADDON(pNode)->dwFlag &= (~ADDON_NODE_DEC_MASK);

	return FFAT_OK;
}


/**
 * set user DEC
 *
 * @param		pNode		: [IN] node pointer
 * @param		pDECI		: [IN] DEC info
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 * @version		OCT-07-2008 [DongYoung Seo] move buffer size checking routine 
 *											before global DEC checking routine
 * @version		OCT-07-2008 [DongYoung Seo] remove signature related code
 * @version		OCT-08-2008 [DongYoung Seo] check byte alignment of user assigned buffer
 */
FFatErr
ffat_dec_setUDEC(Node* pNode, FFatDirEntryCacheInfo* pDECI)
{
	UserDEC*		pUserDEC;
	_DECInfo*		pDECInfo;
	AddonNode*		pAddonNode;
	t_int8*			pBuff;			// aligned buffer pointer
	t_int32			dwSize;			// buffer size after pointer alignment

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDECI);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	if (pDECI->dwSize < _SIZE_MIN_UDEC)
	{
		FFAT_LOG_PRINTF((_T("User DEC size is too small")));
		return FFAT_EINVALID;
	}

	pAddonNode = NODE_ADDON(pNode);
	if (pAddonNode->dwFlag & ADDON_NODE_GDEC)
	{
		DECNode*	pDECNode;

		pDECNode = _gdec_getDECNode(pNode);
		if (pDECNode != NULL)
		{
			_gdec_resetGDECNode(pDECNode);

			_RESET_GLOBAL_DEC(pNode);
		}
	}
	else if (pAddonNode->dwFlag & ADDON_NODE_UDEC)
	{
		FFAT_LOG_PRINTF((_T("User DEC is already allocated")));
		return FFAT_EINVALID;
	}

	// get aligned buffer pointer
	pBuff					= FFAT_GET_ALIGNED_ADDR(pDECI->pBuff);
	dwSize					= (t_int32)(pDECI->dwSize - (pBuff - pDECI->pBuff));

	pUserDEC				= (UserDEC*)pBuff;		// set DEC pointer
	pUserDEC->pBuff			= pDECI->pBuff;
	pUserDEC->dwSize		= pDECI->dwSize;

	pUserDEC->pDECNode		= (DECNode*)FFAT_GET_ALIGNED_ADDR(pBuff + sizeof(UserDEC));
	pUserDEC->dwMaxEntryCnt	= (t_int32)((t_uint8*)pBuff 
										- (t_uint8*)pUserDEC->pDECNode + dwSize);
	pUserDEC->dwMaxEntryCnt	-= (sizeof(DECNode) + sizeof(_DECInfo));
	pUserDEC->dwMaxEntryCnt	/= sizeof(DECEntry);
	pUserDEC->dwMaxEntryCnt	&= 0xFFFFFFFE;			// make even

	FFAT_ASSERT((pUserDEC->dwMaxEntryCnt % 2) == 0);

	// initialize DEC Node
	_initDECNode(NODE_VOL(pNode), pUserDEC->pDECNode);

	pDECInfo = _FIRST_INFO_OF_NODE(pUserDEC->pDECNode);

	// initialize DEC Info
	_initDECInfo(pDECInfo, _SIZE_UDEC_BLOCK(pUserDEC->dwMaxEntryCnt));

	// add to DEC info list
	ESS_DLIST_ADD_HEAD(&(pUserDEC->pDECNode->stHeadDECInfo), &(pDECInfo->stListDECInfo));

	// set User DEC to Node
	_SET_NODE_ADDON_UDEC_PTR(pNode, pUserDEC);
	_SET_USER_DEC(pNode);

	return FFAT_OK;
}


/**
 * release user DEC
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
extern FFatErr
ffat_dec_releaseUDEC(Node* pNode)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	if ((NODE_ADDON(pNode)->dwFlag & ADDON_NODE_UDEC) == 0)
	{
		FFAT_LOG_PRINTF((_T("User DEC is not allocated")));
		return FFAT_EINVALID;
	}

	_SET_NODE_ADDON_UDEC_PTR(pNode, NULL);

	_RESET_USER_DEC(pNode);

	return FFAT_OK;
}


/**
 * get user DEC info
 *
 * @param		pNode		: [IN] node pointer
 * @param		pDECI		: [IN] DEC info
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_getUDECInfo(Node* pNode, FFatDirEntryCacheInfo* pDECI)
{
	UserDEC*		pUserDEC;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDECI);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	if ((NODE_ADDON(pNode)->dwFlag & ADDON_NODE_UDEC) == 0)
	{
		//User DEC is not allocated
		return FFAT_EINVALID;
	}

	pUserDEC		= (UserDEC*)_NODE_ADDON_UDEC(pNode);

	pDECI->pBuff	= pUserDEC->pBuff;
	pDECI->dwSize	= pUserDEC->dwSize;

	return FFAT_OK;
}


/**
 * reset memory for global DEC
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		SungWoo Jo
 * @version		OCT-15-2007 [SungWoo Jo] First Writing.
 * @version		JAN-22-2008 [GwangOk Go] Refactoring DEC module
 */
FFatErr
ffat_dec_deallocateGDEC(Node* pNode)
{
	_AddonDECType	eDECType;
	t_int32			dwBlockEntryCount;
	DECNode*		pDECNode = NULL;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	eDECType = _getDECNodeAndEntryCount(pNode, &pDECNode, &dwBlockEntryCount);
	if (eDECType != ADDON_DEC_GLOBAL)
	{
		return FFAT_OK;
	}

	_gdec_resetGDECNode(pDECNode);

	_RESET_GLOBAL_DEC(pNode);

	return FFAT_OK;
}


/**
 * lookup a node in a directory
 * lookup a node that name is psName and store directory to pGetNodeDE
 *
 * This function is used by FFAT CORE. so it does not perform node lock
 *
 * @param		pNodeParent	: [IN] parent node
 * @param		pNodeChild	: [IN] child node
 * @param		psName		: [IN] name
 * @param		dwLen		: [IN] name length
 * @param		dwFlag		: [IN] lookup flag
 * @param		pNodeDE		: [IN/OUT] directory entry for node
 * @param		pNumericTail: [IN/OUT] numeric tail
 * @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않음 or 일부에 대한 lookup을 수행함
 * [en] @return	FFAT_OK		: lookup does not process at ADDON module or a part of lookup processes.
 * @return		FFAT_DONE	: ADDON module에서 lookup을 성공적으로 처리함.
 * [en] return	FFAT_DONE	: lookup processes successfully at ADDON module. 
 * @return		FFAT_ENOENT	: 해당 node가 존재하지 않음
 * [en] @return	FFAT_ENOENT	: relevant nodes do not exist.
 * @return		negative	: error 
 * @author		DongYoung Seo
 * @version		AUG-10-2006 [DongYoung Seo] First Writing
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module (support FFAT_LOOKUP_FOR_CREATE)
 */
FFatErr
ffat_dec_lookup(Node* pNodeParent, Node* pNodeChild, t_wchar* psName,
				t_int32 dwLen, FFatLookupFlag dwFlag, FatGetNodeDe* pNodeDE,
				NodeNumericTail* pNumericTail, ComCxt* pCxt)
{
	FFatErr			r;
	_AddonDECType	eDECType;
	DECNode*		pDECNode = NULL;
	t_int32			dwBlockEntryCount;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pNodeDE);

	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);

	eDECType = _getDECNodeAndEntryCount(pNodeParent, &pDECNode, &dwBlockEntryCount);
	if (eDECType == ADDON_DEC_NONE)
	{
		if (_gGlobalDEC.bUseGDEC == FFAT_TRUE)
		{
			// parent node에 Global DEC 또는 User DEC가 할당되어 있지 않으며
			// Global DEC를 사용하는 경우 Global DEC를 할당
			// [en] in case Global DEC or User DEC does not be allocated at parent node and Global DEC uses,
			//      Global DEC is allocated.
			r = _gdec_allocateGDEC(pNodeParent, &pDECNode);
			if (r == FFAT_ENOSPC)
			{
				// not enough space
				return FFAT_OK;
			}
			else if (r < 0)
			{
				return r;
			}

			dwBlockEntryCount = FFAT_GDEC_BASE_ENTRY_COUNT;
		}
		else
		{
			// not use DEC
			return FFAT_OK;
		}
	}

	r = _lookupAndBuild(pNodeParent, pNodeChild, pDECNode, dwBlockEntryCount,
					psName, dwLen, dwFlag, pNodeDE, pNumericTail, pCxt);
// debug begin
#ifdef _DEC_HIT_MISS
	if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) == 0)
	{
		// normal lookup
		if (r == FFAT_DONE)
		{
			_DEC_HIT
		}
		else if (r != FFAT_ENOENT)
		{
			_DEC_MISS
		}
		//_DEC_PRINT_HIT_MISS
	}
#endif
// debug end

	return r;
}


/**
 * insert entry in DEC
 *
 * @param		pNodeParent	: [IN] parent node
 * @param		pNodeChild	: [IN] child node
 * @param		psName		: [IN] name
 * @param		dwLen		: [IN] name length
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOSPC	: DEC is full
 * @return		negative	: error 
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_insertEntry(Node* pNodeParent, Node* pNodeChild, t_wchar* psName)
{
	FFatErr			r;
	_AddonDECType	eDECType;
	t_boolean		bLFN;
	t_int32			dwEntryIndex;
	t_int32			dwBlockEntryCount;
	DECNode*		pDECNode = NULL;
	DECEntry*		pDECEntry;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);

	eDECType = _getDECNodeAndEntryCount(pNodeParent, &pDECNode, &dwBlockEntryCount);
	if (eDECType == ADDON_DEC_NONE)
	{
		return FFAT_OK;
	}

	dwEntryIndex = pNodeChild->stDeInfo.dwDeStartOffset >> FAT_DE_SIZE_BITS;

	FFAT_ASSERT((dwEntryIndex >= 0) && (dwEntryIndex < _MASK_ENTRY_INDEX));

	if ((pNodeChild->dwFlag & NODE_NAME_SFN) == 0)
	{
		bLFN = FFAT_TRUE;
	}
	else
	{
		bLFN = FFAT_FALSE;
	}

	r = _insertEntry(pNodeParent, &pDECNode, dwBlockEntryCount, psName, pNodeChild->wNameLen,
					pNodeChild->wNamePartLen, pNodeChild->bSfnNameSize, dwEntryIndex,
					&(pNodeChild->stDE), bLFN, &pDECEntry);

	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

// debug begin
#ifdef _DEC_DEBUG
	_dec_checkDECNodeLastDECEntry();
#endif
// debug end

	return r;
}


/**
 * remove entry in DEC
 *
 * @param		pNodeParent	: [IN] parent node (parent of entry to remove)
 * @param		pNodeChild	: [IN] child node (entry to remove)
 * @return		FFAT_OK		: success
 * @return		negative	: error 
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
FFatErr
ffat_dec_removeEntry(Node* pNodeParent, Node* pNodeChild)
{
	_AddonDECType	eDECType;
	DECNode*		pDECNode = NULL;
	_DECInfo*		pDECInfo;
	DECEntry*		pDECEntry;
	t_int32			dwIndex;
	t_int32			dwCurEntryIndex;
	t_int32			dwRemEntryIndex;
	t_int32			dwBlockEntryCount;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);

	if (NODE_IS_DIR(pNodeChild) == FFAT_TRUE)
	{
		// 지우려는 entry가 directory인 경우 global dec가 할당되어 있으면 삭제
		// [en] in case directory is entry which is considered deleting, 
		//      it is deleted if global dec is allocated.
		eDECType = _getDECNodeAndEntryCount(pNodeChild, &pDECNode, &dwBlockEntryCount);
		if (eDECType == ADDON_DEC_GLOBAL)
		{
			_gdec_resetGDECNode(pDECNode);

			_RESET_GLOBAL_DEC(pNodeChild);
		}
	}

	eDECType = _getDECNodeAndEntryCount(pNodeParent, &pDECNode, &dwBlockEntryCount);
	if (eDECType == ADDON_DEC_NONE)
	{
		return FFAT_OK;
	}

	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

	dwRemEntryIndex = (pNodeChild->stDeInfo.dwDeStartOffset) >> FAT_DE_SIZE_BITS;

	FFAT_ASSERT(dwRemEntryIndex >= 0 && dwRemEntryIndex < _MASK_ENTRY_INDEX);

	// loop DEC Info list
	ESS_DLIST_FOR_EACH_ENTRY(_DECInfo, pDECInfo, &(pDECNode->stHeadDECInfo), stListDECInfo)
	{
		if ((dwRemEntryIndex > pDECInfo->wLastEntryIndex) || (pDECInfo->wLastEntryIndex == _MASK_ENTRY_INDEX))
		{
			continue;
		}

		pDECEntry = _FIRST_ENTRY_OF_INFO(pDECInfo);

		// loop DEC Entry
		for (dwIndex = 0; dwIndex < dwBlockEntryCount; dwIndex++)
		{
			FFAT_ASSERT(_DECE_ENTRY_INDEX(pDECEntry) <= (t_uint32)pDECNode->wLastEntryIndex || _DECE_ENTRY_INDEX(pDECEntry) == _MASK_ENTRY_INDEX);
	
    // Fix done on 02/03/2012 by Utkarsh Pandey for Empty DECEntry check                   
                        if ((pDECEntry->wLfnFirstChar == _MASK_FIRST_NAME_CHAR)&&
			   (pDECEntry->bSfnFirstChar  == 0xFF)&&
                           (pDECEntry->wLfnLastChar  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntry->wLfn2ndLastChar  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntry->bSfnLastChar  == 0xFF)&&
                           (pDECEntry->bSfn2ndLastChar  == 0xFF)&&
                           (pDECEntry->bFirstExtention  == 0xFF)&&
                           (pDECEntry->wEntryIndex  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntry->dwNameLength2NT  == 0xFFFFFFFF))            
                          {
				// empty or invalid entry
				pDECEntry++;
				continue;
		          } 

			dwCurEntryIndex = _DECE_ENTRY_INDEX(pDECEntry);

			FFAT_ASSERT(dwCurEntryIndex >= 0 && dwCurEntryIndex <= _MASK_ENTRY_INDEX);
			
			if (dwRemEntryIndex == dwCurEntryIndex)
			{
				FFAT_ASSERT(pDECInfo->wValidEntryCount > 0);
				FFAT_ASSERT(pDECInfo->wValidEntryCount <= dwBlockEntryCount);
				FFAT_ASSERT(pDECNode->wValidEntryCount > 0);

				// invalid mark at DEC entry
				FFAT_MEMSET(pDECEntry, 0xFF, sizeof(DECEntry));
				pDECInfo->wValidEntryCount--;
				pDECNode->wValidEntryCount--;

				FFAT_ASSERT(pDECInfo->wValidEntryCount >= 0);
				FFAT_ASSERT(pDECInfo->wValidEntryCount < dwBlockEntryCount);
				FFAT_ASSERT(pDECNode->wValidEntryCount >= 0);

				FFAT_ASSERT((dwRemEntryIndex == pDECNode->wLastEntryIndex) ? (dwRemEntryIndex == pDECInfo->wLastEntryIndex) : FFAT_TRUE);

				if (dwRemEntryIndex == pDECInfo->wLastEntryIndex)
				{
					// 삭제되는 entry가 DEC info의 last entry인 경우
					// [en] in case, last entry of DEC info is entry which is considered deleting
					if (dwRemEntryIndex == pDECNode->wLastEntryIndex)
					{
						// 삭제되는 entry가 마지막 DEC info에 있는 경우
						//[en] in case, last DEC info is entry which is considered deleting	
						FFAT_ASSERT(&(pDECInfo->stListDECInfo) == pDECNode->stHeadDECInfo.pPrev);
						FFAT_ASSERT(pDECInfo->stListDECInfo.pNext == &(pDECNode->stHeadDECInfo));

						if (pDECInfo->wValidEntryCount > 0)
						{
							// DEC info에 entry가 있는 경우
							// [en] in case, entry exists at DEC info
							pDECEntry = _getLastDECEntry(pDECInfo, dwIndex - 1);
							FFAT_ASSERT(pDECEntry != NULL);		// valid가 있기때문에 NULL이 될수 없음
																// [en] this can not be NULL because there are valid
							pDECInfo->wLastEntryIndex	= _DECE_ENTRY_INDEX(pDECEntry);
							pDECNode->wLastEntryIndex	= pDECInfo->wLastEntryIndex;
							pDECNode->pLastDECEntry		= pDECEntry;

							FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));
						}
						else if (pDECInfo->stListDECInfo.pPrev != &(pDECNode->stHeadDECInfo))
						{
							// DEC info에 entry가 없고, DEC info가 2개 이상인 경우 (EXTEND가 있는 경우)
							// [en] in case there is no entry at DEC info and there are DEC info more than 2
							//      (in case of existing EXTEND)
							_DECInfo*	pDECInfoPrev;

							FFAT_ASSERT(pDECInfo->wValidEntryCount == 0);

							// 이전 DEC info에서 last entry를 찾아야 함
							// [en] last entry should be found at existing DEC info
							pDECInfoPrev = ESS_DLIST_GET_ENTRY(pDECInfo->stListDECInfo.pPrev, _DECInfo, stListDECInfo);
							
							if (pDECInfoPrev->wLastEntryIndex != _MASK_ENTRY_INDEX)
							{
								// get last DEC entry in previous DEC info
								pDECEntry = _getLastDECEntry(pDECInfoPrev, _COUNT_GDEC_BASE_ENTRY - 1);

								FFAT_ASSERT(pDECEntry != NULL);
								FFAT_ASSERT(pDECInfoPrev->wLastEntryIndex == _DECE_ENTRY_INDEX(pDECEntry));

								pDECNode->wLastEntryIndex	= pDECInfoPrev->wLastEntryIndex;
								pDECNode->pLastDECEntry		= pDECEntry;

								FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));
							}
							else
							{
								// 이전 DEC info가 빈 경우는 first DEC info인  경우 (BASE인 경우)
								// [en] in case of first DEC info if previous DEC info is empty
								//      ( in case of BASE )
								FFAT_ASSERT(&(pDECInfoPrev->stListDECInfo) == pDECNode->stHeadDECInfo.pNext);

								pDECNode->wLastEntryIndex	= _MASK_ENTRY_INDEX;
								pDECNode->pLastDECEntry		= NULL;

								FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));
							}

							// delete from DEC info list
							ESS_DLIST_DEL(pDECInfo->stListDECInfo.pPrev, pDECInfo->stListDECInfo.pNext);

							// invalidate buffer
							FFAT_MEMSET(pDECInfo, 0xFF, _SIZE_GDEC_EXTEND);

							FFAT_ASSERT(pDECInfo->wLastEntryIndex == _MASK_ENTRY_INDEX);

							if (pDECInfo == (_DECInfo*)(_gGlobalDEC.pFirstExtend))
							{
								// set first extend
								_gdec_setFirstExtend();
							}
						}
						else
						{
							// DEC info에 entry가 없고, DEC info가 1개인 경우 (EXTEND가 없는 경우)
							// [en] in case there is no entry at DEC info and there are DEC info more than 1
							//      (in case of no existing EXTEND)
							// DEC node에 valid entry가 없다
							// [en] there is no valid entry in DEC node
							FFAT_ASSERT(pDECInfo->stListDECInfo.pPrev == &(pDECNode->stHeadDECInfo));
							FFAT_ASSERT(pDECInfo->wValidEntryCount == 0);
							FFAT_ASSERT(pDECNode->wValidEntryCount == 0);

							pDECInfo->wLastEntryIndex	= _MASK_ENTRY_INDEX;
							pDECNode->wLastEntryIndex	= _MASK_ENTRY_INDEX;
							pDECNode->pLastDECEntry		= NULL;
						}
					}
					else
					{
						// 삭제되는 entry가 마지막 DEC info에 있지 않은 경우
						// [en] in case, there is no entry which is considered deleting	at last DEC info 
						if (pDECInfo->wValidEntryCount > 0)
						{
							// DEC info에 entry가 있는 경우
							// [en] in case there is entry at DEC info
							FFAT_ASSERT(pDECInfo->wLastEntryIndex != _MASK_ENTRY_INDEX);
							FFAT_ASSERT(pDECInfo->wLastEntryIndex != pDECNode->wLastEntryIndex);

							pDECEntry = _getLastDECEntry(pDECInfo, dwIndex - 1);
							FFAT_ASSERT(pDECEntry != NULL);		// valid가 있기때문에 NULL이 될수 없음
																// [en] this can not be NULL because there are valid
							pDECInfo->wLastEntryIndex	= _DECE_ENTRY_INDEX(pDECEntry);
							FFAT_ASSERT(pDECInfo->wLastEntryIndex != pDECNode->wLastEntryIndex);
						}
						else if (pDECInfo->stListDECInfo.pPrev != &(pDECNode->stHeadDECInfo))
						{
							// DEC info에 entry가 없고, previous DEC info가 있는 경우 (EXTEND인 경우)
							// [en] in case there is no entry at DEC info and there is previous DEC info
							//      (in case of no existing EXTEND)
							FFAT_ASSERT(pDECInfo->wValidEntryCount == 0);

							// delete from DEC info list
							ESS_DLIST_DEL(pDECInfo->stListDECInfo.pPrev, pDECInfo->stListDECInfo.pNext);

							// invalidate buffer
							FFAT_MEMSET(pDECInfo, 0xFF, _SIZE_GDEC_EXTEND);

							if (pDECInfo == (_DECInfo*)(_gGlobalDEC.pFirstExtend))
							{
								// set first extend
								_gdec_setFirstExtend();
							}
						}
						else
						{
							// DEC info에 entry가 없고, previous DEC info가 없는 경우 (BASE인 경우)
							// [en] in case there is no entry at DEC info and there is no previous DEC info
							//      ( in case of BASE )
							FFAT_ASSERT(pDECInfo->wValidEntryCount == 0);

							pDECInfo->wLastEntryIndex	= _MASK_ENTRY_INDEX;
						}
					}
				}

				goto out;
			}

			pDECEntry++;
		}
	}

out:
	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

// debug begin
#ifdef _DEC_DEBUG
	_dec_checkDECNodeLastDECEntry();
#endif
// debug end

	return FFAT_OK;
}


//=============================================================================
//
//	Static Functions
//


/**
 * initialize for global DEC
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 * @version		OCT-06-2009 [DongYoung Seo] change return type from void to FFatErr
 *								FFAT_MALLOC may return error
 */
static FFatErr
_gdec_init(void)
{
#ifdef FFAT_GDEC
	// allocate buffer for GDEC
	_gGlobalDEC.pBuffer = (t_uint8*)FFAT_MALLOC(FFAT_GDEC_MEM_SIZE, ESS_MALLOC_NONE);
	if (_gGlobalDEC.pBuffer == NULL)
	{
		FFAT_PRINT_CRITICAL((_T("Fail to allocate memory for GDEC\n")));
		return FFAT_ENOMEM;
	}

	// kkaka, check it.
	// why fill all 32KB buffer to 0xFF
	FFAT_MEMSET(_gGlobalDEC.pBuffer, 0xFF, FFAT_GDEC_MEM_SIZE);
	FFAT_ASSERT(_gGlobalDEC.pBuffer == (t_uint8*)FFAT_GET_ALIGNED_ADDR(_gGlobalDEC.pBuffer));

	// init DECNode List for GDEC
	ESS_DLIST_INIT(&(_gGlobalDEC.stHeadDECNode));
	_gGlobalDEC.dwDECNodeCount	= 0;

	// set last base & first extend
	_gGlobalDEC.pLastBase		= _gGlobalDEC.pBuffer;
	_gGlobalDEC.pFirstExtend	= _gGlobalDEC.pBuffer + FFAT_GDEC_MEM_SIZE;

	_gGlobalDEC.bUseGDEC		= FFAT_TRUE;
#else
	FFAT_MEMSET(&_gGlobalDEC, 0x00, sizeof(_gGlobalDEC));

	_gGlobalDEC.bUseGDEC		= FFAT_FALSE;
#endif

	return FFAT_OK;
}


/**
 * terminate for global DEC
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	 Refactoring DEC module
 */
static void
_gdec_terminate(void)
{
	// free buffer for GDEC
	FFAT_FREE(_gGlobalDEC.pBuffer, FFAT_GDEC_MEM_SIZE);

	_gGlobalDEC.bUseGDEC		= FFAT_FALSE;
}


/**
 * initialize DEC Node
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pDECNode	: [IN] DEC node pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_initDECNode(Vol* pVol, DECNode* pDECNode)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pDECNode);

// debug begin
#ifdef FFAT_DEBUG
	pDECNode->dwSig				= _DEC_NODE_SIG;
#endif
// debug end

	pDECNode->wValidEntryCount	= 0;
	pDECNode->wLastEntryIndex	= _MASK_ENTRY_INDEX;
	pDECNode->pLastDECEntry		= NULL;

	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

	ESS_DLIST_INIT(&pDECNode->stHeadDECInfo);

	pDECNode->pVol		= pVol;
}


/**
 * initialize DEC Info
 *
 * @param		pDECInfo	: [IN] DEC info pointer
 * @param		dwBlockSize	: [IN] size of DEC entries in one DEC info
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_initDECInfo(_DECInfo* pDECInfo, t_int32 dwBlockSize)
{
	FFAT_ASSERT(pDECInfo);

// debug begin
#ifdef FFAT_DEBUG
	pDECInfo->dwSig				= _DEC_INFO_SIG;
#endif
// debug end

	pDECInfo->wValidEntryCount	= 0;
	pDECInfo->wLastEntryIndex	= _MASK_ENTRY_INDEX;

	// kkaka. why does it set all of the memory area?
	FFAT_MEMSET((t_uint8*)pDECInfo + sizeof(_DECInfo), 0xFF, dwBlockSize);
}


/**
 * allocate and initialize memory for GDEC
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		SungWoo Jo
 * @version		OCT-15-2007 [SungWoo Jo]	First Writing.
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static FFatErr
_gdec_allocateGDEC(Node* pNode, DECNode** ppDECNode)
{
	DECNode*		pDECNode;
	_DECInfo*		pDECInfo;
	Vol*			pVol = NODE_VOL(pNode);

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);
	FFAT_ASSERT(_gGlobalDEC.bUseGDEC == FFAT_TRUE);
	FFAT_ASSERT(_NODE_ADDON_UDEC(pNode) == NULL);
	FFAT_ASSERT((NODE_ADDON(pNode)->dwFlag & ADDON_NODE_UDEC) == 0);

	// get free DEC Node
	pDECNode = _gdec_getFreeGDECNode();
	if (pDECNode == NULL)
	{
		// There is no free space to allocate GDEC
		return FFAT_ENOSPC;
	}

	// initialize DEC Node
	_initDECNode(pVol, pDECNode);

	// add to DECNode list
	ESS_DLIST_ADD_HEAD(&(_gGlobalDEC.stHeadDECNode), &(pDECNode->stListDECNode));

	pDECInfo = _FIRST_INFO_OF_NODE(pDECNode);

	// initialize DEC Info
	_initDECInfo(pDECInfo, _SIZE_GDEC_BLOCK);

	if ((t_uint8*)pDECNode > _gGlobalDEC.pLastBase)
	{
		// set last DECNode
		_gGlobalDEC.pLastBase = (t_uint8*)pDECNode;
	}

	FFAT_ASSERT(((t_uint32)pDECNode & FFAT_MEM_ALIGN_MASK) == 0);

	// add to DEC info list
	ESS_DLIST_ADD_HEAD(&(pDECNode->stHeadDECInfo), &(pDECInfo->stListDECInfo));

	_gGlobalDEC.dwDECNodeCount++;

	pDECNode->dwCluster	= NODE_C(pNode);

	_SET_GLOBAL_DEC(pNode);

	*ppDECNode = pDECNode;

	return FFAT_OK;
}


/**
 * reset DEC Node and DEC Info for GDEC
 *
 * @param		pDECNode	: [IN] DEC node pointer
 * @return		FFAT_OK		: success
 * @return		void
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_gdec_resetGDECNode(DECNode* pDECNode)
{
	_DECInfo*	pDECInfo;
	_DECInfo*	pDECInfoTemp;
	t_boolean	bSetFirstExtend = FFAT_FALSE;

	// _DECInfo invalid mark is connected at DECNode
	ESS_DLIST_FOR_EACH_ENTRY_SAFE(_DECInfo, pDECInfo, pDECInfoTemp, &(pDECNode->stHeadDECInfo), stListDECInfo)
	{
		// kkaka, check it
		// why do all memory area fill to 0xFF? it consumes computing power.
		FFAT_MEMSET(pDECInfo, 0xFF, _SIZE_GDEC_EXTEND);

		if ((t_uint8*)pDECInfo == _gGlobalDEC.pFirstExtend)
		{
			bSetFirstExtend = FFAT_TRUE;
		}
	}

	// delete from DECNode list
	ESS_DLIST_DEL(pDECNode->stListDECNode.pPrev, pDECNode->stListDECNode.pNext);

	// kkaka, check it
	// why do all memory area fill to 0xFF? it consumes computing power.
	// reset buffer
	FFAT_MEMSET(pDECNode, 0xFF, sizeof(DECNode));

	_gGlobalDEC.dwDECNodeCount--;

	if ((t_uint8*)pDECNode == _gGlobalDEC.pLastBase)
	{
		// 지워질 DEC node가 last base인 경우
		// [en] in case DEC node which is considered deleting is last base
		// set last base address.
		_gdec_setLastBase();
	}

	if (bSetFirstExtend == FFAT_TRUE)
	{
		// first extend가 지워진 경우
		// [en] in case first extend is already deleted
		// set first extend address
		_gdec_setFirstExtend();
	}

// debug begin
#ifdef _DEC_DEBUG
	_dec_checkDECNodeLastDECEntry();
#endif
// debug end

	return;
}


/**
 * get free DEC node (free base space) for global DEC
 *
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static DECNode*
_gdec_getFreeGDECNode(void)
{
	t_uint8*	pCurDECNode = _gGlobalDEC.pBuffer;
	t_uint8*	pCurDECInfo;
	EssDList*	pstListTail;

	FFAT_ASSERT(pCurDECNode);

	// EXTEND보다 작은 영역에 빈 공간이 있는지 확인
	// [en] check empty space at area smaller than EXTEND
	while ((pCurDECNode + _SIZE_GDEC_BASE) <= (t_uint8*)(_gGlobalDEC.pFirstExtend))
	{
		if (((DECNode*)pCurDECNode)->wValidEntryCount == -1)
		{
// debug begin
#ifdef _DEC_DEBUG
			_dec_checkDECNodeLastDECEntry();
#endif
// debug end

			// empty or invalid space
			return (DECNode*)pCurDECNode;
		}

		pCurDECNode += _SIZE_GDEC_BASE;
	}

	pCurDECInfo = (t_uint8*)(_gGlobalDEC.pFirstExtend);

	// EXTEND 영역에 빈 공간이 있는지 확인
	// [en] check empty space at EXTEND area
	while (pCurDECInfo <= _LAST_EXTEND_OF_GDEC(_gGlobalDEC.pBuffer))
	{
		if (((_DECInfo*)pCurDECInfo)->wValidEntryCount == -1)
		{
			// Move the lowest GDEC Extend to free GDEC Extend
			_DECInfo*	pDECInfoSrc = (_DECInfo*)(_gGlobalDEC.pFirstExtend);	// THIS IS CURRENT LOWEST GDEC EXTEND
			_DECInfo*	pDECInfoDes = (_DECInfo*)pCurDECInfo;					// THIS IS FREE GDEC EXTEND
			DECNode*	pDECNode;
			DECEntry*	pTempEntry;

			// empty or invalid space
			FFAT_ASSERT(pCurDECInfo != _gGlobalDEC.pFirstExtend);

			// copy first extend to empty space
			FFAT_MEMCPY(pCurDECInfo, _gGlobalDEC.pFirstExtend, _SIZE_GDEC_EXTEND);

			// update DEC info list
			ESS_DLIST_ADD((&(pDECInfoSrc->stListDECInfo))->pPrev, (&(pDECInfoSrc->stListDECInfo))->pNext,
							&(pDECInfoDes->stListDECInfo));

			// get last DEC entry of source DEC info
			pTempEntry = _getLastDECEntry(pDECInfoSrc, _COUNT_GDEC_BASE_ENTRY - 1);

			pDECNode = _gdec_getDECNodeByLastEntry(pTempEntry);
			if (pDECNode != NULL)
			{
				FFAT_ASSERT(pTempEntry == pDECNode->pLastDECEntry);
				FFAT_ASSERT(pDECNode->wLastEntryIndex == pDECInfoDes->wLastEntryIndex);

				// DEC node의 last entry가 이동한 경우
				// [en] in case of having moved last entry of DEC node
				pDECNode->pLastDECEntry		= (DECEntry*)((t_uint8*)(pDECNode->pLastDECEntry) +
												(pCurDECInfo - _gGlobalDEC.pFirstExtend));

				FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));
			}

			// reset buffer of first extend
			FFAT_MEMSET(_gGlobalDEC.pFirstExtend, 0xFF, _SIZE_GDEC_EXTEND);

			// set first extend
			_gdec_setFirstExtend();

			// kkaka check it
			// BASE 정보를 구성하기 위한 빈 공간이 있는지 확인
			// [en] check empty space for consisting BASE information
			if (((_gGlobalDEC.pFirstExtend) - pCurDECNode) >= _SIZE_GDEC_BASE)
			{
// debug begin
#ifdef _DEC_DEBUG
				_dec_checkDECNodeLastDECEntry();
#endif
// debug end

				return (DECNode*)pCurDECNode;
			}
		}

		pCurDECInfo += _SIZE_GDEC_EXTEND;
	}

	// there is no free space at extend area
	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(_gGlobalDEC.stHeadDECNode)) == FFAT_FALSE);

	// DEC Node List에서 LRU DEC Node를 반환
	// [en] return LRU DEC Node at DEC Node List
	pstListTail = ESS_DLIST_GET_TAIL(&(_gGlobalDEC.stHeadDECNode));

	FFAT_ASSERT(ESS_DLIST_IS_EMPTY(&(_gGlobalDEC.stHeadDECNode)) == FFAT_FALSE);

	// 반환할 free DECNode로 LRU DECNode를 선정
	// [en] LRU DECNode is decided as returning free DECNode
	pCurDECNode = (t_uint8*)ESS_DLIST_GET_ENTRY(pstListTail, DECNode, stListDECNode);

	FFAT_ASSERT(pCurDECNode >= (t_uint8*)(_gGlobalDEC.pBuffer));
	FFAT_ASSERT(pCurDECNode <= (t_uint8*)(_gGlobalDEC.pFirstExtend) - _SIZE_GDEC_BASE);

	_gdec_resetGDECNode((DECNode*)pCurDECNode);

	return (DECNode*)pCurDECNode;
}


/**
 * expand DEC node for global DEC
 *
 * @param		ppDECNode	: [IN/OUT] DEC node pointer (if DEC node is moved, pointer change)
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static _DECInfo*
_gdec_expandGDECNode(DECNode** ppDECNode)
{
	_DECInfo*	pFreeDECInfo;

	FFAT_ASSERT(*ppDECNode);

	// get free DEC info
	pFreeDECInfo = _gdec_getFreeExtend(ppDECNode);

	if (pFreeDECInfo != NULL)
	{
		_initDECInfo(pFreeDECInfo, _SIZE_GDEC_BLOCK);

		// add to DECNode list
		ESS_DLIST_ADD_TAIL(&((*ppDECNode)->stHeadDECInfo), &(pFreeDECInfo->stListDECInfo));

		if ((t_uint8*)pFreeDECInfo < _gGlobalDEC.pFirstExtend)
		{
			_gGlobalDEC.pFirstExtend = (t_uint8*)pFreeDECInfo;
		}
	}

// debug begin
#ifdef _DEC_DEBUG
	_dec_checkDECNodeLastDECEntry();
#endif
// debug end

	return pFreeDECInfo;
}


/**
 * get free extend space (free DEC info) for global DEC
 *
 * @param		ppDECNode	: [IN/OUT] DEC node pointer (if DEC node is moved, pointer change)
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static _DECInfo*
_gdec_getFreeExtend(DECNode** ppDECNode)
{
	t_uint8*	pCurDECInfo;
	t_uint8*	pCurDECNode;
	EssDList*	pstListTail;

	FFAT_ASSERT(*ppDECNode);

re:
	pCurDECInfo = _LAST_EXTEND_OF_GDEC(_gGlobalDEC.pBuffer);

	// BASE보다 큰 영역에 빈 공간이 있는지 확인
	// [en] check empty space at area larger than BASE
	while (pCurDECInfo >= ((t_uint8*)(_gGlobalDEC.pLastBase) + _SIZE_GDEC_BASE))
	{
		if (((_DECInfo*)pCurDECInfo)->wValidEntryCount == -1)
		{
// debug begin
#ifdef _DEC_DEBUG
			_dec_checkDECNodeLastDECEntry();
#endif
// debug end

			return (_DECInfo*)pCurDECInfo;
		}

		pCurDECInfo -= _SIZE_GDEC_EXTEND;
	}

	pCurDECNode = (t_uint8*)(_gGlobalDEC.pLastBase);

	// BASE 영역에 빈 공간이 있는지 확인
	// [en] check empty space at BASE area 
	while (pCurDECNode >= _gGlobalDEC.pBuffer)
	{
		if (((DECNode*)pCurDECNode)->wValidEntryCount == -1)
		{
			DECNode*	pDECNodeSrc	= (DECNode*)(_gGlobalDEC.pLastBase);
			DECNode*	pDECNodeDes	= (DECNode*)pCurDECNode;
			_DECInfo*	pDECInfoSrc	= _FIRST_INFO_OF_NODE(pDECNodeSrc);
			_DECInfo*	pDECInfoDes	= _FIRST_INFO_OF_NODE(pDECNodeDes);

			// empty or invalid space
			FFAT_ASSERT(pCurDECNode != _gGlobalDEC.pLastBase);
			
			// copy last base to empty space
			FFAT_MEMCPY(pCurDECNode, _gGlobalDEC.pLastBase, _SIZE_GDEC_BASE);

			if (pDECInfoSrc->stListDECInfo.pNext == &(pDECNodeSrc->stHeadDECInfo))
			{
				// DEC node에 DEC info가 1개만 있는 경우 (BASE만 있는 경우)
				// [en] in case there is the only DEC info in DEC node (in case there is BASE)

				// init DEC info List
				ESS_DLIST_INIT(&(pDECNodeDes->stHeadDECInfo));

				// add to DEC info list
				ESS_DLIST_ADD_TAIL(&(pDECNodeDes->stHeadDECInfo), &(pDECInfoDes->stListDECInfo));

				if (pDECNodeSrc->pLastDECEntry != NULL)
				{
					FFAT_ASSERT(pDECNodeSrc->wLastEntryIndex != _MASK_ENTRY_INDEX);
					FFAT_ASSERT(pDECNodeSrc->pLastDECEntry >= _FIRST_ENTRY_OF_INFO(pDECNodeSrc));
					FFAT_ASSERT(pDECNodeSrc->pLastDECEntry <= _LAST_ENTRY_OF_GDEC_INFO(pDECInfoSrc));

					// set last DEC entry
					pDECNodeDes->pLastDECEntry = (DECEntry*)(pCurDECNode +
									((t_uint8*)(pDECNodeSrc->pLastDECEntry) - _gGlobalDEC.pLastBase));
				}

				FFAT_ASSERT((pDECNodeDes->pLastDECEntry == NULL) ? (pDECNodeDes->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNodeDes->pLastDECEntry) == pDECNodeDes->wLastEntryIndex));
				FFAT_ASSERT((pDECNodeSrc->pLastDECEntry == NULL) ? (pDECNodeSrc->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNodeSrc->pLastDECEntry) == pDECNodeSrc->wLastEntryIndex));
			}
			else
			{
				EssDList*	pListTail;

				// DEC node에 DEC info가 2개 이상 있는 경우 (EXTEND도 있는 경우)
				// [en] in case there are DEC info more than 2 at DEC node
				//      (in case there is also EXTEND)

				// fix DEC info list
				ESS_DLIST_ADD(&(pDECNodeDes->stHeadDECInfo), (&(pDECInfoSrc->stListDECInfo))->pNext,
							&(pDECInfoDes->stListDECInfo));

				pListTail = pDECNodeDes->stHeadDECInfo.pPrev;

				pListTail->pNext = &(pDECNodeDes->stHeadDECInfo);
			}

			// fix DEC node list
			ESS_DLIST_ADD((&(pDECNodeSrc->stListDECNode))->pPrev, (&(pDECNodeSrc->stListDECNode))->pNext,
						&(pDECNodeDes->stListDECNode));

			// reset buffer of last base
			FFAT_MEMSET(_gGlobalDEC.pLastBase, 0xFF, _SIZE_GDEC_BASE);

			// set lase base
			_gdec_setLastBase();

			if (pDECNodeSrc == *ppDECNode)
			{
				// 현재 DEC node가 copy된 경우
				// [en] in case current DEV node is copied
				*ppDECNode = pDECNodeDes;
			}

			FFAT_ASSERT(pCurDECInfo >= _gGlobalDEC.pLastBase + _SIZE_GDEC_BASE);
			FFAT_ASSERT(pCurDECInfo + _SIZE_GDEC_EXTEND <= _gGlobalDEC.pFirstExtend);

// debug begin
#ifdef _DEC_DEBUG
			_dec_checkDECNodeLastDECEntry();
#endif
// debug end

			return (_DECInfo*)pCurDECInfo;
		}

		pCurDECNode -= _SIZE_GDEC_BASE;
	}

	// DEC Node List에서 LRU DEC Node를 반환
	// [en] return LRU DEC Node at DEC Node List
	pstListTail = ESS_DLIST_GET_TAIL(&(_gGlobalDEC.stHeadDECNode));

	IF_UK (pstListTail == &(_gGlobalDEC.stHeadDECNode))
	{
		// there is no node in list
		FFAT_ASSERT(0);
		//return FFAT_EPROG;
		return NULL;
	}

	if (pstListTail->pPrev == &(_gGlobalDEC.stHeadDECNode))
	{
		// there is one node in list
		FFAT_ASSERT(*ppDECNode == ESS_DLIST_GET_ENTRY(pstListTail, DECNode, stListDECNode));

// debug begin
#ifdef _DEC_DEBUG
		_dec_checkDECNodeLastDECEntry();
#endif
// debug end

		return NULL;
	}

	// 반환할 free DECNode로 LRU DECNode를 선정
	// [en] LRU DECNode is decided as returning free DECNode	
	pCurDECNode = (t_uint8*)ESS_DLIST_GET_ENTRY(pstListTail, DECNode, stListDECNode);

	FFAT_ASSERT(pCurDECNode != (t_uint8*)*ppDECNode);

	FFAT_ASSERT(pCurDECNode >= (t_uint8*)(_gGlobalDEC.pBuffer));
	FFAT_ASSERT(pCurDECNode <= (t_uint8*)(_gGlobalDEC.pFirstExtend) - _SIZE_GDEC_BASE);

	_gdec_resetGDECNode((DECNode*)pCurDECNode);

	goto re;
}


/**
 * set last base address (last DEC node) of global DEC
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_gdec_setLastBase(void)
{
	t_uint8*	pTemp = _gGlobalDEC.pLastBase;

	// loop by base size
	while (pTemp >= _gGlobalDEC.pBuffer)
	{
		if (((DECNode*)pTemp)->wValidEntryCount != -1)
		{
			// there is empty of invalid base
			_gGlobalDEC.pLastBase = pTemp;
			return;
		}

		pTemp -= _SIZE_GDEC_BASE;
	}

	// set initial value
	_gGlobalDEC.pLastBase = _gGlobalDEC.pBuffer;
	return;
}


/**
 * set first extend address (first additional DEC info) of global DEC
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_gdec_setFirstExtend(void)
{
	t_uint8*	pTemp = _gGlobalDEC.pFirstExtend;

	// loop by extend size
	while (pTemp <= _LAST_EXTEND_OF_GDEC(_gGlobalDEC.pBuffer))
	{
		if (((_DECInfo*)pTemp)->wValidEntryCount != -1)
		{
			// there is empty or invalid extend
			_gGlobalDEC.pFirstExtend = pTemp;
			return;
		}

		pTemp += _SIZE_GDEC_EXTEND;
	}

	// set initial value
	_gGlobalDEC.pFirstExtend = _gGlobalDEC.pBuffer + FFAT_GDEC_MEM_SIZE;
	return;
}


/**
 * lookup and build a node with directory entry cache
 * if there is no exist directory entry for target node, it will update offset to the last entry
 *
 * @param		pNodeParent	: [IN] parent node
 * @param		pNodeChild	: [IN] child node
 * @param		psName		: [IN] name (in case of case non-sensitive, it is upper name)
 * @param		dwLen		: [IN] name length
 * @param		dwFlag		: [IN] lookup flag
 * @param		pNodeDE		: [IN/OUT] directory entry for node
 * @param		pNumericTail: [IN/OUT] numeric tail
 * @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않음 or 일부에 대한 lookup을 수행함
 * [en] @return	FFAT_OK		: lookup does not process at ADDON module or a part of lookup processes.
 * @return		FFAT_DONE	: ADDON module에서 lookup을 성공적으로 처리함.
 * [en] return	FFAT_DONE	: lookup processes successfully at ADDON module.
 * @return		FFAT_ENOENT	: 해당 node가 존재하지 않음
 * [en] @return	FFAT_ENOENT	: relevant nodes do not exist.
 * @return		negative	: error
 * @author		DongYoung Seo
 * @version		AUG-10-2006 [DongYoung Seo] First Writing
 * @version		APR-17-2007 [DongYoung Seo] add memory free code
 * @version		JAN-22-2008 [GwangOk Go] Refactoring DEC module (support FFAT_LOOKUP_FOR_CREATE)
 * @version		AUG-11-2008 [DongYoung Seo] add code to update pNodeChild->wSfnNameSize 
 *								after inserting numeric tail
 * @version		APR-10-2009 [GwangOk Go] numeric tail support 23bits & add second last character into entry
 * @version		NOV-12-2009 [Sangyoon Oh] fix the bug that omits adding directory entries to DEC that are located behind the volume label entry[CQ25783]
 */
static FFatErr
_lookupAndBuild(Node* pNodeParent, Node* pNodeChild, DECNode* pDECNode, t_int32 dwBlockEntryCount,
			t_wchar* psName, t_int32 dwLen, FFatLookupFlag dwFlag, FatGetNodeDe* pNodeDE,
			NodeNumericTail* pNumericTail, ComCxt* pCxt)
{
	FFatErr			r;
	_DECInfo*		pDECInfo;
	DECEntry*		pDECEntry = NULL;
	t_wchar			wLfnFirstChar;			// first character of name part of long filename
	t_wchar			wLfnLastChar;			// last character of name part of long filename
	t_wchar			wLfn2ndLastChar = 0;	// second last character of name part of long filename
	t_int32			dwRequiredEntryCount;	// required free entry count
	FatVolInfo*		pVolInfo;				// volume info pointer
	t_int32			dwStartEntryIndex;		// entry index from parent first offset
	t_int32			dwCurEntryIndex = 0;
	t_int32			dwCurEntryCount = 0;
	t_wchar*		psCurName = NULL;		// current node name storage
	t_int32			dwCurNameLen;			// current node name length
	t_int32			dwIndex = 0;
	t_int32			dwNextStartIndex = 0;
	FatGetNodeDe	stNodeDeTemp;
	t_boolean		bLFN;

	t_uint8			bSfnFirstChar = 0;		// first character of name part of short filename
	t_uint8			bSfnLastChar = 0;			// last character of name part of short filename
	t_uint8			bSfn2ndLastChar = 0;	// second last character of name part of short filename
	t_int32			dwNamaPartLen;
	t_int32			dwSfnNameSize;
	Vol*			pVol;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(NODE_IS_DIR(pNodeParent) == FFAT_TRUE);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(pNodeChild->wNamePartLen >= 1);
	FFAT_ASSERT((pNodeChild->bSfnNameSize) >= 1 && (pNodeChild->bSfnNameSize <= FAT_SFN_NAME_PART_LEN));
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pNodeDE);

	pVol		= NODE_VOL(pNodeParent);
	pVolInfo	= VOL_VI(pVol);

	FFAT_ASSERT(pDECNode);
	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pVolInfo == NODE_VI(pNodeChild));

	wLfnFirstChar	= psName[0];
	wLfnLastChar	= psName[pNodeChild->wNamePartLen - 1];

	if (pNodeChild->wNamePartLen >= 2)
	{
		wLfn2ndLastChar	= psName[pNodeChild->wNamePartLen - 2];
	}

	if ((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0)
	{
		// SFN도 사용할 경우 (FAT 호환)
		// [en] in case of using SFN (FAT compatibility)
		bSfnFirstChar	= pNodeChild->stDE.sName[0];
		bSfnLastChar	= pNodeChild->stDE.sName[pNodeChild->bSfnNameSize - 1];

		if (pNodeChild->bSfnNameSize >= 2)
		{
			bSfn2ndLastChar	= pNodeChild->stDE.sName[pNodeChild->bSfnNameSize - 2];
		}
	}

	if ((VOL_FLAG(pVol) & VOL_ADDON_XDE) == 0)
	{
		// Extended DE를 사용하지 않을 경우
		// [en] in case of no using Extended DE
		dwRequiredEntryCount = pNodeDE->dwTargetEntryCount;
	}
	else
	{
		// Extended DE를 사용할 경우
		// [en] in case of using Extended DE
		dwRequiredEntryCount = pNodeDE->dwTargetEntryCount + 1;
	}

	FFAT_ASSERT(pNodeDE->dwOffset  <= FAT_DIR_SIZE_MAX);
	dwStartEntryIndex	= pNodeDE->dwOffset >> FAT_DE_SIZE_BITS;

	FFAT_MEMCPY(&stNodeDeTemp, pNodeDE, sizeof(FatGetNodeDe));

	stNodeDeTemp.dwClusterOfOffset = 0;		// use 'stNodeDeTemp.dwCluster'

	if (dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE))
	{
		// initialize previous information
		if (NODE_IS_ROOT(pNodeParent) == FFAT_TRUE)
		{
			dwNextStartIndex = 0;			// there is no entry
		}
		else
		{
			dwNextStartIndex = 2;			// set it to the offset for .. entry 
		}
	}

	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

	if (_IS_EMPTY_DEC_NODE(pDECNode) == FFAT_TRUE)
	{
		// there is no entry on DEC
		FFAT_ASSERT(NODE_C(pNodeParent) == pNodeDE->dwCluster);

		if (NODE_IS_ROOT(pNodeParent) == FFAT_TRUE)
		{
			stNodeDeTemp.dwOffset	= 0;
		}
		else
		{
			stNodeDeTemp.dwOffset	= FAT_DE_SIZE << 1;	// ignore '.' and '..'
		}

		pDECInfo = (_DECInfo*)((t_uint8*)pDECNode + sizeof(DECNode));
		pDECEntry = (DECEntry*)((t_uint8*)pDECInfo + sizeof(_DECInfo));

		goto build;
	}

	FFAT_ASSERT(pDECNode->wLastEntryIndex != _MASK_ENTRY_INDEX);

	// check there is valid node information from look start
	if (pDECNode->wLastEntryIndex < dwStartEntryIndex)
	{
		EssDList*	pstListTailTemp;

		pstListTailTemp = ESS_DLIST_GET_TAIL(&(pDECNode->stHeadDECInfo));

		pDECInfo = ESS_DLIST_GET_ENTRY(pstListTailTemp, _DECInfo, stListDECInfo);
		pDECEntry = pDECNode->pLastDECEntry;

		FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

		goto build_from_last;
	}

	//===================================================================
	// lookup part

	stNodeDeTemp.psName			= psName;
	stNodeDeTemp.dwNameLen		= dwLen;
	stNodeDeTemp.psShortName	= pNodeChild->stDE.sName;
	stNodeDeTemp.bExactOffset	= FFAT_TRUE;

	// loop DEC Info list
	ESS_DLIST_FOR_EACH_ENTRY(_DECInfo, pDECInfo, &(pDECNode->stHeadDECInfo), stListDECInfo)
	{
		if (((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) == 0) &&
				((dwStartEntryIndex > pDECInfo->wLastEntryIndex)
					|| (pDECInfo->wLastEntryIndex == _MASK_ENTRY_INDEX)))
		{
			continue;
		}

		pDECEntry = _FIRST_ENTRY_OF_INFO(pDECInfo);

		// lookup in DEC
		for (dwIndex = 0; dwIndex < dwBlockEntryCount; dwIndex++)
		{
			dwCurEntryIndex = _DECE_ENTRY_INDEX(pDECEntry);

			FFAT_ASSERT((dwCurEntryIndex >= 0) && (dwCurEntryIndex <= _MASK_ENTRY_INDEX));
	// Fix done on 02/03/2012 by Utkarsh Pandey for Empty DECEntry check
                        if ((pDECEntry->wLfnFirstChar == _MASK_FIRST_NAME_CHAR)&&
                            (pDECEntry->bSfnFirstChar  == 0xFF)&&
                            (pDECEntry->wLfnLastChar  == _MASK_FIRST_NAME_CHAR)&&
                            (pDECEntry->wLfn2ndLastChar  == _MASK_FIRST_NAME_CHAR)&&
                            (pDECEntry->bSfnLastChar  == 0xFF)&&
                            (pDECEntry->bSfn2ndLastChar  == 0xFF)&&
                            (pDECEntry->bFirstExtention  == 0xFF)&&
                            (pDECEntry->wEntryIndex  == _MASK_FIRST_NAME_CHAR)&&
                            (pDECEntry->dwNameLength2NT  == 0xFFFFFFFF))
			{
				// empty or invalid entry
				goto next;
			}

			if (dwFlag & FFAT_LOOKUP_FREE_DE)
			{
				goto next_update_freede;
			}

			if (((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) == 0) &&
				(dwCurEntryIndex < dwStartEntryIndex))
			{
				goto next;
			}

			if ((t_uint32)dwLen != _DECE_NAME_LENGTH(pDECEntry) ||
				(wLfnLastChar != pDECEntry->wLfnLastChar) ||
				(wLfn2ndLastChar != pDECEntry->wLfn2ndLastChar) ||
				(wLfnFirstChar != pDECEntry->wLfnFirstChar))
			{
				if ((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0)
				{
					// SFN도 사용하는 경우 (FAT 호환)
					// [en] in case of using SFN (FAT compatibility)
					// comparing with SFN 

					if ((pNodeChild->stDE.sName[FAT_SFN_NAME_PART_LEN] != pDECEntry->bFirstExtention) ||
						(bSfnLastChar != pDECEntry->bSfnLastChar) ||
						(bSfn2ndLastChar != pDECEntry->bSfn2ndLastChar) ||
						(bSfnFirstChar != pDECEntry->bSfnFirstChar))
					{
						// first or last character is not same one.
						if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME)) &&
							(wLfnFirstChar == pDECEntry->wLfnFirstChar))
						{
							// Name part의 첫번째 문자와 Ext part의 첫번째 문자가 같으면 numeric tail 업데이트
							// [en] update numeric tail if first character of Name part is equal to 
							//      first character of Ext part
							goto next_update_nt;
						}
						else
						{
							goto next_update_freede;
						}
					}
				}
				else
				{
					// LFN만 사용할 경우 (FAT 비호환)
					// [en] in case of using LFN (no FAT compatibility)
					// no comparing with SFN

					goto next_update_freede;
				}
			}

			stNodeDeTemp.dwOffset			= (dwCurEntryIndex << FAT_DE_SIZE_BITS);
			stNodeDeTemp.dwClusterOfOffset	= 0;

			// 조건에 만족하는 DE만 찾음
			// [en] find DE satisfying condition
			r = ffat_dir_getDirEntry(pVol, pNodeParent, &stNodeDeTemp, FFAT_TRUE, FFAT_TRUE, pCxt);
			if (r < 0)
			{
				if ((r == FFAT_EEOF) || (r == FFAT_ENOENT))
				{
					// 조건에 맞는 파일이 없는 경우
					// [en] there is no file satisfying condition
					goto next_update_nt;
				}
				else if (r == FFAT_EXDE)
				{
					r = FFAT_EFAT;
				}

				FFAT_ASSERT(0);
				goto out;
			}

			FFAT_ASSERT(r == FFAT_OK);
			
			// LFN only일 경우 SFN만 리턴될 수 없다.
			// [en] in case of using only LFN, only SFN can not return
			FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (stNodeDeTemp.dwEntryCount > 1));

			FFAT_MEMCPY(pNodeDE, &stNodeDeTemp, sizeof(FatGetNodeDe));

			r = FFAT_DONE;
			goto out;

next_update_nt:
			FFAT_ASSERT(stNodeDeTemp.dwCluster == NODE_COP(pNodeChild));

			// update numeric tail
			r = _dec_updateNumericTail(pNodeChild, dwFlag, pNumericTail, pDECEntry, NULL);
			FFAT_EO(r, (_T("fail to update numeric tail")));

next_update_freede:
			// update free DE
			r = _dec_updateFreeDe(pNodeChild, dwFlag, dwNextStartIndex, dwCurEntryIndex,
							dwRequiredEntryCount, pCxt);
			FFAT_EO(r, (_T("fail to update free DE")));

			dwCurEntryCount = (t_uint8) ESS_MATH_CD(_DECE_NAME_LENGTH(pDECEntry), FAT_LFN_NAME_CHAR);
			if (_DECE_IS_LFN(pDECEntry) == FFAT_TRUE)
			{
				// long file name, increase for short file name entry
				dwCurEntryCount++;
			}

			if ((VOL_FLAG(pVol) & VOL_ADDON_XDE) &&
				(_DECE_IS_VOLUME_NAME(pDECEntry) == FFAT_FALSE))	// volume name entry does not have XDE
			{
				// Extended DE를 사용할 경우, Volume is mounted with XDE flag
				// [en] in case of using Extended DE, Volume is mounted with XDE flag
				dwCurEntryCount++;
			}

			dwNextStartIndex = dwCurEntryIndex + dwCurEntryCount;
next:
			if (dwCurEntryIndex == pDECNode->wLastEntryIndex)
			{
				FFAT_ASSERT(pDECInfo->stListDECInfo.pNext == &(pDECNode->stHeadDECInfo));
				FFAT_ASSERT(pDECInfo == ESS_DLIST_GET_ENTRY(ESS_DLIST_GET_TAIL(&(pDECNode->stHeadDECInfo)), _DECInfo, stListDECInfo));
				FFAT_ASSERT(pDECEntry == pDECNode->pLastDECEntry);

				goto build_from_last;
			}

			pDECEntry++;
		}
	}

	//===========================================================================
	// build and lookup part

build_from_last:
	FFAT_ASSERT(dwCurEntryIndex == pDECNode->wLastEntryIndex);
	FFAT_ASSERT(pDECNode->wLastEntryIndex + dwCurEntryCount > 0);

	// set start offset
	stNodeDeTemp.dwOffset	= ((pDECNode->wLastEntryIndex + dwCurEntryCount) << FAT_DE_SIZE_BITS);

	FFAT_ASSERT((pDECNode->wLastEntryIndex + dwCurEntryCount) == dwNextStartIndex);

build:
	// allocate memory for node name
	psCurName = FFAT_LOCAL_ALLOC(FFAT_NAME_BUFF_SIZE, pCxt);
	FFAT_ASSERT(psCurName);

	// get all directory entry
	stNodeDeTemp.dwTargetEntryCount	= 0;
	stNodeDeTemp.psName				= NULL;
	stNodeDeTemp.dwNameLen			= 0;
	stNodeDeTemp.psShortName		= NULL;
	stNodeDeTemp.bExactOffset		= FFAT_FALSE;
	stNodeDeTemp.dwClusterOfOffset	= 0;			// init cluster of offset

	// build directory entry
	do
	{
		// get directory entry
		r = ffat_dir_getDirEntry(pVol, pNodeParent,
						&stNodeDeTemp, FFAT_FALSE, FFAT_TRUE, pCxt);
		if (r < 0)
		{
			// Node를 찾지 못했거나 에러 발생.
			// [en] it can not find node or occurs error
			if (r == FFAT_EEOF)
			{
				if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME)) &&
					(pNodeChild->dwFlag & NODE_NAME_NUMERIC_TAIL))
				{
					// update numeric tail
					r = ffat_node_insertNumericTail(pNodeChild, pNumericTail);
					IF_UK (r < 0)
					{
						FFAT_LOG_PRINTF((_T("Fail to insert numeric tail")));
						goto out;
					}

					if (r == FFAT_OK1)
					{
						// initialize NumericTail
						ffat_node_initNumericTail(pNumericTail, pNumericTail->wMax + 1);

						r = FFAT_OK;
						goto set_offset;
					}

					if (pNodeChild->bSfnNameSize < 8)
					{
						// update byte count of SFN entry
						pNodeChild->bSfnNameSize = (t_int8)_getSfnNameSize(pVol, pNodeChild->stDE.sName);
					}
				}

				if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) &&
					(pNodeChild->stDeInfo.dwFreeCount == 0))
				{
					// update free DE
					r = _dec_updateFreeDeLast(pNodeChild, dwNextStartIndex, dwRequiredEntryCount, pCxt);
					FFAT_EO(r, (_T("fail to update free de")));
				}

				if (dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME))
				{
					FFAT_MEMCPY(pNodeDE, &stNodeDeTemp, sizeof(FatGetNodeDe));
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
		// [en] in case of using only LFN, only SFN can not return
		FFAT_ASSERT(((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) ? FFAT_TRUE : (stNodeDeTemp.dwEntryCount > 1));

		dwCurEntryIndex = stNodeDeTemp.dwDeStartOffset >> FAT_DE_SIZE_BITS;

#ifdef FFAT_VFAT_SUPPORT
		// get long file name
		r = FFATFS_GenNameFromDirEntry(pVolInfo, stNodeDeTemp.pDE, stNodeDeTemp.dwEntryCount,
										psCurName, &dwCurNameLen, FAT_GEN_NAME_LFN);
#else
		r = FFATFS_GenNameFromDirEntry(pVolInfo, stNodeDeTemp.pDE, stNodeDeTemp.dwEntryCount,
										psCurName, &dwCurNameLen, FAT_GEN_NAME_SFN);
#endif
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("fail to generate name from DE")));
			goto out;
		}

		if (stNodeDeTemp.dwEntryCount >= 2)
		{
			bLFN = FFAT_TRUE;
		}
		else
		{
			bLFN = FFAT_FALSE;
		}

		// psCurName을 이용하여 dwNamaPartLen 구함
		// [en] dwNamaPartLen can be got by using psCurName
		// stNodeDeTemp.pDE 를 이용하여 dwSfnNameSize 구함
		// [en] dwSfnNameSize can be got by using stNodeDeTemp.pDE
		dwNamaPartLen	= _getNamePartLen(pVol, psCurName, dwCurNameLen);
		dwSfnNameSize	= _getSfnNameSize(pVol, stNodeDeTemp.pDE[stNodeDeTemp.dwEntryCount - 1].sName);

		r = _insertEntry(pNodeParent, &pDECNode, dwBlockEntryCount, psCurName,
					dwCurNameLen, dwNamaPartLen, dwSfnNameSize, dwCurEntryIndex,
					&stNodeDeTemp.pDE[stNodeDeTemp.dwEntryCount - 1], bLFN, &pDECEntry);
		if (r == FFAT_ENOSPC)
		{
			r = FFAT_OK;
			goto set_offset;
		}
		FFAT_EO(r, (_T("fail to insert entry into dec")));

		if ((dwFlag & FFAT_LOOKUP_FREE_DE) == 0)
		{

			// check volume label
			if (stNodeDeTemp.dwEntryCount == 1)
			{
				FFAT_ASSERT(stNodeDeTemp.pDE[0].bAttr != FFAT_ATTR_LONG_NAME);
				if (stNodeDeTemp.pDE[0].bAttr & FFAT_ATTR_VOLUME)
				{
					// ignore volume label
					FFAT_ASSERT(NODE_IS_ROOT(pNodeParent) == FFAT_TRUE);
					// make the name comparing below failed intentionally
					dwCurNameLen = 0;
					//break;[CQ25783]
				}
			}

			// 대소문자 구분을 안 할 경우 (FAT 호환)
			// [en] in case of no case-sensitive (FAT compatibility) 
			// LFN과 SFN를 모두 비교 (psName is a string translated into upper character)
			// [en] comparing LFN with SFN (psName is a string translated into upper character)

			if ((wLfnLastChar == pDECEntry->wLfnLastChar) &&
				(wLfn2ndLastChar == pDECEntry->wLfn2ndLastChar) &&
				(wLfnFirstChar == pDECEntry->wLfnFirstChar) &&
				(dwLen == dwCurNameLen) &&
				// 대소문자 구분을 안 할 경우와 할 경우가 구분됨
				// [en] distinguishing case-sensitive and no case-sensitive
				(((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0) ? (FFAT_WCSUCMP(psName, psCurName) == 0) : (FFAT_WCSCMP(psName, psCurName) == 0)))
			{
				// lookupForCreate 에서는 pNodeChild에 정보를 채워주어야 한다
				// [en] pNodeChild should be filled with lookupForCreate
				// we found it
				FFAT_MEMCPY(pNodeDE, &stNodeDeTemp, sizeof(FatGetNodeDe));

				FFAT_DEBUG_DEC_PRINTF((_T("found name:%s"), ffat_debug_w2a(psCurName, NODE_VOL(pNodeParent))));

				r = FFAT_DONE;
				goto out;
			}

			if (((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0) &&
				(stNodeDeTemp.dwEntryCount > 1) &&
				(bSfnLastChar == pDECEntry->bSfnLastChar) &&
				(bSfn2ndLastChar == pDECEntry->bSfn2ndLastChar) &&
				(bSfnFirstChar == pDECEntry->bSfnFirstChar))
			{
				FFAT_ASSERT((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0);

				// get short file name
				r = FFATFS_GenNameFromDirEntry(pVolInfo, stNodeDeTemp.pDE, stNodeDeTemp.dwEntryCount,
								psCurName, &dwCurNameLen, FAT_GEN_NAME_SFN);
				IF_UK (r < 0)
				{
					FFAT_LOG_PRINTF((_T("fail to generate name from DE")));
					goto out;
				}

				if ((dwLen == dwCurNameLen) && (FFAT_WCSUCMP(psName, psCurName) == 0))
				{
					// we found it
					FFAT_MEMCPY(pNodeDE, &stNodeDeTemp, sizeof(FatGetNodeDe));

					FFAT_DEBUG_DEC_PRINTF((_T("found name:%s"), ffat_debug_w2a(psCurName, NODE_VOL(pNodeParent))));
					r = FFAT_DONE;
					goto out;
				}
			}
		}

		// set next read point
		stNodeDeTemp.dwOffset			= stNodeDeTemp.dwDeEndOffset + FAT_DE_SIZE;

		if (stNodeDeTemp.dwOffset & VOL_CSM(pVol))
		{
			stNodeDeTemp.dwClusterOfOffset	= stNodeDeTemp.dwDeEndCluster;
		}
		else
		{
			stNodeDeTemp.dwClusterOfOffset = 0;
		}

		FFAT_ASSERT(((stNodeDeTemp.dwDeStartOffset >> FAT_DE_SIZE_BITS) + stNodeDeTemp.dwTotalEntryCount) == ((stNodeDeTemp.dwDeEndOffset >> FAT_DE_SIZE_BITS) + 1));

		// update numeric tail
		// to use short file name
		r = _dec_updateNumericTail(pNodeChild, dwFlag, pNumericTail, NULL, &stNodeDeTemp.pDE[stNodeDeTemp.dwEntryCount - 1]); 
		FFAT_EO(r, (_T("fail to update numeric tail")));

		// update free DE
		r = _dec_updateFreeDe(pNodeChild, dwFlag, dwNextStartIndex, dwCurEntryIndex, dwRequiredEntryCount, pCxt);
		FFAT_EO(r, (_T("fail to update free de")));

		dwNextStartIndex = dwCurEntryIndex + stNodeDeTemp.dwTotalEntryCount;
	} while (1);

set_offset:
	FFAT_ASSERT(r == FFAT_OK);

	// set next lookup offset to the last entry
	FFAT_ASSERT(pNodeDE->dwCluster == stNodeDeTemp.dwCluster);
	pNodeDE->dwCluster		= stNodeDeTemp.dwCluster;
	pNodeDE->dwOffset		= stNodeDeTemp.dwOffset;
	pNodeDE->dwClusterOfOffset	= stNodeDeTemp.dwClusterOfOffset;

out:
	FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));

// debug begin
#ifdef _DEC_DEBUG
	_dec_checkDECNodeLastDECEntry();
#endif
// debug end

	// free memory for directory entry
	FFAT_LOCAL_FREE(psCurName, FFAT_NAME_BUFF_SIZE, pCxt);

	return r;
}


/**
 * insert entry in DEC node
 *
 * @param		ppDECNode			: [IN/OUT] DEC node pointer
 * @param		dwBlockEntryCount	: [IN] count of DEC entries in one DEC info
 * @param		psName				: [IN] name
 * @param		dwNameLen			: [IN] character count of name
 * @param		dwNamaPartLen		: [IN] character count of name part of long filename
 * @param		dwSfnNameSize		: [IN] byte size of name part of short filename
 * @param		dwEntryIndex		: [IN] inserted entry index
 * @param		pDeSFN				: [IN] directory entry of short file name
 * @param		bLFN				: [IN] long or short file name flag
 * @param		ppDECEntry			: [OUT] address of inserted DEC entry
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static FFatErr
_insertEntry(Node* pNodeParent, DECNode** ppDECNode, t_int32 dwBlockEntryCount, t_wchar* psName,
				t_int32 dwNameLen, t_int32 dwNamaPartLen, t_int32 dwSfnNameSize, t_int32 dwEntryIndex,
				FatDeSFN* pDeSFN, t_boolean bLFN, DECEntry** ppDECEntry)
{
	FFatErr		r;

	_DECInfo*	pDECInfoCur;
	_DECInfo*	pDECInfoTemp		= NULL;
	_DECInfo*	pDECInfoTemp2		= NULL;

	DECEntry*	pDECEntryCur;
	DECEntry*	pDECEntryEmpty		= NULL;
	DECEntry*	pDECEntryMoveStart	= NULL;		// DEC entry가 이동될 경우, 시작 DEC entry
												// [en] in case of moving DEC entry, start of DEC entry
	DECNode*	pDECNodeOld			= NULL;

	EssDList*	pstListMovedStart	= NULL;		// DEC entry가 이동될 경우, 시작 DEC entry가 있는 DEC Info의 list
												// [en] in case of moving DEC entry, 
												//		DEC Info's list including start of DEC entry
	EssDList*	pstListTemp;

	t_int32		dwIndex;
	t_int32		dwCurEntryIndex;

	t_boolean	bFindOnlyEmpty = FFAT_FALSE;
	t_boolean	bInsertLast = FFAT_FALSE;

	FFAT_ASSERT(*ppDECNode);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pDeSFN);
	FFAT_ASSERT((*ppDECNode)->wValidEntryCount != -1);

	FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

	// loop DEC Info list
	ESS_DLIST_FOR_EACH_ENTRY(_DECInfo, pDECInfoCur, &((*ppDECNode)->stHeadDECInfo), stListDECInfo)
	{
		FFAT_ASSERT(dwEntryIndex != pDECInfoCur->wLastEntryIndex);

		if ((dwEntryIndex > pDECInfoCur->wLastEntryIndex) ||
				(pDECInfoCur->wLastEntryIndex == _MASK_ENTRY_INDEX))
		{
			// 추가될 entry의 index가 현재 DEC Info의 마지막 entry의 index보다 큰 경우
			// [en] in case index of adding entry is larger than index of last entry in current DEC Info
			FFAT_ASSERT(bFindOnlyEmpty == FFAT_FALSE);

			if (pDECInfoCur->stListDECInfo.pNext != &((*ppDECNode)->stHeadDECInfo))
			{
				// 현재 DEC Info가 DEC Info list의 마지막이 아닌 경우 다음 DEC Info로
				// [en] in case current DEC Info is different from the last of DEC Info list,
				//      move next DEC Info
				continue;
			}

			bInsertLast = FFAT_TRUE;

			// in case of last DEC Info
			if (pDECInfoCur->wValidEntryCount == dwBlockEntryCount)
			{
				if (_IS_ACTIVATED_UDEC(pNodeParent) == FFAT_TRUE)
				{
					FFAT_LOG_PRINTF((_T("User DEC is full")));
					r = FFAT_ENOSPC;
					goto out;
				}

				FFAT_ASSERT(_IS_ACTIVATED_GDEC(pNodeParent) == FFAT_TRUE);

				pDECNodeOld = *ppDECNode;

				// 마지막 DEC Info가 꽉찬 경우
				// [en] in case of fulling last DEC Info
				// entry가 추가되고 난 후, 빈 entry가 없는 경우
				// [en] in case there is no empty entry after adding entry
				pDECInfoTemp = _gdec_expandGDECNode(ppDECNode);
				if (pDECInfoTemp == NULL)
				{
					// There is no space in (DEC - 1)
					r = FFAT_ENOSPC;
					goto out;
				}

				pDECInfoCur = pDECInfoTemp;

				if (pDECNodeOld != *ppDECNode)
				{
					FFAT_ASSERT(pDECEntryMoveStart == NULL);
					FFAT_ASSERT(pstListMovedStart == NULL);
				}
			}
		}

		FFAT_DEBUG_DEC_PRINTF((_T("dwEntryIndex[%d]\n"), dwEntryIndex));
		FFAT_DEBUG_DEC_PRINTF((_T("pDECInfoCur->wLastEntryIndex[%d]\n"), pDECInfoCur->wLastEntryIndex));

		pDECEntryCur = _FIRST_ENTRY_OF_INFO(pDECInfoCur);

		// loop DEC Entry
		for (dwIndex = 0; dwIndex < dwBlockEntryCount; dwIndex++)
		{
			FFAT_DEBUG_DEC_PRINTF((_T("_DECE_ENTRY_INDEX(pDECEntryCur)[%d]\n"), _DECE_ENTRY_INDEX(pDECEntryCur)));
         // Fix done on 02/03/2012 by Utkarsh Pandey for Empty DECEntry check
                        if ((pDECEntryCur->wLfnFirstChar == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntryCur->bSfnFirstChar  == 0xFF)&&
                           (pDECEntryCur->wLfnLastChar  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntryCur->wLfn2ndLastChar  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntryCur->bSfnLastChar  == 0xFF)&&
                           (pDECEntryCur->bSfn2ndLastChar  == 0xFF)&&
                           (pDECEntryCur->bFirstExtention  == 0xFF)&&
                           (pDECEntryCur->wEntryIndex  == _MASK_FIRST_NAME_CHAR)&&
                           (pDECEntryCur->dwNameLength2NT  == 0xFFFFFFFF))
			{
				// in case of empty entry
				pDECEntryEmpty = pDECEntryCur;

				if (bFindOnlyEmpty == FFAT_TRUE)
				{
					FFAT_ASSERT(pDECEntryMoveStart != NULL);

					// 이전에 빈 entry가 없고, entry를 중간에 추가해야 하는 경우
					// [en] in case entry should be added in the middle, 
					//      because there is no empty entry by previous entries
					goto move_forward;
				}

				if ((pDECEntryEmpty == (*ppDECNode)->pLastDECEntry + 1) ||
					(pDECInfoCur->wLastEntryIndex == _MASK_ENTRY_INDEX))
				{
					// 이전에 빈 entry가 없고, DEC Info의 끝에 entry 추가되는 경우
					// [en] in case entry should be added the last of DEC Info, 
					//      because there is no empty entry by previous entries
					FFAT_ASSERT(bInsertLast == FFAT_TRUE);
					FFAT_ASSERT(pstListMovedStart == NULL);

					// DEC Info의 마지막에 entry를 추가
					// [en] add entry the last of DEC Info
					_storeEntry(NODE_VOL(pNodeParent), pDECEntryEmpty, psName, dwNameLen,
								dwNamaPartLen, dwSfnNameSize, dwEntryIndex, pDeSFN, bLFN);
					*ppDECEntry = pDECEntryEmpty;

					pDECInfoCur->wValidEntryCount++;
					pDECInfoCur->wLastEntryIndex	= (t_uint16)dwEntryIndex;

					FFAT_ASSERT(pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo));

					(*ppDECNode)->wValidEntryCount++;
					(*ppDECNode)->pLastDECEntry		= pDECEntryEmpty;
					(*ppDECNode)->wLastEntryIndex	= (t_uint16)dwEntryIndex;

					FFAT_ASSERT(pDECInfoCur->wValidEntryCount >= 0);
					FFAT_ASSERT(pDECInfoCur->wValidEntryCount <= dwBlockEntryCount);

					FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

					r = FFAT_OK;
					goto out;
				}

				if ((pDECEntryMoveStart == NULL) && (bInsertLast == FFAT_TRUE))
				{
					// 마지막 DEC Info에 빈 entry가 있고, DEC Info의 끝에 entry 추가되는 경우
					// [en] in case entry should be added the last of DEC Info, 
					//      because there is empty entry at last DEC Info

					// 빈 entry 다음부터 DEC Info의 마지막 entry까지 한 entry씩 이동
					// [en] move each one entry from the next of empty entry to last entry of DEC Info
					FFAT_ASSERT((*ppDECNode)->pLastDECEntry > pDECEntryEmpty);
					_moveDECEntries(pDECEntryEmpty, pDECEntryEmpty + 1,
									(t_int32)((*ppDECNode)->pLastDECEntry - pDECEntryEmpty));

					// DEC Info의 마지막에 entry를 추가
					// [en] add entry the last of DEC Info
					_storeEntry(NODE_VOL(pNodeParent), (*ppDECNode)->pLastDECEntry, psName, dwNameLen,
								dwNamaPartLen, dwSfnNameSize, dwEntryIndex, pDeSFN, bLFN);
					*ppDECEntry = (*ppDECNode)->pLastDECEntry;

					pDECInfoCur->wValidEntryCount++;
					pDECInfoCur->wLastEntryIndex	= (t_uint16)dwEntryIndex;

					FFAT_ASSERT(pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo));

					(*ppDECNode)->wValidEntryCount++;
					(*ppDECNode)->wLastEntryIndex	= (t_uint16)dwEntryIndex;

					FFAT_ASSERT(pDECInfoCur->wValidEntryCount >= 0);
					FFAT_ASSERT(pDECInfoCur->wValidEntryCount <= dwBlockEntryCount);

					FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

					r = FFAT_OK;
					goto out;
				}

				// empty or invalid entry
				goto next;
			}

			if ((bFindOnlyEmpty == FFAT_TRUE) || (bInsertLast == FFAT_TRUE))
			{
				goto next;
			}

			dwCurEntryIndex = _DECE_ENTRY_INDEX(pDECEntryCur);

			FFAT_ASSERT(dwCurEntryIndex >= 0 && dwCurEntryIndex <= _MASK_ENTRY_INDEX);

			FFAT_ASSERT(dwEntryIndex != dwCurEntryIndex);

			// comparing with entry index 
			if (dwEntryIndex < dwCurEntryIndex)
			{
				// 현재 entry의 앞에 entry를 추가해야 한다
				// [en] entry should be added in front of current entry
				if (pDECEntryEmpty != NULL)
				{
					// 이전에 빈 entry가 있었던 경우,빈 entry 다음부터 현재 entry까지 한 entry씩 이동
					// [en] in case there was empty entry previously,  
					//      move each one entry from the next of empty entry to current entry 
					if (pDECEntryCur > pDECEntryEmpty + 1)
					{
						_moveDECEntries(pDECEntryEmpty, pDECEntryEmpty + 1,
										(t_int32)(pDECEntryCur - pDECEntryEmpty - 1));
					}
// debug begin
#ifdef FFAT_DEBUG
					else
					{
						FFAT_ASSERT(pDECEntryEmpty == pDECEntryCur - 1);
					}
#endif
// debug end

					// add entry
					_storeEntry(NODE_VOL(pNodeParent), pDECEntryCur - 1, psName, dwNameLen, dwNamaPartLen,
								dwSfnNameSize, dwEntryIndex, pDeSFN, bLFN);
					*ppDECEntry = pDECEntryCur - 1;

					pDECInfoCur->wValidEntryCount++;
					(*ppDECNode)->wValidEntryCount++;

					FFAT_ASSERT(pDECInfoCur->wValidEntryCount >= 0);
					FFAT_ASSERT(pDECInfoCur->wValidEntryCount <= dwBlockEntryCount);

					r = FFAT_OK;
					goto out;
				}
				else
				{
					// 이전에 빈 entry가 없었던 경우, 빈 entry를 계속 찾음
					// [en] in case there was no empty entry previously, 
					//		it find empty entry continuously
					pDECEntryMoveStart	= pDECEntryCur;
					bFindOnlyEmpty		= FFAT_TRUE;
					pstListMovedStart	= &(pDECInfoCur->stListDECInfo);
				}
			}

next:
			pDECEntryCur++;
		}

		FFAT_ASSERT(pDECEntryEmpty == NULL);
	}

	if (_IS_ACTIVATED_UDEC(pNodeParent) == FFAT_TRUE)
	{
		// User DEC is full
		r = FFAT_ENOSPC;
		goto out;
	}

	FFAT_ASSERT(_IS_ACTIVATED_GDEC(pNodeParent) == FFAT_TRUE);

	// expand DEC Node
	FFAT_ASSERT(pDECEntryMoveStart != NULL);
	FFAT_ASSERT(pstListMovedStart != NULL);

	pDECNodeOld = *ppDECNode;

	// entry가 추가 될 곳 이후에 빈 entry가 없는 경우
	// [en] in case there is no empty entry after adding entry
	pDECInfoTemp = _gdec_expandGDECNode(ppDECNode);
	if (pDECInfoTemp == NULL)
	{
		// DEC가 꽉 채워졌지만 entry를 중간에 추가해야 한다 --> 그래서 마지막 entry를 지워야 한다
		// [en] entry should be added in the middle, however, DEC was already full
		//      therefore, last entry must be deleted

		EssDList*	pstListPrev = ESS_DLIST_GET_PREV(&(pDECInfoCur->stListDECInfo));

		// There is no space in DEC
		pDECInfoCur		= ESS_DLIST_GET_ENTRY(pstListPrev, _DECInfo, stListDECInfo);
		pDECEntryEmpty	= _LAST_ENTRY_OF_GDEC_INFO(pDECInfoCur);

		FFAT_ASSERT(pDECInfoCur->wLastEntryIndex == _DECE_ENTRY_INDEX(pDECEntryEmpty));

		pDECInfoCur->wValidEntryCount--;
		pDECInfoCur->wLastEntryIndex = _DECE_ENTRY_INDEX(pDECEntryEmpty - 1);

		FFAT_ASSERT(pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo));

		(*ppDECNode)->wValidEntryCount--;
		(*ppDECNode)->wLastEntryIndex = pDECInfoCur->wLastEntryIndex;
		(*ppDECNode)->pLastDECEntry = pDECEntryEmpty - 1;

		FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));
	}
	else
	{
		pDECInfoCur		= pDECInfoTemp;
		pDECEntryEmpty	= _FIRST_ENTRY_OF_INFO(pDECInfoTemp);
	}

	if (pDECNodeOld != *ppDECNode)
	{
		// current DEC node가 이동된 경우
		// [en] in case of moving current DEC node
		if (((t_uint8*)pDECEntryMoveStart >= (t_uint8*)pDECNodeOld) &&
			((t_uint8*)pDECEntryMoveStart <= (t_uint8*)pDECNodeOld + _SIZE_GDEC_BASE))
		{
			// 첫번째 DEC info에 있어 이동된 경우
			// [en] in case of moving first DEC info
			pDECEntryMoveStart = (DECEntry*)((t_uint8*)pDECEntryMoveStart - (t_uint8*)pDECNodeOld + (t_uint8*)*ppDECNode);
		}

		if (((t_uint8*)pstListMovedStart >= (t_uint8*)pDECNodeOld) &&
			((t_uint8*)pstListMovedStart <=  (t_uint8*)pDECNodeOld + _SIZE_GDEC_BASE))
		{
			// 첫번째 DEC info에 있어 이동된 경우
			// [en] in case of moving first DEC info
			pstListMovedStart = (EssDList*)((t_uint8*)pstListMovedStart - (t_uint8*)pDECNodeOld + (t_uint8*)*ppDECNode);
		}
	}

move_forward:
	// pDECEntryMoveStart 부터 pDECEntryEmpty 이전까지 entry들을 한 entry씩 이동하고
	// pDECEntryMoveStart 위치에 새로운 entry를 추가
	// [en] entries move each one entry from pDECEntryMoveStart to pDECEntryEmpty 
	//      and new entry is added at location of pDECEntryMoveStart

	FFAT_ASSERT((t_uint8*)pDECEntryMoveStart >= (t_uint8*)ESS_DLIST_GET_ENTRY(pstListMovedStart, _DECInfo, stListDECInfo) + sizeof(_DECInfo));
	FFAT_ASSERT(pDECEntryEmpty != (*ppDECNode)->pLastDECEntry);
	FFAT_ASSERT(dwEntryIndex != (*ppDECNode)->wLastEntryIndex);
	FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

	if (pstListMovedStart == &(pDECInfoCur->stListDECInfo))
	{
		// entry가 추가될 곳과 빈 entry가 같은 entry block에 있는 경우 (빈 entry가 뒤에 있음)
		// [en] in case of same entry block both adding entry and empty entry (there is empty entry behind)
		FFAT_ASSERT(pDECEntryEmpty != _FIRST_ENTRY_OF_INFO(pDECInfoCur));

		if (pDECEntryEmpty != pDECEntryMoveStart)
		{
			// 빈 entry 뒤에 entry가 있는 경우
			// [en] there is entry after empty entry
			FFAT_ASSERT(pDECEntryEmpty > pDECEntryMoveStart);
			_moveDECEntries(pDECEntryMoveStart + 1, pDECEntryMoveStart,
							(t_int32)(pDECEntryEmpty - pDECEntryMoveStart));
		}

		// 빈 entry가 마지막인 경우
		// [en] in case empty entry is last one
		FFAT_ASSERT((pDECEntryEmpty != pDECEntryMoveStart) ? FFAT_TRUE : (pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo)));	// 마지막 DEC info 이다

		_storeEntry(NODE_VOL(pNodeParent), pDECEntryMoveStart, psName, dwNameLen,
					dwNamaPartLen, dwSfnNameSize, dwEntryIndex, pDeSFN, bLFN);
		*ppDECEntry = pDECEntryMoveStart;

		pDECInfoCur->wValidEntryCount++;
		(*ppDECNode)->wValidEntryCount++;

		if (pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo) &&
			((*ppDECNode)->pLastDECEntry < pDECEntryEmpty))
		{
			// 마지막 entry block 이고, last entry 뒤에 추가된 경우
			// [en] there is last entry block and in case it is added after last entry
			(*ppDECNode)->pLastDECEntry = pDECEntryEmpty;
		}

		FFAT_ASSERT(pDECInfoCur->wLastEntryIndex != _MASK_ENTRY_INDEX);

		if (dwEntryIndex > pDECInfoCur->wLastEntryIndex)
		{
			FFAT_ASSERT((*ppDECNode)->wLastEntryIndex == pDECInfoCur->wLastEntryIndex);
			FFAT_ASSERT(pDECEntryEmpty == pDECEntryMoveStart);
			FFAT_ASSERT(dwEntryIndex == _DECE_ENTRY_INDEX(pDECEntryEmpty));
			
			pDECInfoCur->wLastEntryIndex = (t_uint16)dwEntryIndex;

			if (pDECInfoCur->stListDECInfo.pNext == &((*ppDECNode)->stHeadDECInfo))
			{
				FFAT_ASSERT((*ppDECNode)->pLastDECEntry == pDECEntryEmpty);

				// 마지막 entry block 인 경우
				// [en] in case of last entry block
				(*ppDECNode)->wLastEntryIndex = (t_uint16)dwEntryIndex;
			}
		}

		FFAT_ASSERT(pDECInfoCur->wValidEntryCount >= 0);
		FFAT_ASSERT(pDECInfoCur->wValidEntryCount <= dwBlockEntryCount);
		FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

		r = FFAT_OK;
		goto out;
	}

	FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

	FFAT_ASSERT(pstListMovedStart);
	// entry가 추가될 곳과 빈 entry가 다른 entry block에 있는 경우
	// entry block사이에 entry 이동이 필요함
	// [en] in case of different entry block both adding entry and empty entry 
	//      it needs moving entry between entry blocks
	for (pstListTemp = &(pDECInfoCur->stListDECInfo); pstListTemp != pstListMovedStart->pPrev; pstListTemp = pstListTemp->pPrev)
	{
		pDECInfoTemp = ESS_DLIST_GET_ENTRY(pstListTemp, _DECInfo, stListDECInfo);

		if (pstListTemp == &(pDECInfoCur->stListDECInfo))
		{
			// 마지막 entry block인 경우
			// [en] in case of last entry block
			DECEntry*	pFirstEntry	= _FIRST_ENTRY_OF_INFO(pDECInfoTemp);

			if (pDECEntryEmpty != pFirstEntry)
			{
				// entry가 있는 경우
				// [en] in case there is entry
				FFAT_ASSERT(pDECEntryEmpty > pFirstEntry);

				// 현재 entry block의 first entry부터 previous entry of empty entry를 한 entry씩 이동
				// [en] previous entry of empty entry is moved each one entry from current first entry of entry block
				_moveDECEntries(pFirstEntry + 1, pFirstEntry,
								(t_int32)(pDECEntryEmpty - pFirstEntry));

				FFAT_ASSERT(pDECInfoCur->wLastEntryIndex == _DECE_ENTRY_INDEX(_getLastDECEntry(pDECInfoCur, _COUNT_GDEC_BASE_ENTRY - 1)));
			}
			else if (pstListTemp->pNext == &((*ppDECNode)->stHeadDECInfo))
			{
				// 새로 할당받아 entry가 없는 경우 (마지막 DEC info이다)
				// [en] in case there is no entry, because it is allocated newly. (this is last DEC info)
				pDECInfoTemp->wLastEntryIndex = (*ppDECNode)->wLastEntryIndex;
			}

			pDECInfoCur->wValidEntryCount++;
		}
		else if (pstListTemp == pstListMovedStart)
		{
			// 처음 entry block인 경우
			// [en] in case of first entry block
			DECEntry*	pLastEntry	= _LAST_ENTRY_OF_GDEC_INFO(pDECInfoTemp);

			FFAT_ASSERT(pDECInfoTemp2 != NULL);

			// 현재 entry block의 last entry를 다음 entry block의 first entry에 복사
			// [en] last entry of current entry block is copied at first entry of next entry block
			FFAT_MEMCPY(_FIRST_ENTRY_OF_INFO(pDECInfoTemp2), pLastEntry, sizeof(DECEntry));

			if (pLastEntry != pDECEntryMoveStart)
			{
				// 이동할 entry가 없음
				// [en] there is no moving entry
				FFAT_ASSERT(pLastEntry > pDECEntryMoveStart);
				_moveDECEntries(pDECEntryMoveStart + 1, pDECEntryMoveStart,
								(t_int32)(pLastEntry - pDECEntryMoveStart));
			}

			_storeEntry(NODE_VOL(pNodeParent), pDECEntryMoveStart, psName, dwNameLen, dwNamaPartLen,
						dwSfnNameSize, dwEntryIndex, pDeSFN, bLFN);
			*ppDECEntry = pDECEntryMoveStart;

			pDECInfoTemp->wLastEntryIndex = _DECE_ENTRY_INDEX(pLastEntry);

			(*ppDECNode)->wValidEntryCount++;

			if (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) != (*ppDECNode)->wLastEntryIndex)
			{
				// 빈 entry 뒤에 entry가 없어 last entry의 위치가 변경
				// [en] location of last entry is changed because there is no entry behind empty entry
				(*ppDECNode)->pLastDECEntry = pDECEntryEmpty;

				FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));
			}

			FFAT_ASSERT(pDECInfoCur->wLastEntryIndex == _DECE_ENTRY_INDEX(_getLastDECEntry(pDECInfoCur, _COUNT_GDEC_BASE_ENTRY - 1)));
		}
		else
		{
			// 중간 entry block인 경우
			// [en] in case of middle entry block
			DECEntry*	pFirstEntry	= _FIRST_ENTRY_OF_INFO(pDECInfoTemp);
			DECEntry*	pLastEntry	= _LAST_ENTRY_OF_GDEC_INFO(pDECInfoTemp);

			FFAT_ASSERT(pDECInfoTemp2 != NULL);

			// 현재 entry block의 last entry를 다음 entry block의 first entry에 복사
			// [en] last entry of current entry block is copied at first entry of next entry block
			FFAT_MEMCPY(_FIRST_ENTRY_OF_INFO(pDECInfoTemp2),  pLastEntry, sizeof(DECEntry));

			// 현재 entry block의 first entry부터 last block entry를 한 entry씩 이동
			// [en] last block entry is moved by each one entry from first entry of current entry block
			_moveDECEntries((pFirstEntry + 1), pFirstEntry, (dwBlockEntryCount - 1));
			
			pDECInfoTemp->wLastEntryIndex = _DECE_ENTRY_INDEX(pLastEntry);

			FFAT_ASSERT(pDECInfoCur->wLastEntryIndex == _DECE_ENTRY_INDEX(_getLastDECEntry(pDECInfoCur, _COUNT_GDEC_BASE_ENTRY - 1)));
		}

		pDECInfoTemp2 = pDECInfoTemp;
	}

	r = FFAT_OK;

out:
	FFAT_ASSERT((*ppDECNode)->pLastDECEntry != NULL);
	FFAT_ASSERT(_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex);
	FFAT_ASSERT((*ppDECNode)->wLastEntryIndex != _MASK_ENTRY_INDEX);
	FFAT_ASSERT(((*ppDECNode)->pLastDECEntry == NULL) ? ((*ppDECNode)->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX((*ppDECNode)->pLastDECEntry) == (*ppDECNode)->wLastEntryIndex));

	return r;
}


/**
 * store info in DEC entry
 *
 * @param		pDECEntry		: [IN] address of DEC entry to insert
 * @param		psName			: [IN] name
 * @param		dwNameLen		: [IN] name length
 * @param		dwSfnNameSize	: [IN] byte size of name part of short filename
 * @param		dwEntryIndex	: [IN] entry index to insert
 * @param		pDeSFN			: [IN] directory entry of short file name
 * @param		bLFN			: [IN] long or short file name flag
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 * @version		APR-10-2009 [GwangOk Go]	numeric tail support 23bits & add second last character into entry
 */
static void
_storeEntry(Vol* pVol, DECEntry* pDECEntry, t_wchar* psName, t_int32 dwNameLen, t_int32 dwNamePartLen,
			t_int32 dwSfnNameSize, t_int32 dwEntryIndex, FatDeSFN* pDESFN, t_boolean bLFN)
{
	t_int32		dwNumericTail;

	FFAT_ASSERT(pDECEntry);
	FFAT_ASSERT(psName);
	FFAT_ASSERT((dwNameLen >= 0) && (dwNameLen <= (1 << _LEN_NAME_LENGTH)));
	FFAT_ASSERT(dwEntryIndex >= 0 && dwEntryIndex <= _MASK_ENTRY_INDEX);
	FFAT_ASSERT(pDESFN);
	FFAT_ASSERT((bLFN == FFAT_TRUE) || (bLFN == FFAT_FALSE));
	FFAT_ASSERT(dwNamePartLen >= 1);
	FFAT_ASSERT((dwSfnNameSize >= 1) && (dwSfnNameSize <= FAT_SFN_NAME_PART_LEN));

	if ((VOL_FLAG(pVol) & VOL_CASE_SENSITIVE) == 0)
	{
		// 대소문자 구분을 안 할 경우 -> 대문자로 저장 (FAT호환)
		// [en] in case of no case-sensitive, it stores by upper case. (FAT compatibility) 
		pDECEntry->wLfnFirstChar	= FFAT_TOWUPPER(psName[0]);
		pDECEntry->wLfnLastChar		= FFAT_TOWUPPER(psName[dwNamePartLen - 1]);

		if (dwNamePartLen >= 2)
		{
			pDECEntry->wLfn2ndLastChar	= FFAT_TOWUPPER(psName[dwNamePartLen - 2]);
		}
		else
		{
			pDECEntry->wLfn2ndLastChar	= 0;
		}
	}
	else
	{
		// 대소문자 구분을 할 경우 -> 그냥 저장 (FAT 비호환)
		// [en] in case of case-sensitive, it just stores. (no FAT compatibility) 
		pDECEntry->wLfnFirstChar	= psName[0];
		pDECEntry->wLfnLastChar		= psName[dwNamePartLen - 1];

		if (dwNamePartLen >= 2)
		{
			pDECEntry->wLfn2ndLastChar	= psName[dwNamePartLen - 2];
		}
		else
		{
			pDECEntry->wLfn2ndLastChar	= 0;
		}
	}

	if ((VOL_FLAG(pVol) & VOL_LFN_ONLY) == 0)
	{
		// SFN을 사용할 경우 (FAT호환)
		// [en] in case of using SFN (FAT compatibility)
		pDECEntry->bSfnFirstChar	= pDESFN->sName[0];
		pDECEntry->bSfnLastChar		= pDESFN->sName[dwSfnNameSize - 1];
		pDECEntry->bFirstExtention	= pDESFN->sName[FAT_SFN_NAME_PART_LEN];

		if (dwSfnNameSize >= 2)
		{
			pDECEntry->bSfn2ndLastChar	= pDESFN->sName[dwSfnNameSize - 2];
		}
		else
		{
			pDECEntry->bSfn2ndLastChar	= 0;
		}

		dwNumericTail = FFATFS_GetNumericTail(pDESFN);

		FFAT_ASSERT(dwNumericTail >= 0 && dwNumericTail < (1 << _LEN_NUMERIC_TAIL));
	}
	else
	{
		// SFN을 사용하지 않을 경우 (FAT 비호환)
		// [en] in case of no using SFN (no FAT compatibility)
		pDECEntry->bSfnFirstChar	= 0;
		pDECEntry->bSfnLastChar		= 0;
		pDECEntry->bFirstExtention	= 0;
		pDECEntry->bSfn2ndLastChar	= 0;

		dwNumericTail = 0;
	}

	pDECEntry->wEntryIndex		= (t_uint16)dwEntryIndex;

	_SET_DEC_ENTRY_NL_2NT(pDECEntry, dwNameLen, bLFN, dwNumericTail);

	if (pDESFN->bAttr == FFAT_ATTR_VOLUME)
	{
		// SET VOLUME NAME
		pDECEntry->bSfnFirstChar = pDECEntry->bSfnLastChar = 0xFF;
	}
}


/**
 * get last DEC entry in one entry block
 *
 * @param		pDECInfo		: [IN] DEC info of entry block
 * @param		dwStartIndex	: [IN] starting entry index to find
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static DECEntry*
_getLastDECEntry(_DECInfo* pDECInfo, t_int32 dwStartIndex)
{
	DECEntry*	pDECEntry;

	// FFAT_ASSERT(dwStartIndex >= 0 && dwStartIndex < _COUNT_DEC_ENTRY);

	pDECEntry = _FIRST_ENTRY_OF_INFO(pDECInfo) + dwStartIndex;

	while (dwStartIndex >= 0)
	{
   // Fix done on 02/03/2012 by Utkarsh Pandey for Empty DECEntry check             
                 if (!((pDECEntry->wLfnFirstChar == _MASK_FIRST_NAME_CHAR)&&
                     (pDECEntry->bSfnFirstChar  == 0xFF)&&
                     (pDECEntry->wLfnLastChar  == _MASK_FIRST_NAME_CHAR)&&
                     (pDECEntry->wLfn2ndLastChar  == _MASK_FIRST_NAME_CHAR)&&
                     (pDECEntry->bSfnLastChar  == 0xFF)&&
                     (pDECEntry->bSfn2ndLastChar  == 0xFF)&&
                     (pDECEntry->bFirstExtention  == 0xFF)&&
                     (pDECEntry->wEntryIndex  == _MASK_FIRST_NAME_CHAR)&&
                     (pDECEntry->dwNameLength2NT  == 0xFFFFFFFF)))	
                {  
			return pDECEntry;
		}

		pDECEntry--;
		dwStartIndex--;
	}

	return NULL;
}


/**
 * get DEC node by last entry index
 *
 * @param		pLastDECEntry		: [IN] last DECEntry to find
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 * @version		NOV-03-2008 [GwangOk Go]	change wLastEntryIndex intro pLastDECEntry
 */
static DECNode*
_gdec_getDECNodeByLastEntry(DECEntry* pLastDECEntry)
{
	DECNode*	pDECNode;

	ESS_DLIST_FOR_EACH_ENTRY(DECNode, pDECNode, &(_gGlobalDEC.stHeadDECNode), stListDECNode)
	{
		if (pDECNode->pLastDECEntry == pLastDECEntry)
		{
			return pDECNode;
		}
	}

	return NULL;
}


/**
 * update free directory entry info
 *
 * @param		pNode				: [IN] node pointer
 * @param		dwFlag				: [IN] flag for lookup
 * @param		dwFreeStartIndex	: [IN] start entry index of free directory entry
 * @param		dwCurStartIndex		: [IN] start entry index of current directory entry
 * @param		dwEntryCount		: [IN] required free entry count
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static FFatErr
_dec_updateFreeDe(Node* pNode, FFatLookupFlag dwFlag, t_int32 dwFreeStartIndex,
					t_int32 dwCurStartIndex, t_int32 dwEntryCount, ComCxt* pCxt)
{
	FFatErr		r;
	t_int32		dwFreeEntryCount;

	if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME | FFAT_LOOKUP_FREE_DE)) == 0)
	{
		// No need to update free DE
		return FFAT_OK;
	}

	if (pNode->stDeInfo.dwFreeCount != 0)
	{
		// it already has enough free entry
		return FFAT_OK;
	}

	FFAT_ASSERT(pNode);
	FFAT_ASSERT((dwFreeStartIndex >= 0) && (dwFreeStartIndex < _MASK_ENTRY_INDEX));
	FFAT_ASSERT((dwCurStartIndex >= dwFreeStartIndex) && (dwCurStartIndex < _MASK_ENTRY_INDEX));
	FFAT_ASSERT((dwEntryCount >= 0) && (dwEntryCount < _MASK_ENTRY_INDEX));

	dwFreeEntryCount = dwCurStartIndex - dwFreeStartIndex;

	if (dwFreeEntryCount >= dwEntryCount)
	{
		// there is enough free entry
		FFAT_ASSERT(pNode->stDeInfo.dwFreeCount == 0);

		pNode->stDeInfo.dwFreeOffset	= dwFreeStartIndex << FAT_DE_SIZE_BITS;
		pNode->stDeInfo.dwFreeCount		= dwFreeEntryCount;

		if (NODE_COP(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
		{
			// root directory 일 경우는 root 임을 설정함.
			// [en] in case of root directory, sets as root
			pNode->stDeInfo.dwFreeCluster	= FFATFS_FAT16_ROOT_CLUSTER;
		}
		else
		{
			r = FFATFS_GetClusterOfOffset(NODE_VI(pNode), NODE_COP(pNode),
										pNode->stDeInfo.dwFreeOffset, &pNode->stDeInfo.dwFreeCluster,
										pCxt);
			FFAT_ER(r, (_T("fail to get cluster by offset")));

			IF_UK (FFATFS_IS_EOF(NODE_VI(pNode), pNode->stDeInfo.dwFreeCluster) == FFAT_TRUE)
			{
				FFAT_ASSERT(0);
				pNode->stDeInfo.dwFreeCount = 0;
				return FFAT_OK;
			}
		}
	}

	return FFAT_OK;
}


/**
 * update free directory entry info after last entry lookup
 *
 * @param		pNode				: [IN] node pointer
 * @param		dwFreeStartIndex	: [IN] start entry index of free directory entry
 * @param		dwEntryCount		: [IN] required free entry count
 * @return		FFAT_OK				: success
 * @return		else				: error
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static FFatErr
_dec_updateFreeDeLast(Node* pNode, t_int32 dwFreeStartIndex, t_int32 dwEntryCount, ComCxt* pCxt)
{
	FFatErr		r;
	FatVolInfo*	pVolInfo;
	t_int32		dwFreeEntryCount;
	t_uint32	dwCurCluster;
	t_int32		dwLastEndOffset;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(dwFreeStartIndex >= 0 && dwFreeStartIndex < _MASK_ENTRY_INDEX);
	FFAT_ASSERT(dwEntryCount >= 0 && dwEntryCount < _MASK_ENTRY_INDEX);

	pVolInfo = NODE_VI(pNode);
	FFAT_ASSERT(pVolInfo);

	dwLastEndOffset = dwFreeStartIndex << FAT_DE_SIZE_BITS;

	if (NODE_COP(pNode) == FFATFS_FAT16_ROOT_CLUSTER)
	{
		dwFreeEntryCount = VI_REC(pVolInfo) - dwFreeStartIndex;

		if (dwFreeEntryCount < dwEntryCount)
		{
			pNode->stDeInfo.dwFreeCount = 0;
			return FFAT_OK;
		}

		// root directory 일 경우는 root 임을 설정함.
		// [en] in case of root directory, sets as root
		pNode->stDeInfo.dwFreeCluster = FFATFS_FAT16_ROOT_CLUSTER;
	}
	else
	{
		if (dwLastEndOffset == 0)
		{
			pNode->stDeInfo.dwLastDeOffset	= 0;
		}
		else
		{
			pNode->stDeInfo.dwLastDeOffset	= dwLastEndOffset - FAT_DE_SIZE;
		}

		r = FFATFS_GetClusterOfOffset(pVolInfo, NODE_COP(pNode),
						pNode->stDeInfo.dwLastDeOffset, &dwCurCluster, pCxt);
		FFAT_ER(r, (_T("fail to get cluster by offset")));

		if (FFATFS_IS_EOF(pVolInfo, dwCurCluster) == FFAT_TRUE)
		{
			pNode->stDeInfo.dwFreeCount		= 0;
			pNode->stDeInfo.dwLastDeCluster	= NODE_COP(pNode);

			return FFAT_OK;
		}

		// set last & free cluster
		pNode->stDeInfo.dwLastDeCluster = pNode->stDeInfo.dwFreeCluster = dwCurCluster;

		FFAT_ASSERT((dwLastEndOffset % VI_CS(pVolInfo)) == (dwLastEndOffset & VI_CSM(pVolInfo)));
		FFAT_ASSERT((VI_CS(pVolInfo) - (dwLastEndOffset & VI_CSM(pVolInfo))) % FAT_DE_SIZE == 0);

		if (((dwLastEndOffset & VI_CSM(pVolInfo)) == 0) && (dwLastEndOffset != 0))
		{
			// in case of cluster align
			dwFreeEntryCount = 0;
		}
		else
		{
			dwFreeEntryCount = (VI_CS(pVolInfo) - (dwLastEndOffset & VI_CSM(pVolInfo))) >> FAT_DE_SIZE_BITS;
		}

		while (dwFreeEntryCount < dwEntryCount)
		{
			r = FFATFS_GetNextCluster(pVolInfo, dwCurCluster, &dwCurCluster, pCxt);
			FFAT_ER(r, (_T("fail to get next cluster")));

			if (FFATFS_IS_EOF(pVolInfo, dwCurCluster) == FFAT_TRUE)
			{
				pNode->stDeInfo.dwFreeCount = 0;

				return FFAT_OK;
			}

			if (dwFreeEntryCount == 0)
			{
				pNode->stDeInfo.dwFreeCluster = dwCurCluster;
			}

			dwFreeEntryCount += (VI_CS(pVolInfo) >> FAT_DE_SIZE_BITS);
		}
	}

	pNode->stDeInfo.dwFreeOffset	= dwLastEndOffset;
	pNode->stDeInfo.dwFreeCount		= dwFreeEntryCount;

	return FFAT_OK;
}


/**
 * update numeric tail
 *
 * reuse _updateNumericTail() in ffat_node.c
 *
 * @param		pNode			: [IN] Node poitner
 * @param		dwFlag			: [IN] flag for lookup
 * @param		pNumericTail	: [IN/OUT] numeric tail information
 * @param		pDECE			: [IN] a DEC Entry, may be NULL
 *									   this must not be NULL when pDE is NULL
 * @param		pDE				: [IN] a DE, may be NULL
 *									   this must not be NULL when pDECE is NULL
 * @author		DongYoung Seo
 * @version		AUG-11-2006 [DongYoung Seo] First Writing
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 * @version		02-DEC-2008 [DongYoung Seo] add some parameters for simple code
 */
static FFatErr
_dec_updateNumericTail(Node* pNode, FFatLookupFlag dwFlag, NodeNumericTail* pNumericTail,
						DECEntry* pDECE, FatDeSFN* pDE)
{
	t_int32		dwNumericTail;

	if ((dwFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME)) == 0)
	{
		// THIS IS NOT CREATE OR RENAME
		return FFAT_OK;
	}

	if ((pNode->dwFlag & NODE_NAME_NUMERIC_TAIL) == 0)
	{
		// NO NEED TO GET NUEMRIC TAIL
		return FFAT_OK;
	}

	FFAT_ASSERT(pNumericTail);

	if (pDECE)
	{
		FFAT_ASSERT(_DECE_NUMERIC_TAIL(pDECE) >= 0);
		FFAT_ASSERT(_DECE_NUMERIC_TAIL(pDECE) < (1 << _LEN_NUMERIC_TAIL));

		// get numeric tail for DECE
		dwNumericTail = _DECE_NUMERIC_TAIL(pDECE);
	}
	else
	{
		FFAT_ASSERT(pDE);
		// get numeric tail from DE
		dwNumericTail = FFATFS_GetNumericTail(pDE);
	}

	if (dwNumericTail == 0)
	{
		// there is no numeric tail
		return FFAT_OK;
	}

	FFAT_ASSERT((dwNumericTail >= 0) && (dwNumericTail <= _MASK_NUMERIC_TAIL));

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
 * get DEC node and block entry count
 *
 * @param		pNode				: [IN] node pointer
 * @param		ppDECNode			: [OUT] DEC node
 * @param		pdwBlockEntryCount	: [OUT] entry count in one entry block
 * @return		_AddonDECType		: DEC type
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static _AddonDECType
_getDECNodeAndEntryCount(Node* pNode, DECNode** ppDECNode, t_int32* pdwBlockEntryCount)
{
	UserDEC*	pUserDEC;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(ppDECNode);
	FFAT_ASSERT(pdwBlockEntryCount);
	FFAT_ASSERT(NODE_IS_DIR(pNode) == FFAT_TRUE);

	if (NODE_ADDON(pNode)->dwFlag & ADDON_NODE_UDEC)
	{
		// user dec가 설정되어 있는 경우
		// [en] in case of setting user dec
		pUserDEC			= _NODE_ADDON_UDEC(pNode);

		*ppDECNode			= pUserDEC->pDECNode;
		*pdwBlockEntryCount	= pUserDEC->dwMaxEntryCnt;

		_SET_USER_DEC(pNode);

		return ADDON_DEC_USER;
	}
	else if (_gGlobalDEC.bUseGDEC == FFAT_TRUE)
	{
		*ppDECNode = _gdec_getDECNode(pNode);

		if (*ppDECNode != NULL)
		{
			// move to head of list
			ESS_DLIST_MOVE_HEAD(&(_gGlobalDEC.stHeadDECNode), &((*ppDECNode)->stListDECNode));

			*pdwBlockEntryCount = FFAT_GDEC_BASE_ENTRY_COUNT;

			_SET_GLOBAL_DEC(pNode);

			return ADDON_DEC_GLOBAL;
		}
	}

	// parent node에 DEC node가 할당되어 있지 않은 경우
	// [en] in case of no allocating DEC node at parent node
	return ADDON_DEC_NONE;
}


/**
 * move DEC entries
 *
 * @param		pDesEntry		: [IN] destination DEC entry
 * @param		pSrcEntry		: [IN] source DEC entry
 * @param		dwEntryCount	: [IN] count of DEC entries
 * @return		AddonDECType	: DEC type
 * @author		GwangOk Go
 * @version		JAN-22-2008 [GwangOk Go]	Refactoring DEC module
 */
static void
_moveDECEntries(DECEntry* pDesEntry, DECEntry* pSrcEntry, t_int32 dwEntryCount)
{
	t_int32		dwIndex;

	FFAT_ASSERT(pDesEntry);
	FFAT_ASSERT(pSrcEntry);
	FFAT_ASSERT(pDesEntry != pSrcEntry);
	FFAT_ASSERT(dwEntryCount > 0);

	if (pDesEntry < pSrcEntry)
	{
		for (dwIndex = 0; dwIndex < dwEntryCount; dwIndex++)
		{
			FFAT_MEMCPY(pDesEntry++, pSrcEntry++, sizeof(DECEntry));
		}
	}
	else
	{
		pDesEntry += (dwEntryCount - 1);
		pSrcEntry += (dwEntryCount - 1);

		for (dwIndex = 0; dwIndex < dwEntryCount; dwIndex++)
		{
			FFAT_MEMCPY(pDesEntry--, pSrcEntry--, sizeof(DECEntry));
		}
	}
}


/**
 * get DEC node corresponding to node
 *
 * @param		pNode		: [IN] node pointer
 * @return		DECNode*	: DEC node pointer
 * @author		GwangOk Go
 * @version		JUN-19-2009 [GwangOk Go]	write comment
 */
static DECNode*
_gdec_getDECNode(Node* pNode)
{
	DECNode*	pDECNode;

	FFAT_ASSERT(pNode);

	ESS_DLIST_FOR_EACH_ENTRY(DECNode, pDECNode, &(_gGlobalDEC.stHeadDECNode), stListDECNode)
	{
		if ((NODE_VOL(pNode) == pDECNode->pVol) &&
			(NODE_C(pNode) == pDECNode->dwCluster))
		{
			return pDECNode;
		}
	}

	return NULL;
}


/**
 * get length (character count) of name part of long filename
 *
 * @param		pVol		: [IN] volume pointer
 * @param		psName		: [IN] long filename string
 * @param		dwNameLen	: [IN] long filename length
 * @return		t_int32		: length of name part
 * @author		GwangOk Go
 * @version		JUN-19-2009 [GwangOk Go]	write comment
 */
static t_int32
_getNamePartLen(Vol* pVol, t_wchar* psName, t_int32 dwNameLen)
{
	t_int32		dwIndex;
	t_int32		dwIndex2;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(psName);
	
	//				FAT compatibility	   OS_SPECIFIC
	// test.txt...		4 (test.txt)			4
	// ..test.txt..		6 (..test.txt)			6
	// test..txt.		5 (test..txt)			5
	// ....				invalid					4
	// test.			4 (test)				5
	// test...			4 (test)				7
	// .test			5						5
	// ..test			6						6
	// ...test			7						7
	// ...test...		7						10
	// .test.txt		5						5
	// ..test..txt		7						7
	// ..test...txt		8						8
	// ..test.txt		6						6

	dwIndex = dwNameLen - 1;

	if (VOL_FLAG(pVol) & VOL_OS_SPECIFIC_CHAR)
	{
		// skip tailing dot
		for (/* NONE */; dwIndex >= 0; dwIndex--)
		{
			if (psName[dwIndex] != '.')
			{
				break;
			}
		}

		// check dot & dotdot
		FFAT_ASSERT(((dwIndex < 0) && (dwNameLen <= 2)) == 0);
	}

	for (/* NONE */; dwIndex >= 0; dwIndex--)
	{
		if (psName[dwIndex] == '.')
		{
			break;
		}
	}

	if (dwIndex <= 0)
	{
		// '.'이 없거나 '.'이 첫글자만 있는 경우
		// or '.'으로 있는 경우
		// [en] in case of no '.' or in case the only first character is '.'
		//      or in case there is '.'
		return dwNameLen;
	}

	FFAT_ASSERT(dwIndex != dwNameLen - 1);

	for (dwIndex2 = 0; dwIndex2 <= dwIndex; dwIndex2++)
	{
		if (psName[dwIndex2] != '.')
		{
			break;
		}
	}

	if (dwIndex == dwIndex2 - 1)
	{
		// leading '.'만 있는 경우
		// [en] in case there is leading '.'
		return dwNameLen;
	}

	return dwIndex;
}


/**
 * get name size (byte size) of short filename
 *
 * @param		pVol		: [IN] volume pointer
 * @param		psName		: [IN] short filename string
 * @return		t_int32		: name size
 * @author		GwangOk Go
 * @version		JUN-19-2009 [GwangOk Go]	write comment
 */
static t_int32
_getSfnNameSize(Vol* pVol, t_uint8* pName)
{
	t_int32		dwIndex;

	if (VOL_FLAG(pVol) & VOL_LFN_ONLY)
	{
		// In this case, Dummy SFN is used.
		return FAT_SFN_NAME_PART_LEN;
	}

	for (dwIndex = 0; dwIndex < FAT_SFN_NAME_PART_LEN; dwIndex++)
	{
		if (pName[dwIndex] == ' ')
		{
			break;
		}
	}

	return dwIndex;
}

// debug begin
#ifdef _DEC_DEBUG
	/**
	 * debug function to check pLastDECEntry of DECNode
	 *
	 * @return		FFAT_OK		: success
	 * @return		negative	: fail
	 * @author		GwangOk Go
	 * @version		OCT-31-2008 [GwangOk Go]	First Writing.
	 */
	static FFatErr
	_dec_checkDECNodeLastDECEntry(void)
	{
		DECNode*	pDECNode;
		DECNode*	pDECNodeTemp;

		if (_gGlobalDEC.bUseGDEC == FFAT_FALSE)
		{
			return FFAT_OK;
		}

		// volume에 속하는 DECNode를 reset
		// [en] DECNode including volume is reset
		ESS_DLIST_FOR_EACH_ENTRY_SAFE(DECNode, pDECNode, pDECNodeTemp, &(_gGlobalDEC.stHeadDECNode), stListDECNode)
		{
			FFAT_ASSERT((pDECNode->pLastDECEntry == NULL) ? (pDECNode->wLastEntryIndex == _MASK_ENTRY_INDEX) : (_DECE_ENTRY_INDEX(pDECNode->pLastDECEntry) == pDECNode->wLastEntryIndex));
		}

		return FFAT_OK;
	}
#endif
// debug end


