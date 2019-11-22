/**
 * @file	vdfs_layout.h
 * @brief	Internal constants and data structures for eMMCFS
 * @date	01/11/2013
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file defines all important eMMCFS data structures and constants
 *
 * @see		SRC-SEP-11012-HLD001_-eMMCFS-File-System.doc
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef _LINUX_EMMCFS_FS_H
#define _LINUX_EMMCFS_FS_H

#include <linux/types.h>

/*
 * The VDFS filesystem constants/structures
 */
/** crc size in bytes */
#define CRC32_SIZE	4

/** Maximum length of name string of a file/directory */
#define	VDFS_FILE_NAME_LEN		255
/** Maximum length of full path */
#define EMMCFS_FULL_PATH_LEN		1023

/** image created signed with crc */
#define CRC_ENABLED 1
#define CRC_DISABLED 0

#define SECTOR_SIZE		512
#define SECTOR_SIZE_SHIFT	9
#define SECTOR_PER_PAGE		(PAGE_CACHE_SIZE / SECTOR_SIZE)
#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define SB_SIZE			(sizeof(struct emmcfs_super_block))
#define SB_SIZE_IN_SECTOR	(SB_SIZE / SECTOR_SIZE)
#define VDFS_EXSB_SIZE_SECTORS	5
#define VDFS_EXSB_LEN		(SECTOR_SIZE * VDFS_EXSB_SIZE_SECTORS)
#define VDFS_SB_ADDR		0
#define VDFS_SB_COPY_ADDR	8
#define VDFS_SB_OFFSET		(VDFS_SB_ADDR + 2)
#define VDFS_SB_COPY_OFFSET	(VDFS_SB_COPY_ADDR + 2)
#define VDFS_EXSB_OFFSET	(VDFS_SB_ADDR + 3)
#define VDFS_EXSB_COPY_OFFSET	(VDFS_SB_COPY_ADDR + 3)
/** Base super block location in an eMMCFS volume.
 *  First 1024 bytes is reserved area. */
#define EMMCFS_RESERVED_AREA_LENGTH		1024

/* Magic Numbers.
 */
#define VDFS_LAYOUT_VERSION			"0001"
#define VDFS_SB_SIGNATURE			"VDFS"
#define EMMCFS_SB_VER_MAJOR	1
#define EMMCFS_SB_VER_MINOR	0

#define EMMCFS_CAT_FOLDER_MAGIC			"rDe"
#define EMMCFS_CAT_FILE_MAGIC			"eFr"
#define VDFS_XATTR_REC_MAGIC			"XAre"
#define VDFS_SNAPSHOT_BASE_TABLE		"CoWB"
#define VDFS_SNAPSHOT_EXTENDED_TABLE		"CoWE"
#define EMMCFS_FORK_MAGIC			0x46
#define EMMCFS_NODE_DESCR_MAGIC			0x644E		/*"dN"*/
#define EMMCFS_OOPS_MAGIC			"DBGe"

#define EMMCFS_BTREE_HEAD_NODE_MAGIC		"eHND"
#define EMMCFS_BTREE_LEAF_NODE_MAGIC		"NDlf"
#define EMMCFS_BTREE_INDEX_NODE_MAGIC		"NDin"
#define EMMCFS_EXTTREE_KEY_MAGIC		"ExtK"
#define VDFS_HRDTREE_KEY_MAGIC			"HrdL"


#define VDFS_PACK_METADATA_MAGIC		"PKmT"
#define VDFS_PACK_METADATA_VERSION		"0003"

#define VDFS_PACK_KEY_SIGNATURE_ROOT		"PKrt"
#define VDFS_PACK_KEY_SIGNATURE_FILE_TINY	"PKty"
#define VDFS_PACK_KEY_SIGNATURE_FILE_FRAG	"PKfg"
#define VDFS_PACK_KEY_SIGNATURE_FILE_CHUNK	"PKch"
#define VDFS_PACK_KEY_SIGNATURE_FOLDER		"PKfd"
#define VDFS_PACK_KEY_SIGNATURE_HLINK		"PKhl"
#define VDFS_PACK_KEY_SIGNATURE_SYMLINK		"PKsl"
#define VDFS_PACK_KEY_SIGNATURE_DEVICE		"PKdv"
#define VDFS_PACK_KEY_SIGNATURE_FIFO		"PKff"
#define VDFS_PACK_KEY_SIGNATURE_SOCKET		"PKsk"

#define VERSION_SIZE				(sizeof(u64))
#define VDFS_MOUNT_COUNT(version)		(0xffffffff & (version >> 32))
#define VDFS_SYNC_COUNT(version)		(0xffffffff & version)
/* inode bitmap magics and crcs*/
#define SMALL_AREA_BITMAP_MAGIC "sfab"
#define SMALL_AREA_BITMAP_MAGIC_LEN (sizeof(SMALL_AREA_BITMAP_MAGIC) - 1 \
					+ VERSION_SIZE)

/* inode bitmap magics and crcs*/
#define INODE_BITMAP_MAGIC "inob"
#define INODE_BITMAP_MAGIC_LEN (sizeof(INODE_BITMAP_MAGIC) - 1 + VERSION_SIZE)

/* fsm bitmap magics and crcs*/
#define FSM_BMP_MAGIC		"fsmb"
#define FSM_BMP_MAGIC_LEN	(sizeof(FSM_BMP_MAGIC) - 1 + VERSION_SIZE)

/* return offset of the crc number in buffer */
#define VDFS_CRC32_OFFSET(buff, block_size)	(buff+(block_size - CRC32_SIZE))
/* real data size of block is block size without signature and crc number */
#define VDFS_BIT_BLKSIZE(X, MAGIC_LEN)	(((X) -\
		(MAGIC_LEN + CRC32_SIZE))<<3)

/** Special inodes */
enum special_ino_no {
	VDFS_ROOT_INO = 1,		/** root inode */
	VDFS_CAT_TREE_INO,		/** catalog tree inode */
	VDFS_FSFILE = VDFS_CAT_TREE_INO,/** First special file */
	VDFS_SPACE_BITMAP_INO,		/** free space bitmap inode */
	VDFS_EXTENTS_TREE_INO,		/** inode bitamp inode number */
	VDFS_FREE_INODE_BITMAP_INO,	/** Free space bitmap inode */
	VDFS_HARDLINKS_TREE_INO,	/** Hardlinks tree inode */
	VDFS_XATTR_TREE_INO,		/** XAttr tree ino */
	VDFS_SMALL_AREA_BITMAP,
	VDFS_LSFILE = VDFS_SMALL_AREA_BITMAP,
	VDFS_SMALL_AREA,
	VDFS_SNAPSHOT_INO,
	VDFS_ORPHAN_INODES_INO,		/** Orphan inodes inode */
	VDFS_1ST_FILE_INO		/** First file inode */
};

#define VDFS_SF_NR	(VDFS_LSFILE - VDFS_FSFILE + 1)
#define VDFS_SF_INDEX(x)	(x - VDFS_FSFILE)





/* Flags definition for vdfs_unicode_string */
/* maximum length of unicode string */
#define EMMCFS_UNICODE_STRING_MAX_LEN 255

#define VDFS_XATTR_NAME_MAX_LEN 200
#define VDFS_XATTR_VAL_MAX_LEN 200

/**
 * @brief	The struct vdfs_unicode_string is used to keep and represent
 *		any Unicode string on eMMCFS. It is used for file and folder
 *		names representation and for symlink case.
 */
struct vdfs_unicode_string {
	/** The string's length */
	__le32 length;
	/** The chain of the Unicode symbols */
	u8 unicode_str[EMMCFS_UNICODE_STRING_MAX_LEN];
} __packed;

/**
 * @brief	The eMMCFS stores dates in unsigned 64-bit integer seconds and
 *		unsigned 32-bit integer nanoseconds.
 */
struct emmcfs_date {
	/** The seconds part of the date */
	__le64 seconds;
	/** The nanoseconds part of the date */
	__le32 nanoseconds;
} __packed;


#define DATE_RESOLUTION_IN_NANOSECONDS_ENABLED 1

/**
 * @brief	For each file and folder, eMMCFS maintains a record containing
 *		access permissions.
 */
struct emmcfs_posix_permissions {
	/* File mode        16 |11 8|7  4|3  0| */
	/*                     |_rwx|_rwx|_rwx| */
	__le16 file_mode;			/** File mode */
	__le32 uid;				/** User ID */
	__le32 gid;				/** Group ID */
} __packed;

/** Permissions value for user read-write operations allow. */
#define EMMCFS_PERMISSIONS_DEFAULT_RW  0x666
/** Permissions value for user read-write-execute operations allow. */
#define EMMCFS_PERMISSIONS_DEFAULT_RWX 0x777

/* Flags definition for emmcfs_extent structure */
/** Extent describes information by means of continuous set of LEBs */
#define EMMCFS_LEB_EXTENT	0x80
/** Extent describes binary patch which should be apply inside of block or LEB
 * on the offset in bytes from begin */
#define EMMCFS_BYTE_PATCH	0x20
/** The information fragment is pre-allocated but not really written yet */
#define EMMCFS_PRE_ALLOCATED	0x10

/**
 * @brief	On VDFS volume all existed data are described by means of
 *		extents.
 */
struct vdfs_extent {
	/** start block  */
	__le64	begin;
	/** length in blocks */
	__le32	length;
} __packed;

/**
 * @brief	File blocks in catalog tree are described by means extent plus
 *		extent iblock.
 */
struct vdfs_iextent {
	/** file data location */
	struct vdfs_extent	extent;
	/** extent start block logical index */
	__le64	iblock;
} __packed;

/** VDFS maintains information about the contents of a file using the
 * emmcfs_fork structure.
 */
#define VDFS_EXTENTS_COUNT_IN_FORK	9
/**
 * @brief	The VDFS fork structure.
 */
struct vdfs_fork {
	/** magic */
	__u8	magic;			/* 0x46 – F */
	/** The size in bytes of the valid data in the fork */
	__le64			size_in_bytes;
	/** The total number of allocation blocks which is
	 * allocated for file system object under last actual
	 * snapshot in this fork */
	__le32			total_blocks_count;
	/** The set of extents which describe file system
	 * object's blocks placement */
	struct vdfs_iextent	extents[VDFS_EXTENTS_COUNT_IN_FORK];
} __packed;

/* Snapshots Management */
/** START  ->> SNAPSHOT structuries  -----------------------------------------*/
#define VDFS_SNAPSHOT_MAX_SIZE (16 * 1024 * 1024)

/* Snapshot tables */
#define VDFS_SNAPSHOT_EXT_TABLES	8
#define VDFS_SNAPSHOT_BASE_TABLES	2

/**
 * @brief	The eMMCFS transaction.
 */
struct emmcfs_transaction {
	/** Original sectors location  */
	__le64 on_volume;
	/** Location in snapshot */
	__le64 on_snapshot;
	/** Block count in transaction */
	__le16 page_count;
} __packed;

/**
 * @brief	The eMMCFS snapshot descriptor.
 */
struct emmcfs_snapshot_descriptor {
	/** Signature magic */
	__u8 signature[4];				/* 0x65534e50 eSNP */
	/* mount version */
	__le32 mount_count;
	/* snapshot version count */
	__le32 version;
	/** Count of valid records in Root Snapshots Table */
	__le16 transaction_count;
	/** The offset from the end of this structure to place of CRC32
	 * checksum */
	__le16 checksum_offset;
	/** First transaction in snapshot */
	struct emmcfs_transaction first_transaction;
} __packed;

#define VDFS_SNAPSHOT_BASE_TABLE		"CoWB"
#define VDFS_SNAPSHOT_EXTENDED_TABLE		"CoWE"

struct vdfs_translation_record {
	/* logical block  */
	__le64 f_iblock;
	/* phisical block */
	__le64 m_iblock;
} __packed;

struct vdfs_snapshot_descriptor {
	/* signature */
	__u8 signature[4];
	/* mount count */
	__le32 mount_count;
	/* sync count */
	__le32 sync_count;
	/* offset to CRC */
	__le32 checksum_offset;
} __packed;

struct vdfs_base_table {
	/* descriptor: signature, mount count, sync count and checksumm offset*/
	struct vdfs_snapshot_descriptor descriptor;
	/* last metadata iblock number */
	__le64 last_page_index[VDFS_SF_NR];
	/* offset into translation tables for special files */
	__le32 translation_table_offsets[VDFS_SF_NR];
} __packed;

struct vdfs_extended_record {
	/* special file id */
	__u8 object_id;
	/* object iblock */
	__le64 table_index;
	/* meta iblock */
	__le64 meta_iblock;
} __packed;

struct vdfs_extended_table {
	struct vdfs_snapshot_descriptor descriptor;
	/* records count in extended table */
	__le32 records_count;
};

/** END  -<< SNAPSHOT structuries  -----------------------------------------*/

/**
 * @brief	The VDFS debug record, described one file system fail
 */
#define DEBUG_FUNCTION_LINE_LENGTH 5
#define DEBUG_FUNCTION_NAME_LENGTH 25

struct vdfs_debug_record {
	/** volume uuid */
	__le64 uuid;
	/** line number */
	__u8 line[DEBUG_FUNCTION_LINE_LENGTH];
	/** Oops function name */
	__u8 function[DEBUG_FUNCTION_NAME_LENGTH];
	/** error code */
	__le32 error_code;
	/** fail number */
	__le16 fail_number;
	/** record timestamp in jiffies */
	__le32 fail_time;
	/** mount count */
	__le32 mount_count;
	/** sync count */
	__le32 sync_count;

} __packed;

/**
 * @brief	The eMMCFS snapshot descriptor.
 */
struct vdfs_debug_descriptor {
	/** Signature magic */
	__u8 signature[4];
	/* fail count */
	__le32 record_count;
	/** next_oops offset */
	__le16 offset_to_next_record;
} __packed;

/**
 * @brief	The version of structures and file system at whole
 *		is tracked by.
 */
struct emmcfs_version {
	/** Major version number */
	__u8 major:4;
	/** Minor version number */
	__u8 minor:4;
} __packed;

/**
 * @brief	The eMMCFS superblock.
 */
struct emmcfs_super_block {
	/** magic */
	__u8	signature[4];		/* VDFS */
	/** vdfs layout version **/
	__u8 layout_version[4];
	/** The version of eMMCFS filesystem */
	struct emmcfs_version	version;
	/** log2 (Block size in bytes) */
	__u8	log_block_size;
	/** log2 (LEB size in bytes) */
	__u8	log_leb_size;

	/** Lotal lebs count */
	__le64	total_leb_count;

	/** Total volume encodings */
	__le64	volume_encodings;

	/** Creation timestamp */
	struct emmcfs_date	creation_timestamp;

	/** 128-bit uuid for volume */
	__u8	volume_uuid[16];
	/** Volume name */
	char	volume_name[16];

	/** --- leb bitmap parameters --- */

	/** TODO */
	__u8	lebs_bm_padding[2];
	/** Log2 for blocks described in one block */
	__le32	lebs_bm_log_blocks_block;
	/** Number of blocks in leb bitmap */
	__le32	lebs_bm_blocks_count;
	/** Number of blocks described in last block */
	__le32	lebs_bm_bits_in_last_block;

	/** File driver git repo branch name */
	char mkfs_git_branch[64];
	/** File driver git repo revision hash */
	char mkfs_git_hash[40];

	/** Total volume sectors count */
	__le64	sectors_per_volume;

	/** Case insensitive mode */
	__u8 case_insensitive;

	/** Read-only image */
	__u8 read_only;

	__le32 log_cell_size;
	/** metadata updating algorithm type */
	__u8 metadata_updating_cow;
	/** Metadata superpage aligment */
	__u8 log_super_page_size;

	__le32 erase_block_size;

	__u8 log_erase_block_size;
	/** Padding */
	__u8	reserved[298];
	/** Checksum */
	__le32	checksum;
} __packed;

/**
 * @brief	The eMMCFS extended superblock.
 */

#define VDFS_TABLES_EXTENTS_COUNT	24
#define VDFS_META_BTREE_EXTENTS		96

struct vdfs_extended_super_block {
	/** File system files count */
	__le64			files_count;
	/** File system folder count */
	__le64			folders_count;
	/** Extent describing the volume */
	struct vdfs_extent	volume_body;
	/** Number of mount operations */
	__le32			mount_counter;
	/* SYNC counter */
	__le32			sync_counter;
	/** Number of umount operations */
	__le32			umount_counter;
	/** Flag indicating signed image */
	__u8 crc;
	/** Debug area position */
	struct vdfs_extent	debug_area;
	/** OTP area position */
	struct vdfs_extent	otp_area;
	__le64 tiny_files_counter;

	/** translation tables extents */
	struct vdfs_extent tables[VDFS_TABLES_EXTENTS_COUNT];
	/** translation tables total blocks count */
	__le32 tables_tbc;
	/** btrees extents */
	struct vdfs_extent meta[VDFS_META_BTREE_EXTENTS];
	/* btrees extents total block count */
	__le32 meta_tbc;
	/* translation tables extention */
	struct vdfs_extent	extension;
	/* small area bitmap */
	struct vdfs_fork	small_area;

	__le32 generation;

	/** Reserved */
	__u8 reserved[314];
	/** Extended superblock checksum */
	__le32			checksum;
} __packed;

/* Catalog btree record types */
#define VDFS_CATALOG_RECORD_DUMMY		0x00
#define VDFS_CATALOG_FOLDER_RECORD		0x01
#define VDFS_CATALOG_FILE_RECORD		0x02
#define VDFS_CATALOG_HLINK_RECORD		0x03

#define VDFS_CATALOG_UNPACK_INODE		0x10

#define VDFS_CATALOG_PTREE_RECORD		0x80
#define VDFS_CATALOG_PTREE_ROOT			0x80
#define VDFS_CATALOG_PTREE_FOLDER		0x81
#define VDFS_CATALOG_PTREE_FILE_INLINE		0x82
#define VDFS_CATALOG_PTREE_FILE_FRAGMENT	0x83
#define VDFS_CATALOG_PTREE_FILE_CHUNKS		0x84
#define VDFS_CATALOG_PTREE_SYMLINK		0x85
#define VDFS_CATALOG_PTREE_BLKDEV		0x86
#define VDFS_CATALOG_PTREE_CHRDEV		0x87
#define VDFS_CATALOG_PTREE_FIFO			0x88
#define VDFS_CATALOG_PTREE_SOCKET		0x89

/**
 * @brief	On-disk structure to hold generic for all the trees.
 */
struct emmcfs_generic_key {
	/** Unique number that identifies structure */
	__u8 magic[4];
	/** Length of tree-specific key */
	__le32 key_len;
	/** Full length of record containing the key */
	__le32 record_len;
} __packed;

/**
 * @brief	On-disk structure to catalog tree keys.
 */
struct emmcfs_cattree_key {
	/** Generic key part */
	struct emmcfs_generic_key gen_key;
	/** Catalog tree record type */
	u8 record_type;
	/** Object id of parent object (directory) */
	__le64 parent_id;
	/** Object's name */
	struct vdfs_unicode_string name;
} __packed;


/** @brief	Btree search key for xattr tree
 */
struct vdfs_xattrtree_key {
	/** Key */
	struct emmcfs_generic_key gen_key;
	/** Object ID */
	__le64 object_id;
	__u8 name_len;
	char name[VDFS_XATTR_NAME_MAX_LEN];
} __packed;

/** @brief	Xattr tree information.
 */
struct vdfs_raw_xattrtree_record {
	/** Key */
	struct vdfs_xattrtree_key key;
	/** Value */
	__le32 value;
} __packed;
/**
 * @brief	On-disk structure to hold file and folder records.
 */
struct vdfs_catalog_folder_record {
	/** Flags */
	__le32	flags;
	__le32	generation;
	/** Amount of files in the directory */
	__le64	total_items_count;
	/** Link's count for file */
	__le64	links_count;
	/** Object id - unique id within filesystem */
	__le64  object_id;
	/** Permissions of record */
	struct emmcfs_posix_permissions	permissions;
	/** Record creation time */
	struct emmcfs_date	creation_time;
	/** Record modification time */
	struct emmcfs_date	modification_time;
	/** Record last access time */
	struct emmcfs_date	access_time;
} __packed;

#define TINY_DATA_SIZE (sizeof(struct vdfs_fork) - sizeof(__le64) - \
	sizeof(u8))

struct vdfs_tiny_file_data {
	u8 len;
	__le64 i_size;
	u8 data[TINY_DATA_SIZE];
} __packed;

struct vdfs_small_file_data {
	__le16 len;
	__le64 i_size;
	__le64 cell;
} __packed;

/**
 * @brief	On-disk structure to hold file records in catalog btree.
 */
struct vdfs_catalog_file_record {
	/** Common part of record (file or folder) */
	struct vdfs_catalog_folder_record common;
	union {
		/** Fork containing info about area occupied by file */
		struct vdfs_fork	data_fork;
		struct vdfs_tiny_file_data tiny;
		struct vdfs_small_file_data small;
	};
} __packed;


/**
 * @brief	On-disk structure to hold file and folder records.
 */
struct vdfs_pack_common_value {
	/** Link's count for file */
	__le64	links_count;
	/* unpacked file size in bytes for files, items count for directories */
	__le64	size;
	/** Object id - unique id within filesystem */
	__le64  object_id;
	/** Permissions of record */
	struct emmcfs_posix_permissions	permissions;
	/** Record creation time */
	struct emmcfs_date	creation_time;
	__le64 xattr_offset;
	__le32 xattr_size;
	__le32 xattr_count;
} __packed;

struct vdfs_packtree_meta_common_ondisk {
	char packtree_layout_version[4];
	__le16 chunk_cnt;  /* chunk count in image */
	__le16 squash_bss; /* squashfs image block size shift */
	__le16 compr_type; /* squashfs image compression type */
	__le32 inodes_cnt; /* number of inodes in the squashfs image */
	__le64 packoffset; /* packtree first bnode offset in file (in pages) */
	__le64 nfs_offset; /* inode finder info for nfs callbacks */
	__le64 xattr_off;  /* xattr area offset in file (in pages) */
	__le64 chtab_off;  /* chunk index table offset */
} __packed;
/**
 * @brief	On-disk structure to hold pack tree insert point value.
 */
struct vdfs_pack_insert_point_value {
	/** Common part of record (file or folder) */
	struct vdfs_pack_common_value common;
	struct vdfs_packtree_meta_common_ondisk pmc;
	__le64 start_ino;	/* start allocated inode no */
	/* key values to finding source expanded squashfs image */
	__le64 source_image_parent_object_id;
	struct vdfs_unicode_string source_image_name;
} __packed;

/**
 * @brief	On-disk structure to hold pack tree dir value.
 */
struct vdfs_pack_dir_value {
	/** Common part of record (file or folder) */
	struct vdfs_pack_common_value common;
} __packed;

/**
 * @brief	On-disk structure to hold pack tree file fragment.
 */
struct vdfs_pack_fragment {
	/* chunk index in chunk offsets table */
	__le32 chunk_index;
	/* offset in unpacked chunk */
	__le32 unpacked_offset;
} __packed;

/* todo set to dynamic allocation */
#define VDFS_PACK_MAX_INLINE_FILE_SIZE	0

/**
 * @brief	On-disk structure to hold pack tree tiny file value.
 */
struct vdfs_pack_file_tiny_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__u8 data[VDFS_PACK_MAX_INLINE_FILE_SIZE];
} __packed;

/**
 * @brief	On-disk structure to hold one-fragment-lenght-file value.
 */
struct vdfs_pack_file_fragment_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	struct vdfs_pack_fragment fragment;
} __packed;

/**
 * @brief	On-disk structure to hold file several-chunks-lenght-file
 *		(chunk count up to VDFS_CHUNK_MAX_COUNT_IN_BNODE) value.
 */
struct vdfs_pack_file_chunk_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__le32 chunk_index;	/* start chunk index */
} __packed;

/**
 * @brief	On-disk structure to hold pack tree symlink value.
 */
struct vdfs_pack_symlink_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__u8 data[VDFS_FILE_NAME_LEN + 1];
} __packed;

/**
 * @brief	On-disk structure to hold pack tree device file value.
 */
struct vdfs_pack_device_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__le32 rdev;
} __packed;

/**
 * @brief	On-disk vdfs pack inode find data structure.
 */
struct vdfs_pack_nfs_item {
	__le32 bnode_id;
	__le32 record_i;
} __packed;

/**
 * @brief	On-disk structure to hold hardlink records in catalog btree.
 */
struct vdfs_catalog_hlink_record {
	/** Id of hardlink within hardlinks area */
	__le64 object_id;
	/** file mode */
	__le16 file_mode;
} __packed;


/** START  ->> EXTENTS OVERFLOW BTREE structuries  --------------------------*/
/** @brief	Extents overflow information.
 */
struct emmcfs_exttree_key {
	/** Key */
	struct emmcfs_generic_key gen_key;
	/** Object ID */
	__le64 object_id;
	/** Block number */
	__le64 iblock;
} __packed;

/** @brief	Extents overflow tree information.
 */
struct emmcfs_exttree_record {
	/** Key */
	struct emmcfs_exttree_key key;
	/** Extent for key */
	struct vdfs_extent lextent;
} __packed;

/** END  -<< EXTENTS OVERFLOW BTREE  BTREE structuries    --------------------*/

/** START  ->> HARD LINKS BTREE structuries --------------------------------- */
/** @brief	hard links btree key.
 */
struct vdfs_hlinktree_key {
	/** Generic key */
	struct emmcfs_generic_key gen_key;
	/** Hard link inode number */
	__le64 inode_ino;
} __packed;

/** @brief	Hard lins tree record
 */
struct vdfs_hdrtree_record {
	/** Hard link tree key */
	struct vdfs_hlinktree_key key;
	/** Hard link tree value (inode & fork) */
	struct vdfs_catalog_file_record hardlink_value;
} __packed;

/** END  -<< HARD LINKS BTREE structuries   -------------------------------- */

/**
 * @brief	On-disk structure to hold essential information about B-tree.
 */
struct vdfs_raw_btree_head {
	/** Magic */
	__u8 magic[4];
	union {
	__le64 full_version;
	__le32 version[2];
	};
	/** The bnode id of root of the tree */
	__le32 root_bnode_id;
	/** Height of the tree */
	__le16 btree_height;
	/** Padding */
	__u8 padding[2];
	/** Starting byte of free bnode bitmap, bitmap follows this structure */
	__u8 bitmap;
} __packed;

/**
 * @brief	On-disk structure representing information about bnode common
 *		to all the trees.
 */
struct vdfs_gen_node_descr {
	/** Magic */
	__u8 magic[4];
	union {
	__le64 full_version;
	__le32 version[2];
	};
	/** Free space left in bnode */
	__le16 free_space;
	/** Amount of records that this bnode contains */
	__le16 recs_count;
	/** Node id */
	__le32 node_id;
	/** Node id of left sibling */
	__le32 prev_node_id;
	/** Node id of right sibling */
	__le32 next_node_id;
	/** Type of bnode node or index (value of enum emmcfs_node_type) */
	__u8 type;
} __packed;

/**
 * @brief	Generic value for index nodes - node id of child.
 */
struct generic_index_value {
	/** Node id of child */
	__le32 node_id;
} __packed;


#endif	/* _LINUX_EMMCFS_FS_H */
