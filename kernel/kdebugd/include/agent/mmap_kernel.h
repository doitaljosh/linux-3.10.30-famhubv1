#ifndef _MMAP_KERNEL_H
#define _MMAP_KERNEL_H

/* extern declaration for kdbg_agent_fops */
extern const struct file_operations kdbg_agent_fops;

/* mmap function prototypes */
int kdbg_agent_open(struct inode *inode, struct file *filp);
int kdbg_agent_mmap(struct file *filp, struct vm_area_struct *vma);
int kdbg_agent_close(struct inode *inode, struct file *filp);

#endif
