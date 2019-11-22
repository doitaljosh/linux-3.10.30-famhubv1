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
 * @file		ffatfs_bs.c
 * @brief		boot sector module for FFAT FATFS
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-20-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"

#include "ffatfs_types.h"
#include "ffatfs_main.h"
#include "ffatfs_bs.h"
#include "ffatfs_de.h"
#include "ffatfs_fat.h"
#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_FFATFS_BS)

#define _TO_UINT16(x)			((x[0]) | (x[1] << 8))

static t_uint32		_getDataClusterCount(t_int8* pBootSector);
static FFatFatType	_getFatType(t_int8* pBootSector);
static t_uint32		_getEOC(t_int8* pBootSector);
static t_uint32		_getBAD(t_int8* pBootSector);
static t_uint32		_getRootDirSectorCount(t_int8* pBootSector);

/**
 * retrieve volume information from boot sector
 *
 * @param		pVolInfo		: volume information
 * @param		pBootSector		: boot sector storage
 * @param		dwLDevSectorCount		: logical device sector count
 * @param		dwLDevSectorSizeBits	: logical device sector size in bit count
 * @return		FFAT_OK			: success
 * @return		negative		: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 * @version		NOV-14-2008 [DongYoung Seo] add checking code for user area is in the device range
 * @version		MAR-05-2009 [GwangOk Go] change parameter pLDevInfo into dwLDevSize
 */
FFatErr
ffat_fs_bs_retrieveCommon(FatVolInfo* pVolInfo, t_int8* pBootSector,
						  t_uint32 dwLDevSectorCount, t_int32 dwLDevSectorSizeBits)
{
	FatBSCommon*	pBSC;
	FatBS32*		pBS32;
	t_uint32		dwReservedSectors;		// reserved sector count
	t_uint32		dwTemp;					// temporary variable

#ifdef FFAT_STRICT_CHECK
	IF_UK ((pVolInfo == NULL) || (pBootSector == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(pBootSector);

	// check signature
	IF_UK (((t_uint8)pBootSector[BS_SIG0_OFFSET] != BS_SIG0) ||
			((t_uint8)pBootSector[BS_SIG1_OFFSET] != BS_SIG1))
	{
		FFAT_LOG_PRINTF((_T("Invalid boot sector signature !!")));
		return FFAT_ENOSUPPORT;
	}

	pBSC	= (FatBSCommon*)pBootSector;
	pBS32	= (FatBS32*)(pBootSector + BS_FAT32_OFFSET);

	pVolInfo->dwSectorSize			= _TO_UINT16(pBSC->wBytesPerSec);
	pVolInfo->wSectorSizeBits		= (t_int16)EssMath_Log2(pVolInfo->dwSectorSize);
	IF_UK (pVolInfo->wSectorSizeBits < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid sector size in boot sector")));
		return FFAT_ENOSUPPORT;
	}
	pVolInfo->wSectorSizeMask		= (t_int16)(pVolInfo->dwSectorSize - 1);

	FFAT_ASSERT(pVolInfo->dwSectorSize == (1 << pVolInfo->wSectorSizeBits));

	pVolInfo->dwSectorCount			= _TO_UINT16(pBSC->wTotSec16);
	if (pVolInfo->dwSectorCount == 0)
	{
		pVolInfo->dwSectorCount		= FFAT_BO_UINT32(pBSC->dwTotSec32);
	}

	pVolInfo->wSectorPerCluster			= pBSC->bSectorsPerClus;
	pVolInfo->wSectorPerClusterBits		= (t_int16)EssMath_Log2(pVolInfo->wSectorPerCluster);
	IF_UK (VI_SPCB(pVolInfo) < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid sector per cluster in boot sector")));
		return FFAT_ENOSUPPORT;
	}

	FFAT_ASSERT(pVolInfo->wSectorPerCluster == (1 << pVolInfo->wSectorPerClusterBits));

	pVolInfo->dwClusterSize		= pVolInfo->wSectorPerCluster * pVolInfo->dwSectorSize;
	pVolInfo->dwClusterSizeMask	= pVolInfo->dwClusterSize - 1;
	pVolInfo->dwClusterSizeBits	= EssMath_Log2(pVolInfo->dwClusterSize);
	IF_UK (VI_CSB(pVolInfo) < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster size in boot sector")));
		return FFAT_ENOSUPPORT;
	}

#if 0
	// never check sector count with logical device sector count
	// windows mobile may make a partition over logical device range.
	// check it with valid cluster area
	// check sector count

	// case that FAT sector size is greater than logical device sector size
	IF_UK (((pVolInfo->wSectorSizeBits >= dwLDevSectorSizeBits) &&
		(pVolInfo->dwSectorCount << (pVolInfo->wSectorSizeBits - dwLDevSectorSizeBits)) > dwLDevSectorCount) ||

		// case that FAT sector size is smaller than logical device sector size
		((pVolInfo->wSectorSizeBits < dwLDevSectorSizeBits) &&
		(pVolInfo->dwSectorCount > (dwLDevSectorCount << (dwLDevSectorSizeBits - pVolInfo->wSectorSizeBits)))))
	{
		FFAT_LOG_PRINTF((_T("Invalid sector count, too many sector count")));
		return FFAT_ENOSUPPORT;
	}
#endif

	FFAT_ASSERT(pVolInfo->dwClusterSize == (1 << pVolInfo->dwClusterSizeBits));

	pVolInfo->dwClusterCount	= _getDataClusterCount(pBootSector);
	pVolInfo->dwFatType			= _getFatType(pBootSector);
	pVolInfo->dwEOC				= _getEOC(pBootSector);
	pVolInfo->dwBAD				= _getBAD(pBootSector);

	if (FFAT_BO_UINT16(pBSC->wFATSz16) != 0)
	{
		pVolInfo->dwFatSize = FFAT_BO_UINT16(pBSC->wFATSz16);
	}
	else
	{
		pVolInfo->dwFatSize = FFAT_BO_UINT32(pBS32->dwFATSz32);
	}

	pVolInfo->dwFatCount			= pBSC->bNumFATs;
	IF_UK ((VI_FC(pVolInfo) > 5) || (VI_FC(pVolInfo) == 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid FAT count")));
		return FFAT_ENOSUPPORT;
	}

	pVolInfo->wRootEntryCount		= _TO_UINT16(pBSC->wRootEntCnt);
	pVolInfo->dwRootSectorCount		= _getRootDirSectorCount(pBootSector);

	dwReservedSectors				= FFAT_BO_UINT16(pBSC->wRsvdSecCnt);
	pVolInfo->dwFirstFatSector		= dwReservedSectors;
	pVolInfo->dwFirstDataSector		= dwReservedSectors + 
										(VI_FC(pVolInfo) * VI_FSC(pVolInfo)) + VI_RSC(pVolInfo);

	if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		pVolInfo->dwRootCluster		= FFAT_BO_UINT32(pBS32->dwRootClus);
		pVolInfo->dwFirstRootSector	= FFATFS_GetFirstSectorOfCluster(pVolInfo, pVolInfo->dwRootCluster);
		pVolInfo->wRootEntryCount	= 0;	// this is for FAT16
		pVolInfo->dwRootSectorCount	= 0;	// this is for FAT16

		pVolInfo->wClusterPerFatSector		= (t_int16)(pVolInfo->dwSectorSize >> 2);	// sector size / 4
		pVolInfo->wClusterPerFatSectorBits	= (t_int16)EssMath_Log2(pVolInfo->wClusterPerFatSector);

		IF_UK (pVolInfo->wClusterPerFatSector < 0)
		{
			FFAT_LOG_PRINTF((_T("Invalid wClusterPerFatSector size in boot sector")));
			return FFAT_ENOSUPPORT;
		}

		// set volume operation function pointer
		pVolInfo->pVolOp = &gstVolOp32;
	}
	else if (FFATFS_IS_FAT16(pVolInfo) == FFAT_TRUE)
	{
		pVolInfo->dwRootCluster		= FFATFS_FAT16_ROOT_CLUSTER;
		pVolInfo->dwFirstRootSector	= dwReservedSectors + (VI_FC(pVolInfo) * VI_FSC(pVolInfo));

		pVolInfo->wClusterPerFatSector		= (t_int16)(VI_SS(pVolInfo) >> 1);	// sector size / 4
		pVolInfo->wClusterPerFatSectorBits	= (t_int16)EssMath_Log2(pVolInfo->wClusterPerFatSector);

		IF_UK (pVolInfo->wClusterPerFatSector < 0)
		{
			FFAT_LOG_PRINTF((_T("Invalid wClusterPerFatSector size in boot sector")));
			return FFAT_ENOSUPPORT;
		}

		// set volume operation function pointer
		pVolInfo->pVolOp = &gstVolOp16;
	}
	else
	{
		FFAT_LOG_PRINTF((_T("Not supported FAT type")));
		return FFAT_ENOSUPPORT;
	}

	FFAT_ASSERT(pVolInfo->wClusterPerFatSector == (1 << pVolInfo->wClusterPerFatSectorBits));

	// check FATsize and cluster count is valid
	dwTemp = (t_uint32)ESS_MATH_CDB((VI_CC(pVolInfo) + 2), pVolInfo->wClusterPerFatSector, pVolInfo->wClusterPerFatSectorBits);
	IF_UK (dwTemp > pVolInfo->dwFatSize)
	{
#if 0
		FFAT_LOG_PRINTF((_T("Invalid cluster count , cluster count is over FAT size")));
		return FFAT_EINVALID;
#else
		FFAT_ASSERTP(0, (_T("Impossible on FFAT")));
		pVolInfo->dwClusterCount = (VI_FSC(pVolInfo) << pVolInfo->wClusterPerFatSectorBits) - 2;
#endif
	}

	pVolInfo->dwLVFSOFF = FFATFS_GET_FAT_SECTOR_OF_CLUSTER(pVolInfo, VI_LCN(pVolInfo));
	FFAT_ASSERT(pVolInfo->dwLVFSOFF < (VI_FFS(pVolInfo) + VI_FSC(pVolInfo)));

	// check data area is in device range
	// get last Sector of data area
	dwTemp = FFATFS_GetFirstSectorOfCluster(pVolInfo, VI_LCN(pVolInfo));
	dwTemp += (VI_SPC(pVolInfo) - 1);		// this is the last sector number of user space

	// case that FAT sector size is greater than logical device sector size
	IF_UK (((pVolInfo->wSectorSizeBits >= dwLDevSectorSizeBits) &&
		(dwTemp << (pVolInfo->wSectorSizeBits - dwLDevSectorSizeBits)) >= dwLDevSectorCount) ||

		// case that FAT sector size is smaller than logical device sector size
		((pVolInfo->wSectorSizeBits < dwLDevSectorSizeBits) &&
		(dwTemp >= (dwLDevSectorCount << (dwLDevSectorSizeBits - pVolInfo->wSectorSizeBits)))))
 	{
 		FFAT_LOG_PRINTF((_T("Invalid cluster count , data area is over Device size")));
 		return FFAT_EINVALID;
 	}

	return FFAT_OK;
}


/**
 * retrieve FAT32 FSInfo for sector buffer
 *
 * @param		pVolInfo		: volume information
 * @param		pBootSector		: boot sector pointer
 * @return		FFAT_OK			: success
 * @return		negative		: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_fs_bs_retrieveFat32FSInfo(FatVolInfo* pVolInfo, t_int8* pBootSector)
{
	Fat32FSInfo* pFSI;

#ifdef FFAT_STRICT_CHECK
	IF_UK (pBootSector == NULL)
	{
		return FFAT_EINVALID;
	}
#endif

	pFSI = (Fat32FSInfo*)pBootSector;

	// check leadsig
	IF_UK (FFAT_BO_UINT32(pFSI->dwFSI_LeadSig) != BS_FSI_LEAD_SIG)
	{
		FFAT_LOG_PRINTF((_T("Invalid FSInfo sector, BS_FSI_LEAD_SIG")));
		return FFAT_EINVALID;
	}

	// check struct sig
	IF_UK (FFAT_BO_UINT32(pFSI->dwFSI_StrucSig) != BS_FSI_STRUCT_SIG)
	{
		FFAT_LOG_PRINTF((_T("Invalid FSInfo sector, BS_FSI_STRUCT_SIG")));
		return FFAT_EINVALID;
	}

	// check trailsig
	IF_UK (FFAT_BO_UINT32(pFSI->dwFSI_TrailSig) != BS_FSI_TRAIL_SIG)
	{
		FFAT_LOG_PRINTF((_T("Invalid FSInfo sector, BS_FSI_TRAIL_SIG")));
		return FFAT_EINVALID;
	}

	pVolInfo->stVolInfoCache.dwFreeClusterHint	= FFAT_BO_UINT32(pFSI->dwFSI_Nxt_Free);

	if (FFATFS_IS_VALID_CLUSTER(pVolInfo, VIC_FCH(VI_VIC(pVolInfo))) == FFAT_FALSE)
	{
		pVolInfo->stVolInfoCache.dwFreeClusterHint = FFATFS_FREE_CLUSTER_INVALID;
	}

	return FFAT_OK;
}


/**
 * mount a volume
 *
 * read boot sector and retrieve volume information
 *
 * @param		pVolInfo		: [IN/OUT] volume pointer
 * @param		pLDevInfo		: logical device information
 * @param		pDev			: [IN] void type pointer for device IO
 *									block device IO를 이용할때 전달된다.
 * @param		dwFlag			: [IN] flags for mount
 * @param		pCxt			: [IN] current running context
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		MAR-05-2009 [GwangOk Go] set initial info about relationship between block & FAT sector
 * @version		APR-01-2009 [GwangOk Go] support case that FAT sector is larger than block before mounted
 * @version		JUN-17-2009 [JeongWoo Park] Add the mount flag for specific naming rule.
 *											- case sensitive / Os specific character set
 */
FFatErr
ffat_fs_bs_mount(FatVolInfo* pVolInfo, FFatLDevInfo* pLDevInfo, void* pDev,
					ComCxt* pCxt, FFatMountFlag dwFlag)
{
	FFatErr			r;
	t_int8*			pBootSector = NULL;
	FFatCacheInfo	stCI;

	FFAT_ASSERT(pVolInfo);

	pVolInfo->pDevice	= pDev;
	VI_SET_CXT(pVolInfo, pCxt);

	// allocation memory for boot sector
	pBootSector = FFAT_LOCAL_ALLOC(pLDevInfo->dwDevSectorSize, VI_CXT(pVolInfo));
	FFAT_ASSERT(pBootSector != NULL);

// debug begin
#ifdef FFAT_DEBUG
	// 현재는 volume의 sector count 정보가 없으므로
	// 일단 100으로 설정
	pVolInfo->dwSectorCount = 0xFFFFFFFF;
#endif
// debug end

	// set time stamp
	ffat_fs_incTimeStamp();
	pVolInfo->wTimeStamp	= ffat_fs_getTimeStamp();

	FFAT_INIT_CI(&stCI, NULL, pDev);

	// read BPB sector
	r = ffat_al_readSector2(0, pBootSector, 1,
					(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_FS), &stCI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read boot sector")));
		r = FFAT_EIO;
		goto out;
	}

	FFAT_ASSERT(pLDevInfo->dwDevSectorCount != 0);
	FFAT_ASSERT(pLDevInfo->dwDevSectorSize != 0);

	// retrieve volume information from boot sector
	r = ffat_fs_bs_retrieveCommon(pVolInfo, pBootSector,
							pLDevInfo->dwDevSectorCount, pLDevInfo->dwDevSectorSizeBits);
	FFAT_EO(r, (_T("fail to parse boot sector")));

	FFAT_ASSERT(pLDevInfo->dwBlockSize != 0);

	// FATSectorPerBlock에 관한 정보는 VFSAL에서 설정해야 하나 (BTFS core에는 block에 관한 개념이 없음)
	// mount 도중 다른 모듈에서 read/write sector를 하기 위해서 여기서 설정함
	// mount후 block size가 변경되면 VFSAL에서 재설정함
	if (pLDevInfo->dwBlockSize >= pVolInfo->dwSectorSize)
	{
		pLDevInfo->dwFATSectorPerBlock		= pLDevInfo->dwBlockSize / pVolInfo->dwSectorSize;
		pLDevInfo->dwFATSectorPerBlockBits	= pLDevInfo->dwBlockSizeBits - pVolInfo->wSectorSizeBits;
		pLDevInfo->dwFATSectorPerBlockMask	= pLDevInfo->dwFATSectorPerBlock - 1;
	}
	else
	{
		pLDevInfo->dwFATSectorPerBlock		= -(pVolInfo->dwSectorSize / pLDevInfo->dwBlockSize);
		pLDevInfo->dwFATSectorPerBlockBits	= pVolInfo->wSectorSizeBits - pLDevInfo->dwBlockSizeBits;
		pLDevInfo->dwFATSectorPerBlockMask	= -pLDevInfo->dwFATSectorPerBlock - 1;
	}

#ifndef FFAT_VFAT_SUPPORT
	if (FFATFS_IS_FAT16(pVolInfo) == FFAT_FALSE)
	{
		// vfat를 지원하지 않는 경우 FAT16만 지원
		return FFAT_ENOSUPPORT;
	}
#endif

	if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		// read BPB sector
		r = ffat_al_readSector2(1, pBootSector, 1,
					(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_FS), &stCI);
		if (r == 1)
		{
			// if success get free cluster hint
			ffat_fs_bs_retrieveFat32FSInfo(pVolInfo, pBootSector);
		}
	}

	pVolInfo->dwVIFlag = VI_FLAG_NONE;

	if (dwFlag & FFAT_MOUNT_FAT_MIRROR)
	{
		pVolInfo->dwVIFlag |= VI_FLAG_FAT_MIRROR;
	}

	if (dwFlag & FFAT_MOUNT_CASE_SENSITIVE)
	{
		pVolInfo->dwVIFlag |= VI_FLAG_CASE_SENSITIVE;
	}

	if (dwFlag & FFAT_MOUNT_OS_SPECIFIC_CHAR)
	{
		pVolInfo->dwVIFlag |= VI_FLAG_OS_SPECIFIC_CHAR;
	}

#ifdef FFAT_MIRROR_FAT_ON_REMOVABLE_DEVICE
	if (pLDevInfo->dwFlag & FFAT_DEV_REMOVABLE)
	{
		pVolInfo->dwVIFlag |= VI_FLAG_FAT_MIRROR;
	}
#endif

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBootSector, pLDevInfo->dwDevSectorSize, VI_CXT(pVolInfo));
	return r;
}


/**
 * retrieve boot sector info from boot sector before mount
 * (sector size, cluster size, first data sector)
 *
 * @param		pDev			: [IN] void type pointer for device IO
 *									block device IO를 이용할때 전달된다.
 * @param		dwIOSize		: [IN] current I/O size
 * @param		pdwSectorSize	: [OUT] sector size storage
 * @param		pdwClusterSize	: [OUT] clsuter size storage
 * @param		pdwFirstDataSector	: [OUT] first data sector storage
 * @param		pCxt			: [IN] current running context
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo (dy76.seo@samsung.com)
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] add cluster size
 * @version		FEB-18-2008 [GwangOk Go] add first data sector
 */
FFatErr
ffat_fs_bs_getBSInfoFromBS(void* pDev, t_int32 dwIOSize, t_int32* pdwSectorSize,
						   t_int32* pdwClusterSize, t_uint32* pdwFirstDataSector, ComCxt* pCxt)
{
	FFatErr			r;
	t_int8*			pBootSector = NULL;
	FFatCacheInfo	stCI;
	FatBSCommon*	pBSC;
	FatBS32*		pBS32;
	t_uint32		dwFatSize;
	t_int32			dwRootSectorCount;

	FFAT_ASSERT(pDev);
	FFAT_ASSERT(pdwSectorSize);
	FFAT_ASSERT(pdwClusterSize);
	FFAT_ASSERT(pdwFirstDataSector);

	IF_UK ((EssMath_IsPowerOfTwo(dwIOSize) == ESS_FALSE) || 
			(dwIOSize < 512))
	{
		FFAT_LOG_PRINTF((_T("Invalid IOSize")));
		return FFAT_EINVALID;
	}

	// allocation memory for boot sector
	pBootSector = FFAT_LOCAL_ALLOC(dwIOSize, pCxt);
	FFAT_ASSERT(pBootSector != NULL);

	FFAT_INIT_CI(&stCI, NULL, pDev);

	// read BPB sector
	r = ffat_al_readSector2(0, pBootSector, 1,
				(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_FS), &stCI);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read boot sector")));
		r = FFAT_EIO;
		goto out;
	}

	pBSC	= (FatBSCommon*)pBootSector;
	pBS32	= (FatBS32*)(pBootSector + BS_FAT32_OFFSET);

	// get sector size & cluster size
	*pdwSectorSize	= _TO_UINT16(pBSC->wBytesPerSec);
	*pdwClusterSize	= *pdwSectorSize * pBSC->bSectorsPerClus;

	if (FFAT_BO_UINT16(pBSC->wFATSz16) != 0)
	{
		dwFatSize = FFAT_BO_UINT16(pBSC->wFATSz16);
	}
	else
	{
		dwFatSize = FFAT_BO_UINT32(pBS32->dwFATSz32);
	}

	if (_getFatType(pBootSector) == FFAT_FAT32)
	{
		dwRootSectorCount	= 0;	// this is for FAT16
	}
	else
	{
		dwRootSectorCount	= _getRootDirSectorCount(pBootSector);
	}

	// get first data sector
	*pdwFirstDataSector = FFAT_BO_UINT16(pBSC->wRsvdSecCnt)
						+ (pBSC->bNumFATs * dwFatSize) + dwRootSectorCount;
	
	if ((EssMath_IsPowerOfTwo(*pdwSectorSize) == ESS_TRUE) &&
		(*pdwSectorSize >= 512))
	{
		r = FFAT_OK;
	}
	else
	{
		r = FFAT_ENOSUPPORT;
	}

out:
	FFAT_LOCAL_FREE(pBootSector, dwIOSize, pCxt);

	return r;
}


/**
 * get volume name
 *
 * This function get volume name entry on boot sector
 *
 * @param		pVolInfo		: [IN] volume information
 * @param		psVolLabel		: [OUT] volume name storage
 * @param		dwVolLabelLen	: [IN] character count that can be stored at psVolLabel
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @return		FFAT_ENOMEM		: Not enough memory
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
ffat_fs_bs_getVolLabel(FatVolInfo* pVolInfo, t_wchar* psVolLabel, t_int32 dwVolLabelLen)
{
	FFatErr			r;
	t_int8*			pBootSector = NULL;
	FatBS16*		pBS16;				// pointer for FAT16 boot sector
	FatBS32*		pBS32;				// pointer for FAT32 boot sector
	FatDeSFN*		pDE = NULL;

	FFAT_ASSERT(pVolInfo);
	FFAT_ASSERT(psVolLabel);

	IF_UK (dwVolLabelLen < (FAT_SFN_NAME_CHAR + 1))
	{
		FFAT_LOG_PRINTF((_T("To small name buffer size for Volume name")));
		return FFAT_EINVALID;
	}

	FFAT_MEMSET(psVolLabel, 0x00, (FAT_SFN_NAME_CHAR + 1) * sizeof(t_wchar));

	// allocation memory for boot sector
	pBootSector = FFAT_LOCAL_ALLOC(VI_SS(pVolInfo), VI_CXT(pVolInfo));
	FFAT_ASSERT(pBootSector != NULL);

	// read BPB sector
	r = ffat_fs_cache_readSector(0, pBootSector, 1,
					(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_FS), pVolInfo);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("Fail to read boot sector")));
		r = FFAT_EIO;
		goto out;
	}

	pDE = (FatDeSFN*)FFAT_LOCAL_ALLOC(sizeof(FatDeSFN), VI_CXT(pVolInfo));
	FFAT_ASSERT (pDE != NULL);

	FFAT_MEMSET(pDE, 0x20, sizeof(FatDeSFN));

	if (FFATFS_IS_FAT16(pVolInfo) == FFAT_TRUE)
	{
		pBS16 = (FatBS16*)(pBootSector + BS_FAT1216_OFFSET);
		FFAT_MEMCPY(pDE->sName, pBS16->sVolLab, FAT_SFN_NAME_CHAR);
	}
	else
	{
		pBS32	= (FatBS32*)(pBootSector + BS_FAT32_OFFSET);
		FFAT_MEMCPY(pDE->sName, pBS32->sVolLab, FAT_SFN_NAME_CHAR);
	}

	r = ffat_fs_de_genShortVolumeLabel(pVolInfo, pDE, psVolLabel);
	FFAT_EO(r, (_T("fail to get volume name from directory entry")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pDE, sizeof(FatDeSFN), VI_CXT(pVolInfo));
	FFAT_LOCAL_FREE(pBootSector, VI_SS(pVolInfo), VI_CXT(pVolInfo));

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
ffat_fs_bs_isValidBootSector(t_int8* pBootSector)
{
	FatBSCommon*	pBSC;

	t_uint32		dwSectorSize;			// sector size
	t_int16			wSectorSizeBits;		// sector size bit count
	t_uint32		dwSectorCount;			// total sector count
	t_int16			wSectorPerCluster;		// sector per cluster
	t_int16			wSectorPerClusterBits;	// sector per cluster bit count

	t_int32			dwClusterSize;			// cluster size in byte
	t_int32			dwClusterSizeBits;		// cluster size bit count

	t_uint32		dwClusterCount;			// total cluster count

	// check signature
	IF_UK (((t_uint8)pBootSector[BS_SIG0_OFFSET] != BS_SIG0) ||
			((t_uint8)pBootSector[BS_SIG1_OFFSET] != BS_SIG1))
	{
		FFAT_LOG_PRINTF((_T("Invalid boot sector signature !!")));
		return FFAT_EINVALID;
	}

	pBSC	= (FatBSCommon*)pBootSector;

	dwSectorSize			= _TO_UINT16(pBSC->wBytesPerSec);
	wSectorSizeBits			= (t_int16)EssMath_Log2(dwSectorSize);
	IF_UK (wSectorSizeBits < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid sector size in boot sector")));
		return FFAT_EINVALID;
	}

	// 2012.12.14_anshuma.s@samsung.com_Fix the bug for Issue QA2012120003 
	/* Add extra checks for boot sector fields like dwSectorSize,  wSectorPerCluster,
	 * bNumFATs and wRsvdSecCnt
	 */
 	IF_UK((dwSectorSize == 0) || (dwSectorSize > 4096))
	{
		FFAT_LOG_PRINTF((_T("Invalid size")));
		return FFAT_EINVALID;
	}

	IF_UK((dwSectorSize != 512) && (dwSectorSize != 1024) && (dwSectorSize != 2048) && (dwSectorSize != 4096))
	{
		FFAT_LOG_PRINTF((_T("Invalid size, size does not belong to set {512,1024,2048,4096}")));
		return FFAT_EINVALID;
	}
	dwSectorCount			= _TO_UINT16(pBSC->wTotSec16);
	if (dwSectorCount == 0)
	{
		dwSectorCount		= FFAT_BO_UINT32(pBSC->dwTotSec32);
		IF_UK(dwSectorCount == 0)
		{
			FFAT_LOG_PRINTF((_T("Invalid sector count in boot sector")));
			return FFAT_EINVALID;
		}
	}

	wSectorPerCluster			= pBSC->bSectorsPerClus;
	wSectorPerClusterBits		= (t_int16)EssMath_Log2(wSectorPerCluster);
	IF_UK (wSectorPerClusterBits < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid sector per cluster in boot sector")));
		return FFAT_EINVALID;
	}

	IF_UK((wSectorPerCluster == 0) || (wSectorPerCluster & (wSectorPerCluster - 1)) != 0)	
	{
		FFAT_LOG_PRINTF((_T("Sectors per cluster is not power of 2")));
		return FFAT_EINVALID;
	}

	dwClusterSize		= wSectorPerCluster * dwSectorSize;
	dwClusterSizeBits	= EssMath_Log2(dwClusterSize);
	IF_UK (dwClusterSizeBits < 0)
	{
		FFAT_LOG_PRINTF((_T("Invalid cluster size in boot sector")));
		return FFAT_EINVALID;
	}

	dwClusterCount	= _getDataClusterCount(pBootSector);

	if (FFAT_BO_UINT16(pBSC->wFATSz16) != 0)
	{
		// FAT 32
		IF_UK (dwClusterCount > BS_FAT16_MAX_CLUSTER_COUNT)
		{
			FFAT_LOG_PRINTF((_T("Invalid cluster config")));
			return FFAT_EINVALID;
		}
	}
	else
	{
		// FAT12 or FAT16
		IF_UK (dwClusterCount <= BS_FAT16_MAX_CLUSTER_COUNT)
		{
			FFAT_LOG_PRINTF((_T("Invalid cluster config")));
			return FFAT_EINVALID;
		}
	}

	IF_UK (pBSC->bNumFATs > 5 || pBSC->bNumFATs < 1)
	{
		FFAT_LOG_PRINTF((_T("Invalid FAT count")));
		return FFAT_EINVALID;
	}

	IF_UK(pBSC->wRsvdSecCnt == 0)
	{
		FFAT_LOG_PRINTF((_T("Reserved sector count should be greater than 0")));
		return FFAT_EINVALID;
	}
	return FFAT_OK;
}


//=============================================================================
//
//	STATIC FUNCTIONS
//


/**
 * retrieve volume information from boot sector
 *
 * @param		pBootSector		: format information
 * @return		FFAT_OK			: success
 * @return		0xFFFFFFFF(-1)	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static t_uint32
_getDataClusterCount(t_int8* pBootSector)
{
	t_uint32		dwFATSz;
	t_uint32		dwTotSec;
	t_uint32		dwDataCluster;
	t_uint32		dwRootSectors;		// sectors for root directory
	t_uint32		dwReservedSectors;	// reserved sector count
	t_uint32		dwNumFATs;			// count of FATs
	t_uint32		dwClusterSizeBits;	// bit count for cluster size
	FatBSCommon*	pBSC;				// common part of boot sector
	FatBS32*		pBS32;

	FFAT_ASSERT(pBootSector);

	pBSC	= (FatBSCommon*)pBootSector;
	pBS32	= (FatBS32*)(pBootSector + BS_FAT32_OFFSET);

	if (FFAT_BO_UINT16(pBSC->wFATSz16) != 0)
	{
		dwFATSz = FFAT_BO_UINT16(pBSC->wFATSz16);
	}
	else
	{
		dwFATSz = FFAT_BO_UINT32(pBS32->dwFATSz32);
	}

	dwTotSec = _TO_UINT16(pBSC->wTotSec16);
	if (dwTotSec == 0)
	{
		dwTotSec = FFAT_BO_UINT32(pBSC->dwTotSec32);
	}

	dwReservedSectors	= FFAT_BO_UINT16(pBSC->wRsvdSecCnt);
	dwNumFATs			= pBSC->bNumFATs;
	dwRootSectors		= _getRootDirSectorCount(pBootSector);
	dwClusterSizeBits	= EssMath_Log2(pBSC->bSectorsPerClus);

	dwDataCluster = dwTotSec - (dwReservedSectors + (dwNumFATs * dwFATSz));
	dwDataCluster = dwDataCluster - dwRootSectors;
	dwDataCluster = dwDataCluster >> dwClusterSizeBits;

	return dwDataCluster;
}


/**
 * return FAT type
 *
 * @param		pBootSector		: boot sector pointer
 * @return		FFAT_FAT12		: The volume is FAT12
 * @return		FFAT_FAT16		: The volume is FAT16
 * @return		FFAT_FAT32		: The volume is FAT32
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static FFatFatType
_getFatType(t_int8* pBootSector)
{
	t_uint32		dwClusterCount;

	FFAT_ASSERT(pBootSector != NULL);

	dwClusterCount = _getDataClusterCount(pBootSector);

	if (dwClusterCount < BS_FAT12_MAX_CLUSTER_COUNT)
	{
		return FFAT_FAT12;
	}

	if (dwClusterCount < BS_FAT16_MAX_CLUSTER_COUNT)
	{
		return FFAT_FAT16;
	}

	return FFAT_FAT32;
}


/**
 * return EOC value
 *
 * @param		pBootSector		: boot sector
 * @return		FAT12_EOC		: EOC mark for FAT12
 * @return		FAT16_EOC		: EOC mark for FAT16
 * @return		FAT32_EOC		: EOC mark for FAT32
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static t_uint32
_getEOC(t_int8* pBootSector)
{
	FFatFatType		dwFatType;

	FFAT_ASSERT(pBootSector != NULL);

	dwFatType = _getFatType(pBootSector);

	if (dwFatType == FFAT_FAT12)
	{
		return FAT12_EOC;
	}

	if (dwFatType == FFAT_FAT16)
	{
		return FAT16_EOC;
	}

	return FAT32_EOC;
}


/**
 * return BAD value
 *
 * @param		pBootSector		: boot sector
 * @return		FAT12_BAD		: BAD mark for FAT12
 * @return		FAT16_BAD		: BAD mark for FAT16
 * @return		FAT32_BAD		: BAD mark for FAT32
 * @author		InHwan Choi
 * @version		NOV-17-2007 [InHwan Choi] First Writing.
 */
static t_uint32
_getBAD(t_int8* pBootSector)
{
	FFatFatType		dwFatType;

	FFAT_ASSERT(pBootSector != NULL);

	dwFatType = _getFatType(pBootSector);

	if (dwFatType == FFAT_FAT12)
	{
		return FAT12_BAD;
	}

	if (dwFatType == FFAT_FAT16)
	{
		return FAT16_BAD;
	}

	return FAT32_BAD;
}


/**
 * return the count of sectors occupied by the root directory
 *
 * @param		pBootSector		: boot sector pointer
 * @return		sector count for root directory
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static t_uint32
_getRootDirSectorCount(t_int8* pBootSector)
{
	FatBSCommon*		pBSC;
	t_uint32			dwRootDirSectors;	// sector count for root directory
	t_uint32			dwRootEntryCount;	// count of root directory entry
	t_uint32			dwSectorSize;		// sector size
	t_uint32			dwSectorSizeBits;	// bit count for sector size

	FFAT_ASSERT(pBootSector != NULL);

	pBSC = (FatBSCommon*)pBootSector;

	dwRootEntryCount	= (t_uint32)_TO_UINT16(pBSC->wRootEntCnt);
	if (dwRootEntryCount == 0)
	{
		return 0;
	}

	dwSectorSize		= (t_uint32)_TO_UINT16(pBSC->wBytesPerSec);
	dwSectorSizeBits	= EssMath_Log2(dwSectorSize);
	dwRootDirSectors	= dwRootEntryCount * FAT_DE_SIZE;		// x 32 byte

	dwRootDirSectors = ESS_MATH_CDB(dwRootDirSectors, dwSectorSize, dwSectorSizeBits);

	return dwRootDirSectors;
}

