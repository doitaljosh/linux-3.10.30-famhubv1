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
 * @file		ffat_main.c
 * @brief		FFAT CORE module manager
 *				This module manage all FFAT filesystem component.
 *				It operates init/terminate/lock/unlock etc...
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */


// C-Runtime headers

// base library

// FFAT headers
#include "ffat_config.h"
#include "ffat_al.h"

#include "ffat_common.h"
#include "ffatfs_main.h"

#include "ffat_file.h"
#include "ffat_vol.h"
#include "ffat_node.h"
#include "ffat_dir.h"
#include "ffat_main.h"
#include "ffat_misc.h"

#include "ffatfs_api.h"
#include "ffat_addon_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_MAIN)

// global variables
FFatMainInfo		gstFFatMain;			// FFAT Main information

// definitions
typedef signed int	_InitStatus;
enum __InitStatus
{
	_INIT_NONE		= 0x00000000,
	_INIT_COMMON	= 0x00000001,
	_INIT_DIR		= 0x00000002,
	_INIT_FILE		= 0x00000004,
	_INIT_NODE		= 0x00000008,
	_INIT_VOL		= 0x00000010,
	_INIT_FFATFS	= 0x00000020,
	_INIT_ADDON		= 0x00000040,
	_INIT_AL		= 0x00000080,

	_INIT_ALL		= 0x7FFFFFFF	// ALL modules are initialized
};

// static functions
static FFatErr	_terminate(t_boolean bCheckInit, _InitStatus dwInitState);

static void		_initFSCtlCmdTable(void);
static FFatErr	_fsctlIsVolFAT32(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlGetRootClusterNo(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlGetRootNode(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlSyncVol(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlIsValidBootSector(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlAddCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlRemoveCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlChkCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlLDevIO(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);

static FFatErr	_registerCallback(ComCxt* pCxt);

// static variables
static PFN_FSCTL	pFSCtlCmdTable[(FFAT_FSCTL_CORE_END & ~(FFAT_FSCTL_BASE)) + 1];

#define	_STATISTIC_CACHE_CALLBACK
#define	_STATISTIC_FAT_CALLBACK
#define _STATISTIC_LOCK_COUNT
#define _STATISTIC_UNLOCK_COUNT
#define _STATISTIC_PRINT

// debug begin
#ifdef FFAT_DEBUG
	static FFatErr	_debugFSCtl(FFatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);

	static FFatErr	_debugFSCtlILogRecoveryTestSet(FFatLogRecoveryTestFlag dwType);
	static FFatErr	_debugLogRecovery(FFatLogRecoveryTestFlag dwType, t_boolean bSet, t_boolean bCheck);

	static FFatErr	_debugFSCtlIFCCH(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
	static FFatErr	_debugBase(Vol* pVol, ComDebug* pComDebug, ComCxt* pCxt);
	static FFatErr	_debugRemount(Vol* pVol, void* pDebug, ComCxt* pCxt);
	typedef struct
	{
		t_uint32		dwLockCount;		// lock operation count
		t_uint32		dwUnlockCount;		// unlock operation count
		t_uint32		dwMount;			// mount count
		t_uint32		dwUmount;			// umount count
		t_uint32		dwGetVolumeStatus;	// FFAT_GetVolumeStatus() count
		t_uint32		dwSetVolumeLabel;	// set volume name count
		t_uint32		dwGetVoluemName;	// get volume name count
		t_uint32		dwLookup;			// lookup count
		t_uint32		dwFSCtl;			// fsctl count
		t_uint32		dwMakedir;			// make directory count
		t_uint32		dwRemovedir;		// remove directory count
		t_uint32		dwReaddir;			// readdir count
		t_uint32		dwCreate;			// file creation count
		t_uint32		dwOpen;				// open count
		t_uint32		dwClose;			// open count
		t_uint32		dwRead;				// read count
		t_uint32		dwWrite;			// write count
		t_uint32		dwUnlink;			// unlink count
		t_uint32		dwSecureUnlink;		// secure unlink count
		t_uint32		dwChangeSize;		// change size count
		t_uint32		dwRename;			// rename count
		t_uint32		dwSetNodeStatus;	// set node status count
		t_uint32		dwGetNodeStatus;	// get node status count
		t_uint32		dwSyncNode;			// node sync count
		t_uint32		dwSyncVol;			// volume sync count
		t_uint32		dwSync;				// sync count
		t_uint32		dwGetNodeClusters;	// Get node clusters count
		t_uint32		dwCacheCallBack;	// call back count
		t_uint32		dwFatCallBack;		// call back count
	} _MainDebug;

	#define _MAIN_DEBUG()		((_MainDebug*)&_stMainDebug)

	//static _MainDebug	_stMainDebug;
	static t_int32 _stMainDebug[sizeof(_MainDebug) / sizeof(t_int32)];

	#undef	_STATISTIC_CACHE_CALLBACK
	#undef	_STATISTIC_FAT_CALLBACK

	#define	_STATISTIC_CACHE_CALLBACK		_MAIN_DEBUG()->dwCacheCallBack++;
	#define	_STATISTIC_FAT_CALLBACK			_MAIN_DEBUG()->dwFatCallBack++;

	#undef _STATISTIC_LOCK_COUNT
	#undef _STATISTIC_UNLOCK_COUNT

	#define _STATISTIC_LOCK_COUNT			_MAIN_DEBUG()->dwLockCount++;
	#define _STATISTIC_UNLOCK_COUNT			_MAIN_DEBUG()->dwUnlockCount++;

	#undef _STATISTIC_PRINT
	#define _STATISTIC_PRINT				_printStatistics();
	static void		_printStatistics(void);

	#if 0
		#define FFAT_DEBUG_LOCK_PRINTF		FFAT_DEBUG_PRINTF("[MAIN_LOCK] "); FFAT_DEBUG_PRINTF
	#else
		#define FFAT_DEBUG_LOCK_PRINTF(_msg)
	#endif
#endif
// debug end


/**
 * This function initializes FFAT filesystem.
 *
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREADY	: already initialized
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_main_init(t_boolean bForce)
{
	_InitStatus		dwInitState;
	ComCxt			stCxt;			// a temporary context
	FFatErr			r;

	// check FFAT init state
	if ((FFAT_IS_INITIALIZED() == FFAT_TRUE) && (bForce == FFAT_FALSE))
	{
		return FFAT_EINIT_ALREADY;
	}

// debug begin
#ifdef FFAT_DEBUG
	FFAT_MEMSET(_MAIN_DEBUG(), 0x00, sizeof(_MainDebug));
#endif
// debug end

	dwInitState = _INIT_NONE;

	// initialize base library
	r = EssPStack_Init();
	IF_UK (r != ESS_OK)
	{
		FFAT_LOG_PRINTF((_T("fail to init PStack")));
		return r;
	}

	// initializes main structure
	FFAT_MEMSET(&gstFFatMain, 0x00, sizeof (FFatMainInfo));

// debug begin
//	ESS_DebugInit(FFAT_PRINTF, FFAT_GETCHAR);
// debug end

	// check size of internal & external structures
	FFAT_ASSERTP(sizeof(Vol) == sizeof(FFatVol), (_T("Incorrect Vol struct size, Vol/FFatVol:%u/%u \n"), sizeof(Vol), sizeof(FFatVol)));
	FFAT_ASSERTP(sizeof(Node) == sizeof(FFatNode), (_T("Incorrect Node struct size, Node/FFatNode:%d/%d \n"), sizeof(Node), sizeof(FFatNode)));

	r = ffat_al_init();
	FFAT_ER(r, (_T("fail to init AL")));

	dwInitState |= _INIT_AL;

	r = FFAT_MAIN_GET_FREE_LOCK_FOR_MAIN(&(gstFFatMain.pLock));
	FFAT_EO(r, (_T("Fail to get lock for FFAT Main")));

	r = FFAT_MAIN_LOCK();
	FFAT_EO(r, (_T("fail to lock FFatMain")));

	r = ffat_common_init();
	FFAT_EO(r, (_T("fail to init FFatCommon")));
	dwInitState |= _INIT_COMMON;

	// initializes all modules
	r = ffat_dir_init();
	FFAT_EO(r, (_T("fail to init FFatDir")));
	dwInitState |= _INIT_DIR;

	r = ffat_file_init();
	FFAT_EO(r, (_T("Fail to init FFatFile")));
	dwInitState |= _INIT_FILE;

	r = ffat_node_init();
	FFAT_EO(r, (_T("fail to init FFatNode")));
	dwInitState |= _INIT_NODE;

	r = ffat_vol_init();
	FFAT_EO(r, (_T("Fail to init FFatVol")));
	dwInitState |= _INIT_VOL;

	r = FFATFS_Init(bForce);
	FFAT_EO(r, (_T("fail to init FFatfs")));
	dwInitState |= _INIT_FFATFS;

	r = ffat_addon_init(bForce);
	FFAT_EO(r, (_T("fail to init FFatAddon")));
	dwInitState |= _INIT_ADDON;

	_initFSCtlCmdTable();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	// register callback
	r = _registerCallback(&stCxt);
	if (r < 0)
	{
		ffat_cxt_delete(&stCxt);	// always return FFAT_OK
		FFAT_PRINT_PANIC((_T("fail to register callback function")));

		goto out;
	}
	
	r = ffat_cxt_delete(&stCxt);
	FFAT_EO(r, (_T("fail to delete context")));

	gstFFatMain.dwFlag |= FFAT_MAIN_INIT;
	r = FFAT_OK;

out:
	// unlock main
	r |= FFAT_MAIN_UNLOCK();

	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("There is some error while initializing FFAT !!")));
		_terminate(FFAT_FALSE, dwInitState);
	}

	return r;
}


/**
 * This function terminates FFAT filesystem.
 * It does not stop termination even if there is some errors while terminating.
 * 
 * @author		DongYoung Seo
 * @version		JUL-10-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_main_terminate()
{
	return _terminate(FFAT_TRUE, _INIT_ALL);
}


/**
 * return the information of FFAT MAIN module
 * 
 * @author		DongYoung Seo
 * @version		JUL-10-2006 [DongYoung Seo] First Writing.
 */
FFatMainInfo*
ffat_main_getMainInfo(void)
{
	return &gstFFatMain;
}


/**
 * This function controls FFAT filesystem
 * 
 * lock이 필요할 경우 각각의 command를 처리하는 부분에서 수행하도록 한다.
 *
 * @param		dwCmd		: filesystem control command
 * @param		pParam0		: parameter 0
 * @param		pParam1		: parameter 1
 * @param		pParam2		: parameter 2
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-25-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_main_fsctl(FFatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	if (FFAT_IS_FSCTL_CMD_FOR_ADDON(dwCmd))
	{
		return ffat_addon_fsctl(dwCmd, pParam0, pParam1, pParam2, pCxt);
	}

	if ((dwCmd > 0) && (dwCmd <= FFAT_FSCTL_CORE_END))
	{
		return pFSCtlCmdTable[(dwCmd & FFAT_FSCTL_CORE_MASK)- 1](pParam0, pParam1, pParam2, pCxt);
	}

// debug begin
#ifdef FFAT_DEBUG
	if (dwCmd & FFAT_FSCTL_DEBUG_BASE)
	{
		return _debugFSCtl(dwCmd, pParam0, pParam1, pParam2, pCxt);
	}

	if (dwCmd & FFAT_FSCTL_ADDON_DEBUG_BASE)
	{
		return ffat_addon_fsctl(dwCmd, pParam0, pParam1, pParam2, pCxt);
	}
#endif
// debug end

	return FFAT_ENOSUPPORT;
}


/**
* This function is a callback function for cache 
* 
* @param	pVolInfo		: FFATFS volume information
* @param	dwSector		: sector number
* @param	dwFlag			: cache flag
* @param	pCxt			: [IN] context of current operation
* @author	DongYoung Seo
* @version	JUL-25-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_main_cacheCallBack(struct _FatVolInfo* pVolInfo, t_uint32 dwSector,
						FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	Vol*		pVol;
	FFatErr		r;

	FFAT_ASSERT(pVolInfo);

	_STATISTIC_CACHE_CALLBACK

	pVol = ESS_GET_ENTRY(pVolInfo, Vol, stVolInfo);

	r = ffat_addon_cacheCallBack(pVol, dwSector, dwFlag, pCxt);

	return r;
}


/**
* This function locks FFAT CORE (Critical sector)
*
* @param		pCxt		: [IN] context of current operation
*
* @author		DongYoung Seo
* @version		JAN-16-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_core_lock(ComCxt* pCxt)
{
	FFatErr		r;

	r = FFATFS_Lock(pCxt);
	FFAT_ER(r, (_T("fail to lock FFAT CORE")));

	pCxt->dwLockCore++;

	pCxt->dwFlag |= COM_FLAG_CORE_LOCKED;

	return FFAT_OK;
}



/**
* This function unlocks FFAT CORE (Critical sector)
*
* @param		pCxt		: [IN] context of current operation
*
* @author		DongYoung Seo
* @version		JAN-16-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_core_unlock(ComCxt* pCxt)
{
	FFatErr		r;

	if (pCxt->dwFlag & COM_FLAG_CORE_LOCKED)
	{
		FFAT_ASSERT(pCxt->dwLockCore > 0);

		if (--pCxt->dwLockCore == 0)
		{
			pCxt->dwFlag &=(~COM_FLAG_CORE_LOCKED);
		}

		r = FFATFS_UnLock(pCxt);
		FFAT_ER(r, (_T("fail to unlock FFAT CORE")));
	}
	else
	{
		FFAT_ASSERT(pCxt->dwLockCore == 0);
	}

	return FFAT_OK;
}


//=============================================================================
//
//	Static functions
//


/**
 * This function terminates FFAT filesystem.
 * It does not stop termination even if there is some errors while terminating.
 * (Policy : FFAT must be terminated when this function is called 
 *		because filesystem termination is target power off time)
 * 
 * @param		bCheckInit	: [IN] check FFAT init state
 * @param		dwInitState	: [IN] initialized state
 * @return		FFAT_OK			: success
 * @return		FFAT_EINIT		: FFAT is not initialized
 * @return		else			: there is one or more error while terminating.
 * @author		DongYoung Seo
 * @version		JUL-10-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_terminate(t_boolean bCheckInit, _InitStatus dwInitState)
{
	t_int32		r = FFAT_OK;

	if (bCheckInit)
	{
		// check initialization state
		IF_UK (FFAT_IS_INITIALIZED() == FFAT_FALSE)
		{
			return FFAT_EINIT;
		}
	}

	if (dwInitState & _INIT_AL)
	{
		// lock FFAT core
		r = FFAT_MAIN_LOCK();
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Fail to lock FFAT MAIN")));
			return r;
		}
	}

	// terminate FFatAddon
	// It should be terminated before FFatfs termination.
	if (dwInitState & _INIT_ADDON)
	{
		r |= ffat_addon_terminate(NULL);
	}

	// terminate FFATFS()
	if (dwInitState & _INIT_FFATFS)
	{
		r |= FFATFS_Terminate();
	}

	// terminate FFatVol
	if (dwInitState & _INIT_VOL)
	{
		r |= ffat_vol_terminate();
	}

	// terminate FFatNode
	if (dwInitState & _INIT_NODE)
	{
		r |= ffat_node_terminate();
	}

	// terminate FFatFile
	if (dwInitState & _INIT_FILE)
	{
		r |= ffat_file_terminate();
	}

	// terminate FFatDir
	if (dwInitState & _INIT_DIR)
	{
		r |= ffat_dir_terminate();
	}

	if (dwInitState & _INIT_COMMON)
	{
		r |= ffat_common_terminate();
	}

	if (dwInitState & _INIT_AL)
	{
		r |= FFAT_MAIN_UNLOCK();
		r |= FFAT_LOCK_RELEASE(&(gstFFatMain.pLock));

		r |= ffat_al_terminate();
	}

	gstFFatMain.dwFlag = FFAT_MAIN_NONE;

	_STATISTIC_PRINT
	return r;
}


/**
 * This function initialize fsctl command table
 * It does not stop termination even if there is some errors while terminating.
 * 
 * @author		DongYoung Seo
 * @version		JUL-10-2006 [DongYoung Seo] First Writing.
 */
static void
_initFSCtlCmdTable(void)
{
	FFatFSCtlCmd dwMask = ~FFAT_FSCTL_BASE;

	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_VOL_IS_FAT32) -1]			= _fsctlIsVolFAT32;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_GET_ROOT_CLUSTER_NO) -1]	= _fsctlGetRootClusterNo;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_GET_ROOT) -1]				= _fsctlGetRootNode;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_SYNC_VOL) -1]				= _fsctlSyncVol;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_IS_VALID_BOOTSECTOR) -1]	= _fsctlIsValidBootSector;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_ADD_CACHE) -1]				= _fsctlAddCache;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_REMOVE_CACHE) -1]			= _fsctlRemoveCache;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_CHK_CACHE) -1]				= _fsctlChkCache;
	pFSCtlCmdTable[(dwMask & FFAT_FSCTL_LDEV_IO) -1]				= _fsctlLDevIO;
}


/**
 * This function checks the volume is FAT32 or not
 * 
 * @param		pParam0	: FFatVol pointer
 * @param		pParam1	: cluster number storage pointer
 * @param		pParam2	: Don't care
* @param		pCxt	: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlIsVolFAT32(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	t_boolean*		pBool;

	IF_UK (FFAT_IS_INITIALIZED() == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("FFAT is not initialized")));
		return FFAT_EINIT;
	}

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	pBool = pParam1;
	*pBool = VOL_IS_FAT32((Vol*)pParam0);

	return FFAT_OK;
}


/**
 * This function get root cluster number from volume information
 * 
 * @param		pParam0	: FFatVol pointer
 * @param		pParam1	: cluster number storage pointer
 * @param		pParam2	: Don't care
 * @param		pCxt	: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		FFAT_EINIT	: FFAT is not initialized 
 * @return		FFAT_EACCESS: volume is not mounted
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlGetRootClusterNo(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	Vol*		pVol			= pParam0;
	t_uint32*	pdwClusterNo	= pParam1;

	IF_UK (FFAT_IS_INITIALIZED() == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("FFAT is not initialized")));
		return FFAT_EINIT;
	}

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted ")));
		return FFAT_EACCESS;
	}

	*pdwClusterNo = NODE_C(VOL_ROOT(pVol));

	return FFAT_OK;
}


/**
 * This function get root node from volume information
 * 
 * @param		pParam0	: [IN] FFatVol pointer
 * @param		pParam1	: [IN/OUT] root node pointer
 * @param		pParam2	: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlGetRootNode(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	Vol*		pVol			= pParam0;
	Node*		pRootNode		= pParam1;

	IF_UK ((pVol == NULL) || (pRootNode == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted ")));
		return FFAT_EACCESS;
	}

	FFAT_MEMCPY(pRootNode, VOL_ROOT(pVol), sizeof(Node));

	return FFAT_OK;
}


/**
 * This function sync a volume
 * 
 * @param		pParam0	: [IN] volume pointer
 * @param		pParam1	: Don't care
 * @param		pParam2	: Don't care
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlSyncVol(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	Vol*		pVol			= pParam0;

	IF_UK (pVol == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	IF_UK (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("volume is not mounted ")));
		return FFAT_EACCESS;
	}

	return ffat_vol_sync(pVol, FFAT_TRUE, pCxt);
}


/**
 * Check the buffer for boot sector is valid or not
 * 
 * @param		pParam0		: [IN] boot sector storage.
 * @param		pParam1		: Don't care
 * @param		pParam2		: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @param		FFAT_OK			: the buffer is a boot sector
 * @param		FFAT_EINVALID	: the buffer is not a boot sector
 * @author		DongYoung Seo
 * @version		JAN-11-2007 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlIsValidBootSector(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	IF_UK (pParam0 == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	return FFATFS_IsValidBootSector((t_int8*)pParam0);
}


/**
* Add FFATFS cache
* 
* @param		pParam0		: [IN] structure pointer for FFatAddCache
* @param		pParam1		: Dont't care
* @param		pParam2		: Don't care
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK			: the buffer is a boot sector
* @param		FFAT_EINVALID	: the buffer is not a boot sector
* @param		else			: error
* @author		DongYoung Seo
* @version		MAR-28-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_fsctlAddCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatAddCache*		pAddCache;

	IF_UK (pParam0 == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	pAddCache = (FFatAddCache*)pParam0;

	return FFATFS_AddCache(pAddCache->pBuff, pAddCache->dwSize,
						pAddCache->dwSectorSize, pCxt);
}


/**
* Remove FFATFS cache
* 
* @param		pParam0		: [IN] structure pointer for FFatAddCache
* @param		pParam1		: Dont't care
* @param		pParam2		: Don't care
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK			: the buffer is a boot sector
* @param		FFAT_EINVALID	: the buffer is not a boot sector
* @param		else			: error
* @author		DongYoung Seo
* @version		MAR-28-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_fsctlRemoveCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatRemoveCache*		pRemoveCache;

	IF_UK (pParam0 == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	pRemoveCache = (FFatAddCache*)pParam0;

	return FFATFS_RemoveCache(pRemoveCache->pBuff, pCxt);
}


/**
* Check FFATFS cache exist or not
* 
* @param		pParam0		: [IN] structure pointer for FFatAddCache
* @param		pParam1		: Dont't care
* @param		pParam2		: Don't care
* @param		pCxt		: [IN] context of current operation
* @param		FFAT_OK		: the proper cache does not exist
* @param		FFAT_OK1	: the proper cache exists
* @param		else		: error
* @author		Soojeong Kim
* @version		AUG-09-2007 [Soojeong Kim] First Writing.
* @version		JAN-06-2009 [DongYoung Seo] change code to support nestle request
*/
static FFatErr
_fsctlChkCache(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	IF_UK (pParam0 == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	return FFATFS_ChkCache((FFatCheckCache*)pParam0, pCxt);
}


/**
 * Sector IO toward logical device
 * 
 * @param		pParam0		: [IN] volume pointer
 * @param		pParam1		: [IN] Sector IO structure pointer
 * @param		pParam2		: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @param		FFAT_OK			: Sector IO Success
 * @param		FFAT_EINVALID	: Invalid parameter
 * @param		FFAT_ENOSUPPORT	: Not support operation
 * @param		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlLDevIO(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	Vol*			pVol;
	FFatLDevIO*		pLDevIO;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	pVol		= (Vol*)pParam0;
	pLDevIO		= (FFatLDevIO*)pParam1;

	return ffat_misc_ldevIO(pVol, pLDevIO, pCxt);
}


/**
 *  Register call back functions
 * 
 * @param		pCxt			: [IN] context of current operation
 * @param		FFAT_OK			: Sector IO Success
 * @param		else			: error
 * @author		DongYoung Seo
 * @version		MAY-29-2007 [DongYoung Seo] First Writing.
 */
static FFatErr
_registerCallback(ComCxt* pCxt)
{
	FFatErr		r;

	r = FFATFS_RegisterCacheCallback(ffat_main_cacheCallBack, FFAT_TRUE, pCxt);
	FFAT_EO(r, (_T("fail to register callback function")));

out:
	return r;
}

//
//	End of Static functions
//
//=============================================================================



// debug begin
#ifdef FFAT_DEBUG

//=============================================================================
//
//	debug area
//


/**
 * This function locks FFAT Main structure for global lock
 *
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_main_lock(void)
{
	FFatErr		r;
	r = _MAIN_LOCK();

	_STATISTIC_LOCK_COUNT

	FFAT_ASSERT(_MAIN_DEBUG()->dwLockCount == (_MAIN_DEBUG()->dwUnlockCount + 1));

	return r;
}


/**
 * This function release lock for FFAT Main structure for global lock
 *
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_main_unlock(void)
{
	_STATISTIC_UNLOCK_COUNT

	FFAT_ASSERT(_MAIN_DEBUG()->dwLockCount == _MAIN_DEBUG()->dwUnlockCount);

	return _MAIN_UNLOCK();
}


	void	ffat_main_statisticMount(void)				{_MAIN_DEBUG()->dwMount++;}			// mount count
	void	ffat_main_statisticUmount(void)				{_MAIN_DEBUG()->dwUmount++;}			// umount count
	void	ffat_main_statisticGetVolumeStatus(void)	{_MAIN_DEBUG()->dwGetVolumeStatus++;}	// FFAT_GetVolumeStatus() count
	void	ffat_main_statisticSetVolumeLabel(void)		{_MAIN_DEBUG()->dwSetVolumeLabel++;}	// set volume name count
	void	ffat_main_statisticGetVoluemName(void)		{_MAIN_DEBUG()->dwGetVoluemName++;}	// get volume name count
	void	ffat_main_statisticLookup(void)				{_MAIN_DEBUG()->dwLookup++;}			// lookup count
	void	ffat_main_statisticFSCtl(void)				{_MAIN_DEBUG()->dwFSCtl++;}			// fsctl count
	void	ffat_main_statisticMakedir(void)			{_MAIN_DEBUG()->dwMakedir++;}			// make directory count
	void	ffat_main_statisticRemovedir(void)			{_MAIN_DEBUG()->dwRemovedir++;}		// remove directory count
	void	ffat_main_statisticReaddir(void)			{_MAIN_DEBUG()->dwReaddir++;}			// readdir count
	void	ffat_main_statisticCreate(void)				{_MAIN_DEBUG()->dwCreate++;}			// file creation count
	void	ffat_main_statisticOpen(void)				{_MAIN_DEBUG()->dwOpen++;}				// open count
	void	ffat_main_statisticClose(void)				{_MAIN_DEBUG()->dwClose++;}				// close count
	void	ffat_main_statisticRead(void)				{_MAIN_DEBUG()->dwRead++;}				// read count
	void	ffat_main_statisticWrite(void)				{_MAIN_DEBUG()->dwWrite++;}			// write count
	void	ffat_main_statisticUnlink(void)				{_MAIN_DEBUG()->dwUnlink++;}			// unlink count
	void	ffat_main_statisticSecureUnlink(void)		{_MAIN_DEBUG()->dwSecureUnlink++;}		// secure unlink count
	void	ffat_main_statisticChangeSize(void)			{_MAIN_DEBUG()->dwChangeSize++;}		// change size count
	void	ffat_main_statisticRename(void)				{_MAIN_DEBUG()->dwRename++;}			// rename count
	void	ffat_main_statisticSetNodeStatus(void)		{_MAIN_DEBUG()->dwSetNodeStatus++;}	// set node status count
	void	ffat_main_statisticGetNodeStatus(void)		{_MAIN_DEBUG()->dwGetNodeStatus++;}	// get node status count
	void	ffat_main_statisticSyncNode(void)			{_MAIN_DEBUG()->dwSyncNode++;}			// node sync count
	void	ffat_main_statisticSyncVol(void)			{_MAIN_DEBUG()->dwSyncVol++;}			// volume sync count
	void	ffat_main_statisticSync(void)				{_MAIN_DEBUG()->dwSync++;}				// sync count
	void	ffat_main_statisticGetNodeClusters(void)	{_MAIN_DEBUG()->dwGetNodeClusters++;}	// Get node clusters count


	/**
	 * This function controls FFAT filesystem for debug
	 * 
	 * @param		dwCmd		: file system control command
	 * @param		pParam0		: parameter 0
	 * @param		pParam1		: parameter 1
	 * @param		pParam2		: parameter 2
	 * @param		pCxt		: [IN] context of current operation
	 * @author		DongYoung Seo
	 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugFSCtl(FFatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
	{
		if (dwCmd == FFAT_FSCTL_LOG_RECOVERY_TEST)
		{
			return _debugFSCtlILogRecoveryTestSet(*(FFatLogRecoveryTestFlag*)pParam0);
		}
		else if (dwCmd == FFAT_FSCTL_INVALIDATE_FCCH)
		{
			return _debugFSCtlIFCCH(pParam0, pParam1, pParam2, pCxt);
		}
		else if (dwCmd == FFAT_FSCTL_DEBUG_BASE)
		{
			return _debugBase((Vol*)pParam0, (ComDebug*)pParam1, pCxt);
		}

		return FFAT_EINVALID;
	}


	/**
	 * This function sets log recovery type
	 * 
	 * @param		dwType	: log recovery type
	 * @author		DongYoung Seo
	 * @version		JAN-22-2007 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugFSCtlILogRecoveryTestSet(FFatLogRecoveryTestFlag dwType)
	{
		return _debugLogRecovery(dwType, FFAT_TRUE, FFAT_FALSE);
	}


	/**
	 * This function sets log recovery type
	 * 
	 * @param		dwType	: log recovery type
	 * @author		DongYoung Seo
	 * @version		JAN-22-2007 [DongYoung Seo] First Writing.
	 */
	FFatErr
	ffat_main_LogRecoveryTestCheck(FFatLogRecoveryTestFlag dwType)
	{
		return _debugLogRecovery(dwType, FFAT_FALSE, FFAT_TRUE);
	}


	/**
	 * This function sets log recovery type
	 * 
	 * @param		dwType	: log recovery type
	 * @param		bSet	: set log recovery
	 * @param		bCheck	: log recovery type
	 * @author		DongYoung Seo
	 * @version		JAN-22-2007 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugLogRecovery(FFatLogRecoveryTestFlag dwType, t_boolean bSet, t_boolean bCheck)
	{
		static FFatLogRecoveryTestFlag	dwLRType;

		if (bSet == FFAT_TRUE)
		{
			dwLRType = dwType;
		}

		if (bCheck == FFAT_TRUE)
		{
			if (dwType == FFAT_LRT_NONE)
			{
				FFAT_LOG_PRINTF((_T("Invalid LogRecoveryType")));
				return FFAT_EINVALID;
			}

			if (dwLRType == dwType)
			{
				FFAT_ASSERTP(0, (_T("Check log recovery here, Power off !!")));
				return -1;
			}
		}

		return FFAT_OK;
	}


	/**
	* This function invalidate free cluster cache information
	* 
	* @param		pVol	: volume pointer
	* @param		pCxt		: [IN] context of current operation
	* @author		DongYoung Seo
	* @version		SEP-01-2006 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugFSCtlIFCCH(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
	{
		Vol*		pVol;
		FFatErr		r;

		pVol = pParam0;

		if (pVol == NULL)
		{
			FFAT_LOG_PRINTF((_T("Invalid volume point")));
			return FFAT_EINVALID;
		}

		if (VOL_IS_MOUNTED(pVol) == FFAT_FALSE)
		{
			FFAT_LOG_PRINTF((_T("volume is not mounted")));
			return FFAT_EACCESS;
		}

		r = FFATFS_FSCtl(FAT_FSCTL_IVIC, VOL_VI(pVol), NULL, NULL);
		FFAT_ER(r, (_T("fail to invalid FCC on FFATFS")));

		r= ffat_addon_fsctl(FFAT_FSCTL_INVALIDATE_FCCH, pVol, NULL, NULL, pCxt);
		FFAT_ER(r, (_T("fail to invalid FCC on addon module")));

		return FFAT_OK;
	}


	/**
	* This function is a base function for debug fsctl
	* 
	* @param		pVol		: [IN] volume pointer
	* @param		pComDebug	: [IN] debug context
	* @param		pCxt		: [IN] context of current operation
	* @author		DongYoung Seo
	* @version		SEP-01-2006 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugBase(Vol* pVol, ComDebug* pDebugInfo, ComCxt* pCxt)
	{
		FFatErr		r;

		if (pDebugInfo->dwCmd == FFAT_FSCTL_DEBUG_REMOUNT)
		{
			r = _debugRemount(pVol, pDebugInfo->pData, pCxt);
		}
		else
		{
			r = FFAT_ENOSUPPORT;
		}

		return r;
	}


	/**
	* unit test for remount operation
	* 
	* @param		pVol		: [IN] volume pointer
	* @param		pComDebug	: [IN] debug context
	* @param		pCxt		: [IN] context of current operation
	* @author		DongYoung Seo
	* @version		SEP-01-2006 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_debugRemount(Vol* pVol, void* pDebug, ComCxt* pCxt)
	{
		typedef struct 
		{
			FFatMountFlag		dwFlag;
		} _DebugRemount;

		return ffat_vol_remount(pVol, &(((_DebugRemount*)pDebug)->dwFlag), pCxt);
	}


	static void
	_printStatistics(void)
	{
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
		FFAT_DEBUG_PRINTF((_T("=======    FFAT API CALL STATISTICS  =======================\n")));
		FFAT_DEBUG_PRINTF((_T("============================================================\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Mount(): ", 			_MAIN_DEBUG()->dwMount));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Umount(): ", 			_MAIN_DEBUG()->dwUmount));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_GetVolumeStatus(): ",	_MAIN_DEBUG()->dwGetVolumeStatus));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_SetVolumeLabel(): ", 	_MAIN_DEBUG()->dwSetVolumeLabel));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_GetVoluemName(): ", 	_MAIN_DEBUG()->dwGetVoluemName));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Lookup(): ", 			_MAIN_DEBUG()->dwLookup));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_FSCtl(): ", 			_MAIN_DEBUG()->dwFSCtl));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Makedir(): ", 		_MAIN_DEBUG()->dwMakedir));	
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Removedir(): ", 		_MAIN_DEBUG()->dwRemovedir));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Readdir(): ", 		_MAIN_DEBUG()->dwReaddir));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Create(): ", 			_MAIN_DEBUG()->dwCreate));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Open(): ", 			_MAIN_DEBUG()->dwOpen));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Close(): ", 			_MAIN_DEBUG()->dwClose));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Read(): ", 			_MAIN_DEBUG()->dwRead));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Write(): ", 			_MAIN_DEBUG()->dwWrite));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Unlink(): ", 			_MAIN_DEBUG()->dwUnlink));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_SecureUnlink(): ", 	_MAIN_DEBUG()->dwSecureUnlink));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_ChangeSize(): ", 		_MAIN_DEBUG()->dwChangeSize));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Rename(): ", 			_MAIN_DEBUG()->dwRename));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_SetNodeStatus(): ", 	_MAIN_DEBUG()->dwSetNodeStatus));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_GetNodeStatus(): ", 	_MAIN_DEBUG()->dwGetNodeStatus));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_SyncNode(): ", 		_MAIN_DEBUG()->dwSyncNode));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_SyncVol(): ", 		_MAIN_DEBUG()->dwSyncVol));
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_Sync(): ", 			_MAIN_DEBUG()->dwSync));	
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FFAT_GetNodeClusters(): ",	_MAIN_DEBUG()->dwGetNodeClusters));


		FFAT_DEBUG_PRINTF((_T("\n")));
		FFAT_DEBUG_PRINTF((_T("=======    MISC CALL STATICS     =======================\n")));
		FFAT_DEBUG_PRINTF((_T("\n")));

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Main Lock Count: ", 		_MAIN_DEBUG()->dwLockCount));		
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "Main Unlock Count: ", 		_MAIN_DEBUG()->dwUnlockCount));		

		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "CacheCallBack Count: ", 	_MAIN_DEBUG()->dwCacheCallBack));		
		FFAT_DEBUG_PRINTF((_T(" %30s %d\n"), "FatCallBack Count: ", 		_MAIN_DEBUG()->dwFatCallBack));		

		FFAT_DEBUG_PRINTF((_T("============================================================\n")));
	}
#endif

// debug end

