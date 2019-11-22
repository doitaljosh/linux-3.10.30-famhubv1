/*
 * sysctl.h: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 *
 ****************************************************************
 ****************************************************************
 **
 **  WARNING:
 **  The values in this file are exported to user space via 
 **  the sysctl() binary interface.  Do *NOT* change the
 **  numbering of any existing values here, and do not change
 **  any numbers within any one set of values.  If you have to
 **  redefine an existing interface, use a new number for it.
 **  The kernel will then return -ENOTDIR to any application using
 **  the old binary interface.
 **
 ****************************************************************
 ****************************************************************
 */
#ifndef _LINUX_SYSCTL_H
#define _LINUX_SYSCTL_H

#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <linux/rbtree.h>
#include <uapi/linux/sysctl.h>

/* For the /proc/sys support */
struct ctl_table;
struct nsproxy;
struct ctl_table_root;
struct ctl_table_header;
struct ctl_dir;

typedef struct ctl_table ctl_table;

typedef int proc_handler (struct ctl_table *ctl, int write,
			  void __user *buffer, size_t *lenp, loff_t *ppos);

extern int proc_dostring(struct ctl_table *, int,
			 void __user *, size_t *, loff_t *);
extern int proc_dointvec(struct ctl_table *, int,
			 void __user *, size_t *, loff_t *);
extern int proc_dointvec_minmax(struct ctl_table *, int,
				void __user *, size_t *, loff_t *);
extern int proc_dointvec_jiffies(struct ctl_table *, int,
				 void __user *, size_t *, loff_t *);
extern int proc_dointvec_userhz_jiffies(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
extern int proc_dointvec_ms_jiffies(struct ctl_table *, int,
				    void __user *, size_t *, loff_t *);
extern int proc_doulongvec_minmax(struct ctl_table *, int,
				  void __user *, size_t *, loff_t *);
extern int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int,
				      void __user *, size_t *, loff_t *);
extern int proc_do_large_bitmap(struct ctl_table *, int,
				void __user *, size_t *, loff_t *);

/*
 * Register a set of sysctl names by calling register_sysctl_table
 * with an initialised array of struct ctl_table's.  An entry with 
 * NULL procname terminates the table.  table->de will be
 * set up by the registration and need not be initialised in advance.
 *
 * sysctl names can be mirrored automatically under /proc/sys.  The
 * procname supplied controls /proc naming.
 *
 * The table's mode will be honoured both for sys_sysctl(2) and
 * proc-fs access.
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.  A
 * null procname disables /proc mirroring at this node.
 *
 * sysctl(2) can automatically manage read and write requests through
 * the sysctl table.  The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 * 
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases.
 */

/* Support for userspace poll() to watch for changes */
struct ctl_table_poll {
	atomic_t event;
	wait_queue_head_t wait;
};

static inline void *proc_sys_poll_event(struct ctl_table_poll *poll)
{
	return (void *)(unsigned long)atomic_read(&poll->event);
}

#define __CTL_TABLE_POLL_INITIALIZER(name) {				\
	.event = ATOMIC_INIT(0),					\
	.wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.wait) }

#define DEFINE_CTL_TABLE_POLL(name)					\
	struct ctl_table_poll name = __CTL_TABLE_POLL_INITIALIZER(name)

/* A sysctl table is an array of struct ctl_table: */
struct ctl_table 
{
	const char *procname;		/* Text ID for /proc/sys, or zero */
	void *data;
	int maxlen;
	umode_t mode;
	struct ctl_table *child;	/* Deprecated */
	proc_handler *proc_handler;	/* Callback for text formatting */
	struct ctl_table_poll *poll;
	void *extra1;
	void *extra2;
};

struct ctl_node {
	struct rb_node node;
	struct ctl_table_header *header;
};

/* struct ctl_table_header is used to maintain dynamic lists of
   struct ctl_table trees. */
struct ctl_table_header
{
	union {
		struct {
			struct ctl_table *ctl_table;
			int used;
			int count;
			int nreg;
		};
		struct rcu_head rcu;
	};
	struct completion *unregistering;
	struct ctl_table *ctl_table_arg;
	struct ctl_table_root *root;
	struct ctl_table_set *set;
	struct ctl_dir *parent;
	struct ctl_node *node;
};

struct ctl_dir {
	/* Header must be at the start of ctl_dir */
	struct ctl_table_header header;
	struct rb_root root;
};

struct ctl_table_set {
	int (*is_seen)(struct ctl_table_set *);
	struct ctl_dir dir;
};

struct ctl_table_root {
	struct ctl_table_set default_set;
	struct ctl_table_set *(*lookup)(struct ctl_table_root *root,
					   struct nsproxy *namespaces);
	int (*permissions)(struct ctl_table_header *head, struct ctl_table *table);
};

/* struct ctl_path describes where in the hierarchy a table is added */
struct ctl_path {
	const char *procname;
};

#ifdef CONFIG_SYSCTL

struct file;

/* @FIXME. dirty hack:
 * this is to workaround smack checks and to let non root users set some of
 * sysctl values*/
extern ssize_t vd_proc_sysctl_operation(struct ctl_table *table,
		struct file *file, int write, char __user *buf, size_t *count,
		loff_t *ppos);

#define VD_PROC_SYSCTL_TABLE_OP(_name)						\
ssize_t vd_proc_sysctl_write_##_name(struct file *file,			\
		const char __user *buf, size_t count, loff_t *ppos)		\
{										\
	return vd_proc_sysctl_operation(_name, file, 1, (char __user *)buf,	\
			&count, ppos);						\
}										\
										\
ssize_t vd_proc_sysctl_read_##_name(struct file *file,				\
		char __user *buf, size_t count, loff_t *ppos)			\
{										\
	vd_proc_sysctl_operation(_name, file, 0, buf, &count, ppos);		\
	return count;								\
}

/* generates vd_proc_sysctl_TAB_operations file_operations structure */
#define VD_PROC_SYSCTL_DEFINE_FILE_OPS(_name)					\
extern ssize_t vd_proc_sysctl_write_##_name(struct file *file,			\
		const char __user *buf, size_t count, loff_t *ppos);		\
extern ssize_t vd_proc_sysctl_read_##_name(struct file *file,			\
		char __user *buf, size_t count, loff_t *ppos);			\
										\
static const struct file_operations vd_proc_sysctl_##_name##_operations = {	\
	.write		= vd_proc_sysctl_write_##_name,				\
	.read		= vd_proc_sysctl_read_##_name,				\
	.llseek		= no_llseek						\
}

void proc_sys_poll_notify(struct ctl_table_poll *poll);

extern void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *));
extern void retire_sysctl_set(struct ctl_table_set *set);

void register_sysctl_root(struct ctl_table_root *root);
struct ctl_table_header *__register_sysctl_table(
	struct ctl_table_set *set,
	const char *path, struct ctl_table *table);
struct ctl_table_header *__register_sysctl_paths(
	struct ctl_table_set *set,
	const struct ctl_path *path, struct ctl_table *table);
struct ctl_table_header *register_sysctl(const char *path, struct ctl_table *table);
struct ctl_table_header *register_sysctl_table(struct ctl_table * table);
struct ctl_table_header *register_sysctl_paths(const struct ctl_path *path,
						struct ctl_table *table);

void unregister_sysctl_table(struct ctl_table_header * table);

extern int sysctl_init(void);
#else /* CONFIG_SYSCTL */

#define VD_PROC_SYSCTL_DEFINE_FILE_OPS(_name)
#define VD_PROC_SYSCTL_TABLE_OP(_name)

extern ssize_t vd_proc_sysctl_operation(struct ctl_table *table,
		struct file *file, int write, char __user *buf, size_t *count,
		loff_t *ppos);

static inline struct ctl_table_header *register_sysctl_table(struct ctl_table * table)
{
	return NULL;
}

static inline struct ctl_table_header *register_sysctl_paths(
			const struct ctl_path *path, struct ctl_table *table)
{
	return NULL;
}

static inline void unregister_sysctl_table(struct ctl_table_header * table)
{
}

static inline void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *))
{
}

#endif /* CONFIG_SYSCTL */

#endif /* _LINUX_SYSCTL_H */
