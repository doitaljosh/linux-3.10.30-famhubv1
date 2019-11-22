/*
 * Samsung Electronics
 * TTV SBB project
 *
 * macros and API missing or incompatible in kernel 3.8
 */
#include <linux/version.h>

// message.c patch
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
int task_cgroup_path_from_hierarchy(struct task_struct *task, int hierarchy_id,
            char *buf, size_t buflen);
#endif


