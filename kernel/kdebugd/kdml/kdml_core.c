/*
 * kdml_core.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <kdebugd.h>
#include <trace/events/kmem.h>
#define CREATE_TRACE_POINTS
#include "kdml/kdml_trace.h"
#include "kdml/kdml_packet.h"
#include "kdml/kdml.h"
#include "kdml/kdml_internal.h"
#include "kdml/kdml_rb_tree.h"
#include <linux/mm.h>
#include "kdml/kdml_meminfo.h"

/* select the argument for KDML */
#ifdef CONFIG_KDML_AUTO_START
int kdml_init_mode = KDML_MODE_RUNNING;
#else
int kdml_init_mode = KDML_MODE_NONE;
#endif

struct kdml_conf kdml_global_conf;

static int kdml_register_tracer(void);
static void kdml_unregister_tracer(void);

static int __init early_kdml_init(char *arg)
{
	char *options = NULL;

	if (!arg)
		return -EINVAL;

	options = arg;

	kdml_init_mode = (int)simple_strtoul(options, &options, 16);
	if (kdml_init_mode == KDML_MODE_RUNNING) {
		/*
		 * TODO: Decide on the string and
		 * Add corresponding filter
		 */
	}
	ddebug_info("kdml_init_mode: %d\n",
			kdml_init_mode);
	return 0;
}
early_param("kdml", early_kdml_init);

static int register_kdml_function(int mode)
{
	int ret = 0;
	if (mode == KDML_MODE_RUNNING) {
		ddebug_info("kdml set in detailed mode\n");
		/* remove previous allocations */
		kdml_clear_allocations();
		ret = kdml_register_tracer();
	}
	return ret;
}


static void unregister_kdml_function(int mode)
{
	if (mode == KDML_MODE_RUNNING)
		kdml_unregister_tracer();
	tracepoint_synchronize_unregister();
}

int kdml_get_current_mode(void)
{
	int mode = kdml_init_mode;

	if (mode < KDML_MODE_NONE || mode >= KDML_MODE_MAX) {
		ddebug_err("invalid mode: %d\n", mode);
		mode = KDML_MODE_NONE;
	}
	return mode;
}

int kdml_set_current_mode(int mode)
{
	int current_mode = kdml_init_mode;
	int ret;

	if (mode == current_mode)
		return 0;

	if (mode < KDML_MODE_NONE || mode >= KDML_MODE_MAX) {
		ddebug_err("invalid mode: %d, not changing from: %d\n", mode, current_mode);
		return -1;
	}
	unregister_kdml_function(current_mode);
	ret = register_kdml_function(mode);
	if (!ret)
		kdml_init_mode = mode;

	return ret;
}

static int kdml_register_tracer(void)
{
	int ret = 0;
	int filter = kdml_global_conf.kdml_filter;

	if (filter & KDML_TRACE_PAGE_ALLOC) {
		ret = register_trace_mm_page_alloc_kdml(probe_mm_page_alloc, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for mm_page_alloc\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			return -1;
		} else {
			ddebug_info("Tracer registered for mm_page_alloc\n");
		}
		ret = register_trace_mm_page_free(probe_mm_page_free, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for mm_page_alloc\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_mm_page_free;
		} else {
			ddebug_info("Tracer registered for mm_page_free\n");
		}
	}

	if (filter & KDML_TRACE_KMALLOC) {
		ret = register_trace_kmalloc(probe_kmalloc, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kmalloc\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kmalloc;
		} else {
			ddebug_info("Tracer registered for kmalloc\n");
		}

		ret = register_trace_kmalloc_node(probe_kmalloc_node, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kmalloc_node\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kmalloc_node;
		} else {
			ddebug_info("Tracer registered for kmalloc node\n");
		}

		ret = register_trace_kfree(probe_kfree, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kfree\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kfree;
		} else {
			ddebug_info("Tracer registered for kfree\n");
		}
	}

	if (filter & KDML_TRACE_KMEMCACHE) {
		ret = register_trace_kmem_cache_alloc(probe_kmem_cache_alloc, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kmem_cache_alloc\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kmem_cache_alloc;
		} else {
			ddebug_info("Tracer registered for kmem_cache_alloc\n");
		}

		ret = register_trace_kmem_cache_alloc_node(probe_kmem_cache_alloc_node, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kmem_cache_alloc_node\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kmem_cache_alloc_node;
		} else {
			ddebug_info("Tracer registered for kmem_cache_alloc_node\n");
		}

		ret = register_trace_kmem_cache_free(probe_kmem_cache_free, NULL);
		if (ret == -EEXIST) {
			ddebug_err("Tracer already registered for kmem_cache_free\n");
		} else if (ret) {
			ddebug_err("Tracer failed\n");
			goto err_trace_kmem_cache_free;
		} else {
			ddebug_info("Tracer registered for kmem_cache_free\n");
		}
	}
	return 0;

	/* it is safe to call unregister_trace_xxx even if it was never registered */
err_trace_kmem_cache_free:
	unregister_trace_kmem_cache_alloc_node(probe_kmem_cache_alloc_node, NULL);
err_trace_kmem_cache_alloc_node:
	unregister_trace_kmem_cache_alloc(probe_kmem_cache_alloc, NULL);
err_trace_kmem_cache_alloc:
	unregister_trace_kfree(probe_kfree, NULL);
err_trace_kfree:
	unregister_trace_kmem_cache_alloc_node(probe_kmem_cache_alloc_node, NULL);
err_trace_kmalloc_node:
	unregister_trace_kmalloc(probe_kmalloc, NULL);
err_trace_kmalloc:
	unregister_trace_mm_page_free(probe_mm_page_free, NULL);
err_trace_mm_page_free:
	unregister_trace_mm_page_alloc_kdml(probe_mm_page_alloc, NULL);

	return -1;
}

static void kdml_unregister_tracer(void)
{
	int filter = kdml_global_conf.kdml_filter;

	if (filter & KDML_TRACE_PAGE_ALLOC) {
		unregister_trace_mm_page_alloc_kdml(probe_mm_page_alloc, NULL);
		unregister_trace_mm_page_free(probe_mm_page_free, NULL);
	}
	if (filter & KDML_TRACE_KMALLOC) {
		unregister_trace_kmalloc(probe_kmalloc, NULL);
		unregister_trace_kmalloc_node(probe_kmalloc_node, NULL);
		unregister_trace_kfree(probe_kfree, NULL);
	}
	if (filter & KDML_TRACE_KMEMCACHE) {
		unregister_trace_kmem_cache_alloc(probe_kmem_cache_alloc, NULL);
		unregister_trace_kmem_cache_alloc_node(probe_kmem_cache_alloc_node, NULL);
		unregister_trace_kmem_cache_free(probe_kmem_cache_free, NULL);
	}
}

int kdbg_kdml_init(void)
{
	int ret = -1;
	struct kdml_conf *conf = &kdml_global_conf;

	/* initialize the data-structures
	 * create cache
	 */
	conf->kdml_node_cache = KMEM_CACHE(kdml_rb_type, 0);
	if (!conf->kdml_node_cache) {
		ddebug_err("Error in KMEM_CACHE..\n");
		return ret;
	}

	conf->kdml_summary_node_cache = KMEM_CACHE(kdml_summary_type, 0);
	if (!conf->kdml_summary_node_cache) {
		ddebug_err("Error in KMEM_CACHE..\n");
		goto err_destroy_node_cache;
	}

	conf->kdml_module_cache = KMEM_CACHE(kdml_module_type, 0);
	if (!conf->kdml_module_cache) {
		ddebug_err("Error in KMEM_CACHE..\n");
		goto err_destroy_summary_cache;
	}

	conf->entries_per_caller = MAX_ENTRIES_PER_CALLER;
	conf->kdml_filter = KDML_TRACE_KMALLOC | KDML_TRACE_KMEMCACHE | KDML_TRACE_PAGE_ALLOC;

	/* initialize database */
	conf->kdml_rbtree = RB_ROOT;
	conf->kdml_summary_tree = RB_ROOT;
	INIT_LIST_HEAD(&(conf->kdml_module_list));

	/* test with detail mode */
	ret = kdml_register_module_probe();
	if (ret) {
		ddebug_err("Error in tracing modules...\n");
		goto err_destroy_module_cache;
	}

	ret = register_kdml_function(kdml_init_mode);
	if (ret) {
		ddebug_err("Error in setting KDML, can't continue...\n");
		goto err_unregister_module_probe;
	}

	/* register the counter monitors for summary mode */
	ret = kdml_ctr_register_meminfo_functions();
	if (ret) {
		ddebug_err("Error in memory counters..\n");
		goto err_unregister_kdml_function;
	}

	/* register counter monitor */
	ret = kdbg_register("DEBUG: Kernel Memory Leak Detector" KDML_VERSION_STRING,
			kdml_menu, NULL, KDBG_MENU_KDML);
	if (ret) {
		ddebug_err("Error in registering KDML with kdebugd..\n");
		goto err_unregister_meminfo;
	}

	ddebug_info("KDML Init done..\n");
	return 0;

	/* error handling... undo all the operations.. */
err_unregister_meminfo:
	kdml_ctr_unregister_meminfo_functions();
err_unregister_kdml_function:
	unregister_kdml_function(kdml_init_mode);
err_unregister_module_probe:
	kdml_unregister_module_probe();
err_destroy_module_cache:
	kmem_cache_destroy(conf->kdml_module_cache);
err_destroy_summary_cache:
	kmem_cache_destroy(conf->kdml_summary_node_cache);
err_destroy_node_cache:
	kmem_cache_destroy(conf->kdml_node_cache);
	return ret;
}

/* TODO: Check how we can test the exit function */
void kdbg_kdml_exit(void)
{
	struct kdml_conf *conf = &kdml_global_conf;

	/* remove the counter monitors for summary */
	kdml_ctr_unregister_meminfo_functions();
	/* reset the mode of KDML, if not already reset */
	kdml_set_current_mode(KDML_MODE_NONE);
	/* unregister probing of module */
	kdml_unregister_module_probe();
	/* buffer used by kdml for printing */
	kdml_cleanup_probe_memory();
	/* remove the caches */
	kmem_cache_destroy(conf->kdml_node_cache);
	kmem_cache_destroy(conf->kdml_summary_node_cache);
	kmem_cache_destroy(conf->kdml_module_cache);

	return;
}

