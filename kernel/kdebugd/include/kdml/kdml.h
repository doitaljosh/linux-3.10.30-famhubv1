/*
 * kdml.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef __KDML_H__
#define __KDML_H__

#define KDML_VERSION_STRING " v1.0.1"

/*
 * restrict kdml memory usage:
 * - allocations after MAX_ENTRIES_PER_CALLER per caller will not be tracked
 * - 0 means unrestricted
 */
#define MAX_ENTRIES_PER_CALLER 1000

/* GFP flags for kdml internal allocations */
#define GFP_KDML_FLAGS  (GFP_NOWAIT | \
		__GFP_NORETRY | __GFP_NOMEMALLOC | \
		__GFP_NOWARN)

/* global structure: used as placeholder */
struct kdml_conf {
	struct kmem_cache *kdml_node_cache;
	struct kmem_cache *kdml_summary_node_cache;
	struct kmem_cache *kdml_module_cache;
	struct rb_root kdml_rbtree;
	int kdml_filter;
	struct rb_root kdml_summary_tree;
	struct list_head kdml_module_list;
	int entries_per_caller; /* restrict entries */
};

extern struct kdml_conf kdml_global_conf;

extern int kdml_init_mode;

/* module related functions */
extern int kdml_register_module_probe(void);
extern void kdml_unregister_module_probe(void);

extern int kdml_menu(void);
extern void kdml_print_caller_summary_info(void);
extern int kdml_print_module_summary_info(void);
extern void kdml_clear_allocations(void);
extern void kdbg_kdml_exit(void);
extern int kdbg_kdml_init(void);
#ifdef CONFIG_SLABINFO
extern int kdml_get_cache_list(struct kdml_slab_info_line *slabinfo_arr,
		const int count, int *result);
#endif

#endif
