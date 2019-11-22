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
 * @file		ffat_vol.c
 * @brief		The volume module for FFAT
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */


// includes
#include "ess_pstack.h"
#include "ffat_common.h"
#include "ffat_al.h"

#include "ffat_main.h"
#include "ffat_vol.h"
#include "ffat_node.h"
#include "ffat_dir.h"
#include "ffat_misc.h"

#include "ffatfs_api.h"
#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_VOL)

#define _SET_VOL_MOUNTED(_pVol)		((_pVol)->dwFlag |= VOL_MOUNTED)
#define _RESET_VOL_MOUNTED(_pVol)	((_pVol)->dwFlag &= (~VOL_MOUNTED))
#define _SET_VOL_MOUNTING(_pVol)	((_pVol)->dwFlag |= VOL_MOUNTING)
#define _RESET_VOL_MOUNTING(_pVol)	((_pVol)->dwFlag &= (~VOL_MOUNTING))

static void	_fillVolumeStatus(Vol* pVol, FFatVolumeStatus* pStatus);

/**
 * This function initializes FFatVol module
 *
 * @return		FFAT_OK			: Success
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_vol_init(void)
{
	/* nothing to do */


	return FFAT_OK;
}

/**
 * This function terminates FFatVol module
 *
 * @return		FFAT_OK			: Success
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_vol_terminate(void)
{
	/* nothing to do */

	return FFAT_OK;
}


/**
 * This function mount a volume
 *
 * @param		pVol	: volume structure pointer
 * @param		pRoot		: root node pointer
 * @param		pdwFlag	: mount flag
 * @param		pDev	: pointer for user information.
							This pointer is used for block device IO
 * @param		pCxt	: context of current operation
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOMEM		: not enough memory
 * @return		FFAT_EPANIC		: system operation error such as lock operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		JUN-17-2009 [JeongWoo Park] Add the mount flag for specific naming rule.
 *											- case sensitive / Os specific character set
 */
FFatErr
ffat_vol_mount(Vol* pVol, Node* pRoot, FFatMountFlag* pdwFlag, FFatLDevInfo* pLDevInfo,
				void* pDev, ComCxt* pCxt)
{
	FFatErr			r;

#ifdef FFAT_STRICT_CHECK
	// check parameter
	IF_UK ((pVol == NULL) || (pRoot == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	// check volume flag
	FFAT_MEMSET(pVol, 0x00, sizeof(Vol));

	// create a lock for volume
	r = ffat_lock_initRWLock(&pVol->stRWLock);
	FFAT_ER(r, (_T("Fail to create a lock for a volume")));

	// Lock vol and main
	r = VOL_GET_READ_LOCK(pVol);
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	VOL_ROOT(pVol) = pRoot;

	// set reset flag for root
	pRoot->wSig		= NODE_SIG_RESET;

	// set volume mounting flag
	_SET_VOL_MOUNTING(pVol);

	r = FFATFS_Mount(VOL_VI(pVol), pLDevInfo, pDev, pCxt, *pdwFlag);
	FFAT_EO(r, (_T("fail to mount a volume")));

	// copy logical device info
	FFAT_MEMCPY(&pVol->stDevInfo, pLDevInfo, sizeof(FFatLDevInfo));

	VOL_TS(pVol)	= (t_uint16)(FFAT_MAIN_INC_TIMESTAMP() & 0xFFFF);
	VOL_MSD(pVol)	= FAT_DE_SIZE * FAT_DE_COUNT_MAX;

	r = ffat_node_initRootNode(pVol, VOL_ROOT(pVol), pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to init root node")));
		goto out;
	}

	if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
	{
		VOL_ROOT(pVol)->dwCluster = VOL_VI(pVol)->dwRootCluster;
	}

	if (*pdwFlag & FFAT_MOUNT_FAT_MIRROR)
	{
		pVol->dwFlag |= VOL_FAT_MIRROR;
	}

	// check read only
	if (*pdwFlag & FFAT_MOUNT_RDONLY)
	{
		pVol->dwFlag |= VOL_RDONLY;
	}

	if (*pdwFlag & FFAT_MOUNT_SYNC_META)
	{
		pVol->dwFlag |= VOL_SYNC_META;
	}

	if (*pdwFlag & FFAT_MOUNT_CASE_SENSITIVE)
	{
		pVol->dwFlag |= VOL_CASE_SENSITIVE;
	}

	if (*pdwFlag & FFAT_MOUNT_OS_SPECIFIC_CHAR)
	{
		pVol->dwFlag |= VOL_OS_SPECIFIC_CHAR;
	}

#ifdef FFAT_SYNC_METADATA_ON_REMOVABLE_DEVICE
	if (pVol->stDevInfo.dwFlag & FFAT_DEV_REMOVABLE)
	{
		pVol->dwFlag |= VOL_SYNC_META;
	}
#endif

#ifdef FFAT_SYNC_USERDATA_ON_REMOVABLE_DEVICE
	if (pVol->stDevInfo.dwFlag & FFAT_DEV_REMOVABLE)
	{
		pVol->dwFlag |= VOL_SYNC_USER;
	}
#endif

#ifdef FFAT_MIRROR_FAT_ON_REMOVABLE_DEVICE
	if (pVol->stDevInfo.dwFlag & FFAT_DEV_REMOVABLE)
	{
		pVol->dwFlag |= VOL_FAT_MIRROR;
	}
#endif

	_SET_VOL_MOUNTED(pVol);

	r = ffat_addon_afterMount(pVol, pdwFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to mount with addon module")));
		goto out_dec_ref_count;
	}

	FFAT_MAIN_INC_REFCOUNT();

	NODE_INC_REF_COUNT(VOL_ROOT(pVol));

	_RESET_VOL_MOUNTING(pVol);

	// TFS5 does not support FAT mirroring for removable device, decided on 01-DEC-2008 by PL
	//FFAT_ASSERT((VOL_IS_REMOVABLE(pVol) == FFAT_TRUE) ? FFAT_TRUE : ((VOL_FLAG(pVol) & VOL_FAT_MIRROR) == 0));

	r = FFAT_OK;
	goto out;

out_dec_ref_count:
	// release a lock for root directory
	ffat_node_terminateNode(VOL_ROOT(pVol), pCxt);

	FFAT_MAIN_DEC_REFCOUNT();

out:
	r |= VOL_PUT_READ_LOCK(pVol);

out_vol:
	IF_UK (r < 0)
	{
		FFATFS_Umount(VOL_VI(pVol), FFAT_CACHE_FORCE, pCxt);

		FFAT_LOG_PRINTF((_T("There is some error while mounting a volume!!")));
		r |= ffat_lock_terminateRWLock(&pVol->stRWLock);
		_RESET_VOL_MOUNTED(pVol);
	}

	return r;
}


/**
*  re-mount a volume
* This function changes operation move of a volume
*	Transaction On/Off and transaction type
*	set volume read only
*
*	This function is used on Linux BOX.
*
* @param		pVol			: [IN] volume pointer
* @param		pdwFlag			: [INOUT] mount flag
*										FFAT_MOUNT_NO_LOG
*										FFAT_MOUNT_LOG_LLW
*										FFAT_MOUNT_LOG_FULL_LLW
*										FFAT_MOUNT_RDONLY
* @return		FFAT_OK			: Success
* @return		FFAT_EINVALID	: Invalid parameter
* @return		FFAT_ENOMEM		: not enough memory
* @return		FFAT_EPANIC		: system operation error such as lock operation
* @author		DongYoung Seo
* @version		12-DEC-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_vol_remount(Vol* pVol, FFatMountFlag* pdwFlag, ComCxt* pCxt)
{
	FFatMountFlag		dwFlagMask;
	FFatMountFlag		dwFlagCur;		// currentm mount flag
	FFatErr				r;

	dwFlagMask = (FFAT_MOUNT_NO_LOG | FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW | FFAT_MOUNT_RDONLY);

	FFAT_ASSERT(pVol);
	FFAT_ASSERT((*pdwFlag & (~dwFlagMask)) == 0);
	FFAT_ASSERT(VOL_FLAG(pVol) & VOL_MOUNTED);
	FFAT_ASSERT((VOL_FLAG(pVol) & VOL_MOUNTING) == 0);

	// MASK MOUNT FLAG
	*pdwFlag &= dwFlagMask;

	r = VOL_GET_WRITE_LOCK(pVol);			// get volume exclusive lock
	FFAT_ER(r, (_T("fail to get vol write lock")));

	dwFlagCur = VOL_FLAG(pVol);

	r = ffat_addon_remount(pVol, pdwFlag, pCxt);
	FFAT_EO(r, (_T("Fail to removed on addon module")));

	if (*pdwFlag & VOL_RDONLY)
	{
		VOL_FLAG(pVol) &= (~(VOL_SYNC_META | VOL_SYNC_USER));			// REMOVE SYNC FLAG
		VOL_FLAG(pVol) |= VOL_RDONLY;
	}
	else
	{
		VOL_FLAG(pVol) &= (~VOL_RDONLY);
	}

	// current version does not need to do something after remount
	// r = ffat_addon_afterRemount();

out:
	if (r != FFAT_OK)
	{
		VOL_FLAG(pVol) = dwFlagCur;			// set original mount flag
	}

	r |= VOL_PUT_WRITE_LOCK(pVol);			// put exclusive lock
	return r;
}


/**
 * This function un-mount a volume
 *
 * @param		pVol	: [IN] volume structure pointer
 * @param		dwFlag	: [IN] mount flag
 *							FFAT_UMOUNT_FORCE	: un-mount a volume by force 
									even if there is some operation in the volume
 * @param		pCxt	: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOMEM		: not enough memory
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_vol_umount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pVol == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(VOL_IS_MOUNTED(pVol) == FFAT_TRUE);

	r = VOL_GET_READ_LOCK(pVol);
	FFAT_ER(r, (_T("fail to get write lock for a volume")));

	// check busy state
	if ((VOL_IS_BUSY(pVol) == FFAT_TRUE) && ((dwFlag & FFAT_UMOUNT_FORCE) == 0))
	{
		FFAT_LOG_PRINTF((_T("the volume is busy")));
		r = FFAT_EBUSY;
		goto out;
	}

	NODE_DEC_REF_COUNT(VOL_ROOT(pVol));
	NODE_CLEAR_OPEN(VOL_ROOT(pVol));

	// close root directory
	r = ffat_node_close(VOL_ROOT(pVol), (FFAT_NODE_CLOSE_SYNC | FFAT_NODE_CLOSE_RELEASE_RESOURCE), pCxt);
	FFAT_EO(r, (_T("fail to close root node")));

	r = ffat_addon_umount(pVol, dwFlag, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to unmount ADDON module")));
		goto out;
	}

	// sync volume
	r = FFATFS_Umount(VOL_VI(pVol),
			(FFAT_CACHE_SYNC | FFAT_CACHE_DISCARD | FFAT_CACHE_FORCE), pCxt);
	if (r != FFAT_OK)
	{
		NODE_INC_REF_COUNT(VOL_ROOT(pVol));		// undo decrease ref count
		FFAT_LOG_PRINTF((_T("fail to umount volume")));
		goto out;
	}

	// set new time stamp to set unmount
	pVol->wTimeStamp = (t_uint16)(FFAT_MAIN_INC_TIMESTAMP() & 0xFFFF);

	// reset volume flag
	pVol->dwFlag = VOL_FREE;

	// unlock volume
	r = VOL_PUT_READ_LOCK(pVol);
	FFAT_ER(r, (_T("fail to put write lock for a volume")));

	// release lock for volume
	r = ffat_lock_terminateRWLock(&pVol->stRWLock);
	FFAT_EO(r, (_T("fail to release lock")));

	FFAT_MAIN_DEC_REFCOUNT();
	return FFAT_OK;

out:
	// unlock volume
	r |= VOL_PUT_READ_LOCK(pVol);

	return r;
}


/** 
 * retrieve boot sector info from boot sector before mount
 * (sector size, cluster size, first data sector)
 * 
 * @param		pDev			: [IN] user pointer for block device IO
 *								this parameter is used for block device IO 
 *								such as sector read or write.
 *								User can distinguish devices from this pointer.
 * @param		dwIOSize		: [IN] current I/O size
 * @param		pdwSectorSize	: [OUT] sector size storage
 * @param		pdwClusterSize	: [OUT] clsuter size storage
 * @param		pdwFirstDataSector	: [OUT] first data sector storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] add cluster size
 * @version		FEB-18-2008 [GwangOk Go] add first data sector
 */
FFatErr
ffat_vol_getBSInfoFromBS(void* pDev, t_int32 dwIOSize, t_int32* pdwSectorSize,
						 t_int32* pdwClusterSize, t_uint32* pdwFirstDataSector, ComCxt* pCxt)
{
	FFatErr			r;
	
#ifdef FFAT_STRICT_CHECK
	// check parameter
	IF_UK ((pdwSectorSize == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	r = FFATFS_GetBSInfoFromBS(pDev, dwIOSize, pdwSectorSize, pdwClusterSize, pdwFirstDataSector, pCxt);
	FFAT_EO(r, (_T("fail to mount a volume")));

	r = FFAT_OK;

out:

	return r;
}


/**
 * sync a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		bLock		: [IN]	FFAT_TRUE : lock a volume
 *									FFAT_FALSE : do not lock a volume
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_vol_sync(Vol* pVol, t_boolean bLock, ComCxt* pCxt)
{
	FFatErr			r;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pVol == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	if (bLock == FFAT_TRUE)
	{
		r = VOL_GET_READ_LOCK(pVol);
		FFAT_ER(r, (_T("fail to get read lock")));
	}

	VOL_INC_REFCOUNT(pVol);

	r = ffat_addon_syncVol(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync a volume on addon module")));

	r = FFATFS_SyncVol(VOL_VI(pVol), FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync a volume")));

	r = ffat_addon_afterSyncVol(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync a volume on addon module")));

out:
	VOL_DEC_REFCOUNT(pVol);

	if (bLock == FFAT_TRUE)
	{
		r |= VOL_PUT_READ_LOCK(pVol);
	}

	return r;
}


/**
 * get status of  a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pStatus		: [OUT] volume information storage
 * @param		pBuff		: [IN] buffer pointer, may be NULL
 * @param		dwSize		: [IN] size of buffer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: volume is not mounted
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		MAY-09-2007 [DongYoung Seo] add new parameter buffer and size for performance
 */
FFatErr
ffat_vol_getStatus(Vol* pVol, FFatVolumeStatus* pStatus, t_int8* pBuff,
					t_int32 dwSize, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVol == NULL) || (pStatus == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted")));
		return FFAT_EACCESS;
	}

	r = VOL_GET_READ_LOCK(pVol);
	FFAT_ER(r, (_T("fail to get read lock")));

	VOL_INC_REFCOUNT(pVol);

	FFAT_MEMSET(pStatus, 0x00, sizeof(FFatVolumeStatus));

	r = ffat_addon_getVolumeStatus(pVol, pStatus, pBuff, dwSize, pCxt);
	if (r == FFAT_DONE)
	{
		r = FFAT_OK;
		goto out;
	}

	r = FFATFS_UpdateVolumeStatus(VOL_VI(pVol), pBuff, dwSize, pCxt);
	FFAT_EO(r, (_T("fail to update volume status")));

out:
	_fillVolumeStatus(pVol, pStatus);

	VOL_DEC_REFCOUNT(pVol);

	r |= VOL_PUT_READ_LOCK(pVol);

	return r;
}


/**
 * get volume name
 *
 * @param		pVolInfo		: [IN] volume information
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
ffat_vol_getVolumeLabel(Vol* pVol, t_wchar* psVolLabel, t_int32 dwVolLabelLen, ComCxt* pCxt)
{
	FFatErr		r;

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVol == NULL) || (psVolLabel == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted")));
		return FFAT_EACCESS;
	}

	r = VOL_GET_READ_LOCK(pVol);
	FFAT_ER(r, (_T("fail to get read lock")));

	VOL_INC_REFCOUNT(pVol);

	r = FFATFS_GetVolumeLabel(VOL_VI(pVol), psVolLabel, dwVolLabelLen, pCxt);

	VOL_DEC_REFCOUNT(pVol);

	r |= VOL_PUT_READ_LOCK(pVol);

	return r;
}


/**
 * set volume name to psVolLabel
 *
 * @param		pVolInfo		: [IN] volume information
 * @param		psVolLabel		: [IN] new volume name
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_vol_setVolumeLabel(Vol* pVol, t_wchar* psVolLabel, ComCxt* pCxt)
{
	FFatErr		r;
	t_uint32	pClusters[NODE_MAX_CLUSTER_FOR_CREATE];

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVol == NULL) || (psVolLabel == NULL))
	{
		FFAT_LOG_PRINTF((_T("invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted")));
		return FFAT_EACCESS;
	}

	IF_UK (VOL_FLAG(pVol) & VOL_LFN_ONLY)
	{
		FFAT_PRINT_ERROR((_T("Setting volume label is not allowed in Non-FAT volume")));
		return FFAT_EINVALID;
	}

	r = VOL_GET_READ_LOCK(pVol);
	FFAT_ER(r, (_T("fail to get read lock")));

	VOL_INC_REFCOUNT(pVol);

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_VOLUME_NAME_BEFORE);

re:
	r = FFATFS_SetVolumeLabel(VOL_VI(pVol), psVolLabel, pCxt);
	if (r == FFAT_ENOSPC)
	{
		if (VOL_IS_FAT32(pVol) == FFAT_TRUE)
		{
			r = ffat_dir_expand(VOL_ROOT(pVol), 1, 0, pClusters, pCxt);
			FFAT_EO(r, (_T("fail to expand root directory")));

			goto re;
		}
	}

	FFAT_EO(r, (_T("Fail to set volume name")));

	FFAT_DEBUG_LRT_CHECK(FFAT_LRT_SET_VOLUME_NAME_AFTER);

	r = ffat_addon_afterSetVolumeLabel(pVol, pCxt);
	FFAT_EO(r, (_T("Fail to set volume name on addon module")));

out:
	VOL_DEC_REFCOUNT(pVol);

	r |= VOL_PUT_READ_LOCK(pVol);

	return r;
}


//=============================================================================
//
//	STATIC FUNCTIONS
//


/**
 * get status of  a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pStatus		: [OUT] volume information storage
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
static void
_fillVolumeStatus(Vol* pVol, FFatVolumeStatus* pStatus)
{
	t_uint32		dwSectorCount;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pStatus);

	pStatus->dwFatType			= FFATFS_GetFatType(VOL_VI(pVol));
	pStatus->dwClusterSize		= VOL_CS(pVol);
	pStatus->dwClusterSizeBits	= VOL_CSB(pVol);
	pStatus->dwClusterCount		= VOL_CC(pVol);
	pStatus->dwFreeClusterCount	= VOL_FCC(pVol);
	pStatus->dwMaxFileSize		= FFATFS_GetMaxFileSize(VOL_VI(pVol));
	pStatus->dwMaxNameLen		= FFAT_NAME_MAX_LENGTH;

	// FAT type에 따른 max file size와 volume의 size 중 작은 것을 선택한다.
	dwSectorCount = pStatus->dwClusterCount << (pStatus->dwClusterSizeBits - VOL_SSB(pVol));	// sector count
	if (dwSectorCount <= (pStatus->dwMaxFileSize >> VOL_SSB(pVol)))
	{
		pStatus->dwMaxFileSize = dwSectorCount << VOL_SSB(pVol);
	}
}

