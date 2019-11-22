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

#ifndef USER_SPACE
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>

#include "emmcfs.h"

#include <linux/mmzone.h>

#else
#include "vdfs_tools.h"
#endif

#include "btree.h"

#if defined(CONFIG_VDFS_CRC_CHECK)
static inline __le32 __get_checksum_offset_from_btree(struct vdfs_btree *btree)
{
	return btree->node_size_bytes -	EMMCFS_BNODE_FIRST_OFFSET;
}

static __le32 *__get_checksum_addr_for_bnode(void *bnode_data,
		struct vdfs_btree *btree)
{
	void *check_offset = (char *)bnode_data +
		__get_checksum_offset_from_btree(btree);
	__le32 *offset = check_offset;

	return offset;
}

static inline __le32 __calc_crc_for_bnode(void *bnode_data,
		struct vdfs_btree *btree)
{
	return crc32(0, bnode_data, __get_checksum_offset_from_btree(btree));
}

inline void emmcfs_calc_crc_for_bnode(void *bnode_data,
		struct vdfs_btree *btree)
{
	*__get_checksum_addr_for_bnode(bnode_data, btree) =
		__calc_crc_for_bnode(bnode_data, btree);
}
#endif

static struct emmcfs_bnode *alloc_bnode(struct vdfs_btree *btree,
		__u32 node_id, enum emmcfs_get_bnode_mode mode);

static void free_bnode(struct emmcfs_bnode *bnode);


#ifdef CONFIG_VDFS_DEBUG
static void dump_bnode(void *data, struct page **pages,
		unsigned int page_per_node, unsigned int node_size_bytes) {
	int count;

	if (*pages)
		for (count = 0; count < (int)page_per_node; count++)
			pr_emerg("index:%lu phy addr:0x%llx\n",
				pages[count]->index,
				(long long unsigned int)
				page_to_phys(pages[count]));

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS,
			16, 1, data,
			node_size_bytes, 1);
}
#endif

void vdfs_dump_panic_remount(struct emmcfs_bnode *bnode,
		const char *fmt, ...)
{
	struct vdfs_sb_info *sbi = bnode->host->sbi;
	const char *device = sbi->sb->s_id;
#ifdef CONFIG_VDFS_DEBUG
	struct va_format vaf;
	va_list args;
	void *data = bnode->data;

	mutex_lock(&sbi->dump_meta);
	preempt_disable();
	_sep_printk_start();

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	pr_emerg("VDFS(%s): error in %pf, %pV\n", device,
			__builtin_return_address(0), &vaf);
	va_end(args);
	dump_bnode(data, bnode->pages, bnode->host->pages_per_node,
			bnode->host->node_size_bytes);

	preempt_enable();
	_sep_printk_end();
	mutex_unlock(&sbi->dump_meta);
#endif /* CONFIG_VDFS_DEBUG */

#ifdef CONFIG_VDFS_PANIC_ON_ERROR
	panic("VDFS(%s): forced kernel panic after fatal error\n", device);
#else
	if (!(sbi->sb->s_flags & MS_RDONLY)) {
		pr_emerg("VDFS(%s): remount to read-only mode\n", device);
		sbi->sb->s_flags |= MS_RDONLY;
	}
#endif
}

#if defined(CONFIG_VDFS_DEBUG)
static void dump_bnode_to_disk(struct vdfs_sb_info *sbi,
		struct emmcfs_bnode *bnode)
{
	int ret;
	int count;
	struct vdfs_layout_sb *l_sb = sbi->raw_superblock;
	sector_t start_offset = le64_to_cpu(l_sb->exsb.debug_area.begin);
	int debug_area_length = (int)le32_to_cpu(l_sb->exsb.debug_area.length);
	int pages_count;

	pages_count = (debug_area_length < (int)bnode->host->pages_per_node) ?
		debug_area_length : (int)bnode->host->pages_per_node;


	if (VDFS_IS_READONLY(sbi->sb)) {
		EMMCFS_ERR("can not dump bnode to disk: read only fs");
		return;
	}

	EMMCFS_ERR("dump bnode pages to disk");
	for (count = 0; count < pages_count; count++) {
		lock_page(bnode->pages[count]);
		set_page_writeback(bnode->pages[count]);
		ret = vdfs_write_page(sbi, bnode->pages[count],
			((start_offset + (sector_t)count) <<
			(PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT)), 8, 0, 1);
		unlock_page(bnode->pages[count]);
		if (ret)
			break;
	}
}


static void __vdfs_dump_bnode_to_console(struct emmcfs_bnode *bnode, void *data)
{
	struct vdfs_sb_info *sbi = bnode->host->sbi;
	const char *device = sbi->sb->s_id;

	mutex_lock(&sbi->dump_meta);
	preempt_disable();
	_sep_printk_start();
	pr_emerg("VDFS(%s): error in %pf", device, __builtin_return_address(0));
	dump_bnode(data, bnode->pages, bnode->host->pages_per_node,
			bnode->host->node_size_bytes);
	preempt_enable();
	_sep_printk_end();
	mutex_unlock(&sbi->dump_meta);
}

#endif
/**
 * @brief		Extract bnode into memory.
 * @param [in]	bnode	bnode to read
 * @param [in]	create	Flag if to create (1) or get existing (0) bnode
 * @return		Returns zero in case of success, error code otherwise
 */
static int read_or_create_bnode_data(struct emmcfs_bnode *bnode, int create,
		int force_insert)
{
	struct vdfs_btree *btree = bnode->host;
	pgoff_t page_idx;
	pgprot_t prot;
	void *data;
	int ret;
	int count;
	int type = VDFS_META_READ;
#if defined(CONFIG_VDFS_DEBUG)
	int reread_count = VDFS_META_REREAD;
#endif

	page_idx = bnode->node_id * btree->pages_per_node;
	if ((btree->btree_type == VDFS_BTREE_PACK) ||
			(btree->btree_type == VDFS_BTREE_INST_CATALOG) ||
			(btree->btree_type == VDFS_BTREE_INST_XATTR) ||
			(btree->btree_type == VDFS_BTREE_INST_EXT)) {
		page_idx += (pgoff_t)btree->tree_metadata_offset;
		type = VDFS_PACKTREE_READ;
	}
#if defined(CONFIG_VDFS_DEBUG)
do_reread:
#endif
	ret = vdfs_read_or_create_pages(btree->inode, page_idx,
			btree->pages_per_node, bnode->pages,
			type, 0, force_insert);
	if (ret)
		goto err_read;

	prot = PAGE_KERNEL;
	/* Unfortunately ARM don't have PAGE_KERNEL_RO */
#if defined(CONFIG_VDFS_DEBUG) && defined(PAGE_KERNEL_RO)
	if (bnode->mode == EMMCFS_BNODE_MODE_RO)
		prot = PAGE_KERNEL_RO;
#endif
	data = vm_map_ram(bnode->pages, btree->pages_per_node, -1, prot);
	if (!data) {
		EMMCFS_ERR("unable to vmap %d pages", btree->pages_per_node);
		ret = -ENOMEM;
		release_pages(bnode->pages, (int)btree->pages_per_node, 0);
		vdfs_fatal_error(bnode->host->sbi, "vm_map_ram fails");
		goto err_read;
	}

#if defined(CONFIG_VDFS_DEBUG)
	if (create) {
		__u16 poison_val;
		__u16 max_poison_val = (__u16)(btree->pages_per_node <<
			(PAGE_CACHE_SHIFT - 1));
		__u16 *curr_pointer = data;

		for (poison_val = 0; poison_val < max_poison_val;
				poison_val++, curr_pointer++)
			*curr_pointer = poison_val;
	}
#endif

	if (!create && bnode->node_id != 0 &&
			*(u16*)data != EMMCFS_NODE_DESCR_MAGIC) {
		goto err_exit_vunmap;
	}
	if (!create && !PageDirty(bnode->pages[0]) &&
			(btree->btree_type < VDFS_BTREE_PACK)) {
		u16 magic = 0;
		u16 magic_len = 4;
		void *__version = (char *)data + magic_len;

		__le64 real_version, version = vdfs_get_page_version(
				bnode->host->sbi, btree->inode, page_idx);

		real_version = *((__le64 *)(__version));
		if (real_version != version) {
			EMMCFS_ERR("METADATA PAGE VERSION MISMATCH: %s,"
					" inode_num - %lu, bnode_num - %u,"
					" must be -"
					" %u.%u,"
					" real - %u.%u",
					(char *)&magic, btree->inode->i_ino,
					bnode->node_id,
					VDFS_MOUNT_COUNT(version),
					VDFS_SYNC_COUNT(version),
					VDFS_MOUNT_COUNT(real_version),
					VDFS_SYNC_COUNT(real_version));
			goto err_exit_vunmap;
		}
	}
#if defined(CONFIG_VDFS_CRC_CHECK)
	if (create)
		emmcfs_calc_crc_for_bnode(data, btree);
	if ((!create) && (!PageChecked(bnode->pages[0]) &&
			(btree->btree_type != VDFS_BTREE_PACK))) {
		__le32 from_disk = *__get_checksum_addr_for_bnode(data, btree);
		__le32 calculated = __calc_crc_for_bnode(data, btree);

		if (from_disk != calculated) {
			EMMCFS_ERR("CRC missmatch");
			EMMCFS_ERR("Btree Inode id: %lu Bnode id: %u",
					btree->inode->i_ino, bnode->node_id);
			EMMCFS_ERR("Expected %x, recieved: %x",
					calculated, from_disk);
			goto err_exit_vunmap;
		}
	}
#endif
	for (count = 0; count < (int)btree->pages_per_node; count++)
		SetPageChecked(bnode->pages[count]);

	/* publish bnode data */
	bnode->data = data;

#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	if (!create)
		bnode_sanity_check(bnode);
#endif
	return 0;

err_exit_vunmap:
	for (count = 0; count < (int)btree->pages_per_node; count++)
		ClearPageUptodate(bnode->pages[count]);

#ifdef CONFIG_VDFS_DEBUG
	dump_bnode_to_disk(bnode->host->sbi, bnode);
	__vdfs_dump_bnode_to_console(bnode, data);
#endif
	vm_unmap_ram(data, btree->pages_per_node);
	release_pages(bnode->pages, (int)btree->pages_per_node, 0);
	memset(bnode->pages, 0, sizeof(struct page *) * btree->pages_per_node);
#ifdef CONFIG_VDFS_DEBUG
	if (--reread_count >= 0) {
		pr_err("do bnode re-read %d\n",
			VDFS_META_REREAD - reread_count);
		goto do_reread;
	}
#endif
	ret = -EINVAL;
	if (is_sbi_flag_set(btree->sbi, IS_MOUNT_FINISHED))
		vdfs_fatal_error(bnode->host->sbi, "file: bnode validate");

err_read:
	/* publish error code */
	bnode->data = ERR_PTR(ret);
	memset(bnode->pages, 0, sizeof(struct page *) * btree->pages_per_node);
	return ret;
}

static int bnode_hash_fn(__u32 bnode_id)
{
	/*bnode_id = (bnode_id >> 16) + bnode_id;
	bnode_id = (bnode_id >> 8) + bnode_id;*/
	return bnode_id & VDFS_BNODE_HASH_MASK;
}

static void hash_bnode(struct emmcfs_bnode *bnode)
{
	struct vdfs_btree *btree = bnode->host;
	int hash = bnode_hash_fn(bnode->node_id);

	hlist_add_head(&bnode->hash_node, &btree->hash_table[hash]);
}

static void unhash_bnode(struct emmcfs_bnode *bnode)
{
	hlist_del(&bnode->hash_node);
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
			if (mode < node->mode)
				node->mode = mode;
			return node;
		}
	}
	return NULL;
}


#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
static void save_get_bnode_stack(struct emmcfs_bnode *bnode)
{
	struct vdfs_get_bnode_trace *bnode_trace =
		kzalloc(sizeof(*bnode_trace), GFP_KERNEL);

	if (WARN(!bnode_trace, "can not allocate memory for get-bnode-trace"))
		return;

	bnode_trace->trace.nr_entries = 0;
	bnode_trace->trace.entries = &bnode_trace->stack_entries[0];
	bnode_trace->trace.max_entries = VDFS_GET_BNODE_STACK_ITEMS;

	/* How many "lower entries" to skip. */
	bnode_trace->trace.skip = 2;

	save_stack_trace(&bnode_trace->trace);
	bnode_trace->mode = bnode->mode;

	list_add(&bnode_trace->list, &bnode->get_traces_list);
}

static void print_get_bnode_stack(struct emmcfs_bnode *bnode)
{
	struct vdfs_get_bnode_trace *trace;
	int i = 0;

	pr_default("*******************************\n");
	pr_default("Get-traces for bnode #%u\n", bnode->node_id);
	list_for_each_entry(trace, &bnode->get_traces_list, list) {
		pr_err("Stack %2d %s:\n", i++,
			(trace->mode == EMMCFS_BNODE_MODE_RW) ? "rw" : "ro");
		print_stack_trace(&trace->trace, 4);
	}
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
#else
static void save_get_bnode_stack(struct emmcfs_bnode *bnode) { }
static void print_get_bnode_stack(struct emmcfs_bnode *bnode) { }
static void free_get_bnode_stack(struct emmcfs_bnode *bnode) { }
#endif

static DECLARE_WAIT_QUEUE_HEAD(bnode_wq);

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

		if (!test_bit((int)node_id, btree->bitmap->data))
			return ERR_PTR(-ENOENT);
	}

	mutex_lock(&btree->hash_lock);
	bnode = get_bnode_from_hash(btree, node_id, mode);
	if (!bnode) {
		bnode = alloc_bnode(btree, node_id, mode);
		if (IS_ERR(bnode)) {
			mutex_unlock(&btree->hash_lock);
			return bnode;
		}
		hash_bnode(bnode);
		save_get_bnode_stack(bnode);
		mutex_unlock(&btree->hash_lock);
		read_or_create_bnode_data(bnode, 0, 0);
		wake_up_all(&bnode_wq);
	} else {
		bnode->ref_count++;

		/*
		 * Algorithms expects that nobody will take any bnode
		 * recursevely, so, its wrong behaviour if ref_count > 2
		 */
		if (mode == EMMCFS_BNODE_MODE_RW && bnode->ref_count > 2) {
			pr_err("VDFS: bnode %d ref_count %d\n",
					bnode->node_id, bnode->ref_count);
			print_get_bnode_stack(bnode);
		}
		save_get_bnode_stack(bnode);
		mutex_unlock(&btree->hash_lock);
		wait_event(bnode_wq, bnode->data != NULL);
	}

	if (IS_ERR(bnode->data)) {
		void * ret = bnode->data;
		vdfs_put_bnode(bnode);
		return ret;
	}

	if (mode == EMMCFS_BNODE_MODE_RW) {
		vdfs_assert_transaction(btree->sbi);
		vdfs_assert_btree_write(btree);
	} else {
		vdfs_assert_btree_lock(btree);
	}

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
	void * ret;
	vdfs_assert_btree_write(btree);

	if (bitmap->free_num == 0)
		return ERR_PTR(-ENOSPC);

	if (__test_and_set_bit((int)bitmap->first_free_id, bitmap->data)) {
		vdfs_dump_panic_remount(btree->head_bnode,
			"cannot allocate bnode id");
		return ERR_PTR(-EFAULT);
	}

	bnode = alloc_bnode(btree, bitmap->first_free_id, EMMCFS_BNODE_MODE_RW);
	ret = bnode;
	if (IS_ERR(bnode))
		goto out_err;

	if (read_or_create_bnode_data(bnode, 1, 0))
		goto out_err_create;

	/* Updata bitmap information */
	bitmap->first_free_id = (__u32)find_next_zero_bit(bitmap->data,
			(int)bitmap->bits_num, (int)bitmap->first_free_id);
	bitmap->free_num--;
	emmcfs_mark_bnode_dirty(btree->head_bnode);
	mutex_lock(&btree->hash_lock);
	hash_bnode(bnode);
	save_get_bnode_stack(bnode);
	mutex_unlock(&btree->hash_lock);

	return bnode;

out_err_create:
	ret = bnode->data;
	free_bnode(bnode);
out_err:
	__clear_bit((int)bitmap->first_free_id, bitmap->data);
	return ret;
}

static void mark_bnode_accessed(struct emmcfs_bnode *bnode)
{
	unsigned int i;

	if (!bnode->pages[0] || PageActive(bnode->pages[0]))
		return;

	for (i = 0; i < bnode->host->pages_per_node; i++) {
		/* force page activation */
		SetPageReferenced(bnode->pages[i]);
		mark_page_accessed(bnode->pages[i]);
	}
}

static struct emmcfs_bnode *alloc_bnode(struct vdfs_btree *btree,
		__u32 node_id, enum emmcfs_get_bnode_mode mode)
{
	struct emmcfs_bnode *bnode;

	bnode = kzalloc(sizeof(struct emmcfs_bnode) +
			sizeof(struct page *) * btree->pages_per_node,
			GFP_KERNEL);
	if (!bnode)
		return ERR_PTR(-ENOMEM);

	bnode->host = btree;
	bnode->node_id = node_id;
	bnode->mode = mode;
	bnode->ref_count = 1;
#ifdef CONFIG_VDFS_DEBUG_GET_BNODE
	INIT_LIST_HEAD(&bnode->get_traces_list);
#endif

	return bnode;
}

static void free_bnode(struct emmcfs_bnode *bnode)
{
	unsigned int i;

	if (!IS_ERR_OR_NULL(bnode->data))
		vm_unmap_ram(bnode->data, bnode->host->pages_per_node);
	bnode->data = NULL;

	for (i = 0; i < bnode->host->pages_per_node; i++) {
		if (!IS_ERR_OR_NULL(bnode->pages[i]))
			page_cache_release(bnode->pages[i]);
		bnode->pages[i] = NULL;
	}

	free_get_bnode_stack(bnode);
	kfree(bnode);
}

/**
 * @brief		Interface for freeing bnode structure.
 * @param [in]	bnode	Node to be freed
 * @return	void
 */
void vdfs_put_bnode(struct emmcfs_bnode *bnode)
{
	struct vdfs_btree *btree = bnode->host;

	mutex_lock(&btree->hash_lock);
	if (bnode->ref_count <= 0) {
		print_get_bnode_stack(bnode);
		vdfs_dump_panic_remount(btree->head_bnode,
			"ref count less 0");
	}
	if (--bnode->ref_count == 0)
		unhash_bnode(bnode);
	else
		bnode = NULL;
	mutex_unlock(&btree->hash_lock);

	if (bnode) {
		mark_bnode_accessed(bnode);
		free_bnode(bnode);
	}
}

/**
 * @brief			preallocate a reseve for splitting btree
 * @param btee			which btree needs bnodes reserve
 * @param alloc_to_bnode_id	at least this bnode will be available after
 *				expanding
 * @return			0 on success or err code
 * */
static int vdfs_prealloc_one_bnode_reserve(struct vdfs_btree *btree,
		__u32 reserving_bnode_id, int force_insert)
{
	struct emmcfs_bnode *bnode;
	int ret;

	bnode = alloc_bnode(btree, reserving_bnode_id, EMMCFS_BNODE_MODE_RW);
	if (IS_ERR(bnode))
		return PTR_ERR(bnode);

	ret = read_or_create_bnode_data(bnode, 1, force_insert);
	if (ret)
		goto out;
	temp_stub_init_new_node_descr(bnode, 0);
	emmcfs_mark_bnode_dirty(bnode);
out:
	free_bnode(bnode);

	return ret;
}

/**
 * @brief		checks if btree has anough bnodes for safe splitting,
 *			and tries to allocate resever if it has not.
 * @param [in] btree	which btree to check
 * @return		0 if success or error code
 * */
int check_bnode_reserve(struct vdfs_btree *btree, int force_insert)
{
	pgoff_t available_bnodes, free_bnodes, used_bnodes, i, bnodes_deficit;
	pgoff_t new_bnodes_num, reserve;
	int ret = 0;

	reserve = (pgoff_t)(emmcfs_btree_get_height(btree) * 4);

	available_bnodes = (pgoff_t)VDFS_LAST_TABLE_INDEX(btree->sbi,
			btree->inode->i_ino) + 1lu;

	used_bnodes = btree->bitmap->bits_num - btree->bitmap->free_num;

	free_bnodes = available_bnodes - used_bnodes;

	bnodes_deficit = reserve - free_bnodes;

	if (bnodes_deficit <= 0)
		goto exit;

	new_bnodes_num = available_bnodes + bnodes_deficit;
	for (i = available_bnodes; i < new_bnodes_num; i++) {
		ret = vdfs_prealloc_one_bnode_reserve(btree, (__u32)i,
				force_insert);
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


	if (bnode_id == EMMCFS_INVALID_NODE_ID) {
		vdfs_dump_panic_remount(bnode, "invalid bnode id %u", bnode_id);
		return -EINVAL;
	}

	mutex_lock(&btree->hash_lock);
	if (--bnode->ref_count != 0) {
		vdfs_dump_panic_remount(bnode, "invalid ref count %d",
				bnode->ref_count);
		mutex_unlock(&btree->hash_lock);
		return -EFAULT;
	}

	unhash_bnode(bnode);
	mutex_unlock(&btree->hash_lock);

	free_bnode(bnode);

	if (!test_and_clear_bit((int)bnode_id, bitmap->data)) {
		vdfs_dump_panic_remount(btree->head_bnode,
				"already clear bit %u", bnode_id);
		return -EFAULT;
	}

	/* TODO - the comment same as at vdfs_alloc_new_bnode */
	emmcfs_mark_bnode_dirty(btree->head_bnode);
	if (bitmap->first_free_id > bnode_id)
		bitmap->first_free_id = bnode_id;
	bitmap->free_num++;

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
	bitmap->bits_num = (__u32)(bitmap->size * 8);

	bitmap->start_id = start_bnode_id;
	bitmap->end_id = start_bnode_id + bitmap->bits_num;

	bitmap->free_num = (__u32)bitmap->bits_num -
		(__u32)__bitmap_weight(bitmap->data, (int)bitmap->bits_num);
	bitmap->first_free_id = (__u32)find_first_zero_bit(bitmap->data,
			bitmap->bits_num) + start_bnode_id;

	bitmap->host = host_bnode;

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
	if (node->mode != EMMCFS_BNODE_MODE_RW) {
		print_get_bnode_stack(node);
		vdfs_dump_panic_remount(node, "invalid bnode mode %u",
				node->mode);
	}
	vdfs_assert_btree_write(node->host);
	vdfs_add_chunk_bnode(node->host->sbi, node->pages);
}

int vdfs_check_and_sign_dirty_bnodes(struct page **page,
		struct vdfs_btree *btree, __u64 version)
{
	void *bnode_data;

	bnode_data = vm_map_ram(page, btree->pages_per_node,
				-1, PAGE_KERNEL);
	if (!bnode_data) {
		printk(KERN_ERR "can not allocate virtual memory");
		return -ENOMEM;
	}
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	meta_sanity_check_bnode(bnode_data, btree, page[0]->index);
#endif
	memcpy((char *)bnode_data + 4, &version, VERSION_SIZE);
#if defined(CONFIG_VDFS_CRC_CHECK)
	emmcfs_calc_crc_for_bnode(bnode_data, btree);
#endif
	vm_unmap_ram(bnode_data, btree->pages_per_node);
	return 0;
}
