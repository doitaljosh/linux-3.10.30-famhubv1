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
 * @file        linux_init_glue.c
 * @brief       This file initializes rfs_glue module.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "rfs_linux.h"
#include "linux_vnode.h"
#include "linux_volume.h"

#include <linux/module.h>
#include <linux/slab.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_API)

/* Slab cache for Vnode */
PLINUX_KMEM_CACHE		g_pVnodeCache = NULL;

/******************************************************************************/
/* EXTERNAL FUNCTIONS                                                         */
/******************************************************************************/

/**
 * @brief		Interface function for super block initialization
 * @param[in]	plxFsType	filesystem type
 * @param[in]	dwFlag		flag
 * @param[in]	pDevName	name of file system
 * @param[in]	pData		private date
 * @return      a pointer of super block on success, negative error code on failure
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 38)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
LINUX_ERROR 
LxGetSb(
	IN	PLINUX_FS_TYPE		plxFsType, 
	IN	int					dwFlag, 
	IN	const char*			pDevName, 
	IN	void*				pData, 
	IN	PLINUX_VFS_MNT		plxMnt)
{
	return LINUX_GetSbBdev(plxFsType, dwFlag, pDevName, pData, VolFillSuper, plxMnt);
}
#else
PLINUX_SUPER 
LxGetSb(
	IN	PLINUX_FS_TYPE		plxFsType, 
	IN	int					dwFlag, 
	IN	const char*			pDevName, 
	IN	void*				pData)
{
	return LINUX_GetSbBdev(plxFsType, dwFlag, pDevName, pData, VolFillSuper);
}
#endif /* RFS_FOR_2_6_18 */


EXPORT_SYMBOL(LxGetSb);
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
PLINUX_DENTRY LxMount(
	IN      PLINUX_FS_TYPE          plxFsType,
        IN      int			dwFlag,
        IN      const char*		pDevName, 
        IN      void*			pData)
{
	return LINUX_MountFs(plxFsType, dwFlag, pDevName, pData, VolFillSuper);
}


EXPORT_SYMBOL(LxMount); 
#endif


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/
/**
 * @brief		Function to initialized inode slab
 * @param[in]	pFoo		memory pointer for new inode structure
 * @param[in]	pCache		a pointer for inode cache
 * @param[in]	nFlags		control flag
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
static void 
_LxInitInodeOnce(
	IN	void*				pFoo)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void 
_LxInitInodeOnce(
	IN	PLINUX_KMEM_CACHE	pCache, 
	IN	void*				pFoo)
#else
static void 
_LxInitInodeOnce(
	IN	void*				pFoo, 
	IN	PLINUX_KMEM_CACHE	pCache, 
	IN	unsigned long		nFlags)
#endif
{
	PVNODE pVnode = (PVNODE) pFoo;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
	LINUX_InodeInitOnce(&pVnode->stLxInode);
#else
	if ((nFlags & (LINUX_SLAB_CTOR_VERIFY | LINUX_SLAB_CTOR_CONSTRUCTOR)) ==
			LINUX_SLAB_CTOR_CONSTRUCTOR)
	{
		LINUX_InodeInitOnce(&pVnode->stLxInode);
	}
#endif

}

/**
 * @brief		Function to initialize an inode cache
 */
static LINUX_ERROR __init 
_LxInitSlabCache(void)
{
	if (g_pVnodeCache != NULL)
	{
		RFS_ASSERT(0);
		return 0;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	g_pVnodeCache = LINUX_KmemCacheCreate("rfs_vnode_cache",
						sizeof(struct _VNODE),
						0, 
						(LINUX_SLAB_RECLAIM_ACCOUNT | LINUX_SLAB_MEM_SPREAD),
						_LxInitInodeOnce);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
	g_pVnodeCache = LINUX_KmemCacheCreate("rfs_vnode_cache",
						sizeof(struct _VNODE),
						0, 
						LINUX_SLAB_RECLAIM_ACCOUNT,
						_LxInitInodeOnce);
#else
	g_pVnodeCache = LINUX_KmemCacheCreate("rfs_vnode_cache",
						sizeof(struct _VNODE),
						0, 
						LINUX_SLAB_RECLAIM_ACCOUNT,
						_LxInitInodeOnce, 
						NULL);
#endif

	if (!g_pVnodeCache)
	{
		LNX_SMZ(("Creating cache for vnode fails"));
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief		Function to destroy an inode cache
 */
static void 
_LxInitDestroySlabCache(void)
{
	/*
	 * LINUX_KmemCacheDestroy return type is changed
	 * from 'int' to 'void' after 2.6.19
	 */
	LINUX_KmemCacheDestroy(g_pVnodeCache);
	g_pVnodeCache = NULL;

}

/**
 * @brief		Interface function for registration rfs
 */
static LINUX_ERROR __init 
LxInitModuleInit_RFS(void)
{
	LINUX_ERROR		dwLinuxError = 0;

	/* init inode cache */
	dwLinuxError = _LxInitSlabCache();
	if (dwLinuxError)
	{
		LNX_SMZ(("Initializing slab for RFS fails"));
		return dwLinuxError;
	}

	return 0;
}

/**
 * @brief		Interface function for unregistration rfs
 */
static void __exit 
LxInitModuleExit_RFS(void)
{
	_LxInitDestroySlabCache();
}

/*****************************************************************************/
/* NESTLE PRIVATE FUNCTIONS													 */
/*****************************************************************************/

/**
 * @brief		Interface function for allocating a VNODE
 * @param[in]	pSb		linux super block
 * @return      a pointer of linux inode in VNODE
 */
PLINUX_INODE 
LxAllocateInode(
	IN	PLINUX_SUPER		pSb)
{
	PVNODE		pNewInode = NULL;

	RFS_ASSERT(pSb);

	/* do nothing in nestle */
	pNewInode = LINUX_KmemCacheAlloc(g_pVnodeCache, LINUX_GFP_NOFS);
	if (!pNewInode)
	{
		LNX_SMZ(("Allocating cache for inode fails"));
		return NULL;
	}

	return &pNewInode->stLxInode;
}

/**
 * @brief		Interface function for super block initialization
 * @param[in]	pInode		a pointer of linux inode in Vnode
 * @return      void
 */
void 
LxDestroyInode(
	IO	PLINUX_INODE		pInode)
{
	PVNODE pVnode;
	
    LNX_ASSERT_ARGV(pInode);

	pVnode = VnodeGetVnodeFromInode(pInode);
    LNX_ASSERT_ARGV(pVnode);

	/* skip if root node */
	if (pVnode->pVcb->pRoot == pVnode)
	{
		return;
	}

	/* release memory for Native node */
	if (pVnode->pVnodeOps && pVnode->pVnodeOps->pfnDestroyNode)
	{
		FERROR nErr;

		/* call native operation(DestoryNode) */
		nErr = pVnode->pVnodeOps->pfnDestroyNode(pVnode);
		if (nErr != FERROR_NO_ERROR)
		{
			LNX_EMZ(("NativeFS DestoryNode fails(nErr: 0x%08x)", -nErr));
		}
	}
	else
	{
		LNX_CMZ(("No Native operation for destroying Node"));
		RFS_ASSERT(0);
	}

	/* release cache memory for Vnode (and inode) */
	LINUX_KmemCacheFree(g_pVnodeCache, pVnode);
}


module_init(LxInitModuleInit_RFS);
module_exit(LxInitModuleExit_RFS);

MODULE_LICENSE("Samsung, Proprietary");
// end of file
