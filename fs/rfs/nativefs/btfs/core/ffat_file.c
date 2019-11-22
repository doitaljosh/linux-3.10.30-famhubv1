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
 * @file		ffat_file.c
 * @brief		The global configuration for FFAT
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

// includes
#include "ess_types.h"
#include "ess_math.h"

#include "ffat_types.h"
#include "ffat_errno.h"

#include "ffat_common.h"
#include "ffat_file.h"
#include "ffat_main.h"
#include "ffat_vol.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffat_addon_api.h"

#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_FILE)

// static function
static FFatErr	_expandPrepare(Node* pNode, t_uint32 dwSize, t_uint32* pdwPrevEOF,
							FFatVC* pVC, t_uint32* pdwNewClusterCount, ComCxt* pCxt);
static FFatErr	_shrinkPrepare(Node* pNode, t_uint32 dwSize, t_uint32* pdwNewEOF,
							FFatVC* pVC, ComCxt* pCxt);
static FFatErr	_shrinkPrepareForRecoveryDirtySize(Node* pNode, t_uint32 dwSize,
							  t_uint32* pdwNewEOF, FFatVC* pVC, ComCxt* pCxt);
static FFatErr	_shrink(Node* pNode, t_uint32 dwSize, t_uint32 dwNewEOF, FFatVC* pVC,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_expand(Node* pNode, t_uint32 dwSize, t_uint32 dwPrevEOF, FFatVC* pVC,
							t_uint32 dwNewClusterCount, FFatChangeSizeFlag dwCSFlag,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt);

static t_int32	_read(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
							FFatReadFlag dwReadFlag, ComCxt* pCxt);
static t_int32	_write(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
							FFatWriteFlag dwWriteFlag, ComCxt* pCxt);
static FFatErr	_writePrepareClusters(Node* pNode, t_uint32 dwOffset, t_int32 dwSize,
							t_uint32* pdwPrevEOC, t_uint32* pdwNewClusters,
							FFatVC* pVC_Cur, FFatVC* pVC_New, ComCxt* pCxt);
static FFatErr	_writeAllocClusters(Node* pNode, t_uint32 dwOffset, t_int32 dwSize,
							t_uint32 dwPrevEOC, t_uint32 dwNewClusters,
							FFatVC* pVC_Cur, FFatVC* pVC_New, FFatCacheFlag dwCacheFlag,
							FatAllocateFlag dwAllocFlag, ComCxt* pCxt);
static FFatErr	_writeGetAvailableSize(Node* pNode, t_uint32 dwOffset,
							t_int32* pdwSize, ComCxt* pCxt);
static FFatErr	_checkAndAdjustSize(Node* pNode, t_uint32 dwOffset, t_int32* pdwSize, t_boolean bRead);
static t_uint32	_getNewClusterCount(Node* pNode, t_uint32 dwOffset, t_int32 dwSize);

/**
 * This function initializes FFatFile module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_file_init(void)
{
	// nothing to do

	return FFAT_OK;
}

/**
 * This function terminates FFatFile module
 *
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_file_terminate(void)
{
	// nothing to do

	return FFAT_OK;
}


/**
 * change node size
 *
 * @param		pNode		: [IN] target node pointer
 * @param		dwSize		: [IN] New node size
 * @param		dwCSFlag	: [IN] change size flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] construct information for log in CORE module and give this info to ADDON module
 * @version		APR-13-2009 [JW Park] Add the restore code of node information for error
 * @version		OCT-20-2009 [JW Park] Add the consideration about dirty-size state of node
 * @version		DEC-14-2009 [JW Park] Bug fix about wrong dirty-size state
 *									at the operation that requires more free clusters than existed.
 */
FFatErr
ffat_file_changeSize(Node* pNode, t_uint32 dwSize, FFatChangeSizeFlag dwCSFlag,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	FatDeUpdateFlag		dwDeUpdateFlag;

	FFatVC				stVC;
	t_uint32			dwEOF;
	t_uint32			dwNewClusterCount = 0;
	t_uint32			dwOrgNodeCluster;
	t_uint32			dwOrgNodeSize;
	t_boolean			bExpand;

// debug begin
#ifdef FFAT_DEBUG
	t_boolean			b1stRetry = FFAT_TRUE;		// boolean to check expand retry count.
													// there must be only one retry to expand possible byte
#endif
// debug end

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);
	stVC.pVCE = NULL;

	if ((dwCSFlag & FFAT_CHANGE_SIZE_NO_LOCK) == 0)
	{
		r = NODE_GET_WRITE_LOCK(pNode);
		FFAT_ER(r, (_T("fail to get node write lock")));

		r = VOL_GET_READ_LOCK(pVol);
		FFAT_EOTO(r, (_T("fail to get vol read lock")), out_wlock);
	}

	VOL_INC_REFCOUNT(pVol);

	IF_UK (NODE_IS_VALID(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("invalid node pointer")));
		r = FFAT_EINVALID;
		goto out_rlock;
	}

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(VOL_VI(pVol), NODE_DE(pNode)));
	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_RDONLY(pNode) == FFAT_FALSE);

	IF_UK ((pNode->dwSize == dwSize) &&
		((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) == 0))
	{
		r = FFAT_OK;
		goto out_rlock;
	}

	IF_UK (dwSize > FFATFS_GetMaxFileSize(VOL_VI(pVol)))
	{
		FFAT_PRINT_ERROR((_T("Too big size - over maximum file size(%ld)"), FFATFS_GetMaxFileSize(VOL_VI(pVol))));
		r = FFAT_ERANGE;
		goto out_rlock;
	}

	IF_UK (VOL_IS_RDONLY(pVol) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("Read only volume")));
		r = FFAT_EROFS;
		goto out_rlock;
	}

	// check time stamp
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(pVol, pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not same")));
		r = FFAT_EXDEV;
		goto out_rlock;
	}

	// lock CORE for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EOTO(r, (_T("fail to lock CORE")), out_rlock);

	if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
	{
		// volume is sync mode
		dwCacheFlag |= FFAT_CACHE_SYNC;
	}

	dwDeUpdateFlag = FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_MTIME 
			| FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_CLUSTER | FAT_UPDATE_DE_WRITE_DE;

	if (dwSize > NODE_S(pNode))
	{
		bExpand = FFAT_TRUE;

		if (dwCSFlag & FFAT_CHANGE_SIZE_RECORD_DIRTY_SIZE)
		{
			// start the dirty size state
			if (NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE)
			{
				NODE_SET_DIRTY_SIZE_BEGIN(pNode);
			}
		}
		else
		{
			// If normal expand, then update the size and remove the dirty-size state
			dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
		}
	}
	else
	{
		bExpand = FFAT_FALSE;

		// If dwSize is smaller than the original size before dirty which is recorded in DE,
		// remove the dirty-size state
		if ((NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE) &&
			(dwSize <= FFATFS_GetDeSize(NODE_DE(pNode))))
		{
			dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
		}
	}

	FFAT_ASSERT((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) ? (bExpand == FFAT_FALSE) : FFAT_TRUE);

	// keep original size and cluster for restore
	dwOrgNodeCluster	= NODE_C(pNode);
	dwOrgNodeSize		= NODE_S(pNode);

	stVC.pVCE = FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE != NULL);

	stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

retry:		// retry to expand possible count of byte
	VC_INIT(&stVC, VC_NO_OFFSET);

	if (bExpand == FFAT_TRUE)
	{
		FFAT_ASSERT(dwSize > pNode->dwSize);

		r = _expandPrepare(pNode, dwSize, &dwEOF, &stVC, &dwNewClusterCount, pCxt);

		FFAT_ASSERT((b1stRetry == FFAT_FALSE) ? (r == FFAT_OK) : FFAT_TRUE);
	}
	else
	{
		if ((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) == 0)
		{
			r = _shrinkPrepare(pNode, dwSize, &dwEOF, &stVC, pCxt);
		}
		else
		{
			r = _shrinkPrepareForRecoveryDirtySize(pNode, dwSize, &dwEOF, &stVC, pCxt);
		}
	}

	FFAT_EOTO(r, (_T("fail to truncate file")), out_nospc);

	r = ffat_addon_changeSize(pNode, dwSize, dwEOF, &stVC, dwCSFlag, &dwCacheFlag, pCxt);
	FFAT_EOTO(r, (_T("fail to change node size on ADDON module")), out_clock);

	IF_UK (r == FFAT_DONE)
	{
		// node size change operation is done
		r = FFAT_OK;
		goto out;
	}

	if (bExpand == FFAT_TRUE)
	{
		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_EXTEND_ALLOCATE_CLUSTER_BEFORE);

		r |= _expand(pNode, dwSize, dwEOF, &stVC, dwNewClusterCount, dwCSFlag, dwCacheFlag, pCxt);

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_EXTEND_ALLOCATE_CLUSTER_AFTER);
	}
	else
	{
		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_SHRINK_DEALLOCATE_CLUSTER_BEFORE);

		r = _shrink(pNode, dwSize, dwEOF, &stVC, dwCacheFlag, pCxt);

		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_SHRINK_DEALLOCATE_CLUSTER_AFTER);
	}

	FFAT_ASSERT(((dwSize > 0) && (r == FFAT_OK) && (stVC.dwTotalClusterCount > 0)) ? 
							FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNode->dwLastCluster) == FFAT_TRUE : FFAT_TRUE);

	FFAT_EO(r, (_T("fail to truncate file")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_UPDATE_DE_BEFORE);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DE_UPDATE_BEFORE);

	// update child node
	r = ffat_node_updateSFNE(pNode, dwSize, 0, NODE_C(pNode), dwDeUpdateFlag, dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to update short file name entry")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DE_UPDATE_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_TRUNCATE_UPDATE_DE_AFTER);

out:
	FFAT_ASSERT(ffat_share_checkNodeLastClusterInfo(pNode, pCxt) == FFAT_OK);
	FFAT_ASSERT(ffat_share_checkNodePAL(pNode, pCxt) == FFAT_OK);

	r |= ffat_addon_afterChangeSize(pNode, &stVC, dwOrgNodeSize, bExpand, FFAT_IS_SUCCESS(r), pCxt);

out_nospc:
	if ((dwCSFlag & FFAT_CHANGE_SIZE_AVAILABLE) && (r == FFAT_ENOSPC))
	{
		t_int32		dwFreeSpace;

		// check this is the first try. if it is not first try it is error.
		//	because _writeGetAvailableSize() returns expandable byte count.
		FFAT_ASSERT((b1stRetry == FFAT_TRUE) ? (FFAT_FALSE == (b1stRetry = FFAT_FALSE)) : FFAT_FALSE);
		FFAT_ASSERT(bExpand == FFAT_TRUE);

		r = _writeGetAvailableSize(pNode, NODE_S(pNode), &dwFreeSpace, pCxt);
		if (r == FFAT_OK)
		{
			FFAT_ASSERT(dwFreeSpace > 0);
			dwSize = NODE_S(pNode) + dwFreeSpace;
			goto retry;
		}
	}

out_clock:
	IF_UK (r < 0)
	{
		// If dirty-size-begin is set, remove this flag for error.
		if (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
		{
			NODE_CLEAR_DIRTY_SIZE_BEGIN(pNode);
			NODE_CLEAR_DIRTY_SIZE(pNode);		// dirty-size flag can be set at log module.
		}

		if (r != FFAT_ENOSPC)
		{
			// restore original node information
			ffat_node_updateSFNE(pNode, dwOrgNodeSize, 0, dwOrgNodeCluster,
								(FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_CLUSTER |
								FAT_UPDATE_DE_WRITE_DE |  FAT_UPDATE_DE_FORCE),
								dwCacheFlag, pCxt);

			pNode->dwLastCluster = 0;
			ffat_node_initPAL(pNode);
		}
	}

	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(VOL_VI(pVol), NODE_DE(pNode)));
	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_FALSE);

	// lock CORE for free cluster sync
	r |= ffat_core_unlock(pCxt);		// ignore error

out_rlock:
	VOL_DEC_REFCOUNT(pVol);

	if ((dwCSFlag & FFAT_CHANGE_SIZE_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
	}

out_wlock:
	if ((dwCSFlag & FFAT_CHANGE_SIZE_NO_LOCK) == 0)
	{
		r |= NODE_PUT_WRITE_LOCK(pNode);
	}

	return r;
}


/**
 * read data to a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwReadFlag	: [IN] flag for read operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_file_read(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
				FFatReadFlag dwReadFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNode == NULL) || (pBuff == NULL) || (dwSize < 0) || (NODE_VOL(pNode)== NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = NODE_GET_READ_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get rad lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("Fail to lock volume")), out_vol);

	// CHECK TIME STAMP
	// check time stamp between volume and parent node
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	IF_UK (NODE_IS_FILE((Node*)pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_EISDIR;
		goto out;
	}

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	r = _read(pNode, dwOffset, pBuff, dwSize, dwReadFlag, pCxt);

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_READ_LOCK(pNode);

	return r;
}

/**
 * write data to a node
 * file에 대한 write를 수행할 수 있다. !!!
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwWriteFlag	: [IN] flag for write operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EACCESS	: not mounted or read-only volume
 * @return		FFAT_EISDIR		: this is not a file
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
ffat_file_write(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
				FFatWriteFlag dwWriteFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pNode == NULL) || (pBuff == NULL) || (NODE_VOL(pNode)== NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
	
	IF_UK (dwSize < 0)
	{
		FFAT_LOG_PRINTF((_T("Size is overflowed or minus")));
		return FFAT_ERANGE;
	}
#endif

	if ((dwWriteFlag & FFAT_WRITE_NO_LOCK) == 0)
	{
		r = NODE_GET_WRITE_LOCK(pNode);
		FFAT_ER(r, (_T("fail to get node write lock")));

		r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
		FFAT_EOTO(r, (_T("fail to get vol read lock")), out_vol);
	}

	IF_UK (NODE_IS_VALID(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("the node is not initialized yet ")));
		r = FFAT_EINVALID;
		goto out;
	}

	IF_UK (NODE_IS_FILE((Node*)pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_EISDIR;
		goto out;
	}

	// check time stamp validity
	IF_UK (FFAT_MAIN_CHECK_TIME_STAMP(NODE_VOL(pNode), pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("Time stamp is not correct, incorrect volume")));
		r = FFAT_EXDEV;
		goto out;
	}

	IF_UK (VOL_IS_RDONLY(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("This volume is mounted with read-only flag")));
		r = FFAT_EROFS;
		goto out;
	}

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));
	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_RDONLY(pNode) == FFAT_FALSE);

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	r = _write(pNode, dwOffset, pBuff, dwSize, dwWriteFlag, pCxt);

	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);
	FFAT_ASSERT(NODE_C(pNode) == FFATFS_GetDeCluster(NODE_VI(pNode), NODE_DE(pNode)));

out:
	if ((dwWriteFlag & FFAT_WRITE_NO_LOCK) == 0)
	{
		r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));
	}

out_vol:
	if ((dwWriteFlag & FFAT_WRITE_NO_LOCK) == 0)
	{
		r |= NODE_PUT_WRITE_LOCK(pNode);
	}

	return r;
}

/**
 * unlink a file.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 *									may be NULL
 * @param		pNode			: [IN] node pointer
 * @param		dwNUFlag		: [IN] flag for unlink operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		31-AUG-2006 [DongYoung Seo] First Writing
 * @history		04-DEC-2007 [InHwan Choi] apply to open unlink 
 * @history		08-SEP-2008 [DongYoung Seo] remove node close routine after node unlink
 */
FFatErr
ffat_file_unlink(Node* pNodeParent, Node* pNode, NodeUnlinkFlag dwNUFlag, ComCxt* pCxt)
{
	FFatErr			r;

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

	IF_UK (NODE_IS_FILE(pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_EISDIR;
		goto out;
	}

	r = ffat_node_unlink(pNodeParent, pNode, dwNUFlag, pCxt);
	FFAT_EO(r, (_T("fail to unlink node")));

out:
	return r;
}


/**
 * get cluster that are occupied to a file.
 * or allocated cluster for file
 *
 * @param		pNode		: [IN] a node pointer
 * @param		dwOffset	: [IN] start offset
 * @param		dwSize		: [IN] size in byte
 * @param		pFP			: [IN] file pointer hint for fast access
 *									may be NULL
 * @param		pVC			: [IN] cluster information storage
 * @param		bAllocate	: [IN] FFAT_TRUE : allocate cluster for dwSize
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_EINVALID	: invalid parameter, invalid get block area
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] if dwClusterCount is 1, use ffat_misc_getClusterOfOffset()
 * @version		JUN-15-2009 [JeongWoo Park] fix the overflow of dwLastCluster by using cluster count
 * @version		SEP-04-2009 [JW Park] Add the code to check the case that EOC can be returned.
 * @version		NOV-10-2009 [JW Park] Add the code to check whether the offset is over file size.
 */
FFatErr
ffat_file_getClusters(Node* pNode, t_uint32 dwOffset, t_uint32 dwSize,
					FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;
	t_uint32			dwClusterCount;				// cluster count for dwSize
	t_uint32			dwClusterCountInNode;		// cluster count for node
	NodePAL				stPAL = {FFAT_NO_OFFSET, 0, 0};		// previous access location

#ifdef FFAT_STRICT_CHECK
	if ((pNode == NULL) || (pVC == NULL) || (pVC->pVCE == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	pVol = NODE_VOL(pNode);

	r = NODE_GET_WRITE_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get node read lock")));

	r = VOL_GET_READ_LOCK(pVol);
	FFAT_EOTO(r, (_T("fail to get vol read lock")), out_vol);

	VOL_INC_REFCOUNT(NODE_VOL(pNode));

	IF_UK (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		FFAT_LOG_PRINTF((_T("this is not a file ")));
		r = FFAT_EISDIR;
		goto out;
	}

	IF_UK (dwSize == 0)
	{
		pVC->dwTotalClusterCount	= 0;
		pVC->dwValidEntryCount		= 0;
		r = FFAT_OK;
		goto out;
	}

	// Check whether dwOffset is over file size.
	IF_UK (dwOffset >= NODE_S(pNode))
	{
		r = FFAT_ERANGE;
		goto out;
	}

	// Check overflow
	IF_UK ((dwOffset + dwSize) < dwOffset)
	{
		IF_UK ((dwOffset + dwSize) != 0)
		{
			// overflow
			r = FFAT_ERANGE;
			goto out;
		}

		// decrease one to prevent the overflow, The result will be same.
		dwSize--;
	}

	// check size
	dwClusterCountInNode	= ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));
	dwClusterCount			= ESS_MATH_CDB((dwOffset + dwSize), VOL_CS(pVol), VOL_CSB(pVol));
	IF_UK (dwClusterCountInNode < dwClusterCount)
	{
		// not enough clusters to access
		FFAT_PRINT_ERROR((_T("invalid get cluster off, dwOffset/dwSize/NodeSize:%d/%d/%d"), dwOffset, dwSize, NODE_S(pNode)));
		r = FFAT_ERANGE;
		goto out;
	}

	// get required cluster count to access
	dwClusterCount -= (dwOffset >> VOL_CSB(pVol));

	VC_INIT(pVC, (dwOffset & (~VOL_CSM(pVol))));

	if (dwClusterCount == 1)
	{
		// get cluster by offset
		r = ffat_misc_getClusterOfOffset(pNode, dwOffset, &pVC->pVCE[0].dwCluster, &stPAL, pCxt);
		FFAT_EO(r, (_T("fail get cluster of offset")));

		if (FFATFS_IS_EOF(VOL_VI(pVol), pVC->pVCE[0].dwCluster) == FFAT_TRUE)
		{
			r = FFAT_EEOF;
			goto out;
		}

		VC_VEC(pVC)	= 1;
		VC_CC(pVC)	= 1;
		pVC->pVCE[0].dwCount = 1;

		// set previous access location
		ffat_node_setPAL(pNode, &stPAL);
	}
	else
	{
		// get vectored cluster information by offset
		r = ffat_misc_getVectoredCluster(pVol, pNode, 0, dwOffset, dwClusterCount, pVC, NULL, pCxt);
		FFAT_EO(r, (_T("fail get vectored cluster")));
	}

	FFAT_ASSERT(pVC->dwTotalClusterCount <= dwClusterCount);

out:
	VOL_DEC_REFCOUNT(NODE_VOL(pNode));

	r |= VOL_PUT_READ_LOCK(pVol);

out_vol:
	r |= NODE_PUT_WRITE_LOCK(pNode);

	return r;
}


//=============================================================================
//
//	STATIC FUNCTIONS
//


/**
 * get new cluster count for data write
 * write에 필요한 new cluster의 수를 구한다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write start offset
 * @param		dwSize		: [IN] write size
 * @author		DongYoung Seo
 * @version		AUG-18-2006 [DongYoung Seo] First Writing
 * @version		JUL-31-2007 [DongYoung Seo] update for 4GB file support
 * @version		FEB-15-2009 [DongYoung Seo] change function scope to static
 */
static t_uint32
_getNewClusterCount(Node* pNode, t_uint32 dwOffset, t_int32 dwSize)
{
	const t_uint32	dw2GB = 0x80000000;		// 2GB
	t_uint32		dwNewSize;
	t_uint32		dwNewClusterCount;
	t_uint32		dwCurSize;
	t_uint32		dwBCCNew;			// base cluster count for new size
	t_uint32		dwBCCCur;			// base cluster count for current

	Vol*		pVol;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_VOL(pNode));
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	dwCurSize = NODE_S(pNode);
	dwNewSize = dwOffset + dwSize;

	if (dwNewSize > NODE_S(pNode))
	{
		if (dwNewSize >= dw2GB)
		{
			dwBCCNew = ESS_MATH_CDB(dw2GB, VOL_CS(pVol), VOL_CSB(pVol));
			dwNewSize = dwNewSize - dw2GB;
		}
		else
		{
			dwBCCNew = 0;
		}

		if (dwCurSize >= dw2GB)
		{
			dwBCCCur = ESS_MATH_CDB(dw2GB, VOL_CS(pVol), VOL_CSB(pVol));
			dwCurSize = dwCurSize - dw2GB;
		}
		else
		{
			dwBCCCur = 0;
		}

		dwNewClusterCount = ESS_MATH_CDB(dwNewSize, VOL_CS(pVol), VOL_CSB(pVol))
						- ESS_MATH_CDB(dwCurSize, VOL_CS(pVol), VOL_CSB(pVol))
						+ dwBCCNew - dwBCCCur;
	}
	else
	{
		// do not need more cluster
		dwNewClusterCount = 0;
	}

	return dwNewClusterCount;
}


/**
 * construct information to expand file
 *
 * @param		pNode				: [IN] target node pointer
 * @param		dwSize				: [IN] new size
 * @param		pdwPrevEOF			: [OUT] previous EOF
 * @param		pVC					: [OUT] vectored cluster to be allocated
 * @param		pdwNewClusterCount	: [OUT] total cluster count for expand node
 * @param		pCxt				: [IN] context of current operation
 * @author		GwangOk Go
 * @version		NOV-24-2008 [GwangOk Go] First Writing.
 * @version		FEB-14-2009 [DongYoung Seo] implement unfinished code for the node has additional cluster for size
 */
static FFatErr
_expandPrepare(Node* pNode, t_uint32 dwSize, t_uint32* pdwPrevEOF, FFatVC* pVC,
				t_uint32* pdwNewClusterCount, ComCxt* pCxt)
{
	t_uint32		dwCurClusterCount;	// current node cluster count
	t_uint32		dwNewClusterCount;	// cluster count for dwSize(new size)
	t_uint32		dwFreeCount;		// free cluster count
	t_uint32		dwSpareClusters;	// additional cluster count for the node size.
										// a node may have more clusters over it's size
	t_uint32		dwLastCluster;		// real last cluster number for node (end of cluster chain)

	FFatErr			r = FFAT_OK;
	Vol*			pVol;

	pVol = NODE_VOL(pNode);

	if (NODE_C(pNode) != 0)
	{
		if (pNode->dwLastCluster == 0)
		{
			r = ffat_misc_getClusterOfOffset(pNode, (NODE_S(pNode) - 1),
								&pNode->dwLastCluster, NULL, pCxt);
			FFAT_EO(r, (_T("fail to get cluster of offset")));
		}

		// 이 작업을 하는 이유는 여분의 cluster가 있을 경우를 대비하기 위해서이다.
		r = ffat_misc_getLastCluster(pVol, pNode->dwLastCluster, &dwLastCluster,
								&dwSpareClusters, pCxt);
		FFAT_EO(r, (_T("fail to get last cluster")));

		FFAT_ASSERT(dwLastCluster == pNode->dwLastCluster);		// never occur on BTFS

		*pdwPrevEOF = dwLastCluster;

		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), *pdwPrevEOF) == FFAT_TRUE);
	}
	else
	{
		dwSpareClusters = 0;

		FFAT_ASSERT(NODE_S(pNode) == 0);

		*pdwPrevEOF = 0;

		VC_O(pVC)	= VC_NO_OFFSET;
		VC_FC(pVC)	= 0;
	}

	dwCurClusterCount = ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));
	dwNewClusterCount = ESS_MATH_CDB(dwSize, VOL_CS(pVol), VOL_CSB(pVol));

	if (dwNewClusterCount <= dwSpareClusters)
	{
		// no need to allocate cluster
		*pdwNewClusterCount = 0;
		r = FFAT_OK;
		goto out;
	}
	else
	{
		dwCurClusterCount += dwSpareClusters;
		dwNewClusterCount -= dwSpareClusters;
	}

	FFAT_ASSERT(dwNewClusterCount >= dwCurClusterCount);

	if (dwNewClusterCount > dwCurClusterCount)
	{
		// set offset of pVC
		VC_O(pVC) = dwCurClusterCount << VOL_CSB(pVol);

		// get free clusters
		r = ffat_misc_getFreeClusters(pNode, (dwNewClusterCount - dwCurClusterCount), 
								pVC, *pdwPrevEOF, &dwFreeCount, FAT_ALLOCATE_NONE, pCxt);

		FFAT_ASSERT((r == FFAT_OK) ? ((dwNewClusterCount - dwCurClusterCount) == dwFreeCount) : FFAT_TRUE);
		FFAT_ASSERT((r == FFAT_OK) ? (VC_CC(pVC) == dwFreeCount) : FFAT_TRUE);

		// if VC is full or free cluster is smaller than requested, FFAT_ENOSPC is returned.
		if (r == FFAT_ENOSPC)
		{
			IF_UK (dwFreeCount > 0)
			{
				// dwFreeCount is total free count of this volume
				r = FFAT_OK;

				FFAT_ASSERT(dwFreeCount == VC_CC(pVC));
				FFAT_ASSERT(VC_IS_FULL(pVC) == FFAT_TRUE);
			}
		}

		FFAT_EO(r, (_T("fail to get free cluster")));
	}

	*pdwNewClusterCount = dwNewClusterCount - dwCurClusterCount;

out:
	return r;
}


/**
 * construct information to shrink file
 *
 * @param		pNode			: [IN] target node pointer
 * @param		dwSize			: [IN] new size
 * @param		pdwNewEOF		: [OUT] new EOF
 * @param		pVC				: [OUT] vectored cluster to be deallocated
 * @param		pCxt			: [IN] context of current operation
 * @author		GwangOk Go
 * @version		NOV-24-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_shrinkPrepare(Node* pNode, t_uint32 dwSize, t_uint32* pdwNewEOF, FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;

	t_uint32		dwCurClusterCount;	// current node cluster count
	t_uint32		dwNewClusterCount;	// cluster count for dwSize(new size)

	pVol = NODE_VOL(pNode);

	dwCurClusterCount = ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));
	dwNewClusterCount = ESS_MATH_CDB(dwSize, VOL_CS(pVol), VOL_CSB(pVol));

	if (dwCurClusterCount == dwNewClusterCount)
	{
		// new last offset is within current last cluster
		// no need to deallocate cluster
		*pdwNewEOF	= 0;
		VC_O(pVC)	= VC_NO_OFFSET;
		VC_FC(pVC)	= 0;
	}
	else
	{
		// overflow can not be happened at here.
		FFAT_ASSERT(dwSize < (dwSize + VOL_CS(pVol)));

		if (dwSize == 0)
		{
			// new size is '0'
			*pdwNewEOF	= 0;
			VC_O(pVC)	= 0;
		}
		else
		{
			// new size is greater than '0'

			// get new EOC
			r = ffat_misc_getClusterOfOffset(pNode, (dwSize - 1), pdwNewEOF, NULL, pCxt);
			FFAT_ER(r, (_T("fail to get cluster of offset")));

			// offset of VC is first offset of next cluster
			VC_O(pVC)	= (dwSize + VOL_CS(pVol) - 1) & (~VOL_CSM(pVol));
		}

		// get clusters to be deallocated
		r = ffat_misc_getVectoredCluster(pVol, pNode, 0, (dwSize + VOL_CS(pVol) - 1),
										0, pVC, NULL, pCxt);
		FFAT_ER(r, (_T("fail to get vectored cluster")));

		FFAT_ASSERT((dwSize == 0) ? (VC_FC(pVC) == NODE_C(pNode)) : FFAT_TRUE);
		FFAT_ASSERT((VC_IS_FULL(pVC) == FFAT_FALSE) ? (VC_CC(pVC) == (dwCurClusterCount - dwNewClusterCount)) : FFAT_TRUE);
	}

	return FFAT_OK;
}

/**
* construct information to shrink file which is dirty-sized node to be recovered
*
* It uses only FFATFS module not Addon module
* Addon module assumes that cluster can not be allocated more than file size.
*
* @param		pNode			: [IN] target node pointer
* @param		dwSize			: [IN] new size
* @param		pdwNewEOF		: [OUT] new EOF
* @param		pVC				: [OUT] vectored cluster to be deallocated
* @param		pCxt			: [IN] context of current operation
* @author		JW Park
* @version		OCT-22-2009 [JW Park] First Writing.
*/
static FFatErr
_shrinkPrepareForRecoveryDirtySize(Node* pNode, t_uint32 dwSize, t_uint32* pdwNewEOF, FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	t_uint32		dwCluster;

	FFAT_ASSERT(NODE_C(pNode) != 0);

	pVol = NODE_VOL(pNode);

	if (dwSize == 0)
	{
		// size is '0'
		*pdwNewEOF	= 0;
		VC_O(pVC)	= 0;
		dwCluster	= NODE_C(pNode);
	}
	else
	{
		// Get EOF
		r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), NODE_C(pNode), (dwSize - 1), pdwNewEOF, pCxt);
		FFAT_EO(r, (_T("fail to get cluster of offset ")));

		// offset of VC is first offset of next cluster
		VC_O(pVC)	= (dwSize + VOL_CS(pVol) - 1) & (~VOL_CSM(pVol));

		// check after EOF
		r = FFATFS_GetNextCluster(VOL_VI(pVol), *pdwNewEOF, &dwCluster, pCxt);
		FFAT_EO(r, (_T("fail to get cluster of offset ")));

		if (FFATFS_IS_EOF(VOL_VI(pVol), dwCluster) == FFAT_TRUE)
		{
			// no need to deallocate cluster
			*pdwNewEOF	= 0;
			VC_O(pVC)	= VC_NO_OFFSET;
			VC_FC(pVC)	= 0;

			return FFAT_OK;
		}
	}

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

	r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwCluster, 0, pVC, FFAT_FALSE, pCxt);
	FFAT_EO(r, (_T("fail to get cluster of offset ")));

	FFAT_ASSERT((dwSize == 0) ? (VC_FC(pVC) == NODE_C(pNode)) : FFAT_TRUE);
	
out:
	return r;
}


/**
 * extend file size
 *
 * @param		pNode			: [IN] target node pointer
 * @param		dwSize			: [IN] New node size
 * @param		dwPrevEOF		: [IN] previous EOF
 * @param		pVC				: [IN] vectored cluster to be allocated
 * @param		pdwNewClusterCount	: [IN] cluster count to be allocated
 * @param		dwCSFlag		: [IN] change size flag
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] get information to expand from _expandPrepare()
 * @version		APR-13-2009 [JeongWoo Park] Add the code to set / reset first cluster / size of node
 */
static FFatErr
_expand(Node* pNode, t_uint32 dwSize, t_uint32 dwPrevEOF, FFatVC* pVC,
		t_uint32 dwNewClusterCount, FFatChangeSizeFlag dwCSFlag,
		FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr				r;
	FatAllocateFlag		dwFAFlag = FAT_ALLOCATE_NONE;		// alloc flag

	Vol*				pVol;
	NodePAL				stPAL;								//	previous access location
	FFatVCE				stLastVCE;
	t_uint32			dwOriginalSize;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNode->dwSize < dwSize);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);
	// create를 위한 change size operation은 항상 크기가 0으로 된다. 그러므로 이 함수에는 오지 않는다.
	FFAT_ASSERT((dwCSFlag & FFAT_CHANGE_SIZE_FOR_CREATE) == 0);
	FFAT_ASSERT(pVC);

	pVol = NODE_VOL(pNode);

	dwFAFlag = FAT_ALLOCATE_NONE;
	dwOriginalSize = NODE_S(pNode);

	if (dwNewClusterCount != 0)
	{
		FFAT_ASSERT(VC_VEC(pVC) > 0);
		FFAT_ASSERT(VC_CC(pVC) > 0);
		FFAT_ASSERT(dwNewClusterCount >= VC_CC(pVC));

		r = ffat_misc_makeClusterChainVC(pNode, dwPrevEOF, pVC, FAT_UPDATE_NONE,
										dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to make cluster chain")));

		dwNewClusterCount -= VC_CC(pVC);
		if (dwNewClusterCount > 0)
		{
			FFAT_ASSERT(VC_IS_FULL(pVC) == FFAT_TRUE);

			if ((dwCSFlag & FFAT_CHANGE_SIZE_NO_INIT_CLUSTER) == 0)
			{
				dwFAFlag |= FAT_ALLOCATE_INIT_CLUSTER;
			}

			FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_ALLOCATE_CLUSTER_BEFORE);

			r = ffat_misc_allocateCluster(pNode, VC_LC(pVC), dwNewClusterCount,
										pVC, &stLastVCE, dwFAFlag, dwCacheFlag, pCxt);
			FFAT_EO(r, (_T("fail to allocate cluster")));

			pNode->dwLastCluster = stLastVCE.dwCluster + stLastVCE.dwCount - 1;

			stPAL.dwCluster	= stLastVCE.dwCluster;
			stPAL.dwCount	= stLastVCE.dwCount;

			FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_ALLOCATE_CLUSTER_AFTER);
		}
		else
		{
			pNode->dwLastCluster = VC_LC(pVC);

			stPAL.dwCluster	= pVC->pVCE[VC_LEI(pVC)].dwCluster;
			stPAL.dwCount	= pVC->pVCE[VC_LEI(pVC)].dwCount;
		}

		// offset should be aligned by cluster
		stPAL.dwOffset	= ((dwSize - 1) - ((stPAL.dwCount - 1) << VOL_CSB(pVol))) & (~VOL_CSM(pVol));

		// set previous access location
		ffat_node_setPAL(pNode, &stPAL);

		if (NODE_C(pNode) == 0)
		{
			// update short file name entry
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC)) == FFAT_TRUE);
			NODE_C(pNode) = VC_FC(pVC);
		}
	}
	else
	{
		FFAT_ASSERT(VC_CC(pVC) == 0);

		if ((pNode->dwLastCluster == 0) && (dwPrevEOF != 0))
		{
			pNode->dwLastCluster = dwPrevEOF;
		}
	}

	// For using GFS at next ffat_node_readWriteInit(), update the file size
	NODE_S(pNode) = dwSize;

	if ((dwCSFlag & FFAT_CHANGE_SIZE_NO_INIT_CLUSTER) == 0)
	{
		// initialize expanded area
		r = ffat_node_readWriteInit(pNode, dwOriginalSize, NULL, (dwSize - dwOriginalSize), pVC,
									NULL, (dwCacheFlag | FFAT_CACHE_DIRECT_IO), FFAT_RW_INIT, pCxt);
		FFAT_EO(r, (_T("fail to init node data")));
	}

	r = FFAT_OK;

out:
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to expand a node - undoing~")));

		if (VC_CC(pVC) > 0)
		{
			// deallocate allocated cluster, ignore error
			ffat_misc_deallocateCluster(pNode, dwPrevEOF, VC_FC(pVC),
								0, NULL, dwFAFlag, dwCacheFlag, pCxt);

			// if undo to size 0, reset start cluster of node
			if (NODE_C(pNode) == VC_FC(pVC))
			{
				NODE_C(pNode) = 0;
			}
		}

		NODE_S(pNode) = dwOriginalSize;
	}

	return r;
}


/**
 * file의 크기를 감소 시킨다.(shrink file size)
 *
 * @param		pNode		: [IN] target node pointer
 * @param		dwSize		: [IN] New node size
 * @param		dwNewEOF	: [IN] new last cluster number
 * @param		pVC			: [IN] vectored cluster information to be deallocated
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] get information to shrink from _shrinkPrepare()
 */
static FFatErr
_shrink(Node* pNode, t_uint32 dwSize, t_uint32 dwNewEOF, FFatVC* pVC, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FatAllocateFlag		dwFAFlag = FAT_ALLOCATE_NONE;
	FFatErr				r;

	Vol*		pVol;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNode->dwSize >= dwSize);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);
	FFAT_ASSERT(pNode->dwSize >= 0);

	pVol = NODE_VOL(pNode);

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNode->dwCluster) == FFAT_TRUE);

	IF_UK (VC_CC(pVC) == 0)
	{
		return FFAT_OK;
	}

	FFAT_ASSERT((dwSize == 0) ? ((dwNewEOF == 0) && (VC_FC(pVC) == NODE_C(pNode))) : FFAT_TRUE);

	r = ffat_misc_deallocateCluster(pNode, dwNewEOF, VC_FC(pVC), 0, pVC, dwFAFlag,
					dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to deallocate cluster")));

	if (dwSize == 0)
	{
		pNode->dwCluster = 0;
	}

	pNode->dwLastCluster = dwNewEOF;

out:
	ffat_node_initPAL(pNode);	// in shrink, initialize PAL info in success or fail

	return r;
}


/**
 * read data from a node
 *
 * 읽으려고 하는 크기보다 작을 경우 가능한 크기까지만 읽는다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] read start offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] read size
 * @param		dwReadflag	: [IN] flag for read operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		OCT-14-2008 [GwangOk Go] remove ffat_misc_getClusterOfOffset() & modify ffat_misc_getVectoredCluster()
 * @version		MAY-26-2009 [JeongWoo Park] Add the consideration of the overflow of dwOffset and dwSize
 */
static t_int32
_read(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
			FFatReadFlag dwReadFlag, ComCxt* pCxt)
{
	t_uint32			dwClusterCount;
	Vol*				pVol;
	FFatErr				r;
	FFatVC				stVC;
	t_int32				dwReadSize = 0;
	t_uint32			dwLastOffset;			// last read offset
	FFatCacheFlag		dwCacheFlag = FFAT_CACHE_NONE;	// flag for cache operation
	NodePAL				stPAL = {FFAT_NO_OFFSET, 0, 0};		// previous access location

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pBuff);

	r = _checkAndAdjustSize(pNode, dwOffset, &dwSize, FFAT_TRUE);
	FFAT_ER(r, (_T("invalid offset or size for reading")));

	IF_UK (dwSize == 0)
	{
		// Nothing to read
		return 0;
	}

	FFAT_ASSERT(dwSize > 0);

	pVol		= NODE_VOL(pNode);
	stVC.pVCE	= NULL;

	if (dwReadFlag & FFAT_READ_DIRECT_IO)
	{
		dwCacheFlag |= FFAT_CACHE_DIRECT_IO;
	}

#ifdef FFAT_DIRECT_IO_TRIGGER
	if (dwSize >= FFAT_DIRECT_IO_TRIGGER)
	{
		dwCacheFlag |= FFAT_CACHE_DIRECT_IO;
	}
#endif

	// allocate memory for Vectored Cluster Information
	stVC.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC.pVCE != NULL);

	// get required cluster count for read
	dwLastOffset	= (dwOffset & VOL_CSM(pVol)) + dwSize - 1;

	// read에 필요한 total cluster의 수를 구한다.
	dwClusterCount	= ESS_MATH_CDB((dwLastOffset + 1), VOL_CS(pVol), VOL_CSB(pVol));

	stVC.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

	VC_INIT(&stVC, dwOffset & (~VOL_CSM(pVol)));

	// get vectored cluster information by offset
	r = ffat_misc_getVectoredCluster(pVol, pNode, 0, dwOffset, dwClusterCount, &stVC, &stPAL, pCxt);
	FFAT_EO(r, (_T("fail get vectored cluster")));

	FFAT_ASSERT((VC_TEC(&stVC) == VC_VEC(&stVC)) ? FFAT_TRUE : dwClusterCount == VC_CC(&stVC));

	// read
	r = ffat_node_readWriteInit(pNode, dwOffset, pBuff, (t_uint32)dwSize, &stVC, (t_uint32*)&dwReadSize,
								dwCacheFlag, FFAT_RW_READ, pCxt);
	FFAT_EO(r, (_T("fail to write data to node")));

	// update directory entry

	// update DE
	if ((dwReadFlag & FFAT_READ_UPDATE_ADATE) && (VOL_IS_RDONLY(pVol) == FFAT_FALSE))
	{
		r = ffat_node_updateSFNE(pNode, pNode->dwSize, 0, 0,
						(FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_WRITE_DE),
						dwCacheFlag, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to write SFNE")));

			// restore directory entry
			goto out;
		}
	}

	r = ffat_addon_afterReadFile(pNode, &stVC, pCxt);
	FFAT_EO(r, (_T("fail to after operation for read")));

out:
	FFAT_LOCAL_FREE(stVC.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	IF_LK (r >= 0)
	{
		// set previous access location
		ffat_node_setPAL(pNode, &stPAL);

		return dwReadSize;
	}

	return r;
}


/**
 * write data to a file
 *
 * only for a file.!!!!
 * DO NOT USER THIS FUNCTION FOR DIRECTORY, NEVER!!!
 * (FILE & DIRECTORY HAS DIFFERECT WRITE POLICY)
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwWriteFlag	: [IN] flag for write operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		MAY-15-2007 [DongYoung Seo] update DE when the size is changed
											even if dwWriteFlag is FFAT_WRITE_NO_META_UPDATE
 * @version		OCT-12-2008 [DongYoung Seo] add error checking code after _writePrepareClusters()
 * @version		OCT-14-2008 [GwangOk Go] allocate free cluster in CORE module and give this info to ADDON module
 * @version		OCT-22-2008 [GwangOk Go] even if not enough free cluster, write existing free cluster
 * @version		OCT-28-2008 [DongYoung Seo] add code to check not enough free space to expanding file to the write start offset
 * @version		OCT-30-2008 [DongYoung Seo] even if not enough free cluster, write existing free cluster
 * @version		APR-13-2009 [JeongWoo Park] Add the restore code of node information for error
 *											Bug fix for setting of first cluster of node with many fragment
 * @version		Aug-29-2009 [SangYoon Oh] Remove the parameter stVC_Cur when calling ffat_addon_afterWriteFile
 * @version		OCT-27-2009 [JW Park] Add the consideration about dirty-size node
 * @version		DEC-14-2009 [JW Park] Bug fix about wrong dirty-size state
 *									at the operation that requires more free clusters than existed.
 */
static t_int32
_write(Node* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize,
				FFatWriteFlag dwWriteFlag, ComCxt* pCxt)
{
	FFatErr				r;
	t_int32				dwWrittenSize = 0;
	t_uint32			dwPrevEOC = 0;				// node의 last offset에 해당하는 cluster
	t_uint32			dwNewClusters = 0;			// new cluster count for write
	t_uint32			dwOrgNodeCluster;			// original node cluster
	t_uint32			dwOrgNodeSize;				// original node size

	FatAllocateFlag		dwAllocFlag;				// allocation flag
	FatDeUpdateFlag		dwDeUpdateFlag;				// DE update flag

	FFatVC				stVC_Cur;					// storage for current clusters
	FFatVC				stVC_New;					// storage for new clusters

	FFatCacheFlag		dwCacheFlag;				// flag for cache operation
	FFatCacheFlag		dwCacheFlagMeta;			// cache flag for meta-data
	t_boolean			bAddonInvoked;				// boolean to check ADDON module is invoked or not.
	t_boolean			bCoreLocked = FFAT_FALSE;
	Vol*				pVol;

// debug begin
#ifdef FFAT_DEBUG
	t_boolean			b1stRetry = FFAT_TRUE;		// boolean to check write retry count.
	// there must be only one retry to write possible byte
#endif
// debug end

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);		// THIS FUNCTION IS ONLY FOR FILE
														// DO NOT USE FOR DIRECTORY

	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);	// this is for only file.

	r = _checkAndAdjustSize(pNode, dwOffset, &dwSize, FFAT_FALSE);
	FFAT_ER(r, (_T("invalid offset or size for writing")));

	IF_UK (dwSize == 0)
	{
		// too big offset and size
		// 0을 리턴할지 ERANGE 에러를 리턴할지는 nestle에서 0byte짜리 size를 넘겨주느냐에 따라 결정
		// 0을 넘겨주지 않으면 ERANGE 그렇지 않으면 0을 리턴하고 ffat_nestle.c의 AST 삭제
		return FFAT_ERANGE;
	}

	FFAT_ASSERT(dwSize > 0);

	pVol = NODE_VOL(pNode);

	stVC_Cur.pVCE	= NULL;
	stVC_New.pVCE	= NULL;

	dwAllocFlag		= FAT_ALLOCATE_NONE;
	dwCacheFlag		= FFAT_CACHE_NONE;
	dwCacheFlagMeta	= FFAT_CACHE_NONE;

	if (dwWriteFlag & FFAT_WRITE_DIRECT_IO)
	{
		dwCacheFlag		|= FFAT_CACHE_DIRECT_IO;
	}

#ifdef FFAT_DIRECT_IO_TRIGGER
	if (dwSize >= FFAT_DIRECT_IO_TRIGGER)
	{
		dwCacheFlag		|= FFAT_CACHE_DIRECT_IO;
	}
#endif

	if (dwWriteFlag & FFAT_WRITE_SYNC)
	{
		dwCacheFlag		|= FFAT_CACHE_SYNC;
		dwCacheFlagMeta	|= FFAT_CACHE_SYNC;
	}
	else
	{
		if (VOL_IS_SYNC_META(pVol) == FFAT_TRUE)
		{
			dwCacheFlagMeta	|= FFAT_CACHE_SYNC;
		}
		
		if (VOL_IS_SYNC_USER(pVol) == FFAT_TRUE)
		{
			dwCacheFlag		|= FFAT_CACHE_SYNC;
		}
	}

	// remove dirty-size flag at sync mode.
	if (dwCacheFlag & FFAT_CACHE_SYNC)
	{
		dwWriteFlag &= ~FFAT_WRITE_RECORD_DIRTY_SIZE;
	}
	
	// allocate memory for Vectored Cluster Information
	stVC_Cur.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC_Cur.pVCE != NULL);
	stVC_Cur.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

	stVC_New.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_ASSERT(stVC_New.pVCE != NULL);
	stVC_New.dwTotalEntryCount		= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

	// lock CORE for free cluster sync
	r = ffat_core_lock(pCxt);
	FFAT_EOTO(r, (_T("fail to lock CORE")), out_nothing_done);

	bCoreLocked = FFAT_TRUE;

	// keep original size and cluster for restore
	dwOrgNodeCluster	= NODE_C(pNode);
	dwOrgNodeSize		= NODE_S(pNode);

retry:		// retry to write possible count of byte

	// set update flag of DE
	dwDeUpdateFlag = FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_MTIME
					| FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_WRITE_DE;

	if (dwWriteFlag & FFAT_WRITE_RECORD_DIRTY_SIZE)
	{
		// start the dirty size state if size is changed
		if ((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE)
			&&
			(dwOrgNodeSize < (dwOffset + dwSize)))
		{
			NODE_SET_DIRTY_SIZE_BEGIN(pNode);
		}
	}
	else
	{
		// If normal write like sync write, then remove the dirty-size state
		dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
	}

	bAddonInvoked = FFAT_FALSE;

	VC_INIT(&stVC_New, 0);			// do not remove this list

	if (dwOffset <= dwOrgNodeSize)
	{
		// offset이 file size보다 작거나 같은 경우
		VC_INIT(&stVC_Cur, (dwOffset & (~VOL_CSM(pVol))));
	}
	else
	{
		// offset이 file size보다 커서 init cluster가 필요
		VC_INIT(&stVC_Cur, (dwOrgNodeSize & (~VOL_CSM(pVol))));
	}

	// get cluster chain & free cluster
	r = _writePrepareClusters(pNode, dwOffset, dwSize, &dwPrevEOC, &dwNewClusters,
						&stVC_Cur, &stVC_New, pCxt);
	FFAT_EO(r, (_T("Fail to prepare write opertion")));

	FFAT_ASSERT(dwNewClusters >= VC_CC(&stVC_New));

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_LOG_BEFORE);

	r = ffat_addon_writeFile(pNode, (dwOffset + dwSize), dwPrevEOC,
							&stVC_Cur, &stVC_New, dwWriteFlag, &dwCacheFlagMeta, pCxt);
	FFAT_EO(r, (_T("fail to write data on ADDON module")));

	bAddonInvoked = FFAT_TRUE;

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_LOG_AFTER);

	IF_UK (r == FFAT_DONE)
	{
		// node write operation is done !!
		r = FFAT_OK;
		goto out;
	}

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_ALLOCATE_CLUSTER_BEFORE);

	if (dwOrgNodeSize <= dwOffset)
	{
		dwWriteFlag |= FFAT_WRITE_NO_INIT_CLUSTER;
	}

	// NO INIT CLUSTER FLAG가 있을 경우 cluster를 0x00로 초기화 하지 않음
	if ((dwWriteFlag & FFAT_WRITE_NO_INIT_CLUSTER) == 0)
	{
		dwAllocFlag |= FAT_ALLOCATE_INIT_CLUSTER;
	}

	r = _writeAllocClusters(pNode, dwOffset, dwSize, dwPrevEOC, dwNewClusters,
							&stVC_Cur, &stVC_New, dwCacheFlagMeta, dwAllocFlag, pCxt);
	FFAT_EO(r, (_T("fail to allocate required clusters")));

	// by iris : for HSDPA, cluster가 이미 존재하여 할당 받을게 없다면, default로 DE도 update하지 않음.
	if (VC_CC(&stVC_New) == 0)
	{
		dwWriteFlag = dwWriteFlag | FFAT_WRITE_NO_META_UPDATE;
	}

	// lock CORE for free cluster sync
	r = ffat_core_unlock(pCxt);
	FFAT_EOTO(r, (_T("fail to unlock CORE")), out_dealloc_fat);

	bCoreLocked = FFAT_FALSE;

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_ALLOCATE_CLUSTER_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DATA_WRITE_BEFORE);
	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_DATA_BEFORE);

	// write
	// do real-write,이제 cluster 할당도 끝났으니 write를 해볼까나..
	r = ffat_node_readWriteInit(pNode, dwOffset, pBuff, (t_uint32)dwSize, &stVC_Cur, (t_uint32*)&dwWrittenSize,
								dwCacheFlag, FFAT_RW_WRITE, pCxt);
	FFAT_EOTO(r, (_T("fail to write data to node")), out_dealloc_fat);

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_DATA_AFTER);
	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DATA_WRITE_AFTER);

	FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_METADATA_BEFORE);

	if (((dwWriteFlag & FFAT_WRITE_NO_META_UPDATE) == 0)
		||
		(dwOrgNodeCluster == 0)
		||
		(dwOrgNodeSize < (dwOffset + dwWrittenSize))
		||
		((NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE) &&		// remove the dirty-size state if synchronous (over)write
		((dwWriteFlag & FFAT_WRITE_RECORD_DIRTY_SIZE) == 0))
		)
	{
		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DE_UPDATE_BEFORE);

		if (dwOrgNodeCluster == 0)
		{
			// update directory entry
			// update cluster
			// [BUG FIX] : By using first cluster of stVC_Cur, wrong start cluster can be set with many fragment status.
			r = ffat_node_updateSFNE(pNode, (dwOffset + dwWrittenSize), 0, VC_FC(&stVC_New),
						(dwDeUpdateFlag | FAT_UPDATE_DE_CLUSTER), dwCacheFlagMeta, pCxt);
		}
		else
		{
			// update directory entry
			r = ffat_node_updateSFNE(pNode, ESS_GET_MAX(dwOrgNodeSize, (dwOffset + dwWrittenSize)), 
							0, 0, dwDeUpdateFlag, dwCacheFlagMeta, pCxt);
		}

		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to write SFNE")));

			// restore directory entry
			goto out_dealloc_fat;
		}

		FFAT_DEBUG_EIO_CHECK(FFAT_EIO_TEST_WRITE_METADATA_AFTER);
		FFAT_DEBUG_LRT_CHECK(FFAT_LRT_WRITE_DE_UPDATE_AFTER);
	}

	FFAT_ASSERT(ffat_share_checkNodeLastClusterInfo(pNode, pCxt) == FFAT_OK);
	FFAT_ASSERT(ffat_share_checkNodePAL(pNode, pCxt) == FFAT_OK);

out_dealloc_fat:
	IF_UK ((r < 0) && (dwNewClusters != 0))
	{
		// If dirty-size-begin is set, remove this flag for error.
		IF_UK (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
		{
			NODE_CLEAR_DIRTY_SIZE_BEGIN(pNode);
			NODE_CLEAR_DIRTY_SIZE(pNode);		// dirty-size flag can be set at log module.
		}

		// 오류가 발생되었고 CLUSTER ALLOCATION이 수행되었을 경우
		// Cluster allocation을 초기화 하여야 한다.
		// 현재 VC에는 allocation 되지 않은 cluster 정보도 포함되어 있으므로 전달하지 않는다.
		FFAT_ASSERT(VC_CC(&stVC_New) > 0);
		FFAT_ASSERT(VC_VEC(&stVC_New) > 0);
		ffat_misc_deallocateCluster(pNode, dwPrevEOC, VC_FC(&stVC_New), 0,
						&stVC_New, (dwAllocFlag | FAT_DEALLOCATE_FORCE), (dwCacheFlagMeta | FFAT_CACHE_FORCE), pCxt);

		// restore original node information
		ffat_node_updateSFNE(pNode, dwOrgNodeSize, 0, dwOrgNodeCluster,
							(FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_CLUSTER |
							FAT_UPDATE_DE_WRITE_DE | FAT_UPDATE_DE_FORCE),
							dwCacheFlagMeta, pCxt);
	}

out:
	IF_LK (bAddonInvoked == FFAT_TRUE)
	{
		r |= ffat_addon_afterWriteFile(pNode, dwOrgNodeSize, dwCacheFlagMeta, FFAT_IS_SUCCESS(r), pCxt);
	}

	// check do retry or not.
	// write possible bytes to file when there is some free clusters on the volume
	//	although there is not enough free space for requested size.
	IF_UK (r == FFAT_ENOSPC)
	{
		// check this is the first try. if it is not first try it is error.
		//	because _writeGetAvailableSize() returns writable byte count.
		FFAT_ASSERT((b1stRetry == FFAT_TRUE) ? (FFAT_FALSE == (b1stRetry = FFAT_FALSE)) : FFAT_FALSE);

		// If dirty-size-begin is set, remove this flag before re-try.
		IF_UK (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
		{
			NODE_CLEAR_DIRTY_SIZE_BEGIN(pNode);
			NODE_CLEAR_DIRTY_SIZE(pNode);		// dirty-size flag can be set at log module.
		}

		r = _writeGetAvailableSize(pNode, dwOffset, &dwSize, pCxt);
		if (r == FFAT_OK)
		{
			goto retry;
		}
	}

	// If dirty-size-begin is set, remove this flag for error.
	IF_UK (NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_TRUE)
	{
		FFAT_ASSERT(r < 0);

		NODE_CLEAR_DIRTY_SIZE_BEGIN(pNode);
		NODE_CLEAR_DIRTY_SIZE(pNode);		// dirty-size flag can be set at log module.
	}

out_nothing_done:
	IF_LK (bCoreLocked == FFAT_TRUE)
	{
		// lock CORE for free cluster sync
		r |= ffat_core_unlock(pCxt);
	}

	IF_LK (r == FFAT_OK)
	{
		return dwWrittenSize;
	}

	IF_UK (r != FFAT_ENOSPC)
	{
		// there is some write error except for ENOSPC
		// initialize last cluster & previous last location
		pNode->dwLastCluster = 0;
		ffat_node_initPAL(pNode);
	}

	FFAT_ASSERT(NODE_IS_DIRTY_SIZE_BEGIN(pNode) == FFAT_FALSE);

	return r;
}


/**
 * get available file size for write
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write start offset
 * @param		dwSize		: [OUT] available write wise
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOSPC	: There is no free space
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		OCT-01-2008 [DongYoung Seo] First Writing.
 */
static FFatErr
_writeGetAvailableSize(Node* pNode, t_uint32 dwOffset, t_int32* pdwSize, ComCxt* pCxt)
{
	Vol*		pVol;			// volume pointer
	t_uint32	dwCount;		// available cluster count
	t_uint32	dwCC_Cur;		// current cluster count
	t_uint32	dwCC_Offset;	// cluster count for offset
	FFatErr		r;

	pVol = NODE_VOL(pNode);

	r = ffat_addon_getAvailableClusterCountForNode(pNode, &dwCount, pCxt);
	FFAT_ER(r, (_T("fail to get available free cluster count")));

	if (r != FFAT_DONE)
	{
		FFAT_ASSERT(r == FFAT_OK);

		// get Available cluster count
		r = FFATFS_UpdateVolumeStatus(VOL_VI(pVol), NULL, 0, pCxt);
		FFAT_ER(r, (_T("fail to update volume status")));

		dwCount = VOL_FCC(pVol);
	}

	dwCC_Cur	= ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));
	dwCC_Offset	= ESS_MATH_CDB(dwOffset, VOL_CS(pVol), VOL_CSB(pVol));

	IF_LK (dwCC_Cur >= dwCC_Offset)
	{
		// write start offset is within the file or within the last cluster
		dwCount += (dwCC_Cur - dwCC_Offset);
	}
	else
	{
		// start offset is over the last cluster of the file
		if ((dwCC_Offset - dwCC_Cur) > dwCount)
		{
			// and there is not enough free cluster to reach the write start offset
			FFAT_PRINT_DEBUG((_T("There is not enough free cluster to start write")));
			return FFAT_ENOSPC;
		}

		dwCount -= (dwCC_Offset - dwCC_Cur);
	}

	*pdwSize = (dwCount << VOL_CSB(pVol));

	if (dwOffset & VOL_CSM(pVol))
	{
		*pdwSize += (VOL_CS(pVol) - (dwOffset & VOL_CSM(pVol)));
	}

	if (*pdwSize ==0)
	{
		return FFAT_ENOSPC;
	}

	return FFAT_OK;
}


/**
 * prepare cluster for write
 * in case of append write, get free cluster
 * in case of over write, get cluster chain
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwOffset		: [IN] write start offset
 * @param		dwSize			: [IN] write size
 * @param		pdwPrevEOC		: [OUT] previous EOC
 * @param		pdwNewClusters	: [OUT] the number of clusters for write
 * @param		pVC_Cur			: [OUT] current cluster chain
 * @param		pVC_New			: [OUT] free clusters newly allocated (may not contain any free cluster)
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: There is not enough free space
 * @return		else			: error
 * @author		GwangOk Go
 * @version		OCT-10-2008 [GwangOk Go] First Writing.
 * @version		OCT-12-2008 [DongYoung Seo] change FFAT_CACHE_SYNC setting code at end of this function
								*pdwNewClusters |= FFAT_CACHE_SYNC; ==> *pdwCacheFlag |= FFAT_CACHE_SYNC;
 * @version		OCT-29-2008 [DongYoung Seo] remove parameter *pdwNewClusters and *dwRequestClusters
 */
static FFatErr
_writePrepareClusters(Node* pNode, t_uint32 dwOffset, t_int32 dwSize, t_uint32* pdwPrevEOC,
						t_uint32* pdwNewClusters, FFatVC* pVC_Cur, FFatVC* pVC_New, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;
	Vol*			pVol;
	t_uint32		dwLastCluster;			// node의 last offset에 해당하는 cluster
	t_uint32		dwFreeCount = 0;		// free cluster count
	t_uint32		dwClusterCount;			// cluster count for writing
	t_uint32		dwNewClusters;			// new cluster count for write

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwPrevEOC);
	FFAT_ASSERT(pVC_Cur);
	FFAT_ASSERT(pVC_New);
	FFAT_ASSERT(pCxt);

	pVol = NODE_VOL(pNode);

	// check is there enough free cluster
	dwNewClusters = _getNewClusterCount(pNode, dwOffset, dwSize);

	// get total cluster count for data write
	// data write에 필요한 total cluster의 수를 구한다. (all cluster for over write and append write)
	dwClusterCount = ESS_MATH_CDB((dwOffset + dwSize),  VOL_CS(pVol), VOL_CSB(pVol))
						- ESS_MATH_CDB(VC_O(pVC_Cur),  VOL_CS(pVol), VOL_CSB(pVol));

	if (dwNewClusters > VOL_FCC(pVol))
	{
		// not enough free cluster
		r = FFAT_ENOSPC;
		goto out;
	}

	if (pNode->dwSize == 0)
	{
		*pdwPrevEOC			= 0;
		dwLastCluster		= 0;
	}
	else
	{
		if (NODE_S(pNode) <= dwOffset)
		{
			// the update start offset is over file size.
			if (pNode->dwLastCluster == 0)
			{
				// last cluster in invalid. get last cluster from last offset
				r = ffat_misc_getClusterOfOffset(pNode, (NODE_S(pNode) - 1),
								&pNode->dwLastCluster, NULL, pCxt);
				FFAT_EO(r, (_T("Fail to get cluster")));
			}

			dwLastCluster = pNode->dwLastCluster;

			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwLastCluster) == FFAT_TRUE);

			if (NODE_S(pNode) & VOL_CSM(pVol))
			{
				// 마지막 cluster에 write할 공간이 있는 경우 마지막 cluster 정보가 필요
				VC_VEC(pVC_Cur)	= 1;
				VC_CC(pVC_Cur)	= 1;

				pVC_Cur->pVCE[0].dwCount	= 1;
				pVC_Cur->pVCE[0].dwCluster	= pNode->dwLastCluster;
			}
		}
		else
		{
			// get vectored clusters by offset
			r = ffat_misc_getVectoredCluster(pVol, pNode, 0, dwOffset, dwClusterCount,
									pVC_Cur, NULL, pCxt);
			FFAT_EO(r, (_T("fail get vectored cluster")));

			// check is there all clusters
			if ((VC_IS_FULL(pVC_Cur) == FFAT_TRUE) &&
				(VC_LCO(pVC_Cur, pVol) != (NODE_S(pNode) & (~VOL_CSM(pVol)))))
			{
				FFAT_ASSERT(VC_LCO(pVC_Cur, pVol) < NODE_S(pNode));
				// not enough cluster storage
				// get cluster for write
				r = ffat_misc_getClusterOfOffset(pNode, (NODE_S(pNode) - 1), &dwLastCluster, NULL, pCxt);
				FFAT_EO(r, (_T("Fail to get cluster")));
			}
			else
			{
				dwLastCluster = VC_LC(pVC_Cur);
			}
		}

		*pdwPrevEOC = dwLastCluster;
	}

	*pdwNewClusters = dwNewClusters;

	if (dwNewClusters > 0)
	{
		// get free clusters for write
		r = ffat_misc_getFreeClusters(pNode, dwNewClusters, pVC_New, dwLastCluster,
										&dwFreeCount, FAT_ALLOCATE_NONE, pCxt);
		if (r ==  FFAT_ENOSPC)
		{
			IF_UK (dwFreeCount != 0)
			{
				FFAT_ASSERT(VC_IS_FULL(pVC_New) == FFAT_TRUE);
				r = FFAT_OK;
				goto out;
			}

			FFAT_ASSERT(((r == FFAT_ENOSPC) && (VC_IS_FULL(pVC_New) == FFAT_FALSE)) ? (dwFreeCount == 0) : ((dwFreeCount > 0) && (dwFreeCount <= dwNewClusters)));
		}
		FFAT_EO(r, (_T("fail to get free clusters")));
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * Cluster allocation for file write operation (only for file)
 *
 * _write() 에서 사용되는 cluster allocation을 위한 함수이다.
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write start offset
 * @param		dwSize		: [IN] write size
 * @param		dwPrevEOC	: [IN] previous EOC
 * @param		dwNewClusters		: [IN] the number of cluster newly allocated for write
 * @param		pVC_Cur		: [IN/OUT]
 *									IN : partial or full free clusters
 *									(pVC->dwEntryCount == 0) : there is no free cluster info
 *									OUT : cluster for writing (it may be newly allocated)
 *									May not contain all of the clusters
 * @param		pVC_New		: [IN] free clusters
 *									may not contain any free cluster.
 * @param		dwCacheFlag	: [IN] flags for cache operation
 * @param		dwAllocFlag	: [IN] flags for allocation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-07-2006 [DongYoung Seo] First Writing.
 * @version		AUG-14-2007 [DongYoung Seo] bug fix. potential vulnerability
 *											when there is many fragmentation
 *											on the file. (overwrite only)
 * @version		OCT-14-2008 [GwangOk Go] receive cluster chain & free clusters from _write()
 * @version		OCT-29-2008 [DongYoung Seo] remove parameter *pdwNewClusters and *dwRequestClusters
 * @version		FEB-14-2009 [DongYoung Seo] deallocate clusters when initialization is failed
 * @version		APR-13-2009 [JeongWoo Park] Add the code to set / reset first cluster and size of node
 */
static FFatErr
_writeAllocClusters(Node* pNode, t_uint32 dwOffset, t_int32 dwSize, t_uint32 dwPrevEOC,
					t_uint32 dwNewClusters, FFatVC* pVC_Cur, FFatVC* pVC_New,
					FFatCacheFlag dwCacheFlag, FatAllocateFlag dwAllocFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	t_uint32			dwOriginalEOC;
	t_uint32			dwOriginalSize;
	NodePAL				stPAL;	//	previous access location

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC_Cur);
	FFAT_ASSERT(pVC_New);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(dwSize > 0);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	dwOriginalEOC = dwPrevEOC;
	dwOriginalSize = NODE_S(pNode);

	if (dwNewClusters != 0)
	{
		FFAT_ASSERT(dwNewClusters >= VC_CC(pVC_New));
		FFAT_ASSERT(dwNewClusters <= VOL_CC(pVol));
		FFAT_ASSERT(NODE_C(pNode) != 0 ? dwPrevEOC != 0 : FFAT_TRUE);
		FFAT_ASSERT(VC_CC(pVC_New) > 0);

		// make cluster chain using free clusters in pVC_New
		r = ffat_misc_makeClusterChainVC(pNode, dwPrevEOC, pVC_New, 
						FAT_UPDATE_NONE, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to make cluster chain")));

		dwNewClusters -= VC_CC(pVC_New);

		if (VC_IS_FULL(pVC_Cur) == FFAT_FALSE)
		{
			// merge it to pVC_Cur.
			ffat_com_mergeVC(pVC_Cur, pVC_New);
		}

		// set first cluster of node
		if (NODE_C(pNode) == 0)
		{
			FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC_New)) == FFAT_TRUE);
			NODE_C(pNode) = VC_FC(pVC_New);
		}

		if (dwNewClusters == 0)
		{
			// we allocated clusters to need
			pNode->dwLastCluster = VC_LC(pVC_New);

			stPAL.dwCluster	= pVC_New->pVCE[VC_LEI(pVC_New)].dwCluster;
			stPAL.dwCount	= pVC_New->pVCE[VC_LEI(pVC_New)].dwCount;
		}
		else
		{
			// we have to allocate more clusters
			FFatVC				stVC_Temp;				// temporary VC to get the first allocated cluster
			FFatVCE				stVCE;
			FFatVCE				stLastVCE;

			FFAT_ASSERT(VC_IS_FULL(pVC_New) == FFAT_TRUE);

			VC_INIT(&stVC_Temp, VC_NO_OFFSET);

			stVC_Temp.pVCE				= &stVCE;
			stVC_Temp.dwTotalEntryCount	= 1;

			stVCE.dwCluster		= 0;
			stVCE.dwCount		= 0;

			FFAT_ASSERT(dwNewClusters > 0);

			// set new EOC
			dwPrevEOC = VC_LC(pVC_New);

			// allocate cluster
			r = ffat_misc_allocateCluster(pNode, dwPrevEOC, dwNewClusters, &stVC_Temp,
											&stLastVCE, dwAllocFlag, dwCacheFlag, pCxt);
			FFAT_EOTO(r, (_T("fail to allocate cluster")), out_dealloc_fat);	// pVC_New의 cluster만 deallocte하면됨

			pNode->dwLastCluster = stLastVCE.dwCluster + stLastVCE.dwCount - 1;

			stPAL.dwCluster	= stLastVCE.dwCluster;
			stPAL.dwCount	= stLastVCE.dwCount;
		}
	}
	else
	{
		FFAT_ASSERT(VC_CC(pVC_New) == 0);
		FFAT_ASSERT(VC_CC(pVC_Cur) > 0);

		stPAL.dwCluster	= pVC_Cur->pVCE[VC_LEI(pVC_Cur)].dwCluster;
		stPAL.dwCount	= pVC_Cur->pVCE[VC_LEI(pVC_Cur)].dwCount;
	}

	// offset should be aligned by cluster
	stPAL.dwOffset	= ((dwOffset + dwSize - 1) - ((stPAL.dwCount - 1) << VOL_CSB(pVol))) & (~VOL_CSM(pVol));

	// set previous access location
	ffat_node_setPAL(pNode, &stPAL);

	// For using GFS at next ffat_node_readWriteInit(), update the file size
	if (dwOriginalSize < (dwOffset + dwSize))
	{
		NODE_S(pNode) = dwOffset + dwSize;
	}

	// initialize sectors
	if (dwOriginalSize < dwOffset)
	{
		r = ffat_node_readWriteInit(pNode, dwOriginalSize, NULL, (dwOffset - dwOriginalSize), pVC_Cur,
									NULL, FFAT_CACHE_DIRECT_IO, FFAT_RW_INIT, pCxt);
		FFAT_EOTO(r, (_T("fail to init node data")), out_dealloc_fat);
	}

	r = FFAT_OK;

out_dealloc_fat:
	if ((r < 0) && (VC_CC(pVC_New) > 0))
	{
		ffat_misc_deallocateCluster(pNode, dwOriginalEOC, VC_FC(pVC_New), 0,
									pVC_New, (dwAllocFlag | FAT_DEALLOCATE_FORCE), (dwCacheFlag | FFAT_CACHE_FORCE), pCxt);
		
		// if undo to size 0, reset start cluster of node
		if (NODE_C(pNode) == VC_FC(pVC_New))
		{
			NODE_C(pNode) = 0;
		}

		NODE_S(pNode) = dwOriginalSize;
	}

out:
	return r;
}


/**
* check the offset and size is valid for IO
* and adjust the size at limit (file size, 4GB)
*
* @param		pNode			: [IN] node pointer
* @param		dwOffset		: [IN] start offset
* @param		pdwSize			: [INOUT] IO size
* @param		bRead			: [IN] Is this for read?
* @author		DongYoung Seo
* @version		JUL-30-2007 [DongYoung Seo] First Writing
* @version		MAY-26-2009 [JeongWoo Park] edit the function role to be used for both read and write
*											Add the code to check overflow and to adjust dwSize.
*/
static FFatErr
_checkAndAdjustSize(Node* pNode, t_uint32 dwOffset, t_int32* pdwSize, t_boolean bRead)
{
	t_uint32	dwNewSize;
	t_uint32	dwLimitSize;

	FFAT_ASSERT(pNode);

	IF_UK (*pdwSize < 0)
	{
		FFAT_LOG_PRINTF((_T("invalid read or write size - over 2GB")));
		return FFAT_ERANGE;
	}

	if (bRead == FFAT_TRUE)
	{
		dwLimitSize = NODE_S(pNode);

		IF_UK (dwOffset >= dwLimitSize)
		{
			// read의 경우 offset이 file의 마지막 이후이므로 더이상 읽을 것이 없다.
			*pdwSize = 0;
			return FFAT_OK;
		}
	}
	else
	{
		dwLimitSize = FFATFS_GetMaxFileSize(NODE_VI(pNode));

		IF_UK (dwOffset >= dwLimitSize)
		{
			// write의 경우 offset이 Max file size보다 크면 안됨
			// FAT16의 경우 2GB
			// FAT32의 경우 4GB
			FFAT_PRINT_ERROR((_T("Too big offset - over maximum file size(%ld)"), FFATFS_GetMaxFileSize(NODE_VI(pNode))));
			return FFAT_ERANGE;
		}
	}
	
	dwNewSize = dwOffset + (t_uint32)(*pdwSize);

	// check over-flow & over-limit
	IF_UK ((dwNewSize < dwOffset) ||
		(dwNewSize > dwLimitSize))
	{
		*pdwSize = (t_int32)(dwLimitSize - dwOffset);
	}

	FFAT_ASSERT(*pdwSize >= 0);

	return FFAT_OK;
}


//
//	END OF STATIC FUNCTIONS
//
//=============================================================================

