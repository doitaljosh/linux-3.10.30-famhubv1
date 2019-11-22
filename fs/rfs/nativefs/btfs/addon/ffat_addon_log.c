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
* @file		ffat_addon_log.c
* @brief	log manager with write ahead log, WAL, scheme
* @author	Qing Zhang
* @version	OCT-20-2006 [Qing Zhang]
* @version	AUG-15-2007 [DongYoung Seo] rewrite for LLW on independent day
* @see		None
*/

//
//	Terminology
//		LLW		: Lazy Log Writer
//		LL		: Lazy Log
//

///////////////////////////////////////////////////////////////////////////////
//
//
//		basic log entry description
//
//		Normal Log Entry
//
//		  |------------|
//		  | log header |		<== sequence number ,log version etc..
//		  | sub log    |
//		  | sub log    |
//		  | .....      |
//		  | log tail   |		<== this is for log confirm
//		  |-------------
//
//		Lazy Log Entry
//
//		  |------------|
//		  | log header |		<== first log, sequence number ,log version etc..
//		  | sub logs   |
//		  | sub log    |		<= end of first log
//		  | log header |		<= second log, sequence number ,log version etc..
//		  | sub logs   |
//		  | sub log    |		<= end of second log
//		  | log header |		<= nth log, sequence number ,log version etc..
//		  | sub logs   |
//		  | sub log    |		<= end of nth log
//		  | log confirm|		<= confirm log for LLW
//		  --------------
//
//		&&&& Caution &&&&
//		- Lazy log Entry does not have log tail on each normal log entry
//			- this reduces log size (slightly, amount of sizeof(LogTail))
//
//		Log Slot Update Policy
//			pLI->wCurSlot has the last valid log entry.
//			it must be updated before new log write.
//
//		Log for Rename
//			there is a exception case.
//			All data for recovery must be stored a log entry but name for rename may be longer than an entry.
//			Log module has an supplementary entry at next of the last entry to store name.
//			Caution. if there is two log entry at log file, the previous on must be synchronized before 2nd log written.
//
//		Log policy for there is not enough space for log file
//			Mount operation will be successfully ended even if there is not enough free space for log file.
//			And log feature is disabled for current volume.
//			Log feature will not be enabled when some files are deleted and volume has enough space.
//				- why ?
//					User gets error when user want to write some amount of data and they delete exact size of file.
//					when log file is created automatically.
//					(사용자가 data write를 위해 필요한 크기만큼 파일을 지운 후 다시 write를 할경우 
//						자동으로 log file이 생성되어 space를 사용하게 되면 fail될수 있다.)
//
///////////////////////////////////////////////////////////////////////////////

#include "ess_list.h"
#include "ess_math.h"
#include "ess_bitmap.h"

#include "ffat_common.h"
#include "ffat_node.h"
#include "ffat_file.h"
#include "ffat_dir.h"
#include "ffat_misc.h"
#include "ffat_share.h"

#include "ffat_addon_misc.h"
#include "ffat_addon_types_internal.h"
#include "ffat_addon_log.h"
#include "ffat_addon_xattr.h"
#include "ffat_addon_format.h"

#include "ffatfs_api.h"

#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ADDON_LOG)

// debug begin

#ifdef FFAT_DEBUG
	#define _LOG_REPLAY
#else
	#undef _LOG_DEBUG_
#endif

#define _LOG_REPLAY		// change Log replay as default option

//#define _LOG_DEBUG_

// debug end

#if defined(_LOG_DEBUG_) || defined(FFAT_DEBUG_FILE)
	#define _PRINT_LOG(_pVol, _pLog, _msg)		_printLog(_pVol, _pLog, _msg)
	#define _PRINT_SUBLOG(_pVol, _pSL)			_printSubLog(_pVol, _pSL)
#else
	#define _PRINT_LOG(_pVol, _pLog, _msg)		((void)0)
	#define _PRINT_SUBLOG(_pVol, _subLog)		((void)0)
#endif

#define _LOG_VERSION			((t_uint32)0x3C5A0301)	// format:	1st 2 bytes - signature
														//			2nd 2 bytes - major.minor
														// binary: 3C (0011 1100), 5A (0101 1010)
														// 0200: TFS4 2.0.0
														// 0201: TFS4 2.0.1
														// 0202: TFS4 2.0.2
														// 0300: BTFS 1.0.0
														// 0301: BTFS 1.0.1
#define _LLW_CONFIRM			(~_LOG_VERSION)			// confirm log for LLW

#define _MAX_SEQNUM				0xFFFFFFFFUL			// this value will be incremented by one whenever log record is recorded
														// (1 << sizeof(t_uint32) << 2) - 1
														// (t_uint32)-1
#define _MAX_LOG_TYPE			19						// log type max

#define _LOG_SIZE_OF_SLOT		FFAT_SECTOR_SIZE		// the size of a log entry(slot)
#define _LLW_SLOT_NOT_ASSIGNED	-1						// log slot is not assigned

#define _LOG_FILE_UID			FFAT_ADDON_LOG_FILE_UID
#define _LOG_FILE_GID			FFAT_ADDON_LOG_FILE_GID
#define _LOG_FILE_PERM			FFAT_ADDON_LOG_FILE_PERMISSION

#ifdef FFAT_NO_LOG_NO_SYNC
	#define _META_SYNC_FOR_NO_LOG		FFAT_CACHE_NONE
#else
	#define _META_SYNC_FOR_NO_LOG		FFAT_CACHE_SYNC
#endif

// get log main
#define _LOG_MAIN()						((LogMain*)_pLogMain)

// get log information from volume
#define _LOG_INFO(_pVol)				(VOL_ADDON(_pVol)->pLI)
#define _NODE_OUINFO(_pNode)			(NODE_ADDON(_pNode)->stOUI)

#define _NODE_OUEI(_pNode)				(_NODE_OUINFO(_pNode).dwOUEntryIndex)
#define _NODE_EAEI(_pNode)				(_NODE_OUINFO(_pNode).dwEAEntryIndex)

// get sub log from log header
//	_pLH	: Log Header
#define _GET_SUBLOG(_pLH)				(SubLog*)((t_int8*)(_pLH) + sizeof(LogHeader))

// get log tail from a log
//	_pLH	: Log Header
#define _GET_LOG_TAIL(_pLH)				(LogTail*)((t_int8*)(_pLH) + (_pLH)->uwUsedSize - \
											sizeof(LogTail))

// get new log sequence
//	pLI = LogInfo pointer
#define _UPDATE_LOG_SEQ(_pLI)			((_pLI)->udwSeqNum)++;
#define _UPDATE_LOG_SEQ_UNDO(_pLI)		((_pLI)->udwSeqNum)--;

// get new log slot
//	_pLI	= LogInfo pointer,
//	_dwC	= written log slot count
#define _UPDATE_SLOT(_pLI, _dwC)	do {	\
										((_pLI)->wCurSlot) = (t_int16)((_pLI)->wCurSlot) + _dwC;		\
										if (((_pLI)->wCurSlot) >= LOG_MAX_SLOT) ((_pLI)->wCurSlot) = 0;	\
									} while (0);

#define _UPDATE_SLOT_UNDO(_pLI)	do {											\
									if ((_pLI)->wCurSlot == 0)					\
									{											\
										(_pLI)->wCurSlot = (LOG_MAX_SLOT - 1);	\
									}											\
									else										\
									{											\
										((_pLI)->wCurSlot)--;					\
									}											\
								} while (0);

#define _IS_LOGGING_ENABLED(_pVol)		FFAT_IS_LOGGING_ENABLED(_pVol)

// minimum size for a FAT update log
#define _MIN_FAT_SUBLOG_SIZE			(sizeof(SubLogHeader) + sizeof(SubLogFat) + sizeof(FFatVCE))
#define	_RSVD_SIZE_ALLOCATE_FAT			_MIN_FAT_SUBLOG_SIZE
#define	_RSVD_SIZE_DEALLOCATE_FAT		_MIN_FAT_SUBLOG_SIZE
#define _RSVD_SIZE_CREATE_DE			(sizeof(SubLogHeader) + sizeof(SubLogCreateDe))
#define _RSVD_SIZE_UPDATE_DE			(sizeof(SubLogHeader) + sizeof(SubLogUpdateDe))
#define _RSVD_SIZE_DELETE_DE			(sizeof(SubLogHeader) + sizeof(SubLogDeleteDe))
#define _RSVD_SIZE_EA					(sizeof(SubLogHeader) + sizeof(SubLogEA))
#define _RSVD_SIZE_UPDTE_ROOT_EA		(sizeof(SubLogHeader) + sizeof(SubLogUpdateRootEA))

#define _RSVD_SIZE_LOG_TAIL				(sizeof(LogTail))

// check current log is stored within sector
//	_wUsed	: used byte count
//	_wNew	: current log size
//	_wRsvd	: reserved size for next logs
#define _IS_OUT_OF_RANGE(_pVol, _wUsed, _wNew, _wRsvd)									\
					((t_int16)((_wUsed) + (_wNew) + (_wRsvd)) > ((t_int16)VOL_SS(_pVol) ) ?	\
						FFAT_TRUE : FFAT_FALSE)

#define _IS_NO_VALID_LOG(pLI)		(((pLI)->dwFlag & LI_FLAG_NO_LOG) ? FFAT_TRUE: FFAT_FALSE)

#define _VCE_LC(pVCE)				((pVCE)->dwCluster + (pVCE)->dwCount - 1)

#define _SET_CALLBACK(_pVol, _dwCacheFlag)		if (_LLW(_pVol)->pVol == _pVol)					\
												{												\
													(_dwCacheFlag) |= FFAT_CACHE_SYNC_CALLBACK;	\
												}

#ifdef FFAT_DYNAMIC_ALLOC
	#define _LLW(_pVol)					(_LOG_INFO(pVol)->pLLW)
#else
	#define _LLW(_pVol)					(&(_LOG_MAIN()->stLLW))
#endif

// for open unlink
#define _OU_LOG_SLOT_BASE		(LOG_MAX_SLOT + LOG_EXT_SLOT)	//!< the base slot number for open unlink log

#define _INVALID_ENTRY_INDEX	(0x7FFFFFFF)

#define _EINVALID_LOG_VER	FFAT_OK2		//!< Invalid log version, this log is not for current version
#define _ENO_VALID_LOG		FFAT_OK1		//!< no valid log

// for debug
#define _LOG_BACKUP(_pV, _pVS, _pC)
#define _LOG_RESTORE(_pV, _dwF, _pC)

typedef signed int	_FATActionType;
enum __FATActionType
{
	_ALLOC_FAT		= 0x0001,
	_DEALLOC_FAT	= 0x0002
};

typedef signed int	_HPAType;
enum __HPAType
{
	_Normal		= 0x0001,
	_HPA	= 0x0002,
};

typedef signed int	_RecoverActionType;
enum __RecoverActionType
{
	_Redo	= 0x0001,
	_Undo	= 0x0002
};

typedef signed int	_TransactionStateType;
enum __TransactionStateType
{
	_Finished		= 0x0001,
	_UnFinished		= 0x0002
};

typedef unsigned short	_SubLogType;
enum __SubLogType
{
	LM_SUBLOG_NONE				= 0x0001,		//!< No sub log
	LM_SUBLOG_ALLOC_FAT			= 0x0002,		//!< cluster allocation
	LM_SUBLOG_DEALLOC_FAT		= 0x0004,		//!< cluster deallocation
	LM_SUBLOG_UPDATE_DE			= 0x0008,		//!< update directory entry
	LM_SUBLOG_CREATE_DE			= 0x0010,		//!< create directory entry
	LM_SUBLOG_DELETE_DE			= 0x0020,		//!< delete directory entry
	LM_SUBLOG_SET_EA			= 0x0040,		//!< set extended attribute
	LM_SUBLOG_DELETE_EA			= 0x0080,		//!< delete extended attribute
	LM_SUBLOG_UPDATE_ROOT_EA	= 0x0100,		//!< update Root EA
	LM_SUBLOG_DUMMY				= 0x7FFFFFFF
};

typedef unsigned short	_SubLogFlag;
enum __SubLogFlag
{
	LM_SUBLOG_FLAG_CONTINUE		= 0x0001,		//!< there is another log
	LM_SUBLOG_FLAG_LAST			= 0x0002,		//!< the last log entry
	LM_SUBLOG_FLAG_DUMMY		= 0x7FFFFFFF
};

typedef signed int	_CreateAttr;
enum __CreateAttr
{
	NODE_ATTR_DIR			= 0x0001,		// directory
	NODE_ATTR_FILE			= 0x0002,		// file
	NODE_ATTR_DUMMY			= 0x7FFFFFFF
};

typedef struct SubLogCreateDe				//!< REDO policy: create DE for file & directory
{
	t_uint32	dwCluster;					//!< cluster number for start of parent node
	t_uint32	dwDeStartCluster;			//!< cluster number to save first directory entry
	t_int32		dwDeStartOffset;			//!< offset from beginning of parent's start cluster in byte
	t_int32		dwDeCount;					//!< count of directory entries

	t_uint32	udwUid;						//!< User ID
	t_uint32	udwGid;						//!< Group ID
	t_uint16	uwPerm;						//!< Permission

	/* Filename info */
	t_int16		wNameLen;					//!< filename length, character count
											//!< 0 :	Short File Name only
											//!<		or name is too long.
	t_int16		wNameInExtra;				//!< save filename in extra log space or not
											//!< FFAT_TRUE: yes FFAT_FALSE: no
	t_int16		wExtraNo;					//!< NO. of extra log space where filename is saved
	FatDeSFN	stDE;						//!< new short filename entry
} SubLogCreateDe;

typedef struct _LM_DeleteDe					//!< REDO policy: delete DE for file & directory
{
	t_uint32	udwCluster;					//!< cluster number for current node
	t_uint32	dwDeStartCluster;			//!< cluster number to save first directory entry
	t_int32		dwDeStartOffset;			//!< offset from beginning of parent's start cluster in byte
	t_int32		dwDeCount;					//!< count of directory entries
	FatDeSFN	stDE;						//!< short filename entry that will be deleted
} SubLogDeleteDe;

typedef struct _LM_UpdateDe					//!< REDO policy: update SFN DE for file & directory
{
	t_uint32	dwClusterSFNE;				//!< cluster number of SFNE
	t_int32		dwOffsetSFNE;				//!< offset for SFNE from beginning of parent's start cluster in byte
	FatDeSFN	stOldDE;					//!< directory entry before updating
	FatDeSFN	stNewDE;					//!< directory entry after updating
	t_boolean	bUpdateXDE;					//!< flag for update XDE (if 0, no need to update XDE)
	XDEInfo		stOldXDEInfo;				//!< XDEInfo before updating
	XDEInfo		stNewXDEInfo;				//!< XDEInfo after updating
} SubLogUpdateDe;

// Cluster allocation/de-allocate structure
//
//   100          101          102
// +-----+      +-----+      +-----+
// + 101 + ---> + 102 + ---> + EOC +
// +-----+      +-----+      +-----+
// PrevEOF    dwFirstCluster PrevEOF+Count

// TBD: _log_genAllocFat 과 _log_genDeallocFat 함수는 동일 함수임.
// 하지만 FAT chain 정보를 저장하려면 두 함수 모두 존재해야 함.

typedef struct								//!< UNDO policy : allocate FAT chain
{
	t_uint32	udwCluster;					//!< start cluster of a whole fat chain

	t_uint32	udwPrevEOF;					//!< previous EOF (end of file) cluster
	t_uint32	dwCount;					//!< cluster count to allocate/deallocate
	t_uint32	udwFirstCluster;			//!< first cluster of fat chain
	_HPAType	udwHPAType;					//!< flag for HPA or Normal node
	t_int32		dwValidEntryCount;			//!< total entry count on pVCE
											//!< may be 0
											//!< udwPrevEOF or udwFirstCluster 
											//!<	must have valid value when this is 0
	// here is the storage for vectored cluster entry (not vectored cluster!!)
} SubLogFat;

typedef struct
{
	t_uint32	udwFirstCluster;			//!< first cluster of extended attribute
	t_uint32	udwDelOffset;				//!< offset of entry to be deleted
	t_uint32	udwInsOffset;				//!< offset of entry to be inserted
	EAMain		stEAMain;					//!< EAMain structure
	EAEntry		stEAEntry;					//!< EAEntry structure
} SubLogEA;

// sublog for updated Root EA first cluster in BPB
typedef struct
{
	t_uint32	udwOldFirstCluster;			//!< Old first cluster of extended attribute of Root
	t_uint32	udwNewFirstCluster;			//!< New first cluster of extended attribute of Root
} SubLogUpdateRootEA;

// Log record structure
//
// +------------+   pFirst   +---------+   pNext   +---------+   pNext
// + Log Header + ---------> + SubLog  + --------> + SubLog  + --------> ...--> LogTail
// +------------+            +---------+           +---------+

typedef struct
{
	t_uint32		udwLogVer;			//!< log version,
										//!< CAUTION do not change this variable position
										//!< this is the base information for log
	LogType			udwLogType;			//!< log type
	t_uint32		udwSeqNum;			//!< sequence number: 0~LM_MAX_SEQNUM
	LogDirtyFlag	uwFlag;				//!< dirty flag
	t_int16			wUsedSize;			//!< data size in current log
										//!< Log Tail Included
	t_int16			wDummy;				//!< dummy for byte alignment
} LogHeader;

// log tail for normal log
typedef struct
{
	t_uint32		udwInvertedSeqNum;	//!< inverted sequence number
} LogTail;

// confirm log for Lazy Log
typedef struct
{
	t_uint32	udwInvertedLogVer;		//!< inverted log version for, 
										//!<do not change sequence of this variable.
										//!< do not move this variable
	t_int32		dwLogSize;				//!< size of log
} LogConfirm;

typedef struct
{
	_SubLogFlag		uwNextSLFlag;		//!< next sub-log flag
	_SubLogType		uwSublogType;		//!< sub-log type
} SubLogHeader;

typedef struct
{
	SubLogHeader	stSubLogHeader;
	union 
	{									//!< sub-log structure
		SubLogFat			stFat;		// FAT allocation or deallocation
		SubLogUpdateDe		stUpdateDE;	// update DE
		SubLogCreateDe		stCreateDE;	// create DE
		SubLogDeleteDe		stDeleteDE;	// delete DE
		SubLogEA			stEA;		// extended attribute
		SubLogUpdateRootEA	stUpdateRootEA;	// update Root EA
	} u;
} SubLog;

// open unlink log header
typedef struct
{
	t_uint32		udwLogVer;			//!< log version,
										//!< CAUTION do not change this variable position
										//!< this is the base information for log
	LogType			udwLogType;			//!< log type
	t_uint32		udwValidEntry;		//!< count of valid entry count
	t_uint32 		reserved;			//!< dummy
	t_uint8			pBitmap[LOG_OPEN_UNLINK_BITMAP_BYTE];
										//!< open unlink slot bitmap 1: occupy, 0 : free
	t_uint8			reserved1;			//!< for 4byte align
} OULogHeader;	// size is LOG_OPEN_UNLINK_HEADER_SIZE(32Byte)

// this structure is for static memory allocation
typedef struct
{
	EssList			stList;				//!< free list
	LogInfo			stLogInfo;			//!< log information
	LogLazyWriter*	pLLW;				//!< pointer of LLW information
} VolLogInfo;

// main structure for LOG Module
typedef struct
{
	EssList			stFreeList;							//!< volume info free list
	VolLogInfo		stVolLogInfo[FFAT_MOUNT_COUNT_MAX];	//!< volume log information
	LogLazyWriter	stLLW;
	LogType			dwValidityFlag;						//!< to check SEC NAND 
														//!< this flag will be added to log header
} LogMain;

// static functions
static FFatErr	_enableLogging(Vol* pVol, FFatMountFlag* pdwFlag, t_boolean bReMount, ComCxt* pCxt);
static FFatErr	_disableLogging(Vol* pVol, ComCxt* pCxt);

static FFatErr	_getFreeLogInfo(Vol* pVol);
static FFatErr	_releaseLogInfo(Vol* pVol);

static FFatErr	_openAndCreateLogArea(Vol* pVol, ComCxt* pCxt);
static FFatErr	_checkRootClusters(Vol* pVol, ComCxt* pCxt);

static t_uint32	_getLogId(t_uint32 udwLogType);

static FFatErr	_logRecovery(Vol* pVol, t_uint16* pwValidSlots, t_uint32* pdwLatestSeqNo, ComCxt* pCxt);
static FFatErr	_readSlot(Vol* pVol, LogHeader* pLog, t_uint16 uwId, ComCxt* pCxt);
static FFatErr	_checkLogHeader(LogHeader* pLog);
static FFatErr	_checkLazyLog(LogHeader* pBuff);
static FFatErr	_getNextLog(Vol* pVol, LogHeader* pCurLogOrig, LogHeader** ppCurLog,
							LogHeader* pNextLogOrig, LogHeader** ppNextLog,
							t_uint16* pwSlot, t_boolean* pbNextIsNewLLW, ComCxt* pCxt);

static FFatErr	_logWriteSlot(Vol* pVol, LogHeader* pLH, t_uint16 uwSlot, ComCxt* pCxt);
static FFatErr	_writeEmptySlot(Vol* pVol, t_uint16 uwId, ComCxt* pCxt);
static FFatErr	_logReset(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_logResetWithClean(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt);
static FFatErr	_logWriteTransaction(Vol* pVol, LogHeader* pLog, ComCxt* pCxt);

static t_int16	_makeIncEven(t_int16 wValue);

static FFatErr	_logRecoverFinishedTransaction(Vol* pVol, LogHeader* pLH, ComCxt* pCxt);

static FFatErr	_allocAndInitLogHeader(Vol* pVol, LogHeader** ppLog, LogType udwLogType,
							FFatCacheFlag dwCacheFlag, ComCxt* pCxt);

static FFatErr	_sublogGenCreateDE(Vol* pVol, LogHeader* pLH, Node* pNode, t_wchar* psName,
							t_int16 wReservedSize, t_boolean bForce, ComCxt* pCxt);
static FFatErr	_sublogGenDeleteDE(Vol* pVol, LogHeader* pLH, Node* pNode, ComCxt* pCxt);
static FFatErr	_sublogGenUpdateDE(LogHeader* pLH, Node* pNode, FatDeSFN* pDEOldSFN,
							XDEInfo* pNewXDEInfo);
static FFatErr	_sublogGenFat(Vol* pVol, Node* pNode, LogHeader* pLH, t_uint32 dwCluster,
							FatAllocate* pAlloc, t_int16 wReservedSize);
static FFatErr	_sublogGenAllocateFat(Vol* pVol, Node* pNode, LogHeader* pLH, t_uint32 dwCluster,
							FatAllocate* pAlloc, t_int16 wReservedSize);
static FFatErr	_sublogGenDeallocateFat(Vol* pVol, Node* pNode, LogHeader* pLH,
							t_uint32 dwCluster, FatAllocate* pAlloc, t_int16 wReservedSize);
static FFatErr	_sublogGenEA(Vol* pVol, LogHeader* pLH, t_uint16 uwSublogType,
							t_uint32 udwFirstCluster, t_uint32 udwDelOffset,
							t_uint32 udwInsOffset, EAMain* pEAMain, EAEntry* pEAEntry);
static FFatErr	_sublogGenUpdateRootEA(Vol* pVol, LogHeader* pLH,
										t_uint32 udwOldRootEACluster, t_uint32 udwNewRootEACluster);
static void		_sublogGenNoLog(LogHeader* pLH);
static void		_subLogCheckDeStartCluster(Vol* pVol, t_uint32 udwCluster,
							t_uint32* pdwDeStartCluster, t_int32 dwDeStartOffset, ComCxt* pCxt);
static t_int16		_sublogGetSize(SubLog* pSL);
static FatDeSFN*	_sublogGetNewDE(LogHeader* pLH);
static SubLogFat*	_sublogGetSubLogFat(LogHeader* pLH);

static FFatErr	_logCreateNew(Vol* pVol, Node* pNode,
							t_wchar* psName, FatAllocate* pSubDir,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);
static FFatErr	_logCreateEA(Vol* pVol, Node* pNode, FatAllocate* pAlloc,
							FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt);
static FFatErr	_logCompactEA(Vol* pVol, Node* pNode, FatAllocate* pOldAlloc,
							FatAllocate* pNewAlloc,	FFatCacheFlag *pdwCacheFlag,
							ComCxt* pCxt);

static FFatErr	_logFileWrite(Vol* pVol, Node* pNode, FatDeSFN* pstOldSFN,
							FatAllocate* pAlloc, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);
static FFatErr	_logUnlink(Vol* pVol, Node* pNodeChild, FatDeallocate* pAlloc,
							FatDeallocate* pAllocEA, NodeUnlinkFlag dwNUFlag,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);
static FFatErr	_logRename(Vol* pVol, Node* pSrc, Node* pOldDes, Node* pDes,
							t_wchar* psName, t_boolean bUpdateDe, t_boolean bDeleteSrcDe,
							t_boolean bDeleteDesDe, t_boolean bCreateDesDe,
							FatDeallocate* pDeallocateDes, FatDeallocate* pDeallocateDesEA,
							FatDeSFN* pDotDotOrig, FatDeSFN* pDotDot,
							FFatRenameFlag dwFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);

static FFatErr	_logTruncate(Vol* pVol, Node* pNodeChild, FatDeSFN* pstOldSFN,
							FatAllocate* pAlloc, FFatCacheFlag* pdwCacheFlag,
							LogType udwLogType, ComCxt* pCxt);
static FFatErr	_logExtendFileSize(Node* pNode, t_uint32 dwSize, t_uint32 dwEOF,
							FFatVC* pVC, FFatChangeSizeFlag dwFlag,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);
static FFatErr	_logShrinkFileSize(Node* pNodeOrig, t_uint32 dwSize, t_uint32 dwEOF,
							FFatVC* pVC, FFatChangeSizeFlag dwCSFlag,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);

static FFatErr	_logExpandDir(Vol* pVol, Node* pNode, FatAllocate* pNewDe,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);

static FFatErr	_logSetState(Vol* pVol, Node* pNode, FatDeSFN* pDEOldSFN, XDEInfo* pNewXDEInfo,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);

static FFatErr	_logTruncateDir(Vol* pVol, Node* pNode, FatAllocate* pDealloc,
							FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt);

// for LLW
static void		_initLLW(LogLazyWriter* pLLW);
static FFatErr	_syncLL(Vol* pVol, ComCxt* pCxt);
static FFatErr	_logWriteLLW(Vol* pVol, LogHeader* pLH, ComCxt* pCxt);
static FFatErr	_addToLL(Vol* pVol, LogHeader* pLH, ComCxt* pCxt);
static FFatErr	_writeLL(Vol* pVol, LogLazyWriter* pLLW, ComCxt* pCxt);
static FFatErr	_mergeLL(Vol* pVol, LogHeader* pLH, ComCxt* pCxt);
static t_boolean	_canLLbeMerged(Vol* pVol, LogHeader* pCurLH, LogHeader* pPrevLH,
							FatDeSFN** ppNewDECur, FatDeSFN** ppNewDEPrev,
							SubLogFat** ppSLFatCur, SubLogFat** ppSLFatPrev);

// for open unlink
static FFatErr	_initLogOpenUnlink(Vol* pVol, t_int32 dwSlotIndex, t_int32 dwSlotCount,
							t_int8* pBuff, ComCxt* pCxt);
static FFatErr  _logDeleteOpenUnlink(Node* pNode, ComCxt* pCxt);
static FFatErr	_readSlotOpenUnlink(Vol* pVol, OULogHeader* pOULog,
							t_int32 dwSlotIndex, ComCxt* pCxt);
static FFatErr	_writeSlotOpenUnlink(Vol* pVol, OULogHeader* pOULog,
							t_uint32 dwId, ComCxt* pCxt);
static t_int32	_allocateBitmapOpenUnlink(OULogHeader* pOULog);
static FFatErr	_deallocateBitmapOpenUnlink(OULogHeader* pOULog, t_uint32 dwEntryOffset);
static FFatErr	_logOpenUnlink(Vol* pVol, Node* pNode, t_uint32 dwOUCluster,
							t_uint32 dwEACluster, ComCxt* pCxt);
static FFatErr	_logRecoveryOpenUnlink(Vol* pVol, ComCxt* pCxt);
static FFatErr	_commitOpenUnlink(Vol* pVol, ComCxt* pCxt);

static FFatErr	_getReservedAreaForLog(Vol* pVol, ComCxt* pCxt);
static FFatErr	_getSectorsForLogFile(Vol* pVol, Node* pNodeLog, ComCxt* pCxt);

static FFatErr	_initLogFile(Vol* pVol, ComCxt* pCxt);
static FFatErr	_checkLogArea(Vol* pVol, ComCxt* pCxt);

static FFatErr	_getLogCreatInfo(Vol* pVol, LogCreatInfo* pstLogCreatInfo, ComCxt* pCxt);
static FFatErr	_setLogCreatInfo(Vol* pVol, LogCreatInfo* pstLogCreatInfo, ComCxt* pCxt);
static FFatErr	_checkLogCreation(Vol* pVol, ComCxt* pCxt);

#ifdef FFAT_LITTLE_ENDIAN
	#define _adjustByteOrderSubLogs(_a, _b)	((void)0)
	#define _adjustByteOrder(_a, _b)		((void)0)
	#define _boLogHeader(_a)				((void)0)
	#define _boLogTail(_a)					((void)0)
	#define _boLogHeaderOpenUnlink(_a)		((void)0)
	#define _boLogConfirm(_a)				((void)0)
	#define _boSubLogHeader(_a)				((void)0)
	#define _boSubLogFat(_a)				((void)0)
	#define _boSubLogFatVCE(_a, _b)			((void)0)
	#define _boSubLogUpdateDe(_a)			((void)0)
	#define _boSubLogCreateDe(_a)			((void)0)
	#define _boSubLogDeleteDe(_a)			((void)0)
	#define _boSubLogEA(_a)					((void)0)
	#define _boSubLogFileName(_a, _b)		((void)0)
	#define _boSubLogUpdateRootEA(_a)		((void)0)
#else
	static void		_adjustByteOrderSubLogs(SubLog* pSL, t_boolean bLogWrite);
	static FFatErr	_adjustByteOrder(LogHeader* pLH, t_boolean bLogWrite);
	static void		_boLogHeader(LogHeader* pLH);
	static void		_boLogTail(LogTail* pLT);
	static void		_boLogHeaderOpenUnlink(OULogHeader* pOULog);
	static void		_boLogConfirm(LogConfirm* pLog);
	static void		_boSubLogHeader(SubLogHeader* pSL);
	static void		_boSubLogFat(SubLogFat* pFat);
	static void		_boSubLogFatVCE(FFatVCE* pVCE, t_int32 dwValidEntryCount);
	static void		_boSubLogUpdateDe(SubLogUpdateDe* pUpdateDe);
	static void		_boSubLogCreateDe(SubLogCreateDe* pCreateDe);
	static void		_boSubLogDeleteDe(SubLogDeleteDe* pDeleteDe);
	static void		_boSubLogEA(SubLogEA* pEA);
	static void		_boSubLogFileName(t_wchar* psName, t_uint16 uwNameLen);
	static void		_boSubLogUpdateRootEA(SubLogUpdateRootEA* pUpdateRootEA);
#endif

static LogLazyWriter*	_allocLLW(Vol* pVol);
static void				_freeLLW(Vol* pVol);

#define		FFAT_DEBUG_LOG_PRINTF(_vol, _msg)

// debug begin

#if defined(_LOG_DEBUG_) || defined(FFAT_DEBUG_FILE)
	static FFatErr	_printLog(Vol* pVol, LogHeader* pLH, char* psType);
	static FFatErr	_printSubLog(Vol* pVol, SubLog* pSL);
	static void		_printDE(Vol* pVol, FatDeSFN* pDE);
#endif

#ifdef _LOG_REPLAY
	#undef _LOG_BACKUP
	#undef _LOG_RESTORE

	#define _LOG_BACKUP(_pV, _pVS, _pC)		_logBackup(_pV, _pVS, _pC)
	#define _LOG_RESTORE(_pV, _dwF, _pC)	_logRestore(_pV, _dwF, _pC)

	static FFatErr	_logBackup(Vol* pVol, t_int16 wValidSlots, ComCxt* pCxt);
	static FFatErr	_logRestore(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt);
#endif

#if defined(_LOG_DEBUG_) || defined(FFAT_DEBUG_FILE)
	static char* _gpLogName[_MAX_LOG_TYPE + 1] =
	{
		"Create New",
		"Delete",
		"Extend",
		"Shrink",
		"Write ",
		"Rename",
		"Set State",
		"Truncate Directory",
		"Expand Directory",
		"Confirm",					// for close
		"HPA Create",
		"EA Create",
		"EA Set",
		"EA Delete",
		"EA Compaction",
		"Open Unlink",
		"Create Symlink",
		"None"
	};
	#undef		FFAT_DEBUG_LOG_PRINTF

	#ifdef _LOG_DEBUG_
		//#define		FFAT_DEBUG_LOG_PRINTF(_vol, _msg)	FFAT_PRINT_VERBOSE((_T("BTFS_LOG, %s()/%d"), __FUNCTION__, __LINE__)); FFAT_PRINT_VERBOSE(_msg)
		#define		FFAT_DEBUG_LOG_PRINTF(_vol, _msg)	_BTFS_MY_PRINTF(_T("[BTFS_LOG] ")); _BTFS_MY_PRINTF _msg
	#elif defined(FFAT_DEBUG_FILE)
		#define		FFAT_DEBUG_LOG_PRINTF(_vol, _msg)	FFAT_PRINT_FILE(_vol, _msg)
	#endif
#endif

#ifdef FFAT_DEBUG
	// to check cluster allocation validity for log recovery. 
	t_uint32	dwFirstClusterForNodeVariableToCheckClusterAlloc;
#endif

// debug end

static t_int32*	_pLogMain;		// pointer for log main information


//////////////////////////////////////////////////////////////////////////
//
//	DYNAMIC / STATIC MEMORY 
//

#ifdef FFAT_DYNAMIC_ALLOC
	#define _ALLOC_FREE_LOG_INFO()		_allocLogInfoDynamic()
	static VolLogInfo*	_allocLogInfoDynamic(void);
#else
	#define _ALLOC_FREE_LOG_INFO()		_allocLogInfoStatic()
	static VolLogInfo*	_allocLogInfoStatic(void);
#endif

//
//
//
//////////////////////////////////////////////////////////////////////////

/**
 * ffat_log_init initializes log module
 * @return FFAT_OK		: success
 * @author		DongYoung.seo
 * @version		JAN-12-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_log_init(void)
{
#ifdef FFAT_DYNAMIC_ALLOC

	FFAT_ASSERT(LOG_OPEN_UNLINK_HEADER_SIZE == sizeof(OULogHeader));

	// nothing to do
	// memory is allocated by dynamically

	_pLogMain = NULL;

	return FFAT_OK;

#else

	t_int32		i;

	// allocate memory for Vol Log info
	FFAT_ASSERT(EssMath_IsPowerOfTwo(LOG_OPEN_UNLINK_SLOT) == ESS_TRUE);

	_pLogMain = (t_int32*)FFAT_MALLOC(sizeof(LogMain), ESS_MALLOC_NONE);
	IF_UK (_pLogMain == NULL)
	{
		goto out;
	}

	ESS_LIST_INIT(&_LOG_MAIN()->stFreeList);

	for (i = 0; i < FFAT_MOUNT_COUNT_MAX; i++)
	{
		ESS_LIST_ADD_HEAD(&_LOG_MAIN()->stFreeList, &(_LOG_MAIN()->stVolLogInfo[i]).stList);
	}

	_LOG_MAIN()->dwValidityFlag = LM_LOG_NONE;

	_initLLW(_LLW(NULL));

	return FFAT_OK;

out:
	ffat_log_terminate();

	return FFAT_ENOMEM;

#endif
}


/**
 * ffat_log_terminate() terminates log module
 * 
 * @return		FFAT_OK		: success
 * @author		DongYoung.seo
 * @version		JAN-12-2007 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_log_terminate(void)
{
	// release memory

	if (_pLogMain)
	{
		FFAT_FREE(_pLogMain, sizeof(LogMain));
		_pLogMain = NULL;
	}

	return FFAT_OK;
}


/** 
 * mount a volume
 * 
 * @param		pVol		: [IN] pointer of volume
 * @param		pdwFlag		: [IN/OUT] mount flag
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_NOMEM		: not enough memory
 * @return		FFAT_EINVALID	: There is a directory that has log file name
 * @return		else			: log init failed
 * @author
 * @version
 * @history		FEB-13-2007 [DongYoung Seo] : do not use log on read-only volume
 * @history		MAR-26-2007 [DongYoung Seo] : release log info on error
 * @history		DEC-04-2007 [InHwan Choi] : apply to open unlink
 * @history		11-DEC-2008 [DongYoung Seo] : add meta-data sync code after log recovery
 * @history		23-MAR-2009 [JeongWoo Park] : change the mount flag as In/Out to notify auto log-off
 */
FFatErr
ffat_log_mount(Vol* pVol, FFatMountFlag* pdwFlag, ComCxt* pCxt)
{
	FFAT_ASSERT(pVol);

	// check no log flag
	if (*pdwFlag & (FFAT_MOUNT_NO_LOG | FFAT_MOUNT_RDONLY))
	{
		// logging enable related flag can not be used with no log flag
		_LOG_INFO(pVol)		= NULL;
		pVol->dwFlag		&= (~VOL_ADDON_LOGGING);

		// update mount flag if log is off.
		*pdwFlag	&= (~FFAT_MOUNT_LOG_MASK);
		*pdwFlag	|= FFAT_MOUNT_NO_LOG;

		return FFAT_OK;
	}
	
	return _enableLogging(pVol, pdwFlag, FFAT_FALSE, pCxt);
}


/**
*  re-mount a volume
* This function changes operation move of a volume
*	- Transaction On/Off 
*	- Change transaction type
*	- set volume read only
*
* This function is used on Linux BOX.
* I does not optimize this routine because this ia not a main feature.
* To improve performance of remounting add condition checking about below
*	- There is no flag change (it is same mount flag)
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
* @history		23-MAR-2009 [JeongWoo Park] : change the mount flag as In/Out to notify auto log-off
*/
FFatErr
ffat_log_remount(Vol* pVol, FFatMountFlag* pdwFlag, ComCxt* pCxt)
{
	t_boolean		bIsRDOnly = FFAT_FALSE;
	FFatErr			r;

	FFAT_ASSERT((*pdwFlag & (FFAT_MOUNT_NO_LOG | FFAT_MOUNT_RDONLY)) ? ((*pdwFlag & (FFAT_MOUNT_LOG_LLW | FFAT_MOUNT_LOG_FULL_LLW)) == 0) : FFAT_TRUE);

	r = _disableLogging(pVol, pCxt);
	FFAT_EO(r, (_T("Fail to disable logging")));

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE);

	// check no log flag
	if ((*pdwFlag & (FFAT_MOUNT_NO_LOG | FFAT_MOUNT_RDONLY)) == 0)
	{
		if (VOL_IS_RDONLY(pVol) == FFAT_TRUE)
		{
			// remove read only flag
			VOL_FLAG(pVol)	&= (~VOL_RDONLY);
			bIsRDOnly		= FFAT_TRUE;
		}

		// logging enable related flag can not be used with no log flag
		r = _enableLogging(pVol, pdwFlag, FFAT_TRUE, pCxt);
		FFAT_EO(r, (_T("Fail to enable logging")));
	}
	else
	{
		// update mount flag if log is off.
		*pdwFlag	&= (~FFAT_MOUNT_LOG_MASK);
		*pdwFlag	|= FFAT_MOUNT_NO_LOG;
	}

	FFAT_ASSERT(((*pdwFlag & (FFAT_MOUNT_NO_LOG | FFAT_MOUNT_RDONLY)) == 0) ? (_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE) : FFAT_TRUE);

out:
	if (r != FFAT_OK)
	{
		if (bIsRDOnly == FFAT_TRUE)
		{
			VOL_FLAG(pVol)	|= VOL_RDONLY;
		}
	}

	return r;
}


/**
 * ffat_log_release release log
 * 
 * @param	pVol		: [IN] pointer of volume 
 * @param	pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 12-DEC-2008 [DongYoung Seo] make a sub-routine _disableLogging to support remount
 */
FFatErr
ffat_log_umount(Vol* pVol, ComCxt* pCxt)
{
	return _disableLogging(pVol, pCxt);
}


/**
* ffat_log_operationFail is called when a file system operation is failed 
* to remove the log entry just be written for this file system operation
* 
* @param	pVol		: [IN] pointer of volume
* @param	dwType		: [IN] type of node
*								XDE will be cleaned for log type RENAME and UNLINK
* @param	pNode		: [IN] pointer of Node, to remove open unlink information
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success to remove the log slot
* @return	else		: failed
* @author
* @version
* @version		MAR-27-2009 [JeongWoo Park] Add the code to consider for recovery of log creation
*/
FFatErr
ffat_log_operationFail(Vol* pVol, LogType dwType, Node* pNode, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;

	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		IF_UK (LOG_IS_LOG(pNode) == FFAT_TRUE)
		{
			// erase Log creation info
			_setLogCreatInfo(pVol, NULL, pCxt);
		}

		return FFAT_OK;
	}

	if ((dwType & (LM_LOG_RENAME | LM_LOG_UNLINK)) && (pNode != NULL))
	{
		// reset open unlink information
		r |= _logDeleteOpenUnlink(pNode, pCxt);	// ignore error
	}

	// reset log
	r |= _logResetWithClean(pVol, (FFAT_CACHE_SYNC | FFAT_CACHE_FORCE), pCxt);

	return r;
}


/**
 * This function synchronizes all log entry in a volume
 * This function is called before volume synchronization
 * 
 * @param		pVol		: [IN] volume pointer
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JAN-25-2007 [DongYoung Seo] First Writing.
 * @mark		sync and close logs need special consideration
 */
FFatErr
ffat_log_syncVol(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;

	FFAT_ASSERT(pVol);

	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	// if no log slot in log file, do nothing
	if (_IS_NO_VALID_LOG(_LOG_INFO(pVol)))
	{
		goto out;
	}

	r = _syncLL(pVol, pCxt);
	FFAT_ER(r, (_T("fail to sync LLW")));

out:
	return FFAT_OK;
}


/**
 * This function synchronizes all log entry in a volume
 * This function is called when all operation is in synchronized state (after volume is synchronized)
 * 
 * @param		pVol		: [IN] volume pointer
 * @param		dwCacheFlag	: [IN] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		JAN-25-2007 [DongYoung Seo] First Writing.
 * @mark		sync and close logs need special consideration
 */
FFatErr
ffat_log_afterSyncVol(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogInfo*		pLI;

	FFAT_ASSERT(pVol);

	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	pLI = _LOG_INFO(pVol);

	//if no log slot in log file, do nothing
	if (_IS_NO_VALID_LOG(pLI) == FFAT_TRUE)
	{
		goto out;
	}

	//else clean all log slots to prevent data is redo or undo after power-off
	r = _logResetWithClean(pVol, dwCacheFlag, pCxt);
	if (r < 0)
	{
		return r;
	}

out:
	FFAT_ASSERT(pLI->dwFlag & LI_FLAG_NO_LOG);

	return FFAT_OK;
}

/**
* Init node for log
*
* initializes open unlink slot information
*
* @param		pNode		: [IN] node pointer
* @return		FFAT_OK		: success
* @return		negative	: fail
* @author		DongYoung Seo
* @version		SEP-27-2006 [DongYoung Seo] First Writing.
* @version		JAN-18-2008 [GwangOk Go] Refactoring DEC module
*/
FFatErr
ffat_log_initNode(Node* pNode)
{
	FFAT_ASSERT(pNode);

	_NODE_OUEI(pNode) = _INVALID_ENTRY_INDEX;
	_NODE_EAEI(pNode) = _INVALID_ENTRY_INDEX;

	return FFAT_OK;
}


/**
* This function is a callback function for cache 
* This function is called before a cache entries synchronized
* 
* @param	pVol			: volume pointer
*								may be NULL
* @param	dwSector		: sector number
* @param	dwFlag			: cache flag
* @param	pCxt		: [IN] context of current operation
* @author	DongYoung Seo
* @version	JUL-25-2006 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_log_cacheCallBack(Vol* pVol, ComCxt* pCxt)
{
	return _syncLL(pVol, pCxt);
}


/**
* ffat_log_confirm writes confirm log.
* confirm log is used to indicate that previous operations are finished
* 
* NOTICE
* This confirm log will recorded in first slot by sync flag.
* it is almost same with _logResetWithClean()
*
* @param	pNode		: [IN] pointer of node
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success
* @return	FFAT_NOMEM	: not enough memory to write log
* @return	else		: log write failed
* @version	FEB-19-2008 [JeongWoo Park] rename ffat_log_close -> ffat_log_confirm.
*							this can be used in close, afterSync, afterWrite
*/
FFatErr
ffat_log_confirm(Node* pNode, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH = NULL;			// pointer for log
	LogInfo*		pLI;
	Vol*			pVol;

	pVol = NODE_VOL(pNode);

	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	pLI = _LOG_INFO(pVol);

	// if no log slot in log file, do nothing
	if (_IS_NO_VALID_LOG(pLI) == FFAT_TRUE)
	{
		FFAT_ASSERT(pLI->wCurSlot == 0);
		FFAT_ASSERT((pLI->uwPrevDirtyFlag == LM_FLAG_NONE) || (pLI->uwPrevDirtyFlag & LM_FLAG_SYNC));
		goto out1;
	}

	// for prevent to record the duplicated confirm log
	if ((pLI->udwPrevLogType & LM_LOG_CONFIRM) ||
		(pLI->udwPrevLogType == LM_LOG_NONE))
	{
		FFAT_ASSERT(pLI->wCurSlot == 0);
		FFAT_ASSERT((pLI->uwPrevDirtyFlag == LM_FLAG_NONE) || (pLI->uwPrevDirtyFlag & LM_FLAG_SYNC));
		goto out1;
	}

	// else data is already synchronized to device, write close log to prevent data is recovered after power-off
	// allocate memory for log record
	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_CONFIRM, FFAT_CACHE_SYNC, pCxt);
	FFAT_ASSERT(r == FFAT_OK);

	_sublogGenNoLog(pLH);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

out1:
	r = FFAT_OK;

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * this function is called before directory expansion
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNode			: [IN] node(directory) pointer
 * @param		dwPrecEOC		: [IN] the last cluster of pNode
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		SEP-23-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_log_expandDir(Node* pNode, t_uint32 dwPrevEOC, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_int32			r;
	FatAllocate		stAlloc;
	FFatVC			stVC;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	*pdwCacheFlag |= FFAT_CACHE_SYNC;			// In current version, I do not have all of the cluster information

	FFAT_MEMSET(&stAlloc, 0x00, sizeof(FatAllocate));
	stAlloc.dwPrevEOF	= dwPrevEOC;
	stAlloc.pVC			= &stVC;
	VC_INIT(&stVC, VC_NO_OFFSET);

	r = _logExpandDir(NODE_VOL(pNode), pNode, &stAlloc, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log for directory expansion")));

	r = FFAT_OK;

out:
	return r;
}


/**
 * writes expand directory log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pAlloc			: [IN] allocated FAT chain information for expanding directory
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	pCxt		: [IN] context of current operation
 * @return	FFAT_OK	: success
 * @author 
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version		Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenAllocateFat
 */
static FFatErr
_logExpandDir(Vol* pVol, Node* pNode, FatAllocate* pAlloc,
				FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;			// pointer for sub-log within log record
	t_int16			wReservedSize;		// reserved byte for next log

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT(pdwCacheFlag);

	// if log is disabled, return immediately
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_EXPAND_DIR, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	wReservedSize = _RSVD_SIZE_LOG_TAIL;
	_sublogGenAllocateFat(pVol, pNode, pLH, pNode->dwCluster, pAlloc, wReservedSize);

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// write transaction log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * this function is called before node create operation.
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNodeParent		: [IN] parent node pointer
 * @param		pNodeChild		: [IN] child node pointer
 *										For Dir : new cluster number is stored ate pNodeChild->dwCluster
 * @param		psName			: [IN] node name
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @author		DongYoung Seo
 * @version		SEP-21-2006 [DongYoung Seo] First Writing.
 * @version		NOV-14-2007 [DongYoung Seo] Log optimization.
 *											Do not write log when update is done in one sector
 * @version		MAR-27-2009 [JeongWoo Park] Add the code to consider for recovery of log creation
 */
FFatErr
ffat_log_create(Node* pNodeParent, Node* pNodeChild,
					t_wchar* psName, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FatAllocate*			pSubDir = NULL;
	FFatErr					r;
	FatAllocate				stDir;				// cluster information for new directory
	FFatVC					stVC;				// Vectored Cluster
	FFatVCE					stVCE;				// Entry for VC
	Vol*					pVol;

	FFAT_ASSERT(pNodeParent);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(psName);

	pVol = NODE_VOL(pNodeParent);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;

		// is this for creation of log file
		IF_UK (LOG_IS_LOG(pNodeChild) == FFAT_TRUE)
		{
			LogCreatInfo	stLogCreatInfo;

			stLogCreatInfo.dwDeStartCluster	=  pNodeChild->stDeInfo.dwDeStartCluster;
			stLogCreatInfo.dwDeStartOffset	=  pNodeChild->stDeInfo.dwDeStartOffset;
			stLogCreatInfo.dwDeCount		=  pNodeChild->stDeInfo.dwDeCount;
			stLogCreatInfo.dwStartCluster	=  0;

			r  = _setLogCreatInfo(pVol, &stLogCreatInfo, pCxt);
			FFAT_ER(r, (_T("record the log creation info is failed")));
		}

		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNodeChild, NODE_ACCESS_CREATE);
	FFAT_ER(r, (_T("log file can not be re-created")));

	// here. we gather information for log
	if (NODE_IS_DIR(pNodeChild) == FFAT_TRUE)
	{
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNodeChild->dwCluster) == FFAT_TRUE);

		pSubDir = &stDir;
		stDir.dwCount			= 1;
		stDir.dwHintCluster		= 0;
		stDir.dwPrevEOF			= 0;
		stDir.dwFirstCluster	= pNodeChild->dwCluster;
		stDir.pVC				= &stVC;

		VC_INIT(&stVC, VC_NO_OFFSET);
		stVC.dwTotalEntryCount		= 1;
		stVC.dwTotalClusterCount	= 1;
		stVC.dwValidEntryCount		= 1;
		stVC.pVCE					= &stVCE;

		stVCE.dwCluster				= pNodeChild->dwCluster;
		stVCE.dwCount				= 1;
	}
	else
	{
		// this is a file creation
		// check it needs a log. Do not need log recovery when there is just one sector update.
		if (_LOG_INFO(pVol)->uwPrevDirtyFlag & LM_FLAG_SYNC)
		{
			if ((pNodeChild->stDeInfo.dwDeStartOffset >> VOL_SSB(pVol))
					== (pNodeChild->stDeInfo.dwDeEndOffset >> VOL_SSB(pVol)))
			{
				// the directory entries for new node are in a sector
				// we do not need to write log
				if ((VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW) == 0)
				{
					*pdwCacheFlag |= FFAT_CACHE_SYNC;
					r = FFAT_OK;
					goto out;
				}
			}
		}
	}

	r = _logCreateNew(pVol, pNodeChild, psName, pSubDir, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log for create")));

	r = FFAT_OK;

out:
	return r;
}


/**
 * write log for createSymlink
 *
 * @param		pNode			: [IN] node pointer
 * @param		psName			: [IN] node name
 * @param		pVC				: [IN] vectored cluster info
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		GwangOk Go
 * @version		DEC-05-2007 [GwangOk Go] First Writing.
 * @version		DEC-21-2008 [DongYoung Seo] add wReservedSize check routine
 */
FFatErr
ffat_log_createSymlink(Node* pNode, t_wchar* psName, FFatVC* pVC, 
						FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	FatAllocate		stAlloc;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;
	t_int16			wReservedSize;	// reserved size of next log

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwCacheFlag);

	pVol = NODE_VOL(pNode);

	FFAT_ASSERT(pVol);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_CREATE);
	FFAT_ER(r, (_T("log file can not be re-created")));

	wReservedSize = _RSVD_SIZE_CREATE_DE + _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	stAlloc.dwCount = VC_CC(pVC);
	stAlloc.dwHintCluster = 0;
	stAlloc.dwPrevEOF = 0;
	if (VC_CC(pVC) > 0)
	{
		stAlloc.dwFirstCluster = VC_FC(pVC);
	}
	else
	{
		stAlloc.dwFirstCluster = 0;
	}

	stAlloc.dwLastCluster = VC_LC(pVC);
	stAlloc.pVC = pVC;

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_CREATE_SYMLINK, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	wReservedSize	-= _RSVD_SIZE_CREATE_DE;
	_sublogGenCreateDE(pVol, pLH, pNode, psName, wReservedSize, FFAT_FALSE, pCxt);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;
	//start cluster of FAT chain is 0 before allocating clusters
	_sublogGenAllocateFat(pVol, pNode, pLH, 0, &stAlloc, wReservedSize);

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// write transaction log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * this function is called before node size change operation.
 *
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNode			: [IN] node pointer,
 *									all node information is already updated state.
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwSize			: [IN] New node size
 * @param		dwEOF			: [IN] if expand, previous EOF. if shrink, new EOF
 * @param		dwCSFlag		: [IN] change size flag
 * @param		pdwCacheFlag	: [IN] flag for cache operations
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 * @version		MAR-27-2009 [JeongWoo Park] Add the code to consider for recovery of log creation
 * @version		OCT-22-2009 [JeongWoo Park] Add the code to consider for recovery of dirty-size node
 */
FFatErr
ffat_log_changeSize(Node* pNode, t_uint32 dwSize, t_uint32 dwEOF, FFatVC* pVC,
					FFatChangeSizeFlag dwCSFlag, FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt)
{
	Vol*		pVol = NODE_VOL(pNode);
	FFatErr		r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC);
	FFAT_ASSERT(pdwCacheFlag);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;

		// is this for creation of log file
		IF_UK (LOG_IS_LOG(pNode) == FFAT_TRUE)
		{
			LogCreatInfo	stLogCreatInfo;

			stLogCreatInfo.dwDeStartCluster	=  pNode->stDeInfo.dwDeStartCluster;
			stLogCreatInfo.dwDeStartOffset	=  pNode->stDeInfo.dwDeStartOffset;
			stLogCreatInfo.dwDeCount		=  pNode->stDeInfo.dwDeCount;
			stLogCreatInfo.dwStartCluster	= NODE_C(pNode);

			if (NODE_C(pNode) == 0)
			{
				stLogCreatInfo.dwStartCluster	= VC_FC(pVC);
			}

			r  = _setLogCreatInfo(pVol, &stLogCreatInfo, pCxt);
			FFAT_ER(r, (_T("record the log creation info(truncate) is failed")));
		}

		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_TRUNCATE);
	FFAT_ER(r, (_T("log file can not be truncated")));

	// check it needs a log. Do not need log recovery when there is just one sector update.
	if (_LOG_INFO(pVol)->uwPrevDirtyFlag & LM_FLAG_SYNC)
	{
		if (VC_CC(pVC) == 0)
		{
			// there is no FAT update
			// we do not need to write log
			*pdwCacheFlag |= FFAT_CACHE_SYNC;
			r = FFAT_OK;
			goto out;
		}
	}

	if (pNode->dwSize < dwSize)
	{
		FFAT_ASSERT((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) == 0);

		r = _logExtendFileSize(pNode, dwSize, dwEOF, pVC, dwCSFlag, pdwCacheFlag, pCxt);
		if ((NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE) && (NODE_C(pNode) == 0))
		{
			r = _logOpenUnlink(pVol, pNode, pVC->pVCE[0].dwCluster, 0, pCxt);
			FFAT_EO(r, (_T("fail to write open unlink log")));
		}
	}
	else
	{
		r = _logShrinkFileSize(pNode, dwSize, dwEOF, pVC, dwCSFlag, pdwCacheFlag, pCxt);
		if ((NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE) && (dwSize == 0))
		{
			r = _logDeleteOpenUnlink(pNode, pCxt);
			FFAT_EO(r, (_T("fail to delete open unlink log")));
		}
	}

out:
	return r;
}


/**
 * this function is called before node unlinking operation for log recovery
 *
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNode			: [IN] node pointer
 * @param		pVC				: [IN] vectored cluster storage
 * @param		dwNUFlag		: [IN] flags for unlink operations
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK		: nothing to do.
 * @return		FFAT_DONE	: unlink operation is successfully done.
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		SEP-16-2006 [DongYoung Seo] First Writing.
 * @version		MAR-30-2009 [JeongWoo Park] remove the wrong condition about skip of log recording
 * @version		OCT-28-2009 [JeongWoo Park] change the rmdir as synchronous operation
 *                                          to protect the wrong log recovery
 *											For the detail, refer the comment of code.
 */
FFatErr
ffat_log_unlink(Node* pNode, FFatVC* pVC, NodeUnlinkFlag dwNUFlag,
				FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	FatDeallocate		stDealloc;
	t_uint32			dwClusterCount;		// count of cluster 

	FatDeallocate		stDeallocEA;		// deallocation information for eXtended Attribute
	FFatVC				stVCEA;				// vectored clusters for eXtended Attribute
	t_uint32			dwClusterEA = 0;	// first cluster of eXtended Attribute

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pVC);

	pVol = NODE_VOL(pNode);
	stVCEA.pVCE = NULL;

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_UNLINK);
	FFAT_ER(r, (_T("log file can not be deleted")));

	if (dwNUFlag & NODE_UNLINK_NO_LOG)
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
		return FFAT_OK;
	}

	// For deletion of a directory, do synchronous operation to reset log area.
	// Otherwise, the following cases can be happened at recovery time.
	// 1) At recovery time, REDO of the previous log slots about child under this directory
	//    will corrupt cluster area that is already used by other file.
	//    (REDO of updateDE/createDE/deleteDE of child will modify the user data of file.)
	//    [Example]
	//		(1) unlink of "/a/dir/file" (2) rmdir of "/a/dir" (dealloc 9444) (3) write "/a/file2" (alloc 9444) (4) Other operation
	//		The 9444 cluster is already used by "/a/file2"
	//		At recovery time, REDO of (1) in log slot will record delete mark of DE at 9444 cluster
	//		because DE of "/a/dir/file" was recored in 9444 cluster of "/a/dir".
	//		So after REDO of (3) step, some user data of "/a/file2" will changed as 0xE5(delete mark of DE).
	//		
	// 2) If power-off during the recovery about RMDIR operation that has de-allocated FAT chains of the directory,
	//    at the next recovery time, REDO of the previous log slots about child under this directory
	//    will do wrong operation because FAT chain of parent directory already freed and
	//    following FAT chain is impossible.
	//	  [Example]
	//		In case of 1)'s example and DEs of the "/a/dir/file" is recorded both 9444 and 9445.
	//		At first recovery time, the REDO operation of (3) step will make 9444/9445 clusters as free.
	//		At second recovery time, the REDO operation of (1) step can not be performed
	//		because sub-log about DE has only start cluster number(9444)
	//		and it can not follow the FAT chain after 9444 that is already freed.
	
	if (NODE_IS_DIR(pNode) == FFAT_TRUE)
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// update deallocate information for log
	stDealloc.dwHintCluster		= 0;
	stDealloc.dwPrevEOF			= 0;
	stDealloc.dwFirstCluster	= NODE_C(pNode);
	stDealloc.pVC				= pVC;

	if (NODE_C(pNode) != 0)
	{
		// node has clusters (get vectored cluster information from offset 0)
		r = ffat_misc_getVectoredCluster(pVol, pNode, NODE_C(pNode), 0, 0,
										pVC, NULL, pCxt);
		// A file will be deleted by force
		if (r < 0)
		{
			*pdwCacheFlag			|= FFAT_CACHE_SYNC;
			stDealloc.dwCount		= VC_CC(pVC);	// we have count 
			stDealloc.dwLastCluster	= VC_LC(pVC);	// we do not have it
		}
		else
		{
			dwClusterCount = ESS_MATH_CDB(NODE_S(pNode), VOL_CS(pVol), VOL_CSB(pVol));
			if (dwClusterCount > VC_CC(pVC))
			{
				// stVC does not have all cluster information
				FFAT_ASSERT(VC_IS_FULL(pVC) == FFAT_TRUE);

				*pdwCacheFlag			|= FFAT_CACHE_SYNC;
				stDealloc.dwCount		= 0;			// we do not have exact count 
				stDealloc.dwLastCluster	= 0;			// we do not have exact last cluster
			}
			else
			{
				// WE HAVE ALL CLUSTER INFORMATION, SO CACHES MAY BE IN DIRTY STATE !!
				stDealloc.dwCount		= VC_CC(pVC);	// we have count 
				stDealloc.dwLastCluster	= VC_LC(pVC);	// we do not have it
			}
		}
	}
	else
	{
		// node does not have cluster
		stDealloc.dwCount		= 0;			// we have count 
		stDealloc.dwLastCluster	= 0;			// we do not have it
	}

	r = ffat_ea_getEAFirstCluster(pNode, &dwClusterEA, pCxt);
	if (r == FFAT_OK)
	{
		FFAT_ASSERT(FFATFS_IsValidCluster((FatVolInfo*)NODE_VI(pNode), dwClusterEA) == FFAT_TRUE);

		VC_INIT(&stVCEA, VC_NO_OFFSET);

		// allocate memory for vectored cluster information
		stVCEA.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
		FFAT_ASSERT(stVCEA.pVCE);

		stVCEA.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

		r = ffat_misc_getVectoredCluster(pVol, pNode, dwClusterEA, FFAT_NO_OFFSET, 0,
										&stVCEA, NULL, pCxt);
		FFAT_EO(r, (_T("fail to get cluster for node")));

		// update deallocate information for log
		stDeallocEA.dwHintCluster	= 0;
		stDeallocEA.dwPrevEOF		= 0;
		stDeallocEA.dwFirstCluster	= dwClusterEA;
		stDeallocEA.pVC				= &stVCEA;
		stDeallocEA.dwCount			= VC_CC(&stVCEA);
		stDeallocEA.dwLastCluster	= VC_LC(&stVCEA);

		if (VC_IS_FULL(&stVCEA) == FFAT_TRUE)
		{
			// stVC does not have all cluster information

			*pdwCacheFlag				|= FFAT_CACHE_SYNC;
			stDeallocEA.dwCount			= 0;
			stDeallocEA.dwLastCluster	= 0;
		}
	}
	else if (r == FFAT_ENOXATTR)
	{
		// there is no extended attribute cluster
		stDeallocEA.dwCount			= 0;
		stDeallocEA.dwLastCluster	= 0;
		stDeallocEA.pVC				= NULL;
	}
	else
	{
		goto out;
	}

	r = _logUnlink(pVol, pNode, &stDealloc, &stDeallocEA, dwNUFlag, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write unlink log")));

	if (dwNUFlag & NODE_UNLINK_OPEN)
	{
		r = _logOpenUnlink(pVol, pNode, NODE_C(pNode), dwClusterEA, pCxt);
		FFAT_EO(r, (_T("fail to write open unlink log")));
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(stVCEA.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	return r;
}

/**
* this function is called at delete open-unlinked node operation for log recovery
*
* @param		pNode		: [IN] node pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: nothing to do.
* @return		else		: error
* @author		JeongWoo Park
* @version		APR-29-2009 [JeongWoo Park] First Writing.
*/
FFatErr
ffat_log_unlinkOpenUnlinkedNode(Node* pNode, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if ((_NODE_OUEI(pNode) == _INVALID_ENTRY_INDEX) &&
		(_NODE_EAEI(pNode) == _INVALID_ENTRY_INDEX))
	{
		FFAT_ASSERT((NODE_C(pNode) == 0) || (NODE_IS_DIR(pNode) == FFAT_TRUE));
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_DELETE_NODE);
	FFAT_ER(r, (_T("log file can not be deleted")));

	// Before deallocation FAT chain, remove the previous log about this node like write,expand,shrink.
	// Otherwise, the lost cluster can be made by log recovery at sudden power off.
	r = _logResetWithClean(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_ER(r, (_T("fali to reset&clean log")));

	return r;
}

/**
 * this function is called after delete open-unlinked node operation for log recovery
 *
 * @param		pNode		: [IN] node pointer
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: nothing to do.
 * @return		else		: error
 * @author		InHwan Choi
 * @version		DEC-05-2007 [InHwan Choi] First Writing.
 * @version		MAR-29-2009 [DongYoung Seo] change function from ffat_log_unlinkOpenUnlinkedNode
 *										to ffat_log_unlinkOpenUnlinkedNodeAfter,
 *										this is after operation for deleting an open unlinked node
 * @version		APR-29-2009 [JeongWoo Park] change function from ffat_log_unlinkOpenUnlinkedNodeAfter
 *										to ffat_log_afterUnlinkOpenUnlinkedNode.
 */
FFatErr
ffat_log_afterUnlinkOpenUnlinkedNode(Node* pNode, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	if ((_NODE_OUEI(pNode) == _INVALID_ENTRY_INDEX) &&
			(_NODE_EAEI(pNode) == _INVALID_ENTRY_INDEX))
	{
		FFAT_ASSERT((NODE_C(pNode) == 0) || (NODE_IS_DIR(pNode) == FFAT_TRUE));
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_DELETE_NODE);
	FFAT_ER(r, (_T("log file can not be deleted")));

	r = _logDeleteOpenUnlink(pNode, pCxt);
	FFAT_ER(r, (_T("fali to delete open unlink")));

	return r;
}


/**
 * this function is called before node status change operation.
 *
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNode		: [IN] node pointer,
 * @param		pStatus		: [IN] node information
 * @param		pdwCacheFlag: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		DongYoung Seo
 * @version		NOV-09-2006 [DongYoung Seo] First Writing.
 * @version		DEC-31-2008 [JeongWoo Park] Bug fix : restore original node operation is wrong.
 * @version		MAR-30-2009 [GwangOk Go] write log in case of updating XDE
 */
FFatErr
ffat_log_setStatus(Node* pNode, FFatNodeStatus* pStatus, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_int32			r;
	FatDeSFN		stDE;		// for SFN backup
	Vol*			pVol;
	XDEInfo*		pOldXDEInfo;
	XDEInfo			stNewXDEInfo;
	t_boolean		bUpdateXDE = FFAT_FALSE;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pStatus);
	FFAT_ASSERT(pdwCacheFlag);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_SET_STAT);
	FFAT_ER(r, (_T("log file can not be set stat")));

	if (VOL_FLAG(pVol) & VOL_ADDON_XDE)
	{
		pOldXDEInfo = &(NODE_ADDON(pNode)->stXDE);

		FFAT_ASSERT((pOldXDEInfo->dwPerm & FFAT_XDE_WRITES) ? ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0) : (pNode->stDE.bAttr & FFAT_ATTR_RO));

		FFAT_MEMCPY(&stNewXDEInfo, pOldXDEInfo, sizeof(XDEInfo));

		if ((pStatus->dwAttr & FFAT_ATTR_RO) && (stNewXDEInfo.dwPerm & FFAT_XDE_WRITES))
		{
			// attribute에 read only 속성이 있고 permission에는 write 속성이 있는 경우
			stNewXDEInfo.dwPerm &= ~FFAT_XDE_WRITES;
			bUpdateXDE = FFAT_TRUE;
		}
		else if (((pStatus->dwAttr & FFAT_ATTR_RO) == 0) && ((stNewXDEInfo.dwPerm & FFAT_XDE_WRITES) == 0))
		{
			// attribute에 read only 속성이 없고, permission에는 write 속성이 없는 경우
			stNewXDEInfo.dwPerm |= FFAT_XDE_WRITES;
			bUpdateXDE = FFAT_TRUE;
		}
	}

	if ((_LOG_INFO(pVol)->uwPrevDirtyFlag & LM_FLAG_SYNC) && (bUpdateXDE == FFAT_FALSE))
	{
		// there is one a DE update
		// we do not need to write log
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
		return FFAT_OK;
	}

	FFAT_MEMCPY(&stDE, &pNode->stDE, FAT_DE_SIZE);

	pNode->stDE.wCrtDate	= FFAT_BO_UINT16((t_uint16)(pStatus->dwCTime >> 16));
	pNode->stDE.wCrtTime	= FFAT_BO_UINT16((t_uint16)(pStatus->dwCTime & 0xFFFF));
	pNode->stDE.wWrtDate	= FFAT_BO_UINT16((t_uint16)(pStatus->dwMTime >> 16));
	pNode->stDE.wWrtTime	= FFAT_BO_UINT16((t_uint16)(pStatus->dwMTime & 0xFFFF));
	pNode->stDE.wLstAccDate	= FFAT_BO_UINT16((t_uint16)(pStatus->dwATime >> 16));
	pNode->stDE.bCrtTimeTenth	= (t_uint8)pStatus->dwCTimeTenth;

	r = ffat_node_updateSFNE(pNode, 0, (t_uint8)(pStatus->dwAttr & FFAT_ATTR_MASK), 0,
						FAT_UPDATE_DE_ATTR, FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to update SFNE")));

	if (bUpdateXDE == FFAT_FALSE)
	{
		// no need to write XDE info
		r = _logSetState(pVol, pNode, &stDE, NULL, pdwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to set node status log")));
	}
	else
	{
		// need to write XDE info
		r = _logSetState(pVol, pNode, &stDE, &stNewXDEInfo, pdwCacheFlag, pCxt);
		FFAT_EO(r, (_T("fail to set node status log")));
	}

	r = FFAT_OK;

out:
	// restore original node information
	FFAT_MEMCPY(&pNode->stDE, &stDE, FAT_DE_SIZE);

	return r;
}


/**
 * this function is called before XDE update operation.
 *
 * @param		pNode		: [IN] node pointer,
 * @param		pNewXDEInfo	: [IN] new XDE information
 * @param		pdwCacheFlag: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: error
 * @author		GwangOk Go
 * @version		MAR-30-2009 [GwangOk Go] First Writing.
 */
FFatErr
ffat_log_updateXDE(Node* pNode, FFatExtendedDirEntryInfo* pNewXDEInfo,
				FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_int32			r;
	FatDeSFN		stDE;		// for SFN backup
	Vol*			pVol;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNewXDEInfo);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pCxt);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_SET_STAT);
	FFAT_ER(r, (_T("log file can not be set stat")));

	FFAT_MEMCPY(&stDE, &pNode->stDE, FAT_DE_SIZE);

	if ((pNewXDEInfo->dwPerm & FFAT_XDE_WRITES) && (pNode->stDE.bAttr & FFAT_ATTR_RO))
	{
		// new permission에는 write 속성이 있으며 read only 속성이 있는 경우
		pNode->stDE.bAttr &= ~FFAT_ATTR_RO;
	}
	else if (((pNewXDEInfo->dwPerm & FFAT_XDE_WRITES) == 0) && ((pNode->stDE.bAttr & FFAT_ATTR_RO) == 0))
	{
		// new permission에는 write 속성이 없으나 read only 속성이 없는 경우
		pNode->stDE.bAttr |= FFAT_ATTR_RO;
	}
/*2010_0317_kyungsik 
	Remove following code to write the log for UID,GID etc.*/
/* 
	else
	{
		// no need to write log
		return FFAT_OK;
	}
*/

	r = _logSetState(pVol, pNode, &stDE, pNewXDEInfo, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to set node status log")));

	r = FFAT_OK;

out:
	// restore original node information
	FFAT_MEMCPY(&pNode->stDE, &stDE, FAT_DE_SIZE);

	return r;
}


/**
 * This function is called before directory truncation.
 *
 * after this function clusters (that connected to the next of dwPrevEOC) will be free.
 * 
 * @param		pNode				: [IN] node(directory) pointer
 * @param		dwPrecEOC			: [IN] the last cluster of pNode
* @param		pdwCacheFlag		: [OUT] flag for cache operation
 * @author		DongYoung Seo
 * @version		SEP-29-2006 [DongYoung Seo] First Writing.
 */
FFatErr
ffat_log_truncateDir(Node* pNode, t_uint32 dwPrevEOC, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_int32					r;
	FatAllocate				stAlloc;
	FFatVC					stVC;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pdwCacheFlag);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	*pdwCacheFlag |= FFAT_CACHE_SYNC;			// In current version, I do not have all of the cluster information

	FFAT_MEMSET(&stAlloc, 0x00, sizeof(FatAllocate));
	stAlloc.dwPrevEOF	= dwPrevEOC;
	stAlloc.pVC		= &stVC;
	VC_INIT(&stVC, VC_NO_OFFSET);

	// gossip by KKAKA.
	// gather clusters for directory truncation ?.. hmm..
	// It makes real performance improvement ?...
	// I'm not sure.. but.. it reduces write operation.
	//		so it is more efficient on the Flash Memory
	
	// ToDo. gather cluster information for log.

	// ffat_log_lookup() is not for real lookup operation
	// It's for directory truncation.
	r = _logTruncateDir(NODE_VOL(pNode), pNode, &stAlloc, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log for directory expansion")));

	r = FFAT_OK;
out:

	return r;
}


/**
 * log for extended attribute create operation 
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwOffset		: [IN] write offset
 * @param		pBuff			: [IN] buffer pointer
 * @param		dwSize			: [IN] write size
 * @param		pFP				: [IN/OUT] file pointer hint
									마지막 write 한 위치로 hint가 update 됨.
									may be NULL.
 * @param		pVC				: [IN/OUT] storage for free clusters
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		InHwan Choi
 * @version		NOV-26-2007 [InHwan Choi] : First Writing.
 * @version		FEB-03-2009 [JeongWoo Park] : Add the consideration for open-unlinked node.
 */
FFatErr
ffat_log_createEA(Node* pNode, FFatVC* pVC, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	FatAllocate			stAlloc;

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_WRITE);
	FFAT_ER(r, (_T("log file can not be written")));

	FFAT_ASSERT(VC_CC(pVC) > 0);

	// set allocated cluster info
	stAlloc.dwCount			= VC_CC(pVC);
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= 0;
	stAlloc.dwFirstCluster	= VC_FC(pVC);
	stAlloc.dwLastCluster	= VC_LC(pVC);
	stAlloc.pVC				= pVC;

	r = _logCreateEA(pVol, pNode, &stAlloc, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		// write open unlink log
		r = _logOpenUnlink(pVol, pNode, 0, VC_FC(pVC), pCxt);
		FFAT_EO(r, (_T("fail to write open unlink log for XATTR")));
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * log for extended attribute set operation 
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwPrevEOC		: [IN] previous last cluster
 * @param		pNewVC			: [IN/OUT] storage for expanded clusters
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-11-2008 [GwangOk Go] : First Writing.
 * @version		DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version		DEC-21-2008 [DongYoung Seo] add cache flag checking code for sync mode
 * @version		Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenAllocateFat
 */
FFatErr
ffat_log_setEA(Node* pNode, FFatVC* pVCOld, FFatVC* pVCNew, t_uint32 udwDelOffset,
			t_uint32 udwInsOffset, EAMain* pEAMain, EAEntry* pEAEntryOld,
			FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	LogHeader*		pLH = NULL;		// pointer for log
	SubLog*			pSL;
	FatAllocate		stAlloc;
	t_int16			wReservedSize;	// reserved size for next logs

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_WRITE);
	FFAT_ER(r, (_T("log file can not be written")));

	// allocate log header
	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_EA_SET, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	if (pVCNew->pVCE == NULL)
	{
		wReservedSize = _RSVD_SIZE_LOG_TAIL;
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
		_sublogGenEA(pVol, pLH, LM_SUBLOG_SET_EA, VC_FC(pVCOld), udwDelOffset, udwInsOffset,
					pEAMain, pEAEntryOld);
	}
	else
	{
		wReservedSize = _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenEA(pVol, pLH, LM_SUBLOG_SET_EA, VC_FC(pVCOld), udwDelOffset, udwInsOffset,
					pEAMain, pEAEntryOld);

		FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
		FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
		FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
		wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;

		FFAT_ASSERT(VC_CC(pVCOld) > 0);
		FFAT_ASSERT(VC_CC(pVCNew) > 0);

		// set deallocated cluster info
		stAlloc.dwCount			= VC_CC(pVCNew);
		stAlloc.dwHintCluster	= 0;				// don't need to fill in
		stAlloc.dwPrevEOF		= VC_LC(pVCOld);
		stAlloc.dwFirstCluster	= VC_FC(pVCNew);
		stAlloc.dwLastCluster	= 0;				// don't need to fill in
		stAlloc.pVC				= pVCNew;

		// allocate cluster
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
		_sublogGenAllocateFat(pVol, pNode, pLH, VC_FC(pVCOld), &stAlloc, wReservedSize);
	}

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	if (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * log for extended attribute deletion operation 
 *
 * @param		pNode			: [IN] node pointer
 * @param		udwFirstCluster	: [IN] first cluster of extended attribute
 * @param		udwDelOffset	: [IN] offset of entry to be deleted
 * @param		pEAMain			: [IN] EAMain
 * @param		pEAEntry		: [IN] EAEntry
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-06-2008 [GwangOk Go] : First Writing
 * @version		DEC-21-2008 [DongYoung Seo] add wReservedSize to check 
 *										is there enough space for next log
 */
FFatErr
ffat_log_deleteEA(Node* pNode, t_uint32 udwFirstCluster, t_uint32 udwDelOffset, EAMain* pEAMain,
				EAEntry* pEAEntry, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	Vol*			pVol;
	LogHeader*		pLH			= NULL;		// pointer for log
	SubLog*			pSL;
	t_int16			wReservedSize;			// reserved size for next logs

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(udwDelOffset != 0);
	FFAT_ASSERT(pEAMain);
	FFAT_ASSERT(pEAEntry);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pCxt);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_WRITE);
	FFAT_ER(r, (_T("log file can not be written")));

	// allocate log header
	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_EA_DELETE, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	wReservedSize = _RSVD_SIZE_EA + _RSVD_SIZE_LOG_TAIL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_EA;

	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	_sublogGenEA(pVol, pLH, LM_SUBLOG_DELETE_EA, udwFirstCluster, udwDelOffset, 0, pEAMain, pEAEntry);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	if (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * log for extended attribute compaction operation 
 *
 * @param		pNode			: [IN] node pointer
 * @param		pOldVC			: [IN] vectored cluster to deallocate
 * @param		pNewVC			: [IN] vectored cluster to allocate
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		GwangOk Go
 * @version		JUN-23-2008 [GwangOk Go] : First Writing.
 * @version		DEC-05-2008 [JeongWoo Park] : remove the case of log recording fail
 *									by the large VC
 * @version		FEB-03-2009 [JeongWoo Park] : Add the consideration for open-unlinked node.
 */
FFatErr
ffat_log_compactEA(Node* pNode, FFatVC* pOldVC, FFatVC* pNewVC, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	FatAllocate			stOldAlloc;
	FatAllocate			stNewAlloc;

	FFAT_ASSERT(pNode);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_WRITE);
	FFAT_ER(r, (_T("log file can not be written")));

	FFAT_ASSERT(VC_CC(pOldVC) > 0);

	// sync mode must be set for compaction EA
	// [STORM/20090203] For open-unlinked node, OU slot can records only newly allocated cluster.
	// so original clusters must be deallocated with sync mode within the life cycle of compaction log slot.
	*pdwCacheFlag |= FFAT_CACHE_SYNC;

	// set deallocated cluster info
	stOldAlloc.dwCount			= VC_CC(pOldVC);
	stOldAlloc.dwHintCluster	= 0;
	stOldAlloc.dwPrevEOF		= 0;
	stOldAlloc.dwFirstCluster	= VC_FC(pOldVC);
	stOldAlloc.dwLastCluster	= VC_LC(pOldVC);
	stOldAlloc.pVC				= pOldVC;

	// set allocated cluster info
	stNewAlloc.dwCount			= VC_CC(pNewVC);
	stNewAlloc.dwHintCluster	= 0;
	stNewAlloc.dwPrevEOF		= 0;
	stNewAlloc.dwFirstCluster	= VC_FC(pNewVC);
	stNewAlloc.dwLastCluster	= VC_LC(pNewVC);
	stNewAlloc.pVC				= pNewVC;

	r = _logCompactEA(pVol, pNode, &stOldAlloc, &stNewAlloc, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		// write open unlink log for newly allocated clusters
		r = _logOpenUnlink(pVol, pNode, 0, VC_FC(pNewVC), pCxt);
		FFAT_EO(r, (_T("fail to write open unlink log for XATTR")));
	}

	r = FFAT_OK;

out:
	return r;
}


/**
 * log for write operation 
 *
 * @param		pNode			: [IN] node pointer
 * @param		dwLastOffset	: [IN] the last write offset + 1 (write offset + size) <== new file size
 * @param		dwPrevEOC		: [IN] previous last cluster
 *										this may be 0 when there is no cluster allocation
 * @param		pVC_Cur			: [IN] storage for clusters that are used for write
 * @param		pVC_New			: [IN] new clusters that are used for write
 * @param		dwWriteFlag		: [IN] flag for write operation
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] : First Writing.
 * @history		FEB-06-2006 [DongYoung Seo] : update first cluster information.
 * @history		DEC-08-2007 [InHwan Choi] : apply to open unlink
 * @history		OCT-10-2008 [GwangOk Go] : receive log info (free clusters to be allocated) from CORE module
 * @version		OCT-29-2008 [DongYoung Seo] remove parameter *pdwNewClusters and *dwRequestClusters
 * @version		OCT-27-2009 [JW Park] Add the consideraion about dirty-size node.
 */
FFatErr
ffat_log_writeFile(Node* pNode, t_uint32 dwLastOffset, t_uint32 dwPrevEOC, 
					FFatVC* pVC_Cur, FFatVC* pVC_New, FFatWriteFlag dwWriteFlag,
					FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	Vol*				pVol;
	FFatErr				r;
	FatDeSFN			stDeOrig;
	FatDeUpdateFlag		dwDeUpdateFlag;
	FatAllocate			stAlloc;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pVC_Cur);
	FFAT_ASSERT(pVC_New);
	FFAT_ASSERT((dwPrevEOC == 0) ? (VC_IS_EMPTY(pVC_New) != FFAT_TRUE) : FFAT_TRUE);

	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNode, NODE_ACCESS_WRITE);
	FFAT_ER(r, (_T("log file can not be written")));

	if (VC_IS_EMPTY(pVC_New) == FFAT_TRUE)
	{
		// how to treat this case.
		//	1. we do not need to allocate cluster
		//	2. updating a directory entry is all of the work.

		//	sync or write lazy log
		//	sync win !! ==> when there is no other small write.
		//	lazy log win!! ==> when there is some consecutive write requests..

		// let's just go out. with out logging. this is just one sector write.
		// do not need to write log and delay DE synchronization

		// @20070913-iris: for HSDPA, 할당 받을 cluster가 없다면, DE를 update하지 않는다.
		return FFAT_OK;
	}

	IF_UK (VC_IS_FULL(pVC_New) == FFAT_TRUE)
	{
		// there are not all cluster info
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// store original SFNE
	FFAT_MEMCPY(&stDeOrig, &pNode->stDE, sizeof(FatDeSFN));

	dwDeUpdateFlag = FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_MTIME | FAT_UPDATE_DE_ATIME;

	if ((dwWriteFlag & FFAT_WRITE_RECORD_DIRTY_SIZE) == 0)
	{
		// If normal write like sync write, then remove the dirty-size state
		dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
	}

	// directory entry update
	if (NODE_C(pNode) == 0)
	{
		FFAT_ASSERT(NODE_S(pNode) == 0);
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), VC_FC(pVC_New)) == FFAT_TRUE);
		FFAT_ASSERT(VC_CC(pVC_New) > 0);

		// update directory entry
		// update cluster
		r = ffat_node_updateSFNE(pNode, dwLastOffset, 0, VC_FC(pVC_New),
								(dwDeUpdateFlag | FAT_UPDATE_DE_CLUSTER),
								FFAT_CACHE_NONE, pCxt);
	}
	else
	{
		// update directory entry
		r = ffat_node_updateSFNE(pNode, ESS_GET_MAX(pNode->dwSize, dwLastOffset),
								0, 0, dwDeUpdateFlag, FFAT_CACHE_NONE, pCxt);
	}
	FFAT_EO(r, (_T("fail to update SFNE")));

	// for log
	stAlloc.dwCount			= VC_CC(pVC_New);
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= dwPrevEOC;
	if (VC_CC(pVC_New) > 0)
	{
		stAlloc.dwFirstCluster	= VC_FC(pVC_New);
	}
	else
	{
		stAlloc.dwFirstCluster	= 0;
	}
	stAlloc.dwLastCluster	= 0;
	stAlloc.pVC				= pVC_New;

	if ((NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE) && (NODE_C(pNode) == 0))
	{
		r = _logOpenUnlink(pVol, pNode, VC_FC(pVC_New), 0, pCxt);
		FFAT_EO(r, (_T("fail to write open unlink log")));
	}

	r = _logFileWrite(pVol, pNode, &stDeOrig, &stAlloc, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write log")));

	r = FFAT_OK;

out:
	// restore original SFNE
	FFAT_MEMCPY(&pNode->stDE, &stDeOrig, sizeof(FatDeSFN));

	return r;
}


/**
 * this function is called before node rename operation.
 *
 * no need to lock node
 * parameter validity check는 다시 수행할 필요없다.
 *
 * @param		pNodeSrcParent	: [IN] parent node of Source
 * @param		pNodeSrc		: [IN] source node pointer
 * @param		pNodeDesParent	: [IN] parent of destination node
 * @param		pNodeDes		: [IN] destination node pointer
 *									may be NULL, when destination node is not exist
 * @param		pNodeNewDes		: [IN] new destination node pointer
 * @param		psName			: [IN] node name
 * @param		dwFlag			: [IN] rename flag
 *										must care FFAT_RENAME_TARGET_OPENED
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		22-SEP-2006 [DongYoung Seo] First Writing.
 * @version		08-DEC-2008 [DongYoung Seo] open rename support
 * @version		08-DEC-2008 [DongYoung Seo] eXtended Attribute Support
 * @version		15-DEC-2008 [JeongWoo Park] Add the code to logging for EA cluster(even not open status)
 */
FFatErr
ffat_log_rename(Node* pNodeSrcParent, Node* pNodeSrc, Node* pNodeDesParent,
			Node* pNodeDes, Node* pNodeNewDes, t_wchar* psName,
			FFatRenameFlag dwFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	t_boolean		bUpdateDe		= FFAT_FALSE;	// just directory entry update or not
													// always FFAT_FALSE in current version
	t_boolean		bDeleteSrcDe	= FFAT_TRUE;	// delete source directory entry
													// always FFAT_TRUE in current version
	t_boolean		bDeleteOldDesDe	= FFAT_TRUE;	// delete DE of old destination,
	t_boolean		bCreateDesDe	= FFAT_TRUE;	// create destination directory entry
													// always FFAT_TRUE in current version.
	FatDeallocate*	pDeallocateOld	= NULL;			// old destination's clusters
	FatDeallocate	stDeallocateOld;
	FFatVC			stVC_Dealloc;
	Vol*			pVol;
	FatDeSFN*		pDotDotOrig		= NULL;			// original DOTDOT entry
	FatDeSFN*		pDotDot			= NULL;			// new DOTDOT entry for directory

	FatDeallocate	stDeallocEA;					// deallocation information for eXtended Attribute
	FFatVC			stVCEA;							// vectored clusters for eXtended Attribute
	t_uint32		dwClusterEA = 0;				// first cluster of eXtended Attribute

	t_int32			r;

	FFAT_ASSERT(pNodeSrcParent);
	FFAT_ASSERT(pNodeSrc);
	FFAT_ASSERT(psName);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pNodeNewDes);

	pVol = NODE_VOL(pNodeSrc);

	// is log enabled ?
	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		*pdwCacheFlag |= _META_SYNC_FOR_NO_LOG;
		return FFAT_OK;
	}

	r = ffat_log_isAccessable(pNodeSrc, NODE_ACCESS_RENAME);
	FFAT_ER(r, (_T("log file can not be renamed")));

	if (pNodeDes != NULL)
	{
		r = ffat_log_isAccessable(pNodeDes, NODE_ACCESS_RENAME);
		FFAT_ER(r, (_T("log file can not be renamed")));
	}

	// init FFatVC for EA
	stVCEA.dwTotalClusterCount	= 0;
	stVCEA.dwValidEntryCount	= 0;
	stVCEA.pVCE					= NULL;

	// init FatDeallocate for EA
	stDeallocEA.dwCount			= 0;
	stDeallocEA.dwLastCluster	= 0;
	stDeallocEA.pVC				= NULL;

	// init FFatVC for data
	stVC_Dealloc.pVCE			= NULL;

	if (pNodeDes == NULL)
	{
		// target is not exist
		bDeleteSrcDe	= FFAT_TRUE;		// delete source DE
		bDeleteOldDesDe = FFAT_FALSE;		// do not delete old destination
		bCreateDesDe	= FFAT_TRUE;		// create new destinations
	}
	else if (ffat_node_isSameNode(pNodeSrc, pNodeDes) == FFAT_TRUE)
	{
		// target is exist and it is equaL to pNodeSrc
		FFAT_ASSERT(pNodeNewDes);

		pNodeDes	= NULL;		// set node DES to null, because it is not exist

		bDeleteSrcDe	= FFAT_TRUE;		// delete source DE
		bDeleteOldDesDe = FFAT_FALSE;		// do not delete old destination
		bCreateDesDe	= FFAT_TRUE;		// create new destinations
	}
	else
	{
		// target is exist and it is not a same node
		FFAT_ASSERT(pNodeNewDes);

		bDeleteSrcDe	= FFAT_TRUE;		// delete DE for source
		bDeleteOldDesDe = FFAT_TRUE;		// delete DE for old destination
		bCreateDesDe	= FFAT_TRUE;		// create DE for new destination

		FFAT_ASSERT(NODE_IS_VALID(pNodeDes) == FFAT_TRUE);

		if (NODE_C(pNodeDes) != 0)
		{
			VC_INIT(&stVC_Dealloc, 0);

			pDeallocateOld = &stDeallocateOld;
			stDeallocateOld.dwHintCluster	= 0;
			stDeallocateOld.dwPrevEOF		= 0;
			stDeallocateOld.dwFirstCluster	= pNodeDes->dwCluster;	// set cluster number

			stVC_Dealloc.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
			FFAT_ASSERT(stVC_Dealloc.pVCE);

			stVC_Dealloc.dwTotalEntryCount	= FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

			stDeallocateOld.pVC				= &stVC_Dealloc;

			// get vectored cluster information from offset 0
			r = ffat_misc_getVectoredCluster(pVol, pNodeDes, NODE_C(pNodeDes), 0,
											0, &stVC_Dealloc, NULL, pCxt);
			FFAT_EO(r, (_T("fail to get clusters for a node")));

			FFAT_ASSERT(VC_CC(&stVC_Dealloc) > 0);
			stDeallocateOld.dwCount			= VC_CC(&stVC_Dealloc);
			stDeallocateOld.dwLastCluster	= VC_LC(&stVC_Dealloc);

			if (VC_IS_FULL(&stVC_Dealloc) == FFAT_TRUE)
			{
				// there is not all of the cluster for destination node
				*pdwCacheFlag					|= FFAT_CACHE_SYNC;
				stDeallocateOld.dwCount			= 0;
				stDeallocateOld.dwLastCluster	= 0;
			}
		}

		// GET CLUSTER FOR EA
		r = ffat_ea_getEAFirstCluster(pNodeDes, &dwClusterEA, pCxt);
		if (r == FFAT_OK)
		{
			FFAT_ASSERT(FFATFS_IsValidCluster((FatVolInfo*)NODE_VI(pNodeDes), dwClusterEA) == FFAT_TRUE);

			VC_INIT(&stVCEA, VC_NO_OFFSET);

			// allocate memory for vectored cluster information
			stVCEA.pVCE = (FFatVCE*)FFAT_LOCAL_ALLOC(FFAT_ALLOC_BUFF_SIZE, pCxt);
			FFAT_ASSERT(stVCEA.pVCE);

			stVCEA.dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

			r = ffat_misc_getVectoredCluster(pVol, pNodeDes, dwClusterEA, FFAT_NO_OFFSET, 0,
											&stVCEA, NULL, pCxt);
			FFAT_EO(r, (_T("fail to get cluster for node")));

			// update deallocate information for log
			stDeallocEA.dwHintCluster	= 0;
			stDeallocEA.dwPrevEOF		= 0;
			stDeallocEA.dwFirstCluster	= dwClusterEA;
			stDeallocEA.pVC				= &stVCEA;
			stDeallocEA.dwCount			= VC_CC(&stVCEA);
			stDeallocEA.dwLastCluster	= VC_LC(&stVCEA);

			if (VC_IS_FULL(&stVCEA) == FFAT_TRUE)
			{
				// stVC does not have all cluster information

				*pdwCacheFlag				|= FFAT_CACHE_SYNC;
				stDeallocEA.dwCount			= 0;
				stDeallocEA.dwLastCluster	= 0;
			}
		}
		else if (r != FFAT_ENOXATTR)
		{
			goto out;
		}
	}

	// check the node is directory and parent is not same.
	// --> entry for ".." should be updated
	if((NODE_C(pNodeSrcParent) != NODE_C(pNodeDesParent)) && (NODE_IS_DIR(pNodeSrc) == FFAT_TRUE))
	{
		// allocate memory for DOTDOT entry
		pDotDotOrig = (FatDeSFN*) FFAT_LOCAL_ALLOC(sizeof(FatDeSFN), pCxt);
		FFAT_ASSERT(pDotDotOrig);

		pDotDot = (FatDeSFN*) FFAT_LOCAL_ALLOC(sizeof(FatDeSFN), pCxt);
		FFAT_ASSERT(pDotDot);

		// update ".."
		FFAT_ASSERT((FFATFS_GetDeCluster(VOL_VI(pVol), &pNodeSrc->stDE)) > 0);

		r = ffat_readWritePartialCluster(pVol, pNodeSrcParent,
					FFATFS_GetDeCluster(VOL_VI(pVol), &pNodeSrc->stDE),
					0x20, sizeof(FatDeSFN), (t_int8*)pDotDotOrig, FFAT_TRUE,
					FFAT_CACHE_DATA_DE, pCxt);
		FFAT_EO(r, (_T("fail to read directory entry for \"..\"")));

		FFAT_MEMCPY(pDotDot, pDotDotOrig, sizeof(FatDeSFN));

		// 0 means root directory.
		if (NODE_C(pNodeDesParent) == FFATFS_FAT16_ROOT_CLUSTER)
		{
			FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

			r = FFATFS_SetDeCluster(pDotDot, 0);
		}
		else
		{
			r = FFATFS_SetDeCluster(pDotDot, NODE_C(pNodeDesParent));
		}
		FFAT_EO(r, (_T("fail to update directory entries of src")));
	}

	r = _logRename(pVol, pNodeSrc, pNodeDes, pNodeNewDes, psName,
						bUpdateDe, bDeleteSrcDe, bDeleteOldDesDe, bCreateDesDe,
						pDeallocateOld, &stDeallocEA, pDotDotOrig, pDotDot,
						dwFlag, pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("fail to write rename log")));

	// [BUG FIX : CQ FLASH00019484] wrong condition check : Just check whether target is opened
	//if ((stDeallocEA.dwCount > 0) || (dwFlag & FFAT_RENAME_TARGET_OPENED))
	if (dwFlag & FFAT_RENAME_TARGET_OPENED)
	{
		r = _logOpenUnlink(pVol, pNodeDes, NODE_C(pNodeDes), dwClusterEA, pCxt);
		FFAT_EO(r, (_T("fail to write open unlink log")));
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pDotDot, sizeof(FatDeSFN), pCxt);
	FFAT_LOCAL_FREE(pDotDotOrig, sizeof(FatDeSFN), pCxt);
	FFAT_LOCAL_FREE(stVCEA.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);
	FFAT_LOCAL_FREE(stVC_Dealloc.pVCE, FFAT_ALLOC_BUFF_SIZE, pCxt);

	return r;
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
ffat_log_isAccessable(Node* pNode, NodeAccessFlag dwFlag)
{
	if (ffat_log_isLogNode(pNode) == FFAT_TRUE)
	{
		if (dwFlag & NODE_ACCESS_MASK)
		{
			return FFAT_EACCLOGFILE;
		}
	}

	return FFAT_OK;
}


/** 
* ffat_log_isLogFile check whether a node is log file or not
* 
* @param 		pNode		: [IN] Node to be checked 
* 
* @return 		FFAT_TRUE	: is log file
* @return 		FFAT_TRUE	: not log file
* @author 		ZhangQing
* @version		JAN-29-2008 [DongYoung Seo] : modify for hidden log area
*/
t_boolean
ffat_log_isLogNode(Node* pNode)
{
	if (_IS_LOGGING_ENABLED(NODE_VOL(pNode)) == FFAT_FALSE)
	{
		return FFAT_FALSE;
	}

	if ((_LOG_INFO(NODE_VOL(pNode))->dwFirstCluster != 0) &&
		(NODE_C(pNode) == _LOG_INFO(NODE_VOL(pNode))->dwFirstCluster))
	{
		return FFAT_TRUE;
	}

	return FFAT_FALSE;
}


/**
 * initialize log area on formatting
 *
 * @param	pVol 			: [IN] volume pointer
 * @param	pDevice			: [IN] device pointer
 * @param	dwStartSector	: [IN] start sector of log area
 * @param	dwEndSector		: [IN] end sector of log area
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_EIO		: fail to write log header
 * @author	GwangOk Go
 * @version	JUN-01-2009 [GwangOk Go] First Writing.
 */
FFatErr
ffat_log_initLogArea(Vol* pVol, void* pDevice, t_uint32 dwStartSector, t_uint32 dwEndSector, ComCxt* pCxt)
{
	FFatErr			r;
	t_int8*			pBuff;
	LogHeader*		pLogHeader;
	t_uint32		dwCurSector;
	t_int32			dwRetCount;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pDevice);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(dwStartSector < dwEndSector);
	FFAT_ASSERT((dwEndSector - dwStartSector + 1) == LOG_SECTOR_COUNT);

	pBuff = FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pBuff != NULL);

	FFAT_MEMSET(pBuff, 0x00, VOL_SS(pVol));

	pLogHeader = (LogHeader*)pBuff;

	pLogHeader->udwLogVer = _LOG_VERSION;

	_boLogHeader(pLogHeader);

	for (dwCurSector = dwStartSector; dwCurSector <= dwEndSector; dwCurSector++)
	{
		// write log header
		dwRetCount = ffat_ldev_writeSector(pDevice, dwCurSector, pBuff, 1);
		IF_UK (dwRetCount != 1)
		{
			FFAT_LOG_PRINTF((_T("Fail to write log header")));
			r = FFAT_EIO;
			goto out;
		}
	}

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

	return r;
}


//=============================================================================
//
// for HIDDEN PROTECTED AREA
//


/**
 * enable logging
 * 
 * @param		pVol				: [IN] pointer of volume
 * @param		pdwFlag				: [INOUT] mount flag
 * @param		bReMount			: [IN] is this remount?
 * @param		pCxt				: [IN] context of current operation
 * @return		FFAT_OK				: success
 * @return		FFAT_OK1			: success but log is off
 * @return		FFAT_NOMEM			: not enough memory
 * @return		FFAT_ERECOVERYFAIL	: there were some error while recover from log data.
 * @author
 * @version
 * @history		FEB-13-2007 [DongYoung Seo] do not use log on read-only volume
 * @history		MAR-26-2007 [DongYoung Seo] release log info on error
 * @history		DEC-04-2007 [InHwan Choi] apply to open unlink
 * @history		DEC-11-2008 [DongYoung Seo] add meta-data sync code after log recovery
 * @history		DEC-12-2008 [DongYoung Seo] make a sub-routine for ffat_log_mount() and fat_log_remount()
 * @history		FEB-14-2009 [DongYoung Seo] change return value, return FFAT_ERECOVERYFAIL 
 *										on most error except no memory and IO Error
 * @history		23-MAR-2009 [JeongWoo Park] : change the mount flag as In/Out to notify auto log-off.
 *											  if remount, auto log-off will return error.
 */
static FFatErr
_enableLogging(Vol* pVol, FFatMountFlag* pdwFlag, t_boolean bReMount, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint16		wValidSlots;
	LogInfo*		pLI;

	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_NO_LOG) == 0);
	FFAT_ASSERT((*pdwFlag & FFAT_MOUNT_RDONLY) == 0);
	FFAT_ASSERT(pVol);

	*pdwFlag |= FFAT_LOG_DEFAULT_MOUNT_FLAG;

	r = _getFreeLogInfo(pVol);
	IF_UK (r < 0)
	{
		return r;
	}

	r = _openAndCreateLogArea(pVol, pCxt);
	if (r < 0)
	{
		if (r == FFAT_ENOSPC)
		{
			FFAT_LOG_PRINTF((_T("There is not enough free space for log file - log disable")));

			if (bReMount == FFAT_FALSE)
			{
				_releaseLogInfo(pVol);	// release log info

				// update mount flag if log is off.
				*pdwFlag	&= (~FFAT_MOUNT_LOG_MASK);
				*pdwFlag	|= FFAT_MOUNT_NO_LOG;

				// at Mount(), just log off and return FFAT_OK
				r = FFAT_OK;
			}
		}
		else if (r == FFAT_ERR1)
		{
			FFAT_LOG_PRINTF((_T("There is a directory that has log file name - mount fail")));
			r = FFAT_EINVALID;
		}

		goto out;
	}

	_LOG_RESTORE(pVol, *pdwFlag, pCxt);		// restore log for debugging

	wValidSlots = 0;
	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(pLI);

	pLI->pLLW = _allocLLW(pVol);
	if (pLI->pLLW == NULL)
	{
		FFAT_LOG_PRINTF((_T("Fail to alloc memory fro LLW")));
		r = FFAT_ENOMEM;
		goto out;
	}

	// analysis the log file and make file system to consistent state
	r = _logRecovery(pVol, &wValidSlots, &pLI->udwSeqNum, pCxt);
	FFAT_EO(r, (_T("fail to recover from log")));		// maybe we should forget it without "goto out" if log recovery failed

	r = _logRecoveryOpenUnlink(pVol, pCxt);
	FFAT_EO(r, (_T("fail to recover from open unlink log")));

	// synchronize recovery result to storage
	r = _logReset(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("fail to sync recovery result")));

	r = FFATFS_SyncVol(VOL_VI(pVol), FFAT_CACHE_SYNC, pCxt);
	FFAT_ER(r, (_T("fail to sync volume")));

	// backup the log file, ignore error.
	_LOG_BACKUP(pVol, (t_int16)wValidSlots, pCxt);

	if (wValidSlots > 0)
	{
		pLI->dwFlag &= (~LI_FLAG_NO_LOG);		// clean no log flag

		// write empty slot, ignore error.
		// this prevent further log recovery
		r = _logResetWithClean(pVol, FFAT_CACHE_NONE, pCxt);
		FFAT_EO(r, (_T("fail to reset log")));
	}

	// The init process is successful, make log enable
	// otherwise the log function is disabled
	pVol->dwFlag |= VOL_ADDON_LOGGING;

	if (*pdwFlag & FFAT_MOUNT_LOG_LLW)
	{
		pVol->dwFlag |= VOL_ADDON_LLW;
	}

	if (*pdwFlag & FFAT_MOUNT_LOG_FULL_LLW)
	{
		pVol->dwFlag |= VOL_ADDON_LLW;
		pVol->dwFlag |= VOL_ADDON_FULL_LLW;
	}

	FFAT_ASSERT(pLI->dwFlag & LI_FLAG_NO_LOG);

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("SeqNum:%d \n"), pLI->udwSeqNum));

	r = FFAT_OK;

out:
	if (r < 0)
	{
		_releaseLogInfo(pVol);	// release log info
		_freeLLW(pVol);

		// update mount flag if log is off.
		*pdwFlag	&= (~FFAT_MOUNT_LOG_MASK);
		*pdwFlag	|= FFAT_MOUNT_NO_LOG;

		if (*pdwFlag & FFAT_MOUNT_FORCE)
		{
			FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE);
			return FFAT_OK;
		}

		if ((r != FFAT_ENOMEM) &&
			(r != FFAT_EIO) &&
			(r != FFAT_ENOSPC))
		{
			// set log recovery error
			r = FFAT_ERECOVERYFAIL;
		}
	}

	return r;
}


/**
* disable logging feature
* 
* @param	pVol		: [IN] pointer of volume 
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success
* @return	else		: failed
* @author	DongYoung Seo
* @version	12-DEC-2008 [DongYoung Seo] make a sub-routine from ffat_log_umount()
*/
static FFatErr
_disableLogging(Vol* pVol, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);

	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		FFAT_ASSERT(_LOG_INFO(pVol) == NULL);
		return FFAT_OK;
	}

	r = _logResetWithClean(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_ER(r, (_T("Fail to sync and reset log")));

	r = _commitOpenUnlink(pVol, pCxt);
	FFAT_ER(r, (_T("Fail to sync and reset log")));

	_freeLLW(pVol);
	_releaseLogInfo(pVol);

	pVol->dwFlag		&= (~VOL_ADDON_LOGGING);

	return FFAT_OK;
}


/** 
* ffat_log_hpa write log for HPA
* 
* @param		pVol		: [IN] volume pointerNode to be checked 
* @param		dwLogtype	: [IN] type of log
* @param		pCxt		: [IN] context of current operation
* @param		FFAt_OK		: success
* @return		FFAT_ENOMEM	: not enough memory
* @author		DongYoung Seo
* @version		MAY-26-2007 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_log_hpa(Vol* pVol, LogType dwLogType, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH = NULL;			// pointer for log

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwLogType & LM_LOG_HPA_CREATE);

	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	r = _allocAndInitLogHeader(pVol, &pLH, dwLogType, FFAT_CACHE_SYNC, pCxt);
	FFAT_ASSERT(r == FFAT_OK);

	_sublogGenNoLog(pLH);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	r = FFAT_OK;

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
* get cluster number for entry of a open unlink slot
* 
* @param	pVol			: [IN] pointer of volume 
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
* @return	FFAT_ENOENT		: No valid cluster in the index
* @return	FFAT_EINVALID	: Invalid dwIndex, over slot range
*							: Invalid request, log is not enabled.
* @return	else			: error
* @author	DongYoung Seo
* @version	27-NOV-2008 [DongYoung Seo] First Writing.
*/
FFatErr
ffat_log_getClusterOfOUEntry(Vol* pVol, t_int32 dwIndex, t_uint32* pdwCluster, ComCxt* pCxt)
{
	t_int32			dwIndexSlot;		// index of slot
	t_int32			dwIndexBitmap;		// index of bitmap
	OULogHeader*	pOULog = NULL;		// header of open unlink
	t_uint32*		pOULogSlot;			// log slot for clusters
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(dwIndex >= 0);
	FFAT_ASSERT(pdwCluster);

	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_EINVALID;
	}

	dwIndexSlot = dwIndex / LOG_OPEN_UNLINK_ENTRY_SLOT;
	dwIndexBitmap = dwIndex % LOG_OPEN_UNLINK_ENTRY_SLOT;

	if (dwIndexSlot >= LOG_OPEN_UNLINK_SLOT)
	{
		return FFAT_EINVALID;
	}

	pOULog = (OULogHeader*) FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pOULog);

	// read open unlink slot sector
	r = _readSlotOpenUnlink(pVol, pOULog, dwIndexSlot, pCxt);
	FFAT_EO(r, (_T("fail to read open unlink log header.")));

	if (pOULog->udwValidEntry == 0)
	{
		r = FFAT_ENOENT;
		goto out;
	}

	if (ESS_BITMAP_IS_SET((t_uint8*)pOULog->pBitmap, dwIndex) == ESS_TRUE)
	{
		pOULogSlot = (t_uint32*)(pOULog + (LOG_OPEN_UNLINK_HEADER_SIZE / sizeof(OULogHeader)));

		*pdwCluster = pOULogSlot[dwIndexBitmap];
		FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), *pdwCluster) == FFAT_TRUE);

		r = FFAT_OK;
	}
	else
	{
		r = FFAT_ENOENT;
	}

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pOULog, VOL_SS(pVol), pCxt);

	return r;
}


//=============================================================================
//
// static functions

/**
* Get log ID
* @param	dwLogType		: [IN] transaction log type
* @return				: index for the given log type
* @version	MAY-26-2007 [DongYoung Seo] remove switch case paragraph. replace it to Log2
*/
static t_uint32
_getLogId(t_uint32 udwLogType)
{
	t_uint32		dwLogId;

	if (udwLogType == LM_LOG_NONE)
	{
		return _MAX_LOG_TYPE;
	}

	//remove backup flag
	udwLogType &= ~LM_LOG_FLAG_MASK;

	dwLogId = (t_uint16)(EssMath_Log2(udwLogType) + 1);

	return dwLogId;
}


#ifdef FFAT_DYNAMIC_ALLOC
	/**
	* _getFreeLogInfo Storage for dynamic memory allocation
	* @return	NULL		: failed, there is no free Log Info
	* @return	else		: success
	* @version	MAY-08-2008 [DongYoung Seo] First write
	*/
	static VolLogInfo*
	_allocLogInfoDynamic(void)
	{
		return (VolLogInfo*)FFAT_MALLOC(sizeof(VolLogInfo), ESS_MALLOC_NONE);
	}
#else
	/**
	* _getFreeLogInfo Storage for static memory allocation
	* @return	NULL		: failed, there is no free Log Info
	* @return	else		: success
	* @version	MAY-08-2008 [DongYoung Seo] First write
	*/
	static VolLogInfo*
	_allocLogInfoStatic(void)
	{
		EssList		*pList;
		VolLogInfo	*pLI;

		if (ESS_LIST_IS_EMPTY(&_LOG_MAIN()->stFreeList) == ESS_TRUE)
		{
			FFAT_LOG_PRINTF((_T("There is no more free volume info entry")));
			return NULL;
		}

		pList = ESS_LIST_GET_HEAD(&_LOG_MAIN()->stFreeList);
		pLI = ESS_GET_ENTRY(pList, VolLogInfo, stList);

		// remove entry from list
		ESS_LIST_REMOVE_HEAD(&_LOG_MAIN()->stFreeList);

		return pLI;
	}
#endif


/**
* _getFreeLogInfo get or alloc a free log information
* 
* @param	pVol		: [IN] pointer of volume
* 
* @return	FFAT_OK		: success
* @return	else		: failed
* @author
* @version	MAR-15-2009 [DongYoung Seo]: bug fix:FLASH00020756
*								add initialization code for pLI->stLogInfo
*/
static FFatErr
_getFreeLogInfo(Vol* pVol)
{
	VolLogInfo		*pLI;

	FFAT_ASSERT(pVol);

	pLI = _ALLOC_FREE_LOG_INFO();
	if (pLI == NULL)
	{
		// not enough memory
		FFAT_LOG_PRINTF((_T("Fail to allocate memory for log information")));
		return FFAT_ENOMEM;
	}

	FFAT_MEMSET(&pLI->stLogInfo, 0x00, sizeof(LogInfo));

	_LOG_INFO(pVol) = &pLI->stLogInfo;

	_LOG_INFO(pVol)->dwFlag		= LI_FLAG_NONE;
	_LOG_INFO(pVol)->wCurSlot	= 0;

	// initialize prevDirty Flag
	_LOG_INFO(pVol)->uwPrevDirtyFlag	= LM_FLAG_SYNC;
	_LOG_INFO(pVol)->udwPrevLogType		= LM_LOG_NONE;

	return FFAT_OK;
}


/** 
 * _releaseLogInfo release memory usage
 * 
 * @param pVol		: [IN] pointer of volume 
 * 
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_releaseLogInfo(Vol* pVol)
{
	VolLogInfo*		pLI;

	FFAT_ASSERT(pVol);

	pLI = ESS_GET_ENTRY(_LOG_INFO(pVol), VolLogInfo, stLogInfo);

#ifdef FFAT_DYNAMIC_ALLOC

	FFAT_FREE(pLI, sizeof(VolLogInfo));

#else

	ESS_LIST_ADD_HEAD(&_LOG_MAIN()->stFreeList, &pLI->stList);

#endif

	// reset log info pointer
	_LOG_INFO(pVol) = NULL;

	return FFAT_OK;
}


#ifdef FFAT_BIG_ENDIAN

	/**
	 * changes byte-order of a sub log series
	 * 
	 * @param		pLH			: [IN/OUT] log header information of log slot
	 * @param		bLogWrite	: [IN]	FFAT_TRUE : set byte order for FAT spec (LITTLE ENDIAN), For log write
	 *									FFAT_FALSE: set byte order for system, for recovery
	 * @author		DongYoungSeo
	 * @version		AUG-18-2008 [DongYoung Seo] First write
	 */
	static void
	_adjustByteOrderSubLogs(SubLog* pSL, t_boolean bLogWrite)
	{
		SubLogFat*			pFat;			// sub log for FAT related work
		FFatVCE*			pVCE;			// a temporary vectored cluster entry

		_SubLogFlag			uwNextSLFlag;
		_SubLogType			uwNextSLType;
		t_boolean			bLogRecovery;
		t_int16				wSubLogSize;	// size of a sub log

		FFAT_ASSERT(pSL);

		IF_LK (bLogWrite == FFAT_TRUE)
		{
			bLogRecovery = FFAT_FALSE;
		}
		else
		{
			bLogRecovery = FFAT_TRUE;
		}

		do
		{
			IF_UK (bLogWrite == FFAT_TRUE)
			{
				uwNextSLFlag	= pSL->stSubLogHeader.uwNextSLFlag;
				uwNextSLType	= pSL->stSubLogHeader.uwSublogType;
			}
			else
			{
				uwNextSLFlag	= FFAT_BO_UINT16(pSL->stSubLogHeader.uwNextSLFlag);
				uwNextSLType	= FFAT_BO_UINT16(pSL->stSubLogHeader.uwSublogType);
			}

			FFAT_ASSERT((uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE) || (uwNextSLFlag == LM_SUBLOG_FLAG_LAST));

			_boSubLogHeader(&pSL->stSubLogHeader);

			switch (uwNextSLType)
			{
				case LM_SUBLOG_NONE: 
					// NOTHING TO DO. HEADER ONLY.
					wSubLogSize = _sublogGetSize(pSL);
					break;

				case LM_SUBLOG_ALLOC_FAT:
				case LM_SUBLOG_DEALLOC_FAT:
					pFat = &pSL->u.stFat;

					IF_UK (bLogRecovery == FFAT_TRUE)
					{
						_boSubLogFat(pFat);
					}

					pVCE = (FFatVCE*)((t_int8*)pFat + sizeof(SubLogFat));
					_boSubLogFatVCE(pVCE, pFat->dwValidEntryCount);

					wSubLogSize = _sublogGetSize(pSL);

					IF_LK (bLogWrite == FFAT_TRUE)
					{
						_boSubLogFat(pFat);
					}

					break;

				case LM_SUBLOG_UPDATE_DE:
					IF_LK (bLogWrite == FFAT_TRUE)
					{
						wSubLogSize = _sublogGetSize(pSL);
						_boSubLogUpdateDe(&pSL->u.stUpdateDE);
					}
					else
					{
						_boSubLogUpdateDe(&pSL->u.stUpdateDE);
						wSubLogSize = _sublogGetSize(pSL);
					}
					break;

				case LM_SUBLOG_CREATE_DE:
					IF_LK (bLogWrite == FFAT_TRUE)
					{
						wSubLogSize = _sublogGetSize(pSL);
						_boSubLogCreateDe(&pSL->u.stCreateDE);
					}
					else
					{
						_boSubLogCreateDe(&pSL->u.stCreateDE);
						wSubLogSize = _sublogGetSize(pSL);
					}
					break;

				case LM_SUBLOG_DELETE_DE:
					IF_LK (bLogWrite == FFAT_TRUE)
					{
						wSubLogSize = _sublogGetSize(pSL);
						_boSubLogDeleteDe(&pSL->u.stDeleteDE);
					}
					else
					{
						_boSubLogDeleteDe(&pSL->u.stDeleteDE);
						wSubLogSize = _sublogGetSize(pSL);
					}

					break;

				case LM_SUBLOG_DELETE_EA:
				case LM_SUBLOG_SET_EA:
					IF_LK (bLogWrite == FFAT_TRUE)
					{
						wSubLogSize = _sublogGetSize(pSL);
						_boSubLogEA(&pSL->u.stEA);
					}
					else
					{
						_boSubLogEA(&pSL->u.stEA);
						wSubLogSize = _sublogGetSize(pSL);
					}
					break;
				case LM_SUBLOG_UPDATE_ROOT_EA:
					IF_LK (bLogWrite == FFAT_TRUE)
					{
						wSubLogSize = _sublogGetSize(pSL);
						_boSubLogUpdateRootEA(&pSL->u.stUpdateRootEA);
					}
					else
					{
						_boSubLogUpdateRootEA(&pSL->u.stUpdateRootEA);
						wSubLogSize = _sublogGetSize(pSL);
					}
					break;

				default:
					FFAT_ASSERT(bLogRecovery == FFAT_TRUE ? FFAT_TRUE : FFAT_FALSE);
					return;
					break;
			}

			pSL = (SubLog*)((t_int8*)pSL + wSubLogSize);
		} while(uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE);

		return;
	}


	/**
	 * changes byte-order of a Log to byte-order of current system
	 * 
	 * @param		pLH				: [IN/OUT] log header information of log slot
	 * @param		bLogWrite		: [IN]	FFAT_TRUE : set byte order for FAT spec (LITTLE ENDIAN), For log write
	 *									FFAT_FALSE: set byte order for system, for recovery
	 * @author		DongYoungSeo
	 * @version		AUG-18-2008 [DongYoung Seo] First write
	 */
	static FFatErr
	_adjustByteOrder(LogHeader* pLH, t_boolean bLogWrite)
	{
		SubLog*			pSL;				// sub log
		LogType			dwLogType;			// log type
		t_uint16		wUsedSize;			// size of log
		t_uint16		wTotalUsedSize;		// total log size for LL
		LogTail*		pLT;				// log tail
		LogConfirm*		pLC;				// confirm log
		t_boolean		bLogRecovery;		// boolean for easy understanding

		FFAT_ASSERT(pLH);
		FFAT_ASSERT((bLogWrite == FFAT_TRUE) || (bLogWrite == FFAT_FALSE));

		wTotalUsedSize = 0;

		if (bLogWrite == FFAT_TRUE) 
		{
			bLogRecovery = FFAT_FALSE;
		}
		else
		{
			bLogRecovery = FFAT_TRUE;
		}

		dwLogType = FFAT_BO_UINT32(pLH->udwLogType);

		if (dwLogType & LM_LOG_FLAG_LLW)
		{
			do
			{
				IF_UK (bLogRecovery == FFAT_TRUE)
				{
					_boLogHeader(pLH);		// to system endian
				}

				// check log header
				FFAT_ASSERT((bLogWrite == FFAT_TRUE) ? pLH->udwLogVer == _LOG_VERSION : FFAT_TRUE);
				FFAT_ASSERT((bLogWrite == FFAT_TRUE) ? pLH->wUsedSize <= FAT_SECTOR_SIZE_MAX : FFAT_TRUE);

				dwLogType	= pLH->udwLogType;
				wUsedSize	= pLH->wUsedSize;

				wTotalUsedSize = wTotalUsedSize + wUsedSize;
				if (wTotalUsedSize > LOG_LLW_CACHE_SIZE)
				{
					// invalid log entry
					FFAT_ASSERT(bLogRecovery == FFAT_TRUE);
					FFAT_LOG_PRINTF((_T("Invalid log entry")));
					return FFAT_EINVALID;
				}

				pSL = (SubLog*)((t_int8*)pLH + sizeof(LogHeader));
				_adjustByteOrderSubLogs(pSL, bLogWrite);

				IF_LK (bLogWrite == FFAT_TRUE)
				{
					_boLogHeader(pLH);		// to little endian
				}

				pLH = (LogHeader* )((t_int8*)pLH + wUsedSize);

			} while(pLH->udwLogVer != _LLW_CONFIRM);

			pLC = (LogConfirm*) pLH;
			_boLogConfirm(pLC);
		}
		else
		{
			pSL = (SubLog*)((t_int8*)pLH + sizeof(LogHeader));
			_adjustByteOrderSubLogs(pSL, bLogWrite);

			dwLogType	= pLH->udwLogType;
			wUsedSize	= pLH->wUsedSize;

			_boLogHeader(pLH);

			pLH = (LogHeader* )((t_int8*)pLH + wUsedSize);

			pLT = (LogTail*) pLH;
			_boLogTail(pLT);
		}

		return FFAT_OK;
	}


	/**
	 * _boLogHeader changes byte-order of Log Header according to byte-order of current system
	 * 
	 * @param		pLH 		: [IN/OUT] a log header
	 * @author
	 * @version
	 */
	static void
	_boLogHeader(LogHeader* pLH)
	{
		FFAT_ASSERT(pLH);

		pLH->udwLogVer		= FFAT_BO_UINT32(pLH->udwLogVer);
		pLH->udwSeqNum		= FFAT_BO_UINT32(pLH->udwSeqNum);
		pLH->udwLogType		= FFAT_BO_UINT32(pLH->udwLogType);

		pLH->uwFlag			= FFAT_BO_UINT16(pLH->uwFlag);
		pLH->wUsedSize		= FFAT_BO_UINT16(pLH->wUsedSize);
	}


	/** 
	 * changes byte-order of LogTail structure according to the byte-order of target system
	 * 
	 * @param	pLT		: [IN/OUT] log tail
	 * @author	DongYoungSeo
	 * @version	APR-04-2007 [DongYoung Seo] First write
	 */
	static void
	_boLogTail(LogTail* pLT)
	{
		FFAT_ASSERT(pLT);

		pLT->udwInvertedSeqNum	= FFAT_BO_UINT32(pLT->udwInvertedSeqNum);
	}


	/**
	*  changes byte-order of Log Header according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] confirm log
	* @author			DongYoung Seo
	* @version			SEP-27-2006 [DongYoung Seo] First Writing.
	*/
	static void
	_boLogConfirm(LogConfirm* pLog)
	{
		FFAT_ASSERT(pLog);

		pLog->udwInvertedLogVer			= FFAT_BO_UINT32(pLog->udwInvertedLogVer);
		pLog->dwLogSize					= FFAT_BO_INT32(pLog->dwLogSize);
	}


	/**
	*  changes byte-order of Sub Log Header according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogHeader(SubLogHeader* pSL)
	{
		FFAT_ASSERT(pSL);

		pSL->uwNextSLFlag	= FFAT_BO_UINT16(pSL->uwNextSLFlag);
		pSL->uwSublogType	= FFAT_BO_UINT16(pSL->uwSublogType);
	}


	/**
	*  changes byte-order of Sub Log for DE creation according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogCreateDe(SubLogCreateDe* pCreateDe)
	{
		t_wchar*		psName;

		FFAT_ASSERT(pCreateDe);

		pCreateDe->udwCluster			= FFAT_BO_UINT32(pCreateDe->udwCluster);
		pCreateDe->dwDeStartCluster	= FFAT_BO_UINT32(pCreateDe->dwDeStartCluster);
		pCreateDe->dwDeStartOffset		= FFAT_BO_INT32(pCreateDe->dwDeStartOffset);
		pCreateDe->dwDeCount			= FFAT_BO_INT32(pCreateDe->dwDeCount);

		pCreateDe->uwPerm				= FFAT_BO_UINT16(pCreateDe->uwPerm);
		pCreateDe->udwUid				= FFAT_BO_UINT32(pCreateDe->udwUid);
		pCreateDe->udwGid				= FFAT_BO_UINT32(pCreateDe->udwGid);

		pCreateDe->wNameLen				= FFAT_BO_INT16(pCreateDe->wNameLen);
		pCreateDe->wNameInExtra			= FFAT_BO_INT16(pCreateDe->wNameInExtra);
		pCreateDe->wExtraNo				= FFAT_BO_INT16(pCreateDe->wExtraNo);

		if (pCreateDe->wNameInExtra == FFAT_FALSE)
		{
			psName = (t_wchar*)((t_int8*)pCreateDe + sizeof(SubLogCreateDe));
			_boSubLogFileName(psName, pCreateDe->wNameLen);
		}
	}


	/**
	*  changes byte-order of Sub Log for DE deletion according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogDeleteDe(SubLogDeleteDe* pDeleteDe)
	{
		FFAT_ASSERT(pDeleteDe);

		pDeleteDe->udwCluster			= FFAT_BO_UINT32(pDeleteDe->udwCluster);
		pDeleteDe->dwDeStartCluster	= FFAT_BO_UINT32(pDeleteDe->dwDeStartCluster);
		pDeleteDe->dwDeStartOffset		= FFAT_BO_INT32(pDeleteDe->dwDeStartOffset);
		pDeleteDe->dwDeCount 			= FFAT_BO_INT32(pDeleteDe->dwDeCount);
	}


	/**
	*  changes byte-order of Sub Log for DE updating according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogUpdateDe(SubLogUpdateDe* pUpdateDe)
	{
		FFAT_ASSERT(pUpdateDe);

		pUpdateDe->dwDeStartCluster	= FFAT_BO_UINT32(pUpdateDe->dwDeStartCluster);
		pUpdateDe->dwDeStartOffset		= FFAT_BO_INT32(pUpdateDe->dwDeStartOffset);
	}


	/**
	*  changes byte-order of Sub Log for FAT related work 
	*	according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogFat(SubLogFat* pFat)
	{
		FFAT_ASSERT(pFat);

		pFat->udwCluster			= FFAT_BO_UINT32(pFat->udwCluster);
		pFat->udwPrevEOF			= FFAT_BO_UINT32(pFat->udwPrevEOF);
		pFat->dwCount				= FFAT_BO_UINT32(pFat->dwCount);
		pFat->dwHPAType				= FFAT_BO_UINT32(pFat->dwHPAType);
		pFat->udwFirstCluster		= FFAT_BO_UINT32(pFat->udwFirstCluster);
		pFat->dwValidEntryCount		= FFAT_BO_INT32(pFat->dwValidEntryCount);
	}


	/**
	*  changes byte-order of Sub Log for Extended Attribute related work 
	*	according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version		AUG-20-2008 [DongYoung Seo] add byte order change code for stEAMain and stEAEntry
	*/
	static void
	_boSubLogEA(SubLogEA* pEA)
	{
		FFAT_ASSERT(pEA);

		pEA->udwFirstCluster		= FFAT_BO_UINT32(pEA->udwFirstCluster);
		pEA->udwDelOffset			= FFAT_BO_UINT32(pEA->udwDelOffset);

		pEA->udwDelOffset			= FFAT_BO_UINT32(pEA->udwInsOffset);

		pEA->stEAMain.uwSig			= FFAT_BO_UINT16(pEA->stEAMain.uwSig);
		pEA->stEAMain.uwCrtTime		= FFAT_BO_UINT16(pEA->stEAMain.uwCrtTime);
		pEA->stEAMain.uwCrtDate		= FFAT_BO_UINT16(pEA->stEAMain.uwCrtDate);
		pEA->stEAMain.uwValidCount	= FFAT_BO_UINT16(pEA->stEAMain.uwValidCount);
		pEA->stEAMain.udwTotalSpace	= FFAT_BO_UINT32(pEA->stEAMain.udwTotalSpace);
		pEA->stEAMain.udwUsedSpace	= FFAT_BO_UINT32(pEA->stEAMain.udwUsedSpace);

		pEA->stEAEntry.uwNameSize	= FFAT_BO_UINT16(pEA->stEAEntry.uwNameSize);
		pEA->stEAEntry.udwEntryLength	= FFAT_BO_UINT32(pEA->stEAEntry.udwEntryLength);
		pEA->stEAEntry.udwValueSize	= FFAT_BO_UINT32(pEA->stEAEntry.udwValueSize);
	}

	/**
	*  changes byte-order of Sub Log for Update Root EA related work 
	*	according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version		DEC-22-2008 [JeongWoo Park] add byte order change code for Update Root EA
	*/
	static void
	_boSubLogUpdateRootEA(SubLogUpdateRootEA* pUpdateRootEA)
	{
		FFAT_ASSERT(pUpdateRootEA);

		pUpdateRootEA->udwOldFirstCluster = FFAT_BO_UINT32(pUpdateRootEA->udwOldFirstCluster);
		pUpdateRootEA->udwNewFirstCluster = FFAT_BO_UINT32(pUpdateRootEA->udwNewFirstCluster);
	}


	/**
	*  changes byte-order of Sub Log for Vectored Clusters
	*	according to byte-order of current system
	* 
	* @param pLog		: [IN/OUT] sub log header
	* @author
	* @version
	*/
	static void
	_boSubLogFatVCE(FFatVCE* pVCE, t_int32 dwValidEntryCount)
	{
		t_int32 i;

		for (i = 0; i < dwValidEntryCount; i++)
		{
			pVCE[i].dwCluster	= FFAT_BO_UINT32(pVCE[i].dwCluster);
			pVCE[i].dwCount		= FFAT_BO_INT32(pVCE[i].dwCount);
		}
	}


	/**
	*  changes byte-order of Sub Log for file name
	*	according to byte-order of current system
	* 
	* @param	psName		: [IN/OUT] sub log header
	* @param	uwNameLen	: [IN] length of file name.
	*							may be 0.
	* @author
	* @version
	*/
	static void
	_boSubLogFileName(t_wchar* psName, t_uint16 uwNameLen)
	{
		t_uint16 i;

		for (i = 0; i < uwNameLen; i++)
		{
			psName[i] = FFAT_BO_UINT16((t_uint16)psName[i]);
		}
	}

	/**
	 * _boLogHeader changes byte-order of Log Header according to byte-order of current system
	 * 
	 * @param pLog 		: [IN/OUT] log header information of log slot
	 *
	 * @author		InHwan Choi
	 * @version 	DEC-05-2007 [InHwan Choi] First Writing.
	 */
	static void
	_boLogHeaderOpenUnlink(OULogHeader* pOULog)
	{
		FFAT_ASSERT(pOULog);

		pOULog->udwLogVer		= FFAT_BO_UINT32(pOULog->udwLogVer);
		pOULog->udwLogType		= FFAT_BO_UINT32(pOULog->udwLogType);
		pOULog->udwValidEntry	= FFAT_BO_UINT32(pOULog->udwValidEntry);
	}
#endif	// end of #ifdef FFAT_BIG_ENDIAN

/**
* _readSlot reads one log slot
* 
* @param	pVol				: [IN] volume pointer
* @param	pLH					: [OUT] log header information of log slot
* @param	uwId				: [IN] log slot id
* @param	pCxt				: [IN] context of current operation
* @return	FFAT_OK				: success to get a non-empty log slot
* @return	_ENO_VALID_LOG		: get a empty log slot, or this is an empty slot
* @return	_EINVALID_LOG_VER	: log file is created from another version of BTFS
* @return	FFAT_EINVALID		: log slot is invalid
* @return	FFAT_EIO			: fail to read log slot
* @version	AUG-12-2008 [DongYoung Seo] bug Fix. - add code to check the next log is partial LLW or not.
* @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_readSlot(Vol* pVol, LogHeader* pLH, t_uint16 uwId, ComCxt* pCxt)
{
	LogInfo*	pLI;
	t_int32		i;
	FFatErr		r;

	FFAT_ASSERT(pLH);

	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(uwId < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[uwId], 1, (t_int8*)pLH,
				(FFAT_CACHE_META_IO | FFAT_CACHE_DATA_LOG),
				FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("fail to read data from log file")));
		return FFAT_EIO;
	}

	// set order to check log header
	_boLogHeader(pLH);

	r = _checkLogHeader(pLH);
	FFAT_ER(r, (_T("fail to check log header")));

	if ((r == _EINVALID_LOG_VER) || (r == _ENO_VALID_LOG))
	{
		return r;
	}

	// check the log is LLW
	if (pLH->udwLogType & LM_LOG_FLAG_LLW)
	{
		if ((uwId + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) > LOG_MAX_SLOT)
		{
			// this is not a valid LLW log.
			// this entry must be a partial LLW entry. the post part off LLW was already overwritten.

			// return No Valid Log
			return _ENO_VALID_LOG;
		}

		// this log is LLW
		// sync current LLW
		r = _syncLL(_LLW(pVol)->pVol, pCxt);
		FFAT_ER(r, (_T("fail to sync LLW")));

		FFAT_ASSERT((uwId + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) <= LOG_MAX_SLOT);

		for (i = 0; i < (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol)); i++)
		{
			r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[uwId + i], 1,
						((t_int8*)_LLW(pVol)->pBuff + (i << VOL_SSB(pVol))),
						(FFAT_CACHE_META_IO | FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC),
						FFAT_TRUE, pCxt);
			IF_UK (r != 1)
			{
				FFAT_LOG_PRINTF((_T("fail to read data from log file")));
				return FFAT_EIO;
			}
		}

		_adjustByteOrder((LogHeader*)_LLW(pVol)->pBuff, FFAT_FALSE);

		r = _checkLazyLog((LogHeader*)_LLW(pVol)->pBuff);
		FFAT_ER(r, (_T("fail to check log header")));

		r = FFAT_OK;
	}

	return r;
}


/**
* check a log slot
* 
* @param	pLH				: [OUT] log header information of log slot
* 
* @return	FFAT_OK				: success to get a non-empty log slot
* @return	_ENO_VALID_LOG		: get a empty log slot, or this is an empty slot
* @return	_EINVALID_LOG_VER	: log file is created from another version of BTFS
* @return	FFAT_EINVALID		: log slot is invalid
*/
static FFatErr
_checkLogHeader(LogHeader* pLH)
{
	LogTail*		pLT;
	FFatErr			r;

	FFAT_ASSERT(pLH);

	// empty slot
	if ((pLH->udwLogVer == 0) || (pLH->udwLogType == LM_LOG_NONE))
	{
		r = _ENO_VALID_LOG;
		goto out;
	}

	//slot is backup log slot
	if (pLH->udwLogType & LM_LOG_FLAG_BACKUP)
	{
		r = _ENO_VALID_LOG;
		goto out;
	}

	// check if the log record is valid
	if (pLH->udwLogVer != _LOG_VERSION)
	{
		FFAT_LOG_PRINTF((_T("Log file is created from another version of BTFS")));
		r = _EINVALID_LOG_VER;
		goto out;
	}

	if (_getLogId(pLH->udwLogType) >= _MAX_LOG_TYPE)
	{
		FFAT_LOG_PRINTF((_T("invalid log format")));
		r = FFAT_EINVALID;
		goto out;
	}

	if (pLH->uwFlag & LM_LOG_FLAG_LLW)
	{
		// this is LLW
		// check log tail
		pLT = (LogTail*)((t_int8*)pLH + pLH->wUsedSize);
		if (pLT->udwInvertedSeqNum != (0xFFFFFFFF ^ pLH->udwSeqNum))
		{
			FFAT_LOG_PRINTF((_T("Invalid log tail!!")));
			r = FFAT_EINVALID;
			goto out;
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
* check validity of lazy log
* 
* @param	pLH 		: Log Header
* 
* @return	FFAT_OK		: checking success
* @return	FFAT_ENOENT	: there is no lazy log slot
* @author	DongYoung Seo
* @version	JUL-25-2007 [DongYoung Seo] First Writing.
*/
static FFatErr
_checkLazyLog(LogHeader* pLH)
{
	t_int8*			pLogBegin;
	LogHeader*		pPrevLH = NULL;
	LogConfirm*		pLogConfirm;
	t_int32			dwSize;
	FFatErr			r;

	FFAT_ASSERT(pLH);

	dwSize		= 0;
	pLogBegin	= (t_int8*)pLH;

	if ((pLH->uwFlag & LM_LOG_FLAG_LLW_BEGIN) == 0)
	{
		// this must be a partial log entry
		return FFAT_ENOENT;
	}

	do
	{
		// check LLW Flag
		if ((pLH->udwLogType | LM_LOG_FLAG_LLW) == 0)
		{
			// This is not LL
			return FFAT_ENOENT;
		}

		r = _checkLogHeader(pLH);
		FFAT_ER(r, (_T("fail to check log header")));

		if ((r == FFAT_OK1) || (r == FFAT_OK2))
		{
			return FFAT_ENOENT;
		}

		if (pPrevLH)
		{
			// this is for checking the case that current LL is partially written & combined with another LL of the same shape
			IF_UK (pLH->udwSeqNum != (pPrevLH->udwSeqNum + 1))
			{
				// Lazy Log is not continuous
				return FFAT_ENOENT;
			}
		}

		pPrevLH = pLH;

		dwSize += pLH->wUsedSize;
		pLH = (LogHeader*)(pLogBegin + dwSize);

		FFAT_ASSERT(dwSize < LOG_LLW_CACHE_SIZE);

		// check the next log is confirm log?
		if (pLH->udwLogVer == _LLW_CONFIRM)
		{
			pLogConfirm = (LogConfirm*)pLH;

			// this is the last log, check total size
			dwSize += sizeof(LogConfirm);
			if (dwSize != pLogConfirm->dwLogSize)
			{
				// invalid log confirm
				FFAT_DEBUG_PRINTF((_T("Incorrect Lazy Log entry size, corrupted log entry!!\n")));
				FFAT_ASSERT(0);
				return FFAT_ENOENT;
			}

			break;
		}

	} while (1);

	return FFAT_OK;
}


/**
* _logWriteSlot writes one log slot
* 
* @param	pVol			: [IN] pointer of volume
* @param	pLH				: [IN] log header information of log slot
* @param	uwId			: [IN] log slot id
* @param	pCxt			: [IN] context of current operation 
* @return	FFAT_OK			: success to write log slot
* @return	FFAT_EIO		: fail to write log slot
* @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
* @version	APR-27-2009 [JeongWoo Park] Add the initialize code for remained area of log
*/
static FFatErr
_logWriteSlot(Vol* pVol, LogHeader* pLH, t_uint16 uwSlot, ComCxt* pCxt)
{
	LogInfo*		pLI;		// log info
	LogTail*		pLT;		// log tail
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);

	pLI = _LOG_INFO(pVol);
	pLT = (LogTail*)((t_int8*)pLH + pLH->wUsedSize);

	_UPDATE_LOG_SEQ(pLI);

	// make log header
	pLH->udwSeqNum		= pLI->udwSeqNum;
	pLH->wUsedSize		+= sizeof(LogTail);

	FFAT_ASSERT(pLH->wUsedSize <= VOL_SS(pVol));

	pLT->udwInvertedSeqNum	= 0xFFFFFFFF ^ pLH->udwSeqNum;

	// initialize the remained area of log
	if (pLH->wUsedSize & VOL_SSM(pVol))
	{
		FFAT_MEMSET(((t_int8*)pLH + pLH->wUsedSize), 0x00,
					(VOL_SS(pVol) - (pLH->wUsedSize & VOL_SSM(pVol))));
	}

	// set previous log flag
	pLI->uwPrevDirtyFlag	= pLH->uwFlag;

	_PRINT_LOG(pVol, pLH, "Write Log");
	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("log write, LogType/seq no/slot:0x%X/%d/%d\n"), pLH->udwLogType, pLH->udwSeqNum, uwSlot));

	_adjustByteOrder(pLH, FFAT_TRUE);

	// set log tail

	FFAT_ASSERT(uwSlot < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[uwSlot], 1, (t_int8*)pLH,
				(FFAT_CACHE_META_IO | FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC),
				FFAT_FALSE, pCxt);
	if (r != 1)
	{
		if (r >= 0)
		{
			r = FFAT_EIO;
		}

		FFAT_LOG_PRINTF((_T("fail to write log data to a log file")));
		return r;
	}

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("Log written \n")));

	return FFAT_OK;
}


/**
* _writeEmptySlot writes a empty log slot
*
* @param	pVol		: [IN] pointer of volume
* @param	uwId		: [IN] log slot id
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success
* @return	else		: failed
*/
static FFatErr
_writeEmptySlot(Vol* pVol, t_uint16 uwId, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH = NULL;			// pointer for log

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_NONE, FFAT_CACHE_NONE, pCxt);
	FFAT_ER(r, (_T("fail to allocate memory for log header")));

	// add empty sub log
	_sublogGenNoLog(pLH);

	r = _logWriteSlot(pVol, pLH, uwId, pCxt);
	FFAT_EO(r, (_T("fail to write log slot")));

out:
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
* synchronize cache to storage and clean log file
* 
* @param	pVol		: [IN] pointer of volume
* @param	dwCacheFlag	: [IN] flag for cache operation
* @param	pCxt		: [IN] context of current operation 
* @return	FFAT_OK		: success
* @return	else		: failed
*/
static FFatErr
_logReset(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	LogInfo*		pLI;
	FFatErr			r;

	FFAT_ASSERT(pVol);

	pLI = _LOG_INFO(pVol);

	if (_IS_NO_VALID_LOG(pLI) == FFAT_TRUE)
	{
		FFAT_ASSERT(pLI->wCurSlot == 0);
		FFAT_ASSERT((pLI->uwPrevDirtyFlag == LM_FLAG_NONE) || (pLI->uwPrevDirtyFlag & LM_FLAG_SYNC));

		// nothing to do
		return FFAT_OK;
	}

	r = _syncLL(pVol, pCxt);
	FFAT_ER(r, (_T("fail to sync LLW")));

	// sync FFC's Dirty FCCE(Free Cluster Cache Entry)
	// LLW sync가 이것 보다 먼저 되어야 한다. merge시 유의 
	r = ffat_fcc_syncVol(pVol, FFAT_CACHE_SYNC, pCxt);
	FFAT_ER(r, (_T("fail to sync volume")));

	//Synchronize cache to volume when log file is full
	if (dwCacheFlag & FFAT_CACHE_SYNC)
	{
		r = FFATFS_SyncVol(VOL_VI(pVol), dwCacheFlag, pCxt);
		FFAT_ER(r, (_T("fail to sync volume")));
	}

	pLI->dwFlag		|= LI_FLAG_NO_LOG;
	pLI->wCurSlot	= 0;

	// initialize previous info
	pLI->uwPrevDirtyFlag	= LM_FLAG_NONE;

	return FFAT_OK;
}


/**
* synchronize cache to storage and clean log file
* 
* @param	pVol 		: [IN] pointer of volume
* @param	dwCacheFlag	: [IN] flag for cache operation
* @param	pCxt		: [IN] context of current operation
* 
* @return	FFAT_OK		: success
* @return	else		: failed
*/
static FFatErr
_logResetWithClean(Vol* pVol, FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogInfo*		pLI;

	FFAT_ASSERT(pVol);

	pLI = _LOG_INFO(pVol);

	if (_IS_NO_VALID_LOG(pLI) == FFAT_TRUE)
	{
		FFAT_ASSERT(pLI->wCurSlot == 0);
		FFAT_ASSERT((pLI->uwPrevDirtyFlag == LM_FLAG_NONE) || (pLI->uwPrevDirtyFlag & LM_FLAG_SYNC));

		// nothing to do
		return FFAT_OK;
	}

	r = _logReset(pVol, dwCacheFlag, pCxt);
	FFAT_ER(r, (_T("fail to reset log")));

	r = _writeEmptySlot(pVol, 0, pCxt);
	FFAT_ER(r, (_T("fail to write empty slot on the log")));

	return FFAT_OK;
}


/**
* _logWriteTransaction writes one transaction to log
* 
* @param	pVol			: [IN] pointer of volume
* @param	pLH				: [IN] log header information of log slot
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
* @return	else			: failed
* @version	05-MAR-2009 [DongYoung Seo]: Add sync code when log slots are full (after log rotation)
* @version	03-SEP-2009 [JW Park]: If LM_FLAG_SYNC, no need to call _logWriteLLW()
*/
static FFatErr
_logWriteTransaction(Vol* pVol, LogHeader* pLH, ComCxt* pCxt)
{
	FFatErr			r;
	LogInfo*		pLI;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);
	FFAT_ASSERT(pLH->wUsedSize >= sizeof(LogHeader));

	pLI = _LOG_INFO(pVol);

	// 1. Synchronize log area when all log entry are used
	// 2. If a transaction needs to be synchronized to storage,
	// previous transactions recorded in log must be synchronized to storage first.
	// If previous transaction is in first log slot and is already synchronized,
	// we do not need to synchronize again.
	if (pLH->uwFlag & LM_FLAG_SYNC)
	{
		r = _logReset(pVol, FFAT_CACHE_SYNC, pCxt);
		FFAT_ER(r, (_T("fail to reset log")));

		pLH->uwFlag &= (~LM_FLAG_DIRTY);		// remove dirty flag
	}
	else
	{
		// if no sync flag, try to write in LLW.
		r = _logWriteLLW(pVol, pLH, pCxt);
		if (FFAT_DONE == r)
		{
			r = FFAT_OK;
			pLI->udwPrevLogType = pLH->udwLogType;
			goto out;
		}

		if (pLI->wCurSlot == 0)
		{
			// this the first time after use of all log slot(or may be the first log)
			r = _logReset(pVol, FFAT_CACHE_SYNC, pCxt);
			FFAT_EO(r, (_T("fail to reset log")));
		}
	}

	// remove LLW flag
	pLH->udwLogType &= (~LM_LOG_FLAG_LLW);

	// if the previous log flag is sync then write log on the first slot
	FFAT_ASSERT((pLI->uwPrevDirtyFlag & LM_FLAG_SYNC) ? (pLI->wCurSlot == 0) : FFAT_TRUE);

	r = _logWriteSlot(pVol, pLH, pLI->wCurSlot, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	pLI->dwFlag		&= (~LI_FLAG_NO_LOG);

	if (pLH->uwFlag & LM_FLAG_SYNC)
	{
		pLI->wCurSlot	= 0;
	}
	else
	{
		_UPDATE_SLOT(pLI, 1);
	}

	pLI->udwPrevLogType = pLH->udwLogType;

	r = FFAT_OK;

out:
	// We cannot synchronize log file when log file is full at here
	// because if we synchronize, the last transaction information will be lost and cannot be recovered

	return r;
}


/**
* _makeIncEven make a int i be even if it is not even through increasing 1
* for 4byte alignment.
* 
* @param i		: the int to be changed
* @return		: A even int
* @author
* @version
*/
static t_int16
_makeIncEven(t_int16 wValue)
{
	if (wValue & 0x0001)
	{
		wValue += 1; //make uwNameLen be even
	}
	return wValue;
}


/**
* _subLogCheckDeStartCluster checks the value of DE start cluster, modify it if it is incorrect.
* Incorrect value is due to value pass of ffat_addon_log.c
* 
* @param	dwCluster 			: cluster of first cluster
* @param	pdwDeStartCluster 	: [IN/OUT]pointer to the value of DE start cluster
* @param	pCxt				: [IN] context of current operation
* 
*/
static void
_subLogCheckDeStartCluster(Vol* pVol, t_uint32 udwCluster,
				t_uint32* pdwDeStartCluster, t_int32 dwDeStartOffset, ComCxt* pCxt)
{
	FFatErr			r;

	if (*pdwDeStartCluster == 0)
	{
		r = FFATFS_GetClusterOfOffset(VOL_VI(pVol), udwCluster,
						dwDeStartOffset, pdwDeStartCluster, pCxt);
		FFAT_ASSERT(r == FFAT_OK);
	}
}


/**
* write file name to extra slot
* 
* @ param	pVol			: [IN] volume pointer
* @ param	psName			: [IN] Node Name
* @param	wEatrano		: [IN] Number of extra slot
* @param	pCxt			: [IN] context of current operation
* @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_sublogExtraWriteFilename(Vol* pVol, t_wchar* psName, t_int16 wExtraNo, ComCxt* pCxt)
{
	LogInfo*		pLI;
	t_int32			dwIndex;
	t_int32			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(wExtraNo >= 0);

	dwIndex = LOG_MAX_SLOT + wExtraNo;
	pLI		= _LOG_INFO(pVol);

	FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("sub-log write, index(%d)\n"), dwIndex));

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], 1, (t_int8*)psName,
					(FFAT_CACHE_META_IO | FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("fail to write filename to extra log space")));
		return FFAT_EIO;
	}

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("Filename is written to extra log space\n")));

	return FFAT_OK;
}


/**
*	add an empty sub log for some log type that does not need to have sub log
* 
* @param	pLH			: [IN] pointer of log header
* @author	DongYoung Seo
* @version	AUG-19-2008 [DongYoung Seo] First Writing.
*/
static void
_sublogGenNoLog(LogHeader* pLH)
{
	SubLog* 		pSL;

	FFAT_ASSERT(pLH);
	FFAT_ASSERT(pLH->wUsedSize >= sizeof(LogHeader));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwSublogType	= LM_SUBLOG_NONE;
	pSL->stSubLogHeader.uwNextSLFlag	= LM_SUBLOG_FLAG_LAST;

	pLH->wUsedSize = pLH->wUsedSize + sizeof(SubLogHeader);

	return;
}


/**
* _sublogGenCreateDE generates create DE sub-log
* 
* @param	pVol			: [IN] pointer of volume
* @param	pLH				: [IN] pointer of log header
* @param	pNodeChild		: [IN] pointer of node
* @param	psName			: [IN] new file name
* @param	wReservedSize	: [IN] reserved size in log slot
* @param	bForce			: [IN] true: truncate psName if psName is too large
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
*/
static FFatErr
_sublogGenCreateDE(Vol* pVol, LogHeader* pLH, Node* pNodeChild, t_wchar* psName,
					t_int16 wReservedSize, t_boolean bForce, ComCxt* pCxt)
{
	FFatErr				r;
	SubLog* 			pSL;				// sub log pointer
	SubLogCreateDe*		pCreate;
	NodeDeInfo*			pDeChild;
	t_wchar* 			psNameNew;
	t_uint16			wNameLen;
	t_uint16			wNameLenEven;
	t_boolean			bOutOfRange;
	t_int8*				pCurrentPos;
	XDEInfo*			pXDEInfo;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	psNameNew = (t_wchar*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(psNameNew);

	//Save psName in psNameNew
	wNameLen		= (t_int16)pNodeChild->wNameLen;
	FFAT_WCSNCPY(psNameNew, psName, wNameLen);
	psNameNew[wNameLen] = 0;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_CREATE_DE;

	pCreate = &pSL->u.stCreateDE;
	pDeChild = &pNodeChild->stDeInfo;

	pCreate->dwCluster			= NODE_COP(pNodeChild);
	// fill DE info
	pCreate->dwDeStartCluster	= pDeChild->dwDeStartCluster;
	pCreate->dwDeStartOffset	= pDeChild->dwDeStartOffset;
	pCreate->dwDeCount			= pDeChild->dwDeCount;

	_subLogCheckDeStartCluster(pVol, pCreate->dwCluster,
				&pCreate->dwDeStartCluster, pCreate->dwDeStartOffset, pCxt);

	FFAT_ASSERT((pCreate->dwDeStartCluster == 1) ? (NODE_COP(pNodeChild) == VOL_RC(pVol))
							: (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pCreate->dwDeStartCluster) == FFAT_TRUE));
	FFAT_ASSERT(pCreate->dwDeStartOffset < FAT_DIR_SIZE_MAX);
	FFAT_ASSERT(pCreate->dwDeCount <= (FAT_DE_COUNT_MAX + 1));				// +1 : for XDE

	pXDEInfo = &(((AddonNode*)&(pNodeChild->stAddon.dwNode))->stXDE);

	pCreate->uwPerm		= (t_uint16)pXDEInfo->dwPerm;
	pCreate->udwUid		= pXDEInfo->dwUID;
	pCreate->udwGid		= pXDEInfo->dwGID;

	FFAT_MEMCPY(&pCreate->stDE, &pNodeChild->stDE, sizeof(FatDeSFN));

	pCreate->wNameLen		= wNameLen;
	pCreate->wNameInExtra	= FFAT_FALSE;
	pCreate->wExtraNo		= -1;

	wNameLenEven = _makeIncEven(wNameLen);

	pCurrentPos		= (t_int8*)pSL + (sizeof(SubLogHeader) + sizeof(SubLogCreateDe));
	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogCreateDe));

	if (pNodeChild->dwFlag & NODE_NAME_SFN)
	{
		//do not need to save psName
		pCreate->wNameLen = 0;
	}
	else
	{
		bOutOfRange = _IS_OUT_OF_RANGE(pVol, pLH->wUsedSize,
							(wNameLenEven * sizeof(t_wchar)), wReservedSize);
		if (!bForce)
		{
			// check if the filename is too big to be saved in log record
			if ((pLH->uwFlag & LM_FLAG_SYNC) || (bOutOfRange == FFAT_TRUE))
			{
				pCreate->wNameLen = 0;
				pLH->uwFlag |= LM_FLAG_SYNC;
				goto out;
			}
		}

		// Use Extra log space to save file name if there is not enough space
		if (bOutOfRange)
		{
			_boSubLogFileName(psNameNew, uwNameLen);

			pCreate->wNameInExtra 	= FFAT_TRUE;
			pCreate->wExtraNo 		= 0;
			_sublogExtraWriteFilename(pVol, psNameNew, pCreate->wExtraNo, pCxt);

			pLH->uwFlag |= LM_FLAG_SYNC;
			// pCurrentPos and *puwUsedSize do not need to be changed 
			// since psName is stored in extra space
			goto out;
		}
		else
		{
			FFAT_WCSNCPY((t_wchar*)pCurrentPos, psNameNew, wNameLen);
			pCurrentPos		+= wNameLen * sizeof(t_wchar);
			pLH->wUsedSize	+= wNameLen * sizeof(t_wchar);

			if (wNameLen & 0x0001)
			{
				FFAT_MEMSET((t_int8*)pCurrentPos, 0x00, sizeof(t_wchar));

				pCurrentPos			+= sizeof(t_wchar);
				pLH->wUsedSize		+= sizeof(t_wchar);
			}
		}
	}

	pLH->uwFlag |= LM_FLAG_DE_DIRTY;

out:
	_PRINT_SUBLOG(pVol, pSL);

	FFAT_LOCAL_FREE(psNameNew, VOL_SS(pVol), pCxt);

	r = FFAT_OK;

	return r;
}


/**
* _subLogGetDeleteDe generates delete DE sub-log
* 
* @param pLH			: [IN] pointer of log header
* @param pNode			: [IN] pointer of node
* @param puwUsedSize 	: [IN/OUT] Already used size in log slot
* 
* @return FFAT_OK	: success
*/
static FFatErr
_sublogGenDeleteDE(Vol* pVol, LogHeader* pLH, 
					Node* pNode, ComCxt* pCxt)
{
	SubLog* 		pSL;
	SubLogDeleteDe*	pDeleteDe;
	NodeDeInfo*		pDeInfo;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);
	FFAT_ASSERT(pNode);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_DELETE_DE;

	pDeleteDe = &pSL->u.stDeleteDE;
	pDeInfo = &pNode->stDeInfo;

	pDeleteDe->udwCluster			= NODE_COP(pNode);
	// fill DE info
	pDeleteDe->dwDeStartCluster	= pDeInfo->dwDeStartCluster;
	pDeleteDe->dwDeStartOffset		= pDeInfo->dwDeStartOffset;
	pDeleteDe->dwDeCount			= pDeInfo->dwDeCount;

	FFAT_ASSERT((pDeleteDe->dwDeStartCluster == 1) ? (NODE_COP(pNode) == VOL_RC(pVol)) 
							: (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pDeleteDe->dwDeStartCluster) == FFAT_TRUE));
	FFAT_ASSERT(pDeleteDe->dwDeStartOffset < FAT_DIR_SIZE_MAX);
	FFAT_ASSERT(pDeleteDe->dwDeCount <= (FAT_DE_COUNT_MAX + 1));		// +1 : for XDE

	_subLogCheckDeStartCluster(pVol, pDeleteDe->udwCluster, 
				&pDeleteDe->dwDeStartCluster, pDeleteDe->dwDeStartOffset, pCxt);

	FFAT_ASSERT((pDeleteDe->dwDeStartCluster == 1) ? (NODE_COP(pNode) == VOL_RC(pVol)) 
							: (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pDeleteDe->dwDeStartCluster) == FFAT_TRUE));
	FFAT_ASSERT(pDeleteDe->dwDeStartOffset < FAT_DIR_SIZE_MAX);
	FFAT_ASSERT(pDeleteDe->dwDeCount <= (FAT_DE_COUNT_MAX + 1));			// +1 : for XDE

	FFAT_MEMCPY(&pDeleteDe->stDE, &pNode->stDE, sizeof(FatDeSFN));

	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogDeleteDe));

	if(!(pLH->uwFlag & LM_FLAG_SYNC))//2010_0323_kyungsik Add to delete 0KB file.
	{
		pLH->uwFlag |= LM_FLAG_FAT_DIRTY;
	}
	_PRINT_SUBLOG(pVol, pSL);

	return FFAT_OK;
}


/**
* _sublogGenUpdateDE generates update DE sub-log
* 
* @param	ppSublog		: [IN/OUT] pointer of sub-log
* @param	pNode			: [IN] pointer of node
* @param	pstOldSFN 		: [IN] old SFN DE information, maybe null
* @param	pNewXDEInfo		: [IN] new XDE information
* @return	FFAT_OK			: success
* @version	DEC-05-2008 [JeongWoo Park] Add the code that consider XDE
* @version	MAR-30-2009 [GwangOk Go] Add pNewXDEInfo parameter
*/
static FFatErr
_sublogGenUpdateDE(LogHeader* pLH, Node* pNode, FatDeSFN* pDEOldSFN, XDEInfo* pNewXDEInfo)
{
	SubLog* 		pSL;
	SubLogUpdateDe*	pUDE;

	FFAT_ASSERT(pLH);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_UPDATE_DE;

	pUDE = &pSL->u.stUpdateDE;

	pUDE->dwClusterSFNE	= pNode->stDeInfo.dwDeClusterSFNE;
	pUDE->dwOffsetSFNE	= pNode->stDeInfo.dwDeOffsetSFNE;

	FFAT_ASSERT((pUDE->dwClusterSFNE == 1) ? (NODE_COP(pNode) == VOL_RC(NODE_VOL(pNode))) 
											: (FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pUDE->dwClusterSFNE) == FFAT_TRUE));
	FFAT_ASSERT(pUDE->dwOffsetSFNE < FAT_DIR_SIZE_MAX);

	FFAT_ASSERT((pUDE->dwClusterSFNE == FFATFS_FAT16_ROOT_CLUSTER) ? (pUDE->dwClusterSFNE == NODE_COP(pNode))
											: (FFATFS_IS_VALID_CLUSTER(NODE_VI(pNode), pUDE->dwClusterSFNE) == FFAT_TRUE));
	FFAT_ASSERT(pUDE->dwOffsetSFNE < FAT_DIR_SIZE_MAX);

	// fill DE info
	if (pDEOldSFN == NULL)
	{
		FFAT_MEMSET(&pUDE->stOldDE, 0x00, sizeof(FatDeSFN));
	}
	else
	{
		FFAT_MEMCPY(&pUDE->stOldDE, pDEOldSFN, sizeof(FatDeSFN));
	}

	FFAT_MEMCPY(&pUDE->stNewDE, &pNode->stDE, sizeof(FatDeSFN));
//2010_0323_kyungsik
/*
	if (pNewXDEInfo == NULL)
	{
		pUDE->bUpdateXDE = FFAT_FALSE;
	}
*/
	if ((pNewXDEInfo == NULL) || ((pNode->stDE.bNTRes & ADDON_SFNE_MARK_XDE) == 0))
	{
		pUDE->bUpdateXDE = FFAT_FALSE;
	}
	else
	{
		XDEInfo*	pOldXDEInfo;

		pOldXDEInfo = &(NODE_ADDON(pNode)->stXDE);

		pUDE->stOldXDEInfo.dwUID	= pOldXDEInfo->dwUID;
		pUDE->stOldXDEInfo.dwGID	= pOldXDEInfo->dwGID;
		pUDE->stOldXDEInfo.dwPerm	= pOldXDEInfo->dwPerm;

		pUDE->stNewXDEInfo.dwUID	= pNewXDEInfo->dwUID;
		pUDE->stNewXDEInfo.dwGID	= pNewXDEInfo->dwGID;
		pUDE->stNewXDEInfo.dwPerm	= pNewXDEInfo->dwPerm;

		pUDE->bUpdateXDE = FFAT_TRUE;
	}

	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogUpdateDe));

	if(!(pLH->uwFlag & LM_FLAG_SYNC))//2010_0323_kyungsik Add to update the gid & uid.on the Log recovery
	{
		pLH->uwFlag |= LM_FLAG_FAT_DIRTY;
	}
	_PRINT_SUBLOG(NODE_VOL(pNode), pSL);

	return FFAT_OK;
}


/**
* _sublogGenFat generates allocate/deallocate FAT chain sub-log
* 
* @param	pVol			: [IN] pointer of volume
* @param	pNode			: [IN] node pointer
* @param	pLH				: [IN] log header
* @param	dwCluster		: [IN] start cluster of FAT chain. 
* 							  It is used to distinguish current final FAT chain with 
* 							  other existing FAT chains in recovery process of removable device.
* 							  If a FAT chain is totally freed from beginning, dwCluster = 0.
* @param	pAlloc			: [IN] allocate/deallocate FAT chain information
* @param	wReservedSize	: [IN] reserved size in log slot
* @return	FFAT_OK			: success
* @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode to identify whether node is HPA or normal
*/
static FFatErr
_sublogGenFat(Vol* pVol, Node* pNode, LogHeader* pLH, t_uint32 dwCluster,
				FatAllocate* pAlloc, t_int16 wReservedSize)
{
	SubLog*			pSL;
	SubLogFat*		pFat;
	t_int32			dwValidEntryCount;
	t_int32			dwVCESize;
	t_int8*			pCurrentPos;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT((dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT((pAlloc->dwCount >= 0) && (pAlloc->dwCount <= (t_int32)VOL_CC(pVol)));
	FFAT_ASSERT((pAlloc->dwFirstCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pAlloc->dwFirstCluster) == FFAT_TRUE) : FFAT_TRUE);
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pFat = &pSL->u.stFat;

	pFat->udwCluster			= dwCluster;
	pFat->udwPrevEOF			= pAlloc->dwPrevEOF;
	pFat->dwCount				= pAlloc->dwCount;
	pFat->udwFirstCluster		= pAlloc->dwFirstCluster;
	pFat->udwHPAType			= (NODE_ADDON_FLAG(pNode) & ADDON_NODE_HPA) ? _HPA : _Normal;

	dwValidEntryCount			= pAlloc->pVC->dwValidEntryCount;
	pFat->dwValidEntryCount 	= dwValidEntryCount;

	pCurrentPos		= (t_int8*)pSL + (sizeof(SubLogHeader) + sizeof(SubLogFat));
	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogFat));

	dwVCESize = dwValidEntryCount * sizeof(FFatVCE);

	if ((pLH->uwFlag & LM_FLAG_SYNC)
		|| (_IS_OUT_OF_RANGE(pVol, pLH->wUsedSize, dwVCESize, wReservedSize) == FFAT_TRUE))
//		|| (dwVCESize == 0))	--> 추가한 이유는? delete many files 성능이 떨어져 삭제함 (jade, 2008-12-24)
	{
		pFat->dwValidEntryCount = 0;
		pLH->uwFlag |= LM_FLAG_SYNC;
	}
	else if (dwVCESize != 0)
	{
		FFAT_MEMCPY(pCurrentPos, pAlloc->pVC->pVCE, dwVCESize);
		pLH->wUsedSize = pLH->wUsedSize + (t_uint16)dwVCESize;
		pLH->uwFlag |= LM_FLAG_FAT_DIRTY;
	}

	_PRINT_SUBLOG(pVol, pSL);

	return FFAT_OK;
}


/**
* _sublogGenAllocateFat generates allocate FAT chain sub-log
* 
* @param	pVol			: [IN] pointer of volume
* @param	pNode			: [IN] node pointer
* @param	pLH				: [IN] Log header
* @param	dwCluster		: [IN] start cluster of FAT chain before allocating clusters
*									may be 0
* @param	pAlloc			: [IN] allocate FAT chain information
* @param	wReservedSize	: [IN] reserved byte for next log
* @return	FFAT_OK			: success
* @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
* @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenFat
*/
static FFatErr
_sublogGenAllocateFat(Vol* pVol, Node* pNode, LogHeader* pLH, t_uint32 dwCluster, FatAllocate* pAlloc,
					t_int16 wReservedSize)
{
	SubLog*			pSL;

	FFAT_ASSERT(pLH);
	FFAT_ASSERT((dwCluster != 0) ? (FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), dwCluster) == FFAT_TRUE) : FFAT_TRUE);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_ALLOC_FAT;

	_sublogGenFat(pVol, pNode, pLH, dwCluster, pAlloc, wReservedSize);

	return FFAT_OK;
}


/**
* _sublogGenDeallocateFat generates deallocate FAT chain sub-log
* 
* @param	pVol			: [IN] pointer of volume
* @param	pNode			: [IN] node pointer
* @param	pLH				: [IN] pointer of log header
* @param	dwCluster		: [IN] start cluster of FAT chain after deallocating clusters
* @param	pAlloc			: [IN] deallocate FAT chain information
* @param	wReservedSize	: [IN] reserved byte for next log
* @return	FFAT_OK			: success
* @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
* @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenFat
*/
static FFatErr
_sublogGenDeallocateFat(Vol* pVol, Node* pNode, LogHeader* pLH, t_uint32 dwCluster,
						FatAllocate* pAlloc, t_int16 wReservedSize)
{
	SubLog* 		pSL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_DEALLOC_FAT;

	_sublogGenFat(pVol, pNode, pLH, dwCluster, pAlloc, wReservedSize);

	return FFAT_OK;
}


/**
 * generate SubLogEA
 * 
 * @param	pVol 			: [IN] pointer of volume
 * @param	pLH				: [IN] pointer of log header
 * @param	udwFirstCluster	: [IN] first cluster of extended attribute
 * @param	udwDelOffset	: [IN] offset of entry to be deleted
 * @param	udwInsOffset	: [IN] offset of entry to be inserted
 * @param	pEAMain			: [IN] EAMain
 * @param	pEAEntry		: [IN] EAEntry
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author	GwangOk Go
 * @version	AUG-06-2008 [GwangOk Go] First Writing
 * @version	AUG-20-2008 [DongYoung Seo] modify uwUsedSize setting routine
 *								from "+ sizeof(SubLogDeleteDe)" to "+ sizeof(SubLogEA)"
 */
static FFatErr
_sublogGenEA(Vol* pVol, LogHeader* pLH, t_uint16 uwSublogType, t_uint32 udwFirstCluster,
			t_uint32 udwDelOffset, t_uint32 udwInsOffset, EAMain* pEAMain, EAEntry* pEAEntry)
{
	SubLog* 		pSL;
	SubLogEA*		pEA;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = uwSublogType;

	pEA = &pSL->u.stEA;

	pEA->udwFirstCluster	= udwFirstCluster;
	pEA->udwDelOffset		= udwDelOffset;
	pEA->udwInsOffset		= udwInsOffset;

	FFAT_MEMCPY(&pEA->stEAMain, pEAMain, sizeof(EAMain));
	FFAT_MEMCPY(&pEA->stEAEntry, pEAEntry, sizeof(EAEntry));

	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogEA));

	_PRINT_SUBLOG(pVol, pSL);

	return FFAT_OK;
}

/**
* generate SubLogUpdateRootEA
* 
* @param	pVol				: [IN] pointer of volume
* @param	pLH					: [IN] pointer of log header
* @param	udwOldRootEACluster	: [IN] old root EA first cluster
* @param	udwNewRootEACluster	: [IN] new root EA first cluster
* @return	FFAT_OK				: success
* @return	else				: failed
* @author	JeongWoo Park
* @version	DEC-22-2008 [JeongWoo Park] First Writing
*/
static FFatErr
_sublogGenUpdateRootEA(Vol* pVol, LogHeader* pLH,
					t_uint32 udwOldRootEACluster, t_uint32 udwNewRootEACluster)
{
	SubLog* 			pSL;
	SubLogUpdateRootEA*	pstUpdateRootEA;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwSublogType = LM_SUBLOG_UPDATE_ROOT_EA;

	pstUpdateRootEA = &pSL->u.stUpdateRootEA;

	pstUpdateRootEA->udwOldFirstCluster	= udwOldRootEACluster;
	pstUpdateRootEA->udwNewFirstCluster	= udwNewRootEACluster;

	pLH->wUsedSize	+= (sizeof(SubLogHeader) + sizeof(SubLogUpdateRootEA));

	_PRINT_SUBLOG(pVol, pSL);

	return FFAT_OK;
}


/**
* read name from extra slot
 * @param	pVol			: [IN] volume pointer
 * @param	psName			: [IN/OUT] Name storage
 * @param	uwNameLen		: [IN] length of psName storage
 * @param	wExtraNo		: [IN] number of extra slot
 * @param	pCxt			: [IN] context of current operation
 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_sublogExtraReadFilename(Vol* pVol, t_wchar* psName, t_uint16 uwNameLen,
							t_int16 wExtraNo, ComCxt* pCxt)
{
	LogInfo*	pLI;
	t_int32		dwIndex;		// log sector index
	FFatErr		r;

	FFAT_ASSERT(pVol);

	pLI		= _LOG_INFO(pVol);
	dwIndex	= LOG_MAX_SLOT + wExtraNo;

	FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], 1, (t_int8*)psName,
					(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC), FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("fail to read filename from extra log space")));
		return FFAT_EIO;
	}

	psName[uwNameLen] = 0;

	return FFAT_OK;
}


/** 
 * _sublogRedoCreateDe redo create DE sub-log
 * 
 * @param pVol 			: [IN] pointer of volume
 * @param ppSublog 		: [IN/OUT] pointer of sub-log
 * @param state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param		pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version	???-??-???? [-------------] First Writing
 * @version	JAN-21-2009 [JeongWoo Park] Bug fix for (SFN + XDE) wrong update
 */
static FFatErr
_sublogRedoCreateDe(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	FFatErr				r;
	SubLogCreateDe*		pCreate;			//create DE information
	FatDeLFN*			pDE = NULL;
	t_int32				dwLFNE_Count;		// LFN Entry Count
	t_uint8				bCheckSum = 0;
	t_int16				wNameLen;
	t_int8*				pCurrentPos;
	t_wchar*			psName;
	t_int32				dwUpdateDECount;	// DE count for update
	t_int32				dwOffset;			// offset of DE

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_CREATE_DE);

	pCreate = &((*ppSublog)->u.stCreateDE);
	if (pCreate->dwDeCount == 0)
	{
		return FFAT_OK;
	}

	pDE = (FatDeLFN*)FFAT_LOCAL_ALLOC(VOL_MSD(pVol), pCxt);
	FFAT_ASSERT(pDE);

	psName = (t_wchar*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(psName);

	pCurrentPos = (t_int8*)pCreate + sizeof(SubLogCreateDe);

	wNameLen = pCreate->wNameLen;
	if (wNameLen == 0)  //1. no LFN 2. LFN is too long
	{
		// no long filename
		dwLFNE_Count = 0;
	}
	else
	{
#ifdef FFAT_VFAT_SUPPORT
		bCheckSum = FFATFS_GetCheckSum(&pCreate->stDE);
		if (pCreate->wNameInExtra == FFAT_FALSE)
		{
			FFAT_ASSERT(wNameLen > 0);

			FFAT_WCSNCPY(psName, (t_wchar*)pCurrentPos, wNameLen);
			pCurrentPos += (_makeIncEven(wNameLen)) * sizeof(t_wchar);
		}
		else
		{
			r = _sublogExtraReadFilename(pVol, psName, wNameLen, pCreate->wExtraNo, pCxt);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Failed to read filename from extra spaces")));
				goto out;
			}

			_boSubLogFileName(psName, (t_uint16)wNameLen);

			//do not need to change pCurrentPos
		}

		psName[wNameLen] = 0;

		r = FFATFS_GenLFNE(psName, wNameLen, pDE, &dwLFNE_Count, bCheckSum);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("Generate LFN failed")));
			goto out;
		}
#else
		FFAT_ASSERT(0);
#endif
	}

	// copy SFN to pDE
	FFAT_MEMCPY(&pDE[dwLFNE_Count], &pCreate->stDE, sizeof(FatDeSFN));

	ffat_xde_generateXDE(pVol, &pCreate->stDE, (ExtendedDe*)(pDE + dwLFNE_Count + 1),
						pCreate->udwUid, pCreate->udwGid, pCreate->uwPerm, bCheckSum);

	if (wNameLen == 0)		//1. no LFN 2. LFN is too long
	{
		//modify SFN only
		//If LFN is too long, the usual process is undo instead of redo.
		//Special case: the code can reach here only when redo unfinished creating existing filename
		//LFN modification information will be lost, but it is not important

		if (VOL_FLAG(pVol) & VOL_ADDON_XDE)
		{
			// if XDE is activated, then SFN + XDE must be written.
			dwUpdateDECount = 2;
		}
		else
		{
			// only SFN must be written.
			dwUpdateDECount = 1;
		}

		FFAT_ASSERT(pCreate->dwDeCount >= dwUpdateDECount);

		// get offset from Start Cluster
		dwOffset	= pCreate->dwDeStartOffset +
						((pCreate->dwDeCount - dwUpdateDECount) << FAT_DE_SIZE_BITS);

		r = ffat_writeDEs(pVol, pCreate->dwCluster, 0,
						dwOffset, (t_int8*)&pDE[dwLFNE_Count],
						(dwUpdateDECount * FAT_DE_SIZE),
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}
	else
	{
		//write LFN & SFN
		r = ffat_writeDEs(pVol, pCreate->dwCluster, pCreate->dwDeStartCluster,
						pCreate->dwDeStartOffset,
						(t_int8*)pDE, (pCreate->dwDeCount * FAT_DE_SIZE),
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}

	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Write LFN & SFN failed")));
		goto out;
	}

	*ppSublog = (SubLog*) pCurrentPos;

out:
	FFAT_LOCAL_FREE(psName, VOL_SS(pVol), pCxt);
	FFAT_LOCAL_FREE(pDE, VOL_MSD(pVol), pCxt);

	return r;
}


/**
 * _sublogUndoCreateDe undo create DE sub-log
 * 
 * @param pVol			: [IN] pointer of volume
 * @param ppSublog		: [IN/OUT] pointer of sub-log
 * @param state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param pCxt			: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogUndoCreateDe(Vol* pVol, SubLog** ppSublog, 
					_TransactionStateType state, ComCxt* pCxt)
{
	FFatErr				r;
	SubLogCreateDe* 	pCreateDe;			//create DE information
	t_uint16			uwNameLen;
	t_int8*				pCurrentPos;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_CREATE_DE);
	
	pCreateDe = &((*ppSublog)->u.stCreateDE);

	r = ffat_deleteDEs(pVol, pCreateDe->dwDeStartCluster, pCreateDe->dwDeStartOffset,
				pCreateDe->dwDeStartCluster, pCreateDe->dwDeCount, FFAT_FALSE, 
				(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to delete DE\n")));
	}

	pCurrentPos		= (t_int8*)pCreateDe + sizeof(SubLogCreateDe);
	uwNameLen		= pCreateDe->wNameLen;
	pCurrentPos		+= uwNameLen * sizeof(t_wchar);

	if (uwNameLen & 0x0001)
	{
		pCurrentPos += sizeof(t_wchar);
	}

	*ppSublog = (SubLog*) pCurrentPos;

	return r;
}


/** 
 * _sublogRedoUpdateDe redo update DE sub-log
 * 
 * @param		pVol 		: [IN] pointer of volume
 * @param		ppSublog 	: [IN/OUT] pointer of sub-log
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogRedoUpdateDe(Vol* pVol, SubLog** ppSublog, ComCxt* pCxt)
{
	FFatErr				r;
	SubLogUpdateDe* 	pUpdateDe;	//update DE information
	t_int8*				pCurrentPos;

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_DE);

	pUpdateDe = &((*ppSublog)->u.stUpdateDE);

	if (pUpdateDe->bUpdateXDE == FFAT_FALSE)
	{
		r = ffat_writeDEs(pVol, 0, pUpdateDe->dwClusterSFNE, pUpdateDe->dwOffsetSFNE,
						(t_int8*)&pUpdateDe->stNewDE, FAT_DE_SIZE,
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}
	else
	{
		FatDeSFN		stDE[2];	// SFNE + XDE

		FFAT_MEMCPY(&stDE[0], &pUpdateDe->stNewDE, sizeof(FatDeSFN));

		// generate XDE
		ffat_xde_generateXDE(pVol, &pUpdateDe->stNewDE,
							(ExtendedDe*)&stDE[1],
							pUpdateDe->stNewXDEInfo.dwUID,
							pUpdateDe->stNewXDEInfo.dwGID,
							(t_uint16)pUpdateDe->stNewXDEInfo.dwPerm,
							FFATFS_GetCheckSum(&pUpdateDe->stNewDE));

		// write SFNE & XDE
		r = ffat_writeDEs(pVol, 0, pUpdateDe->dwClusterSFNE, pUpdateDe->dwOffsetSFNE,
						(t_int8*)stDE, (FAT_DE_SIZE * 2),
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}

	pCurrentPos = (t_int8*)pUpdateDe + sizeof(SubLogUpdateDe);
	*ppSublog = (SubLog*) pCurrentPos;

	return r;
}


/** 
 * _sublogUndoUpdateDe undo update DE sub-log
 * 
 * @param		pVol		: [IN] pointer of volume
 * @param		ppSublog	: [IN/OUT] pointer of sub-log
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		else		: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogUndoUpdateDe(Vol* pVol, SubLog** ppSublog, ComCxt* pCxt)
{
	FFatErr				r;
	SubLogUpdateDe*		pUpdateDe;	//update DE information
	t_int8*				pCurrentPos;

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_DE);

	pUpdateDe = &((*ppSublog)->u.stUpdateDE);

	if (pUpdateDe->bUpdateXDE == FFAT_FALSE)
	{
		//write SFN
		r = ffat_writeDEs(pVol, 0, pUpdateDe->dwClusterSFNE, pUpdateDe->dwOffsetSFNE,
						(t_int8*)&pUpdateDe->stOldDE, FAT_DE_SIZE,
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}
	else
	{
		FatDeSFN		stDE[2];	// SFNE + XDE

		FFAT_MEMCPY(&stDE[0], &pUpdateDe->stOldDE, sizeof(FatDeSFN));

		// generate XDE
		ffat_xde_generateXDE(pVol, &pUpdateDe->stOldDE,
							(ExtendedDe*)&stDE[1],
							pUpdateDe->stOldXDEInfo.dwUID,
							pUpdateDe->stOldXDEInfo.dwGID,
							(t_uint16)pUpdateDe->stOldXDEInfo.dwPerm,
							FFATFS_GetCheckSum(&pUpdateDe->stOldDE));

		// write SFNE & XDE
		r = ffat_writeDEs(pVol, 0, pUpdateDe->dwClusterSFNE, pUpdateDe->dwOffsetSFNE,
						(t_int8*)stDE, (FAT_DE_SIZE * 2),
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	}

	pCurrentPos = (t_int8*)pUpdateDe + sizeof(SubLogUpdateDe);
	*ppSublog = (SubLog*) pCurrentPos;
	
	return r;
}


/** 
 * _sublogRedoDeleteDe redo delete DE sub-log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pDeleteDe		: [IN] delete DE information
 * @param	ppSublog		: [IN/OUT] pointer of sub-log
 * @param	state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param	pCxt		: [IN] context of current operation
 * @return	FFAT_OK		: success
 * @return	else			: failed
 * @author
 * @version
 */
static FFatErr
_sublogRedoDeleteDe(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	FFatErr			r;
	SubLogDeleteDe* 	pDeleteDe;
	t_int8*			pCurrentPos;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_DELETE_DE);

	pDeleteDe = &((*ppSublog)->u.stDeleteDE);

	r = ffat_deleteDEs(pVol, pDeleteDe->dwDeStartCluster, pDeleteDe->dwDeStartOffset,
				pDeleteDe->dwDeStartCluster, pDeleteDe->dwDeCount, FFAT_FALSE, 
				(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to delete DE\n")));
	}

	pCurrentPos = (t_int8*)pDeleteDe + sizeof(SubLogDeleteDe);
	*ppSublog = (SubLog*) pCurrentPos;
	
	return r;
}


/** 
 * _getFatFirstCluster gets first cluster from PrevEOF when first cluster is not given
 * 
 * @param		pVol 		: [IN] pointer of volume
 * @param		pFat 		: [IN] pointer of LM_FAT
 * @param		pCxt		: [IN] context of current operation
 * @return		FFAT_OK		: success
 * @return		negative	: fail
 * @author 
 * @version 
 */
static FFatErr
_getFatFirstCluster(Vol* pVol, SubLogFat* pFat, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32 		dwNextCluster;

	if (pFat->udwFirstCluster == 0)
	{
		//try to get first cluster
		if (pFat->udwPrevEOF != 0)
		{
			r = FFATFS_GetNextCluster(VOL_VI(pVol), pFat->udwPrevEOF, &dwNextCluster, pCxt);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("Failed to read FAT table!\n")));
				return r;
			}
			if (FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) || (dwNextCluster == 0))
			{
				pFat->udwFirstCluster = 0;
				goto out;
			}
			pFat->udwFirstCluster = dwNextCluster;
		}
	}

out:
	return FFAT_OK;
}


/**
 * _subLogDoFat do allocate/deallocate FAT sub-log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	uwActionType	: [IN] _ALLOC_FAT : do allocate FAT sub-log
 * 								_DEALLOC_FAT : do deallocate FAT sub-log
 * @param	ppSL			: [IN/OUT] pointer of sub-log
 * @param	state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author
 * @version	03-DEC-2008 [DongYoung Seo] remove external device checking code
 *								it may not may all clusters chain on it
 *								when ejected while cluster allocation
 * @version	30-MAR-2009 [DongYoung Seo] increase pointer of next log event if there is no cluster info.
 * @version	Aug-29-2009 [SangYoon Oh] Add the code to update Partial HPA when the pFAT is identified as HPA
 */
static FFatErr
_subLogDoFat(Vol* pVol, _FATActionType uwActionType, SubLog** ppSL,
		_TransactionStateType state, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;
	SubLogFat* 		pFat;			// allocate/deallocate FAT information
	FatAllocate		stAlloc;
	t_uint16		uwSublogType;
	t_int32			dwValidEntryCount;
	t_int8*			pCurrentPos;
	t_uint32		dwTemp;
	FFatVC			stTempVC;		// temporary VC
	FFatCacheFlag	dwFFatCacheFlag;

	dwFFatCacheFlag = (FFAT_CACHE_FORCE | FFAT_CACHE_SYNC);
	FFAT_ASSERT((uwActionType == _ALLOC_FAT) || (uwActionType == _DEALLOC_FAT));

	uwSublogType = (*ppSL)->stSubLogHeader.uwSublogType;

	FFAT_ASSERT((uwSublogType == LM_SUBLOG_ALLOC_FAT) || (uwSublogType == LM_SUBLOG_DEALLOC_FAT));

	pFat = &((*ppSL)->u.stFat);

	//if not exist first cluster, return directly
	_getFatFirstCluster(pVol, pFat, pCxt);
	if (pFat->udwFirstCluster == 0)
	{
		goto out;
	}

	stAlloc.dwCount			= pFat->dwCount;
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= pFat->udwPrevEOF;
	stAlloc.dwFirstCluster	= pFat->udwFirstCluster;
	stAlloc.dwLastCluster	= 0;
	stAlloc.pVC				= NULL;

	pCurrentPos = (t_int8*)pFat + sizeof(SubLogFat);

	dwValidEntryCount = pFat->dwValidEntryCount;
	if (dwValidEntryCount > 0)
	{
		stAlloc.pVC = &stTempVC;

		stAlloc.pVC->dwTotalClusterCount	= pFat->dwCount;
		stAlloc.pVC->dwValidEntryCount		= dwValidEntryCount;

		stAlloc.pVC->dwTotalEntryCount = FFAT_ALLOC_BUFF_SIZE / sizeof(FFatVCE);

		stAlloc.pVC->pVCE = (FFatVCE*)pCurrentPos;

		pCurrentPos += (dwValidEntryCount * sizeof(FFatVCE));
	}

	FFAT_ASSERT((uwActionType == _ALLOC_FAT) || (uwActionType == _DEALLOC_FAT));

	if (uwActionType == _ALLOC_FAT)
	{
		if (stAlloc.dwCount != 0)
		{
			FFAT_ASSERT(dwValidEntryCount > 0);

			if (dwValidEntryCount == 0)
			{
				// this must be undo
				FFAT_LOG_PRINTF((_T("Fail to recover from log")));
				return FFAT_EPROG;
			}

			r = FFATFS_MakeClusterChainVC(VOL_VI(pVol), stAlloc.dwPrevEOF, stAlloc.pVC, 
							FAT_UPDATE_FORCE, dwFFatCacheFlag, NULL, pCxt);

			if (pFat->udwHPAType == _HPA)
			{
				r = ffat_hpa_updatePartialHPA(pVol, NULL, stAlloc.dwFirstCluster, stAlloc.dwCount, stAlloc.pVC, FFAT_TRUE, dwFFatCacheFlag, pCxt);
				FFAT_ASSERT(r == FFAT_OK);
			}
		}
	}
	else
	{
		FFAT_ASSERT(uwActionType == _DEALLOC_FAT);

		if (pFat->udwHPAType == _HPA)
		{
			r = ffat_hpa_updatePartialHPA(pVol, NULL, stAlloc.dwFirstCluster, stAlloc.dwCount, stAlloc.pVC, FFAT_FALSE, dwFFatCacheFlag, pCxt);
			FFAT_ASSERT(r == FFAT_OK);
		}

		r = FFATFS_DeallocateCluster(VOL_VI(pVol), pFat->dwCount, &stAlloc, &dwTemp,
						NULL, FAT_DEALLOCATE_FORCE, dwFFatCacheFlag,
						NULL, pCxt);
	}

	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to allocate/deallocate clusters\n")));
	}

out:
	*ppSL = (SubLog*)((t_int8*)(*ppSL) + _sublogGetSize(*ppSL));

	return r;
}


/**
 * _sublogRedoAllocateFat redo allocate FAT sub-log. The function is called only when pVCE is not empty. 
 * It is usually called by recover finished transaction process.
 *
 * Special case:
 * Called by recover unfinished transaction process when allocate SubLogFat for expanding DE in rename 
 * In this situation, to removable device, it returns FFAT_OK1 if the allocate FAT is occupied by another file.
 *
 * @param	pVol 			: [IN] pointer of volume
 * @param	ppSublog 		: [IN/OUT] pointer of sub-log
 * @param	state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author
 * @version
 */
static FFatErr
_sublogRedoAllocateFat(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	return _subLogDoFat(pVol, _ALLOC_FAT, ppSublog, state, pCxt);
}


/** 
 * _sublogUndoAllocateFat undo allocate FAT sub-log
 * 
 * @param pVol 			: [IN] pointer of volume
 * @param pFat 			: [IN] allocate FAT information
 * @param ppSublog 		: [IN/OUT] pointer of sub-log
 * @param state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param		pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogUndoAllocateFat(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	return _subLogDoFat(pVol, _DEALLOC_FAT, ppSublog, state, pCxt);
}


/** 
 * _sublogRedoDeallocateFat redo deallocate FAT sub-log
 * 
 * @param pVol 			: [IN] pointer of volume
 * @param ppSublog 		: [IN/OUT] pointer of sub-log
 * @param state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogRedoDeallocateFat(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	return _subLogDoFat(pVol, _DEALLOC_FAT, ppSublog, state, pCxt);
}


/** 
 * _sublogUndoDeallocateFat undo deallocate FAT sub-log
 * 
 * @param pVol 			: [IN] pointer of volume
 * @param ppSublog 		: [IN/OUT] pointer of sub-log
 * @param state			: [IN] transaction state type: Finished/UnFinished transaction
 * @param pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogUndoDeallocateFat(Vol* pVol, SubLog** ppSublog, _TransactionStateType state, ComCxt* pCxt)
{
	return _subLogDoFat(pVol, _ALLOC_FAT, ppSublog, state, pCxt);
}


/**
 * _sublogUndoSetEA undo extended attribute set
 * 
 * @param pVol			: [IN] pointer of volume
 * @param ppSublog		: [IN/OUT] pointer of sub-log
 * @param pCxt			: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogUndoSetEA(Vol* pVol, SubLog** ppSublog, ComCxt* pCxt)
{
	FFatErr			r;
	SubLogEA*		pEA;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_SET_EA);

	pEA = &((*ppSublog)->u.stEA);

	r = ffat_ea_undoSetEA(pVol, pEA->udwFirstCluster, pEA->udwDelOffset, pEA->udwInsOffset,
							&pEA->stEAMain, &pEA->stEAEntry, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to undo set EA\n")));
	}

	*ppSublog = (SubLog*)((t_int8*)pEA + sizeof(SubLogEA));

	return r;
}


/**
 * _sublogRedoDeleteEA redo extended attribute deletion
 * 
 * @param pVol			: [IN] pointer of volume
 * @param ppSublog		: [IN/OUT] pointer of sub-log
 * @param pCxt			: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_sublogRedoDeleteEA(Vol* pVol, SubLog** ppSublog, ComCxt* pCxt)
{
	FFatErr			r;
	SubLogEA*		pEA;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_DELETE_EA);
	
	pEA = &((*ppSublog)->u.stEA);

	FFAT_ASSERT(pEA->udwInsOffset == 0);

	r = ffat_ea_redoDeleteEA(pVol, pEA->udwFirstCluster, pEA->udwDelOffset,
							&pEA->stEAMain, &pEA->stEAEntry, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to delete EA\n")));
	}

	*ppSublog = (SubLog*)((t_int8*)pEA + sizeof(SubLogEA));

	return r;
}

/**
* _subLogUndoUpdateRootEA undo update Root extended attribute
* 
* @param		 pVol		: [IN] pointer of volume
* @param		ppSublog	: [IN/OUT] pointer of sub-log
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		else		: failed
* @author		JeongWoo Park
* @version		DEC-22-2008 [JeongWoo Park] : First Writing
*/
static FFatErr
_subLogUndoUpdateRootEA(Vol* pVol, SubLog** ppSublog, ComCxt* pCxt)
{
	FFatErr				r;
	SubLogUpdateRootEA*	pUpdteRootEA;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(ppSublog);

	FFAT_ASSERT((*ppSublog)->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_ROOT_EA);

	pUpdteRootEA = &((*ppSublog)->u.stUpdateRootEA);

	// update Root EA as OLD
	r = ffat_ea_setRootEAFirstCluster(pVol, pUpdteRootEA->udwOldFirstCluster,
									(FFAT_CACHE_FORCE | FFAT_CACHE_SYNC), pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to undo update Root EA\n")));
	}

	*ppSublog = (SubLog*)((t_int8*)pUpdteRootEA + sizeof(SubLogUpdateRootEA));

	return r;
}


/**
 * common function to redo log info
 *
 * @param		pVol		: [IN] pointer of volume 
 * @param		pLog		: [IN] pointer of log header
 * @param		state		: [IN] transaction state type: Finished/UnFinished transaction
 * @param		pCxt		: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-11-2008 [GwangOk Go] : First Writing
 * @version		MAR-30-2009 [JeongWoo Park] : Add the consideration of Open-unlink
 */
static FFatErr
_logRedoCommon(Vol* pVol, LogHeader* pLog, _TransactionStateType state, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;
	SubLog* 		pSL;
	_SubLogFlag		uwNextSLFlag;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLog);
	FFAT_ASSERT(pCxt);

	pSL = (SubLog*)((t_uint8*)pLog + sizeof(LogHeader));

	do
	{
		uwNextSLFlag	= FFAT_BO_UINT16((t_uint16)pSL->stSubLogHeader.uwNextSLFlag);

		FFAT_ASSERT((uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE) || (uwNextSLFlag == LM_SUBLOG_FLAG_LAST));

		_PRINT_SUBLOG(pVol, pSL);

		switch (pSL->stSubLogHeader.uwSublogType)
		{
			case LM_SUBLOG_ALLOC_FAT:
				r = _sublogRedoAllocateFat(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_DEALLOC_FAT:
				if ((pLog->udwLogType & LM_LOG_OPEN_UNLINK) &&
					(state == _Finished))
				{
					// Open unlink log will be redo -> de-allocation chain
					// but additional FAT alloc/dealloc log will undo/redo for this FAT chain.
					// So cluster chain will be not continuous from the start of cluster chain.
					// To prevent this, Finished operation with Open-unlink must skip
					// the log recovery about FAT deallocation.
					
					// Skip, Just increase pointer of sub log
					pSL = (SubLog*)((t_int8*)pSL + _sublogGetSize(pSL));
				}
				else
				{
					r = _sublogRedoDeallocateFat(pVol, &pSL, state, pCxt);
				}
				break;

			case LM_SUBLOG_UPDATE_DE:
				r = _sublogRedoUpdateDe(pVol, &pSL, pCxt);
				break;

			case LM_SUBLOG_CREATE_DE:
				r = _sublogRedoCreateDe(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_DELETE_DE:
				r = _sublogRedoDeleteDe(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_DELETE_EA:
				r = _sublogRedoDeleteEA(pVol, &pSL, pCxt);
				break;

			case LM_SUBLOG_SET_EA:
			case LM_SUBLOG_UPDATE_ROOT_EA:
			default:
				FFAT_ASSERT(0);
				break;
		}

	} while ((r == FFAT_OK) && (uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE));

	if (r < 0)
	{
		FFAT_PRINT_CRITICAL((_T("Fail to recover from log")));
		return r;
	}

	return FFAT_OK;
}


/**
 * common function to undo log info
 *
 * @param		pVol		: [IN] pointer of volume 
 * @param		pLog		: [IN] pointer of log header
 * @param		state		: [IN] transaction state type: Finished/UnFinished transaction
 * @param		pCxt		: [IN] context of current operation
 * @author		GwangOk Go
 * @version		AUG-11-2008 [GwangOk Go] : First Writing
 * @version		AUG-12-2008 [DongYoung Seo] : modify assert code to check uwNext.
 */
static FFatErr
_logUndoCommon(Vol* pVol, LogHeader* pLog, _TransactionStateType state, ComCxt* pCxt)
{
	FFatErr			r = FFAT_OK;
	SubLog* 		pSL;
	_SubLogFlag		uwNextSLFlag;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLog);
	FFAT_ASSERT(pCxt);

	pSL = (SubLog*)((t_uint8*)pLog + sizeof(LogHeader));

	do
	{
		uwNextSLFlag	= FFAT_BO_UINT16((t_uint16)pSL->stSubLogHeader.uwNextSLFlag);

		FFAT_ASSERT((uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE) || (uwNextSLFlag == LM_SUBLOG_FLAG_LAST));

		_PRINT_SUBLOG(pVol, pSL);

		switch (pSL->stSubLogHeader.uwSublogType)
		{
			case LM_SUBLOG_ALLOC_FAT:
				r = _sublogUndoAllocateFat(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_DEALLOC_FAT:
				r = _sublogUndoDeallocateFat(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_UPDATE_DE:
				r = _sublogUndoUpdateDe(pVol, &pSL, pCxt);
				break;

			case LM_SUBLOG_CREATE_DE:
				r = _sublogUndoCreateDe(pVol, &pSL, state, pCxt);
				break;

			case LM_SUBLOG_SET_EA:
				r = _sublogUndoSetEA(pVol, &pSL, pCxt);
				break;
			
			case LM_SUBLOG_UPDATE_ROOT_EA:
				r = _subLogUndoUpdateRootEA(pVol, &pSL, pCxt);
				break;

			case LM_SUBLOG_DELETE_DE:
			case LM_SUBLOG_DELETE_EA:
			default:
				r = FFAT_OK;
				FFAT_ASSERT(0);
				break;
		}
	} while ((r == FFAT_OK) && (uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE));

	return FFAT_OK;
}


/**
* _unfinishedHPA recover undo HPA operation (Undo/Redo operation)
* 
* @param pVol 		: [IN] pointer of volume 
* @param pLog 		: [IN] pointer of log header
* 
* @return FFAT_OK		: success
* @return else			: failed
* @author 
* @version 
*/
static FFatErr
_unfinishedHPA(Vol* pVol, LogHeader* pLog, ComCxt* pCxt)
{
	FFatErr	r;
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLog);

	r = ffat_hpa_undoHPACreate(pVol, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("Failed to recover HPA \n")));
	}

	return FFAT_OK;
}

/**
* _unfinishedEACompaction recover unfinished EA compaction (Undo/Redo operation)
* 
* @param	pVol		: [IN] pointer of volume 
* @param	pLog		: [IN] pointer of log header
* @return	FFAT_OK		: success
* @return	FFAT_EIO	: IO Error
* @return	else		: failed
* @author
* @version	DEC-05-2008 [JeongWoo Park] first writing
* @version	DEC-22-2008 [JeongWoo Park] Support ROOT EA
* @version	FEB-03-2009 [JeongWoo Park] Add the consideration for open-unlinked node.
* @version	MAR-13-2009 [DongYoung Seo] bug fix FLASH00020847
*										remove infinite loop caused by no sub-log offset increasing.
*/
static FFatErr
_unfinishedEACompaction(Vol* pVol, LogHeader* pLog, ComCxt* pCxt)
{
	FFatErr			r;
	SubLog*			pSL;			// sub log pointer
	_SubLogFlag		uwNextSLFlag;	// flag for next sub log
	t_boolean		bRedo;			// boolean for read/undo  (FFAT_TRUE:redo)

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLog);
	FFAT_ASSERT(pCxt);

	pSL = (SubLog*)((t_uint8*)pLog + sizeof(LogHeader));

	// ****************** CompactionEA 순서 *******************************************************
	//	1) 복사를 위한 새로운 Cluster allocate	(POR시 -> UNDO)
	//	2) EA 복사								(POR시 -> UNDO)
	//	3) DE(Root EA) update					(POR시 -> UNDO / REDO) <= update 여부가 판단 기준
	//	4) 기존 Cluster deallocate				(POR시 -> REDO)
	//    - open-unlink된 node의 경우 판단기준이 되는 DE가 없음 : 이 경우 REDO 되어야 함.
	//		(REDO시 기존 cluster deallocate가 되고 new cluster에 대해서는
	//		open-unlink slot 처리시 deallocate됨)
	// ********************************************************************************************

	// *** REDO / UNDO 여부 파악 : DE(Root EA)가 업데이트 되어 있으면 REDO, 그렇지 않으면 UNDO
	do
	{
		SubLogUpdateDe*		pUpdateDe;		//update DE information
		SubLogUpdateRootEA*	pUpdateRootEA;	// update Root EA information
		FatDeSFN			stSFNE;
		FFatCacheInfo		stCI;
		t_uint32			dwEAFirstClusterReal;
		t_uint32			dwEAFirstClustertmp;

		if (pSL->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_DE)
		{
			// UPDATE DE
			_PRINT_SUBLOG(pVol, pSL);
			FFAT_ASSERT((FFAT_BO_UINT16((t_uint16)pSL->stSubLogHeader.uwNextSLFlag)) == LM_SUBLOG_FLAG_CONTINUE);

			pUpdateDe = &(pSL->u.stUpdateDE);

			// Read SFNE from device
			if (pUpdateDe->dwClusterSFNE == FFATFS_FAT16_ROOT_CLUSTER)
			{
				FFAT_ASSERT(FFATFS_IS_FAT16(VOL_VI(pVol)) == FFAT_TRUE);

				r = FFATFS_ReadWriteOnRoot(VOL_VI(pVol), pUpdateDe->dwOffsetSFNE, (t_int8*)&stSFNE,
											FAT_DE_SIZE,(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC),
											FFAT_TRUE, NULL, pCxt);
				FFAT_ER(r, (_T("read SFNE is failed !")));
			}
			else
			{
				FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

				//2009.0716@chunum.kong_[fast read]_Cluster-unaligned-Read
				r = FFATFS_ReadWritePartialCluster(VOL_VI(pVol), pUpdateDe->dwClusterSFNE,
													(pUpdateDe->dwOffsetSFNE & VOL_CSM(pVol)), FAT_DE_SIZE,
													(t_int8*)&stSFNE, FFAT_TRUE, (FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC),
													&stCI, pCxt, FFAT_FALSE);
				IF_UK (r != FAT_DE_SIZE)
				{
					FFAT_LOG_PRINTF((_T("read SFNE is failed !!")));
					return FFAT_EIO;
				}
			}

			// 디바이스에 저장되어 있는 EA cluster 값 계산
			dwEAFirstClusterReal = (t_uint32)((FFAT_BO_UINT16(stSFNE.wCrtDate) << 16) + (FFAT_BO_UINT16(stSFNE.wCrtTime)));

			// log의 OldDE에 있는 EA cluster 값 계산
			dwEAFirstClustertmp = (t_uint32)((FFAT_BO_UINT16(pUpdateDe->stOldDE.wCrtDate) << 16) +
											(FFAT_BO_UINT16(pUpdateDe->stOldDE.wCrtTime)));

			if (dwEAFirstClusterReal == dwEAFirstClustertmp)
			{
				// Old DE로 남아있음 --> UNDO
				bRedo = FFAT_FALSE;
			}
			else
			{
				// log의 NewDE에 있는 EA cluster 값 계산
				dwEAFirstClustertmp = (t_uint32)((FFAT_BO_UINT16(pUpdateDe->stNewDE.wCrtDate) << 16) +
												(FFAT_BO_UINT16(pUpdateDe->stNewDE.wCrtTime)));
				if (dwEAFirstClusterReal == dwEAFirstClustertmp)
				{
					// New DE로 기록 되어 있음 --> REDO
					bRedo = FFAT_TRUE;
				}
				else
				{
					// 이상하다. 이것도 저것도 아닌 값을 가지고 있다 -> sector transaction이 안 지켜지는 듯..
					FFAT_ASSERT(0);
					return FFAT_EINVALID;
				}
			}
			pSL = (SubLog*)((t_int8*)pUpdateDe + sizeof(SubLogUpdateDe));
		}
		else if (pSL->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_ROOT_EA)
		{
			// UPDATE ROOT EA
			_PRINT_SUBLOG(pVol, pSL);
			FFAT_ASSERT((FFAT_BO_UINT16((t_uint16)pSL->stSubLogHeader.uwNextSLFlag)) == LM_SUBLOG_FLAG_CONTINUE);

			pUpdateRootEA = &(pSL->u.stUpdateRootEA);

			// 디바이스에 저장되어 있는 Root EA Cluster 값 계산
			r = ffat_ea_getRootEAFirstCluster(pVol, &dwEAFirstClusterReal, pCxt);
			FFAT_ER(r, (_T("read Root EA is failed !")));

			if (dwEAFirstClusterReal == pUpdateRootEA->udwOldFirstCluster)
			{
				// Old DE로 남아있음 --> UNDO
				bRedo = FFAT_FALSE;
			}
			else if (dwEAFirstClusterReal == pUpdateRootEA->udwNewFirstCluster)
			{
				// New DE로 기록 되어 있음 --> REDO
				bRedo = FFAT_TRUE;
			}
			else
			{
				// 이상하다. 이것도 저것도 아닌 값을 가지고 있다 -> sector transaction이 안 지켜지는 듯..
				FFAT_ASSERT(0);
				return FFAT_EINVALID;
			}

			pSL = (SubLog*)((t_int8*)pUpdateRootEA + sizeof(SubLogUpdateRootEA));
		}
		else
		{
			// OPEN UNLINKED NODE
			FFAT_ASSERT(pSL->stSubLogHeader.uwSublogType & (LM_SUBLOG_ALLOC_FAT | LM_SUBLOG_DEALLOC_FAT));

			// redo 처리 : redo시 old clusters에 대해서만 deallocte를 수행하는데
			// new clusters에 대해서는 open-unlink slot에 기록되어 있기 때문에 별도로 deallocate 처리가 됨.
			bRedo = FFAT_TRUE;
		}
	} while (0);

	// *** RECOVERY
	do
	{
		uwNextSLFlag	= FFAT_BO_UINT16((t_uint16)(pSL->stSubLogHeader.uwNextSLFlag));

		FFAT_ASSERT((uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE) || (uwNextSLFlag == LM_SUBLOG_FLAG_LAST));

		_PRINT_SUBLOG(pVol, pSL);

		switch (pSL->stSubLogHeader.uwSublogType)
		{
			// *** UNDO시 Compaction을 위해서 할당받았던 New cluster들 deallocate
			case LM_SUBLOG_ALLOC_FAT:
				if (bRedo == FFAT_FALSE)
				{
					r = _sublogUndoAllocateFat(pVol, &pSL, _UnFinished, pCxt);
				}
				else
				{
					// increase pointer of sub log
					pSL = (SubLog*)((t_int8*)pSL + _sublogGetSize(pSL));
				}

				break;

			// *** REDO시 EA 복사가 다 끝나고 DE update도 끝난 것이므로 기존 cluster를 deallocate 하면 됨
			case LM_SUBLOG_DEALLOC_FAT:
				if (bRedo == FFAT_TRUE)
				{
					r = _sublogRedoDeallocateFat(pVol, &pSL, _UnFinished, pCxt);
				}
				else
				{
					// increase pointer of sub log
					pSL = (SubLog*)((t_int8*)pSL + _sublogGetSize(pSL));
				}

				break;
			
			default:
				FFAT_ASSERT(0);
				break;
		}
	} while(uwNextSLFlag == LM_SUBLOG_FLAG_CONTINUE);

	return FFAT_OK;
}


/** 
 * _log_recover_last_transaction recover the last un-finished transaction
 * 
 * @param pVol 		: [IN] pointer of volume 
 * @param pLog 		: [IN] pointer of log header
 * @param pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_logRecoverUnfinishedTransaction(Vol* pVol, LogHeader* pLH, ComCxt* pCxt)
{
	FFatErr	r;
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);

	_PRINT_LOG(pVol, pLH, "UnfinishedTransaction");

	switch (pLH->udwLogType & (~LM_LOG_FLAG_MASK))
	{
		case LM_LOG_UNLINK:
		case LM_LOG_SHRINK:
		case LM_LOG_SET_STATE:
		case LM_LOG_RENAME:
		case LM_LOG_TRUNCATE_DIR:
		case LM_LOG_EA_DELETE:
			r = _logRedoCommon(pVol, pLH, _UnFinished, pCxt);
			break;

		case LM_LOG_CREATE_NEW:
		case LM_LOG_CREATE_SYMLINK:
		case LM_LOG_EXTEND:
		case LM_LOG_WRITE:
		case LM_LOG_EXPAND_DIR:
		case LM_LOG_EA_CREATE:
		case LM_LOG_EA_SET:
			r = _logUndoCommon(pVol, pLH, _UnFinished, pCxt);
			break;

		case LM_LOG_NONE:
		case LM_LOG_CONFIRM:
			r = FFAT_OK;
			break;
		case LM_LOG_HPA_CREATE:
			r = _unfinishedHPA(pVol, pLH, pCxt);
			break;
		case LM_LOG_EA_COMPACTION:
			r = _unfinishedEACompaction(pVol, pLH, pCxt);
			break;

		default:
			FFAT_ASSERT(0);
			r = FFAT_EINVALID;
			break;
	}

	return r;
}


/** 
 * _logRecoverFinishedTransaction redo finished transaction
 * 
 * @param pVol 		: [IN] pointer of volume 
 * @param pLog 		: [IN] pointer of log header
 * @param pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return else			: failed
 * @author 
 * @version 
 */
static FFatErr
_logRecoverFinishedTransaction(Vol* pVol, LogHeader* pLH, ComCxt* pCxt)
{
	FFatErr		r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);

	_PRINT_LOG(pVol, pLH, "FinishedTransaction");

	// Write the dirty DE info to cache or NAND
	switch (pLH->udwLogType & (~LM_LOG_FLAG_MASK))
	{
	case LM_LOG_CREATE_NEW:
	case LM_LOG_CREATE_SYMLINK:
	case LM_LOG_UNLINK:
	case LM_LOG_EXTEND:
	case LM_LOG_SHRINK:
	case LM_LOG_SET_STATE:
	case LM_LOG_WRITE:
	case LM_LOG_RENAME:
	case LM_LOG_EXPAND_DIR:
	case LM_LOG_TRUNCATE_DIR:
		r = _logRedoCommon(pVol, pLH, _Finished, pCxt);
		break;

	case LM_LOG_NONE:
	case LM_LOG_CONFIRM:
		r = FFAT_OK;
		break;

	case LM_LOG_HPA_CREATE:
		FFAT_ASSERT(0);
		// never reach here
		// HPA creation, showing and removal operation is synchronized.
		r = FFAT_EPROG;
		break;

	case LM_LOG_EA_CREATE:
	case LM_LOG_EA_SET:
	case LM_LOG_EA_DELETE:
	case LM_LOG_EA_COMPACTION:
		FFAT_ASSERT(0);	// can't reach here, because logging ea operations is synchronized
		r = FFAT_EPROG;
		break;

	default:
		FFAT_ASSERTP(0, (_T("We can't reach here !!")));
		r = FFAT_EINVALID;
		break;
	}

	return r;
}


/** 
 * _logRecovery recover file system by using log file
 * It read log file, redo finished dirty transaction, and recover last unfinished transaction
 * 
 * @param	pVol 			: [IN] pointer of volume 
 * @param	pwValidSlots	: [IN/OUT] number of valid log slots
 * @param	pdwLatestSeqNo	: [IN/OUT] the latest log sequence number, 
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author 
 * @version	FEB-28-2008 [DongYoung Seo] bug fix.
 *							log module does not recover log at the last entry
 *							fix : does not check wSlot at loop. it is checked at _getNextLog
 *								_getNextLog() return FFAT_OK2, when there is no more log entry and
 *								update pcurLog
 * @version	MAR-20-2009 [DongYoung Seo] remove assert for invalid log version
 * @version	JUN-15-2009 [JeongWoo Park] consider the partial LLW at first slot
 */
static FFatErr
_logRecovery(Vol* pVol, t_uint16* pwValidSlots, t_uint32* pdwLatestSeqNo, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint16		wSlot;
	t_uint32		dwCurrSeqNum;
	t_uint32		dwNextSeqNum;
	LogHeader*		pCurLog;			// pointer for current log
	LogHeader*		pNextLog;			// pointer for next log
	LogHeader*		pCurLHOrig;			// original pointer for current log
	LogHeader*		pNextLHOrig;		// original pointer for next log
	t_boolean		bNextIsNewLLW = FFAT_FALSE;
										// Next log is a new LLW

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pwValidSlots);
	FFAT_ASSERT(pdwLatestSeqNo);
	FFAT_ASSERT(pCxt);

	// allocate memory for log record
	r = _allocAndInitLogHeader(pVol, &pCurLHOrig, LM_LOG_NONE, FFAT_CACHE_NONE, pCxt);
	FFAT_ASSERT(r == FFAT_OK);

	r = _allocAndInitLogHeader(pVol, &pNextLHOrig, LM_LOG_NONE, FFAT_CACHE_NONE, pCxt);
	FFAT_ASSERT(r == FFAT_OK);

	pNextLog = pNextLHOrig;

	// read the first log slot
	r = _readSlot(pVol, pCurLHOrig, 0, pCxt);
	IF_UK (r < 0)
	{
		// set random sequence number to avoiding seq number conflict.
		*pdwLatestSeqNo = ((t_uint16)FFAT_RAND()) & 0x7FFF;
		
		// [Jw.Park 2009-06-15]If first slot is invalid(partial LLW etc), just return OK to prevent mount failure.
		r = FFAT_OK;
		
		goto out;
	}

	if (r == _EINVALID_LOG_VER)
	{
		FFAT_LOG_PRINTF((_T("current log is created from another version of BTFS")));
		*pdwLatestSeqNo = pCurLHOrig->udwSeqNum;
		r = FFAT_OK;
		goto out;
	}

	if (r == _ENO_VALID_LOG)
	{
		FFAT_LOG_PRINTF((_T("There is no valid log at current log slot")));
		*pdwLatestSeqNo = pCurLHOrig->udwSeqNum;
		r = FFAT_OK;
		goto out;
	}

	if (pCurLHOrig->udwLogType & LM_LOG_FLAG_LLW)
	{
		pCurLog = (LogHeader*)(_LLW(pVol)->pBuff);
	}
	else
	{
		pCurLog = pCurLHOrig;
	}

	pNextLog->udwLogType = LM_LOG_NONE;

	for (wSlot = 0; /*none*/; /*none*/)			// wSlot will be increased at _getNextLog
	{
		r = _getNextLog(pVol, pCurLHOrig, &pCurLog,
							pNextLHOrig, &pNextLog, &wSlot, &bNextIsNewLLW, pCxt);
		IF_UK (r < 0)
		{
			if (r == FFAT_ENOENT)
			{
				// no more entry, stop
				break;
			}

			FFAT_LOG_PRINTF((_T("Fail to get next log or no more log entry")));
			goto out;
		}

		// If the sequence number of next slot is not continuous with
		// the sequence number of current slot, then current slot is the last slot
		dwCurrSeqNum = pCurLog->udwSeqNum;
		dwNextSeqNum = pNextLog->udwSeqNum;

		if ((dwNextSeqNum != (dwCurrSeqNum + 1)) &&
				(!((dwNextSeqNum == 0) && (dwCurrSeqNum == _MAX_SEQNUM))))
		{
			break;
		}

		r = _checkLogHeader(pNextLog);
		if ((r < 0) || (r == _EINVALID_LOG_VER))
		{
			// next log is an invalid log, current log is the last one
			break;
		}

		// if current log record is in dirty state, recover it
		if (pCurLog->uwFlag & LM_FLAG_DIRTY)
		{
			r = _logRecoverFinishedTransaction(pVol, pCurLog, pCxt);
			FFAT_EO(r, (_T("fail to recover from log")));
		}

		FFAT_ASSERT(r >= 0);

		// check this is the last log end
		if (wSlot == (LOG_MAX_SLOT - 1))
		{
			pCurLog = pNextLog;
			break;
		}
	}

	// Undo last transaction
	r = _logRecoverUnfinishedTransaction(pVol, pCurLog, pCxt);
	FFAT_EO(r, (_T("fail to recover from log")));

	if ((pCurLog->udwLogType == LM_LOG_NONE) && (wSlot == 1))
	{
		// there is no valid log 
		*pwValidSlots = 0;
	}
	else
	{
		*pwValidSlots = wSlot;
	}

	*pdwLatestSeqNo = pCurLog->udwSeqNum;
	// set new sequence number for new log

	FFAT_ASSERT(*pwValidSlots <= LOG_MAX_SLOT);

	r = FFAT_OK;

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pNextLHOrig, VOL_SS(pVol), pCxt);
	FFAT_LOCAL_FREE(pCurLHOrig, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * this function create log area (file) and
 * open log area(file) <== get information of log area
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_ERR1		: Invalid log file,
 *								there is a directory that has log file name
 * @return	else			: failed
 * @author
 * @version JAN-29-2008 [DongYoung Seo] user reserved area for log storage
 * @version JAN-29-2008 [DongYoung Seo] add checking routine for 
 *								directory that has log file name.
 * @version JAN-05-2009 [JeongWoo Park] Add check routine for the clusters of Root
 *								before lookup for log file
 * @version MAR-20-2009 [DongYoung Seo] unlink log file when log truncation failure
 * @version MAR-27-2009 [JeongWoo Park] Add check routine for log creation & recovery
 * @version JUN-10-2009 [GwangOk Go] check reserved region for logging before log file lookup
 */
static FFatErr
_openAndCreateLogArea(Vol* pVol, ComCxt* pCxt)
{
	Node*				pNodeLog = NULL;
	FFatConfig*			pConf;
	FFatErr				r;
	t_wchar*			psLogFileName;		// name of log
	LogInfo*			pLI;
											// flag for change size operation
	XDEInfo				stXDEInfo = {_LOG_FILE_UID, _LOG_FILE_GID, _LOG_FILE_PERM};

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	// no log write during log init
	pVol->dwFlag &= (~VOL_ADDON_LOGGING);

	psLogFileName = (t_wchar*)FFAT_LOCAL_ALLOC(FFAT_LOG_FILE_NAME_MAX_LENGTH * sizeof(t_wchar), pCxt);
	FFAT_ASSERT(psLogFileName);

	pConf = ffat_al_getConfig();

	FFAT_ASSERT(sizeof(pConf->stLog.psFileName) == (FFAT_LOG_FILE_NAME_MAX_LENGTH * sizeof(t_wchar)));

	FFAT_WCSCPY(psLogFileName, pConf->stLog.psFileName);

	// Before lookup the log file in ROOT, check the clusters of Root.
	//	If power-off was occurred in root expansion, the FAT chain of Root can be end with FREE.
	r = _checkRootClusters(pVol, pCxt);
	FFAT_EO(r, (_T("fail to check root clusters")));

	r = _checkLogCreation(pVol, pCxt);
	FFAT_EO(r, (_T("fail to check log creation & recovery")));

	// check reserved area
	r = _getReservedAreaForLog(pVol, pCxt);
	if ((r == FFAT_OK) || (r != FFAT_EINVALID))
	{
		// we found log area
		goto out;
	}

	FFAT_CPRINT_DEBUG(r < 0, (_T("there is not enough reserved region as log area")));

	pNodeLog = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNodeLog);

	ffat_node_resetNodeStruct(pNodeLog);

	// find the existing log file
	r = ffat_node_lookup(VOL_ROOT(pVol), pNodeLog, psLogFileName, 0,
						(FFAT_LOOKUP_FOR_CREATE | FFAT_LOOKUP_NO_LOCK), NULL, pCxt);
	if ((r != FFAT_OK) && (r != FFAT_ENOENT))
	{
		goto out;
	}

	NODE_ADDON_FLAG(pNodeLog) |= ADDON_NODE_LOG;

	// if it is not found, create log file
	if (r == FFAT_ENOENT)
	{
		// create log file
		r = ffat_node_create(VOL_ROOT(pVol), pNodeLog, psLogFileName,
				(FFAT_CREATE_ATTR_RO | FFAT_CREATE_ATTR_HIDDEN | FFAT_CREATE_ATTR_SYS | FFAT_CREATE_NO_LOCK),
				&stXDEInfo, pCxt);
		FFAT_EO(r, (_T("fail to create log file")));

		// truncate log file
		r = ffat_file_changeSize(pNodeLog, (LOG_SECTOR_COUNT << VOL_SSB(pVol)),
								FFAT_CHANGE_SIZE_NO_LOCK, 
								(FFAT_CACHE_DATA_LOG | FFAT_CACHE_META_IO | FFAT_CACHE_SYNC), pCxt);
		if (r != FFAT_OK)
		{
			// delete created log file
			ffat_node_unlink(VOL_ROOT(pVol), pNodeLog, 
							(NODE_UNLINK_NO_LOCK | NODE_UNLINK_NO_LOG | NODE_UNLINK_SYNC), pCxt);

			FFAT_DEBUG_PRINTF((_T("fail to change log file size")));
			goto out;
		}

		// get sector of log file
		r = _getSectorsForLogFile(pVol, pNodeLog, pCxt);
		FFAT_EO(r, (_T("fail to get sector for log file")));

		// init log file
		r = _initLogFile(pVol, pCxt);
		FFAT_EO(r, (_T("fail to init log file")));

		// sync after log file creation
		r = FFATFS_SyncVol(VOL_VI(pVol), FFAT_CACHE_SYNC, pCxt);
		FFAT_EO(r, (_T("fail to sync volume")));

		// erase the log creation info
		r  = _setLogCreatInfo(pVol, NULL, pCxt);
		FFAT_EO(r, (_T("erase the log creation info(truncate) is failed")));
	}
	else
	{
		// check is this a directory or is file size wrong
		if ((NODE_IS_DIR(pNodeLog) == FFAT_TRUE) ||
			(NODE_S(pNodeLog) != (t_uint32)(LOG_SECTOR_COUNT << VOL_SSB(pVol))))
		{
			FFAT_PRINT_DEBUG((_T("there is not enough reserved region as log area")));

			// there is not enough spare space for log
			// and there is a directory that has name for log file
			// or file size is wrong
			r = FFAT_ERR1;
			goto out;
		}

		FFAT_ASSERT(NODE_IS_VALID(pNodeLog) == FFAT_TRUE);
		FFAT_ASSERT(NODE_S(pNodeLog) == (t_uint32)(LOG_SECTOR_COUNT << VOL_SSB(pVol)));

		// get area for log
		r = _getSectorsForLogFile(pVol, pNodeLog, pCxt);
		FFAT_EO(r, (_T("fail to get sector for log file")));

		// check log sector
		r = _checkLogArea(pVol, pCxt);
		if (r == FFAT_EINVALID)
		{
			r = FFAT_ERR1;
			goto out;
		}

		FFAT_EO(r, (_T("wrong log file header is wrong")));
	}

	pLI = _LOG_INFO(pVol);
	pLI->dwFirstCluster = NODE_C(pNodeLog);

out:
	FFAT_LOCAL_FREE(pNodeLog, sizeof(Node), pCxt);
	FFAT_LOCAL_FREE(psLogFileName, FFAT_LOG_FILE_NAME_MAX_LENGTH * sizeof(t_wchar), pCxt);

	if (r != FFAT_OK)
	{
		FFAT_LOG_PRINTF((_T("There exists problems with log, disable it\n")));
		// sync volume to avoid partial update
		FFATFS_SyncVol(VOL_VI(pVol), FFAT_CACHE_SYNC, pCxt);
	}

	return r;
}

/**
* this function check the clusters of root,
* and recover un-finished FAT chain of ROOT.
* 
* @param	pVol			: [IN] pointer of volume
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
* @return	else			: failed
* @author	JeongWoo Park
* @version	JAN-05-2009 [JeongWoo Park] : first writing
* @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
*/
static FFatErr
_checkRootClusters(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;
	t_uint32		dwEOCCluster;
	t_uint32 		dwCurCluster;
	t_uint32		dwNextCluster;
	FatVolInfo*		pVI;

	pVI = VOL_VI(pVol);

	if (FFATFS_IS_FAT16(pVI) == FFAT_TRUE)
	{
		// FAT16 root cluster can not be expanded.
		return FFAT_OK;
	}

	FFAT_ASSERT(FFATFS_IS_FAT32(pVI) == FFAT_TRUE);

	dwEOCCluster = VI_RC(pVI);
	dwCurCluster = VI_RC(pVI);

	// Scan FAT chain
	while (1)
	{
		r = FFATFS_GetNextCluster(pVI, dwCurCluster, &dwNextCluster, pCxt);
		FFAT_EO(r, (_T("Failed to read FAT table!\n")));

		if (FFATFS_IS_VALID_CLUSTER(pVI, dwNextCluster) == FFAT_TRUE)
		{
			dwEOCCluster = dwCurCluster;
			dwCurCluster = dwNextCluster;
			continue;
		}
		else if (FFATFS_IS_EOF(VOL_VI(pVol), dwNextCluster) == FFAT_TRUE)
		{
			// Root has EOC. Just return.
			r = FFAT_OK;
			goto out;
		}
		else
		{
			if (dwNextCluster == 0)
			{
				// Power-off was occurred in ROOT expansion. mark EOC
				//	FAT chain : 2	-->	32		-->	34		-->	0
				//						[dwEOC]		dwCur		dwNext
				//				Cluster 34 is newly allocated for Root Dir EXPANSION
				r = FFATFS_UpdateCluster(pVI, dwEOCCluster, pVI->dwEOC,
								(FFAT_CACHE_META_IO | FFAT_CACHE_FORCE | FFAT_CACHE_SYNC),
								VOL_ROOT(pVol), pCxt);
				goto out;
			}
			else
			{
				// Wrong cluster chain.
				FFAT_LOG_PRINTF((_T("The Root cluster chain is corrupted.\n")));
				r = FFAT_EFAT;
				goto out;
			}
		}
	}

out:
	return r;
}

/**
* Initializes LLW
* @param		pLLW		: pointer of LLW
*
* @author		DongYoung Seo
* @version		JUL-23-2007 [DongYoung Seo] First Writing.
*/
static void
_initLLW(LogLazyWriter* pLLW)
{
	FFAT_ASSERT(pLLW);

	pLLW->pCurPtr		= pLLW->pBuff;
	pLLW->wLogSlot		= _LLW_SLOT_NOT_ASSIGNED;
	pLLW->wRecentLLSize	= 0;

#ifndef FFAT_DYNAMIC_ALLOC
	pLLW->pVol			= NULL;			// to distinguish volume
#endif

}


/**
* write log for LLW
* @param		pVol		: [IN] volume information
* @param		pLLW		: [IN] LLW pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_EIO	: I/O error
* @author		DongYoung Seo
* @version		JUL-23-2007 [DongYoung Seo] First Writing.
* @version		SEP-28-2008 [DongYoung Seo] update previous log flag after log write
* @version		MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
* @version		APR-27-2009 [JeongWoo Park] Add the initialize code for remained area of log
*/
static FFatErr
_writeLL(Vol* pVol, LogLazyWriter* pLLW, ComCxt* pCxt)
{
	t_int32			dwWriteCount;		// sector count for LLW
	t_int32			dwRestCount;		// rest sector count
	t_int32			dwCount;
	t_int32			dwIndex;
	t_int32			dwLLWSize;
	LogInfo*		pLI;
	t_int8*			pBuff;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLLW);

	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(pLI->wCurSlot == pLLW->wLogSlot);
	FFAT_ASSERT((pLLW->wLogSlot + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) <= LOG_MAX_SLOT);

	dwLLWSize = (t_int32)(pLLW->pCurPtr - pLLW->pBuff);

	// initialize the remained area of LLW
	if (dwLLWSize & VOL_SSM(pVol))
	{
		FFAT_MEMSET(pLLW->pCurPtr, 0x00, (VOL_SS(pVol) - (dwLLWSize & VOL_SSM(pVol))));
	}

	// ceiling up to remove partial sector write
	dwWriteCount = ESS_MATH_CDB(dwLLWSize, VOL_SS(pVol), VOL_SSB(pVol));

	FFAT_ASSERT(dwWriteCount <= (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol)));

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("Lazy Log write, dwIndex/dwWriteCount:%d/%d\n"), pLLW->wLogSlot, dwWriteCount));

	dwIndex = pLLW->wLogSlot;
	pBuff	= pLLW->pBuff;

#ifndef FFAT_BIG_ENDIAN
	FFAT_ASSERT(_checkLazyLog((LogHeader*)pBuff) == FFAT_OK);
#endif

	dwRestCount = dwWriteCount;

	do
	{
		// get continuous sector count
		for (dwCount = 1; dwCount < dwRestCount; dwCount++)
		{
			if (pLI->pdwSectors[dwIndex + dwCount] == pLI->pdwSectors[dwIndex + dwCount - 1])
			{
				continue;
			}
		}

		r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], dwCount, pBuff,
					(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC | FFAT_CACHE_DATA_LOG),
					FFAT_FALSE, pCxt);
		if (r != dwCount)
		{
			FFAT_LOG_PRINTF((_T("fail to write log to log file")));
			if (r >= 0)
			{
				return FFAT_EIO;
			}
			return r;
		}

		dwRestCount		-= dwCount;
		pBuff			+= (dwCount << VOL_SSB(pVol));
		dwIndex			+= dwCount;

	} while(dwRestCount	 > 0);

	// initialize it
	_initLLW(_LLW(pVol));

	FFAT_ASSERT(dwWriteCount > 0);

	_UPDATE_SLOT(pLI, (t_int16)dwWriteCount);
	FFAT_ASSERT(pLI->wCurSlot < LOG_MAX_SLOT);

	// set previous log flag
	pLI->uwPrevDirtyFlag	&= (~LM_FLAG_SYNC);		// remove sync flag
	pLI->uwPrevDirtyFlag	|= LM_FLAG_DIRTY;		// set dirty flag

	return FFAT_OK;
}


/**
* synchronize old logs and write new log for LLW
*
* @param		pVol		: current operational volume
*								may be NULL
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success
* @return		FFAT_EIO	: I/O error
* @author		DongYoung Seo
* @version		JUL-23-2007 [DongYoung Seo] First Writing.
* @version		JUL-03-2008 [DongYoung Seo] correct assert range check routine.(5min)
* @version		JAN-15-2009 [DongYoung Seo] change pLLW initialization sequence after chance validity of pVol
*/
static FFatErr
_syncLL(Vol* pVol, ComCxt* pCxt)
{
	LogLazyWriter*		pLLW;
	LogConfirm*			pLogConfirm;
	FFatErr				r;
	LogInfo*			pLI;

	if (pVol == NULL)
	{
		// there is no valid log
		return FFAT_OK;
	}

	pLLW = _LLW(pVol);
	FFAT_ASSERT(pLLW);

	if (pLLW->pVol == NULL)
	{
		// there is no valid log
		return FFAT_OK;
	}

	FFAT_ASSERT(pLLW->pCurPtr <= (pLLW->pBuff + LOG_LLW_CACHE_SIZE - sizeof(LogConfirm)));

	pLLW = _LLW(pVol);

	if (pLLW->pVol != pVol)
	{
		// nothing to do.
		return FFAT_OK;
	}

	if (pLLW->pBuff == pLLW->pCurPtr)
	{
		// there is no data
		return FFAT_OK;
	}

	pVol	= pLLW->pVol;
	pLI		= _LOG_INFO(pVol);

	// attach confirm data
	pLogConfirm		= (LogConfirm*)pLLW->pCurPtr;
	pLogConfirm->udwInvertedLogVer	= _LLW_CONFIRM;

	pLLW->pCurPtr	+= sizeof(LogConfirm);			// set the next pointer

	pLogConfirm->dwLogSize	= (t_int32)(pLLW->pCurPtr - pLLW->pBuff);		// set log size

	// check is there enough free space for LLW at log file
	FFAT_ASSERT((pLLW->wLogSlot + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) <= LOG_MAX_SLOT);

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("LLW SYNC, cur_slot/size:%d/%d\n"), _LLW(pVol)->wLogSlot, pLogConfirm->dwLogSize));

	// set byte order for lazy log
	_adjustByteOrder((LogHeader*)pLLW->pBuff, FFAT_TRUE);

	r = _writeLL(pVol, pLLW, pCxt);
	FFAT_EO(r, (_T("fail to write LL")));

	FFAT_ASSERT(pLI->wCurSlot < LOG_MAX_SLOT);

	r = FFAT_OK;
out:

	return r;
}


/**
* write log routine for LLW.
* This is not a real log write. just add to it.
*
* @param	pVol		: [IN] volume pointer
* @param	pLH			: [IN] log pointer
* @return	FFAT_OK		: success, or noting to do (this also success)
* @return	FFAT_DONE	: log writing is done
* @return	FFAT_EIO	: I/O error
* @author	DongYoung Seo
* @version	JUL-23-2007 [DongYoung Seo] First Writing.
* @version	JUN-03-2008 [DongYoung Seo] [bug fix] sync LLW when current log type is not LLW
*/
static FFatErr
_logWriteLLW(Vol* pVol, LogHeader* pLH, ComCxt* pCxt)
{
	FFatErr				r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);

	if ((VOL_FLAG(pVol) & VOL_ADDON_LLW) == 0)
	{
		return FFAT_OK;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		// Set LLW flag for FULL LLW MOUNT
		pLH->udwLogType |= LM_LOG_FLAG_LLW;
	}

	// This request is not LLW
	if ((pLH->udwLogType & LM_LOG_FLAG_LLW) == 0)
	{
		// check volume change and write
		r = _syncLL(pVol, pCxt);
		FFAT_ER(r, (_T("Fail to sync LLW")));

		return FFAT_OK;
	}

	// add new log to lazy log buffer
	r = _addToLL(pVol, pLH, pCxt);
	FFAT_ER(r, (_T("fail to add data to LLW")));

	return FFAT_DONE;
}


/**
* add a log to Lazy Log Buffer
*
* @param		pVol		: [IN] volume pointer
* @param		pLH			: [IN] log pointer
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success, noting to do
* @return		FFAT_DONE	: log written is done
* @return		FFAT_EIO	: I/O error
* @author		DongYoung Seo
* @version		JUL-23-2007 [DongYoung Seo] First Writing.
* @version		SEP-24-2008 [DongYoung Seo] Move new LLW update code after 
*											checking free log entry count
*/
static FFatErr
_addToLL(Vol* pVol, LogHeader* pLH, ComCxt* pCxt)
{
	LogInfo*			pLI;
	LogLazyWriter*		pLLW;
	t_uint16			dwSize;			// current log size
	FFatErr				r = FFAT_OK;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pLH);

	pLLW = _LLW(pVol);

	FFAT_ASSERT(pLH->udwLogType & LM_LOG_FLAG_LLW);
	FFAT_ASSERT((VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW) ? FFAT_TRUE : (pLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND)));

	pLI = _LOG_INFO(pVol);

	if (pLLW->pVol != pVol)
	{
		// volume is changed and log buffer is shared by several volume
		r = _syncLL(pLLW->pVol, pCxt);
		FFAT_EO(r, (_T("fail to sync LLW")));
	}

	// copy data to the buffer
	// check is there enough free buffer space for new log
	if ((pLLW->pCurPtr + (pLH->wUsedSize) + (sizeof(LogConfirm)))
				> (pLLW->pBuff + LOG_LLW_CACHE_SIZE))
	{
		r = _syncLL(pLLW->pVol, pCxt);
		FFAT_EO(r, (_T("fail to sync data for LLW")));
	}

	if (pLLW->wRecentLLSize == 0)
	{
		// this is the first log entry
		pLH->uwFlag |= LM_LOG_FLAG_LLW_BEGIN;
	}
	else if (pLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND))
	{
		// try to merge log, this is for write and extend(part of write on LINUX)
		r = _mergeLL(pVol, pLH, pCxt);
		if (r == FFAT_DONE)
		{
			// OK GOOD.
			r = FFAT_OK;
			goto out;
		}
	}

	FFAT_ASSERT(r == FFAT_OK);

	_UPDATE_LOG_SEQ(pLI);				// update log sequence number
	pLH->udwSeqNum	= pLI->udwSeqNum;	// [BUG FIX : 2008-11-25] use the seqNum after increase it.

	_PRINT_LOG(pVol, pLH, "Lazy Log");

	if (pLLW->wLogSlot == _LLW_SLOT_NOT_ASSIGNED)
	{
		FFAT_ASSERT(pLLW->wRecentLLSize == 0);		// this must be the first LL

    // bug fix_JW.Park_modify reset log's condition to resolve linux problem related appendwrite 
		if (((pLI->wCurSlot + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) > LOG_MAX_SLOT) ||
			(pLI->wCurSlot == 0))
		{
			// there is not enough space for LLW
			// sync current log
			r = _logReset(pVol, FFAT_CACHE_SYNC, pCxt);
			FFAT_EO(r, (_T("fail to reset log")));
		}

		pLLW->pVol		= pVol;
		pLLW->wLogSlot	= pLI->wCurSlot;

		FFAT_ASSERT((pLLW->wLogSlot + (LOG_LLW_CACHE_SIZE >> VOL_SSB(pVol))) <= LOG_MAX_SLOT);

		pLI->dwFlag		&= (~LI_FLAG_NO_LOG);
	}

	dwSize = pLH->wUsedSize;

	FFAT_MEMCPY(pLLW->pCurPtr, pLH, dwSize);	// copy new log to LL buffer
	pLLW->pCurPtr		+= dwSize;
	pLLW->wRecentLLSize	= dwSize;				// set recent Lazy Log Size for remove fail operation from LOG

	FFAT_ASSERT(pLLW->pCurPtr <= (pLLW->pBuff + LOG_LLW_CACHE_SIZE));

	FFAT_ASSERT((pLLW->wLogSlot >= 0) && (pLLW->wLogSlot < LOG_MAX_SLOT));

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("LazyLog ADD, slot/size/TotalSize:%d/%d/%d\n"),pLLW->wLogSlot, dwSize ,(pLLW->pCurPtr - pLLW->pBuff)));

	r = FFAT_OK;

out:
	return r;
}


/**
* check can a new lazy log be merged or not
*
* this function make a decision only with start cluster number
*	do not use short file name or others.
*	the start cluster number is the ultimate clue.
*
* merge condition.
*	1. same node
*
* @param		pVol		: [IN] volume pointer
* @param		pCurLH		: [IN] current log
* @param		pPrevLH		: [IN] previous log
*								byte order is already adjusted.
*								except FFatVC area (cluster chain)
* @param		ppNewDECur	: [OUT] storage pointer of NewDE on current log
* @param		ppNewDEPrev	: [OUT] storage pointer of NewDE on previous log
* @param		pSLFatCur	: [OUT] storage pointer of SubLogFat on current log
* @param		pSLFatPrev	: [OUT] storage pointer of SubLogFat on previous log
* @return		FFAT_TRUE	: can be merged
* @return		FFAT_FALSE	: can no be merged
* @author		DongYoung Seo
* @version		AUG-18-2008 [DongYoung Seo] First Writing
* @version		SEP-12-2008 [DongYoung Seo] add check routine for no previous cluster
* @version		03-DEC-2008 [DongYoung Seo] remove assert for open unlink checking
*/
static t_boolean
_canLLbeMerged(Vol* pVol, LogHeader* pCurLH, LogHeader* pPrevLH,
				FatDeSFN** ppNewDECur, FatDeSFN** ppNewDEPrev,
				SubLogFat** ppSLFatCur, SubLogFat** ppSLFatPrev)
{
	t_uint32		dwClusterCur;		// cluster number at DE of current log
	t_uint32		dwClusterPrev;		// cluster number at DE of previous log
	FatDeSFN*		pNewDECur;			// new DE for current log
	FatDeSFN*		pNewDEPrev;			// new DE for previous log
	SubLogFat*		pSLFatCur;			// SubLogFat for current node
	SubLogFat*		pSLFatPrev;			// SubLogFat for previous node
	FFatVCE*		pVCEPrev;			// VCE pointer of previous log
	t_uint32		dwLCPrev;			// the last cluster of previous log
	const t_int32	dwMaxEntryCount = (t_int32)((VOL_SS(pVol)
											- sizeof(LogHeader)
											- sizeof(SubLogHeader)
											- sizeof(SubLogUpdateDe)
											- sizeof(SubLogHeader)
											- sizeof(SubLogFat)
											- sizeof(LogTail))
											/ sizeof(FFatVCE));
										// the max count of VCE
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCurLH);
	FFAT_ASSERT(pPrevLH);
	FFAT_ASSERT(ppNewDECur);
	FFAT_ASSERT(ppNewDEPrev);
	FFAT_ASSERT(ppSLFatCur);
	FFAT_ASSERT(ppSLFatPrev);
	FFAT_ASSERT(pCurLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND));		// only for write

	// check log type is same
	if (pCurLH->udwLogType != pPrevLH->udwLogType)
	{
		// log type is not same. can not merge
		return FFAT_FALSE;
	}

	// get new DE
	pNewDECur	= _sublogGetNewDE(pCurLH);
	pNewDEPrev	= _sublogGetNewDE(pPrevLH);

	if ((pNewDECur == NULL) || (pNewDEPrev == NULL))
	{
		// New or previous is log for open unlinked node
		FFAT_PRINT_VERBOSE((_T("can not be merged because this or previous is open unlinked node ")));
		return FFAT_FALSE;
	}

	dwClusterCur	= FFATFS_GetDeCluster(VOL_VI(pVol), pNewDECur);
	dwClusterPrev	= FFATFS_GetDeCluster(VOL_VI(pVol), pNewDEPrev);

	// Check the first cluster is same or not
	if (dwClusterCur != dwClusterPrev)
	{
		return FFAT_FALSE;
	}

	pSLFatCur	= _sublogGetSubLogFat(pCurLH);
	pSLFatPrev	= _sublogGetSubLogFat(pPrevLH);

	FFAT_ASSERT(pSLFatCur);		// never be NULL, this is lazy log
	FFAT_ASSERT(pSLFatPrev);	// never be NULL, this is lazy log

	if ((pSLFatPrev->dwValidEntryCount + pSLFatCur->dwValidEntryCount) > dwMaxEntryCount)
	{
		// there is too many VCE, let's cut here.
		return FFAT_FALSE;
	}

	if (pSLFatPrev->dwValidEntryCount > 0)
	{
		// get VCE of previous log
		pVCEPrev	= (FFatVCE*)((t_int8*)pSLFatPrev + sizeof(SubLogFat));

		// get last cluster of previous log
		dwLCPrev	= pVCEPrev[pSLFatPrev->dwValidEntryCount - 1].dwCluster
							+ pVCEPrev[pSLFatPrev->dwValidEntryCount - 1].dwCount - 1;

		if ((pSLFatCur->udwPrevEOF != 0) && (dwLCPrev != pSLFatCur->udwPrevEOF))
		{
			FFAT_ASSERTP(0, (_T("Never reach here")));
			return FFAT_FALSE;
		}
	}

	*ppNewDECur		= pNewDECur;
	*ppNewDEPrev	= pNewDEPrev;
	*ppSLFatCur		= pSLFatCur;
	*ppSLFatPrev	= pSLFatPrev;

	return FFAT_TRUE;
}


/**
* merge a lazy log with previous log.
* cluster allocation log can be merged with previous log.
*	1. write log	: merge cluster allocation log
*	2. extend log	: merge cluster allocation log
* @param		pVol		: [IN] volume pointer
* @param		pCurLH		: [IN] current log
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: can not merge.
* @return		FFAT_DONE	: merge success.
* @author		DongYoung Seo
* @version		AUG-14-2008 [DongYoung Seo] First Writing.
* @version		SEP-28-2008 [DongYoung Seo] add previous log type checking routine
*										log can not be merged when previous log is not write or extend
* @version		DEC-29-2008 [DongYoung Seo] change calculation routine for stVCPrev.dwTotalEntryCount 
*										the devider must not be sizeof(FFatVC) but sizeof(FFatVCE).
* @version		SEP-03-2009 [JW Park] Bug fix about the following after merge
*						1) pSLFatPrev->udwFirstCluster : If previous is 0, it must be updated with new
*						2) pPrevLH->uwFlag : previous flag and new flag must be ORing.
*/
static FFatErr
_mergeLL(Vol* pVol, LogHeader* pCurLH, ComCxt* pCxt)
{
	LogHeader*		pPrevLH;
	LogLazyWriter*	pLLW;
	SubLogFat*		pSLFatCur;	// SubLogFat pointer for current log
	SubLogFat*		pSLFatPrev;	// SubLogFat pointer for previous log
	FFatVC			stVCCur;	// vectored Cluster for current log
	FFatVC			stVCPrev;	// vectored Cluster for previous log
	FatDeSFN*		pNewDECur;	// new DE for current log
	FatDeSFN*		pNewDEPrev;	// new DE for previous log
	t_int32			dwVECPrev;	// previous valid entry count on previous log
	t_int16			wSizeInc;	// increased size

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCurLH);
	FFAT_ASSERT(pCxt);
	FFAT_ASSERT(pCurLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND));		// only for write

	pLLW = _LLW(_pVol);

	FFAT_ASSERT(pLLW);
	FFAT_ASSERT(pLLW->pVol == pVol);
	FFAT_ASSERT(pLLW->wRecentLLSize > 0);

	// get previous log pointer
	pPrevLH = (LogHeader*)(pLLW->pCurPtr - pLLW->wRecentLLSize);

	// adjust byte order
	FFAT_ASSERT(pPrevLH->udwLogVer == _LOG_VERSION);

	if (!(pPrevLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND)))
	{
		return FFAT_OK;
	}

	FFAT_ASSERT(pPrevLH->udwLogType & (LM_LOG_WRITE | LM_LOG_EXTEND));

	// check cluster allocation information can be connected
	if (_canLLbeMerged(pVol, pCurLH, pPrevLH, &pNewDECur,
				&pNewDEPrev, &pSLFatCur, &pSLFatPrev) == FFAT_FALSE)
	{
		FFAT_DEBUG_LOG_PRINTF(pVol, (_T("current LL can not be merged \n")));
		return FFAT_OK;
	}

	// check SubLogFat is the last log or not
	FFAT_ASSERT(((SubLog*)((t_int8*)pSLFatCur - sizeof(SubLogHeader)))->stSubLogHeader.uwNextSLFlag == LM_SUBLOG_FLAG_LAST);

	// ok. let's merge them
	VC_INIT(&stVCCur, VC_NO_OFFSET);
	VC_INIT(&stVCPrev, VC_NO_OFFSET);

	stVCPrev.dwTotalClusterCount	= pSLFatPrev->dwCount;
	stVCPrev.dwValidEntryCount		= pSLFatPrev->dwValidEntryCount;
	stVCPrev.dwTotalEntryCount		= LOG_LLW_CACHE_SIZE / sizeof(FFatVCE);
	stVCPrev.pVCE					= (FFatVCE*)((t_int8*)pSLFatPrev + sizeof(SubLogFat));

	stVCCur.dwTotalClusterCount		= pSLFatCur->dwCount;
	stVCCur.dwValidEntryCount		= pSLFatCur->dwValidEntryCount;
	stVCCur.dwTotalEntryCount		= pSLFatCur->dwValidEntryCount;
	stVCCur.pVCE					= (FFatVCE*)((t_int8*)pSLFatCur + sizeof(SubLogFat));

	// backup previous VEC
	dwVECPrev = VC_VEC(&stVCPrev);

	// let's merge last VCE on the previous log
	ffat_com_mergeVC(&stVCPrev, &stVCCur);

// debug begin
#ifdef FFAT_DEBUG
	{
		t_int32		dwMaxEntryCount;

		dwMaxEntryCount = (t_int32)((VOL_SS(pVol)
								- sizeof(LogHeader) - sizeof(SubLogHeader)
								- sizeof(SubLogUpdateDe) - sizeof(SubLogHeader)
								- sizeof(SubLogFat) - sizeof(LogTail))
								/ sizeof(FFatVCE));
											// the max count of VCE
		FFAT_ASSERT(VC_VEC(&stVCPrev) <= dwMaxEntryCount);
	}
#endif
// debug end

	// UPDATE DIRECTORY ENTRY
	FFAT_MEMCPY(pNewDEPrev, pNewDECur, sizeof(FatDeSFN));

	// update cluster count on previous log
	pSLFatPrev->dwCount				= VC_CC(&stVCPrev);
	pSLFatPrev->dwValidEntryCount	= VC_VEC(&stVCPrev);
	
	if ((pSLFatPrev->udwFirstCluster == 0) &&
		(VC_CC(&stVCPrev) > 0))
	{
		pSLFatPrev->udwFirstCluster = VC_FC(&stVCPrev);
	}

	FFAT_ASSERT(pSLFatPrev->dwValidEntryCount >= dwVECPrev);

	wSizeInc = (t_int16)((pSLFatPrev->dwValidEntryCount - dwVECPrev) * sizeof(FFatVCE));

	// update used size & flag
	pPrevLH->wUsedSize = pPrevLH->wUsedSize + wSizeInc;
	pPrevLH->uwFlag	|= pCurLH->uwFlag;

	FFAT_ASSERT(pCurLH->wUsedSize < LOG_LLW_CACHE_SIZE);

	pLLW->pCurPtr		+= wSizeInc;
	pLLW->wRecentLLSize	= pLLW->wRecentLLSize + wSizeInc;	// set recent Lazy Log Size for remove fail operation from LOG

	FFAT_ASSERT(pLLW->pCurPtr <= (pLLW->pBuff + LOG_LLW_CACHE_SIZE));

	FFAT_ASSERT(_checkLogHeader(pCurLH) == FFAT_OK);
	FFAT_ASSERT(_checkLogHeader((LogHeader*)(pLLW->pCurPtr - pLLW->wRecentLLSize)) == FFAT_OK);

	_PRINT_LOG(pVol, pCurLH, "Log Merged");

	FFAT_DEBUG_LOG_PRINTF(pVol, (_T("current LL is merged with previous one \n")));

	return FFAT_DONE;
}


/**
* a small helper the for log operation
* @param		pVol		: [IN] volume pointer
* @param		ppLH		: [OUT] log pointer, it will has sector size buffer
* @param		udwLogtype	: [IN] type of log
* @param		dwCacheFlag	: [IN] flag for cache operation.
* @param		pCxt		: [IN] context of current operation
* @return		FFAT_OK		: success, noting to do
* @return		FFAT_ENOEME	: not enough memory
* @author		DongYoung Seo
* @version		AUG-02-2007 [DongYoung Seo] First Writing.
* @version		APR-01-2008 [DongYoung Seo] rename _logHelper->_allocAndInitLogHeader
*/
static FFatErr
_allocAndInitLogHeader(Vol* pVol, LogHeader** ppLH, LogType udwLogType,
						FFatCacheFlag dwCacheFlag, ComCxt* pCxt)
{
	// allocate memory for log record
	*ppLH = (LogHeader*) FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(*ppLH);

	FFAT_MEMSET(*ppLH, 0x00, sizeof(LogHeader));

	// initialize and set variables
	(*ppLH)->udwLogVer	= _LOG_VERSION;
	(*ppLH)->udwLogType	= udwLogType;
	(*ppLH)->wUsedSize	= sizeof(LogHeader);
	(*ppLH)->uwFlag		= (dwCacheFlag & FFAT_CACHE_SYNC) ? LM_FLAG_SYNC : LM_FLAG_NONE;

	return FFAT_OK;
}


/**
 * log - extend file size
 *
 * @param		pNode			: [IN] target node pointer
 * @param		pVC				: [IN] vectored cluster information
 * @param		dwSize			: [IN] New node size
 * @param		dwEOF			: [IN] previous EOF
 * @param		pVC				: [IN] vectored cluster to be allocated
 * @param		dwCSFlag		: [IN] change size flag
 * @param		pdwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt			: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] get VC from CORE module
 * @version		OCT-21-2009 [JW Park] add the consideration about dirty-size state of node
 */
static FFatErr
_logExtendFileSize(Node* pNode, t_uint32 dwSize, t_uint32 dwEOF, FFatVC* pVC,
					FFatChangeSizeFlag dwCSFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;
	LogType				dwLogType;
	FatAllocate			stAlloc;
	FatDeSFN			stDE_Backup;		// backup directory entry
	t_uint32			dwOrigCluster;		// back original cluster
	FatDeUpdateFlag		dwDeUpdateFlag;

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pNode->dwSize < dwSize);
	FFAT_ASSERT(NODE_IS_FILE(pNode) == FFAT_TRUE);

	// create를 위한 change size operatoin은 항상 크기가 0으로 된다. 그러므로 이 함수에는 오지 않는다.
	FFAT_ASSERT((dwCSFlag & FFAT_CHANGE_SIZE_FOR_CREATE) == 0);

	pVol = NODE_VOL(pNode);

	// is log enabled ?
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	FFAT_MEMCPY(&stDE_Backup, &pNode->stDE, sizeof(FatDeSFN));
	dwOrigCluster = NODE_C(pNode);

	IF_UK (VC_IS_FULL(pVC) == FFAT_TRUE)
	{
		// there are not all cluster info
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// update alloc information
	stAlloc.dwCount			= VC_CC(pVC);
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= dwEOF;
	if (VC_CC(pVC) > 0)
	{
		stAlloc.dwFirstCluster = VC_FC(pVC);
	}
	else
	{
		stAlloc.dwFirstCluster = 0;
	}
	stAlloc.dwLastCluster	= 0;
	stAlloc.pVC				= pVC;

	dwLogType = LM_LOG_EXTEND;

	if (NODE_C(pNode) == 0)
	{
		NODE_C(pNode) = VC_FC(pVC);
	}

	dwDeUpdateFlag = FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_MTIME
					| FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_CLUSTER;

	// If normal expand, remove the dirty-size state
	if ((dwCSFlag & FFAT_CHANGE_SIZE_RECORD_DIRTY_SIZE) == 0)
	{
		// If normal expand, then update the size and remove the dirty-size state
		dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
	}

	// update child node without DE update
	r = ffat_node_updateSFNE(pNode, dwSize, 0, pNode->dwCluster, dwDeUpdateFlag,
							FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to update DE")));

	if (dwCSFlag & FFAT_CHANGE_SIZE_LAZY_SYNC)
	{
		// set LLW Flag
		dwLogType |= LM_LOG_FLAG_LLW;
	}

	// write log
	r = _logTruncate(pVol, pNode, &stDE_Backup, &stAlloc, pdwCacheFlag, dwLogType, pCxt);
	FFAT_EO(r, (_T("fail to write truncation log")));

out:
	// restore SFNE
	FFAT_MEMCPY(&pNode->stDE, &stDE_Backup, sizeof(FatDeSFN));
	pNode->dwCluster = dwOrigCluster;

	return r;
}


/**
 * log - file의 크기를 감소 시킨다.(shrink file size)
 *
 * @param		pNode		: [IN] target node pointer
 * @param		dwSize		: [IN] New node size
 * @param		dwEOF		: [IN] new EOF
 * @param		pVC			: [IN] vectored cluster information to be deallocated
 * @param		dwCSFlag	: [IN] flag for change size
 * @param		dwCacheFlag	: [OUT] flag for cache operation
 * @param		pCxt		: [IN] context of current operation
 * @author		DongYoung Seo
 * @version		AUG-31-2006 [DongYoung Seo] First Writing.
 * @version		NOV-24-2008 [GwangOk Go] add dwEOF
 * @version		OCT-21-2009 [JW Park] add the consideration about dirty-size state of node
 */
static FFatErr
_logShrinkFileSize(Node* pNodeOrig, t_uint32 dwSize, t_uint32 dwEOF, FFatVC* pVC,
					FFatChangeSizeFlag dwCSFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr				r;
	Vol*				pVol;

	Node*				pNode = NULL;	// we can not update real node. so use a temporary node
										// for short file name entry
	LogType				dwLogType;
	FatAllocate			stAlloc;
	FatDeUpdateFlag		dwDeUpdateFlag;

	FFAT_ASSERT(pNodeOrig);
	FFAT_ASSERT((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) ? (pNodeOrig->dwSize == dwSize) : (pNodeOrig->dwSize > dwSize));
	FFAT_ASSERT(NODE_IS_FILE(pNodeOrig) == FFAT_TRUE);
	FFAT_ASSERT(pNodeOrig->dwSize >= 0);

	pVol = NODE_VOL(pNodeOrig);

	// is log enabled ?
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pNodeOrig->dwCluster) == FFAT_TRUE);

	if ((dwSize == pNodeOrig->dwSize) &&
		((dwCSFlag & FFAT_CHANGE_SIZE_RECOVERY_DIRTY_SIZE) == 0))
	{
		return FFAT_OK;
	}

	pNode = (Node*)FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNode);

	// copy original node data to temporary node
	FFAT_MEMCPY(pNode, pNodeOrig, sizeof(Node));

	// update deallocate information for log
	stAlloc.dwHintCluster	= 0;
	stAlloc.dwPrevEOF		= dwEOF;
	if (VC_CC(pVC) > 0)
	{
		stAlloc.dwFirstCluster = VC_FC(pVC);
	}
	else
	{
		stAlloc.dwFirstCluster = 0;
	}
	stAlloc.pVC				= pVC;

	// check pVC has all cluster information
	if (VC_TEC(pVC) == VC_VEC(pVC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
		stAlloc.dwCount			= 1;				// we have count 
		stAlloc.dwLastCluster	= 0;		// maybe not use it -> delete last cluster?
	}
	else
	{
		stAlloc.dwCount			= VC_CC(pVC);	// we have count 
		stAlloc.dwLastCluster	= VC_LC(pVC);	// we do not have it	// maybe not use it -> delete last cluster?
	}

	// when it can not store all of the cluster data to stVC
	dwLogType = LM_LOG_SHRINK;

	if (0 == dwSize)
	{
		pNode->dwCluster = 0;
	}

	dwDeUpdateFlag = FAT_UPDATE_DE_ATIME | FAT_UPDATE_DE_MTIME
					| FAT_UPDATE_DE_SIZE | FAT_UPDATE_DE_CLUSTER;

	// If dwSize is smaller than the original size before dirty which is recorded in DE,
	// remove the dirty-size state
	if ((NODE_IS_DIRTY_SIZE(pNode) == FFAT_TRUE) &&
		(dwSize <= FFATFS_GetDeSize(NODE_DE(pNode))))
	{
		dwDeUpdateFlag |= FAT_UPDATE_REMOVE_DIRTY;
	}

	// update child node
	r = ffat_node_updateSFNE(pNode, dwSize, 0, pNode->dwCluster, dwDeUpdateFlag,
							FFAT_CACHE_NONE, pCxt);
	FFAT_EO(r, (_T("fail to update SFNE")));

	// write log
	r = _logTruncate(pVol, pNode, &pNodeOrig->stDE, &stAlloc, pdwCacheFlag, dwLogType, pCxt);
	FFAT_EO(r, (_T("fail to write truncation log")));

	r = FFAT_OK;

out:
	FFAT_LOCAL_FREE(pNode, sizeof(Node), pCxt);

	return r;
}


/**
 * writes truncate log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pstOldSFN		: [IN] old SFN DE before truncate
 * @param	pAlloc			: [IN] Allocate/Deallocate FAT chain information
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	udwLogType		: [IN] LM_LOG_EXTEND or LM_LOG_SHRINK
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_NOMEM		: not enough memory to write log
 * @return	else			: log write failed
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenAllocateFat
 */
static FFatErr
_logTruncate(Vol* pVol, Node* pNode, FatDeSFN* pstOldSFN, FatAllocate* pAlloc,
					FFatCacheFlag* pdwCacheFlag, LogType udwLogType, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;			// sub log pointer
	t_uint32		dwCluster;
	t_int16			wReservedSize;			// reserved size for next logs

	// if log is disabled, return immediately
	if (_IS_LOGGING_ENABLED(pVol) == FFAT_FALSE)
	{
		return FFAT_OK;
	}

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(((udwLogType & (~LM_LOG_FLAG_MASK)) == LM_LOG_EXTEND) || ((udwLogType & (~LM_LOG_FLAG_MASK)) == LM_LOG_SHRINK));

	r = _allocAndInitLogHeader(pVol, &pLH, udwLogType, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	wReservedSize = _RSVD_SIZE_UPDATE_DE + _RSVD_SIZE_LOG_TAIL;
	if (udwLogType & LM_LOG_EXTEND)
	{
		wReservedSize += _RSVD_SIZE_ALLOCATE_FAT;
	}
	else
	{
		wReservedSize += _RSVD_SIZE_DEALLOCATE_FAT;
	}

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize	-= _RSVD_SIZE_UPDATE_DE;
	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE)
	{
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeEndOffset, pNode->stDeInfo.dwDeEndCluster, pCxt) == FFAT_OK);
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeOffsetSFNE, pNode->stDeInfo.dwDeClusterSFNE, pCxt) == FFAT_OK);

		// update DE
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenUpdateDE(pLH, pNode, pstOldSFN, NULL);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	// (de)allocate FAT,
	// do not change sub log generation sequence for log merge !!!
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	if (udwLogType & LM_LOG_EXTEND)
	{
		FFAT_ASSERT((udwLogType & (~LM_LOG_FLAG_MASK)) == LM_LOG_EXTEND);

		wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;

		dwCluster = FFATFS_GetDeCluster(VOL_VI(pVol), pstOldSFN); //dwCluster may be 0
		_sublogGenAllocateFat(pVol, pNode, pLH, dwCluster, pAlloc, wReservedSize);
	}
	else if (udwLogType & LM_LOG_SHRINK)
	{
		FFAT_ASSERT((udwLogType & (~LM_LOG_FLAG_MASK)) == LM_LOG_SHRINK);
		FFAT_ASSERT((udwLogType & LM_LOG_FLAG_LLW) == 0);		// LLW is only for expand operation

		wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;

		dwCluster = pNode->dwCluster;  //dwCluster may be 0
		_sublogGenDeallocateFat(pVol, pNode, pLH, dwCluster, pAlloc, wReservedSize);
	}

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	if (r < 0)
	{
		goto out;
	}

	if ((VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW) ||
		(udwLogType & LM_LOG_FLAG_LLW))
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * create extended attribute operation log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pAlloc			: [IN] Allocate FAT chain information
 * @param	pdwCacheFlag	: [IN/OUT] flags for cache operation
 *								FFAT_CACHE_SYNC_CALLBACK will be added for LLW
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author	InHwan Choi
 * @version	NOV-26-2007 [InHwan Choi] First Writing
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	DEC-22-2008 [JeongWoo Park] support ROOT EA
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenAllocateFat
 */
static FFatErr
_logCreateEA(Vol* pVol, Node* pNode, FatAllocate* pAlloc,
			FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt)
{	
	FFatErr			r;
	LogHeader*		pLH			= NULL;		// pointer for log
	SubLog*			pSL;
	FatDeSFN		stDeOrig;				// storage for old SFNE
	t_int16			wReservedSize;			// reserved size for next logs

	FFAT_ASSERT(pVol);

	// if log is disabled, return immediately
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT(pdwCacheFlag);

	// allocate log header
	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_EA_CREATE, FFAT_CACHE_SYNC, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	wReservedSize = _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	if (NODE_IS_ROOT(pNode) == FFAT_FALSE)
	{
		if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE)
		{
			// store original SFNE
			FFAT_MEMCPY(&stDeOrig, &pNode->stDE, sizeof(FatDeSFN));

			// pNode SFN의 CrTime과 CrDate에 cluster번호를 적는다.
			pNode->stDE.wCrtDate = (t_uint16)(pAlloc->dwFirstCluster >> 16);
			pNode->stDE.wCrtTime = (t_uint16)(pAlloc->dwFirstCluster & 0xFFFF);

			// pNode SFN의 CrTime_tenth 에 EA flag를 적는다.
			pNode->stDE.bNTRes |= ADDON_SFNE_MARK_XATTR;

			// update DE
			_sublogGenUpdateDE(pLH, pNode, &stDeOrig, NULL);

			// restore original SFNE
			FFAT_MEMCPY(&pNode->stDE, &stDeOrig, sizeof(FatDeSFN));
		}
	}
	else
	{
		// ROOT
		_sublogGenUpdateRootEA(pVol, pLH, 0, pAlloc->dwFirstCluster);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;
	// At creating EA, there is no starting cluster.
	_sublogGenAllocateFat(pVol, pNode, pLH, 0, pAlloc, wReservedSize);

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	if (r < 0)
	{
		goto out;
	}

	_SET_CALLBACK(pVol, *pdwCacheFlag)

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * expand extended attribute operation log
 * 
 * @param	pVol 			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pOldAlloc		: [IN] Deallocate FAT chain information
 * @param	pNewAlloc		: [IN] Allocate FAT chain information
 * @param	pdwCacheFlag	: [IN/OUT] flags for cache operation
 *								FFAT_CACHE_SYNC_CALLBACK will be added for LLW
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author	GwangOk Go
 * @version	JUN-23-2008 [GwangOk Go] First Writing
 * @version	DEC-05-2008 [JeongWoo Park] edit the sequence of log. DE log must be ahead of FAT log.
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 */
static FFatErr
_logCompactEA(Vol* pVol, Node* pNode, FatAllocate* pOldAlloc, FatAllocate* pNewAlloc,
			FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH			= NULL;		// pointer for log
	SubLog*			pSL;
	FatDeSFN		stDeOrig;
	t_int16			wReservedSize;			// reserved size for next logs

	FFAT_ASSERT(pVol);

	// if log is disabled, return immediately
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pOldAlloc);
	FFAT_ASSERT(pNewAlloc);
	FFAT_ASSERT(pdwCacheFlag);
	FFAT_ASSERT(pCxt);

	// allocate log header
	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_EA_COMPACTION, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	// update DE (record the DE log at first : FAT log can make the out-of-range of a sector)
	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	wReservedSize = _RSVD_SIZE_DEALLOCATE_FAT + _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	if (NODE_IS_ROOT(pNode) == FFAT_FALSE)
	{
		if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE)
		{
			// store original SFNE
			FFAT_MEMCPY(&stDeOrig, &pNode->stDE, sizeof(FatDeSFN));

			FFAT_ASSERT(pNode->stDE.bNTRes & ADDON_SFNE_MARK_XATTR);

			// pNode SFN의 CrTime과 CrDate에 cluster번호를 적는다.
			pNode->stDE.wCrtDate = (t_uint16)(pNewAlloc->dwFirstCluster >> 16);
			pNode->stDE.wCrtTime = (t_uint16)(pNewAlloc->dwFirstCluster & 0xFFFF);

			// update directory entry (only for Mtime, Atime)
			r = ffat_node_updateSFNE(pNode, pNode->dwSize, 0, 0,
									(FAT_UPDATE_DE_MTIME | FAT_UPDATE_DE_ATIME),
									FFAT_CACHE_NONE, pCxt);
			FFAT_EO(r, (_T("fail to update SFNE")));

			// update DE
			_sublogGenUpdateDE(pLH, pNode, &stDeOrig, NULL);

			// restore original SFNE
			FFAT_MEMCPY(&pNode->stDE, &stDeOrig, sizeof(FatDeSFN));
		}
	}
	else
	{
		// ROOT
		_sublogGenUpdateRootEA(pVol, pLH, pOldAlloc->dwFirstCluster, pNewAlloc->dwFirstCluster);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// deallocate cluster
	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	_sublogGenDeallocateFat(pVol, pNode, pLH, 0, pOldAlloc, wReservedSize);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;
	// allocate cluster
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	_sublogGenAllocateFat(pVol, pNode, pLH, 0, pNewAlloc, wReservedSize);

	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	if (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * writes file system write operation log
 * 
 * @param	pVol 			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pstOldSFN 		: [IN] old SFN DE before write
 * @param	pAlloc 			: [IN] Allocate FAT chain information
 * @param	pdwCacheFlag	: [IN/OUT] flags for cache operation
 *								FFAT_CACHE_SYNC_CALLBACK will be added for LLW
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	else				: failed
 * @author
 * @version	JUL-25-2007 [DongYoung Seo] Add pdwCacheFlag for LLW
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenAllocateFat
 */
FFatErr
_logFileWrite(Vol* pVol, Node* pNode, FatDeSFN* pstOldSFN, FatAllocate* pAlloc,
				FFatCacheFlag *pdwCacheFlag, ComCxt* pCxt)
{	
	FFatErr			r;
	LogHeader*		pLH		= NULL;			// pointer for log
	SubLog*			pSL;					// sub log pointer
	t_uint32		dwCluster;
	t_int16			wReservedSize;			// reserved size for next logs

	FFAT_ASSERT(pVol);

	// if log is disabled, return immediately
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pstOldSFN);
	FFAT_ASSERT(pAlloc);
	FFAT_ASSERT(pdwCacheFlag);

	r = _allocAndInitLogHeader(pVol, &pLH, (LM_LOG_WRITE | LM_LOG_FLAG_LLW),
					*pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	wReservedSize = _RSVD_SIZE_UPDATE_DE + _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_UPDATE_DE;
	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE)
	{
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeEndOffset, pNode->stDeInfo.dwDeEndCluster, pCxt) == FFAT_OK);
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeOffsetSFNE, pNode->stDeInfo.dwDeClusterSFNE, pCxt) == FFAT_OK);

		// update DE
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenUpdateDE(pLH, pNode, pstOldSFN, NULL);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	// do not change sub log generation sequence.
	// the last log must be SubLogFat for log merge
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	wReservedSize -= _RSVD_SIZE_ALLOCATE_FAT;
	dwCluster = FFATFS_GetDeCluster(VOL_VI(pVol), pstOldSFN);				//dwCluster may be 0
	_sublogGenAllocateFat(pVol, pNode, pLH, dwCluster, pAlloc, wReservedSize);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & (VOL_ADDON_LLW | VOL_ADDON_FULL_LLW))
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag)
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * writes unlink log
 * 
 * @param	pVol 			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pDealloc		: [IN] deallocate FAT chain information
 * @param	pDeallocEA		: [IN] deallocate FAT chain information for extended attribute
 * @param	dwNUFlag		: [IN] open unlink flag
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	pCxt		: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_NOMEM		: not enough memory to write log
 * @return	else			: log write failed
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	MAR-30-2009 [DongYoung Seo] change open unlink checking code from FFAT_IS_OPEN_UNLINK() 
 *										to using dwNUFlag. node open flag is added at the end of unlink operation
 * @version	MAR-30-2009 [JeongWoo Park] Add the code to record the log type with OPEN_UNLINK 
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenDeallocateFat
 */
static FFatErr
_logUnlink(Vol* pVol, Node* pNode, FatDeallocate* pDealloc, FatDeallocate* pDeallocEA,
			NodeUnlinkFlag dwNUFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;			// pointer for sub log
	t_int16			wReservedSize;	// reserved size for next logs

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);
	FFAT_ASSERT(pDealloc);
	FFAT_ASSERT(pDeallocEA);

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);
	FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pNode) == FFAT_FALSE);

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_UNLINK, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	if (dwNUFlag & NODE_UNLINK_OPEN)
	{
		pLH->udwLogType |= LM_LOG_OPEN_UNLINK;
	}

	wReservedSize = _RSVD_SIZE_DELETE_DE + _RSVD_SIZE_DEALLOCATE_FAT +
					_RSVD_SIZE_DEALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	if (pDeallocEA->pVC != NULL)
	{
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenDeallocateFat(pVol, pNode, pLH, 0, pDeallocEA, wReservedSize);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	_sublogGenDeallocateFat(pVol, pNode, pLH, 0, pDealloc, wReservedSize);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DELETE_DE;
	// delete DE
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	_sublogGenDeleteDE(pVol, pLH, pNode, pCxt);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * writes set-status log
 * 
 * @param	pVol 			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pstOldSFN 		: [IN] old SFN DE information
 * @param	pNewXDEInfo		: [IN] new XDE information
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_NOMEM		: not enough memory to write log
 * @return	else			: log write failed
 */
static FFatErr
_logSetState(Vol* pVol, Node* pNode, FatDeSFN* pDEOldSFN, XDEInfo* pNewXDEInfo,
			FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;			// pointer for sub-log

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	if (NODE_IS_OPEN_UNLINK(pNode) == FFAT_TRUE)
	{
		return FFAT_OK;
	}

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_SET_STATE, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeEndOffset, pNode->stDeInfo.dwDeEndCluster, pCxt) == FFAT_OK);
	FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pNode), pNode->stDeInfo.dwDeOffsetSFNE, pNode->stDeInfo.dwDeClusterSFNE, pCxt) == FFAT_OK);

	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	_sublogGenUpdateDE(pLH, pNode, pDEOldSFN, pNewXDEInfo);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}



/**
 * writes create new file/directory log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pNodeChild		: [IN] pointer of node
 * @param	psName			: [IN] new file name
 * @param	pSubDir			: [IN] To directory : sub-directory allocate FAT chain information
 * 								To file : it is null
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	pCxt		: [IN] context of current operation
 * @return FFAT_OK		: success
 * @return FFAT_NOMEM	: not enough memory to write log
 * @return else			: log write failed
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNodeChild when calling _sublogGenAllocateFat
 * @remarks
 * Create Log information has 2 cases (the second case):
 * 2. Create new file or directory
 *		Saved information in log file:
 *		(1) New psName (SFN and LFN DEs),
 *		(2) To directory: Allocated cluster (pSubDir) for sub-directory
 *		Undo it in recover process
 *
 *    Log structure:
 *		SubLogCreateDe
 *		--> LM_FAT for sub-directory (only to directory)
 *
 */
static FFatErr
_logCreateNew(Vol* pVol, Node* pNodeChild,
					t_wchar* psName, FatAllocate* pSubDir,
					FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;
	t_int16			wReservedSize;	// reserved log size for next log

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNodeChild);
	FFAT_ASSERT(pdwCacheFlag);

	// if log is disabled, return immediately
	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_CREATE_NEW, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	// get reserved size
	wReservedSize = _RSVD_SIZE_CREATE_DE + _RSVD_SIZE_ALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

	if (NODE_IS_DIR(pNodeChild))
	{
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
	}
	else
	{
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	}

	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_CREATE_DE;
	_sublogGenCreateDE(pVol, pLH, pNodeChild, psName, wReservedSize, FFAT_FALSE, pCxt);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize		-= _RSVD_SIZE_ALLOCATE_FAT;
	if (NODE_IS_DIR(pNodeChild))
	{
		FFAT_ASSERT(pSubDir);
		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;

		//start cluster of FAT chain is 0 before allocating clusters
		_sublogGenAllocateFat(pVol, pNodeChild, pLH, wReservedSize, pSubDir, wReservedSize);
		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// write transaction log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * Generate and write the Rename log
 *
 * @param	pVol			: [IN] pointer of volume
 * @param	pSrc			: [IN] source node
 * @param	pOldDes			: [IN] Old destination node
 * @param	pDes			: [IN] destination node
 * @param	psName			: [IN] renamed name
 * @param	bUpdateDe		: [IN] Update DEs or not
 * @param	bDeleteSrcDe	: [IN] Delete source DEs or not
 * @param	bDeleteOldDe	: [IN] Delete old destination DEs or not
 * @param	bCreateDesDe	: [IN] Create destination DEs or not
 * @param	pDeallocateOld	: [IN] Deallocate FAT chain information of old destination node
 * @param	pDeallocateEA	: [IN] cluster information for eXtended Attribute for destination node
 * @param	pDotDot			: [IN] directory entry pointer for ".." entry for directory.
 *								It may be NULL, when it does not need to be updated.
 * @param		dwFlag		: [IN] rename flag
 *								must care FFAT_RENAME_TARGET_OPENED
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @return	FFAT_OK			: success
 * @return	else			: failed
 * @author 
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	APR-29-2009 [JeongWoo Park] add dwFlag parameter to check open unlink state
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pDes when calling _sublogGenDeallocateFat
 */
static FFatErr
_logRename(Vol* pVol, Node* pSrc, Node* pOldDes, Node* pDes,
				t_wchar* psName, t_boolean bUpdateDe, t_boolean bDeleteSrcDe,
				t_boolean bDeleteDesDe, t_boolean bCreateDesDe,
				FatDeallocate* pDeallocateDes, FatDeallocate* pDeallocateDesEA,
				FatDeSFN* pDotDotOrig, FatDeSFN* pDotDot,
				FFatRenameFlag dwFlag, FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH = NULL;			// pointer for log
	SubLog*			pSL;				// pointer for sub log
	SubLog*			pLastSL = NULL;		// pointer for last sub log
	t_int16			wReservedSize;			// reserved size for next logs

	Node*			pNodeTmp = NULL;

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_RENAME, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	if (dwFlag & FFAT_RENAME_TARGET_OPENED)
	{
		pLH->udwLogType |= LM_LOG_OPEN_UNLINK;
	}

	// allocate memory for log record
	pNodeTmp = (Node*) FFAT_LOCAL_ALLOC(sizeof(Node), pCxt);
	FFAT_ASSERT(pNodeTmp);

	ffat_node_resetNodeStruct(pNodeTmp);

	wReservedSize = _RSVD_SIZE_UPDATE_DE + _RSVD_SIZE_DELETE_DE + _RSVD_SIZE_DELETE_DE +
					_RSVD_SIZE_UPDATE_DE + _RSVD_SIZE_CREATE_DE + 
					_RSVD_SIZE_DEALLOCATE_FAT + _RSVD_SIZE_DEALLOCATE_FAT +
					_RSVD_SIZE_LOG_TAIL;

	pNodeTmp->pVol = pVol;

	wReservedSize -= _RSVD_SIZE_UPDATE_DE;
	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	if (bUpdateDe)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		FFAT_ASSERT(NODE_IS_OPEN_UNLINK(pDes) == FFAT_FALSE);
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pDes), pDes->stDeInfo.dwDeEndOffset, pDes->stDeInfo.dwDeEndCluster, pCxt) == FFAT_OK);
		FFAT_ASSERT(ffat_share_checkClusterOfOffset(pVol, NODE_COP(pDes), pDes->stDeInfo.dwDeOffsetSFNE, pDes->stDeInfo.dwDeClusterSFNE, pCxt) == FFAT_OK);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenUpdateDE(pLH, pDes, &pSrc->stDE, NULL);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DELETE_DE;
	if (bDeleteSrcDe)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenDeleteDE(pVol, pLH, pSrc, pCxt);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DELETE_DE;
	if (bDeleteDesDe)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenDeleteDE(pVol, pLH, pOldDes, pCxt);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_UPDATE_DE;
	// Update DE for directory DOTDOT child
	if (pDotDot)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag	= LM_SUBLOG_FLAG_CONTINUE;
		pNodeTmp->dwFlag					= NODE_FLAG_NONE;
		pNodeTmp->stDeInfo.dwDeStartCluster = NODE_C(pDes);
		pNodeTmp->stDeInfo.dwDeStartOffset	= 32;
		pNodeTmp->stDeInfo.dwDeEndCluster	= NODE_C(pDes);
		pNodeTmp->stDeInfo.dwDeEndOffset	= 32;
		pNodeTmp->stDeInfo.dwDeClusterSFNE	= NODE_C(pDes);
		pNodeTmp->stDeInfo.dwDeOffsetSFNE	= 32;
		FFAT_MEMCPY(&pNodeTmp->stDE, pDotDot, sizeof(FatDeSFN));

		_sublogGenUpdateDE(pLH, pNodeTmp, pDotDotOrig, NULL);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_CREATE_DE;
	//psName is must for redo. Try to give enough log space to save psName
	if (bCreateDesDe)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenCreateDE(pVol, pLH, pDes, psName, wReservedSize, FFAT_TRUE, pCxt);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	if (pDeallocateDes != NULL)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		FFAT_ASSERT(bDeleteDesDe == FFAT_TRUE);
		_sublogGenDeallocateFat(pVol, pDes, pLH, 0, pDeallocateDes, wReservedSize);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize >= _RSVD_SIZE_LOG_TAIL);

	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	if (pDeallocateDesEA->pVC != NULL)
	{
		pLastSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);

		pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_CONTINUE;
		_sublogGenDeallocateFat(pVol, pDes, pLH, 0, pDeallocateDesEA, wReservedSize);

		pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	}

	FFAT_ASSERT(pLastSL);

	pLastSL->stSubLogHeader.uwNextSLFlag = (t_uint16)FFAT_BO_INT16((t_int16)LM_SUBLOG_FLAG_LAST);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// write transaction log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pNodeTmp, sizeof(Node), pCxt);
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * writes truncate directory log
 * 
 * @param	pVol			: [IN] pointer of volume
 * @param	pNode			: [IN] pointer of node
 * @param	pDealloc		: [IN] deallocated FAT chain information for truncating directory
 * @param	pdwCacheFlag	: [OUT] flag for cache operation
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK	: success
 * @author 
 * @version	DEC-21-2008 [DongYoung Seo] add wReservedSize to check is there enough space for next log
 * @version	Aug-29-2009 [SangYoon Oh] Add the parameter pNode when calling _sublogGenDeallocateFat
 */
static FFatErr
_logTruncateDir(Vol* pVol, Node* pNode, FatAllocate* pDealloc,
					FFatCacheFlag* pdwCacheFlag, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader*		pLH;			// pointer for log
	SubLog*			pSL;			// pointer for sub-log
	t_int16			wReservedSize;	// reserved size for next logs

	FFAT_ASSERT(pVol);

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	if (pDealloc == NULL)
	{
		return FFAT_OK;
	}

	r = _allocAndInitLogHeader(pVol, &pLH, LM_LOG_TRUNCATE_DIR, *pdwCacheFlag, pCxt);
	FFAT_EO(r, (_T("log base work failed")));

	wReservedSize = _RSVD_SIZE_DEALLOCATE_FAT + _RSVD_SIZE_LOG_TAIL;

	pSL = (SubLog*)((t_int8*)pLH + pLH->wUsedSize);
	wReservedSize -= _RSVD_SIZE_DEALLOCATE_FAT;
	pSL->stSubLogHeader.uwNextSLFlag = LM_SUBLOG_FLAG_LAST;
	_sublogGenDeallocateFat(pVol, pNode, pLH, pNode->dwCluster, pDealloc, wReservedSize);

	FFAT_ASSERT((VOL_SS(pVol) - pLH->wUsedSize) >= wReservedSize);
	FFAT_ASSERT(wReservedSize < VOL_SS(pVol));
	FFAT_ASSERT(wReservedSize == _RSVD_SIZE_LOG_TAIL);

	// generate log header and write log
	r = _logWriteTransaction(pVol, pLH, pCxt);
	IF_UK (r < 0)
	{
		goto out;
	}

	if (VOL_FLAG(pVol) & VOL_ADDON_FULL_LLW)
	{
		_SET_CALLBACK(pVol, *pdwCacheFlag);
	}

	r = FFAT_OK;

out:
	IF_UK ((r == FFAT_OK) && (pLH->uwFlag & LM_FLAG_SYNC))
	{
		*pdwCacheFlag |= FFAT_CACHE_SYNC;
	}

	// free the allocated local memory
	FFAT_LOCAL_FREE(pLH, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * get current log and next log from log file
 * 
 * @param	pVol			: [IN] volume pointer
 * @param	pCurLogOrig		: [IN] original buffer pointer for current log, should be volume sector size
 * @param	ppCurLog		: [IN/OUT] new current log pointer storage
 * @param	pNextLogOrig	: [IN] original buffer pointer for next log, should be volume sector size
 * @param	ppNextLog		: [IN/OUT] new next log pointer storage
 * @param	pwSlot			: [OUT] total log slots until current recovery
 * @param	pbNextIsNewLLW	: [OUT] boolean for next log new LLW or not
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_ENOENT		: no more log entry
 * @return	FFAT_EINVALID	: log slot is invalid
 * @author
 * @version	FEB-28-2008 [DongYoung Seo] bug fix.
 *								log module does not recover log at the last entry
  *								_getNextLog() return FFAT_OK2, when there is no more log entry and
 *								update pcurLog
 */
static FFatErr
_getNextLog(Vol* pVol, LogHeader* pCurLogOrig, LogHeader** ppCurLog,
			LogHeader* pNextLogOrig, LogHeader** ppNextLog,
			t_uint16* pwSlot, t_boolean* pbNextIsNewLLW, ComCxt* pCxt)
{
	LogConfirm*			pConfirm;			// pointer for confirm log
	t_boolean			bReadNextSlot = FFAT_TRUE;
	FFatErr				r;

	// update current log
	if ((*ppNextLog)->udwLogType & LM_LOG_FLAG_LLW)
	{
		if (*pbNextIsNewLLW == FFAT_TRUE)
		{
			*ppCurLog = *ppNextLog;
			*pbNextIsNewLLW = FFAT_FALSE;
		}
		else
		{
			(*ppCurLog) = (LogHeader*)(((t_int8*)(*ppCurLog)) + (*ppCurLog)->wUsedSize);
		}
	}
	else if ((*ppNextLog)->udwLogType != LM_LOG_NONE)
	{
		*ppCurLog = *ppNextLog;
		*pbNextIsNewLLW = FFAT_FALSE;
	}
	else
	{
		// this is the first log entry
		// do nothing
	}

	if ((*ppCurLog)->udwLogType & LM_LOG_FLAG_LLW)
	{
		// This log is LLW
		// check current log is confirm mark(last log)
		*ppNextLog = (LogHeader*)(((t_int8*)(*ppCurLog)) + (*ppCurLog)->wUsedSize);
		if ((*ppNextLog)->udwLogVer == _LLW_CONFIRM)
		{
			// this is the last LLW log, let's read the next log slot
			// calculate real entry size 
			pConfirm = (LogConfirm*)(*ppNextLog);

			// increase slot count
			*pwSlot = (t_uint16)(*pwSlot + (ESS_MATH_CDB(pConfirm->dwLogSize, VOL_SS(pVol), VOL_SSB(pVol))));

			if (*pwSlot >= LOG_MAX_SLOT)
			{
				// This is the last log, stop
				return FFAT_ENOENT;
			}

			FFAT_MEMCPY(pCurLogOrig, *ppCurLog, (*ppCurLog)->wUsedSize);
			*ppCurLog = pCurLogOrig;
		}
		else
		{
			bReadNextSlot = FFAT_FALSE;
		}
	}
	else
	{
		if ((*ppNextLog)->udwLogType != LM_LOG_NONE)
		{
			// the next log is a normal log
			FFAT_MEMCPY(pCurLogOrig, *ppNextLog, VOL_SS(pVol));
			*ppCurLog = pCurLogOrig;
		}

		(*pwSlot)++;

		if (*pwSlot >= LOG_MAX_SLOT)
		{
			// This is the last log, stop
			return FFAT_ENOENT;
		}
	}

	FFAT_ASSERT(*pwSlot < LOG_MAX_SLOT);

	r = FFAT_OK;

	if (bReadNextSlot == FFAT_TRUE)
	{
		// get next log
		r = _readSlot(pVol, pNextLogOrig, *pwSlot, pCxt);
		IF_UK (r < 0)
		{
			FFAT_LOG_PRINTF((_T("invalid log header")));
			return r;
		}

		(*ppNextLog) = pNextLogOrig;

		if ((*ppNextLog)->udwLogType & LM_LOG_FLAG_LLW)
		{
			*pbNextIsNewLLW = FFAT_TRUE;
			(*ppNextLog) = (LogHeader*)(_LLW(pVol)->pBuff);
		}
	}

	return r;
}


/**
* init open unlink area in log file
* 
* @param	pVol		: [IN] pointer of volume
* @param	dwSlotIndex	: [IN] index of log slot
* @param	dwSlotcount	: [IN] count of slot
* @param	pCxt		: [IN] context of current operation
* @param	pBuff		: [IN] sector size buffer
* @return	FFAT_OK	: success
* @return	else		: failed
* @author	InHwan Choi
* @version	DEC-05-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_initLogOpenUnlink(Vol* pVol, t_int32 dwSlotIndex, t_int32 dwSlotCount,
					t_int8* pBuff, ComCxt* pCxt)
{
	FFatErr				r;
	t_int32			dwIndex;
	LogInfo*			pLI;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pBuff);
	FFAT_ASSERT(dwSlotIndex >= 0);
	FFAT_ASSERT(dwSlotCount >= 0);

	FFAT_MEMSET(pBuff, 0x00, VOL_SS(pVol));

	// init OU log header
	((OULogHeader*)pBuff)->udwLogVer	= _LOG_VERSION;
	((OULogHeader*)pBuff)->udwLogType	= LM_LOG_OPEN_UNLINK;

	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(dwSlotIndex + dwSlotCount <= LOG_OPEN_UNLINK_SLOT);

	for(dwIndex = dwSlotIndex; dwIndex < (dwSlotIndex + dwSlotCount); dwIndex++)
	{
		r = ffat_readWriteSectors(pVol, NULL,
					pLI->pdwSectors[LOG_MAX_SLOT + LOG_EXT_SLOT + dwIndex], 1, pBuff,
					(FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
		if (r != 1)
		{
			if (r >= 0)
			{
				FFAT_LOG_PRINTF((_T("Fail to write open unlink logs")));
				r = FFAT_EIO;
			}

			goto out;
		}
	}

	r = FFAT_OK;

out :
	return r;
}


/**
 * This function is called after delete node operation for log recovery
 * This function removes cluster entry for data and EA Cluster chain
 *
 * @param		pNode			: [IN] node pointer
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: nothing to do
 * @return		else			: error
 * @author		InHwan Choi
 * @version		JAN-20-2008 [InHwan Choi] First Writing.
 * @version		08-DEC-2008 [DongYoung Seo] add check routing for dwOUEntryIndex and dwEAEntryIndex
 *									both of them may be invalid entry index.
 * @version		03-FEB-2009 [JeongWoo Park] policy change
 *									1) remove unnecessary parameter(dwOUEntryIndex, dwEAEntryIndex)
 *									2) OUEntry, EAEntry can be stored in different slots
 */
FFatErr
_logDeleteOpenUnlink(Node* pNode, ComCxt* pCxt)
{
	Vol*			pVol;
	FFatErr			r;
	OULogHeader*	pOULog;
	t_uint32*		pOULogSlot;
	t_int32			dwCurSlotIndex;			// index of slot for current operation
	t_int32			dwCurEntryIndex;		// index of entry for current operation in slot
	t_int32			dwOldSlotIndex = -1;	// index of slot for old operation
	t_boolean		bIsEA;					// is this EAcluster ?

	FFAT_ASSERT(pNode);

	if ((_NODE_OUEI(pNode) == _INVALID_ENTRY_INDEX) &&
		(_NODE_EAEI(pNode) == _INVALID_ENTRY_INDEX))
	{
		// nothing to do
		return FFAT_OK;
	}

	pVol = NODE_VOL(pNode);

	pOULog = FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pOULog);

	pOULogSlot = (t_uint32*)(pOULog + (LOG_OPEN_UNLINK_HEADER_SIZE / sizeof(OULogHeader)));

	// calculate slot index and entry offset in open unlink log slot
	if (_NODE_OUEI(pNode) != _INVALID_ENTRY_INDEX)
	{
		// NODE cluster
		bIsEA = FFAT_FALSE;
		dwCurSlotIndex	= (_NODE_OUEI(pNode) >> 16) & 0xFFFF;
		dwCurEntryIndex	= _NODE_OUEI(pNode) & 0xFFFF;
	}
	else
	{
delete_ou_for_ea:

		// EA cluster
		FFAT_ASSERT(_NODE_EAEI(pNode) != _INVALID_ENTRY_INDEX);
		bIsEA = FFAT_TRUE;
		dwCurSlotIndex	= (_NODE_EAEI(pNode) >> 16) & 0xFFFF;
		dwCurEntryIndex	= _NODE_EAEI(pNode) & 0xFFFF;
	}

	// read open unlink slot sector if old & current slot is different
	if (dwOldSlotIndex != dwCurSlotIndex)
	{
		if (dwOldSlotIndex >= 0)
		{
			// write open unlink slot sector for old operation
			r = _writeSlotOpenUnlink(pVol, pOULog, dwOldSlotIndex, pCxt);
			IF_UK (r < 0)
			{
				FFAT_LOG_PRINTF((_T("fail to write open unlink log header.")));
				goto out;
			}
		}

		r = _readSlotOpenUnlink(pVol, pOULog, dwCurSlotIndex, pCxt);
		IF_UK (r < 0)
		{
			FFAT_ASSERT(0);
			FFAT_LOG_PRINTF((_T("fail to read open unlink log header.")));
			goto out;
		}

		dwOldSlotIndex = dwCurSlotIndex;
	}

	// debug begin
#ifdef FFAT_DEBUG
	if (bIsEA == FFAT_TRUE)
	{
		t_uint32 udwEACluster;
		r = ffat_ea_getEAFirstCluster(pNode, &udwEACluster, pCxt);
		IF_UK (r < 0)
		{
			FFAT_ASSERTP(0, (_T("There is no extended attribute cluster")));
		}

		FFAT_ASSERT(pOULogSlot[dwCurEntryIndex] == FFAT_BO_UINT32(udwEACluster));
	}
#endif
	// debug end

	// deallocate OU cluster
	pOULogSlot[dwCurEntryIndex] = 0;

	r = _deallocateBitmapOpenUnlink(pOULog, dwCurEntryIndex);
	FFAT_EO(r, (_T("fail to deallocate Bitmap")));
	
	pOULog->udwValidEntry--;	// decrease valid entry count;

	if ((bIsEA == FFAT_FALSE) && (_NODE_EAEI(pNode) != _INVALID_ENTRY_INDEX))
	{
		// we must redo for EA cluster
		goto delete_ou_for_ea;
	}

	// write open unlink slot sector
	r = _writeSlotOpenUnlink(pVol, pOULog, dwCurSlotIndex, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write open unlink log header.")));
		goto out;
	}

	// reset OU entry index of node
	_NODE_OUEI(pNode) = _INVALID_ENTRY_INDEX;
	_NODE_EAEI(pNode) = _INVALID_ENTRY_INDEX;

	// update free slot hint
	_LOG_INFO(pVol)->udwOUFreeSectorHint = dwCurSlotIndex;

	r = FFAT_OK;

out:
	// free the allocated local memory	
	FFAT_LOCAL_FREE(pOULog, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * writes open unlink log
 * 
 * @param		pVol			: [IN] pointer of volume
 * @param		pNode			: [IN] pointer of node
 * @param		dwOUCluster		: [IN] deallocate FAT chain cluster for open unlink chain
 *									if 0, no need to update the OU slot
 * @param		dwEACluster		: [IN] deallocate FAT chain cluster for extended attribute
 *									if 0, no need to update the OU slot
 * @param		pCxt			: [IN] context of current operation
 * @return		FFAT_OK			: success
 * @return		FFAT_NOMEM		: not enough memory to write log
 * @return		else			: log write failed
 * @author		InHwan Choi
 * @version		DEC-05-2007 [InHwan Choi] First Writing.
 * @version		DEC-30-2008 [DongYoung Seo] bug fix on free slot lookup routine.
 *									it didn't check all of the slots to find free slot
 * @version		DEC-30-2008 [DongYoung Seo] Policy Change.
 *									This function does not return no space error any more.
 *									current log slots for unlink can store 7000 open unlinked files
 *									There must not occur 7000 files were open unliked.
 * @version		FEB-03-2009 [JeongWoo Park] Policy Change.
 *									1) dwOUCluster, dwEACluster can be stored in different slots.
 *									2) If old value exist, then modify the value with new
 */
static FFatErr
_logOpenUnlink(Vol* pVol, Node* pNode, t_uint32 dwOUCluster, t_uint32 dwEACluster, ComCxt* pCxt)
{
	FFatErr				r;
	OULogHeader*		pOULog;
	t_uint32*			pOULogSlot;
	t_int32				dwCurSlotIndex;			// index of slot for current operation
	t_int32				dwCurEntryIndex;		// index of entry for current operation in slot
	t_int32				dwCheckedSlotCount;		// count of checked slot 
	t_int32				dwOldSlotIndex = -1;	// index of slot for old operation
	t_boolean			bIsNew;					// is this OU entry new ?
	t_boolean			bIsEA;					// is this EAcluster ?
	
	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pNode);

	FFAT_ASSERT(_IS_LOGGING_ENABLED(pVol) == FFAT_TRUE);

	if ((dwOUCluster == 0) && (dwEACluster == 0))
	{
		// Nothing to do
		return FFAT_OK;
	}

	pOULog = FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pOULog);

	FFAT_ASSERT(LOG_OPEN_UNLINK_HEADER_SIZE == sizeof(OULogHeader));
	pOULogSlot = (t_uint32*)(pOULog + (LOG_OPEN_UNLINK_HEADER_SIZE / sizeof(OULogHeader)));

	// setting for slot index and bitmap index
	if (dwOUCluster > 0)
	{
		// NODE cluster
		bIsEA = FFAT_FALSE;

		if (_NODE_OUEI(pNode) == _INVALID_ENTRY_INDEX)
		{
			// new entry
			dwCurSlotIndex	= _LOG_INFO(pVol)->udwOUFreeSectorHint;
			dwCurEntryIndex = -1;
			bIsNew = FFAT_TRUE;
		}
		else
		{
			// existed entry
			dwCurSlotIndex	= (_NODE_OUEI(pNode) >> 16) & 0xFFFF;
			dwCurEntryIndex	= _NODE_OUEI(pNode) & 0xFFFF;
			bIsNew = FFAT_FALSE;
		}
	}
	else
	{
update_ou_for_ea:

		// EA cluster
		FFAT_ASSERT(dwEACluster > 0);

		bIsEA = FFAT_TRUE;
		
		if (_NODE_EAEI(pNode) == _INVALID_ENTRY_INDEX)
		{
			// new entry
			dwCurSlotIndex	= _LOG_INFO(pVol)->udwOUFreeSectorHint;
			dwCurEntryIndex	= -1;
			bIsNew = FFAT_TRUE;
		}
		else
		{
			// existed entry
			dwCurSlotIndex	= (_NODE_EAEI(pNode) >> 16) & 0xFFFF;
			dwCurEntryIndex	= _NODE_EAEI(pNode) & 0xFFFF;
			bIsNew = FFAT_FALSE;
		}
	}

	dwCheckedSlotCount = 0;

	// read & allocate OU slot
	do
	{
		// read open unlink slot sector if old & current slot is different
		if (dwOldSlotIndex != dwCurSlotIndex)
		{
			if (dwOldSlotIndex >= 0)
			{
				// write open unlink slot sector for old operation
				r = _writeSlotOpenUnlink(pVol, pOULog, dwOldSlotIndex, pCxt);
				IF_UK (r < 0)
				{
					FFAT_LOG_PRINTF((_T("fail to write open unlink log header.")));
					goto out;
				}
			}

			r = _readSlotOpenUnlink(pVol, pOULog, dwCurSlotIndex, pCxt);
			IF_UK (r < 0)
			{
				FFAT_ASSERT(0);
				FFAT_LOG_PRINTF((_T("fail to read open unlink log header.")));
				goto out;
			}

			dwOldSlotIndex = dwCurSlotIndex;
		}

		FFAT_ASSERT(pOULog->udwLogVer == _LOG_VERSION);
		FFAT_ASSERT(pOULog->udwLogType == LM_LOG_OPEN_UNLINK);
		FFAT_ASSERT(pOULog->udwValidEntry <= LOG_OPEN_UNLINK_ENTRY_SLOT);

		if (bIsNew == FFAT_TRUE)
		{
			// find free area
			if (pOULog->udwValidEntry < LOG_OPEN_UNLINK_ENTRY_SLOT)
			{
				// we found free slot !!!
				dwCurEntryIndex = _allocateBitmapOpenUnlink(pOULog);

				// update NODE index
				if (bIsEA == FFAT_FALSE)
				{
					_NODE_OUEI(pNode) = (dwCurSlotIndex << 16) | (dwCurEntryIndex & 0xFFFF);
				}
				else
				{
					_NODE_EAEI(pNode) = (dwCurSlotIndex << 16) | (dwCurEntryIndex & 0xFFFF);
				}

				// increase valid entry count;
				pOULog->udwValidEntry++;

				_LOG_INFO(pVol)->udwOUFreeSectorHint = dwCurSlotIndex;

				break;
			}
			else
			{
				// we must scan next slot
				dwCurSlotIndex++;
				dwCurSlotIndex = dwCurSlotIndex & LOG_OPEN_UNLINK_SLOT_MASK;

				if (dwCheckedSlotCount >= LOG_OPEN_UNLINK_SLOT)
				{
					FFAT_LOG_PRINTF((_T("There is no empty entry in open unlink log slot.")));
					FFAT_ASSERT(0);		// check enough space while developing
					r = FFAT_OK;		// ignore not enough space error
					goto out;
				}

				dwCheckedSlotCount++;
				continue;
			}
		}
		else
		{
			// no need to find free, just break;
			break;
		}
	} while(1);

	// write cluster number
	if (bIsEA == FFAT_FALSE)
	{
		pOULogSlot[dwCurEntryIndex] = FFAT_BO_UINT32(dwOUCluster);

		if (dwEACluster > 0)
		{
			// we must redo for EA cluster
			goto update_ou_for_ea;
		}
	}
	else
	{
		pOULogSlot[dwCurEntryIndex] = FFAT_BO_UINT32(dwEACluster);
	}

	// write open unlink slot sector
	r = _writeSlotOpenUnlink(pVol, pOULog, dwCurSlotIndex, pCxt);
	IF_UK (r < 0)
	{
		FFAT_LOG_PRINTF((_T("fail to write open unlink log header.")));
		goto out;
	}

	r = FFAT_OK;

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pOULog, VOL_SS(pVol), pCxt);

	return r;
}


/**
 * _logRecoveryOU recover lost cluster caused by open unlink
 * It read log file, deallocate fat chain
 * 
 * @param	pVol 		: [IN] pointer of volume 
 * @param	pCxt		: [IN] context of current operation
 * @return	FFAT_OK		: success
 * @return	else		: failed
 * @author	InHwan Choi
 * @version	DEC-07-2007 [InHwan Choi] First Writing.
 * @version	DEC-19-2008 [DongYoung Seo] make a sub function _commitOpenUnlink.
 */
static FFatErr
_logRecoveryOpenUnlink(Vol* pVol, ComCxt* pCxt)
{
	return _commitOpenUnlink(pVol, pCxt);
}


/**
* this function commits all open unlinked cluster cluster chain
* It read log file and deallocate FAT chain
* 
* @param	pVol 		: [IN] pointer of volume 
* @param	pCxt		: [IN] context of current operation
* @return	FFAT_OK		: success
* @return	else		: failed
* @author	InHwan Choi
* @version	DEC-07-2007 [InHwan Choi] First Writing.
* @version	DEC-19-2008 [DongYoung Seo] change function name from _logRecoveryOpenUnlink.
*								to use this function on un-mounting operation
*/
static FFatErr
_commitOpenUnlink(Vol* pVol, ComCxt* pCxt)
{

	FFatErr				r;
	OULogHeader*		pOULog;
	FatAllocate			stAlloc;
	t_uint32			dwIndexSlot;
	t_int32				dwValidBitmapIndex;
	t_uint32			dwValidEntry;
	t_uint32			dwTemp;
	t_uint32			dwCluster;
	t_uint32*			pOULogSlot;

	FFAT_ASSERT(pVol);

	_LOG_INFO(pVol)->udwOUFreeSectorHint = 0;

	pOULog = (OULogHeader*)FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
	FFAT_ASSERT(pOULog);

	pOULogSlot = (t_uint32*)(pOULog + (LOG_OPEN_UNLINK_HEADER_SIZE / sizeof(OULogHeader)));

	// file의 open unlink log area를 차례차례 읽는다.
	for(dwIndexSlot = 0; dwIndexSlot < LOG_OPEN_UNLINK_SLOT; dwIndexSlot++)
	{
		// read open unlink slot sector
		r = _readSlotOpenUnlink(pVol, pOULog, dwIndexSlot, pCxt);
		FFAT_EO(r, (_T("fail to read open unlink log header.")));

		dwValidEntry = pOULog->udwValidEntry;

		if (dwValidEntry == 0)
		{
			FFAT_ASSERT(EssBitmap_GetLowestBitOne((t_uint8*)pOULog->pBitmap, LOG_OPEN_UNLINK_BITMAP_BYTE) == ESS_ENOENT);
			continue;
		}

		do
		{
			dwValidBitmapIndex = EssBitmap_GetLowestBitOne((t_uint8*)pOULog->pBitmap, LOG_OPEN_UNLINK_BITMAP_BYTE);
			if (dwValidBitmapIndex < 0)
			{
				FFAT_ASSERT(dwValidBitmapIndex == ESS_ENOENT);
				FFAT_ASSERT(0);
			}

			dwCluster = FFAT_BO_UINT32(pOULogSlot[dwValidBitmapIndex]);

// debug begin
#ifdef FFAT_DEBUG
			pOULogSlot[dwValidBitmapIndex] = 0;
#endif
// debug end

			FFAT_ASSERT(FFATFS_IsValidCluster(VOL_VI(pVol), dwCluster) == FFAT_TRUE);

			stAlloc.dwPrevEOF 		= 0;
			stAlloc.dwFirstCluster	= dwCluster;
			stAlloc.pVC			= NULL;

			r = FFATFS_DeallocateCluster(VOL_VI(pVol), 0, &stAlloc, &dwTemp, NULL,
							FAT_DEALLOCATE_FORCE, (FFAT_CACHE_FORCE | FFAT_CACHE_SYNC),
							NULL, pCxt);
			FFAT_EO(r, (_T("fail to deallocate cluster")));

			r = _deallocateBitmapOpenUnlink(pOULog, dwValidBitmapIndex);
			FFAT_EO(r, (_T("fail to deallocate bitmap open unlink")));

			dwValidEntry--;
		} while(dwValidEntry > 0);

// debug begin
#ifdef FFAT_DEBUG
		// is log slot really empty?
		FFAT_ASSERT(EssBitmap_GetLowestBitOne((t_uint8*)pOULog->pBitmap, LOG_OPEN_UNLINK_BITMAP_BYTE) == ESS_ENOENT);

		for(r = 0; r < LOG_OPEN_UNLINK_ENTRY_SLOT; r++)
		{
			FFAT_ASSERT(pOULogSlot[r] == 0);
		}
#endif
// debug end

		r = _initLogOpenUnlink(pVol, dwIndexSlot, 1, (t_int8*)pOULog, pCxt);
		FFAT_EO(r, (_T("fail to init from open unlink log")));
	}

	r = FFAT_OK;

out:
	// free the allocated local memory
	FFAT_LOCAL_FREE(pOULog, VOL_SS(pVol), pCxt);

	return r;
}


/**
* _readFreeSlotOpenUnlink reads one open unlink log slot
* 적어도 free entry는 2개 이상이어야 함.
* 
* @param	pVol			: [IN] volume pointer
* @param	pOULog			: [OUT] log header information of log slot
* @param	dwId			: [IN] open unlink slot number
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success to get a empty log slot
* @return	FFAT_EINVALID	: log slot is invalid
* @return	FFAT_EIO		: fail to read log slot
* @author	InHwan Choi
* @version	DEC-05-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_readSlotOpenUnlink(Vol* pVol, OULogHeader* pOULog, t_int32 dwSlotIndex, ComCxt* pCxt)
{
	LogInfo*		pLI;
	t_int32			dwIndex;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pOULog);
	FFAT_ASSERT(dwSlotIndex >= 0);
	FFAT_ASSERT(dwSlotIndex < LOG_OPEN_UNLINK_SLOT);

	pLI			= _LOG_INFO(pVol);
	dwIndex		= _OU_LOG_SLOT_BASE + dwSlotIndex;

	FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], 1, (t_int8*)pOULog,
					FFAT_CACHE_DATA_LOG, FFAT_TRUE, pCxt);
	IF_UK (r != 1)
	{
		FFAT_LOG_PRINTF((_T("fail to read data from log file")));
		return FFAT_EIO;
	}

	// endian translation
	_boLogHeaderOpenUnlink(pOULog);

	// check open unlink header
	if ((pOULog->udwLogVer != _LOG_VERSION) ||
		(pOULog->udwLogType != LM_LOG_OPEN_UNLINK) ||
		(pOULog->udwValidEntry > LOG_OPEN_UNLINK_ENTRY_SLOT))
	{
		r = _initLogOpenUnlink(pVol, dwSlotIndex, 1, (t_int8*)pOULog, pCxt);
		FFAT_ER(r, (_T("fail to open unlink log init")));
	}

	return FFAT_OK;
}


/**
* writeSlotOpenUnlink writes one open unlink log slot
* 
* @param	pVol			: [IN] volume pointer
* @param	pOULog 			: [OUT] log header information of log slot
* @param	dwId			: [IN]	open unlink slot number
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success to write log slot
* @return	FFAT_EIO		: fail to write log slot
* @author	InHwan Choi
* @version	DEC-05-2007 [InHwan Choi] First Writing.
* @version	NOV-27-2008 [DongYoung Seo] change I/O policy from DIRECT IO to SYNC MODE I/O
*/
static FFatErr
_writeSlotOpenUnlink(Vol* pVol, OULogHeader* pOULog, t_uint32 dwId, ComCxt* pCxt)
{
	t_int32			dwIndex;
	LogInfo*		pLI;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pOULog);

	dwIndex	= _OU_LOG_SLOT_BASE + dwId;
	pLI		= _LOG_INFO(pVol);

	FFAT_ASSERT(dwIndex < LOG_SLOT_COUNT);

	//endian translation
	_boLogHeaderOpenUnlink(pOULog);

	r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], 1, (t_int8*)pOULog,
				(FFAT_CACHE_DATA_LOG | FFAT_CACHE_SYNC), FFAT_FALSE, pCxt);
	if (r != 1)
	{
		if (r >= 0)
		{
			r = FFAT_EIO;
		}

		FFAT_LOG_PRINTF((_T("fail to write open unlink log data to a log file")));
		return r;
	}

	return FFAT_OK;
}


/**
* _allocateBitmapOpenUnlink allocate one free entry for open unlink log
* 
* @param		pOULog		: [OUT] log header information of log slot
* 
* @return		dwFreeBitmapOffset: success to get a empty log slot
* @author		InHwan Choi
* @version		DEC-05-2007 [InHwan Choi] First Writing.
*/
static t_int32
_allocateBitmapOpenUnlink(OULogHeader* pOULog)
{
	t_int32			dwFreeBitmapIndex;

	FFAT_ASSERT(pOULog);

	// get free bitmap offset
	dwFreeBitmapIndex = EssBitmap_GetLowestBitZero((t_uint8*)pOULog->pBitmap, LOG_OPEN_UNLINK_BITMAP_BYTE);
	if (dwFreeBitmapIndex < 0)
	{
		FFAT_ASSERT(dwFreeBitmapIndex == ESS_ENOENT);
		FFAT_ASSERT(0);
	}
	FFAT_ASSERT(dwFreeBitmapIndex < LOG_OPEN_UNLINK_ENTRY_SLOT);
	FFAT_ASSERT(ESS_BITMAP_IS_CLEAR((t_uint8*)pOULog->pBitmap, dwFreeBitmapIndex));

	// set bitmap
	ESS_BITMAP_SET(pOULog->pBitmap, dwFreeBitmapIndex);
	
	FFAT_ASSERT(ESS_BITMAP_IS_SET((t_uint8*)pOULog->pBitmap, dwFreeBitmapIndex));

	// save open unlink index
	return dwFreeBitmapIndex;
}


/**
* _deallocateBitmapOpenUnlink allocate one entry for open unlink log
* 
* @param	pOULog 			: [OUT] log header information of log slot
* @param	dwEntryOffset	: [IN]	bitmap offset that deallocate cluster
* 
* @return	FFAT_OK		: always success
* @author	InHwan Choi
* @version	DEC-07-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_deallocateBitmapOpenUnlink(OULogHeader* pOULog, t_uint32 dwEntryOffset)
{
	FFAT_ASSERT(pOULog);

	FFAT_ASSERT(ESS_BITMAP_IS_SET((t_uint8*)pOULog->pBitmap, dwEntryOffset));

	// clear bitmap
	ESS_BITMAP_CLEAR(pOULog->pBitmap, dwEntryOffset);

	FFAT_ASSERT(ESS_BITMAP_IS_CLEAR((t_uint8*)pOULog->pBitmap, dwEntryOffset));

	// always FFAT_OK
	return FFAT_OK;
}


/**
* get log area from reserved area
*
* if there is enough log area fill the information on LogInfo
*
* @param	pVol 			: [IN] volume pointer
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success, there is enough log area at the end of the volume
* @return	FFAT_EINVALID	: fail, there is not enough space for log or invalid log sector
* @author	DongYoung Seo
* @version	JAN-30-2008 [DongYoung Seo] First Writing.
* @version	APR-01-2009 [GwangOk Go] support case that FAT sector is larger than block before mounted
* @version	JUN-10-2009 [GwangOk Go] use reserved region as log area 
*/
static FFatErr
_getReservedAreaForLog(Vol* pVol, ComCxt* pCxt)
{
	t_int32			dwIndex;
	LogInfo*		pLI;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	pLI = _LOG_INFO(pVol);

	// check is there enough area at the reserved region
	if (((t_int32)pVol->stVolInfo.dwFirstFatSector - FFAT_BPB_RESERVED_SECTOR) >= LOG_SECTOR_COUNT)
	{
		for (dwIndex = LOG_SECTOR_COUNT - 1; dwIndex >= 0; dwIndex--)
		{
			// fill log area
			pLI->pdwSectors[dwIndex] = pVol->stVolInfo.dwFirstFatSector - LOG_SECTOR_COUNT + dwIndex;
		}

		return _checkLogArea(pVol, pCxt);
	}
	else
	{
		return FFAT_EINVALID;
	}
}


/**
* get log area from log file
*
* @param	pVol 			: [IN] volume pointer
* @param	pNodeLog		: [IN]	bitmap offset that deallocate cluster
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success, there is enough log area at the end of the volume
* @return	else			: error
* @author	DongYoung Seo
* @version	DEC-07-2007 [InHwan Choi] First Writing.
*/
static FFatErr
_getSectorsForLogFile(Vol* pVol, Node* pNodeLog, ComCxt* pCxt)
{
	t_uint32		dwSC;		// count of sector
	t_uint32		dwCC;		// count of cluster
	t_uint32		dwSN;		// sector number
	FFatVC			stVC;
	t_uint32		dwCount;
	t_int32			i;
	t_uint32		j;
	t_uint32*		pSectorStorage;
	LogInfo*		pLI;
	FFatErr			r;

	dwSC	= NODE_S(pNodeLog) >> VOL_SSB(pVol);

	stVC.pVCE	= (FFatVCE*)FFAT_LOCAL_ALLOC((sizeof(FFatVCE) * dwSC), pCxt);
	FFAT_ASSERT(stVC.pVCE);

	VC_INIT(&stVC, 0);
	stVC.dwTotalEntryCount	= dwSC;

	dwCC = ESS_MATH_CDB(NODE_S(pNodeLog), VOL_CS(pVol), VOL_CSB(pVol));

	// get vectored cluster information from offset 0
	r = ffat_misc_getVectoredCluster(pVol, pNodeLog, NODE_C(pNodeLog), 0, dwCC, &stVC, NULL, pCxt);
	FFAT_EO(r, (_T("fail to get cluster for node")));

	FFAT_ASSERT(VC_CC(&stVC) == dwCC);

	pLI = _LOG_INFO(pVol);
	pSectorStorage = pLI->pdwSectors;

	// fill log info
	for (i = 0; i < VC_VEC(&stVC); i++)
	{
		dwSN = FFATFS_GetFirstSectorOfCluster(VOL_VI(pVol), stVC.pVCE[i].dwCluster);
		dwCount = stVC.pVCE[i].dwCount << VOL_SPCB(pVol);

		if (dwCount > dwSC)
		{
			dwCount = dwSC;
		}

		for (j = 0; j < dwCount; j++)
		{
			*pSectorStorage = dwSN + j;
			pSectorStorage++;
		}

		dwSC -= dwCount;

		if (dwSC == 0)
		{
			break;
		}
	}

out:
	dwSC	= NODE_S(pNodeLog) >> VOL_SSB(pVol);

	FFAT_LOCAL_FREE(stVC.pVCE, (sizeof(FFatVCE) * dwSC), pCxt);

	return r;
}


/**
 * check the header of log sector is wrong
 *
 * @param	pVol 			: [IN] volume pointer
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_EIO		: fail to read log header
 * @return	FFAT_EINVALID	: wrong log header
 * @author	GwangOk Go
 * @version	JUN-10-2009 [GwangOk Go] First Writing.
 */
static FFatErr
_checkLogArea(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;
	LogInfo*		pLI;
	LogHeader		stLogHeader;
	FFatCacheInfo	stCI;
	t_int32			dwIndex;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(pLI);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	for (dwIndex = 0; dwIndex < LOG_SECTOR_COUNT; dwIndex += (LOG_SECTOR_COUNT - 1))
	{
		// read log header
		r = FFATFS_ReadWritePartialSector(VOL_VI(pVol), pLI->pdwSectors[dwIndex], 0,
										sizeof(LogHeader), (t_int8*)&stLogHeader,
										(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_LOG),
										&stCI, FFAT_TRUE, pCxt);
		IF_UK (r != sizeof(LogHeader))
		{
			FFAT_PRINT_DEBUG((_T("fail to read log header")));
			r = FFAT_EIO;
			goto out;
		}

		_boLogHeader(&stLogHeader);

		// check log header
		if ((stLogHeader.udwLogVer != _LOG_VERSION) && (stLogHeader.udwLogVer != _LLW_CONFIRM))
		{
			FFAT_PRINT_DEBUG((_T("sector doesn't have correct log header")));
			r = FFAT_EINVALID;
			goto out;
		}
	}

	r = FFAT_OK;
out:
	return r;
}


/**
 * initialize log file on mounting
 *
 * @param	pVol 			: [IN] volume pointer
 * @param	pCxt			: [IN] context of current operation
 * @return	FFAT_OK			: success
 * @return	FFAT_EIO		: fail to write log header
 * @author	GwangOk Go
 * @version	JUN-10-2009 [GwangOk Go] First Writing.
 */
static FFatErr
_initLogFile(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;
	LogHeader		stLogHeader;
	LogInfo*		pLI;
	FFatCacheInfo	stCI;
	t_int32			dwIndex;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	FFAT_INIT_CI(&stCI, NULL, VOL_DEV(pVol));

	pLI = _LOG_INFO(pVol);

	FFAT_ASSERT(pLI);

	stLogHeader.udwLogVer	= _LOG_VERSION;
	stLogHeader.udwLogType	= LM_LOG_NONE;

	_boLogHeader(&stLogHeader);

	for (dwIndex = LOG_SECTOR_COUNT - 1; dwIndex >= 0; dwIndex--)
	{
		// write log header
		r = FFATFS_ReadWritePartialSector(VOL_VI(pVol), pLI->pdwSectors[dwIndex], 0,
										sizeof(LogHeader), (t_int8*)&stLogHeader,
										(FFAT_CACHE_DIRECT_IO | FFAT_CACHE_DATA_LOG),
										&stCI, FFAT_FALSE, pCxt);
		IF_UK (r != sizeof(LogHeader))
		{
			FFAT_PRINT_DEBUG((_T("fail to write log header")));
			r = FFAT_EIO;
			goto out;
		}
	}

	r = FFAT_OK;

out:
	return r;
}


/**
* get the information of log creation from BPB
*
* @param		pVol 			: [IN] volume pointer
* @param		pstLogCreatInfo	: [OUT]	The information of log creation
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: there is the information of log creation in BPB
* @return		FFAT_ENOENT		: there is no information of log creation
* @return		else			: error
* @author		JeongWoo Park
* @version		MAR-26-2009 [JeongWoo Park] First Writing
*/
static FFatErr
_getLogCreatInfo(Vol* pVol, LogCreatInfo* pstLogCreatInfo, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pstLogCreatInfo);
	FFAT_ASSERT(pCxt);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	// read the information of log creation from BPB
	r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_LOG_INFO_OFFSET,
										sizeof(LogCreatInfo), (t_int8*)pstLogCreatInfo,
										(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO),
										&stCI, FFAT_TRUE, pCxt);
	if (r != sizeof(LogCreatInfo))
	{
		FFAT_LOG_PRINTF((_T("fail to read Log creation information from BPB")));
		return FFAT_EIO;
	}

	// check validity of Log creation information
	if (FFAT_STRNCMP((char*)pstLogCreatInfo->szSignature, FFAT_LOG_CREATE_INFO_SIGNATURE,
					FFAT_LOG_CREATE_INFO_SIGNATURE_SIZE) == 0)
	{
#ifdef FFAT_BIG_ENDIAN
		pstLogCreatInfo->dwDeStartCluster	= FFAT_BO_UINT32(pstLogCreatInfo->dwDeStartCluster);
		pstLogCreatInfo->dwDeStartOffset	= FFAT_BO_UINT32(pstLogCreatInfo->dwDeStartOffset);
		pstLogCreatInfo->dwDeCount			= FFAT_BO_UINT32(pstLogCreatInfo->dwDeCount);
		pstLogCreatInfo->dwStartCluster		= FFAT_BO_UINT32(pstLogCreatInfo->dwStartCluster);
#endif // end of #ifdef FFAT_BIG_ENDIAN
		r = FFAT_OK;
	}
	else
	{
		r = FFAT_ENOENT;
	}

	return r;
}

/**
* set or erase the information of log creation in BPB
*
* @param		pVol 			: [IN] volume pointer
* @param		pstLogCreatInfo	: [IN]	The information of log creation
*										if NULL, erase the information of log creation
* @param		pCxt			: [IN] context of current operation
* @return		FFAT_OK			: there is the information of log creation in BPB
* @return		else			: error
* @author		JeongWoo Park
* @version		MAR-26-2009 [JeongWoo Park] First Writing
*/
static FFatErr
_setLogCreatInfo(Vol* pVol, LogCreatInfo* pstLogCreatInfo, ComCxt* pCxt)
{
	FatVolInfo*		pVolInfo;
	FFatCacheInfo	stCI;
	LogCreatInfo	stLogInfoTemp;
	FFatErr			r;

	FFAT_ASSERT(pVol);
	FFAT_ASSERT(pCxt);

	pVolInfo = VOL_VI(pVol);

	FFAT_INIT_CI(&stCI, NULL, VI_DEV(pVolInfo));

	// generate Log creation information
	if (pstLogCreatInfo == NULL)
	{
		// if NULL, erase log creation info
		pstLogCreatInfo = &stLogInfoTemp;
		FFAT_MEMSET(pstLogCreatInfo, 0x00, sizeof(LogCreatInfo));
	}
	else
	{
		FFAT_MEMCPY(pstLogCreatInfo->szSignature, FFAT_LOG_CREATE_INFO_SIGNATURE,
					FFAT_LOG_CREATE_INFO_SIGNATURE_SIZE);
#ifdef FFAT_BIG_ENDIAN
		pstLogCreatInfo->dwDeStartCluster	= FFAT_BO_UINT32(pstLogCreatInfo->dwDeStartCluster);
		pstLogCreatInfo->dwDeStartOffset	= FFAT_BO_UINT32(pstLogCreatInfo->dwDeStartOffset);
		pstLogCreatInfo->dwDeCount			= FFAT_BO_UINT32(pstLogCreatInfo->dwDeCount);
		pstLogCreatInfo->dwStartCluster		= FFAT_BO_UINT32(pstLogCreatInfo->dwStartCluster);
#endif // end of #ifdef FFAT_BIG_ENDIAN
	}

	// write the information of log creation to BPB
	r = FFATFS_ReadWritePartialSector(pVolInfo, 0, ADDON_BPB_LOG_INFO_OFFSET,
									sizeof(LogCreatInfo), (t_int8*)pstLogCreatInfo,
									(FFAT_CACHE_DATA_FS | FFAT_CACHE_META_IO | FFAT_CACHE_SYNC),
									&stCI, FFAT_FALSE, pCxt);
	if (r != sizeof(LogCreatInfo))
	{
		FFAT_LOG_PRINTF((_T("fail to write Log creation information from BPB")));
		return FFAT_EIO;
	}

	return FFAT_OK;
}

/**
* this function check the validity of log creation,
* and recover un-finished the creation of log file.
* In current version, the recovery will delete the incomplete log file.
* 
* @param	pVol			: [IN] pointer of volume
* @param	pCxt			: [IN] context of current operation
* @return	FFAT_OK			: success
* @return	else			: failed
* @author	JeongWoo Park
* @version	MAR-26-2009 [JeongWoo Park] : first writing
*/
static FFatErr
_checkLogCreation(Vol* pVol, ComCxt* pCxt)
{
	FFatErr			r;
	LogCreatInfo	stLogCreatInfo;
	FatAllocate		stAlloc;
	t_uint32		dwTemp;

	r = _getLogCreatInfo(pVol, &stLogCreatInfo, pCxt);
	IF_LK (r == FFAT_ENOENT)
	{
		// NOTHING TO DO
		return FFAT_OK;
	}

	FFAT_EO(r, (_T("Failed to get log creation info")));

	// Deallocate log clusters
	if (stLogCreatInfo.dwStartCluster != 0)
	{
		stAlloc.dwPrevEOF 		= 0;
		stAlloc.dwFirstCluster	= stLogCreatInfo.dwStartCluster;
		stAlloc.pVC				= NULL;

		r = FFATFS_DeallocateCluster(VOL_VI(pVol), 0, &stAlloc, &dwTemp, NULL,
									FAT_DEALLOCATE_FORCE, (FFAT_CACHE_FORCE | FFAT_CACHE_SYNC),
									NULL, pCxt);
		FFAT_EO(r, (_T("fail to deallocate cluster")));
	}

	// Delete the Directory entry
	FFAT_ASSERT(stLogCreatInfo.dwDeCount > 0);

	r = ffat_deleteDEs(pVol, VOL_RC(pVol), stLogCreatInfo.dwDeStartOffset,
						stLogCreatInfo.dwDeStartCluster, stLogCreatInfo.dwDeCount, FFAT_FALSE,
						(FFAT_CACHE_DATA_DE | FFAT_CACHE_SYNC), NULL, pCxt);
	FFAT_EO(r, (_T("Failed to delete DE")));

	// Erase the information of log creation
	r = _setLogCreatInfo(pVol, NULL, pCxt);

out:
	return r;
}


/**
* allocate a memory chunk for LLW
*
* @return	NULL		: not enough memory
* @return	else		: success
* @author	DongYoung Seo
* @version	MAY-21-2008 [DongYoung Seo] First Writing.
*/
static LogLazyWriter*
_allocLLW(Vol* pVol)
{
#ifdef FFAT_DYNAMIC_ALLOC

	LogLazyWriter*		pLLW;

	pLLW = (LogLazyWriter*)FFAT_MALLOC(sizeof(LogLazyWriter), ESS_MALLOC_IO);
	if (pLLW)
	{
		_initLLW(pLLW);
		pLLW->pVol = pVol;
	}

	return pLLW;

#else

	return _LLW(NULL);

#endif
}


/**
* free a memory chunk for LLW
*
* @param	pVol		: [IN] volume pointer
* @return	NULL		: not enough memory
* @return	else		: success
* @author	DongYoung Seo
* @version	MAY-21-2008 [DongYoung Seo] First Writing.
*/
static void
_freeLLW(Vol* pVol)
{
	if(_LOG_INFO(pVol) != NULL)
	{
#ifdef FFAT_DYNAMIC_ALLOC
		FFAT_FREE(_LOG_INFO(pVol)->pLLW, sizeof(LogLazyWriter));
#endif

		_LOG_INFO(pVol)->pLLW = NULL;
	}
}


/**
* get SubLogFat form a log
*
* @param	pLH				: [IN] log header
* @return	valid pointer	: a SubLogFat pointer 
* @return	NULL			: there is no log related to FAT
* @author	DongYoung Seo
* @version	AUG-20-2008 [DongYoung Seo] First Writing
*/
static SubLogFat*
_sublogGetSubLogFat(LogHeader* pLH)
{
	SubLog*			pSL;
	_SubLogFlag		uwSLFlag;		// flag of current sub log

	FFAT_ASSERT(pLH);

	pSL = (SubLog*)((t_int8*)pLH + sizeof(LogHeader));

	do
	{
		if (pSL->stSubLogHeader.uwSublogType == LM_SUBLOG_ALLOC_FAT)
		{
			return &pSL->u.stFat;
		}

		uwSLFlag = pSL->stSubLogHeader.uwNextSLFlag;
		pSL = (SubLog*)((t_int8*)pSL + _sublogGetSize(pSL));
	} while(uwSLFlag != LM_SUBLOG_FLAG_LAST);

	return NULL;
}


/**
* get new directory entry from log
*
* @param	pLH				: [IN] log header
* @return	valid pointer	: a directory entry pointer for new DE
* @return	NULL			: there is no log related to directory entry
* @author	DongYoung Seo
* @version	AUG-20-2008 [DongYoung Seo] First Writing
*/
static FatDeSFN*
_sublogGetNewDE(LogHeader* pLH)
{
	SubLog*			pSL;
	_SubLogType		uwDEMask;		// mask for directory entry related log
	_SubLogFlag		uwSLFlag;		// flag of current sub log

	FFAT_ASSERT(pLH);

	pSL = (SubLog*)((t_int8*)pLH + sizeof(LogHeader));
	uwDEMask = LM_SUBLOG_UPDATE_DE | LM_SUBLOG_CREATE_DE;

	do
	{
		if (pSL->stSubLogHeader.uwSublogType & uwDEMask)
		{
			if (pSL->stSubLogHeader.uwSublogType == LM_SUBLOG_UPDATE_DE)
			{
				return &pSL->u.stUpdateDE.stNewDE;
			}
			else
			{
				FFAT_ASSERT(pSL->stSubLogHeader.uwSublogType == LM_SUBLOG_CREATE_DE);
				return &pSL->u.stCreateDE.stDE;
			}
		}

		uwSLFlag = pSL->stSubLogHeader.uwNextSLFlag;
		pSL = (SubLog*)((t_int8*)pSL + _sublogGetSize(pSL));
	} while(uwSLFlag != LM_SUBLOG_FLAG_LAST);

	return NULL;
}


/**
* get size of a sub log
*
* @param	pSL			: [IN] sub log header
* @author	DongYoung Seo
* @version	AUG-20-2008 [DongYoung Seo] First Writing
*/
static t_int16
_sublogGetSize(SubLog* pSL)
{
	t_int32		dwSize;			// size of a sublog
	t_int16		wNameLen;

	FFAT_ASSERT(pSL);

	dwSize = sizeof(SubLogHeader);

	switch(pSL->stSubLogHeader.uwSublogType)
	{
	case LM_SUBLOG_NONE:
				// no sub log
				break;

		case LM_SUBLOG_ALLOC_FAT:
		case LM_SUBLOG_DEALLOC_FAT:
				dwSize += sizeof(SubLogFat);
				dwSize += (pSL->u.stFat.dwValidEntryCount * sizeof(FFatVCE));
				break;

		case LM_SUBLOG_DELETE_DE:
				dwSize += sizeof(SubLogDeleteDe);
				break;

		case LM_SUBLOG_UPDATE_DE:
				dwSize += sizeof(SubLogUpdateDe);
				break;

		case LM_SUBLOG_CREATE_DE:
				dwSize += sizeof(SubLogCreateDe);

				if (pSL->u.stCreateDE.wNameInExtra == FFAT_FALSE)
				{
					wNameLen = pSL->u.stCreateDE.wNameLen;
					wNameLen = _makeIncEven(wNameLen);
					dwSize += (wNameLen * sizeof(t_wchar));
				}
				break;

		case LM_SUBLOG_SET_EA:
		case LM_SUBLOG_DELETE_EA:
				dwSize += sizeof(SubLogEA);
				break;
		case LM_SUBLOG_UPDATE_ROOT_EA:
				dwSize += sizeof(SubLogUpdateRootEA);
				break;

		default :
			FFAT_ASSERTP(0, (_T("not implemented yet")));
			dwSize = 0;
			break;
	}

	FFAT_ASSERT(dwSize < FAT_SECTOR_MAX_SIZE);

	return (t_int16)dwSize;
}


// debug begin

//=============================================================================
//
//	DEBUG PART
//


//====================================================================
//
//	FOR LOG RECOVERY
//
//	2006/11/20, DongYoung Seo... a dish of gossip
//	All log related function work as like a real operation 
//	but it does not update any data on cache or any data on block device.
//	It reduces performance but I did not have time for log recovery
//	
//	Good Things of log (Pros)
//	1. It reduces meta-data write operation significantly.
//	2. It improves performance from number 1.
//
//	This log algorithm is a good one. But there is a little improvement point. (Cons)
//	1. It does not support file level transaction.
//	2. It want all of the meta-data update information before real update.
//	3. Log had been updated before FFAT_CORE is updated.

#ifdef _LOG_REPLAY
	/**
	 * backup log information
	 * 
	 * @param	pVol			: [IN] pointer of volume
	 * @param	nValidLogNum	: [IN] number of valid log slots
	 * @param	pCxt			: [IN] context of current operation
	 * @return	FFAT_OK			: success
	 * @author 
	 * @version 
	 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
	 */
	static FFatErr
	_logBackup(Vol* pVol, t_int16 wValidSlots, ComCxt* pCxt)
	{
		FFatErr			r = FFAT_OK;
		t_int32			dwIndex;
		t_uint32		dwSN;
		LogInfo*		pLI;
		t_int8*			pBuff;

		FFAT_ASSERT(pVol);

		if (wValidSlots == 0)
		{
			return FFAT_OK;
		}

		pBuff = (t_int8*) FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
		FFAT_ASSERT(pBuff);

		dwIndex	= 0;
		pLI		= _LOG_INFO(pVol);

		// for full log backup
		wValidSlots = LOG_SLOT_COUNT;

		while (wValidSlots > 0)
		{
			dwIndex = wValidSlots - 1;

			FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

			dwSN = pLI->pdwSectors[dwIndex];

			r = ffat_readWriteSectors(pVol, NULL, pLI->pdwSectors[dwIndex], 1, pBuff,
						(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC | FFAT_CACHE_DATA_LOG), FFAT_TRUE, pCxt);
			if (r != 1)
			{
				FFAT_LOG_PRINTF((_T("Fail to read logs slots")));
				r = FFAT_EIO;
				goto out;
			}

			dwIndex = wValidSlots + LOG_SLOT_COUNT - 1;

			FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

			dwSN = pLI->pdwSectors[dwIndex] ;

			r = ffat_readWriteSectors(pVol, NULL, dwSN, 1, pBuff,
						(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC | FFAT_CACHE_DATA_LOG), FFAT_FALSE, pCxt);
			if (r != 1)
			{
				FFAT_LOG_PRINTF((_T("Fail to write logs")));
				r = FFAT_EIO;
				goto out;
			}

			wValidSlots--;
		}

		FFAT_DEBUG_LOG_PRINTF(pVol, (_T("%d slots backed up \n"), LOG_SLOT_COUNT));

	out:
		FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

		return r;
	}


	/**
	 * restore logs from backup area
	 * 
	 * @param	pVol			: [IN] pointer of volume
	 * @param	dwFlag			: [IN] mount flag
	 * @param	pCxt			: [IN] context of current operation
	 * @return	FFAT_OK			: success
	 * @author 
	 * @version 
	 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
	 * @version	MAR-26-2009 [DongYoung Seo] change I/O flag from direct I/O to meta I/O
	 */
	static FFatErr
	_logRestore(Vol* pVol, FFatMountFlag dwFlag, ComCxt* pCxt)
	{
		FFatErr			r = FFAT_OK;
		t_int8*			pBuff;
		LogInfo*		pLI;
		t_int32			dwTotalSlots;
		t_uint32		dwSN;
		t_int32			dwIndex;

		FFAT_ASSERT(pVol);

		if ((dwFlag & FFAT_MOUNT_LOG_REPLAY) == 0)
		{
			return FFAT_OK;
		}

		pBuff = (t_int8*) FFAT_LOCAL_ALLOC(VOL_SS(pVol), pCxt);
		FFAT_ASSERT(pBuff);

		pLI		= _LOG_INFO(pVol);

		dwTotalSlots = LOG_SLOT_COUNT;

		while (dwTotalSlots > 0)
		{
			dwIndex = dwTotalSlots + LOG_SLOT_COUNT - 1;

			FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));

			dwSN = pLI->pdwSectors[dwIndex];

			r = ffat_readWriteSectors(pVol, NULL, dwSN, 1, pBuff,
						(FFAT_CACHE_META_IO | FFAT_CACHE_DATA_LOG),
						FFAT_TRUE, pCxt);
			if (r != 1)
			{
				FFAT_LOG_PRINTF((_T("Fail to read logs slots")));
				r = FFAT_EIO;
				goto out;
			}

			dwIndex -= LOG_SLOT_COUNT;

			FFAT_ASSERT(dwIndex < (sizeof(pLI->pdwSectors) / sizeof(t_uint32)));
			FFAT_ASSERT(dwIndex >= 0);

			dwSN = pLI->pdwSectors[dwIndex];

			r = ffat_readWriteSectors(pVol, NULL, dwSN, 1, pBuff,
						(FFAT_CACHE_META_IO | FFAT_CACHE_SYNC | FFAT_CACHE_DATA_LOG),
						FFAT_FALSE, pCxt);
			if (r != 1)
			{
				FFAT_LOG_PRINTF((_T("Fail to read logs slots")));
				r = FFAT_EIO;
				goto out;
			}

			dwTotalSlots--;
		}

		FFAT_DEBUG_LOG_PRINTF(pVol, (_T("%d slots restored \n"), LOG_SLOT_COUNT));

	out:
		FFAT_LOCAL_FREE(pBuff, VOL_SS(pVol), pCxt);

		return r;
	}
#endif


#if defined(_LOG_DEBUG_) || defined(FFAT_DEBUG_FILE)
	/**
	* Print log record
	*
	* @param	pVol		: [IN] volume pointer
	* @param	pLH			: [IN] pointer of log record to print
	* @param	dwType		: [IN] log type string
	* @return	FFAT_OK		: Success 
	* @return	else		: failed
	*/
	static FFatErr
	_printLog(Vol* pVol, LogHeader* pLH, char* psType)
	{
		t_int32		dwlogType;

		FFAT_ASSERT(pLH);

		dwlogType = _getLogId(pLH->udwLogType);
		if (dwlogType > _MAX_LOG_TYPE)
		{
			FFAT_LOG_PRINTF((_T("invalid logType")));
			FFAT_ASSERT(0);
			return FFAT_EFAT;
		}

		if (dwlogType != _MAX_LOG_TYPE)
		{
			FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[%s] LogType/SeqNo/Size/Name:0x%X/%d/%d/%s\n"),
						psType, pLH->udwLogType, pLH->udwSeqNum,
						pLH->wUsedSize, _gpLogName[dwlogType - 1]));
		}

		return FFAT_OK;
	}

	/**
	* Print log record
	*
	* @param	pVol		: [IN] volume pointer
	* @param	pSL			: [IN] pointer of sub log record to print
	* @return	FFAT_OK		: Success
	* @return	else		: failed
	*/
	static FFatErr
	_printSubLog(Vol* pVol, SubLog* pSL)
	{
		SubLogFat*				pFAT;
		SubLogUpdateDe*			pUDE;
		SubLogCreateDe*			pCDE;
		SubLogDeleteDe*			pDDE;
		SubLogEA*				pEA;
		SubLogUpdateRootEA*		pUREA;
		t_int32					i;
		FFatVCE*				pVCE;
		t_uint16				uwLogType;

		FFAT_ASSERT(pSL);

		uwLogType = FFAT_BO_UINT16(pSL->stSubLogHeader.uwSublogType);

		switch (uwLogType)
		{
			case LM_SUBLOG_ALLOC_FAT:
			case LM_SUBLOG_DEALLOC_FAT:
				pFAT = &pSL->u.stFat;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] %s ALLOC, NodeCluster/PrevEOC/Count/FirstCluster/ValidEntryCount:%d/%d/%d/%d/%d\n"),
											(uwLogType == LM_SUBLOG_ALLOC_FAT) ? "ALLOC" : "DEALLOC",
											pFAT->udwCluster, pFAT->udwPrevEOF,
											pFAT->dwCount, pFAT->udwFirstCluster,
											pFAT->dwValidEntryCount));
				if (pFAT->dwValidEntryCount > 0)
				{
					pVCE = (FFatVCE*)(((t_int8*)pFAT) + sizeof(SubLogFat));
					for (i = 0; i < pFAT->dwValidEntryCount; i++)
					{
						FFAT_DEBUG_LOG_PRINTF(pVol, (_T("(%d/%d)"),pVCE[i].dwCluster, pVCE[i].dwCount));

						FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), pVCE[i].dwCluster)== FFAT_TRUE);
						FFAT_ASSERT(FFATFS_IS_VALID_CLUSTER(VOL_VI(pVol), (pVCE[i].dwCluster + pVCE[i].dwCount - 1)) == FFAT_TRUE);
					}
//					FFAT_DEBUG_LOG_PRINTF((_T("\n")));
				}
				break;

			case LM_SUBLOG_UPDATE_DE:
				pUDE = &pSL->u.stUpdateDE;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] UpdateDE, ClusterForSFNE/OffsetForSFNE:%d/%d\n"),
											pUDE->dwClusterSFNE, pUDE->dwOffsetSFNE));
				_printDE(pVol, &pUDE->stOldDE);
				_printDE(pVol, &pUDE->stNewDE);
				break;

			case LM_SUBLOG_CREATE_DE:
				pCDE = &pSL->u.stCreateDE;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] CreateDE, DeStartCluster/DeStartOffset/DeCount:%d/%d/%d\n"),
											pCDE->dwDeStartCluster, pCDE->dwDeStartOffset,
											pCDE->dwDeCount));
				_printDE(pVol, &pCDE->stDE);
				break;

			case LM_SUBLOG_DELETE_DE:
				pDDE = &pSL->u.stDeleteDE;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] DeleteDE, DeStartCluster/DeStartOffset/DeCount:%d/%d/%d\n"),
											pDDE->dwDeStartCluster, pDDE->dwDeStartOffset, pDDE->dwDeCount));
				_printDE(pVol, &pDDE->stDE);
				break;

			case LM_SUBLOG_SET_EA:
			case LM_SUBLOG_DELETE_EA:
				pEA = &pSL->u.stEA;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] %s, FirstCluster/DelOffset/InsOffset/Size:%d/%d/%d/%d\n"),
										(uwLogType == LM_SUBLOG_SET_EA) ? "SET_EA" : "DEL_EA",
										pEA->udwFirstCluster, pEA->udwDelOffset, pEA->udwInsOffset,pEA->stEAEntry.udwEntryLength));
				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("				EAMain(org)Total/Used/ValidCount:%d/%d/%d\n"),
										pEA->stEAMain.udwTotalSpace, pEA->stEAMain.udwUsedSpace, pEA->stEAMain.uwValidCount));
				break;

			case LM_SUBLOG_UPDATE_ROOT_EA:
				pUREA = &pSL->u.stUpdateRootEA;

				FFAT_DEBUG_LOG_PRINTF(pVol, (_T("[SUBLOG] UPDATE_ROOT_EA, OldFirstCluster/NewFirstCluster:%d/%d\n"),
										pUREA->udwOldFirstCluster, pUREA->udwNewFirstCluster));
				break;

			default:
				FFAT_ASSERTP(0, (_T("Invalid log type")));
				break;
		}

		return FFAT_OK;
	}


	/**
	* Print directory entry
	*
	* @param	pVol		: [IN] volume pointer
	* @param	pDE			: [IN] a directory entry
	*/
	static void
	_printDE(Vol* pVol, FatDeSFN* pDE)
	{
		FatDeSFN	stDE;

		FFAT_MEMCPY(&stDE, pDE, sizeof(FatDeSFN));
		stDE.bAttr = '\0';

		FFAT_DEBUG_LOG_PRINTF(pVol, (_T("Name:%s, Attr/Cluster/Size/NTRes:%d/%d/%d/0x%2x\n"),
									pDE->sName,
									FFATFS_GetDeAttr(pDE),
									FFATFS_GetDeCluster(VOL_VI(pVol), pDE),
									FFATFS_GetDeSize(pDE),
									pDE->bNTRes));
	}
#endif		// end of #if defined(_LOG_DEBUG_) || defined(FFAT_DEBUG_FILE)

// debug end
