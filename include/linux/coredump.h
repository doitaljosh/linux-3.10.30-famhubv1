#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/siginfo.h>

#ifndef CONFIG_PLAT_TIZEN
#ifdef CONFIG_PROC_VD_TASK_LIST
#include <linux/vd_task_policy.h>
#else
/* allowTask and exceptTask numbers macro. */
#define ALLOWED_TASK_NUM    5
#define EXCEPT_TASK_NUM     4

/* Only allowTask is need to show information */
extern const char *allowTask[ALLOWED_TASK_NUM];
/* exceptTask list for not to display show information */
extern const char *exceptTask[EXCEPT_TASK_NUM];
#endif
#endif /*CONFIG_PLAT_TIZEN*/
/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
extern int dump_write(struct file *file, const void *addr, int nr);
extern int dump_seek(struct file *file, loff_t off);
#ifdef CONFIG_COREDUMP
struct coredump_params;
extern void do_coredump(siginfo_t *siginfo);
#else
static inline void do_coredump(siginfo_t *siginfo) {}
#endif

#ifdef CONFIG_MINIMAL_CORE
extern void do_minimal_core(struct coredump_params *cprm);
#endif

#endif /* _LINUX_COREDUMP_H */
