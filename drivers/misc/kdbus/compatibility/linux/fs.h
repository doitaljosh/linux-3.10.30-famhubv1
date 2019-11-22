#include_next <linux/fs.h>

#ifndef KDBUS_FS_H
#define KDBUS_FS_H

#include <linux/version.h>
//memfd.c
// inspiration:
// https://groups.google.com/forum/#!searchin/linux.kernel/file_inode/linux.kernel/o6_raT6IOrs/9JHAg66rmngJ
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
static inline struct inode *file_inode(struct file *f)
{
        return f->f_mapping->host;
}
#endif
#endif /* KDBUS_FS_H */
