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
 * @file		ffat_addon_nand.c
 * @brief		Module for NAND Flash memory support
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @version		JAN-05-2007 [DongYoung Seo] Add Static MetaData Allocation
 * @see			None
 */


#include "ffat_addon_types.h"

#include "ffat_common.h"

#include "ffat_vol.h"

#include "ffatfs_fat.h"
#include "ffatfs_main.h"
#include "ffatfs_api.h"

#include "ffat_addon_nand.h"
#include "ffat_addon_types_internal.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_NAND)

//================================================
// definitions for SMDA
//

// get SMDA from Vol ptr.
#define VOL_SMDA(_pVol)				(VOL_ADDON(_pVol)->pSMDA)

// check SDMA is activated or not
#define SMDA_IS_ACTAVATED(_pSMDA)	((_pSMDA == NULL) ? FFAT_FALSE : (((_pSMDA)->dwFirstCluster == 0) ? FFAT_FALSE : FFAT_TRUE))

//
// definitions for SMDA
//================================================

// for NAND cleaning operation
static FFatErr	_deleteFreeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt);
static FFatErr	_eraseClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount);



/**
 * This function mount a volume
 * 이 함수는 mount operation의 마지막에 호출이 된다.
 *
 * @param		pVol		: volume pointer
 * @param		dwFlag		: mount flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: Success
 * @author		DongYoung Seo
 * @version		AUG-11-2008 [DongYoung Seo] First Writing.
 * @version		SEP-03-2008 [DongYoung Seo] add Device erase flag check routine
 *								for erase sector support
 */
FFatErr
ffat_nand_mount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;

	FFAT_ASSERT(pVol);

	if ((dwFlag & FFAT_MOUNT_ERASE_SECTOR) || 
		(pVol->stDevInfo.dwFlag & FFAT_DEV_ERASE))
	{
		VOL_FLAG(pVol) |= VOL_ADDON_ERASE_SECTOR;
	}

	if (dwFlag & FFAT_MOUNT_CLEAN_NAND)
	{
		r = ffat_nand_cleanVolume(pVol, pCxt);
		FFAT_EO(r, (_T("Fail to clean a volume")));
	}

out:
	return r ;
}

/**
 * clean NAND device 
 * Call sector erase function for not used sector 
 * from pCN->dwStartCluster to (pCN->dwStartCluster + pCN->dwClusterCount - 1)
 *
 * @param		pVol		: [IN/OUT] volume pointer
 * @param		pCN			: [IN] Clean NAND information
 * @param		pCxt		: [IN] context of current operation
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_nand_cleanNand(Vol* pVol, FFatCleanNand* pCN, ComCxt* pCxt)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCN);

	return _deleteFreeClusters(pVol, pCN->dwStartCluster, pCN->dwClusterCount, pCxt);
}


/**
* clean a volume
* erase all not used clusters in a volume
*
* @param		pVol		: [IN/OUT] volume pointer
* @param		pCxt		: [IN] context of current operation
* @return		void
* @author		DongYoung Seo
* @version		MAY-05-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_nand_cleanVolume(Vol* pVol, ComCxt* pCxt)
{
	FFAT_ASSERT(pVol);

	return _deleteFreeClusters(pVol, 2, VOL_CC(pVol), pCxt);
}


/**
 * delete clusters with cluster chain traverse
 * Call sector erase function for not used sector 
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pVC		: [IN] Vectored Cluster information
 *									Maybe NULL
 * @param		dwCluster	: [IN] start to delete
 * @param		dwCount		: [IN] cluster count to delete
 * @param		pCxt		: [IN] context of current operation
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_nand_deleteClusterChain(Vol* pVol, FFatVC* pVC,
						t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	#define	_ENTRY_COUNT		4

	FFatVC		stVC;
	FFatVCE		stVCE[_ENTRY_COUNT];
	t_int32					i;
	t_uint32				dwStartCluster;		// delete start cluster
	FFatErr			r;

	if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (VOL_IS_ERASE_SECTOR(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (dwCount == 0)
	{
		dwCount = VI_CC(VOL_VI(pVol));
	}

	if (pVC)
	{
		for (i = 0; i < VC_VEC(pVC); i++)
		{
			r = _eraseClusters(pVol, pVC->pVCE[i].dwCluster, pVC->pVCE[i].dwCount);
			FFAT_ER(r, (_T("fail to erase clusters")));
		}

		r = FFATFS_GetNextCluster(VOL_VI(pVol), VC_LC(pVC), &dwCluster, pCxt);
		FFAT_ER(r, (_T("fail to get next cluster for NAND cleaning")));
	}

	// total entry count
	stVC.dwTotalEntryCount	= _ENTRY_COUNT;
	stVC.pVCE				= &stVCE[0];
	dwStartCluster			= dwCluster;

	do
	{
		if (FFATFS_IS_EOF(VOL_VI(pVol), dwStartCluster) == FFAT_TRUE)
		{
			break;
		}

		VC_INIT(&stVC, VC_NO_OFFSET);

		r = FFATFS_GetVectoredCluster(VOL_VI(pVol), dwStartCluster, dwCount,
						&stVC, FFAT_FALSE, pCxt);
		FFAT_ER(r, (_T("fail to get vectored cluster")));

		for (i = 0; i < VC_VEC(&stVC); i++)
		{
			r = _eraseClusters(pVol, stVC.pVCE[i].dwCluster, stVC.pVCE[i].dwCount);
			FFAT_ER(r, (_T("fail to erase clusters")));
		}

		dwCount -= VC_CC(&stVC);

		r = FFATFS_GetNextCluster(VOL_VI(pVol), VC_LC(&stVC), &dwStartCluster, pCxt);
		FFAT_ER(r, (_T("fail to get next cluster")));
	} while (dwCount > 0);

	return FFAT_OK;
}

//=============================================================================
//
//	static functions
//

/**
 * erase clusters only for not used cluster
 * Call sector erase function for not used sector 
 * from dwCluster to (dwCluster + dwCount - 1)
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwCluster	: [IN] start to delete
 * @param		dwCount		: [IN] cluster count to delete
 * @param		pCxt		: [IN] context of current operation
 * @return		void
 * @author		DongYoung Seo
 * @version		AUG-21-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_deleteFreeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	t_uint32		dwStartCluster;	// start cluster
	t_uint32		dwNextCluster;	// next cluster
	t_int32			dwContCount;	// contiguous count
	t_uint32		i;
	FFatErr			r;

	if (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if (dwCount == 0)
	{
		return FFAT_OK;
	}

	if (VOL_IS_ERASE_SECTOR(pVol) == FFAT_FALSE)
	{
		// this device does not provide erase operation
		return FFAT_OK;
	}

	// adjust cluster count
	if ((dwCluster + dwCount - 1) > VOL_LCN(pVol))
	{
		dwCount = VOL_LCN(pVol) - dwCluster + 1;
	}

	dwStartCluster = dwCluster;
	dwContCount = 0;

	for (i = 0; i < dwCount; i++)
	{
		r = FFATFS_GetNextCluster(VOL_VI(pVol), (dwCluster + i), &dwNextCluster, pCxt);
		FFAT_ER(r, (_T("fail to get next cluster")));

		if (dwNextCluster != FAT_FREE)
		{
			if (dwContCount > 0)
			{
				r = _eraseClusters(pVol, dwStartCluster, dwContCount);
				FFAT_ER(r, (_T("fail to erase clusters")));

				r = ffat_fcc_addFreeClusters(pVol, dwStartCluster, dwContCount, pCxt);
				FFAT_ER(r, (_T("fail to add FCC")));
			}

			dwContCount = 0;
			dwStartCluster = dwCluster + i + 1;	// set next start cluster

			continue;
		}

		dwContCount++;
	}

	if (dwContCount > 0)
	{
		r = _eraseClusters(pVol, dwStartCluster, dwContCount);
		FFAT_ER(r, (_T("fail to erase clusters")));

		r = ffat_fcc_addFreeClusters(pVol, dwStartCluster, dwContCount, pCxt);
		FFAT_ER(r, (_T("fail to add FCC")));
	}

	return FFAT_OK;
}


/**
 * erase clusters 
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwCluster	: [IN] start to delete
 * @param		dwCount		: [IN] cluster count to erase
 * @return		void
 * @author		DongYoung Seo
 * @version		NOV-04-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_eraseClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount)
{
	FFatCacheInfo	stCI;
	t_uint32		dwSector;		// erase start sector number
	t_uint32		dwSectorCount;
	t_int32			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwCount > 0);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	dwSector = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), dwCluster);
	dwSectorCount = dwCount << VI_SPCB(VOL_VI(pVol));

	r = ffat_al_eraseSector(dwSector, dwSectorCount, FFAT_CACHE_NONE, &stCI);
	IF_UK (r != (t_int32)dwSectorCount)
	{
		FFAT_LOG_PRINTF((_T("fail to erase sectors")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}
