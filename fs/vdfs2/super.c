/**
 * @file	fs/emmcfs/super.c
 * @brief	The eMMCFS initialization and superblock operations.
 * @author	Dmitry Voytik, d.voytik@samsung.com
 * @author
 * @date	01/17/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * In this file mount and super block operations are implemented.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>
#include <linux/version.h>
#include <linux/genhd.h>
#include <linux/exportfs.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>
#include <linux/bootmem.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/blkdev.h>

#include "vdfs_layout.h"
#include "emmcfs.h"
#include "btree.h"
#include "packtree.h"
#include "debug.h"

#include "cattree.h"
#include "exttree.h"
#include "hlinktree.h"
#include "xattrtree.h"

#define VDFS_MOUNT_INFO(fmt, ...)\
do {\
	printk(KERN_INFO "[VDFS] " fmt, ##__VA_ARGS__);\
} while (0)

/* writeback thread */
static struct task_struct *writeback_thread;

/* Prototypes */
static inline void get_dev_sectors_count(struct super_block *sb,
							sector_t *s_count);

static void vdfs_free_debug_area(struct super_block *sb);

static void free_lru(struct vdfs_btree *btree)
{
	struct list_head *pos, *tp;
	int i;

	list_for_each_safe(pos, tp, &btree->active_use)
		emmcfs_put_cache_bnode(list_entry(pos, struct emmcfs_bnode,
				lru_node));

	list_for_each_safe(pos, tp, &btree->passive_use)
		emmcfs_put_cache_bnode(list_entry(pos, struct emmcfs_bnode,
				lru_node));

	btree->active_use_count = 0;
	btree->passive_use_count = 0;

	for (i = 0; i < VDFS_BNODE_HASH_SIZE; i++) {
		struct hlist_node *iterator;
		hlist_for_each(iterator, &btree->hash_table[i]) {
			VDFS_DEBUG_BUG_ON(1);
			EMMCFS_ERR("Lost bnode #%d!", hlist_entry(iterator,
				struct emmcfs_bnode, hash_node)->node_id);

		}

	}


}

/**
 * @brief			B-tree destructor.
 * @param [in,out]	btree	Pointer to btree that will be destroyed
 * @return		void
 */
void emmcfs_put_btree(struct vdfs_btree *btree)
{

	free_lru(btree);
	emmcfs_destroy_free_bnode_bitmap(btree->bitmap);
	EMMCFS_BUG_ON(!btree->head_bnode);
	/*emmcfs_put_bnode(btree->head_bnode);*/
	iput(btree->inode);
	kfree(btree->rw_tree_lock);
	kfree(btree->split_buff);
	kfree(btree);
}

/**
 * @brief			Inode bitmap destructor.
 * @param [in,out]	sbi	Pointer to sb info which free_inode_bitmap
 *				will be destroyed
 * @return		void
 */
static void destroy_free_inode_bitmap(struct vdfs_sb_info *sbi)
{
	iput(sbi->free_inode_bitmap.inode);
	sbi->free_inode_bitmap.inode = NULL;
}

/** Debug mask is a module parameter. This parameter enables particular debug
 *  type printing (see EMMCFS_DBG_* in fs/emmcfs/debug.h).
 */
unsigned int debug_mask = 0
		/*+ EMMCFS_DBG_INO*/
		/*+ EMMCFS_DBG_FSM*/
		/*+ EMMCFS_DBG_SNAPSHOT*/
		/*+ EMMCFS_DBG_TRANSACTION*/
		+ EMMCFS_DBG_TMP
		;

/** The eMMCFS inode cache
 */
static struct kmem_cache *vdfs_inode_cachep;

void vdfs_init_inode(struct emmcfs_inode_info *inode)
{
	inode->name = NULL;
	inode->fork.total_block_count = 0;
	inode->record_type = 0;

	inode->fork.prealloc_block_count = 0;
	inode->fork.prealloc_start_block = 0;

	inode->bnode_hint.bnode_id = 0;
	inode->bnode_hint.pos = -1;
	inode->flags = 0;
	inode->is_in_dirty_list = 0;

	inode->ptree.symlink.data = NULL;
}
/**
 * @brief			Method to allocate inode.
 * @param [in,out]	sb	Pointer to eMMCFS superblock
 * @return			Returns pointer to inode, NULL on failure
 */
static struct inode *vdfs_alloc_inode(struct super_block *sb)
{
	struct emmcfs_inode_info *inode;

	inode = kmem_cache_alloc(vdfs_inode_cachep, GFP_KERNEL);
	if (!inode)
		return NULL;

	vdfs_init_inode(inode);

	return &inode->vfs_inode;
}

/**
 * @brief			Method to destroy inode.
 * @param [in,out]	inode	Pointer to inode for destroy
 * @return		void
 */
static void emmcfs_destroy_inode(struct inode *inode)
{
	struct emmcfs_inode_info *inode_info = EMMCFS_I(inode);

	kfree(inode_info->name);

	if (inode_info->record_type == VDFS_CATALOG_PTREE_SYMLINK)
		kfree(inode_info->ptree.symlink.data);

	kmem_cache_free(vdfs_inode_cachep, inode_info);
}

/**
 * @brief		Sync starting superblock.
 * @param [in]	sb	Superblock information
 * @return		Returns error code
 */
int vdfs_sync_first_super(struct vdfs_sb_info *sbi)
{
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	__u32 checksum;
	int ret = 0;

	lock_page(sbi->superblocks);

	checksum = crc32(0, exsb, sizeof(*exsb) - sizeof(exsb->checksum));
	exsb->checksum = cpu_to_le32(checksum);
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	if (!is_fork_valid(&exsb->small_area))
		BUG();
#endif
	set_page_writeback(sbi->superblocks);
	ret = vdfs_write_page(sbi, sbi->superblocks, VDFS_EXSB_OFFSET,
		VDFS_EXSB_SIZE_SECTORS, 3 * SB_SIZE_IN_SECTOR * SECTOR_SIZE, 1);
#ifdef CONFIG_VDFS_STATISTIC
	sbi->umount_written_bytes += (VDFS_EXSB_SIZE_SECTORS
			<< SECTOR_SIZE_SHIFT);
#endif
	unlock_page(sbi->superblocks);

	return ret;
}

/**
 * @brief		Sync finalizing superblock.
 * @param [in]	sb	Superblock information
 * @return		Returns error code
 */
int vdfs_sync_second_super(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	__u32 checksum;
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);

	lock_page(sbi->superblocks);
	set_page_writeback(sbi->superblocks);
		/* Update cheksum */
	checksum = crc32(0, exsb, sizeof(*exsb) - sizeof(exsb->checksum));
	exsb->checksum = cpu_to_le32(checksum);
#if defined(CONFIG_VDFS_META_SANITY_CHECK)
	if (!is_fork_valid(&exsb->small_area))
		BUG();
#endif
	ret = vdfs_write_page(sbi, sbi->superblocks, VDFS_EXSB_COPY_OFFSET,
		VDFS_EXSB_SIZE_SECTORS, 3 * SB_SIZE_IN_SECTOR * SECTOR_SIZE, 1);
#ifdef CONFIG_VDFS_STATISTIC
	sbi->umount_written_bytes += (VDFS_EXSB_SIZE_SECTORS
			<< SECTOR_SIZE_SHIFT);
#endif
	unlock_page(sbi->superblocks);

	return ret;
}


/**
 * @brief		Method to write out all dirty data associated
 *				with the superblock.
 * @param [in,out]	sb	Pointer to the eMMCFS superblock
 * @param [in,out]	wait	Block on write completion
 * @return		Returns 0 on success, errno on failure
 */
int vdfs_sync_fs(struct super_block *sb, int wait)
{
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct emmcfs_inode_info *inode_info;
	int ret = 0;
	if (wait) {
again:
		spin_lock(&VDFS_SB(sb)->dirty_list_lock);
		while (!list_empty(&sbi->dirty_list_head)) {
			inode_info = list_entry(sbi->dirty_list_head.next,
					struct emmcfs_inode_info,
					dirty_list);
			inode_info->is_in_dirty_list = 0;
			list_del_init(&inode_info->dirty_list);

			/*
			 * Don't bother with inodes beeing freed,
			 * the writeout is handled by the freer.
			 */
			spin_lock(&inode_info->vfs_inode.i_lock);
			if (inode_info->vfs_inode.i_state &
				(I_FREEING | I_WILL_FREE)) {
				spin_unlock(&inode_info->vfs_inode.i_lock);
				continue;
			}

			atomic_inc(&inode_info->vfs_inode.i_count);
			spin_unlock(&inode_info->vfs_inode.i_lock);
			spin_unlock(&VDFS_SB(sb)->dirty_list_lock);
			ret = filemap_write_and_wait(
					inode_info->vfs_inode.i_mapping);
			iput(&inode_info->vfs_inode);
			if (ret)
				return ret;
			spin_lock(&VDFS_SB(sb)->dirty_list_lock);
		}
		spin_unlock(&VDFS_SB(sb)->dirty_list_lock);
		down_write(&sbi->snapshot_info->transaction_lock);
		if (!list_empty(&sbi->dirty_list_head)) {
			up_write(&sbi->snapshot_info->transaction_lock);
			goto again;
		}
		ret = vdfs_update_metadata(sbi);
		up_write(&sbi->snapshot_info->transaction_lock);
	}
	return ret;
}

/**
 * @brief			Method to free superblock (unmount).
 * @param [in,out]	sbi	Pointer to the eMMCFS superblock
 * @return		void
 */
static void destroy_super(struct vdfs_sb_info *sbi)
{
	percpu_counter_destroy(&sbi->free_blocks_count);
	sbi->raw_superblock_copy = NULL;
	sbi->raw_superblock = NULL;

	if (sbi->superblocks) {
		kunmap(sbi->superblocks);
		__free_pages(sbi->superblocks, 0);
	}

	if (sbi->superblocks_copy) {
		kunmap(sbi->superblocks_copy);
		__free_pages(sbi->superblocks_copy, 0);
	}
}

/**
 * @brief			Method to free sbi info.
 * @param [in,out]	sb	Pointer to a superblock
 * @return		void
 */
static void emmcfs_put_super(struct super_block *sb)
{
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	sbi->umount_time = 1;
	destroy_packtrees_list(sbi);
	emmcfs_put_btree(sbi->catalog_tree);
	sbi->catalog_tree = NULL;
	emmcfs_put_btree(sbi->extents_tree);
	sbi->extents_tree = NULL;
	emmcfs_put_btree(sbi->hardlink_tree);
	sbi->hardlink_tree = NULL;
	emmcfs_put_btree(sbi->xattr_tree);
	sbi->xattr_tree = NULL;

	if (sbi->free_inode_bitmap.inode)
		destroy_free_inode_bitmap(sbi);

	if (sbi->fsm_info)
		emmcfs_fsm_destroy_management(sb);

	if (VDFS_DEBUG_PAGES(sbi))
		vdfs_free_debug_area(sb);

	destroy_small_files_area_manager(sbi);
#ifdef CONFIG_VDFS_QUOTA
	destroy_quota_manager(sbi);
#endif
	destroy_high_priority(&sbi->high_priority);
	if (sbi->snapshot_info)
		emmcfs_destroy_snapshot_manager(sbi);
	destroy_super(sbi);

#ifdef CONFIG_VDFS_STATISTIC
	printk(KERN_INFO "Bytes written during umount : %lld\n",
			sbi->umount_written_bytes);
#endif
#if defined(CONFIG_VDFS_PROC)
	vdfs_destroy_proc_entry(sbi);
#endif
	kfree(sbi);
	EMMCFS_DEBUG_SB("finished");
}

/**
 * @brief			This function handle umount start.
 * @param [in,out]	sb	Pointer to a superblock
 * @return		void
 */
static void emmcfs_umount_begin(struct super_block *sb)
{
#ifdef CONFIG_VDFS_STATISTIC
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	/* reset the page counter */
	sbi->umount_written_bytes = 0;
#endif
}
/**
 * @brief			Force FS into a consistency state and
 *				lock it (for LVM).
 * @param [in,out]	sb	Pointer to the eMMCFS superblock
 * @remark			TODO: detailed description
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_freeze(struct super_block *sb)
{
	/* d.voytik-TODO-29-12-2011-17-24-00: [emmcfs_freeze]
	 * implement emmcfs_freeze() */
	int ret = 0;
	EMMCFS_DEBUG_SB("finished (ret = %d)", ret);
	return ret;
}

/**
 * @brief			Calculates metadata size, using extended
 *				superblock's forks
 * @param [in,out]	sbi	Pointer to the eMMCFS superblock
 * @return		metadata size in 4K-blocks
 */
static u64 calc_special_files_size(struct vdfs_sb_info *sbi)
{
	u64 res = 0;
	struct vdfs_extended_super_block *exsb  = VDFS_RAW_EXSB(sbi);

	res += le64_to_cpu(exsb->meta_tbc);
	res += le64_to_cpu(exsb->tables_tbc);

	return res;
}

/**
 * @brief			Get FS statistics.
 * @param [in,out]	dentry	Pointer to directory entry
 * @param [in,out]	buf	Point to kstatfs buffer where information
 *				will be placed
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block	*sb = dentry->d_sb;
	struct vdfs_sb_info	*sbi = sb->s_fs_info;
#ifdef CONFIG_VDFS_CHECK_FRAGMENTATION
	int count;
#endif

	buf->f_type = (long) VDFS_SB_SIGNATURE;
	buf->f_bsize = sbi->block_size;
	buf->f_blocks = sbi->total_leb_count << sbi->log_blocks_in_leb;
	buf->f_bavail = buf->f_bfree =
			percpu_counter_sum(&sbi->free_blocks_count);
	buf->f_files = sbi->files_count + sbi->folders_count;
	buf->f_fsid.val[0] = sbi->volume_uuid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (sbi->volume_uuid >> 32) & 0xFFFFFFFFUL;
	buf->f_namelen = VDFS_FILE_NAME_LEN;

#ifdef CONFIG_VDFS_CHECK_FRAGMENTATION
	for (count = 0; count < EMMCFS_EXTENTS_COUNT_IN_FORK; count++) {
		msleep(50);
		printk(KERN_INFO "in %d = %lu", count, sbi->in_fork[count]);
	}

	msleep(50);
	printk(KERN_INFO "in extents overflow = %lu", sbi->in_extents_overflow);
#endif

	{
		const char *device_name = sb->s_bdev->bd_part->__dev.kobj.name;
		u64 meta_size = calc_special_files_size(sbi) <<
			(sbi->block_size_shift - 10);
		u64 data_size = (buf->f_blocks - percpu_counter_sum(
				&sbi->free_blocks_count) -
				calc_special_files_size(sbi)) <<
			(sbi->block_size_shift - 10);

		printk(KERN_INFO "%s: Meta: %lluKB     Data: %lluKB\n",
				device_name, meta_size, data_size);
		printk(KERN_INFO "There are %llu tiny files at volume\n",
				atomic64_read(&sbi->tiny_files_counter));
	}

	EMMCFS_DEBUG_SB("finished");
	return 0;
}

/**
 * @brief			Evict inode.
 * @param [in,out]	inode	Pointer to inode that will be evicted
 * @return		void
 */
static void emmcfs_evict_inode(struct inode *inode)
{
	struct vdfs_sb_info *sbi = VDFS_SB(inode->i_sb);
	int error = 0;
	enum orphan_inode_type type;
	sector_t freed_runtime_iblocks;

	EMMCFS_DEBUG_INO("evict inode %lu nlink\t%u",
			inode->i_ino, inode->i_nlink);

	inode->i_size = 0;
	truncate_inode_pages(&inode->i_data, 0);
	invalidate_inode_buffers(inode);

	/* todo unpacked inode - do nothing */
	if (EMMCFS_I(inode)->record_type == VDFS_CATALOG_UNPACK_INODE) {
		inode->i_state = I_FREEING | I_CLEAR; /* todo */
		return;
	}
	spin_lock(&VDFS_SB(inode->i_sb)->dirty_list_lock);
	if (EMMCFS_I(inode)->is_in_dirty_list) {
		list_del(&EMMCFS_I(inode)->dirty_list);
		EMMCFS_I(inode)->is_in_dirty_list = 0;
	}
	spin_unlock(&VDFS_SB(inode->i_sb)->dirty_list_lock);


	/* Internal packtree inodes - do nothing */
	if (EMMCFS_I(inode)->record_type >= VDFS_CATALOG_PTREE_RECORD)
		goto no_delete;

	if (!is_vdfs_inode_flag_set(inode, TINY_FILE) &&
			!is_vdfs_inode_flag_set(inode, SMALL_FILE) &&
			(S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode))) {
		mutex_lock(&EMMCFS_I(inode)->truncate_mutex);
		freed_runtime_iblocks = vdfs_truncate_runtime_blocks(0,
			&EMMCFS_I(inode)->runtime_extents);
		mutex_unlock(&EMMCFS_I(inode)->truncate_mutex);
		vdfs_free_reserved_space(inode, freed_runtime_iblocks);
	}

	if (inode->i_nlink)
		goto no_delete;


	error = vdfs_xattrtree_remove_all(sbi->xattr_tree, inode->i_ino);
	if (error) {
		EMMCFS_ERR("can not clear xattrs for ino#%lu", inode->i_ino);
		goto no_delete;
	}

	if (is_vdfs_inode_flag_set(inode, SMALL_FILE))
		type = ORPHAN_SMALL_FILE;
	else if (!is_vdfs_inode_flag_set(inode, TINY_FILE) &&
			(S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)))
		type = ORPHAN_REGULAR_FILE;
	else
		type = ORPHAN_TINY_OR_SPECIAL_FILE;
	BUG_ON(vdfs_kill_orphan_inode(EMMCFS_I(inode), type,
			&EMMCFS_I(inode)->fork));

no_delete:
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35) ||\
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	clear_inode(inode);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33)
	end_writeback(inode);
#endif
}

/*
 * Structure of the eMMCFS super block operations
 */
static struct super_operations emmcfs_sops = {
	.alloc_inode	= vdfs_alloc_inode,
	.destroy_inode	= emmcfs_destroy_inode,
	.put_super	= emmcfs_put_super,
	.sync_fs	= vdfs_sync_fs,
	.freeze_fs	= emmcfs_freeze,
	.statfs		= emmcfs_statfs,
	.umount_begin	= emmcfs_umount_begin,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	.delete_inode	= emmcfs_evict_inode,
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) || \
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	.evict_inode	= emmcfs_evict_inode,
#else
	BUILD_EMMCFS_BUG();
#endif
};

/**
 * @brief		Determines volume size in sectors.
 * @param [in]	sb	VFS super block
 * @param [out]	s_count	Stores here sectors count of volume
 * @return	void
 */
static inline void get_dev_sectors_count(struct super_block *sb,
							sector_t *s_count)
{
	*s_count = sb->s_bdev->bd_inode->i_size >> SECTOR_SIZE_SHIFT;
}

/**
 * @brief		Determines volume size in blocks.
 * @param [in]	sb	VFS super block
 * @param [out] b_count	Stores here blocks count of volume
 * @return	void
 */
static inline void get_dev_blocks_count(struct super_block *sb,
							sector_t *b_count)
{
	*b_count = sb->s_bdev->bd_inode->i_size >> sb->s_blocksize_bits;
}

/**
 * @brief			Sanity check of eMMCFS super block.
 * @param [in]	esb		The eMMCFS super block
 * @param [in]	str_sb_type	Determines which SB is verified on error
 *				printing
 * @param [in]	silent		Doesn't print errors if silent is true
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_verify_sb(struct emmcfs_super_block *esb, char *str_sb_type,
				int silent)
{
	__le32 checksum;
	/* check magic number */
	if (memcmp(esb->signature, VDFS_SB_SIGNATURE,
				strlen(VDFS_SB_SIGNATURE))) {
		if (!silent || debug_mask & EMMCFS_DBG_SB)
			EMMCFS_ERR("%s: bad signature - %.8s, "
				"expected - %.8s\n ", str_sb_type,
				esb->signature, VDFS_SB_SIGNATURE);
		return -EINVAL;
	}

	if (memcmp(esb->layout_version, VDFS_LAYOUT_VERSION,
		strlen(VDFS_LAYOUT_VERSION))) {
		EMMCFS_ERR("Invalid mkfs layout version: %.4s,\n"
			"driver uses %.4s version\n", esb->layout_version,
			VDFS_LAYOUT_VERSION);
		return -EINVAL;
	}

	/* check version */
	if (esb->version.major != EMMCFS_SB_VER_MAJOR ||\
			esb->version.minor != EMMCFS_SB_VER_MINOR) {
		if (!silent || debug_mask & EMMCFS_DBG_SB)
			EMMCFS_ERR("%s: bad version (major = %d, minor = %d)\n",
					str_sb_type, (int)esb->version.major,
					(int)esb->version.minor);
		return -EINVAL;
	}
	/* check crc32 */
	checksum = crc32(0, esb, sizeof(*esb) - sizeof(esb->checksum));
	if (esb->checksum != checksum) {
		EMMCFS_ERR("%s: bad checksum - 0x%x, must be 0x%x\n",
				str_sb_type, esb->checksum, checksum);
		return -EINVAL;
	}
	return 0;
}

/**
 * @brief		Fill run-time superblock from on-disk superblock.
 * @param [in]	esb	The eMMCFS super block
 * @param [in]	sb	VFS superblock
 * @return		Returns 0 on success, errno on failure
 */
static int fill_runtime_superblock(struct emmcfs_super_block *esb,
		struct super_block *sb)
{
	int ret = 0;
	unsigned long block_size;
	unsigned int bytes_in_leb;
	unsigned long long total_block_count;
	sector_t dev_block_count;
	struct vdfs_sb_info *sbi = sb->s_fs_info;

	/* check total block count in SB */
	if (*((long *) esb->mkfs_git_branch) &&
			*((long *) esb->mkfs_git_hash)) {
		VDFS_MOUNT_INFO("mkfs git branch is \"%s\"\n",
				esb->mkfs_git_branch);
		VDFS_MOUNT_INFO("mkfs git revhash \"%.40s\"\n",
				esb->mkfs_git_hash);
	}

	/* check if block size is supported and set it */
	block_size = 1 << esb->log_block_size;
	if (block_size & ~(512 | 1024 | 2048 | 4096)) {
		EMMCFS_ERR("unsupported block size (%ld)\n", block_size);
		ret = -EINVAL;
		goto err_exit;
	}

	if (block_size == 512) {
		sbi->log_blocks_in_page = 3;
		sbi->log_block_size = 9;
	} else if (block_size == 1024) {
		sbi->log_blocks_in_page = 2;
		sbi->log_block_size = 10;
	} else if (block_size == 2048) {
		sbi->log_blocks_in_page = 1;
		sbi->log_block_size = 11;
	} else {
		sbi->log_blocks_in_page = 0;
		sbi->log_block_size = 12;
	}

	sbi->block_size = block_size;
	if (!sb_set_blocksize(sb, sbi->block_size))
		EMMCFS_ERR("can't set block size\n");
	sbi->block_size_shift = esb->log_block_size;
	sbi->log_sectors_per_block = sbi->block_size_shift - SECTOR_SIZE_SHIFT;
	sbi->sectors_per_volume = esb->sectors_per_volume;

	sbi->offset_msk_inblock = 0xFFFFFFFF >> (32 - sbi->block_size_shift);

	/* check if LEB size is supported and set it */
	bytes_in_leb = 1 << esb->log_leb_size;
	if (bytes_in_leb == 0 || bytes_in_leb < block_size) {
		EMMCFS_ERR("unsupported LEB size (%u)\n", bytes_in_leb);
		ret = -EINVAL;
		goto err_exit;
	}
	sbi->log_blocks_in_leb = esb->log_leb_size - esb->log_block_size;

	sbi->btree_node_size_blks = 1 << sbi->log_blocks_in_leb;


	sbi->total_leb_count = le64_to_cpu(esb->total_leb_count);
	total_block_count = sbi->total_leb_count << sbi->log_blocks_in_leb;

	get_dev_blocks_count(sb, &dev_block_count);

	if (((total_block_count > dev_block_count) &&
			(!test_option(sbi, STRIPPED))) ||
			(total_block_count == 0)) {
		EMMCFS_ERR("bad FS block count: %llu, device has %llu blocks\n",
				total_block_count,
				(long long unsigned int)dev_block_count);
		ret = -EINVAL;
		goto err_exit;
	}

	sbi->lebs_bm_log_blocks_block =
		le32_to_cpu(esb->lebs_bm_log_blocks_block);
	sbi->lebs_bm_bits_in_last_block =
		le32_to_cpu(esb->lebs_bm_bits_in_last_block);
	sbi->lebs_bm_blocks_count =
		le32_to_cpu(esb->lebs_bm_blocks_count);

	sbi->log_super_page_size = le32_to_cpu(esb->log_super_page_size);
	/* squash 128 bit UUID into 64 bit by xoring */
	sbi->volume_uuid = le64_to_cpup((void *)esb->volume_uuid) ^
			le64_to_cpup((void *)esb->volume_uuid + sizeof(u64));

	if (esb->case_insensitive)
		set_option(sbi, CASE_INSENSITIVE);

err_exit:
	return ret;
}

/**
 * @brief		Add record to oops area
 * @param [in] sbi	Pointer to super block info data structure
 * @param [in] line	Function line number
 * @param [in] name	Function name
 * @param [in] err	Error code
 * @return		Returns 0 on success, errno on failure.
 */
int vdfs_log_error(struct vdfs_sb_info *sbi, unsigned int line,
		const char *name, int err)
{
	struct vdfs_debug_descriptor *debug_descriptor;
	struct vdfs_debug_record *new_record = NULL;
	struct page **debug_pages;
	void *debug_area = NULL;
	unsigned int checksum;
	int ret = 0;
	int offset = 0;
	int count = 0;
	struct vdfs_extended_super_block *exsb;
	unsigned int debug_page_count = VDFS_DEBUG_AREA_PAGE_COUNT(sbi);
	int len = strlen(name);

	if (VDFS_IS_READONLY(sbi->sb))
		return 0;

	debug_pages = VDFS_DEBUG_PAGES(sbi);
	for (count = 0; count < debug_page_count; count++) {
		if (!debug_pages[count])
			return -EINVAL;
	}
	exsb = VDFS_RAW_EXSB(sbi);
	debug_descriptor = (struct vdfs_debug_descriptor *)
			VDFS_DEBUG_AREA(sbi);
	if (!debug_descriptor)
		return -EINVAL;
	debug_area = VDFS_DEBUG_AREA(sbi);
	if (!debug_area)
		return -EINVAL;

	offset = le32_to_cpu(debug_descriptor->offset_to_next_record);
	debug_descriptor->record_count = cpu_to_le32(le32_to_cpu(
		debug_descriptor->record_count) + 1);

	new_record = (struct vdfs_debug_record *)((void *)debug_area +
			le32_to_cpu(debug_descriptor->offset_to_next_record));



	BUG_ON(((void *)new_record - (void *)debug_area
		+ sizeof(struct vdfs_debug_record) + sizeof(checksum))
		> debug_page_count * PAGE_CACHE_SIZE);

	memset(new_record, 0, sizeof(*new_record));
	new_record->error_code = cpu_to_le32(err);
	new_record->fail_number = cpu_to_le32(debug_descriptor->record_count);
	new_record->uuid = cpu_to_le32(sbi->volume_uuid);
	new_record->fail_time = cpu_to_le32(jiffies);
	new_record->mount_count = cpu_to_le32(exsb->mount_counter);

	snprintf(new_record->line, DEBUG_FUNCTION_LINE_LENGTH, "%d", line);
	memcpy(new_record->function, name, len < DEBUG_FUNCTION_NAME_LENGTH ?
			len : DEBUG_FUNCTION_NAME_LENGTH);

	/* calculate offset to next error record */
	offset += sizeof(struct vdfs_debug_record);
	/* check if we have space for next record*/
	if ((offset + sizeof(struct vdfs_debug_record) + sizeof(checksum))
			>=  debug_page_count * PAGE_CACHE_SIZE)
		offset = sizeof(struct vdfs_debug_descriptor);

	debug_descriptor->offset_to_next_record = cpu_to_le32(offset);
	checksum = crc32(0, debug_area, DEBUG_AREA_CRC_OFFSET(sbi));
	*((__le32 *)((char *)debug_area + DEBUG_AREA_CRC_OFFSET(sbi)))
			= cpu_to_le32(checksum);

	for (count = 0; count < debug_page_count; count++) {
		lock_page(debug_pages[count]);
		set_page_writeback(debug_pages[count]);
		ret = vdfs_write_page(sbi, debug_pages[count],
			((VDFS_DEBUG_AREA_START(sbi)) <<
			(PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT)) +
			 + (count << (PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT)),
			 sbi->block_size / SECTOR_SIZE, 0, 1);
		unlock_page(debug_pages[count]);
		if (ret)
			break;
	}

	return ret;
}

/**
 * @brief			VDFS volume sainty check : it is vdfs or not
 * @param [in]		sb	VFS superblock info stucture
 * @return			Returns 0 on success, errno on failure
 *
 */
static int emmcfs_volume_check(struct super_block *sb)
{
	int ret = 0;
	struct page *superblocks;
	void *raw_superblocks;
	struct emmcfs_super_block *esb;
	struct vdfs_sb_info *sbi = sb->s_fs_info;

	superblocks = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!superblocks)
		return -ENOMEM;

	lock_page(superblocks);
	/* size of the VDFS superblock is fixed = 4K or 8 sectors */
	ret = vdfs_read_page(sb->s_bdev, superblocks, VDFS_SB_ADDR,
		PAGE_TO_SECTORS(1), 0);
	unlock_page(superblocks);

	if (ret)
		goto error_exit;

	raw_superblocks = kmap(superblocks);
	sbi->raw_superblock = raw_superblocks;

	/* check superblocks */
	esb = (struct emmcfs_super_block *)raw_superblocks;
	ret = emmcfs_verify_sb(esb, "SB", 0);
	if (ret)
		goto error_exit;

	raw_superblocks++;
	ret = emmcfs_verify_sb(esb, "SB", 0);
	if (ret)
		goto error_exit;
	sbi->superblocks = superblocks;
	return 0;
error_exit:
	sbi->superblocks = NULL;
	sbi->raw_superblock = NULL;
	kunmap(superblocks);
	__free_page(superblocks);
	return ret;
}


/**
 * @brief				Load debug area from disk to memory.
 * @param [in]	sb			Pointer to superblock
 * @return				Returns 0 on success, errno on failure
 */
static int vdfs_load_debug_area(struct super_block *sb)
{
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct page **debug_pages;
	struct vdfs_debug_descriptor *debug_descriptor = NULL;
	void *debug_area = NULL;
	int ret = 0, is_oops_area_present = 0;
	unsigned int checksum;
	int count = 0;
	unsigned int debug_page_count = VDFS_DEBUG_AREA_PAGE_COUNT(sbi);

	debug_pages = kzalloc(sizeof(struct page *) * debug_page_count,
			GFP_KERNEL);

	if (!debug_pages)
		return -ENOMEM;

	for (count = 0; count < debug_page_count; count++) {
		debug_pages[count] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!debug_pages[count]) {
			count--;
			for (; count >= 0; count--) {
				unlock_page(debug_pages[count]);
				__free_page(debug_pages[count]);
			}
			kfree(debug_pages);
			return -ENOMEM;
		}
		lock_page(debug_pages[count]);
	}

	ret = vdfs_read_pages(sb->s_bdev, debug_pages,
		VDFS_DEBUG_AREA_START(sbi) << (PAGE_CACHE_SHIFT -
		SECTOR_SIZE_SHIFT), debug_page_count);

	for (count = 0; count < debug_page_count; count++)
		unlock_page(debug_pages[count]);

	if (ret)
		goto exit_free_page;

	debug_area = vmap(debug_pages, debug_page_count, VM_MAP, PAGE_KERNEL);

	if (!debug_area) {
		ret = -ENOMEM;
		goto exit_free_page;
	}

	debug_descriptor = (struct vdfs_debug_descriptor *)debug_area;
	is_oops_area_present = !(strncmp(debug_descriptor->signature,
			EMMCFS_OOPS_MAGIC, sizeof(EMMCFS_OOPS_MAGIC) - 1));

	if (!is_oops_area_present) {
		memset((void *)debug_area, 0,
				VDFS_DEBUG_AREA_LENGTH_BYTES(sbi));
		debug_descriptor->offset_to_next_record = cpu_to_le32(
				sizeof(struct vdfs_debug_descriptor));
		/* copy magic number */
		memcpy((void *)&debug_descriptor->signature, EMMCFS_OOPS_MAGIC,
				sizeof(EMMCFS_OOPS_MAGIC) - 1);

		checksum = crc32(0, debug_area, DEBUG_AREA_CRC_OFFSET(sbi));
		*((__le32 *)((char *)debug_area + DEBUG_AREA_CRC_OFFSET(sbi)))
				= cpu_to_le32(checksum);

		for (count = 0; count < debug_page_count; count++) {
			lock_page(debug_pages[count]);
			set_page_writeback(debug_pages[count]);
		}

		for (count = 0; count < debug_page_count; count++) {
			sector_t sector_to_write;

			sector_to_write = VDFS_DEBUG_AREA_START(sbi) <<
					(PAGE_CACHE_SHIFT - SECTOR_SIZE_SHIFT);

			sector_to_write += count << (PAGE_CACHE_SHIFT -
					SECTOR_SIZE_SHIFT);

			ret = vdfs_write_page(sbi, debug_pages[count],
					sector_to_write, 8, 0, 1);
			if (ret)
				goto exit_unmap_page;
		}

		for (count = 0; count < debug_page_count; count++)
			unlock_page(debug_pages[count]);
	}

	sbi->debug_pages = debug_pages;
	sbi->debug_area = debug_area;
	return 0;

exit_unmap_page:
	vunmap(debug_area);
exit_free_page:
	for (count = 0; count < debug_page_count; count++) {
		unlock_page(debug_pages[count]);
		__free_page(debug_pages[count]);
	}
	kfree(debug_pages);

	return ret;
}

/**
 * @brief				Free debug memory
 * @param [in]	sb			Pointer to superblock
 * @return				none
 */
static void vdfs_free_debug_area(struct super_block *sb)
{
	int count = 0;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	unsigned int debug_pages_count = VDFS_DEBUG_AREA_PAGE_COUNT(sbi);

	vunmap(sbi->debug_area);
	sbi->debug_area = NULL;
	for (count = 0; count < debug_pages_count; count++)
		__free_page(sbi->debug_pages[count]);
	kfree(sbi->debug_pages);
	sbi->debug_pages = NULL;
}

/**
 * @brief		Reads and checks and recovers eMMCFS super blocks.
 * @param [in]	sb	The VFS super block
 * @param [in]	silent	Do not print errors if silent is true
 * @return		Returns 0 on success, errno on failure
 */
static int vdfs_sb_read(struct super_block *sb, int silent)
{
	void *raw_superblocks;
	void *raw_superblocks_copy;
	struct emmcfs_super_block *esb;
	struct emmcfs_super_block *esb_copy;
	int ret = 0;
	int check_sb;
	int check_sb_copy;
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct page *superblocks, *superblocks_copy;

	superblocks = sbi->superblocks;
	raw_superblocks = sbi->raw_superblock;
	if (!raw_superblocks) {
		ret = -EINVAL;
		goto error_exit;
	}

	/* alloc page for superblock copy*/
	superblocks_copy = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (IS_ERR(superblocks_copy)) {
		EMMCFS_ERR("Fail to alloc page for reading superblocks copy");
		ret = -ENOMEM;
		goto error_exit;
	}
	/* read super block and extended super block from volume */
	/*  0    1    2    3      8    9   10   11     15   */
	/*  | SB | SB | SB |ESB   | SB | SB | SB |ESB   |   */
	/*  |    superblocks      |   superblocks_copy  |   */

	lock_page(superblocks_copy);
	ret = vdfs_read_page(sb->s_bdev, superblocks_copy, VDFS_SB_COPY_ADDR,
			PAGE_TO_SECTORS(1), 0);
	unlock_page(superblocks_copy);

	if (ret)
		goto err_superblock_copy;

	raw_superblocks_copy = kmap(superblocks_copy);
	if (!raw_superblocks_copy) {
		ret = -ENOMEM;
		goto err_superblock_copy;
	}

	/* check superblocks */
	esb = (struct emmcfs_super_block *)(raw_superblocks + VDFS_ESB_OFFSET);
	esb_copy =  (struct emmcfs_super_block *)(raw_superblocks_copy +
			VDFS_ESB_OFFSET);

	check_sb = emmcfs_verify_sb(esb, "SB", silent);
	check_sb_copy = emmcfs_verify_sb(esb_copy, "SB_COPY", silent);

	if (check_sb && check_sb_copy) {
		/*both superblocks are corrupted*/
		ret = check_sb;
		if (!silent || debug_mask & EMMCFS_DBG_SB)
			EMMCFS_ERR("can't find an VDFS filesystem on dev %s",
					sb->s_id);
	} else if ((!check_sb) && check_sb_copy) {
		/*first superblock is ok, copy is corrupted, recovery*/
		memcpy(esb_copy, esb, sizeof(*esb_copy));
		/* write superblock COPY to disk */
		lock_page(superblocks_copy);
		set_page_writeback(superblocks_copy);
		ret = vdfs_write_page(sbi, superblocks_copy,
				VDFS_SB_COPY_OFFSET, 2, SB_SIZE * 2, 1);
		unlock_page(superblocks_copy);
	} else if (check_sb && (!check_sb_copy)) {
		/*first superblock is corrupted, recovery*/
		memcpy(esb, esb_copy, sizeof(*esb));
		/* write superblock to disk */
		lock_page(superblocks);
		set_page_writeback(superblocks);
		ret = vdfs_write_page(sbi, superblocks,
				VDFS_SB_OFFSET, 2, SB_SIZE * 2, 1);
		unlock_page(superblocks);
	}

	if (ret)
		goto err_superblock_copy_unmap;
	ret = fill_runtime_superblock(esb, sb);

	if (ret)
		goto err_superblock_copy_unmap;

	sbi->raw_superblock_copy = raw_superblocks_copy;
	sbi->superblocks_copy = superblocks_copy;

	sbi->erase_block_size = le32_to_cpu(esb->erase_block_size);
	sbi->log_erase_block_size = esb->log_erase_block_size;
	sbi->erase_block_size_in_blocks = sbi->erase_block_size >>
			sbi->block_size_shift;
	sbi->log_erase_block_size_in_blocks = sbi->log_erase_block_size -
			sbi->block_size_shift;
	if (esb->read_only) {
		VDFS_SET_READONLY(sb);
		set_option(VDFS_SB(sb), STRIPPED);
	}
	return 0;
err_superblock_copy_unmap:
	kunmap(superblocks_copy);
err_superblock_copy:
	__free_page(superblocks_copy);
error_exit:
	sbi->raw_superblock = NULL;
	kunmap(sbi->superblocks);
	__free_page(sbi->superblocks);
	sbi->superblocks = NULL;
	return ret;
}

/**
 * @brief		Verifies eMMCFS extended super block checksum.
 * @param [in]	exsb	The eMMCFS super block
 * @return		Returns 0 on success, errno on failure
 */
static int emmcfs_verify_exsb(struct vdfs_extended_super_block *exsb)
{
	__le32 checksum;

	if (!VDFS_EXSB_VERSION(exsb)) {
		EMMCFS_ERR("Bad version of extended super block");
		return -EINVAL;
	}

	/* check crc32 */
	checksum = crc32(0, exsb, sizeof(*exsb) - sizeof(exsb->checksum));

	if (exsb->checksum != checksum) {
		EMMCFS_ERR("bad checksum of extended super block - 0x%x, "\
				"must be 0x%x\n", exsb->checksum, checksum);
		return -EINVAL;
	} else
		return 0;
}

/**
 * @brief		Reads and checks eMMCFS extended super block.
 * @param [in]	sb	The VFS super block
 * @return		Returns 0 on success, errno on failure
 */
static int vdfs_extended_sb_read(struct super_block *sb)
{
	struct vdfs_sb_info *sbi = sb->s_fs_info;
	struct vdfs_extended_super_block *exsb = VDFS_RAW_EXSB(sbi);
	struct vdfs_extended_super_block *exsb_copy =
			VDFS_RAW_EXSB_COPY(sbi);
	int ret = 0;
	int check_exsb;
	int check_exsb_copy;

	check_exsb = emmcfs_verify_exsb(exsb);
	check_exsb_copy = emmcfs_verify_exsb(exsb_copy);

	if (check_exsb && check_exsb_copy) {
		EMMCFS_ERR("Extended superblocks are corrupted");
		ret = check_exsb;
		return ret;
	} else if ((!check_exsb) && check_exsb_copy) {
		/* extended superblock copy are corrupted, recovery */
		lock_page(sbi->superblocks);
		memcpy(exsb_copy, exsb, sizeof(*exsb_copy));
		set_page_writeback(sbi->superblocks);
		ret = vdfs_write_page(sbi, sbi->superblocks,
			VDFS_EXSB_COPY_OFFSET, VDFS_EXSB_SIZE_SECTORS,
			(SB_SIZE_IN_SECTOR * 3) * SECTOR_SIZE, 1);
		unlock_page(sbi->superblocks);
	} else if (check_exsb && (!check_exsb_copy)) {
		/* main extended superblock are corrupted, recovery */
		lock_page(sbi->superblocks_copy);
		memcpy(exsb, exsb_copy, sizeof(*exsb));
		set_page_writeback(sbi->superblocks_copy);
		ret = vdfs_write_page(sbi, sbi->superblocks_copy,
				VDFS_EXSB_OFFSET, VDFS_EXSB_SIZE_SECTORS,
				(SB_SIZE_IN_SECTOR * 3) * SECTOR_SIZE, 1);
		unlock_page(sbi->superblocks_copy);
	}

	atomic64_set(&sbi->tiny_files_counter,
			le64_to_cpu(exsb->tiny_files_counter));

	return ret;
}

/**
 * @brief		The eMMCFS B-tree common constructor.
 * @param [in]	sbi	The eMMCFS superblock info
 * @param [in]	btree	The eMMCFS B-tree
 * @param [in]	inode	The inode
 * @return		Returns 0 on success, errno on failure
 */
int fill_btree(struct vdfs_sb_info *sbi,
		struct vdfs_btree *btree, struct inode *inode)
{
	struct emmcfs_bnode *head_bnode, *bnode;
	struct vdfs_raw_btree_head *raw_btree_head;
	__u64 bitmap_size;
	int err = 0;
	enum emmcfs_get_bnode_mode mode;
	int loop;

	btree->sbi = sbi;
	btree->inode = inode;
	btree->pages_per_node = 1 << (sbi->log_blocks_in_leb +
			sbi->block_size_shift - PAGE_SHIFT);
	btree->log_pages_per_node = sbi->log_blocks_in_leb +
			sbi->block_size_shift - PAGE_SHIFT;
	btree->node_size_bytes = btree->pages_per_node << PAGE_SHIFT;

	btree->rw_tree_lock = kzalloc(sizeof(rw_mutex_t), GFP_KERNEL);
	if (!btree->rw_tree_lock)
		return -ENOMEM;

	init_mutex(btree->rw_tree_lock);
	if (VDFS_IS_READONLY(sbi->sb) ||
			(btree->btree_type == VDFS_BTREE_PACK))
		mode = EMMCFS_BNODE_MODE_RO;
	else
		mode = EMMCFS_BNODE_MODE_RW;

	mutex_init(&btree->hash_lock);

	for (loop = 0; loop < VDFS_BNODE_HASH_SIZE; loop++)
		INIT_HLIST_HEAD(&btree->hash_table[loop]);

	btree->active_use_count = 0;
	INIT_LIST_HEAD(&btree->active_use);

	btree->passive_use_count = 0;
	INIT_LIST_HEAD(&btree->passive_use);

	head_bnode =  emmcfs_get_bnode(btree, 0, mode);
	if (IS_ERR(head_bnode)) {
		err = PTR_ERR(head_bnode);
		goto free_mem;
	}

	raw_btree_head = head_bnode->data;

	/* Check the magic */
	if (memcmp(raw_btree_head->magic, EMMCFS_BTREE_HEAD_NODE_MAGIC,
				sizeof(EMMCFS_BTREE_HEAD_NODE_MAGIC) - 1)) {
		err = -EINVAL;
		goto err_put_bnode;
	}

	btree->head_bnode = head_bnode;

	/* Fill free bnode bitmpap */
	bitmap_size = btree->node_size_bytes -
		((void *) &raw_btree_head->bitmap - head_bnode->data) -
		EMMCFS_BNODE_FIRST_OFFSET;
	btree->bitmap = build_free_bnode_bitmap(&raw_btree_head->bitmap, 0,
			bitmap_size, head_bnode);
	if (IS_ERR(btree->bitmap)) {
		err = PTR_ERR(btree->bitmap);
		btree->bitmap = NULL;
		goto err_put_bnode;
	}

	/* Check if root bnode non-empty */
	bnode = emmcfs_get_bnode(btree, emmcfs_btree_get_root_id(btree),
			EMMCFS_BNODE_MODE_RO);

	if (IS_ERR(bnode)) {
		err = PTR_ERR(bnode);
		goto err_put_bnode;
	}

	if (EMMCFS_BNODE_DSCR(bnode)->recs_count == 0) {
		emmcfs_put_bnode(bnode);
		err = -EINVAL;
		goto err_put_bnode;
	}
	emmcfs_put_bnode(bnode);

	mutex_init(&btree->split_buff_lock);
	btree->split_buff = kzalloc(btree->node_size_bytes, GFP_KERNEL);
	if (!btree->split_buff)
		goto err_put_bnode;

	return 0;

err_put_bnode:
	emmcfs_put_bnode(head_bnode);
	kfree(btree->bitmap);
free_mem:
	kfree(btree->rw_tree_lock);
	return err;
}

/**
 * @brief			Catalog tree constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_fill_cat_tree(struct vdfs_sb_info *sbi)
{
	struct inode *inode = NULL;
	struct vdfs_btree *cat_tree;

	int err = 0;

	BUILD_BUG_ON(sizeof(struct vdfs_btree_gen_record) !=
			sizeof(struct vdfs_cattree_record));

	cat_tree = kzalloc(sizeof(*cat_tree), GFP_KERNEL);
	if (!cat_tree)
		return -ENOMEM;

	cat_tree->btree_type = EMMCFS_BTREE_CATALOG;
	cat_tree->max_record_len = sizeof(struct emmcfs_cattree_key) +
			sizeof(struct vdfs_catalog_file_record) + 1;
	inode = vdfs_special_iget(sbi->sb, VDFS_CAT_TREE_INO);
	if (IS_ERR(inode)) {
		int ret = PTR_ERR(inode);
		kfree(cat_tree);
		return ret;
	}

	err = fill_btree(sbi, cat_tree, inode);
	if (err)
		goto err_put_inode;

	cat_tree->comp_fn = test_option(sbi, CASE_INSENSITIVE) ?
			emmcfs_cattree_cmpfn_ci : emmcfs_cattree_cmpfn;
	sbi->catalog_tree = cat_tree;

	/*sbi->max_cattree_height = count_max_cattree_height(sbi);*/
	return 0;

err_put_inode:
	iput(inode);
	EMMCFS_ERR("can not read catalog tree");
	kfree(cat_tree);
	return err;
}

/**
 * @brief			Extents tree constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_fill_ext_tree(struct vdfs_sb_info *sbi)
{
	struct inode *inode = NULL;
	struct vdfs_btree *ext_tree;
	int err = 0;

	BUILD_BUG_ON(sizeof(struct vdfs_btree_gen_record) !=
			sizeof(struct vdfs_exttree_record));

	ext_tree = kzalloc(sizeof(*ext_tree), GFP_KERNEL);
	if (!ext_tree)
		return -ENOMEM;

	ext_tree->btree_type = EMMCFS_BTREE_EXTENTS;
	ext_tree->max_record_len = sizeof(struct emmcfs_exttree_key) +
			sizeof(struct emmcfs_exttree_record);
	inode = vdfs_special_iget(sbi->sb, VDFS_EXTENTS_TREE_INO);
	if (IS_ERR(inode)) {
		int ret;
		kfree(ext_tree);
		ret = PTR_ERR(inode);
		return ret;
	}

	err = fill_btree(sbi, ext_tree, inode);
	if (err)
		goto err_put_inode;

	ext_tree->comp_fn = emmcfs_exttree_cmpfn;
	sbi->extents_tree = ext_tree;
	return 0;

err_put_inode:
	iput(inode);
	EMMCFS_ERR("can not read extents overflow tree");
	kfree(ext_tree);
	return err;
}


/**
 * @brief			xattr tree constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
static int vdsf_fill_xattr_tree(struct vdfs_sb_info *sbi)

{
	struct inode *inode = NULL;
	struct vdfs_btree *xattr_tree;
	int err = 0;

	BUILD_BUG_ON(sizeof(struct vdfs_btree_gen_record) !=
			sizeof(struct vdfs_xattrtree_record));

	xattr_tree = kzalloc(sizeof(*xattr_tree), GFP_KERNEL);
	if (!xattr_tree)
		return -ENOMEM;

	xattr_tree->btree_type = VDFS_BTREE_XATTRS;
	xattr_tree->max_record_len = sizeof(struct vdfs_xattrtree_key) +
			VDFS_XATTR_VAL_MAX_LEN;
	inode = vdfs_special_iget(sbi->sb, VDFS_XATTR_TREE_INO);
	if (IS_ERR(inode)) {
		int ret;
		kfree(xattr_tree);
		ret = PTR_ERR(inode);
		return ret;
	}

	err = fill_btree(sbi, xattr_tree, inode);
	if (err)
		goto err_put_inode;

	xattr_tree->comp_fn = vdfs_xattrtree_cmpfn;
	sbi->xattr_tree = xattr_tree;
	return 0;

err_put_inode:
	iput(inode);
	EMMCFS_ERR("can not read extended attributes tree");
	kfree(xattr_tree);
	return err;
}
/**
 * @brief			Hardlinks area constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
static int emmcfs_init_hlinks_area(struct vdfs_sb_info *sbi)
{
	struct inode *inode = NULL;
	struct vdfs_btree *hardlink_tree;
	int err = 0;

	hardlink_tree = kzalloc(sizeof(*hardlink_tree), GFP_KERNEL);
	if (!hardlink_tree)
		return -ENOMEM;

	hardlink_tree->btree_type = VDFS_BTREE_HARD_LINK;
	hardlink_tree->max_record_len = sizeof(struct vdfs_hdrtree_record);
	inode = vdfs_special_iget(sbi->sb, VDFS_HARDLINKS_TREE_INO);

	if (IS_ERR(inode)) {
		int ret;
		kfree(hardlink_tree);
		ret = PTR_ERR(inode);
		return ret;
	}

	err = fill_btree(sbi, hardlink_tree, inode);
	if (err)
		goto err_put_inode;

	hardlink_tree->comp_fn = vdfs_hlinktree_cmpfn;
	sbi->hardlink_tree = hardlink_tree;
	return 0;

err_put_inode:
	iput(inode);
	EMMCFS_ERR("can not read hard link tree");
	return err;
}

/**
 * @brief			Free inode bitmap constructor.
 * @param [in,out]	sbi	The eMMCFS super block info
 * @return			Returns 0 on success, errno on failure
 */
static int build_free_inode_bitmap(struct vdfs_sb_info *sbi)
{
	int ret = 0;
	struct inode *inode;

	inode = vdfs_special_iget(sbi->sb, VDFS_FREE_INODE_BITMAP_INO);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		return ret;
	}

	atomic64_set(&sbi->free_inode_bitmap.last_used, VDFS_1ST_FILE_INO);
	sbi->free_inode_bitmap.inode = inode;
	return ret;
}


#ifdef CONFIG_VDFS_NFS_SUPPORT
/**
 * @brief			Encode inode for export via NFS.
 * @param [in]	ino	Inode to be exported via nfs
 * @param [in,out]	fh	File handle allocated by nfs, this function
 *				fill this memory ino and generation.
 * @param [in,out]	max_len	max len of file handle,
 *			nfs pass to this function max len of the fh
 *			this function return real size of the fh.
 * @param [in] parent	Parent inode of the ino.
 * @return			Returns FILEID_INO32_GEN_PARENT
 */
#if defined(CONFIG_SAMSUNG_VD_DTV_PLATFORM) || \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5))
static int vdfs_encode_fh(
		struct inode *ino, __u32 *fh, int *max_len,
			struct inode *parent
		)
{

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5))
	struct dentry *dentry = d_find_any_alias(ino);
#else
	struct dentry *dentry = list_entry(ino->i_dentry.next,
					    struct dentry, d_alias);
#endif
#else
static int vdfs_encode_fh(
		struct dentry *dentry, __u32 *fh, int *max_len,
					int connectable
		)
{

	struct inode *ino = dentry->d_inode;
#endif
	struct vdfs_indirect_key coords;

	struct vdfs_sb_info *sbi = ino->i_sb->s_fs_info;
	int ret  = 0;
	struct emmcfs_inode_info *inode_i = EMMCFS_I(ino);
	struct emmcfs_cattree_key *record;

	coords.ino = ino->i_ino;
	coords.generation = ino->i_generation;

	coords.inode_type = inode_i->record_type;
	memcpy(fh, (void *) &coords, sizeof(struct vdfs_indirect_key));
	*max_len = (sizeof(struct vdfs_indirect_key) +
			(sizeof(u32) - 1)) / sizeof(u32);
	if (inode_i->record_type > VDFS_CATALOG_PTREE_RECORD)
		return FILEID_INO32_GEN_PARENT;

	record = emmcfs_alloc_cattree_key(dentry->d_name.len,
			inode_i->record_type);

	if (IS_ERR(record))
		return PTR_ERR(record);

	emmcfs_fill_cattree_key(record, inode_i->parent_id, dentry->d_name.name,
			dentry->d_name.len);

	ret = vdfs_add_indirect_index(sbi->catalog_tree, coords.generation,
			coords.ino, record);
	return FILEID_INO32_GEN_PARENT;
}

/**
 * @brief			Decode file handle to dentry for NFS.
 * @param [in]	sb	Superblock struct
 * @param [in]	fid	File handle allocated by nfs.
 * @param [in]	fh_len	len of file handle,
 * @param [in]	fh_type	file handle type
 * @return			Returns decoded dentry
 */
static struct dentry *vdfs_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct vdfs_indirect_key *key = (struct vdfs_indirect_key *)fid;
	struct inode *inode = NULL;
	int ret = 0;

	switch (fh_type) {
	case FILEID_INO32_GEN_PARENT:
		if (key->inode_type > VDFS_CATALOG_PTREE_RECORD)
			inode = vdfs_get_packtree_indirect_inode(sb, key);
		else
			inode = vdfs_get_indirect_inode(VDFS_SB(sb)->
				catalog_tree, key);
		if (IS_ERR(inode)) {
			ret = PTR_ERR(inode);
			goto exit;
		}
		break;
	default:
		ret = -ENOENT;
		break;
	}

exit:
	if (ret)
		return ERR_PTR(ret);
	else
		return d_obtain_alias(inode);
}


static const struct export_operations vdfs_export_ops = {
	.encode_fh = vdfs_encode_fh,
	.fh_to_dentry = vdfs_fh_to_dentry,
};
#endif

/**
 * @brief			Dirty extended super page.
 * @param [in,out]	sb	The VFS superblock
 */
void vdfs_dirty_super(struct vdfs_sb_info *sbi)
{
	if (!(VDFS_IS_READONLY(sbi->sb)))
		set_sbi_flag(sbi, EXSB_DIRTY);

}

#include <linux/genhd.h>
/**
 * @brief			Initialize the eMMCFS filesystem.
 * @param [in,out]	sb	The VFS superblock
 * @param [in,out]	data	FS private information
 * @param [in]		silent	Flag whether to print error message
 * @remark			Reads super block and FS internal data
 *				structures for futher use.
 * @return			Return 0 on success, errno on failure
 */
static int vdfs_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret	= 0;
	struct vdfs_sb_info *sbi;
	struct inode *root_inode;
	char bdev_name[BDEVNAME_SIZE];

#ifdef CONFIG_EMMCFS_PRINT_MOUNT_TIME
	unsigned long mount_start = jiffies;
#endif
	BUILD_BUG_ON(sizeof(struct vdfs_tiny_file_data)
			!= sizeof(struct vdfs_fork));

#if defined(VDFS_GIT_BRANCH) && defined(VDFS_GIT_REV_HASH) && \
		defined(VDFS_VERSION)
	printk(KERN_ERR "%.5s", VDFS_VERSION);
	VDFS_MOUNT_INFO("version is \"%s\"", VDFS_VERSION);
	VDFS_MOUNT_INFO("git branch is \"%s\"", VDFS_GIT_BRANCH);
	VDFS_MOUNT_INFO("git revhash \"%.40s\"", VDFS_GIT_REV_HASH);
#endif
#ifdef CONFIG_VDFS_NOOPTIMIZE
	printk(KERN_WARNING "[VDFS-warning]: Build optimization "
			"is switched off");
#endif

	BUILD_BUG_ON(sizeof(struct vdfs_extended_super_block) != 2048);

	if (!sb)
		return -ENXIO;
	if (!sb->s_bdev)
		return -ENXIO;
	if (!sb->s_bdev->bd_part)
		return -ENXIO;

	VDFS_MOUNT_INFO("mounting %s", bdevname(sb->s_bdev, bdev_name));
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;
	sbi->sb = sb;
#ifdef CONFIG_VDFS_HW2_SUPPORT
	/* Check support of hw decompression by block device */
	if (sb->s_bdev->bd_disk) {
		const struct block_device_operations *ops;
		ops = sb->s_bdev->bd_disk->fops;
		if (ops->hw_decompress_vec) {
			sbi->use_hw_decompressor = 1;
			VDFS_MOUNT_INFO("hw decompression enabled\n");
		}
	}
#endif
	INIT_LIST_HEAD(&sbi->packtree_images.list);
	mutex_init(&sbi->packtree_images.lock_pactree_list);
	INIT_LIST_HEAD(&sbi->dirty_list_head);
	spin_lock_init(&sbi->dirty_list_lock);
	percpu_counter_init(&sbi->free_blocks_count, 0);
	sb->s_maxbytes = EMMCFS_MAX_FILE_SIZE_IN_BYTES;

	ret = emmcfs_parse_options(sb, data);
	if (ret) {
		EMMCFS_ERR("unable to parse mount options\n");
		ret = -EINVAL;
		goto emmcfs_parse_options_error;
	}

	ret = emmcfs_volume_check(sb);
	if (ret)
		goto not_emmcfs_volume;

	ret = vdfs_sb_read(sb, silent);
	if (ret)
		goto emmcfs_sb_read_error;

#ifdef EMMCFS_VERSION
	if (test_option(sbi, VERCHECK)) {
		int version = 0;
		char *parsing_string = EMMCFS_RAW_SB(sbi)->mkfs_git_branch;
		if (!sscanf(parsing_string, "v.%d", &version)) {
			EMMCFS_ERR("Not a release version!\n");
			ret = -EINVAL;
			goto emmcfs_extended_sb_read_error;
		} else if (version < CONFIG_VDFS_OLDEST_LAYOUT_VERSION) {
			EMMCFS_ERR("Non-supported on-disk layout. "
				"Please use version of mkfs.emmcfs utility not "
				"older than %d\n",
				CONFIG_VDFS_OLDEST_LAYOUT_VERSION);
			ret = -EINVAL;
			goto emmcfs_extended_sb_read_error;
		}
	}
#endif


	ret = vdfs_extended_sb_read(sb);
	if (ret)
		goto emmcfs_extended_sb_read_error;

	if (!(VDFS_IS_READONLY(sb))) {
		ret = vdfs_load_debug_area(sb);
		if (ret)
			goto emmcfs_extended_sb_read_error;
	}

#ifdef CONFIG_VDFS_CRC_CHECK
	if (VDFS_RAW_EXSB(sbi)->crc == CRC_ENABLED)
		set_sbi_flag(sbi, VDFS_META_CRC);
	else {
		VDFS_MOUNT_INFO("Driver supports only signed volumes\n");
		ret  = -1;
		goto emmcfs_extended_sb_read_error;
	}
#else
	/* if the image is signed reset the signed flag */
	if (VDFS_RAW_EXSB(sbi)->crc == CRC_ENABLED)
		VDFS_RAW_EXSB(sbi)->crc = CRC_DISABLED;
#endif
	VDFS_MOUNT_INFO("mounted %d times\n",
			VDFS_RAW_EXSB(sbi)->mount_counter);

	/*emmcfs_debug_print_sb(sbi)*/;

	sb->s_op = &emmcfs_sops;
#ifdef CONFIG_VDFS_NFS_SUPPORT
	sb->s_export_op = &vdfs_export_ops;
#endif
	/* s_magic is 4 bytes on 32-bit; system */
	sb->s_magic = (unsigned long) VDFS_SB_SIGNATURE;

	sbi->max_cattree_height = 5;
	ret = vdfs_build_snapshot_manager(sbi);
	if (ret)
		goto emmcfs_build_snapshot_manager_error;

	if (!(VDFS_IS_READONLY(sb))) {
		ret = emmcfs_fsm_build_management(sb);
		if (ret)
			goto emmcfs_fsm_create_error;
	}

	if (test_option(sbi, DEBUG_AREA_CHECK)) {
		int debug_ctr = 0, records_count = 73;
		for (; debug_ctr < records_count; debug_ctr++) {
			ret = VDFS_LOG_ERROR(sbi, debug_ctr);
			if (ret)
				goto emmcfs_fill_ext_tree_error;
		}
		ret = -EAGAIN;
		goto emmcfs_fill_ext_tree_error;
	}

	ret = emmcfs_fill_ext_tree(sbi);
	if (ret)
		goto emmcfs_fill_ext_tree_error;

	ret = emmcfs_fill_cat_tree(sbi);
	if (ret)
		goto emmcfs_fill_cat_tree_error;

	ret = vdsf_fill_xattr_tree(sbi);
	if (ret)
		goto emmcfs_fill_xattr_tree_error;

	ret = emmcfs_init_hlinks_area(sbi);
	if (ret)
		goto emmcfs_hlinks_area_err;

	ret = init_small_files_area_manager(sbi);
	if (ret)
		goto init_small_files_area_error;

#ifdef CONFIG_VDFS_QUOTA
	ret = build_quota_manager(sbi);
	if (ret)
		goto build_quota_error;
#endif
	if (!(VDFS_IS_READONLY(sb))) {
		ret = build_free_inode_bitmap(sbi);
		if (ret)
			goto build_free_inode_bitmap_error;

		ret = vdfs_process_orphan_inodes(sbi);
		if (ret)
			goto process_orphan_inodes_error;
	}

	/* allocate root directory */
	root_inode = get_root_inode(sbi->catalog_tree);
	if (IS_ERR(root_inode)) {
		EMMCFS_ERR("failed to load root directory\n");
		ret = PTR_ERR(root_inode);
		goto emmcfs_iget_err;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 5)
	sb->s_root = d_alloc_root(root_inode);
#else
	sb->s_root = d_make_root(root_inode);
#endif
	if (!sb->s_root) {
		EMMCFS_ERR("unable to get root inode\n");
		ret = -EINVAL;
		goto d_alloc_root_err;
	}

	if (DATE_RESOLUTION_IN_NANOSECONDS_ENABLED)
		sb->s_time_gran = 1;
	else
		printk(KERN_WARNING
			"Date resolution in nanoseconds is disabled\n");

#ifdef CONFIG_VDFS_CHECK_FRAGMENTATION
	for (count = 0; count < EMMCFS_EXTENTS_COUNT_IN_FORK; count++)
		sbi->in_fork[count] = 0;
	sbi->in_extents_overflow = 0;
#endif

	if (test_option(sbi, BTREE_CHECK)) {
		ret = vdfs_check_btree_links(sbi->catalog_tree, NULL);
		if (ret) {
			EMMCFS_ERR("Bad btree!!!\n");
			goto btree_not_verified;
		}
	}


	if (!(VDFS_IS_READONLY(sb))) {
		/* If somebody will switch power off right atre mounting,
		 * mount count will not inclreased
		 * But this operation takes a lot of time, and dramatically
		 * increase mount time */
		le32_add_cpu(&(VDFS_RAW_EXSB(sbi)->mount_counter), 1);
		le32_add_cpu(&(VDFS_RAW_EXSB(sbi)->generation), 1);
		vdfs_dirty_super(sbi);
	}

	set_sbi_flag(sbi, IS_MOUNT_FINISHED);
	EMMCFS_DEBUG_SB("finished ok");

#ifdef CONFIG_EMMCFS_PRINT_MOUNT_TIME
	{
		unsigned long result = jiffies - mount_start;
		printk(KERN_INFO "Mount time %lu ms\n", result * 1000 / HZ);
	}
#endif

	init_high_priority(&sbi->high_priority);
#if defined(CONFIG_VDFS_PROC)
	ret = vdfs_build_proc_entry(sbi);
	if (ret)
		goto build_proc_entry_error;
#endif
	return 0;
#if defined(CONFIG_VDFS_PROC)
build_proc_entry_error:
#endif
btree_not_verified:
	dput(sb->s_root);
	sb->s_root = NULL;
	root_inode = NULL;

d_alloc_root_err:
	iput(root_inode);

emmcfs_iget_err:
process_orphan_inodes_error:
	destroy_free_inode_bitmap(sbi);

build_free_inode_bitmap_error:
#ifdef CONFIG_VDFS_QUOTA
	destroy_quota_manager(sbi);

build_quota_error:
#endif
	destroy_small_files_area_manager(sbi);

init_small_files_area_error:
	emmcfs_put_btree(sbi->hardlink_tree);
	sbi->hardlink_tree = NULL;

emmcfs_hlinks_area_err:
	emmcfs_put_btree(sbi->xattr_tree);
	sbi->xattr_tree = NULL;

emmcfs_fill_xattr_tree_error:
	emmcfs_put_btree(sbi->catalog_tree);
	sbi->catalog_tree = NULL;

emmcfs_fill_cat_tree_error:
	emmcfs_put_btree(sbi->extents_tree);
	sbi->extents_tree = NULL;

emmcfs_fill_ext_tree_error:
	emmcfs_fsm_destroy_management(sb);

emmcfs_fsm_create_error:
	emmcfs_destroy_snapshot_manager(sbi);

emmcfs_build_snapshot_manager_error:
	if (!(VDFS_IS_READONLY(sb)))
		vdfs_free_debug_area(sb);

emmcfs_extended_sb_read_error:
emmcfs_sb_read_error:
emmcfs_parse_options_error:
not_emmcfs_volume:
	destroy_super(sbi);

	sb->s_fs_info = NULL;
	kfree(sbi);
	EMMCFS_DEBUG_SB("finished with error = %d", ret);

	return ret;
}


/**
 * @brief				Method to get the eMMCFS super block.
 * @param [in,out]	fs_type		Describes the eMMCFS file system
 * @param [in]		flags		Input flags
 * @param [in]		dev_name	Block device name
 * @param [in,out]	data		Private information
 * @param [in,out]	mnt		Mounted eMMCFS filesystem information
 * @return				Returns 0 on success, errno on failure
 */
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
static int emmcfs_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data,
		vdfs_fill_super, mnt);
}
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) || \
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
static struct dentry *emmcfs_mount(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, vdfs_fill_super);
}
#endif

static void vdfs_kill_block_super(struct super_block *sb)
{
	kill_block_super(sb);
}

/*
 * Structure of the eMMCFS filesystem type
 */
static struct file_system_type vdfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "vdfs",
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 35)
	.get_sb		= emmcfs_get_sb,
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 20) || \
		LINUX_VERSION_CODE == KERNEL_VERSION(3, 0, 33) || \
		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 5)
	.mount		= emmcfs_mount,
#else
	BUILD_EMMCFS_BUG();
#endif
	.kill_sb	= vdfs_kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

/**
 * @brief				Inode storage initializer.
 * @param [in,out]	generic_inode	Inode for init
 * @return		void
 */
static void emmcfs_init_inode_once(void *generic_inode)
{
	struct emmcfs_inode_info *inode = generic_inode;

	mutex_init(&inode->truncate_mutex);
	inode_init_once(&inode->vfs_inode);
	INIT_LIST_HEAD(&inode->runtime_extents);
}

/**
 * @brief	Initialization of the eMMCFS module.
 * @return	Returns 0 on success, errno on failure
 */
static int __init init_emmcfs_fs(void)
{
	int ret;

	vdfs_inode_cachep = kmem_cache_create("vdfs_icache",
				sizeof(struct emmcfs_inode_info), 0,
				SLAB_HWCACHE_ALIGN, emmcfs_init_inode_once);
	if (!vdfs_inode_cachep) {
		EMMCFS_ERR("failed to initialise inode cache\n");
		return -ENOMEM;
	}

	ret = vdfs_init_btree_caches();
	if (ret)
		goto fail_create_btree_cache;

	ret = vdfs_hlinktree_cache_init();
	if (ret)
		goto fail_create_hlinktree_cache;

	ret = emmcfs_exttree_cache_init();
	if (ret)
		goto fail_create_exttree_cache;

	ret = emmcfs_fsm_cache_init();

	if (ret)
		goto fail_create_fsm_cache;
#if defined(CONFIG_VDFS_PROC)
	ret = vdfs_dir_init();
	if (ret)
		goto fail_register_proc;
#endif
	ret = register_filesystem(&vdfs_fs_type);
	if (ret)
		goto failed_register_fs;
	writeback_thread = NULL;
	return 0;

failed_register_fs:
#if defined(CONFIG_VDFS_PROC)
	vdfs_dir_exit();
fail_register_proc:
#endif
	emmcfs_fsm_cache_destroy();
fail_create_fsm_cache:
	emmcfs_exttree_cache_destroy();
fail_create_exttree_cache:
	vdfs_hlinktree_cache_destroy();
fail_create_hlinktree_cache:
	vdfs_destroy_btree_caches();
fail_create_btree_cache:

	return ret;
}

/**
 * @brief		Module unload callback.
 * @return	void
 */
static void __exit exit_emmcfs_fs(void)
{
#if defined(CONFIG_VDFS_PROC)
	vdfs_dir_exit();
#endif
	emmcfs_fsm_cache_destroy();
	unregister_filesystem(&vdfs_fs_type);
	kmem_cache_destroy(vdfs_inode_cachep);
	emmcfs_exttree_cache_destroy();
	vdfs_hlinktree_cache_destroy();
}

module_init(init_emmcfs_fs)
module_exit(exit_emmcfs_fs)

module_param(debug_mask, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_mask, "Debug mask (1 - print debug for superblock ops,"\
				" 2 - print debug for inode ops)");
MODULE_AUTHOR("Samsung R&D Russia - System Software Lab - VD Software Group");
MODULE_VERSION(__stringify(VDFS_VERSION));
MODULE_DESCRIPTION("Vertically Deliberate improved performance File System");
MODULE_LICENSE("GPL");
