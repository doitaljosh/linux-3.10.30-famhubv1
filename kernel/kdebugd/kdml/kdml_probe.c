/*
 * kdml_probe.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <trace/events/kmem.h>
#include <trace/events/module.h>
#include "kdebugd.h"
#include "kdml/kdml_packet.h"
#include "kdml/kdml.h"
#include "kdml/kdml_internal.h"
#include "kdml/kdml_rb_tree.h"
#include <linux/module.h>
#include <linux/stacktrace.h>
#define MAX_TRACE		16	/* stack trace length */
#define KB 1024

#define MAX_CALLER_ENTRY_COUNT 1000
#define MAX_MODULE_COUNT 100

/* to protect summary database */
static DEFINE_SPINLOCK(kdml_summary_lock);
/* to protect detail database */
static DEFINE_SPINLOCK(kdml_detail_lock);
/* to protect module db (preemption disabled) */
static DEFINE_SPINLOCK(kdml_module_lock);
static struct kdml_summary_node *kdml_summary_copy_buffer;

/* allocator based summary:
 *   kmalloc
 *   kmem_cache_alloc
 *   alloc_pages
 */
static struct kdml_allocator_req_summary kdml_alloc_summary;
static void print_summary_line(struct kdml_summary_node *summary);

static void kdml_get_allocation_summary(struct kdml_allocator_req_summary *summary)
{
	unsigned long flags;
	spin_lock_irqsave(&kdml_detail_lock, flags);
	memcpy(summary, &kdml_alloc_summary, sizeof(*summary));
	spin_unlock_irqrestore(&kdml_detail_lock, flags);
}

static void kdml_reset_summary_stats(void)
{
	memset(&kdml_alloc_summary, 0, sizeof(kdml_alloc_summary));
}

/*
 * Save stack trace to the given array of MAX_TRACE size.
 */
static int __save_stack_trace(unsigned long *trace)
{
	struct stack_trace stack_trace;

	stack_trace.max_entries = MAX_TRACE;
	stack_trace.nr_entries = 0;
	stack_trace.entries = trace;
	/* skip till kdml functions:
	 * probe_xxx and handle_alloc
	 */
	stack_trace.skip = 4;
	save_stack_trace(&stack_trace);

	return (int)stack_trace.nr_entries;
}

/* core function for allocation:
 *   Adds the entries to database
 */
static void handle_alloc(struct kdml_alloc_info *entry, gfp_t gfp_flags)
{
	struct kdml_rb_type *rb_node;
	struct kdml_summary_type *node, *list_node = NULL;
	struct kdml_summary_node *summary = NULL;

	unsigned long flags;
	struct kdml_conf *conf = &kdml_global_conf;

	/* for summary mode */
	unsigned int allocator_type = entry->allocator_type;
	uint32_t call_site = entry->call_site;
	uint32_t bytes_req = entry->bytes_req;
	int max_entries_per_caller = conf->entries_per_caller;
	int kdml_restrict_entries = max_entries_per_caller > 0 ? 1: 0;

	if (call_site == kdml_backtrace_addr) {
		unsigned int i;
		unsigned int nr_entries;
		unsigned long trace[MAX_TRACE];	/* stack trace */
		nr_entries = (unsigned int)__save_stack_trace(trace);
		ddebug_print("---------------------\n");
		ddebug_print("comm \"%s\", pid %d\n", current->comm, current->pid);
		ddebug_print("backtrace:\n");
		for (i = 0; i < nr_entries; i++)
			ddebug_print(" %pS\n", (void *)trace[i]);
		ddebug_print("---------------------\n");
	}

	/*
	 * rb tree: based on the caller
	 * search and update
	 */
	spin_lock_irqsave(&kdml_summary_lock, flags);
	node = summary_rbtree_search(&conf->kdml_summary_tree, call_site);
	if (node) {
		list_node = node;
		summary = &list_node->summary;
		/* restrict entries per caller */
		if (kdml_restrict_entries &&
				(summary->alloc_count >= max_entries_per_caller)) {
			summary->skip_alloc++;
			spin_unlock_irqrestore(&kdml_summary_lock, flags);
			return;
		}
		/* update stats */
		summary->net_allocation += bytes_req;
		summary->alloc_count++;
		summary->alloc++;
	}
	spin_unlock_irqrestore(&kdml_summary_lock, flags);

	/* allocate the rb node */
	rb_node = kmem_cache_alloc_notrace(conf->kdml_node_cache, GFP_KDML_FLAGS);
	if (!rb_node) {
		ddebug_err("kmem_cache_alloc, gfp_flags %d.\n", gfp_flags);
		return;
	}

	/* insert in rbtree and update the statistics */
	memcpy(&rb_node->value, (char *)entry, sizeof(*entry));

	spin_lock_irqsave(&kdml_detail_lock, flags);

	/* insert in rbtree */
	(void)rbtree_insert(&conf->kdml_rbtree, rb_node);

	/* update allocator based summary */
	(kdml_alloc_summary.alloc_count[allocator_type])++;
	kdml_alloc_summary.alloc_req[allocator_type] += bytes_req;

	spin_unlock_irqrestore(&kdml_detail_lock, flags);

	ddebug_info("rbtree_insert, ptr %p.\n", entry->ptr);

	/* create new node, if not already */
	if (!summary) {
		list_node = kmem_cache_alloc_notrace(conf->kdml_summary_node_cache, GFP_KDML_FLAGS);
		if (!list_node) {
			ddebug_err("kmem_cache_alloc, gfp_flags %d.\n", gfp_flags);
			return;
		}
		memset(list_node, 0, sizeof(*list_node));
		summary = &list_node->summary;
		summary->call_site = call_site;
		summary->allocator_type = allocator_type;
		summary->net_allocation = 0;
		summary->alloc_count = 0;
		summary->alloc = 0;
		summary->dealloc = 0;

		/*
		 * search again if it was added to list in meantime
		 * this can happen as allocation or entry is done outside lock
		 *
		 * if found, delete the newly allocated entry and update old
		 */
		spin_lock_irqsave(&kdml_summary_lock, flags);
		node = summary_rbtree_search(&conf->kdml_summary_tree, call_site);
		if (!node) {
			summary_rbtree_insert(&conf->kdml_summary_tree, list_node);
		} else {
			kmem_cache_free_notrace(conf->kdml_summary_node_cache, list_node);
			summary = &node->summary;
		}
		/* update stats */
		summary->net_allocation += bytes_req;
		summary->alloc_count++;
		summary->alloc++;
		spin_unlock_irqrestore(&kdml_summary_lock, flags);
	}
}

static void handle_dealloc(const void *ptr)
{
	/* DB 1: per allocation request */
	struct kdml_alloc_info *entry;
	struct kdml_rb_type *rb_node;
	unsigned long flags;
	struct kdml_conf *conf = &kdml_global_conf;
	unsigned int alloc_type;
	/* DB 2: per caller */
	struct kdml_summary_type *node;
	struct kdml_summary_node *kdml_summary = NULL;
	unsigned long call_site;

	spin_lock_irqsave(&kdml_detail_lock, flags);

	/* search and delete the object in rbtree and update stats */
	rb_node = rbtree_search(&conf->kdml_rbtree, (key_type) ptr);
	if (!rb_node) {
		spin_unlock_irqrestore(&kdml_detail_lock, flags);
		return;
	}
	rbtree_delete(&conf->kdml_rbtree, rb_node);

	/* update summary */
	alloc_type = rb_node->value.allocator_type;
	(kdml_alloc_summary.alloc_count[alloc_type])--;
	kdml_alloc_summary.alloc_req[alloc_type] -= rb_node->value.bytes_req;

	spin_unlock_irqrestore(&kdml_detail_lock, flags);

	/* update stats */
	entry = &rb_node->value;

	/* Summary DB2 */
	call_site = entry->call_site;
	spin_lock_irqsave(&kdml_summary_lock, flags);
	node = summary_rbtree_search(&conf->kdml_summary_tree, call_site);
	if (node)
		kdml_summary = &node->summary;

	if (kdml_summary && kdml_summary->alloc_count) {
		kdml_summary->net_allocation -= entry->bytes_req;
		kdml_summary->alloc_count--;
		kdml_summary->dealloc++;
	}
	spin_unlock_irqrestore(&kdml_summary_lock, flags);

	kmem_cache_free_notrace(conf->kdml_node_cache, rb_node);
	ddebug_info("rbtree_delete, ptr %p.\n", ptr);
}

/*
 * probe_kmalloc
 * tracepoint handler kmalloc family.
 */
void
probe_kmalloc(void *ignore, unsigned long call_site, const void *ptr,
		size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags)
{
	struct timespec ts;
	struct kdml_alloc_info entry = {.ptr = (uint32_t)ptr,
					.call_site = (uint32_t)call_site,
					.bytes_req = (uint32_t)bytes_req,
					.allocator_type = (uint32_t)KDML_ALLOC_KMALLOC,
					.timestamp = (uint64_t)0
					};

	/* initialize other fields */
	do_posix_clock_monotonic_gettime(&ts);
	entry.timestamp = timespec_to_ns(&ts);

	ddebug_info("kmalloc, call_site %pS (%p), ptr %p, bytes_req %u, bytes_alloc %u, gfp_flags %x\n",
			(void *)call_site, (void *)call_site, ptr, bytes_req, bytes_alloc, gfp_flags);

	handle_alloc(&entry, gfp_flags);
}

/* not called */
void
probe_kmalloc_node(void *ignore, unsigned long call_site, const void *ptr,
		size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags, int node)
{
	struct timespec ts;
	struct kdml_alloc_info entry = {.ptr = (uint32_t) ptr,
					.call_site = (uint32_t) call_site,
					.bytes_req = (uint32_t) bytes_req,
					.allocator_type = (uint32_t)KDML_ALLOC_KMALLOC_NODE,
					.timestamp = (uint64_t)0
					};

	/* initialize other fields */
	do_posix_clock_monotonic_gettime(&ts);
	entry.timestamp = timespec_to_ns(&ts);

	ddebug_info("kmalloc, call_site %pS (%p), ptr %p, bytes_req %u, bytes_alloc %u, gfp_flags %x\n",
			(void *)call_site, (void *)call_site, ptr, bytes_req, bytes_alloc, gfp_flags);

	handle_alloc(&entry, gfp_flags);
}


/*
 * probe_kfree
 * tracepoint handler kfree family.
 */
void
probe_kfree(void *ignore, unsigned long call_site, const void *ptr)
{
	/* kfree(NULL) is allowed and called from kernel */
	if (!ptr)
		return;

	ddebug_info("kfree, call_site %pS (%lu), ptr %p\n", (void *)call_site, call_site, ptr);

	handle_dealloc(ptr);
}

void probe_mm_page_alloc(void *ignore, struct page *page, unsigned int order,
		gfp_t gfp_flags, unsigned long call_site)
{
	int pages = 1 << order;
	struct timespec ts;
	struct kdml_alloc_info entry;

	/* return if allocation belongs to slab
	 * Slabs pass this flag to alloc_page()
	 */
	/*
	 * Using this condition, we can avoid recursion:
	 *   this would happen in case:
	 * kmalloc()
	 *    --> probe_kmalloc()
	 *    or
	 * kmem_cache_alloc()
	 *    --> probe_kmem_cache_alloc()
	 *    or
	 * mm_page_alloc()
	 *   -->probe_mm_page_alloc()
	 *
	 *        --> kmem_cache_alloc_notrace()
	 *            --> alloc_page()
	 *                --> probe_mm_page_alloc()
	 */
	if (gfp_flags & __GFP_NOTRACK)
		return;

	entry.ptr = (uint32_t)page;
	entry.call_site = (uint32_t)call_site;
	entry.bytes_req = (uint32_t) pages * PAGE_SIZE;
	entry.allocator_type = (uint32_t)KDML_ALLOC_PAGE;

	/* initialize other fields */
	do_posix_clock_monotonic_gettime(&ts);
	entry.timestamp = timespec_to_ns(&ts);

	handle_alloc(&entry, gfp_flags);
}

void probe_mm_page_free(void *ignore, struct page *page, unsigned int order)
{
	/*
	 * These are not tracked as slabs pass __GFP_NOTRACK
	 * we check in trace_mm_page_alloc
	 */
	if (PageSlab(page)) {
		__ClearPageSlab(page);
		return;
	}

	handle_dealloc((void *)page);
}

void
probe_kmem_cache_alloc(void *ignore, unsigned long call_site, const void *ptr,
		 size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags)
{
	struct timespec ts;
	struct kdml_alloc_info entry = {.ptr = (uint32_t) ptr,
					.call_site = (uint32_t) call_site,
					.bytes_req = (uint32_t) bytes_req,
					.allocator_type = (uint32_t)KDML_ALLOC_CACHE,
					.timestamp = (uint64_t)0
					};
	/* initialize other fields */
	do_posix_clock_monotonic_gettime(&ts);
	entry.timestamp = timespec_to_ns(&ts);

	ddebug_info("kmem_cache_alloc, call_site %pS (%p), ptr %p, bytes_req %u, bytes_alloc %u, gfp_flags %x\n",
			(void *)call_site, (void *)call_site, ptr, bytes_req, bytes_alloc, gfp_flags);

	handle_alloc(&entry, gfp_flags);
}

void
probe_kmem_cache_alloc_node(void *ignore, unsigned long call_site,
		const void *ptr,
		size_t bytes_req,
		size_t bytes_alloc,
		gfp_t gfp_flags,
		int node)
{
	struct timespec ts;
	struct kdml_alloc_info entry = {.ptr = (uint32_t) ptr,
					.call_site = (uint32_t) call_site,
					.bytes_req = (uint32_t) bytes_req,
					.allocator_type = (uint32_t)KDML_ALLOC_CACHE_NODE,
					.timestamp = (uint64_t)0
					};
	/* initialize other fields */
	do_posix_clock_monotonic_gettime(&ts);
	entry.timestamp = timespec_to_ns(&ts);

	ddebug_info("kmem_cache_alloc_node, call_site %pS (%p), ptr %p, bytes_req %u, bytes_alloc %u, gfp_flags %x\n",
			(void *)call_site, (void *)call_site, ptr, bytes_req, bytes_alloc, gfp_flags);

	handle_alloc(&entry, gfp_flags);

}

void
probe_kmem_cache_free(void *ignore, unsigned long call_site, const void *ptr)
{
	/* kfree(NULL) is allowed */
	if (!ptr)
		return;

	ddebug_info("kmem_cache_free, call_site %pS (%lu), ptr %p\n", (void *)call_site, call_site, ptr);
	handle_dealloc(ptr);
}

void kdml_clear_allocations(void)
{
	struct kdml_conf *conf = &kdml_global_conf;
	struct rb_root *root;
	struct rb_node *node;
	unsigned long flags;

	/* clear the rb tree db */
	root = &(conf->kdml_rbtree);
	spin_lock_irqsave(&kdml_detail_lock, flags);
	node = rb_first(root);
	while (node) {
		struct kdml_rb_type *data = container_of(node, struct kdml_rb_type, rb_node);
		rb_erase(&data->rb_node, root);
		node = rb_next(node);
		/* delete current node */
		kmem_cache_free_notrace(conf->kdml_node_cache, data);
	}
	kdml_reset_summary_stats();
	spin_unlock_irqrestore(&kdml_detail_lock, flags);

	/* clear the rb tree db */
	root = &(kdml_global_conf.kdml_summary_tree);
	spin_lock_irqsave(&kdml_summary_lock, flags);
	node = rb_first(root);
	while (node) {
		struct kdml_summary_type *data = container_of(node, struct kdml_summary_type, node);
		rb_erase(&data->node, root);
		node = rb_next(node);
		/* delete current node */
		kmem_cache_free_notrace(conf->kdml_node_cache, data);
	}
	spin_unlock_irqrestore(&kdml_summary_lock, flags);

	return;
}

static char *str_kdml_alloc_type[KDML_ALLOC_MAX] = {
	"invalid",
	"kmalloc",
	"kmalloc_node",
	"kmem_cache_alloc",
	"kmem_cache_alloc_node",
	"alloc_page"
};

static const char *type_to_str(enum kdml_alloc_type type)
{
	if ((type < KDML_ALLOC_INVALID)
		|| (type >= KDML_ALLOC_MAX))
		type = KDML_ALLOC_INVALID;
	return str_kdml_alloc_type[type];
}

static void print_summary_header(void)
{
	ddebug_print("----------------------------------------------------------------------------\n");
	ddebug_print(" allocator_type     alloc_bytes   count (alloc/dealloc/not-tracked)   caller\n");
	ddebug_print("----------------------------------------------------------------------------\n");

}

static void print_summary_footer(void)
{
	ddebug_print("----------------------------------------------------------------------------\n");
}


/*
 * we can add a lock to avoid 2 callers getting the pointer,
 * but currently it serves the purpose as user would see either:
 *    - caller based summary or - module based summary at one time
 */
static int kdml_get_summary_snapshot(unsigned long *caller_list, int *count)
{
	/* can use__get_free_pages() and free_pages() too */
	struct kdml_conf *conf = &kdml_global_conf;
	struct rb_root *root = &conf->kdml_summary_tree;
	unsigned long flags;
	struct rb_node *node;
	int i;
	size_t caller_summary_mem = MAX_CALLER_ENTRY_COUNT * sizeof(struct kdml_summary_node);

	if (!kdml_summary_copy_buffer)
		kdml_summary_copy_buffer = kmalloc(caller_summary_mem, GFP_KERNEL | __GFP_NOTRACK);
	if (!kdml_summary_copy_buffer) {
		ddebug_err("Error in allocating snapshot memory...\n");
		return -1;
	}
	ddebug_info("snapshot memory: %p\n", kdml_summary_copy_buffer);
	spin_lock_irqsave(&kdml_summary_lock, flags);

	for (node = rb_first(root), i = 0; node; node = rb_next(node), i++) {
		struct kdml_summary_type *data = rb_entry(node, struct kdml_summary_type, node);
		struct kdml_summary_node *summary = &data->summary;
		if (i == MAX_CALLER_ENTRY_COUNT) {
			ddebug_err("Increase memory for snapshot..\n");
			break;
		}
		memcpy(kdml_summary_copy_buffer + i, summary, sizeof(struct kdml_summary_node));
		/* print_summary_line(summary); */
	}
	*count = i;
	*caller_list = (unsigned long)kdml_summary_copy_buffer;
	spin_unlock_irqrestore(&kdml_summary_lock, flags);
	ddebug_info("Summary Array: %p, no. of entries: %d\n",
			kdml_summary_copy_buffer, *count);
	return 0;
}

void kdml_cleanup_probe_memory(void)
{
	kfree(kdml_summary_copy_buffer);
}

static void kdml_snapshot_summary_and_print(void)
{
	unsigned long summary_list;
	struct kdml_summary_node *summary_arr;
	int summary_count = 0;
	int ret;
	int i;
	enum kdml_alloc_type type;
	struct alloc_type_summary alloc_summary[KDML_ALLOC_MAX] = {{0, 0, 0}, };

	ret = kdml_get_summary_snapshot(&summary_list, &summary_count);
	if (ret) {
		ddebug_err("Error in taking snapshot...\n");
		return;
	}
	if (!summary_count) {
		ddebug_print("No samples... Ensure KDML is running..\n");
		return;
	}
	summary_arr = (struct kdml_summary_node *)summary_list;
	ddebug_info("Summary Array: %p, No. of entries: %d\n",
			summary_arr, summary_count);

	print_summary_header();
	for (i = 0; i < summary_count; i++) {
		struct kdml_summary_node *summary = &summary_arr[i];
		print_summary_line(summary);
		alloc_summary[summary->allocator_type].unique_callers++;
		alloc_summary[summary->allocator_type].alloc_count += summary->alloc_count;
		alloc_summary[summary->allocator_type].alloc_size += summary->net_allocation;
	}
	print_summary_footer();

	ddebug_print("--------------------------------------------------------\n");
	ddebug_print("Allocator                Callers     Count    Size (KB)\n");
	for (type = KDML_ALLOC_KMALLOC; type < KDML_ALLOC_MAX; type++) {
		unsigned long alloc_size = alloc_summary[type].alloc_size;
		ddebug_print("%-22s  %8d  %8d\t%8lu\n",
				type_to_str(type),
				alloc_summary[type].unique_callers,
				alloc_summary[type].alloc_count,
				alloc_size/KB);
	}
	ddebug_print("--------------------------------------------------------\n");
}

static void print_allocator_summary(void)
{
	enum kdml_alloc_type type;
	struct kdml_allocator_req_summary summary;

	kdml_get_allocation_summary(&summary);
	ddebug_print("Allocator Summary....\n");
	ddebug_print("------------------------------------------\n");
	ddebug_print("Allocator                Count     Size(KB)\n");
	ddebug_print("------------------------------------------\n");
	for (type = KDML_ALLOC_KMALLOC; type < KDML_ALLOC_MAX; type++) {
		ddebug_print("%-22s  %8d  %8ld\n", type_to_str(type),
				summary.alloc_count[type], summary.alloc_req[type]/KB);
	}
	ddebug_print("------------------------------------------\n");
}

void kdml_print_caller_summary_info(void)
{
	print_allocator_summary();
	kdml_snapshot_summary_and_print();
}

#ifdef KDML_DEBUG
/* As there are too many entries, we avoid printing it...
 * further, summary is sufficient
 */
static void kdml_print_detail_info(void)
{
	struct rb_root *root;
	struct kdml_conf *conf = &kdml_global_conf;
	struct timespec ts;
	unsigned long flags;

	struct rb_node *node;

	root = &conf->kdml_rbtree;

	ddebug_print("Allocator Pointer  Caller Size Timestamp\n");
	spin_lock_irqsave(&kdml_detail_lock, flags);
	for (node = rb_first(root); node; node = rb_next(node)) {
		struct kdml_rb_type *data = rb_entry(node, struct kdml_rb_type, rb_node);
		const char *type = type_to_str(data->value.allocator_type);
		ts = ns_to_timespec(data->value.timestamp);

		ddebug_print("%s %p %d %pS %ld.%09ld\n",
				type,
				(void *)(data->value.ptr),
				data->value.bytes_req,
				(void *)(data->value.call_site),
				ts.tv_sec, ts.tv_nsec);
	}
	spin_unlock_irqrestore(&kdml_detail_lock, flags);

	return;
}
#endif


static void probe_module_load(void *ignore, struct module *mod)
{
	struct kdml_module_info *mod_info;
	struct kdml_conf *conf = &kdml_global_conf;
	struct kdml_module_type *node = NULL;

	node = kmem_cache_alloc_notrace(conf->kdml_module_cache, GFP_KDML_FLAGS);
	if (!node) {
		ddebug_err("kmem_cache_alloc failed for module\n");
		return;
	}
	mod_info = &(node->mod_info);
	mod_info->mod_ptr = mod;
	mod_info->module_init = (unsigned long)mod->module_init;
	mod_info->module_core = (unsigned long)mod->module_core;
	mod_info->init_text_size = mod->init_text_size;
	mod_info->core_text_size = mod->core_text_size;
	strncpy(mod_info->name, mod->name, MAX_MODULE_NAME_LEN - 1);
	mod_info->name[MAX_MODULE_NAME_LEN - 1] = '\0';

	spin_lock(&kdml_module_lock);
	/* similar to cat /proc/modules */
	list_add(&node->node, &conf->kdml_module_list);
	spin_unlock(&kdml_module_lock);
}

static void probe_module_free(void *ignore, struct module *mod)
{
	struct list_head *head;
	struct kdml_module_type *entry, *tmp;
	struct kdml_conf *conf = &kdml_global_conf;
	int found = 0;

	head = &(conf->kdml_module_list);

	spin_lock(&kdml_module_lock);
	/* search for the module entry and remove */
	list_for_each_entry_safe(entry, tmp, head, node) {
		struct kdml_module_info *mod_info = &(entry->mod_info);
		if (mod_info->mod_ptr == mod) {
			list_del(&entry->node);
			found = 1;
			break;
		}
	}
	spin_unlock(&kdml_module_lock);
	if (found) {
		ddebug_info("freeing module entry...\n");
		kmem_cache_free_notrace(conf->kdml_module_cache, entry);
	} else {
		ddebug_err("Module entry not found in list..\n");
	}

	return;
}

static int get_module_list(struct kdml_module_info *module_arr, const int count, int *result)
{
	struct kdml_conf *conf = &kdml_global_conf;
	struct kdml_module_type *node;
	int i = 0;

	spin_lock(&kdml_module_lock);
	list_for_each_entry(node, &conf->kdml_module_list, node) {
		struct kdml_module_info *mod_info = &node->mod_info;
		if (i == count)
			break;
		memcpy(&module_arr[i], mod_info, sizeof(struct kdml_module_info));
		i++;
	}
	spin_unlock(&kdml_module_lock);
	*result = i;
	return 0;
}

static inline int within(unsigned long addr, unsigned long start, unsigned long size)
{
	return (addr >= start && addr < start + size);
}

static void print_summary_line(struct kdml_summary_node *summary)
{
	uint32_t alloc_size = summary->net_allocation;
	ddebug_print("%-16s  %10u  %5d (%6d/%6d/%6d)  %pS\n",
			type_to_str(summary->allocator_type),
			alloc_size,
			summary->alloc_count,
			summary->alloc,
			summary->dealloc,
			summary->skip_alloc,
			(void *)summary->call_site
		    );
}

int kdml_print_module_summary_info(void)
{
	struct kdml_module_info *module_arr;
	struct kdml_summary_node *summary_arr = NULL;
	unsigned long summary_list = 0;
	int module_count = MAX_MODULE_COUNT, summary_count = 0;
	int result;
	int ret;
	int i, j, k;
	int option;
	struct kdml_module_info *module;
	int caller_idx;
	int entry_found = 0;
	size_t module_mem = (size_t)module_count * sizeof(struct kdml_module_info);

	module_arr = kmalloc(module_mem, GFP_KERNEL | __GFP_NOTRACK);
	if (!module_arr) {
		ddebug_err("Error in allocating memory..\n");
		return -1;
	}

	/* get list of modules */
	ret = get_module_list(module_arr, module_count, &result);
	if (ret < 0) {
		ddebug_err("Error in getting module list..\n");
		goto free_module;
	} else if (!result) {
		ddebug_print("KDML: No module found..\n");
		goto free_module;
	} else if (module_count == result) {
		ddebug_err("Too many modules.. only tracking %d\n", module_count);
	} else {
		/* Normal case: MAX_MODULE_COUNT should be sufficient */
		module_count = result;
	}

	/* sort the list */
	for (i = 0; i < module_count; i++) {
		k = i;
		for (j = k + 1; j < module_count; j++) {
			struct kdml_module_info temp;
			if (module_arr[k].module_core > module_arr[j].module_core) {
				memcpy(&temp, &module_arr[j], sizeof(struct kdml_module_info));
				memcpy(&module_arr[j], &module_arr[k], sizeof(struct kdml_module_info));
				memcpy(&module_arr[k], &temp, sizeof(struct kdml_module_info));
			}
		}
	}


	/* TODO: Add one entry for kernel too... */
	/* get caller summary */
	ret = kdml_get_summary_snapshot(&summary_list, &summary_count);
	if (ret) {
		ddebug_err("Error in taking snapshot...\n");
		goto free_module;
	}
	if (!summary_count) {
		ddebug_print("No samples... Ensure KDML is running..\n");
		goto free_module;
	}

	summary_arr = (struct kdml_summary_node *)summary_list;
	ddebug_info("Summary Array: %p, No. of entries: %d\n",
			summary_arr, summary_count);

	caller_idx = 0;
	for (i = 0; i < module_count; i++) {
		ddebug_info("%d: Module: %s", i, module_arr[i].name);
		ddebug_info(" core: %lx-%lx, init: %lx-%lx\n",
				module_arr[i].module_core,
				module_arr[i].module_core + module_arr[i].core_text_size,
				module_arr[i].module_init,
				module_arr[i].module_init + module_arr[i].init_text_size);
		module_arr[i].memory_used = 0;
		for (caller_idx = 0; caller_idx < summary_count; caller_idx++) {
			unsigned long addr = summary_arr[caller_idx].call_site;
			/* skip kernel address */
			if (addr >= PAGE_OFFSET) {
				ddebug_info("Skip: entry for kernel address: %pS (0x%x > 0x%x)\n",
						addr, addr, PAGE_OFFSET);
				break;
			}
			if (within(addr, module_arr[i].module_core, module_arr[i].core_text_size)
					|| within(addr, module_arr[i].module_init, module_arr[i].init_text_size)) {
				module_arr[i].memory_used += summary_arr[caller_idx].net_allocation;
				/*
				 * this is based on observation that for a module:
				 * range lies b/w core and init
				 *    core: be400000-be400094, init: be402000-be402000
				 */
			} else if (addr > (module_arr[i].module_init + module_arr[i].init_text_size)) {
				ddebug_info("break at: %pS\n", addr);
				break;
			}
		}
	}

	ddebug_print("--------------------------------------\n");
	ddebug_print(" Index  Module Name            Memory\n");
	ddebug_print("--------------------------------------\n");
	for (i = 0; i < module_count; i++) {
		ddebug_print("%2d   %-20s  %10d\n",
				i + 1,
				module_arr[i].name,
				module_arr[i].memory_used);

	}
	/* check the call_site in summary list which lies in the module */
	ddebug_print("--------------------------------------\n");
	ddebug_print("Enter index (max %d) --> ", module_count);
	option = debugd_get_event_as_numeric(NULL, NULL);
	ddebug_print("\n");
	option--;
	if ((option < 0) || (option >= module_count)) {
		ddebug_err("Invalid Index.\n");
		ret = -1;
		goto free_module;
	}

	module = &module_arr[option];
	/* as this is filled by us, should be valid */
	WARN_ON(!module);
	if (!module) {
		ddebug_err("couldn't find entry..");
		ret = -1;
		goto free_module;
	}

	ddebug_print("%s [core: %x-%x, init: %x-%x]\n",
			module->name,
			module->module_core,
			module->module_core + module->core_text_size,
			module->module_init,
			module->module_init + module->init_text_size);

	for (caller_idx = 0; caller_idx < summary_count; caller_idx++) {
		struct kdml_summary_node *summary = &summary_arr[caller_idx];
		unsigned long addr = summary->call_site;

		/* find a range for module and then we don't need to check each entry.. */
		if (!within(addr, module->module_core, module->core_text_size)
				&& !within(addr, module->module_init, module->init_text_size)) {
			/* address doesn't lie in module range */
			continue;
		}

		/* for first time, print the header and then print info */
		if (!entry_found) {
			entry_found = 1;
			print_summary_header();
		}
		print_summary_line(summary);
	}

	if (entry_found)
		print_summary_footer();
	else
		ddebug_print("No entry found for the module..\n");

free_module:
	kfree(module_arr);
	return ret;
}

int kdml_register_module_probe(void)
{
	int ret;
	ret = register_trace_module_load(probe_module_load, NULL);
	if (ret < 0) {
		ddebug_err("Error in registering module trace..\n");
		return -1;
	}

	ret = register_trace_module_free(probe_module_free, NULL);
	if (ret < 0) {
		ddebug_err("Error in registering module trace..\n");
		unregister_trace_module_load(probe_module_load, NULL);
		return -1;
	}

	return 0;
}

void kdml_unregister_module_probe(void)
{
	unregister_trace_module_load(probe_module_load, NULL);
	unregister_trace_module_free(probe_module_free, NULL);
	tracepoint_synchronize_unregister();
}

