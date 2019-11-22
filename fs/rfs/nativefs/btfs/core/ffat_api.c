/*
 * TFS4 2.0 FFAT(Final FAT) filesystem Developed by Flash Planning Group.
 *
 * Fopyright 2006-2007 by Memory Division, Samsung Electronics, Inc.,
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
 * @file		ffat_api.c
 * @brief		FFAT API module.
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_types.h"
#include "ess_math.h"

#include "ffat_common.h"

#include "ffat_api.h"

#include "ffat_main.h"
#include "ffat_vol.h"
#include "ffat_dir.h"
#include "ffat_file.h"

#include "ffatfs_api.h"

#include "ffat_al.h"

// debug begin
//#define _DEBUG_API

#ifdef BTFS_DETAILED_LOG
	#undef _DEBUG_API
	#define _DEBUG_API
#endif
// debug end

// defines
#define _DEBUG_MEM_CHECK_BEGIN
#define _DEBUG_MEM_CHECK_END

// debug begin
#ifdef FFAT_DEBUG
	#if (FFAT_LOCK_TYPE != FFAT_LOCK_MULTIPLE)
		#undef _DEBUG_MEM_CHECK_BEGIN
		#undef _DEBUG_MEM_CHECK_END

		#define _DEBUG_MEM_CHECK_BEGIN	ffat_com_debugMemCheckBegin(__FUNCTION__);
		#define _DEBUG_MEM_CHECK_END	ffat_com_debugMemCheckEnd();
	#endif
#endif
// debug end

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_CORE_API)

// debug begin
#ifndef FFAT_DEBUG
// debug end
	#define _STATISTIC_MOUNT
	#define _STATISTIC_UMOUNT
	#define _STATISTIC_GETVOLUMESTATUS
	#define _STATISTIC_SETVOLUMENAME
	#define _STATISTIC_GETVOLUEMNAME
	#define _STATISTIC_LOOKUP
	#define _STATISTIC_FSCTL
	#define _STATISTIC_MAKEDIR
	#define _STATISTIC_REMOVEDIR
	#define _STATISTIC_READDIR
	#define _STATISTIC_CREATE
	#define _STATISTIC_OPEN
	#define _STATISTIC_CLOSE
	#define _STATISTIC_READ
	#define _STATISTIC_WRITE
	#define _STATISTIC_UNLINK
	#define _STATISTIC_SECUREUNLINK
	#define _STATISTIC_CHANGESIZE
	#define _STATISTIC_RENAME
	#define _STATISTIC_SETNODESTATUS
	#define _STATISTIC_GETNODESTATUS
	#define _STATISTIC_SYNCNODE
	#define _STATISTIC_SYNCVOL
	#define _STATISTIC_SYNC
	#define _STATISTIC_GETNODECLUSTERS
// debug begin
#else
	#define _STATISTIC_MOUNT				ffat_main_statisticMount();
	#define _STATISTIC_UMOUNT				ffat_main_statisticUmount();
	#define _STATISTIC_GETVOLUMESTATUS		ffat_main_statisticGetVolumeStatus();
	#define _STATISTIC_SETVOLUMENAME		ffat_main_statisticSetVolumeLabel();
	#define _STATISTIC_GETVOLUEMNAME		ffat_main_statisticGetVoluemName();
	#define _STATISTIC_LOOKUP				ffat_main_statisticLookup();
	#define _STATISTIC_FSCTL				ffat_main_statisticFSCtl();
	#define _STATISTIC_MAKEDIR				ffat_main_statisticMakedir();
	#define _STATISTIC_REMOVEDIR			ffat_main_statisticRemovedir();
	#define _STATISTIC_READDIR				ffat_main_statisticReaddir();
	#define _STATISTIC_CREATE				ffat_main_statisticCreate();
	#define _STATISTIC_OPEN					ffat_main_statisticOpen();
	#define _STATISTIC_CLOSE				ffat_main_statisticClose();
	#define _STATISTIC_READ					ffat_main_statisticRead();
	#define _STATISTIC_WRITE				ffat_main_statisticWrite();
	#define _STATISTIC_UNLINK				ffat_main_statisticUnlink();
	#define _STATISTIC_SECUREUNLINK			ffat_main_statisticSecureUnlink();
	#define _STATISTIC_CHANGESIZE			ffat_main_statisticChangeSize();
	#define _STATISTIC_RENAME				ffat_main_statisticRename();
	#define _STATISTIC_SETNODESTATUS		ffat_main_statisticSetNodeStatus();
	#define _STATISTIC_GETNODESTATUS		ffat_main_statisticGetNodeStatus();
	#define _STATISTIC_SYNCNODE				ffat_main_statisticSyncNode();
	#define _STATISTIC_SYNCVOL				ffat_main_statisticSyncVol();
	#define _STATISTIC_SYNC					ffat_main_statisticSync();
	#define _STATISTIC_GETNODECLUSTERS		ffat_main_statisticGetNodeClusters();
#endif
// debug end

// debug begin
#ifndef _DEBUG_API
// debug end
	#define	_DEBUG_LOOKUP_IN
	#define	_DEBUG_LOOKUP_OUT
	#define	_DEBUG_OPEN_IN
	#define	_DEBUG_OPEN_OUT
	#define	_DEBUG_CLOSE_IN
	#define	_DEBUG_CLOSE_OUT
	#define	_DEBUG_MAKEDIR_IN
	#define	_DEBUG_MAKEDIR_OUT
	#define	_DEBUG_REMOVEDIR_IN
	#define	_DEBUG_REMOVEDIR_OUT
	#define	_DEBUG_CREATE_IN
	#define	_DEBUG_CREATE_OUT
	#define	_DEBUG_READ_IN
	#define	_DEBUG_READ_OUT
	#define	_DEBUG_WRITE_IN
	#define	_DEBUG_WRITE_OUT
	#define	_DEBUG_CREATESYMLINK_IN
	#define	_DEBUG_CREATESYMLINK_OUT
	#define	_DEBUG_READSYMLINK_IN
	#define	_DEBUG_READSYMLINK_OUT
	#define	_DEBUG_SYNCNODE_IN
	#define	_DEBUG_SYNCNODE_OUT
	#define	_DEBUG_SYNCVOL_IN
	#define	_DEBUG_SYNCVOL_OUT
	#define	_DEBUG_UNLINK_IN
	#define	_DEBUG_UNLINK_OUT
	#define	_DEBUG_CHANGESIZE_IN
	#define	_DEBUG_CHANGESIZE_OUT
	#define	_DEBUG_RENAME_IN
	#define	_DEBUG_RENAME_OUT
	#define	_DEBUG_GETVOLUMESTATUS_IN
	#define	_DEBUG_GETVOLUMESTATUS_OUT
// debug begin
#else
	#define _DEBUG_LOOKUP_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Lookup (Parent(Ptr/C)/Child/Name/Flag):((0x%X/0x%X)/0x%X/%s/0x%X)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), dwFlag));
	#define _DEBUG_LOOKUP_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Lookup (Parent(Ptr/C)/Child/Name/Flag):((0x%X/0x%X)/0x%X/%s/0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), dwFlag, r));
	#define _DEBUG_OPEN_IN				FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Open Node/Inode:0x%X/0x%X\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, (t_uint32)pInode));
	#define _DEBUG_OPEN_OUT				FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Open Node/Inode:0x%X/0x%X, Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, (t_uint32)pInode, r));
	#define _DEBUG_CLOSE_IN				FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Close Node/dwFlag:0x%X/0x%X\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, dwFlag));
	#define _DEBUG_CLOSE_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Close Node/Inode/dwFlag:0x%X/0x%X, Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, dwFlag, r));
	#define _DEBUG_MAKEDIR_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] MakeDir (Parent/Child/Name):(0x%X/0x%X/%s)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent))));
	#define _DEBUG_MAKEDIR_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] MakeDir (pNode/Name/Cluster):(0x%X/%u), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeChild, NODE_C((Node*)pNodeChild), r));
	#define _DEBUG_REMOVEDIR_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] RemoveDir (pNode/Cluster/IsOpened):(0x%X/%u/%d)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), bIsOpened));
	#define _DEBUG_REMOVEDIR_OUT		FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] RemoveDir (pNode):(0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, r));
	#define _DEBUG_CREATE_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Create (Parent(Ptr/C)/Child/Name/Flag/pAddon):((0x%X/0x%X)/0x%X/%s/0x%X/0x%X)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), (t_uint32)pAddonInfo, dwFlag));
	#define _DEBUG_CREATE_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Create (Parent(Ptr/C)/Child/Name/Flag):((0x%X/0x%X)/0x%X/%s/0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), dwFlag, r));
//	#define	_DEBUG_READ_IN				FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%d] Read Node/Cluster/NodeSize/offset/WriteSize:0x%X/%d/%d/%d \n"), FFAT_DEBUG_GET_TASK_ID()(), pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwOffset, dwSize));
//	#define	_DEBUG_READ_OUT				FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%d] Read Node/Cluster/NodeSize/offset/WriteSize:0x%X/%d/%d/%d, ret:0x%X\n"), FFAT_DEBUG_GET_TASK_ID()(), pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwOffset, dwSize, r));
	#define	_DEBUG_READ_IN	
	#define	_DEBUG_READ_OUT

	#define	_DEBUG_WRITE_IN				FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Write Node/Cluster/NodeSize/offset/WriteSize/SFN:0x%X/%d/%d/%d/%d/%s \n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwOffset, dwSize, ((Node*)pNode)->stDE.sName));
	#define	_DEBUG_WRITE_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Write Node/Cluster/NodeSize/offset/WriteSize/SFN:0x%X/%d/%d/%d/%d/%s, ret:0x%X \n"), \
																FFAT_DEBUG_GET_TASK_ID(),(t_uint32) pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwOffset, dwSize, ((Node*)pNode)->stDE.sName, r));
	#define _DEBUG_CREATESYMLINK_IN		FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] CreateSymlink (Parent(Ptr/C)/Child/Name/Path/Flag/pAddon):((0x%X/0x%X)/0x%X/%s/%s/0x%X/0x%X)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), ffat_debug_w2a_2nd(psPath, NODE_VOL((Node*)pNodeParent)), (t_uint32)pAddonInfo, dwFlag));
	#define _DEBUG_CREATESYMLINK_OUT	FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] CreateSymlink (Parent(Ptr/C)/Child/Name/Flag):((0x%X/0x%X)/0x%X/%s/0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeParent, NODE_C((Node*)pNodeParent), (t_uint32)pNodeChild, ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeParent)), dwFlag, r));
	#define _DEBUG_READSYMLINK_IN		FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] ReadSymlink Node(Ptr/C):(0x%X/0x%X)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode)));
	#define _DEBUG_READSYMLINK_OUT		FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] ReadSymlink Node(Ptr/C):(0x%X/0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), r));
	#define	_DEBUG_SYNCNODE_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%d] SyncNode Node:0x%X\n"), FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode));
	#define	_DEBUG_SYNCNODE_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%d] SyncNode Node:0x%X, ret:0x%X\n"), FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, r));
	#define	_DEBUG_SYNCVOL_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%d] SyncVol Vol:0x%X\n"), FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pVol));
	#define	_DEBUG_SYNCVOL_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%d] SyncVol Vol:0x%X, ret:0x%X\n"), FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pVol, r));
	#define	_DEBUG_UNLINK_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Unlink Node/Cluster/Size/SFN:0x%X/%u/%d/%s"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), ((Node*)pNode)->stDE.sName));
	#define	_DEBUG_UNLINK_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Unlink Node/Cluster/Size/SFN:x%X/%u/%d/%s, ret:0x%X\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), ((Node*)pNode)->stDE.sName, r));
	#define	_DEBUG_CHANGESIZE_IN		FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] ChageSize Node/Cluster/NodeSize/NewSize:0x%X/%u/%d/%d\n"),	\
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwSize));
	#define	_DEBUG_CHANGESIZE_OUT		FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] ChageSize Node/Cluster/NodeSize/NewSize:0x%X/%u/%d/%d, ret:0x%Xd\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNode, NODE_C((Node*)pNode), NODE_S((Node*)pNode), dwSize, r));
	#define _DEBUG_RENAME_IN			FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] Rename Src(Node/Cluster/Size)/Des(Node/Cluster/Size)/NewName:(0x%X/%u/%d)/(0x%X/%u/%u)/%s\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeSrc, NODE_C((Node*)pNodeSrc), NODE_S((Node*)pNodeSrc), (t_uint32)pNodeDes, NODE_C((Node*)pNodeDes), NODE_S((Node*)pNodeDes), ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeSrc))));
	#define _DEBUG_RENAME_OUT			FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] Rename Src(Node/Cluster/Size)/Des(Node/Cluster/Size)/NewName:(0x%X/%u/%d)/(0x%X/%u/%u)/%s, ret:0x%X\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pNodeSrc, NODE_C((Node*)pNodeSrc), NODE_S((Node*)pNodeSrc), (t_uint32)pNodeDes, NODE_C((Node*)pNodeDes), NODE_S((Node*)pNodeDes), ffat_debug_w2a(psName, NODE_VOL((Node*)pNodeSrc)), r));
	#define _DEBUG_GETVOLUMESTATUS_IN	FFAT_PRINT_VERBOSE((_T("[BTFS API IN:%u] GetVolume Status (Vol):(0x%X)\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pVol));
	#define _DEBUG_GETVOLUMESTATUS_OUT	FFAT_PRINT_VERBOSE((_T("[BTFS API OUT:%u] GetVolume Status(Vol):(0x%X), Ret:%d\n"), \
																FFAT_DEBUG_GET_TASK_ID(), (t_uint32)pVol, r));
#endif
// debug end





/**
 * initializes FFAT filesystem.
 *
 * It initialize all FFAT components.
 * This function must be called before use FFAT filesystem.
 * Do not use twice this function before termination.
 *
 * @param		bForce				: force initialization.
 * @return		FFAT_OK				: Success
 * @return		FFAT_EINIT_ALREADY	: FFAT is already initialized
 * @return		else				: error
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Init(t_boolean bForce)
{
	FFAT_STACK_VAR

	FFAT_STACK_BEGIN();

	return ffat_main_init(bForce);
}


/**
 * terminates FFAT filesystem.
 *
 * It releases all resource for FFAT filesystem.
 * This function does not guarantee concurrent operation. (VFS must guarantee it)
 *
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 * @version		JAN-15-2009 [DongYoung Seo] remove lock code
 */
FFatErr
FFAT_Terminate(void)
{
	FFAT_STACK_VAR
	t_int32		r;

	FFAT_CHECK_INIT_RETURN();

	FFAT_STACK_BEGIN();

	r = ffat_main_terminate();

	return	r;
}


/**
 * mounts a volume
 *
 * @param		pVol		: volume pointer
 * @param		pRoot		: root node pointer
 * @param		pdwFlag		: mount flag
 *								FFAT_MOUNT_RDONLY		: mount volume read only mode
 *								FFAT_MOUNT_FAT_MIRROR	: do FAT mirroring
 *								FFAT_MOUNT_NO_LOG		: do not write log
 * @param		pDev		: user pointer for block device IO
 *								this parameter is used for block device IO 
 *								such as sector read or write.
 *								User can distinguish devices from this pointer.
 *								Nestle	: pointer of VCB
 *								FSM		: logical device ID
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: 1. Invalid parameter
 *								  2. There is a directory that has log file name
 * @return		FFAT_ENOMEM		: not enough memory
 * @return		FFAT_EPANIC		: system operation error such as lock operation
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Mount(FFatVol* pVol, FFatNode* pRoot, FFatMountFlag* pdwFlag, FFatLDevInfo* pLDevInfo,
					void* pDev)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_MOUNT

	//_DEBUG_MEM_CHECK_BEGIN		// do not use memory check for log, and new cache

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_mount((Vol*)pVol, (Node*)pRoot, pdwFlag, pLDevInfo, pDev, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	//_DEBUG_MEM_CHECK_END			// do not use Memory check for log
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 *  re-mount a volume
 * This function changes operation move of a volume
 *	Transaction On/Off and transaction type
 *	set volume read only
 *
 *	This function is used on Linux BOX.
 *
 * @param		pVol			: [IN] volume pointer
 * @param		pdwFlag			: [INOUT] mount flag
 *									available flags
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
FFAT_Remount(FFatVol* pVol, FFatMountFlag* pdwFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_UMOUNT

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_remount((Vol*)pVol, pdwFlag, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	//_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}



/**
 *  unmount a volume
 *
 * @param		pVol		: volume pointer
 * @param		dwFlag		: mount flag
 *							FFAT_UMOUNT_FORCE : un-mount volume even if there is some error
 *												or filesystem is doing something.
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOMEM		: not enough memory
 * @return		FFAT_EPANIC		: system operation error such as lock operation
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Umount(FFatVol* pVol, FFatMountFlag dwFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_UMOUNT

	//_DEBUG_MEM_CHECK_BEGIN			// do not check memory for cache removal

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_umount((Vol*)pVol, dwFlag, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	//_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/** 
 * retrieve boot sector info from boot sector before mount
 * (sector size, cluster size, first data sector)
 * 
 * @param		pDev			: [IN] user pointer for block device IO
 *								this parameter is used for block device IO 
 *								such as sector read or write.
 *								User can distinguish devices from this pointer.
 * @param		dwIOSize		: [IN] current I/O size
 * @param		pdwSectorSize	: [OUT] sector size storage
 * @param		pdwClusterSize	: [OUT] cluster size storage
 * @param		pdwFirstDataSector	: [OUT] first data sector storage
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] add cluster size
 * @version		FEB-18-2008 [GwangOk Go] add first data sector
 */
FFatErr
FFAT_GetBSInfoFromBS(void* pDev, t_int32 dwIOSize, t_int32* pdwSectorSize,
					 t_int32* pdwClusterSize, t_uint32* pdwFirstDataSector)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();

	_DEBUG_MEM_CHECK_BEGIN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_getBSInfoFromBS(pDev, dwIOSize, pdwSectorSize,
								pdwClusterSize, pdwFirstDataSector, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 * get status of  a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @param		pStatus		: [OUT] volume information storage
 * @param		pBuff		: [IN] buffer pointer, may be NULL
 * @param		dwSize		: [IN] size of buffer
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: volume is not mounted
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 * @version		MAY-09-2007 [DongYoung Seo] add new parameter buffer and size for performance
 */
FFatErr
FFAT_GetVolumeStatus(FFatVol* pVol, FFatVolumeStatus* pStatus, t_int8* pBuff, t_int32 dwSize)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_GETVOLUMESTATUS
	_DEBUG_GETVOLUMESTATUS_IN
	_DEBUG_MEM_CHECK_BEGIN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_getStatus((Vol*)pVol, pStatus, pBuff, dwSize, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_GETVOLUMESTATUS_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 * get volume name
 *
 * @param		pVolInfo		: [IN] volume information
 * @param		psVolLabel		: [OUT] volume name storage
 * @param		dwVolLabelLen	: [IN] character count that can be stored at psVolLabel
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
FFAT_GetVolumeLabel(FFatVol* pVol, t_wchar* psVolLabel, t_int32 dwVolLabelLen)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_GETVOLUEMNAME

	_DEBUG_MEM_CHECK_BEGIN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_getVolumeLabel((Vol*)pVol, psVolLabel, dwVolLabelLen, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 * set volume name to psVolLabel
 *
 * @param		pVolInfo	: [IN] volume information
 * @param		psVolLabel	: [IN] new volume name, Maximum name length is 11 characters.
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter
 * @return		FFAT_EIO		: IO error
 * @author		DongYoung Seo
 * @version		JAN-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
FFAT_SetVolumeLabel(FFatVol* pVol, t_wchar* psVolLabel)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_SETVOLUMENAME
	_DEBUG_MEM_CHECK_BEGIN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_setVolumeLabel((Vol*)pVol, psVolLabel, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 * FFAT filesystem control.
 *
 * lock이 필요할 경우 각각의 command를 처리하는 부분에서 수행하도록 한다.
 *
 * @param		dwCmd		: filesystem control command
 * @param		pParam0		: parameter 0
 * @param		pParam1		: parameter 1
 * @param		pParam2		: parameter 2
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JUL-25-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_FSCtl(FFatFSCtlCmd dwCmd, void* pParam0, void* pParam1, void* pParam2)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_FSCTL
	_DEBUG_MEM_CHECK_BEGIN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_main_fsctl(dwCmd, pParam0, pParam1, pParam2, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
 * lookup a node in a directory
 *
 * @param		pNodeParent	: [IN] Parent node
 * @param		pNodeChild	: [IN/OUT] Child node information storage
 * @param		psName		: [IN] node name
 * @param		dwFlag		: [IN] lookup flag
 *								FFAT_LOOKUP_FOR_CREATE : create를 위한 lookup일 경우 반드시 설정한다.
 *										Lookup 과정에서 creation에 필요한 추가적인 정보를 수집한다.
 * @param		pAddonInfo	: [IN] info of ADDON node (may be NULL)
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ETOOLONG	: too long name
 * @author		DongYoung Seo
 * @version		AUG-09-2006 [DongYoung Seo] First Writing
 */
FFatErr
FFAT_Lookup(FFatNode* pNodeParent, FFatNode* pNodeChild, t_wchar* psName, FFatLookupFlag dwFlag,
			void* pAddonInfo)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_LOOKUP
	_DEBUG_LOOKUP_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_lookup((Node*)pNodeParent, (Node*)pNodeChild, psName, 0, dwFlag,
						pAddonInfo, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_LOOKUP_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();
	return r;
}


/**
* open a node
* 
* This function must be used after lookup operation to assign 
* a lock and to set open flag.
*
* Notice : This function does not perform lookup operation.
*
* @param		pNode		: [IN] node pointer
* @param		pInode		: [IN] correspondent INODE Pointer (VFS)
*								may be NULL
* @return		FFAT_OK			: success
* @return		FFAT_EINVALID	: Invalid parameter
* @author		DongYoung Seo
* @version		AUG-14-2006 [DongYoung Seo] First Writing
*/
FFatErr
FFAT_Open(FFatNode* pNode, void* pInode)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_OPEN
	_DEBUG_OPEN_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_open((Node*) pNode, pInode, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_OPEN_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * close a node
 *
 * This function release all resource for a node.
 * After use this function the node pointer should not be used.
 * Node가 close 될 때 호출된다.
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwFlag			: [IN] flag for node close
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-14-2006 [DongYoung Seo] First Writing
 */
FFatErr
FFAT_Close(FFatNode* pNode, FFatNodeCloseFlag dwFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_CLOSE
	_DEBUG_CLOSE_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_close((Node*) pNode, dwFlag, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_CLOSE_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * make a directory
 *
 * Directory를 생성한다..
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN/OUT] child node pointer
 * @param		psName			: [IN/OUT] name string
 * @param		dwFlag			: [IN] create flag
 *						FFAT_CREATE_ATTR_DIR	: directory 생성
 *						FFAT_CREATE_LOOKUP		: lookup을 수행하지 않은 Node에 대해 
 *													create를 수행할 경우 반드시 설정.
 * @param		pAddonInfo		: [IN] info of ADDON node (may be NULL)
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 * @author		DongYoung Seo
 * @version		SEP-01-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Makedir(FFatNode* pNodeParent, FFatNode* pNodeChild, t_wchar* psName,
			 FFatCreateFlag dwFlag, void* pAddonInfo)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	t_int32		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_MAKEDIR
	_DEBUG_MAKEDIR_IN
	FFAT_STACK_BEGIN();

	dwFlag = (dwFlag | FFAT_CREATE_ATTR_DIR) & (~FFAT_CREATE_ATTR_VOLUME);

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	FFAT_ASSERT(dwFlag & FFAT_CREATE_ATTR_DIR);

	r = ffat_node_create((Node*)pNodeParent, (Node*)pNodeChild, psName, dwFlag, pAddonInfo, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MAKEDIR_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * unlink a node
 *
 * @param		pNodeParent		: [IN] parent node.
 *									NULL : there is no parent == open unlinked node
 * @param		pNode			: [IN] node pointer
 * @param		bIsOpened		: [IN]	TRUE : opened unlink
 *										FALSE : Not opened unlink
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ENOTDIR	: this is not a directory
 * @return		else			: error
 * @return		FFAT_OK		: SUCCESS
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-12-2008 [DongYoung Seo] add parameter bIsOpened to support open unlink
 * @version		JAN-30-2009 [JeongWoo Park] remove the assert to check whether bIsOpened is false
 */
FFatErr
FFAT_Removedir(FFatNode* pNodeParent, FFatNode* pNode, t_boolean bIsOpened)
{
	FFAT_STACK_VAR
	ComCxt				stCxt;
	FFatErr				r;
	NodeUnlinkFlag		dwNUFlag = NODE_UNLINK_NONE;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_REMOVEDIR
	_DEBUG_REMOVEDIR_IN
	FFAT_STACK_BEGIN();

	IF_UK (NODE_IS_DIR((Node*)pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_ENOTDIR;
		goto out;
	}

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	if (pNodeParent)
	{
		if (bIsOpened == FFAT_TRUE)
		{
			dwNUFlag |= NODE_UNLINK_OPEN;
		}

		r = ffat_dir_remove((Node*)pNodeParent, (Node*)pNode, dwNUFlag, &stCxt);
	}
	else
	{
		FFAT_ASSERT(bIsOpened == FFAT_FALSE);
		r = ffat_node_unlinkOpenUnlinked((Node*)pNode, &stCxt);
	}

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_REMOVEDIR_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * Read an entry in a directory
 *
 * @param		pNode			: [IN] node pointer, must be a directory
 * @param		dwOffset		: [IN] entry lookup start offset
 * @param		pdwOffsetNext	: [IN/OUT] next readdir offset
 * @param		psName			: [OUT] name storage, It should be 256*2 byte
 * @param		dwNameLen		: [IN] character of the name storage(psName)
 * @return		FFAT_OK			: readdir success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOMOREENT	: no more entry
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		JUL-25-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Readdir(FFatNode* pNode, t_uint32 dwOffset, t_uint32* pdwOffsetNext,
					t_wchar* psName, t_int32 dwNameLen)
{
	FFAT_STACK_VAR
	ReaddirInfo		stRI;
	ComCxt			stCxt;
	FFatErr			r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_READDIR
	FFAT_STACK_BEGIN();

	_DEBUG_MEM_CHECK_BEGIN

#ifdef FFAT_VFAT_SUPPORT
		FFAT_INIT_RI(&stRI, psName, NULL, dwNameLen, 0, NULL, NULL);
#else
		FFAT_INIT_RI(&stRI, NULL, psName, 0, dwNameLen, NULL, NULL);
#endif

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

#ifdef FFAT_VFAT_SUPPORT
	r = ffat_dir_readdir((Node*)pNode, dwOffset, &stRI, READDIR_LFN, &stCxt);
#else
	r = ffat_dir_readdir((Node*)pNode, dwOffset, &stRI, READDIR_SFN, &stCxt);
#endif
	if (r == FFAT_OK)
	{
		if (pdwOffsetNext != NULL)
		{
			*pdwOffsetNext = stRI.dwOffsetNext;
		}
	}

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * create a node
 *
 * Directory와 File을 생성한다.
 * file과 directory에 대한 구별은 dwFlag를 통해 처리된다.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN/OUT] child node pointer
 * @param		psName			: [IN/OUT] name string
 * @param		dwFlag			: [IN] create flag
 *						FFAT_CREATE_ATTR_DIR	: directory 생성
 *						FFAT_CREATE_LOOKUP		: lookup을 수행하지 않은 Node에 대해 
 *													create를 수행할 경우 반드시 설정.
 * @param		pAddonInfo		: [IN] info of ADDON node (may be NULL)
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_Create(FFatNode* pNodeParent, FFatNode* pNodeChild, t_wchar* psName,
			FFatCreateFlag dwFlag, void* pAddonInfo)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN;
	_STATISTIC_CREATE
	FFAT_STACK_BEGIN();
	_DEBUG_CREATE_IN

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	FFAT_ASSERT((dwFlag & FFAT_CREATE_ATTR_DIR) == 0);

	r = ffat_node_create((Node*)pNodeParent, (Node*)pNodeChild, psName, dwFlag, pAddonInfo, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_CREATE_OUT
	_DEBUG_MEM_CHECK_END;
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * read data from a file
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwReadFlag	: [IN] flag for read operation
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
FFAT_Read(FFatNode* pNode, t_uint32 dwOffset, t_int8* pBuff,
			t_int32 dwSize, FFatReadFlag dwReadFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r, rr;
	
	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_READ
	_DEBUG_READ_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_file_read((Node*)pNode, dwOffset, pBuff, dwSize, dwReadFlag, &stCxt);

	rr = ffat_cxt_delete(&stCxt);
	IF_UK (rr < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to delete context")));
		r = rr;
	}

out:
	_DEBUG_READ_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * write data to a file
 *
 * 
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwOffset	: [IN] write offset
 * @param		pBuff		: [IN] buffer pointer
 * @param		dwSize		: [IN] write size
 * @param		dwFlag		: [IN] cache flag for IO
 * @return		positive value : written byte
 *								This value may not equal to requested write size
 *								when the volume does not have enough free space
 * @return		FFAT_OK		: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EACCESS	: not mounted or read-only volume
 * @return		FFAT_EISDIR		: this is not a file
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 *									(fail to enlarge file size to the write start offset.)
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
FFAT_Write(FFatNode* pNode, t_uint32 dwOffset, t_int8* pBuff, t_int32 dwSize, 
			FFatWriteFlag dwWriteFlag)
{
	FFAT_STACK_VAR
	ComCxt			stCxt;
	FFatErr			r, rr;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_WRITE
	_DEBUG_WRITE_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_file_write((Node*)pNode, dwOffset, pBuff, dwSize, dwWriteFlag, &stCxt);

	rr = ffat_cxt_delete(&stCxt);
	IF_UK (rr < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to delete context")));
		r = rr;
	}

out:
	_DEBUG_WRITE_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

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
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 */
FFatErr
FFAT_CreateSymlink(FFatNode* pNodeParent, FFatNode* pNodeChild, t_wchar* psName,
						t_wchar* psPath, FFatCreateFlag dwFlag, void* pAddonInfo)
{
	FFAT_STACK_VAR
	FFatErr		r;
	ComCxt		stCxt;

	IF_UK ((pNodeParent == NULL) || (pNodeChild == NULL) || (psName == NULL) || (psPath == NULL))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN;
	_DEBUG_CREATESYMLINK_IN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	FFAT_ASSERT((dwFlag & FFAT_CREATE_ATTR_DIR) == 0);

	r = ffat_node_createSymlink((Node*)pNodeParent, (Node*)pNodeChild,
						psName, psPath, dwFlag, pAddonInfo, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_CREATESYMLINK_OUT
	_DEBUG_MEM_CHECK_END;
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * read symlink info & get target path
 *
 * @param		pNode			: [IN] node pointer
 * @param		psPath			: [OUT] target path
 * @param		dwLen			: [IN] length of psPath, in character count
 * @param		pdwLen			: [OUT] count of character stored at psPath
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: invalid parameter, this is not symbolic link
 * @return		FFAT_ENOSUPPORT	: volume does not support symlink
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EIO		: io error while reading symlink info
 * @return		else			: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 */
FFatErr
FFAT_ReadSymlink(FFatNode* pNode, t_wchar* psPath, t_int32 dwLen, t_int32* pdwLen)
{
	FFAT_STACK_VAR
	FFatErr		r;
	ComCxt		stCxt;

	IF_UK ((pNode == NULL) || (psPath == NULL) || (pdwLen == NULL) || (dwLen <= 0))
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_DEBUG_READSYMLINK_IN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_readSymlink((Node*)pNode, psPath, dwLen, pdwLen, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_READSYMLINK_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * sync a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		dwSizeToBe	: [IN] Size to be (size of VNODE)
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_SyncNode(FFatNode* pNode, t_uint32 dwSizeToBe)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_SYNCNODE
	_DEBUG_SYNCNODE_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_sync((Node*)pNode, dwSizeToBe, NODE_SYNC_NONE, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_SYNCNODE_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * sync a volume
 *
 * @param		pVol		: [IN] volume pointer
 * @return		FFAT_OK			: Success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_SyncVol(FFatVol* pVol)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_STATISTIC_SYNCVOL
	_DEBUG_MEM_CHECK_BEGIN
	_DEBUG_SYNCVOL_IN

	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_vol_sync((Vol*)pVol, FFAT_TRUE, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_SYNCVOL_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * set status of a node
 *
 * It can change attribute, last access date, write date/time, creat data/time
 * (Others will be ignored)
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [IN] node information
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_SetNodeStatus(FFatNode* pNode, FFatNodeStatus* pStatus)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_SETNODESTATUS
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_setStatus((Node*)pNode, pStatus, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * get status of a node
 *
 * @param		pNode		: [IN] node pointer
 * @param		pStatus		: [IN/OUT] node information
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @author		DongYoung Seo
 * @version		AUG-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_GetNodeStatus(FFatNode* pNode, FFatNodeStatus* pStatus)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_GETNODESTATUS
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_getStatus((Node*)pNode, pStatus, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * rename a node
 *
 * it can not rename to another volume
 * it can not rename a file to a directory or a directory to a file.
 * it can rename a directory to another exist directory when it does not have any node in it.
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node
 * @param		pNodeDesParent	: [IN] parent node of destination(target)
 * @param		pNodeDes		: [IN] destination node
 * @param		psName			: [IN] target node name
 * @param		dwFlag			: [IN] rename flag
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOSUPPORT	: no support rename between other volumes
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ENOTDIR	: parent is not a directory
 * @return		FFAT_EACCESS	: volume is mounted with read-only flag
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 * @author		DongYoung Seo
 * @version		28-AUG-2006 [DongYoung Seo] First Writing.
 * @version		09-DEC-2008 [DongYoung Seo] add pNodeNew to support open rename
 * @version		FEB-11-2009 [GwangOk Go] update renamed node info on pNodeSrc (delete pNodeNew)
 */
FFatErr
FFAT_Rename(FFatNode* pNodeSrcParent, FFatNode* pNodeSrc,
			FFatNode* pNodeDesParent, FFatNode* pNodeDes,
			t_wchar* psName, FFatRenameFlag dwFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_RENAME
	_DEBUG_RENAME_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_node_rename((Node*)pNodeSrcParent, (Node*)pNodeSrc,
							(Node*)pNodeDesParent, (Node*)pNodeDes,
							psName, dwFlag, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_RENAME_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * change file size
 *
 * @param		pNode		: [IN] target node pointer
 * @param		dwSize		: [IN] New file size
 * @param		dwFlag		: [IN] change size flag
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_ChangeSize(FFatNode* pNode, t_uint32 dwSize, FFatChangeSizeFlag dwFlag)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_CHANGESIZE
	_DEBUG_CHANGESIZE_IN
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_file_changeSize((Node*)pNode, dwSize, dwFlag, FFAT_CACHE_NONE, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_CHANGESIZE_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * unlink a node
 *
 * @param		pNodeParent		: [IN] parent node pointer
 *									may be NULL ==> open unlinked node
 * @param		pNode			: [IN] node pointer
 * @param		bIsOpened		: [IN] open unlink indicator
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @history		DEC-04-2007 [InHwan Choi] open unlink 
 */
FFatErr
FFAT_Unlink(FFatNode* pNodeParent, FFatNode* pNode, t_boolean bIsOpened)
{
	FFAT_STACK_VAR
	ComCxt				stCxt;
	FFatErr				r;
	NodeUnlinkFlag		dwNUFlag = NODE_UNLINK_NONE;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_UNLINK
	_DEBUG_UNLINK_IN
	FFAT_STACK_BEGIN();

	IF_UK (NODE_IS_FILE((Node*)pNode) == FFAT_FALSE)
	{
		FFAT_LOG_PRINTF((_T("This is not a file")));
		r = FFAT_EISDIR;
		goto out;
	}

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	if (pNodeParent != NULL)
	{
		if (bIsOpened == FFAT_TRUE)
		{
			dwNUFlag |= NODE_UNLINK_OPEN;
		}

		r = ffat_file_unlink((Node*) pNodeParent, (Node*)pNode, dwNUFlag, &stCxt);
	}
	else
	{
		FFAT_ASSERT(bIsOpened == FFAT_FALSE);
		r = ffat_node_unlinkOpenUnlinked((Node*)pNode, &stCxt);
	}

	FFAT_ASSERT(r != FFAT_ENOSPC);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_UNLINK_OUT
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * unlink a node securely
 *
 * @param		pNodeParent		: [IN] parent node pointer
 *									may be NULL
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_EACCESS	: read only volume
 * @return		FFAT_EXDEV		: media ejected (node is not in the volume, an orphan node)
 * @return		FFAT_EISDIR		: this is a directory
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_SecureUnlink(FFatNode* pNodeParent, FFatNode* pNode)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_SECUREUNLINK
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_file_unlink((Node*)pNodeParent, (Node*)pNode, NODE_UNLINK_SECURE, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * get cluster that are occupied to a file.
 * or allocated cluster for file
 *
 * @param		pNode		: [IN] a node pointer
 * @param		dwOffset	: [IN] start offset
 * @param		dwSize		: [IN] size in byte
 * @param		pVC			: [IN] cluster information storage
 * @return		FFAT_OK			: success
 * @return		FFAT_EINVALID	: Invalid parameter
 * @return		FFAT_ENOSPC		: There is not enough free space on the volume
 * @return		else			: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_GetNodeClusters(FFatNode* pNode, t_uint32 dwOffset, t_uint32 dwSize, FFatVC* pVC)
{
	FFAT_STACK_VAR
	ComCxt		stCxt;
	FFatErr		r;

	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN
	_STATISTIC_GETNODECLUSTERS
	FFAT_STACK_BEGIN();

	// Construct a context
	r = ffat_cxt_create(&stCxt);
	FFAT_EO(r, (_T("fail to create context")));

	r = ffat_file_getClusters((Node*)pNode, dwOffset, dwSize, (FFatVC*)pVC, &stCxt);

	r |= ffat_cxt_delete(&stCxt);

out:
	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return r;
}


/**
 * get node size
 *
 * @param		pNode		: [IN] node pointer
 * @return		node size in byte
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetSize(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ((Node*)pNode)->dwSize;
}



/**
 * get start cluster of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		start cluster number
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetCluster(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ((Node*)pNode)->dwCluster;
}


/**
 * get attribute of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		 attribute
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint8
FFAT_GetAttribute(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ((Node*)pNode)->stDE.bAttr;
}


/**
 * get last access date of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		last access date
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetLastAccessDate(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(FFAT_BO_UINT16(((Node*)pNode)->stDE.wLstAccDate));
}


/**
 * get last access time of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		last access time 
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetLastAccessTime(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return 0;
}


/**
 * get write date of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		write date
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetWriteDate(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(FFAT_BO_UINT16(((Node*)pNode)->stDE.wWrtDate));
}


/**
 * get write time of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		write time
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetWriteTime(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(FFAT_BO_UINT16(((Node*)pNode)->stDE.wWrtTime));
}


/**
 * get creation date of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		creation date
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetCreateDate(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(FFAT_BO_UINT16(((Node*)pNode)->stDE.wCrtDate));
}


/**
 * get creation time of a node
 *
 * @param		pNode		: [IN] node pointer
 * @return		creation time
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetCreateTime(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(FFAT_BO_UINT16(((Node*)pNode)->stDE.wCrtTime));
}


/**
 * get parent cluster number
 *
 * @param		pNode		: [IN] node pointer
 * @return		parent cluster number
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetParentCluster(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (t_uint32)(NODE_COP((Node*)pNode));
}


/**
* get directory entry start cluster
*
* @param		pNode		: [IN] node pointer
* @return		directory entry start byte offset from parent directory
* @author		DongYoung Seo
* @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetDeStartCluster(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ((Node*)pNode)->stDeInfo.dwDeStartCluster;
}


/**
 * get directory entry offset
 *
 * @param		pNode		: [IN] node pointer
 * @return		directory entry start byte offset from parent directory
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
t_uint32
FFAT_GetDeStartOffset(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ((Node*)pNode)->stDeInfo.dwDeStartOffset;
}


/** 
 * get cluster size
 * this function should be used after mount
 * 
 * @param		pVol			: [IN] volume pointer
 * @return		t_int32		: cluster size
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		MAR-05-2009 [GwangOk Go] change ClusterSize from parameter to return value
*/
t_int32
FFAT_GetClusterSize(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE);

	return VOL_CS((Vol*)pVol);
}


/** 
 * get cluster size bit count
 * 
 * @param		pVol			: [IN] volume pointer
 *									may be NULL
 *									one of pVol or pNode must be valid.
 * @param		pNode			: [IN] node pointer
 *									may be NULL
 *									one of pVol or pNode must be valid.
 * @param		pdwClusterSizeBit	: [OUT] cluster size storage
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JAN-04-2007 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetClusterSizeBits(FFatVol* pVol, FFatNode* pNode, t_int32* pdwClusterSizeBit)
{
	if ((pVol != NULL) && (VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE))
	{
		*pdwClusterSizeBit = VOL_CS((Vol*)pVol);
	}
	else if ((pNode != NULL) && (NODE_IS_VALID((Node*)pNode) == FFAT_TRUE))
	{
		*pdwClusterSizeBit = VOL_CS(NODE_VOL((Node*)pNode));
	}
	else
	{
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/** 
 * get cluster size mask
 * 
 * @param	pVol				: [IN] volume pointer
 *									may be NULL
 * @param	pNode				: [IN] node pointer
 *									may be NULL
 *					one of pVol or pNode must be valid.
 * @param	pdwClusterSizeMask	: [OUT] cluster size mask storage
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetClusterSizeMask(FFatVol* pVol, FFatNode* pNode, t_uint32* pdwClusterSizeMask)
{
	if ((pVol != NULL) && (VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE))
	{
		*pdwClusterSizeMask = VOL_CSM((Vol*)pVol);
	}
	else if ((pNode != NULL) && (NODE_IS_VALID((Node*)pNode) == FFAT_TRUE))
	{
		*pdwClusterSizeMask = VOL_CSM(NODE_VOL((Node*)pNode));
	}
	else
	{
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/** 
 * get cluster size
 * this function should be used after mount
 * 
 * @param		pVol		: [IN] volume pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 *								may be NULL
 *						one of pVol or pNode must be valid.
 * @return		t_int32		: sector size
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] change SectorSize from parameter to return value
 */
t_int32
FFAT_GetSectorSize(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE);

	return VOL_SS((Vol*)pVol);
}


/** 
 * get cluster size
 * this function should be used after mount
 * 
 * @param		pVol		: [IN] volume pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 *								may be NULL
 *						one of pVol or pNode must be valid.
 * @return		t_int32		: sector size bit count
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2008 [GwangOk Go] change SectorSizeBits from parameter to return value
 */
t_int32
FFAT_GetSectorSizeBits(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE);

	return VOL_SSB((Vol*)pVol);
}


/** 
 * get sector per a cluster bits
 * 
 * @param		pVol		: [IN] volume pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 *								may be NULL
 *						one of pVol or pNode must be valid.
 * @param		pdwSPC	: [OUT] sector per cluster
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetSectorPerCluster(FFatVol* pVol, FFatNode* pNode, t_int32* pdwSPC)
{
	FFAT_ASSERT(pVol || pNode);
	FFAT_ASSERT(pdwSPC);

	if ((pVol != NULL) && (VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE))
	{
		*pdwSPC = VOL_SPC((Vol*)pVol);
	}
	else if ((pNode != NULL) && (NODE_IS_VALID((Node*)pNode) == FFAT_TRUE))
	{
		*pdwSPC = VOL_SPC(NODE_VOL((Node*)pNode));
	}
	else
	{
		return FFAT_EINVALID;
	}

	FFAT_ASSERT(EssMath_IsPowerOfTwo(*pdwSPC) == ESS_TRUE);
	FFAT_ASSERT(*pdwSPC > 0);

	return FFAT_OK;
}


/** 
 * get sector per a cluster bits
 * 
 * @param		pVol		: [IN] volume pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 *								may be NULL
 *						one of pVol or pNode must be valid.
 * @param		pdwBits	: [OUT] sector per cluster bits
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetSectorPerClusterBits(FFatVol* pVol, FFatNode* pNode, t_int32* pdwBits)
{
	if ((pVol != NULL) && (VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE))
	{
		*pdwBits = VOL_SPCB((Vol*)pVol);
	}
	else if ((pNode != NULL) && (NODE_IS_VALID((Node*)pNode) == FFAT_TRUE))
	{
		*pdwBits = VOL_SPCB(NODE_VOL((Node*)pNode));
	}
	else
	{
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/** 
 * get cluster size
 * 
 * @param		pVol		: [IN] volume pointer
 *								may be NULL
 * @param		pNode		: [IN] node pointer
 *								may be NULL
 *						one of pVol or pNode must be valid.
 * @param		pdwSectorSizeMask	: [OUT] sector size mask storage
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetSectorSizeMask(FFatVol* pVol, FFatNode* pNode, t_uint32* pdwSectorSizeMask)
{
	if ((pVol != NULL) && (VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE))
	{
		*pdwSectorSizeMask = VOL_SSM((Vol*)pVol);
	}
	else if ((pNode != NULL) && (NODE_IS_VALID((Node*)pNode) == FFAT_TRUE))
	{
		*pdwSectorSizeMask = VOL_SSM(NODE_VOL((Node*)pNode));
	}
	else
	{
		return FFAT_EINVALID;
	}

	return FFAT_OK;
}


/** 
 * get first sector of cluster
 * 
 * @param		pVol		: [IN] volume pointer
 * @param		dwCluster	: [IN] cluster number
 * @param		pdwSector	: [IN] sector number storage
 * @return		FFAT_OK
 * @author		DongYoung Seo
 * @version		SEP-29-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetSectorOfCluster(FFatVol* pVol, t_uint32 dwCluster, t_uint32* pdwSector)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pdwSector);
	FFAT_ASSERT(dwCluster >= 1);

	if (dwCluster == 1)
	{
		// This must be FAT16
		FFAT_ASSERT(VOL_IS_FAT16((Vol*)pVol) == FFAT_TRUE);
		*pdwSector = VOL_FRS((Vol*)pVol);
	}
	else
	{
		*pdwSector = FFATFS_GET_SECTOR_OF_CLUSTER(VOL_VI((Vol*)pVol), dwCluster, 0);
	}

	return FFAT_OK;
}


/**
 * get first data sector
 * this function should be used after mount
 *
 * @param		pVol		: [IN] volume pointer
 * @return		first data sector number
 * @author		GwangOk Go
 * @version		FEB-18-2009 [GwangOk Go] First Writing.
 */
t_uint32
FFAT_GetFirstDataSector(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(VOL_IS_MOUNTED((Vol*)pVol) == FFAT_TRUE);

	return VOL_FDS((Vol*)pVol);
}


/**
 * get bit count of cluster count per a FAT sector
 *
 * @param		pVol		: [IN] volume pointer
 * @return		bit count of cluster count per a FAT sector
 * @author		DongYoung Seo
 * @version		MAR-17-2009 [DongYoung Seo] First Writing
 */
t_uint32
FFAT_GetClusterCountPerFATSectorBits(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);

	return VOL_CCPFSB((Vol*)pVol);
}


/** 
 * get volume
 * 
 * @param		pNode		: [IN] node pointer
 * @return		pointer of Vol
 * @author		DongYoung Seo
 * @version		SEP-29-2006 [DongYoung Seo] First Writing.
*/
FFatVol*
FFAT_GetVol(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);
	return (FFatVol*)NODE_VOL((Node*)pNode);
}


/** 
 * get root node
 * 
 * @param		pVol		: [IN] volume pointer
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetRootNode(FFatVol* pVol, FFatNode* pNode)
{
	FFAT_CHECK_INIT_RETURN();
	FFAT_LOCK_GLOBAL_RETURN();
	_DEBUG_MEM_CHECK_BEGIN

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);

	FFAT_MEMCPY(pNode, VOL_ROOT((Vol*)pVol), sizeof(Node));

	_DEBUG_MEM_CHECK_END
	FFAT_UNLOCK_GLOBAL();

	return FFAT_OK;
}


/** 
* get pointer of root node
* 
* @param		pVol		: [IN] volume pointer
* @param		ppNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		else		: error
* @author		DongYoung Seo
* @version		SEP-28-2006 [DongYoung Seo] First Writing.
*/
FFatErr
FFAT_GetRootNodePtr(FFatVol* pVol, FFatNode** ppNode)
{
	FFAT_CHECK_INIT_RETURN();

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppNode);

	*ppNode = (FFatNode*)VOL_ROOT((Vol*)pVol);

	return FFAT_OK;
}


/** 
* get pointer of INODE on VFS
* 
* @param		pNode		: [IN] node pointer
* @return		corresponding INODE pointer
* @author		DongYoung Seo
* @version		OCT-1-2007 [DongYoung Seo] First Writing.
*/
void*
FFAT_GetInode(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);
	return ((Node*)pNode)->pInode;
}


/** 
 * check node is a file
 * 
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_TRUE	: A file
 * @return		FFAT_FALSE	: node a file
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		OCT-18-2006 [DongYoung Seo] First Writing.
*/
t_boolean
FFAT_NodeIsFile(FFatNode* pNode)
{
	FFAT_STACK_VAR

	FFAT_STACK_BEGIN();

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);

	return NODE_IS_FILE((Node*)pNode);
}


/** 
 * check node is a directory
 * 
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_TRUE	: A directory
 * @return		FFAT_FALSE	: node a directory
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		OCT-18-2006 [DongYoung Seo] First Writing.
*/
t_boolean
FFAT_NodeIsDir(FFatNode* pNode)
{
	FFAT_STACK_VAR

	FFAT_STACK_BEGIN();

#ifdef FFAT_STRICT_CHECK
	IF_UK (pNode == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	FFAT_ASSERT(pNode);

	return NODE_IS_DIR((Node*)pNode);
}


/** 
* check node is root directory
* 
* @param		pNode		: [IN] node pointer
* @return		FFAT_TRUE	: A directory
* @return		FFAT_FALSE	: node a directory
* @return		else		: error
* @author		DongYoung Seo
* @version		OCT-13-2007 [DongYoung Seo] First Writing.
*/
t_boolean
FFAT_NodeIsRoot(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return (((Node*)pNode)->dwFlag & NODE_ROOT_DIR) ? FFAT_TRUE : FFAT_FALSE;
}

/** 
* check node is dirty-size state
* 
* @param		pNode		: [IN] node pointer
* @return		FFAT_TRUE	: dirty-size state node
* @return		FFAT_FALSE	: normal node
* @return		else		: error
* @author		JW Park
* @version		OCT-27-2009 [JW Park] First Writing.
*/
t_boolean
FFAT_NodeIsDirtySize(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return NODE_IS_DIRTY_SIZE((Node*)pNode);
}

/** 
* check node is dirty-size-rdonly state
* 
* dirty-size-rdonly means that
* the node is in dirty-size state in read-only volume.
* the node structure was built at the time that volume is read-only.
* In this case, the recovery about dirty-size node is not performed.
* Then if the read-only attribute of volume is removed by REMOUNT,
* this node must be recovered ahead of real operation
* like write/rename/expand/unlink/sync
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_TRUE	: dirty-size state node
* @return		FFAT_FALSE	: normal node
* @return		else		: error
* @author		JW Park
* @version		OCT-29-2009 [JW Park] First Writing.
*/
t_boolean
FFAT_NodeIsDirtySizeRDOnly(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return NODE_IS_DIRTY_SIZE_RDONLY((Node*)pNode);
}


/** 
* set INODE of VFS at pNode
* 
* @param		pNode		: [IN] node pointer
* @param		pInode		: [IN] index node pointer (VNODE for nestle)
* @author		DongYoung Seo
* @version		AUG-28-2008 [DongYoung Seo] First Writing.
*/
void
FFAT_NodeSetInode(FFatNode* pNode, void* pInode)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pInode);

	NODE_INODE((Node*)pNode) = pInode;
}


/** 
* get INODE fro Node
* 
* @param		pNode		: [IN] node pointer
* @author		DongYoung Seo
* @version		AUG-28-2008 [DongYoung Seo] First Writing.
*/
void*
FFAT_NodeGetInode(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return NODE_INODE((Node*)pNode);
}


/** 
 * check node is symbolic file
 * 
 * @param		pNode		: [IN] node pointer
 * @return		FFAT_TRUE	: symbolic file
 * @return		FFAT_FALSE	: no symbolic file
 * @return		else		: error
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 */
t_boolean
FFAT_NodeIsSymbolicFile(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ffat_node_isSymlink((Node*)pNode);
}

/** 
 * check volume is formatted for FAT32
 * 
 * @param		pVol		: [IN] volume pointer
 * @return		FFAT_TRUE	: volume is FAT32
 * @return		FFAT_FALSE	: volume is not FAT32
 * @author		GwangOk Go
 * @version		JAN-12-2009 [DongYoung SEo] First Writing.
 */
t_boolean
FFAT_VolIsFAT32(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);

	return VOL_IS_FAT32((Vol*)pVol);
}


/** 
 * check volume is read only
 * 
 * @param		pVol		: [IN] volume pointer
 * @return		FFAT_TRUE	: volume is read only
 * @return		FFAT_FALSE	: volume is not read only
 * @author		GwangOk Go
 * @version		AUG-28-2008 [GwangOk Go] First Writing.
 * @version		DEC-12-2008 [DongYoung Seo] Change Parameter from FFatNode To FFatVol
 */
t_boolean
FFAT_VolIsReadOnly(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);

	return VOL_IS_RDONLY((Vol*)pVol);
}


/** 
 * Get stored child node pointer for creation.
 * 
 * @param		pNode		: [IN] node pointer
 * @return		pointer of a child node	: this may be NULL when there is no child node
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-05-2007 [DongYoung Seo] Change function work 
 *								from checking to return child pointer
 * @version		NOV-11-2008 [DongYoung Seo] remove a ASSERT to check bReset is FFAT)TRUE
 *								to use this function on debug routine
 */
FFatNode*
FFAT_GetChildPtr(FFatNode* pNode, t_boolean bReset)
{
	FFatNode*		pNodeChild;

	FFAT_ASSERT(pNode);

	pNodeChild = (FFatNode*)(((Node*)pNode)->pChildNode);

	IF_LK(bReset == FFAT_TRUE)
	{
		((Node*)pNode)->pChildNode = NULL;
	}

	return pNodeChild;
}


/**
 * get FAT sector of cluster
 *
 * @param		pVolInfo		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster allocation count
 * @return		FFAT_OK			: success
 * @author		InHwan Choi
 * @version		DEC-11-2007 [InHwan Choi] First Writing.
 */
t_uint32
FFAT_GetFatSectorOfCluster(FFatNode* pNode)
{
	Vol* 	pVol;
	pVol = ((Node*)pNode)->pVol;
	
	return FFATFS_GetFatSectorOfCluster(VOL_VI(pVol), ((Node*)pNode)->dwCluster);
}


/**
 * get offset of FAT sector 
 *
 * @param		pVolInfo		: [IN] volume pointer
 * @param		dwCluster		: [IN] cluster allocation count
 * @return		FFAT_OK			: success
 * @author		InHwan Choi
 * @version		DEC-11-2007 [InHwan Choi] First Writing.
 */
t_uint32
FFAT_GetOffsetOfFatSector(FFatNode* pNode)
{
	Vol*		pVol;

	pVol = ((Node*)pNode)->pVol;

	return FFATFS_GetOffsetOfFatSector(VOL_VI(pVol), ((Node*)pNode)->dwCluster);
}


/**
 * get a spin lock
 *	this lock is provided for Abstraction layer for VFS
 *	do not call FFAT function after lock, it will be a dead-lock !!

 *
 * @return		FFAT_OK			: success
 * @author		DongYount Seo
 * @version		APR-17-2008 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_GetSpinLock(void)
{
	return ffat_lock_getSpin();
}


/**
 * release spin lock
 *	this lock is provided for Abstraction layer for VFS
 *	do not call FFAT function after lock, it will be a dead-lock !!
 *
 * @return		FFAT_OK			: success
 * @author		DongYount Seo
 * @version		APR-17-2008 [DongYoung Seo] First Writing.
 */
FFatErr
FFAT_PutSpinLock(void)
{
	return ffat_lock_putSpin();
}


/**
* reset node structure
*	This function is used for optimal initialization.
*	do not need to initialize all of the node structure to 0x00
*
* @param		pNode			: pointer of node
* @author		DongYount Seo
* @version		SEP-10-2008 [DongYoung Seo] First Writing.
*/
void
FFAT_ResetNodeStruct(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	ffat_node_resetNodeStruct((Node*)pNode);
	return;
}


/**
 * get logical device information from volume information
 *
 * @param		pVol		: pointer of node
 * @author		GwangOk Go
 * @version		DEC-15-2008 [GwangOk Go] First Writing.
 */
FFatLDevInfo*
FFAT_GetLDevInfo(FFatVol* pVol)
{
	FFAT_ASSERT(pVol);
	
	return &((Vol*)pVol)->stDevInfo;
}


/**
 * set logical device info of volume structure
 *
 * @param		pVol			: volume pointer
 * @param		pLDevInfo		: logical device info
 * @author		GwangOk Go
 * @version		MAR-05-2009 [GwangOk Go] First Writing.
 */
void
FFAT_SetLDevInfo(FFatVol* pVol, FFatLDevInfo* pLDevInfo)
{
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLDevInfo);

	FFAT_MEMCPY(&((Vol*)pVol)->stDevInfo, pLDevInfo, sizeof(FFatLDevInfo));

	return;
}


/**
* retrieve the GUID of node
*
* @param		pNode			: pointer of node
* @param		pstXDEInfo		: pointer storage of GUID, permission
* @author		JeongWoo Park
* @version		SEP-10-2008 [JeongWoo Park] First Writing.
*/
FFatErr
FFAT_GetGUIDFromNode(FFatNode* pNode, void* pstXDEInfo)
{
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pstXDEInfo);

	return ffat_node_GetGUIDFromNode((Node*)pNode, pstXDEInfo);
}


/**
 * check whether node is accessible (log file is not accessible)
 * ACCESS_MODE may be implemented later
 *
 * @param		pNode			: pointer of node
 * @author		GwangOk Go
 * @version		JAN-21-2009 [GwangOk Go] First Writing.
 */
FFatErr
FFAT_CheckPermission(FFatNode* pNode)
{
	FFAT_ASSERT(pNode);

	return ffat_node_isAccessible((Node*)pNode, NODE_ACCESS_MASK);
}


// debug begin
#ifdef FFAT_DEBUG
	extern FFatErr
	FFAT_ChangeFatTableFat32(FFatVol* pVol)
	{
		return FFATFS_ChangeFatTableFAT32(VOL_VI((Vol*)pVol));
	}

#endif
// debug end

