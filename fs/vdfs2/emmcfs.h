/**
 * @file	fs/emmcfs/emmcfs.h
 * @brief	Internal constants and data structures for eMMCFS.
 * @date	01/17/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file defines eMMCFS common internal data structures, constants and
 * functions
 *
 * @see		TODO: documents
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef _EMMCFS_EMMCFS_H_
#define _EMMCFS_EMMCFS_H_

#ifndef USER_SPACE
#include <linux/fs.h>
#include <linux/aio.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/pagemap.h>
#include <linux/completion.h>
#include <linux/version.h>
#include <linux/percpu_counter.h>
#include <linux/list.h>
#include <linux/xattr.h>
#include "vdfs_layout.h"
#include "btree.h"
#include "cattree.h"
#include "fsm_btree.h"
#include "debug.h"
#endif

#ifdef CONFIG_VDFS_NOOPTIMIZE
#pragma GCC optimize("O0")
#endif

/*mutex classes*/
#define HL_TREE		1
#define SMALL_AREA	2
#define VDFS_REG_DIR_M	3

#define EMMCFS_CAT_MAX_NAME	255
#define UUL_MAX_LEN 20

struct vdfs_packtree_meta_common {
	__u16 chunk_cnt;  /* chunk count in image */
	__u16 squash_bss; /* squashfs image block size shift */
	__u16 compr_type; /* squashfs image compression type */
	__u32 inodes_cnt; /* number of inodes in the squashfs image */
	__u64 packoffset; /* packtree first bnode offset in file */
	__u64 nfs_offset; /* inode finder info for nfs callbacks */
	__u64 xattr_off;  /* xattr area offset in file (in pages) */
	__u64 chtab_off;  /* chunk index table offset */
};

#define DECOMPRESSOR_ARRAY_LEN 5
#define VDFS_Z_NEED_DICT_ERR	-7
enum compressors_t {
	zlib = 0,
	lzo,
	xz,
	lzma,
	gzip
};
#define TOTAL_PAGES		32
#define TOTAL_PAGES_SHIFT	5

struct ioctl_install_params {
	struct vdfs_packtree_meta_common pmc;
	/* source expanded image file descr */
	int image_fd;
	char dst_dir_name[VDFS_FILE_NAME_LEN + 1];
	char packtree_layout_version[4];
};

struct ioctl_uninstall_params {
	/* key pair for parent directory of installed packtree image */
	__u64 parent_id;
	char name[VDFS_FILE_NAME_LEN + 1];
};

#define VDFS_IOC_GET_INODE_TYPE	_IOR('Q', 1, u_int8_t)
#define VDFS_IOC_GET_PARENT_ID	_IOR('Q', 2, u_int64_t)
#define VDFS_IOC_INSTALL	_IOW('Q', 1, struct ioctl_install_params)
#define VDFS_IOC_UNINSTALL	_IOW('Q', 2, struct ioctl_uninstall_params)

static inline __u32 vdfs_get_packtree_max_record_size(void)
{
	__u32 size = sizeof(struct vdfs_pack_file_tiny_value);

	if (size < sizeof(struct vdfs_pack_file_fragment_value))
		size = sizeof(struct vdfs_pack_file_fragment_value);

	if (size < sizeof(struct vdfs_pack_file_chunk_value))
		size = sizeof(struct vdfs_pack_file_chunk_value);

	if (size < sizeof(struct vdfs_pack_symlink_value))
		size = sizeof(struct vdfs_pack_symlink_value);

	return size + sizeof(struct emmcfs_cattree_key);
}

/* Enabels additional print at mount - how long mount is */
/* #define CONFIG_EMMCFS_PRINT_MOUNT_TIME */


#define EMMCFS_BUG() do { BUG(); } while (0)

#define EMMCFS_BUG_ON(cond) do { \
	BUG_ON((cond)); \
} while (0)

#ifdef CONFIG_VDFS_DEBUG
#define VDFS_DEBUG_BUG_ON(cond) do { \
	BUG_ON((cond)); \
} while (0)
#else
#define VDFS_DEBUG_BUG_ON(cond) do { \
	if (cond) \
		EMMCFS_ERR(#cond); \
} while (0)
#endif

/* macros */
#define VDFS_IS_READONLY(sb) (sb->s_flags & MS_RDONLY)
#define VDFS_SET_READONLY(sb) (sb->s_flags |= MS_RDONLY)

/*#define CONFIG_VDFS_CHECK_FRAGMENTATION*/
/*#define	CONFIG_VDFS_POPO_HELPER*/

extern unsigned int file_prealloc;
extern unsigned int cattree_prealloc;

#define EMMMCFS_VOLUME_START	4


#define VDFS_LINK_MAX 32

#define VDFS_INVALID_BLOCK (~((sector_t) 0xffff))

/** Maximal supported file size. Equal maximal file size supported by VFS */
#define EMMCFS_MAX_FILE_SIZE_IN_BYTES MAX_LFS_FILESIZE

/* The eMMCFS runtime flags */
#define EXSB_DIRTY		0
#define IS_MOUNT_FINISHED	2
#define VDFS_META_CRC		3

/** The VDFS inode flags */
#define HAS_BLOCKS_IN_EXTTREE	1
#define VDFS_IMMUTABLE		2
#ifdef CONFIG_VDFS_QUOTA
#define HAS_QUOTA		3
#endif
#define TINY_FILE		9
#define HARD_LINK		10
#define SMALL_FILE		11
#define ORPHAN_INODE		12

/* Macros for calculating catalog tree expand step size.
 * x - total blocks count on volume
 * right shift 7 - empirical value. For example, on 1G volume catalog tree will
 * expands by 8M one step*/
#define TREE_EXPAND_STEP(x)	(x >> 7)

/* Preallocation blocks amount */
#define FSM_PREALLOC_DELTA	0

/* Length of log buffer */
#define BUFFER_LENGTH 32768
#define MAX_FUNCTION_LENGTH 30
#define EMMCFS_PROC_DIR_NAME "fs/emmcfs"
#define EMMCFS_MAX_STAT_LENGTH			55

#define VDFS_INVALID	(-EINVAL)
#define VDFS_VALID	0

/**
 * @brief		Compare two 64 bit values
 * @param [in]	b	first value
 * @param [in]	a	second value
 * @return		Returns 0 if a == b, 1 if a > b, -1 if a < b
 */
static inline int cmp_2_le64(__le64 a, __le64 b)
{
	if (le64_to_cpu(a) == le64_to_cpu(b))
		return 0;
	else
		return (le64_to_cpu(a) > le64_to_cpu(b)) ? 1 : -1;
}

/* Flags that should be inherited by new inodes from their parent. */
#define EMMCFS_FL_INHERITED	0
/* force read-only ioctl command */
#define VDFS_IOC_FORCE_RO		_IOR('A', 1, long)
/* ioctl command code for open_count fetch */
#define	VDFS_IOC_GET_OPEN_COUNT	_IOR('C', 1, long)
/* ioctl command codes for high priority tasks */
#define VDFS_IOC_GRAB			_IOR('D', 1, long)
#define VDFS_IOC_RELEASE		_IOR('E', 1, long)
#define VDFS_IOC_RESET			_IOR('F', 1, long)
#define VDFS_IOC_GRAB2PARENT		_IOR('G', 1, long)
#define VDFS_IOC_RELEASE2PARENT		_IOR('H', 1, long)


#ifndef USER_SPACE

#define EMMCFS_LOG_BITS_PER_PAGE (3 + PAGE_SHIFT)
#define VDFS_BITS_PER_PAGE(magic_size) ((1 << EMMCFS_LOG_BITS_PER_PAGE) -\
		((magic_size + CRC32_SIZE) << 3))
#define EMMCFS_BITMAP_PAGE_MASK (((__u64) 1 << EMMCFS_LOG_BITS_PER_PAGE) - 1)

#define EMMCFS_LOG_HLINKS_PER_PAGE 2
#define EMMCFS_LOG_ONE_HLINK_DATA_SZ (PAGE_SHIFT - EMMCFS_LOG_HLINKS_PER_PAGE)
#define EMMCFS_HLINK_IN_PAGE_MASK (((hlink_id_t) \
			1 << EMMCFS_LOG_HLINKS_PER_PAGE) - 1)

struct packtrees_list {
	/* pack tree multi-access protection */
	struct mutex lock_pactree_list;

	struct list_head list;
};

struct vdfs_small_files {
	int cell_size;
	int log_cell_size;
	struct inode *bitmap_inode;
	struct inode *area_inode;
};

struct vdfs_int_container {
	int value;
	struct list_head list;
};

struct vdfs_high_priority {
	struct list_head high_priority_tasks;
	struct completion high_priority_done;
	struct mutex	task_list_lock; /* protect high_priority_tasks list */
};

#endif /*USER_SPACE*/
#define QUOTA_XATTR "user.VDFS_QUOTA"
#define QUOTA_HAS_XATTR "user.VDFS_QUOTA_HAS"
#define QUOTA_XATTR_LEN strlen(QUOTA_XATTR)
#define INITIAL_QUOTA_ARRAY_SIZE 10
#ifndef USER_SPACE
static inline int is_quota_xattr(const char *attr)
{
	if (strlen(attr) != QUOTA_XATTR_LEN)
		return 0;

	return !memcmp(attr, QUOTA_XATTR, strlen(attr));
}

struct vdfs_quota {
	__u64 has;
	__u64 max;
	__u64 ino;
};

/** @brief	Maintains private super block information.
 */
struct vdfs_sb_info {
	/** The VFS super block */
	struct super_block	*sb;
	/** The page that contains superblocks */
	struct page *superblocks;
	/** The page that contains superblocks copy */
	struct page *superblocks_copy;
	/** The eMMCFS mapped raw superblock data */
	void *raw_superblock;
	/** The eMMCFS mapped raw superblock copy data */
	void *raw_superblock_copy;
	/** The page that contains debug area */
	struct page **debug_pages;
	/** Mapped debug area */
	void *debug_area;
	/** The eMMCFS flags */
	unsigned long flags;
	/** Allocated block size in bytes */
	unsigned int		block_size;
	/** Allocated block size shift  */
	unsigned int		block_size_shift;
	/** Allocated block size mask for offset  */
	unsigned int		offset_msk_inblock;
	/** Size of LEB in blocks */
	unsigned int		log_blocks_in_leb;
	/* log blocks in page  */
	unsigned int		log_blocks_in_page;
	/** Log sector per block */
	unsigned int		log_sectors_per_block;
	/** The eMMCFS mount options */
	unsigned int		mount_options;
	/** Total block count in the volume */
	unsigned long long	total_leb_count;
	/** The current value of free block count on the whole eMMCFS volume */
	struct percpu_counter	free_blocks_count;
	/** The files count on the whole eMMCFS volume */
	unsigned long long	files_count;
	/** The folders count on the whole eMMCFS volume */
	unsigned long long	folders_count;
	/** 64-bit uuid for volume */
	u64			volume_uuid;
	/** How many blocks in the bnode */
	unsigned long		btree_node_size_blks;
	/** Catalog tree in memory */
	struct vdfs_btree	*catalog_tree;
	/** Maximum value of catalog tree height */
	int			max_cattree_height;
	/** Extents overflow tree */
	struct vdfs_btree	*extents_tree;
	/** Hard link tree*/
	struct vdfs_btree	*hardlink_tree;
	/** Xattr tree */
	struct vdfs_btree	*xattr_tree;
	/** Number of blocks in LEBs bitmap */
	__le64		lebs_bm_blocks_count;
	/** Log2 for blocks described in one block */
	__le32		lebs_bm_log_blocks_block;
	/** Number of blocks described in last block */
	__le32		lebs_bm_bits_in_last_block;
	u32 erase_block_size;
	u8 log_erase_block_size;
	u32 erase_block_size_in_blocks;
	u8 log_erase_block_size_in_blocks;
	u8 log_block_size;

	u32 *erase_blocks_counters;

#ifdef CONFIG_VDFS_QUOTA
	int quota_first_free_index;
	int quota_current_size;
	struct vdfs_quota *quotas;
#endif

	/** Free space bitmap information */
	struct {
		struct inode *inode;
		atomic64_t last_used;
	} free_inode_bitmap;

	atomic64_t tiny_files_counter;
	/** Free space management */
	struct emmcfs_fsm_info *fsm_info;

	/** Snapshot manager */
	struct vdfs_snapshot_info *snapshot_info;

	/** Circular buffer for logging */
	struct emmcfs_log_buffer *buffer;

	/** Number of sectors per volume */
	__le64	sectors_per_volume;

	/** log flash superpage size */
	unsigned int log_super_page_size;
#ifdef CONFIG_VDFS_STATISTIC
	/** Number of written pages */
	__le64	umount_written_bytes;
#endif
	int bugon_count;
	/* opened packtree list */
	struct packtrees_list packtree_images;

	/* decompression type : hardware or software */
	int use_hw_decompressor;

	struct vdfs_small_files *small_area;
	struct vdfs_high_priority high_priority;
	/* dirty inodes head list*/
	spinlock_t dirty_list_lock;
	struct list_head dirty_list_head;
	char umount_time;

	struct vdfs_proc_dir_info *proc_info;

	/* new files mode mask, filled if fmask mount option is specifided */
	umode_t fmask;

	/* new directory mode mask, filled if dmask mount option
	 * is specifided */
	umode_t dmask;
};

#endif

/** @brief	Current extent information.
 */
struct vdfs_extent_info {
	/** physical start block of the extent */
	u64 first_block;
	/** logical start block of the extent  */
	sector_t iblock;
	/** block count in the extent*/
	u32 block_count;
};

struct vdfs_runtime_extent_info {
	/** logical start block of the extent  */
	sector_t iblock;
	sector_t alloc_hint;
	/** block count in the extent*/
	u32 block_count;
	struct list_head list;
};

/** @brief	Current fork information.
 */
struct vdfs_fork_info {
	/**
	 * The total number of allocation blocks which are
	 * allocated for file system object described by this fork */
	u32 total_block_count;
	/**
	 * Number of blocks,which are preallocated by free space manager for
	 * file system object described by this fork */
	__u32 prealloc_block_count;
	/* Preallocated space first block physical number */
	sector_t prealloc_start_block;
	/**
	 * The set of extents which describe file system object's blocks
	 * placement */
	struct vdfs_extent_info extents[VDFS_EXTENTS_COUNT_IN_FORK];
	/** Used extents */
	unsigned int used_extents;
};

#ifndef USER_SPACE

struct chunk_info {
	__u64 data_start;	/* file offset in bytes of chunk */
	__u32 length;		/* length in bytes of chunk */
	__u8  compressed;	/* is chunk compressed or not */
};

/** @brief	vdfs pack insert point info.
 */
struct vdfs_pack_insert_point_info {
	/* start allocated inode no */
	__u64 start_ino;
	struct vdfs_packtree_meta_common pmc;
	/* key values to finding source expanded squashfs image */
	__u64 source_image_parent_object_id;
	struct vdfs_unicode_string source_image_name;
	char packtree_layout_version[4];
};

/**
 * @brief	vdfs pack file tiny info.
 */
struct vdfs_pack_file_tiny_info {
	__u8 data[VDFS_PACK_MAX_INLINE_FILE_SIZE];
};

/**
 * @brief	vdfs pack file fragment info.
 */
struct vdfs_pack_file_fragment_info {
	/* chunk number */
	__u32 chunk_num;
	/* offset in unpacked chunk */
	__u32 unpacked_offset;
};

/**
 * @brief	vdfs pack file chunk info.
 */
struct vdfs_pack_file_chunk_info {
	/* index of the first chunk of this file */
	__u32 chunk_index;
};

/**
 * @brief	vdfs pack symlink info
 */
struct vdfs_pack_symlink_info {
	__u8 *data;
};

/**
 * @brief	vdfs pack inode find data structure.
 */
struct vdfs_pack_nfs_info {
	__u32 bnode_id;
	__u32 rec_offset;
};

/* packtree parameters */
struct packtree_params {
	struct vdfs_pack_insert_point_info packtree_info;
};

/** @brief	Maintains free space management info.
 */
struct emmcfs_fsm_info {
	/* Superblock info structure pointer */
	struct vdfs_sb_info *sbi;
	/** Free space start (in blocks)*/
	__u32		free_space_start;
	/** LEB state bitmap pages */
	struct page	**pages;
	/** TODO */
	__u32		page_count;
	/** Mapped pages (ptr to LEB bitmap)*/
	void		*data;
	/** Bitmap length in bytes */
	__u64		length_in_bytes;
	/** Lock for LEBs bitmap  */
	struct mutex	lock;
	/** Free space bitmap inode (VFS inode) */
	struct inode	*bitmap_inode;
	/** Lock for FSM B-tree */
	struct  emmcfs_fsm_btree tree;
};

struct vdfs_tiny_file_data_info {
	u8 len;
	u64 i_size;
	u8 data[TINY_DATA_SIZE];
};

struct vdfs_small_file_data_info {
	u16 len;
	u64 i_size;
	u64 cell;
};

/** @brief	Maintains private inode info.
 */
struct emmcfs_inode_info {
	/** VFS inode */
	struct inode	vfs_inode;
	__u8 record_type;
	/** The inode fork information */

	union {
		struct vdfs_fork_info fork;
		struct vdfs_tiny_file_data_info tiny;
		struct vdfs_small_file_data_info small;
		struct {
			struct {
				__u64 offset;
				__u32 size;
				__u32 count;
			} xattr;
			union {
				struct vdfs_pack_insert_point_info root;
				struct vdfs_pack_file_tiny_info tiny;
				struct vdfs_pack_file_fragment_info frag;
				struct vdfs_pack_file_chunk_info chunk;
				struct vdfs_pack_symlink_info symlink;
			};
			struct installed_packtree_info *tree_info;
		} ptree;
	};
	/* runtime fork */
	struct list_head runtime_extents;
	/** Truncate_mutex is for serializing emmcfs_truncate_blocks() against
	 *  emmcfs_getblock()
	 */
	struct mutex truncate_mutex;
	/* TODO - put name-par_id and hlink_id under union */
	/** Name information */
	char *name;
	/** Parent ID */
	__u64 parent_id;
	/** Inode flags */
	long unsigned int flags;
	struct {
		/** The bnode ID */
		__u32 bnode_id;
		/** Position */
		__s32 pos;
	} bnode_hint;

	struct list_head dirty_list;
	int is_in_dirty_list;
	/** difference between file_open() and file_release() calls */
	atomic_t open_count;
#ifdef CONFIG_VDFS_QUOTA
	int quota_index;
#endif
};

struct vdfs_snapshot_info {
	/** Lock metadata update */
	struct mutex insert_lock;
	/** Transaction tree rb-tree root */

	/** Transaction lock */
	struct rw_semaphore transaction_lock;
	/* snapshot tables protections */
	struct rw_semaphore tables_lock;
	spinlock_t task_log_lock;

	/** Task log rb-tree root */
	struct rb_root task_log_root;

	/* current snapshot state : describe current and next snapshot */
	unsigned long *snapshot_bitmap;
	/* next snapshot state */
	unsigned long *next_snapshot_bitmap;
	/* moved iblocks bitmap */
	unsigned long *moved_iblock;

	/* sizeof snapshot_bitmap & next_snapshot_bitmap in bits */
	unsigned long bitmap_size_in_bits;
	/* last allocation place for btree */
	unsigned long int hint_btree;
	/* last allocation place for bitmaps */
	unsigned long int hint_bitmaps;
	/* --------------------- */
	/*tables pages count */
	unsigned int tables_pages_count;
	/* base table pages */
	struct page **base_table;
	/* vmaped base table */
	void *base_t;
	/* extended table page */
	struct page *extended_table[1];
	/* mapped exteded table */
	void *extended_t;

	__u8 exteneded_table_count;

	/* first free sector in snapshot area */
	sector_t isector;

	__u32 sync_count;
	/* flag - use base table or expand table */
	__u8 use_base_table;
	/* dirty pages ammount */
	unsigned int dirty_pages_count;

	int flags;

};

struct vdfs_indirect_key {
	/** generation */
	__le32 generation;
	/** ino */
	__le64 ino;
	/** to determine is it a packtree inode */
	__u8   inode_type;
};

/** @brief	Structure for log buffer data.
 */
struct emmcfs_log_buffer_data {
	/** Functioncall name */
	char name[MAX_FUNCTION_LENGTH];
	/** Process id called this function */
	int pid;
	/** TODO */
	unsigned int nest;
	/** TODO */
	unsigned int mutex;
	/** TODO */
	int ret_code;
	/** Time */
	struct timeval time;
};

/** @brief	Task logging.
 */
struct vdfs_task_log {
	/** Task pid */
	int pid;
	/** Task hold r-lock n times */
	unsigned int r_lock_count;
	/** Node definition for inclusion in a red-black tree */
	struct rb_node node;
};

enum snapshot_table_type {
	SNAPSHOT_BASE_TABLE,
	SNAPSHOT_EXT_TABLE
};


struct vdfs_wait_list {
	struct list_head list;
	struct completion wait;
	sector_t number;
};

/**  @brief	struct for writeback function
 *
 */
struct vdfs_mpage_data {
	struct bio *bio;
	sector_t last_block_in_bio;
};

/**
 * @brief		Get eMMCFS inode info structure from
 *			encapsulated inode.
 * @param [in]	inode	VFS inode
 * @return		Returns pointer to eMMCFS inode info data structure
 */
static inline struct emmcfs_inode_info *EMMCFS_I(struct inode *inode)
{
	return container_of(inode, struct emmcfs_inode_info, vfs_inode);
}

/**
 * @brief		Get eMMCFS superblock from VFS superblock.
 * @param [in]	sb	The VFS superblock
 * @return		Returns eMMCFS run-time superblock
 */
static inline struct vdfs_sb_info *VDFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/**
 * @brief		Calculates count of blocks,
 *			necessary for store inode->i_size bytes.
 * @param [in]	inode	VFS inode
 * @return		Returns pointer to eMMCFS inode info data structure
 */
static inline __u32 inode_size_to_blocks(struct inode *inode)
{
	return (i_size_read(inode) + VDFS_SB(inode->i_sb)->block_size - 1) >>
		VDFS_SB(inode->i_sb)->block_size_shift;
}

static inline int emmcfs_produce_err(struct vdfs_sb_info *sbi)
{
	if (sbi->bugon_count <= 0) {
		BUG_ON(sbi->bugon_count < 0);
		sbi->bugon_count--;
		return -ERANGE;
	}
	sbi->bugon_count--;

	return 0;
}

/**
 * @brief	save function name and error code to oops area (first 1K of
 * a volume).
 */
int vdfs_log_error(struct vdfs_sb_info *sbi, unsigned int ,
		const char *name, int err);

/**
 * @brief		wrapper : write function name and error code on disk in
 *			debug area
 * @param [in]	sbi	Printf format string.
 * @return	void
 */
#define VDFS_LOG_ERROR(sbi, err) vdfs_log_error(sbi, __LINE__, __func__, err)



/**
 * @brief		Validate fork.
 * @param [in] fork	Pointer to the fork for validation
 * @return		Returns 1 if fork is valid, 0 in case of wrong fork
 * */
static inline int is_fork_valid(const struct vdfs_fork *fork)
{
	if (!fork)
		goto ERR;
	if (fork->magic != EMMCFS_FORK_MAGIC)
		goto ERR;

	return 1;
ERR:
	printk(KERN_ERR "fork is invalid");
	return 0;
}

/**
 * @brief		Set flag in superblock.
 * @param [in]	sbi	Superblock information
 * @param [in]	flag	Value to be set
 * @return	void
 */
static inline void set_sbi_flag(struct vdfs_sb_info *sbi, int flag)
{
	set_bit(flag, &sbi->flags);
}

/**
 * @brief		Check flag in superblock.
 * @param [in]	sbi	Superblock information
 * @param [in]	flag	Flag to be checked
 * @return		Returns flag value
 */
static inline int is_sbi_flag_set(struct vdfs_sb_info *sbi, int flag)
{
	return test_bit(flag, &sbi->flags);
}

/**
 * @brief		Clear flag in superblock.
 * @param [in]	sbi	Superblock information
 * @param [in]	flag	Value to be clear
 * @return	void
 */
static inline void clear_sbi_flag(struct vdfs_sb_info *sbi, int flag)
{
	clear_bit(flag, &sbi->flags);
}

/**
 * @brief		Set flag in emmcfs inode info
 * @param [in]	sbi	VFS inode
 * @param [in]	flag	flag to set
 * @return		Return previous flag value
 */
static inline int set_vdfs_inode_flag(struct inode *inode, int flag)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	return test_and_set_bit(flag, &inode_info->flags);
}

static inline struct inode *INODE(struct kiocb *iocb)
{
	return iocb->ki_filp->f_path.dentry->d_inode;
}

/**
 * @brief		Set flag in emmcfs inode info
 * @param [in]	sbi	VFS inode
 * @param [in]	flag	Flag to be checked
 * @return		Returns flag value
 */
static inline int is_vdfs_inode_flag_set(struct inode *inode, int flag)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	return test_bit(flag, &inode_info->flags);
}

/**
 * @brief		Clear flag in emmcfs inode info
 * @param [in]	sbi	VFS inode
 * @param [in]	flag	Flag to be cleared
 * @return		Returns flag value
 */
static inline int clear_vdfs_inode_flag(struct inode *inode, int flag)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	return test_and_clear_bit(flag, &inode_info->flags);
}


/**
 * @brief		Copy layout fork into run time fork.
 * @param [out]	rfork	Run time fork
 * @param [in]	lfork	Layout fork
 * @return	void
 */
static inline void emmcfs_lfork_to_rfork(struct vdfs_fork *lfork,
		struct vdfs_fork_info *rfork)
{
	unsigned i;
	/* Caller must check fork, call is_fork_valid(struct emmcfs_fork *) */
	/* First blanked extent means - no any more valid extents */

	rfork->used_extents = 0;
	for (i = 0; i < VDFS_EXTENTS_COUNT_IN_FORK; ++i) {
		struct vdfs_iextent *lextent;

		lextent = &lfork->extents[i];

		rfork->extents[i].first_block =
				le64_to_cpu(lextent->extent.begin);
		rfork->extents[i].block_count =
				le32_to_cpu(lextent->extent.length);
		rfork->extents[i].iblock = le64_to_cpu(lextent->iblock);
		if (rfork->extents[i].first_block)
			rfork->used_extents++;
	}
}

/**
 * @brief		Get current time for inode.
 * @param [in]	inode	The inode for which current time will be returned
 * @return		Time value for current inode
 */
static inline struct timespec emmcfs_current_time(struct inode *inode)
{
	return (inode->i_sb->s_time_gran < NSEC_PER_SEC) ?
		current_fs_time(inode->i_sb) : CURRENT_TIME_SEC;
}

/**
 * @brief		returns log blocks per page.
 * @param [in]	sb	Superblock structure.
 * @return		log blocks per page
 */
static inline int log_blocks_per_page(struct super_block *sb)
{
	return PAGE_SHIFT - VDFS_SB(sb)->block_size_shift;
}

/**
 * @brief		returns log sectors per block.
 * @param [in]	sb	Superblock structure.
 * @return		log sector per block.
 */
static inline int log_sectors_per_block(struct super_block *sb)
{
	return VDFS_SB(sb)->block_size_shift - SECTOR_SIZE_SHIFT;
}

/* super.c */

int vdfs_sync_fs(struct super_block *sb, int wait);

int vdfs_sync_first_super(struct vdfs_sb_info *sbi);
int vdfs_sync_second_super(struct vdfs_sb_info *sbi);

void vdfs_dirty_super(struct vdfs_sb_info *sbi);
void vdfs_init_inode(struct emmcfs_inode_info *inode);
/** todo move to btree.c ?
 * @brief	The eMMCFS B-tree common constructor.
 */
int fill_btree(struct vdfs_sb_info *sbi,
		struct vdfs_btree *btree, struct inode *inode);

/**
 * @brief	Function removes superblock from writeback thread queue.
 */
void vdfs_remove_sb_from_wbt(struct super_block *sb);

/* inode.c */
static inline void vdfs_put_inode_into_dirty_list(struct inode *inode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	spin_lock(&sbi->dirty_list_lock);
	if (!EMMCFS_I(inode)->is_in_dirty_list) {
		list_add(&EMMCFS_I(inode)->dirty_list, &sbi->dirty_list_head);
		EMMCFS_I(inode)->is_in_dirty_list = 1;
	}
	spin_unlock(&sbi->dirty_list_lock);
}
int vdfs_mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc);
/**
 * @brief	Checks and copy layout fork into run time fork.
 */
int vdfs_parse_fork(struct inode *inode, struct vdfs_fork *lfork);

/**
 * @brief	Form layout fork from run time fork.
 */
void vdfs_form_fork(struct vdfs_fork *lfork, struct inode *inode);

/**
 * @brief	Translation logical numbers to physical.
 */
int vdfs_get_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create);
int vdfs_get_block_da(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create);
int vdfs_get_int_block(struct inode *inode, sector_t iblock,
	struct buffer_head *bh_result, int create, int da);
/**
 * @brief	TODO Check it!!! Find old block in snapshots.
 */
sector_t emmcfs_find_old_block(struct emmcfs_inode_info *inode_info,
				sector_t iblock,  __u32 *max_blocks);

/**
 * @brief	Get free inode index[es].
 */
int vdfs_get_free_inode(struct vdfs_sb_info *sbi, ino_t *i_ino, int count);

/**
 * @brief	Free several inodes.
 */
int vdfs_free_inode_n(struct vdfs_sb_info *sbi, __u64 inode_n, int count);
int vdfs_update_metadata(struct vdfs_sb_info *sbi);
/**
 * @brief	Write inode to bnode.
 */
int emmcfs_write_inode_to_bnode(struct inode *inode);

/**
 * @brief	Propagate flags from inode i_flags to EMMCFS_I(inode)->flags.
 */
void vdfs_get_vfs_inode_flags(struct inode *inode);

/**
 * @brief	Set vfs inode i_flags according to EMMCFS_I(inode)->flags.
 */
void vdfs_set_vfs_inode_flags(struct inode *inode);

/**
 * @brief		Method to look up an entry in a directory.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
struct dentry *emmcfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags);
#else
struct dentry *emmcfs_lookup(struct inode *dir, struct dentry *dentry,
						struct nameidata *nd);
#endif

/**
 * @brief			Method to read inode.
 */
struct inode *get_inode_from_record(struct vdfs_cattree_record *record);
struct inode *vdfs_special_iget(struct super_block *sb, unsigned long ino);
struct inode *get_root_inode(struct vdfs_btree *tree);
int get_iblock_extent(struct inode *inode, sector_t iblock,
		struct vdfs_extent_info *result, sector_t *hint_block);
ssize_t vdfs_gen_file_aio_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos);
void vdfs_free_reserved_space(struct inode *inode, sector_t iblocks_count);
/* options.c */

/**
 * @brief	Parse eMMCFS options.
 */
int emmcfs_parse_options(struct super_block *sb, char *input);

/* btree.c */
int check_bnode_reserve(struct vdfs_btree *btree);
u16 emmcfs_btree_get_height(struct vdfs_btree *btree);
/**
 * @brief	Get next B-tree record transparently to user.
 */
void *emmcfs_get_next_btree_record(struct emmcfs_bnode **__bnode, int *index);

/**
 * @brief	B-tree verifying
 */
int emmcfs_verify_btree(struct vdfs_btree *btree);

/* bnode.c */

/**
 * @brief	Get from cache or create page and buffers for it.
 */
struct page *emmcfs_alloc_new_page(struct address_space *, pgoff_t);

struct page *emmcfs_alloc_new_signed_page(struct address_space *mapping,
					     pgoff_t page_index);
long emmcfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len);
/**
 * @brief	Build free bnode bitmap runtime structure
 */
struct emmcfs_bnode_bitmap *build_free_bnode_bitmap(void *data,
		__u32 start_bnode_id, __u64 size_in_bytes,
		struct emmcfs_bnode *host_bnode);

#if defined(CONFIG_VDFS_META_SANITY_CHECK)
void  bnode_sanity_check(struct emmcfs_bnode *bnode);
void  meta_sanity_check_bnode(void *bnode_data, struct vdfs_btree *btree,
		pgoff_t index);
#endif
/**
 * @brief	Clear memory allocated for bnode bitmap.
 */
void emmcfs_destroy_free_bnode_bitmap(struct emmcfs_bnode_bitmap *bitmap);

/* cattree.c */

/**
 * @brief	Catalog tree key compare function for case-sensitive usecase.
 */
int emmcfs_cattree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);

/**
 * @brief	Catalog tree key compare function for case-insensitive usecase.
 */
int emmcfs_cattree_cmpfn_ci(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);

/**
 * @brief	Interface to search specific object (file) in the catalog tree.
 */
int emmcfs_cattree_find_old(struct vdfs_btree *tree,
		__u64 parent_id, const char *name, int len,
		struct emmcfs_find_data *fd, enum emmcfs_get_bnode_mode mode);


/**
 * @brief	Allocate key for catalog tree record.
 */
struct emmcfs_cattree_key *emmcfs_alloc_cattree_key(int name_len,
		u8 record_type);

/**
 * @brief	Fill already allocated key with data.
 */
void emmcfs_fill_cattree_key(struct emmcfs_cattree_key *fill_key, u64 parent_id,
		const char *name, unsigned int len);

/**
 * @brief	Fill already allocated value area (file or folder) with data
 *		from VFS inode.
 */
int emmcfs_fill_cattree_value(struct inode *inode, void *value_area);

/**
 * @brief	Add new object into catalog tree.
 */
int emmcfs_add_new_cattree_object(struct inode *inode, u64 parent_id,
		struct qstr *name);

/**
 * @brief	Get the object from catalog tree basing on hint.
 */
int emmcfs_get_from_cattree(struct emmcfs_inode_info *inode_i,
		struct emmcfs_find_data *fd, enum emmcfs_get_bnode_mode mode);

/* cattree-helper.c */
int get_record_type_on_mode(struct inode *inode, u8 *record_type);

int vdfs_remove_indirect_index(struct vdfs_btree *tree, struct inode *inode);

int vdfs_add_indirect_index(struct vdfs_btree *tree, __le32 generation,
		__u64 ino, struct emmcfs_cattree_key *key);

struct inode *vdfs_get_indirect_inode(struct vdfs_btree *tree,
		struct vdfs_indirect_key *indirect_key);
/* extents.c */

int vdfs_exttree_get_extent(struct vdfs_sb_info *sbi, __u64 object_id,
		sector_t iblock, struct vdfs_extent_info *result);
int emmcfs_extree_insert_extent(struct vdfs_sb_info *sbi,
		unsigned long object_id, struct vdfs_extent_info *extent);
int emmcfs_exttree_cache_init(void);
void emmcfs_exttree_cache_destroy(void);
struct emmcfs_exttree_key *emmcfs_get_exttree_key(void);
void emmcfs_put_exttree_key(struct emmcfs_exttree_key *key);
int vdfs_runtime_extent_add(sector_t iblock, sector_t alloc_hint,
		struct list_head *list);
int vdfs_runtime_extent_del(sector_t iblock,
		struct list_head *list) ;
u32 vdfs_runtime_extent_count(struct list_head *list);
sector_t vdfs_truncate_runtime_blocks(sector_t new_size_iblocks,
		struct list_head *list);
u32 vdfs_runtime_extent_exists(sector_t iblock, struct list_head *list);
/**
 * @brief	Add newly allocated blocks chunk to extents overflow area.
 */
int emmcfs_exttree_add(struct vdfs_sb_info *sbi, unsigned long object_id,
		struct vdfs_extent_info *extent);


/**
 * @brief	Expand file in extents overflow area.
 */
sector_t emmcfs_exttree_add_block(struct emmcfs_inode_info *inode_info,
				  sector_t iblock, int cnt);
/**
 * @brief	Logical to physical block numbers translation for blocks
 *		which placed in extents overflow.
 */
sector_t vdfs_exttree_get_block(struct emmcfs_inode_info *inode_info,
				  sector_t iblock, __u32 *max_blocks);
/**
 * @brief	Extents tree key compare function.
 */
int emmcfs_exttree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);

/* fsm.c */

/**
 * @brief	Build free space management.
 */
int emmcfs_fsm_build_management(struct super_block *);

/**
 * @brief	Destroy free space management.
 */
void emmcfs_fsm_destroy_management(struct super_block *);

/**
 * @brief	Get free space block chunk from tree and update free
 *		space manager bitmap.
 */
__u64 emmcfs_fsm_get_free_block(struct vdfs_sb_info *sbi, __u64 block_offset,
		__u32 *length_in_blocks, int alignment, int lock, int da);
/**
 * @brief	Function is the fsm_free_block_chunk wrapper. Pust free space
 *		chunk to tree and updates free space manager bitmap.
 *		This function is called during truncate or unlink inode
 *		processes.
 */
int emmcfs_fsm_put_free_block(struct emmcfs_inode_info *inode_info,
		__u64 offset, __u32 length_in_blocks, int da);
/**
 * @brief	Puts preallocated blocks back to the tree.
 */
void emmcfs_fsm_discard_preallocation(struct emmcfs_inode_info *inode_info);

/**
 * @brief	Function is the fsm_free_block_chunk wrapper.
 *		It is called during evict inode or process orphan inodes
 *		processes (inode already doesn't exist, but exttree inode blocks
 *		still where).
 */
int emmcfs_fsm_free_exttree_extent(struct vdfs_sb_info *sbi,
	__u64 offset, __u32 length);

/* fsm_btree.c */

/**
* @brief	Function used to initialize FSM B-tree.
*/
void emmcfs_init_fsm_btree(struct  emmcfs_fsm_btree *btree);

/**
 * @brief	Initialize the eMMCFS FSM B-tree cache.
 */
int  emmcfs_fsm_cache_init(void);

/**
 * @brief	Destroy the eMMCFS FSM B-tree cache.
 */
void emmcfs_fsm_cache_destroy(void);

/* file.c */

/**
 * @brief	This function is called when a file is opened with O_TRUNC or
 *		truncated with truncate()/ftruncate() system calls.
 *		1) It truncates exttree extents according to new file size.
 *		2) If fork internal extents contains more extents than new file
 *		size in blocks, internal fork is also truncated.
 */
int vdfs_truncate_blocks(struct inode *inode, loff_t new_size);

/**
 * @brief	This function is called during inode delete/evict VFS call.
 *		Inode internal fork extents have been already cleared, it's
 *		time to clear exttree extents for this inode and, finally,
 *		remove exttree orphan inode list record for this inode.
 */
int vdfs_fsm_free_exttree(struct vdfs_sb_info *sbi, __u64 ino);

/* snapshot.c */

#ifdef CONFIG_VDFS_DEBUG
void vdfs_check_moved_iblocks(struct vdfs_sb_info *sbi, struct page **pages,
		int page_count);
#endif

int emmcfs_build_snapshot_manager(struct vdfs_sb_info *sbi);
void emmcfs_destroy_snapshot_manager(struct vdfs_sb_info *sbi);
int vdfs_start_transaction(struct vdfs_sb_info *sbi);
int vdfs_stop_transaction(struct vdfs_sb_info *sbi);
void vdfs_add_chunk_bitmap(struct vdfs_sb_info *sbi, struct page *page,
		int lock);
void vdfs_add_chunk_bnode(struct vdfs_sb_info *sbi, struct page **pages);
void vdfs_add_chunk_no_lock(struct vdfs_sb_info *sbi, ino_t object_id,
		pgoff_t page_index);
int update_translation_tables(struct vdfs_sb_info *sbi);
int vdfs_build_snapshot_manager(struct vdfs_sb_info *sbi);
int vdfs_get_meta_iblock(struct vdfs_sb_info *sbi, ino_t ino_n,
		sector_t page_index, sector_t *meta_iblock);
loff_t vdfs_special_file_size(struct vdfs_sb_info *sbi, ino_t ino_n);
void vdfs_update_bitmaps(struct vdfs_sb_info *sbi);
int vdfs_check_page_offset(struct vdfs_sb_info *sbi, ino_t object_id,
		pgoff_t page_index, char *is_new);
/* data.c */

int vdfs_read_page(struct block_device *, struct page *, sector_t ,
			unsigned int , unsigned int);
int vdfs_write_page(struct vdfs_sb_info *, struct page *, sector_t ,
			unsigned int , unsigned int, int);
struct page *emmcfs_read_create_page(struct inode *inode, pgoff_t page_index);

struct page *vdfs_read_or_create_small_area_page(struct inode *inode,
		pgoff_t index);

struct bio *allocate_new_bio(struct block_device *bdev,
		sector_t first_sector, int nr_vecs);
int vdfs_read_pages(struct block_device *bdev, struct page **page,
			sector_t sector_addr, unsigned int page_count);
int emmcfs_write_snapshot_pages(struct vdfs_sb_info *sbi, struct page **pages,
		sector_t start_sector, unsigned int page_count, int mode);

int emmcfs_mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc, long int *written_pages_num);

int vdfs_read_or_create_pages(struct vdfs_sb_info *sbi, struct inode *inode,
		struct page **pages, pgoff_t start_page_index, int count,
		int async);

int vdfs_sign_pages(struct page *page, int magic_len,
		__u64 version);

int vdfs_validate_page(struct page *page);

int vdfs_validate_crc(char *buff, int buff_size, const char *magic,
		int magic_len);

void vdfs_update_block_crc(char *buff, int block_size, int ino);

int vdfs_set_bits(char *buff, int buff_size, int offset, int count,
		int magic_len, int block_size);

int vdfs_clear_bits(char *buff, int buff_size, int offset, int count,
		int magic_len, int block_size);

unsigned long  vdfs_find_next_zero_bit(const void *addr,
		unsigned long size, unsigned long offset,
		unsigned int block_size, unsigned int magic_len);

unsigned long vdfs_find_next_bit(const void *addr, unsigned long size,
			unsigned long offset, unsigned int block_size,
			unsigned int magic_len);
ssize_t vdfs_gen_file_buff_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos);

int vdfs_sync_metadata(struct vdfs_sb_info *sbi);

int vdfs_table_IO(struct vdfs_sb_info *sbi, struct page **pages,
		s64 sectors_count, int rw, sector_t *isector);

struct page *vdfs_read_or_create_page(struct inode *inode, pgoff_t index);
struct bio *vdfs_mpage_bio_submit(int rw, struct bio *bio);

int vdfs_mpage_writepage(struct page *page,
		struct writeback_control *wbc, void *data);

/* orphan.c */
enum orphan_inode_type {
	ORPHAN_SMALL_FILE,
	ORPHAN_REGULAR_FILE,
	ORPHAN_TINY_OR_SPECIAL_FILE,
};
/**
 * @brief	After mount orphan inodes processing.
 */
int vdfs_process_orphan_inodes(struct vdfs_sb_info *sbi);

int vdfs_kill_orphan_inode(struct emmcfs_inode_info *inode_info,
		enum orphan_inode_type type, void *loc_info);

/* ioctl.c */

/**
 * @brief	ioctl (an abbreviation of input/output control) is a system
 *		call for device-specific input/output operations and other
 *		 operations which cannot be expressed by regular system calls
 */
long emmcfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long vdfs_dir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
void vdfs_clear_list(struct list_head *list);
void init_high_priority(struct vdfs_high_priority *high_priority);
void destroy_high_priority(struct vdfs_high_priority *high_priority);
int check_permissions(struct vdfs_sb_info *sbi);

/* xattr.c */
int vdfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags);

ssize_t vdfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t size);
int vdfs_removexattr(struct dentry *dentry, const char *name);

int vdfs_xattrtree_cmpfn(struct emmcfs_generic_key *__key1,
		struct emmcfs_generic_key *__key2);
ssize_t vdfs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int vdfs_xattrtree_remove_all(struct vdfs_btree *tree, u64 object_id);
int vdfs_init_security_xattrs(struct inode *inode,
		const struct xattr *xattr_array, void *fs_data);
/* packtree_inode.c */

int vdfs_install_packtree(struct file *parent_dir, struct file *image_file,
		struct ioctl_install_params *pm);
int vdfs_read_packtree_inode(struct inode *inode,
		struct emmcfs_cattree_key *key);

/* packtree.c */
struct installed_packtree_info *vdfs_get_packtree(struct inode *root_inode);


static inline int is_tree(ino_t object_type)
{
	if (object_type == VDFS_CAT_TREE_INO ||
			object_type == VDFS_EXTENTS_TREE_INO ||
			object_type == VDFS_HARDLINKS_TREE_INO ||
			object_type == VDFS_XATTR_TREE_INO)
		return 1;
	else
		return 0;
}

/* small.c */
int init_small_files_area_manager(struct vdfs_sb_info *sbi);
void destroy_small_files_area_manager(struct vdfs_sb_info *sbi);
int vdfs_get_free_cell(struct vdfs_sb_info *sbi, u64 *cell_num);
int vdfs_free_cell(struct vdfs_sb_info *sbi, u64 cell_n);
ssize_t process_tiny_small_file(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos);
ssize_t read_tiny_file(struct kiocb *iocb, const struct iovec *iov);
ssize_t read_small_file(struct kiocb *iocb, const struct iovec *iov);
int write_tiny_small_page(struct page *page, struct writeback_control *wbc);
int read_tiny_small_page(struct page *page);
int writepage_tiny_small(struct address_space *mapping,
		struct writeback_control *wbc);
int vdfs_readpages_tinysmall(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages);
#ifdef CONFIG_VDFS_QUOTA
/* quota.c */
int build_quota_manager(struct vdfs_sb_info *sbi);
void destroy_quota_manager(struct vdfs_sb_info *sbi);
int get_next_quota_index(struct vdfs_sb_info *sbi);
int update_has_quota(struct vdfs_sb_info *sbi, __u64 ino, int index);
int get_quota(struct dentry *dentry);
#endif

#if defined(CONFIG_VDFS_PROC)
/* procfs.c */
int vdfs_dir_init(void);
void vdfs_dir_exit(void);
int vdfs_build_proc_entry(struct vdfs_sb_info *sbi);
void vdfs_destroy_proc_entry(struct vdfs_sb_info *sbi);
#endif

/* macros */

# define IFTODT(mode)	(((mode) & 0170000) >> 12)
# define DTTOIF(dirtype)	((dirtype) << 12)

#define VDFS_GET_CATTREE_RECORD_TYPE(record) (record->key->record_type)

#define EMMCFS_IS_FOLDER(record_type)\
	((record_type) == EMMCFS_CATALOG_FOLDER_RECORD)

#define EMMCFS_IS_FILE(record_type)\
	((record_type) & EMMCFS_CATALOG_FILE_RECORD)

#define EMMCFS_IS_REGFILE(record_type)\
	((record_type) == EMMCFS_CATALOG_FILE_RECORD)

#define EMMCFS_BLOCKS_IN_LEB(sbi) (1 << sbi->log_blocks_in_leb)

#define EMMCFS_LEB_FROM_BLK(sbi, blk) (blk >> sbi->log_blocks_in_leb)
#define EMMCFS_LEB_START_BLK(sbi, leb_n) (leb_n << sbi->log_blocks_in_leb)
#define EMMCFS_BLK_INDEX_IN_LEB(sbi, blk) \
		(blk & ((1 << sbi->log_blocks_in_leb) - 1))

#define VDFS_ESB_OFFSET		(2 * SB_SIZE)

#define EMMCFS_RAW_SB(sbi)	((struct emmcfs_super_block *) \
				(sbi->raw_superblock + 1 * SB_SIZE))

#define VDFS_RAW_EXSB(sbi)	((struct vdfs_extended_super_block *) \
				(sbi->raw_superblock + 3 * SB_SIZE))


#define VDFS_RAW_EXSB_COPY(sbi) ((struct vdfs_extended_super_block *) \
				(sbi->raw_superblock_copy + 3 * SB_SIZE))

#define VDFS_LAST_EXTENDED_SB(sbi) ((struct vdfs_extended_super_block *) \
			(sbi->raw_superblock + 3 * SB_SIZE))
#define VDFS_DEBUG_AREA_START(sbi) le64_to_cpu(VDFS_LAST_EXTENDED_SB(sbi)-> \
		debug_area.begin)

#define VDFS_DEBUG_AREA_LENGTH_BLOCK(sbi) le32_to_cpu( \
			(VDFS_RAW_EXSB(sbi))->debug_area.length)

#define VDFS_DEBUG_AREA(sbi) (sbi->debug_area)
#define VDFS_DEBUG_PAGES(sbi) (sbi->debug_pages)

#define VDFS_DEBUG_AREA_LENGTH_BYTES(sbi) (VDFS_DEBUG_AREA_LENGTH_BLOCK(sbi) \
		<< sbi->block_size_shift)

#define VDFS_DEBUG_AREA_PAGE_COUNT(sbi) (VDFS_DEBUG_AREA_LENGTH_BYTES(sbi) \
		>> PAGE_CACHE_SHIFT)

#define DEBUG_AREA_CRC_OFFSET(sbi) (VDFS_DEBUG_AREA_LENGTH_BYTES(sbi) \
			- sizeof(unsigned int))

#define VDFS_LOG_DEBUG_AREA(sbi, err) \
	vdfs_log_error(sbi, __LINE__, __func__, err);

#define VDFS_SNAPSHOT_VERSION(x) (((__u64)le32_to_cpu(x->descriptor.\
		mount_count) << 32) | (le32_to_cpu(x->descriptor.sync_count)))

#define VDFS_EXSB_VERSION(exsb) (((__u64)le32_to_cpu(exsb->mount_counter)\
		<< 32) | (le32_to_cpu(exsb->sync_counter)))

#define VDFS_GET_TABLE_OFFSET(sbi, x) (le32_to_cpu( \
	((struct vdfs_base_table *)sbi->snapshot_info->base_t)-> \
		translation_table_offsets[VDFS_SF_INDEX(x)]))

#define VDFS_GET_TABLE(sbi, x) ((__le64 *)(sbi->snapshot_info->base_t + \
	le32_to_cpu(((struct vdfs_base_table *)sbi->snapshot_info->base_t)-> \
	translation_table_offsets[VDFS_SF_INDEX(x)])))

#define VDFS_LAST_TABLE_INDEX(sbi, x) (le64_to_cpu( \
	((struct vdfs_base_table *)sbi->snapshot_info->base_t)-> \
	last_page_index[VDFS_SF_INDEX(x)]))

#define GET_TABLE_INDEX(sbi, ino_no, page_offset) \
		(is_tree(ino_no) ? page_offset \
		>> (sbi->log_blocks_in_leb + sbi->block_size_shift - \
		PAGE_SHIFT) : page_offset)

#define PAGE_TO_SECTORS(x) (x * ((1 << (PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT))))

#ifdef CONFIG_SMP

/*
 * These macros iterate all files on all CPUs for a given superblock.
 * files_lglock must be held globally.
 */
#define do_file_list_for_each_entry(__sb, __file)		\
{								\
	int i;							\
	for_each_possible_cpu(i) {				\
		struct list_head *list;				\
		list = per_cpu_ptr((__sb)->s_files, i);		\
		list_for_each_entry((__file), list, f_u.fu_list)

#define while_file_list_for_each_entry				\
	}							\
}

#else

#define do_file_list_for_each_entry(__sb, __file)		\
{								\
	struct list_head *list;					\
	list = &(sb)->s_files;					\
	list_for_each_entry((__file), list, f_u.fu_list)

#define while_file_list_for_each_entry				\
}

#endif

/* VDFS mount options */
enum vdfs_mount_options {
	VDFS_MOUNT_TINY,
	VDFS_MOUNT_TINYSMALL,
	VDFS_MOUNT_STRIPPED,
	VDFS_MOUNT_CASE_INSENSITIVE,
	VDFS_MOUNT_VERCHECK,
	VDFS_MOUNT_BTREE_CHECK,
	VDFS_MOUNT_DEBUG_AREA_CHECK,
	VDFS_MOUNT_FORCE_RO,
	VDFS_MOUNT_FMASK,
	VDFS_MOUNT_DMASK
};

#define clear_option(sbi, option)	(sbi->mount_options &= \
						~(1 << VDFS_MOUNT_##option))
#define set_option(sbi, option)		(sbi->mount_options |= \
						(1 << VDFS_MOUNT_##option))
#define test_option(sbi, option)	(sbi->mount_options & \
						(1 << VDFS_MOUNT_##option))
#endif /* USER_SPACE */

#endif /* _EMMCFS_EMMCFS_H_ */
