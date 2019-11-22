/*
 * kdml_internal.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef __KDML_INTERNAL_H__
#define __KDML_INTERNAL_H__

#define ddebug_info(fmt, args...) do {} while (0)

/*
#define ddebug_info(fmt, args...) {		\
		printk(KERN_ERR			\
			"KDML::%s %d" fmt,	\
			__func__, __LINE__, ## args);	\
}
*/

#define ddebug_enter(void) {			\
		printk(KERN_ERR			\
			"KDML::%s %d\n",	\
			__func__, __LINE__);	\
}

#define ddebug_err(fmt, args...) {			\
		printk(KERN_ERR				\
			"ERR:: KDML::%s %d " fmt,	\
			__func__, __LINE__, ## args);	\
}

#define ddebug_print(fmt, args...) {		\
		printk(KERN_CRIT		\
			 fmt, ## args);	\
}


enum {
	KDML_MODE_NONE = 0,
	KDML_MODE_RUNNING = 1,
	KDML_MODE_MAX,
};

enum {
	KDML_TRACE_KMALLOC = 1 << 0,
	KDML_TRACE_KMALLOC_NODE = 1 << 1,
	KDML_TRACE_KMEMCACHE = 1 << 2,
	KDML_TRACE_KMEMCACHE_NODE = 1 << 3,
	KDML_TRACE_PAGE_ALLOC = 1 << 4,
};

enum {
	KDML_MENU_TRACE_START = 1,
	KDML_MENU_TRACE_STOP,
	KDML_MENU_TRACE_CALLER_REP_SUMMARY,
	KDML_MENU_TRACE_MODULE_REP_SUMMARY,
	KDML_MENU_TRACE_SUMMARY,
	KDML_MENU_KERNEL_TRACE_SUMMARY,
	KDML_MENU_USER_TRACE_SUMMARY,
	KDML_MENU_SNAPSHOT_SLABINFO,
	KDML_MENU_CLEAR_STATS,
	KDML_MENU_FILTER_FUNC,
	KDML_MENU_RESTRICT,
	KDML_MENU_EXIT = 99,
};

extern unsigned long kdml_backtrace_addr;
extern void kdml_cleanup_probe_memory(void);

extern int kdml_get_current_mode(void);
extern int kdml_set_current_mode(int);

/* probe functions */
extern void probe_mm_page_alloc(void *ignore, struct page *page, unsigned int order,
		gfp_t gfp_flags, unsigned long call_site);
extern void probe_mm_page_free(void *ignore, struct page *page, unsigned int order);
extern void
probe_kmalloc(void *ignore, unsigned long call_site, const void *ptr,
		size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags);
extern void
probe_kmalloc_node(void *ignore, unsigned long call_site, const void *ptr,
		size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags, int node);
extern void
probe_kfree(void *ignore, unsigned long call_site, const void *ptr);
extern void
probe_kmem_cache_alloc(void *ignore, unsigned long call_site,
		const void *ptr, size_t bytes_req, size_t bytes_alloc,
		gfp_t gfp_flags);
extern void
probe_kmem_cache_alloc_node(void *ignore, unsigned long call_site,
		const void *ptr,
		size_t bytes_req,
		size_t bytes_alloc,
		gfp_t gfp_flags,
		int node);
extern void
probe_kmem_cache_free(void *ignore, unsigned long call_site, const void *ptr);
#endif /* __KDML_INTERNAL_H__ */
