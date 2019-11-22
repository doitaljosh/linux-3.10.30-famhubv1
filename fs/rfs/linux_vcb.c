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
 * @file        linux_vcb.c
 * @brief       This file includes volume control operations.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */


#include "ns_misc.h"
#include "linux_vcb.h"
#include "linux_vnode.h"
#include "linux_xattr.h"

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_VCB)


/******************************************************************************/
/* INTERNAL FUNCTIONS                                                         */
/******************************************************************************/
/**
 * @brief		Get a Hash Value 
 * @param[in]	pVcb		contains a hash table
 * @param[in]	llVnodeID	Vnode ID to get a hash value.
 * @returns 	hash value
 */
unsigned long
VcbHash(
	IN	PVOLUME_CONTROL_BLOCK	pVcb, 
	IN	unsigned long long		llVnodeID)
{
	unsigned long long llValue = llVnodeID | (unsigned long) pVcb;

	llValue = llValue + (llValue >> VNODE_HASH_SIZE_BITS) +
		(llValue >> VNODE_HASH_SIZE_BITS * 2);

	LNX_VMZ(("Vcb hash value %016llx %016llx %016llx",
			llVnodeID, llValue, (llValue & (unsigned long) VNODE_HASH_MASK)));
    
	return ((unsigned long) llValue & (unsigned long) VNODE_HASH_MASK);
}

/**
 * @brief		Find Vnode by Vnode ID
 * @param[in]	pVcb		contains vnodes
 * @param[in]	llVnodeID	Vnode ID to look for
 * @returns		pointer of Vnode found
 */
PVNODE
VcbFindVnode(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	unsigned long long		llVnodeID)
{
	PVNODE              pVnode = NULL;
	PLINUX_HLIST_HEAD   phlHead = NULL;
	PLINUX_INODE		pInode = NULL;

	/* get the head of hash list */
	phlHead = pVcb->aHashTable + VcbHash(pVcb, llVnodeID);

	/* get spinlock before searching hash list */
	LINUX_SpinLock(&pVcb->spinHash);

	/* look for the proper vnode in the hash list */
	LINUX_HLIST_FOR_EACH(pVnode, phlHead, hleHash)
	{
		/* if find vnode, get the inode of vnode */
		if (pVnode->llVnodeID == llVnodeID)
		{
			pInode = LINUX_GrabInode(&pVnode->stLxInode);
			if (pInode)
			{
				// found
				LNX_DMZ(("Vnode is found %016llx", pVnode->llVnodeID));
				break;
			}
		}
	}

	LINUX_SpinUnlock(&pVcb->spinHash);

	/* found */
	if (pVnode && (pVnode->llVnodeID == llVnodeID)
			&& (&(pVnode->stLxInode) == pInode))
	{
		/* check i_count of assigned inode */
		RFS_ASSERT(0 < LINUX_AtomicRead(&pVnode->stLxInode.i_count));
	}
	else
	{
		/* if not find vnode, return NULL */
		pVnode = NULL;
	}

	return pVnode;
}

/******************************************************************************/
/* NESTLE PUBLIC VCB FUNCTION                                                 */
/******************************************************************************/

/**
 * @brief		Set operation set of Volume Control Block
 * @param[in]	pVcb			VFS Volume Control Block
 * @param[in]	pVolumeOps		volume control opearation set
 * @returns
 *		- void
 * @remarks 
 * 
 */
void
VcbSetOperation(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	void*					pVolumeOps)
{
	LNX_ASSERT_ARGV(pVcb);
	LNX_ASSERT_ARGV(pVolumeOps);

	pVcb->pVolumeOps = (PVCB_OPERATIONS) pVolumeOps;

	return;
}

/**
 * @brief		Set the block's size(in bytes) and the bit size of Volume Control Block
 * @param[in]	pVcb			VFS Volume Control Block
 * @param[in]	dwBlockSize		the size of block in bytes
 * @returns
 *		- void
 * @remarks 
 * 
 */
FERROR
VcbSetBlockSize(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	unsigned int			dwBlockSize)
{
	int result;

	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVcb->pSb, FERROR_NOT_INITIALIZED);
	LNX_ASSERT_ARG(dwBlockSize, FERROR_INVALID);

	LNX_VMZ(("block size: %u", dwBlockSize));

	result = LINUX_SbSetBlocksize(pVcb->pSb, dwBlockSize);
	if (!result)
	{
		/* fail to Set block size */
		LNX_SMZ(("fail to set blocksize: %u", dwBlockSize));
		return FERROR_INVALID;
	}

	return FERROR_NO_ERROR;
}


/**
 * @brief		Set the Root Vnode of Volume Control Block
 * @param[in]	pVcb		Volume Control Block
 * @param[in]	pVnode		Root vnode of Volume control block
 * @returns
 *		- FERROR_INSUFFICIENT_MEMORY
 *		- FERROR_NO_ERROR
 * @remarks 
 * 
 */
FERROR
VcbSetRoot(
	IN	PVOLUME_CONTROL_BLOCK 	pVcb,
	IN	PVNODE 					pVnode)
{
	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVcb->pSb, FERROR_NOT_INITIALIZED);
	LNX_ASSERT_ARG(pVnode, FERROR_INVALID);

	RFS_ASSERT(pVnode->stLxInode.i_sb);

	/* allocate dentry for the root */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	pVcb->pSb->s_root = LINUX_DAllocRoot(&pVnode->stLxInode);
#else
	pVcb->pSb->s_root = LINUX_DMakeRoot(&pVnode->stLxInode);
#endif	
	if (NULL == pVcb->pSb->s_root)
	{
		LNX_SMZ(("Allocating root dentry fails"));

		return FERROR_INSUFFICIENT_MEMORY;
	}

	/* link pVnode to pRoot */
	pVnode->pstLxDentry = pVcb->pSb->s_root;
	pVcb->pRoot = pVnode;

	/* external mount status */
	if (IS_VCB_EXTERNAL(pVcb))
	{
		LINUX_MODE dwMode = pVnode->stLxInode.i_mode;

		dwMode |= ACL_RWX;
		dwMode &= ~pVcb->stExtOpt.wFsDmask;

		pVnode->stLxInode.i_mode = dwMode;
		pVnode->stLxInode.i_uid = pVcb->stExtOpt.dwFsUid;
		pVnode->stLxInode.i_gid = pVcb->stExtOpt.dwFsGid;
	}

	return FERROR_NO_ERROR;
}

/**
 * @brief		Find the vnode with index, or create new vnode from index
 * @param[in]	pVcb			Volume control block pointer
 * @param[in]	llVnodeID		unique Index of vnode
 * @param[in]	pParentVnode	parent vnode
 * @param[in]	pFileOps		file operation set to be used for creation
 * @param[in]	pVnodeOps		node operation set to be used for creation
 * @param[out]	pNew			flag whether vnode is created now
 * @returns
 *		- pVnode	found or created vnode
 * @remarks 
 *      
 */
PVNODE 
VcbFindOrCreateVnode(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	unsigned long long		llVnodeID,
	IN	PVNODE					pParentVnode, /* if root, canbe NULL */
	IN	void*					pFileOps,
	IN	void*					pVnodeOps,
	OUT	unsigned int*			pNew)
{
	PVNODE			pVnode;

	LNX_ASSERT_ARG(pVcb, NULL);
	LNX_ASSERT_ARG(pFileOps, NULL);
	LNX_ASSERT_ARG(pVnodeOps, NULL);
	LNX_ASSERT_ARG(pNew, NULL);

	if (pParentVnode && (pParentVnode->pVcb != pVcb))
	{
		LNX_CMZ(("Parent's Vcb and Vcb isn't the same."));
		RFS_ASSERT(0);
		return NULL;
	}

	LNX_VMZ(("vcb find or create vnode %016llx, %d pid",
			llVnodeID, LINUX_g_CurTask->pid));

	/* try to find vnode with VnodeID */
	pVnode = VcbFindVnode(pVcb, llVnodeID);

	/* if not found, allocate new one */
	if (NULL == pVnode)
	{
		LNX_VMZ(("Vnode is not found. allocate a new one %016llx",
				llVnodeID));

		/* create and initialize new vnode */
		pVnode = VnodeCreate(pVcb);
		
		if (pVnode)
		{
			VnodeInitialize(pVnode,
								llVnodeID,
								pVcb,
								pParentVnode,
								pFileOps,
								pVnodeOps);

			/* set the flag if vnode is newly created */
			*pNew = TRUE;
		}
		else
		{
			LNX_SMZ(("Creating New Vnode fails"));
			*pNew = FALSE;
			/* pVnode is NULL */
		}
	}
	/* if found */
	else
	{
		LNX_VMZ(("Vnode is found %016llx", pVnode->llVnodeID));

		/* clear the flag if vnode already exists */
		*pNew = FALSE;
	}

	return pVnode;
}

/**
 * @brief		Mark dirty flag at volume
 * @param[in]	pVcb			Volume control block pointer
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
FERROR
VcbMarkDirty(
	IN  PVOLUME_CONTROL_BLOCK	pVcb)
{
	LNX_ASSERT_ARG(pVcb, FERROR_INVALID);
	LNX_ASSERT_ARG(pVcb->pSb, FERROR_INVALID);

	/* need sb lock? -> fixed 20090116 */

	LINUX_LockSuper(pVcb->pSb);
	pVcb->pSb->s_dirt = 1;
	LINUX_UnlockSuper(pVcb->pSb);

	return FERROR_NO_ERROR;
}
#endif

/**
 * Notify the change of free block count to Nestle
 * @brief		Notify the change of free block count to Nestle
 * @param[in]	pVcb			vcb for mounted volume
 * @param[in]	dwTotalCount	The total block count
 * @param[in]	dwFreeCount		The free block count
 * @param[in]	dwBlockSize		The block size
 */
void
VcbNotifyFreeBlockCount(
	IN	PVOLUME_CONTROL_BLOCK	pVcb,
	IN	unsigned int	dwTotalCount,
	IN	unsigned int	dwFreeCount,
	IN	unsigned int	dwBlockSize)
{

#ifdef RFS_CLUSTER_CHANGE_NOTIFY
	if (gfpClusterUsageNotify)
	{
		if (pVcb)
			gfpClusterUsageNotify(dwBlockSize, dwTotalCount, dwFreeCount, pVcb->pSb->s_id);
		else
			gfpClusterUsageNotify(dwBlockSize, dwTotalCount, dwFreeCount, "");
	}
#endif

	return;
}

/******************************************************************************/
/* NESTLE PRIVATE VCB FUNCTION                                                */
/******************************************************************************/
/**
 * @brief		Create a Volume Control Block
 * @param[io]	pSb			linux super block
 * @param[in]	dwFlag		refer to MOUNT_FLAG
 * @param[in]	pNlsIo		linux NLS table
 * @param[in]	pstExtOpt	extended mount option for GLUE layer
 * @returns
 *		- PVOLUME_CONTROL_BLOCK : created VCB pointer
 *	
 */
PVOLUME_CONTROL_BLOCK 
VcbCreate(
	IO	PLINUX_SUPER		pSb,
	IN	MOUNT_FLAG			dwMntFlag,
	IN	unsigned int		dwPrvFlag,
	IN	PLINUX_NLS_TABLE	pNlsIo,
	IN	PEXT_MOUNT_OPT		pstExtOpt)
{
	PVOLUME_CONTROL_BLOCK	pVcb = NULL;
	int						i;

	LNX_ASSERT_ARG(pSb, NULL);
	LNX_ASSERT_ARG(pNlsIo, NULL);

	/* allocate VCB memory */
	pVcb = RtlAllocMem(sizeof(VOLUME_CONTROL_BLOCK));
	if (pVcb == NULL)
	{
		LNX_SMZ(("Allocating memory for VCB fails"));
		return NULL;
	}

	RtlFillMem(pVcb, 0, sizeof(VOLUME_CONTROL_BLOCK));

	/*
	 * additional superblock setup
	 */
	pSb->s_fs_info	= pVcb;
	pSb->s_magic	= 0;	/* magic number */
	/* no access time update */
	pSb->s_flags	|= (LINUX_MS_NOATIME | LINUX_MS_NODIRATIME);
	/*
	 * s_maxbytes limits the maximum file size for write or truncate
	 * If necessary, NativeFS need to set the specific value.
	 */
	pSb->s_maxbytes	= LINUX_LLONG_MAX;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	/*
	 * s_dirt is set by NativeFS.
	 * If s_dirt is not zero, Linux VFS calls write_super() with superblock mutex.
	 * Inside write_super(), s_dirt should be zero. (be clear) 
	 */
	pSb->s_dirt		= 0;
#endif

	/* operation sets */
	pSb->s_op		= &g_stLinuxSuperOps;		/* superblock operation table */
	pSb->s_xattr	= g_stLinuxXattrHandlers;	/* xattr handler table */

	/* initialize VCB */
	pVcb->pSb			= pSb;
	pVcb->pNlsTableIo	= pNlsIo;
	pVcb->pNlsTableDisk	= NULL;
	pVcb->pRoot			= NULL;
	pVcb->pVolumeOps	= NULL;
	pVcb->dwFlag		= dwMntFlag;
	pVcb->pDisk			= (PLOGICAL_DISK) pSb->s_bdev;
	pVcb->pNative		= NULL;			/* Native FS */
	pVcb->dwPrvFlag		= dwPrvFlag;	/* private flag besides mount flag(dwFlag) */

	/* extended mount option */
	memcpy(&pVcb->stExtOpt, pstExtOpt, sizeof(EXT_MOUNT_OPT));

	/* initialize spin lock for vnode hash */
	LINUX_SpinLockInit(&pVcb->spinHash);

	/* initialize vnode hash list */
	for (i = 0; i < VNODE_HASH_SIZE; i++)
	{
		LINUX_INIT_HLIST_HEAD(&pVcb->aHashTable[i]);
	}

	return pVcb;
}

/**
 * @brief		Destroy a Volume Control Block
 * @param[in]	Vcb	pointer to destroy
 * @returns
 *		- void
 */
void
VcbDestroy(
	IN	PVOLUME_CONTROL_BLOCK	pVcb)
{
	LNX_ASSERT_ARGV(pVcb);

	pVcb->pSb->s_fs_info = NULL;

	RtlFreeMem(pVcb);

	return;
}

/**
 * @brief		Insert a Vnode into the hash table in Vcb
 * @param[in]	pVcb	contains a hash table
 * @param[in]	pVnode	Vnode pointer to insert
 * @returns
 *		- void
 */
void
VcbInsertHash(
	IN  PVOLUME_CONTROL_BLOCK   pVcb,
    IN  PVNODE                  pVnode)
{
	unsigned long dwHash;

	LNX_ASSERT_ARGV(pVcb);
	LNX_ASSERT_ARGV(pVnode);

	/* get spinlock before handling hash list */
	LINUX_SpinLock(&pVcb->spinHash);

	/* get hash value of vnode ID */
	dwHash = VcbHash(pVcb, pVnode->llVnodeID);

	/* add vnode to hash list */
	LINUX_HLIST_ADD_HEAD(&pVnode->hleHash, (pVcb->aHashTable + dwHash));

	/* unlock spinlock after handling hash list */
	LINUX_SpinUnlock(&pVcb->spinHash);

	return;
}

/**
 * @brief		Remove a Vnode from the hash table in Vcb
 * @param[in]	pVcb	contains a hash table
 * @param[in]	pVnode	Vnode pointer to insert
 * @returns
 *		- void
 */
void
VcbRemoveHash(
	IN  PVOLUME_CONTROL_BLOCK   pVcb,
	IN  PVNODE                  pVnode)
{
	LNX_ASSERT_ARGV(pVcb);
	LNX_ASSERT_ARGV(pVnode);

	/* get spinlock before handling hash list */
	LINUX_SpinLock(&pVcb->spinHash);

	/* remove vnode from hash list */
	LINUX_HLIST_DEL_INIT(&pVnode->hleHash);

	/* unlock spinlock after handling hash list */
	LINUX_SpinUnlock(&pVcb->spinHash);

	return;
}

/**
 * @brief		set dentry operations
 * @param[in]	pVcb	vcb pointer to check
 * @return		0 if use Linux VFS's dentry operation, or 1
 */
int
VcbSetDentryOps(
	IN	PVOLUME_CONTROL_BLOCK	pVcb)
{

	PLINUX_SUPER	pSb = pVcb->pSb;

	if (!strcmp(pSb->s_type->name, RFS_BTFS))
	{
		if (!(pVcb->dwFlag & MOUNT_ALLOW_OS_NAMING_RULE))
		{
			// case insensitive
			pSb->s_root->d_op = &g_stLinuxDentryOpsI;
		}
		// 20090807 invalid code
#ifdef SUPPORT_VFAT_OFF
		else if (pVcb->dwPrvFlag & PRIVATE_VCB_VFAT_OFF)
		{
			// sfn only
			pSb->s_root->d_op = &g_stLinuxDentryOpsSFN;
		}
#endif
	}

	return 1;
}

/*
 * Define symbols
 */
#include <linux/module.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
EXPORT_SYMBOL(VcbMarkDirty);
#endif
EXPORT_SYMBOL(VcbSetNative);
EXPORT_SYMBOL(VcbGetNative);
EXPORT_SYMBOL(VcbGetRoot);
EXPORT_SYMBOL(VcbSetOperation);
EXPORT_SYMBOL(VcbSetBlockSize);
EXPORT_SYMBOL(VcbGetBlockSize);
EXPORT_SYMBOL(VcbGetBlockSizeBits);
EXPORT_SYMBOL(VcbGetLogicalDisk);
EXPORT_SYMBOL(VcbFindOrCreateVnode);
EXPORT_SYMBOL(VcbFindVnode);
EXPORT_SYMBOL(VcbNotifyFreeBlockCount);

// end of file
