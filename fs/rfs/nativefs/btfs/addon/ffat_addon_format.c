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
 * @file		ffat_addon_format.h
 * @brief		format operation.
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-21-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"

#include "ffat_al.h"

#include "ffatfs_api.h"

#include "ffat_addon_format.h"
#include "ffat_addon_log.h"

#include "ffat_main.h"
#include "ffat_node.h"
#include "ffat_vol.h"


#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_FORMAT)

typedef struct _ClusterPerDiskSize
{
	t_uint		dwSectorCount;
	t_int32		dwSecPerClus;
} ClusterPerDiskSize;

static const ClusterPerDiskSize stCPD16[] =
{
	{ 8400,			0},		// up to 4.1MB
	{ 32680,		2},		// up to 16MB
	{ 262144,		4},		// up to 128MB
	{ 524288,		8},		// up to 256MB
	{ 1048576,		16},	// up to 512MB
	{ 2097152,		32},	// up to 1GB    <-- not recommended above 1GB
	{ 4194304,		64},	// up to 2GB
	{ 0,			0}
};

static const ClusterPerDiskSize stCPD32[] =
{
	{ 66600,		0},		// up to 32.5MB
	{ 133120,		1},		// up to 65MB
	{ 264192,		2},		// up to 129MB
	{ 532480,		4},		// up to 260MB
	{ 16777216,		8},		// up to 8GB
	{ 33554432,		16},	// up to 16GB
	{ 67108864,		32},	// up to 32GB
	{ 0xFFFFFFFF,	64},	// greater than 32GB
	{ 0,			0}
};

#define _SPC_MAX			128		//!< max sector per cluster 

// STATIC FUNCTIONS
static FFatErr	_checkDeviceInfo(FFatFormatInfo* pFormatInfo);
static t_int32	_getClusterSize(FFatFatType dwFatType, t_uint32 dwNumSectors, t_int32 dwSectorSize);
static FFatErr	_checkFormatInfo(FFatFormatInfo* pFormatInfo);
static FFatErr	_buildBS(FFatFormatInfo* pFormatInfo, t_int8* pBootSector);
static FFatErr	_changeEndian(FFatFormatInfo* pFI, t_int8* pBootSector);

static FFatErr	_buildBSCommon(FFatFormatInfo* pFI, 
									FatBSCommon* pBSC, t_uint32 dwReservedSectors);
static FFatErr	_buildBS32(FFatFormatInfo* pFI, FatBSCommon* pBSC, 
							FatBS32* pBS32, t_uint32* pdwReservedSectors);
static FFatErr	_buildBS16(FFatFormatInfo* pFI, FatBSCommon* pBSC, 
							FatBS16* pBS16, t_uint32* pdwReservedSectors);

static FFatErr	_buildFAT32FSInfo(FFatFormatInfo* pFI, t_int8* pBootSector);

static FFatErr	_eraseAll(FFatFormatInfo* pFI);
static FFatErr	_initFat(FFatFormatInfo* pFI, Vol* pVol, ComCxt* pCxt);
static FFatErr	_InitRootDirectory(FFatFormatInfo* pFI, Vol* pVol, ComCxt* pCxt);
static void		_formatMarkFat(t_int8* pSector, t_uint32 dwCluster,
									t_uint32 dwValue, FFatFatType dwFatType);

#define _FORMAT_PRINT_ERROR(msg)			FFAT_PRINT_ERROR(msg);

/**
 * format a volume
 *
 * @param		pFormatInfo	: format information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-21-2006 [DongYoung Seo] First Writing.
 * @version		MAR-05-2009 [GwangOk Go] modify FFatFormatInfo structure
 */
FFatErr
ffat_addon_format(FFatFormatInfo* pFormatInfo, ComCxt* pCxt)
{
	Vol*			pVol = NULL;
	t_int8*			pBuffTemp = NULL;
	t_int8*			pBootSector = NULL;
	t_int32			dwTemp;
	FFatErr			r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pFormatInfo);

	FFAT_DEBUG_PRINTF((_T("Formatting a logical device")));
	FFAT_DEBUG_PRINTF((_T("Sector Count/Size : %d/ %d"), pFormatInfo->dwSectorCount, pFormatInfo->dwSectorSize));

	// make reserved area at the end of volume
	IF_UK (pFormatInfo->dwSectorCount < LOG_SECTOR_COUNT)
	{
		_FORMAT_PRINT_ERROR((_T("Too small device")));
		return FFAT_EINVALID;
	}

//	pFormatInfo->dwSectorCount -= LOG_SECTOR_COUNT;

	r = _checkFormatInfo(pFormatInfo);
	FFAT_EO(r, NULL);

	pVol = FFAT_LOCAL_ALLOC(sizeof(Vol), pCxt);
	FFAT_ASSERT(pVol != NULL);

	FFAT_MEMSET(pVol, 0x00, sizeof(Vol));

	VOL_DEV(pVol) = pFormatInfo->pDevice;

	pBootSector = FFAT_LOCAL_ALLOC(pFormatInfo->dwSectorSize, pCxt);
	FFAT_ASSERT(pBootSector != NULL);

	FFAT_MEMSET(pBootSector, 0x00, pFormatInfo->dwSectorSize);

	// adjust align basis
	if (pFormatInfo->wAlignBasis < FFAT_FORMAT_ALIGN)
	{
		// default 4 sector align
		pFormatInfo->wAlignBasis = FFAT_FORMAT_ALIGN;
	}

	r = _buildBS(pFormatInfo, pBootSector);
	FFAT_EO(r, (_T("fail to build BS")));

	if (pFormatInfo->psVolumeLabel)
	{
		FFAT_ASSERT(FFAT_WCSLEN(pFormatInfo->psVolumeLabel) < FFAT_VOLUME_NAME_MAX_LENGTH);

		// check volume name is valid
		r = ffat_fs_de_isValidVolumeLabel((t_wchar*)pFormatInfo->psVolumeLabel);
		if (r != FFAT_TRUE)
		{
			_FORMAT_PRINT_ERROR((_T("invalid name for FAT filesystem")));
			goto out;
		}
	}

	// initialize boot sector
	// delete boot sector
	pBuffTemp = FFAT_LOCAL_ALLOC(pFormatInfo->dwSectorSize, pCxt);
	FFAT_ASSERT(pBuffTemp != NULL);

	FFAT_MEMSET(pBuffTemp, 0x00, pFormatInfo->dwSectorSize);

	r = ffat_ldev_writeSector(pFormatInfo->pDevice, 0, pBuffTemp, 1);
	FFAT_EO(r, (_T("Fail to init boot sector")));

	// write signature
	pBootSector[BS_SIG0_OFFSET] = (t_int8)(BS_SIG0 & 0xFF);
	dwTemp = BS_SIG1;
	pBootSector[BS_SIG1_OFFSET] = (t_int8)(dwTemp & 0xFF);

	// erase all boot sector area
	r = _eraseAll(pFormatInfo);
	IF_UK ((r < 0) && (r != FFAT_ENOSUPPORT))
	{
		_FORMAT_PRINT_ERROR((_T("fail to erase entire volume")));
		goto out;
	}

	// write boot sector
	r = _changeEndian(pFormatInfo, pBootSector);
	FFAT_EO(r, (_T("fail to change byte order")));

	// retrieve volume information from boot sector
	r = ffat_fs_bs_retrieveCommon(VOL_VI(pVol), pBootSector,
							pFormatInfo->dwSectorCount, pFormatInfo->dwSectorSizeBits);
	FFAT_EO(r, (_T("fail to retrieve volume information")));

	r = _initFat(pFormatInfo, pVol, pCxt);
	FFAT_EO(r, (_T("fail to create FAT area")));

	r = _InitRootDirectory(pFormatInfo, pVol, pCxt);
	FFAT_EO(r, (_T("fail to create root directory")));

	// write boot sector
	r = ffat_ldev_writeSector(pFormatInfo->pDevice, 0, pBootSector, 1);
	IF_UK (r != 1)
	{
		_FORMAT_PRINT_ERROR((_T("fail to write boot sector")));
		r = FFAT_EIO;
		goto out;
	}

	if (pFormatInfo->dwFatType == FFAT_FAT32)
	{
		// write backup boot sector
		r = ffat_ldev_writeSector(pFormatInfo->pDevice, FAT_BACKUP_BOOT, pBootSector, 1);
		IF_UK (r != 1)
		{
			_FORMAT_PRINT_ERROR((_T("fail to write boot sector")));
			r = FFAT_EIO;
			goto out;
		}

		r = _buildFAT32FSInfo(pFormatInfo, pBootSector);
		FFAT_EO(r, (_T("Fail to build FAT32 FSInfo")));

		// write SFInfo sector
		r = ffat_ldev_writeSector(pFormatInfo->pDevice, 1, pBootSector, 1);
		IF_UK (r != 1)
		{
			_FORMAT_PRINT_ERROR((_T("fail to write boot sector")));
			r = FFAT_EIO;
			goto out;
		}
	}

	// write signature for log area
	r = ffat_log_initLogArea(pVol, pFormatInfo->pDevice, pVol->stVolInfo.dwFirstFatSector - LOG_SECTOR_COUNT,
							pVol->stVolInfo.dwFirstFatSector - 1, pCxt);
	FFAT_EO(r, (_T("fail to init reserved area")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuffTemp, pFormatInfo->dwSectorSize, pCxt);
	FFAT_LOCAL_FREE(pBootSector, pFormatInfo->dwSectorSize, pCxt);
	FFAT_LOCAL_FREE(pVol, sizeof(Vol), pCxt);

	return r;
}


/**
 * Check logical device information validity.
 *
 * @param		pFormatInfo		: format information
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_checkFormatInfo(FFatFormatInfo* pFormatInfo)
{
	FFatErr		r;

	ESS_ASSERT(pFormatInfo);

	r = _checkDeviceInfo(pFormatInfo);
	FFAT_EO(r, (_T("Invalid device info")));

	// check cluster size
	IF_UK (pFormatInfo->dwSectorsPerCluster > _SPC_MAX)
	{
		_FORMAT_PRINT_ERROR((_T("Too big cluster size")));
		return FFAT_EINVALID;
	}

	if (pFormatInfo->dwSectorSize > FAT_SECTOR_MAX_SIZE)
	{
		_FORMAT_PRINT_ERROR((_T("Too big sector size")));
		return FFAT_EINVALID;
	}

	// get a valid cluster size
	if (pFormatInfo->dwSectorsPerCluster == 0)
	{
		r = _getClusterSize(pFormatInfo->dwFatType, 
				pFormatInfo->dwSectorCount, pFormatInfo->dwSectorSize);
		FFAT_EO(r, (_T("Fail to get cluster size")));

		pFormatInfo->dwSectorsPerCluster = r;
	}
	
	IF_UK (EssMath_IsPowerOfTwo(pFormatInfo->dwSectorsPerCluster) == ESS_FALSE)
	{
		FFAT_EO(r, (_T("Invalid cluster size")));
	}

	if ((pFormatInfo->dwRsvdSector < 0) || ((t_uint32)pFormatInfo->dwRsvdSector > pFormatInfo->dwSectorCount))
	{
		FFAT_EO(r, (_T("invalid reserved sector count")));
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * Check logical device information validity.
 *
 * @param		pFormatInfo		: format information
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_checkDeviceInfo(FFatFormatInfo* pFormatInfo)
{
	t_int32			dwBits;

	FFAT_ASSERT(pFormatInfo);

	IF_UK ((pFormatInfo->dwSectorSize < FFAT_LDEV_MIN_SECTOR_SIZE) ||
			(pFormatInfo->dwSectorSize > FFAT_LDEV_MAX_SECTOR_SIZE))
	{
		_FORMAT_PRINT_ERROR((_T("too small or big sector size")));
		return FFAT_EINVALID;
	}

	IF_UK (EssMath_IsPowerOfTwo(pFormatInfo->dwSectorSize) == 0)
	{
		_FORMAT_PRINT_ERROR((_T("Invalid OptimialIOSize")));
		return FFAT_EINVALID;
	}

	dwBits = EssMath_Log2(pFormatInfo->dwSectorSize);
	IF_UK (dwBits != pFormatInfo->dwSectorSizeBits)
	{
		_FORMAT_PRINT_ERROR((_T("Invalid parameter, dwSectorSizebits")));
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/**
 * get cluster size for sector count
 *
 * @param		dwFatType		: FAT type
 * @param		dwNumSectors	: sector count
 * @param		dwSectorSize	: sector size in byte
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 * @version		MAR-26-2007 [DongYoung Seo] add too small partition check
 */
static t_int32
_getClusterSize(FFatFatType dwFatType, t_uint32 dwNumSectors, t_int32 dwSectorSize)
{
	const ClusterPerDiskSize*		pMap;

	t_int32		i;

	if (dwFatType == FFAT_FAT16)
	{
		pMap = stCPD16;
	}
	else IF_LK (dwFatType == FFAT_FAT32)
	{
		pMap = stCPD32;
	}
	else
	{
		_FORMAT_PRINT_ERROR((_T("Invalid FAT type")));
		return FFAT_EINVALID;
	}

	if (dwSectorSize != FFAT_SECTOR_SIZE)
	{
		dwNumSectors = dwNumSectors / (dwSectorSize / FFAT_SECTOR_SIZE);
	}

	i = 0;

	IF_UK (dwNumSectors <= pMap[i].dwSectorCount)
	{
		_FORMAT_PRINT_ERROR((_T("TOO small partition")));
		return FFAT_EINVALID;
	}

	i++;

	while (pMap[i].dwSectorCount != 0)
	{
		if (dwNumSectors <= pMap[i].dwSectorCount)
		{
			return pMap[i].dwSecPerClus;
		}
		i++;
	}

	return FFAT_EINVALID;
}


/**
 * Make a boot sector
 *
 * @param		pFormatInfo		: format information
 * @param		pBootSector		: boot sector storage
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_buildBS(FFatFormatInfo* pFormatInfo, t_int8* pBootSector)
{	
	FatBSCommon*	pBSC;
	t_uint32		dwReservedSectors;
	FFatErr			r;

	pBSC		= (FatBSCommon*)pBootSector;
	
	if (pFormatInfo->dwRsvdSector == 0)
	{
		// default 값을 사용한다.
		dwReservedSectors = FFAT_BPB_RESERVED_SECTOR;
	}
	else
	{
		// 받은것으로 부터 reserved sector 를 계산한다.
		dwReservedSectors = pFormatInfo->dwRsvdSector;
	}

	// add sectors for log area to reserved sector count
	dwReservedSectors += LOG_SECTOR_COUNT;

re_align:
	r = _buildBSCommon(pFormatInfo, pBSC, dwReservedSectors);
	FFAT_EO(r, (_T("fail to build Boot Sector Common")));

	if (pFormatInfo->dwFatType == FFAT_FAT32)
	{
		r = _buildBS32(pFormatInfo, pBSC, (FatBS32*)(pBootSector + BS_FAT32_OFFSET), &dwReservedSectors);
		FFAT_EO(r, (_T("fail to build boot sector for FAT32")));
	}
	else
	{
		r = _buildBS16(pFormatInfo, pBSC, (FatBS16*)(pBootSector + BS_FAT1216_OFFSET), &dwReservedSectors);
		FFAT_EO(r, (_T("fail to build boot sector for FAT16")));
	}

	if (r > 0)
	{
		goto re_align;
	}

	r = FFAT_OK;
out:

	return r;
}

/**
 * Make a boot sector common part
 *
 * @param		pFI					: [IN] format information
 * @param		pBSC				: [OUT] Boot Sector Common part pointer.
 * @param		dwReservedSectors	: [IN] reserved sector count
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_buildBSCommon(FFatFormatInfo* pFI, FatBSCommon* pBSC, t_uint32 dwReservedSectors)
{
	FFAT_ASSERT(pFI);
	FFAT_ASSERT(pBSC);

	// fill jump instruction code
	pBSC->bJmpBoot[0] = FAT_JMP_INSTRUCTION0;
	pBSC->bJmpBoot[1] = FAT_SIG_FFAT1;
	pBSC->bJmpBoot[2] = FAT_JMP_INSTRUCTION2;

	// fill OEM name string
	FFAT_MEMCPY(pBSC->pOemName, BS_OEM_NAME, BS_OEM_NAME_LENGTH);

	// fill byte per sector
	pBSC->wBytesPerSec[0] = (t_uint8) (pFI->dwSectorSize & 0x00FF);
	pBSC->wBytesPerSec[1] = (t_uint8) ((pFI->dwSectorSize  & 0xFF00) >> 8);

	pBSC->wRsvdSecCnt = (t_uint16)dwReservedSectors;

	FFAT_ASSERT(pFI->dwSectorsPerCluster != 0);
	FFAT_ASSERT(EssMath_IsPowerOfTwo((t_int16)pFI->dwSectorsPerCluster) == ESS_TRUE);

	IF_UK (pFI->dwSectorsPerCluster > BPB_MAX_SECTOR_PER_CLUSTER)
	{
		_FORMAT_PRINT_ERROR((_T("too big sector per cluster")));
		return FFAT_EINVALID;
	}

	pBSC->bSectorsPerClus = (t_uint8)pFI->dwSectorsPerCluster;

	IF_UK ((pBSC->bSectorsPerClus * pFI->dwSectorSize) > BPB_MAX_CLUSTER_SIZE)
	{
		_FORMAT_PRINT_ERROR((_T("Too big cluster size")));
		return FFAT_EINVALID;
	}

	pBSC->bNumFATs = (t_uint8) FFAT_BPB_DEFAULT_FAT_COUNT;
	pBSC->wFATSz16 = 0;

	// fill root entry count, total sector16/32
	if (pFI->dwFatType == FFAT_FAT16)
	{
		// set root entry count to 512
		pBSC->wRootEntCnt[1]	= (t_uint8) ((FFAT_ROOT_DIR_ENTRY_COUNT >> 8) & 0xFF);
		pBSC->wRootEntCnt[0]	= (t_uint8) (FFAT_ROOT_DIR_ENTRY_COUNT & 0xFF);
		if (pFI->dwSectorCount < 0x10000)
		{
			pBSC->wTotSec16[0]	= (t_uint8) (pFI->dwSectorCount & 0x00FF);
			pBSC->wTotSec16[1]	= (t_uint8) ((pFI->dwSectorCount & 0xFF00) >> 8);
			pBSC->dwTotSec32	= 0L;
		}
		else
		{
			pBSC->wTotSec16[0]	= (t_uint8) 0;
			pBSC->wTotSec16[1]	= (t_uint8) 0;
			pBSC->dwTotSec32	= (t_uint32)pFI->dwSectorCount;
		}
	}
	else IF_LK (pFI->dwFatType == FFAT_FAT32)
	{
		pBSC->wRootEntCnt[1]	= (t_uint8) 0;
		pBSC->wRootEntCnt[0]	= (t_uint8) 0;
		pBSC->wTotSec16[0]		= (t_uint8) 0;
		pBSC->wTotSec16[1]		= (t_uint8) 0;
		pBSC->dwTotSec32		= pFI->dwSectorCount ;
	}
	else
	{
		_FORMAT_PRINT_ERROR((_T("invalid fat type")));
		return FFAT_EINVALID;
	}

	pBSC->bMedia		= FAT_MEDIA_FIXED;
	pBSC->wSecPerTrack	= pFI->wAlignBasis;	// record this field for dosfsck
	pBSC->wNumHeads		= 1;				// record this field for dosfsck
	pBSC->dwHiddSec		= 0;

	FFAT_ASSERT(FFAT_ROOT_DIR_ENTRY_COUNT <= FAT_DE_MAX);

	IF_UK (((FFAT_ROOT_DIR_ENTRY_COUNT * FAT_DE_SIZE) % pFI->dwSectorSize) != 0)
	{
		_FORMAT_PRINT_ERROR((_T("directory entries should be multiple of 16!")));
		FFAT_DEBUG_PRINTF((_T(" = %d"), FFAT_ROOT_DIR_ENTRY_COUNT));
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/**
 * Make a boot sector FAT32 part
 *
 * @param		pFI					: [IN] format information
 * @param		pBSC				: [IN] Boot Sector Common part pointer.
 * @param		pBS32				: [IN/OUT] Boot Sector for FAT32 part pointer.
 * @param		pdwReservedSectors	: [OUT] reserved sector count for alignment
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 * @version		NOV-04-2000 [DongYoung Seo] add year validity checking code
 *											the year must be stored over 1980
 * @version		NOV-24-2008 [DongYoung Seo] add FAT size checking code
 */
static FFatErr
_buildBS32(FFatFormatInfo* pFI, FatBSCommon* pBSC, FatBS32* pBS32, t_uint32* pdwReservedSectors)
{
	t_uint32		dwClusterCount;
	t_uint32		dwNumSectors;		// sector count of this volume
	t_uint32		dwFatData;			// sector count for FAT and Data area
	t_uint32		dwFatLength;		// sector count of FAT area
	t_int32			dwBytesPerSec;		// byte per a sector
	t_uint32		dwStartDataSector;
	t_uint32		dwCreationTime;		// creation(format) time
	t_int32			dwAlign;
	FFatTime		stTime;				// for volume ID
	t_uint32		dwClustersPerFatSector;		// cluster entry count per a FAT sector
	t_uint32		dwTemp;				// temporary variable

	FFAT_ASSERT(pFI);
	FFAT_ASSERT(pFI->dwFatType == FFAT_FAT32);
	FFAT_ASSERT(pBSC);
	FFAT_ASSERT(pBS32);
	FFAT_ASSERT(pdwReservedSectors);

	dwNumSectors = pFI->dwSectorCount;

	///////////////////////////////////////////////////////////////////////////////
	//
	//	How to get FAT size(sector count) ?
	//
	//	BytesPerSec	: bytes per a sector
	//	4			: a FAT entry size for a FAT32 cluster
	//
	//	Sector count for FAT and Data ==> dwNumSectors - pBSC->wRsvdSecCnt
	//	Bytes for FAT and Data = {Sector count for FAT and Data} * BytesPerSec
	//	Total byte usage for a Cluster = ({a cluster entry on FAT} * {FAT Count}) + {size of a cluster}
	//
	//	Cluster Count = {Bytes for FAT and Data} / {Total byte usage for a Cluster}
	//	FAT Sector Count for FAT32 = ({Cluster Count} * 4) / {BytesPerSec} with ceiling divide (rounding up)
	//
	//	BUT. Bytes for FAT and Data area is too big. (may be over 32bit integer)
	//	So. I optimize this equation as like belows.
	//
	//	The minimum sector size is 512. and 512 byte FAT sector can contain 128 entries.
	//
	//	FAT Sector Count for FAT32 ==>
	//
	//					({Bytes for FAT and Data} / {Total byte usage for a Cluster}) * 4
	//			=	--------------------------------------------------------------
	//										BytesPerSec
	//
	//								{Bytes for FAT and Data} * 4
	//			=	----------------------------------------------------
	//					{Total byte usage for a Cluster} * BytesPerSec
	//
	//		. substitute "Bytes for FAT and Data = {Sector count for FAT and Data} * BytesPerSec"
	//
	//					{Sector count for FAT and Data} * BytesPerSec * 4
	//			=	--------------------------------------------------------
	//					{Total byte usage for a Cluster} * BytesPerSec
	//
	//					{Sector count for FAT and Data} * 4
	//			=	-----------------------------------------	(with rounding up)
	//					{Total byte usage for a Cluster}
	//
	///////////////////////////////////////////////////////////////////////////////

	dwFatData		= dwNumSectors - pBSC->wRsvdSecCnt;	// get total sector count for FAT & data area
	dwBytesPerSec	= pFI->dwSectorSize;				// get sector size in byte

	// sector size ==> pBSC->bSectorsPerClus * dwBytesPerSec
	// byte for a cluster ==> 4 * pBSC->bNumFATs
	dwFatLength		= ((pBSC->bSectorsPerClus * dwBytesPerSec) + (4 * pBSC->bNumFATs));
															// get byte per a cluster storage
	dwFatLength		= ESS_MATH_CD((dwFatData * 4), dwFatLength);
															// get cluster count

	pBS32->dwFATSz32	= dwFatLength;
	pBS32->wExtFlags	= 0;
	pBS32->wFSVer		= 0;
	pBS32->dwRootClus	= 0x00000002;
	pBS32->wFSInfo		= 0x0001;
	pBS32->wBkBootSec	= FAT_BACKUP_BOOT;

	FFAT_MEMSET(pBS32->sReserved, 0, sizeof(pBS32->sReserved));

	// fat12/16 Information
	pBS32->bDrvNum		= 0x00;
	pBS32->bReserved1	= 0;
	pBS32->bBootSig		= FAT_SIG_EXT;

	FFAT_LOCALTIME(&stTime);

	dwCreationTime		= stTime.tm_mday + ((stTime.tm_mon + 1) << 5);
	if (stTime.tm_year >= 80)
	{
		dwCreationTime	+= ((stTime.tm_year + 1900 - 1980) << 9);
	}

	dwCreationTime		= dwCreationTime << 16;
	dwCreationTime		+= (stTime.tm_sec >> 1) + (stTime.tm_min << 5) + (stTime.tm_hour << 11);

	pBS32->dwVolID[0]	= (t_uint8)(dwCreationTime & 0x000000ff);
	pBS32->dwVolID[1]	= (t_uint8)((dwCreationTime & 0x0000ff00) >> 8);
	pBS32->dwVolID[2]	= (t_uint8)((dwCreationTime & 0x00ff0000) >> 16);
	pBS32->dwVolID[3]	= (t_uint8)((dwCreationTime & 0xff000000) >> 24);
	FFAT_MEMCPY(pBS32->sVolLab, "NO NAME    ", BS_VOLUME_LABEL_MAX_LENGTH);

	FFAT_MEMCPY(pBS32->sFileSysType, BS_SIGN_FAT32, BS_FILESYSTEM_TYPE_LENGTH);

	dwFatData		-= (dwFatLength * pBSC->bNumFATs);
	dwFatData		-= (dwFatData % pBSC->bSectorsPerClus);
	dwClusterCount	= ESS_MATH_CD(dwFatData, pBSC->bSectorsPerClus);
	IF_UK (dwClusterCount < BS_FAT16_MAX_CLUSTER_COUNT)
	{
		_FORMAT_PRINT_ERROR((_T("too small cluster count!")));
		_FORMAT_PRINT_ERROR((_T("%d < %d"), dwClusterCount, BS_FAT16_MAX_CLUSTER_COUNT));
		return FFAT_EINVALID;
	}

	dwStartDataSector = pBSC->wRsvdSecCnt + (pBSC->bNumFATs * dwFatLength);

	IF_UK (EssMath_IsPowerOfTwo((t_int16)pFI->wAlignBasis) == 0)
	{
		_FORMAT_PRINT_ERROR((_T("Invalid logical device info, wAlignBasis")));
		_FORMAT_PRINT_ERROR((_T("It should be power of two")));
		return FFAT_EINVALID;
	}

	dwAlign = pFI->dwStartSector + dwStartDataSector;
	if (pFI->wAlignBasis == 0)
	{
		dwAlign = 0;
	}
	else
	{
		dwAlign = dwAlign & (pFI->wAlignBasis - 1);
	}

	if (dwAlign != 0)
	{
		FFAT_PRINT_VERBOSE((_T("realign, DIFF : %d"), dwAlign));
		*pdwReservedSectors = *pdwReservedSectors + (pFI->wAlignBasis - dwAlign);
		return dwAlign;
	}

	// check is there enough space for cluster 0 and 0
	dwClustersPerFatSector	= pFI->dwSectorSize / 4;		// get clusters per a FAT Sector

	// check cluster count and cluster size
	dwTemp = (t_uint32)ESS_MATH_CD((dwClusterCount + 2), dwClustersPerFatSector);
	if (dwFatLength != dwTemp)
	{
		FFAT_PRINT_VERBOSE((_T("realign for FAT size")));
		// incorrect FAT length
		*pdwReservedSectors = *pdwReservedSectors + 1;
		return 1;
	}

	return FFAT_OK;
}


/**
 * Make a boot sector FAT16 part
 *
 * @param		pFI			: format information
 * @param		pBSC		: Boot Sector Common part pointer.
 * @param		pBS16		: boot sector 16 pointer
 * @param		pdwReservedSectors	: reserved sector count storage
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 * @version		SEP-10-2008 [DongYoung Seo] Add 2GB volume checking code.
 *								The maximum volume size 2GB for FAT16
 * @version		NOV-24-2008 [DongYoung Seo] add FAT size checking code
 */
static FFatErr
_buildBS16(FFatFormatInfo* pFI, FatBSCommon* pBSC, FatBS16* pBS16, t_uint32* pdwReservedSectors)
{
	t_uint32		dwClusterCount;
	t_uint32		dwNumSectors;
	t_uint32		dwFatData;
	t_uint32		dwFatLength;
	t_int32			dwBytesPerSec;
	t_uint32		dwCreationTime;
	t_uint32		dwStartDataSector;
	t_uint32		dwRootDirSectors;	// sectors for root directory
	t_int32			dwAlign;
	FFatTime		stTime;				// for volume ID
	t_uint32		dwClustersPerFatSector;		// cluster entry count per a FAT sector
	t_uint32		dwTemp;				// temporary variable

	FFAT_ASSERT(pFI);
	FFAT_ASSERT(pFI->dwFatType == FFAT_FAT16);
	FFAT_ASSERT(pBSC);
	FFAT_ASSERT(pBS16);
	FFAT_ASSERT(pdwReservedSectors);

	dwNumSectors = pFI->dwSectorCount;

	if ((FAT_FAT16_VOLUME_SIZE_MAX >> pFI->dwSectorSizeBits) < dwNumSectors)
	{
		_FORMAT_PRINT_ERROR((_T("Too big volume size, the maximum volume size for FAT16 is 2GB")));
		return FFAT_EINVALID;
	}

	///////////////////////////////////////////////////////////////////////////////
	//
	//	How to get FAT size(sector count) ?
	//
	//	BytesPerSec	: bytes per a sector
	//	2			: a FAT entry size for a FAT16 cluster
	//
	//	Sector count for FAT and Data ==> dwNumSectors - pBSC->wRsvdSecCnt
	//	Bytes for FAT and Data = {Sector count for FAT and Data} * BytesPerSec
	//	Total byte usage for a Cluster = ({a cluster entry on FAT} * {FAT Count}) + {size of a cluster}
	//
	//	Cluster Count = {Bytes for FAT and Data} / {Total byte usage for a Cluster}
	//	FAT Sector Count for FAT16 = ({Cluster Count} * 2) / {BytesPerSec} with ceiling divide (rounding up)
	//
	//	BUT. Bytes for FAT and Data area is too big. (may be over 32bit integer)
	//	So. I optimize this equation as like belows.
	//
	//	The minimum sector size is 512. and a 512 byte FAT sector can contain 256 entries.
	//
	//	FAT Sector Count for FAT16 ==>
	//
	//					({Bytes for FAT and Data} / {Total byte usage for a Cluster}) * 2
	//			=	--------------------------------------------------------------
	//										BytesPerSec
	//
	//								{Bytes for FAT and Data} * 2
	//			=	----------------------------------------------------
	//					{Total byte usage for a Cluster} * BytesPerSec
	//
	//		. substitute "Bytes for FAT and Data = {Sector count for FAT and Data} * BytesPerSec"
	//
	//					{Sector count for FAT and Data} * BytesPerSec * 2
	//			=	--------------------------------------------------------
	//					{Total byte usage for a Cluster} * BytesPerSec
	//
	//					{Sector count for FAT and Data} * 2
	//			=	-----------------------------------------	(with rounding up)
	//					{Total byte usage for a Cluster}
	//
	///////////////////////////////////////////////////////////////////////////////

	dwFatData		= dwNumSectors - pBSC->wRsvdSecCnt;	// get total sector count for FAT & data area
	dwBytesPerSec	= pFI->dwSectorSize;				// get sector size in byte

	dwRootDirSectors = ESS_MATH_CDB((FFAT_ROOT_DIR_ENTRY_COUNT * FAT_DE_SIZE), 
								pFI->dwSectorSize, pFI->dwSectorSizeBits);

	// subtract root directory
	dwFatData		-= dwRootDirSectors;

	// sector size ==> pBSC->bSectorsPerClus * dwBytesPerSec
	// byte for a cluster ==> 2 * pBSC->bNumFATs
	dwFatLength		= ((pBSC->bSectorsPerClus * dwBytesPerSec) + (2 * pBSC->bNumFATs));
																	// get byte per a cluster storage
	dwFatLength		= ESS_MATH_CD((dwFatData * 2), dwFatLength);	// get cluster count

	pBSC->wFATSz16	= (t_uint16)(dwFatLength & 0xffff);
	dwFatData		-= (dwFatLength * pBSC->bNumFATs);
	dwFatData		-= (dwFatData % pBSC->bSectorsPerClus);
	dwClusterCount	= ESS_MATH_CD(dwFatData, pBSC->bSectorsPerClus);
	IF_UK (dwClusterCount < BS_FAT12_MAX_CLUSTER_COUNT)
	{
		_FORMAT_PRINT_ERROR((_T("too small cluster count!")));
		_FORMAT_PRINT_ERROR((_T(" = %d < %d"), dwClusterCount, BS_FAT12_MAX_CLUSTER_COUNT));
		return FFAT_EINVALID;
	}
	else IF_UK (dwClusterCount > BS_FAT16_MAX_CLUSTER_COUNT)
	{
		_FORMAT_PRINT_ERROR((_T("too many cluster count!")));
		_FORMAT_PRINT_ERROR((_T(" = %d > %d"), dwClusterCount, BS_FAT16_MAX_CLUSTER_COUNT));
		return FFAT_EINVALID;
	}

	// fat12/16 Information
	pBS16->bDrvNum		= 0x00;
	pBS16->bReserved1	= 0;
	pBS16->bBootSig		= FAT_SIG_EXT;

	FFAT_LOCALTIME(&stTime);
	dwCreationTime		= stTime.tm_mday + ((stTime.tm_mon + 1) << 5);
	if (stTime.tm_year >= 80)
	{
		dwCreationTime	+= ((stTime.tm_year + 1900 - 1980) << 9);
	}

	dwCreationTime		= dwCreationTime << 16;
	dwCreationTime		+= (stTime.tm_sec >> 1) + (stTime.tm_min << 5) + (stTime.tm_hour << 11);

	pBS16->dwVolID[0]	= (t_uint8)(dwCreationTime & 0x000000ff);
	pBS16->dwVolID[1]	= (t_uint8)((dwCreationTime & 0x0000ff00) >> 8);
	pBS16->dwVolID[2]	= (t_uint8)((dwCreationTime & 0x00ff0000) >> 16);
	pBS16->dwVolID[3]	= (t_uint8)((dwCreationTime & 0xff000000) >> 24);
	FFAT_MEMCPY(pBS16->sVolLab, "NO NAME    ", BS_VOLUME_LABEL_MAX_LENGTH);

	FFAT_MEMCPY(pBS16->sFileSysType, BS_SIGN_FAT16, BS_FILESYSTEM_TYPE_LENGTH);

	dwStartDataSector = pBSC->wRsvdSecCnt + pBSC->bNumFATs * dwFatLength + dwRootDirSectors;

	IF_UK (EssMath_IsPowerOfTwo((t_int16)pFI->wAlignBasis) == 0)
	{
		_FORMAT_PRINT_ERROR((_T("Invalid logical device info, wAlignBasis")));
		_FORMAT_PRINT_ERROR((_T("It should be power of two")));
		return FFAT_EINVALID;
	}

	dwAlign = pFI->dwStartSector + dwStartDataSector;
	dwAlign = dwAlign & (pFI->wAlignBasis - 1);

	if (dwAlign != 0)
	{
		FFAT_PRINT_VERBOSE((_T("realign, diff : %d"), dwAlign));
		*pdwReservedSectors = *pdwReservedSectors + (pFI->wAlignBasis - dwAlign);
		return dwAlign;
	}

	// check is there enough space for cluster 0 and 0
	dwClustersPerFatSector	= pFI->dwSectorSize / 2;		// get clusters per a FAT Sector

	// check cluster count and cluster size
	dwTemp = (t_uint32)ESS_MATH_CD((dwClusterCount + 2), dwClustersPerFatSector);
	if (dwFatLength != dwTemp)
	{
		// incorrect FAT length
		FFAT_PRINT_VERBOSE((_T("realign for FAT size")));
		*pdwReservedSectors = *pdwReservedSectors + 1;
		return 1;
	}

	return FFAT_OK;
}


/**
 * adjust byte order
 *
 * @param		pFI				: format information
 * @param		pBootSector		: Boot Sector Common part pointer.
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static FFatErr	
_changeEndian(FFatFormatInfo* pFI, t_int8* pBootSector)
{
	FatBSCommon*	pBSC;
	FatBS32*		pBS32;

	pBSC	= (FatBSCommon*)pBootSector;
	pBS32	= (FatBS32*)(pBootSector + BS_FAT32_OFFSET);
		
	pBSC->wRsvdSecCnt	= FFAT_BO_UINT16(pBSC->wRsvdSecCnt);
	pBSC->wFATSz16		= FFAT_BO_UINT16(pBSC->wFATSz16);
	pBSC->wSecPerTrack	= FFAT_BO_UINT16(pBSC->wSecPerTrack);
	pBSC->wNumHeads		= FFAT_BO_UINT16(pBSC->wNumHeads);
	pBSC->dwHiddSec		= FFAT_BO_UINT32(pBSC->dwHiddSec);
	pBSC->dwTotSec32	= FFAT_BO_UINT32(pBSC->dwTotSec32);

	if (pFI->dwFatType == FFAT_FAT32)
	{
		pBS32->dwFATSz32	= FFAT_BO_UINT32(pBS32->dwFATSz32);
		pBS32->wExtFlags	= FFAT_BO_UINT16(pBS32->wExtFlags);
		pBS32->wFSVer		= FFAT_BO_UINT16(pBS32->wFSVer);
		pBS32->dwRootClus	= FFAT_BO_UINT32(pBS32->dwRootClus);
		pBS32->wFSInfo		= FFAT_BO_UINT16(pBS32->wFSInfo);
		pBS32->wBkBootSec	= FFAT_BO_UINT16(pBS32->wBkBootSec);
	}

	return FFAT_OK;
}

/**
 * erase entire volume
 *
 * @param		pFI		: format information
 * @param		pCI		: cache information
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_eraseAll(FFatFormatInfo* pFI)
{
	FFatErr		r;

	r = ffat_ldev_eraseSector(pFI->pDevice, 0, pFI->dwSectorCount);
	if (r != (t_int32)pFI->dwSectorCount)
	{
		if (r == FFAT_ENOSUPPORT)
		{
			return FFAT_ENOSUPPORT;
		}
		return FFAT_EIO;
	}

	return FFAT_OK;
}


/**
 * Initialize FAT area
 *
 * @param		pFI			: format information
 * @param		pVol		: volume information
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing
 * @version		AUG-08-2006 [DongYoung Seo] bug fix. 
 *							add code for FAT size when it is smaller than align base
 * @version		SEP-09-2008 [DongYoung Seo] add FAT size checking routine
 * @version		JAN-18-2010 [JW Park] change malloc from pstack to dynamic.
 */
static FFatErr
_initFat(FFatFormatInfo* pFI, Vol* pVol, ComCxt* pCxt)
{
	t_int8*			pBuff = NULL;
	t_int32			dwSize;
	FatVolInfo*		pVolInfo;
	t_uint32		i, j;
	t_uint32		dwFirstSector;		// first FAT sector
	t_uint32		dwLastSector;		// last FAT sector
	t_uint32		dwWriteSectorCount;	// sector count to write
	FFatErr			r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pFI);
	FFAT_ASSERT(pVol);

	pVolInfo	= VOL_VI(pVol);

	if (pFI->pBuff == NULL)
	{
		// FIRST try. with optimal IO size
		dwSize = ESS_GET_MAX(pFI->dwSectorSize, FFAT_FORMAT_BUFF_SIZE);

		//pBuff	= FFAT_LOCAL_ALLOC(dwSize , pCxt);
		pBuff = (t_int8*)FFAT_MALLOC(dwSize, ESS_MALLOC_IO);
		if (pBuff == NULL)
		{
			dwSize = pFI->dwSectorSize;
			//pBuff	= FFAT_LOCAL_ALLOC(dwSize , pCxt);
			pBuff = (t_int8*)FFAT_MALLOC(dwSize, ESS_MALLOC_IO);
			FFAT_ASSERT(pBuff != NULL);
		}
	}
	else
	{
		pBuff	= pFI->pBuff;
		dwSize	= pFI->dwBuffSize;

		dwSize	-= (t_uint32)FFAT_GET_ALIGN_OFFSET(pBuff);
		pBuff	= (t_int8*)FFAT_GET_ALIGNED_ADDR(pBuff);
	}

	FFAT_ASSERT(pVolInfo->dwFirstFatSector != 0);

	dwFirstSector	= pVolInfo->dwFirstFatSector;
	dwLastSector	= dwFirstSector + VI_FSC(pVolInfo) - 1;

	dwWriteSectorCount	= dwSize >> pFI->dwSectorSizeBits;

	// check size of FAT area.
	//	the maximum size of FAT 
	//	refer to : http://support.microsoft.com/default.aspx?scid=KB;EN-US;Q314463&
	if ((VI_FSC(pVolInfo) * VI_FC(pVolInfo)) > (t_uint32)(FAT_FAT_MAX_SIZE >> pFI->dwSectorSizeBits))
	{
		_FORMAT_PRINT_ERROR((_T("Too big FAT size - invalid cluster size")));
		return FFAT_EINVALID;
	}

	FFAT_DEBUG_PRINTF((_T("write sector count for FAT INIT : %d"), dwWriteSectorCount));

	// adjust write sector count for align
	if (dwWriteSectorCount & pFI->wAlignBasis)
	{
		dwWriteSectorCount = dwWriteSectorCount & ~(pFI->wAlignBasis - 1);
		FFAT_DEBUG_PRINTF((_T("new aligned write sector count : %d"), dwWriteSectorCount));
	}

	// prepare memory for initialization
	FFAT_MEMSET(pBuff, 0x00, (dwWriteSectorCount << pFI->dwSectorSizeBits));

	// MARK
	_formatMarkFat(pBuff, 0, 0x0FFFFFF8, pVolInfo->dwFatType);
	_formatMarkFat(pBuff, 1, 0x0FFFFFFF, pVolInfo->dwFatType);

	if (pVolInfo->dwFatType == FFAT_FAT32)
	{
		_formatMarkFat(pBuff, 2, FAT32_END, pVolInfo->dwFatType);
	}

	// adjust write size
	if ((dwFirstSector + dwWriteSectorCount) > dwLastSector)
	{
		dwWriteSectorCount = dwLastSector - dwFirstSector + 1;
	}

	// check write align
	if (((dwFirstSector + pFI->dwStartSector) & (pFI->wAlignBasis - 1))
		&& (dwWriteSectorCount >= pFI->wAlignBasis))
	{
		// use i to the first write sector count
		i = dwWriteSectorCount - (dwWriteSectorCount & (pFI->wAlignBasis - 1));
	}
	else
	{
		// use i to the first write sector count
		i = dwWriteSectorCount;
	}

	FFAT_ASSERT(i > 0);

	for (j = 0; j < VI_FC(pVolInfo); j++)
	{
		// init first FAT sector
		r = ffat_ldev_writeSector(pFI->pDevice, 
							(dwFirstSector + (VI_FSC(pVolInfo) * j)), pBuff, i);
		IF_UK (r != (t_int32)i)
		{
			_FORMAT_PRINT_ERROR((_T("Fail to write sector")));
			r = FFAT_EIO;
			goto out;
		}
	}

	// MARK to 0x00 at pBuff, 
	// 2번째 write 부터는 0x00로 채워진 sector를 write 한다.
	FFAT_MEMSET(pBuff, 0x00, 12);

	i += dwFirstSector;
	while (i <= dwLastSector)
	{
		// check write sector size
		if ((dwLastSector - i) < dwWriteSectorCount)
		{
			dwWriteSectorCount = dwLastSector - i + 1;
			if (dwWriteSectorCount == 0)
			{
				break;
			}
		}

		FFAT_ASSERT(i >= dwFirstSector);
		FFAT_ASSERT((i + dwWriteSectorCount - 1) <= dwLastSector);

		for (j = 0; j < VI_FC(pVolInfo); j++)
		{
			r = ffat_ldev_writeSector(pFI->pDevice, i + (VI_FSC(pVolInfo) * j),
							pBuff, dwWriteSectorCount);
			IF_UK (r != (t_int32)dwWriteSectorCount)
			{
				_FORMAT_PRINT_ERROR((_T("Fail to write sector")));
				r = FFAT_EIO;
				goto out;
			}
		}
		i += dwWriteSectorCount;
	}

	r = FFAT_OK;

out:
	//FFAT_LOCAL_FREE(pBuff, dwSize, pCxt);
	FFAT_FREE(pBuff, dwSize);

	return r;
}


/**
 *  mark a FAT entry with dwValue
 *
 * @param		pSector		: sector buffer
 * @param		dwCluster	: cluster number
 * @param		dwValue		: mark value
 * @param		dwFatType	: FAT type
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 */
static void
_formatMarkFat(t_int8* pSector, t_uint32 dwCluster,
						t_uint32 dwValue, FFatFatType dwFatType)
{
	t_uint32 dwIdx;

	if (dwFatType == FFAT_FAT16)
	{
		dwValue &= 0x0000FFFF;
		dwIdx = dwCluster << 1;

		pSector[dwIdx]		= (t_int8)(dwValue & 0x00FF);
		pSector[dwIdx + 1]	= (t_int8)((dwValue & 0xFF00) >> 8);
	}
	else if (dwFatType == FFAT_FAT32)
	{
		if (dwCluster != 1)
		{
			dwValue &= 0x0FFFFFFF;
		}
		dwIdx = dwCluster << 2;

		pSector[dwIdx]		= (t_int8)(dwValue & 0x000000FF);
		pSector[dwIdx + 1]	= (t_int8)((dwValue & 0x0000FF00) >> 8);
		pSector[dwIdx + 2]	= (t_int8)((dwValue & 0x00FF0000) >> 16);
		pSector[dwIdx + 3]	= (t_int8)((dwValue & 0xFF000000) >> 24);
	}
	else
	{
		dwValue &= 0x0FFF;
		dwIdx = dwCluster + (dwCluster >> 1);
		if ((dwIdx & 0x01) == 0)
		{
			// even
			pSector[dwIdx]		= (t_int8)(dwValue & 0x00FF);
			pSector[dwIdx + 1]	= (t_int8)(((pSector[dwIdx + 1] & 0xF0)
												| ((dwValue & 0x0F00) >> 8)));
		}
		else
		{
			pSector[dwIdx]		= (t_int8)((pSector[dwIdx] & 0x0F) | ((dwValue & 0x000F) << 4));
			pSector[dwIdx + 1]	= (t_int8)((dwValue & 0x0Ff0) >> 4);
		}
	}
}


/**
 * Initialize root directory
 *
 * @param		pFI		: format information
 * @param		pVol	: volume information
 * @param		pCxt	: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-24-2006 [DongYoung Seo] First Writing.
 * @history		FEB-21-2007 [DongYoung Seo] check first write sector count with aligned count.
 * @history		NOV-12-2008 [DongYoung Seo] bug fix - write volume name on root directory
 * @history		JAN-18-2010 [JW Park] change malloc from pstack to dynamic.
 */
static FFatErr
_InitRootDirectory(FFatFormatInfo* pFI, Vol* pVol, ComCxt* pCxt)
{
	t_int8*			pBuff = NULL;
	t_int32			dwSize;
	FatVolInfo*		pVolInfo;
	t_uint32		i;
	t_uint32		dwFirstSector;		// first FAT sector
	t_uint32		dwLastSector;		// last FAT sector
	t_uint32		dwWriteSectorCount;	// sector count to write
	FFatErr			r;

	FFAT_STACK_VAR;
	FFAT_STACK_END();

	FFAT_ASSERT(pFI);
	FFAT_ASSERT(pVol);

	pVolInfo	= VOL_VI(pVol);

	VI_SET_CXT(pVolInfo, pCxt);

	if (pFI->pBuff == NULL)
	{
		// FIRST try. with optimal IO size
		dwSize = ESS_GET_MAX(pFI->dwSectorSize, FFAT_FORMAT_BUFF_SIZE);

		//pBuff	= FFAT_LOCAL_ALLOC(dwSize , pCxt);
		pBuff = (t_int8*)FFAT_MALLOC(dwSize, ESS_MALLOC_IO);

		if (pBuff == NULL)
		{
			dwSize = pFI->dwSectorSize;
			//pBuff	= FFAT_LOCAL_ALLOC(dwSize , pCxt);
			pBuff = (t_int8*)FFAT_MALLOC(dwSize, ESS_MALLOC_IO);
			FFAT_ASSERT(pBuff != NULL);
		}
	}
	else
	{
		pBuff	= pFI->pBuff;
		dwSize	= pFI->dwBuffSize;

		dwSize	-= (t_uint32)FFAT_GET_ALIGN_OFFSET(pBuff);
		pBuff	= (t_int8*)FFAT_GET_ALIGNED_ADDR(pBuff);
	}

	// init root directory area
	FFAT_ASSERT(pVolInfo->dwFirstRootSector != 0);

	dwFirstSector	= pVolInfo->dwFirstRootSector;

	if (FFATFS_IS_FAT32(pVolInfo) == FFAT_TRUE)
	{
		dwLastSector	= dwFirstSector + VI_SPC(pVolInfo) - 1;
	}
	else
	{
		dwLastSector	= dwFirstSector + VI_RSC(pVolInfo) - 1;
	}

	dwWriteSectorCount	= dwSize >> pFI->dwSectorSizeBits;

	FFAT_DEBUG_PRINTF((_T("write sector count for FAT INIT : %d"), dwWriteSectorCount));

	// adjust write sector count for align
	if (dwWriteSectorCount & pFI->wAlignBasis)
	{
		dwWriteSectorCount = dwWriteSectorCount & ~(pFI->wAlignBasis - 1);
		FFAT_DEBUG_PRINTF((_T("new aligned write sector count : %d"), dwWriteSectorCount));
	}

	// prepare memory for initialization
	FFAT_MEMSET(pBuff, 0x00, (dwWriteSectorCount << pFI->dwSectorSizeBits));

	// adjust write size
	if ((dwFirstSector + dwWriteSectorCount) > dwLastSector)
	{
		dwWriteSectorCount = dwLastSector - dwFirstSector + 1;
	}

	// check write align
	if ((dwFirstSector + pFI->dwStartSector) & pFI->wAlignBasis)
	{
		// use i to the first write sector count
		i = dwWriteSectorCount - (dwWriteSectorCount & pFI->wAlignBasis);
	}
	else
	{
		// use i to the first write sector count
		i = dwWriteSectorCount;
	}

	if (i != 0)
	{
		FFAT_ASSERT(i > 0);
		// init first root directory area
		r = ffat_ldev_writeSector(pFI->pDevice, dwFirstSector, pBuff, i);
		IF_UK (r != (t_int32)i)
		{
			_FORMAT_PRINT_ERROR((_T("Fail to write sector")));
			r = FFAT_EIO;
			goto out;
		}
	}

	i = (dwFirstSector + i);
	while (i <= dwLastSector)
	{
		// check write sector size
		if (((dwLastSector - i) + 1) < dwWriteSectorCount)
		{
			dwWriteSectorCount = (dwLastSector - i) + 1;
			if (dwWriteSectorCount == 0)
			{
				break;
			}
		}

		FFAT_ASSERT(i >= dwFirstSector);
		FFAT_ASSERT((i + dwWriteSectorCount - 1) <= dwLastSector);

		r = ffat_ldev_writeSector(pFI->pDevice, i, pBuff, dwWriteSectorCount);
		IF_UK (r != (t_int32)dwWriteSectorCount)
		{
			_FORMAT_PRINT_ERROR((_T("Fail to write sector")));
			r = FFAT_EIO;
			goto out;
		}
		i += dwWriteSectorCount;
	}

	// root directory area initialized
	// ============================================================
	// now we write a volume label entry on the root directory
	if (pFI->psVolumeLabel != NULL)
	{
		// check volume name is valid
		FFAT_ASSERT(ffat_fs_de_isValidVolumeLabel((t_wchar*)pFI->psVolumeLabel) == FFAT_TRUE);

		r = ffat_fs_de_setVolLabel(pVolInfo, (t_wchar*)pFI->psVolumeLabel, FFAT_TRUE);
		FFAT_EO(r, (_T("fail to set volume name")));
	}

	r = FFAT_OK;

out:
	//FFAT_LOCAL_FREE(pBuff, dwSize, pCxt);
	FFAT_FREE(pBuff, dwSize);

	return r;
}


/**
 * Make a boot sector FAT32 part
 *
 * @param		pFI				: [IN] format information
 * @param		pBootSector		: [IN] Boot Sector storage pointer
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		DongYoung Seo
 * @version		JUL-23-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_buildFAT32FSInfo(FFatFormatInfo* pFI, t_int8* pBootSector)
{
	Fat32FSInfo*		pFSI;
	FFAT_ASSERT(pBootSector);

	FFAT_MEMSET(pBootSector, 0x00, pFI->dwSectorSize);

	pFSI	= (Fat32FSInfo*)pBootSector;

	pFSI->dwFSI_LeadSig		= FFAT_BO_UINT32(BS_FSI_LEAD_SIG);
	pFSI->dwFSI_StrucSig	= FFAT_BO_UINT32(BS_FSI_STRUC_SIG);
	pFSI->dwFSI_Free_Count	= FFAT_BO_UINT32(0xFFFFFFFF);
	pFSI->dwFSI_Nxt_Free	= FFAT_BO_UINT32(0x00000002);
	pFSI->dwFSI_TrailSig	= FFAT_BO_UINT32(BS_FSI_TRAIN_SIG);

	return FFAT_OK;
}

