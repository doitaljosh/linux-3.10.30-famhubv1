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
 * @file		ffat_addon_api.c
 * @brief		The file implements APIs for FFatAddon module
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-08-2006 [DongYoung Seo] First writing
 * @version		FEB-02-2009 [DongYoung Seo] Remove code ofr user fast seek
 * @version		JAN-11-2010 [ChunUm Kong] Modifying comment (English/Korean)
 * @see			None
 */

// includes

#include "ess_types.h"
#include "ess_math.h"

#include "ffat_config.h"
#include "ffat_types.h"
#include "ffat_errno.h"
#include "ffat_common.h"

#include "ffat_addon_types.h"
#include "ffat_addon_types_internal.h"

#include "ffat_main.h"
#include "ffat_node.h"
#include "ffat_vol.h"
#include "ffat_misc.h"
#include "ffat_file.h"

#include "ffatfs_api.h"
#include "ffatfs_types.h"

#include "ffat_addon_misc.h"
#include "ffat_addon_fastseek.h"
#include "ffat_addon_nand.h"
#include "ffat_addon_chkdsk.h"
#include "ffat_addon_log.h"
#include "ffat_addon_fcc.h"
#include "ffat_addon_format.h"
#include "ffat_addon_xattr.h"
#include "ffat_addon_spfile.h"
#include "ffat_addon_xde.h"
#include "ffat_addon_debug.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_API)

//#define _API_DEBUG

#ifdef FFAT_DEBUG
	#define CHKDSK_AFTER_MOUNT
#endif


// ADDON does not have it's own lock to avoid dead lock status
// There is two path to come into ADDON module.
//	1. CORE invokes an ADDON API
//	2. FFATFS callback invokes ADDON through CORE
// But there is dead lock condition at below case
//		When TASK A is running in the ADDON, it invokes FFATFS. (ADDON->FFATFS)
//		When TASK B is running at the FFATFS, it read data from cache. cache calls callback. 
//				(FFATFS->ADDON).
// It must be occur in real environment when FFATFS and ADDON has different lock
//
// So I share lock with FFATFS in current version
//
// DO NOT DISABLE BELOW DEFINITION.
#define _SHARE_LOCK_WITH_FFATFS


#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	#define _GET_LOCK(_pCxt)			_getLock(_pCxt)
	#define _PUT_LOCK(_pCxt)			_putLock(_pCxt)
#else
	#define _GET_LOCK(_pCxt)			FFAT_OK
	#define _PUT_LOCK(_pCxt)			FFAT_OK
#endif


// array for function pointer of FSCTL
static PFN_FSCTL	_pCmdTable[(FFAT_FSCTL_ADDON_END & ~(FFAT_FSCTL_ADDON_BASE)) + 1];

// internal functions
static FFatErr	_initCmdTable(PFN_FSCTL* _pCmdTable);

static FFatErr	_fsctlReaddirStat(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlReaddirGetNode(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlCleanDir(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlCleanNand(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlReaddirUnlink(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlDEC(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlChkDsk(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlFormat(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlExtendedAttribute(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlGetShortName(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlGetLongName(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
static FFatErr	_fsctlSetExtendedDE(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);

#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	static FFatErr	_getLock(ComCxt* pCxt);
	static FFatErr	_putLock(ComCxt* pCxt);
#endif

#ifdef FFAT_CMP_NAME_MULTIBYTE
	static void	_checkUnderScore(Node* pNode, t_wchar* psName, t_int32 dwLen);
#endif

// debug begin
#ifdef FFAT_DEBUG
	static FFatErr	_debugFSCtl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1,
							void* pParam2, ComCxt* pCxt);
	static FFatErr	_fsctlFCC(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);
	static FFatErr	_fsctlIFCCH(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt);

	static void		_external_struct_size_check(void);
	static void		_addon_configuration_check(void);
#endif
// debug end

static AddonMain		_stAddonMain;				// main structure of ADDON

#ifdef _API_DEBUG
	#define		FFAT_DEBUG_ADDON_LOCK_PRINTF		FFAT_DEBUG_PRINTF("[ADDON_LOCK] "); FFAT_DEBUG_PRINTF
#else
	#define		FFAT_DEBUG_ADDON_LOCK_PRINTF(_msg)
#endif


/**
 * This function initializes FFatAddon module
 *
 * @param		bForce		: [IN] initialize by force
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		FEB-02-2009 [DongYoung Seo] Add ASSERT to check sizeof AddonNode and AddonVol
 */
FFatErr
ffat_addon_init(t_boolean bForce)
{
	t_int32		dwCmdCount;
	ComCxt		stCxt;			// a dummy context for lock status
	FFatErr		r;

	FFAT_ASSERT(sizeof(AddonVol) == (FFAT_ADDON_VOL_SIZE * sizeof(t_int32)));
	FFAT_ASSERT(sizeof(((Vol*)(0))->stAddon) == (FFAT_ADDON_VOL_SIZE * sizeof(t_int32)));

	FFAT_ASSERT(sizeof(AddonNode) == (FFAT_ADDON_NODE_SIZE * sizeof(t_int32)));
	FFAT_ASSERT(sizeof(((Node*)(0))->stAddon) == (FFAT_ADDON_NODE_SIZE * sizeof(t_int32)));

	dwCmdCount = FFAT_FSCTL_ADDON_END & ~(FFAT_FSCTL_ADDON_BASE);

	FFAT_ASSERT((dwCmdCount + 1) == (sizeof(_pCmdTable) / sizeof(void*)));

	_initCmdTable(_pCmdTable);

// debug begin
#ifdef FFAT_DEBUG
	_external_struct_size_check();
	_addon_configuration_check();
#endif
// debug end

	FFAT_MEMSET(&stCxt, 0x00, sizeof(ComCxt));

	stCxt.dwFlag	= COM_FLAG_NONE;	// no flag
	stCxt.pPStack	= NULL;				// no PSTACK

	FFAT_MEMSET(&_stAddonMain, 0x00, sizeof(_stAddonMain));

#ifndef _SHARE_LOCK_WITH_FFATFS
	r = FFAT_GET_FREE_LOCK(&_stAddonMain.pLock);
	FFAT_ER(r, (_T("fail to get a free lock")));
#endif
	_stAddonMain.dwRefCount = 0;

	r = _GET_LOCK(&stCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	ffat_debug_init();	// don't care error

	r = ffat_log_init();
	FFAT_EO(r, (_T("fail to initialize log module ")));

	r = ffat_fastseek_init(bForce);
	FFAT_EO(r, (_T("fail to initialize fast seek")));

	r = ffat_hpa_init();
	FFAT_EO(r, (_T("fail to initialize HPA")));

	r = ffat_fcc_init();
	FFAT_EO(r, (_T("fail to initialize Free Cluster Cache")));

	r = ffat_dec_init();
	FFAT_EO(r, (_T("fail to initialize DEC")));

	r = ffat_spfile_init();
	FFAT_ER(r, (_T("fail to initialize Symlink")));

	r = _PUT_LOCK(&stCxt);
	FFAT_EO(r, (_T("fail to unlock ADDON")));

	return FFAT_OK;

out:
	ffat_addon_terminate(&stCxt);
	r |= _PUT_LOCK(&stCxt);

	return r;
}


/**
 * This function terminates FFatAddon module
 *
 * @param		pCxt		: [IN] current context
 *								may be NULL
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_terminate(ComCxt* pCxt)
{
	ComCxt		stCxt;			// a temporary context
	FFatErr		r = FFAT_OK;

	if (pCxt == NULL)
	{
		FFAT_MEMSET(&stCxt, 0x00, sizeof(ComCxt));
		pCxt = &stCxt;
	}

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r |= ffat_dec_terminate();

	r |= ffat_fcc_terminate();

	r |= ffat_hpa_terminate();

	r |= ffat_log_terminate();

	r |= ffat_fastseek_terminate();

	ffat_debug_terminate();		// don't care error

	r |= _PUT_LOCK(pCxt);

#ifndef _SHARE_LOCK_WITH_FFATFS
	r |= FFAT_RELEASE_LOCK(&_MAIN()->pLock);
#endif

	return r;
}


/**
 * This function controls FFAT ADDON module
 *
 * @param		dwCmd		: [IN] filesystem control command, operation type
 * @param		pParam0		: [IN] parameter 0
 * @param		pParam1		: [IN] parameter 1
 * @param		pParam2		: [IN] parameter 2
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-21-2006 [DongYoung Seo] First Writing.
 */
FFatErr	
ffat_addon_fsctl(FFatFSCtlCmd dwCmd, void* pParam0,
				void* pParam1, void* pParam2, ComCxt* pCxt)
{

// debug begin
#ifdef FFAT_DEBUG
	if ((dwCmd & FFAT_FSCTL_DEBUG_BASE) ||
		(dwCmd & FFAT_FSCTL_ADDON_DEBUG_BASE))
	{
		return _debugFSCtl(dwCmd, pParam0, pParam1, pParam2, pCxt);
	}
#endif
// debug end

	if ((dwCmd <= FFAT_FSCTL_ADDON_BASE) || (dwCmd > FFAT_FSCTL_ADDON_END))
	{
		FFAT_LOG_PRINTF((_T("Not supported command for ADDON module")));
		return FFAT_ENOSUPPORT;
	}

	if (_pCmdTable[dwCmd & (~FFAT_FSCTL_ADDON_BASE)] == NULL)
	{
		//FFAT_LOG_PRINTF((_T("Not supported command for ADDON module")));
		return FFAT_ENOSUPPORT;
	}

	return _pCmdTable[dwCmd & (~FFAT_FSCTL_ADDON_BASE)](pParam0, pParam1, pParam2, pCxt);
}


/**
* This function is a callback function for cache 
* 
* @param	pVol			: volume information
* @param	dwSector		: sector number
* @param	dwFlag			: cache flag
* @param	pCxt			: [IN] context of current operation
* @author	DongYoung Seo
* @version	JUL-25-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_cacheCallBack(Vol* pVol, t_uint32 dwSector, FFatCacheFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	dwSector = dwSector;		// suppress warning
	dwFlag = dwFlag;			// suppress warning

	r = ffat_log_cacheCallBack(pVol, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* Sector IO toward logical device
* 
* @param		pVol			: [IN] volume pointer
* @param		pLDevIO			: [IN] Sector IO structure pointer
* @param		pCxt			: [IN] context of current operation
* @param		FFAT_OK			: Sector IO Success
* @param		FFAT_EINVALID	: Invalid parameter
* @param		else			: error
* @author		DongYoung Seo
* @version		MAY-29-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter pCxt

*/
FFatErr
ffat_addon_ldevIO(Vol* pVol, FFatLDevIO* pLDevIO, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLDevIO);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_hpa_ldevIO(pVol, pLDevIO, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


//=============================================================================
//
//	FUNCTION FOR VOLUME
//
//

/**
 * This function mount a volume
 * 이 함수는 mount operation의 마지막에 호출이 된다.
 * [en] this function is invoked mount operation at last. 
 * log recovery등 ADDON moudle에 포함되는 mount 관련 operation을 수행한다.
 * [en] this executes operations related mount including ADDON module, such as log recovery etc.
 *
 * @param		pVol		: volume pointer
 * @param		pdwFlag		: mount flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: Success
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 * @version		JAN-05-2007 [DongYoung Seo] Add Static Meta-data Area
 * @version		JAN-16-2009 [JeongWoo Park] Add the check code about HPA & XDE & EA mount flag
 * @version		MAY-13-2009 [JeongWoo Park] remove the ASSERT code about "RDONLY | HPA_CREATE"
 * @version		Aug-29-2009 [SangYoon Oh] Move HPA mount ahead of Log mount
 */
FFatErr
ffat_addon_afterMount(Vol* pVol, FFatMountFlag* pdwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	// notice !!! 
	// do not change this initialization sequence 

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_NO_LOG) ? ((*pdwFlag & (FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW)) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & (FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW)) ? ((*pdwFlag & FFAT_MOUNT_NO_LOG) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_CLEAN_NAND) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_LOG_INIT) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_HPA_REMOVE) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_ERASE_SECTOR) == 0) : FFAT_TRUE);

	// check mount flag
	if ((*pdwFlag & FFAT_MOUNT_HPA_MASK) &&
		(*pdwFlag & (FFAT_MOUNT_XDE | FFAT_MOUNT_XATTR)))
	{
		FFAT_LOG_PRINTF((_T("HPA mount flag can not be used with XDE | XATTR")));
		r = FFAT_ENOSUPPORT;
		goto out;
	}

	ffat_debug_mount(pVol, pCxt);	// don't care error

	// do not change order (ffat_log_mount()보다 먼저 해야 함)
	// [en] do not change order (this function must be located before ffat_log_mount())
	r = ffat_xde_mount(pVol, *pdwFlag, pCxt);
	FFAT_EO(r, (_T("Fail to mount Extend DE module")));

	// do not change order (ffat_log_mount()보다 먼저 해야 함)
	// [en] do not change order (this function must be located before ffat_log_mount())
	r = ffat_hpa_mount(pVol, *pdwFlag, pCxt);
	FFAT_EO(r, (_T("fail to mount HPA")));

	// log recovery.
	r = ffat_log_mount(pVol, pdwFlag, pCxt);
	FFAT_EO(r, (_T("fail to initialize log module")));

	// init DEC (ffat_log_mount()보다 나중에 해야 함)
	// [en] init DEC, do not change order (this function must be located after ffat_log_mount())
	r = ffat_dec_mount(pVol);
	FFAT_EO(r, (_T("Fail to init Directory Entry Cache")));

	// init FCC
	r = ffat_fcc_mount(pVol);
	FFAT_EO(r, (_T("fail to init Free Cluster Cache")));

	ffat_spfile_mount(pVol, *pdwFlag);	// void return
	r = ffat_ea_mount(pVol, *pdwFlag, pCxt);
	FFAT_EO(r, (_T("Fail to mount Extend Attr module")));

	r = ffat_nand_mount(pVol, *pdwFlag, pCxt);
	FFAT_EO(r, (_T("fail to mount ADDON nand module")));

	r = FFAT_OK;

#ifdef CHKDSK_AFTER_MOUNT
	r = ffat_addon_chkdsk(pVol, FFAT_CHKDSK_CHECK_ONLY, pCxt);
#endif

out:
	if (r != FFAT_OK)
	{
		ffat_fcc_umount(pVol, *pdwFlag, pCxt);

		// release HPA
		ffat_hpa_umount(pVol, pCxt);

		// release log
		ffat_log_umount(pVol, pCxt);

		ffat_dec_umount(pVol);

		ffat_debug_umount(pVol);
	}

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
*  re-mount a volume
*  This function changes operation move of a volume
*  Transaction On/Off and transaction type
*  set volume read only
*
*  This function is used on Linux BOX.
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
ffat_addon_remount(Vol* pVol, FFatMountFlag* pdwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);

	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_NO_LOG) ? ((*pdwFlag & (FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW)) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & (FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW)) ? ((*pdwFlag & FFAT_MOUNT_NO_LOG) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_CLEAN_NAND) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_LOG_INIT) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_HPA_CREATE) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_HPA_REMOVE) == 0) : FFAT_TRUE);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) ? ((*pdwFlag & FFAT_MOUNT_ERASE_SECTOR) == 0) : FFAT_TRUE);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_log_remount(pVol, pdwFlag, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * This function u-mount a volume
 * log recovery등 ADDON module에 포함되는 umount 관련 operation을 수행한다.
 * [en] this executes operations related umount including ADDON module, such as log recovery etc..
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwFlag		: [IN] un-mount flag
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_umount(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_fcc_umount(pVol, dwFlag, pCxt);
	FFAT_EO(r, (_T("fail to umount FCC")));

	r = ffat_hpa_umount(pVol, pCxt);
	FFAT_EO(r, (_T("fail to umount HPA")));

	// log recovery.
	r = ffat_log_umount(pVol, pCxt);
	FFAT_EO(r, (_T("fail to un-mount on log module")));

	r = ffat_fastseek_umount(pVol);
	FFAT_EO(r, (_T("fail to un-mount on GFS module")));

	r = ffat_dec_umount(pVol);
	FFAT_EO(r, (_T("fail to un-mount on GDEC module")));

	ffat_debug_umount(pVol);

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * get status of a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pStatus		: [OUT] volume information storage
 * @param		pBuff		: [IN] buffer pointer, may be NULL
 * @param		dwSize		: [IN] size of buffer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_DONE	: volume status gather operation success
 * @return		FFAT_OK		: just ok, nothing is done.
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		Aug-29-2009 [SangYoon Oh] Removed ASSERT code regarding HPA
 */
FFatErr
ffat_addon_getVolumeStatus(Vol* pVol, FFatVolumeStatus* pStatus, t_int8* pBuff,
							t_int32 dwSize, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_hpa_getVolumeStatus(pVol, pStatus, pCxt);
	if (r == FFAT_DONE)
	{
		goto out;
	}
	FFAT_EO(r, (_T("get fail to volume status of HPA")));

	r = ffat_fcc_getVolumeStatus(pVol, pBuff, dwSize, pCxt);
	FFAT_EO(r, (_T("get fail to volume status of HPA")));

out:
	// CHECK TOTAL CLUSTER COUNT
	FFAT_ASSERT(r >= 0 ? pStatus->stHPA.dwClusterCount <= VOL_CC(pVol) : FFAT_TRUE);
	// CHECK FREE CLUSTER COUNT
	FFAT_ASSERT(r >= 0 ? pStatus->dwFreeClusterCount < VOL_CC(pVol) : FFAT_TRUE);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * sync a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JAN-25-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_syncVol(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	dwCacheFlag = dwCacheFlag;		// suppress warning

	r = ffat_log_syncVol(pVol, pCxt);
	FFAT_EO(r, (_T("fail to log sync")));

	r = ffat_fcc_syncVol(pVol, dwCacheFlag, pCxt);

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* called after sync a volume
*
* @param		pVol		: [IN] volume pointer
* @param		dwCacheFlag	: [IN] flag for cache operation
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		else		: error
* @author		DongYoung Seo
* @version		JAN-25-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_afterSyncVol(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_log_afterSyncVol(pVol, dwCacheFlag, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


//
//	END OF FUNCTION FOR VOLUME
//
//=============================================================================


/**
 * Initialize a node structure for FFAT ADDON module
 * do not change value except Node.stAddon
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pNodeParent	: [IN] parent node pointer
 *								It may be NULL.
 * @param		pNodeChild	: [IN/OUT] child node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 * @history		DEC-07-2007 [InHwan Choi] apply to open unlink
 */
FFatErr
ffat_addon_initNode(Vol* pVol, Node* pNodeParent, Node* pNodeChild, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	pVol = pVol;		// suppress warning

	// reset flag
	NODE_ADDON_FLAG(pNodeChild) = ADDON_NODE_NONE;

	r = ffat_fastseek_initNode(pNodeChild);
	FFAT_EO(r, (_T("fail to init node - fast seek")));

	r = ffat_dec_initNode(pNodeChild);
	FFAT_EO(r, (_T("fail to init node for DEC")));

	r = ffat_hpa_initNode(pNodeParent, pNodeChild);
	FFAT_EO(r, (_T("fail to init node for HPA")));

	r = ffat_log_initNode(pNodeChild);
	FFAT_EO(r, (_T("fail to init node for LOG")));

	r = ffat_ea_initNode(pNodeChild);
	FFAT_EO(r, (_T("fail to init node for EA")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * terminate node structure
 * do not change value except Node.stAddon
 *
 * Node의 사용이 종료될때 호출된다.
 * [en] this is invoked terminating node's usage
 * ex)close operation
 *
 * @param		pNode		: [IN/OUT] child node pointer
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-26-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_terminateNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r |= ffat_fastseek_terminateNode(pNode);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * open a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing.
 * @version		MAR-18-2009 [GwangOk Go] don't check log file (allow to read log file)
 */
FFatErr
ffat_addon_openNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// check the node is log file
	//r = ffat_addon_isAccessable(pNode, NODE_ACCESS_OPEN); // don't check log file (allow to read log file)

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * close a node
 * All resource for a Node should be release in this function.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_afterCloseNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		r = ffat_dec_deallocateGDEC(pNode);
	}

	r |= _PUT_LOCK(pCxt);

	return r;
}


#if 0
/**
 * sync a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-28-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_syncNode(void)
{
	// nothing to do in current version
	return FFAT_OK;
}
#endif


/**
* called after sync a node
*
* @param		pCxt	: context of current operation
* @return		FFAT_OK		: success
* @author		DongYoung Seo
* @version		NOV-28-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_afterSyncNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// record the confirm log for prevent undo
	r = ffat_log_confirm(pNode, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


//=============================================================================
//
//	FAT/Cluster 관련
//  [en] related FAT/Cluster
//

/**
 * get cluster number of offset or adjacent
 * This function uses FastSeek Module
 *
 * *pdwCluser is an adjacent one when the dwOffset is not same is *pdwOffset,
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwOffset		: [IN] offset for the cluster
 * @param		pdwCluster		: [OUT] cluster number storage
 * @param		pdwOffset		: [OUT] offset of *pdwCluster
 * @param		pPAL			: [OUT] previous access location
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success but not completed.
 * @return		FFAT_DONE		: success and not thing to do additional work.
 * @return		negative		: error
 * @author		DongYoung Seo
 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_getClusterOfOffset(Node* pNode, t_uint32 dwOffset, t_uint32* pdwCluster,
								t_uint32* pdwOffset, NodePAL* pPAL, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_fastseek_getClusterOfOffset(pNode, dwOffset, pdwCluster, pdwOffset, pPAL, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * get free clusters without FAT update
 *
 * @param		pNode			: [IN] Node pointer
 * @param		dwCount			: [IN] free cluster request count
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwHint			: [IN] free cluster hint, lookup start cluster
 * @param		pdwFreeCount	: [IN] allocated free cluster count
 *										this has free cluster count on FFAT_ENOSPC error.
 *										this value is 0 when there is not enough free cluster
 * @param		dwAllocFlag		: [IN] flag for (de)allocation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_ENOSPC		: Not enough free cluster on the volume
 *									or not enough free entry at pVC (pdwFreeCount is updated)
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 * @version		14-FEB-2009 [DongYoung Seo] change type of dwCount from t_int32 to t_uint32
 */
FFatErr
ffat_addon_getFreeClusters(Node* pNode, t_uint32 dwCount, FFatVC* pVC, t_uint32 dwHint,
							t_uint32* pdwFreeCount, FatAllocateFlag dwAllocFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pdwFreeCount);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	*pdwFreeCount = 0;

	r = ffat_hpa_getFreeClusters(pNode, dwCount, pVC, dwHint, pdwFreeCount, dwAllocFlag, pCxt);
	if (r != FFAT_OK)
	{
		goto out;
	}

	// FFAT_OK가 왔다면, 달라질 것이 없다. HPA가 켜져 있다면, HPA에서 끝난다.
	// [en] there is no problem if FFAT_OK is returned. it is terminated at HPA if HPA is working.
	r = ffat_fcc_getFreeClusters(pNode, dwCount, pVC, dwHint, pdwFreeCount, pCxt);
	FFAT_EO(r, (_T("fail to get free clusters")));

out:
	FFAT_ASSERT((r == FFAT_ENOSPC) ? ((VC_IS_FULL(pVC) == FFAT_TRUE) || (*pdwFreeCount == 0)) : FFAT_TRUE);
	FFAT_ASSERT((r == FFAT_DONE) ? (dwCount == VC_CC(pVC)) : FFAT_TRUE);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * add free clusters to ADDON module
 *
 * ADDON에 cluster정보를 추가한다.
 * [en] cluster information is added at ADDON
 * get free cluster 후 make cluster chain 전에 error 발생시 사용 (FCC가 full일수 없음)
 * [en] it is used if error occur, after getting free cluster or before making cluster chain.
 *      ( FCC is not possible to full status. ) 
 *
 * @param		pVol			: [IN] Volume pointer
 * @param		dwCluster		: [IN] cluster number
 * @param		dwCount			: [IN] cluster count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: add free clusters to FCC
 * @return		else			: fail
 * @author		GwangOk Go
 * @version		OCT-10-2008 [GwangOk Go] First Writing.
 */
FFatErr
ffat_addon_addFreeClusters(Vol* pVol, t_uint32 dwCluster, t_uint32 dwCount, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_fcc_addFreeClusters(pVol, dwCluster, dwCount, pCxt);

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * add free clusters to ADDON module
 *
 * ADDON에 vectored 형태의 cluster정보를 추가한다.
 * [en] cluster information of vector type is added at ADDON
 * get free cluster 후 make cluster chain 전에 error 발생시 사용 (FCC가 full일수 없음)
 * [en] it is used if error occur, after getting free cluster or before making cluster chain.
 *      ( FCC is not possible to full status. ) 
 *
 * @param		pVol			: [IN] Volume pointer
 * @param		pVC				: [IN] cluster storage
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK 		: add free clusters to FCC
 * @return		else			: fail
 * @author		GwangOk Go
 * @version		OCT-10-2008 [GwangOk Goi] First Writing.
 */
FFatErr
ffat_addon_addFreeClustersVC(Vol* pVol, FFatVC* pVC, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pCxt);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_fcc_addFreeClustersVC(pVol, pVC, pCxt);

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* get maximum available clusters for a node
*
* @param		pNode		: [IN] Node Pointer
* @param		pdwCount	: [OUT] maximum available cluster count
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_DONE	: volume status gather operation success
* @return		FFAT_OK		: just ok, nothing is done.
* @return		else		: logic error, programming error
* @author		DongYoung Seo
* @version		OCT-01-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_getAvailableClusterCountForNode(Node* pNode, t_uint32* pdwCount, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCount);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_hpa_getAvailableClusterCountForNode(pNode, pdwCount, pCxt);
	FFAT_EO(r, (_T("fail to get available clusters from HPA")));

	if (r == FFAT_DONE)
	{
		// done!! let's go out.
		goto out;
	}

	r = ffat_fcc_getVolumeStatus(NODE_VOL(pNode), NULL, 0, pCxt);
	FFAT_EO(r, (_T("get fail to volume status of HPA")));

	if (r == FFAT_DONE)
	{
		*pdwCount = VOL_FCC(NODE_VOL(pNode));
	}

out:
	FFAT_ASSERT((r == FFAT_DONE) ? (*pdwCount <= VOL_CC(NODE_VOL(pNode))) : FFAT_TRUE);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * deallocate clusters
 *
 * deallocation을 수행하면서 free cluster 정보를 update 한다.
 * [en] free cluster information is updated executing deallocation.
 *
 * 해당되는 모든 CLUSTER에 대한 DE-ALLOCATION을 수행한다.
 * [en] deallocation executes about all clusters needed deallocation
 * 주의 : 일부 CLUSTER에 대해서만 DE-ALLOCATION을 수행하지 말것
 * [en] Attention : do not execute about some clusters.
 * deallocation을 수행하면 모든 cluster에 대해서 수행하고 FFAT_DONE을 RETURN 한다.
 * [en] all clusters execute if deallocation executes, and FFAT_DONE returns.
 * first deallocated cluster는 pVC를 통해 구할 수 있다.
 * [en] first deallocated cluster can be got through pVC.
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwNewEOC		: [IN] new end of chain
 *										dwNewEOC는 EOC mark가 기록된다.
 *									[en] dwNewEOC records EOC mark.	
 *										May be 0 (if this is 0 dwFirstCluster should not be 0)
 * @param		pdwFirstCluster	: [IN/OUT] first cluster to be deallocated
 *					   			  [IN] 0일 경우는 dwPrevEOF에서 next cluster를 구한다.
 *									[en] in case of 0, next cluster can be got at dwPrevEOF.
 *							    		May be 0 (if this is 0 dwPrevEOF should not be 0)
 *							      [OUT] First deallocated cluster number
 * @param		dwCount			: [IN] cluster count to allocate
 *									   0일 경우는 EOC 까지 처리한다.
 *                                   [en] in case of 0, it processes by EOC.
 *									   아닐경우에도, 일단은 EOF를 만날때 까지 처리한다.
 *									 [en]  in other cases, it just processes until arriving EOF.
 * @param		pVC				: [IN] cluster information
 *										cluster 정보가 저장되어 있다.
 *									[en] cluster information is stored. 
 *										주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
 *									[en] Attention : all clusters can not be included.
 *										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 *									[en] in case of no cluster information, it may be NULL.
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_DONE		: dwCount 만큼의 cluster allocation success
 * [en] @return		FFAT_DONE	: success cluster allocation as long as dwCount
 * @return		FFAT_OK			: do partial deallocate. or do nothing
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 * @version		Aug-29-2009 [SangYoon Oh] Add some parameters when calling ffat_hpa_deallocateClusters
 */
FFatErr
ffat_addon_deallocateCluster(Node* pNode, t_uint32 dwNewEOC, t_uint32* pdwFirstCluster,
							t_uint32 dwCount,t_uint32* pdwDeallocCount,
							FFatVC* pVC, FatAllocateFlag dwFAFlag,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	t_uint32	dwCluster = 0;		// start cluster to delete
	FFatErr		r;

	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (VOL_IS_ERASE_SECTOR(NODE_VOL(pNode)) == FFAT_TRUE)
	{
		// erase sector, do not check error
		if ((pVC == NULL) && (*pdwFirstCluster == 0))
		{
			r = ffat_misc_getNextCluster(pNode, dwNewEOC, &dwCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));
		}
		else
		{
			dwCluster = *pdwFirstCluster;
		}

		if (FFATFS_IsValidCluster(NODE_VI(pNode), dwCluster) == FFAT_TRUE)
		{
			ffat_nand_deleteClusterChain(NODE_VOL(pNode), pVC, dwCluster, dwCount, pCxt);
		}
	}

	// do secure unlink when the block device does not support erase operation
	if ((dwFAFlag & FAT_ALLOCATE_SECURE) && 
		(VOL_IS_ERASE_SECTOR(NODE_VOL(pNode)) == FFAT_FALSE))
	{
		if (*pdwFirstCluster == 0)
		{
			r = ffat_misc_getNextCluster(pNode, dwNewEOC, &dwCluster, pCxt);
			FFAT_EO(r, (_T("fail to get next cluster")));
		}
		else
		{
			dwCluster = *pdwFirstCluster;
		}

		if (FFATFS_IsValidCluster(NODE_VI(pNode), dwCluster) == FFAT_TRUE)
		{
			ffat_addon_misc_secureDeallocate(NODE_VOL(pNode), dwCluster, dwCount, pCxt);
		}
	}

    // HPA
	if (*pdwFirstCluster == 0)
	{
		r = ffat_misc_getNextCluster(pNode, dwNewEOC, &dwCluster, pCxt);
		FFAT_EO(r, (_T("fail to get next cluster")));
	}
	else
	{
		dwCluster = *pdwFirstCluster;
	}

	if (FFATFS_IsValidCluster(NODE_VI(pNode), dwCluster) == FFAT_TRUE)
	{
		r = ffat_hpa_deallocateClusters(pNode, dwCluster, dwCount, pVC, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to deallocate clusters")));
	}

	r = ffat_fcc_deallocateCluster(pNode, dwNewEOC, pdwFirstCluster, dwCount,
					pdwDeallocCount, pVC, dwFAFlag, dwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to deallocate clusters")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * deallocation operation 이후, deallocated cluster의 정보를 
 * ADDON module로 전달하기위한 함수이다.
 * [en] this function is forwarding deallocated cluster information to ADDON module, 
 *      after deallocation operation
 *
 * first deallocated cluster는 pVC를 통해 구할 수 있다.
 * [en] first deallocated cluster can be got through pVC.
 *
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] cluster information
 *									   storage for deallocated clusters 
 *									   주의 : 모든 cluster가 포함되어 있지 않을 수 있다.
 *                                  [en] Attention : all clusters can not be included.
 *										may be NULL, cluster 정보가 없을 경우 NULL일 수 있다.
 *                                  [en] in case of no cluster information, it may be NULL.
 * @param		dwFirstCluster	: [IN] the first cluster of deallocated cluster chain
 * @param		dwDeallocCount	: [IN] deallocated count
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-22-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_afterDeallocateCluster(Node* pNode, FFatVC* pVC, t_uint32 dwFirstCluster,
								t_uint32 dwDeallocCount, ComCxt* pCxt)
{
	FFatVC		stVC;
	FFatVCE		stVCE;

	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	if (dwDeallocCount == 0)
	{
		// nothing to do
		return FFAT_OK;
	}

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if ((pVC == NULL) || (VC_IS_EMPTY(pVC) == FFAT_TRUE))
	{
		VC_INIT(&stVC, VC_NO_OFFSET);
		stVC.dwTotalClusterCount	= 1;
		stVC.dwTotalEntryCount		= 1;
		stVC.dwValidEntryCount		= 1;
		stVC.pVCE					= &stVCE;
		stVCE.dwCluster				= dwFirstCluster;
		stVCE.dwCount				= 1;

		pVC = &stVC;
	}

	r = ffat_hpa_afterDeallocateCluster(pNode, pVC, dwDeallocCount, pCxt);
	FFAT_EO(r, (_T("fail to update free cluster count on HPA")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* After function call for making cluster chain 
*
* @param		pNOde			: [IN] Node pointer
* @param		dwPrevEOF		: [IN] previous End Of File cluster number. New cluster are connected to this
*										may be 0 ==> no previous cluster
* @param		pFVC			: [IN] Vectored Cluster Information
* @param		dwFUFlag		: [IN] flags for FAT update
* @param		dwCacheFlag		: [IN] flag for cache operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo
* @version		NOV-15-2007 [DongYoung Seo] First Writing.
* @version		JAN-14-2009 [JeongWoo Park] bug fix for the wrong condition in while().
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter dwCacheFlag when calling ffat_hpa_makeClusterChainVCAfter
*/
FFatErr
ffat_addon_afterMakeClusterChain(Node* pNode, t_uint32 dwPrevEOF, t_int32 dwClusterCount,
						t_uint32* pdwClusters, FatUpdateFlag dwFUFlag,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatVC		stVC;
	FFatVCE		stVCE;
	t_int32		dwIndex;
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	VC_INIT(&stVC, VC_NO_OFFSET);

	stVC.pVCE = &stVCE;
	stVC.dwTotalEntryCount = 1;
	stVC.dwValidEntryCount = 1;

	dwIndex = 0;

	do
	{
		stVCE.dwCluster = pdwClusters[dwIndex];
		stVCE.dwCount	= 1;

		dwIndex++;

		while ((dwIndex  < dwClusterCount)
				&& ((pdwClusters[dwIndex - 1] + 1) == pdwClusters[dwIndex]))
				
		{
			stVCE.dwCount++;
			dwIndex++;
		}

		stVC.dwTotalClusterCount = stVCE.dwCount;

		r = ffat_hpa_makeClusterChainVCAfter(pNode, &stVC, dwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to after operation for make cluster chain")));

	} while (dwIndex < dwClusterCount);

	r = FFAT_OK;

out:
	r |= _PUT_LOCK(pCxt);
	return r;
}


/**
* After function call for making cluster chain with VC
*
* @param		pNOde			: [IN] Node pointer
* @param		dwPrevEOF		: [IN] previous End Of File cluster number.
										New cluster are connected to this
*										may be 0 ==> no previous cluster
* @param		pVC				: [IN] Vectored Cluster Information
* @param		dwFUFlag		: [IN] flags for FAT update
* @param		dwCacheFlag		: [IN] flag for cache operation
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: success
* @author		DongYoung Seo
* @version		NOV-15-2007 [DongYoung Seo] First Writing.
* @version		Aug-29-2009 [SangYoon Oh] Add the parameter dwCacheFlag when calling ffat_hpa_makeClusterChainVCAfter
*/
FFatErr
ffat_addon_afterMakeClusterChainVC(Node* pNode, t_uint32 dwPrevEOF,
								FFatVC* pVC, FatUpdateFlag dwFUFlag,
								FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_hpa_makeClusterChainVCAfter(pNode, pVC, dwCacheFlag, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* this function will be invoked after successful volume name change
*
* @param		pVol		: [IN] Volume Name pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: it has access permission
* @return		else		: error
* @author		DongYoung Seo
* @version		NOV-19-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_afterSetVolumeLabel(Vol* pVol, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_dec_deallocateGDEC(VOL_ROOT(pVol));
	FFAT_EO(r, (_T("Fail to deallocate GDEC")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * get next cluster number
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwCluster		: [IN] cluster
 * @param		pdwNextCluster	: [OUT] next cluster storage
 * @return		FFAT_OK			: not found next cluster
 * @return		FFAT_DONE		: success, found next cluster
 * @return		negative		: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_getNextCluster(Node* pNode, t_uint32 dwCluster, t_uint32* pdwNextCluster)
{
	// cluster chain에 관련된 module이 추가될 경우 이 기능을 사용한다.
	// [en] in case of adding modules related cluster chain, this function uses.  
	// 현재는 아무런 작업을 하지 않는다.
	// [en] have not worked, now.
	// enable function call at ffat_misc_getNextCluster() to use this function

	return FFAT_OK;
}


/**
 * get cluster information.
 *
 * vector 형태로 cluster의 정보를 구한다.
 * [en] cluster information of vector type can be got. 
 * 적은 메모리로 많은 cluster의 정보를 한번에 얻을 수 있다.
 * [en] much cluster information can be got one time by using less memory.
 *
 * 주의 : pVC의 내용은 초기화 하지 않는다.
 * [en] Attention : contents of pVC do not initialize. 
  * 정보를 추가해서 저장한다.
 * [en] it stores information adding. 
 * 단, dwOffset을 사용할때 pVC는 초기화되어 있어야 한다
 * [en] merely, pVC must be initialized when it uses dwOffset. 
 * 
 * @param		pNode			: [IN] node pointer
 * @param		dwCluster		: [IN] start cluster number (may be 0. if 0, dwOffset must be valid.)
 * @param		dwOffset		: [IN] start offset (if dwCluster is 0, this is used. if not, ignored)
 * @param		dwCount			: [IN] cluster count
 * @param		pVC				: [OUT] vectored cluster information
 * @param		pdwNewCluster	: [OUT] new start cluster after operation
 * @param		pdwNewCount		: [OUT] new cluster count after operation
 * @param		pPAL			: [OUT] previous access location
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @author		DongYoung Seo
 * @version		OCT-20-2006 [DongYoung Seo] First Writing.
 * @version		OCT-14-2008 [GwangOk Go] add parameter dwOffset
 */
FFatErr
ffat_addon_getVectoredCluster(Node* pNode, t_uint32 dwCluster, t_uint32 dwOffset,
							t_uint32 dwCount, FFatVC* pVC, t_uint32* pdwNewCluster,
							t_uint32* pdwNewCount, NodePAL* pPAL, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwNewCluster);
	FFAT_ASSERT(pdwNewCount);

	if (pNode == NULL)
	{
		// if Node is NULL, do not use ADDON module
		return FFAT_OK;
	}

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	*pdwNewCluster	= dwCluster;
	*pdwNewCount	= dwCount;

	// get cluster information from VEC
	r = ffat_fastseek_getVectoredCluster(pNode, dwCount, pVC, dwOffset, pdwNewCluster,
										pdwNewCount, pPAL, pCxt);
	if (r < 0)
	{
		// do not return error to FFatCore
		// this is just an widget for performance improvement.
		// There is no harm to ignore error 
		r = FFAT_OK;
	}

	r |= _PUT_LOCK(pCxt);

	return r;
}

//=============================================================================
//
//	Directory 관련
//  related Directory
//

/**
 * lookup a node in a directory
 * lookup a node that name is psName and store directory to pGetNodeDE
 *
 * @param		pNodeParent	: [IN] Parent node
 * @param		pNodeChild	: [IN/OUT] Child node, 
 *								Free DE information will be updated when flag is FFAT_LOOKUP_FOR_CREATE
 * @param		psName		: [IN] node name
 * @param		dwLen		: [IN] name length
 * @param		pdwOffset	:	[IN]	 : lookup start offset
 *								[OUT]	: next lookup start point
 *											This value will changed when there is partial lookup on DEC module
 * @param		dwFlag		:	[IN]	: lookup flag, refer to FFatLookupFlag
 * @param		pGetNodeDE	: [OUT] directory entry for node
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않음 or 일부에 대한 lookup을 수행함
 * [en] @return	FFAT_OK		: lookup does not process at ADDON module or a part of lookup processes.
 * @return		FFAT_DONE	: ADDON module에서 lookup을 성공적으로 처리함.
 * [en] return	FFAT_DONE	: lookup processes successfully at ADDON module. 
 * @return		FFAT_ENOENT	: 해당 node가 존재하지 않음
 * [en] @return	FFAT_ENOENT	: relevant nodes do not exist.
 * @return		negative	: error
 * @author		DongYoung Seo
 * @version		AUG-10-2006 [DongYoung Seo] First Writing
* @version		MAY-13-2009 [DongYoung Seo] Support Name compare with multi-byte(CQID:FLASH00021814)
 */
FFatErr
ffat_addon_lookup(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, t_int32 dwLen,
					FFatLookupFlag dwFlag, FatGetNodeDe* pNodeDE,
					NodeNumericTail* pNumericTail, ComCxt* pCxt)
{
#ifdef FFAT_CMP_NAME_MULTIBYTE
	FatGetNodeDe*	pNodeDE_Backup;		// for backing up
#endif

	FFatErr			r;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pNodeDE);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	ffat_xde_lookup(pNodeChild);		// void return

	r = ffat_hpa_lookup(pNodeParent, pNodeChild, psName, dwLen, dwFlag, pNodeDE);
	if (r == FFAT_DONE)
	{
		r = FFAT_DONE;
		goto out;
	}

#ifdef FFAT_CMP_NAME_MULTIBYTE
	// check under strike and set NODE_NAME_UNDERSCORE to pNodeChild->dwFlag
	_checkUnderScore(pNodeChild, psName, dwLen);

	if (pNodeChild->dwFlag & NODE_NAME_UNDERSCORE)
	{
		pNodeDE_Backup = (FatGetNodeDe*)FFAT_LOCAL_ALLOC(sizeof(FatGetNodeDe), pCxt);
		FFAT_ASSERT(pNodeDE_Backup);

		FFAT_MEMCPY(pNodeDE_Backup, pNodeDE, sizeof(FatGetNodeDe));
	}
	else
	{
		pNodeDE_Backup = NULL;
	}

	// if there is no directory entry cache just return FFAT_OK
	r = ffat_dec_lookup(pNodeParent, pNodeChild, psName, dwLen, dwFlag, pNodeDE, pNumericTail, pCxt);
	if ((pNodeChild->dwFlag & NODE_NAME_UNDERSCORE) && (r == FFAT_ENOENT))
	{
		// retry to check not converted character

		// RESTORE ORIGINAL GetNodeDE
		FFAT_MEMCPY(pNodeDE, pNodeDE_Backup, sizeof(FatGetNodeDe));

		r = FFAT_OK;
	}

	FFAT_LOCAL_FREE(pNodeDE_Backup, sizeof(FatGetNodeDe), pCxt);

#else
	// if there is no directory entry cache just return FFAT_OK
	r = ffat_dec_lookup(pNodeParent, pNodeChild, psName, dwLen, dwFlag, pNodeDE, pNumericTail, pCxt);
#endif

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * add node information after lookup operation
 *
 * lookup의 결과를 ADDON module에 알려준다.
 * [en] the result of lookup is announced at ADDON module.
 * path cache 등에서 사용할 수 있다.
 * [en] it can be use at path cache, etc..
 *
 * @param		pNodeParent	: [IN] Parent node
 * @param		pNodeChild	: [IN] Child node information
 * @param		dwFlag		: [IN] lookup flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: ADDON module에서 lookup을 처리하지 않음
 * [en] return	FFAT_OK		: lookup does not process at ADDON module
 * @return		negative	: error 
 * @author		DongYoung Seo
 * @version		AUG-10-2006 [DongYoung Seo] First Writing
 * @version		MAY-21-2009 [JeongWoo Park] Add the code to deallocateGFSE.
 *											Buf fix for FLASH00021764
 */
FFatErr
ffat_addon_afterLookup(Node* pNodeParent, Node* pNodeChild,
						FFatLookupFlag dwFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_hpa_afterLookup(pNodeParent, pNodeChild);
	FFAT_EO(r, (_T("fail lookup after operatoin on HPA")));

	r = ffat_spfile_afterLookup(pNodeChild, NULL, pCxt);
	FFAT_EO(r, (_T("fail lookup after operatoin on Symlink")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called before directory expansion.
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNode			: [IN] node(directory) pointer
 * @param		dwPrecEOC		: [IN] the last cluster of pNode
 * @param		pdwCacheFlag	: [OUT] flag for cache operation 
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_expandDir(Node* pNode, t_uint32 dwPrevEOC, FFatCacheFlag* pdwCacheFlag,
					ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_log_expandDir(pNode, dwPrevEOC, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write expanding dir log")));

out:
	r |= _PUT_LOCK(pCxt);
	return r;
}


/**
 * This function is called before directory truncation.
 *
 * after this function clusters (that connected to the next of dwPrevEOC) will be free.
 * 
 * @param		pNode			: [IN] node(directory) pointer
 * @param		dwPrecEOC		: [IN] the last cluster of pNode
 * @param		dwOffset		: [IN] the last entry offset
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-29-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_truncateDir(Node* pNode, t_uint32 dwPrevEOC, t_uint32 dwOffset,
						FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_int32		dwCount = 2;		// 2 ==> guard cluster
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// leave some clusters to avoid frequent deallocation and allocation
	do 
	{
		// leave a cluster
		r = ffat_misc_getNextCluster(pNode, dwPrevEOC, &dwPrevEOC, pCxt);
		if ((r < 0) || (FFATFS_IS_EOF(NODE_VI(pNode), dwPrevEOC) == FFAT_TRUE))
		{
			// no more cluster
			r = FFAT_OK;
			goto out;
		}
	} while (--dwCount);

	r = ffat_log_truncateDir(pNode, dwPrevEOC, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write truncation log")));

	// truncate directory, dont'care error
	ffat_misc_deallocateCluster(pNode, dwPrevEOC, 0, 0, NULL,
					FAT_DEALLOCATE_DISCARD_CACHE, *pdwCacheFlag, pCxt);

	ffat_fastseek_resetFrom(pNode, (dwOffset + FAT_DE_SIZE));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * This function is called after filling node info
 *
 * @param		pNode			: [IN] node pointer
 * @param		pNodeDE			: [IN] info of directory entry
 * @param		pAddonInfo		: [IN/OUT] buffer of ADDON node (may be NULL)
 * @return		FFAT_OK			: success
 * @return		negative		: fail
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 */
void
ffat_addon_fillNodeInfo(Node* pNode, FatGetNodeDe* pNodeDE, void* pAddonInfo)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNodeDE);

	// don't need to lock because of node lock

	ffat_xde_fillNodeInfo(pNode, pNodeDE, (XDEInfo*)pAddonInfo);

	return;
}


/**
* after lookup short & long directory entry, get contiguous extended directory entry
*
* @param		pVol		: [IN] volume pointer
* @param		pNodeDE		: [IN/OUT] directory entry information for a node.
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_OK1	: need to skip this DE in CORE module
* @return		else		: error
* @author		GwangOk Go
* @version		AUG-14-2008 [GwangOk Go] First Writing.
*/
FFatErr
ffat_addon_afterGetNodeDE(Vol* pVol, FatGetNodeDe* pNodeDE, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeDE);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_xde_getXDE(pVol, pNodeDE, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
}


// Please write function comment !!
FFatErr
ffat_addon_GetGUIDFromNode(Node* pNode, void* pstXDEInfo)
{
	return ffat_xde_getGUIDFromNode(pNode, (XDEInfo*)pstXDEInfo);
}


//=============================================================================
//
//	File 관련
//	related File
//

/**
 * this function is called before node create operation.
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 *									It has all information for new node.
 * @param		psName			: [IN] node name
 * @param		pDE				: [IN] storage for directory entry, 
 *									LFNE and SFNE are stored at here. Entry count is pNodeChild->stDeInfo.dwDeCount
 * @param		bCheckSum		: [IN] check sum (SFN일 경우는 값이 없음)
 * [en] param	bCheckSum		: [IN] check sum (in case of SFN, no value)
 * @param		pAddonInfo		: [IN] info of ADDON node (may be NULL)
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-21-2006 [DongYoung Seo] First Writing.
 * @version		JAN-28-2009 [DongYoung Seo] BUG FIX - this function had returned FFAT_OK even if there is some error.
 * @version		JUN-17-2009 [GwangOk Go] add creating fifo, socket
 */
FFatErr
ffat_addon_create(Node* pNodeParent, Node* pNodeChild, t_wchar* psName, 
					FatDeSFN* pDE, t_uint8 bCheckSum, void* pAddonInfo,
					FFatCreateFlag dwCreateFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pdwCacheFlag);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// create extended directory entry
	ffat_xde_create(pNodeChild, pDE, bCheckSum, (XDEInfo*)pAddonInfo);

	// create special file (except for symlink)
	ffat_spfile_create(pNodeChild, dwCreateFlag);

	// log for creation
	r = ffat_log_create(pNodeParent, pNodeChild, psName, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to log create operation")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called after node creation operation.
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 * @param		psName			: [IN] name of created node
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @param		bSuccess		: [IN]	FFAT_TRUE : create success
 *										FFAT_FALSE : creation operation fail
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-04-2006 [DongYoung Seo] First Writing.
 * @version		SEP-04-2006 [DongYoung Seo] add two parameter pdwClsutersDE, dwClsuterCountDE
 *										to support XDE
 */
FFatErr
ffat_addon_afterCreate(Node* pNodeParent, Node* pNodeChild, t_wchar* psName,
						t_uint32* pdwClustersDE, t_int32 dwClusterCountDE,
						t_boolean bSuccess, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	IF_LK (bSuccess == FFAT_TRUE)
	{
		r = ffat_dec_insertEntry(pNodeParent, pNodeChild, psName);
		if (r == FFAT_ENOSPC)
		{
			r = FFAT_OK;
		}
		FFAT_EO(r, (_T("fail to insert entry into dec")));

		r = ffat_fastseek_resetFrom(pNodeChild, 0);
		FFAT_EO(r, (_T("fail to reset fast seek")));

		r = ffat_xde_afterCreate(pNodeParent, pNodeChild, pdwClustersDE, dwClusterCountDE);
		FFAT_EO(r, (_T("fail on after operation for XDE")));
	}
	else
	{
		FFAT_ASSERT(bSuccess == FFAT_FALSE);

		r = ffat_log_operationFail(NODE_VOL(pNodeParent), LM_LOG_CREATE_NEW, pNodeChild, pCxt);
		FFAT_EO(r, (_T("fail to reset log operation")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* This function is called after read data from a file
*
* @param		pNode		: [IN] node pointer
 *								may not exist all clusters
* @param		pCxt		: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-16-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_addon_afterReadFile(Node* pNode, FFatVC* pVC, ComCxt* pCxt)
{
#if 0
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// on success
	//r = ffat_fastseek_update(pNode, pVC, pCxt); // comment?? guess that it occurs loss of performance.  
	//FFAT_ASSERT(r == FFAT_OK);

	r |= _PUT_LOCK(pCxt);

	return r;
#endif

	return FFAT_OK;
}


/**
 * write data to a file
 * only for file.!!!!
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwLastOffset	: [IN] write offset + write size
 * @PARAM		dwPrevEOC		: [IN] The last cluster of Node. 
 *									   this may be 0 when there is no cluster allocation
 * @param		pVC_Cur			: [IN/OUT] storage for clusters used for write
 * @param		pVC_New			: [IN/OUT] newly allocated cluster used for write
 * @param		dwWriteFlag		: [IN] flag for write operation
 * @param		pdwCacheFlag	: [IN/OUT] flag for cache operation
 * @param		pbSync			: [OUT] descriptor for cache operation
 *								FFAT_TRUE  : All meta-data update should be synchronized to the device
 *								FFAT_FALSE : Meta-data sync is not a mandatory one.
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		OCT-10-2008 [GwangOk Go]    receive log info (free clusters to be allocated) from CORE module
 * @version		OCT-29-2008 [DongYoung Seo] remove parameter *pdwNewClusters and *dwRequestClusters
 */
t_int32
ffat_addon_writeFile(Node* pNode, t_uint32 dwLastOffset, t_uint32 dwPrevEOC,
						FFatVC* pVC_Cur, FFatVC* pVC_New,
						FFatWriteFlag dwWriteFlag, FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pVC_Cur);
	FFAT_ASSERT(pVC_New);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	r = ffat_log_writeFile(pNode, dwLastOffset, dwPrevEOC, pVC_Cur, pVC_New,
							dwWriteFlag, pdwCacheFlag, pCxt);

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
* This function is called after write data to a file
*
* @param		pNode			: [IN] node pointer
* @param		dwOriginalSize	: [IN] Original node size
* @param		dwCacheFlag		: [IN] flag for cache operation
* @param		bSuccess		: [IN]	FFAT_TRUE : operation success
*										FFAT_FALSE : operation fail
* @param		pCxt			: [IN] context of current operation
* @author		DongYoung Seo
* @version		MAY-16-2007 [DongYoung Seo] First Writing.
* @version		FEB-19-2009 [JeongWoo Park] Add the code
*											to record confirm log for sync mode.
* @version		APR-14-2009 [JeongWoo Park] Add the code to reset GFS for error
* @version		Aug-29-2009 [SangYoon Oh] Remove the parameter pVC
*/
FFatErr
ffat_addon_afterWriteFile(Node* pNode, t_uint32 dwOriginalSize,
							FFatCacheFlag dwCacheFlag, t_boolean bSuccess, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	IF_LK (bSuccess == FFAT_TRUE)
	{
		r = FFAT_OK;

		// on success, why did i update fast seek?
		//r = ffat_fastseek_update(pNode, pVC, pCxt); // comment?? guess that it occurs loss of performance.
		//FFAT_ASSERT(r == FFAT_OK);

		// [CQ FLASH00020263] : commit log for sync mode
		if (dwCacheFlag & FFAT_CACHE_SYNC)
		{
			FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(NODE_DE(pNode))) : FFAT_TRUE);

			// record the confirm log for prevent undo
			r |= ffat_log_confirm(pNode, pCxt);
		}
	}
	else
	{
		r = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_WRITE, pNode, pCxt);
		FFAT_EO(r, (_T("fail to reset log operation")));

		r = ffat_fastseek_resetFrom(pNode, dwOriginalSize);
		FFAT_EO(r, (_T("fail to reset fast seek")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * rename a node
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node
 * @param		pNodeDesParent	: [IN] parent node of destination(target)
 * @param		pNodeDes		: [IN] destination node
 *									   destination 정보는 lookup을 수행한 valid 정보이다.
 *									[en] destination information is valid information executed lookup.
 *									   may be NULL
 * @param		pNodeNewDes		: [IN] new destination node
 *									   new directory entry 가 기록될 위치를 저장하고 있다.
 *									[en] new directory entry stores recording location.
 * @param		psName			: [IN] target node name
 * @param		dwFlag			: [IN] rename flag
 *										must care FFAT_RENAME_TARGET_OPENED
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		SEP-19-2007 [DongYoung Seo] release lock when rename operation is fail on symlink
 * @version		FEB-25-2009 [GwangOk Go] set xde & xattr flag on NTRes before log write
 */
FFatErr
ffat_addon_rename(Node* pNodeSrcParent, Node* pNodeSrc,
			Node* pNodeDesParent, Node* pNodeDes, Node* pNodeNewDes, t_wchar* psName,
			FFatRenameFlag dwFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pdwCacheFlag);

	dwFlag = dwFlag;		// suppress warning

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if ((ffat_debug_isDubugFile(pNodeSrc) == FFAT_TRUE) ||
		((pNodeDes != NULL) && (ffat_debug_isDubugFile(pNodeDes) == FFAT_TRUE)))
	{
		r = FFAT_EACCESS;
		goto out;
	}

	ffat_spfile_rename(pNodeSrc, pNodeNewDes);

	r = ffat_hpa_rename(pNodeSrc, pNodeDesParent, pNodeDes, pNodeNewDes);
	FFAT_EO(r, (_T("fail to rename a node on HPA module")));

	r = ffat_fastseek_rename(pNodeSrc, pNodeDes, pNodeNewDes);
	FFAT_EO(r, (_T("fail to rename a node at fast seek module")));

	// update related extended directory entry
	ffat_xde_rename(pNodeSrc, pNodeNewDes);		// void

	// update related extended attribute entry
	ffat_ea_renameEA(pNodeSrc, pNodeNewDes);	// void

	// log write
	r = ffat_log_rename(pNodeSrcParent, pNodeSrc, pNodeDesParent, pNodeDes, pNodeNewDes,
					psName, dwFlag, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to log for rename operation")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * rename시 directory entry관련 update를 수행
 * [en] when rename operation is executing, updates related directory entry.
 *
 * @param		pNodeSrc	: [IN] source node pointer
 * @param		pNodeNewDes	: [IN] destination node pointer
 * @param		pDE			: [IN] directory entry buffer pointer
 * @param		bCheckSum	: [IN] check sum (SFN일 경우는 값이 없음)
 * [en] param	bCheckSum	: [IN] check sum (in case of SFN, no value)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing
 */
FFatErr
ffat_addon_renameUpdateDE(Node* pNodeSrc, Node* pNodeNewDes, FatDeSFN* pDE,
							t_uint8 bCheckSum, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeNewDes);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pCxt);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// update related extended directory entry 
	ffat_xde_renameUpdateDE(pNodeSrc, pNodeNewDes, pDE, bCheckSum);		// void

	r = _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called after rename a node
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node
 * @param		pNodeDesParent	: [IN] parent node of destination(target)
 * @param		pNodeDes		: [IN] destination node
 *		       						   destination 정보는 lookup을 수행한 valid 정보이다.
 *									[en] destination information is valid information executed lookup.
 * @param		pNodeNew		: [IN] New Node Information
 * @param		psName			: [IN] target node name
 * @param		dwFlag			: [IN] rename flag
 *										must care FFAT_RENAME_TARGET_OPENED
 * @param		bSuccess		: [IN]	FFAT_TRUE : operation success
 *										FFAT_FALSE : operation fail
 * @param		pdwClustersDE	: [IN] cluster for write
 * @param		dwClusterCountDE: [IN] cluster count in pdwClustersDE
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		OCT-02-2006 [DongYoung Seo] First Writing.
 * @version		OCT-02-2006 [DongYoung Seo] Support Open Rename
 * @version		DEC-16-2008 [JeongWoo Park] Add the code for EA deallocation.
 * @version		JAN-06-2009 [DongYoung Seo] Add parameter pdwClsutersDE, dwClusterCountDE
 *											for SFNE update on XDE
 * @version		FEB-04-2009 [JeongWoo Park] remove the NULL pointer access problem at failure
 */
FFatErr
ffat_addon_afterRename(Node* pNodeSrcParent, Node* pNodeSrc,
			Node* pNodeDesParent, Node* pNodeDes, Node* pNodeNew,
			t_wchar* psName, FFatRenameFlag dwFlag, t_boolean bSuccess,
			t_uint32* pdwClustersDE, t_int32 dwClusterCountDE,
			ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNodeSrcParent);
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(pNodeDesParent);
	FFAT_ASSERT(pNodeNew);
	FFAT_ASSERT(psName);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	IF_LK (bSuccess == FFAT_TRUE)
	{
		// node rename notification

		// EA 처리
		// deallocate EA if the node is normally unlinked.
		if (pNodeDes != NULL)
		{
			if ((ffat_node_isSameNode(pNodeSrc, pNodeDes) == FFAT_FALSE) &&
				((dwFlag & FFAT_RENAME_TARGET_OPENED) == 0))
			{
				FFAT_ASSERT(NODE_IS_VALID(pNodeDes) == FFAT_TRUE);

				r = ffat_ea_deallocate(pNodeDes, pCxt);
				if ((r != FFAT_OK) && (r != FFAT_ENOXATTR))
				{
					FFAT_EO(r, (_T("fail to deallocate EA")));
				}
			}
		}

		// ========================================================================
		// DEC Start
		//

		// Never move insert after remove
		//	why??
		//		insert entry before remove the last entry on the DEC
		r = ffat_dec_insertEntry(pNodeDesParent, pNodeNew, psName);
		if (r == FFAT_ENOSPC)
		{
			r = FFAT_OK;
		}
		FFAT_EO(r, (_T("fail to insert entry into dec")));

		r = ffat_dec_removeEntry(pNodeSrcParent, pNodeSrc);
		FFAT_EO(r, (_T("fail to reset DEC - for source node")));

		if (pNodeDes != NULL)
		{
			r = ffat_dec_removeEntry(pNodeDesParent, pNodeDes);
			FFAT_EO(r, (_T("fail to reset DEC - for destination node")));
		}

		//
		// DEC End
		// ========================================================================

		r = ffat_xde_afterRename(pNodeDesParent, pNodeNew, pdwClustersDE, dwClusterCountDE);

		r = ffat_fastseek_afterRename(pNodeSrc, pNodeDes, pNodeNew);
		FFAT_EO(r, (_T("fail to release fast seek ")));

		// process rename operation about hidden area 
	}
	else
	{
		FFAT_ASSERT(bSuccess == FFAT_FALSE);
		if ((pNodeDes != NULL) &&
			(ffat_node_isSameNode(pNodeSrc, pNodeDes) == FFAT_TRUE))
		{
			r = ffat_log_operationFail(NODE_VOL(pNodeSrcParent), LM_LOG_RENAME, NULL, pCxt);
		}
		else
		{
			r = ffat_log_operationFail(NODE_VOL(pNodeSrcParent), LM_LOG_RENAME, pNodeDes, pCxt);
		}

		FFAT_EO(r, (_T("fail to reset log operation")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called before node unlinking operation. 
 * for both file and directory
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 *									   may be NULL
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwFlag			: [IN] flags for unlink operations
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: nothing to do.
 * @return		FFAT_DONE		: unlink operation is successfully done.
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		SEP-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_unlink(Node* pNodeParent, Node* pNode, FFatVC* pVC,
					NodeUnlinkFlag dwFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		// HPA
		r = ffat_hpa_removeDir(pNode);
		FFAT_EO(r, (_T("fail to unlink on HPA")));
	}

	// log write
	r = ffat_log_unlink(pNode, pVC, dwFlag, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to log unlink operation")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}

/**
* this function is called at unlink open-unlinked node operation.
*
* @param		pNode		: [IN] node pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: nothing to do.
* @return		else		: error
* @author		JeongWoo Park
* @version		APR-29-2009 [JeongWoo Park] First Writing.
*/
FFatErr
ffat_addon_unlinkOpenUnlinkedNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	// log write
	r = ffat_log_unlinkOpenUnlinkedNode(pNode, pCxt);
	FFAT_EO(r, (_T("fail to log operation for unlink opend-unlinked node")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}

/**
 * this function is called after unlink open-unlinked node operation.
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: nothing to do.
 * @return		else		: error
 * @author		InHwan Choi
 * @version		DEC-05-2007 [InHwan Choi] First Writing.
 * @version		NOV-12-2008 [DongYoung Seo] remove ASSERT to check node is file.
 *									A directory may be unlinked in open state
 * @version		MAR-29-2009 [DongYoung Seo] change function name from ffat_addon_unlinkOpenUnlinkedNode
 *									to ffat_addon_unlinkOpenUnlinkedNodeAfter,
 *									this routine is after operation for unlink
 * @version		APR-29-2009 [JeongWoo Park] change function name from ffat_addon_unlinkOpenUnlinkedNodeAfter
 *									to ffat_addon_afterUnlinkOpenUnlinkedNode
 */
FFatErr
ffat_addon_afterUnlinkOpenUnlinkedNode(Node* pNode, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	// deallocate clusters of extended attribute
	r = ffat_ea_deallocate(pNode, pCxt);
	if ((r != FFAT_OK) && (r != FFAT_ENOXATTR))
	{
		FFAT_EO(r, (_T("fail to deallocate EA while unlink opend-unlinked node")));
	}

	// log operation
	r = ffat_log_afterUnlinkOpenUnlinkedNode(pNode, pCxt);
	FFAT_EO(r, (_T("fail to log operation for after unlink opend-unlinked node")));

out:
	r |= _PUT_LOCK(pCxt);

	return FFAT_OK;
}


/**
 * this function is called after node unlinking operation.
 *
 * 1. update directory entry cache
 * 2. update path cache (future work)
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNodeParent	: [IN] parent node pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 * @param		dwNUFlag	: [IN] unlink flag
 * @param		bSuccess	: [IN]	FFAT_TRUE : operation success
 *									FFAT_FALSE : operation fail
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-04-2006 [DongYoung Seo] First Writing.
 * @version		DEC-15-2008 [JeongWoo Park] Add the code for EA deallocation.
 */
FFatErr
ffat_addon_afterUnlink(Node* pNodeParent, Node* pNode, NodeUnlinkFlag dwNUFlag,
						t_boolean bSuccess, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	IF_LK (bSuccess == FFAT_TRUE)
	{
		// node unlink notification

		// deallocate EA if the node is normally unlinked.
		if ((dwNUFlag & NODE_UNLINK_OPEN) == 0)
		{
			r = ffat_ea_deallocate(pNode, pCxt);
			if ((r != FFAT_OK) && (r != FFAT_ENOXATTR))
			{
				FFAT_EO(r, (_T("fail to deallocate EA")));
			}
		}

		// for DEC
		r = ffat_dec_removeEntry(pNodeParent, pNode);
	}
	else
	{
		FFAT_ASSERT(bSuccess == FFAT_FALSE);

		r = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_UNLINK, pNode, pCxt);
		FFAT_EO(r, (_T("fail to reset log operation")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called before node size change operation.
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNode			: [IN] node pointer,
 *										all node information is already updated state.
 * @param		dwSize			: [IN] New node size
 * @param		dwEOF			: [IN] if expand, previous EOF. if shrink, new EOF
 * @param		pVC				: [IN] vectored cluster information
 * @param		dwCSFlag		: [IN] change size flag
 * @param		dwCacheFlag		: [IN] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] add dwEOF
 */
FFatErr
ffat_addon_changeSize(Node* pNode, t_uint32 dwSize, t_uint32 dwEOF, FFatVC* pVC,
					FFatChangeSizeFlag dwCSFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	// log write
	r = ffat_log_changeSize(pNode, dwSize, dwEOF, pVC, dwCSFlag, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called after node size change operation.
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 *
 * @param		pNode			: [IN] node pointer,
 *									all node information is already updated state.
 * @param		pVC				: [IN] cluster information for expand
 * @param		dwOriginalSize	: [IN] Original node size
 * @param		bExpand			: [IN] is expand or shrink
 * @param		bSuccess		: [IN]	FFAT_TRUE : operation success
 *										FFAT_FALSE : operation fail
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		OCT-23-2006 [DongYoung Seo] First Writing.
 * @version		FEB-05-2009 [JeongWoo Park] edit the parameter and ASSERT about size.
 * @version		APR-14-2009 [JeongWoo Park] Add the code to reset GFS for error
 */
FFatErr
ffat_addon_afterChangeSize(Node* pNode, FFatVC* pVC, t_uint32 dwOriginalSize,
							t_boolean bExpand, t_boolean bSuccess, ComCxt* pCxt)
{
	FFatErr		r = FFAT_OK;

	FFAT_ASSERT(pNode);

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	IF_LK (bSuccess == FFAT_TRUE)
	{
		if (bExpand == FFAT_FALSE)
		{
			// shrink
			r = ffat_fastseek_resetFrom(pNode, pNode->dwSize);
			FFAT_EO(r, (_T("fail to reset fast seek")));
		}
		// random write 성능비교 후 결정
		// decide "else" condition after random write performance test
// 		else
// 		{
// 			// expand
// 			r = ffat_fastseek_update(pNode, pVC, pCxt);  // comment?? guess that it occurs loss of performance.
// 		}
	}
	else
	{
		FFAT_ASSERT(bSuccess == FFAT_FALSE);

		if (bExpand == FFAT_TRUE)
		{
			r = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_EXTEND, pNode, pCxt);
			FFAT_EO(r, (_T("fail to reset log operation")));

			r = ffat_fastseek_resetFrom(pNode, dwOriginalSize);
		}
		else
		{
			r = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_SHRINK, pNode, pCxt);
			FFAT_EO(r, (_T("fail to reset log operation")));

			r = ffat_fastseek_resetFrom(pNode, pNode->dwSize);
		}
		FFAT_EO(r, (_T("fail to reset fast seek")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called before node status change operation.
 *
 * parameter validity check는 다시 수행할 필요없다.
 * [en] parameter validity check need not execute again.
 * pStatus->dwCTime은 Core의 SFNE updte를 위하여 Addon에서 수정되서 리턴된다.
 * [en] pStatus->dwCTime returns to modify at ADDON for updating SFNE of Core. 
 *
 * @param		pNode		: [IN] node pointer,
 * @param		pStatus		: [INOUT] node information
 * @param		pdwCacheFlag: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 * @version		NOV-23-2007 [InHwan Choi] add ffat_ea_setCreateTime()
 * @version		SEP-19-2008 [DongYoung Seo] release lock when ffat_symlink_setStatus() is failed
 * @version		DEC-31-2008 [JeongWoo Park] change the sequence of log record after SYM/EA
 * @version		MAR-18-2009 [GwangOk Go] can use extended attribute on symlink
 * @version		APR-01-2009 [DongYoung Seo] bug fix, CQID:FLASH00021232, 
 *										add HPA invoking routine to check HPA root
 */
FFatErr
ffat_addon_setStatus(Node* pNode, FFatNodeStatus* pStatus,
						FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	if (ffat_debug_isDubugFile(pNode) == FFAT_TRUE)
	{
		r = FFAT_EACCESS;
		goto out;
	}

	r = ffat_hpa_setStatus(pNode);
	FFAT_ER(r, (_T("fail to set status on HPA")));

	// After EA / SYMLINK operation, do LOG operation
	//		EA / SYMLINK 모듈에서 자체적인 Create time을 변경하게 되지만
	//		이는 Critical하지 않으므로 log recovery 대상에서 제외
	//      [en] Create time is changed by itself at EA / SYMLINK module, 
	//           however, it is excepting log recovery because this is not critical. 
	r = ffat_ea_setStatus(pNode, pStatus, pCxt);
	if ((r < 0) && (r != FFAT_ENOXATTR))
	{
		FFAT_PRINT_CRITICAL((_T("fail to set status at EA Module")));
		goto out;
	}

	r = ffat_spfile_setStatus(pNode, pStatus, pCxt);
	FFAT_EO(r, (_T("fail to set status at Symlink module")));

	r = ffat_log_setStatus(pNode, pStatus, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

	r = ffat_xde_setStatus(pNode, pStatus->dwAttr, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * this function is called after node status change operation.
 *
 * @param		pNode		: [IN] node pointer
 * @param		bSuccess	: [IN]	FFAT_TRUE : operation success
 *									FFAT_FALSE : operation fail
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_afterSetStatus(Node* pNode, t_boolean bSuccess, ComCxt* pCxt)
{
	FFatErr		r;

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));
	
	if (bSuccess == FFAT_FALSE)
	{
		r = ffat_log_operationFail(NODE_VOL(pNode), LM_LOG_SET_STATE, pNode, pCxt);
		FFAT_EO(r, (_T("fail to reset log operation")));
	}

out:
	r |= _PUT_LOCK(pCxt);

	return r;
}


/**
 * create symlink node & write symlink info
 *
 * @param		pNodeParent	: [IN] parent node pointer
 * @param		pNodeChild	: [IN] child node pointer
 * @param		psName		: [IN] node name
 * @param		psPath		: [IN] target path
 * @param		dwFlag		: [IN] node creation flag
 * @param		pAddonInfo	: [IN] info of ADDON node (may be NULL)
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-27-2007 [DongYoung Seo] modify for Multi-thread.
 */
FFatErr
ffat_addon_createSymlink(Node* pNodeParent, Node* pNodeChild, t_wchar* psName,
							t_wchar* psPath, FFatCreateFlag dwFlag, void* pAddonInfo, ComCxt* pCxt)
{
	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(psPath);

	return ffat_spfile_createSymlink(pNodeParent, pNodeChild, psName, psPath, dwFlag, pAddonInfo, pCxt);
}


/**
 * read symlink info & get target path
 *
 * @param		pNode		: [IN] node pointer
 * @param		psPath		: [OUT] target path
 * @param		dwLen		: [IN] length of psPath, in character count
 * @param		pdwLen		: [OUT] count of character stored at psPath
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 */
FFatErr
ffat_addon_readSymlink(Node* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen, ComCxt* pCxt)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psPath);
	FFAT_ASSERT(NODE_ADDON_FLAG(pNode) & ADDON_NODE_SYMLINK);
	FFAT_ASSERT(dwLen > 0);
	FFAT_ASSERT(pdwLen);

	return ffat_spfile_readSymlink(pNode, psPath, dwLen, pdwLen, NULL, pCxt);
}


/**
 * check node is symlink
 *
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 */
FFatErr
ffat_addon_isSymlink(Node* pNode)
{
	return SYMLINK_IS_SYMLINK(pNode);
}


/**
 * get node status information from short file name entry 
 * if the node has extended attribute, return true ctime
 *
 * @param		pNode		: [IN] node pointer
 * @param		pDE			: [IN] Short File Name entry pointer
 * @param		dwDeCluster	: [IN] DE가 있는 cluster number  (parent first cluster가 아님)
 * [en] param	dwDeCluster	: [IN] cluster number including DE (this is not parent first cluster)
 * @param		dwDeOffset	: [IN] DE offset from the parent first cluster
 * @param		pStatus		: [OUT] Node status
 * @param		pCxt		: [IN] context of current operation
 * @author		InHwan Choi
 * @version		NOV-23-2007 [InHwan Choi] First Writing.
 * @version		NOV-04-2008 [DongYoung Seo] shift left 16 bit for access date
 * @version		DEC-23-2008 [JeongWoo Park] modify for symlink bug
 * @version		FEB-03-2009 [GwangOk Go] use CrtTimeTenth as symlink signature
 * @version		MAR-18-2009 [DongYoung Seo] add pStatus->dwAllocSize
 * @version		JUN-17-2009 [GwangOk Go] add fifo, socket
 * @version		OCT-20-2009 [JW Park] Add the consideration about dirty-size state of node
 */
FFatErr
ffat_addon_getStatusFromDe(Node* pNode, FatDeSFN* pDE, t_uint32 dwDeCluster,
						t_uint32 dwDeOffset, FFatNodeStatus* pStatus, ComCxt* pCxt)
{
	FFatErr		r;
	t_uint16	wCrtDate;
	t_uint16	wCrtTime;
	t_uint8		bCrtTimeTenth;
	t_uint32	dwAllocSizeEA;		// allocated size of EA
	Vol*		pVol;
	
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDE);
	FFAT_ASSERT(pStatus);

	pVol = NODE_VOL(pNode);

	pStatus->dwAttr		= FFATFS_GetDeAttr(pDE);

	r = ffat_ea_getCreateTime(pNode, pDE, &wCrtDate, &wCrtTime, pCxt);
	IF_UK ((r < 0) && (r != FFAT_ENOXATTR))
	{
		FFAT_ER(r, (_T("fail to get create time from xde")));
	}
	if (r == FFAT_OK)
	{
		// use creation date/time in extended cluster
		pStatus->dwCTime	= ((t_uint32)wCrtDate << 16) | (t_uint32)wCrtTime;
	}
	else
	{
		// use creation date/time in DE
		pStatus->dwCTime	= (FFATFS_GetDeCDate(pDE) << 16) | FFATFS_GetDeCTime(pDE);
	}

	r = ffat_ea_getAllocSize(pNode, &dwAllocSizeEA, pCxt);
	FFAT_ER(r, (_T("Fail to get allocated size of EA")));

	r = ffat_spfile_afterLookup(pNode, &bCrtTimeTenth, pCxt);
	FFAT_ER(r, (_T("fail to get create time from Symlink")));

	switch (NODE_ADDON_FLAG(pNode) & ADDON_NODE_SPECIAL_FILES)
	{
	case ADDON_NODE_SYMLINK:
		// set symlink flag
		pStatus->dwAttr	|= FFAT_NODE_SYMLINK;

		// use creation time tenth in symlink info
		pStatus->dwCTimeTenth = bCrtTimeTenth;
		break;

	case ADDON_NODE_FIFO:
		// set fifo flag
		pStatus->dwAttr	|= FFAT_NODE_FIFO;

		// creation time tenth is not used
		pStatus->dwCTimeTenth = 0;
		break;

	case ADDON_NODE_SOCKET:
		// set socket flag
		pStatus->dwAttr	|= FFAT_NODE_SOCKET;

		// creation time tenth is not used
		pStatus->dwCTimeTenth = 0;
		break;

	default:
		pStatus->dwCTimeTenth = FFATFS_GetDeCTimeTenth(pDE);
		break;
	}

	// update pStatus
	pStatus->dwCluster		= FFATFS_GetDeCluster(VOL_VI(pVol), pDE);
	pStatus->dwIno1			= dwDeCluster;
	pStatus->dwIno2			= dwDeOffset;

	pStatus->dwSize			= NODE_S(pNode);
	FFAT_ASSERT((NODE_IS_DIRTY_SIZE(pNode) == FFAT_FALSE) ? (NODE_S(pNode) == FFATFS_GetDeSize(pDE)) : FFAT_TRUE);
	
	pStatus->dwATime		= FFATFS_GetDeADate(pDE) << 16;
	pStatus->dwMTime		= (FFATFS_GetDeWDate(pDE) << 16) | FFATFS_GetDeWTime(pDE);
	
	pStatus->dwAllocSize	= (pStatus->dwSize + VOL_CS(pVol) - 1) & (~VOL_CSM(pVol));
	pStatus->dwAllocSize	+= dwAllocSizeEA;		// add allocated size for EA

	return FFAT_DONE;
}


/**
 * this function checks node access permission
 *
 * @param		pNode		: [IN] node pointer,
 * @param		dwFlag		: [IN] New node size
 * @return		FFAT_OK			: it has access permission
 * @return		FFAT_EACCESS	: do not have access permission
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JAN-25-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_addon_isAccessable(Node* pNode, NodeAccessFlag dwFlag)
{
	FFatErr		r;

	FFAT_ASSERT(dwFlag & NODE_ACCESS_MASK);

	r = ffat_log_isAccessable(pNode, dwFlag);

	if (r == FFAT_OK)
	{
		r = ffat_debug_isAccessible(pNode);
	}

	return r;
}


// debug begin
#ifdef FFAT_DEBUG
	/**
	* check Directory Entry information on the Node Structure
	*
	* @param		pNode		: [IN] Node Pointer
	* @param		FFAT_OK		: success
	* @author		DongYoung Seo
	* @version		19-DEC--2008 [DongYoung Seo] First Writing
	*/
	FFatErr
	ffat_addon_checkNodeDeInfo(Node* pNode)
	{
		return ffat_hpa_checkNodeDeInfo(pNode);
	}
#endif
// debug end



//=============================================================================
//	static functions
//

/**
 * initializes command table for ADDON fsctl
 *
 * @param		_pCmdTable		: [IN] command table pointer
 * @return		FFAT_OK			: success but not completed.
 * @return		negative		: error
 * @author		DongYoung Seo
 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
 * @version		FEB-02-2009 [DongYoung Seo] Remove user fast seek
 */
static FFatErr
_initCmdTable(PFN_FSCTL* _pCmdTable)
{
	t_uint32	dwMask;

	dwMask = ~(t_uint32)FFAT_FSCTL_ADDON_BASE;

	_pCmdTable[FFAT_FSCTL_ADDON_BASE & dwMask]		= NULL;
	_pCmdTable[FFAT_FSCTL_CLEAN_NAND & dwMask]		= _fsctlCleanNand;
	_pCmdTable[FFAT_FSCTL_READDIR_UNLINK & dwMask]	= _fsctlReaddirUnlink;
	_pCmdTable[FFAT_FSCTL_READDIR_GET_NODE & dwMask]= _fsctlReaddirGetNode;
	_pCmdTable[FFAT_FSCTL_READDIR_STAT & dwMask]	= _fsctlReaddirStat;
	_pCmdTable[FFAT_FSCTL_DIR_ENTRY_CACHE & dwMask]	= _fsctlDEC;
	_pCmdTable[FFAT_FSCTL_FORMAT & dwMask]			= _fsctlFormat;
	_pCmdTable[FFAT_FSCTL_CLEAN_DIR & dwMask]		= _fsctlCleanDir;
	_pCmdTable[FFAT_FSCTL_CHKDSK & dwMask]			= _fsctlChkDsk;
	_pCmdTable[FFAT_FSCTL_EXTENDED_ATTRIBUTE & dwMask]	= _fsctlExtendedAttribute;
	_pCmdTable[FFAT_FSCTL_GET_SHORT_NAME & dwMask]	= _fsctlGetShortName;
	_pCmdTable[FFAT_FSCTL_GET_LONG_NAME & dwMask]	= _fsctlGetLongName;
	_pCmdTable[FFAT_FSCTL_SET_GUID_PERM & dwMask]	= _fsctlSetExtendedDE;

	return FFAT_OK;
}


//====================================================================
//	
//	Internal Functions
//


/**
 * fsctl helper function for readdir_stat
 *
 * @param		pParam0		: [IN] node pointer
  * @param		pParam1		: [IN/OUT] readdir state result storage
 * @param		pParam2		: [IN]don't care
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		DongYoung Seo
 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlReaddirStat(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatErr		r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		// Invalid parameter
		return FFAT_EINVALID;
	}

	r = NODE_GET_READ_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_addon_misc_readdirStat((Node*) pParam0, (FFatReaddirStatInfo*) pParam1, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out_vol:
	r |= NODE_PUT_READ_LOCK((Node*)pParam0);

	return r;
}


/**
 * fsctl helper function for readdir_get node information
 *
 * CAUTION: Must be released resource of node after using
 *
 * @param		pParam0		: [IN] node pointer
 * @param		pParam1		: [OUT] readdir get node information storage
 * @param		pParam2		: readdir flag
 * @return		FFAT_OK		: success 
 * @return		negative	: error
 * @author		DongYoung Seo
 * @version		SEP-13-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlReaddirGetNode(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatErr		r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		// Invalid parameter
		return FFAT_EINVALID;
	}

	r = NODE_GET_READ_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_addon_misc_readdirGetNode((Node*) pParam0, (FFatReaddirGetNodeInfo*) pParam1, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out_vol:
	r |= NODE_PUT_READ_LOCK((Node*)pParam0);

	return r;
}


/**
 * fsctl helper function for readdir_get node information
 *
 * @param		pParam0		: [IN] node pointer
 * @param		pParam1		: [OUT] readdir unlink information storage
 * @param		pParam2		: readdir flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		negative	: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlReaddirUnlink(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatErr		r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		// Invalid parameter
		return FFAT_EINVALID;
	}

	r = NODE_GET_WRITE_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_addon_misc_readdirUnlink((Node*) pParam0, (FFatReaddirUnlinkInfo*) pParam1, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out_vol:
	r |= NODE_PUT_WRITE_LOCK((Node*)pParam0);

	return r;

}



/**
 * fsctl helper function for readdir_get node information
 *
 * @param		pParam0		: FFatAddonFormatInfo pointer
 * @param		pParam1		: Not used
 * @param		pParam2		: Not used
 * @return		FFAT_OK		: success
 * @return		FFAT_OK		: success 
 * @return		negative	: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlFormat(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
#ifdef NS_CONFIG_LINUX

	FFAT_ASSERTP(0, (_T("Never reach here")));
	return FFAT_EPROG;

#else
	FFatErr		r;

	IF_UK (pParam0 == NULL)
	{
		// Invalid parameter
		return FFAT_EINVALID;
	}

	r = _GET_LOCK(pCxt);
	FFAT_ER(r, (_T("fail to lock ADDON")));

	r = ffat_addon_format((FFatFormatInfo*) pParam0, pCxt);

	r |= _PUT_LOCK(pCxt);

	return r;
#endif

}

/**
 * fsctl helper function for clean dir
 *
 * @param		pParam0		: [IN] node pointer
 * @param		pParam1		: Don't care
 * @param		pParam2		: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		DongYoung Seo
 * @version		SEP-13-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlCleanDir(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	Node*		pNode;
	FFatErr		r;

	IF_UK (pParam0 == NULL)
	{
		return FFAT_EINVALID;
	}

	pNode = (Node*)pParam0;

	// lock node
	r = NODE_GET_WRITE_LOCK(pNode);
	FFAT_ER(r, (_T("fail to get node write lock")));

	// lock volume
	r = VOL_GET_READ_LOCK(NODE_VOL(pNode));
	FFAT_EOTO(r, (_T("fail to get vol read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_addon_misc_cleanDir(pNode, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL(pNode));

out_vol:
	r |= NODE_PUT_WRITE_LOCK(pNode);

	return r;
}


/**
* fsctl sub routine for chkdsk
*
* @param		pParam0		: [IN] volume pointer
* @param		pParam1		: [IN] chkdsk flag
* @param		pParam2		: Don't care
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success 
* @return		Negative	: error
* @author		DongYoung Seo
* @version		SEP-13-2006 [DongYoung Seo] First Writing.
*/
static FFatErr
_fsctlChkDsk(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatChkdskFlag	dwFlag;
	FFatErr			r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		return FFAT_EINVALID;
	}

	r = VOL_GET_WRITE_LOCK((Vol*)pParam0);
	FFAT_ER(r, (_T("fail to get vol write lock")));

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	dwFlag = *(FFatChkdskFlag*)pParam1;

	r = ffat_addon_chkdsk((Vol*) pParam0, dwFlag, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_WRITE_LOCK((Vol*)pParam0);
	
	return r;
}


/**
 * fsctl helper function to get short file name from long file name
 * 
 * @param		pParam0		: [IN/OUT] FFatGetShortLongName structure pointer
 * @param		pParam1		: Don't care
 * @param		pParam2		: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		GwangOk Go
 * @version		NOV-15-2007 [GwangOk Go] First Writing.
 */
static FFatErr
_fsctlGetShortName(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
#ifdef FFAT_VFAT_SUPPORT
	IF_UK (pParam0 == NULL)
	{
		return FFAT_EINVALID;
	}

	return ffat_addon_misc_getShortLongName((FFatGetShortLongName*)pParam0,
						FFAT_FSCTL_GET_SHORT_NAME, pCxt);
#else
	return FFAT_ENOSUPPORT;
#endif
}

/**
 * fsctl helper function to get long file name from short file name
 * 
 * @param		pParam0		: [IN/OUT] FFatGetShortLongName structure pointer
 * @param		pParam1		: Don't care
 * @param		pParam2		: Don't care
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		GwangOk Go
 * @version		NOV-15-2007 [GwangOk Go] First Writing.
 */
static FFatErr
_fsctlGetLongName(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
#ifdef FFAT_VFAT_SUPPORT
	IF_UK (pParam0 == NULL)
	{
		return FFAT_EINVALID;
	}

	return ffat_addon_misc_getShortLongName((FFatGetShortLongName*)pParam0,
						FFAT_FSCTL_GET_LONG_NAME, pCxt);
#else
	return FFAT_ENOSUPPORT;
#endif
}


/**
 * fsctl helper function for clean dir
 *
 * @param		pParam0		: [IN] 
 * @param		pParam1		: [IN] 
 * @param		pParam2		: [IN] 
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		DongYoung Seo
 * @version		SEP-13-2006 [DongYoung Seo] First Writing.
 */
static FFatErr	
_fsctlCleanNand(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatErr		r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	// get volume write lock
	r = VOL_GET_WRITE_LOCK((Vol*)pParam0);
	FFAT_ER(r, (_T("fail get get volume write lock")));

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	r = ffat_nand_cleanNand((Vol*)pParam0, (FFatCleanNand*)pParam1, pCxt);

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_WRITE_LOCK((Vol*)pParam0);

	return r;
}


/**
 * fsctl helper function for Directory Entry Cache
 *
 * @param		pParam0		: [IN/OUT] node pointer
 * @param		pParam1		: [IN] flag for DEC
 * @param		pParam2		: [IN] structure pointer for DEC
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		DongYoung Seo
 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
 */
static FFatErr
_fsctlDEC(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatDECFlag		dwFlag;
	FFatErr			r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	r = NODE_GET_WRITE_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	dwFlag = *((FFatDECFlag*)pParam1);

	if (dwFlag == FFAT_DEC_SET)
	{
		r = ffat_dec_setUDEC((Node*) pParam0, (FFatDirEntryCacheInfo*) pParam2);
	}
	else if (dwFlag == FFAT_DEC_RELEASE)
	{
		r = ffat_dec_releaseUDEC((Node*) pParam0);
	}
	else if (dwFlag == FFAT_DEC_GET_INFO)
	{
		r = ffat_dec_getUDECInfo((Node*) pParam0,  (FFatDirEntryCacheInfo*) pParam2);
	}
	else
	{
		r = FFAT_EINVALID;
	}

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out_vol:
	r |= NODE_PUT_WRITE_LOCK((Node*)pParam0);

	return r;
}


/**
 * fsctl support extended attribute 
 *
 * @param		pParam0		: [IN] node pointer
 * @param		pParam1		: [IN] EA flag
 * @param		pParam2		: [IN/OUT] EA info
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		InHwan Choi
 * @version		NOV-15-2007 [InHwan Choi] First Writing.
 */
static FFatErr
_fsctlExtendedAttribute(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatXAttrCmd		dwCmd;
	FFatErr			r;
	t_uint32		udwSize;

	IF_UK (pParam0)
	{
		IF_UK ((VOL_FLAG(NODE_VOL((Node*)pParam0)) & VOL_ADDON_XATTR) == 0)
		{
			// in case, volume does not support extended attribute
			return FFAT_ENOSUPPORT;
		}
	}

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL) || (pParam2 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter.")));
		return FFAT_EINVALID;
	}

	dwCmd = ((FFatXAttrInfo*)pParam1)->dwCmd;

	r = NODE_GET_WRITE_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out_vol);

	r = _GET_LOCK(pCxt);
	FFAT_EO(r, (_T("fail to lock ADDON")));

	switch(dwCmd)
	{
		case FFAT_XATTR_CMD_SET:
			r = ffat_ea_set((Node*)pParam0, (FFatXAttrInfo*)pParam1, pCxt);
			*(t_int32*)pParam2 = 0;
			break;

		case FFAT_XATTR_CMD_GET:
			r = ffat_ea_get((Node*)pParam0, (FFatXAttrInfo*)pParam1, &udwSize, pCxt);
			*(t_int32*)pParam2 = udwSize;
			break;

		case FFAT_XATTR_CMD_REMOVE:
			r = ffat_ea_delete((Node*)pParam0, (FFatXAttrInfo*)pParam1, pCxt);
			*(t_int32*)pParam2 = 0;
			break;

		case FFAT_XATTR_CMD_LIST:
			r = ffat_ea_list((Node*)pParam0, (FFatXAttrInfo*)pParam1, &udwSize, pCxt);
			*(t_int32*)pParam2 = udwSize;
			break;

		default:
			FFAT_LOG_PRINTF((_T("invalid parameter")));
			r = FFAT_EINVALID;
			break;
	}

	r |= _PUT_LOCK(pCxt);

out:
	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out_vol:
	r |= NODE_PUT_WRITE_LOCK((Node*)pParam0);

	return r;
}


/**
 * set extended DE info
 *
 * @param		pParam0		: [IN] node pointer
 * @param		pParam1		: [IN] info of extended directory entry
 * @param		pParam2		: don't care
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success 
 * @return		Negative	: error
 * @author		GwangOk Go
 * @version		AUG-19-2008 [GwangOk Go] First Writing.
 */
static FFatErr
_fsctlSetExtendedDE(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
{
	FFatErr			r;

	IF_UK ((pParam0 == NULL) || (pParam1 == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter.")));
		return FFAT_EINVALID;
	}

	r = NODE_GET_WRITE_LOCK((Node*)pParam0);
	FFAT_ER(r, (_T("fail to get node write lock")));

	r = VOL_GET_READ_LOCK(NODE_VOL((Node*)pParam0));
	FFAT_EOTO(r, (_T("fail to get volume read lock")), out);

	r = ffat_xde_updateXDE((Node*)pParam0, (XDEInfo*)pParam1, pCxt);

	r |= VOL_PUT_READ_LOCK(NODE_VOL((Node*)pParam0));

out:
	r |= NODE_PUT_WRITE_LOCK((Node*)pParam0);

	return r;
}


#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	/**
	 * lock ADDON module
	 *
	 * @param		pCxt			: [IN] context of current operation
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EINVALID	: invalid parameter
	 * @return		FFAT_EPANIC		: fail
	 * @author		DongYoung Seo
	 * @version		DEC-17-2007 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_getLock(ComCxt* pCxt)
	{
		FFatErr		r = FFAT_OK;

		FFAT_ASSERT(pCxt);

	#ifdef _SHARE_LOCK_WITH_FFATFS

		r = FFATFS_Lock(pCxt);
		FFAT_ER(r, (_T("fail to lock ADDON")));

		pCxt->dwLockAddon++;

		pCxt->dwFlag |= COM_FLAG_ADDON_LOCKED;

	#else

		#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
			if ((pCxt->dwFlag & COM_FLAG_ADDON_LOCKED) == 0)
			{
				r = ffat_al_getLock(_MAIN()->pLock);
				if (r == FFAT_OK)
				{
					pCxt->dwFlag |= COM_FLAG_ADDON_LOCKED;
					_MAIN()->dwRefCount++;
				}

				FFAT_DEBUG_ADDON_LOCK_PRINTF("Get, ref:%d \n", _MAIN()->dwRefCount);
			}
			else
			{
				_MAIN()->dwRefCount++;
				FFAT_DEBUG_ADDON_LOCK_PRINTF("Re-Enter, ref:%d \n", _MAIN()->dwRefCount);
			}
		#endif
	#endif

		return r;
	}


	/**
	 * unlock ADDON module
	 *
	 * @param		pCxt			: [IN] context of current operation
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EINVALID	: invalid parameter
	 * @return		FFAT_EPANIC		: fail
	 * @author		DongYoung Seo
	 * @version		DEC-17-2007 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_putLock(ComCxt* pCxt)
	{
		FFatErr		r = FFAT_OK;

		FFAT_ASSERT(pCxt);

	#ifdef _SHARE_LOCK_WITH_FFATFS

		if (pCxt->dwFlag & COM_FLAG_ADDON_LOCKED)
		{
			FFAT_ASSERT(pCxt->dwLockAddon > 0);

			if (--pCxt->dwLockAddon == 0)
			{
				pCxt->dwFlag &= (~COM_FLAG_ADDON_LOCKED);
			}

			r = FFATFS_UnLock(pCxt);
			FFAT_ER(r, (_T("fail to lock ADDON")));
		}
		else
		{
			FFAT_ASSERT(pCxt->dwLockAddon == 0);
		}

	#else
		#if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
			if (pCxt->dwFlag & COM_FLAG_ADDON_LOCKED)
			{
				_MAIN()->dwRefCount--;

				if (_MAIN()->dwRefCount == 0)
				{
					r = ffat_al_putLock(_MAIN()->pLock);
					if (r == FFAT_OK)
					{
						pCxt->dwFlag &= (~COM_FLAG_ADDON_LOCKED);
					}
					FFAT_DEBUG_ADDON_LOCK_PRINTF("Put, ref:%d \n", _MAIN()->dwRefCount);
				}
				else
				{
					FFAT_DEBUG_ADDON_LOCK_PRINTF("Out, ref:%d \n", _MAIN()->dwRefCount);
				}

			}
		#endif	// end of #if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)
	#endif	// end of #ifdef _SHARE_LOCK_WITH_FFATFS

		return r;
	}
#endif		// #if (FFAT_LOCK_TYPE == FFAT_LOCK_MULTIPLE)


#ifdef FFAT_CMP_NAME_MULTIBYTE

	/**
	* check under score and set NODE_NAME_UNDERSCORE
	*
	* @param		pNode		: [IN] Node pointer
	* @param		psName		: [IN] Name string
	* @param		dwLen		: [IN] length of name character
	* @author		DongYoung Seo
	* @version		MAY-12-2009 [DongYoung Seo] First Writing.
	*/
	static void
	_checkUnderScore(Node* pNode, t_wchar* psName, t_int32 dwLen)
	{
		FFAT_ASSERT(pNode);
		FFAT_ASSERT(psName);
		FFAT_ASSERT(dwLen > 0);

		while (--dwLen > 0)
		{
			if (psName[dwLen] == '_')
			{
				pNode->dwFlag |= NODE_NAME_UNDERSCORE;
				return;
			}
		}

		return;
	}

#endif


// debug begin
#ifdef FFAT_DEBUG
	//====================================================================
	//	DEBUG AREA 
	//	Do not lay any function except for debug.
	//


	/**
	* fsctl helper function for Free Cluster Cache
	*
	* @param		pParam0		: [IN/OUT] Volume
	* @param		pParam1		: [IN] flag for FCC
	* @param		pParam2		: [IN] structure pointer for DEC
	* @param		pCxt		: [IN] context of current operation
	* @return		FFAT_OK		: success 
	* @return		Negative	: error
	* @author		DongYoung Seo
	* @version		AUG-07-2006 [DongYoung Seo] First Writing.
	*/
	static FFatErr
	_fsctlFCC(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
	{

		FFatFCCFlag					dwFlag;
		FFatFreeClusterCache*			pFCC;

		pFCC	= (FFatFreeClusterCache*) pParam1;

		dwFlag = *((FFatDECFlag*)pParam2);

		if (dwFlag == FFAT_FCC_SET)
		{
			return ffat_fcc_addFreeClustersFromTo((Vol*) pParam0, pFCC->dwStartVolume,
				pFCC->dwEndVolume, pCxt);
		}
		return FFAT_EINVALID;
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
	_fsctlIFCCH(void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
	{
		IF_UK (pParam0 == NULL)
		{
			FFAT_LOG_PRINTF((_T("Invalid volume pointer")));
			return FFAT_EINVALID;
		}

		IF_UK (VOL_IS_MOUNTED((Vol*)pParam0) == FFAT_FALSE)
		{
			FFAT_LOG_PRINTF((_T("volume is not mounted")));
			return FFAT_EACCESS;
		}

		return ffat_hpa_fsctl(FFAT_FSCTL_INVALIDATE_FCCH, (Vol*)pParam0);
	}


	/**
	 * structure size checking function
	 *
	 * @author		DongYoung Seo
	 * @version		AUG-07-2006 [DongYoung Seo] First Writing.
	 */
	static void
	_external_struct_size_check(void)
	{
		if (sizeof(_stAddonMain) != sizeof(AddonMain))
		{
			FFAT_DEBUG_PRINTF((_T("Incorrect AddonMain struct size, _stAddonMain/AddonMain:%u/%u"), sizeof(_stAddonMain), sizeof(AddonMain)));
			FFAT_ASSERT(0);
		}

		if (sizeof(FFatAddonVol) != sizeof(AddonVol))
		{
			FFAT_DEBUG_PRINTF((_T("Incorrect AddonVol struct size, FFatAddonVol/AddonVol:%u/%u"), sizeof(FFatAddonVol), sizeof(AddonVol)));
			FFAT_ASSERT(0);
		}

		if (sizeof(FFatAddonNode) != sizeof(AddonNode))
		{
			FFAT_DEBUG_PRINTF((_T("Incorrect AddonNode struct size, FFatAddonNode/AddonNode:%d/%d"), sizeof(FFatAddonNode), sizeof(AddonNode)));
			FFAT_ASSERT(0);
		}

		return;
	}

	/**
	* addon configuration checking function
	*
	* @author		JeongWoo Park
	* @version		DEC-16-2008 [JeongWoo Park] First Writing.
	*/
	static void
	_addon_configuration_check(void)
	{
		if (ADDON_BPB_START_OFFSET < ADDON_BPB_START_OFFSET_LIMIT)
		{
			FFAT_DEBUG_PRINTF((_T("Too small ADDON_BPB_START_OFFSET, must be (>= %d)"), ADDON_BPB_START_OFFSET_LIMIT));
			FFAT_ASSERT(0);
		}

		if ((ADDON_BPB_START_OFFSET + ADDON_BPB_SIZE - 1) > ADDON_BPB_END_OFFSET_LIMIT)
		{
			FFAT_DEBUG_PRINTF((_T("Adjust ADDON_BPB_START_OFFSET to fit BPB, the end offset(%d) must be (<= %d)"),
								(ADDON_BPB_START_OFFSET + ADDON_BPB_SIZE - 1), ADDON_BPB_END_OFFSET_LIMIT));
			FFAT_ASSERT(0);
		}

		return;
	}


	/**
	 * FFATFS control for debug
	 *
	 * @param		dwCmd		: filesystem control command
	 * @param		pParam0		: parameter 0
	 * @param		pParam1		: parameter 1
	 * @param		pParam2		: parameter 2
	 * @param		pCxt		: [IN] context of current operation
	 * @author		DongYoung Seo
	 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_debugFSCtl(FatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2, ComCxt* pCxt)
	{
		FFatErr		r;

		r = _GET_LOCK(pCxt);
		FFAT_ER(r, (_T("fail to lock ADDON")));

		if (dwCmd == FFAT_FSCTL_INVALIDATE_FCCH)
		{
			r = _fsctlIFCCH(pParam0, pParam1, pParam2, pCxt);
		}
		else if (dwCmd == FFAT_FSCTL_FREE_CLUSTER_CACHE)
		{
			r = _fsctlFCC(pParam0, pParam1, pParam2, pCxt);
		}
		else
		{
			r = FFAT_ENOSUPPORT;
		}

		r |= _PUT_LOCK(pCxt);

		return r;
	}

#endif
// debug end

