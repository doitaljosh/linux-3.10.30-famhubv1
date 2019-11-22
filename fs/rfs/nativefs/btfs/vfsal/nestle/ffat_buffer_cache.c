/*
* TFS4 2.0 FFAT(Final FAT) filesystem Developed by Flash Planning Group.
*
* Copyright 2006-2007 by Memory Division, Samsung Electronics, Inc.,
* San #16, Banwol-Ri, Taean-Eup, Hwasung-City, Gyeonggi-Do, Korea
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
* @file		ffat_buffer_cache.c
* @brief		This file implements functions to interface with TFS4 FAL buffer cache
* @author		DongYoung Seo(dy76.seo@samsung.com)
* @version		JUL-21-2006 [DongYoung Seo] First writing
* @see			None
*/


// for Nestle
#include "ns_nativefs.h"

#include "ess_types.h"

#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_api.h"

#include "ffat_nestle.h"

#include "ffat_al.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_VFSAL_BC)

//=============================================================================
//
// Interface with TFS4 BCM,
// pUser of FFatCacheInfo is logical device id.
//

#define _NESTLE_VECTOR_IO

// get logical device id
#define _VCB(_pCI)				(NS_PVCB)((_pCI)->pDev)
#define	_VOL(_pVcb)				(FFatVol*)(NS_GetNativeVcb(_pVcb))

// get sector size
//#define _SS(_pCI)		NS_GetBlockSize(_VCB(_pCI))
// get sector size bit count
//#define _SSB(_pCI)		NS_GetBlockSizeBits(_VCB(_pCI))

// types
typedef struct
{
	FFatErr		dwFFat;
	t_int32		dwNestle;
} ErrorNoConvert;

static const ErrorNoConvert _pErrnoTbl[] = 
{
	{FFAT_OK,			FERROR_NO_ERROR},
	{FFAT_ENOMEM,		FERROR_INSUFFICIENT_MEMORY},
	{FFAT_EIO,			FERROR_IO_ERROR},
	{FFAT_EINVALID,		FERROR_INVALID},
	{FFAT_EXDEV,		FERROR_MEDIA_EJECTED}
};

static const FlagConvert pCacheFlags[] =
{
	{NS_BCM_OP_SYNC,		FFAT_CACHE_SYNC},	// not support
	{NS_BCM_OP_DIRECT,		(FFAT_CACHE_DIRECT_IO & (~FFAT_CACHE_SYNC))},
	{NS_BCM_OP_META,		FFAT_CACHE_DATA_META},
	{NS_BCM_OP_HOT,			FFAT_CACHE_DATA_HOT}
};

static FFatErr	_errnoToFFAT(FERROR dwErrno);
static t_uint32	_convertFlagToNestle(t_uint32 dwFFatFlag, 
									 const FlagConvert* pConvertDB, t_int32 dwCount);

// for statistic
#define _STATISTICS_READ(_dwS)
#define _STATISTICS_WRITE(_dwS)
#define _STATISTICS_ERASE(_dwS)
#define _STATISTIC_INIT
#define _STATISTIC_PRINT

// debug begin
#ifdef FFAT_DEBUG
	typedef struct
	{
		t_uint32		dwRead;				// read request count
		t_uint32		dwTotalReadSector;	// read sector count
		t_uint32		dwWrite;			// write request count
		t_uint32		dwTotalWriteSector;	// write sector count
		t_uint32		dwErase;			// erase request count
		t_uint32		dwTotalEraseSector;	// erase sector count
	} _debugStatisc;

	#define _STATISTIC()		((_debugStatisc*)&_stStatics)

	static t_int32 _stStatics[sizeof(_debugStatisc) / sizeof(t_int32)];

	#undef _STATISTICS_READ
	#undef _STATISTICS_WRITE
	#undef _STATISTICS_ERASE
	#undef _STATISTIC_INIT
	#undef _STATISTIC_PRINT

	#define _STATISTICS_READ(_dwS)		_STATISTIC()->dwRead++; _STATISTIC()->dwTotalReadSector += _dwS;
	#define _STATISTICS_WRITE(_dwS)		_STATISTIC()->dwWrite++; _STATISTIC()->dwTotalWriteSector += _dwS;
	#define _STATISTICS_ERASE(_dwS)		_STATISTIC()->dwErase++; _STATISTIC()->dwTotalEraseSector += _dwS;

	#define _STATISTIC_INIT				FFAT_MEMSET(&_stStatics, 0x00, sizeof(_stStatics));
	#define _STATISTIC_PRINT			_printStatistics();

	static void			_printStatistics(void);
#endif
// debug end

#define FFAT_MAX_BLOCK_SIZE		4096

#ifdef FFAT_BLOCK_IO
	t_int8	_gszTempBuffer[FFAT_MAX_BLOCK_SIZE];
#endif

/**
* This function initialize Block I/O module
*
* @return		FFAT_OK		: success
* @return		negative	: error number
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_initBlockIO(void)
{
	_STATISTIC_INIT

	return FFAT_OK;
}


/**
* This function terminate Block I/O module
*
* @return		FFAT_OK		: success
* @return		negative	: error number
* @author		DongYoung Seo
* @version		JAN-10-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_termianteBlockIO(void)
{
	_STATISTIC_PRINT

	return FFAT_OK;
}


/**
* This function read sectors from buffer cache before volume is mounted
*
* @param		dwSector	: sector number
* @param		dwCount		: sector count
* @param		pBuff		: buffer pointer
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information.
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		OCT-12-2007 [DongYoung Seo] First Writing.
*/
t_int32
ffat_al_readSector2(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount, 
					FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FERROR				err = FERROR_NO_ERROR;
	unsigned int		dwNSFlag;
	NS_PVNODE			pVnode;

	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_DATA_META) : FFAT_TRUE);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_SYNC) : FFAT_TRUE);
	FFAT_ASSERT(dwCount == 1);

	_STATISTICS_READ(dwCount)

	dwNSFlag	= _convertFlagToNestle(dwFlag, pCacheFlags, (sizeof(pCacheFlags) / sizeof(FlagConvert)));

	if (pCI->pNode)
	{
		pVnode = FFAT_GetInode((FFatNode*)pCI->pNode);
	}
	else
	{
		pVnode = NULL;
	}

	err = NS_ReadBuffer(_VCB(pCI), pVnode, dwSector, dwCount, (char*)pBuff, dwNSFlag);
	if (FERROR_NO_ERROR == err)
	{
		return dwCount;
	}

	return _errnoToFFAT(err);
}


/**
 * This function read sectors from buffer cache
 *
 * @param		dwSector	: sector number
 * @param		dwCount		: sector count
 * @param		pBuff		: buffer pointer
 * @param		dwFlag		: cache flag
 * @param		pCI			: Cache information.
 * @return		0 or above	: read sector count
 * @return		negative	: error number
 * @author		DongYoung Seo
 * @version		OCT-12-2007 [DongYoung Seo] First Writing.
 * @version		DEC-09-2008 [GwangOk Go] support block I/O
 * @version		APR-01-2009 [GwangOk Go] support case that FAT sector is larger than block before mounted
 */
t_int32
ffat_al_readSector(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount, 
					FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FERROR				err = FERROR_NO_ERROR;
	unsigned int		dwNSFlag;
	NS_PVNODE			pVnode;

#ifdef FFAT_BLOCK_IO
	FFatLDevInfo*		pLDevInfo;
	t_int32				dwSectorSize;
#endif

	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);

	_STATISTICS_READ(dwCount)

	dwNSFlag	= _convertFlagToNestle(dwFlag, pCacheFlags, (sizeof(pCacheFlags) / sizeof(FlagConvert)));

	if (pCI->pNode)
	{
		pVnode = FFAT_GetInode((FFatNode*)pCI->pNode);
	}
	else
	{
		pVnode = NULL;
	}

#ifndef FFAT_BLOCK_IO
	err = NS_ReadBuffer(_VCB(pCI), pVnode, dwSector, dwCount, (char*)pBuff, dwNSFlag);
#else

	pLDevInfo		= FFAT_GetLDevInfo(_VOL(_VCB(pCI)));
	dwSectorSize	= FFAT_GetSectorSize(_VOL(_VCB(pCI)));

	FFAT_ASSERT(pLDevInfo->dwFATSectorPerBlock != 0);

	if (pLDevInfo->dwFATSectorPerBlock == 1)
	{
		// sector 크기와 block 크기가 같다
		err = NS_ReadBuffer(_VCB(pCI), pVnode, dwSector, dwCount, pBuff, dwNSFlag);
	}
	else if (pLDevInfo->dwFATSectorPerBlock > 1)
	{
		// sector 크기가 block 크기보다 크다

		t_uint32		dwTempSector = dwSector;
		t_int32			dwRemainCount = dwCount;
		t_int32			dwTempCount = 0;

		if (dwSector & pLDevInfo->dwFATSectorPerBlockMask)
		{
			// head part
			// 한 block을 읽어 필요한 sector만 복사

			// read one block
			err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			if (((dwSector & pLDevInfo->dwFATSectorPerBlockMask) + dwCount) < (t_uint32)pLDevInfo->dwFATSectorPerBlock)
			{
				// read할 sector 한 block 이내인 경우
				dwTempCount = dwCount;
			}
			else
			{
				// read할 sector가 한 block 이상인 경우
				dwTempCount = pLDevInfo->dwFATSectorPerBlock - (dwSector & pLDevInfo->dwFATSectorPerBlockMask);
			}

			// copy sectors
			FFAT_MEMCPY(pBuff, _gszTempBuffer + ((dwSector & pLDevInfo->dwFATSectorPerBlockMask) * dwSectorSize),
						(dwTempCount * dwSectorSize));

			dwRemainCount -= dwTempCount;
			dwTempSector += dwTempCount;
			pBuff += (dwTempCount * dwSectorSize);
		}

		if (dwRemainCount >= pLDevInfo->dwFATSectorPerBlock)
		{
			// body part
			// block 단위로 read

			dwTempCount = dwRemainCount & (~pLDevInfo->dwFATSectorPerBlockMask);

			// read blocks
			err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								(dwTempCount >> pLDevInfo->dwFATSectorPerBlockBits), pBuff, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			dwRemainCount -= dwTempCount;
			dwTempSector += dwTempCount;
			pBuff += (dwTempCount * dwSectorSize);
		}

		if (dwRemainCount > 0)
		{
			// tail
			// 한 block을 읽어 필요한 sector만 복사

			// read one block
			err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			// copy sectors
			FFAT_MEMCPY(pBuff, _gszTempBuffer, (dwRemainCount * dwSectorSize));
		}
	}
	else
	{
		// sector 크기가 block 크기보다 작다

		FFAT_ASSERT(pLDevInfo->dwFATSectorPerBlock < 0);

		// read blocks
		err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwSector << pLDevInfo->dwFATSectorPerBlockBits),
							(dwCount << pLDevInfo->dwFATSectorPerBlockBits), pBuff, dwNSFlag);
		if (FERROR_NO_ERROR != err)
		{
			goto out;
		}
	}

out:

#endif
	if (FERROR_NO_ERROR == err)
	{
		return dwCount;
	}

	return _errnoToFFAT(err);
}


/**
* This function read sectors from buffer cache
*
* @param		pVC		: vectored sector storage
* @param		pBuff		: buffer pointer
* @param		dwCount		: IO request count
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information
* @param		dwSSB		: sector size bit count
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		DEC-18-2007 [DongYoung Seo] First Writing.
*/
t_int32
ffat_al_readSectorVS(FFatVS* pVS, FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
#ifdef _NESTLE_VECTOR_IO
#ifndef FFAT_BLOCK_IO
	FERROR				err = FERROR_NO_ERROR;
	unsigned int		dwNSFlag;
	NS_PVNODE			pVnode;
	t_int32				i;		// sector size
	t_int32				dwCount;
	NS_PVI				pVI;

	FFAT_ASSERT(pVS);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);

	dwNSFlag	= _convertFlagToNestle(dwFlag, pCacheFlags, (sizeof(pCacheFlags) / sizeof(FlagConvert)));

	if (pCI->pNode)
	{
		pVnode = FFAT_GetInode((FFatNode*)pCI->pNode);
	}
	else
	{
		pVnode = NULL;
	}

	pVI = (NS_PVI)pVS;		// NS_PVI and FFatVS are same structure

	err = NS_ReadBufferV(_VCB(pCI), pVnode, pVI, dwNSFlag);
	if (FERROR_NO_ERROR == err)
	{
		dwCount = 0;
		for (i = 0; i < pVS->dwValidEntryCount; i++)
		{
			dwCount += pVS->pVSE[i].dwCount;
		}

		FFAT_ASSERT(dwCount > 0);

		return dwCount;
	}

	return _errnoToFFAT(err);
#else	// #ifndef FFAT_BLOCK_IO
	t_int32				dwIndex;
	t_uint32			dwCount;

	FFAT_ASSERT(pVS);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_SYNC) : FFAT_TRUE);

	for (dwIndex = 0; dwIndex < pVS->dwValidEntryCount; dwIndex++)
	{
		dwCount = ffat_al_readSector(pVS->pVSE[dwIndex].dwSector, pVS->pVSE[dwIndex].pBuff,
									pVS->pVSE[dwIndex].dwCount, dwFlag, pCI);
		if (dwCount != pVS->pVSE[dwIndex].dwCount)
		{
			return FFAT_EIO;
		}
	}

	return FFAT_OK;
#endif	// #ifndef FFAT_BLOCK_IO

#else	// #ifdef _NESTLE_VECTOR_IO
	t_int32		i;
	t_int32		dwReadCount = 0;
	t_int32		r;

	for (i = 0; i < pVS->dwValidEntryCount; i++)
	{
		r = ffat_al_readSector(pVS->pVSE[i].dwSector, pVS->pVSE[i].pBuff,
			pVS->pVSE[i].dwCount, dwFlag, pCI);
		if (r < 0)
		{
			// I/O error
			return r;
		}

		dwReadCount += r;
	}

	return dwReadCount;
#endif	// #ifdef _NESTLE_VECTOR_IO
}


/**
 * This function write sectors through buffer cache
 *
 * @param		dwSector	: sector number
 * @param		dwCount		: sector count
 * @param		pBuff		: buffer pointer
 * @param		dwFlag		: cache flag
 * @param		pCI			: Cache information.
 * @return		0 or above	: read sector count
 * @return		negative	: error number
 * @author		DongYoung Seo
 * @version		OCT-12-2007 [DongYoung Seo] First Writing.
 * @version		DEC-09-2008 [GwangOk Go] support block I/O
 * @version		APR-01-2009 [GwangOk Go] support case that FAT sector is larger than block before mounted
 */
t_int32
ffat_al_writeSector(t_uint32 dwSector, t_int8* pBuff, t_int32 dwCount,
					FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FERROR				err = FERROR_NO_ERROR;
	unsigned int		dwNSFlag;
	NS_PVNODE			pVnode;

#ifdef FFAT_BLOCK_IO
	FFatLDevInfo*		pLDevInfo;
	t_int32				dwSectorSize;
#endif

	FFAT_ASSERT(dwCount > 0);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_SYNC) : FFAT_TRUE);

	_STATISTICS_WRITE(dwCount)

	dwNSFlag	= _convertFlagToNestle(dwFlag, pCacheFlags, (sizeof(pCacheFlags) / sizeof(FlagConvert)));

	if (pCI->pNode)
	{
		pVnode = FFAT_GetInode((FFatNode*)pCI->pNode);
	}
	else
	{
		pVnode = NULL;
	}

#ifndef FFAT_BLOCK_IO
	err = NS_WriteBuffer(_VCB(pCI), pVnode, dwSector, dwCount, (char*)pBuff, dwNSFlag);
#else

	pLDevInfo		= FFAT_GetLDevInfo(_VOL(_VCB(pCI)));
	dwSectorSize	= FFAT_GetSectorSize(_VOL(_VCB(pCI)));

	if (pLDevInfo->dwFATSectorPerBlock == 1)
	{
		// sector 크기와 block 크기가 같다
		err = NS_WriteBuffer(_VCB(pCI), pVnode, dwSector, dwCount, pBuff, dwNSFlag);
	}
	else if (pLDevInfo->dwFATSectorPerBlock > 1)
	{
		// sector 크기가 block 크기보다 크다

		t_uint32		dwTempSector = dwSector;
		t_int32			dwRemainCount = dwCount;
		t_int32			dwTempCount = 0;

		if (dwSector & pLDevInfo->dwFATSectorPerBlockMask)
		{
			// head part
			// 한 block을 읽어 write할 sector를 update후 write

			// read one block
			err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			if (((dwSector & pLDevInfo->dwFATSectorPerBlockMask) + dwCount) < (t_uint32)pLDevInfo->dwFATSectorPerBlock)
			{
				// write할 sector 한 block 이내인 경우
				dwTempCount = dwCount;
			}
			else
			{
				// write할 sector가 한 block 이상인 경우
				dwTempCount = pLDevInfo->dwFATSectorPerBlock - (dwSector & pLDevInfo->dwFATSectorPerBlockMask);
			}

			// copy sectors
			FFAT_MEMCPY(_gszTempBuffer + ((dwSector & pLDevInfo->dwFATSectorPerBlockMask) * dwSectorSize),
						pBuff, (dwTempCount * dwSectorSize));

			// write one block
			err = NS_WriteBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			dwRemainCount -= dwTempCount;
			dwTempSector += dwTempCount;
			pBuff += (dwTempCount * dwSectorSize);
		}

		if (dwRemainCount >= pLDevInfo->dwFATSectorPerBlock)
		{
			// body part
			// block 단위로 write

			dwTempCount = dwRemainCount & (~pLDevInfo->dwFATSectorPerBlockMask);

			// write blocks
			err = NS_WriteBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								(dwTempCount >> pLDevInfo->dwFATSectorPerBlockBits), pBuff, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			dwRemainCount -= dwTempCount;
			dwTempSector += dwTempCount;
			pBuff += (dwTempCount * dwSectorSize);
		}

		if (dwRemainCount > 0)
		{
			// tail part
			// 한 block을 읽어 write할 sector를 update후 write

			// read one block
			err = NS_ReadBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}

			// copy sectors
			FFAT_MEMCPY(_gszTempBuffer, pBuff, (dwRemainCount * dwSectorSize));

			// write one block
			err = NS_WriteBuffer(_VCB(pCI), pVnode, (dwTempSector >> pLDevInfo->dwFATSectorPerBlockBits),
								1, _gszTempBuffer, dwNSFlag);
			if (FERROR_NO_ERROR != err)
			{
				goto out;
			}
		}
	}
	else
	{
		// sector 크기가 block 크기보다 작다

		FFAT_ASSERT(pLDevInfo->dwFATSectorPerBlock < 0);

		// read blocks
		err = NS_WriteBuffer(_VCB(pCI), pVnode, (dwSector << pLDevInfo->dwFATSectorPerBlockBits),
							(dwCount << pLDevInfo->dwFATSectorPerBlockBits), pBuff, dwNSFlag);
		if (FERROR_NO_ERROR != err)
		{
			goto out;
		}
	}

out:

#endif
	if ((pVnode) && !(dwNSFlag & (NS_BCM_OP_SYNC | NS_BCM_OP_DIRECT) ))
	{
		NS_MarkVnodeDataDirty(pVnode);
	}

	if (FERROR_NO_ERROR == err)
	{
		return dwCount;
	}

	return _errnoToFFAT(err);
}


/**
* This function write sectors from buffer cache
*
* @param		pVC		: vectored sector storage
* @param		pBuff		: buffer pointer
* @param		dwCount		: IO request count
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information
* @param		dwSSB		: sector size bit count
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		DEC-18-2007 [DongYoung Seo] First Writing.
*/
t_int32
ffat_al_writeSectorVS(FFatVS* pVS, FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
#ifdef _NESTLE_VECTOR_IO
#ifndef FFAT_BLOCK_IO
	FERROR				err = FERROR_NO_ERROR;
	unsigned int		dwNSFlag;
	NS_PVNODE			pVnode;
	t_int32				i;		// sector size
	t_int32				dwCount;
	NS_PVI				pVI;

	FFAT_ASSERT(pVS);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_SYNC) : FFAT_TRUE);

	dwNSFlag	= _convertFlagToNestle(dwFlag, pCacheFlags, 
						(sizeof(pCacheFlags) / sizeof(FlagConvert)));

	if (pCI->pNode)
	{
		pVnode = FFAT_GetInode((FFatNode*)pCI->pNode);
	}
	else
	{
		pVnode = NULL;
	}

	pVI = (NS_PVI)pVS;		// NS_PVI and FFatVS are same structure

	err = NS_WriteBufferV(_VCB(pCI), pVnode, pVI, dwNSFlag);
	if (FERROR_NO_ERROR == err)
	{
		dwCount = 0;
		for (i = 0; i < pVS->dwValidEntryCount; i++)
		{
			dwCount += pVS->pVSE[i].dwCount;
		}

		FFAT_ASSERT(dwCount > 0);

		return dwCount;
	}

	return _errnoToFFAT(err);
#else	// #ifndef FFAT_BLOCK_IO
	t_int32				dwIndex;
	t_uint32			dwCount;

	FFAT_ASSERT(pVS);
	FFAT_ASSERT(pCI);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? ((dwFlag & FFAT_CACHE_META_IO) == FFAT_CACHE_META_IO) : FFAT_TRUE);
	FFAT_ASSERT((dwFlag & FFAT_CACHE_DATA_META) ? (dwFlag & FFAT_CACHE_SYNC) : FFAT_TRUE);

	for (dwIndex = 0; dwIndex < pVS->dwValidEntryCount; dwIndex++)
	{
		dwCount = ffat_al_writeSector(pVS->pVSE[dwIndex].dwSector, pVS->pVSE[dwIndex].pBuff,
									pVS->pVSE[dwIndex].dwCount, dwFlag, pCI);
		if (dwCount != pVS->pVSE[dwIndex].dwCount)
		{
			return FFAT_EIO;
		}
	}

	return FFAT_OK;
#endif	// #ifndef FFAT_BLOCK_IO

#else	// #ifdef _NESTLE_VECTOR_IO
	t_int32		i;
	t_int32		dwWriteCount = 0;
	t_int32		r;

	for (i = 0; i < pVS->dwValidEntryCount; i++)
	{
		r = ffat_al_writeSector(pVS->pVSE[i].dwSector, pVS->pVSE[i].pBuff,
			pVS->pVSE[i].dwCount, dwFlag, pCI);
		if (r < 0)
		{
			// I/O error
			return r;
		}

		dwWriteCount += r;
	}

	return dwWriteCount;
#endif	// #ifdef _NESTLE_VECTOR_IO
}


/**
* This function erase sectors through buffer cache
* This is for NAND support.
*
* @param		dwSector	: sector number
* @param		dwCount		: sector count
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information.
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		JUL-17-2006 [DongYoung Seo] First Writing.
*/
t_int32
ffat_al_eraseSector(t_uint32 dwSector, t_int32 dwCount, 
					FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	FERROR		err;
	NS_DISK_SECTOR_RANGE	stRange;

#ifdef FFAT_BLOCK_IO
	FFatLDevInfo*		pLDevInfo;
#endif

#ifndef FFAT_BLOCK_IO
	stRange.dwSectorNum		= dwSector;
	stRange.dwNumSectors	= dwCount;
#else
	pLDevInfo = FFAT_GetLDevInfo(_VOL(_VCB(pCI)));

	stRange.dwSectorNum		= dwSector >> pLDevInfo->dwFATSectorPerBlockBits;
	stRange.dwNumSectors	= dwCount >> pLDevInfo->dwFATSectorPerBlockBits;
#endif

	// need to discard buffer cache.
	// but buffer cache does not have interface for discard.
	err = NS_EraseSectors(NS_GetLogicalDisk(_VCB(pCI)),&stRange);
	IF_UK (FERROR_NO_ERROR != err)
	{
		if (FERROR_NOT_SUPPORTED == err)
		{
			return FFAT_ENOSUPPORT;
		}
		return FFAT_EIO;
	}

	return dwCount;
}


/**
* This function flush(sync) all buffers for a volume or an entry
* 
*
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information. (contain volume information)
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		JUL-17-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_bc_sync(FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	// Nestle does not support this
	FFAT_LOG_PRINTF((_T("Nestle does not provide buffer cache sync")));
	return FFAT_OK;
}


/**
* This function flush(sync) all buffers for a volume or an entry
* 
*
* @param		dwFlag		: cache flag
* @param		pCI			: Cache information. (contain volume information)
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		JUL-17-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_al_syncDev(FFatCacheFlag dwFlag, FFatCacheInfo* pCI)
{
	// Nestle BCM Does not support this
	FFAT_LOG_PRINTF((_T("Nestle does not provide buffer cache sync")));

	return FFAT_OK;
}


/**
* This function write sectors to the logical device
*
* @param		pDevice		: pointer of logical device
* @param		dwSector	: sector number
* @param		dwCount		: sector count
* @param		pBuff		: buffer pointer
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		NOV-28-2007 [DongYoung Seo] First Writing.
*/
t_int32
ffat_ldev_writeSector(void* pDevice, t_uint32 dwSector,
					  t_int8* pBuff, t_int32 dwCount)
{
	FERROR		err;

	err = NS_WriteSectors(pDevice, dwSector, dwCount, (char*)pBuff, NS_NONE);
	if (FERROR_NO_ERROR == err)
	{
		return dwCount;
	}

	return _errnoToFFAT(err);
}


/**
* This function erase sectors to the logical device
*
* @param		pDevice		: pointer of logical device
* @param		dwSector	: sector number
* @param		dwCount		: sector count
* @return		0 or above	: read sector count
* @return		negative	: error number
* @author		DongYoung Seo
* @version		NOV-28-2007 [DongYoung Seo] First Writing.
*/
t_int32
ffat_ldev_eraseSector(void* pDevice, t_uint32 dwSector, t_int32 dwCount)
{
	FERROR		err;
	NS_DISK_SECTOR_RANGE	stRange;

	stRange.dwSectorNum		= dwSector;
	stRange.dwNumSectors	= dwCount;

	// check !!not implemented yet
	err = NS_EraseSectors(pDevice, &stRange);

	if ((FERROR_NO_ERROR == err) || (FERROR_NOT_SUPPORTED == err))
	{
		return dwCount;
	}

	return _errnoToFFAT(err);
}

//=============================================================================
//
//	static functions
//


/**
* convert FFAT flag to Nestle flag
*
* @param		dwFFatFlag		: [IN] Nestle flag
* @param		pConvertDB		: [IN] convert data
* @param		dwCount			: [IN] data count in pConvertDB
* @return		FFAT flag
* @author		DongYoung Seo
* @version		02-JUN-2008 [DongYoung Seo] First Writing.
*/
static t_uint32
_convertFlagToNestle(t_uint32 dwFFatFlag, const FlagConvert* pConvertDB, t_int32 dwCount)
{
	t_uint32	dwNestleFlag = 0;
	t_int32		i;

	for (i = 0; i < dwCount; i++)
	{
		if (pConvertDB[i].dwFFatFlag & dwFFatFlag)
		{
			dwNestleFlag |= pConvertDB[i].dwNestleFlag;
		}
	}

	return dwNestleFlag;
}


/**
* convert Errno for TFS4 to error to FFAT
* 
*
* @param		dwErrno		: FFAT cache flag
* @return		FFAT error number
* @author		DongYoung Seo
* @version		JUL-25-2006 [DongYoung Seo] First Writing.
*/
static FFatErr
_errnoToFFAT(FERROR dwErrno)
{
	t_int32		i;

	for (i = 0; i < (sizeof(_pErrnoTbl) / sizeof(ErrorNoConvert)); i++)
	{
		if (_pErrnoTbl[i].dwNestle == dwErrno)
		{
			return _pErrnoTbl[i].dwFFat;
		}
	}

	FFAT_ASSERTP(0, (_T("there is no profit error number, check it!!")));

	return FERROR_SYSTEM_PANIC;
}


// debug begin
#ifdef FFAT_DEBUG
	static void
	_printStatistics(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======   BLOCK DEVICE I/O    STATICS    ===================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Read Request Count: ",	_STATISTIC()->dwRead));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Read Sector Count: ",		_STATISTIC()->dwTotalReadSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Write Request Count: ",	_STATISTIC()->dwWrite));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Write Sector Count: ",	_STATISTIC()->dwTotalWriteSector));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Erase Request Count: ",	_STATISTIC()->dwErase));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Erase Sector Count: ",	_STATISTIC()->dwTotalEraseSector));

		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
	}
#endif
// debug end
