#include "emmcfs.h"
#ifdef CONFIG_VDFS_QUOTA
int build_quota_manager(struct vdfs_sb_info *sbi)
{
	sbi->quotas = kzalloc(INITIAL_QUOTA_ARRAY_SIZE *
			sizeof(struct vdfs_quota), GFP_KERNEL);
	if (!sbi->quotas)
		return -ENOMEM;
	sbi->quota_current_size = INITIAL_QUOTA_ARRAY_SIZE;
	sbi->quota_first_free_index = 0;
	return 0;
}

void destroy_quota_manager(struct vdfs_sb_info *sbi)
{
	kfree(sbi->quotas);
}

int get_next_quota_index(struct vdfs_sb_info *sbi)
{
	int index;

	index = ++sbi->quota_first_free_index;

	if (index > sbi->quota_current_size) {
		void *new = krealloc(sbi->quotas,
				     (sbi->quota_current_size << 1) *
					     sizeof(struct vdfs_quota),
				     GFP_KERNEL | __GFP_ZERO);
		if (unlikely(ZERO_OR_NULL_PTR(new)))
			return -ENOMEM;
		/* Set realloced array and new size */
		sbi->quotas = new;
		sbi->quota_current_size <<= 1;
	}

	return index;
}

int get_quota(struct dentry *dentry)
{
	struct vdfs_sb_info *sbi = VDFS_SB(dentry->d_inode->i_sb);
	int index;
	char temp[UUL_MAX_LEN + 1];
	ssize_t rc;

	memset(temp, 0, UUL_MAX_LEN + 1);
	rc = vdfs_getxattr(dentry, QUOTA_HAS_XATTR, temp, UUL_MAX_LEN);
	if (IS_ERR_VALUE(rc))
		return rc;

	index = get_next_quota_index(sbi);

	BUG_ON(index >= sbi->quota_current_size);

	sbi->quotas[index].ino = dentry->d_inode->i_ino;
	if (kstrtoull(temp, 10, &sbi->quotas[index].has) < 0)
		return -EINVAL;

	memset(temp, 0, UUL_MAX_LEN + 1);
	rc = vdfs_getxattr(dentry, QUOTA_XATTR, temp, UUL_MAX_LEN);
	if (IS_ERR_VALUE(rc))
		return rc;

	if (kstrtoull(temp, 10, &sbi->quotas[index].max) < 0)
		return -EINVAL;
	return index;
}

#endif
