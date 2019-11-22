/**
 * @file	fs/emmcfs/bnode.c
 * @brief	Basic B-tree node operations.
 * @author
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements bnode operations and its related functions.
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef USER_SPACE
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>

#include "emmcfs.h"

#include <linux/mmzone.h>

#else
#include "vdfs_tools.h"
#endif

#include "btree.h"

#define VDFS_BNODE_RESERVE(btree) (emmcfs_btree_get_height(btree)*4)



#if defined(CONFIG_VDFS_CRC_CHECK)
inline __le32 __get_checksum_offset(struct emmcfs_bnode *node)
{
	return node->host->node_size_bytes - EMMCFS_BNODE_FIRST_OFFSET;
}

inline __le32 *__get_checksum_addr(struct emmcfs_bnode *node)
{
	return node->data + __get_checksum_offset(node);
}

inline void __update_checksum(struct emmcfs_bnode *node)
{
	*__get_checksum_addr(node) = crc32(0, node->data,
			__get_checksum_offset(node));
}

inline __le32 __get_checksum(struct emmcfs_bnode *node)
{
	return *__get_checksum_addr(node);
}

inline __le32 __get_checksum_offset_from_btree(struct vdfs_btree
		*btree)
{
	return btree->node_size_bytes -	EMMCFS_BNODE_FIRST_OFFSET;
}

inline __le32 *__get_checksum_addr_from_data(void *bnode_data,
		struct vdfs_btree *btree)
{
	return bnode_data + __get_checksum_offset_from_btree(btree);
}

inline __le32 __calc_crc_for_bnode(struct emmcfs_bnode *bnode)
{
	return crc32(0, bnode->data, __get_checksum_offset(bnode));
}
inline void emmcfs_calc_crc_for_bnode(void *bnode_data,
		struct vdfs_btree *btree)
{
	*__get_checksum_addr_from_data(bnode_data, btree) =
		crc32(0, bnode_data, __get_checksum_offset_from_btree(btree));
}
#endif


/**
 * @brief		Extract bnode into memory.
 * @param [in]	btree	The B-tree from which bnode is to be extracted
 * @param [in]	node_id	Id of extracted bnode
 * @param [in]	create	Flag if to create (1) or get existing (0) bnode
 * @param [in]	mode	Mode in which the extacted bnode will be used
 * @return		Returns pointer to bnode in case of success,
 *			error code otherwise
 */
static struct emmcfs_bnode *__get_bnode(struct vdfs_btree *btree,
	__u32 node_id, int create, enum emmcfs_get_bnode_mode mode)
{
	struct inode *inode = btree->inode;
	struct emmcfs_bnode *node;
	struct page **pages = NULL;
	void *err_ret_code = NULL;
	u16 *magic;
	unsigned int i;
	pgoff_t page_index;
	int ret;

	if (!inode) {
		if (!is_sbi_flag_set(btree->sbi, IS_MOUNT_FINISHED))
			return ERR_PTR(-EINVAL);
		else
			EMMCFS_BUG();
	}

	node = kzalloc(sizeof(struct emmcfs_bnode), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	page_index = node_id * btree->pages_per_node;
	if (btree->btree_type == VDFS_BTREE_PACK)
		page_index += btree->tree_metadata_offset;

	pages = kzalloc(sizeof(*pages) * btree->pages_per_node, GFP_KERNEL);
	if (!pages) {
		struct emmcfs_bnode *ret = ERR_PTR(-ENOMEM);
		kfree(node);
		return ret;
	}

	ret = vdfs_read_or_create_pages(btree->sbi, btree->inode, pages,
			page_index, btree->pages_per_node, 0);
	if (ret) {
		err_ret_code = ERR_PTR(ret);
		goto err_exit;
	}

	node->data = vmap(pages, btree->pages_per_node, VM_MAP, PAGE_KERNEL);
	if (!node->data) {
		EMMCFS_ERR("unable to vmap %d pages", btree->pages_per_node);
		err_ret_code = ERR_PTR(-ENOMEM);
		goto err_exit;
	}

#if defined(CONFIG_VDFS_DEBUG)
	if (create) {
		__u16 poison_val;
		__u16 max_poison_val = btree->pages_per_node <<
			(PAGE_CACHE_SHIFT - 1);
		__u16 *curr_pointer = node->data;

		for (poison_val = 0; poison_val < max_poison_val;
				poison_val++, curr_pointer++)
			*curr_pointer = poison_val;
	}
#endif

	magic = node->data;
	if (!create && node_id != 0 && *magic != EMMCFS_NODE_DESCR_MAGIC) {
		err_ret_code = ERR_PTR(-ENOENT);
		goto err_exit_vunmap;
	}

	node->pages = pages;
	node->host = btree;
	node->node_id = node_id;

	node->mode = mode;

#if defined(CONFIG_VDFS_CRC_CHECK)
	if (create)
		__update_checksum(node);
	if ((!PageDirty(node->pages[0]) &&
			(btree->btree_type != VDFS_BTREE_PACK))) {
		__le32 from_disk = __get_checksum(node);
		__le32 calculated = __calc_crc_for_bnode(node);
		if (from_disk != calculated) {
			EMMCFS_ERR("Btree Inode id: %lu Bnode id: %u",
					btree->inode->i_ino, node_id);
			EMMCFS_ERR("Expected %u, recieved: %u",
					calculated, from_disk);
			if (!is_sbi_flag_set(btree->sbi, IS_MOUNT_FINISHED)) {
				err_ret_code = ERR_PTR(-EINVAL);
				goto err_exit_vunmap;
			} else
				EMMCFS_BUG();
		}
	}
#endif

	atomic_set(&node->ref_count, 1);

	return node;

err_exit_vunmap:
	vunmap(node->data);
err_exit:
	for (i = 0; i < btree->pages_per_node; i++) {
		if (pages[i] && !IS_ERR(pages[i]))
			page_cache_release(pages[i]);
	}
	kfree(pages);
	kfree(node);
	return err_ret_code;
}

static int bnode_hash_fn(__u32 bnode_id)
{
	/*bnode_id = (bnode_id >> 16) + bnode_id;
	bnode_id = (bnode_id >> 8) + bnode_id;*/
	return bnode_id & VDFS_BNODE_HASH_MASK;
}

static void remove_from_lru(struct emmcfs_bnode *bnode)
{
	struct vdfs_btree *btree = bnode->host;

	list_del(&bnode->lru_node);

	if (!bnode->state)
		btree->passive_use_count--;
	else
		btree->active_use_count--;



}

static void add_to_passive(struct emmcfs_bnode *bnode)
{
#if VDFS_PASSIVE_SIZE
	struct vdfs_btree *btree = bnode->host;

	list_add(&bnode->lru_node, &btree->passive_use);
	bnode->state = 0;

	btree->passive_use_count++;
	if (btree->passive_use_count > VDFS_PASSIVE_SIZE) {
		/* Remove last element*/
		emmcfs_put_cache_bnode(list_entry(btree->passive_use.prev,
				struct emmcfs_bnode, lru_node));
		btree->passive_use_count--;

	}
#endif
}

static void passive_to_active(struct emmcfs_bnode *bnode);

static void add_to_active(struct emmcfs_bnode *bnode)
{
	struct vdfs_btree *btree = bnode->host;
#if VDFS_ACTIVE_SIZE

	list_add(&bnode->lru_node, &btree->active_use);
	bnode->state = 1;

	btree->active_use_count++;
	if (btree->active_use_count > VDFS_ACTIVE_SIZE) {
		struct emmcfs_bnode *deleting =
			list_entry(btree->active_use.prev,
					struct emmcfs_bnode, lru_node);

		if (!deleting->node_id) {
			passive_to_active(deleting);
			return;
		}
		list_del(btree->active_use.prev);
		add_to_passive(deleting);
		btree->active_use_count--;
	}
#endif
}

static void passive_to_active(struct emmcfs_bnode *bnode)
{
	remove_from_lru(bnode);
	add_to_active(bnode);
}

static void hash_bnode(struct emmcfs_bnode *bnode)
{
	struct vdfs_btree *btree = bnode->host;
	int hash = bnode_hash_fn(bnode->node_id);

	hlist_add_head(&bnode->hash_node, &btree->hash_table[hash]);

	if (!bnode->node_id)
		add_to_active(bnode);
	else
		add_to_passive(bnode);

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
		INIT_LIST_HEAD(&bnode->get_traces_list);
		spin_lock_init(&bnode->get_traces_lock);
#endif

}

static void unhash_bnode(struct emmcfs_bnode *bnode)
{
	hlist_del(&bnode->hash_node);
	remove_from_lru(bnode);
}


static struct emmcfs_bnode *get_bnode_from_hash(struct vdfs_btree *btree,
		__u32 bnode_id, enum emmcfs_get_bnode_mode mode)
{
	int hash = bnode_hash_fn(bnode_id);
	struct hlist_node *iterator;

	hlist_for_each(iterator, &btree->hash_table[hash]) {
		struct emmcfs_bnode *node = hlist_entry(iterator,
				struct emmcfs_bnode, hash_node);
		if (node->node_id == bnode_id) {
			node->mode = mode;
			if (node->state == 0)
				passive_to_active(node);
			return node;
		}
	}
	return NULL;
}


#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
static int save_get_bnode_stack(struct emmcfs_bnode *bnode)
{
	struct vdfs_get_bnode_trace *bnode_trace =
		kzalloc(sizeof(*bnode_trace), GFP_KERNEL);

	if (WARN(!bnode_trace, "can not allocate memory for get-bnode-trace"))
		return -ENOMEM;

	bnode_trace->trace.nr_entries = 0;
	bnode_trace->trace.entries = &bnode_trace->stack_entries[0];
	bnode_trace->trace.max_entries = VDFS_GET_BNODE_STACK_ITEMS;

	/* How many "lower entries" to skip. */
	bnode_trace->trace.skip = 2;

	save_stack_trace(&bnode_trace->trace);

	spin_lock(&bnode->get_traces_lock);
	list_add(&bnode_trace->list, &bnode->get_traces_list);
	spin_unlock(&bnode->get_traces_lock);

	return 0;
}

static void print_get_bnode_stack(struct emmcfs_bnode *bnode)
{
	struct vdfs_get_bnode_trace *trace;
	int i = 0;

	spin_lock(&bnode->get_traces_lock);

	printk(KERN_DEFAULT "*******************************\n");
	printk(KERN_DEFAULT "Get-traces for bnode #%u\n", bnode->node_id);
	list_for_each_entry(trace, &bnode->get_traces_list, list) {
		printk(KERN_DEFAULT "Stack %2d:\n", i++);
		print_stack_trace(&trace->trace, 4);
	}

	spin_unlock(&bnode->get_traces_lock);
}

static void free_get_bnode_stack(struct emmcfs_bnode *bnode)
{
	struct vdfs_get_bnode_trace *trace, *tmp;
	list_for_each_entry_safe(trace, tmp, &bnode->get_traces_list,
			list) {
		list_del(&trace->list);
		kfree(trace);
	}
}
#endif

/**
 * @brief		Interface for extracting bnode into memory.
 * @param [in]	btree	B-tree from which bnode is to be extracted
 * @param [in]	node_id	Id of extracted bnode
 * @param [in]	mode	Mode in which the extacted bnode will be used
 * @return		Returns pointer to bnode in case of success,
 *			error code otherwise
 */
struct emmcfs_bnode *vdfs_get_bnode(struct vdfs_btree *btree,
		__u32 node_id, enum emmcfs_get_bnode_mode mode, int wait)
{
	struct emmcfs_bnode *bnode;

	if (node_id) {
		if (node_id >= btree->bitmap->bits_num)
			return ERR_PTR(-ENOENT);

		if (!test_bit(node_id, btree->bitmap->data))
			return ERR_PTR(-ENOENT);
	}

	mutex_lock(&btree->hash_lock);
	bnode = get_bnode_from_hash(btree, node_id, mode);
	if (!bnode) {
		bnode = __get_bnode(btree, node_id, 0, mode);
		if (IS_ERR(bnode)) {
			mutex_unlock(&btree->hash_lock);
			return bnode;
		}

		hash_bnode(bnode);
	} else {
		int val;
		/* !!!
		 * Algorithms expects that nobody will take any bnode
		 * recursevely, so, its wrong behaviour if ref_count > 1 */

		val = atomic_inc_return(&bnode->ref_count);

		/* Temporary stub, until double getting will be fixed.
		 * Here is supposed to be val > 1 */

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
		if (val > 2) {
			EMMCFS_DEBUG_TMP("val is %d", val);
			print_get_bnode_stack(bnode);
			EMMCFS_BUG();
		}
#else
		EMMCFS_BUG_ON(mode == EMMCFS_BNODE_MODE_RW && val > 2);
#endif
	}
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	bnode_sanity_check(bnode);
#endif

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	{
		int ret = 0;
		ret = save_get_bnode_stack(bnode);
		if (ret) {
			emmcfs_put_bnode(bnode);
			return ERR_PTR(ret);
		}
	}
#endif

	mutex_unlock(&btree->hash_lock);
	return bnode;
}

/**
 * @brief		Create, reserve and prepare new bnode.
 * @param [in]	btree	The B-tree from which bnode is to be prepared
 * @return		Returns pointer to bnode in case of success,
 *			error code otherwise
 */
struct emmcfs_bnode *vdfs_alloc_new_bnode(struct vdfs_btree *btree)
{
	struct emmcfs_bnode_bitmap *bitmap = btree->bitmap;
	struct emmcfs_bnode *bnode;

	if (bitmap->free_num == 0)
		return ERR_PTR(-ENOSPC);

	mutex_lock(&btree->hash_lock);
	/*spin_lock(&bitmap->spinlock);*/
	EMMCFS_BUG_ON(test_and_set_bit(bitmap->first_free_id, bitmap->data));
	/*spin_unlock(&bitmap->spinlock);*/
	/* TODO bitmap->data  directly points to head bnode memarea so head
	 * bnode is always kept in memory. Maybe better to split totally
	 * on-disk and runtime structures and sync them only in sync-points
	 * (sync callback, write_inode) ?
	 */
	emmcfs_mark_bnode_dirty(btree->head_bnode);

	bnode = __get_bnode(btree, bitmap->first_free_id, 1,
			EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(bnode)) {
		clear_bit(bitmap->first_free_id, bitmap->data);
		mutex_unlock(&btree->hash_lock);
		return bnode;
	}

	/* Updata bitmap information */
	bitmap->first_free_id = find_next_zero_bit(bitmap->data,
			bitmap->bits_num, bitmap->first_free_id);
	bitmap->free_num--;
	emmcfs_mark_bnode_dirty(btree->head_bnode);

	hash_bnode(bnode);
	mutex_unlock(&btree->hash_lock);

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	{
		int ret = 0;
		ret = save_get_bnode_stack(bnode);
		if (ret) {
			emmcfs_put_bnode(bnode);
			return ERR_PTR(ret);
		}
	}
#endif

	return bnode;
}

static void free_bnode(struct emmcfs_bnode *bnode)
{
	unsigned int i;

	vunmap(bnode->data);
	for (i = 0; i < bnode->host->pages_per_node; i++)
		page_cache_release(bnode->pages[i]);

	bnode->data = NULL;

	kfree(bnode->pages);
	kfree(bnode);
}

/**
 * @brief			Free bnode structure.
 * @param [in]	bnode		Node to be freed
 * @param [in]	keep_in_cache	Whether to save freeing bnode in bnode cache
 * @return	void
 */
static void __put_bnode(struct emmcfs_bnode *bnode, int keep_in_cache)
{
	EMMCFS_BUG_ON(!bnode->data || !bnode->pages);

	if (keep_in_cache)
		return;
	else
		unhash_bnode(bnode);

	free_bnode(bnode);
}

/**
 * @brief		Interface for freeing bnode structure.
 * @param [in]	bnode	Node to be freed
 * @return	void
 */
void vdfs_put_bnode(struct emmcfs_bnode *bnode)
{
	int val;

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	spin_lock(&bnode->get_traces_lock);
#endif
	val = atomic_dec_return(&bnode->ref_count);
	EMMCFS_BUG_ON(val < 0);

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	if (val == 0)
		free_get_bnode_stack(bnode);
	spin_unlock(&bnode->get_traces_lock);
#endif
	__put_bnode(bnode, 1);
}

/**
 * @brief		Interface for freeing bnode structure without
 *			saving it into cache.
 * @param [in]	bnode	Node to be freed
 * @return	void
 */
void emmcfs_put_cache_bnode(struct emmcfs_bnode *bnode)
{
	__put_bnode(bnode, 0);
}


/**
 * @brief			preallocate a reseve for splitting btree
 * @param btee			which btree needs bnodes reserve
 * @param alloc_to_bnode_id	at least this bnode will be available after
 *				expanding
 * @return			0 on success or err code
 * */
static int vdfs_prealloc_one_bnode_reserve(struct vdfs_btree *btree,
		__u32 reserving_bnode_id)
{
	struct emmcfs_bnode *bnode;

	bnode = __get_bnode(btree, reserving_bnode_id, 1, EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(bnode))
		return PTR_ERR(bnode);

	EMMCFS_BUG_ON(atomic_dec_return(&bnode->ref_count));
	temp_stub_init_new_node_descr(bnode, 0);
	emmcfs_mark_bnode_dirty(bnode);
	free_bnode(bnode);

	return 0;
}

/**
 * @brief		checks if btree has anough bnodes for safe splitting,
 *			and tries to allocate resever if it has not.
 * @param [in] btree	which btree to check
 * @return		0 if success or error code
 * */
int check_bnode_reserve(struct vdfs_btree *btree)
{
	pgoff_t available_bnodes, free_bnodes, used_bnodes;
	long int bnodes_deficit, new_bnodes_num;
	long int i;
	int ret = 0;

	available_bnodes = VDFS_LAST_TABLE_INDEX(btree->sbi,
			btree->inode->i_ino) + 1;

	used_bnodes = btree->bitmap->bits_num - btree->bitmap->free_num;

	free_bnodes = available_bnodes - used_bnodes;

	bnodes_deficit = (long int) VDFS_BNODE_RESERVE(btree) - free_bnodes;

	if (bnodes_deficit <= 0)
		goto exit;

	new_bnodes_num = available_bnodes + bnodes_deficit;
	for (i = available_bnodes; i < new_bnodes_num; i++) {
		ret = vdfs_prealloc_one_bnode_reserve(btree, i);
		if (ret)
			break;
	}

exit:
	return ret;
}


/**
 * @brief		Mark bnode as free.
 * @param [in]	bnode	Node to be freed
 * @return	void
 */
int emmcfs_destroy_bnode(struct emmcfs_bnode *bnode)
{
	__u32 bnode_id = bnode->node_id;
	struct vdfs_btree *btree = bnode->host;
	struct emmcfs_bnode_bitmap *bitmap = btree->bitmap;
	int err = 0;

	EMMCFS_BUG_ON(bnode_id == EMMCFS_INVALID_NODE_ID);

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	spin_lock(&bnode->get_traces_lock);
#endif

	EMMCFS_BUG_ON(atomic_dec_return(&bnode->ref_count));

#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	free_get_bnode_stack(bnode);
	spin_unlock(&bnode->get_traces_lock);
#endif

	/* TODO: use remove_from_page_cache here, when we have optimised
	 * algorithm for getting bnodes */
	__put_bnode(bnode, 0);

	/*spin_lock(&bitmap->spinlock);*/
	if (!test_and_clear_bit(bnode_id, bitmap->data)) {
		if (!is_sbi_flag_set(btree->sbi, IS_MOUNT_FINISHED))
			return -EFAULT;
		else
			EMMCFS_BUG();
	}

	/* TODO - the comment same as at vdfs_alloc_new_bnode */
	emmcfs_mark_bnode_dirty(btree->head_bnode);
	if (bitmap->first_free_id > bnode_id)
		bitmap->first_free_id = bnode_id;
	bitmap->free_num++;
	/*spin_unlock(&bitmap->spinlock);*/

	/* TODO - seems that it's non-necessary, but let it stay under TODO */
	/*mutex_unlock(btree->node_locks + node_id);*/

	return err;
}

/**
 * @brief			Build free bnode bitmap runtime structure.
 * @param [in]	data		Pointer to reserved memory area satisfying
 *				to keep bitmap
 * @param [in]	start_bnode_id	Starting bnode id to fix into bitmap
 * @param [in]	size_in_bytes	Size of reserved memory area in bytes
 * @param [in]	host_bnode	Pointer to bnode containing essential info
 * @return			Returns pointer to bitmap on success,
 *				error code on failure
 */
struct emmcfs_bnode_bitmap *build_free_bnode_bitmap(void *data,
		__u32 start_bnode_id, __u64 size_in_bytes,
		struct emmcfs_bnode *host_bnode)
{
	struct emmcfs_bnode_bitmap *bitmap;

	if (!host_bnode)
		return ERR_PTR(-EINVAL);

	bitmap = kzalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return ERR_PTR(-ENOMEM);

	bitmap->data = data;
	bitmap->size = size_in_bytes;
	bitmap->bits_num = bitmap->size * 8;

	bitmap->start_id = start_bnode_id;
	bitmap->end_id = start_bnode_id + bitmap->bits_num;

	bitmap->free_num = bitmap->bits_num - __bitmap_weight(bitmap->data,
			bitmap->bits_num);
	bitmap->first_free_id = find_first_zero_bit(bitmap->data,
			bitmap->bits_num) + start_bnode_id;

	bitmap->host = host_bnode;
	spin_lock_init(&bitmap->spinlock);


	return bitmap;
}

/**
 * @brief		Clear memory allocated for bnode bitmap.
 * @param [in]	bitmap	Bitmap to be cleared
 * @return	void
 */
void emmcfs_destroy_free_bnode_bitmap(struct emmcfs_bnode_bitmap *bitmap)
{
	kfree(bitmap);
}

/**
 * @brief		Mark bnode as dirty (data on disk and in memory
 *			differ).
 * @param [in]	bnode	Node to be marked as dirty
 * @return	void
 */
void emmcfs_mark_bnode_dirty(struct emmcfs_bnode *node)
{
	EMMCFS_BUG_ON(!node);
	EMMCFS_BUG_ON(node->mode != EMMCFS_BNODE_MODE_RW);
	vdfs_add_chunk_bnode(node->host->sbi, node->pages);
}

int vdfs_check_and_sign_dirty_bnodes(struct page **page,
		struct vdfs_btree *btree, __u64 version)
{
	void *bnode_data;

	bnode_data = vmap(page, btree->pages_per_node,
			VM_MAP, PAGE_KERNEL);
	if (!bnode_data) {
		printk(KERN_ERR "can not allocate virtual memory");
		return -ENOMEM;
	}
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	meta_sanity_check_bnode(bnode_data, btree, page[0]->index);
#endif
	memcpy(bnode_data + 4, &version, VERSION_SIZE);
#if defined(CONFIG_VDFS_CRC_CHECK)
	emmcfs_calc_crc_for_bnode(bnode_data, btree);
#endif
	vunmap(bnode_data);
	return 0;
}
