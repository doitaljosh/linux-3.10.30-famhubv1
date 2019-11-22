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
 * @file		ffat_addon_debug.c
 * @brief		File Debug Modlue for FFAT
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go]	First writing
 * @version		JAN-13-2010 [ChunUm Kong]	Modifying comment (English/Korean)
 * @see			None
 */

#include "ess_math.h"

#include "ffat_common.h"
#include "ffat_config.h"
#include "ffat_errno.h"

#include "ffat_file.h"
#include "ffat_misc.h"
#include "ffatfs_api.h"

#include "ffat_addon_types_internal.h"
#include "ffat_addon_debug.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_GLOBAL_COMMON)

#define _DEBUG_FILE_NAME				"btfs.dbg"
#define _DEBUG_FILE_NAME_LEN			FFAT_STRLEN(_DEBUG_FILE_NAME)
#define _DEBUG_FILE_NAME_BUF_SIZE		((FFAT_STRLEN(_DEBUG_FILE_NAME) + 1) * sizeof(t_wchar))

#define _DEBUG_FILE_SIG_1				0x67626466
#define _DEBUG_FILE_SIG_2				0x73667462

#define _DEBUG_FILE_SECTOR_COUNT		64
#define _DEBUG_FILE_TEMP_BUF_SIZE		1024

#define _DEBUG_FILE_INFO_BUF_SIZE		(VOL_SS(pVol) * 2 + _DEBUG_FILE_SECTOR_COUNT * sizeof(t_uint32))

#define _DEBUG_FILE_UID					FFAT_ADDON_DEBUG_FILE_UID
#define _DEBUG_FILE_GID					FFAT_ADDON_DEBUG_FILE_GID
#define _DEBUG_FILE_PERM				FFAT_ADDON_DEBUG_FILE_PERMISSION

#define _DFILE_INFO(_pVol)				(VOL_ADDON(_pVol)->pDFile)


typedef struct _DebugFileHeader
{
	t_int32				dwSignature1;
	t_int32				dwCurSectorIndex;
	t_int32				dwCurOffset;
	t_int32				dwSignature2;
} DebugFileHeader;

// temporary buffer to store debugging message
t_int8*		_gpPrintBuffer = NULL;

static DebugFileInfo*
_allocDebugFileInfo(Vol* pVol);

static void
_deallocDebugFileInfo(Vol* pVol, DebugFileInfo* pDebugFileInfo);

static FFatErr
_getSectorsOfDebugFile(Vol* pVol, DebugFileInfo* pDebugFileInfo, Node* pNodeDebugFile, ComCxt* pCxt);


/**
 * initialize file debug module
 *
 * @return		void
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
void
ffat_debug_init(void)
{
#if defined(FFAT_DEBUG_FILE) && defined(FFAT_DYNAMIC_ALLOC)
	_gpPrintBuffer = (t_int8*)FFAT_MALLOC((_DEBUG_FILE_TEMP_BUF_SIZE + 1), ESS_MALLOC_NONE);	// including null
	if (_gpPrintBuffer == NULL)
	{
		FFAT_PRINT_CRITICAL((_T("fail to allocate memory\n")));
	}
#endif

	return;
}


/**
 * terminate file debug module
 *
 * @return		void
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
void
ffat_debug_terminate(void)
{
#if defined(FFAT_DEBUG_FILE) && defined(FFAT_DYNAMIC_ALLOC)
	if (_gpPrintBuffer)
	{
		FFAT_FREE(_gpPrintBuffer, (_DEBUG_FILE_TEMP_BUF_SIZE + 1));
	}

	_gpPrintBuffer = NULL;
#endif

	return;
}


/**
 * when volume is mounted, lookup debug file & prepare to use debug file
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		void
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
void
ffat_debug_mount(void* pVol, ComCxt* pCxt)
{
	FFatErr				r;
	Node*				pNodeDebugFile;
	t_wchar*			psDebugFileName;
	DebugFileInfo*		pDebugFileInfo = NULL;
	DebugFileHeader*	pDebugFileHeader;
	FFatCacheInfo		stCI;
	t_boolean			bCreate = FFAT_FALSE;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

 	if (_gpPrintBuffer == NULL)
 	{
		// not initialized
 		return;
 	}

	pNodeDebugFile = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNodeDebugFile);

	psDebugFileName = (t_wchar*)FFAT_LOCAL_ALLOC(_DEBUG_FILE_NAME_BUF_SIZE, pCxt);
	FFAT_ASSERT(psDebugFileName);

	r = FFAT_MBSTOWCS(psDebugFileName, (_DEBUG_FILE_NAME_LEN + 1), 
						_DEBUG_FILE_NAME, _DEBUG_FILE_NAME_LEN, VOL_DEV((Vol*)pVol));
	if (r < 0)
	{
		FFAT_PRINT_DEBUG((_T("fail to convert debug file name into WC from MB")));
		goto out;
	}

	ffat_node_resetNodeStruct(pNodeDebugFile);

	// find the existing debug file
	r = ffat_node_lookup(VOL_ROOT((Vol*)pVol), pNodeDebugFile, psDebugFileName, 0,
						(FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_NO_LOCK), NULL, pCxt);

	if (r == FFAT_OK)
	{
		// if found, check file size & get sectors of debug file
		if (NODE_S(pNodeDebugFile) != (t_uint32)(_DEBUG_FILE_SECTOR_COUNT * VOL_SS((Vol*)pVol)))
		{
			// delete created debug file
			ffat_node_unlink(VOL_ROOT((Vol*)pVol), pNodeDebugFile, 
					(NODE_UNLINK_NO_LOCK | NODE_UNLINK_NO_LOG | NODE_UNLINK_SYNC), pCxt);

			r = FFAT_ENOENT;
		}
		else
		{
			// read first sector (debug file header)
			goto get_sectors;
		}
	}

	if (r == FFAT_ENOENT)
	{
		// if not found or deleted, create debug file
		FFatExtendedDirEntryInfo	stXDEInfo = {_DEBUG_FILE_UID, _DEBUG_FILE_GID, _DEBUG_FILE_PERM};

		// create debug file
		r = ffat_node_create(VOL_ROOT((Vol*)pVol), pNodeDebugFile, psDebugFileName,
				(FFAT_CREATE_ATTR_RO | FFAT_CREATE_ATTR_HIDDEN | FFAT_CREATE_ATTR_SYS | FFAT_CREATE_NO_LOCK),
				&stXDEInfo, pCxt);
		if (r < 0)
		{
			FFAT_PRINT_DEBUG((_T("fail to create debug file")));
			goto out;
		}

		r = ffat_file_changeSize(pNodeDebugFile, (_DEBUG_FILE_SECTOR_COUNT * VOL_SS((Vol*)pVol)),
						FFAT_CHANGE_SIZE_NO_LOCK, (FFAT_CACHE_DATA_LOG | FFAT_CACHE_DIRECT_IO), pCxt);
		if (r < 0)
		{
			FFAT_PRINT_DEBUG((_T("fail to change debug file size")));
			goto out;
		}

		bCreate = FFAT_TRUE;
	}
	else
	{
		// lookup error
		goto out;
	}

get_sectors:
	if ((pDebugFileInfo = _allocDebugFileInfo((Vol*)pVol)) == NULL)
	{
		goto out;
	}
	
	// get area for debug file
	r = _getSectorsOfDebugFile((Vol*)pVol, pDebugFileInfo, pNodeDebugFile, pCxt);
	if (r < 0)
	{
		FFAT_PRINT_DEBUG((_T("fail to get sector for log file")));
		goto out;
	}

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV((Vol*)pVol));

	if (bCreate == FFAT_TRUE)
	{
		pDebugFileHeader = (DebugFileHeader*)pDebugFileInfo->pHeaderSectorBuffer;

		pDebugFileHeader->dwSignature1	= _DEBUG_FILE_SIG_1;
		pDebugFileHeader->dwSignature2	= _DEBUG_FILE_SIG_2;

		pDebugFileHeader->dwCurSectorIndex	= 1;
		pDebugFileHeader->dwCurOffset		= 0;

		// write DebugFileHeader
		r = ffat_al_writeSector(*(pDebugFileInfo->pdwSectorNumbers),
								pDebugFileInfo->pHeaderSectorBuffer, 1, FFAT_CACHE_DIRECT_IO, &stCI);
		if (r != 1)
		{
			goto out;
		}
	}
	else
	{
		// read DebugFileHeader
		r = ffat_al_readSector(*(pDebugFileInfo->pdwSectorNumbers),
								pDebugFileInfo->pHeaderSectorBuffer, 1, FFAT_CACHE_DIRECT_IO, &stCI);
		if (r != 1)
		{
			goto out;
		}

		pDebugFileHeader = (DebugFileHeader*)pDebugFileInfo->pHeaderSectorBuffer;

		if ((pDebugFileHeader->dwSignature1 != _DEBUG_FILE_SIG_1) ||
			(pDebugFileHeader->dwSignature2 != _DEBUG_FILE_SIG_2))
		{
			goto out;
		}

		// read current sector
		r = ffat_al_readSector(*(pDebugFileInfo->pdwSectorNumbers + pDebugFileHeader->dwCurSectorIndex),
								pDebugFileInfo->pCurSectorBuffer, 1, FFAT_CACHE_DIRECT_IO, &stCI);
		if (r != 1)
		{
			goto out;
		}
	}

	pDebugFileInfo->dwFirstCluster = NODE_C(pNodeDebugFile);

	_DFILE_INFO((Vol*)pVol) = pDebugFileInfo;

#ifdef FFAT_DEBUG_FILE
	FFAT_PRINT_FILE(pVol, (_T("[Mount]")));
#endif

out:
	if (r < 0)
	{
		_deallocDebugFileInfo((Vol*)pVol, pDebugFileInfo);

		FFAT_ASSERT(_DFILE_INFO((Vol*)pVol) == NULL);
	}

	FFAT_LOCAL_FREE(psDebugFileName, _DEBUG_FILE_NAME_BUF_SIZE, pCxt);
	FFAT_LOCAL_FREE(pNodeDebugFile, sizeof(Node), pCxt);

	return;
}


/**
 * when volume is unmounted, destroy resource for file debug of volume
 *
 * @param		pVol		: [IN] volume pointer
 * @return		void
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
void
ffat_debug_umount(Vol* pVol)
{
	_deallocDebugFileInfo(pVol, _DFILE_INFO(pVol));

	_DFILE_INFO((Vol*)pVol) = NULL;

	return;
}

#ifdef FFAT_DEBUG_FILE

	/**
	 * copy debug message into temporary buffer
	 *
	 * @param		szMsg		: [IN] debug message
	 * @return		t_int32		: size of debug message
	 * @author		GwangOk Go
	 * @version		MAY-13-2009 [GwangOk Go] First Writing
	 */
	t_int32
	ffat_debug_copyMsgToBuffer(const char* szMsg, ...)
	{
		va_list		ap;
		t_int32		ret;

		if (_gpPrintBuffer == NULL)
		{
			return -1;
		}

		va_start(ap, szMsg);

		ret = FFAT_VSNPRINTF(_gpPrintBuffer, _DEBUG_FILE_TEMP_BUF_SIZE, szMsg, (va_list)ap);	// size is not including NULL

		return ret;
	}


	/**
	 * write debug message into file
	 *
	 * @param		pVol		: [IN] volume pointer
	 * @param		dwSize		: [IN] size of debug message
	 * @return		void
	 * @author		GwangOk Go
	 * @version		MAY-13-2009 [GwangOk Go] First Writing
	 */
	void
	ffat_debug_writeMsgToFile(Vol* pVol, t_int32 dwSize)
	{
		FFatErr		r;
		t_int32		dwCurOffset;
		t_int32		dwCurSectorIndex;
		t_int32		dwTotalSize;
		t_int32		dwCurSize;
		t_int8*		pTempPtr;

		FFatCacheInfo	stCI;

		DebugFileInfo*		pDebugFileInfo;
		DebugFileHeader*	pDebugFileHeader;

		FFAT_ASSERT(_gpPrintBuffer);
		FFAT_ASSERT(pVol);
		FFAT_ASSERT(dwSize <= _DEBUG_FILE_TEMP_BUF_SIZE);

		if ((pDebugFileInfo = _DFILE_INFO((Vol*)pVol)) == NULL)
		{
			// not mounted
			return;
		}

		pDebugFileHeader	= (DebugFileHeader*)pDebugFileInfo->pHeaderSectorBuffer;

		dwCurOffset			= pDebugFileHeader->dwCurOffset;
		dwCurSectorIndex	= pDebugFileHeader->dwCurSectorIndex;

		FFAT_ASSERT(dwCurOffset < VOL_SS(pVol));

		FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

		pTempPtr = _gpPrintBuffer;
		dwTotalSize = dwSize + 1;
		FFAT_ASSERT(dwTotalSize <= _DEBUG_FILE_TEMP_BUF_SIZE + 1);

		while (dwTotalSize > 0)
		{
			// check whether offset exceeds sector boundary
			if (dwCurOffset + dwTotalSize > VOL_SS(pVol))
			{
				dwCurSize = VOL_SS(pVol) - dwCurOffset;
			}
			else
			{
				dwCurSize = dwTotalSize;
			}

			FFAT_ASSERT(pTempPtr + dwCurSize <= _gpPrintBuffer + _DEBUG_FILE_TEMP_BUF_SIZE + 1);

			FFAT_MEMCPY(pDebugFileInfo->pCurSectorBuffer + dwCurOffset, pTempPtr, dwCurSize);

			r = ffat_al_writeSector(*(pDebugFileInfo->pdwSectorNumbers + dwCurSectorIndex),
									pDebugFileInfo->pCurSectorBuffer, 1, FFAT_CACHE_DIRECT_IO, &stCI);
			if (r != 1)
			{
				// if write fail, no update DebugFileHeader
				return;
			}

			dwTotalSize -= dwCurSize;
			dwCurOffset += dwCurSize;
			pTempPtr += dwCurSize;

			FFAT_ASSERT(dwTotalSize >= 0);
			FFAT_ASSERT(dwCurOffset <= VOL_SS(pVol));

			if (dwCurOffset == VOL_SS(pVol))
			{
				// if offset reach to sector boundary, initialize offset
				dwCurOffset = 0;

				if (dwCurSectorIndex == (_DEBUG_FILE_SECTOR_COUNT - 1))
				{
					dwCurSectorIndex = 1; 
				}
				else
				{
					dwCurSectorIndex++;
				}
			}

			pDebugFileHeader->dwCurOffset		= dwCurOffset;
			pDebugFileHeader->dwCurSectorIndex	= dwCurSectorIndex;

			FFAT_ASSERT((dwTotalSize > 0) ? (dwCurOffset == 0) : FFAT_TRUE);
		}

		// update DebugFileHeader
		r = ffat_al_writeSector(*(pDebugFileInfo->pdwSectorNumbers),
								pDebugFileInfo->pHeaderSectorBuffer, 1, FFAT_CACHE_DIRECT_IO, &stCI);
		// don't care write fail

		return;
	}

#endif

/**
 * check whether node is debug file
 *
 * @param		pNode		: [IN] node pointer
 * @return		t_boolean	: true or false
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
t_boolean
ffat_debug_isDubugFile(Node* pNode)
{
	DebugFileInfo*		pDebugFileInfo;

	FFAT_ASSERT(pNode);

	pDebugFileInfo = _DFILE_INFO((Vol*)NODE_VOL(pNode));

	if ((pDebugFileInfo != NULL) &&
		(NODE_C(pNode) != 0) &&
		(NODE_C(pNode) == pDebugFileInfo->dwFirstCluster))
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/**
 * check whether node is accessible
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFatErr		: FFAT_OK or FFAT_EACCESS
 * @author		GwangOk Go
 * @version		MAY-14-2009 [GwangOk Go] First Writing
 */
FFatErr
ffat_debug_isAccessible(Node* pNode)
{
	FFAT_ASSERT(pNode);

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		return FFAT_EACCESS;
	}

	return FFAT_OK;
}


//// Static Function ////


/**
 * get sectors of debug file
 *
 * @param		pVol 			: [IN] volume pointer
 * @param		pDebugFileInfo	: [IN] debug file info
 * @param		pNodeDebugFile	: [IN] node for debug file
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
static FFatErr
_getSectorsOfDebugFile(Vol* pVol, DebugFileInfo* pDebugFileInfo, Node* pNodeDebugFile, ComCxt* pCxt)
{
	t_uint32		dwSectorNumber;		// sector number
	FFatVC			stVC;
	t_uint32		dwCount;
	t_int32			i;
	t_uint32		j;
	t_int32			dwSectorIndex = 0;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeDebugFile);
	FFAT_ASSERT(pCxt);

	stVC.pVCE	= (FFatVCE*)FFAT_LOCAL_ALLOC((sizeof(FFatVCE) * _DEBUG_FILE_SECTOR_COUNT), pCxt);
	FFAT_ASSERT(stVC.pVCE);

	VC_INIT(&stVC, 0);
	stVC.dwTotalEntryCount	= _DEBUG_FILE_SECTOR_COUNT;

	// get vectored cluster information from offset 0
	r = ffat_misc_getVectoredCluster(pVol, pNodeDebugFile, NODE_C(pNodeDebugFile), 0, 0, &stVC, NULL, pCxt);
	FFAT_EO(r, (_T("fail to get cluster for node")));

	FFAT_ASSERT(VC_CC(&stVC) == ESS_MATH_CDB(NODE_S(pNodeDebugFile), VOL_CS(pVol), VOL_CSB(pVol)));

	// store sector numbers
	for (i = 0; i < VC_VEC(&stVC); i++)
	{
		dwSectorNumber = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), stVC.pVCE[i].dwCluster);
		dwCount = stVC.pVCE[i].dwCount << VOL_SPCB(pVol);

		for (j = 0; j < dwCount; j++)
		{
			FFAT_ASSERT(dwSectorIndex < _DEBUG_FILE_SECTOR_COUNT);

			*(pDebugFileInfo->pdwSectorNumbers + dwSectorIndex) = dwSectorNumber + j;
			dwSectorIndex++;
		}
	}

out:
	FFAT_LOCAL_FREE(stVC.pVCE, (sizeof(FFatVCE) * _DEBUG_FILE_SECTOR_COUNT), pCxt);

	return r;
}


/**
 * allocate memory for debug file info & buffers
 *
 * @param		pVol 			: [IN] volume pointer
 * @return		DebugFileInfo	: debug file info
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
static DebugFileInfo*
_allocDebugFileInfo(Vol* pVol)
{
	DebugFileInfo*		pDebugFileInfo;
	
	pDebugFileInfo = (DebugFileInfo*)FFAT_MALLOC((sizeof(DebugFileInfo) + _DEBUG_FILE_INFO_BUF_SIZE), ESS_MALLOC_NONE);
	if (pDebugFileInfo == NULL)
	{
		FFAT_PRINT_CRITICAL((_T("fail to allocate memory for debug file info\n")));
		goto out;
	}

	FFAT_MEMSET(pDebugFileInfo, 0, (sizeof(DebugFileInfo) + _DEBUG_FILE_INFO_BUF_SIZE));

	pDebugFileInfo->dwFirstCluster = 0;
	pDebugFileInfo->pHeaderSectorBuffer = (t_int8*)pDebugFileInfo + sizeof(DebugFileInfo);
	pDebugFileInfo->pCurSectorBuffer = pDebugFileInfo->pHeaderSectorBuffer + VOL_SS(pVol);
	pDebugFileInfo->pdwSectorNumbers = (t_uint32*)(pDebugFileInfo->pCurSectorBuffer + VOL_SS(pVol));

out:
	return pDebugFileInfo;
}


/**
 * deallocate memory for debug file info & buffers
 *
 * @param		pVol 			: [IN] volume pointer
 * @param		pDebugFileInfo	: [IN] debug file info
 * @return		void
 * @author		GwangOk Go
 * @version		MAY-13-2009 [GwangOk Go] First Writing
 */
static void
_deallocDebugFileInfo(Vol* pVol, DebugFileInfo* pDebugFileInfo)
{
	FFAT_ASSERT(pVol);

	if (pDebugFileInfo)
	{
		FFAT_FREE(pDebugFileInfo, (sizeof(DebugFileInfo) + _DEBUG_FILE_INFO_BUF_SIZE));
	}

	return;
}

