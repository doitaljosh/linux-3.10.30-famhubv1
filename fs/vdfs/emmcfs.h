/**
 * VDFS -- Vertically Deliberate improved performance File System
 *
 * Copyright 2012 by Samsung Electronics, Inc.
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

#ifndef _EMMCFS_EMMCFS_H_
#define _EMMCFS_EMMCFS_H_

#ifndef USER_SPACE
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/rbtree.h>
#include <linux/fs.h>
#include <linux/aio.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/pagemap.h>
#include <linux/completion.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/crypto.h>
#include <linux/xattr.h>

#include "vdfs_layout.h"
#include "btree.h"
#include "cattree.h"
#include "debug.h"
#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
#include "../../init/secureboot/include/Secureboot.h"
#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#endif
#endif

#define UUL_MAX_LEN 20

/*
 * Dump out the contents of some memory nicely...
 */
#if defined(CONFIG_SEPARATE_PRINTK_FROM_USER) && !defined(MODULE)
void _sep_printk_start(void);
void _sep_printk_end(void);
#else
#define _sep_printk_start() {}
#define _sep_printk_end() {}
#endif

struct vdfs_packtree_meta_common {
	__u64 packoffset; /* packtree first bnode offset in file */
	__u64 nfs_offset; /* inode finder info for nfs callbacks */
	__u64 xattr_off;  /* xattr area offset in file (in pages) */
	__u64 chtab_off;  /* chunk index table offset */
	__u32 chunk_cnt;  /* chunk count in image */
	__u16 squash_bss; /* squashfs image block size shift */
	__u16 compr_type; /* squashfs image compression type */
	__u32 inodes_cnt; /* number of inodes in the squashfs image */
};

#define VDFS_Z_NEED_DICT_ERR	-7
#define HW_DECOMPRESSOR_PAGES_NUM	32
#define SQUASHFS_COMPR_NR 6

/* INTERNAL CONSTANTS, do not export them via ioctl or disk format */
enum compr_type {
	VDFS_COMPR_UNDEF = 0,	/* Undefined or uninitialized */
	VDFS_COMPR_NONE,	/* No compression */
	VDFS_COMPR_ZLIB,
	VDFS_COMPR_LZO,
	VDFS_COMPR_XZ,
	VDFS_COMPR_LZMA,
	VDFS_COMPR_GZIP,
	VDFS_COMPR_NR,
};

enum vdfs_read_type {
	VDFS_META_READ = 0,
	VDFS_PACKTREE_READ,
	VDFS_FBASED_READ_M,
	VDFS_FBASED_READ_C, /* file based compressed contenes read */
	VDFS_FBASED_READ_UNC, /* file based un-compresed contents read */
	VDFS_READ_NR,
};


#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
struct sdesc {
	struct shash_desc shash;
	char ctx[];
};
#endif

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

struct ioctl_data_link {
	__u64 data_offset;
	__u64 data_length;
	int data_inode_fd;
	char name[VDFS_FILE_NAME_LEN + 1];
};

static inline __u32 vdfs_get_packtree_max_record_size(void)
{
	__u32 size = sizeof(struct vdfs_pack_file_tiny_value);

	if (size < sizeof(struct vdfs_pack_file_fragment_value))
		size = sizeof(struct vdfs_pack_file_fragment_value);

	if (size < sizeof(struct vdfs_pack_file_chunk_value))
		size = sizeof(struct vdfs_pack_file_chunk_value);

	if (size < sizeof(struct vdfs_pack_symlink_value))
		size = sizeof(struct vdfs_pack_symlink_value);

	return size + VDFS_CAT_KEY_MAX_LEN;
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

#define VDFS_IS_READONLY(sb) (sb->s_flags & MS_RDONLY)

extern unsigned int file_prealloc;
extern unsigned int cattree_prealloc;

#define EMMMCFS_VOLUME_START	4


#define VDFS_LINK_MAX 32

#define VDFS_INVALID_BLOCK (~((sector_t) 0xffff))
/* the block size if fixed in vdfs file system = 4K */
#define VDFS_BLOCK_SIZE (1 << 12)

/** Maximal supported file size. Equal maximal file size supported by VFS */
#define EMMCFS_MAX_FILE_SIZE_IN_BYTES MAX_LFS_FILESIZE

/* The eMMCFS runtime flags */
#define EXSB_DIRTY		0
#define IS_MOUNT_FINISHED	2
#define VDFS_META_CRC		3
#define DO_NOT_CHECK_SIGN	4
#define VOLUME_AUTH		5

/** The VDFS inode flags */
#define HAS_BLOCKS_IN_EXTTREE	1
#define VDFS_IMMUTABLE		2
#ifdef CONFIG_VDFS_QUOTA
#define HAS_QUOTA		3
#endif
#define HARD_LINK		10
#define ORPHAN_INODE		12
#define VDFS_COMPRESSED_FILE	13
#define VDFS_AUTH_FILE		15
#define VDFS_READ_ONLY_AUTH	16


/* Macros for calculating catalog tree expand step size.
 * x - total blocks count on volume
 * right shift 7 - empirical value. For example, on 1G volume catalog tree will
 * expands by 8M one step*/
#define TREE_EXPAND_STEP(x)	(x >> 7)

/* Preallocation blocks amount */
#define FSM_PREALLOC_DELTA	0

#define VDFS_META_REREAD	0

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
#define	VDFS_IOC_GET_OPEN_COUNT		_IOR('C', 1, long)
#define	VDFS_IOC32_GET_OPEN_COUNT	_IOR('C', 1, int)
/* ioctl command codes for high priority tasks */
#define VDFS_IOC_GRAB			_IOR('D', 1, long)
#define VDFS_IOC_RELEASE		_IOR('E', 1, long)
#define VDFS_IOC_RESET			_IOR('F', 1, long)
#define VDFS_IOC_GRAB2PARENT		_IOR('G', 1, long)
#define VDFS_IOC_RELEASE2PARENT		_IOR('H', 1, long)

/* File based compression commands */
#define VDFS_IOC_SET_DECODE_STATUS	_IOW('I', 1, int)
#define VDFS_IOC_GET_DECODE_STATUS	_IOR('I', 1, int)
#define VDFS_IOC_GET_COMPR_TYPE		_IOR('J', 1, int)
#define VDFS_IOC_IS_AUTHENTICATED	_IOR('K', 1, int)

#define VDFS_IOC_INSTALL	_IOW('Q', 1, struct ioctl_install_params)
#define VDFS_IOC_UNINSTALL	_IO('Q', 2)
#define VDFS_IOC_DATA_LINK	_IOR('Q', 3, struct ioctl_data_link)

/* Installed vdfs-ro image command */
#define VDFS_RO_IMAGE_INSTALL	_IOR('X', 1, struct vdfs_image_install)

#ifndef USER_SPACE

struct packtrees_list {
	/* pack tree multi-access protection */
	struct mutex lock_pactree_list;

	struct list_head list;
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
	__u64 rsv;	/* reserved for delayed allocation */
	int dirty;	/* 'has' differs from saved in xattr */
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
	/** Volume size in blocks */
	unsigned long long	volume_blocks_count;
	/** The current value of free block count on the whole eMMCFS volume */
	unsigned long long	free_blocks_count;
	/* Reserved for delayed allocations, not in free_blocks_count */
	unsigned long long	reserved_blocks_count;
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
	/** Xattr tree */
	struct vdfs_btree	*xattr_tree;
	/*Sysfs kobject structure*/
	struct kobject	s_kobj;
	struct completion s_kobj_unregister;

	u32 erase_block_size;
	u32 erase_block_size_in_blocks;

	u8 log_erase_block_size_in_blocks;
	u8 log_erase_block_size;
	u8 log_block_size;
	char umount_time;

	u32 *erase_blocks_counters;
	u32 nr_erase_blocks_counters;
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

	/** Free space management */
	struct emmcfs_fsm_info *fsm_info;

	/** Snapshot manager */
	struct vdfs_snapshot_info *snapshot_info;

	/** pending transaction commit */
	struct delayed_work delayed_commit;

	/* fallback completion for metadata writeback */
	wait_queue_head_t meta_bio_wait;
	atomic_t meta_bio_count;

	/* check the debug area in background */
	struct delayed_work delayed_check_debug_area;

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

	struct vdfs_high_priority high_priority;

	struct vdfs_proc_dir_info *proc_info;

	/* new files mode mask, filled if fmask mount option is specifided */
	umode_t fmask;

	/* new directory mode mask, filled if dmask mount option
	 * is specifided */
	umode_t dmask;

	/* installed ro-vdfs-images list */
	struct list_head installed_list;
	struct mutex installed_list_lock;
	int orig_ro;
	/* under catalog_tree->rw_tree_lock */
	struct list_head orphan_inodes;

#ifdef CONFIG_VDFS_DEBUG
	struct mutex dump_meta;
#endif
	__u64 dump_meta_file_offset;

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

struct vdfs_image_install {
	union {
		/* install dir */
		char install_dir[VDFS_FILE_NAME_LEN + 1];
		/* or image name */
		char image_name[VDFS_FILE_NAME_LEN + 1];
	};
	/* total inodes count in an image */
	unsigned int image_inodes_count;
	union {
		/* image file descriptor */
		int image_fd;
		/* parent object id */
		__u64 parent_id;
	};
	/* offset in image in 4k block to image catalog tree */
	__u64 catalog_tree_offset;
	/* offset in  image in 4k block to image xattr tree */
	__u64 xattr_tree_offset;
	/* offset in an image in 4k blocks to an image extents overflow tree */
	__u64 extents_tree_offset;
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
	struct {
		__u64	parent_id;
		__u8	name_len;
		char	name[VDFS_FILE_NAME_LEN];
	} source_image;
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

/*
 * Free space extents are hashed by log2(length) in
 * VDFS_FSM_MAX_ORDER + 1 buckets: 1..2^FSM_MAX_ORDER blocks
 */
#define VDFS_FSM_MAX_ORDER	16

#ifndef CONFIG_VDFS_DEBUG

/*
 * Upper limit of tracked free area extents
 */
# define VDFS_FSM_MAX_FREE_NODES	20480

/*
 * Upper limit of tracked next-free area extents
 */
# define VDFS_FSM_MAX_NEXT_NODES	10240

#else /* CONFIG_VDFS_DEBUG */

/*
 * Tight limits for stress testing
 */
# define VDFS_FSM_MAX_FREE_NODES	256
# define VDFS_FSM_MAX_NEXT_NODES	64

#endif /* CONFIG_VDFS_DEBUG */

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
	/** Lock for LEBs bitmap  */
	struct mutex	lock;
	/** Free space bitmap inode (VFS inode) */
	struct inode	*bitmap_inode;

	/* free extents sorted by start block */
	struct rb_root free_area;

	/* free extents hashed by log2(length) */
	struct list_head free_list[VDFS_FSM_MAX_ORDER + 1];

	/* extents will be freed after commit, sorted by start block */
	struct rb_root	next_free_area;

	/* list of will-be-freed extents */
	struct list_head next_free_list;

	/* count of extents in free_area tree */
	unsigned int	free_area_nodes;

	/* count of extents in next_free_area tree */
	unsigned int	next_free_nodes;

	/* will be freed after metadata commit */
	long long	next_free_blocks;

	/* free space not present in free_area for some reason */
	long long	untracked_blocks;

	/* next free space not present in next_free_area */
	long long	untracked_next_free;
};

struct vdfs_comp_extent_info {
	pgoff_t start_block;
	int offset;
	int len_bytes;
	int blocks_n;
	__u16 flags;
};

struct installed_info {
	struct list_head list;
	/* installed catalog btree */
	struct vdfs_btree *cat_tree;
	/* installed extents overflow tree */
	struct vdfs_btree *ext_tree;
	/* installed extended attributes tree */
	struct vdfs_btree *xattr_tree;
	/* count of open files in this installed image */
	atomic_t open_count;
	/* image start inode number */
	ino_t start_ino;
	/* flag : image is alive */
	int is_not_alive;
};


/** @brief	Maintains private inode info.
 */
struct emmcfs_inode_info {
	/** VFS inode */
	struct inode	vfs_inode;
	/* record type */
	__u8 record_type;

	__u32 comp_extents_n;
	loff_t comp_table_start_offset;
	loff_t comp_size;
	int log_chunk_size;
	enum compr_type compr_type;

	int (*decompressor_fn)(struct page **src, struct page **dst,
			int pages_num, int pages_per_chunk, int offset,
			int len_bytes);

	/** The inode fork information */
	union {
		struct vdfs_fork_info fork;
		struct {
			struct inode *inode;
			__u64 offset;
		} data_link;
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
		/* installed vdfs-ro-image */
		struct vdfs_image_install vdfs_image;
	};
	/* pointer to installed tree from vdfs-ro-image */
	struct installed_info *installed_btrees;
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

	/* for vdfs_sb_info->orphan_inodes */
	struct list_head orphan_list;
	__u64 next_orphan_id;

	/** difference between file_open() and file_release() calls */
	atomic_t open_count;
#ifdef CONFIG_VDFS_QUOTA
	int quota_index;
#endif
#ifdef CONFIG_VDFS_DEBUG_AUTHENTICAION
	int informed_about_fail_read;
#endif
};

struct vdfs_snapshot_info {
	/** Lock metadata update */
	struct mutex insert_lock;
	/** Transaction lock */
	struct rw_semaphore transaction_lock;
	struct rw_semaphore writeback_lock;
	/* snapshot tables protections */
	struct rw_semaphore tables_lock;
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
	/* base table */
	struct vdfs_base_table *base_t;
	/* extended table page */
	struct vdfs_extended_table *extended_table;
	/* first free block in tables area */
	sector_t iblock;
	__u32 sync_count;
	/* dirty pages ammount */
	unsigned int dirty_pages_count;
	/* flag - use base table or expand table */
	__u8 use_base_table;
	__u8 exteneded_table_count;
};

enum snapshot_table_type {
	SNAPSHOT_BASE_TABLE,
	SNAPSHOT_EXT_TABLE
};

/**  @brief	struct for writeback function
 *
 */
struct vdfs_mpage_data {
	sector_t last_block_in_bio;
	struct bio *bio;
};


struct vdfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct vdfs_sb_info *, char *);
	ssize_t (*store)(struct vdfs_sb_info *, const char *, size_t);
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

void __printf(2, 3)
vdfs_fatal_error(struct vdfs_sb_info *sbi, const char *fmt, ...);

/**
 * @brief		Validate fork.
 * @param [in] fork	Pointer to the fork for validation
 * @return		Returns 1 if fork is valid, 0 in case of wrong fork
 * */
static inline int is_fork_valid(const struct vdfs_fork *fork)
{
	if (!fork)
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

/* super.c */

int vdfs_sync_fs(struct super_block *sb, int wait);
int vdfs_sync_first_super(struct vdfs_sb_info *sbi);
int vdfs_sync_second_super(struct vdfs_sb_info *sbi);
void vdfs_dirty_super(struct vdfs_sb_info *sbi);
void vdfs_init_inode(struct emmcfs_inode_info *inode);


struct page **vdfs_get_hw_buffer(struct inode *inode, pgoff_t start_index,
		void **buffer, int pages_count);
void vdfs_put_hw_buffer(struct page **pages);

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
int vdfs_is_tree_alive(struct inode *inode);
struct inode *vdfs_get_image_inode(struct vdfs_sb_info *sbi,
		__u64 parent_id, __u8 *name, size_t name_len);
int init_file_decompression(struct emmcfs_inode_info *inode_i, int debug);
int disable_file_decompression(struct emmcfs_inode_info *inode_i);
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
	struct buffer_head *bh_result, int create, int fsm_flags);
/**
 * @brief	TODO Check it!!! Find old block in snapshots.
 */
sector_t emmcfs_find_old_block(struct emmcfs_inode_info *inode_info,
				sector_t iblock,  __u32 *max_blocks);

/**
 * @brief	Get free inode index[es].
 */
int vdfs_get_free_inode(struct vdfs_sb_info *sbi, ino_t *i_ino,
		unsigned int count);

/**
 * @brief	Free several inodes.
 */
int vdfs_free_inode_n(struct vdfs_sb_info *sbi, ino_t inode_n, int count);
/**
 * @brief	Write inode to bnode.
 */
int vdfs_write_inode(struct inode *inode, struct writeback_control *wbc);

/**
 * @brief	Propagate flags from inode i_flags to EMMCFS_I(inode)->flags.
 */
void vdfs_get_vfs_inode_flags(struct inode *inode);

/**
 * @brief	Set vfs inode i_flags according to EMMCFS_I(inode)->flags.
 */
void vdfs_set_vfs_inode_flags(struct inode *inode);

int vdfs_data_link_create(struct dentry *parent, const char *name,
		struct inode *data_inode, __u64 data_offset, __u64 data_length);

/**
 * @brief		Method to look up an entry in a directory.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
struct dentry *vdfs_lookup(struct inode *dir, struct dentry *dentry,
						unsigned int flags);
#else
struct dentry *vdfs_lookup(struct inode *dir, struct dentry *dentry,
						struct nameidata *nd);
#endif

struct inode *vdfs_iget(struct vdfs_sb_info *sbi, ino_t ino);

/**
 * @brief			Method to read inode.
 */
struct inode *get_inode_from_record(struct vdfs_cattree_record *record,
		struct inode *parent);
struct inode *vdfs_special_iget(struct super_block *sb, unsigned long ino);
struct inode *get_root_inode(struct vdfs_btree *tree);
int get_iblock_extent(struct inode *inode, sector_t iblock,
		struct vdfs_extent_info *result, sector_t *hint_block);
ssize_t vdfs_gen_file_aio_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos);
void vdfs_free_reserved_space(struct inode *inode, sector_t iblocks_count);
/* options.c */

int get_chunk_extent(struct emmcfs_inode_info *inode_i, pgoff_t chunk_idx,
		struct vdfs_comp_extent_info *cext);
/**
 * @brief	Parse eMMCFS options.
 */
int emmcfs_parse_options(struct super_block *sb, char *input);
int vdfs_show_options(struct seq_file *seq, struct dentry *root);

/* btree.c */
int check_bnode_reserve(struct vdfs_btree *btree, int force_insert);
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
 * @brief	Allocate key for catalog tree record.
 */
struct emmcfs_cattree_key *emmcfs_alloc_cattree_key(int name_len,
		u8 record_type);

/**
 * @brief	Fill already allocated key with data.
 */
void emmcfs_fill_cattree_key(struct emmcfs_cattree_key *fill_key, u64 object_id,
		u64 parent_id, const char *name, unsigned int len);

/**
 * @brief	Fill already allocated value area (file or folder) with data
 *		from VFS inode.
 */
void emmcfs_fill_cattree_value(struct inode *inode, void *value_area);

/* cattree-helper.c */
int get_record_type_on_mode(struct inode *inode, u8 *record_type);

/* extents.c */

int vdfs_exttree_get_extent(struct vdfs_sb_info *sbi, struct inode *inode,
		sector_t iblock, struct vdfs_extent_info *result);
int emmcfs_extree_insert_extent(struct vdfs_sb_info *sbi,
		unsigned long object_id, struct vdfs_extent_info *extent,
		int force_insert);
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
		struct vdfs_extent_info *extent, int force_insert);


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

#define VDFS_FSM_ALLOC_DELAYED		0x01	/* for dalayed-allocation */
#define VDFS_FSM_ALLOC_ALIGNED		0x02	/* align to superpage */
#define VDFS_FSM_ALLOC_METADATA		0x04	/* transaction table locked */
#define VDFS_FSM_FREE_UNUSED		0x10	/* never linked to inode */
#define VDFS_FSM_FREE_RESERVE		0x20	/* put back to reserve */

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
		__u32 *length_in_blocks, int fsm_flags);
/**
 * @brief	Function is the fsm_free_block_chunk wrapper. Pust free space
 *		chunk to tree and updates free space manager bitmap.
 *		This function is called during truncate or unlink inode
 *		processes.
 */
int emmcfs_fsm_put_free_block(struct emmcfs_inode_info *inode_info,
		__u64 offset, __u32 length_in_blocks, int fsm_flags);
/**
 * @brief	Puts preallocated blocks back to the tree.
 */
void emmcfs_fsm_discard_preallocation(struct emmcfs_inode_info *inode_info);

void vdfs_commit_free_space(struct vdfs_sb_info *sbi);

/**
 * @brief	Function is the fsm_free_block_chunk wrapper.
 *		It is called during evict inode or process orphan inodes
 *		processes (inode already doesn't exist, but exttree inode blocks
 *		still where).
 */
int emmcfs_fsm_free_exttree_extent(struct vdfs_sb_info *sbi,
	__u64 offset, __u32 length);

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
		unsigned int page_count);
#endif
__le64 vdfs_get_page_version(struct vdfs_sb_info *sbi, struct inode *inode,
		pgoff_t page_index);
int emmcfs_build_snapshot_manager(struct vdfs_sb_info *sbi);
void emmcfs_destroy_snapshot_manager(struct vdfs_sb_info *sbi);
void vdfs_start_transaction(struct vdfs_sb_info *sbi);
void vdfs_stop_transaction(struct vdfs_sb_info *sbi);
void vdfs_start_writeback(struct vdfs_sb_info *sbi);
void vdfs_stop_writeback(struct vdfs_sb_info *sbi);
void vdfs_add_chunk_bitmap(struct vdfs_sb_info *sbi, struct page *page,
		int lock);
void vdfs_add_chunk_bnode(struct vdfs_sb_info *sbi, struct page **pages);
void vdfs_add_chunk_no_lock(struct vdfs_sb_info *sbi, ino_t object_id,
		pgoff_t page_index);
int update_translation_tables(struct vdfs_sb_info *sbi);
int vdfs_build_snapshot_manager(struct vdfs_sb_info *sbi);
int vdfs_get_meta_iblock(struct vdfs_sb_info *sbi, ino_t ino_n,
		pgoff_t page_index, sector_t *meta_iblock);
loff_t vdfs_special_file_size(struct vdfs_sb_info *sbi, ino_t ino_n);
void vdfs_update_bitmaps(struct vdfs_sb_info *sbi);
int vdfs_check_page_offset(struct vdfs_sb_info *sbi, struct inode *inode,
		pgoff_t page_index, char *is_new, int force_insert);
/* data.c */
int vdfs__read(struct inode *inode, int type, struct page **pages,
		unsigned int page_count, int start_block);
int vdfs_dump_chunk_to_disk(void *mapped_chunk, size_t chunk_length,
		const char *name, unsigned int length);
int vdfs_read_page(struct block_device *, struct page *, sector_t ,
			unsigned int , unsigned int);
int vdfs_write_page(struct vdfs_sb_info *, struct page *, sector_t ,
			unsigned int , unsigned int, int);
struct page *emmcfs_read_create_page(struct inode *inode, pgoff_t page_index);

struct bio *allocate_new_bio(struct block_device *bdev,
		sector_t first_sector, unsigned int nr_vecs);
int vdfs_read_pages(struct block_device *bdev, struct page **page,
			sector_t sector_addr, unsigned int page_count);
int emmcfs_write_snapshot_pages(struct vdfs_sb_info *sbi, struct page **pages,
		sector_t start_sector, unsigned int page_count, int mode);

int emmcfs_mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc, long int *written_pages_num);
int get_block_file_based(struct inode *inode, pgoff_t page_index,
		sector_t *res_block);
int vdfs_read_or_create_pages(struct inode *inode, pgoff_t start,
			      unsigned int nr_pages, struct page **pages,
			      enum vdfs_read_type type, int start_block,
			      int force_insert);
int vdfs_read_comp_pages(struct inode *inode, pgoff_t index,
			      int page_count, struct page **pages,
			      enum vdfs_read_type type);
int vdfs_sign_pages(struct page *page, int magic_len,
		__u64 version);
void vdfs_update_block_crc(char *buff, unsigned int block_size,
		unsigned int magic_len);

int vdfs_set_bits(char *buff, int buff_size, unsigned int offset,
		unsigned int count, unsigned int magic_len,
		unsigned int block_size);

int vdfs_clear_bits(char *buff, int buff_size, unsigned int offset,
		unsigned int count, unsigned int magic_len,
		unsigned int block_size);

ssize_t vdfs_gen_file_buff_write(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos);

int vdfs_sync_metadata(struct vdfs_sb_info *sbi);

int vdfs_table_IO(struct vdfs_sb_info *sbi, void *buffer,
		__u64 buffer_size, int rw, sector_t *iblock);

struct page *vdfs_read_or_create_page(struct inode *inode, pgoff_t index,
		enum vdfs_read_type type);
struct bio *vdfs_mpage_bio_submit(int rw, struct bio *bio);

int vdfs_mpage_writepage(struct page *page,
		struct writeback_control *wbc, void *data);

int vdfs_get_table_sector(struct vdfs_sb_info *sbi, sector_t iblock,
		sector_t *result);
struct vdfs_base_table_record *vdfs_get_table(struct vdfs_sb_info *sbi,
		ino_t ino);
/* orphan.c */
int vdfs_add_to_orphan(struct vdfs_sb_info *sbi, struct inode *inode);
void vdfs_del_from_orphan(struct vdfs_sb_info *sbi, struct inode *inode);
int vdfs_process_orphan_inodes(struct vdfs_sb_info *sbi);

/* ioctl.c */

/**
 * @brief	ioctl (an abbreviation of input/output control) is a system
 *		call for device-specific input/output operations and other
 *		 operations which cannot be expressed by regular system calls
 */

void vdfs_update_image_and_dir(struct inode *inode, struct inode *image_inode);
void vdfs_update_parent_dir(struct file *filp);
int vdfs_unlock_source_image(struct vdfs_sb_info *sbi, __u64 parent_id,
		char *name, size_t name_len);
void vdfs_release_image_inodes(struct vdfs_sb_info *sbi, ino_t start_ino,
		unsigned int ino_count);
long vdfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long vdfs_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long vdfs_dir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
void vdfs_clear_list(struct list_head *list);
void init_high_priority(struct vdfs_high_priority *high_priority);
void destroy_high_priority(struct vdfs_high_priority *high_priority);
int vdfs_check_permissions(struct inode *inode, int install_check);

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

struct posix_acl *vdfs_get_acl(struct inode *inode, int type);
int vdfs_init_acl(struct inode *inode, struct inode *dir);
int vdfs_chmod_acl(struct inode *inode);
int vdfs_init_security_xattrs(struct inode *inode,
		const struct xattr *xattr_array, void *fs_data);

/* packtree_inode.c */

int vdfs_install_packtree(struct file *parent_dir, struct file *image_file,
		struct ioctl_install_params *pm);
int vdfs_read_packtree_inode(struct inode *inode,
		struct emmcfs_cattree_key *key);

int unpack_chunk_zlib(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes);
int unpack_chunk_gzip(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes);
int unpack_chunk_lzo(struct page **src, struct page **dst, int pages_num,
		int pages_per_chunk, int offset, int len_bytes);

void dump_fbc_error(struct page **pages, int pages_num, int len,
		struct vdfs_comp_extent_info *cext, loff_t chunk_start_pos,
		struct emmcfs_inode_info *inode_i);

/* packtree.c */
struct installed_packtree_info *vdfs_get_packtree(struct inode *root_inode);


static inline unsigned is_tree(ino_t object_type)
{
	if (object_type == VDFS_CAT_TREE_INO ||
			object_type == VDFS_EXTENTS_TREE_INO ||
			object_type == VDFS_XATTR_TREE_INO)
		return 1;
	else
		return 0;
}

#ifdef CONFIG_VDFS_QUOTA
/* quota.c */
int build_quota_manager(struct vdfs_sb_info *sbi);
void destroy_quota_manager(struct vdfs_sb_info *sbi);
int get_next_quota_index(struct vdfs_sb_info *sbi);
void vdfs_update_quota(struct inode *inode);
int get_quota(struct dentry *dentry);
#else
static inline void vdfs_update_quota(struct inode *inode) { }
#endif

/* procfs.c */
int vdfs_dir_init(void);
void vdfs_dir_exit(void);
int vdfs_build_proc_entry(struct vdfs_sb_info *sbi);
void vdfs_destroy_proc_entry(struct vdfs_sb_info *sbi);
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

#define EMMCFS_LEB_FROM_BLK(sbi, blk) (blk >> sbi->log_blocks_in_leb)
#define EMMCFS_LEB_START_BLK(sbi, leb_n) (leb_n << sbi->log_blocks_in_leb)
#define EMMCFS_BLK_INDEX_IN_LEB(sbi, blk) \
		(blk & ((1 << sbi->log_blocks_in_leb) - 1))

#define VDFS_ESB_OFFSET		(2 * SB_SIZE)

#define EMMCFS_RAW_SB(sbi)	((struct emmcfs_super_block *) \
				((char *)sbi->raw_superblock + 1 * SB_SIZE))

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

#define VDFS_IS_COW_TABLE(table) (!strncmp(table->descriptor.signature, \
	VDFS_SNAPSHOT_BASE_TABLE,\
	sizeof(VDFS_SNAPSHOT_BASE_TABLE) - 1))

#define VDFS_EXSB_VERSION(exsb) (((__u64)le32_to_cpu(exsb->mount_counter)\
		<< 32) | (le32_to_cpu(exsb->sync_counter)))

#define VDFS_GET_TABLE_OFFSET(sbi, x) (le32_to_cpu( \
	((struct vdfs_base_table *)sbi->snapshot_info->base_t)-> \
		translation_table_offsets[VDFS_SF_INDEX(x)]))

#define VDFS_LAST_TABLE_INDEX(sbi, x) (le64_to_cpu( \
	((struct vdfs_base_table *)sbi->snapshot_info->base_t)-> \
	last_page_index[VDFS_SF_INDEX(x)]))

#define GET_TABLE_INDEX(sbi, ino_no, page_offset) \
		(is_tree(ino_no) ? page_offset \
		>> (sbi->log_blocks_in_leb + sbi->block_size_shift - \
		PAGE_SHIFT) : page_offset)

#define PAGE_TO_SECTORS(x) (x * ((1 << (PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT))))

#define is_dlink(inode) (EMMCFS_I(inode)->record_type == \
		VDFS_CATALOG_DLINK_RECORD)


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
	VDFS_MOUNT_CASE_INSENSITIVE,
	VDFS_MOUNT_FORCE_RO,
	VDFS_MOUNT_FMASK,
	VDFS_MOUNT_DMASK,
	VDFS_MOUNT_DO_NOT_CHECK_SIGN,
};

#define clear_option(sbi, option)	(sbi->mount_options &= \
						~(1 << VDFS_MOUNT_##option))
#define set_option(sbi, option)		(sbi->mount_options |= \
						(1 << VDFS_MOUNT_##option))
#define test_option(sbi, option)	(sbi->mount_options & \
						(1 << VDFS_MOUNT_##option))

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
static inline uid_t from_kuid(struct user_namespace *to, uid_t uid)
{
	return uid;
}
static inline gid_t from_kgid(struct user_namespace *to, gid_t gid)
{
	return gid;
}
static inline uid_t i_uid_read(const struct inode *inode)
{
	return inode->i_uid;
}
static inline gid_t i_gid_read(const struct inode *inode)
{
	return inode->i_gid;
}
static inline void i_uid_write(struct inode *inode, uid_t uid)
{
	inode->i_uid = uid;
}
static inline void i_gid_write(struct inode *inode, gid_t gid)
{
	inode->i_gid = gid;
}
#endif

#ifdef CONFIG_VDFS_DEBUG

/* check for active transaction or writeback */
static inline void vdfs_assert_transaction(struct vdfs_sb_info *sbi)
{
	/*
	 * Both vdfs_start_transaction and vdfs_start_writeback
	 * uses current->journal_info as recursion-preventing counter.
	 */
	if (sbi->sb->s_flags & MS_ACTIVE)
		WARN_ON(!current->journal_info);
}

/* btree must be locked for read or write */
static inline void vdfs_assert_btree_lock(struct vdfs_btree *btree)
{
	if (btree->sbi->sb->s_flags & MS_ACTIVE)
		lockdep_assert_held(&btree->rw_tree_lock);
}

/* use it if btree must be locked for write */
static inline void vdfs_assert_btree_write(struct vdfs_btree *btree)
{
	if (btree->sbi->sb->s_flags & MS_ACTIVE) {
		lockdep_assert_held(&btree->rw_tree_lock);
		if (WARN_ON(down_read_trylock(&btree->rw_tree_lock))) {
			up_read(&btree->rw_tree_lock);
			BUG();
		}
	}
}

static inline void vdfs_assert_i_mutex(struct inode *inode)
{
	lockdep_assert_held(&inode->i_mutex);
}


#else /* CONFIG_VDFS_DEBUG */

static inline void vdfs_assert_transaction(struct vdfs_sb_info *sbi) { }
static inline void vdfs_assert_btree_lock(struct vdfs_btree *btree) { }
static inline void vdfs_assert_btree_write(struct vdfs_btree *btree) { }
static inline void vdfs_assert_i_mutex(struct inode *inode) { }
#endif /* CONFIG_VDFS_DEBUG */

#ifdef CONFIG_VDFS_DATA_AUTHENTICATION
int calculate_hash_sha1(unsigned char *buf, unsigned int buf_len,
		unsigned char *hash);
int vdfs_verify_rsa_sha1_signature(unsigned char *buf, unsigned int buf_len,
		unsigned char *signature);
#endif




#endif /* USER_SPACE */

#endif /* _EMMCFS_EMMCFS_H_ */
