/*
 * RFS 3.0 Developed by Flash Software Group.
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
 * @file		linux_logdisk.c
 * @brief		This file includes APIs to NativeFS for accessing logical disk.
 * @version		RFS_3.0.0_b047_RTM
 * @see			none
 * @author		hayeong.kim@samsung.com
 */

#include <linux/genhd.h>
#include <linux/blkdev.h>
#include "linux_logdisk.h"

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_DEV)

#ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT
// FSR is enabled
#ifdef CONFIG_RFS_FS_ERASE_SECTOR
	extern int (*sec_stl_delete) (dev_t dev, u32 start, u32 nums, u32 b_size);

	#define StlSectorErase			sec_stl_delete
// FSR is disabled
#else
	static inline int StlSectorErase(dev_t dev, u32 start, u32 nums, u32 b_size)
	{
		return -EOPNOTSUPP;
	}
#endif

#define ERASABLE_BLK_DEVICE		138	/* stl blk device */
#define IS_ERASABLE_DEV(x)		((MAJOR(x) == ERASABLE_BLK_DEVICE)? 1 : 0)
#define IS_ERASABLE_KERNEL()	((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))? 1 : 0)

#ifdef CONFIG_RFS_FS_ERASE_SECTOR
	#define IS_ERASABLE(x)			(IS_ERASABLE_DEV(x) || IS_ERASABLE_KERNEL())
#else
	#define IS_ERASABLE(x)			(!IS_ERASABLE_DEV(x) && IS_ERASABLE_KERNEL())
#endif
#endif /* #ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT */

#define CMD_META_CLEANUP		0x8A1C	// discard STL mapping


/*
 * TODO 2008.08.18
 * 1. device authorization interface
 * 2. hot cold data management -> in bcache? 
 */

/******************************************************************************/
/* NESTLE PUBLIC LDEV FUNCTIONS                                               */
/******************************************************************************/
/**
 * @brief		IO Control for Logical Disk
 * @param[in]	pLogDisk		pointer to a logical disk.
 * @param[in]	dwControCode	Control code for device I/O control
 * @param[in]	pInBuf			pointer to a Input buffer 
 * @param[in]	dwInBufSize		Input Buffer size
 * @param[out]	pOutBuf			pointer to a Output buffer 
 * @param[in]	dwOutBufSize	Output Buffer size
 * @returns		FERROR_INSUFFICIENT_BUFFER
 *				FERROR_NO_ERROR.
 * @remarks 
 *          - get a device infomation indicated by dwControlCode
 *          - dwControlCode should be same on several OS
 * 
 */
FERROR
LdevIoControl(
		IN PLOGICAL_DISK 	pLogDisk,
		IN unsigned int		dwControlCode,
		IN void*			pInBuf,
		IN unsigned int		dwInBufSize,
		OUT void*			pOutBuf,
		IN unsigned int		dwOutBufSize)
{
	FERROR		nErr = FERROR_NOT_SUPPORTED;

	/* commands that glue cannot deal with */
	if (TARGET_NESTLE != IOCTL_TARGET(dwControlCode))
	{
		LNX_DMZ(("This command (0x%x) is not available", dwControlCode));
		return nErr;
	}

	switch (dwControlCode)
	{
	/* get info of logical disk */
	case IOCTL_LDEV_GET_INFO:
		{
			PFORMAT_DISK_INFO		pDI = (PFORMAT_DISK_INFO) pOutBuf;

			if (pOutBuf == NULL)
			{
				LNX_EMZ(("Buffer is NULL"));
				nErr = FERROR_INVALID;
				NSD_ASSERT(0);
				break;
			}

			if (dwOutBufSize < sizeof(FORMAT_DISK_INFO))
			{
				LNX_EMZ(("Buffer size is smaller than FORMAT_DISK_INFO size."));
				nErr = FERROR_INVALID;
				NSD_ASSERT(0);
				break;
			}

			/* return info of logical disk */
			LdevGetDiskInfo(pLogDisk, 
					&pDI->dwNumSectors,
					&pDI->dwBytesPerSector,
					&pDI->dwStartSectorNum,
					&pDI->dwFlags);
			nErr = FERROR_NO_ERROR;

			break;
		}
	/* read/write sectors from/to logical disk */
	case IOCTL_LDEV_READ_SECTOR:
	case IOCTL_LDEV_WRITE_SECTOR:
		{
#ifndef CONFIG_RFS_FS_BTFS_HPA
			/*
			 * Invalidate to access the logical device directly (normal case)
			 */
			LNX_EMZ(("LdevReadWriteSectors is not supported for Linux"));
			nErr = FERROR_NOT_SUPPORTED;

			NSD_ASSERT(0);
#else
			/* TODO for HPA */
#endif
			break;
		}
	/* erase sectors of logical disk */
	case IOCTL_LDEV_ERASE_SECTOR:
		{
			PDISK_SECTOR_RANGE pSectorRange = (PDISK_SECTOR_RANGE) pInBuf;

			if (pInBuf == NULL)
			{
				LNX_EMZ(("Buffer is null"));
				nErr = FERROR_INVALID;
				NSD_ASSERT(0);
				break;
			}
			if (dwInBufSize < sizeof(DISK_SECTOR_RANGE))
			{
				LNX_EMZ(("Buffer size is smaller than DISK_SECTOR_RANGE size."));
				nErr = FERROR_INVALID;
				NSD_ASSERT(0);
				break;

			}

			/* erase sector mapping of specific FTL */
			nErr = LdevEraseSectors(pLogDisk, pSectorRange);
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_DMZ(("Erase Sectors fails(nErr: 0x%08x)", -nErr));
			}

			break;
		}
	default:
		{
			LNX_DMZ(("invalid ioctl command(0x%08x)", dwControlCode));
			break;
		}
	}

	return nErr;
}


/**
 * @brief		get disk informaion in VFS logical disk
 * @param[in]	pLogDisk			pointer to a logical disk.
 * @param[out]	pdwNumSectors		total number of sectors
 * @param[out]	pdwBytesPerSector	bytes per sector
 * @param[out]	pdwStartSectorNum	start sector number (ce is always 0)
 * @param[out]	pdwFlags			logical disk disk info flags (for ce)
 * @returns 
 *			- void
 * @remarks 
 * @see 
 */
void
LdevGetDiskInfo(
		IN PLOGICAL_DISK 	pLogDisk,
		OUT unsigned int*	pdwNumSectors,
		OUT unsigned int*	pdwBytesPerSector,
		OUT unsigned int*	pdwStartSectorNum,
		OUT DISK_FLAGS*		pdwFlags)
{
	PLINUX_BLOCK_DEVICE	pLxBlockDev = NULL;

	NSD_ASSERT(pLogDisk);

	pLxBlockDev = &(pLogDisk->stLxBDev);

	/*
	 * get the number of sectors
	 */
	if (pdwNumSectors)
	{
		/*
		 * number of blocks in the volume
		 *  : block means the I/O block (not 512B)
		 */
		*pdwNumSectors = (pLxBlockDev->bd_inode->i_size >> 
				pLxBlockDev->bd_inode->i_blkbits);

		LNX_VMZ(("NumSectors(%u) from bd_inode", *pdwNumSectors));
	}

	/*
	 * get the size of a sector
	 */
	if (pdwBytesPerSector)
	{
		/* size of I/O block (not hardsector size) */
		*pdwBytesPerSector = pLxBlockDev->bd_block_size;

		LNX_VMZ(("bytes per sector(%u bytes)", *pdwBytesPerSector));
	}

	/*
	 * get start sector
	 */
	if (pdwStartSectorNum)
	{
		*pdwStartSectorNum = 0;
		LNX_VMZ(("StartSector(0)"));
	}

	/*
	 * get flags for logical device
	 */
	if (pdwFlags)
	{
		DISK_FLAGS	dwTmpFlags = 0;

#ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT
		if (IS_ERASABLE(pLxBlockDev->bd_dev))
		{
#endif
			dwTmpFlags |= DISK_SUPPORT_ERASE_SECTORS;
			LNX_VMZ(("Set Erasable"));
#ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT
		}
#endif
		/*
		 * TODO
		 *  1. How to distinguish the type of device from each other ? 
         *    It's difficult in current device driver
		 *  2. apply other flags
         */

		*pdwFlags = dwTmpFlags;
	}

	return;
}

/**
 * @brief		erases sectors.
 * @param[in]	pLogDisk		Logical disk pointer
 * @param[in]	pSectorRange	Sector range parameter
 * @returns		FERROR
 * @remarks		only for xsr support
 */
FERROR
LdevEraseSectors(
	IN	PLOGICAL_DISK		pLogDisk,
	IN	PDISK_SECTOR_RANGE	pSectorRange)
{
	LINUX_ERROR			dwLinuxError = -EOPNOTSUPP;
	PLINUX_BLOCK_DEVICE	pLxBlockDev = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        struct super_block *sb;
#endif

	LNX_ASSERT_ARG(pLogDisk, FERROR_INVALID);
	LNX_ASSERT_ARG(pSectorRange, FERROR_INVALID);

	pLxBlockDev = &(pLogDisk->stLxBDev);

#ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT
	/* disable to erase sectors */
	if (0 == IS_ERASABLE(pLxBlockDev->bd_dev))
		return FERROR_NO_ERROR;
#endif

	LNX_IMZ(("[in] discard blocks @%s (%u, %u)",
				pLxBlockDev->bd_super->s_id,
				pSectorRange->dwSectorNum, pSectorRange->dwNumSectors));

#ifdef _RFS_INTERNAL_OLD_CODE_SUPPORT
	// if STL device
	if ((dwLinuxError != 0) && IS_ERASABLE_DEV(pLxBlockDev->bd_dev))
	{
		/* call STL operation */
		dwLinuxError = StlSectorErase(pLxBlockDev->bd_dev,
				pSectorRange->dwSectorNum,
				pSectorRange->dwNumSectors,
				pLxBlockDev->bd_block_size);
	}
#else

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)

	/*
	 * 01.02.2013_s.illindra@samsung.com - fix the bug for FSCS issue QA2013010005
	 * Dropped taking sb->s_umount lock(acquired by get_super() previously) while calling
	 * blkdev_issue_discard() as it was causing the deadlock and accessing the sb
	 * through pLxBlockDev->bd_super.
	 */

	sb = pLxBlockDev->bd_super;
	LNX_IMZ(("[in] sb_issue_discard (%d)", blk_queue_discard(bdev_get_queue(pLxBlockDev->bd_super->s_bdev))));

	// if other device with discard() cmd
	{
		LINUX_SECTOR nBlock, nNumBlocks;

		// change sector unit based on s_blocksize to block unit based on 512B Hardsector
		nBlock = pSectorRange->dwSectorNum << (sb->s_blocksize_bits - 9);
		nNumBlocks = pSectorRange->dwNumSectors << (sb->s_blocksize_bits - 9);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
		dwLinuxError = blkdev_issue_discard(sb->s_bdev, nBlock, nNumBlocks,
			LINUX_GFP_NOFS, 0);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		dwLinuxError = blkdev_issue_discard(sb->s_bdev, nBlock, nNumBlocks,
			LINUX_GFP_NOFS,
			BLKDEV_IFL_WAIT | BLKDEV_IFL_BARRIER);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
		dwLinuxError = blkdev_issue_discard(sb->s_bdev, nBlock, nNumBlocks,
			LINUX_GFP_NOFS,
			DISCARD_FL_WAIT | DISCARD_FL_BARRIER);

#else
		dwLinuxError = blkdev_issue_discard(sb->s_bdev, nBlock, nNumBlocks,
			LINUX_GFP_NOFS);
#endif
	
	}
#else

	LNX_IMZ(("[in] ioctl_by_bdev"));
	dwLinuxError = ioctl_by_bdev(pLxBlockDev,
			(unsigned) CMD_META_CLEANUP, (unsigned long) pSectorRange);

#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28) */

#endif /* _RFS_INTERNAL_OLD_CODE_SUPPORT */

	// if not supported
	if (dwLinuxError == -EOPNOTSUPP)
	{

		LNX_IMZ(("[out] No API for discard blocks @%s",
					pLxBlockDev->bd_super->s_id));
		return FERROR_NOT_SUPPORTED;
	}
	else
	{
		LNX_IMZ(("[out] discard blocks @%s (%u, %u) ret %d",
					pLxBlockDev->bd_super->s_id,
					pSectorRange->dwSectorNum, pSectorRange->dwNumSectors,
					dwLinuxError));
		return (dwLinuxError? FERROR_IO_ERROR : FERROR_NO_ERROR);
	}
}

/**
 * @brief		Read / Write sectors
 * @param[in]	pLogDisk		pointer to a logical disk.
 * @param[in]	dwStartSector	start sector number for read / write
 * @param[in]	dwSectorCnt		sector count for read / write
 * @param[in]	pBuffer			pointer to user buffer
 * @param[in]	dwFlag			flag for read / write 
 * @returns		void
 * @remarks		Can not support for LINUX
 * 
 */
FERROR
LdevReadWriteSectors(
	IN PLOGICAL_DISK		pLogDisk,
	IN unsigned int			dwStartSector,
	IN unsigned int			dwSectorCnt,
	IN OUT char*			pBuffer,
	IN DISK_OPERATE_TYPE	dwFlag)
{
	LNX_EMZ(("LdevReadWriteSectors is not supported for Linux"));
	
	NSD_ASSERT(0);

	return FERROR_NOT_SUPPORTED;
}

/*
 * Define Symbols
 */

#include <linux/module.h>

EXPORT_SYMBOL(LdevIoControl);
EXPORT_SYMBOL(LdevGetDiskInfo);
EXPORT_SYMBOL(LdevReadWriteSectors);
EXPORT_SYMBOL(LdevEraseSectors);

// end of file
