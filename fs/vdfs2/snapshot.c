/**
 * @file	fs/emmcfs/snapshot.c
 * @brief	The eMMCFS snapshot logic.
 * @author	Ivan Arishchenko i.arishchenko@samsung.com
 * @date	TODO
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 * TODO: Detailed description
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

#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/vfs.h>
#include <linux/spinlock_types.h>
#include <linux/vmalloc.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/version.h>

#include "emmcfs.h"


static int expand_special_file(struct vdfs_sb_info *sbi, ino_t ino_n,
		__u64 new_last_iblock);
static void insert_new_iblocks(struct vdfs_sb_info *sbi, ino_t ino_n,
		sector_t new_last_iblock);

/**
 * @brief		Search : task->pid already exist or not.
 * @param [in]	root	Root of the tree to insert.
 * @return		TODO Returns 0 on success, errno on failure.
 */
static inline struct vdfs_task_log *search_for_task_log(struct rb_root *root)
{
	struct task_struct *current_task = current;
	struct rb_node **new = &(root->rb_node);
	struct vdfs_task_log *this = NULL;


	/* Figure out where to put new node */
	while (*new) {
		this = container_of(*new, struct vdfs_task_log, node);

		if (current_task->pid < this->pid)
			new = &((*new)->rb_left);
		else if (current_task->pid > this->pid)
			new = &((*new)->rb_right);
		else
			return this; /* no such pid */
	}

	/* pid already exist */
	return NULL;
}

/**
 * @brief		TODO Search : task->pid already exist or not
 * @param [in]	root	TODO Root of the tree to insert
 * @param [in]	data	TODO
 * @return		Returns 0 on success, errno on failure.
 */
static inline int add_task_log(struct rb_root *root,
		struct vdfs_task_log *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	EMMCFS_BUG_ON(!data);

	/* Figure out where to put new node */
	while (*new) {
		struct vdfs_task_log *this = container_of(*new,
				struct vdfs_task_log, node);

		parent = *new;
		if (data->pid < this->pid)
			new = &((*new)->rb_left);
		else if (data->pid > this->pid)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return 0;
}

/**
 * @brief		Get task log start.
 * @param [in]	sbi	The eMMCFS super block info.
 * @return		TODO Returns 0 on success, errno on failure.
 */
static struct vdfs_task_log *get_task_log_start(struct vdfs_sb_info *sbi)
{
	struct vdfs_task_log *current_task_log;
	struct vdfs_task_log *new_task_log;
	int ret;

	spin_lock(&sbi->snapshot_info->task_log_lock);
	current_task_log =
		search_for_task_log(&sbi->snapshot_info->task_log_root);
	spin_unlock(&sbi->snapshot_info->task_log_lock);

	if (!current_task_log) {
again:
		new_task_log = kzalloc(sizeof(struct vdfs_task_log)
				, GFP_KERNEL);
		if (!new_task_log)
			goto again;

		new_task_log->pid = current->pid;
		spin_lock(&sbi->snapshot_info->task_log_lock);
		current_task_log =
			search_for_task_log(&sbi->snapshot_info->task_log_root);
		if (!current_task_log) {
			ret = add_task_log(&sbi->snapshot_info->task_log_root,
				new_task_log);
			current_task_log = new_task_log;
		} else {
			kfree(new_task_log);
		}
		spin_unlock(&sbi->snapshot_info->task_log_lock);
	}

	return current_task_log;
}

/**
 * @brief		TODO
 * @param [in]	root	TODO Transaction rb-tree root.
 * @return	void
 * */
static inline void destroy_task_log(struct rb_root *root)
{
	struct rb_node *current_node;

	if (!root)
		return;

	current_node = rb_first(root);
	while (current_node) {
		struct rb_node *next_node = rb_next(current_node);
		struct vdfs_task_log *task_log;

		task_log = rb_entry(current_node,
				struct vdfs_task_log, node);

		rb_erase(current_node, root);
		kfree(task_log);

		current_node = next_node;
	}
}

/**
 * @brief		Get task log stop.
 * @param [in]	sbi	The eMMCFS super block info.
 * @return		TODO Returns 0 on success, errno on failure.
 */
static struct vdfs_task_log *get_task_log_stop(struct vdfs_sb_info *sbi)
{
	struct vdfs_task_log *current_task_log;

	spin_lock(&sbi->snapshot_info->task_log_lock);
	current_task_log =
		search_for_task_log(&sbi->snapshot_info->task_log_root);
	spin_unlock(&sbi->snapshot_info->task_log_lock);

	if (!current_task_log)
		EMMCFS_BUG();

	return current_task_log;
}

/**
 * @brief		Start transaction.
 * @param [in]	sbi	The eMMCFS super block info.
 * @return		Returns 0 on success, errno on failure.
 */
int vdfs_start_transaction(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct vdfs_task_log *current_task_log;

	current_task_log = get_task_log_start(sbi);

	if (current_task_log->r_lock_count == 0) {
		EMMCFS_DEBUG_MUTEX("transaction_lock down_read");
		down_read(&sbi->snapshot_info->transaction_lock);
		EMMCFS_DEBUG_MUTEX("transaction_lock down_read success");
	}
	current_task_log->r_lock_count++;
	return ret;
}

/**
 * @brief		Stop transaction.
 * @param [in]	sbi	The eMMCFS super block info.
 * @return		Returns 0 on success, errno on failure.
 */
int vdfs_stop_transaction(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct vdfs_task_log *current_task_log;

	current_task_log = get_task_log_stop(sbi);
	EMMCFS_BUG_ON(!current_task_log->r_lock_count);

	if (current_task_log->r_lock_count == 1) {
		EMMCFS_DEBUG_MUTEX("transaction_lock up_read");
		up_read(&sbi->snapshot_info->transaction_lock);
	}
	current_task_log->r_lock_count--;

	return ret;
}
static __u64 validate_base_table(struct vdfs_base_table *table)
{
	__u32 checksum, on_disk_checksum;
	char is_not_base_table;

	is_not_base_table = strncmp(table->descriptor.signature,
			VDFS_SNAPSHOT_BASE_TABLE,
			sizeof(VDFS_SNAPSHOT_BASE_TABLE) - 1);
	/* signature not match */
	if (is_not_base_table)
		return 0;
	checksum = crc32(0, table,
			le32_to_cpu(table->descriptor.checksum_offset));
	on_disk_checksum = le32_to_cpu(*(__le32 *)((char *)table +
			le32_to_cpu(table->descriptor.checksum_offset)));
	return (checksum != on_disk_checksum) ? 0 :
			VDFS_SNAPSHOT_VERSION(table);

}

static __u64 validate_extended_table(struct vdfs_extended_table *table)
{
	__u32 checksum, on_disk_checksum;
	char is_not_ext_table;

	is_not_ext_table = (strncmp(table->descriptor.signature,
			VDFS_SNAPSHOT_EXTENDED_TABLE,
			sizeof(VDFS_SNAPSHOT_EXTENDED_TABLE) - 1));
	/* signature not match */
	if (is_not_ext_table)
		return 0;

	checksum = crc32(0, table,
			le32_to_cpu(table->descriptor.checksum_offset));

	on_disk_checksum = le32_to_cpu(*(__le32 *)((char *)table +
			le32_to_cpu(table->descriptor.checksum_offset)));
	return (checksum != on_disk_checksum) ? 0 :
			VDFS_SNAPSHOT_VERSION(table);
}

static __u64 parse_extended_table(struct vdfs_sb_info *sbi,
		struct vdfs_base_table *base_table,
		struct vdfs_extended_table *extended_table)
{
	int count;
	__u32 rec_count = le32_to_cpu(extended_table->records_count);
	struct vdfs_extended_record *record;
	__le64 *table;
	__u64 last_table_index;

	record = (struct vdfs_extended_record *)((void *)extended_table +
			sizeof(struct vdfs_extended_table));

	for (count = 0; count < rec_count; count++) {

		table = VDFS_GET_TABLE(sbi, record->object_id);
		last_table_index = VDFS_LAST_TABLE_INDEX(sbi,
				record->object_id);

		if (le64_to_cpu(record->table_index) > last_table_index)
			BUG();

	table[le64_to_cpu(record->table_index)] = record->meta_iblock;
		record++;
	}

	base_table->descriptor.mount_count =
			extended_table->descriptor.mount_count;
	base_table->descriptor.sync_count =
			extended_table->descriptor.sync_count;

	return le32_to_cpu(extended_table->descriptor.checksum_offset) +
			CRC32_SIZE;
}


static __u64 get_base_table_size(struct vdfs_base_table *base_table)
{
	__u64 size = 0;
	size = le64_to_cpu(base_table->descriptor.checksum_offset) + CRC32_SIZE;
	return size;
}


static void build_snapshot_bitmaps(struct vdfs_sb_info *sbi)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	/* current snapshot state */
	unsigned long *snapshot_bitmap = snapshot->snapshot_bitmap;
	/* next snapshot state */
	unsigned long *next_snapshot_bitmap = snapshot->next_snapshot_bitmap;
	unsigned long count, page_index;
	unsigned int table_offset;
	int pos, order;

	__le64 *table;

	for (count = VDFS_FSFILE; count <= VDFS_LSFILE; count++) {
		table_offset = VDFS_GET_TABLE_OFFSET(sbi, count);
		if (!table_offset)
			continue;

		table = VDFS_GET_TABLE(sbi, count);
		order = is_tree(count) ? sbi->log_blocks_in_leb :
				sbi->log_blocks_in_page;
		for (page_index = 0; page_index <=
			VDFS_LAST_TABLE_INDEX(sbi, count); page_index++) {
			pos = (int)le64_to_cpu(*table);

			bitmap_set(snapshot_bitmap, pos, (1 << order));
			table++;
		}
	}

	bitmap_copy(next_snapshot_bitmap, snapshot_bitmap,
				snapshot->bitmap_size_in_bits);

}

/* return value -> phisical tables start sector */
static int restore_exsb(struct vdfs_sb_info *sbi, __u64 version)
{
	struct vdfs_extended_super_block *exsb, *exsb_copy;
	__u64 exsb_version, exsb_copy_version;
	int ret = 0;

	exsb = VDFS_RAW_EXSB(sbi);
	exsb_copy = VDFS_RAW_EXSB_COPY(sbi);
	exsb_version = VDFS_EXSB_VERSION(exsb);
	exsb_copy_version = VDFS_EXSB_VERSION(exsb_copy);

	if (exsb_version == exsb_copy_version)
		return 0;

	if (version == exsb_version) {
		memcpy(exsb_copy, exsb,
			sizeof(struct vdfs_extended_super_block));
		ret = vdfs_sync_second_super(sbi);
	} else {
		memcpy(exsb, exsb_copy,
			sizeof(struct vdfs_extended_super_block));
		ret = vdfs_sync_first_super(sbi);
	}

	return ret;

}


static int get_table_version(struct vdfs_sb_info *sbi,
		sector_t start, sector_t length, __u64 *version) {

	unsigned long pages_per_table = length >> (PAGE_CACHE_SHIFT -
			SECTOR_SIZE_SHIFT);
	struct page **pages;
	int count, ret = 0;
	void *table;

	*version = 0;
	pages = kzalloc(sizeof(struct page *) * pages_per_table, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (count = 0; count < pages_per_table; count++) {
		pages[count] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pages[count]) {
			ret = -ENOMEM;
			goto exit;
		}
	}
	ret = vdfs_table_IO(sbi, pages, length, READ, &start);
	if (ret)
		goto exit;
	table = vmap(pages, pages_per_table, VM_MAP, PAGE_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto exit;
	}
	*version = validate_base_table(table);
	vunmap(table);
exit:
	count--;
	for (; count > -1; count--)
		__free_page(pages[count]);
	kfree(pages);
	return ret;

}

/* return value - first start sector of the extended tables */
static int load_base_table(struct vdfs_sb_info *sbi,
		sector_t *ext_table_start, __u64 *table_version_out)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	struct vdfs_extended_super_block *exsb_copy = VDFS_RAW_EXSB_COPY(sbi);
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct page **table_pages;
	sector_t table_start;
	__u64 table1_version, table2_version, table3_version = 0, table_version;
	__u32 meta__tbc, meta__tbc_copy, table_tbc, table_tbc_copy;
	unsigned long pages_per_tables, sectors_per_tables;
	unsigned long tables_size_in_bytes;
	int ret = 0, count;
	void *table;

	meta__tbc = le32_to_cpu(exsb->meta_tbc);
	meta__tbc_copy = le32_to_cpu(exsb_copy->meta_tbc);
	table_tbc = le32_to_cpu(exsb->tables_tbc);
	table_tbc_copy = le32_to_cpu(exsb_copy->tables_tbc);

	tables_size_in_bytes = sizeof(struct vdfs_base_table) + CRC32_SIZE +
				sizeof(__le64) * (meta__tbc >> 1);
	pages_per_tables = DIV_ROUND_UP(tables_size_in_bytes, PAGE_CACHE_SIZE);
	sectors_per_tables = pages_per_tables << (PAGE_CACHE_SHIFT -
			SECTOR_SIZE_SHIFT);

	ret = get_table_version(sbi, 0, sectors_per_tables, &table1_version);
	if (ret)
		return ret;

	ret = get_table_version(sbi, (((sector_t)table_tbc) >> 1) <<
		(sbi->block_size_shift - SECTOR_SIZE_SHIFT), sectors_per_tables,
			&table2_version);
	if (ret)
		return ret;

	if (meta__tbc != meta__tbc_copy) {
		void *exsb_temp;
		exsb_temp = sbi->raw_superblock;
		sbi->raw_superblock = sbi->raw_superblock_copy;
		tables_size_in_bytes = sizeof(struct vdfs_base_table) +
			CRC32_SIZE + sizeof(__le64) * (meta__tbc_copy >> 1);
		pages_per_tables = DIV_ROUND_UP(tables_size_in_bytes,
				PAGE_CACHE_SIZE);
		sectors_per_tables = pages_per_tables << (PAGE_CACHE_SHIFT -
				SECTOR_SIZE_SHIFT);
		ret = get_table_version(sbi, ((sector_t)table_tbc_copy >> 1) <<
			(sbi->block_size_shift - SECTOR_SIZE_SHIFT),
			sectors_per_tables, &table3_version);
		sbi->raw_superblock = exsb_temp;
		if (ret)
			return ret;
	}

	if ((table1_version > table2_version) &&
			(table1_version > table3_version)) {
		table_version = table1_version;
		table_start = 0;
	} else if ((table2_version > table1_version) &&
			(table2_version > table3_version)) {
		table_version = table2_version;
		table_start = ((sector_t)table_tbc >> 1) <<
			(sbi->block_size_shift - SECTOR_SIZE_SHIFT);
	} else if ((table3_version > table1_version) &&
			(table3_version > table2_version)) {
		/* extended super block recovery from copy */
		table_version = table3_version;
		table_start = (table_tbc_copy >> 1) << (sbi->block_size_shift -
				SECTOR_SIZE_SHIFT);
		meta__tbc = meta__tbc_copy;
	} else {
		ret = -EINVAL;
		goto exit;
	}

	tables_size_in_bytes = sizeof(struct vdfs_base_table) + CRC32_SIZE +
				sizeof(__le64) * (meta__tbc >> 1);
	pages_per_tables = DIV_ROUND_UP(tables_size_in_bytes, PAGE_CACHE_SIZE);
	sectors_per_tables = pages_per_tables << (PAGE_CACHE_SHIFT -
			SECTOR_SIZE_SHIFT);

	ret = restore_exsb(sbi, table_version);
	if (ret)
		return ret;

	table_pages = kzalloc(sizeof(struct page *) * pages_per_tables,
			GFP_KERNEL);
	if (!table_pages)
		return -ENOMEM;

	for (count = 0; count < pages_per_tables; count++) {
		table_pages[count] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!table_pages[count]) {
			ret = -ENOMEM;
			goto error_exit;
		}
	}

	*ext_table_start = table_start;
	*table_version_out = table_version;
	ret = vdfs_table_IO(sbi, table_pages, sectors_per_tables, READ,
			&table_start);
	if (ret)
		goto error_exit;
	table = vmap(table_pages, pages_per_tables, VM_MAP, PAGE_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto error_exit;
	}
	table_version = validate_base_table(table);
	if (!table_version) {
		ret = -EINVAL;
		vunmap(table);
	}


	snapshot->tables_pages_count = pages_per_tables;
	snapshot->base_table = table_pages;
	snapshot->base_t = table;
	*ext_table_start += DIV_ROUND_UP(get_base_table_size(snapshot->base_t),
			SECTOR_SIZE);

exit:
	return ret;

error_exit:
	count--;
	for (; count > 0; count--)
		__free_page(table_pages[count]);
	kfree(table_pages);

	return ret;
}


static int load_extended_table(struct vdfs_sb_info *sbi, sector_t start)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	int ret = 0;

	if (!snapshot->extended_table[0]) {

		snapshot->extended_table[0] =
				alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!snapshot->extended_table[0])
			return -ENOMEM;

		snapshot->extended_t = vmap(snapshot->extended_table,
				1, VM_MAP, PAGE_KERNEL);
		if (!snapshot->extended_t) {
			ret = -ENOMEM;
			goto exit;
		}
	}
	ret = vdfs_table_IO(sbi, snapshot->extended_table, 8, READ, &start);
	return ret;
exit:
	__free_page(snapshot->extended_table[0]);
	return ret;
}

int vdfs_build_snapshot_manager(struct vdfs_sb_info *sbi)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	struct vdfs_snapshot_info *snapshot;
	int ret = 0;
	__u64 meta_tbc, base_table_version, ext_table_version;
	__u32 base_table_mount_count, base_table_sync_count;
	__u32 ext_table_mount_count, ext_table_sync_count;
	unsigned long bitmaps_length;
	unsigned long tables_size_in_bytes;
	unsigned long pages_per_tables, sectors_per_tables;
	sector_t isector = 0;
	int count;

	snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;
	sbi->snapshot_info = snapshot;
	ret = load_base_table(sbi, &isector, &base_table_version);
	if (ret)
		goto error_exit_free_snapshot;
	meta_tbc = le64_to_cpu(exsb->meta_tbc);
	bitmaps_length = sizeof(unsigned long) * BITS_TO_LONGS(meta_tbc);

	snapshot->snapshot_bitmap = kzalloc(bitmaps_length, GFP_KERNEL);
	snapshot->moved_iblock = kzalloc(bitmaps_length, GFP_KERNEL);
	snapshot->next_snapshot_bitmap = kzalloc(bitmaps_length, GFP_KERNEL);
	snapshot->bitmap_size_in_bits = meta_tbc;

	if (!snapshot->snapshot_bitmap || !snapshot->moved_iblock ||
			!snapshot->next_snapshot_bitmap) {
		ret = -ENOMEM;
		goto error_exit;
	}

	tables_size_in_bytes = sizeof(struct vdfs_base_table) +
		CRC32_SIZE + sizeof(__le64) * (meta_tbc >> 1);
	pages_per_tables = DIV_ROUND_UP(tables_size_in_bytes,
			PAGE_CACHE_SIZE);
	sectors_per_tables = pages_per_tables << (PAGE_CACHE_SHIFT -
			SECTOR_SIZE_SHIFT);

	for (count = 0; count < VDFS_SNAPSHOT_EXT_TABLES; count++) {
		unsigned long size;
		struct vdfs_base_table *base_table = snapshot->base_t;
		base_table_mount_count = VDFS_MOUNT_COUNT(base_table_version);
		base_table_sync_count = VDFS_SYNC_COUNT(base_table_version);
		ret = load_extended_table(sbi, isector);
		if (ret)
			goto error_exit;
		ext_table_version = validate_extended_table(
				(struct vdfs_extended_table *)
				snapshot->extended_t);
		ext_table_mount_count = VDFS_MOUNT_COUNT(ext_table_version);
		ext_table_sync_count = VDFS_SYNC_COUNT(ext_table_version);
		if ((ext_table_mount_count != base_table_mount_count) ||
				(ext_table_sync_count !=
						base_table_sync_count + 1))
			break;
		size = parse_extended_table(sbi,
			(struct vdfs_base_table *) snapshot->base_t,
			(struct vdfs_extended_table *) snapshot->extended_t);

		base_table_version = VDFS_SNAPSHOT_VERSION(base_table);
		isector += DIV_ROUND_UP(size, SECTOR_SIZE);
		snapshot->exteneded_table_count++;
	}

	snapshot->isector = isector;
	build_snapshot_bitmaps(sbi);

	init_rwsem(&snapshot->tables_lock);
	init_rwsem(&snapshot->transaction_lock);
	spin_lock_init(&snapshot->task_log_lock);

	memset(snapshot->extended_t, 0x0, sizeof(struct vdfs_extended_table));
	return ret;

error_exit:
	kfree(snapshot->snapshot_bitmap);
	kfree(snapshot->moved_iblock);
	kfree(snapshot->next_snapshot_bitmap);
error_exit_free_snapshot:
	kfree(snapshot);
	sbi->snapshot_info = NULL;

	return ret;
}

/**
 * @brief		Destroys snapshot manager.
 * @param [in]	sbi	The eMMCFS super block info.
 * @return	void
 */
void emmcfs_destroy_snapshot_manager(struct vdfs_sb_info *sbi)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	int count;

	sbi->snapshot_info = NULL;

	vunmap(snapshot->base_t);
	vunmap(snapshot->extended_t);
	for (count = 0; count < snapshot->tables_pages_count; count++)
		__free_page(snapshot->base_table[count]);
	__free_page(snapshot->extended_table[0]);
	kfree(snapshot->base_table);
	kfree(snapshot->snapshot_bitmap);
	kfree(snapshot->moved_iblock);
	kfree(snapshot->next_snapshot_bitmap);
	destroy_task_log(&snapshot->task_log_root);
	kfree(snapshot);
}


static __u64 get_new_meta_iblock(struct vdfs_sb_info *sbi, ino_t ino_n)
{
	int hint, nr, align_mask, alignment;
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	unsigned long *snapshot_bitmap = snapshot->snapshot_bitmap;
	unsigned long *next_snapshot_bitmap = snapshot->next_snapshot_bitmap;
	unsigned long int size = snapshot->bitmap_size_in_bits;
	unsigned long position;


	if (is_tree(ino_n)) {
		nr = 1 << sbi->log_blocks_in_leb;
		alignment = (unsigned long)sbi->log_blocks_in_leb;
		hint = sbi->snapshot_info->hint_btree;
	} else {
		nr = 1 << sbi->log_blocks_in_page;
		alignment = (unsigned long)sbi->log_blocks_in_page;

		hint = sbi->snapshot_info->hint_bitmaps;
	}
	/*1 lookup for new iblock in snapshot_bitmap */
	align_mask = (1 << alignment) - 1;
	position = bitmap_find_next_zero_area(snapshot_bitmap, size, hint, nr,
			align_mask);

	if (position > size) {
		position = bitmap_find_next_zero_area(snapshot_bitmap, size, 0,
				nr, align_mask);
		if (position > size)
			BUG();
	}

	/*2 set bits in snapshot_bitmap & next_snapshot_bitmaps */
	bitmap_set(snapshot_bitmap, (int)position, (1 << alignment));
	bitmap_set(next_snapshot_bitmap, (int)position, (1 << alignment));
	bitmap_set(snapshot->moved_iblock, (int)position, (1 << alignment));

	hint = position + nr;
	if (is_tree(ino_n))
		sbi->snapshot_info->hint_btree = hint;
	else
		sbi->snapshot_info->hint_bitmaps = hint;


	return (__u64)position;
}


static void move_iblock(struct vdfs_sb_info *sbi, ino_t ino_n,
		__u64 current_iblock, pgoff_t page_index, __u64 *new_iblock)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	unsigned long *next_snapshot_bitmap = snapshot->next_snapshot_bitmap;
	unsigned long position;
	__le64 *table = VDFS_GET_TABLE(sbi, ino_n);
	__u64 last_table_index = VDFS_LAST_TABLE_INDEX(sbi, ino_n);
	__u64 table_index;
	int nr;

	if (is_tree(ino_n))
		nr = 1 << sbi->log_blocks_in_leb;
	else
		nr = 1 << sbi->log_blocks_in_page;


	table_index = GET_TABLE_INDEX(sbi, ino_n, page_index);
	BUG_ON(table_index > last_table_index);
	position = get_new_meta_iblock(sbi, ino_n);
	bitmap_clear(next_snapshot_bitmap, (int)current_iblock, nr);

	table[table_index] = cpu_to_le64(position);
	*new_iblock = position;
}

static void update_extended_table(struct vdfs_sb_info *sbi, ino_t ino_n,
		sector_t page_index, __u64 new_iblock)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct vdfs_extended_table *ext_table = snapshot->extended_t;
	__u32 rec_count = le32_to_cpu(ext_table->records_count);
	__u32 new_table_size;
	struct vdfs_extended_record *record;
	__u64 table_index;

	table_index = GET_TABLE_INDEX(sbi, ino_n, page_index);

	if (snapshot->use_base_table)
		return;

	new_table_size = (rec_count + 1) * sizeof(struct vdfs_extended_record) +
			sizeof(struct vdfs_extended_table) + CRC32_SIZE;

	if (new_table_size > PAGE_CACHE_SIZE) {
		snapshot->use_base_table = 1;
		return;
	}

	record = snapshot->extended_t + sizeof(struct vdfs_extended_table) +
			rec_count * sizeof(struct vdfs_extended_record);
	record->meta_iblock = cpu_to_le64(new_iblock);
	record->object_id = (char)ino_n;
	record->table_index = cpu_to_le64(table_index);
	ext_table->records_count = cpu_to_le32(rec_count + 1);
}


void vdfs_add_chunk_no_lock(struct vdfs_sb_info *sbi, ino_t object_id,
		pgoff_t page_index)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	unsigned long *moved_iblock = snapshot->moved_iblock;
	__u64 meta_iblock, new_iblock;
	int ret;

	BUG_ON((object_id < VDFS_FSFILE) || (object_id > VDFS_LSFILE));
	/* inconsistent lock state */
	BUG_ON(sbi->snapshot_info->flags);

	ret = vdfs_get_meta_iblock(sbi, object_id, page_index,
			&meta_iblock);
	BUG_ON(ret);

	ret = test_bit(meta_iblock, moved_iblock);
	if (ret)
		return;

	move_iblock(sbi, object_id, meta_iblock, page_index, &new_iblock);

	if (is_tree(object_id))
		BUG_ON((new_iblock) & ((1 << sbi->log_blocks_in_leb) - 1));

	update_extended_table(sbi, object_id, page_index, new_iblock);

	snapshot->dirty_pages_count += (is_tree(object_id)) ?
			(1 << sbi->log_blocks_in_leb) : 1;
}

static int get_bnode_size_in_pages(struct vdfs_sb_info *sbi, ino_t i_ino)
{
	switch (i_ino) {
	case VDFS_CAT_TREE_INO:
		return sbi->catalog_tree->pages_per_node;
	case VDFS_EXTENTS_TREE_INO:
		return sbi->extents_tree->pages_per_node;
	case VDFS_HARDLINKS_TREE_INO:
		return sbi->hardlink_tree->pages_per_node;
	case VDFS_XATTR_TREE_INO:
		return sbi->xattr_tree->pages_per_node;
	default:
		break;
	}
	BUG();
	return 0;
}

#ifdef CONFIG_VDFS_DEBUG

void vdfs_check_moved_iblocks(struct vdfs_sb_info *sbi, struct page **pages,
		int page_count)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	unsigned long *moved_iblock = snapshot->moved_iblock;
	ino_t i_ino = pages[0]->mapping->host->i_ino;
	sector_t meta_iblock;
	int count = 0, chunk_size, i;
	chunk_size = (is_tree(i_ino)) ? get_bnode_size_in_pages(sbi, i_ino) : 1;
	do {
		/* page->index must be exist in snapshot tables */
		BUG_ON(vdfs_get_meta_iblock(sbi, i_ino, pages[count]->index,
				&meta_iblock));
		/* page must be moved */
		for (i = 0; i < chunk_size; i++) {
			BUG_ON(!test_and_clear_bit(meta_iblock, moved_iblock));
			meta_iblock++;
			count++;
		}
	} while (count < page_count);
}
#endif

void vdfs_add_chunk_bitmap(struct vdfs_sb_info *sbi, struct page *page,
		int lock)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	ino_t i_ino = page->mapping->host->i_ino;

	if (PageDirty(page))
		return;
	set_page_dirty(page);

	if (lock)
		down_write(&snapshot->tables_lock);
	vdfs_add_chunk_no_lock(sbi, i_ino, page->index);
	if (lock)
		up_write(&snapshot->tables_lock);
}

void vdfs_add_chunk_bnode(struct vdfs_sb_info *sbi, struct page **pages)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	ino_t i_ino = pages[0]->mapping->host->i_ino;
	int bnode_size_in_pages = get_bnode_size_in_pages(sbi, i_ino);
	int count;

	if (PageDirty(pages[0])) {
		for (count = 0; count < bnode_size_in_pages; count++)
			BUG_ON(!PageDirty(pages[count]));
		/* pages is diry - already moved to new place */
		return;
	}
	for (count = 0; count < bnode_size_in_pages; count++)
		set_page_dirty_lock(pages[count]);
	down_write(&snapshot->tables_lock);
	vdfs_add_chunk_no_lock(sbi, i_ino, pages[0]->index);
	up_write(&snapshot->tables_lock);
}


/**
 * @brief		Check offset: space for page with page_index
 *			is allocated on volume or not; If not allocated
 *			then expand file;
 * @param [in]	sbi	VDFS run-time superblock
 * @return		none
 */
int vdfs_check_page_offset(struct vdfs_sb_info *sbi, ino_t object_id,
		pgoff_t page_index, char *is_new)
{
	sector_t last_table_index;
	sector_t table_index;
	int ret = 0;

	down_write(&sbi->snapshot_info->tables_lock);

	last_table_index = VDFS_LAST_TABLE_INDEX(sbi, object_id);
	if (is_tree(object_id))
		table_index = page_index >> (sbi->log_blocks_in_leb +
				sbi->block_size_shift - PAGE_SHIFT);
	else
		table_index = page_index;

	if (table_index > last_table_index) {
		/* special file extention */
		ret = expand_special_file(sbi, object_id, table_index);
		if (ret)
			goto exit;
		*is_new = 1;
		sbi->snapshot_info->use_base_table = 1;
	} else
		*is_new = 0;
exit:
	up_write(&sbi->snapshot_info->tables_lock);

	return ret;
}

/**
 * @brief		Fill full table descriptor and calculate table CRC
 * @param [in]	sbi	VDFS run-time superblock
 * @param [in]	table	pointer to buffer
 * @param [in]	type	table type : base table or extended table
 * @return		none
 */
static void fill_descriptor_and_calculate_crc(struct vdfs_sb_info *sbi,
		void *table, enum snapshot_table_type type)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct vdfs_snapshot_descriptor *descriptor = table;
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	int count;
	__u32 *checksum;

	if (type == SNAPSHOT_BASE_TABLE) {
		struct vdfs_base_table *base_table = table;
		__u64 last_table_index;

		memcpy((void *)&descriptor->signature, VDFS_SNAPSHOT_BASE_TABLE,
				sizeof(VDFS_SNAPSHOT_BASE_TABLE) - 1);

		base_table->descriptor.checksum_offset = 0;
		for (count = VDFS_FSFILE; count <= VDFS_LSFILE; count++) {
			last_table_index = VDFS_LAST_TABLE_INDEX(sbi, count);
			base_table->descriptor.checksum_offset +=
				sizeof(__le64) * (last_table_index + 1);
		}

		base_table->descriptor.checksum_offset += sizeof(*base_table);

		descriptor->checksum_offset =
				cpu_to_le32(descriptor->checksum_offset);
	} else if (type == SNAPSHOT_EXT_TABLE) {
		struct vdfs_extended_table *ext_table = table;

		memcpy((void *)&descriptor->signature,
				VDFS_SNAPSHOT_EXTENDED_TABLE,
				sizeof(VDFS_SNAPSHOT_EXTENDED_TABLE) - 1);

		ext_table->descriptor.checksum_offset = sizeof(*ext_table) +
				sizeof(struct vdfs_extended_record) *
				le32_to_cpu(ext_table->records_count);
	} else
		EMMCFS_BUG();

	descriptor->mount_count = exsb->mount_counter;
	descriptor->sync_count = cpu_to_le32(snapshot->sync_count);

	checksum = (__u32 *)((char *)table + descriptor->checksum_offset);
	/* sign snapshot : crc32 */
	*checksum = crc32(0, table, descriptor->checksum_offset);

}

#define VDFS_SNAPSHOT_TABLES_COUNT 8

/**
 * @brief		Finalizing metadata update by writing a translation
 *			tables on disk
 * @param [in]	sbi	VDFS run-time superblock
 * @param [in]	iblock	iblock - key for searching
 * @return		Returns 0 on success, errno on failure.
 */
int update_translation_tables(struct vdfs_sb_info *sbi)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct vdfs_snapshot_descriptor *descriptor;
	sector_t sectors_count;
	struct page **table;
	void *mapped_table;
	unsigned int table_size;
	sector_t isector = snapshot->isector;
	int ret = 0;

	enum snapshot_table_type type;

	if (snapshot->exteneded_table_count >= VDFS_SNAPSHOT_TABLES_COUNT ||
			snapshot->use_base_table) {
		struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
		__u32 table_ttb = le32_to_cpu(exsb->tables_tbc);
		__u64 table_sectors_count = table_ttb << (PAGE_CACHE_SHIFT -
				SECTOR_SIZE_SHIFT);

		type = SNAPSHOT_BASE_TABLE;
		table = snapshot->base_table;
		snapshot->use_base_table = 0;
		snapshot->exteneded_table_count = 0;
		mapped_table = snapshot->base_t;

		isector = (isector > (table_sectors_count >> 1)) ? 0 :
				table_sectors_count >> 1;


	} else {
		type = SNAPSHOT_EXT_TABLE;
		snapshot->exteneded_table_count++;
		mapped_table = snapshot->extended_t;
		table = snapshot->extended_table;
	}

	fill_descriptor_and_calculate_crc(sbi, mapped_table, type);
	descriptor = (struct vdfs_snapshot_descriptor *)mapped_table;

	table_size = le32_to_cpu(descriptor->checksum_offset) + CRC32_SIZE;
	sectors_count = (DIV_ROUND_UP(table_size, SECTOR_SIZE));

	ret = vdfs_table_IO(sbi, table, sectors_count, WRITE_FLUSH_FUA,
			&isector);
	snapshot->isector = isector;
	snapshot->sync_count++;
	snapshot->dirty_pages_count = 0;
	memset(snapshot->extended_t, 0x0, sizeof(struct vdfs_extended_table));
#ifdef CONFIG_VDFS_STATISTIC
	sbi->umount_written_bytes += (sectors_count << SECTOR_SIZE_SHIFT);
#endif
	return ret;
}


void vdfs_update_bitmaps(struct vdfs_sb_info *sbi)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	unsigned long *moved_iblock = snapshot->moved_iblock;
	unsigned long *snapshot_bitmap = snapshot->snapshot_bitmap;
	unsigned long *next_snapshot_bitmap = snapshot->next_snapshot_bitmap;
	int nbits = snapshot->bitmap_size_in_bits;

#ifdef CONFIG_VDFS_DEBUG
	/* moved iblock bitmap must be empty after metadata updating */
	BUG_ON(!bitmap_empty(moved_iblock, nbits));
#endif
	bitmap_zero(moved_iblock, nbits);
	bitmap_copy(snapshot_bitmap, next_snapshot_bitmap, nbits);
}

/**
 * @brief		Calculation metadata area expand step size
 * @param [in]	sbi	VDFS run-time superblock
 * @return		Returns expand step size in blocks.
 */
static __u32 calculate_expand_step(struct vdfs_sb_info *sbi)
{
	/* minimal snapshot area size =
	 * 4 btree 2 *(catalog, extents, hlins and xattr) * bnode_size +
	 * 3 bimaps (free space, inode id and small files) * block_size)
	 */
	loff_t volume_size = sbi->sb->s_bdev->bd_inode->i_size;
	loff_t minimal_expand_step = 2 * (4 * (1llu <<
		sbi->log_super_page_size) + 3 * sbi->block_size);
	__u32 expand_step;
	char is_volume_small;
	__u32 blocks_per_superpage = 1 << (sbi->log_super_page_size -
					sbi->log_block_size);
	do_div(volume_size, 50);
	is_volume_small = (minimal_expand_step > volume_size) ?
			1 : 0;
	minimal_expand_step >>= sbi->log_block_size;

	/* allign to superpage size */
	minimal_expand_step += blocks_per_superpage -
			(minimal_expand_step & (blocks_per_superpage - 1));

	expand_step = (is_volume_small) ? minimal_expand_step :
		(1 << (sbi->log_erase_block_size - sbi->log_block_size));

	return expand_step;
}

/**
 * @brief		Expand area on disk (metadata area or tables area) by
 *			expand_block_count
 * @param [in]	sbi	VDFS run-time superblock
 * @param [in]	extents			VDFS extents massive
 * @param [in]	extents_count		How many extents in massive
 * @param [in]	expand_block_count	Expand step size in blocks
 * @param [out] tbc			Total blocks count
 * @return		Returns 0 on success, errno on failure.
 */
static int expand_area(struct vdfs_sb_info *sbi, struct vdfs_extent *extents,
		unsigned int extents_count, unsigned int expand_block_count,
		__le32 *tbc)
{
	int count;
	__u64 start_offset;

	/* lookup for last used extent */
	for (count = 0; count < extents_count; count++) {
		if (extents->begin == 0)
			break;
		extents++;
	}
	if (count >= extents_count)
		return -EINVAL;

	extents--;
	start_offset = le64_to_cpu(extents->begin) +
			le32_to_cpu(extents->length);

	start_offset = emmcfs_fsm_get_free_block(sbi, start_offset,
			&expand_block_count, 1, 0, 0);

	if (!start_offset)
		return -ENOSPC;

	if (le64_to_cpu(extents->begin) + le32_to_cpu(extents->length) ==
			start_offset) {
		extents->length = cpu_to_le32(le32_to_cpu(extents->length) +
				expand_block_count);
	} else {
		extents++;
		extents->begin = cpu_to_le64(start_offset);
		extents->length = cpu_to_le32(expand_block_count);
	}
	*tbc = cpu_to_le32(le32_to_cpu(*tbc) + expand_block_count);

	return 0;

}

static unsigned int calculate_max_table_size(struct vdfs_sb_info *sbi)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	__u32 meta_tbc = le32_to_cpu(exsb->meta_tbc);

	return sizeof(struct vdfs_base_table) + sizeof(__le64) * (meta_tbc >> 1)
			+ CRC32_SIZE;
}

#if 0
static unsigned int calculate_base_table_size(struct vdfs_sb_info *sbi)
{
	int count;
	__u64 last_table_index = 0;
	unsigned int size = 0;

	for (count = VDFS_FSFILE; count <= VDFS_LSFILE; count++) {
		last_table_index = VDFS_LAST_TABLE_INDEX(sbi, count);
		size += (last_table_index + 1) * sizeof(__le64);
	}

	size += sizeof(struct vdfs_base_table) + CRC32_SIZE;

	return size;
}
#endif

static void insert_new_iblocks(struct vdfs_sb_info *sbi, ino_t ino_n,
		sector_t new_last_table_index)
{
	void *dest, *src;
	unsigned long size;
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct vdfs_base_table *base_table = snapshot->base_t;
	__le64 *table = VDFS_GET_TABLE(sbi, ino_n);
	__u64 last_table_index = VDFS_LAST_TABLE_INDEX(sbi, ino_n), offset;
	int count;
	int add_size = sizeof(__le64) * (new_last_table_index -
			last_table_index);
	BUG_ON(last_table_index > new_last_table_index);

	src = (char *)table + sizeof(__le64) * (last_table_index + 1);
	dest = (char *)src + add_size;
	size = (unsigned long)le32_to_cpu(base_table->
		descriptor.checksum_offset) + (unsigned long)base_table -
		(unsigned long)src;

	memmove(dest, src, size);

	for (count = VDFS_FSFILE; count <= VDFS_LSFILE; count++) {
		if (base_table->translation_table_offsets[VDFS_SF_INDEX(ino_n)]
		>= base_table->translation_table_offsets[VDFS_SF_INDEX(count)])
			continue;
		offset = le64_to_cpu(base_table->
			translation_table_offsets[VDFS_SF_INDEX(count)]);
		offset += add_size;
		base_table->translation_table_offsets[VDFS_SF_INDEX(count)] =
			cpu_to_le64(offset);
	}

	offset = le32_to_cpu(base_table->descriptor.checksum_offset);
	offset += add_size;
	base_table->descriptor.checksum_offset = cpu_to_le32(offset);

	for (count = last_table_index + 1; count <= new_last_table_index;
			count++) {
		table[count] = cpu_to_le64(get_new_meta_iblock(sbi, ino_n));

/*
		printk(KERN_ERR "insert ino %d, table_index iblock %lu",
				(int)ino_n,
				table[count]
*/
	}
	base_table->last_page_index[VDFS_SF_INDEX(ino_n)] =
			cpu_to_le32(new_last_table_index);

	if (is_tree(ino_n))
		snapshot->dirty_pages_count += (1 << sbi->log_blocks_in_leb) *
		(new_last_table_index - last_table_index);
	else
		snapshot->dirty_pages_count += (new_last_table_index -
				last_table_index);

}

static int expand_translation_tables(struct vdfs_sb_info *sbi, ino_t ino_n,
		__u64 new_table_index)
{
	struct vdfs_snapshot_info *snapshot = sbi->snapshot_info;
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	unsigned int current_page_count = snapshot->tables_pages_count;
	__u32 meta_tbc = le32_to_cpu(exsb->meta_tbc);
	__u64 current_last_table_index;
	unsigned int new_table_size, new_pages_count, bitmaps_length,
		old_bitmap_length;
	int ret = 0;
	int count;
	struct page **pages = NULL;
	void *mapped_base_table;

	unsigned long *snapshot_bitmap = NULL, *moved_iblock = NULL,
		*next_snapshot_bitmap = NULL;

	current_last_table_index = VDFS_LAST_TABLE_INDEX(sbi, ino_n);
	new_table_size = calculate_max_table_size(sbi);
	new_pages_count = DIV_ROUND_UP(new_table_size, PAGE_CACHE_SIZE);

	if (new_pages_count > current_page_count) {
		unsigned int base_table_in_blocks, extended_table_blocks;
		__u32 table_tbc = le32_to_cpu(exsb->tables_tbc);
		int blocks_in_page;

		blocks_in_page = 1 << sbi->log_blocks_in_page;

		base_table_in_blocks = DIV_ROUND_UP(new_table_size,
				sbi->block_size);
		extended_table_blocks = 2 * VDFS_SNAPSHOT_TABLES_COUNT *
				blocks_in_page;

		if (base_table_in_blocks + extended_table_blocks > table_tbc) {
			ret = expand_area(sbi, &exsb->tables[0],
					VDFS_TABLES_EXTENTS_COUNT,
					table_tbc, &exsb->tables_tbc);
			if (ret)
				return ret;
		}

		pages = kzalloc(sizeof(struct page *) * new_pages_count,
				GFP_KERNEL);
		if (!pages)
			return -ENOMEM;

		for (count = current_page_count; count < new_pages_count;
				count++) {
			pages[count] = alloc_page(GFP_KERNEL | __GFP_ZERO);
			if (!pages[count]) {
				kfree(pages);
				return -ENOMEM;
			}
		}
		for (count = 0; count < current_page_count; count++)
			pages[count] = snapshot->base_table[count];

		mapped_base_table = vmap(pages, new_pages_count, VM_MAP,
				PAGE_KERNEL);
		if (!mapped_base_table) {
			ret = -ENOMEM;
			goto error_exit;
		}

		vunmap(snapshot->base_t);
		snapshot->base_t = mapped_base_table;
		kfree(snapshot->base_table);
		snapshot->base_table = pages;
		snapshot->tables_pages_count = new_pages_count;
	}

	if (meta_tbc > snapshot->bitmap_size_in_bits) {
		old_bitmap_length = sizeof(unsigned long) *
			BITS_TO_LONGS(snapshot->bitmap_size_in_bits);
		bitmaps_length = sizeof(unsigned long) *
				BITS_TO_LONGS(meta_tbc);
		snapshot_bitmap = kzalloc(bitmaps_length, GFP_KERNEL);
		moved_iblock = kzalloc(bitmaps_length, GFP_KERNEL);
		next_snapshot_bitmap = kzalloc(bitmaps_length, GFP_KERNEL);

		if (!snapshot_bitmap || !moved_iblock ||
				!next_snapshot_bitmap) {
			ret = -ENOMEM;
			goto error_exit;
		}

		memcpy(snapshot_bitmap, snapshot->snapshot_bitmap,
				old_bitmap_length);
		memcpy(moved_iblock, snapshot->moved_iblock, old_bitmap_length);
		memcpy(next_snapshot_bitmap, snapshot->next_snapshot_bitmap,
				old_bitmap_length);

		kfree(snapshot->snapshot_bitmap);
		snapshot->snapshot_bitmap = snapshot_bitmap;
		kfree(snapshot->moved_iblock);
		snapshot->moved_iblock = moved_iblock;
		kfree(snapshot->next_snapshot_bitmap);
		snapshot->next_snapshot_bitmap = next_snapshot_bitmap;
		snapshot->bitmap_size_in_bits = meta_tbc;
	}

	insert_new_iblocks(sbi, ino_n, new_table_index);

	return ret;
error_exit:
	if (pages)
		for (count = current_page_count; count < new_pages_count;
								count++)
			__free_page(pages[count]);
	kfree(pages);
	kfree(snapshot_bitmap);
	kfree(moved_iblock);
	kfree(next_snapshot_bitmap);
	return ret;
}

static int expand_special_file(struct vdfs_sb_info *sbi, ino_t ino_n,
		__u64 new_table_index)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	__u32 meta_tbc = le32_to_cpu(exsb->meta_tbc);
	__u32 new_blocks_count;

	long free_blocks;
	sector_t last_table_index;
	int ret = 0, count;

	free_blocks = meta_tbc;
	new_blocks_count = new_table_index - VDFS_LAST_TABLE_INDEX(sbi, ino_n);

	if (is_tree(ino_n))
		new_blocks_count *= (1 << (sbi->log_blocks_in_page +
			sbi->log_blocks_in_leb));
	else
		new_blocks_count *= (1 << sbi->log_blocks_in_page);

	for (count = VDFS_FSFILE; count <= VDFS_LSFILE; count++) {
		last_table_index = VDFS_LAST_TABLE_INDEX(sbi, count);
			free_blocks -= (1 << (sbi->log_blocks_in_page +
				sbi->log_blocks_in_leb)) *
				(last_table_index + 1);
	}

	if ((free_blocks - (long)new_blocks_count) < ((long)meta_tbc >> 1)) {
		ret = expand_area(sbi, exsb->meta,
				VDFS_META_BTREE_EXTENTS,
				calculate_expand_step(sbi),
				&exsb->meta_tbc);
		if (ret)
			return ret;
		set_sbi_flag(sbi, EXSB_DIRTY);
	}

	ret = expand_translation_tables(sbi, ino_n, new_table_index);
	return ret;
}

int vdfs_get_meta_iblock(struct vdfs_sb_info *sbi, ino_t ino_n,
		sector_t page_index, sector_t *meta_iblock)
{
	__le64 *table;
	sector_t last_page_index = VDFS_LAST_TABLE_INDEX(sbi, ino_n);
	pgoff_t table_page_index = GET_TABLE_INDEX(sbi, ino_n, page_index);

	if (table_page_index > last_page_index)
		return -EINVAL;

	table = VDFS_GET_TABLE(sbi, ino_n);
	*meta_iblock = le64_to_cpu(table[table_page_index]);
	return 0;
}

loff_t vdfs_special_file_size(struct vdfs_sb_info *sbi, ino_t ino_n)
{
	sector_t last_page_index = VDFS_LAST_TABLE_INDEX(sbi, ino_n);
	loff_t size;

	if (is_tree(ino_n))
		size = (1llu << sbi->log_super_page_size) *
			(last_page_index + 1);
	else
		size = PAGE_CACHE_SIZE * (last_page_index + 1);


	return size;
}


