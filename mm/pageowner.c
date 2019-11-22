/*
 * pageowner - page-level allocations tracer.
 *
 * This funciton allows tracing most of page-level allocations performed in the
 * kernel. For each allocation a backtrace is saved and can be exported via
 * debugfs. Using a number of additional tools the output of pageowner can be
 * processed to show more useful data: groupped backtraces with overall memory
 * allocated this or that way, per-module memory consumption and much more.
 *
 * Initial version:   Dave Hansen     <dave.hansen@linux.intel.com> 2012.12.07
 * Rewritten version: Sergey Rogachev <s.rogachev@samsung.com>      2014.11.25
 *
 * Released under the terms of GNU General Public License Version 2.0
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/bootmem.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pageowner.h>
#include <linux/sched.h>

/*
 * Internal data representation:
 *
 * Every valid page in the system has its own complimentary pageowner.
 * structure. On flat-memory systems such structures are organized in arrays
 * allocated for each pglist_data structure, sparse memory systems have such
 * arrays for all memory sections. This design mimics the approach used in
 * implementation of memory controller.
 *
 * The structure pageowner contains information about one page-level allocation
 * of some certain page: its order, gfp_mask, current pid and a backtrace.
 *
 * Free pages are marked with (-1) order. For more information about operation
 * of allocation/deallocation routines look to commentary of the functions
 * pageowner_alloc() and pageowner_free().
 *
 * Locking is not used to keep internal representation consistent - all
 * data structures are preallocated on early initialization stage and never
 * freed. Internal representation is consistent at any time, but it can be read
 * by a user in some inconsistent state. It can be explained by time needed to
 * read all entries from debugfs virtual file: during the reading operation
 * internal state is changed many times and already read data can be outdated.
 * Anyway, pageowner allows to track allocations with a good level of
 * precision.
 */

#ifdef PAGEOWNER_INTERNAL_DEBUG
/*
 * Define the macro PAGEOWNER_INTERNAL_DEBUG manually if debug conunter is
 * necessary.
 */
#include <linux/atomic.h>
static atomic_t debug_page_counter = ATOMIC_INIT(0);

static inline void debug_page_counter_add(int n)
{
	atomic_add(n, &debug_page_counter);
}
static inline void debug_page_counter_sub(int n)
{
	atomic_sub(n, &debug_page_counter);
}
static inline void debug_page_counter_print(int is_first_time)
{
	if (is_first_time)
		pr_info("PAGEOWNER_INTERNAL_DEBUG: %d pages allocated\n",
				atomic_read(&debug_page_counter));
}
#else
static inline void debug_page_counter_add(int n) { }
static inline void debug_page_counter_sub(int n) { }
static inline void debug_page_counter_print(int is_first_time) { }
#endif /* PAGEOWNER_INTERNAL_DEBUG */

/**
 * struct pageowner - pageowner's internal page descriptor
 * @order: allocation order, (-1) if the page is free
 * @pid: current pid
 * @gfp_mask: allocation mask
 * @nr_entries: number of entries used in trace_entries array
 * @trace_entries: storage for backtrace
 */
struct pageowner {
	int           order;
	pid_t         pid;
	gfp_t         gfp_mask;
	int           nr_entries;
	unsigned long trace_entries[16];
};

/* Shows how much memory is occupied by pageowner itself. */
static unsigned long total_usage;

#if !defined(CONFIG_SPARSEMEM)
void __meminit pageowner_pgdat_init(struct pglist_data *pgdat)
{
	pgdat->node_page_owner = NULL;
}

static struct pageowner *pageowner_lookup(struct page *page)
{
	struct pageowner *base;
	unsigned long pfn = page_to_pfn(page);
	unsigned long offset;
	struct pglist_data *pgdat = NODE_DATA(page_to_nid(page));

	base = pgdat->node_page_owner;

#ifdef CONFIG_DEBUG_VM
	if (unlikely(!base)) {
		pr_debug("Unable to find page owner structure, "
				"base is NULL\n");
		return NULL;
	}
#endif

	offset = pfn - pgdat->node_start_pfn;

	return base + offset;
}

static int __init pageowner_alloc_node(int nid)
{
	struct pageowner *base;
	unsigned long table_size;
	unsigned long nr_pages;
	unsigned long i;

	nr_pages = NODE_DATA(nid)->node_spanned_pages;
	if (!nr_pages)
		return 0;

	table_size = sizeof(struct pageowner) * nr_pages;

	base = __alloc_bootmem_node_nopanic(NODE_DATA(nid),
			table_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	if (!base)
		return -ENOMEM;

	for (i = 0; i < nr_pages; i++) {
		memset(&base[i], 0, sizeof(struct pageowner));
		base[i].order = -1;
	}

	NODE_DATA(nid)->node_page_owner = base;
	total_usage += table_size;

	return 0;
}

void __init pageowner_init_flatmem(void)
{
	int nid, fail;

	for_each_online_node(nid)  {
		fail = pageowner_alloc_node(nid);
		if (fail)
			goto fail;
	}

	printk(KERN_INFO "allocated %ld bytes of page_owner\n", total_usage);
	return;
fail:
	printk(KERN_CRIT "allocation of page_owner failed.\n");
	panic("Out of memory");
}
#else /* !defined(SPARSEMEM) */
static struct pageowner *pageowner_lookup(struct page *page)
{
	struct pageowner *base;
	unsigned long pfn = page_to_pfn(page);
	unsigned long start_pfn;
	unsigned long offset;
	struct mem_section *section = __pfn_to_section(pfn);

	/*
	 * It is possible that the page's pfn is not aligned to a SECTION
	 * boundary. To calculate the first (0th) pfn of the section the mask
	 * PAGE_SECTION_MASK should be applied.
	 */
	start_pfn = pfn & PAGE_SECTION_MASK;

	base = section->page_owner;

#ifdef CONFIG_DEBUG_VM
	if (unlikely(!base))
		return NULL;
#endif

	offset = pfn - start_pfn;

	return base + offset;
}

static void *__meminit pageowner_alloc_section(size_t size, int nid)
{
	struct pageowner *base;
	int i;

	base = __alloc_bootmem_node_nopanic(NODE_DATA(nid),
			size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
	if (!base)
		return NULL;

	for (i = 0; i < PAGES_PER_SECTION; i++) {
		memset(&base[i], 0, sizeof(struct pageowner));
		base[i].order = -1;
	}

	return base;
}

static int __meminit pageowner_init_section(unsigned long pfn, int nid)
{
	struct mem_section *section;
	struct pageowner *base;
	unsigned long table_size;

	section = __pfn_to_section(pfn);

	if (section->page_owner)
		return 0;

	table_size = sizeof(struct pageowner) * PAGES_PER_SECTION;
	base = pageowner_alloc_section(table_size, nid);
	if (!base) {
		printk(KERN_ERR "page_owner allocation failure\n");
		return -ENOMEM;
	}

	section->page_owner = base;
	total_usage += table_size;
	return 0;
}

void __init pageowner_init_sparsemem(void)
{
	unsigned long pfn;
	int nid;

	for_each_node_state(nid, N_MEMORY) {
		unsigned long start_pfn, end_pfn;

		start_pfn = node_start_pfn(nid);
		end_pfn = node_end_pfn(nid);

		/*
		 * Almost the same loop there is in mm/page_cgroup.c:
		 * page_cgroup_init
		 */
		for (pfn = start_pfn; pfn < end_pfn;
				pfn = ALIGN(pfn + 1, PAGES_PER_SECTION)) {
			if (!pfn_valid(pfn))
				continue;
			if (pfn_to_nid(pfn) != nid)
				continue;
			if (pageowner_init_section(pfn, nid))
				goto oom;
		}
	}
	printk(KERN_INFO "allocated %ld bytes of page_owner\n", total_usage);
	return;
oom:
	printk(KERN_CRIT "allocation of page_owner failed\n");
	panic("Out of memory");
}
#endif /* defined(SPARSEMEM) */

/*
 * pageowner_read - debugfs interface to pageowner
 *
 * Usage:
 * dd if=/sys/kernel/debug/page_owner of=/path/to/some/file bs=1024 or
 * cat /sys/kernel/debug/page_owner > /path/to/some/file
 */
static ssize_t pageowner_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	unsigned long pfn;
	struct page *page;
	struct pageowner *owner;
	char *kbuf;
	int ret = 0;
	ssize_t num_written = 0;
	int blocktype = 0, pagetype = 0;
	struct stack_trace trace;

	debug_page_counter_print(!(*ppos));

	pfn = min_low_pfn + *ppos;

lookup:
	/* Find first valid pfn */
	for (; pfn < max_pfn; pfn++)
		if (pfn_valid(pfn))
			break;

	if (!pfn_valid(pfn))
		return 0;

	page = pfn_to_page(pfn);
	owner = pageowner_lookup(page);
	if (!owner) {
		pfn++;
		goto lookup;
	}

	if (owner->order < 0 || !owner->nr_entries) {
		pfn++;
		goto lookup;
	}

	if (owner->order >= 0 && PageBuddy(page)) {
		printk(KERN_WARNING
				"PageOwner info is inaccurate for PFN %lu\n",
				pfn);
		pfn++;
		goto lookup;
	}

	*ppos = (pfn - min_low_pfn) + 1;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = snprintf(kbuf, count, "Page allocated via order %d, mask 0x%x\n",
			owner->order, owner->gfp_mask);
	if (ret >= count) {
		ret = -ENOMEM;
		goto out;
	}

	/* Print information relevant to grouping pages by mobility */
	blocktype = get_pageblock_migratetype(page);
	pagetype  = allocflags_to_migratetype(owner->gfp_mask);
	ret += snprintf(kbuf+ret, count-ret,
			"PFN %lu Block %lu type %d %s "
			"Flags %s%s%s%s%s%s%s%s%s%s%s%s\n"
			"PID %d\n",
			pfn,
			pfn >> pageblock_order,
			blocktype,
			blocktype != pagetype ? "Fallback" : "        ",
			PageLocked(page)        ? "K" : " ",
			PageError(page)         ? "E" : " ",
			PageReferenced(page)    ? "R" : " ",
			PageUptodate(page)      ? "U" : " ",
			PageDirty(page)         ? "D" : " ",
			PageLRU(page)           ? "L" : " ",
			PageActive(page)        ? "A" : " ",
			PageSlab(page)          ? "S" : " ",
			PageWriteback(page)     ? "W" : " ",
			PageCompound(page)      ? "C" : " ",
			PageSwapCache(page)     ? "B" : " ",
			PageMappedToDisk(page)  ? "M" : " ",
			owner->pid);
	if (ret >= count) {
		ret = -ENOMEM;
		goto out;
	}

	num_written = ret;

	trace.nr_entries = owner->nr_entries;
	trace.entries = &owner->trace_entries[0];

	ret = snprint_stack_trace(kbuf + num_written, count - num_written,
			&trace, 0);
	if (ret >= count - num_written) {
		ret = -ENOMEM;
		goto out;
	}
	num_written += ret;

	ret = snprintf(kbuf + num_written, count - num_written, "\n");
	if (ret >= count - num_written) {
		ret = -ENOMEM;
		goto out;
	}
	num_written += ret;
	ret = num_written;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

out:
	kfree(kbuf);
	return ret;
}

/**
 * pageowner_alloc - mark the page as allocated, save the backtrace
 * @page: pointer to the corresponding page struct
 * @order: numerical order of the allocation
 * @gfp_mask: mask of the allocation
 *
 * Works fine for every page order, when the pages are allocated with non-zero
 * order a corresponding page_owner structure is marked with this order and
 * backtrace is saved, subpages included in this big non-zero-order page
 * allocation are not marked. So, when the user reads pageowner's data, only
 * heading pages' info is printed.
 */
void pageowner_alloc(struct page *page, unsigned int order, gfp_t gfp_mask)
{
	struct stack_trace trace;
	struct pageowner *owner;

	owner = pageowner_lookup(page);
	if (owner) {
		trace.nr_entries = 0;
		trace.max_entries = ARRAY_SIZE(owner->trace_entries);
		trace.entries = &owner->trace_entries[0];
		trace.skip = 3;
		save_stack_trace(&trace);

		owner->order = (int) order;
		owner->gfp_mask = gfp_mask;
		owner->pid = current->pid;
		owner->nr_entries = trace.nr_entries;
	}

	debug_page_counter_add(1 << order);
}

/**
 * pageowner_free - mark the page as free
 * @page: pointer to the corresponding page struct
 *
 * When some non-zero-order page is freed only heading page's page_owner
 * structure is marked as free (order == -1), other sub-pages are already freed
 * in pageowner, because on allocation only heading page's page_owner structure
 * is changed.
 */
void pageowner_free(struct page *page)
{
	struct pageowner *owner;
	owner = pageowner_lookup(page);
	if (owner) {
		debug_page_counter_sub(1 << owner->order);
		owner->order = -1;
	}
}

static const struct file_operations proc_page_owner_operations = {
	.read		= pageowner_read,
};

static int __init pageowner_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_file("page_owner", S_IRUSR, NULL,
			NULL, &proc_page_owner_operations);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	return 0;
}

module_init(pageowner_init)
