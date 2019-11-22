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

#ifndef _PAGEOWNER_H_
#define _PAGEOWNER_H_

#if defined(CONFIG_PAGE_OWNER) && !defined(CONFIG_SPARSEMEM)
void pageowner_alloc(struct page *page, unsigned int order, gfp_t gfp_mask);
void pageowner_free(struct page *page);
void __meminit pageowner_pgdat_init(struct pglist_data *pgdat);
void __init pageowner_init_flatmem(void);

static inline void pageowner_init_sparsemem(void) {}
#elif defined(CONFIG_PAGE_OWNER) && defined(CONFIG_SPARSEMEM)
void pageowner_alloc(struct page *page, unsigned int order, gfp_t gfp_mask);
void pageowner_free(struct page *page);
void __init pageowner_init_sparsemem(void);

static inline void pageowner_pgdat_init(struct pglist_data *pgdat) {}
static inline void pageowner_init_flatmem(void) {}
#else
static inline void pageowner_pgdat_init(struct pglist_data *pgdat) {}
static inline void pageowner_init_flatmem(void) {}
static inline void pageowner_init_sparsemem(void) {}
static inline void pageowner_alloc(struct page *page, unsigned int order, gfp_t gfp_mask) {}
static inline void pageowner_free(struct page *page) {}
#endif

#endif /* _PAGEOWNER_H_ */
