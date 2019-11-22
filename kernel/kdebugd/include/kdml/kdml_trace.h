/*
 * kdml_trace.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#if !defined(_KDML_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _KDML_TRACE_H
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kdml
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <trace/events/gfpflags.h>
/* modify tracepoint to use call_site */
TRACE_EVENT(mm_page_alloc_kdml,

	TP_PROTO(struct page *page, unsigned int order,
			gfp_t gfp_flags, unsigned long call_site),

	TP_ARGS(page, order, gfp_flags, call_site),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	unsigned int,	order		)
		__field(	gfp_t,		gfp_flags	)
		__field(	unsigned long,	call_site	)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
		__entry->gfp_flags	= gfp_flags;
		__entry->call_site	= call_site;
	),

	TP_printk("page=%p pfn=%lu order=%d call_site=%lx gfp_flags=%s",
		__entry->page,
		__entry->page ? page_to_pfn(__entry->page) : 0,
		__entry->order,
		__entry->call_site,
		show_gfp_flags(__entry->gfp_flags))
);
#endif /* _KDML_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
/* path till include is already exported */
#define TRACE_INCLUDE_PATH kdml
#define TRACE_INCLUDE_FILE kdml_trace
#include <trace/define_trace.h>
