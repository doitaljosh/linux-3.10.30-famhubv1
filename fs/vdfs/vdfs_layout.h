/**
 * VDFS -- Vertically Deliberate improved performance File System
 *
 * Copyright 2013 by Samsung Electronics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
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
#define VDFS_LAYOUT_VERSION			"1002"
#define VDFS_SB_SIGNATURE			"VDFS"
#define EMMCFS_SB_VER_MAJOR	1
#define EMMCFS_SB_VER_MINOR	0

#define EMMCFS_CAT_FOLDER_MAGIC			"rDe"
#define EMMCFS_CAT_FILE_MAGIC			"eFr"
#define VDFS_XATTR_REC_MAGIC			"XAre"
#define VDFS_SNAPSHOT_BASE_TABLE		"CoWB"
#define VDFS_SNAPSHOT_EXTENDED_TABLE		"CoWE"
#define EMMCFS_NODE_DESCR_MAGIC			0x644E		/*"dN"*/
#define EMMCFS_OOPS_MAGIC			"DBGe"

#define EMMCFS_BTREE_HEAD_NODE_MAGIC		"eHND"
#define EMMCFS_BTREE_LEAF_NODE_MAGIC		"NDlf"
#define EMMCFS_BTREE_INDEX_NODE_MAGIC		"NDin"
#define EMMCFS_EXTTREE_KEY_MAGIC		"ExtK"
#define EMMCFS_EXT_OVERFL_LEAF			"NDle"
#define VDFS_HRDTREE_KEY_MAGIC			"HrdL"

#define VDFS_CATTREE_ILINK_MAGIC		"ILnk"

#define VDFS_PACK_METADATA_MAGIC		"PKmT"
#define VDFS_PACK_METADATA_VERSION		"0004"

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

#define VDFS_COMPR_ZIP_FILE_DESCR_MAGIC		"Zip"
#define VDFS_COMPR_GZIP_FILE_DESCR_MAGIC	"Gzp"
#define VDFS_COMPR_LZO_FILE_DESCR_MAGIC		"Lzo"
#define VDFS_COMPR_DESCR_START			'C'
#define VDFS_AUTHE_DESCR_START			'H'
#define VDFS_FORCE_DESCR_START			'F'
#define VDFS_COMPR_EXT_MAGIC			"XT"
#define VDFS_COMPR_LAYOUT_VER			0x0004

#define VERSION_SIZE				(sizeof(u64))
#define VDFS_MOUNT_COUNT(version)	(0xffffffff & (__u32)(version >> 32))
#define VDFS_SYNC_COUNT(version)	(0xffffffff & (__u32)version)

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

#define VDFS_CHUNK_FLAG_UNCOMPR 1
#define VDFS_HASH_LEN	20
#define VDFS_CRYPTED_HASH_LEN	256
#define VDFS_HW_COMPR_PAGE_PER_CHUNK 32
#define VDFS_MIN_LOG_CHUNK_SIZE	12
#define VDFS_MAX_LOG_CHUNK_SIZE 20

/** Special inodes */
enum special_ino_no {
	VDFS_ROOTDIR_OBJ_ID = 0,	/** parent_id of root inode */
	VDFS_ROOT_INO = 1,		/** root inode */
	VDFS_CAT_TREE_INO,		/** catalog tree inode */
	VDFS_FSFILE = VDFS_CAT_TREE_INO,/** First special file */
	VDFS_SPACE_BITMAP_INO,		/** free space bitmap inode */
	VDFS_EXTENTS_TREE_INO,		/** inode bitamp inode number */
	VDFS_FREE_INODE_BITMAP_INO,	/** Free space bitmap inode */
	VDFS_XATTR_TREE_INO,		/** XAttr tree ino */
	VDFS_LSFILE = VDFS_XATTR_TREE_INO,
	VDFS_SNAPSHOT_INO,
	VDFS_ORPHAN_INODES_INO,		/** FIXME remove this line breaks fsck*/
	VDFS_1ST_FILE_INO		/** First file inode */
};

#define VDFS_ROOTDIR_NAME	"root"

#define VDFS_SF_NR	(VDFS_LSFILE - VDFS_FSFILE + 1)
#define VDFS_SF_INDEX(x)	(x - VDFS_FSFILE)


#define VDFS_XATTR_NAME_MAX_LEN 200
#define VDFS_XATTR_VAL_MAX_LEN 200

/**
 * @brief	The eMMCFS stores dates in unsigned 64-bit integer seconds and
 *		unsigned 32-bit integer nanoseconds.
 */
struct vdfs_timespec {
	/** The seconds part of the date */
	__le32	seconds;
	__le32	seconds_high;
	/** The nanoseconds part of the date */
	__le32	nanoseconds;
};

static inline struct vdfs_timespec vdfs_encode_time(struct timespec ts)
{
	return (struct vdfs_timespec) {
		.seconds = cpu_to_le32(ts.tv_sec),
		.seconds_high = cpu_to_le32((u64)ts.tv_sec >> 32),
		.nanoseconds = cpu_to_le32(ts.tv_nsec),
	};
}

static inline struct timespec vdfs_decode_time(struct vdfs_timespec ts)
{
	return (struct timespec) {
		.tv_sec = (long)(le32_to_cpu(ts.seconds) +
			((u64)le32_to_cpu(ts.seconds_high) << 32)),
		.tv_nsec = (long)(le32_to_cpu(ts.nanoseconds)),
	};
}

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
};

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
	__le64	length;
};

/**
 * @brief	File blocks in catalog tree are described by means extent plus
 *		extent iblock.
 */
struct vdfs_iextent {
	/** file data location */
	struct vdfs_extent	extent;
	/** extent start block logical index */
	__le64	iblock;
};

struct vdfs_comp_extent {
	char magic[2];
	__le16 flags;
	__le32 len_bytes;
	__le64 start;
};

struct vdfs_comp_file_descr {
	char magic[4];
	__le16 extents_num;
	__le16 layout_version;
	__le64 unpacked_size;
	__le32 crc;
	__le32 log_chunk_size;
	__le64 pad2; /* Alignment to vdfs_comp_extent size */
};

/** VDFS maintains information about the contents of a file using the
 * emmcfs_fork structure.
 */
#define VDFS_EXTENTS_COUNT_IN_FORK	9
/**
 * @brief	The VDFS fork structure.
 */
struct vdfs_fork {
	/** The size in bytes of the valid data in the fork */
	__le64			size_in_bytes;
	/** The total number of allocation blocks which is
	 * allocated for file system object under last actual
	 * snapshot in this fork */
	__le64			total_blocks_count;
	/** The set of extents which describe file system
	 * object's blocks placement */
	struct vdfs_iextent	extents[VDFS_EXTENTS_COUNT_IN_FORK];
};

/* Snapshots Management */
/** START  ->> SNAPSHOT structuries  -----------------------------------------*/
#define VDFS_SNAPSHOT_MAX_SIZE (16 * 1024 * 1024)

/* Snapshot tables */
#define VDFS_SNAPSHOT_EXT_SIZE		4096
#define VDFS_SNAPSHOT_EXT_TABLES	8
#define VDFS_SNAPSHOT_BASE_TABLES	2

#define VDFS_SNAPSHOT_BASE_TABLE		"CoWB"
#define VDFS_SNAPSHOT_EXTENDED_TABLE		"CoWE"

struct vdfs_translation_record {
	/* logical block  */
	__le64 f_iblock;
	/* phisical block */
	__le64 m_iblock;
};

struct vdfs_snapshot_descriptor {
	/* signature */
	__u8 signature[4];
	/* sync count */
	__le32 sync_count;
	/* mount count */
	__le64 mount_count;
	/* offset to CRC */
	__le64 checksum_offset;
};

struct vdfs_base_table {
	/* descriptor: signature, mount count, sync count and checksumm offset*/
	struct vdfs_snapshot_descriptor descriptor;
	/* last metadata iblock number */
	__le64 last_page_index[VDFS_SF_NR];
	/* offset into translation tables for special files */
	__le64 translation_table_offsets[VDFS_SF_NR];
};

struct vdfs_base_table_record {
	__le64 meta_iblock;
	__le32 sync_count;
	__le32 mount_count;
};

struct vdfs_extended_record {
	/* special file id */
	__le64 object_id;
	/* object iblock */
	__le64 table_index;
	/* meta iblock */
	__le64 meta_iblock;
};

struct vdfs_extended_table {
	struct vdfs_snapshot_descriptor descriptor;
	/* records count in extended table */
	__le32 records_count;
	__le32 pad;
};

/** END  -<< SNAPSHOT structuries  -----------------------------------------*/

/**
 * @brief	The VDFS debug record, described one file system fail
 */
#define DEBUG_FUNCTION_LINE_LENGTH 5
#define DEBUG_FUNCTION_NAME_LENGTH 31

struct vdfs_debug_record {
	/** volume uuid */
	__le64 uuid;
	/** line number */
	__u8 line[DEBUG_FUNCTION_LINE_LENGTH];
	/** Oops function name */
	__u8 function[DEBUG_FUNCTION_NAME_LENGTH];
	/** fail number */
	__le32 fail_number;
	/** error code */
	__le32 error_code;
	/** record timestamp in jiffies */
	__le32 fail_time;
	/** mount count */
	__le32 mount_count;
	/** sync count */
	__le32 sync_count;
};

/**
 * @brief	The eMMCFS snapshot descriptor.
 */
struct vdfs_debug_descriptor {
	/** Signature magic */
	__u8 signature[4];
	/* fail count */
	__le32 record_count;
	/** next_oops offset */
	__le32 offset_to_next_record;
};

/* it is fixed constants, so we can use only digits here */
#define IMAGE_CMD_LENGTH (512 - 4 - 4 - 4)

struct vdfs_volume_begins {
	/** magic */
	__u8	signature[4];		/* VDFS */
	/** vdfs layout version **/
	__u8 layout_version[4];
	/* image was created with command line */
	__u8 command_line[IMAGE_CMD_LENGTH];
	/** Checksum */
	__le32	checksum;
};

/**
 * @brief	The eMMCFS superblock.
 */
struct emmcfs_super_block {
	/** magic */
	__u8	signature[4];		/* VDFS */
	/** vdfs layout version **/
	__u8 layout_version[4];

	/* maximum blocks count on volume */
	__le64	maximum_blocks_count;

	/** Creation timestamp */
	struct vdfs_timespec creation_timestamp;

	/** 128-bit uuid for volume */
	__u8	volume_uuid[16];

	/** Volume name */
	char	volume_name[16];

	/** File driver git repo branch name */
	char mkfs_git_branch[64];
	/** File driver git repo revision hash */
	char mkfs_git_hash[40];

	/** log2 (Block size in bytes) */
	__u8	log_block_size;

	/** Metadata bnode size and alignment */
	__u8	log_super_page_size;

	/** Discard request granularity */
	__u8	log_erase_block_size;

	/** Case insensitive mode */
	__u8 case_insensitive;

	/** Read-only image */
	__u8 read_only;

	__u8 image_crc32_present;

	/*force full decompression enable/disable bit*/
	__u8 force_full_decomp_decrypt;

	/** Padding */
	__u8	reserved[68];

	__le64 image_inode_count;

	__le32 pad;

	/*RSA enctypted hash code of superblock*/
	__u8 sb_hash[VDFS_CRYPTED_HASH_LEN];

	/** Checksum */
	__le32	checksum;
};

/**
 * @brief	The eMMCFS extended superblock.
 */

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
	/** inode numbers generation */
	__le32			generation;
	/** Debug area position */
	struct vdfs_extent	debug_area;
	/** OTP area position */
	struct vdfs_extent	otp_area;
	/* btrees extents total block count */
	__le32			meta_tbc;
	__le32			pad;
	/** translation tables extents */
	struct vdfs_extent	tables;
	/** btrees extents */
	struct vdfs_extent	meta[VDFS_META_BTREE_EXTENTS];
	/* translation tables extention */
	struct vdfs_extent	extension;

	/* volume size in blocks, could be increased at first mounting */
	__le64			volume_blocks_count;

	/** Flag indicating signed image */
	__u8			crc;

	/** 128-bit uuid for volume */
	__u8	volume_uuid[16];

	/** Reserved */
	__u8			reserved[875];

	/** Extended superblock checksum */
	__le32			checksum;
};

struct vdfs_superblock {
	struct emmcfs_super_block sing1;
	struct emmcfs_super_block sign2;
	struct emmcfs_super_block superblock;
	struct vdfs_extended_super_block ext_superblock;
};


struct vdfs_layout_sb {
	struct emmcfs_super_block  _sb1;
	struct emmcfs_super_block  _sb2;
	struct emmcfs_super_block  sb;
	struct vdfs_extended_super_block exsb;
};

struct vdfs_meta_block {
	__le32 magic;
	__le32 sync_count;
	__le32 mount_count;
	__u8 data[4096 - 4 - 8 - 4];
	__le32 crc32;
};

/* Catalog btree record types */
#define VDFS_CATALOG_RECORD_DUMMY		0x00
#define VDFS_CATALOG_FOLDER_RECORD		0x01
#define VDFS_CATALOG_FILE_RECORD		0x02
#define VDFS_CATALOG_HLINK_RECORD		0x03
#define VDFS_CATALOG_DLINK_RECORD		0x04
#define VDFS_CATALOG_ILINK_RECORD		0x05
#define VDFS_CATALOG_LLINK_RECORD		0X06
#define VDFS_CATALOG_UNPACK_INODE		0x10


#define VDFS_CATALOG_RO_IMAGE_ROOT		0x40

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
	__le16 key_len;
	/** Full length of record containing the key */
	__le16 record_len;
};

/**
 * @brief	On-disk structure to catalog tree keys.
 */
struct emmcfs_cattree_key {
	/** Generic key part */
	struct emmcfs_generic_key gen_key;
	/** Object id of parent object (directory) */
	__le64 parent_id;
	/** Object id of child object (file) */
	__le64 object_id;
	/** Catalog tree record type */
	__u8 record_type;
	/** Object's name */
	__u8	name_len;
	char	name[VDFS_FILE_NAME_LEN];
};

#define VDFS_CAT_KEY_MAX_LEN	ALIGN(sizeof(struct emmcfs_cattree_key), 8)

/** @brief	Btree search key for xattr tree
 */
struct vdfs_xattrtree_key {
	/** Key */
	struct emmcfs_generic_key gen_key;
	/** Object ID */
	__le64 object_id;
	__u8 name_len;
	char name[VDFS_XATTR_NAME_MAX_LEN];
};

#define VDFS_XATTR_KEY_MAX_LEN	ALIGN(sizeof(struct vdfs_xattrtree_key), 8)

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
	/** Next inode in orphan list */
	__le64  next_orphan_id;
	/** File mode */
	__le16	file_mode;
	__le16	pad;
	/** User ID */
	__le32	uid;
	/** Group ID */
	__le32	gid;
	/** Record creation time */
	struct vdfs_timespec	creation_time;
	/** Record modification time */
	struct vdfs_timespec	modification_time;
	/** Record last access time */
	struct vdfs_timespec	access_time;
};

/**
 * @brief	On-disk structure to hold file records in catalog btree.
 */
struct vdfs_catalog_file_record {
	/** Common part of record (file or folder) */
	struct vdfs_catalog_folder_record common;
	union {
		/** Fork containing info about area occupied by file */
		struct vdfs_fork	data_fork;
	};
};


struct vdfs_image_install_point {
	struct vdfs_catalog_folder_record common;
	/* allocated inodes count */
	__le32 ino_count;
	/* pad */
	__le16 pad1;
	__u8 pad2;
	/* image file name length */
	__u8	name_len;
	/* image file name */
	__u8	name[VDFS_FILE_NAME_LEN];
	/* installed image parent id */
	__le64 image_parent_id;
	/* catalog tree offset */
	__le64 catalog_tree_offset;
	/* extents overflow tree offset */
	__le64 extents_tree_offset;
	/* extended atribute tree offset */
	__le64 xattr_tree_offset;
};
/**
 * @brief	On-disk structure to hold file and folder records.
 */
struct vdfs_pack_common_value {
	/** Link's count for file */
	__le64	links_count;
	/* unpacked file size in bytes for files, items count for directories */
	__le64	size;
	/** File mode */
	__le16	file_mode;
	__le16	pad;
	/** User ID */
	__le32	uid;
	/** Group ID */
	__le32	gid;
	/** Record creation time */
	struct vdfs_timespec	creation_time;
	__le64 xattr_offset;
	__le32 xattr_size;
	__le32 xattr_count;
};

struct vdfs_packtree_meta_common_ondisk {
	char packtree_layout_version[4];
	__le16 squash_bss; /* squashfs image block size shift */
	__le16 compr_type; /* squashfs image compression type */
	__le32 chunk_cnt;  /* chunk count in image */
	__le32 inodes_cnt; /* number of inodes in the squashfs image */
	__le64 packoffset; /* packtree first bnode offset in file (in pages) */
	__le64 nfs_offset; /* inode finder info for nfs callbacks */
	__le64 xattr_off;  /* xattr area offset in file (in pages) */
	__le64 chtab_off;  /* chunk index table offset */
};

/**
 * @brief	On-disk structure to hold pack tree insert point value.
 */
struct vdfs_pack_insert_point_value {
	/** Common part of record (file or folder) */
	struct vdfs_pack_common_value common;
	struct vdfs_packtree_meta_common_ondisk pmc;
	__le64 start_ino;	/* start allocated inode no */
	/* key values to finding source expanded squashfs image */
	struct {
		__u64	parent_id;
		__u8	name_len;
		char	name[VDFS_FILE_NAME_LEN];
	} source_image;

};
/**
 * @brief	On-disk structure to hold pack tree dir value.
 */
struct vdfs_pack_dir_value {
	/** Common part of record (file or folder) */
	struct vdfs_pack_common_value common;
};

/**
 * @brief	On-disk structure to hold pack tree file fragment.
 */
struct vdfs_pack_fragment {
	/* chunk index in chunk offsets table */
	__le32 chunk_index;
	/* offset in unpacked chunk */
	__le32 unpacked_offset;
};

/* todo set to dynamic allocation */
#define VDFS_PACK_MAX_INLINE_FILE_SIZE	0

/**
 * @brief	On-disk structure to hold pack tree tiny file value.
 */
struct vdfs_pack_file_tiny_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__u8 data[VDFS_PACK_MAX_INLINE_FILE_SIZE];
};

/**
 * @brief	On-disk structure to hold one-fragment-lenght-file value.
 */
struct vdfs_pack_file_fragment_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	struct vdfs_pack_fragment fragment;
};

/**
 * @brief	On-disk structure to hold file several-chunks-lenght-file
 *		(chunk count up to VDFS_CHUNK_MAX_COUNT_IN_BNODE) value.
 */
struct vdfs_pack_file_chunk_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__le32 chunk_index;	/* start chunk index */
};

/**
 * @brief	On-disk structure to hold pack tree symlink value.
 */
struct vdfs_pack_symlink_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__u8 data[VDFS_FILE_NAME_LEN + 1];
};

/**
 * @brief	On-disk structure to hold pack tree device file value.
 */
struct vdfs_pack_device_value {
	/** Common part of all vdfs_pack* records */
	struct vdfs_pack_common_value common;
	__le32 rdev;
	__le32 pad;
};

/**
 * @brief	On-disk vdfs pack inode find data structure.
 */
struct vdfs_pack_nfs_item {
	__le32 bnode_id;
	__le32 record_i;
};

/**
 * @brief	On-disk structure to hold hardlink records in catalog btree.
 */
struct vdfs_catalog_hlink_record {
	/** file mode */
	__le16 file_mode;
	__le16	pad1;
	__le32	pad2;
};

/*
 * On-disk structure for data-link record in catalog btree.
 */
struct vdfs_catalog_dlink_record {
	struct vdfs_catalog_folder_record common;
	__le64 data_inode;
	__le64 data_offset;
	__le64 data_length;
};

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
};

/** @brief	Extents overflow tree information.
 */
struct emmcfs_exttree_record {
	/** Key */
	struct emmcfs_exttree_key key;
	/** Extent for key */
	struct vdfs_extent lextent;
};

/** END  -<< EXTENTS OVERFLOW BTREE  BTREE structuries    --------------------*/

/**
 * @brief	On-disk structure to hold essential information about B-tree.
 */
struct vdfs_raw_btree_head {
	/** Magic */
	__u8 magic[4];
	__le32 version[2];
	/** The bnode id of root of the tree */
	__le32 root_bnode_id;
	/** Height of the tree */
	__le16 btree_height;
	/** Padding */
	__u8 padding[2];
	/** Starting byte of free bnode bitmap, bitmap follows this structure */
	__u8 bitmap[0];
};

/**
 * @brief	On-disk structure representing information about bnode common
 *		to all the trees.
 */
struct vdfs_gen_node_descr {
	/** Magic */
	__u8 magic[4];
	__le32 version[2];
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
	__u32 type;
};

/**
 * @brief	Generic value for index nodes - node id of child.
 */
struct generic_index_value {
	/** Node id of child */
	__le32 node_id;
};

#endif	/* _LINUX_EMMCFS_FS_H */
