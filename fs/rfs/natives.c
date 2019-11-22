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
 * @file        natives.c
 * @brief       This file includes APIs for accessing Native Filesystems.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "natives.h"

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_NATIVE)

/*
 * function pointer to transmit NativeFS's fs-operation set to LinuxGlue
 * Each NativeFS should define specific finction pointer and initialize it.
 */
/* for BTFS native */
PNATIVEFS_OPERATIONS (*GetNativeBTFS)(void) = NULL;	

/******************************************************************************/
/* NESTLE PRIVATE FUNCTIONS                                                   */
/******************************************************************************/

/**
 * @brief		Get Native filesystem's Data structure
 * @param[in]	pName		the name of filesystem in bytes (rfs_fat...)
 * @returns		the pointer of NativeFS or NULL
 *
 */
inline PNATIVEFS_OPERATIONS
NativefsGetNative(const char *pName)
{
	/* BTFS native */
	if (!strcmp(pName, RFS_BTFS))
	{
		return GetNativeBTFS();
	}

	/* TODO */
	/* If new nativefs is added, define a function pointer and initialize it */

	return NULL;
}

/**
 * @brief		Call native filesystem's init function
 * @param[in]	pNativeFS	Native Filesystem to initialize
 * @returns		FERROR
 * @see			
 */
FERROR
NativefsInitialize(PNATIVEFS_OPERATIONS	pNativeFS)
{
	FERROR nErr = FERROR_NO_ERROR;
	
	if (pNativeFS == NULL)
	{
		nErr = FERROR_PATH_NOT_FOUND;
		LNX_CMZ(("Fail to find Native filesystem (nErr: %08x)", -nErr));

		return nErr;
	}

	if (pNativeFS->pfnNativeInit)
	{
		/* initialize native filesystem */
		nErr = pNativeFS->pfnNativeInit();
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_EMZ(("NativeFS NativeInit fails(nErr: %08x)", -nErr));
		}
	}
	else
	{
        /* fixed 20090219
         * NativeInit is optional. return success */
		LNX_VMZ(("Native filesystem doesn't have the init function"));
	}

	return nErr;
}

/**
 * @brief		Call all native file system's uninit function
 * @param[in]	void
 * @returns		FERROR
 * @see			
 */
FERROR
NativefsUninitialize(
	IN PNATIVEFS_OPERATIONS	pNativeFS)
{
	FERROR				nErr = FERROR_NO_ERROR;

	if (pNativeFS == NULL)
	{
		nErr = FERROR_PATH_NOT_FOUND;
		LNX_CMZ(("Fail to find Native filesystem"));
		return nErr;
	}

	if (pNativeFS->pfnNativeUninit)
	{
		/* uninitialize native filesystem */
		nErr = pNativeFS->pfnNativeUninit();
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_EMZ(("NativeFS NativeUnlink faile(nErr: %08x)", -nErr));
		}
	}
	else
	{
        /* fixed 20090219
         * NativeUninit is optional. return success */
		LNX_VMZ(("Native filesystem doesn't have the uninit function"));
	}

	return nErr;

}

/**
 * @brief		load Nativefs's operation set and mount disk
 * @param[in]		pSb				linux super_block
 * @param[in]		pVcb			Volume control block
 * @prarm[in/out]	pdwMountFlag	mount flag for Nativefs
 * @param[out]		ppRootVnode		root vnode of volume
 * @returns		FERROR
 * @see			
 */
FERROR
NativefsMountDisk(
		PLINUX_SUPER 			pSb,
		PVOLUME_CONTROL_BLOCK	pVcb,
		PMOUNT_FLAG 			pdwMountFlag,
		PVNODE *				ppRootVnode)
{
	FERROR					nErr = FERROR_NO_ERROR;
	PNATIVEFS_OPERATIONS	pNativeFS = NULL;

	LNX_ASSERT_ARG(pSb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pdwMountFlag, FERROR_INVALID);
	LNX_ASSERT_ARG(ppRootVnode, FERROR_INVALID);
	
	/* get nativeFS from table */
	pNativeFS = NativefsGetNative(pSb->s_type->name);
	if (!pNativeFS)
	{
		LNX_CMZ(("Fail to get NativeFS(%s)", pSb->s_type->name));
		return FERROR_INVALID;
	}

	if (NULL == pNativeFS->pfnMountDisk)
	{
		LNX_CMZ(("No Native interface for MountDisk"));
		RFS_ASSERT(0);
		return FERROR_NOT_SUPPORTED;
	}

	/* NativeFS make Root Vnode during mount */
	nErr = pNativeFS->pfnMountDisk(pVcb, pdwMountFlag, ppRootVnode);

	return nErr;
}


/*
 * Define symbols
 */
#include <linux/module.h>

EXPORT_SYMBOL(GetNativeBTFS);
EXPORT_SYMBOL(NativefsInitialize);
EXPORT_SYMBOL(NativefsUninitialize);

// end of file
