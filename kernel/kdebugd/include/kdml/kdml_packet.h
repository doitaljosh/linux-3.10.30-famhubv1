/*
 * kdml_packet.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef __KDML_PACKET_H__
#define __KDML_PACKET_H__
/* allocator type */
enum kdml_alloc_type {
	KDML_ALLOC_INVALID = 0,
	KDML_ALLOC_KMALLOC = 1,
	KDML_ALLOC_KMALLOC_NODE,
	KDML_ALLOC_CACHE,
	KDML_ALLOC_CACHE_NODE,
	KDML_ALLOC_PAGE,
	KDML_ALLOC_MAX
};

struct alloc_type_summary {
	int unique_callers;
	int alloc_count;
	unsigned long alloc_size;
};

struct kdml_allocator_req_summary {
	int alloc_count[KDML_ALLOC_MAX];
	unsigned long alloc_req[KDML_ALLOC_MAX];
};

/* Detailed structure for storing/sharing allocation detail */
struct kdml_alloc_info {
	/* caller information: CONFIG_TRACING */
	uint32_t ptr; /* allocated memory */
	uint32_t call_site; /* caller information */
	uint32_t bytes_req;
	uint32_t allocator_type; /* allocator type - kmalloc/kmem_cache_alloc */
	int64_t timestamp; /* Allocation request time */
};

struct kdml_summary_node {
	uint32_t call_site; /* unique caller */
	uint32_t net_allocation; /* total allocation in bytes */
	uint32_t allocator_type; /* allocator type - kmalloc/kmem_cache... */
	int32_t alloc_count; /* can be calculated by alloc - dealloc */
	int32_t alloc; /* number of allocations */
	int32_t dealloc; /* number of deallocations */
	int32_t skip_alloc; /* number of allocations skipped */
};

struct kdml_summary_type {
	struct rb_node node;
	struct kdml_summary_node summary;
};

#define MAX_CACHE_NAME_LEN 32
struct kdml_slab_info_line {
	/* refer struct slabinfo in mm/slab.h */
	uint32_t active_objs;
	uint32_t num_objs;
	uint32_t obj_size; /* from struct kmem_cache */
	uint32_t objs_per_slab;
	/* pagesperslab = 1 << cache_order;
	 * cache_size = num_slabs * pagesperslab */
	uint32_t cache_order;
	uint32_t num_slabs;
	char name[MAX_CACHE_NAME_LEN]; /* name of cache */
};

#define MAX_MODULE_NAME_LEN 60 /* taken from MODULE_NAME_LEN - module.h */
struct kdml_module_info {
	struct module *mod_ptr; /* used internally, not valid for tvis */
	uint32_t module_init;
	uint32_t module_core;
	uint32_t init_text_size;
	uint32_t core_text_size;
	uint32_t memory_used; /* used internally, not valid for tvis */
	char name[MAX_MODULE_NAME_LEN];
};

struct kdml_module_type {
	struct list_head node;
	struct kdml_module_info mod_info;
};

#endif /* __KDML_PACKET_H__  */
