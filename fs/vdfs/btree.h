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

#ifndef BTREE_H_
#define BTREE_H_

#include "vdfs_layout.h"


#ifndef USER_SPACE
/* #define CONFIG_VDFS_DEBUG_GET_BNODE */
#endif

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
#include <linux/stacktrace.h>
#endif

#define EMMCFS_BNODE_DSCR(bnode) ((struct vdfs_gen_node_descr *) \
		(bnode)->data)

#define EMMCFS_BNODE_RECS_NR(bnode) \
	le16_to_cpu(EMMCFS_BNODE_DSCR(bnode)->recs_count)

#define VDFS_KEY_LEN(key) (le32_to_cpu(key->key_len))
#define VDFS_RECORD_LEN(key) (le32_to_cpu(key->record_len))
#define VDFS_NEXT_BNODE_ID(bnode) \
	(le32_to_cpu(EMMCFS_BNODE_DSCR(bnode)->next_node_id))
#define VDFS_PREV_BNODE_ID(bnode) \
	(le32_to_cpu(EMMCFS_BNODE_DSCR(bnode)->prev_node_id))

#define VDFS_MAX_BTREE_KEY_LEN VDFS_CAT_KEY_MAX_LEN
#define VDFS_MAX_BTREE_REC_LEN (VDFS_CAT_KEY_MAX_LEN + \
		sizeof(struct vdfs_catalog_file_record))

/** How many free space node should have on removing records, to merge
 * with neighborhood */
#define EMMCFS_BNODE_MERGE_LIMIT	(0.7)

#define VDFS_GET_BNODE_STACK_ITEMS 20

#define VDFS_WAIT_BNODE_UNLOCK 1
#define VDFS_NOWAIT_BNODE_UNLOCK 0

#ifndef USER_SPACE
#include "mutex_on_sem.h"
#include <linux/rbtree.h>
#endif

/**
 * @brief		Get pointer to value in key-value pair.
 * @param [in]	key_ptr	Pointer to key-value pair
 * @return		Returns pointer to value in key-value pair
 */
static inline void *get_value_pointer(void *key_ptr)
{
	struct emmcfs_cattree_key *key = key_ptr;
	if (key->gen_key.key_len > VDFS_MAX_BTREE_KEY_LEN)
		return ERR_PTR(-EINVAL);
	else
		return (void *)((char *)key_ptr + key->gen_key.key_len);
};

/** TODO */
enum emmcfs_get_bnode_mode {
	/** The bnode is operated in read-write mode */
	EMMCFS_BNODE_MODE_RW = 0,
	/** The bnode is operated in read-only mode */
	EMMCFS_BNODE_MODE_RO
};

/** TODO */
enum emmcfs_node_type {
	EMMCFS_FIRST_NODE_TYPE = 1,
	/** bnode type is index */
	EMMCFS_NODE_INDEX = EMMCFS_FIRST_NODE_TYPE,
	/** bnode type is leaf */
	EMMCFS_NODE_LEAF,
	EMMCFS_NODE_NR,
};

/** TODO */
enum emmcfs_btree_type {
	EMMCFS_BTREE_FIRST_TYPE = 0,
	/** btree is catalog tree */
	EMMCFS_BTREE_CATALOG = EMMCFS_BTREE_FIRST_TYPE,
	/** btree is extents overflow tree */
	EMMCFS_BTREE_EXTENTS,
	/** btree is xattr tree */
	VDFS_BTREE_XATTRS,
	/* installed extended atribute tree */
	VDFS_BTREE_PACK,
	VDFS_BTREE_HARD_LINK,
	/* installed catalog tree */
	VDFS_BTREE_INST_CATALOG,
	/* installed extended atribute tree */
	VDFS_BTREE_INST_XATTR,
	/* installed extents overflow tree */
	VDFS_BTREE_INST_EXT,
	EMMCFS_BTREE_TYPES_NR
};

typedef int (emmcfs_btree_key_cmp)(struct emmcfs_generic_key *,
		struct emmcfs_generic_key *);

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
struct vdfs_get_bnode_trace {
	unsigned long stack_entries[VDFS_GET_BNODE_STACK_ITEMS];
	int mode;
	struct stack_trace trace;
	struct list_head list;
};
#endif

/**
 * @brief	Structure contains information about bnode in runtime.
 */
struct emmcfs_bnode {
	/** Pointer to memory area where contents of bnode is mapped to */
	void *data;

	/** The bnode's id */
	__u32 node_id;

	/** Pointer to tree containing this bnode */
	struct vdfs_btree *host;

	/** Mode in which bnode is got */
	enum emmcfs_get_bnode_mode mode;

	struct hlist_node hash_node;

	/* under btree->hash_lock */
	int ref_count;

#ifdef USER_SPACE
	int is_dirty;
#endif

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	/* under btree->hash_lock */
	struct list_head get_traces_list;
#endif

	/**
	 * Array of pointers to struct page representing pages in
	 * memory containing data of this bnode
	 */
	struct page *pages[0];
};

#define	VDFS_BNODE_HASH_SIZE_SHIFT 10
#define	VDFS_BNODE_HASH_SIZE (1 << VDFS_BNODE_HASH_SIZE_SHIFT)
#define	VDFS_BNODE_HASH_MASK (VDFS_BNODE_HASH_SIZE - 1)

/**
 * @brief	An eMMCFS B-tree held in memory.
 */
struct vdfs_btree {
	/** Pointer superblock, possessing  this tree */
	struct vdfs_sb_info *sbi;

	/** The inode of special file containing  this tree */
	struct inode *inode;
	/** Number of pages enough to contain whole bnode in memory */
	unsigned int pages_per_node;
	/** Number of pages enough to contain whole bnode in memory
	 * (power of 2)*/
	unsigned int log_pages_per_node;
	/** Size of bnode in bytes */
	unsigned int node_size_bytes;
	/** Maximal available record length for this tree */
	unsigned short max_record_len;

	/** Type of the tree */
	enum emmcfs_btree_type btree_type;
	/** Comparison function for this tree */
	emmcfs_btree_key_cmp *comp_fn;

	/** Pointer to head (containing essential info)  bnode */
	struct emmcfs_bnode *head_bnode;
	/** Info about free bnode list */
	struct emmcfs_bnode_bitmap *bitmap;

	struct mutex	hash_lock;

	struct hlist_head hash_table[VDFS_BNODE_HASH_SIZE];

	/** Lock to protect tree operations */
	rw_mutex_t rw_tree_lock;
	/** Offset in blocks of tree metadata area start in inode.
	 * (!=0 for packtree metadata appended to the end of squasfs image) */
	sector_t tree_metadata_offset;
	ino_t start_ino;

	void *split_buff;
	struct mutex split_buff_lock;
};

/**
 * @brief	Macro gets essential information about tree contained in head
 *		tree node.
 */
#define EMMCFS_BTREE_HEAD(btree) ((struct vdfs_raw_btree_head *) \
	(btree->head_bnode->data))

struct vdfs_btree_record_pos {
	struct emmcfs_bnode *bnode;
	int pos;
};

/**
 * @brief	Runtime structure representing free bnode id bitmap.
 */
struct emmcfs_bnode_bitmap {
	/** Memory area containing bitmap itself */
	void *data;
	/** Size of bitmap */
	__u64 size;
	/** Starting bnode id */
	__u32 start_id;
	/** Maximal bnode id */
	__u32 end_id;
	/** First free id for quick free bnode id search */
	__u32 first_free_id;
	/** Amount of free bnode ids */
	__u32 free_num;
	/** Amount of bits in bitmap */
	__u32 bits_num;
	/** Pointer to bnode containing this bitmap */
	struct emmcfs_bnode *host;
};

struct vdfs_btree_gen_record {
	void *key;
	void *val;
};

struct vdfs_btree_record_info {
	struct vdfs_btree_gen_record gen_record;
	struct vdfs_btree_record_pos rec_pos;
};


static inline struct vdfs_btree_record_info *VDFS_BTREE_REC_I(
		struct vdfs_btree_gen_record *record)
{
	return container_of(record, struct vdfs_btree_record_info, gen_record);
}


/* Set this to sizeof(crc_type) to handle CRC */
#define EMMCFS_BNODE_FIRST_OFFSET 4
#define EMMCFS_INVALID_NODE_ID 0

typedef __u32 emmcfs_bt_off_t;
#define EMMCFS_BT_INVALID_OFFSET ((__u32) (((__u64) 1 << 32) - 1))

/**
 * @brief	Interface for finding data in the whole tree.
 */
/* NOTE: deprecated function. Use vdfs_btree_find instead */
struct emmcfs_bnode  *emmcfs_btree_find(struct vdfs_btree *tree,
				struct emmcfs_generic_key *key,
				int *pos, enum emmcfs_get_bnode_mode mode);
struct vdfs_btree_gen_record *vdfs_btree_find(struct vdfs_btree *btree,
		struct emmcfs_generic_key *key,
		enum emmcfs_get_bnode_mode mode);

struct vdfs_btree_gen_record *vdfs_btree_build_gen_record(
		struct vdfs_btree *btree, __u32 bnode_id, __u32 pos);
/**
 * @brief	Interface to function get_record.
 */
void *emmcfs_get_btree_record(struct emmcfs_bnode *bnode, int index);

/**
 * @brief	Interface for simple insertion into tree.
 */
int emmcfs_btree_insert(struct vdfs_btree *btree, void *new_data,
		int force_insert);
struct vdfs_btree_gen_record *vdfs_btree_place_data(struct vdfs_btree *btree,
		struct emmcfs_generic_key *gen_key);
int vdfs_get_next_btree_record(struct vdfs_btree_gen_record *record);

/**
 * @brief	Interface for key removal.
 */
int emmcfs_btree_remove(struct vdfs_btree *tree,
			struct emmcfs_generic_key *key);


/* Essential for B-tree algorithm functions */

/**
 * @brief	Removes the specified key at level.
 */
int btree_remove(struct vdfs_btree *tree,
		struct emmcfs_generic_key *key, int level);


void vdfs_put_bnode(struct emmcfs_bnode *bnode);
struct emmcfs_bnode *vdfs_get_bnode(struct vdfs_btree *btree,
	__u32 node_id, enum emmcfs_get_bnode_mode mode, int wait);
/* Temporal stubs */
#define emmcfs_get_bnode(btree, node_id, mode) vdfs_get_bnode(btree, node_id, \
		mode, VDFS_WAIT_BNODE_UNLOCK)
#define emmcfs_put_bnode(bnode) vdfs_put_bnode(bnode)

/**
 * @brief	Mark bnode as free.
 */
int emmcfs_destroy_bnode(struct emmcfs_bnode *bnode);

/**
 * @brief	Create, reserve and prepare new bnode.
 */
struct emmcfs_bnode *vdfs_alloc_new_bnode(struct vdfs_btree *btree);

/**
 * @brief	Get descriptor of next bnode.
 */
struct emmcfs_bnode *emmcfs_get_next_bnode(struct emmcfs_bnode *bnode);

/**
 * @brief	Mark bnode as dirty (data on disk and in memory differs).
 */
void emmcfs_mark_bnode_dirty(struct emmcfs_bnode *node);

/**
 * @brief	Get B-tree rood bnode id.
 */
u32 emmcfs_btree_get_root_id(struct vdfs_btree *btree);

void vdfs_put_btree(struct vdfs_btree *btree, int iput);
void vdfs_release_dirty_record(struct vdfs_btree_gen_record *record);
void vdfs_release_record(struct vdfs_btree_gen_record *record);
void vdfs_mark_record_dirty(struct vdfs_btree_gen_record *record);

inline void emmcfs_calc_crc_for_bnode(void *bnode_data,
		struct vdfs_btree *btree);

#ifdef USER_TEST
void test_init_new_node_descr(struct emmcfs_bnode *bnode,
		enum emmcfs_node_type type);
int temp_stub_insert_into_node(struct emmcfs_bnode *bnode,
		void *new_record, int insert_pos);
#endif

#ifndef USER_SPACE
int vdfs_check_and_sign_dirty_bnodes(struct page **page,
		struct vdfs_btree *btree, __u64 version);
#endif /* USER_SPACE */

int vdfs_init_btree_caches(void);
void vdfs_destroy_btree_caches(void);
int vdfs_check_btree_slub_caches_empty(void);

int vdfs_check_btree_links(struct vdfs_btree *btree, int *dang_num);
int vdfs_check_btree_records_order(struct vdfs_btree *btree);

emmcfs_bt_off_t get_offset(struct emmcfs_bnode *bnode, unsigned int index);
void *get_offset_addr(struct emmcfs_bnode *bnode, unsigned int index);
u_int32_t btree_get_bitmap_size(struct vdfs_sb_info *sbi);

void temp_stub_init_new_node_descr(struct emmcfs_bnode *bnode,
		enum emmcfs_node_type type);
int temp_stub_insert_into_node(struct emmcfs_bnode *bnode,
		void *new_record, int insert_pos);
#ifndef USER_SPACE
void vdfs_dump_panic_remount(struct emmcfs_bnode *bnode,
		const char *fmt, ...);
#else
static inline void vdfs_dump_panic_remount(UNUSED struct emmcfs_bnode *bnode,
		UNUSED const char *fmt, ...) {};
#endif

#endif /* BTREE_H_ */
