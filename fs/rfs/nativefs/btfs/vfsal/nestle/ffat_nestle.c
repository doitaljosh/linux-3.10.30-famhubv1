/*
 * TFS4 2.0 FFAT(Final FAT) filesystem Developed by ESS team.
 *
 * Copyright 2006 by Software Laboratory, Samsung Electronics, Inc.,
 * 416, Maetan-3Dong, Yeongtong-Gu, Suwon-City, Gyeonggi-Do, Korea.
 * All rights reserved.
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

/** 
 * @file		ffat_nestle.c
 * @brief		interface implementation between FFAT and Nestle
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		OCT-11-2006 [DongYoung Seo] First writing
 * @version		FEB-02-2009 [DongYoung Seo] Remove code for user fast seek
 * @see			None
 */


/************************************************************************/
/*                                                                      */
/* Nestle Programmers Guide                                             */
/*                                                                      */
/* 1. Dynamic memory allocation                                         */
/* 2. Inter-file level concurrent read                                  */
/* 3. file level concurrent write                                       */
/*                                                                      */
/*                                                                      */
/*                                                                      */
/* Porting Guide                                                        */
/* 1. INODE number change : NS_ChangeVnodeIndex                         */
/* 2. native should VNODE : NS_FindOrCreateVnodeFromVcb                 */
/* 3. Open Unlink                                                       */
/* 4. Open Rename                                                       */
/* 5. Symbolic Link : Native should create and read                     */
/* 6.                                                                   */
/*                                                                      */
/*                                                                      */
/*                                                                      */
/*                                                                      */
/*                                                                      */
/************************************************************************/


// for base library

// for ESS_BASE
#include "ess_debug.h"
#include "ess_math.h"
#include "ess_bitmap.h"

// for Nestle
#include "ns_nativefs.h"

// for FFAT
#include "ffat_config.h"
#include "ffat_types.h"
#include "ffat_api.h"

#include "ffatfs_types.h"
#include "ffat_addon_types.h"
#include "ffat_addon_types_internal.h"

#include "ffat_al.h"
#include "ffat_nestle.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_VFSAL_NESTLE)


// debug begin

//#define _DEBUG_NODE_STORAGE
//#define _DEBUG_FFAT_NESTLE
//#define _DEBUG_PRINT_INOUT
#ifdef BTFS_DETAILED_LOG
	#undef _DEBUG_FFAT_NESTLE
	#define _DEBUG_FFAT_NESTLE
#endif

// debug end


#define		_4BYTE_INODE_NUMBER			//user 4byte inode number
										// this is temporarily feature to check RFS behavior.

// configurations
#ifndef __cplusplus
	extern const NS_NATIVEFS_OPERATIONS g_nfBTFS;
#endif

#if (NS_BYTE_ORDER == NS_LITTLE_ENDIAN)
	#define	BTFS_NAME_STR		L"btfs"
#else
	#define BTFS_NAME_STR		"\0""b\0""t\0""f\0""s\0"
#endif

#define		_FAT_TYPE_LEN			(5)			// fat type string length, (FAT16, FAT32)
#define		_DIR_FIRST_ENTRY_OFFSET	(64)		// the first entry offset on a directory
#define		_FAST_SEEK_BUFF_SIZE	(2*1024)	// fast seek buffer size
												// automatically assigned to a node
#define		_DEC_BUFF_SIZE			(1*1024)	// DEC buffer size
#define		_CLUSTER_INIT_BUFF_SIZE	(64*1024)	// buffer size for cluster initialization

#define		_VOL(_pVcb)				(FFatVol*)(NS_GetNativeVcb(_pVcb))
#define		_VOL_ROOT(_pVcb)		NS_GetRootFromVcb(_pVcb)

#define		_NODE(_pVnode)			(FFatNode*)(NS_GetNativeNode(_pVnode))

#define		_VNODE_LINK_COUNT		1			// HARD LINK COUNT FOR A NODE
// _s : size
// _t : type (purpose)
#define _MALLOC(_s, _t)				NS_AllocateMemory(_s)
// _p : pointer
// _s : size
#define _FREE(_p, _s)				do { if (_p) NS_FreeMemory(_p); } while (0)

#ifndef _DEBUG_PRINT_INOUT
	#define FFAT_PRINT_VFSAL(msg)
#else
	#define FFAT_PRINT_VFSAL(msg)	FFAT_PRINT_VERBOSE(msg)
	//#define FFAT_PRINT_VFSAL(msg)	FFAT_PRINT_DEBUG(msg)		// print func, line, time, tid
	//#define FFAT_PRINT_VFSAL(msg)	FFAT_PRINT_VERBOSE(msg)
#endif


#define _INIT_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_init(+)"), _NSD_GET_THREAD_ID()));
#define _INIT_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_init(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _TERMINATE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_terminate(+)"), _NSD_GET_THREAD_ID()));
#define _TERMINATE_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_terminate(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _MOUNT_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mount(+) - Vcb[%p],Flags[%x]"), _NSD_GET_THREAD_ID(), pVcb, *pMountFlag));
#ifndef FFAT_BLOCK_IO
	#define _MOUNT_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mount(-) - r[%x],err[-0x%X],VnodeRoot[%p],Flags[%x]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnodeRoot, *pMountFlag));
#else
	#define _MOUNT_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mount(-) - r[%x],err[-0x%X],VnodeRoot[%p],Flags[%x],SectorSize[%d],ClusterSize[%d],FirstDataSector[%d],BlockSize[%d]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnodeRoot, *pMountFlag, dwSectorSize, dwClusterSize, dwFirstDataSector, dwBlockSize));
#endif
#define _FORMAT_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_format(+)"), _NSD_GET_THREAD_ID()));
#define _FORMAT_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_format(-) - r[%x],err[-0x%X]"), _NSD_GET_THREAD_ID(), r, -err));
#define _CHKDSK_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_chkdsk(+)"), _NSD_GET_THREAD_ID()));
#define _CHKDSK_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_chkdsk(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _SYNCVOL_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_syncVol(+) - Vcb[%p]"), _NSD_GET_THREAD_ID(), pVcb));
#define _SYNCVOL_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_syncVol(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _GETVOLINFO_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_getVolumeInfo(+) - Vcb[%p]"), _NSD_GET_THREAD_ID(), pVcb));
#define _GETVOLINFO_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_getVolumeInfo(-) - r[%x],FreeClusters[%d]"), _NSD_GET_THREAD_ID(), r, pVolumeInfo->dwNumFreeClusters));
#define _UMOUNT_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_umount(+) - Vcb[%p],Flags[%x]"), _NSD_GET_THREAD_ID(), pVcb, dwUnmountFlags));
#define _UMOUNT_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_umount(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _REMOUNT_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_remount(+) - Vcb[%p],Flags[%x]"), _NSD_GET_THREAD_ID(), pVcb, *pdwFlag));
#define _REMOUNT_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_remount(-) - r[%x],err[-0x%X],Flags[%x]"), _NSD_GET_THREAD_ID(), r, -err, *pdwFlag));
#define _IOCTL_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_ioctl(+)"), _NSD_GET_THREAD_ID()));
#define _IOCTL_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_ioctl(-)"), _NSD_GET_THREAD_ID()));
#define _FSCTL_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_fsctl(+)"), _NSD_GET_THREAD_ID()));
#define _FSCTL_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_fsctl(-)"), _NSD_GET_THREAD_ID()));
#define _OPEN_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_open(+) - Fcb[%p]"), _NSD_GET_THREAD_ID(), pFcb));
#define _OPEN_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_open(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _LOOKUP_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_lookup(+) - Parent[%p],Name[%s],NameLen[%d],LookupFlag[%x]"),	\
											_NSD_GET_THREAD_ID(), pnParent, ffat_debug_w2a((t_wchar*)pwsName, _VOL(NS_GetVcbFromVnode(pnParent))), dwNameLen, dwLookupFlag));
#define _LOOKUP_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_lookup(-) - r[%x],err[-0x%X],Vnode[%p]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnode));
#define _MKDIR_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mkdir(+) - Parent[%p],Name[%s],NameLen[%d],Attr[%x],UID[%d],GID[%d],Perm[%d]"),	\
											_NSD_GET_THREAD_ID(), pnParent, ffat_debug_w2a((t_wchar*)pwsName, _VOL(NS_GetVcbFromVnode(pnParent))), dwNameLen, dwAttr, dwUID, dwGID, wPerm));
#define _MKDIR_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mkdir(-) - r[%x],err[-0x%X],Vnode[%p]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnode));
#define _CREATE_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_create(+) - Parent[%p],Name[%s],NameLen[%d],Attr[%x],UID[%d],GID[%d],Perm[%d]"),	\
											_NSD_GET_THREAD_ID(), pnParent, ffat_debug_w2a((t_wchar*)pwsName, _VOL(NS_GetVcbFromVnode(pnParent))), dwNameLen, dwAttributes, dwUID, dwGID, wPerm));
#define _CREATE_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_create(-) - r[%x],err[-0x%X],Vnode[%p]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnode));
#define _CREATESYMLINK_IN	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_createSymlink(+) - Parent[%p],Name[%s],NameLen[%d],Attr[%x],UID[%d],GID[%d],Perm[%d],TargetPath[%s]"),	\
											_NSD_GET_THREAD_ID(), pnParent, ffat_debug_w2a((t_wchar*)pwsName, _VOL(NS_GetVcbFromVnode(pnParent))), dwNameLen, dwAttr, dwUID, dwGID, wPerm,	\
											ffat_debug_w2a_2nd((t_wchar*)pwsTargetPath, _VOL(NS_GetVcbFromVnode(pnParent)))));
#define _CREATESYMLINK_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_createSymlink(-) - r[%x],err[-0x%X],Vnode[%p]"), _NSD_GET_THREAD_ID(), r, -err, *ppVnode));
#define _READSYMLINK_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readSymlink(+) - Vnode[%p]"), _NSD_GET_THREAD_ID(), pVnode));
#define _READSYMLINK_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readSymlink(-) - r[%x],Path[%s]"), _NSD_GET_THREAD_ID(), r, ffat_debug_w2a((t_wchar*)pwsPath, _VOL(NS_GetVcbFromVnode(pVnode)))));
#define _UNLINK_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_ffat_unlink(+) - Parent[%p],Target[%p],IsOpened[%d]"), _NSD_GET_THREAD_ID(), pnParent, pnTarget, bIsOpened));
#define _UNLINK_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_ffat_unlink(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _DELETENODE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_deleteNode(+) - Target[%p]"), _NSD_GET_THREAD_ID(), pnTarget));
#define _DELETENODE_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_deleteNode(-) - r[%x],err[-0x%X]"), _NSD_GET_THREAD_ID(), r, -err));
#define _MOVE2_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_move2(+) - SourceParent[%p],Source[%p],pnTargetParent[%p],pnTarget[%p],NewName[%s],NameLen[%d],bIsSourceOpened[%d],bIsTargetOpened[%d]"),	\
											_NSD_GET_THREAD_ID(), pnSourceParent, pnSource, pnTargetParent, pnTarget, ffat_debug_w2a((t_wchar*)pwszNewName, _VOL(NS_GetVcbFromVnode(pnSourceParent))),	\
											dwNameLen, bIsSourceOpened, bIsTargetOpened));
#define _MOVE2_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_move2(-) - r[%x],err[-0x%X]"), _NSD_GET_THREAD_ID(), r, -err));
#define _READDIRUNLINK_IN	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readdirUnlink(+)"), _NSD_GET_THREAD_ID()));
#define _READDIRUNLINK_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readdirUnlink(-)"), _NSD_GET_THREAD_ID()));
#define _CLEANDIR_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_cleanDir(+)"), _NSD_GET_THREAD_ID()));
#define _CLEANDIR_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_cleanDir(-)"), _NSD_GET_THREAD_ID()));
#define _DESTROYNODE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_destroyNode(+) - Vnode[%p]"), _NSD_GET_THREAD_ID(), pVnode));
#define _DESTROYNODE_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_destroyNode(-) - r[%x],err[-0x%X]"), _NSD_GET_THREAD_ID(), r, -err));
#define _SETATTRIBUTES_IN	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setAttributes(+)"), _NSD_GET_THREAD_ID()));
#define _SETATTRIBUTES_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setAttributes(-)"), _NSD_GET_THREAD_ID()));
#define _TRUNCATE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_truncate(+)"), _NSD_GET_THREAD_ID()));
#define _TRUNCATE_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_truncate(-)"), _NSD_GET_THREAD_ID()));
#define _SETFILETIME_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setFileTime(+)"), _NSD_GET_THREAD_ID()));
#define _SETFILETIME_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setFileTime(-)"), _NSD_GET_THREAD_ID()));
#define _MAPBLOCKS_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mapBlocks(+) - Vnode[%p],BlockIndex[%u],BlockCnt[%d]"), _NSD_GET_THREAD_ID(), pVnode, dwBlockIndex, dwBlockCnt));
#define _MAPBLOCKS_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_mapBlocks(-) - r[%x],BlockNum[%u],ContBlockCnt[%d]"), _NSD_GET_THREAD_ID(), r, *pdwBlockNum, *pdwContBlockCnt));
#define _PERMISSION_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_permission(+) - Vnode[%p],OperationMode[%x]"), _NSD_GET_THREAD_ID(), pVnode, dwOperationMode));
#define _PERMISSION_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_permission(-) - r[%x]"), _NSD_GET_THREAD_ID(), r));
#define _EXPANDCLUSTERS_IN	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_expandClusters(+)"), _NSD_GET_THREAD_ID()));
#define _EXPANDCLUSTERS_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_expandClusters(-)"), _NSD_GET_THREAD_ID()));
#define _SYNCFILE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_syncFile(+)"), _NSD_GET_THREAD_ID()));
#define _SYNCFILE_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_syncFile(-)"), _NSD_GET_THREAD_ID()));
#define _CLOSE_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_close(+)"), _NSD_GET_THREAD_ID()));
#define _CLOSE_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_close(-)"), _NSD_GET_THREAD_ID()));
#define _READDIR_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readdir(+) - Fcb[%p],Vnode[%p],Offset[%llu]"), _NSD_GET_THREAD_ID(), pFcb, NS_GetVnodeFromFcb(pFcb), NS_GetOffsetFromFcb(pFcb)));
#define _READDIR_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_readdir(-) - r[%x],NumberOfRead[%u],VnodeID[%llu],Name[%s],AltName[%s]"),	\
											_NSD_GET_THREAD_ID(), r, *pdwNumberOfRead, pEntry->llVnodeID,	\
											ffat_debug_w2a((t_wchar*)pEntry->wszName, _VOL(NS_GetVcbFromVnode(NS_GetVnodeFromFcb(pFcb)))),	\
											ffat_debug_w2a_2nd((t_wchar*)pEntry->wszAltName, _VOL(NS_GetVcbFromVnode(NS_GetVnodeFromFcb(pFcb))))));
#define _WRITE_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_write(+)"), _NSD_GET_THREAD_ID()));
#define _WRITE_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_write(-)"), _NSD_GET_THREAD_ID()));
#define _READ_IN			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_read(+)"), _NSD_GET_THREAD_ID()));
#define _READ_OUT			FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_read(-)"), _NSD_GET_THREAD_ID()));
#define _SETGUIDMODE_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setGuidMode(+)"), _NSD_GET_THREAD_ID()));
#define _SETGUIDMODE_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setGuidMode(-)"), _NSD_GET_THREAD_ID()));
#define _CONVERTNAME_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_convertName(+)"), _NSD_GET_THREAD_ID()));
#define _CONVERTNAME_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_convertName(-)"), _NSD_GET_THREAD_ID()));
#define _SETXATTR_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setXattr(+) - Vnode[%p],Name[%s],Value[%s],ValueSize[%u],ID[%d],Flag[%d]"), _NSD_GET_THREAD_ID(),pVnode,psName,(char*)pValue,dwValueSize,dwID,dwFlag));
#define _SETXATTR_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_setXattr(-) - r[%x]"), _NSD_GET_THREAD_ID(),r));
#define _GETXATTR_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_getXattr(+) - Vnode[%p],Name[%s],BuffSize[%u],ID[%d]"), _NSD_GET_THREAD_ID(),pVnode,psName,dwValueSize,dwID));
#define _GETXATTR_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_getXattr(-) - r[%x],Value[%s],SizeRead[%u]"), _NSD_GET_THREAD_ID(),r,(char*)pValue,*pdwSizeRead));
#define _LISTXATTR_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_listXattr(+) - Vnode[%p],OutBufSize[%u]"), _NSD_GET_THREAD_ID(),pVnode,dwOutBufSize));
#define _LISTXATTR_OUT		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_listXattr(-) - r[%x],List[%s],SizeRead[%u]"), _NSD_GET_THREAD_ID(),r,pOutBuf,*pdwSizeRead));
#define _REMOVEXATTR_IN		FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_removeXattr(+) - Vnode[%p],Name[%s],ID[%d]"), _NSD_GET_THREAD_ID(),pVnode,psName,dwID));
#define _REMOVEXATTR_OUT	FFAT_PRINT_VFSAL((_T("[VFSAL][%u]_removeXattr(-) - r[%x]"), _NSD_GET_THREAD_ID(),r));


// Vol/Node storage for static memory allocation

#ifndef FFAT_DYNAMIC_ALLOC
	static EssList		_slFreeNodeStorage;		// head list of Node Storage, free list
	static EssList		_slFreeVolStorage;		// head list of Node Storage, free list

	typedef struct
	{
		EssList		slFree;
		FFatVol		stVol;
	} _VolStorage;

	typedef struct 
	{
		EssList		slFree;
		FFatNode	stNode;
	} _NodeStorage;

	static FFatErr		_initVolStorage(void);
	static void			_terminateVolStorage(void);
	static FFatVol*		_getFreeVol(void);
	static FFatErr		_releaseVol(FFatVol* pVol);

	static FFatErr		_initNodeStorage(void);
	static void			_terminateNodeStorage(void);
	static FFatNode*	_getFreeNode(void);
	static FFatErr		_releaseNode(FFatNode* pNode);

	static _VolStorage*		_pVolStorage;			// global pointer for volume storage
	static _NodeStorage*	_pNodeStorage;			// global pointer for node storage

	#define	_INIT_VOL_STORAGE			_initVolStorage
	#define	_TERMINATE_VOL_STORAGE		_terminateVolStorage
	#define	_INIT_NODE_STORAGE			_initNodeStorage
	#define	_TERMINATE_NODE_STORAGE		_terminateNodeStorage

#else
	#define	_INIT_VOL_STORAGE()				FFAT_OK
	#define	_TERMINATE_VOL_STORAGE()
	#define	_INIT_NODE_STORAGE()			FFAT_OK
	#define	_TERMINATE_NODE_STORAGE()
#endif

static FFatVol*		_allocVol(void);
static FFatNode*	_allocNode(void);
static void			_freeVol(FFatVol* pVol);
static void			_freeNode(FFatNode* pNode);


// functions for NS_NATIVEFS_OPERATIONS
static FERROR	_init(void);
static FERROR	_terminate(void);
static FERROR	_mount(		IN	NS_PVCB				pVcb, 
							IO	NS_PMOUNT_FLAG		pdwMountFlag,
							OUT	NS_PVNODE*			ppVnodeRoot);
static FERROR	_format(	IN	NS_PLOGICAL_DISK	pLogDisk,
							IN	NS_PFORMAT_PARAMETER	pFormatParam,
							IN	NS_PFORMAT_DISK_INFO		pDiskInfo);
static FERROR	_chkdsk(	IN	NS_PVCB				pVcb,
							IN	void*				pInBuf,
							IN	unsigned int		dwInBufSize,
							OUT	void*				pOutBuf,
							IN	unsigned int		dwOutBufSize);

// functions for NS_VCB_OPS
static FERROR	_syncVol(	IN NS_PVCB pVcb);
static FERROR	_getVolumeInfo(	IN	NS_PVCB			pVcb,
							OUT NS_PVOLUME_INFORMATION	pVolumeInfo);
static FERROR	_umount(	IN	NS_PVCB				pVcb,
							IN	NS_UNMOUNT_FLAG		dwUnmountFlags);
static FERROR	_ioctl(		IN	NS_PVCB				pVcb,
							IN	unsigned int		dwControlCode,
							IN	void*				pInBuf,
							IN	unsigned int		dwInBufSize,
							OUT	void*				pOutBuf,
							OUT	unsigned int		dwOutBufSize);
static FERROR	_fsctl(		IN	unsigned int		dwControlCode,
							IN	void*				pInBuf,
							IN	unsigned int		dwInBufSize,
							OUT	void*				pOutBuf,
							OUT	unsigned int		dwOutBufSize);
static FERROR	_remount(	IN NS_PVCB pVcb,
							IO	NS_PREMOUNT_FLAG pdwFlag);

// functions for NS_VNODE_OPS
static FERROR _open(		IN	NS_PFCB	pFcb);
static FERROR _lookup(		IN	NS_PVNODE			pnParent,
							IN	const wchar_t*		pwsName,
							IN	unsigned int		dwNameLen,
							IN	NS_LOOKUP_FLAG		dwLookupFlag,
							OUT NS_PVNODE*			ppVnode);
static FERROR _mkdir(		IN	NS_PVNODE			pnParent,
							IN	const wchar_t*		pwsName,
							IN	unsigned int		dwNameLen,
							IN	NS_FILE_ATTR		dwAttr,
							IN	unsigned int		dwUID,
							IN	unsigned int		dwGID,
							IN	NS_ACL_MODE			wPerm,
							OUT NS_PVNODE*			ppVnode);
static FERROR _create(		IN	NS_PVNODE			pnParent,
							IN	const wchar_t*		pwsName,
							IN	unsigned int		dwNameLen,
							IN	NS_FILE_ATTR		dwAttributes,
							IN	unsigned int		dwUID,
							IN	unsigned int		dwGID,
							IN	NS_ACL_MODE			wPerm,
							OUT NS_PVNODE*			ppVnode);
static FERROR _createSymlink(
							IN	NS_PVNODE			pnSourceParent,
							IN	const wchar_t*		pwsName,
							IN	unsigned int		dwNameLen,
							IN	NS_FILE_ATTR		dwAttr,
							IN	unsigned int		dwUID,
							IN	unsigned int		dwGID,
							IN	NS_ACL_MODE			wPerm,
							IN	const wchar_t*		pwsTargetPath,
							OUT	NS_PVNODE*			ppVnode);
static FERROR _readSymlink(	IN	NS_PVNODE			pVnode,
							OUT	wchar_t*			pwsPath,
							IN	unsigned int	dwLinkBuffSize,
							OUT unsigned int*	pLinkLen);
static FERROR _ffat_unlink(	IN	NS_PVNODE			pnParent,
							IN	NS_PVNODE			pnTarget,
							IN	BOOL				bIsOpened);

static FERROR _deleteNode(	IN	NS_PVNODE pnTarget);

static FERROR _move2(		IN	NS_PVNODE			pnSourceParent,
							IN	NS_PVNODE			pnSource,
							IN	NS_PVNODE			pnTargetParent,
							IN	NS_PVNODE			pnTarget,
							IN	const wchar_t*		pwszNewName,
							IN	unsigned int		dwNameLen,
							IN	BOOL				bIsSourceOpened,
							IN	BOOL				bIsTargetOpened);

static FERROR _readdirUnlink(
							IN	NS_PVNODE			pVnode,
							IN	NS_PFCB				pFcb,
							IN	unsigned long long	llVnodeID);

static FERROR _cleanDir(	IN	NS_PVNODE			pVnode);

static FERROR _destroyNode(	IN	NS_PVNODE			pVnode);
//101215_chunum.kong_Excute FFAT_Close() excepting release free Vnode. this is modifed _destroyNode() which is basis.
static FERROR _clearNode(	IN	NS_PVNODE			pVnode);


static FERROR _setAttributes(IN	NS_PVNODE			pVnode,
							IN	NS_FILE_ATTR		dwAttributes);

static FERROR _truncate(	IN	NS_PVNODE			pVnode,
							IN	NS_FILE_SIZE		llFileSize,
							IN	BOOL				bFillZero);

static FERROR _setFileTime(	IN	NS_PVNODE			pVnode,
							IN	NS_PSYS_TIME		ptmCreated,
							IN	NS_PSYS_TIME		ptmLastAccessed,
							IN	NS_PSYS_TIME		ptmLastModified);

static FERROR _mapBlocks(	IN	NS_PVNODE			pVnode,
							IN	unsigned int		dwBlockIndex,
							IN	unsigned int		dwBlockCnt,
							OUT unsigned int*		pdwBlockNum,
							OUT unsigned int*		pdwContBlockCnt);

static FERROR _permission(	IN	NS_PVNODE			pVnode,
							IN	NS_OPERATION_MODE	dwOperationMode);

static FERROR _expandClusters(
							IN	NS_PVNODE			pVnode,
							IN	NS_FILE_SIZE		llSize);

static FERROR _syncFile(	IN	NS_PVNODE			pVnode,
							IN	FILE_SIZE			llValidFileSize,
							IN	BOOL				bUpdateAccessTime,
							IN	BOOL				bUpdateModifyTime);

static FERROR _convertName(	IN NS_PVNODE			pParent,
							IN const wchar_t*		pwszInputName,
							IN const unsigned int	pwszInputSize,
							OUT wchar_t*			pwsOutputName,
							IO unsigned int			dwOutputNameSize,
							IN unsigned int			dwConvertType);

static FERROR _setXattr(	IN NS_PVNODE			pVnode,
							IN const char*			psName,
							IN const void*			pValue,
							IN unsigned int			dwValueSize,
							IN NS_XATTR_NAMESPACE_ID	dwID,
							IN NS_XATTR_SET_FLAG	dwFlag);

static FERROR _getXattr(	IN NS_PVNODE			pVnode,
							IN const char*			psName,
							IN const void*			pValue,
							IN unsigned int			dwValueSize,
							IN NS_XATTR_NAMESPACE_ID	dwID,
							OUT unsigned int*		pdwSizeRead);

static FERROR _listXattr(	IN NS_PVNODE			pVnode,
							OUT char*				pOutBuf,
							IN unsigned int			dwOutBufSize,
							OUT unsigned int*		pdwSizeRead);

static FERROR _removeXattr(	IN NS_PVNODE			pVnode,
							IN const char*			psName,
							IN NS_XATTR_NAMESPACE_ID	dwID);

static FERROR _setGuidMode(	IN	NS_PVNODE			pVnode,
							IN	unsigned int		dwUID,
							IN	unsigned int		dwGID,
							IN	NS_ACL_MODE			wPerm);

static unsigned long long	_getNewIDForOpenUnlink(void);

// functions for NS_FILE_OPS
static FERROR _close(		IN NS_PFCB				pFcb);

static FERROR _readdir(		IN	NS_PFCB				pFcb,
							IN	const wchar_t*		pwszFileNameToSearch,
							OUT	NS_PDIR_ENTRY		pEntry,
							OUT	unsigned int*		pdwNumberOfRead);
static FERROR _write(		IN	NS_PFCB				pFcb,
							IN	unsigned char*		pBuffer,
							IN	unsigned int		dwBytesToWrite,
							OUT unsigned int*		pdwBytesWritten);
static FERROR _read(		IN	NS_PFCB				pFcb,
							IN	unsigned char*		pBuffer,
							IN	unsigned int		dwBytesToRead,
							OUT unsigned int*		pdwBytesRead);

// static functions
static t_uint32				_convertFlag(t_uint32 dwNestleFlag, 
									const FlagConvert* pConvertDB, t_int32 dwCount);
static t_uint32				_revertFlag(t_uint32 dwFFatFlag, 
									const FlagConvert* pRevertDB, t_int32 dwCount);
static FERROR				_errnoToNestle(FFatErr dwErrno);
static unsigned long long	_getInodeNumber(FFatNode* pNode);
static unsigned long long	_getInodeNumber2(FFatVol* pVol, t_uint32 dwIno1, t_uint32 dwIno2);
static FFatErr				_setupVnode(NS_PVCB pVCB, NS_PVNODE pVnodeParent, FFatNode* pNode,
									NS_ACL_MODE wPerm, unsigned int dwUID,
									unsigned int dwGID, NS_PVNODE* ppVnode, BOOL* pbNew);

static FFatErr				_recoveryDirtySizeNode(NS_PVNODE pVnode);

static t_uint16	_getDosDate(PSYS_TIME pDate);
static t_uint16	_getDosTime(PSYS_TIME pTime);
static void		_getNestleTime(t_uint16 wTime, t_uint8 bTimeTenth, PSYS_TIME pTime);
static void		_getNestleDate(t_uint16 wDate, PSYS_TIME pTime);
static void		_getNestleDateTime(t_uint32 dwTime, t_uint32 dwTimeTenth, PSYS_TIME pTime);
static t_int32	_readdirGetNode(NS_PVNODE pVnodeParent, NS_PFCB pFcb,
								t_uint32 dwOffset, FFatNode* pNode);
static FFatErr	_fsctlDEC(unsigned int dwCmd, NS_PFS_DE_CACHE pNSDEC);
static void		_updateVnodeTime(NS_PVNODE pVnode, FFatNodeStatus* pStat);
static void		_getLDevStatus(FFatLDevInfo* pLDevInfo, NS_PVCB pVcb);
static void		_setLDevBlockInfo(FFatLDevInfo* pLDevInfo, t_int32 dwBlockSize);

#ifdef FFAT_BLOCK_IO
	static t_int32	_getOptimalBlockSize(t_int32 dwOldBlockSize, t_int32 dwSectorSize,
									t_int32 dwClusterSize, t_uint32 dwFirstDataSector);
#endif

// static variables
static const NS_FILE_OPS _fopFile =
{
	_close,					// Close
	NULL,					// ReadDirectory
	_read,					// ReadFile
	_write,					// WriteFile
	NULL,					// WriteFileBegin
	NULL,					// WriteFileEnd
	NULL,					// SeekFile
};


static const NS_VNODE_OPS _vopFile =
{
	_open,					// Open

	NULL,					// LookupChild
	NULL,					// CreateDirectory
	NULL,					// CreateFile
	NULL,					// CreateSymlink
	NULL,					// ReadSymlink
	_ffat_unlink,			// Unlink
	NULL,					// Move
	_move2,					// Move2
	NULL,					// READDIR unlink
	NULL,					// clean directory

	_deleteNode,			// DeleteNode
	_destroyNode,			// DestroyNode
	//101215_chunum.kong_Excute FFAT_Close() excepting release free Vnode. this is modifed _destroyNode() which is basis.
	_clearNode,				// ClearNode

	_setAttributes,			// SetAttributes
	_truncate,				// SetFileSize
	_setFileTime,			// SetFileTime
	_setGuidMode,			// SetGuidMode

	_mapBlocks,				// MapBlocks
	_syncFile,				// SyncFile

	_permission,			// Permission
	_expandClusters,		// Expand
	_convertName,			// ConvertName (Get short/long name)

	_setXattr,				// SetXAttribute 
	_getXattr,				// GetXAttribute
	_listXattr,				// ListXAttributes
	_removeXattr			// RemoveXAttribute
};


static const NS_FILE_OPS _fopDir =
{
	_close,					// Close
	_readdir,				// ReadDirectory
	NULL,					// ReadFile
	NULL,					// WriteFile
	NULL,					// WriteFileBegin
	NULL,					// WriteFileEnd
	NULL,					// SeekFile (RewindDir)
};


static const NS_VNODE_OPS _vopDir =
{
	_open,					// Open

	_lookup,				// LookupChild
	_mkdir,					// CreateDirectory
	_create,				// CreateFile
	_createSymlink,			// CreateSymlink
	NULL,					// ReadSymlink
	_ffat_unlink,			// Unlink
	NULL,					// Move
	_move2,					// Move2
	_readdirUnlink,			// Readdir Unlink
	_cleanDir,				// Clean a directory

	_deleteNode,			// DeleteNode
	_destroyNode,			// DestroyNode
	//101215_chunum.kong_Excute FFAT_Close() excepting release free Vnode. this is modifed _destroyNode() which is basis.
	_clearNode,			// ClearNode

	_setAttributes,			// SetAttributes
	NULL,					// SetFileSize
	_setFileTime,			// SetFileTime
	_setGuidMode,			// SetGuidMode

	NULL,					// MapBlocks
	_syncFile,				// SyncFile

	NULL,					// Permission
	NULL, 					// Expand
	_convertName,			// ConvertName (Get short/long name)

	_setXattr,				// SetXAttribute 
	_getXattr,				// GetXAttribute
	_listXattr,				// ListXAttributes
	_removeXattr			// RemoveXAttribute
};


static const NS_FILE_OPS _fopSymlink =
{
	NULL,					// Close
	NULL,					// ReadDirectory
	NULL,					// ReadFile
	NULL,					// WriteFile
	NULL,					// WriteFileBegin
	NULL,					// WriteFileEnd
	NULL,					// SeekFile (RewindDir)
};


static const NS_VNODE_OPS _vopSymlink =
{
	NULL,					// Open

	NULL,					// LookupChild
	NULL,					// CreateDirectory
	NULL,					// CreateFile
	NULL,					// CreateSymlink
	_readSymlink,			// ReadSymlink
	_ffat_unlink,			// Unlink
	NULL,					// Move
	_move2,					// Move2
	NULL,					// Readdir Unlink
	NULL,					// Clean a directory

	_deleteNode,			// DeleteNode
	_destroyNode,			// DestroyNode
	//101215_chunum.kong_Excute FFAT_Close() excepting release free Vnode. this is modifed _destroyNode() which is basis.
	_clearNode,			// ClearNode

	_setAttributes,			// SetAttributes
	NULL,					// SetFileSize
	_setFileTime,			// SetFileTime
	_setGuidMode,			// SetGuidMode

	NULL,					// MapBlocks
	_syncFile,				// SyncFile

	NULL,					// Permission
	NULL, 					// Expand
	_convertName,			// ConvertName (Get short/long name)

	_setXattr,				// SetXAttribute 
	_getXattr,				// GetXAttribute
	_listXattr,				// ListXAttributes
	_removeXattr			// RemoveXAttribute
};


static const NS_FILE_OPS _fopFifoSocket =
{
	_close,					// Close
	NULL,					// ReadDirectory
	NULL,					// ReadFile
	NULL,					// WriteFile
	NULL,					// WriteFileBegin
	NULL,					// WriteFileEnd
	NULL,					// SeekFile (RewindDir)
};


static const NS_VNODE_OPS _vopFifoSocket =
{
	_open,					// Open

	NULL,					// LookupChild
	NULL,					// CreateDirectory
	NULL,					// CreateFile
	NULL,					// CreateSymlink
	NULL,					// ReadSymlink
	_ffat_unlink,			// Unlink
	NULL,					// Move
	_move2,					// Move2
	NULL,					// READDIR unlink
	NULL,					// clean directory

	_deleteNode,			// DeleteNode
	_destroyNode,			// DestroyNode
	//101215_chunum.kong_Excute FFAT_Close() excepting release free Vnode. this is modifed _destroyNode() which is basis.
	_clearNode,			// ClearNode

	_setAttributes,			// SetAttributes
	NULL,					// SetFileSize
	_setFileTime,			// SetFileTime
	_setGuidMode,			// SetGuidMode

	NULL,					// MapBlocks
	_syncFile,				// SyncFile

	_permission,			// Permission
	NULL,					// Expand
	_convertName,			// ConvertName (Get short/long name)

	_setXattr,				// SetXAttribute 
	_getXattr,				// GetXAttribute
	_listXattr,				// ListXAttributes
	_removeXattr			// RemoveXAttribute
};


static const NS_VCB_OPS _opVCB =
{
	_syncVol,				// SyncVolume
	_getVolumeInfo,			// GetVolumeInformation
	_umount,				// Unmount
	_remount,				// Remount
	_ioctl					// IOCTL/FSCTL
};


const NS_NATIVEFS_OPERATIONS g_nfBTFS=
{
	(wchar_t*)BTFS_NAME_STR,
	_mount,
	_format,
	_chkdsk,
	_init,
	_terminate,
	_fsctl,
};


NS_PNATIVEFS_OPERATIONS
GetBTFS(void)
{
	return (NS_PNATIVEFS_OPERATIONS)&g_nfBTFS;
}


// Mount Flag 
static const FlagConvert _pMountFlagTable[] = 
{
	{NS_MOUNT_READ_ONLY,			FFAT_MOUNT_RDONLY},
	{NS_MOUNT_FAT_MIRROR,			FFAT_MOUNT_FAT_MIRROR},
	{NS_MOUNT_LOG_INIT,				FFAT_MOUNT_LOG_INIT},
	{NS_MOUNT_TRANSACTION_OFF,		FFAT_MOUNT_NO_LOG},
	{NS_MOUNT_CLEAN_NAND,			FFAT_MOUNT_CLEAN_NAND},
	{NS_MOUNT_HPA,					FFAT_MOUNT_HPA},
	{NS_MOUNT_HPA_CREATE,			FFAT_MOUNT_HPA_CREATE},
	{NS_MOUNT_LLW,					FFAT_MOUNT_LOG_LLW},
	{NS_MOUNT_FULL_LLW,				FFAT_MOUNT_LOG_FULL_LLW},
	{NS_MOUNT_ERASE_SECTOR,			FFAT_MOUNT_ERASE_SECTOR},
	{NS_MOUNT_FILE_GUID,			FFAT_MOUNT_XDE},
	{NS_MOUNT_FILE_XATTR,			FFAT_MOUNT_XATTR},
	{NS_MOUNT_FILE_SPECIAL,			FFAT_MOUNT_SPECIAL_FILES},
	{NS_MOUNT_SYNC_META,			FFAT_MOUNT_SYNC_META},
#if defined(NS_CONFIG_LINUX) || defined(NS_CONFIG_RTOS) // rtos is no need to OS character set, just for test
	{NS_MOUNT_ALLOW_OS_NAMING_RULE, (FFAT_MOUNT_CASE_SENSITIVE | FFAT_MOUNT_OS_SPECIFIC_CHAR)}
#endif // end of #ifdef NS_CONFIG_LINUX
};


// remount flag convert table
static const FlagConvert _pRemountFlagTable[] = 
{
	{NS_REMOUNT_TRANSACTION_OFF,	FFAT_MOUNT_NO_LOG},
	{NS_REMOUNT_LLW,				FFAT_MOUNT_LOG_LLW},
	{NS_REMOUNT_FULL_LLW,			FFAT_MOUNT_LOG_FULL_LLW},
	{NS_REMOUNT_READ_ONLY,			FFAT_MOUNT_RDONLY}
};


// error number conversion table
static const FERROR	_errnoTable[] = 
{
	FERROR_NO_ERROR,				// FFAT_OK (0x00)
	FERROR_PATH_NOT_FOUND,			// FFAT_ENOENT
	FERROR_NO_MORE_ENTRIES,			// FFAT_ENOMOREENT
	FERROR_NO_FREE_SPACE,			// FFAT_ENOSPC
	FERROR_INSUFFICIENT_MEMORY,		// FFAT_ENOMEM
	FERROR_INVALID,					// FFAT_EINVALID
	FERROR_BUSY,					// FFAT_EBUSY
	FERROR_NOT_EMPTY,				// FFAT_ENOTEMPTY
	FERROR_NOT_A_DIRECTORY,			// FFAT_ENOTDIR
	FERROR_NOT_A_FILE,				// FFAT_EISDIR
	FERROR_ALREADY_EXISTS,			// FFAT_EEXIST
	FERROR_ACCESS_DENIED,			// FFAT_EACCESS
	FERROR_NOT_SUPPORTED,			// FFAT_ENOSUPPORT
	FERROR_READONLY_FS,				// FFAT_EROFS
	FERROR_IO_ERROR,				// FFAT_EIO
	FERROR_NAME_TOO_LONG,			// FFAT_ETOOLONG
	FERROR_MEDIA_EJECTED,			// FFAT_EXDEV
	FERROR_FILESYSTEM_CORRUPT,		// FFAT_EFAT
	FERROR_SYSTEM_PANIC,			// FFAT_EPANIC
	FERROR_NOT_INITIALIZED,			// FFAT_EINIT
	FERROR_ALREADY_INITIALIZED,		// FFAT_EINIT_ALREADY
	FERROR_DIR_FULL,				// FFAT_EDIRFULL
	FERROR_NO_XATTR,				// FFAT_ENOXATTR
	FERROR_FULL_XATTR,				// FFAT_EFULLXATTR
	FERROR_RANGE,					// FFAT_ERANGE
	FERROR_ACCESS_SYSTEMFILE,		// FFAT_EACCLOGFILE
	FERROR_RECOVERY_FAILURE,			// FFAT_ERECOVERYFAIL
	FERROR_END_OF_FILE          // FFAT_EENDOFFILE
};


static const FlagConvert _pDeviceFlagTable[] =
{
	{NS_DISK_REMOVABLE_MEDIA,		FFAT_DEV_REMOVABLE},
	{NS_DISK_SUPPORT_ERASE_SECTORS,	FFAT_DEV_ERASE}
};


static const FlagConvert _pLookupFlagTable[] =
{
	{NS_LOOKUP_CREATE,				FFAT_LOOKUP_FOR_CREATE}
};


static const FlagConvert _pChkdskFlagTable[] = 
{
	{NS_CHKDSK_REPAIR,					FFAT_CHKDSK_REPAIR},
	{NS_CHKDSK_SHOW,					FFAT_CHKDSK_SHOW},
	{NS_CHKDSK_REPAIR_INTERACTIVE,		FFAT_CHKDSK_REPAIR_INTERACTIVE},
	{NS_CHKDSK_CHECK_ONLY,				FFAT_CHKDSK_CHECK_ONLY}
};


#define		FFAT_DEBUG_NODE_STORAGE_PRINTF(_msg)
#define		FFAT_DEBUG_ALVFS_PRINTF(_msg)

// debug begin
#ifdef _DEBUG_NODE_STORAGE
	static t_uint32	_dwNodeAlloc = 0;		// allocated node count
	static t_uint32	_dwNodeRelease = 0;		// released node count

	#undef		FFAT_DEBUG_NODE_STORAGE_PRINTF
	#define		FFAT_DEBUG_NODE_STORAGE_PRINTF(_msg)	FFAT_PRINT_VERBOSE((_T("[NODE_STORAGE] %s(), %d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)
#endif

#ifdef _DEBUG_FFAT_NESTLE
	#undef		FFAT_DEBUG_ALVFS_PRINTF
	#define		FFAT_DEBUG_ALVFS_PRINTF(_msg)	FFAT_PRINT_VERBOSE((_T("\n[BTFS_ALVFS]%s(), %d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)

	#ifdef FFAT_DEBUG
		static char*	_w2a(t_wchar* psName, t_int32 dwLen);
	#endif
#endif


// debug end


//=============================================================================
//
//	EXTERNAL FUNCTIONS
//


//=============================================================================
//
//	NS_NATIVEFS_OPERATIONS operations
//


/**
* initialize FFAT filesystem
*
* @return		FERROR_NO_ERROR				: SUCCESS
* @return		else						: error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_init(void)
{
	FFatErr		r;

	_INIT_IN

	// Check attribute
	FFAT_ASSERT(FILE_ATTR_FILE == 0);
	FFAT_ASSERT((unsigned)NS_FILE_ATTR_READONLY == FFAT_ATTR_RO);
	FFAT_ASSERT((unsigned)NS_FILE_ATTR_HIDDEN == FFAT_ATTR_HIDDEN);
	FFAT_ASSERT((unsigned)NS_FILE_ATTR_SYSTEM == FFAT_ATTR_SYS);
	FFAT_ASSERT((unsigned)NS_FILE_ATTR_DIRECTORY == FFAT_ATTR_DIR);
	FFAT_ASSERT((unsigned)NS_FILE_ATTR_ARCHIVE == FFAT_ATTR_ARCH);
	FFAT_ASSERT((unsigned)FILE_ATTR_DIR_ARCHI == FFAT_ATTR_DIR_ARCH);
	FFAT_ASSERT((unsigned)FILE_ATTR_FILE_ARCHI == FFAT_ATTR_ARCH);

	FFAT_ASSERT((unsigned)FFAT_CREATE_ATTR_RO == FFAT_ATTR_RO);
	FFAT_ASSERT((unsigned)FFAT_CREATE_ATTR_HIDDEN == FFAT_ATTR_HIDDEN);
	FFAT_ASSERT((unsigned)FFAT_CREATE_ATTR_SYS == FFAT_ATTR_SYS);
	FFAT_ASSERT((unsigned)FFAT_CREATE_ATTR_DIR == FFAT_ATTR_DIR);
	FFAT_ASSERT((unsigned)FFAT_CREATE_ATTR_ARCH == FFAT_ATTR_ARCH);

	// initializes vol/node storage
	r = _INIT_VOL_STORAGE();
	IF_UK(r != FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("fail to init Volume storage")));
		goto error;
	}

	r = _INIT_NODE_STORAGE();
	IF_UK(r != FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("fail to init Node storage")));
		_TERMINATE_VOL_STORAGE();
		goto error;
	}

	// NESTLE does not initialize NATIVEFS twice.
	// so this it the first initialization
	r = FFAT_Init(FFAT_TRUE);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to init FFATFS")));
		_TERMINATE_NODE_STORAGE();
		_TERMINATE_VOL_STORAGE();
		goto error;
	}

	FFAT_ASSERT(r == FFAT_OK);

error:
	_INIT_OUT

	return _errnoToNestle(r);
}


/**
* terminates FFAT filesystem
*
* @return		FERROR_NO_ERROR				: SUCCESS
* @return		else						: error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_terminate(void)
{
	FFatErr		r;

	_TERMINATE_IN

	r = FFAT_Terminate();

	_TERMINATE_NODE_STORAGE();
	_TERMINATE_VOL_STORAGE();

	_TERMINATE_OUT

	return _errnoToNestle(r);
}


/**
 * interface for volume mounting
 *
 * @param		pVcb			: [IN/OUT] Volume Control block
 * @param		pMountFlag		: [IN/OUT] mount flag
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_IO_ERROR				: I/O error
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing.
 * @version		17-DEC-2008 [JeongWoo Park] Add the code for root GUID.
 * @version		FEB-20-2008 [GwangOk Go] set block size regarding sector align
 * @version		23-MAR-2009 [JeongWoo Park] change the mount flag as IO/OUT.
 */
static FERROR 
_mount(	IN	NS_PVCB			pVcb,
		IO	NS_PMOUNT_FLAG	pMountFlag,
		OUT	NS_PVNODE*		ppVnodeRoot)
{
	FFatMountFlag	dwFFatFlag = FFAT_MOUNT_NO_FLAG;
	FFatVol*		pVol = NULL;			// volume pointer
	FFatNode*		pNodeRoot = NULL;		// root node of a volume
	FFatLDevInfo	stLDevInfo = {0,};
	FFatErr			r = FFAT_NO_VALUE;			// error variable for BTFS

	FFatExtendedDirEntryInfo	stRootXDEInfo;	// for Root GUID

	BOOL			bNew;
	FERROR			err;			// error variable for nestle

	t_int32			dwSectorSize = 0;			// sector size
	t_int32			dwClusterSize = 0;			// cluster size
	t_int32			dwBlockSize = 0;			// block size

#ifdef FFAT_BLOCK_IO
	t_uint32		dwFirstDataSector = 0;		// first data sector
#endif

	_MOUNT_IN

	FFAT_ASSERT(ppVnodeRoot);
	FFAT_ASSERT(*ppVnodeRoot == NULL);

	dwFFatFlag = _convertFlag(*pMountFlag, _pMountFlagTable, sizeof(_pMountFlagTable) / sizeof(FlagConvert));

// when there is no HPA flag, don't check HPA on Mounting
// this is temporary code. if filenand support lseek for 64bit, this should be removed
#if defined(NS_CONFIG_LINUX) || defined(_RFS_TOOLS)
	dwFFatFlag |= FFAT_MOUNT_HPA_NO_CHECK;
#endif

	pVol = _allocVol();
	IF_UK (pVol == NULL)
	{
		FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatVol")));

		err = FERROR_INSUFFICIENT_MEMORY;
		goto err_nestle;
	}

	pNodeRoot = _allocNode();
	IF_UK (pNodeRoot == NULL)
	{
		FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatNode")));

		err = FERROR_INSUFFICIENT_MEMORY;
		goto err_nestle;
	}

	NS_RegisterNativeVcb(pVcb, pVol);

	dwBlockSize = NS_GetBlockSize(pVcb);

	_getLDevStatus(&stLDevInfo, pVcb);
	_setLDevBlockInfo(&stLDevInfo, dwBlockSize);

	r = FFAT_Mount(pVol, pNodeRoot, &dwFFatFlag, &stLDevInfo, (void*)pVcb);
	IF_LK (r == FFAT_OK)
	{
		// get GID, UID, permission of root
		r = FFAT_GetGUIDFromNode(pNodeRoot, &stRootXDEInfo);
		IF_UK (r != FFAT_OK)
		{
			FFAT_PRINT_CRITICAL((_T("Fail to get root GUID from node")));
			goto out;
		}

		r = _setupVnode(pVcb, NULL, pNodeRoot, (NS_ACL_MODE)stRootXDEInfo.dwPerm,
					stRootXDEInfo.dwUID, stRootXDEInfo.dwGID, ppVnodeRoot, &bNew);
		IF_UK (r < 0)
		{
			FFAT_ASSERT(r == FFAT_ENOMEM);

			goto out;
		}

		FFAT_ASSERT(*ppVnodeRoot);
		FFAT_ASSERT(bNew == FFAT_TRUE);

		NS_RegisterVcbOperation(pVcb, (void*)&_opVCB);
		NS_RegisterNativeVcb(pVcb, pVol);

		FFAT_DEBUG_ALVFS_PRINTF((_T("root, inode:0x%016llX\n"), _getInodeNumber(pNodeRoot)));

		dwSectorSize		= FFAT_GetSectorSize(pVol);
		dwClusterSize		= FFAT_GetClusterSize(pVol);

#ifdef FFAT_BLOCK_IO
		dwFirstDataSector	= FFAT_GetFirstDataSector(pVol);

		dwBlockSize = _getOptimalBlockSize(dwBlockSize, dwSectorSize, dwClusterSize, dwFirstDataSector);

		FFAT_ASSERT(dwBlockSize >= dwSectorSize);

		if (dwBlockSize != stLDevInfo.dwBlockSize)
		{
			_setLDevBlockInfo(&stLDevInfo, dwBlockSize);

			// set FAT sector per block
			stLDevInfo.dwFATSectorPerBlock		= stLDevInfo.dwBlockSize / dwSectorSize;
			stLDevInfo.dwFATSectorPerBlockBits	= EssMath_Log2(stLDevInfo.dwFATSectorPerBlock);
			stLDevInfo.dwFATSectorPerBlockMask	= stLDevInfo.dwFATSectorPerBlock - 1;
		}

		err = NS_SetBlockSize(pVcb, dwBlockSize);
#endif

		// set block per cluster
		stLDevInfo.dwBlockPerCluster		= dwClusterSize / dwBlockSize;
		stLDevInfo.dwBlockPerClusterBits	= EssMath_Log2(stLDevInfo.dwBlockPerCluster);
		stLDevInfo.dwBlockPerClusterMask	= stLDevInfo.dwBlockPerCluster - 1;

		FFAT_SetLDevInfo(pVol, &stLDevInfo);
	}

out:
	err = _errnoToNestle(r);

err_nestle:
	IF_UK (err != FERROR_NO_ERROR)
	{
		// Before free, release resource of node
		FFAT_Close(pNodeRoot, FFAT_NODE_CLOSE_RELEASE_RESOURCE);

		_freeNode(pNodeRoot);
		_freeVol(pVol);
	}
	else
	{
		// revert the mount flag to notify the adapted mount flag
		*pMountFlag = _revertFlag(dwFFatFlag, _pMountFlagTable, sizeof(_pMountFlagTable) / sizeof(FlagConvert));
	}

	_MOUNT_OUT

	return err;
}


/**
* format a volume
*
* @param		pLogDisk			: [IN] logical disk pointer
* @param		dwFileSystemType	: [IN] file system type
* @param		dwClusterSize		: [IN] cluster size (sector count per a cluster)
* @param		pFormatParam		: [IN] don't care, i don't know what it is
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_format(	IN	NS_PLOGICAL_DISK	pLogDisk,
			IN	NS_PFORMAT_PARAMETER	pFormatParam,
			IN	NS_PFORMAT_DISK_INFO		pDiskInfo)
{
	FFatErr				r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR				err;				// error variable for nestle
	FFatFormatInfo		stFI;

	_FORMAT_IN

	FFAT_ASSERT(pLogDisk);
	FFAT_ASSERT(pFormatParam);
	FFAT_ASSERT(pDiskInfo);

// debug begin
#ifdef FFAT_DEBUG
	{
		t_uint32		dwDevSectorSize;
		t_uint32		dwDevSectorCount;
		t_uint32		dwDevStartSector;
		t_uint32		dwFlags;

		NS_GetDiskInfo(pLogDisk, &dwDevSectorCount, &dwDevSectorSize, &dwDevStartSector, &dwFlags);

		// logical device의 sector size와 같은 크기의 FAT sector size의 format이어야 함
		FFAT_ASSERT(dwDevSectorSize == pDiskInfo->dwBytesPerSector);
	}
#endif
// debug end

	stFI.psVolumeLabel			= pFormatParam->pwszLabel;
	stFI.pBuff					= NULL;
	stFI.dwBuffSize				= 0;
	stFI.dwSectorsPerCluster	= pFormatParam->dwClusterSize;
	stFI.dwRsvdSector			= 0;

	if (pFormatParam->dwFileSystemType == NS_NFS_TYPE_FAT12)
	{
		stFI.dwFatType = FFAT_FAT12;
	}
	else if (pFormatParam->dwFileSystemType == NS_NFS_TYPE_FAT16)
	{
		stFI.dwFatType = FFAT_FAT16;
	}
	else if (pFormatParam->dwFileSystemType == NS_NFS_TYPE_FAT32)
	{
		stFI.dwFatType = FFAT_FAT32;
	}
	else
	{
		err = FERROR_INVALID;
		goto err_nestle;
	}

	stFI.dwStartSector		= pDiskInfo->dwStartSectorNum;
	stFI.dwSectorCount		= pDiskInfo->dwNumSectors;
	stFI.dwSectorSize		= pDiskInfo->dwBytesPerSector;
	stFI.dwSectorSizeBits	= EssMath_Log2(pDiskInfo->dwBytesPerSector);

	// default 4 sector align
	stFI.wAlignBasis = FFAT_FORMAT_ALIGN;

	stFI.pDevice				= (void*)pLogDisk;

	r = FFAT_FSCtl(FFAT_FSCTL_FORMAT, &stFI, NULL, NULL);

	err = _errnoToNestle(r);

err_nestle:
	_FORMAT_OUT

	return err;
}


/**
* CHKDSK a volume
*
* @param		pVcb				: [IN] VCB pointer
* @param		pInBuf				: [IN] flag for CHKDSK
* @param		dwInBufSize			: [IN] size of flag
* @param		pOutBuf				: [IN] don't care
* @param		dwOutBufSize		: [IN] don't care
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_chkdsk(
			IN	NS_PVCB			pVcb,
			IN	void*			pInBuf,
			IN	unsigned int	dwInBufSize,
			OUT	void*			pOutBuf,
			IN	unsigned int	dwOutBufSize)
{

	// Nestle does not define data structure for CHKDSK

	FFatErr			r;
	FFatChkdskFlag	dwFFatFlag;
	t_int32			dwFlag = *((t_int32*)pInBuf);

	_CHKDSK_IN

	FFAT_ASSERT(pInBuf);
	FFAT_ASSERT(dwInBufSize == sizeof(t_int32));

	dwFFatFlag = _convertFlag(dwFlag, _pChkdskFlagTable, sizeof(_pChkdskFlagTable) / sizeof(FlagConvert));

	r = FFAT_FSCtl(FFAT_FSCTL_CHKDSK, _VOL(pVcb), &dwFFatFlag, NULL);

	_CHKDSK_OUT

	return _errnoToNestle(r);
}


//=============================================================================
//
//	NS_VCB_OPS operations
//


/**
* convert Nestle flag to FFAT flag
*
* @param		pVCB		: [IN] volume control block ptr
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_syncVol(IN NS_PVCB pVcb)
{
	FFatErr		r;
	FFatVol*	pVol;

	_SYNCVOL_IN

	FFAT_ASSERT(pVcb);

	pVol = (FFatVol*)_VOL(pVcb);

	FFAT_ASSERT(FFAT_VolIsReadOnly(pVol) == FFAT_FALSE);

	r = FFAT_SyncVol(pVol);

	_SYNCVOL_OUT

	return _errnoToNestle(r);
}


/**
* get volume information
*
* @param		pVCB		: [IN] volume control block ptr
* @param		pVolumeInfo	: [IN/OUT] volume information storage
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		30-SEP-2008 [DongYoung Seo] add convert routine for HPA
* @version		Aug-29-2009 [SangYoon Oh] Modify the code setting pVolumeInfo
*/
static FERROR 
_getVolumeInfo(	IN	NS_PVCB					pVcb,
				OUT NS_PVOLUME_INFORMATION	pVolumeInfo)
{
	FFatVol*			pVol;
	t_int32				dwSectorSize;

	FFatVolumeStatus	stStatus;
	FFatErr				r;

	_GETVOLINFO_IN

	FFAT_ASSERT(pVcb);
	FFAT_ASSERT(pVolumeInfo);

	pVol = _VOL(pVcb);

	r = FFAT_GetVolumeStatus(pVol, &stStatus, NULL, 0);
	IF_LK (r == FFAT_OK)
	{
		dwSectorSize = FFAT_GetSectorSize(pVol);

		FFAT_ASSERT(stStatus.dwFatType != FFAT_FAT12);

		pVolumeInfo->dwFsName			= NS_NFS_NAME_BTFS;
		if (stStatus.dwFatType == FFAT_FAT32)
		{
			pVolumeInfo->dwFsType		= NS_NFS_TYPE_FAT32;
		}
		else
		{
			pVolumeInfo->dwFsType		= NS_NFS_TYPE_FAT16;
		}
		pVolumeInfo->dwClusterSize		= stStatus.dwClusterSize;
		pVolumeInfo->dwSectorsPerCluster = stStatus.dwClusterSize / dwSectorSize;
		pVolumeInfo->dwBytesPerSector	= (t_uint32)dwSectorSize;
		pVolumeInfo->dwNumClusters		= stStatus.dwClusterCount;
		pVolumeInfo->dwNumFreeClusters	= stStatus.dwFreeClusterCount;
		pVolumeInfo->dwNumAvailClusters	= pVolumeInfo->dwNumFreeClusters;	// = dwNumBlocks - dwNumF
		pVolumeInfo->dwMaxFileNameLen	= FFAT_FILE_NAME_MAX_LENGTH;
		pVolumeInfo->ullMaxFileSize		= stStatus.dwMaxFileSize;
		pVolumeInfo->wszVolumeLabel[0]	= '\0';	// do not fill at here (volume label)

		// update HPA
		pVolumeInfo->stHpa.dwClusterCount		= stStatus.stHPA.dwClusterCount;
		pVolumeInfo->stHpa.dwFreeClusterCount	= stStatus.dwFreeClusterCount;
		pVolumeInfo->stHpa.dwAvailableBlockCount	= stStatus.dwFreeClusterCount;
	}

	_GETVOLINFO_OUT

	return _errnoToNestle(r);
}


/**
* unmount a volume
*
*	un-mount a volume
*	does not remove root node(nestle will call VNODE dispose)
*
*	unregister VFS
*
* @param		pVCB		: [IN] volume control block ptr
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_umount(IN	NS_PVCB		pVcb,
		IN	NS_UNMOUNT_FLAG	dwUnmountFlags)
{
	NS_PVNODE	pVnode;
	FFatNode*	pRootNode = NULL;
	FFatErr		r;

	_UMOUNT_IN

	FFAT_GetRootNodePtr(_VOL(pVcb), &pRootNode);
	FFAT_ASSERT(pRootNode);

	r = FFAT_Umount(_VOL(pVcb), FFAT_MOUNT_NO_FLAG);
	IF_LK (r == FFAT_OK)
	{
		// free memory for volume
		_freeNode(pRootNode);
		_freeVol(_VOL(pVcb));

		pVnode = NS_GetRootFromVcb(pVcb);
		if (pVnode)
		{
			NS_LinkNativeNode(NS_GetRootFromVcb(pVcb), NULL);
		}
	}

	_UMOUNT_OUT

	return _errnoToNestle(r);
}


/**
* remount a volume
*
* unregister VFS
*
* @param		pVCB		: [IN] volume control block ptr
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		23-MAR-2009 [JeongWoo Park] change the mount flag as IO/OUT.
*/
static FERROR
_remount(IN NS_PVCB pVcb, NS_PREMOUNT_FLAG pdwFlag)
{
	NS_REMOUNT_FLAG		dwFlagNotSupport;
	FFatMountFlag		dwFFatFlag;
	FFatErr				r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR				err;				// error variable for nestle

	_REMOUNT_IN

	FFAT_ASSERT(pVcb);

	// set not supported type
	dwFlagNotSupport = NS_REMOUNT_LOG_INIT | NS_REMOUNT_CLEAN_NAND |
						NS_REMOUNT_FILE_GUID | NS_REMOUNT_FILE_XATTR |
						NS_REMOUNT_FILE_SPECIAL;

	// check flags
	if (*pdwFlag & dwFlagNotSupport)
	{
		err = FERROR_NOT_SUPPORTED;
		goto err_nestle;
	}

	dwFFatFlag = _convertFlag(*pdwFlag, _pRemountFlagTable,
						sizeof(_pRemountFlagTable) / sizeof(FlagConvert));

	r = FFAT_Remount(_VOL(pVcb), &dwFFatFlag);

	err = _errnoToNestle(r);

	IF_LK (err == FERROR_NO_ERROR)
	{
		// revert the mount flag to notify the adapted mount flag
		*pdwFlag = _revertFlag(dwFFatFlag, _pRemountFlagTable,
						sizeof(_pRemountFlagTable) / sizeof(FlagConvert));
	}

err_nestle:
	_REMOUNT_OUT

	return err;
}


/**
* IO control
*
* @param		pVCB			: [IN] volume control block ptr
* @param		dwControlCode	: [IN] I/O control command
* @param		pInBuf			: [IN] in buffer ptr
* @param		dwInBufSize		: [IN] in buffer size
* @param		pOutBuf			: [IN] out buffer ptr
* @param		dwOutBufSize	: [IN] size of out buffer
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		29-DEC-2008 [DongYoung Seo] separate fsctl command from _ioctl()
* @version		10-FEB-2009 [DongYoung Seo] move fsctl commands to _fsctl()
* @version		Aug-29-2009 [SangYoon Oh] Modify the code READ_SECTOR and WRITE_SECTOR to call FFAT_FSCtl
* @version		OCT-26-2009 [JW Park] Remove t_ldev_ioctl that is defined at rtos_glue layer.
*/
static FERROR 
_ioctl(	IN	NS_PVCB				pVcb,
		IN	unsigned int		dwControlCode,
		IN	void*				pInBuf,
		IN	unsigned int		dwInBufSize,
		OUT	void*				pOutBuf,
		OUT	unsigned int		dwOutBufSize)
{
	NS_PFAT_CLEAN_PARAMETER		pFCP;			// Fat Clean Param
	FFatCleanNand			stCN;			// FFAT Clean NAND structure
	FFatLDevIO				stLDevIO;
	NS_PDISK_IO_REQUEST		pLDevIO;
	FFatErr					r;

	_IOCTL_IN

	// refer to the NS_IOCTL_CODE

	// structure not defined for each command

	switch(dwControlCode)
	{
		case NS_IOCTL_NATIVE_READ_SECTOR:
				FFAT_ASSERT(pInBuf);
				pLDevIO = (NS_PDISK_IO_REQUEST)pInBuf;
				stLDevIO.dwFlag = FFAT_IO_READ_SECTOR;
				stLDevIO.dwSectorNo = pLDevIO->dwSectorNum;
				stLDevIO.dwCount = pLDevIO->dwNumSectors;
				stLDevIO.pBuff = (t_int8*)pLDevIO->pBuf;
				r = FFAT_FSCtl(FFAT_FSCTL_LDEV_IO, _VOL(pVcb), &stLDevIO, NULL);
				break;

		case NS_IOCTL_NATIVE_WRITE_SECTOR:
				FFAT_ASSERT(pInBuf);
				pLDevIO = (NS_PDISK_IO_REQUEST)pInBuf;
				stLDevIO.dwFlag = FFAT_IO_WRITE_SECTOR;
				stLDevIO.dwSectorNo = pLDevIO->dwSectorNum;
				stLDevIO.dwCount = pLDevIO->dwNumSectors;
				stLDevIO.pBuff = (t_int8*)pLDevIO->pBuf;
				r = FFAT_FSCtl(FFAT_FSCTL_LDEV_IO, _VOL(pVcb), &stLDevIO, NULL);
				break;

		case NS_IOCTL_NATIVE_FAT_CLEAN:
				// I don't know what the parameter is 
				FFAT_ASSERT(pInBuf);
				pFCP = (NS_PFAT_CLEAN_PARAMETER)pInBuf;

				stCN.dwStartCluster	= pFCP->dwStartCluster;
				stCN.dwClusterCount	= pFCP->dwClusterCount;

				r = FFAT_FSCtl(FFAT_FSCTL_CLEAN_NAND, _VOL(pVcb), &stCN, NULL);
				break;

		default:
				r = FFAT_ENOSUPPORT;
// debug begin
#ifdef FFAT_DEBUG
				r = FFAT_FSCtl(FFAT_FSCTL_DEBUG_BASE, _VOL(pVcb), pInBuf, NULL);
#endif
// debug end
				break;
	}

	_IOCTL_OUT

	return _errnoToNestle(r);
}


/**
* FileSystem control
*
* @param		dwControlCode	: [IN] FS control command
* @param		pInBuf			: [IN] in buffer ptr
* @param		dwInBufSize		: [IN] in buffer size
* @param		pOutBuf			: [IN] out buffer ptr
* @param		dwOutBufSize	: [IN] size of out buffer
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		29-DEC-2008 [DongYoung Seo] separate fsctl command from _ioctl()
* @version		01-FEB-2009 [DongYoung Seo] move fsctl commands from _ioctl()
*/
static FERROR 
_fsctl(	IN	unsigned int		dwControlCode,
		IN	void*				pInBuf,
		IN	unsigned int		dwInBufSize,
		OUT	void*				pOutBuf,
		OUT	unsigned int		dwOutBufSize)
{
	FFatAddCache			stAddCache;		// structure FFAT Add Cache
	FFatCheckCache			stCheckCache;	// structure FFAT Check Cache
	FFatErr					r;
	FERROR					nerr;			// error storage for nestle

	_FSCTL_IN

	// refer to the NS_FSCTL_CODE

	// structure not defined for each command

	switch(dwControlCode)
	{
		case NS_FSCTL_NATIVE_GET_VOL_NAME:
				// pInBuf : a pointer of NS_PFS_VOLUME_NAME
				// ((NS_PFS_VOLUME_NAME)(pInBuf))->pVcb			: pVcb
				// (NS_PFS_VOLUME_NAME)pOutBuf)->wszVolumeName	: volume label storage
				// (NS_PFS_VOLUME_NAME)pOutBuf)->wszVolumeName	: size of volume name storage

				FFAT_ASSERT(pInBuf);
				FFAT_ASSERT(pOutBuf);
				r = FFAT_GetVolumeLabel(_VOL(((NS_PFS_VOLUME_NAME)(pInBuf))->pVcb),
								((NS_PFS_VOLUME_NAME)pOutBuf)->wszVolumeName,
								((NS_PFS_VOLUME_NAME)pInBuf)->dwLen);
				IF_UK (r < 0)
				{
					goto out;
				}

				((NS_PFS_VOLUME_NAME)pOutBuf)->dwLen
							= FFAT_WCSLEN(((NS_PFS_VOLUME_NAME)pOutBuf)->wszVolumeName);
				break;

		case NS_FSCTL_NATIVE_SET_VOL_NAME:
				// pInBuf : a pointer of NS_PFS_VOLUME_NAME
				// ((NS_PFS_VOLUME_NAME)(pInBuf))->pVcb			: pVcb
				// (NS_PFS_VOLUME_NAME)pOutBuf)->wszVolumeName	: volume label

				r = FFAT_SetVolumeLabel(_VOL(((NS_PFS_VOLUME_NAME)(pInBuf))->pVcb),
						((NS_PFS_VOLUME_NAME)pInBuf)->wszVolumeName);
				break;

// debug begin
#ifdef FFAT_DEBUG
				// format
		case NS_FSCTL_NATIVE_TEST_INVALIDATE_FCCH:
				// invalidate free cluster count hint
				// pInBuf	: pointer of pVCB
				FFAT_ASSERT(pInBuf);
				r = FFAT_FSCtl(FFAT_FSCTL_INVALIDATE_FCCH, _VOL((NS_PVCB)pInBuf), NULL, NULL);
				break;
#endif
// debug end

		case NS_FSCTL_NATIVE_SET_DEC:
		case NS_FSCTL_NATIVE_RELEASE_DEC:
		case NS_FSCTL_NATIVE_GET_DEC_INFO:
				FFAT_ASSERT(pInBuf);
				r = _fsctlDEC(dwControlCode, (NS_PFS_DE_CACHE)pInBuf);
				break;

		case NS_FSCTL_NATIVE_CHECK_BOOT_SECTOR:
				r = FFAT_FSCtl(FFAT_FSCTL_IS_VALID_BOOTSECTOR, pInBuf, NULL, NULL);
				break;

		case NS_FSCTL_NATIVE_ADD_CACHE:
				FFAT_ASSERT(pInBuf);
				stAddCache.pBuff		= (t_int8*)((NS_PFS_NATIVE_CACHE)pInBuf)->pBuff;
				stAddCache.dwSize		= ((NS_PFS_NATIVE_CACHE)pInBuf)->dwSize;
				stAddCache.dwSectorSize = ((NS_PFS_NATIVE_CACHE)pInBuf)->dwSectorSize;

				r = FFAT_FSCtl(FFAT_FSCTL_ADD_CACHE, &stAddCache, NULL, NULL);
				break;

		case NS_FSCTL_NATIVE_REMOVE_CACHE:
				stCheckCache.dwSectorSize	= ((NS_PFS_NATIVE_CACHE)pInBuf)->dwSectorSize;
				r = FFAT_FSCtl(FFAT_FSCTL_REMOVE_CACHE, pInBuf, NULL, NULL);
				break;

		case NS_FSCTL_NATIVE_CHECK_CACHE:
				// refer to mail from InHwan Choi, on 05-JAN-2009
				// check that is of cache is exist or not.
				//	when exist : return FERROR_NO_ERROR with fill pointer of buffer (use pInBuf)
				//	when not exist : return no more entry

				FFAT_ASSERT(pInBuf);

				stCheckCache.dwSectorSize = ((PFS_NATIVE_CACHE)pInBuf)->dwSectorSize;
				r = FFAT_FSCtl(FFAT_FSCTL_CHK_CACHE, &stCheckCache, NULL, NULL);
				if (r == FFAT_OK1)
				{
					// we got it !!
					FFAT_ASSERT(stCheckCache.pBuff);
					((PFS_NATIVE_CACHE)pInBuf)->pBuff = (char*)stCheckCache.pBuff;
					r = FFAT_OK;
				}
				else
				{
					r = FFAT_ENOENT;
				}
				break;

		default:
				r = FFAT_ENOSUPPORT;
				break;
	}

out:
	nerr = _errnoToNestle(r);

	_FSCTL_OUT

	return	nerr;
}


//=============================================================================
//
//	NS_VNODE_OPS operations
//

/**
 * Open a node
 *
 * @param		pFcb		: FCB pointer
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing.
 */
static FERROR
_open(IN	NS_PFCB	pFcb)
{
	FFatErr		r;
	NS_PVNODE	pVnode;

	_OPEN_IN

	FFAT_ASSERT(pFcb);

	pVnode = NS_GetVnodeFromFcb(pFcb);

	FFAT_DEBUG_ALVFS_PRINTF((_T("FCB:0x%X, VNODE (PTR/ID)/FFatNode:(0x%X/0x%016llX)/0x%X\n"),
						(t_uint32)pFcb, (t_uint32)pVnode, NS_GetVnodeIndex(pVnode), (t_uint32)_NODE(pVnode)));

	r = FFAT_Open(_NODE(pVnode), pVnode);

	_OPEN_OUT

	return _errnoToNestle(r);
}


/**
 * lookup a node
 *
 * caution!!
 *
 *	LOOKUP_FOR_CREATE
 *		_lookup() does not release node structure even if the node does not exist.
 *		FFAT attach some information to accelerate creating.
 *		Consequently.. VFS must call create after looking up with LOOKUP_FOR_CREATE
 *			==> This is the rule of Nestle. !!
 *
 * @param		pnParent		: parent node pointer
 * @param		pwsName			: name
 * @param		dwNameLen		: length of name
 * @param		dwLookupFlag	: flag for lookup
 * @param		ppVnode			: VNODE storage
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_IO_ERROR				: I/O error
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing
 */
static FERROR
_lookup(		IN	NS_PVNODE			pnParent,
				IN	const wchar_t*		pwsName,
				IN	unsigned int		dwNameLen,
				IN	NS_LOOKUP_FLAG		dwLookupFlag,
				OUT NS_PVNODE*			ppVnode)
{
	FFatErr				r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR				err;				// error variable for nestle
	FFatLookupFlag		dwFFatLookupFlag = 0;
	t_wchar				psName[FFAT_NAME_MAX_LENGTH + 1];
	FFatNode*			pNode;
	NS_ACL_MODE			dwACLMode;
	BOOL				bNew;

	FFatExtendedDirEntryInfo	stXDEInfo;

	unsigned int		dwUID;
	unsigned int		dwGID;

	_LOOKUP_IN

	FFAT_ASSERT(pnParent);
	FFAT_ASSERT(ppVnode);
	FFAT_ASSERT(*ppVnode == NULL);
	FFAT_ASSERT(pwsName);
	FFAT_ASSERT(dwNameLen > 0);
	FFAT_ASSERT(dwNameLen <= FFAT_NAME_MAX_LENGTH);

	// get lookup flag
	dwFFatLookupFlag = _convertFlag(dwLookupFlag, _pLookupFlagTable, sizeof(_pLookupFlagTable) / sizeof(FlagConvert));

	if (dwFFatLookupFlag & (FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_FOR_RENAME))
	{
		// nestle의 요청에 의한 lookup for create시 FFAT_LOOKUP_SET_CHILD를 설정하여
		// ffat_node_lookup()에서 parent node에 child node pointer를 설정하도록 한다
		dwFFatLookupFlag |= FFAT_LOOKUP_SET_CHILD;

		FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnParent))) == FFAT_FALSE);
	}

	pNode = _allocNode();
	IF_UK (pNode == NULL)
	{
		FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatNode")));
		err = FERROR_INSUFFICIENT_MEMORY;
		goto err_nestle;
	}

	FFAT_WCSNCPY(psName, pwsName, dwNameLen);
	psName[dwNameLen] = ((t_wchar)'\0');

	r = FFAT_Lookup(_NODE(pnParent), pNode, psName, dwFFatLookupFlag, &stXDEInfo);
	if (r == FFAT_OK)
	{
		dwUID		= stXDEInfo.dwUID;
		dwGID		= stXDEInfo.dwGID;
		dwACLMode	= (NS_ACL_MODE)stXDEInfo.dwPerm;

		FFAT_ASSERT(*ppVnode == NULL);

		r = _setupVnode(NS_GetVcbFromVnode(pnParent), pnParent, pNode,
							dwACLMode, dwUID, dwGID, ppVnode, &bNew);
		if ((r < 0) || (bNew == FFAT_FALSE))
		{
			// Before free, release resource of node
			FFAT_Close(pNode, FFAT_NODE_CLOSE_RELEASE_RESOURCE);

			// there is already an instance
			// free allocated memory
			_freeNode(pNode);
			pNode = NULL;
		}
		else if (FFAT_NodeIsDirtySize(pNode) == FFAT_TRUE)
		{
			// If new node is dirty-size, recovery about dirty-size state.
			if (FFAT_VolIsReadOnly(FFAT_GetVol(pNode)) == FFAT_FALSE)
			{
				r = _recoveryDirtySizeNode(*ppVnode);
				if (r != FFAT_OK)
					goto err_recovery;
			}
		}
	}

	if (r != FFAT_OK)
	{
		// error return

		if ((dwFFatLookupFlag & FFAT_LOOKUP_SET_CHILD) && (r == FFAT_ENOENT))
		{
			// do not release Node
			FFAT_ASSERT(FFAT_GetChildPtr(_NODE(pnParent), FFAT_FALSE) != NULL);
		}
		else
		{
			// lookup flag is not 'lookup for create',
			_freeNode(pNode);
			FFAT_ASSERT(FFAT_GetChildPtr(_NODE(pnParent), FFAT_FALSE) == NULL);
		}
	}
	else
	{
		FFAT_DEBUG_ALVFS_PRINTF((_T("Name:%s, VNODE (PRT/ID)/FFatNode:(0x%X/0x%016llX)/0x%X\n"),
						_w2a(psName, dwNameLen), (t_uint32)*ppVnode, NS_GetVnodeIndex(*ppVnode), (t_uint32)_NODE(*ppVnode)));
		if (dwFFatLookupFlag & FFAT_LOOKUP_SET_CHILD)
		{
			FFAT_ASSERT(FFAT_GetChildPtr(_NODE(pnParent), FFAT_FALSE) == NULL);
			FFAT_GetChildPtr(_NODE(pnParent), FFAT_TRUE);		// TO initialize child pointer
		}
	}

err_recovery:

	err = _errnoToNestle(r);

err_nestle:
	_LOOKUP_OUT

	return err;
}


/**
* make a directory
*
* @param		pnParent		: parent VNODE
* @param		pwsName			: new VNODE name
* @param		dwNameLen		: length of name
* @param		dwUID			: UID, don't care
* @param		dwGID			: GID, don't care
* @param		dwAccessMode	: access mode
* @param		ppVnode			: New VNODE
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_mkdir(		IN	NS_PVNODE				pnParent,
			IN	const wchar_t*			pwsName,
			IN	unsigned int			dwNameLen,
			IN	NS_FILE_ATTR			dwAttr,
			IN	unsigned int			dwUID,
			IN	unsigned int			dwGID,
			IN	NS_ACL_MODE				wPerm,
			OUT NS_PVNODE*				ppVnode)
{
	FFatErr				r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR				err;				// error variable for nestle
	t_wchar		psName[FFAT_NAME_MAX_LENGTH + 1];
	FFatNode*	pNode = NULL;
	BOOL		bNew;
	FFatCreateFlag	dwFlag;
	FFatExtendedDirEntryInfo	stXDEInfo;

	_MKDIR_IN

	FFAT_ASSERT(pnParent);
	FFAT_ASSERT(pwsName);
	FFAT_ASSERT(dwNameLen > 0);
	FFAT_ASSERT(dwNameLen <= FFAT_DIR_NAME_MAX_LENGTH);
	FFAT_ASSERT(ppVnode);
	FFAT_ASSERT(*ppVnode == NULL);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnParent))) == FFAT_FALSE);
	// file attr에 read only가 있으면, mode에 어떤 write로 있지 않아야 함
	FFAT_ASSERT((dwAttr & NS_FILE_ATTR_READONLY) ? ((wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) == 0) : FFAT_TRUE);
	// file attr에 read only가 없으면, mode에 write가 하나라로 있어야 함
	FFAT_ASSERT(((dwAttr & NS_FILE_ATTR_READONLY) == 0) ? (wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) : FFAT_TRUE);

	pNode = FFAT_GetChildPtr(_NODE(pnParent), FFAT_TRUE);

	FFAT_WCSNCPY(psName, pwsName, dwNameLen);
	psName[dwNameLen] = ((t_wchar)'\0');

	dwFlag = (dwAttr & FFAT_ATTR_MASK);

	if (pNode == NULL)
	{
		pNode = _allocNode();
		IF_UK (pNode == NULL)
		{
			FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatNode")));
			err = FERROR_INSUFFICIENT_MEMORY;
			goto err_nestle;
		}

		dwFlag |= FFAT_CREATE_LOOKUP;
	}

	stXDEInfo.dwUID		= dwUID;
	stXDEInfo.dwGID		= dwGID;
	stXDEInfo.dwPerm	= wPerm;

	// check it - there is no creation mode. there is a access mode ?
	// Nestle discard information for before lookup
	// It is more useful nestle does not lookup child node
	r = FFAT_Makedir(_NODE(pnParent), pNode, psName, dwFlag, (void*)&stXDEInfo);
	IF_LK (r == FFAT_OK)
	{
		FFAT_ASSERT(pNode);
		FFAT_ASSERT(*ppVnode == NULL);

		r = _setupVnode(NS_GetVcbFromVnode(pnParent), pnParent, pNode,
						wPerm, dwUID, dwGID, ppVnode, &bNew);

		FFAT_DEBUG_ALVFS_PRINTF((_T("Name:%s, Parent(VNODE/FFatNode)/(New VNODE(PTR/ID)/FFatNode):(0x%X/0x%X)/(0x%X/0x%016llX)/0x%X\n"),
			_w2a(psName, dwNameLen), (t_uint32)pnParent, (t_uint32)_NODE(pnParent), (t_uint32)*ppVnode, NS_GetVnodeIndex(*ppVnode), (t_uint32)pNode));

		IF_LK (r == FFAT_OK)
		{
			FFAT_ASSERT(bNew == FFAT_TRUE);
			NS_MarkVnodeMetaDirty(*ppVnode);
			NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnParent));
		}
		else
		{
			FFAT_ASSERT(r == FFAT_ENOMEM);

			FFAT_Unlink(_NODE(pnParent), pNode, FFAT_FALSE);
			FFAT_Close(pNode, FFAT_NODE_CLOSE_RELEASE_RESOURCE);
		}
	}

	err = _errnoToNestle(r);

err_nestle:
	IF_UK (err != FERROR_NO_ERROR)
	{
		// free allocated memory
		_freeNode(pNode);
	}

	_MKDIR_OUT

	return err;
}


/**
* create a file
*
* @param		pnParent		: [IN] parent VNODE
* @param		pwsName			: [IN] new VNODE name
* @param		dwNameLen		: [IN] length of name
* @param		dwAttribute		: [IN] file attribute, refer to FILE_ATTR, this is co-related to dwAccessMode
* @param		dwUID			: [IN] UID, don't care
* @param		dwGID			: [IN] GID, don't care
* @param		dwAccessMode	: [IN] access mode, this is co-related to dwAttribute
* @param		ppVnode			: [IN] New VNODE
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @return		
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		JUN-17-2009 [GwangOk Go] add fifo, socket
*/
static FERROR
_create(	IN	NS_PVNODE				pnParent,
			IN	const wchar_t*			pwsName,
			IN	unsigned int			dwNameLen,
			IN	NS_FILE_ATTR			dwAttributes,
			IN	unsigned int			dwUID,
			IN	unsigned int			dwGID,
			IN	NS_ACL_MODE				wPerm,
			OUT NS_PVNODE*				ppVnode)
{
	FFatErr						r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR						err;				// error variable for nestle
	t_wchar						psName[FFAT_FILE_NAME_MAX_LENGTH + 1];	// storage name buffer
	FFatNode*					pNode;			// FFAT Node pointer
	BOOL						bNew;			// to check new creation or not
	FFatCreateFlag				dwFlag;			// flag for file creation
	FFatExtendedDirEntryInfo	stXDEInfo;		// some attributes for extended directory entry

	_CREATE_IN

	FFAT_ASSERT(pnParent);
	FFAT_ASSERT(pwsName);
	FFAT_ASSERT(dwNameLen > 0);
	FFAT_ASSERT(dwNameLen <= FFAT_FILE_NAME_MAX_LENGTH);
	FFAT_ASSERT(ppVnode);
	FFAT_ASSERT(*ppVnode == NULL);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnParent))) == FFAT_FALSE);
	// file attr에 read only가 있으면, mode에 어떤 write로 있지 않아야 함
	FFAT_ASSERT((dwAttributes & NS_FILE_ATTR_READONLY) ? ((wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) == 0) : FFAT_TRUE);
	// file attr에 read only가 없으면, mode에 write가 하나라로 있어야 함
	FFAT_ASSERT(((dwAttributes & NS_FILE_ATTR_READONLY) == 0) ? (wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) : FFAT_TRUE);
	FFAT_ASSERT((dwAttributes & NS_FILE_ATTR_DIRECTORY) == 0);

	pNode = FFAT_GetChildPtr(_NODE(pnParent), FFAT_TRUE);

	FFAT_WCSNCPY(psName, pwsName, dwNameLen);
	psName[dwNameLen] = ((t_wchar)'\0');

	dwFlag = (dwAttributes & FFAT_ATTR_MASK);

	if (dwAttributes & NS_FILE_ATTR_FIFO)
	{
		FFAT_ASSERT((dwAttributes & NS_FILE_ATTR_SOCKET) == 0);

		dwFlag |= FFAT_CREATE_FIFO;
	}
	else if (dwAttributes & NS_FILE_ATTR_SOCKET)
	{
		FFAT_ASSERT((dwAttributes & NS_FILE_ATTR_FIFO) == 0);

		dwFlag |= FFAT_CREATE_SOCKET;
	}

	if (pNode == NULL)
	{
		pNode = _allocNode();
		IF_UK (pNode == NULL)
		{
			FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatNode")));
			err = FERROR_INSUFFICIENT_MEMORY;
			goto err_nestle;
		}

		dwFlag |= FFAT_CREATE_LOOKUP;
	}

	stXDEInfo.dwUID		= dwUID;
	stXDEInfo.dwGID		= dwGID;
	stXDEInfo.dwPerm	= wPerm;

	// check it - there is no creation mode. there is a access mode ?
	//	kkaka. 20081025, what's the meaning of upper comment ?
	r = FFAT_Create(_NODE(pnParent), pNode, psName, dwFlag, (void*)&stXDEInfo);
	IF_LK (r == FFAT_OK)
	{
		FFAT_ASSERT(pNode);
		FFAT_ASSERT(*ppVnode == NULL);

		r = _setupVnode(NS_GetVcbFromVnode(pnParent), pnParent, pNode,
							wPerm, dwUID, dwGID, ppVnode, &bNew);

		FFAT_DEBUG_ALVFS_PRINTF((_T("a Node Created, Name:%s, VNODE ID:0x%016llX, VNODE(Parent/Child)/FFatNode(Parent/Child):(0x%X/0x%X)/(0x%X/0x%X)\n"),
			_w2a(psName, dwNameLen), _getInodeNumber(pNode), (t_uint32)pnParent, (t_uint32)*ppVnode, (t_uint32)_NODE(pnParent), (t_uint32)pNode));

		IF_LK (r == FFAT_OK)
		{
			FFAT_ASSERT(NS_GetVnodeIndex(*ppVnode) == _getInodeNumber(pNode));
			FFAT_ASSERT(bNew == FFAT_TRUE);		// check node is exist - never be exist
			NS_MarkVnodeMetaDirty(*ppVnode);
			NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnParent));
		}
		else
		{
			FFAT_ASSERT(r == FFAT_ENOMEM);

			FFAT_Unlink(_NODE(pnParent), pNode, FFAT_FALSE);
			FFAT_Close(pNode, FFAT_NODE_CLOSE_RELEASE_RESOURCE);
		}
	}

	err = _errnoToNestle(r);

err_nestle:
	IF_UK (err != FERROR_NO_ERROR)
	{
		// free allocated memory
		_freeNode(pNode);
	}

	_CREATE_OUT

	return err;
}


/**
 * create a symbolic link
 *
 * @param		pnSourceParent		: parent of source VNODE
 * @param		pnTargetParent		: parent of target VNODE
 * @param		pwsName				: name of source
 * @param		dwNameLen			: length of source VNODE
 * @param		pdwUid			: don't care
 * @param		pdwGid			: don't care
 * @param		pdwPerm			: don't care
 * @param		pwsTargetPath		: target VNODE path
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_IO_ERROR				: I/O error
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing.
 * @version		DEC-05-2007 [GwangOk Go] Implementation.
 */
static FERROR
_createSymlink(	IN	NS_PVNODE			pnParent,
				IN	const wchar_t*		pwsName,
				IN	unsigned int		dwNameLen,
				IN	NS_FILE_ATTR		dwAttr,
				IN	unsigned int		dwUID,
				IN	unsigned int		dwGID,
				IN	NS_ACL_MODE			wPerm,
				IN	const wchar_t*		pwsTargetPath,
				OUT	NS_PVNODE*			ppVnode)
{
	FFatErr						r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR						err;				// error variable for nestle
	t_wchar						psName[FFAT_FILE_NAME_MAX_LENGTH + 1];
	FFatNode*					pNode = NULL;
	BOOL						bNew;
	FFatCreateFlag				dwFlag;
	FFatExtendedDirEntryInfo	stXDEInfo;

	_CREATESYMLINK_IN

	FFAT_ASSERT(pnParent);
	FFAT_ASSERT(pwsName);
	FFAT_ASSERT(dwNameLen > 0);
	FFAT_ASSERT(dwNameLen <= FFAT_FILE_NAME_MAX_LENGTH);
	FFAT_ASSERT(ppVnode);
	FFAT_ASSERT(*ppVnode == NULL);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnParent))) == FFAT_FALSE);
	// file attr에 read only가 있으면, mode에 어떤 write로 있지 않아야 함
	FFAT_ASSERT((dwAttr & NS_FILE_ATTR_READONLY) ? ((wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) == 0) : FFAT_TRUE);
	// file attr에 read only가 없으면, mode에 write가 하나라로 있어야 함
	FFAT_ASSERT(((dwAttr & NS_FILE_ATTR_READONLY) == 0) ? (wPerm & (NS_OTHERS_WRITE | NS_GROUP_WRITE | NS_OWNER_WRITE)) : FFAT_TRUE);

	FFAT_WCSNCPY(psName, pwsName, dwNameLen);
	psName[dwNameLen] = ((t_wchar)'\0');

	dwFlag = (dwAttr & FFAT_ATTR_MASK);

	pNode = FFAT_GetChildPtr(_NODE(pnParent), FFAT_TRUE);

	if (pNode == NULL)
	{
		pNode = _allocNode();
		IF_UK (pNode == NULL)
		{
			FFAT_DEBUG_PRINTF((_T("Not enough memory for FFatNode")));
			err = FERROR_INSUFFICIENT_MEMORY;
			goto err_nestle;
		}

		dwFlag |= FFAT_CREATE_LOOKUP;
	}

	stXDEInfo.dwUID		= dwUID;
	stXDEInfo.dwGID		= dwGID;
	stXDEInfo.dwPerm	= wPerm;

	r = FFAT_CreateSymlink(_NODE(pnParent), pNode, psName, (t_wchar*)pwsTargetPath, dwFlag,
							(void*)&stXDEInfo);
	IF_LK (r == FFAT_OK)
	{
		FFAT_ASSERT(pNode);
		FFAT_ASSERT(*ppVnode == NULL);

		r = _setupVnode(NS_GetVcbFromVnode(pnParent), pnParent, pNode,
						wPerm, dwUID, dwGID, ppVnode, &bNew);

		FFAT_DEBUG_ALVFS_PRINTF((_T("a Symlink Node Created, Name:%s, VNODE ID:0x%016llX, VNODE(Parent/Child)/FFatNode(Parent/Child):(0x%X/0x%X)/(0x%X/0x%X)\n"),
			_w2a(psName, dwNameLen), _getInodeNumber(pNode), (t_uint32)pnParent, (t_uint32)*ppVnode, (t_uint32)_NODE(pnParent), (t_uint32)pNode));

		IF_LK (r == FFAT_OK)
		{
			FFAT_ASSERT(NS_GetVnodeIndex(*ppVnode) == _getInodeNumber(_NODE(*ppVnode)));
			FFAT_ASSERT(bNew == TRUE);
			NS_MarkVnodeMetaDirty(*ppVnode);
			NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnParent));
		}
		else
		{
			FFAT_ASSERT(r == FFAT_ENOMEM);

			FFAT_Unlink(_NODE(pnParent), pNode, FFAT_FALSE);
			FFAT_Close(pNode, FFAT_NODE_CLOSE_RELEASE_RESOURCE);
		}
	}

	err = _errnoToNestle(r);

err_nestle:
	IF_UK (err != FERROR_NO_ERROR)
	{
		// free allocated memory
		_freeNode(pNode);
	}

	_CREATESYMLINK_OUT

	return err;
}


/**
 * read a symbolic link file and return target
 *
 * @param		pVnode						: [IN] VNODE
 * @param		pwsPath						: [IN/OUT] target path storage
 * @param		dwLinkBuffSize				: [INT] sizeof pdwPath in byte
 * @param		pLinkLen					: [IN/OUT] length of pdwPath in character count
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_IO_ERROR				: I/O error
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory, pwsPath is not enough to store symlink path
 * @return		FERROR_INVALID				: Invalid parameter, not symlink
 * @return		FERROR
 * @author		DongYoung Seo
 * @version		16-OCT-2007 [DongYoung Seo] First Writing.
 * @version		DEC-05-2007 [GwangOk Go] Implementation.
 * @version		MAR-26-2009 [DongYoung Seo] Add two parameter, dwLinkBuffSize, pLinkLen
 */
static FERROR
_readSymlink(	IN	NS_PVNODE		pVnode,
				OUT	wchar_t*		pwsPath,
				IN	unsigned int	dwLinkBuffSize,
				OUT unsigned int*	pLinkLen)
{
	FFatErr		r;

	_READSYMLINK_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(pwsPath);
	FFAT_ASSERT(dwLinkBuffSize > 0);
	FFAT_ASSERT(pLinkLen);

	// add length parameter ?
	r = FFAT_ReadSymlink(_NODE(pVnode), (t_wchar*)pwsPath, (dwLinkBuffSize / sizeof(wchar_t)), (t_int32*)pLinkLen);

	_READSYMLINK_OUT

	return _errnoToNestle(r);
}


/**
* unlink a VNODE (file or directory)
*
* @param		pnParent		: parent of VNODE
* @param		pnTarget		: target VNODE
* @param		bIsOpened		: whether open or not
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		NOV-12-2008 [DongYoung Seo] support open unlink for directory
* @version		JAN-30-2009 [JeongWoo Park] support open unlink for directory
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
*/
static FERROR
_ffat_unlink(	IN	NS_PVNODE			pnParent,
				IN	NS_PVNODE			pnTarget,
				IN	BOOL				bIsOpened)
{
	FFatErr		r;

	_UNLINK_IN

	FFAT_ASSERT(pnParent);
	FFAT_ASSERT(pnTarget);
	FFAT_ASSERT(_NODE(pnParent));
	FFAT_ASSERT(NS_GetVnodeIndex(pnTarget) == _getInodeNumber(_NODE(pnTarget)));
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnTarget))) == FFAT_FALSE);

	if (FFAT_NodeIsDir(_NODE(pnTarget)) == FFAT_TRUE)
	{
		FFAT_DEBUG_ALVFS_PRINTF((_T("unlink VNODE(Dir):0x%016llX\n"), NS_GetVnodeIndex(pnTarget)));
		r = FFAT_Removedir(_NODE(pnParent), _NODE(pnTarget), bIsOpened);
	}
	else
	{
		// Before real unlink, Do recovery operation about dirty-size-rdonly node.
		IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pnTarget)) == FFAT_TRUE)
		{
			r = _recoveryDirtySizeNode(pnTarget);
			IF_UK (r < 0)
			{
				goto out;
			}
		}

		FFAT_DEBUG_ALVFS_PRINTF((_T("unlink VNODE(File) ID:0x%016llX\n"), NS_GetVnodeIndex(pnTarget)));
		r = FFAT_Unlink(_NODE(pnParent), _NODE(pnTarget), bIsOpened);
	}

	IF_LK (r == FFAT_OK)
	{
		// change INODE number
		if (bIsOpened == FFAT_TRUE)
		{
			FFAT_DEBUG_ALVFS_PRINTF((_T("change VNODE index for open unlink - from:0x%016llX\n"), NS_GetVnodeIndex(pnTarget)));

			// do not remove this for open unlink
			NS_ChangeVnodeIndex(pnTarget, _getNewIDForOpenUnlink());

			FFAT_DEBUG_ALVFS_PRINTF((_T("change VNODE index for open unlink - to :0x%016llX\n"), NS_GetVnodeIndex(pnTarget)));
		}

		FFAT_ASSERT(NS_GetVnodeLinkCnt(pnTarget) > 0);
		NS_SetVnodeLinkCnt(pnTarget, (NS_GetVnodeLinkCnt(pnTarget) - 1));

		NS_MarkVnodeMetaDirty(pnTarget);
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnTarget));
	}

out:
	_UNLINK_OUT

	return _errnoToNestle(r);
}


/**
 * delete node after open unlink
 *
 * @param		pnTarget					: VNODE pointer
 * @return		FERROR_NO_ERROR				: success
 * @return		FERROR_IO_ERROR				: I/O error
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @author		InHwan Choi
 * @version		DEC-10-2007 [InHwan Choi] First Writing.
 * @version		NOV-11-2008 [DongYoung Seo] Add ASSERT to check open unlink for directory
 * @version		JAN-30-2009 [JeongWoo Park] support the open unlink for directory
 */
static FERROR
_deleteNode(IN	NS_PVNODE pnTarget)
{
	FFatErr			r = FFAT_OK;				// error variable for BTFS
	FERROR			err = FERROR_NO_ERROR;		// error variable for nestle

	_DELETENODE_IN

	FFAT_ASSERT(pnTarget);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnTarget))) == FFAT_FALSE);

	IF_UK (NS_GetNativeNode(pnTarget) == NULL)
	{
		// If pnTarget is not open unlink node, nothing to do return.
		goto out;
	}

	FFAT_DEBUG_ALVFS_PRINTF((_T("Delete VNODE(PTR/ID),FFatNode:(0x%X/0x%016llX)/0x%X\n"),
		(t_uint32)pnTarget, NS_GetVnodeIndex(pnTarget), (t_uint32)_NODE(pnTarget)));
	
	// pnTarget is open unlink node
	if (FFAT_NodeIsDir(_NODE(pnTarget)) == FFAT_TRUE)
	{
		r = FFAT_Removedir(NULL, _NODE(pnTarget), FFAT_FALSE);
	}
	else
	{
		FFAT_ASSERTP((FFAT_NodeIsDirtySizeRDOnly(_NODE(pnTarget)) == FFAT_FALSE), (_T("dirty-size-rdonly node can not be here")));

		r = FFAT_Unlink(NULL, _NODE(pnTarget), FFAT_FALSE);
	}

	IF_LK (r == FFAT_OK)
	{
		FFAT_ASSERT(err == FERROR_NO_ERROR);

		NS_MarkVnodeMetaDirty(pnTarget);
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnTarget));
	}
	else
	{
		err = _errnoToNestle(r);
	}

out:
	_DELETENODE_OUT

	return err;
}


/**
* rename a VNODE to another one (target node may be exist)
*
* Nestle uses move2 instead of move when nativefs provides move2
* we do not need to support PFN_NS_VNODE_OPS_MOVE
*
* @param		pnSourceParent		: parent of source VNODE
* @param		pnSourceFile		: source VNODE
*										Source node will be new target node
* @param		pnTargetParent		: parent of target VNODE
* @param		pnTargetFile		: target node
* @param		pwszNewName			: new node name
* @param		dwNameLen			: name length of new node
* @param		bIsSourceOpened		: Source Node is in open state
*										Don't care source open state
* @param		bIsTargetOpened		: Target Node is in open state
*										target VNODE on Linux always in opened state
* @return		FERROR_NO_ERROR				: success
* @return		FERROR_IO_ERROR				: I/O error
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		09-SEP-2008 [DongYoung Seo] add resource release code
* @version		10-SEP-2008 [DongYoung Seo] remove node copy, 
										replace it with node pointer change
* @version		FEB-11-2009 [GwangOk Go] update renamed node info on pNodeSrc of FFAT_Rename
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
*/
static FERROR
_move2(		IN	NS_PVNODE			pnSourceParent,
			IN	NS_PVNODE			pnSource,
			IN	NS_PVNODE			pnTargetParent,
			IN	NS_PVNODE			pnTarget,
			IN	const wchar_t*		pwszNewName,
			IN	unsigned int		dwNameLen,
			IN	BOOL				bIsSourceOpened,
			IN	BOOL				bIsTargetOpened)
{
	FFatErr				r = FFAT_NO_VALUE;	// error variable for BTFS
	FERROR				err;				// error variable for nestle
	t_wchar				psName[FFAT_FILE_NAME_MAX_LENGTH + 1];
	FFatNode*			pNodeSrc;
	FFatNode*			pNodeDes = NULL;	// to allocate temporal destination node.
											// if target is an exist node do not use this variable
	FFatRenameFlag		dwFlag;				// flag for rename operation

	_MOVE2_IN

	FFAT_ASSERT(pnSourceParent);
	FFAT_ASSERT(pnSource);
	FFAT_ASSERT(pnTargetParent);
	FFAT_ASSERT(pwszNewName);
	FFAT_ASSERT(FFAT_NAME_MAX_LENGTH >= dwNameLen);
	FFAT_ASSERT(NS_GetVnodeIndex(pnSource) == _getInodeNumber(_NODE(pnSource)));
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pnSource))) == FFAT_FALSE);
	FFAT_ASSERT(NS_GetVcbFromVnode(pnSourceParent) == NS_GetVcbFromVnode(pnTargetParent));

	pNodeSrc = _NODE(pnSource);

	if (FFAT_NodeIsDir(pNodeSrc) == FFAT_TRUE)
	{
		IF_UK (dwNameLen > FFAT_DIR_NAME_MAX_LENGTH)
		{
			err = FERROR_NAME_TOO_LONG;
			goto err_nestle;
		}
	}
	else
	{
		IF_UK (dwNameLen > FFAT_FILE_NAME_MAX_LENGTH)
		{
			err = FERROR_NAME_TOO_LONG;
			goto err_nestle;
		}
	}

	// Before real rename, Do recovery operation about dirty-size-rdonly node of source.
	IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pnSource)) == FFAT_TRUE)
	{
		r = _recoveryDirtySizeNode(pnSource);
		IF_UK (r < 0)
		{
			err = _errnoToNestle(r);
			goto err_nestle;
		}
	}

	FFAT_WCSNCPY(psName, pwszNewName, dwNameLen);
	psName[dwNameLen] = ((t_wchar)'\0');

	if (pnTarget)
	{
		FFAT_ASSERT(NS_GetVnodeIndex(pnTarget) == _getInodeNumber(_NODE(pnTarget)));

		// Before real rename, Do recovery operation about dirty-size-rdonly node of target.
		IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pnTarget)) == FFAT_TRUE)
		{
			r = _recoveryDirtySizeNode(pnTarget);
			IF_UK (r < 0)
			{
				err = _errnoToNestle(r);
				goto err_nestle;
			}
		}

		if (bIsTargetOpened)
		{
			dwFlag = FFAT_RENAME_TARGET_OPENED;
		}
		else
		{
			dwFlag = FFAT_RENAME_NONE;
		}

		FFAT_DEBUG_ALVFS_PRINTF((_T("move2 to %s, source/destination VNODE ID:0x%016llX/0x%016llX\n"),
			_w2a(psName, dwNameLen), NS_GetVnodeIndex(pnSource), NS_GetVnodeIndex(pnTarget)));

		r = FFAT_Rename(_NODE(pnSourceParent), pNodeSrc,
						_NODE(pnTargetParent), _NODE(pnTarget),
						(t_wchar*)psName, dwFlag);

		FFAT_ASSERT(pNodeDes == NULL);		// do not use pNodeDes
	}
	else
	{
		FFAT_DEBUG_ALVFS_PRINTF((_T("move2 to %s, source/destination VNODE ID:0x%016llX/Not Exist\n"), _w2a(psName, dwNameLen), NS_GetVnodeIndex(pnSource)));
		//FFAT_ASSERT(!bIsTargetOpened);	// nestle policy is not determined yet.
											// i don't know about bIsTargetOpened can be true when pnTarget is valid

		pNodeDes = _allocNode();
		IF_UK (pNodeDes == NULL)
		{
			err = FERROR_INSUFFICIENT_MEMORY;
			goto err_nestle;
		}

		FFAT_ResetNodeStruct(pNodeDes);

		r = FFAT_Rename(_NODE(pnSourceParent), pNodeSrc, _NODE(pnTargetParent),
						pNodeDes, (t_wchar*)psName, FFAT_RENAME_LOOKUP);
	}

	IF_LK (r == FFAT_OK)
	{
		FFAT_DEBUG_ALVFS_PRINTF((_T("move2 to %s, old ID/new ID:0x%016llX/0x%016llX\n"),
								_w2a(psName, dwNameLen), NS_GetVnodeIndex(pnSource), _getInodeNumber(pNodeSrc)));

		NS_ChangeVnodeIndex(pnSource, _getInodeNumber(pNodeSrc));	// do not remove this for rename
		FFAT_ASSERT(NS_GetVnodeIndex(pnSource) == _getInodeNumber(pNodeSrc));

		NS_MarkVnodeMetaDirty(pnSourceParent);		// set VNODE dirty
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pnSourceParent));
		NS_MarkVnodeMetaDirty(pnSource);			// set VNODE dirty
		NS_MarkVnodeMetaDirty(pnTargetParent);		// set VNODE dirty

		if (pnTarget)
		{
			NS_MarkVnodeMetaDirty(pnTarget);		// set VNODE dirty

			if (pnSource != pnTarget)
			{
				// decrease the link count of unlinked target file
				FFAT_ASSERT(NS_GetVnodeLinkCnt(pnTarget) > 0);
				NS_SetVnodeLinkCnt(pnTarget, (NS_GetVnodeLinkCnt(pnTarget) - 1));

				if (bIsTargetOpened)
				{
					// target is open unlinked
					// do not remove this for open unlink
					NS_ChangeVnodeIndex(pnTarget, _getNewIDForOpenUnlink());
				}
			}
		}
	}

	err = _errnoToNestle(r);

err_nestle:

	if (pNodeDes)
	{
		// free temporary allocated destination node
		_freeNode(pNodeDes);
	}

	_MOVE2_OUT

	return err;
}


/**
* readdir + unlink a node
* This function does not erase directory, just return error.
*
* [NOTICE] 
* In Nestle, this is called after _readdir() with child Vnode ID.
* If Vnode is existed in Nestle, it will called the normal unlink routine instead of this function.
* Otherwise, this is called to delete the child file that is indexed by llVnodeID.
* So this does not check whether vnode in nestle is existed.
*
* @param		pVnode						: [IN] parent directory 
* @param		pFcb						: [IN] File Control Block
* @param		llVnodeID					: [IN] ID of VNODE to be deleted
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_NO_MORE_ENTRIES		: No more entries
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		21-OCT-2007 [DongYoung Seo] First Writing.
* @version		21-DEC-2008 [DongYoung Seo] add resource release code for deleted node
*/
static FERROR
_readdirUnlink (
				IN	NS_PVNODE			pVnode,
				IN	NS_PFCB				pFcb,
				IN	unsigned long long	llVnodeID)
{
	FFatNode*		pNode = NULL;
	t_uint32		dwOffset;			// readdir offset
	FFatErr			r;

	_READDIRUNLINK_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(pFcb);
	FFAT_ASSERT(_NODE(pVnode));
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	pNode = _allocNode();
	IF_UK (pNode == NULL)
	{
		r = FFAT_ENOMEM;
		goto out;
	}

	dwOffset = (t_uint32)NS_GetOffsetFromFcb(pFcb);

	FFAT_ASSERT(dwOffset < 2*1024*1024);

	r = _readdirGetNode(pVnode, pFcb, dwOffset, pNode);
	IF_LK (r < 0)
	{
		goto out;
	}

	if (FFAT_NodeIsDir(pNode) == FFAT_TRUE)
	{
		r = FFAT_EISDIR;
		goto out;
	}

	// readdir unlink
	FFAT_ASSERT(_getInodeNumber(pNode) == llVnodeID);
	FFAT_ASSERTP((FFAT_NodeIsDirtySizeRDOnly(pNode) == FFAT_FALSE), (_T("dirty-size-rdonly can not be here")));

	// delete it
	r = FFAT_Unlink(_NODE(pVnode), pNode, FFAT_FALSE);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to unlink node")));
		goto out;
	}

	r = FFAT_Close(pNode, FFAT_NODE_CLOSE_RELEASE_RESOURCE | FFAT_NODE_CLOSE_SYNC);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to release resource of a node")));
		goto out;
	}

	NS_MarkVnodeMetaDirty(pVnode);
	NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

out:
	_freeNode(pNode);

	_READDIRUNLINK_OUT

	return _errnoToNestle(r);
}


/**
* delete all of the files in a directory
*
* @param		pVnode			: parent directory
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		21-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_cleanDir (IN	NS_PVNODE		pVnode)
{
	FFatErr		r;

	_CLEANDIR_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	r = FFAT_FSCtl(FFAT_FSCTL_CLEAN_DIR, _NODE(pVnode), NULL, NULL);
	if (r == FFAT_OK)
	{
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));
	}

	_CLEANDIR_OUT

	return _errnoToNestle(r);
}


/**
* destroy a VNODE
*
* @param		pVnode						: target VNODE
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_ACCESS_DENIED		: current file does not enough permission
* @return		FERROR_NO_MORE_ENTRIES		: No more entries
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		20-OCT-2009 [JW Park] Remove the sync flag of FFAT_Close.
* @version		15-DEC-2009 [Chunum Kong] Remove FFAT_Close() and just excute to release free Vnode.
* @version		21-APR-2011 [Raghav Gupta] Add FFAT_Close() for mkrfs tool.
*/
static FERROR
_destroyNode(IN	NS_PVNODE pVnode)
{
#ifdef FSTOOL_MKRFS
	FFatErr			r = FFAT_OK;				// error variable for BTFS
#endif
	FERROR			err = FERROR_NO_ERROR;		// error variable for nestle

	_DESTROYNODE_IN

	FFAT_ASSERT(pVnode);

	IF_UK (_NODE(pVnode) == NULL)
	{
		goto out;
	}

	FFAT_DEBUG_ALVFS_PRINTF((_T("Destroy VNODE(PTR/ID),FFatNode:(0x%X/0x%016llX)/0x%X\n"), (t_uint32)pVnode, NS_GetVnodeIndex(pVnode), (t_uint32)_NODE(pVnode)));

#ifdef FSTOOL_MKRFS
/* @Author: Raghav Gupta
   FSTOOL_MKRFS: Macro passed in the mkrfs tool Makefile
   In BTFS_1.0.1_b046:The _destroyNodei() was divided into _destroyNode() and _clearNode().
   After clear_inode(), destroy_inode() is called by Linux VFS.
   clear_inode() calls _clearNode() and destroy_inode() calls _destroyNode().
   But the mkrfs tool, uses the tfs4 vfs, which calls only _destroyNode().
   Therefore in the case of the mkrfs tool, the _clearNode() functionality is implemented in _destroyNode().
*/
	if (FFAT_NodeIsRoot(_NODE(pVnode)) == FFAT_FALSE)
	{
		r = FFAT_Close(_NODE(pVnode), FFAT_NODE_CLOSE_RELEASE_RESOURCE);
		IF_LK (r == FFAT_OK)
		{
			FFAT_ASSERT(err == FERROR_NO_ERROR);

			_freeNode(_NODE(pVnode));
			NS_LinkNativeNode(pVnode, NULL);
		}
		else
		{
			err = _errnoToNestle(r);
		}
	}
	else
	{
		FFAT_ASSERT(err == FERROR_NO_ERROR);

		NS_LinkNativeNode(pVnode, NULL);
		// do not free memory
		// memory for root node will be freed on _umount()
	}
#else
	if (FFAT_NodeIsRoot(_NODE(pVnode)) == FFAT_FALSE)
	{
		_freeNode(_NODE(pVnode));
		NS_LinkNativeNode(pVnode, NULL);
	}
	else
	{
		NS_LinkNativeNode(pVnode, NULL);
		// do not free memory
		// memory for root node will be freed on _umount()
	}
#endif

out:
	_DESTROYNODE_OUT

	return err;
}


/**
* clear a VNODE
*
* @param		pVnode						: target VNODE
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_ACCESS_DENIED		: current file does not enough permission
* @return		FERROR_NO_MORE_ENTRIES		: No more entries
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @author		Chunum Kong
* @version		15-DEC-2009 [Chunum Kong] Excute FFAT_Close(). this is modifed _destroyNode() which is basis.
*/
static FERROR
_clearNode(IN	NS_PVNODE pVnode)
{
	FFatErr			r = FFAT_OK;				// error variable for BTFS
	FERROR			err = FERROR_NO_ERROR;		// error variable for nestle


	FFAT_ASSERT(pVnode);

	IF_UK (_NODE(pVnode) == NULL)
	{
		goto out;
	}

	FFAT_DEBUG_ALVFS_PRINTF((_T("Clear VNODE(PTR/ID),FFatNode:(0x%X/0x%016llX)/0x%X\n"), (t_uint32)pVnode, NS_GetVnodeIndex(pVnode), (t_uint32)_NODE(pVnode)));

	if (FFAT_NodeIsRoot(_NODE(pVnode)) == FFAT_FALSE)
	{
		r = FFAT_Close(_NODE(pVnode), FFAT_NODE_CLOSE_RELEASE_RESOURCE);
		IF_LK (r == FFAT_OK)
		{
			FFAT_ASSERT(err == FERROR_NO_ERROR);
		}
		else
		{
			err = _errnoToNestle(r);
		}
	}
	else
	{
		FFAT_ASSERT(err == FERROR_NO_ERROR);
	}

out:

	return err;
}



/**
* set node attribute
*
* @param		pVnode			: target VNODE
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_setAttributes(		IN	NS_PVNODE		pVnode,
					IN	NS_FILE_ATTR	dwAttributes)
{
	FFatNodeStatus	stStatus;
	FFatErr			r;

	_SETATTRIBUTES_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	r = FFAT_GetNodeStatus(_NODE(pVnode), &stStatus);
	IF_UK (r != FFAT_OK)
	{
		goto out;
	}

	stStatus.dwAttr	= dwAttributes & FFAT_ATTR_MASK;

	r = FFAT_SetNodeStatus(_NODE(pVnode), &stStatus);
	IF_UK (r != FFAT_OK)
	{
		goto out;
	}

	NS_MarkVnodeMetaDirty(pVnode);	// set VNODE dirty
	NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

out:
	_SETATTRIBUTES_OUT

	return _errnoToNestle(r);
}


/**
* change node size
*
* @param		pVnode			: VNODE
* @param		llFileSize		: size to truncate
* @param		bFillZero		:	TRUE	: do cluster initialization at BTFS
*									FALSE	: use nestle interface to init cluster
* @return		FERROR_NO_ERROR			: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		22-AUG-2008 [DongYoung Seo] add assert to check consistency of 
*								bFillZero and NS_IsInitCluster
* @version		22-MAR-2009 [DongYOung Seo] add block size update code
* @version		22-OCT-2009 [JW Park] add FFAT_CHAGE_SIZE_RECORD_DIRTY_SIZE flag
*										to update the size after sync user data in expand
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
*/
static FERROR
_truncate(	IN	NS_PVNODE			pVnode,
			IN	NS_FILE_SIZE		llFileSize,
			IN	BOOL				bFillZero)
{
	FFatErr				r, rr;
	t_uint32			dwCurSize;			// current node size
	t_uint32			dwSize;				// new node size
	FFatNodeStatus		stStat;
	FILE_OFFSET			llOffset;			// offset
	t_uint32			dwBytesToFill;		// byte to fill zero
	t_uint32			dwBytesToFilled;		// byte to fill zero
	FERROR				err;				// error for nestle
	FFatChangeSizeFlag	dwFlag;

	_TRUNCATE_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(bFillZero ? (NS_IsInitCluster(pVnode) == TRUE) : FFAT_TRUE);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	if (llFileSize > 0xFFFFFFFFUL)
	{
		_TRUNCATE_OUT
		return FERROR_NOT_SUPPORTED;
	}

	dwCurSize	= FFAT_GetSize(_NODE(pVnode));
	dwSize = (t_uint32)(llFileSize & 0xFFFFFFFF);

	dwFlag = bFillZero ? FFAT_CHANGE_SIZE_NONE : FFAT_CHANGE_SIZE_NO_INIT_CLUSTER;

	// 2009-10-22, Add the dirty-size flag to update the size after sync user data in expand.
	if (dwCurSize < dwSize)
	{
		dwFlag |= FFAT_CHANGE_SIZE_RECORD_DIRTY_SIZE;
	}

	// Before real truncate, Do recovery operation about dirty-size-rdonly node.
	IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pVnode)) == FFAT_TRUE)
	{
		r = _recoveryDirtySizeNode(pVnode);
		IF_UK (r < 0)
		{
			goto out;
		}
	}

	r = FFAT_ChangeSize(_NODE(pVnode), dwSize, dwFlag);
	IF_LK (r == FFAT_OK)
	{
		// CHECK : why nestle does not update file size?
		if ((dwSize > dwCurSize) &&
			(dwFlag & FFAT_CHANGE_SIZE_NO_INIT_CLUSTER) && 
			(NS_IsInitCluster(pVnode) == TRUE))
		{
			llOffset			= dwCurSize;
			dwBytesToFill		= dwSize - dwCurSize;
			err = NS_FillZeroVnode(pVnode, llOffset, dwBytesToFill, &dwBytesToFilled);
			IF_UK (FERROR_NO_ERROR != err)
			{
				FFAT_ChangeSize(_NODE(pVnode), dwCurSize, dwFlag);
				_TRUNCATE_OUT
				return err;
			}

			FFAT_ASSERT(dwBytesToFill == dwBytesToFilled);
		}

		NS_SetFileSize(pVnode, llFileSize);
		NS_MarkVnodeMetaDirty(pVnode);	// set VNODE dirty
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

		rr = FFAT_GetNodeStatus(_NODE(pVnode), &stStat);
		if (rr == FFAT_OK)
		{
			// update file time
			_updateVnodeTime(pVnode, &stStat);

			// update allocated size
			NS_SetVnodeBlocks(pVnode, stStat.dwAllocSize);
		}
		// don't care error - this is minor case

	}

out:
	_TRUNCATE_OUT

	return _errnoToNestle(r);
}


/**
* change node time attribute
*
* @param		pVnode			:  VNODE
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		04-NOV-2008 [DongYoung Seo] Shift left 16 bit for ATime
*/
static FERROR
_setFileTime(	IN	NS_PVNODE			pVnode,
				IN	NS_PSYS_TIME		ptmCreated,
				IN	NS_PSYS_TIME		ptmLastAccessed,
				IN	NS_PSYS_TIME		ptmLastModified)
{
	FFatNodeStatus	stStatus;
	FFatErr			r;

	_SETFILETIME_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	r = FFAT_GetNodeStatus(_NODE(pVnode), &stStatus);
	IF_UK (r != FFAT_OK)
	{
		goto out;
	}

	if (ptmCreated)
	{
		stStatus.dwCTime = (_getDosDate(ptmCreated) << 16) | _getDosTime(ptmCreated);
	}

	if (ptmLastAccessed)
	{
		stStatus.dwATime = _getDosDate(ptmLastAccessed) << 16;
	}

	if (ptmLastModified)
	{
		stStatus.dwMTime = (_getDosDate(ptmLastModified) << 16) | _getDosTime(ptmLastModified);
	}

	r = FFAT_SetNodeStatus(_NODE(pVnode), &stStatus);
	IF_UK (r != FFAT_OK)
	{
		goto out;
	}

	NS_MarkVnodeMetaDirty(pVnode);	// set VNODE dirty
	NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

out:
	_SETFILETIME_OUT

	return _errnoToNestle(r);
}


/**
* return block information.
*
* block : block number == sector offset
* 
* map block return the block address of the offset.
*
* @param		pVnode			: [IN] VNODE
* @param		dwBlockIndex	: [IN] block offset
* @param		dwBlockCnt		: [IN] required count
* @param		pdwBlockNum		: [OUT] first block number storage
* @param		pdwContBlockCnt	: [OUT] continuous block count
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_mapBlocks(	IN	NS_PVNODE			pVnode,
			IN	unsigned int		dwBlockIndex,
			IN	unsigned int		dwBlockCnt,
			OUT unsigned int*		pdwBlockNum,
			OUT unsigned int*		pdwContBlockCnt)
{
	FFatVol*			pVol;		// volume
	FFatVC				stVC;		// Vector Cluster
	FFatVCE				stVCE;		// Vector cluster entry
	FFatErr				r;
	FFatLDevInfo*		pLDevInfo;

	_MAPBLOCKS_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(_NODE(pVnode));
	FFAT_ASSERT(pdwBlockNum);
	FFAT_ASSERT(pdwContBlockCnt);

	pVol = FFAT_GetVol(_NODE(pVnode));

	pLDevInfo = FFAT_GetLDevInfo(pVol);

	FFAT_INIT_VC(&stVC, (dwBlockIndex << pLDevInfo->dwBlockSizeBits));

	stVC.dwTotalEntryCount = 1;
	stVC.pVCE		= &stVCE;
	stVCE.dwCluster	= 0;
	stVCE.dwCount	= 0;

	r = FFAT_GetNodeClusters(_NODE(pVnode), stVC.dwClusterOffset,
							dwBlockCnt << pLDevInfo->dwBlockSizeBits, &stVC);
	IF_LK (r == FFAT_OK)
	{
		FFAT_ASSERT(stVC.dwValidEntryCount == 1);

		// get first sector of cluster
		FFAT_GetSectorOfCluster(pVol, stVC.pVCE[0].dwCluster, pdwBlockNum);

		FFAT_ASSERT(pLDevInfo->dwFATSectorPerBlock >= 1);

		// calculate block number
		*pdwBlockNum		= (*pdwBlockNum >> pLDevInfo->dwFATSectorPerBlockBits)
								+ (dwBlockIndex & pLDevInfo->dwBlockPerClusterMask);

		*pdwContBlockCnt = stVC.pVCE[0].dwCount << pLDevInfo->dwBlockPerClusterBits;

		// sub head count
		*pdwContBlockCnt -= (dwBlockIndex & pLDevInfo->dwBlockPerClusterMask);

		if (dwBlockCnt < *pdwContBlockCnt)
		{
			t_uint32		dwTemp;

			dwTemp = pLDevInfo->dwBlockPerCluster - ((dwBlockIndex + dwBlockCnt) & pLDevInfo->dwBlockPerClusterMask);

			// sub tail count
			*pdwContBlockCnt -= dwTemp;
		}
	}

	_MAPBLOCKS_OUT

	return _errnoToNestle(r);
}


/**
 * check whether node is accessible (log file is not accessible)
 * ACCESS_MODE may be implemented later
 *
 * 리눅스 vfs의 truncate interface에는 void가 반환되며
 * write 권한이 없어도 truncate가 되기 때문에
 * permission을 통하여 log file인지 아닌지 확인하여야 함
 *
 * @param		pVnode			: [IN] VNODE
 * @param		dwOperationMode	: [IN] operation mode
 * @author		GwangOk Go
 * @version		JAN-21-2009 [GwangOk Go] First Writing.
 */
static FERROR
_permission(NS_PVNODE			pVnode,
			NS_OPERATION_MODE	dwOperationMode)
{
	FFatErr				r;

	FFAT_ASSERT(pVnode);

	_PERMISSION_IN

	if (dwOperationMode & OP_READ)
	{
		// read인 경우 모든 node(log file 포함)에 대한 권한을 허용함
		r = FFAT_OK;
	}
	else
	{
		r = FFAT_CheckPermission(_NODE(pVnode));
	}

	_PERMISSION_OUT

	if (r != FFAT_OK)
	{
		return _errnoToNestle(r);
	}

	return FERROR_NO_ERROR;
}



/**
* change node size
*
*	does not initialize clusters
*	this is only for LINUX
*	do not update file size on VNODE
*	do not set dirty flag on VNODE
*
* @param		pVnode			: VNODE
* @param		llSize			: new VNODE size.
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		05-SEP-2008 [DongYoung Seo] do not update file size of VNODE
* @version		08-SEP-2008 [DongYoung Seo] do not update dirty flag of VNODE
*											Nestle will do this.
* @version		22-MAR-2009 [DongYoung Seo] add NS_SetVnodeBlocks() invoke after change size
* @version		19-OCT-2009 [JW Park] add FFAT_CHAGE_SIZE_RECORD_DIRTY_SIZE flag
*											to update the size after sync user data
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
*/
static FERROR
_expandClusters(NS_PVNODE		pVnode,
				NS_FILE_SIZE	llSize)
{
	FFatErr				r, rr;
	t_uint32			dwSize;
	t_uint32			dwSizePrev;		// previous node size
	FFatChangeSizeFlag	dwFlag;
	FFatNodeStatus		stStat;			// for block size

	_EXPANDCLUSTERS_IN

	FFAT_ASSERT(pVnode);
#if defined(NS_CONFIG_RTOS) || defined(NS_CONFIG_WINCE)
	FFAT_ASSERTP(0, (_T("this is for linux and symbian")));
#endif

	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	if (llSize > 0xFFFFFFFFUL)
	{
		_EXPANDCLUSTERS_OUT
		return FERROR_NOT_SUPPORTED;
	}

	// JAN-15-2008, do not initialize cluster - this is nestle policy
	// 2009-10-19, Add the dirty-size flag to update the size after sync user data.
	dwFlag = FFAT_CHANGE_SIZE_NO_INIT_CLUSTER | FFAT_CHANGE_SIZE_LAZY_SYNC
			| FFAT_CHANGE_SIZE_AVAILABLE | FFAT_CHANGE_SIZE_RECORD_DIRTY_SIZE;

	dwSizePrev = FFAT_GetSize(_NODE(pVnode));
	dwSize = (t_uint32)(llSize & 0xFFFFFFFF);

	if (dwSizePrev >= dwSize)
	{
		_EXPANDCLUSTERS_OUT
		return FERROR_NO_ERROR;
	}

	// Before real expand, Do recovery operation about dirty-size-rdonly node.
	IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pVnode)) == FFAT_TRUE)
	{
		r = _recoveryDirtySizeNode(pVnode);
		IF_UK (r < 0)
		{
			goto out;
		}
	}

	r = FFAT_ChangeSize(_NODE(pVnode), dwSize, dwFlag);

	// no need to fill zero VNODE at here, VFS will do this.
	// do not update file size. NS_SetFileSize(pVnode, llSize);
	// do not set dirty flag on VNODE, -NS_MarkVnodeMetaDirty(pVnode);			// set VNODE dirty
	// BTFS does not update VNODE. but BTFS is on dirty state .!!
	//	VFS will set dirty VNODE itself.
	if (r == FFAT_OK)
	{
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

		// update block size
		rr = FFAT_GetNodeStatus(_NODE(pVnode), &stStat);
		if (rr == FFAT_OK)
		{
			NS_SetVnodeBlocks(pVnode, stStat.dwAllocSize);
		}
		// don't care error. this is minor case
	}

out:
	_EXPANDCLUSTERS_OUT

	return _errnoToNestle(r);
}


/**
*  sync a VNODE
*
* @param		pVnode			:  VNODE
* @param		LLValidFileSize	:  Valid size that user data is synchronized
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		12-OCT-2007 [DongYoung Seo] First Writing.
* @version		01-JUN-2009 [JeongWoo Park] Add the code to skip the sync about root directory.
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
*/
static FERROR
_syncFile(	IN	NS_PVNODE	pVnode,
			IN	FILE_SIZE	llValidFileSize,
			IN	BOOL		bUpdateAccessTime,
			IN	BOOL		bUpdateModifyTime)
{
	FFatErr			r;
	_SYNCFILE_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	if (FFAT_NodeIsRoot(_NODE(pVnode)) == FFAT_TRUE)
	{
		// Nothing to do about Root directory
		r = FFAT_OK;
		goto out;
	}

	if (bUpdateAccessTime || bUpdateModifyTime)
	{
		FFatNodeStatus	stStatus;
		NS_SYS_TIME		stAccessTime;
		NS_SYS_TIME		stModifyTime;

		r = FFAT_GetNodeStatus(_NODE(pVnode), &stStatus);
		IF_UK (r != FFAT_OK)
		{
			goto out;
		}

		NS_GetFileTimes(pVnode, NULL, &stAccessTime, &stModifyTime);

		if (bUpdateAccessTime)
		{
			stStatus.dwATime = _getDosDate(&stAccessTime) << 16;
		}

		if (bUpdateModifyTime)
		{
			stStatus.dwMTime = (_getDosDate(&stModifyTime) << 16) | _getDosTime(&stModifyTime);
		}

		r = FFAT_SetNodeStatus(_NODE(pVnode), &stStatus);
		IF_UK (r != FFAT_OK)
		{
			goto out;
		}
	}

	if (_NODE(pVnode))
	{
		// Before real Sync, Do recovery operation about dirty-size-rdonly node.
		IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pVnode)) == FFAT_TRUE)
		{
			r = _recoveryDirtySizeNode(pVnode);
			IF_UK (r < 0)
			{
				goto out;
			}
		}

		r = FFAT_SyncNode(_NODE(pVnode), (t_uint32)llValidFileSize);
	}
	else
	{
		// This is renamed node
		r = FFAT_OK;
	}

out:
	_SYNCFILE_OUT

	return _errnoToNestle(r);
}


// functions for NS_FILE_OPS

/**
*  close a FCB
*
* @param		pFcb			: a FCB
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		12-OCT-2007 [DongYoung Seo] First Writing.
* @version		20-OCT-2009 [JW Park] Remove the sync flag of FFAT_Close.
*/
static FERROR
_close(	IN NS_PFCB	pFcb)
{
	FFatErr		r;
	NS_PVNODE	pVnode;

	_CLOSE_IN

	FFAT_ASSERT(pFcb);

	pVnode = NS_GetVnodeFromFcb(pFcb);

	// close node
	r = FFAT_Close(_NODE(pVnode), FFAT_NODE_CLOSE_DEC_REFERENCE);

	_CLOSE_OUT

	return _errnoToNestle(r);
}


/**
*  read an entry from a directory
*
* @param		pVnode			: VNODE
* @param		pNativeFcb		: FCB
* @param		dwOrdinal		: read offset
* @param		pEntry			: storage
* @param		pdwNumberOfRead	: count of read byte
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_NO_MORE_ENTRIES		: No more entries
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		12-OCT-2007 [DongYoung Seo] First Writing.
* @version		11-DEC-2008 [DongYoung Seo] bug fix CQID:FLASH00018670
*										correct last access date updating routine.
* @version		JUN-17-2009 [GwangOk Go] add fifo, socket
*/
static FERROR
_readdir(	IN	NS_PFCB				pFcb,
			IN	const wchar_t*		pwszFileNameToSearch,
			OUT	NS_PDIR_ENTRY		pEntry,
			OUT	unsigned int*		pdwNumberOfRead)
{
	FFatErr					r ;
	FFatReaddirStatInfo		stRSI;
	NS_PVNODE				pVnode;
	NS_PVNODE				pVnodeChild;
	t_uint32				dwStartOffset;

	_READDIR_IN

	FFAT_ASSERT(pFcb);
	FFAT_ASSERT(pEntry);
	FFAT_ASSERT(pdwNumberOfRead);

	dwStartOffset = stRSI.dwOffset = (t_uint32)NS_GetOffsetFromFcb(pFcb);
	stRSI.dwOffsetNext		= 0;
#ifdef FFAT_VFAT_SUPPORT
	stRSI.psName			= pEntry->wszName;
	stRSI.dwNameLen			= sizeof(pEntry->wszName) / sizeof(wchar_t);		// set storage size

	stRSI.psShortFileName	= NULL;
	stRSI.dwShortFileNameLen = 0;

	// pEntry->wszAltName is not used. (for Symbian)
//	stRSI.psShortFileName	= pEntry->wszAltName;
//	stRSI.dwShortFileNameLen = sizeof(pEntry->wszAltName) / sizeof(wchar_t);	// set storage size
#else
	stRSI.psName			= NULL;
	stRSI.dwNameLen			= 0;

	stRSI.psShortFileName	= pEntry->wszName;
	stRSI.dwShortFileNameLen = sizeof(pEntry->wszName) / sizeof(wchar_t);		// set storage size
#endif
	stRSI.psNameToSearch	= pwszFileNameToSearch;

	pVnode = NS_GetVnodeFromFcb(pFcb);

	r = FFAT_FSCtl(FFAT_FSCTL_READDIR_STAT, _NODE(pVnode), &stRSI, NULL);
	IF_LK (r == FFAT_OK)
	{
		*pdwNumberOfRead	= stRSI.dwOffsetNext - dwStartOffset;		// set next READDIR offset.

		pEntry->llVnodeID	= _getInodeNumber2(FFAT_GetVol(_NODE(pVnode)),
											stRSI.stStat.dwIno1, stRSI.stStat.dwIno2);

		// If there is existed VNODE in nestle, get status by using the VNODE.
		// the node can be dirty-sized node.
		pVnodeChild = NULL;
		pVnodeChild = NS_FindVnodeFromVcb(NS_GetVcbFromVnode(pVnode), pEntry->llVnodeID);
		if (pVnodeChild != NULL)
		{
			r = FFAT_GetNodeStatus(_NODE(pVnodeChild), &(stRSI.stStat));
			IF_UK (r != FFAT_OK)
			{
				// release the reference count
				NS_ReleaseVnode(pVnodeChild);
				goto out;
			}

			// release the reference count
			NS_ReleaseVnode(pVnodeChild);
		}

		pEntry->dwFileAttribute	= stRSI.stStat.dwAttr & FFAT_ATTR_MASK;

		_getNestleDate((t_uint16)(stRSI.stStat.dwCTime >> 16), &pEntry->ftCreated);
		_getNestleTime((t_uint16)stRSI.stStat.dwCTime, (t_uint8)stRSI.stStat.dwCTimeTenth, &pEntry->ftCreated);

		_getNestleDate((t_uint16)(stRSI.stStat.dwATime >> 16), &pEntry->ftLastAccessed);
		pEntry->ftLastAccessed.wHour			= 0;
		pEntry->ftLastAccessed.wMinute			= 0;
		pEntry->ftLastAccessed.wSecond			= 0;
		pEntry->ftLastAccessed.wMilliseconds	= 0;

		_getNestleDate((t_uint16)(stRSI.stStat.dwMTime >> 16), &pEntry->ftLastModified);
		_getNestleTime((t_uint16)stRSI.stStat.dwMTime, 0, &pEntry->ftLastModified);

		pEntry->llFileSize		= (unsigned long long) stRSI.stStat.dwSize;

		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_SYMLINK) ? ((stRSI.stStat.dwAttr & FFAT_NODE_FIFO) == 0) : FFAT_TRUE);
		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_SYMLINK) ? ((stRSI.stStat.dwAttr & FFAT_NODE_SOCKET) == 0) : FFAT_TRUE);
		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_FIFO) ? ((stRSI.stStat.dwAttr & FFAT_NODE_SYMLINK) == 0) : FFAT_TRUE);
		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_FIFO) ? ((stRSI.stStat.dwAttr & FFAT_NODE_SOCKET) == 0) : FFAT_TRUE);
		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_SOCKET) ? ((stRSI.stStat.dwAttr & FFAT_NODE_SYMLINK) == 0) : FFAT_TRUE);
		FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_SOCKET) ? ((stRSI.stStat.dwAttr & FFAT_NODE_FIFO) == 0) : FFAT_TRUE);

		switch (stRSI.stStat.dwAttr & FFAT_NODE_SPECIAL_FILES)
		{
		case FFAT_NODE_SYMLINK:
			FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_DIR) == 0);
			pEntry->dwFileAttribute |= NS_FILE_ATTR_LINKED;
			break;

		case FFAT_NODE_FIFO:
			FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_DIR) == 0);
			pEntry->dwFileAttribute |= NS_FILE_ATTR_FIFO;
			break;

		case FFAT_NODE_SOCKET:
			FFAT_ASSERT((stRSI.stStat.dwAttr & FFAT_NODE_DIR) == 0);
			pEntry->dwFileAttribute |= NS_FILE_ATTR_SOCKET;
			break;
		}
	}
	else
	{
		*pdwNumberOfRead = 0;
	}

out:
	_READDIR_OUT

	return _errnoToNestle(r);
}


/**
*  write data to a file
*
* @param		pFcb			: FCB
* @param		pBuffer			: out buffer pointer
* @param		dwBytesToWrite	: write size
* @param		pdwBytesWritten	: written size
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @RETURN		FERROR_NO_FREE_SPACE		: Not enough free space
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		12-OCT-2007 [DongYoung Seo] First Writing.
* @version		26-OCT-2009 [JW Park] add FFAT_WRITE_RECORD_DIRTY_SIZE flag
*										to update the size after sync user data in expand
* @version		29-OCT-2009 [JW Park] add the consideration about recovery about dirty-size node
*										if read-only attribute of volume is changed by remount()
* @version		04-DEC-2009 [JW Park] add the code to update time information of VNODE
*/
static FERROR
_write(		IN	NS_PFCB				pFcb,
			IN	unsigned char*		pBuffer,
			IN	unsigned int		dwBytesToWrite,
			OUT unsigned int*		pdwBytesWritten)
{
	FFatErr				r, rr;
	t_uint32			dwOffset;
	FFatWriteFlag		dwFlag = FFAT_WRITE_NONE;

	NS_PVNODE			pVnode;
	FILE_SIZE			llSize;

	FFatNodeStatus		stStat;

	_WRITE_IN

	FFAT_ASSERT(pFcb);
	FFAT_ASSERT(pBuffer);
	FFAT_ASSERT(pdwBytesWritten);

	pVnode	= NS_GetVnodeFromFcb(pFcb);

	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);

	if (NS_IsSyncMode(pFcb))
	{
		dwFlag |= FFAT_WRITE_SYNC;
	}

	if (NS_IsBypassMode(pFcb))
	{
		dwFlag |= FFAT_WRITE_DIRECT_IO;
	}

	if ((NS_IsModtimeUpdate(pFcb) == FALSE) &&
		(NS_IsAcstimeUpdate(pFcb) == FALSE))
	{
		// 모두 수정하지 않도록 설정되어 있을 경우, DE를 업데이트 하지 않음
		// (하나라도 수정하라고 설정이 되어 있으면, DE를 업데이트)
		// (사이즈가 변경될 경우, 설정에 상관없이 DE를 업데이트)
		dwFlag |= FFAT_WRITE_NO_META_UPDATE;
	}

	if (NS_IsInitCluster(pVnode) == TRUE)
	{
		dwFlag |= FFAT_WRITE_NO_INIT_CLUSTER;
	}

	if (0xFFFFFFFFULL < NS_GetOffsetFromFcb(pFcb))
	{
		r = FFAT_ERANGE;
		goto out;
	}

	// Before real write, Do recovery operation about dirty-size-rdonly node.
	IF_UK (FFAT_NodeIsDirtySizeRDOnly(_NODE(pVnode)) == FFAT_TRUE)
	{
		r = _recoveryDirtySizeNode(pVnode);
		IF_UK (r < 0)
		{
			goto out;
		}
	}

	// If not sync/direct IO mode, record dirty-size.
	if ((dwFlag & (FFAT_WRITE_SYNC | FFAT_WRITE_DIRECT_IO)) == 0)
	{
		dwFlag |= FFAT_WRITE_RECORD_DIRTY_SIZE;
	}
	else
	{
		// Sync previous user data.
		NS_SyncBuffer(NS_GetVcbFromVnode(pVnode), pVnode);
	}

	dwOffset = (t_uint32) (NS_GetOffsetFromFcb(pFcb) & 0xFFFFFFFF);

	// @20070718-iris: 64bit file size support
	r = FFAT_Write(_NODE(pVnode), dwOffset, (t_int8*)pBuffer, dwBytesToWrite, dwFlag);
	IF_LK (r > 0)
	{
		// cluster cache 정보를 update 한다.
		llSize = FFAT_GetSize(_NODE(pVnode));
		NS_SetFileSize(pVnode, llSize);
		*pdwBytesWritten = r;
		
		if ((dwFlag & FFAT_WRITE_SYNC) == 0)
		{
			NS_MarkVnodeMetaDirty(pVnode);	// set VNODE dirty
			NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));
		}

		rr = FFAT_GetNodeStatus(_NODE(pVnode), &stStat);
		if (rr == FFAT_OK)
		{
			// update file time
			_updateVnodeTime(pVnode, &stStat);

			// update allocated size
			NS_SetVnodeBlocks(pVnode, stStat.dwAllocSize);
		}
		// don't care error - this is minor case
	}

out:

	_WRITE_OUT

	IF_UK (r < 0)
	{
		return _errnoToNestle(r);
	}

	FFAT_ASSERT(r != 0);

	return FERROR_NO_ERROR;
}


/**
*  read data from a file
*
* @param		pFcb			: FCB
* @param		pBuffer			: out buffer pointer
* @param		dwBytesToWrite	: write size
* @param		pdwBytesWritten	: written size
* @return		FERROR_NO_ERROR				: a file is deleted
* @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
* @return		FERROR_IO_ERROR				: I/O error
* @author		DongYoung Seo
* @version		12-OCT-2007 [DongYoung Seo] First Writing.
*/
static FERROR
_read(		IN	NS_PFCB				pFcb,
			IN	unsigned char*		pBuffer,
			IN	unsigned int		dwBytesToRead,
			OUT unsigned int*		pdwBytesRead)
{
	FFatErr				r;
	t_uint32			dwOffset;
	FFatReadFlag		dwReadFlag = FFAT_READ_NONE;

	NS_PVNODE			pVnode;

	_READ_IN

	FFAT_ASSERT(pFcb);
	FFAT_ASSERT(pBuffer);
	FFAT_ASSERT(pdwBytesRead);

	if (NS_IsBypassMode(pFcb))
	{
		dwReadFlag |= FFAT_READ_DIRECT_IO;
	}

	if (NS_IsAcstimeUpdate(pFcb) == TRUE)
	{
		dwReadFlag |= FFAT_READ_UPDATE_ADATE;
	}

	if (0xFFFFFFFFULL < NS_GetOffsetFromFcb(pFcb))
	{
		r = FFAT_ERANGE;
		goto out;
	}

	dwOffset = (t_uint32) (NS_GetOffsetFromFcb(pFcb) & 0xFFFFFFFF);

	pVnode	= NS_GetVnodeFromFcb(pFcb);

	// @20070718-iris: 64bit file size support
	r = FFAT_Read(_NODE(pVnode), dwOffset, (t_int8*)pBuffer, dwBytesToRead, dwReadFlag);
	IF_LK (r > 0)
	{
		*pdwBytesRead = r;
	}

out:
	_READ_OUT

	IF_UK (r < 0)
	{
		return _errnoToNestle(r);
	}

	return FERROR_NO_ERROR;
}


/**
 * set user/group id, permission
 *
 * @param		pVnode		: [IN] VNODE pointer
 * @param		dwUID		: [IN] user id
 * @param		dwGID		: [IN] group id
 * @param		pdwPerm		: [IN] permission
 * @return		FERROR_NO_ERROR				: a file is deleted
 * @return		FERROR_INSUFFICIENT_MEMORY	: fail to allocate memory
 * @return		FERROR_IO_ERROR				: I/O error
 * @author		GwangOk Go
 * @version		19-AUG-2008 [GwangOk Go] First Writing.
 */
static FERROR
_setGuidMode(	IN	NS_PVNODE			pVnode,
				IN	unsigned int		dwUID,
				IN	unsigned int		dwGID,
				IN	NS_ACL_MODE			wPerm)
{
	FFatErr						r ;
	FFatExtendedDirEntryInfo	stXDEInfo;

	_SETGUIDMODE_IN

	FFAT_ASSERT(pVnode);

	stXDEInfo.dwUID		= dwUID;
	stXDEInfo.dwGID		= dwGID;
	stXDEInfo.dwPerm	= wPerm;

	r = FFAT_FSCtl(FFAT_FSCTL_SET_GUID_PERM, _NODE(pVnode), &stXDEInfo, NULL);
	if (r == FFAT_OK)
	{
		NS_MarkVnodeMetaDirty(pVnode);
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));
	}

	_SETGUIDMODE_OUT

	return _errnoToNestle(r);
}


/**
 * get short/long name
 * 
 * @param		pParent				: parent directory 
 * @param		pwszInputName		: input name
 * @param		pwszInputSize		: length of input name
 * @param		pwsOutputName		: output name buffer
 * @param		dwOutputNameSize	: length of output name buffer
 * @param		dwConvertType		: convert type
 * @return		FFAT_OK		: Success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		18-MAR-2008 [GwangOk Go] First Writing.
 */
static FERROR
_convertName(	IN NS_PVNODE			pParent,
				IN const wchar_t*		pwszInputName,
				IN const unsigned int	pwszInputSize,
				OUT wchar_t*			pwsOutputName,
				IO unsigned int		dwOutputNameSize,
				IN unsigned int		dwConvertType)
{
	FFatGetShortLongName	stGSLN;
	FFatErr		r = FFAT_EINVALID;

	_CONVERTNAME_IN

	FFAT_ASSERT(pParent);
	FFAT_ASSERT(pwszInputName);
	FFAT_ASSERT(pwsOutputName);
	FFAT_ASSERT((dwConvertType == NS_FILE_NAME_LONG_TO_SHORT) || (dwConvertType == NS_FILE_NAME_SHORT_TO_LONG));

	stGSLN.pParent = _NODE(pParent);

	if (dwConvertType == NS_FILE_NAME_LONG_TO_SHORT)
	{
		FFAT_ASSERT(pwszInputSize <= FFAT_FILE_NAME_MAX_LENGTH + 1);

		stGSLN.psLongName = (t_wchar*)pwszInputName;
		stGSLN.dwLongNameLen = FFAT_FILE_NAME_MAX_LENGTH + 1;

		stGSLN.psShortName = pwsOutputName;
		stGSLN.dwShortNameLen = dwOutputNameSize;

		r = FFAT_FSCtl(FFAT_FSCTL_GET_SHORT_NAME, &stGSLN, NULL, NULL);
	}
	else if (dwConvertType == NS_FILE_NAME_SHORT_TO_LONG)
	{
		FFAT_ASSERT(pwszInputSize <= FAT_DE_SFN_MAX_LENGTH + 1);

		stGSLN.psShortName = (t_wchar*)pwszInputName;
		stGSLN.dwShortNameLen = FAT_DE_SFN_MAX_LENGTH + 1;

		stGSLN.psLongName = pwsOutputName;
		stGSLN.dwLongNameLen = dwOutputNameSize;

		r = FFAT_FSCtl(FFAT_FSCTL_GET_LONG_NAME, &stGSLN, NULL, NULL);
	}

	_CONVERTNAME_OUT

	return _errnoToNestle(r);
}


/**
 * set an extended attribute value
 * 
 * @param		pVnode			: Vnode
 * @param		psName			: extended attribute name
 * @param		pValue			: extended attribute value
 * @param		dwValueSize		: size of extended attribute value
 * @param		dwFlag			: extended attribute flag
 * @return		FERROR_NO_ERROR	: Success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		29-JUL-2008 [GwangOk Go] First Writing.
 * @version		22-MAR-2009 [DongYOung Seo] add block size update code
 */
static FERROR
_setXattr (	IN NS_PVNODE				pVnode,
			IN const char*				psName,
			IN const void*				pValue,
			IN unsigned int				dwValueSize,
			IN NS_XATTR_NAMESPACE_ID	dwID,
			IN NS_XATTR_SET_FLAG		dwFlag)
{
	FFatErr				r, rr;
	FFatXAttrInfo		stXAttrInfo;
	t_uint32			udwRetVal;
	FFatNodeStatus		stStat;			// node status to update allocated size 

	_SETXATTR_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(psName);

	// set parameter of FFatXAttrInfo 
	stXAttrInfo.dwCmd		= FFAT_XATTR_CMD_SET;
	stXAttrInfo.psName		= (t_int8*)psName;
	stXAttrInfo.pValue		= (t_int8*)pValue;
	stXAttrInfo.dwSize		= dwValueSize;
	stXAttrInfo.dwSetFlag	= dwFlag;
	stXAttrInfo.dwNSID		= dwID;

	r = FFAT_FSCtl(FFAT_FSCTL_EXTENDED_ATTRIBUTE, _NODE(pVnode), &stXAttrInfo, &udwRetVal);
	if (r == FFAT_OK)
	{
		NS_MarkVnodeMetaDirty(pVnode);
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

		// update block size
		rr = FFAT_GetNodeStatus(_NODE(pVnode), &stStat);
		if (rr == FFAT_OK)
		{
			NS_SetVnodeBlocks(pVnode, stStat.dwAllocSize);
		}
		// don't care error - this is additional operation for set EA
	}

	_SETXATTR_OUT

	return _errnoToNestle(r);
}


/**
 * get an extended attribute value
 * 
 * @param		pVnode			: Vnode
 * @param		psName			: extended attribute name
 * @param		pValue			: extended attribute value
 * @param		dwValueSize		: buffer size of extended attribute value
 * @param		pdwSizeRead		: size of extended attribute value
 * @return		FERROR_NO_ERROR	: Success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		29-JUL-2008 [GwangOk Go] First Writing.
 */
static FERROR
_getXattr (	IN NS_PVNODE				pVnode,
			IN const char*				psName,
			IN const void*				pValue,
			IN unsigned int				dwValueSize,
			IN NS_XATTR_NAMESPACE_ID	dwID,
			OUT unsigned int*			pdwSizeRead)
{
	FFatErr			r;
	FFatXAttrInfo	stXAttrInfo;

	_GETXATTR_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pdwSizeRead);

	// set parameter of FFatXAttrInfo 
	stXAttrInfo.dwCmd		= FFAT_XATTR_CMD_GET;
	stXAttrInfo.psName		= (t_int8*)psName;
	stXAttrInfo.pValue		= (t_int8*)pValue;
	stXAttrInfo.dwSize		= dwValueSize;
	stXAttrInfo.dwSetFlag	= 0;
	stXAttrInfo.dwNSID		= dwID;

	r = FFAT_FSCtl(FFAT_FSCTL_EXTENDED_ATTRIBUTE, _NODE(pVnode), &stXAttrInfo, pdwSizeRead);

	_GETXATTR_OUT

	return _errnoToNestle(r);
}


/**
 * list extended attribute values
 * 
 * @param		pVnode			: Vnode
 * @param		pOutBuf			: extended attribute names
 * @param		dwOutBufSize	: buffer size of extended attribute names
 * @param		pdwSizeRead		: size of extended attribute names
 * @return		FERROR_NO_ERROR	: Success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		29-JUL-2008 [GwangOk Go] First Writing.
 */
static FERROR
_listXattr (	IN NS_PVNODE			pVnode,
				OUT char*				pOutBuf,
				IN unsigned int			dwOutBufSize,
				OUT unsigned int*		pdwSizeRead)
{
	FFatErr			r;
	FFatXAttrInfo	stXAttrInfo;

	_LISTXATTR_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(pdwSizeRead);

	// set parameter of FFatXAttrInfo 
	stXAttrInfo.dwCmd		= FFAT_XATTR_CMD_LIST;
	stXAttrInfo.psName		= (t_int8*)pOutBuf;
	stXAttrInfo.pValue		= NULL;
	stXAttrInfo.dwSize		= dwOutBufSize;
	stXAttrInfo.dwSetFlag	= 0;

	r = FFAT_FSCtl(FFAT_FSCTL_EXTENDED_ATTRIBUTE, _NODE(pVnode), &stXAttrInfo, pdwSizeRead);

	_LISTXATTR_OUT

	return _errnoToNestle(r);
}


/**
 * get an extended attribute value
 * 
 * @param		pVnode			: Vnode
 * @param		psName			: extended attribute name
 * @return		FERROR_NO_ERROR	: Success
 * @return		else			: error
 * @author		GwangOk Go
 * @version		29-JUL-2008 [GwangOk Go] First Writing.
 */
static FERROR
_removeXattr(	IN NS_PVNODE				pVnode,
				IN const char*				psName,
				IN NS_XATTR_NAMESPACE_ID	dwID)
{
	FFatErr			r;
	FFatXAttrInfo	stXAttrInfo;
	t_uint32		udwRetVal;

	_REMOVEXATTR_IN

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(psName);

	// set parameter of FFatXAttrInfo 
	stXAttrInfo.dwCmd		= FFAT_XATTR_CMD_REMOVE;
	stXAttrInfo.psName		= (t_int8*)psName;
	stXAttrInfo.pValue		= NULL;
	stXAttrInfo.dwSize		= 0;
	stXAttrInfo.dwSetFlag	= 0;
	stXAttrInfo.dwNSID		= dwID;

	r = FFAT_FSCtl(FFAT_FSCTL_EXTENDED_ATTRIBUTE, _NODE(pVnode), &stXAttrInfo, &udwRetVal);
	if (r== FFAT_OK)
	{
		NS_MarkVnodeMetaDirty(pVnode);
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

		// no need to update blocks size.
		// removeXattr does not shrink size of EA
	}
	_REMOVEXATTR_OUT

	return _errnoToNestle(r);
}


//=============================================================================
//
//	static utility functions
//


/**
* convert Nestle flag to FFAT flag
*
* @param		dwTFS4Flag		: [IN] Nestle flag
* @param		pConvertDB		: [IN] convert data
* @param		dwCount			: [IN] data count in pConvertDB
* @return		FFAT flag
* @author		DongYoung Seo
* @version		14-AUG-2006 [DongYoung Seo] First Writing.
*/
static t_uint32
_convertFlag(t_uint32 dwNestleFlag, const FlagConvert* pConvertDB, t_int32 dwCount)
{
	t_uint32	dwFFatFlag = 0;
	t_int32		i;

	for (i = 0; i < dwCount; i++)
	{
		if (pConvertDB[i].dwNestleFlag & dwNestleFlag)
		{
			dwFFatFlag |= pConvertDB[i].dwFFatFlag;
		}
	}

	return dwFFatFlag;
}

/**
* revert FFAT flag to Nestle flag
*
* @param		dwFFatFlag		: [IN] ffat flag
* @param		pRevertDB		: [IN] revert data
* @param		dwCount			: [IN] data count in pRevertDB
* @return		Nestle flag
* @author		JeongWoo Park
* @version		23-MAR-2009 [JeongWoo Park] First Writing.
*/
static t_uint32
_revertFlag(t_uint32 dwFFatFlag, const FlagConvert* pRevertDB, t_int32 dwCount)
{
	t_uint32	dwNestleFlag = 0;
	t_int32		i;

	for (i = 0; i < dwCount; i++)
	{
		if (pRevertDB[i].dwFFatFlag & dwFFatFlag)
		{
			dwNestleFlag |= pRevertDB[i].dwNestleFlag;
		}
	}

	return dwNestleFlag;
}


/**
* convert FFAT error number to Nestle errno
*
* @param		dwErrno		: [IN] Nestle flag
* @return		nestle error number
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		04-FEB-2009 [JeongWoo Park] add error bit masking for the error isn't FFAT_OK.
*/
static FERROR
_errnoToNestle(FFatErr dwErrno)
{
	t_int32		dwErr;

	FFAT_ASSERT(dwErrno != FFAT_OK1);
	FFAT_ASSERT(dwErrno != FFAT_OK2);
	FFAT_ASSERT(dwErrno != FFAT_DONE);
	FFAT_ASSERT(dwErrno != FFAT_NO_VALUE);

	if (dwErrno == FFAT_OK)
	{
		return FERROR_NO_ERROR;
	}

	dwErrno = FFAT_BO_INT32(dwErrno & 0x7FFFFFFF);		// masking the MSB & byte ordering

	dwErr = EssBitmap_GetLowestBitOne((t_uint8*)&dwErrno, sizeof(dwErrno));
	FFAT_ASSERT(dwErr >= 0);

	dwErr++;

	FFAT_ASSERT(dwErr < (sizeof(_errnoTable) / sizeof(FERROR)));

	return _errnoTable[dwErr];
}


/**
* generate INODE number from node
*
* INODE number = ((cluster number of DE) << 32) + (DE offset from the beginning of parent)
*
* @param		pNode		: [IN] FFat Node Pointer
* @return		inode number
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static unsigned long long
_getInodeNumber(FFatNode* pNode)
{
	t_uint32	dwCluster;		// cluster of DE start
	t_uint32	dwOffset;		// DE offset from the beginning of parent

	FFAT_ASSERT(pNode);

	dwCluster	= FFAT_GetDeStartCluster(pNode);
	dwOffset	= FFAT_GetDeStartOffset(pNode);

	return _getInodeNumber2(FFAT_GetVol(pNode), dwCluster, dwOffset);
}


/**
* generate INODE number from two INODE
*
* INODE number = ((cluster number of DE) << 32) + (DE offset from the beginning of parent)
*
* @param		pVol		: [IN] Volume Pointer
* @param		dwIno1		: [IN] DE start cluster
* @param		dwIno2		: [IN] DE offset from the beginning of parent
* @return		INODE number
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
* @version		12-JAN-2009 [DongYoung Seo] add code for 4byte INODE number
*/
static unsigned long long
_getInodeNumber2(FFatVol* pVol, t_uint32 dwIno1, t_uint32 dwIno2)
{
	unsigned long long	llCluster;		// DE start cluster
	unsigned long long	llOffset;		// DE offset from the beginning of parent

	FFAT_ASSERT(dwIno1 != 0);		// do not use 0 for i node number (LINUX)

	llCluster	= dwIno1;
	llOffset	= dwIno2;

#ifdef _4BYTE_INODE_NUMBER
	// change code for RFS debug temporarily 
	//	this code will generate 4byte inode number
	// refer to :http://12.52.174.91/wiki/index.php/RFS#How_to_get_unique_inode_number_for_FAT_file_system

	{
		t_uint32		dwSector;			// first sector of cluster
		t_int32			dwSSB;				// bit count for sector size
		t_uint32		dwCSM;				// mask value for cluster size
		const t_int32	dwSizeDEBits = 5;	// bit count for Size of a DE
		FFatErr			r;

		if (llOffset == 0x01)
		{
			// this is root node
			dwSector = 0;
		}
		else
		{
			r = FFAT_GetSectorOfCluster(pVol, (t_uint32)llCluster, &dwSector);
			FFAT_ASSERT(r == FFAT_OK);

			r = FFAT_GetClusterSizeMask(pVol, NULL, &dwCSM);
			FFAT_ASSERT(r == FFAT_OK);

			if (llCluster != 0x01)		// check root
			{
				llOffset = (llOffset & dwCSM);
			}
		}

		dwSSB = FFAT_GetSectorSizeBits(pVol);

    // get index of offset (Bux fix to generate Inode Number over 4byte at 8GB MicroSD(SanDisk))
	  llOffset  = (llOffset >> dwSizeDEBits);   
    llOffset  += dwSector << (dwSSB- dwSizeDEBits);

		FFAT_ASSERT(llOffset <= 0xFFFFFFFF);

		llCluster = 0;			// init cluster
	}
#endif

	return (llCluster << 32) | llOffset;
}


/**
 * setup VNODE from FFAT Node
 *
 * @param		pVCB		: [IN] VCB
 * @param		pVnodeParent: [IN] parent VNODE
 *								NULL : pVnode is a root node
 * @param		pNode		: [IN] FFAT node
 * @param		pdwPerm		: [IN] ACL, may be null
 * @param		dwUID		: [IN] user id,
 * @param		dwGID		: [IN] group id
 * @param		pVnode		: [IN] VNODE, 
								find or create VNODE when *pVnode is NULL
 * @param		pbNew		: [IN] boolean for new or exist, may be NULL
 *									FFAT_TRUE	: already exist VNODE
 *									FFAT_FALSE	: an exist VNODE
 * @return		FFAT_OK		: success
 * @return		FFAT_ENOMEM	: Fail to crate VNODE
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing.
 * @version		22-MAR-2009 [DongYOung Seo] add block size update code
 * @version		JUN-17-2009 [GwangOk Go] add fifo, socket
 */
static FFatErr
_setupVnode(NS_PVCB pVCB, NS_PVNODE pVnodeParent, FFatNode* pNode,
				NS_ACL_MODE wPerm, unsigned int dwUID, unsigned int dwGID,
				NS_PVNODE* ppVnode, BOOL* pbNew)
{
	FFatErr					r;
	FFatNodeStatus			stStat;
	unsigned long long		llVnodeID;			// INODE number
	FILE_SIZE				llSize;
	FILE_ATTR				ftAttr;

	FFAT_ASSERT(pVCB);
	FFAT_ASSERT(ppVnode);
	FFAT_ASSERT(*ppVnode == NULL);
	FFAT_ASSERT(pNode);

	llVnodeID = _getInodeNumber(pNode);

	r = FFAT_GetNodeStatus(pNode, &stStat);
	IF_UK (r < 0)
	{
		return r;
	}

	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_SYMLINK) ? ((stStat.dwAttr & FFAT_NODE_FIFO) == 0) : FFAT_TRUE);
	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_SYMLINK) ? ((stStat.dwAttr & FFAT_NODE_SOCKET) == 0) : FFAT_TRUE);
	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_FIFO) ? ((stStat.dwAttr & FFAT_NODE_SYMLINK) == 0) : FFAT_TRUE);
	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_FIFO) ? ((stStat.dwAttr & FFAT_NODE_SOCKET) == 0) : FFAT_TRUE);
	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_SOCKET) ? ((stStat.dwAttr & FFAT_NODE_SYMLINK) == 0) : FFAT_TRUE);
	FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_SOCKET) ? ((stStat.dwAttr & FFAT_NODE_FIFO) == 0) : FFAT_TRUE);

	switch (stStat.dwAttr & (FFAT_NODE_DIR | FFAT_NODE_SPECIAL_FILES))
	{
	case FFAT_NODE_DIR:
		*ppVnode = NS_FindOrCreateVnodeFromVcb(pVCB, llVnodeID, pVnodeParent,
					(void*)&_fopDir, (void*)&_vopDir, (unsigned int*)pbNew);
		break;

	case FFAT_NODE_SYMLINK:
		*ppVnode = NS_FindOrCreateVnodeFromVcb(pVCB, llVnodeID, pVnodeParent,
					(void*)&_fopSymlink, (void*)&_vopSymlink, (unsigned int*)pbNew);
		break;

	case FFAT_NODE_FIFO:
	case FFAT_NODE_SOCKET:
		*ppVnode = NS_FindOrCreateVnodeFromVcb(pVCB, llVnodeID, pVnodeParent,
					(void*)&_fopFifoSocket, (void*)&_vopFifoSocket, (unsigned int*)pbNew);
		break;

	default:
		*ppVnode = NS_FindOrCreateVnodeFromVcb(pVCB, llVnodeID, pVnodeParent,
					(void*)&_fopFile, (void*)&_vopFile, (unsigned int*)pbNew);
		break;
	}

	if (*ppVnode)
	{
		if (*pbNew == FFAT_TRUE)
		{
			NS_LinkNativeNode(*ppVnode, pNode);

			ftAttr = stStat.dwAttr & FFAT_ATTR_MASK;

			switch (stStat.dwAttr & FFAT_NODE_SPECIAL_FILES)
			{
			case FFAT_NODE_SYMLINK:
				FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_DIR) == 0);
				ftAttr |= NS_FILE_ATTR_LINKED;
				break;

			case FFAT_NODE_FIFO:
				FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_DIR) == 0);
				ftAttr |= NS_FILE_ATTR_FIFO;
				break;

			case FFAT_NODE_SOCKET:
				FFAT_ASSERT((stStat.dwAttr & FFAT_NODE_DIR) == 0);
				ftAttr |= NS_FILE_ATTR_SOCKET;
				break;
			}

			NS_SetVnodeAttr(*ppVnode, ftAttr);
			llSize = (FILE_SIZE)stStat.dwSize;
			NS_SetFileSize(*ppVnode, llSize);

			NS_SetVnodeAcl(*ppVnode, wPerm);
			NS_SetVnodeUid(*ppVnode, dwUID);
			NS_SetVnodeGid(*ppVnode, dwGID);

			_updateVnodeTime(*ppVnode, &stStat);

			NS_SetVnodeLinkCnt(*ppVnode, _VNODE_LINK_COUNT);

			FFAT_ASSERT(FFAT_NodeGetInode(pNode) == NULL);

			FFAT_NodeSetInode(pNode, *ppVnode);

			NS_SetVnodeBlocks(*ppVnode, stStat.dwAllocSize);
		}
// debug begin
		else
		{
			FFAT_ASSERT(NS_GetVnodeIndex(*ppVnode) == llVnodeID);

#if 0	// assert 발생시 풀어줘라
			// this is an exist VNODE
			// check the INODE number is changed
			if (NS_GetVnodeIndex(*ppVnode) != llVnodeID)
			{
				NS_ChangeVnodeIndex(*ppVnode, llVnodeID);
				FFAT_ASSERT(NS_GetVnodeIndex(*ppVnode) == llVnodeID);
			}
#endif
		}
// debug end

		return FFAT_OK;
	}

	return FFAT_ENOMEM;
}

/**
* Recovery the node of dirty-size state.
* This is called at following case
*
* 1) This is called after lookup, if the VNODE is new VNODE and volume is not read-only.
*    If volume is read-only, recovery can not be performed. In this case,
*    node has dirty-size-rdonly state.
* 2) If volume is mounted with read-only, some VNODE is created and then
*    remount operation is called to remove read-only attribute of volume,
*    the recovery operation about dirty-size-rdonly node must be done
*    before write/expand/rename/unlink/sync.
*
* @param		pVnode		: [IN] VNODE pointer
* @author		JW Park
* @version		29-OCT-2009 [JW Park] First Writing.
*/
static FFatErr
_recoveryDirtySizeNode(NS_PVNODE pVnode)
{
	FFatErr		r = FFAT_OK;
	FFatNode*	pNode;

	FFAT_ASSERT(pVnode);
	FFAT_ASSERT(FFAT_VolIsReadOnly(FFAT_GetVol(_NODE(pVnode))) == FFAT_FALSE);
	FFAT_ASSERT(FFAT_NodeIsFile(_NODE(pVnode)) == FFAT_TRUE);
	FFAT_ASSERT(FFAT_NodeIsDirtySize(_NODE(pVnode)) == FFAT_TRUE);

	pNode = _NODE(pVnode);

	r = FFAT_ChangeSize(pNode, FFAT_GetSize(pNode), FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE);
	IF_LK (r == FFAT_OK)
	{
		NS_MarkVnodeMetaDirty(pVnode);	// set VNODE dirty
		NS_MarkDirtyVcb(NS_GetVcbFromVnode(pVnode));

		FFAT_ASSERTP((FFAT_NodeIsDirtySizeRDOnly(pNode) == FFAT_FALSE), (_T("Fail to recovery about dirty-size-rdonly")));
	}

	return r;
}


/**
 * get DOS date from Nestle Date
 *
* @param		pTime		: [IN] time pointer
 * @author		DongYoung Seo
 * @version		11-OCT-2007 [DongYoung Seo] First Writing.
 * @version		04-NOV-2008 [DongYoung Seo] add checking routine DOS Base Year1980
 */
static t_uint16
_getDosDate(PSYS_TIME pDate)
{
	t_uint16		wDate;

	FFAT_ASSERT(pDate);

	wDate = (pDate->wMonth << 5) | (pDate->wDay);

	if (pDate->wYear >= 1970)
	{
		wDate |= ((pDate->wYear - 1970) << 9);
	}

	return wDate;
}


/**
* get DOS time from Nestle time
*
* @param		pTime		: [IN] time pointer
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static t_uint16
_getDosTime(PSYS_TIME pTime)
{
	return (pTime->wHour << 11) | (pTime->wMinute << 5) | (pTime->wSecond >> 1);
}


/**
* get Nestle time from Dos time
*
* @param		wTime		: [IN] DOS time pointer
* @param		pTime		: [IN] Nestle time pointer
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static void
_getNestleTime(t_uint16 wTime, t_uint8 bTimeTenth, PSYS_TIME pTime)
{
	FFAT_ASSERT(pTime);

	pTime->wHour		= wTime >> 11;
	pTime->wMinute		= (wTime >> 5) & 0x3f;
	pTime->wSecond		= ((wTime & 0x1F) << 1) + ((bTimeTenth / 100) & 0x01);
	pTime->wMilliseconds = (bTimeTenth % 100) * 10;
}


/**
* get Nestle date from Dos date
*
* @param		wTime		: [IN] DOS date pointer
* @param		pTime		: [IN] Nestle date pointer
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static void
_getNestleDate(t_uint16 wDate, PSYS_TIME pTime)
{
	FFAT_ASSERT(pTime);

	pTime->wYear	= (wDate >> 9) + 1970;
	pTime->wMonth	= (wDate >> 5) & 0x0F;
	pTime->wDay		= wDate & 0x1F;
}


/**
* get Nestle date and time from Dos date and time
*
* @param		wTime		: [IN] DOS time pointer
* @param		pTime		: [IN] Nestle time pointer
* @author		DongYoung Seo
* @version		11-OCT-2007 [DongYoung Seo] First Writing.
*/
static void
_getNestleDateTime(t_uint32 dwTime, t_uint32 dwTimeTenth, PSYS_TIME pTime)
{
	_getNestleDate((t_uint16)(dwTime >> 16), pTime);
	_getNestleTime((t_uint16)(dwTime & 0xFFFF), (t_uint8)dwTimeTenth, pTime);
}


/**
* readdir + get a node
* 
* [NOTICE]
* Be careful about consistency between pNode and VNODE in Nestle.
* The caller of this function must check whether there is existed VNODE.
*
* @param		pVnodeParent	: parent directory 
* @param		pFcb			: File Control Block
* @param		dwOffset		: readdir start offset
* @param		dwAttribute		: Attribute To delete ?
* @return		positive : number of read byte
* @return		negative : FFAT Error number
* @author		DongYoung Seo
* @version		27-NOV-2007 [DongYoung Seo] First Writing.
*/
static t_int32 
_readdirGetNode(NS_PVNODE pVnodeParent, NS_PFCB pFcb,
					t_uint32 dwOffset, FFatNode* pNode)
{
	FFatErr						r ;
	FFatReaddirGetNodeInfo		stRGNI;

	FFAT_ASSERT(pVnodeParent);
	FFAT_ASSERT(pFcb);

	stRGNI.dwOffset		= dwOffset;
	stRGNI.dwOffsetNext	= 0;
	stRGNI.psName		= NULL;		// no need to get name
	stRGNI.dwNameLen	= 0;		// set storage size
	stRGNI.pNode		= pNode;

	r = FFAT_FSCtl(FFAT_FSCTL_READDIR_GET_NODE, _NODE(pVnodeParent), &stRGNI, NULL);
	IF_UK (r != FFAT_OK)
	{
		return r;
	}

	return stRGNI.dwOffsetNext - stRGNI.dwOffset;		// return read byte
}


/**
* file system control for Directory Entry Cache
* 
* @param		dwCmd		: [IN] command for file system control
* @param		pNSDEC		: [IN] pointer of directory entry cache structure
* @return		FFAT_OK		: Success
* @return		else		: error
* @author		DongYoung Seo
* @version		25-JAN-2008 [DongYoung Seo] First Writing.
*/
static FFatErr
_fsctlDEC(unsigned int dwCmd, NS_PFS_DE_CACHE pNSDEC)
{
	FFatErr			r;
	FFatDECFlag		dwFlag;		// flag for free cluster cache
	FFatNode*		pNode;

	FFatDirEntryCacheInfo	stDECI;

	FFAT_ASSERT(pNSDEC);

#ifdef FFAT_STRICT_CHECK
	if (pNSDEC == NULL)
	{
		FFAT_LOG_PRINTF((_T("Invalid parameter")));
		return FFAT_EINVALID;
	}
#endif

	pNode	= _NODE(NS_GetVnodeFromFcb(pNSDEC->pFcb));

	if (dwCmd == NS_FSCTL_NATIVE_SET_DEC)
	{
		dwFlag			= FFAT_DEC_SET;
		stDECI.pBuff	= (t_int8*)pNSDEC->pBuff;
		stDECI.dwSize	= pNSDEC->dwSize;

		r = FFAT_FSCtl(FFAT_FSCTL_DIR_ENTRY_CACHE, pNode, &dwFlag, &stDECI);
	}
	else if (dwCmd == NS_FSCTL_NATIVE_RELEASE_DEC)
	{
		dwFlag = FFAT_DEC_RELEASE;

		r = FFAT_FSCtl(FFAT_FSCTL_DIR_ENTRY_CACHE, pNode, &dwFlag, NULL);
	}
	else if (dwCmd == NS_FSCTL_NATIVE_GET_DEC_INFO)
	{
		dwFlag	= FFAT_DEC_GET_INFO;

		r = FFAT_FSCtl(FFAT_FSCTL_DIR_ENTRY_CACHE, pNode, &dwFlag, &stDECI);
		if (r == FFAT_OK)
		{
			pNSDEC->pBuff	= (char*)stDECI.pBuff;
			pNSDEC->dwSize	= stDECI.dwSize;
		}
	}
	else
	{
		FFAT_ASSERT(0);
		r = FFAT_ENOSUPPORT;
	}

	return r;
}


/**
* update file time of a VNODE from FFAT status
* 
* @param		pVnode		: [IN] VNODE pointer
* @param		pStat		: [IN] FFAT Node  status
* @return		FFAT_OK		: Success
* @return		else		: error
* @author		DongYoung Seo
* @version		25-JAN-2008 [DongYoung Seo] First Writing.
* @version		24-NOV-2008 [DongYoung Seo] change to update Access date
*/
static void
_updateVnodeTime(NS_PVNODE pVnode, FFatNodeStatus* pStat)
{
	FILE_TIME		ftCTime;		// creation time
	FILE_TIME		ftMTime;		// modification time
	FILE_TIME		ftATime;		// access time

	FFAT_ASSERT(pStat);
	FFAT_ASSERT(pVnode);

	FFAT_MEMSET(&ftCTime, 0x00, sizeof(FILE_TIME));
	FFAT_MEMSET(&ftMTime, 0x00, sizeof(FILE_TIME));
	FFAT_MEMSET(&ftATime, 0x00, sizeof(FILE_TIME));

	_getNestleDateTime(pStat->dwCTime, pStat->dwCTimeTenth, &ftCTime);
	_getNestleDateTime(pStat->dwMTime, 0, &ftMTime);
	_getNestleDateTime(pStat->dwATime, 0, &ftATime);

	NS_SetFileTimes(pVnode, &ftCTime, &ftATime, &ftMTime);

	return;
}


/**
* get a free INODE number for open unlinked node
*
* this function have a potential vulnerability 
*	when overflow of NewID and it meets the lowest previous open unlinked node
* But I think it is impossible in real world. the count is 68719476720.
*
* @return		new ID for open unlink node
* @author		DongYoung Seo
* @version		OCT-31-02008 [DongYoung Seo] First Writing.
*/
static unsigned long long
_getNewIDForOpenUnlink(void)
{

#ifdef _4BYTE_INODE_NUMBER

	return	0xFFFFFFFF;				// just return fixed INODE number for open unlinked node

#else

	#define		_BASE_CLUSTER_NO	0x10000000					// not used cluster no for FAT spec

	static t_uint32		dwBaseCluster = _BASE_CLUSTER_NO;		// BAD Cluster Nod
	static t_uint32		dwOffset = 0;							// base offset
	unsigned long long	llNewID;

	FFAT_GetSpinLock();		// don't care lock error

	llNewID = _getInodeNumber2(dwBaseCluster, dwOffset);

	dwOffset++;
	if (dwOffset == 0xFFFFFFFF)
	{
		dwOffset = 0;
		dwBaseCluster++;

		if (dwBaseCluster == 0xFFFFFFFF)
		{
			// can we reach here?
			dwBaseCluster = _BASE_CLUSTER_NO;
		}
	}

	FFAT_PutSpinLock();	// don't care lock error

	return llNewID;
#endif
}


/**
 * This function gets logical device(partition) information
 * 
 *
 * @param		pLDevInfo	: [OUT] logical device information storage
 * @param		pVcb		: [IN] volume pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] First Writing.
 * @version		DEC-09-2008 [GwangOk Go] add block info to logical device
 */
static void
_getLDevStatus(FFatLDevInfo* pLDevInfo, NS_PVCB pVcb)
{
	NS_PLOGICAL_DISK		pLogDisk;
	NS_DISK_FLAGS			dwFlags;

	FFAT_ASSERT(pLDevInfo);
	FFAT_ASSERT(pVcb);

	pLogDisk = NS_GetLogicalDisk(pVcb);

	FFAT_ASSERT(pLogDisk);

	// get block info of logical device
	NS_GetDiskInfo(pLogDisk,
				(unsigned int*)&pLDevInfo->dwDevSectorCount,
				(unsigned int*)&pLDevInfo->dwDevSectorSize,
				(unsigned int*)&pLDevInfo->dwDevStartSector,
				(unsigned int*)&dwFlags);

	pLDevInfo->dwFlag = _convertFlag(dwFlags, _pDeviceFlagTable,
								(sizeof(_pDeviceFlagTable) / sizeof(FlagConvert)));

	pLDevInfo->wAlignBasis		= 0;

	if (pLDevInfo->wAlignBasis < FFAT_FORMAT_ALIGN)
	{
		// default 4 sector align
		pLDevInfo->wAlignBasis = FFAT_FORMAT_ALIGN;
	}

	pLDevInfo->dwDevSectorSizeBits	= EssMath_Log2(pLDevInfo->dwDevSectorSize);

	return;
}


/**
 * set block info of logical device info structure
 * 
 * @param		pLDevInfo	: [IN] logical device information structure
 * @param		dwBlockSize	: [IN] block size
 * @return		void
 * @author		GwangOk Go
 * @version		MAR-05-2009 [GwangOk Go] First Writing.
 * @version		APR-09-2009 [DongYoung Seo] change assert statement.
 *							from FFAT_ASSERT(pLDevInfo->dwDevSectorCount >= 512);
 *							to FFAT_ASSERT(pLDevInfo->dwDevSectorSize >= 512);
 */
static void
_setLDevBlockInfo(FFatLDevInfo* pLDevInfo, t_int32 dwBlockSize)
{
	FFAT_ASSERT(pLDevInfo);
	FFAT_ASSERT(pLDevInfo->dwDevSectorSize >= 512);	// minimum sector size

	if (pLDevInfo->dwBlockSize == dwBlockSize)
	{
		return;
	}

	pLDevInfo->dwBlockSize		= dwBlockSize;
	pLDevInfo->dwBlockSizeBits	= EssMath_Log2(pLDevInfo->dwBlockSize);

	if (pLDevInfo->dwBlockSizeBits >= pLDevInfo->dwDevSectorSizeBits)
	{
		pLDevInfo->dwBlockCount		= pLDevInfo->dwDevSectorCount >> (pLDevInfo->dwBlockSizeBits - pLDevInfo->dwDevSectorSizeBits);
		pLDevInfo->dwStartBlock		= pLDevInfo->dwDevStartSector >> (pLDevInfo->dwBlockSizeBits - pLDevInfo->dwDevSectorSizeBits);
	}
	else
	{
		pLDevInfo->dwBlockCount		= pLDevInfo->dwDevSectorCount << (pLDevInfo->dwDevSectorSizeBits - pLDevInfo->dwBlockSizeBits);
		pLDevInfo->dwStartBlock		= pLDevInfo->dwDevStartSector << (pLDevInfo->dwDevSectorSizeBits - pLDevInfo->dwBlockSizeBits);
	}

	return;
}


#ifdef FFAT_BLOCK_IO
	/**
	 * calculate optimal block size regarding sector size / cluster size / sector align of data area
	 * 
	 * @param		dwOldBlockSize	: [IN] old block size
	 * @param		dwSectorSize	: [IN] FAT sector size
	 * @param		dwClusterSize	: [IN] cluster size
	 * @param		dwFirstDataSector	: [IN] first FAT sector number of data area
	 * @return		t_int32			: new block size
	 * @author		GwangOk Go
	 * @version		MAR-05-2009 [GwangOk Go] First Writing.
	 */
	static t_int32
	_getOptimalBlockSize(t_int32 dwOldBlockSize, t_int32 dwSectorSize,
							t_int32 dwClusterSize, t_uint32 dwFirstDataSector)
	{
		t_int32	dwBlockSize;

		dwBlockSize = ESS_GET_MAX(dwSectorSize, ESS_GET_MIN(dwOldBlockSize, dwClusterSize));

		FFAT_ASSERT(dwSectorSize <= dwBlockSize);
		FFAT_ASSERT(dwBlockSize <= 4096);

		if (dwSectorSize < dwBlockSize)
		{
			t_int32			dwTemp;

			// check first data sector is aligned by block boundary
			dwTemp = dwFirstDataSector & ((dwBlockSize / dwSectorSize) - 1);

			if (dwTemp != 0)
			{
				dwBlockSize = dwSectorSize;

				// search aligned boundary & calculate block size
				while ((dwTemp & 0x01) == 0)
				{
					dwBlockSize = dwBlockSize << 1;
					dwTemp		= dwTemp >> 1;
				}

				FFAT_ASSERT(dwBlockSize <= 4096);	// maximum block size
			}
		}

		FFAT_ASSERT(dwSectorSize <= dwBlockSize);
		FFAT_ASSERT(dwBlockSize <= dwClusterSize);

		return dwBlockSize;
	}
#endif


/**
 * allocate resource for Volume
 * 
 * @return		NULL			: memory allocation fail, not enough memory
 * @return		else			: success
 * @author		DongYoung Seo
 * @version		18-APR-2008 [DongYoung Seo] First Writing.
 */
static FFatVol*
_allocVol(void)
{
	FFatVol*	pVol;

#ifdef FFAT_DYNAMIC_ALLOC
	pVol = (FFatVol*)_MALLOC(sizeof(FFatVol), ESS_MALLOC_NONE);
#else
	pVol = _getFreeVol();
#endif

	return pVol;
}


/**
 * free resource for Volume
 * 
 * @param		pVol		: volume pointer (may be NULL)
 * @author		DongYoung Seo
 * @version		18-APR-2008 [DongYoung Seo] First Writing.
 */
static void
_freeVol(FFatVol* pVol)
{
	if (pVol)
	{
#ifdef FFAT_DYNAMIC_ALLOC
		_FREE(pVol, sizeof(FFatVol));
#else
		_releaseVol(pVol);
#endif
	}
}


/**
 * allocate resource for Node
 * 
 * @return		NULL			: memory allocation fail, not enough memory
 * @return		else			: success
 * @author		DongYoung Seo
 * @version		18-APR-2008 [DongYoung Seo] First Writing.
 */
static FFatNode*
_allocNode(void)
{
	FFatNode*	pNode;

#ifdef FFAT_DYNAMIC_ALLOC
	pNode = (FFatNode*)_MALLOC(sizeof(FFatNode), ESS_MALLOC_NONE);
#else
	pNode = _getFreeNode();
#endif

	FFAT_DEBUG_ALVFS_PRINTF((_T("Alloc FFatNode:0x%X\n"), (t_uint32)pNode));

	FFAT_ResetNodeStruct(pNode);

	return pNode;
}

/**
 * free resource for Node
 * 
 * @param		pNode		: [IN] node pointer, may be NULL
 * @author		DongYoung Seo
 * @version		18-APR-2008 [DongYoung Seo] First Writing.
 */
static void
_freeNode(FFatNode* pNode)
{
	if (pNode)
	{
		FFAT_DEBUG_ALVFS_PRINTF((_T("Free FFatNode:0x%X\n"), (t_uint32)pNode));

		FFAT_ASSERT((((Node*)pNode)->wSig == NODE_SIG_RESET) ? FFAT_TRUE : (((AddonNode*)(&((Node*)pNode)->stAddon))->pGFS == NULL));

#ifdef FFAT_DYNAMIC_ALLOC
		_FREE(pNode, sizeof(FFatNode));
#else
		_releaseNode(pNode);
#endif
	}
}


#ifndef FFAT_DYNAMIC_ALLOC
	/**
	 * initializes volume storage
	 *	allocates memory for volumes
	 * 
	 * @return		FFAT_OK			: Success
	 * @return		FFAT_ENOEMEM	: Not enough memory
	 * @return		else			: general error, refer to the ffat_errno.h
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_initVolStorage(void)
	{
		int		i;

		_pVolStorage = (_VolStorage*)_MALLOC((sizeof(_VolStorage) * FFAT_MAX_VOL_COUNT), ESS_MALLOC_NONE);
		IF_UK (_pVolStorage == NULL)
		{
			return FFAT_ENOMEM;
		}

		ESS_LIST_INIT(&_slFreeVolStorage);

		for (i = 0; i < FFAT_MAX_VOL_COUNT; i++)
		{
			ESS_LIST_INIT(&_pVolStorage[i].slFree);
			ESS_LIST_ADD_HEAD(&_slFreeVolStorage, &_pVolStorage[i].slFree);
		}

		return FFAT_OK;
	}


	/**
	 * releases memory for volume
	 * 
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static void
	_terminateVolStorage(void)
	{
		IF_UK (_pVolStorage)
		{
			FFAT_ASSERT(EssList_Count(&_slFreeVolStorage) == FFAT_MAX_VOL_COUNT);
			_FREE(_pVolStorage, (sizeof(_VolStorage) * FFAT_MAX_VOL_COUNT));
		}
	}


	/**
	 * get a free volume storage
	 * 
	 * @return		NULL			: there is no free volume storage, fail to get/put lock
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatVol*
	_getFreeVol(void)
	{
		_VolStorage*	pStorage = NULL;
		EssList*		pList;
		FFatErr			r;

		r = FFAT_GetSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return NULL;
		}

		IF_UK (ESS_LIST_IS_EMPTY(&_slFreeVolStorage) == ESS_TRUE)
		{
			FFAT_ASSERT(0);
			// No more free volume structure
			// need to increase FFAT_VOL_COUNT_MAX
			goto out;
		}

		pList = ESS_LIST_GET_HEAD(&_slFreeVolStorage);
		ESS_LIST_DEL(&_slFreeVolStorage, pList->pNext);

		pStorage = ESS_GET_ENTRY(pList, _VolStorage, slFree);

	out:
		r = FFAT_PutSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to release spin lock")));
			if (pStorage)
			{
				_releaseVol(&pStorage->stVol);
			}
			return NULL;
		}

		if (pStorage)
		{
			return &pStorage->stVol;
		}
		else
		{
			return NULL;
		}
	}


	/**
	 * release a free volume storage and add it to free list
	 * 
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EPANIC		: fail to get/put lock
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_releaseVol(FFatVol* pVol)
	{
		_VolStorage*	pStorage;
		FFatErr			r;

		FFAT_ASSERT(pVol);

		pStorage = ESS_GET_ENTRY(pVol, _VolStorage, stVol);

		r = FFAT_GetSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return FFAT_EPANIC;
		}

		ESS_LIST_ADD_HEAD(&_slFreeVolStorage, &pStorage->slFree);

		r = FFAT_PutSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return FFAT_EPANIC;
		}

		return FFAT_OK;
	}


	/**
	 * initializes node storage
	 *	allocates memory for nodes
	 * 
	 * @return		FFAT_OK			: Success
	 * @return		FFAT_ENOEMEM	: Not enough memory
	 * @return		else			: general error, refer to the ffat_errno.h
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_initNodeStorage(void)
	{
		int		i;

		FFAT_DEBUG_NODE_STORAGE_PRINTF((_T("Node Count : %d\n"), FFAT_MAX_NODE_COUNT));

		_pNodeStorage = (_NodeStorage*)_MALLOC((sizeof(_NodeStorage) * FFAT_MAX_NODE_COUNT), ESS_MALLOC_NONE);
		IF_UK (_pNodeStorage == NULL)
		{
			return FFAT_ENOMEM;
		}

		ESS_LIST_INIT(&_slFreeNodeStorage);

		for (i = 0; i < FFAT_MAX_NODE_COUNT; i++)
		{
			ESS_LIST_INIT(&_pNodeStorage[i].slFree);
			ESS_LIST_ADD_HEAD(&_slFreeNodeStorage, &_pNodeStorage[i].slFree);
		}

		return FFAT_OK;
	}


	/**
	 * releases memory for node
	 * 
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static void
	_terminateNodeStorage(void)
	{
		IF_UK (_pNodeStorage)
		{
			FFAT_ASSERT(EssList_Count(&_slFreeNodeStorage) == FFAT_MAX_NODE_COUNT);
			_FREE(_pNodeStorage, (sizeof(_NodeStorage) * FFAT_MAX_NODE_COUNT_MAX));
		}
	}


	/**
	 * get a free node storage
	 * 
	 * @return		NULL			: there is no free node storage, fail to get/put lock
	 * @return		else			: success
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatNode*
	_getFreeNode(void)
	{
		_NodeStorage*	pStorage = NULL;
		EssList*		pList;
		FFatErr			r;

		r = FFAT_GetSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return NULL;
		}

		IF_UK (ESS_LIST_IS_EMPTY(&_slFreeNodeStorage) == ESS_TRUE)
		{
			FFAT_ASSERT(0);
			// No more free node structure
			// need to increase FFAT_NODE_COUNT_MAX
			goto out;
		}

		pList = ESS_LIST_GET_HEAD(&_slFreeNodeStorage);
		ESS_LIST_DEL(&_slFreeNodeStorage, pList->pNext);

		pStorage = ESS_GET_ENTRY(pList, _NodeStorage, slFree);

	out:
		r = FFAT_PutSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to release spin lock")));
			if (pStorage)
			{
				_freeNode(&pStorage->stNode);
			}
			return NULL;
		}

		if (pStorage)
		{
			FFAT_DEBUG_NODE_STORAGE_PRINTF((_T("NODE ALLOC : 0x%X, Alloc/Released:%d/%d(diff:%d)\n"),
									&pStorage->stNode, ++_dwNodeAlloc, _dwNodeRelease, (_dwNodeAlloc - _dwNodeRelease)));

			return &pStorage->stNode;
		}
		else
		{
			return NULL;
		}
	}


	/**
	 * release a free node storage and add it to free list
	 * 
	 * @return		FFAT_OK			: success
	 * @return		FFAT_EPANIC		: fail to get/put lock
	 * @author		DongYoung Seo
	 * @version		18-APR-2008 [DongYoung Seo] First Writing.
	 */
	static FFatErr
	_releaseNode(FFatNode* pNode)
	{
		_NodeStorage*	pStorage;
		FFatErr			r;

		FFAT_ASSERT(pNode);

		FFAT_DEBUG_NODE_STORAGE_PRINTF((_T("NODE RELEASE : 0x%X, Alloc/Released:%d/%d(diff:%d)\n"),
									pNode, _dwNodeAlloc, ++_dwNodeRelease, (_dwNodeAlloc - _dwNodeRelease)));

		pStorage = ESS_GET_ENTRY(pNode, _NodeStorage, stNode);

		r = FFAT_GetSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return FFAT_EPANIC;
		}

		ESS_LIST_ADD_HEAD(&_slFreeNodeStorage, &pStorage->slFree);

		r = FFAT_PutSpinLock();
		IF_UK (r != FFAT_OK)
		{
			FFAT_DEBUG_PRINTF((_T("Fail to get spin lock")));
			return FFAT_EPANIC;
		}

		return FFAT_OK;
	}

#endif	// end of #ifndef FFAT_DYNAMIC_ALLOC


// debug begin
#ifdef FFAT_DEBUG

	#ifdef _DEBUG_FFAT_NESTLE
		/**
		* convert a UNICODE string to a MULTIBYTE string and return pointer of MULTIBYTE
		*
		* Caution : this function is not  reentrant
		* 
		* @author		DongYoung Seo
		* @version		01-NOV-2008 [DongYoung Seo] First Writing.
		*/
		char*
		_w2a(t_wchar* psName, t_int32 dwLen)
		{
			static char		psmbName[(FFAT_FILE_NAME_MAX_LENGTH + 1)* 2];
			t_int32		i;

			for (i = 0; i < dwLen; i++)
			{
				psmbName[i] = (char)psName[i];
			}
			psmbName[i] = '\0';

			return psmbName;
		}
	#endif
#endif
// debug end



