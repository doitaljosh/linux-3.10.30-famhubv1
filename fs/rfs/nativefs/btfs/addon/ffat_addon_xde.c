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
 * @file		ffat_addon_extended_de.c
 * @brief		Extended Directory Entry
 * @author		GwangOk Go
 * @version		MAY-30-2008 [GwangOk Go] First writing
 * @see			None
 */

#include "ffat_main.h"

#include "ffat_addon_xde.h"
#include "ffat_addon_types_internal.h"

#include "ffatfs_api.h"

// debug begin
//#define _DEBUG_XDE
// debug end

#define BTFS_FILE_ZONE_MASK			(eBTFS_DZM_ADDON_XDE)

#define _IS_XDE_ACTIVATED(_pVol)	((VOL_FLAG(_pVol) & VOL_ADDON_XDE) ? FFAT_TRUE : FFAT_FALSE)

#define _XDE_ATTR_VALUE				0xCF	// invalid long directory entry 처럼 보임
#define _XDE_TYPE_VALUE				0x00

#define _XDE_ATTR_SIZE				1
#define _XDE_TYPE_SIZE				1
#define _XDE_CHECKSUM_SIZE			1

#define _XDE_UPDATE_POSITION		(FFAT_XDE_SIGNATURE_SIZE + _XDE_ATTR_SIZE + \
										_XDE_TYPE_SIZE + _XDE_CHECKSUM_SIZE)
#define _XDE_UPDATE_SIZE			10

#define _XDE_ROOT_CHKSUM			0xDE

#define _XDE_DEFAULT_UID			FFAT_ADDON_DEFAULT_UID
#define _XDE_DEFAULT_GID			FFAT_ADDON_DEFAULT_GID
#define _XDE_DEFAULT_PERMISSION		FFAT_ADDON_DEFAULT_PERMISSION


static	void		_genExtendedDE(ExtendedDe* pXDE, t_uint32 dwUID, t_uint32 dwGID,
								t_uint16 wPerm, t_uint8 bCheckSum);
static t_boolean	_isExtendedDE(ExtendedDe* pXDE, t_uint8 bCheckSum);

static FFatErr		_readRootXDE(Vol* pVol, XDEInfo* pstXDEInfo, ComCxt* pCxt);
static FFatErr		_writeRootXDE(Vol* pVol, XDEInfo* pstXDEInfo, ComCxt* pCxt);
static FFatErr		_updatePositionSFNE(Node* pNodeParent, Node* pNodeChild,
								t_uint32* pdwClustersDE, t_int32 dwClusterCountDE);
static FFatErr		_checkExtendedDE(Vol* pVol,t_boolean bInternal,ComCxt* pCxt) ;

#define		FFAT_DEBUG_XDE_PRINTF(_msg)

// debug begin

#ifndef FFAT_DEBUG
	#ifdef _DEBUG_XDE
		#error "_DEBUG_XDE must be used with FFAT_DEBUG define!!"
	#endif
#endif

#ifdef _DEBUG_XDE
	#undef		FFAT_DEBUG_XDE_PRINTF
	#define		FFAT_DEBUG_XDE_PRINTF(_msg)		FFAT_PRINT_VERBOSE((_T("BTFS_XDE, %s()/%d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)
#endif
// debug end


/**
 * if mount flag includes FFAT_MOUNT_XDE, add VOL_ADDON_XDE to volume flag
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwFlag		: [IN] mount flag
 * @param		pCxt		: [IN] Context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] First Writing
 * @version		DEC-17-2008 [JeongWoo Park] support XDE for root
 * @version		MAR-23-2009 [GwangOk Go] support GUID/Permission in case that doesn't have XDE
 */
FFatErr
ffat_xde_mount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r;
	XDEInfo*		pNodeXDEInfo;
	FatVolInfo*		pVolInfo;

	pVolInfo = VOL_VI(pVol);
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(FFAT_XDE_SIGNATURE_STR_SIZE == FFAT_STRLEN(FFAT_XDE_SIGNATURE_STR));
	FFAT_ASSERT(FAT_DE_SIZE == sizeof(ExtendedDe));

	pNodeXDEInfo = &(NODE_ADDON(VOL_ROOT(pVol))->stXDE);
//2010_0320_kyungsik
// Add the following code to prevent going over from XDE volume to normal volme or from normal volume to XDE volume
	if (dwFlag & FFAT_MOUNT_XDE)
	{
		VOL_FLAG(pVol)	|= VOL_ADDON_XDE;
		VOL_MSD(pVol)	+= FAT_DE_SIZE;	// extended entry를 사용하는 volume은 최대 DE count가 하나 커야함

		// Retrieve Root GUID/permission & Update Root node
		r = _readRootXDE(pVol, pNodeXDEInfo, pCxt);
		FFAT_ER(r, (_T("fail to mount the volume with XDE")));
	}
	else
	{
		r = _checkExtendedDE(pVol,FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("fail to mount the volume because of XDE")));

		// XDE를 사용하지 않을 경우
		pNodeXDEInfo->dwUID		= _XDE_DEFAULT_UID;
		pNodeXDEInfo->dwGID		= _XDE_DEFAULT_GID;
		pNodeXDEInfo->dwPerm	= _XDE_DEFAULT_PERMISSION;
	}

	return FFAT_OK;
}


/**
 * create extended directory entry & info
 *
 * @param		pNode		: [IN] node pointer
 * @param		pDE			: [IN] buffer of directory entry
 * @param		bCheckSum	: [IN] check sum (SFN일 경우는 값이 없음)
 * @param		pXDEInfo	: [IN] info of extended DE
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing.
 * @version		MAR-23-2009 [GwangOk Go] support GUID/Permission in case that doesn't have XDE
 */
void
ffat_xde_create(Node* pNode, FatDeSFN* pDE, t_uint8 bCheckSum, XDEInfo* pXDEInfo)
{
	XDEInfo*		pNodeXDEInfo;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDE);

	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE);

	pNodeXDEInfo = &(NODE_ADDON(pNode)->stXDE);

	FFAT_ASSERT((pXDEInfo == NULL) ? (_IS_XDE_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE) : FFAT_TRUE);
	FFAT_ASSERT((pXDEInfo->dwPerm & FFAT_XDE_WRITES) ? ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0) : (pNode->stDE.bAttr & FFAT_ATTR_RO));

	if (_IS_XDE_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		// volume에 extended DE가 설정되어 있지 않은 경우

		pNodeXDEInfo->dwUID		= pXDEInfo->dwUID;
		pNodeXDEInfo->dwGID		= pXDEInfo->dwGID;
		pNodeXDEInfo->dwPerm	= pXDEInfo->dwPerm;

		return;
	}

	pNode->stDE.bNTRes |= ADDON_SFNE_MARK_XDE;

	FFAT_MEMCPY(pNodeXDEInfo, pXDEInfo, sizeof(XDEInfo));

	if (pNode->dwFlag & NODE_NAME_SFN)
	{
		bCheckSum = FFATFS_GetCheckSum(&pNode->stDE);
	}

	_genExtendedDE((ExtendedDe*)(pDE + pNode->stDeInfo.dwDeCount - 1), pXDEInfo->dwUID,
					pXDEInfo->dwGID, (t_uint16)pXDEInfo->dwPerm, bCheckSum);

	FFAT_DEBUG_XDE_PRINTF((_T("create for node:0x%X"), (t_uint32)pNode));

	return;
}


/**
* update DE count for XDE
*
* @param		pNodeChild	: [IN] child node pointer
* @param		pNodeDE		: [IN] info of directory entry
* @author		DongYoung Seo
* @version		04-DEC-2008 [DongYoung SEo] First Writing
*/
void
ffat_xde_lookup(Node* pNodeChild)
{
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(NODE_VOL(pNodeChild));
	FFAT_ASSERT(NODE_IS_ROOT(pNodeChild) == FFAT_FALSE);

	// directory entry count
	if (VOL_FLAG(NODE_VOL(pNodeChild)) & VOL_ADDON_XDE)
	{
		// Extended DE를 사용할 경우
		pNodeChild->stDeInfo.dwDeCount++;
	}

	return;
}


/**
 * this function is invoked after successful node creation
 * this function update position of SFNE
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @author		DongYoung Seo
 * @version		JAN-06-2007 [DongYoung Seo] First Writing
 */
FFatErr
ffat_xde_afterCreate(Node* pNodeParent, Node* pNodeChild,
						t_uint32* pdwClustersDE, t_int32 dwClusterCountDE)
{
	return _updatePositionSFNE(pNodeParent, pNodeChild, pdwClustersDE, dwClusterCountDE);
}


/**
 * fill extended directory info of node
 *
 * @param		pNode		: [IN] node pointer
 * @param		pNodeDE		: [IN] info of directory entry
 * @param		pXDEInfo	: [INOUT] directory entry buffer pointer(maybe NULL)
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 * @version		MAR-23-2009 [GwangOk Go] support GUID/Permission in case that doesn't have XDE
 */
void
ffat_xde_fillNodeInfo(Node* pNode, FatGetNodeDe* pNodeDE, XDEInfo* pXDEInfo)
{
	XDEInfo*		pNodeXDEInfo;
	ExtendedDe*		pXDE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE);

	// set pointer of XDE info of ADDON node
	pNodeXDEInfo = &(NODE_ADDON(pNode)->stXDE);

	if ((_IS_XDE_ACTIVATED(NODE_VOL(pNode)) == FFAT_FALSE) ||
		((pNode->stDE.bNTRes & ADDON_SFNE_MARK_XDE) == 0))
	{
		// volume에 extended DE가 설정되어 있지 않은 경우 혹은
		// SFNE의 NTRes에 XDE mark가 없는 경우
		pNodeXDEInfo->dwUID = _XDE_DEFAULT_UID;
		pNodeXDEInfo->dwGID = _XDE_DEFAULT_GID;

		if (ffat_log_isLogNode(pNode) == FFAT_TRUE)
		{
			pNodeXDEInfo->dwPerm = FFAT_ADDON_LOG_FILE_PERMISSION;
		}
		else if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
		{
			pNodeXDEInfo->dwPerm = FFAT_ADDON_DEBUG_FILE_PERMISSION;
		}
		else if (pNode->stDE.bAttr & FFAT_ATTR_RO)
		{
			pNodeXDEInfo->dwPerm = FFAT_XDE_EXECUTES | FFAT_XDE_READS;
		}
		else
		{
			pNodeXDEInfo->dwPerm = _XDE_DEFAULT_PERMISSION;
		}
	}
	else
	{
		// set pointer of extended directory entry
		pXDE = (ExtendedDe*)&pNodeDE->pDE[pNode->stDeInfo.dwDeCount - 1];

		// store XDE info in ADDON node
		pNodeXDEInfo->dwUID		= FFAT_BO_UINT32(pXDE->dwUID);
		pNodeXDEInfo->dwGID		= FFAT_BO_UINT32(pXDE->dwGID);
		pNodeXDEInfo->dwPerm	= (t_uint32)(FFAT_BO_UINT16(pXDE->wPerm));
	}

	if (pXDEInfo != NULL)
	{
		// store XDE info in pXDEInfo pointer (used in VFS)
		FFAT_MEMCPY(pXDEInfo, pNodeXDEInfo, sizeof(XDEInfo));
	}

	FFAT_DEBUG_XDE_PRINTF((_T("Node:0x%X, UID/GID/PERM:0x%X/0x%X/0x%X"), (t_uint32)pNode, pNodeXDEInfo->dwUID, pNodeXDEInfo->dwGID, pNodeXDEInfo->dwPerm));

	return;
}


/**
 * on rename, set xde flag on NTRes before log write
 *
 * @param		pNodeSrc	: [IN] source node pointer
 * @param		pNodeDes	: [IN] destination node pointer
 * @return		void
 * @author		GwangOk Go
 * @version		FEB-25-2009 [GwangOk Go] First Writing
 */
void
ffat_xde_rename(Node* pNodeSrc, Node* pNodeDes)
{
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDes);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeSrc) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeDes) == FFAT_FALSE);

	if (_IS_XDE_ACTIVATED(NODE_VOL(pNodeDes)) == FFAT_FALSE)
	{
		// volume에 extended DE가 설정되어 있지 않은 경우
		return;
	}

	if (pNodeSrc->stDE.bNTRes & ADDON_SFNE_MARK_XDE)
	{
		// mark extended DE on NTRes of SFNE
		pNodeDes->stDE.bNTRes |= ADDON_SFNE_MARK_XDE;
	}
// debug begin
#ifdef  FFAT_DEBUG
	else
	{
		// '.', '..'을 rename하거나
		// extended DE가 없던 volume을 extended DE로 사용한 경우
		// -> 발생할수 없다 : VFAT volume -> BTFS mount시 고려??

		FFAT_ASSERT(0);
	}
#endif
// debug end

	return;
}


/**
 * this function is invoked after successful node renaming
 * this function update position of SFNE
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @author		DongYoung Seo
 * @version		JAN-06-2007 [DongYoung Seo] First Writing
 */
FFatErr
ffat_xde_afterRename(Node* pNodeParentDes, Node* pNodeNew,
						t_uint32* pdwClustersDE, t_int32 dwClusterCountDE)
{
	return _updatePositionSFNE(pNodeParentDes, pNodeNew, pdwClustersDE, dwClusterCountDE);
}


/**
 * rename시 new node의 extended directory entry 생성
 *
 * @param		pNodeSrc	: [IN] source node pointer
 * @param		pNodeDes	: [IN] destination node pointer
 * @param		pDE			: [IN] directory entry buffer pointer
 * @param		bCheckSum	: [IN] check sum (SFN일 경우는 값이 없음)
 * @return		void
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 * @version		FEB-25-2009 [GwangOk Go] move setting flag on NTRes before log write (ffat_xde_rename)
 */
void
ffat_xde_renameUpdateDE(Node* pNodeSrc, Node* pNodeDes, FatDeSFN* pDE, t_uint8 bCheckSum)
{
	XDEInfo*	pXDEInfo;

	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDes);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeSrc) == FFAT_FALSE);
	FFAT_ASSERT(NODE_IS_ROOT(pNodeDes) == FFAT_FALSE);
	FFAT_ASSERT((NODE_ADDON(pNodeSrc)->stXDE.dwPerm & FFAT_XDE_WRITES) ? ((pNodeSrc->stDE.bAttr & FFAT_ATTR_RO) == 0) : (pNodeSrc->stDE.bAttr & FFAT_ATTR_RO));

	if (_IS_XDE_ACTIVATED(NODE_VOL(pNodeDes)) == FFAT_FALSE)
	{
		// volume에 extended DE가 설정되어 있지 않은 경우
		return;
	}

	if ((pNodeDes->stDE.bNTRes & ADDON_SFNE_MARK_XDE) == 0)
	{
		// '.', '..'을 rename하거나
		// extended DE가 없던 volume을 extended DE로 사용한 경우
		// -> 발생할수 없다 : VFAT volume -> BTFS mount시 고려??
		FFAT_ASSERT(0);
		return;
	}

	if (pNodeDes->dwFlag & NODE_NAME_SFN)
	{
		// SFN일 경우 check sum을 계산
		bCheckSum = FFATFS_GetCheckSum(&pNodeDes->stDE);
	}

	pXDEInfo = &(NODE_ADDON(pNodeSrc)->stXDE);

	// extended directory entry를 만듦
	_genExtendedDE((ExtendedDe*)(pDE + pNodeDes->stDeInfo.dwDeCount - 1), pXDEInfo->dwUID,
					pXDEInfo->dwGID, (t_uint16)pXDEInfo->dwPerm, bCheckSum);

	FFAT_DEBUG_XDE_PRINTF((_T("Node:0x%X, UID/GID/PERM:0x%X/0x%X/0x%X"), (t_uint32)pNodeDes, pXDEInfo->dwUID, pXDEInfo->dwGID, pXDEInfo->dwPerm));

	return;
}


/**
 * update extended directory entry & info
 *
 * @param		pNodeChild	: [IN] node pointer
 * @param		pNewXDEInfo	: [IN] new info of extended DE
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing.
 * @version		DEC-15-2008 [JeongWoo Park] Add consideration of Opened-unlink
 * @version		DEC-17-2008 [JeongWoo Park] support XDE for Root
 * @version		MAR-23-2009 [GwangOk Go] support GUID/Permission in case that doesn't have XDE
 * @version		MAR-30-2009 [GwangOk Go] write log in case of updating SFNE
 */
FFatErr
ffat_xde_updateXDE(Node* pNode, XDEInfo* pNewXDEInfo, ComCxt* pCxt)
{
	FFatErr		r;
	Vol*		pVol;

	XDEInfo*		pOldXDEInfo;
	ExtendedDe		stTempXDE;
	FFatCacheInfo	stCI;
	FFatCacheFlag	dwCacheFlag = FFAT_CACHE_NONE;

	t_boolean	bUpdateSFNE = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNewXDEInfo);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	VOL_INC_REFCOUNT(pVol);

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		FFAT_PRINT_DEBUG((_T("Time stamp is not same")));
		r = FFAT_EXDEV;
		goto out;
	}

	if (VOL_IS_RDONLY(pVol) == FFAT_TRUE)
	{
		FFAT_PRINT_DEBUG((_T("This volume is mounted with read-only flag")));
		r = FFAT_EROFS;
		goto out;
	}

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_SET_STAT);
	FFAT_EO(r, (_T("log file can not be updated XDE")));

	// set pointer of XDE info of ADDON node
	pOldXDEInfo = &(NODE_ADDON(pNode)->stXDE);

	FFAT_ASSERT((pOldXDEInfo->dwPerm & FFAT_XDE_WRITES) ? ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0) : (pNode->stDE.bAttr & FFAT_ATTR_RO));

	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		// DE is already deleted. just update Node and return.
		r = FFAT_OK;
		goto out;
	}

//2010_0317_kyungsik 
	if (NODE_IS_ROOT(pNode) == FFAT_FALSE)
	{

		// XDE를 사용하지 않는 경우는 log를 적을 필요 없음
		r = ffat_log_updateXDE(pNode, pNewXDEInfo, &dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to write log")));

		if ((pNewXDEInfo->dwPerm & FFAT_XDE_WRITES) && (pNode->stDE.bAttr & FFAT_ATTR_RO))
		{
			// new permission에는 write 속성이 있으며 read only 속성이 있는 경우
			pNode->stDE.bAttr &= ~FFAT_ATTR_RO;
			bUpdateSFNE = FFAT_TRUE;
		}
		else if (((pNewXDEInfo->dwPerm & FFAT_XDE_WRITES) == 0) && ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0))
		{
			// new permission에는 write 속성이 없으나 read only 속성이 없는 경우
			pNode->stDE.bAttr |= FFAT_ATTR_RO;
			bUpdateSFNE = FFAT_TRUE;
		}

		if (bUpdateSFNE == FFAT_TRUE)
		{
			// write short directory entry
			r = ffat_node_updateSFNE(pNode, 0, 0, 0, FAT_UPDATE_DE_WRITE_DE, FFAT_CACHE_NONE, pCxt);
			FFAT_EO(r, (_T("fail to update short file name entry")));
		}

		// store XDE info in ADDON node
		pOldXDEInfo->dwUID		= pNewXDEInfo->dwUID;
		pOldXDEInfo->dwGID		= pNewXDEInfo->dwGID;
		pOldXDEInfo->dwPerm		= pNewXDEInfo->dwPerm;

		if ((_IS_XDE_ACTIVATED(pVol) == FFAT_FALSE) || ((pNode->stDE.bNTRes & ADDON_SFNE_MARK_XDE) == 0) )
		{
			// volume에 extended DE가 설정되어 있지 않은 경우

			r = FFAT_OK;
			goto out;
		}


		FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

		dwCacheFlag |= FFAT_CACHE_DATA_DE;

	// write updated value
		stTempXDE.dwUID	= FFAT_BO_UINT32(pNewXDEInfo->dwUID);
		stTempXDE.dwGID	= FFAT_BO_UINT32(pNewXDEInfo->dwGID);
		stTempXDE.wPerm	= FFAT_BO_UINT16((t_uint16)pNewXDEInfo->dwPerm);

		if (pNode->stDeInfo.dwDeEndCluster == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

			r = FFATFS_ReadWriteOnRoot(VOL_VI(pVol),
										(pNode->stDeInfo.dwDeEndOffset + _XDE_UPDATE_POSITION),
										(((t_int8*)&stTempXDE) + _XDE_UPDATE_POSITION), _XDE_UPDATE_SIZE,
										dwCacheFlag, FFAT_FALSE, pNode, pCxt);
			FFAT_EO(r, (_T("fail to update extended DE")));
		}
		else
		{
			
			//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
			r = FFATFS_ReadWritePartialCluster(VOL_VI(pVol), pNode->stDeInfo.dwDeEndCluster,
											((pNode->stDeInfo.dwDeEndOffset & VOL_CSM(pVol)) + _XDE_UPDATE_POSITION),
											_XDE_UPDATE_SIZE,
											(((t_int8*)&stTempXDE) + _XDE_UPDATE_POSITION), FFAT_FALSE,
											dwCacheFlag, &stCI, pCxt, FFAT_FALSE);
			IF_UK (r != _XDE_UPDATE_SIZE)
			{
				FFAT_PRINT_DEBUG((_T("fail to update extended DE")));
				r = FFAT_EIO;
				goto out;
			}
		}
	}
	else
	{
		// write Root GUID/permission
		r = _writeRootXDE(pVol, pNewXDEInfo, pCxt);
		FFAT_EO(r, (_T("fail to write extended DE of Root")));
	}

	FFAT_DEBUG_XDE_PRINTF((_T("Node:0x%X, UID/GID/PERM:0x%X/0x%X/0x%X"), (t_uint32)pNode, pNewXDEInfo->dwUID, pNewXDEInfo->dwGID, pNewXDEInfo->dwPerm));

	r = FFAT_OK;

out:
	VOL_DEC_REFCOUNT(pVol);

	return r;
}


/**
 * change permission of XDE according to attr of directory entry on setStatus
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwAttr		: [IN] attr of directory entry
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		MAR-30-2009 [GwangOk Go] First Writing.
 */
FFatErr
ffat_xde_setStatus(Node* pNode, FFatNodeFlag dwAttr, ComCxt* pCxt)
{
	FFatErr		r;
	Vol*		pVol;

	XDEInfo*		pXDEInfo;
	ExtendedDe		stTempXDE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_VALID(pNode) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_ROOT(pNode) == FFAT_FALSE);

	pVol = NODE_VOL(pNode);

	// set pointer of XDE info of ADDON node
	pXDEInfo = &(NODE_ADDON(pNode)->stXDE);

	FFAT_ASSERT((pXDEInfo->dwPerm & FFAT_XDE_WRITES) ? ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0) : (pNode->stDE.bAttr & FFAT_ATTR_RO));

	if ((pXDEInfo->dwPerm & FFAT_XDE_WRITES) && (dwAttr & FFAT_ATTR_RO))
	{
		// permission에는 write 속성이 있고, attribute에 read only 속성이 있는 경우
		pXDEInfo->dwPerm &= ~FFAT_XDE_WRITES;
	}
	else if (((pXDEInfo->dwPerm & FFAT_XDE_WRITES) == 0) && ((dwAttr & FFAT_ATTR_RO) == 0))
	{
		// permission에는 write 속성이 없고, read only 속성이 없는 경우
		pXDEInfo->dwPerm |= FFAT_XDE_WRITES;
	}
	else
	{
		// 변경할 필요없음
		r = FFAT_OK;
		goto out;
	}

	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		// DE is already deleted. just update Node and return.
		r = FFAT_OK;
		goto out;
	}

	if ((_IS_XDE_ACTIVATED(pVol) == FFAT_FALSE) || ((pNode->stDE.bNTRes & ADDON_SFNE_MARK_XDE) == 0))
	{
		// volume에 extended DE가 설정되어 있지 않거나 SFNE의 NTRes에 XDE mark가 없는 경우

		r = FFAT_OK;
		goto out;
	}

	// write updated value
	stTempXDE.dwUID	= FFAT_BO_UINT32(pXDEInfo->dwUID);
	stTempXDE.dwGID	= FFAT_BO_UINT32(pXDEInfo->dwGID);
	stTempXDE.wPerm	= FFAT_BO_UINT16((t_uint16)pXDEInfo->dwPerm);

	if (pNode->stDeInfo.dwDeEndCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

		r = FFATFS_ReadWriteOnRoot(VOL_VI(pVol),
									(pNode->stDeInfo.dwDeEndOffset + _XDE_UPDATE_POSITION),
									(((t_int8*)&stTempXDE) + _XDE_UPDATE_POSITION), _XDE_UPDATE_SIZE,
									(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), FFAT_FALSE, pNode, pCxt);
		FFAT_EO(r, (_T("fail to update extended DE")));
	}
	else
	{
		FFatCacheInfo	stCI;

		FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		r = FFATFS_ReadWritePartialCluster(VOL_VI(pVol), pNode->stDeInfo.dwDeEndCluster,
										((pNode->stDeInfo.dwDeEndOffset & VOL_CSM(pVol)) + _XDE_UPDATE_POSITION),
										_XDE_UPDATE_SIZE,
										(((t_int8*)&stTempXDE) + _XDE_UPDATE_POSITION), FFAT_FALSE,
										(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), &stCI, pCxt, FFAT_FALSE);
		IF_UK (r != _XDE_UPDATE_SIZE)
		{
			FFAT_LOG_PRINTF((_T("fail to update extended DE")));
			r = FFAT_EIO;
			goto out;
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * after lookup short & long directory entry, get contiguous extended directory entry
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pNodeDE		: [IN/OUT] directory entry information for a node.
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_OK1	: need to skip this DE
 * @return		else		: error
 * @author		GwangOk Go
 * @version		AUG-14-2008 [GwangOk Go] First Writing.
 * @version		JAN-22-2009 [JeongWoo Park] Add the code to check whether SFN is XDE.
 */
FFatErr
ffat_xde_getXDE(Vol* pVol, FatGetNodeDe* pNodeDE, ComCxt* pCxt)
{
	FFatErr				r;
	FatVolInfo*			pVolInfo;
	FatDeSFN*			pDeSFN;
	FFatCacheInfo		stCI;
	ExtendedDe			stXDE;

	t_uint8				bCheckSum;
	t_uint32			dwCurCluster;
	t_uint32			dwCurOffset;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeDE);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(pNodeDE->dwEntryCount >= 1);

	if (_IS_XDE_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// volume에 extended DE가 비활성되어 있는 경우
		return FFAT_OK;
	}

	if (pNodeDE->dwEntryCount == 1)
	{
		bCheckSum = ((ExtendedDe*)&pNodeDE->pDE[0])->bCheckSum1;
		if (_isExtendedDE((ExtendedDe*)&pNodeDE->pDE[0], bCheckSum) == FFAT_TRUE)
		{
			// If SFN is XDE, return OK1 to notice that this DE must be skipped
			// With Multi-thread environment(RFS), this can be happened.
			// ex. while one task do readdir, another task can creat/unlink/rename etc.
			return FFAT_OK1;
		}
	}

	pDeSFN = (FatDeSFN*)&pNodeDE->pDE[pNodeDE->dwEntryCount - 1];

	if (((pDeSFN->bNTRes) & ADDON_SFNE_MARK_XDE) == 0)
	{
		// SFNE의 NTRes에 XDE mark가 없는 경우
		return FFAT_OK;
	}

	pVolInfo = VOL_VI(pVol);

	dwCurOffset = pNodeDE->dwDeEndOffset + FAT_DE_SIZE;

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	if (pNodeDE->dwDeEndCluster == FFATFS_FAT16_ROOT_CLUSTER)
	{
		t_uint32		dwSector;

		FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

		dwSector = VI_FRS(pVolInfo) + (dwCurOffset >> VI_SSB(pVolInfo));

		// read extended directory entry
		r = FFATFS_ReadWritePartialSector(pVolInfo, dwSector,
						(dwCurOffset & VI_SSM(pVolInfo)),
						FAT_DE_SIZE, (t_int8*)&stXDE, FFAT_CACHE_DATA_DE,
						&stCI, FFAT_TRUE, pCxt);

		dwCurCluster = pNodeDE->dwDeEndCluster;
	}
	else
	{
		if (dwCurOffset & VI_CSM(pVolInfo))
		{
			dwCurCluster = pNodeDE->dwDeEndCluster;
		}
		else
		{
			// 마지막 DE가 cluster에 끝에 있는 경우, next cluster를 가져옴
			r = FFATFS_GetNextCluster(pVolInfo, pNodeDE->dwDeEndCluster, &dwCurCluster, pCxt);
			FFAT_ER(r, (_T("fail to get next cluster")));

			FFAT_ASSERT(FFATFS_IsValidCluster(pVolInfo, dwCurCluster) == FFAT_TRUE);
		}

		// read extended directory entry
		//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
		r = FFATFS_ReadWritePartialCluster(pVolInfo, dwCurCluster,
						(dwCurOffset & VI_CSM(pVolInfo)), FAT_DE_SIZE, (t_int8*)&stXDE,
						FFAT_TRUE, FFAT_CACHE_DATA_DE, &stCI, pCxt, FFAT_FALSE);
	}

	IF_UK (r != FAT_DE_SIZE)
	{
		FFAT_LOG_PRINTF((_T("fail to read extended directory entry")));
		return FFAT_EIO;
	}

	if (pNodeDE->dwEntryCount == 1)
	{
		bCheckSum = ffat_fs_de_genChecksum(&pNodeDE->pDE[0]);
	}
	else
	{
		bCheckSum = ((FatDeLFN*)&pNodeDE->pDE[0])->bChecksum;
	}

	if (_isExtendedDE(&stXDE, bCheckSum) != FFAT_TRUE)
	{
		FFAT_DEBUG_PRINTF((_T("Invalid XDE founded, startClu:%d/startOff:0x%x/endClu:%d/endOff:0x%x"),
						pNodeDE->dwDeStartCluster, pNodeDE->dwDeStartOffset, pNodeDE->dwDeEndCluster, pNodeDE->dwDeEndOffset));
		FFAT_ASSERT(0);
		return FFAT_EXDE;
	}

	// copy directory entry
	FFAT_MEMCPY(&pNodeDE->pDE[pNodeDE->dwEntryCount], &stXDE, FAT_DE_SIZE);

	FFAT_ASSERT(pNodeDE->dwDeSfnCluster == pNodeDE->dwDeEndCluster);

	pNodeDE->dwTotalEntryCount	= pNodeDE->dwEntryCount + 1;

	// update end cluster & offset
	pNodeDE->dwDeEndCluster	= dwCurCluster;
	pNodeDE->dwDeEndOffset	= dwCurOffset;

	return FFAT_OK;
}


/**
 * generate extended directory entry
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pDeSFN		: [IN] short directory entry
 * @param		pXDE		: [IN/OUT] the buffer of extended directory entry
 * @param		dwUID		: [IN] user id
 * @param		dwGID		: [IN] group id
 * @param		wPerm		: [IN] permission
 * @param		bCheckSum	: [IN] check sum
 *								if 0 : need to calculate checksum
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 * @version		JAN-22-2009 [JeongWoo Park] bug fix for the condition bCheckSum is need to be calculated.
 */
void
ffat_xde_generateXDE(Vol* pVol, FatDeSFN* pDeSFN, ExtendedDe* pXDE, t_uint32 dwUID,
					t_uint32 dwGID, t_uint16 wPerm, t_uint8 bCheckSum)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pDeSFN);
	FFAT_ASSERT(pXDE);

	if (_IS_XDE_ACTIVATED(pVol) == FFAT_FALSE)
	{
		// volume에 extended DE가 설정되어 있지 않은 경우
		return;
	}

	if ((pDeSFN->bNTRes & ADDON_SFNE_MARK_XDE) == 0)
	{
		return;
	}

	if (bCheckSum == 0)
	{
		bCheckSum = FFATFS_GetCheckSum(pDeSFN);
	}

	_genExtendedDE(pXDE, dwUID, dwGID, wPerm, bCheckSum);

	return;
}

/**
 * Get GUID/Permission information from node
 *
 * @param		pNode		: [IN] node pointer
 * @param		pstXDEInfo	: [OUT] GUID, permission storage
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		JeongWoo Park
 * @version		DEC-17-2008 [JeongWoo Park] First Writing
 * @version		MAR-23-2009 [GwangOk Go] support GUID/Permission in case that doesn't have XDE
 */
FFatErr
ffat_xde_getGUIDFromNode(Node* pNode, XDEInfo* pstXDEInfo)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pstXDEInfo);

	// extended directory entry를 사용하지 않더라도 node에 저장된 GUID/Permission을 반환함

	// store XDE info in pXDEInfo pointer (used in VFS)
	FFAT_MEMCPY(pstXDEInfo, &(NODE_ADDON(pNode)->stXDE), sizeof(XDEInfo));

	FFAT_DEBUG_XDE_PRINTF((_T("Node:0x%X -> UID/GID/PERM:0x%X/0x%X/0x%X"), (t_uint32)pNode, pstXDEInfo->dwUID, pstXDEInfo->dwGID, pstXDEInfo->dwPerm));

	return FFAT_OK;
}


//=============================================================================
//
//	static function
//


/**
 * generate extended directory entry
 *
 * @param		pXDE		: [IN/OUT] the buffer of extended directory entry
 * @param		dwUID		: [IN] user id
 * @param		dwGID		: [IN] group id
 * @param		wPerm		: [IN] permission
 * @param		bCheckSum	: [IN] check sum
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 */
static void
_genExtendedDE(ExtendedDe* pXDE, t_uint32 dwUID, t_uint32 dwGID,
				t_uint16 wPerm, t_uint8 bCheckSum)
{
	FFAT_MEMCPY(pXDE->szSignature, FFAT_XDE_SIGNATURE_STR, FFAT_XDE_SIGNATURE_STR_SIZE);
	pXDE->bCheckSum1	= pXDE->bCheckSum2 = bCheckSum;
	pXDE->bAttr			= _XDE_ATTR_VALUE;
	pXDE->bType			= _XDE_TYPE_VALUE;

	pXDE->wPerm			= FFAT_BO_UINT16(wPerm);
	pXDE->dwUID			= FFAT_BO_UINT32(dwUID);
	pXDE->dwGID			= FFAT_BO_UINT32(dwGID);

	pXDE->dwReserved1	= 0;
	pXDE->dwReserved2	= 0;

	return;
}


/**
 * check extended directory entry
 *
 * @param		pXDE		: [IN] extended directory entry
 * @param		bCheckSum	: [IN] check sum
 * @return		FFAT_TRUE	: this is an extended DE
 * @return		FFAT_FALSE	: this is not extended DE
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 */
static t_boolean
_isExtendedDE(ExtendedDe* pXDE, t_uint8 bCheckSum)
{
	FFAT_ASSERT(pXDE);
	if ((pXDE->bAttr != _XDE_ATTR_VALUE) || (pXDE->bType != _XDE_TYPE_VALUE) ||
		(pXDE->bCheckSum1 != bCheckSum) || (pXDE->bCheckSum2 != bCheckSum))
	{
		return FFAT_FALSE;
	}
	
	if (FFAT_STRNCMP((char*)pXDE->szSignature, FFAT_XDE_SIGNATURE_STR, FFAT_XDE_SIGNATURE_STR_SIZE) != 0)
	{
		return FFAT_FALSE;
	}	

	FFAT_ASSERT(pXDE->dwReserved1 == 0);
	FFAT_ASSERT(pXDE->dwReserved2 == 0);

	return FFAT_TRUE;
}

/**
* read GUID / permission for Root
*
* @param		pXDE		: [IN] extended directory entry
* @param		pstXDEInfo	: [OUT] GUID, permission storage
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		JeongWoo Park
* @version		DEC-17-2008 [JeongWoo Park] First Writing
* @version		MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_readRootXDE(Vol* pVol, XDEInfo* pstXDEInfo, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	ExtendedDe		stXDE;
	FFatErr			r;
	t_int8	stBoot = FFAT_FALSE;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pstXDEInfo);
	FFAT_ASSERT(pCxt);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	if(FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		r = FFATFS_ReadWritePartialSector(pVolInfo, pVolInfo->dwFirstRootSector, 32,
									1, (t_int8*)&stXDE,
									(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
									&stCI, FFAT_TRUE, pCxt);
		IF_UK (r != 1)
		{
			FFAT_LOG_PRINTF((_T("Fail to read/write cluster")));
			return FFAT_EIO;
		}

		IF_UK(stXDE.szSignature[0]!=0)
		{
			stBoot = FFAT_TRUE;
		}
	}
	else
	{
		r = FFATFS_ReadWritePartialSector(pVolInfo, pVolInfo->dwFirstRootSector, 0,
										1, (t_int8*)&stXDE,
										(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
										&stCI, FFAT_TRUE, pCxt);
		IF_UK (r != 1)
		{
			FFAT_LOG_PRINTF((_T("Fail to read/write cluster")));
			return FFAT_EIO;
		}

		IF_UK(stXDE.szSignature[0]!=0)
		{
			stBoot = FFAT_TRUE;
		}
	}

	IF_UK(!stBoot)
	{
		pstXDEInfo->dwUID = _XDE_DEFAULT_UID;
		pstXDEInfo->dwGID = _XDE_DEFAULT_GID;
		pstXDEInfo->dwPerm = _XDE_DEFAULT_PERMISSION;

		return FFAT_OK;		
	}
	else
	{
		r = _checkExtendedDE(pVol,FFAT_TRUE, pCxt);
		if(r < 0)
		{
			return r;
		}

		r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_XDE_OFFSET,
											sizeof(ExtendedDe), (t_int8*)&stXDE,
											(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
											&stCI, FFAT_TRUE, pCxt);
		IF_UK (r != sizeof(ExtendedDe))
		{
			FFAT_LOG_PRINTF((_T("fail to read root extended directory entry")));
			return FFAT_EIO;
		}

		IF_UK (_isExtendedDE(&stXDE, _XDE_ROOT_CHKSUM) == FFAT_TRUE)
		{
			pstXDEInfo->dwUID = FFAT_BO_UINT32(stXDE.dwUID);
			pstXDEInfo->dwGID = FFAT_BO_UINT32(stXDE.dwGID);
			pstXDEInfo->dwPerm = (t_uint32)(FFAT_BO_UINT16(stXDE.wPerm));
		}
		else
		{
			pstXDEInfo->dwUID = _XDE_DEFAULT_UID;
			pstXDEInfo->dwGID = _XDE_DEFAULT_GID;
			pstXDEInfo->dwPerm = _XDE_DEFAULT_PERMISSION;
		}
	}	

	return FFAT_OK;
}

/**
* write GUID / permission for Root
*
* @param		pXDE		: [IN] extended directory entry
* @param		pstXDEInfo	: [IN] GUID, permission storage
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		JeongWoo Park
* @version		DEC-17-2008 [JeongWoo Park] First Writing
* @version		MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_writeRootXDE(Vol* pVol, XDEInfo* pstXDEInfo, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	ExtendedDe		stXDE;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pstXDEInfo);
	FFAT_ASSERT(pCxt);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	// generate Extend DE for root
	_genExtendedDE(&stXDE, pstXDEInfo->dwUID, pstXDEInfo->dwGID,
					(t_int16)pstXDEInfo->dwPerm, _XDE_ROOT_CHKSUM);

	// read extended directory entry for root from BPB
	r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_XDE_OFFSET,
										sizeof(ExtendedDe), (t_int8*)&stXDE,
										(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO | FFAT_CACHE_SYNC),
										&stCI, FFAT_FALSE, pCxt);
	if (r != sizeof(ExtendedDe))
	{
		FFAT_LOG_PRINTF((_T("fail to write root extended directory entry")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}


/**
 * this function update position of SFNE
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @author		DongYoung Seo
 * @version		JAN-06-2007 [DongYoung Seo] First Writing
 */
static FFatErr
_updatePositionSFNE(Node* pNodeParent, Node* pNodeChild,
						t_uint32* pdwClustersDE, t_int32 dwClusterCountDE)
{
	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(pdwClustersDE);
	FFAT_ASSERT(dwClusterCountDE > 0);
	FFAT_ASSERT(dwClusterCountDE <= NODE_MAX_CLUSTER_FOR_CREATE);

	FFAT_ASSERT(pNodeChild->stDeInfo.dwDeClusterSFNE == pNodeChild->stDeInfo.dwDeEndCluster);
	FFAT_ASSERT(pNodeChild->stDeInfo.dwDeOffsetSFNE == pNodeChild->stDeInfo.dwDeEndOffset);

	if (VOL_FLAG(NODE_VOL(pNodeParent)) & VOL_ADDON_XDE)
	{
		// SFNE cluster를 설정
		// SFNE의 위치가 pNode->stDeInfo.dwDeEndOffset이 아니기 때문에
		FFAT_ASSERT(pNodeChild->stDeInfo.dwDeEndOffset != 0);

		if (((NODE_IS_ROOT(pNodeParent) == FFAT_TRUE) && (VOL_IS_FAT16(NODE_VOL(pNodeParent)) == FFAT_TRUE)) ||
			(pNodeChild->stDeInfo.dwDeEndOffset & VOL_CSM(NODE_VOL(pNodeParent))))
		{
			// SFNE가 XDE와 같은 cluster에 있는 경우
			pNodeChild->stDeInfo.dwDeClusterSFNE = pNodeChild->stDeInfo.dwDeEndCluster;
		}
		else
		{
			// SFNE이 XDE의 이전 cluster에 있는 경우
			FFAT_ASSERT(dwClusterCountDE >= 2);
			pNodeChild->stDeInfo.dwDeClusterSFNE = pdwClustersDE[dwClusterCountDE - 2];
		}

		// offset of SFNE
		pNodeChild->stDeInfo.dwDeOffsetSFNE = pNodeChild->stDeInfo.dwDeEndOffset - FAT_DE_SIZE;
	}

	return FFAT_OK;
}

/**
 * this function check whether there is a XDE on the Root directory or not.
 *
 * @param		bInternal			: [IN] FFAT_TRUE : Internal(mount with XDE option), FFAT_FALSE : Internal(mount without XDE option)
 * @author		KyungSik Song
 * @version		May-19-2010 [KyungSik Song] First Writing
 */
static FFatErr
_checkExtendedDE(Vol* pVol,t_boolean bInternal,ComCxt* pCxt)
{
	FFatErr			r;
	t_int8			i;
	t_uint32		dwCluster;
	t_uint32		RootSector=0;
	FatVolInfo*		pVolInfo;
	t_boolean		lastDE=0;
	ExtendedDe		stXDE;
	FFatCacheInfo	stCI;

	pVolInfo = VOL_VI(pVol);
	
	if((pVolInfo->dwClusterSize==512)&&(pVolInfo->dwSectorSize==512))
	{
	  // 2010.07.22_chunum.kong_Fix the bug that mount problem in FAT16 is solved by modifing _checkExtendedDE()'s condition. (RFS report)
		if((pVolInfo->dwRootCluster==0) || (pVolInfo->dwRootCluster==1))
		{
			r = pVolInfo->pVolOp->pfGetNextCluster(pVolInfo, 2, &dwCluster);
			if(r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to read root extended directory entry")));
				return FFAT_EIO;
			}
			if (!FFATFS_IS_EOF(pVolInfo, dwCluster))
			{
				RootSector	= FFATFS_GetFirstSectorOfCluster(pVolInfo, dwCluster);
			}
		}
		else
		{
			r = pVolInfo->pVolOp->pfGetNextCluster(pVolInfo, pVolInfo->dwRootCluster, &dwCluster);
			if(r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to read root extended directory entry")));
				return FFAT_EIO;
			}
			if (!FFATFS_IS_EOF(pVolInfo, dwCluster))
			{
				RootSector	= FFATFS_GetFirstSectorOfCluster(pVolInfo, dwCluster);
			}
		}
	}

	for(i=1; i<(FAT_DE_COUNT_MAX+2); i++)
	{
		//Add to support the compatability with previous version.
		if((RootSector)&&(i > 15))
		{
			r = FFATFS_ReadWritePartialSector(pVolInfo, RootSector, ((i*FAT_DE_SIZE)&0x1FF),
												FFAT_XDE_SIGNATURE_STR_SIZE, (t_int8*)&stXDE,
												(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
												&stCI, FFAT_TRUE, pCxt);
		}
		else
		{ 
			if(pVolInfo->dwSectorSize==512)
			{
				r = FFATFS_ReadWritePartialSector(pVolInfo, (pVolInfo->dwFirstRootSector+((i*FAT_DE_SIZE)>>9)), ((i*FAT_DE_SIZE)&0x1FF),
												FFAT_XDE_SIGNATURE_STR_SIZE, (t_int8*)&stXDE,
												(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
												&stCI, FFAT_TRUE, pCxt);
			}
			else
			{
				r = FFATFS_ReadWritePartialSector(pVolInfo, pVolInfo->dwFirstRootSector, (i*FAT_DE_SIZE),
												FFAT_XDE_SIGNATURE_STR_SIZE, (t_int8*)&stXDE,
												(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
												&stCI, FFAT_TRUE, pCxt);
			}
		}
		if (r != FFAT_XDE_SIGNATURE_STR_SIZE)
		{
			FFAT_LOG_PRINTF((_T("fail to read root extended directory entry")));
			return FFAT_EIO;
		}
		else
		{
      // 2010.05.26_chunum.kong_Bug Fix to compare with character "?" about xde. It occured the mount fail, in case entire files in volume deleted.
			if (FFAT_STRNCMP((char*)&stXDE.szSignature[1], FFAT_XDE_SIGNATURE_STR2, FFAT_XDE_SIGNATURE_STR_SIZE - 1) == 0)
			{
				break;
			}
			else if (stXDE.szSignature[0]==0)
			{
				if(bInternal)
				{
					return FFAT_EINVALID;
				}
				else
				{
					lastDE = 1;
					break;
				}
			}
		}
	}
	if(bInternal)
	{
		if((i == (FAT_DE_COUNT_MAX+2)))
		{
			FFAT_LOG_PRINTF((_T("fail why use XDE option abnormally.")));
			return FFAT_EINVALID;
		}
	}
	else
	{
		if (!lastDE && (i < (FAT_DE_COUNT_MAX+2)))
		{
			FFAT_LOG_PRINTF((_T("fail why use XDE option abnormally.")));
			return FFAT_EINVALID;
		}
	}

	return FFAT_OK;
}

