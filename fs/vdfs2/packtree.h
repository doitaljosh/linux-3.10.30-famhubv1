
#ifndef PACKTREE_H_
#define PACKTREE_H_

/**
 * The vdfs packtree operations.
 */
#define SQUASHFS_COMPRESSED_BIT_BLOCK	(1 << 24)

struct installed_packtree_info {
	struct list_head list;
	struct vdfs_btree *tree;
	struct vdfs_pack_insert_point_info params;
	__u64 *chunk_table; /* chunk table (sorted array of chunk offsets) */

	/* hold unpacked pages */
	struct inode *unpacked_inode;
	atomic_t open_count; /* count of open files in this packtree */
#ifdef CONFIG_VDFS_DEBUG
	/* print dump lock */
	struct mutex dump_lock;
	/* print only once*/
	int print_once;
#endif

};

struct xattr_entry {
	__le16	type;
	__le16	size;
	char	data[0];
};

struct xattr_val {
	__le32	vsize;
	char	value[0];
};

/**
 * @brief	Method to get packtree source image inode.
 */
struct inode *get_packtree_image_inode(struct vdfs_sb_info *sbi,
		__u64 parent_id, __u8 *name, int name_len);

/**
 * @brief	Method to destroy packtree list.
 */
int *destroy_packtrees_list(struct vdfs_sb_info *sbi);

/**
 * @brief		Get inode using indirect key.
 */
struct inode *vdfs_get_packtree_indirect_inode(struct super_block *sb,
		struct vdfs_indirect_key *key);

/**
 * @brief		Destroy installed_packtree information structure.
 */
void destroy_packtree(struct installed_packtree_info *ptr);

/**
 * @brief		Uninstall packtree image function.
 * @param [in]	inode	Pointer to inode
 * @param [in]	data	Pointer to structure with information about
 *			parent directory of installed packtree image.
 * @return		Returns error codes
 */
int vdfs_uninstall_packtree(struct inode *inode,
		struct ioctl_uninstall_params *data);
/* todo */
int read_squashfs_image_simple(struct inode *inode, __u64 offset, __u32 length,
		void *data);

extern const struct address_space_operations emmcfs_aops;
extern const struct file_operations emmcfs_dir_operations;

#endif
