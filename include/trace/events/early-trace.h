#undef TRACE_SYSTEM
#define TRACE_SYSTEM early-trace

#if !defined(_EARLY_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _EARLY_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(early_event,

	TP_PROTO(const char *str),

	TP_ARGS(str),

	TP_STRUCT__entry(
		__string(	str,	str	)
	),

	TP_fast_assign(
		__assign_str(str, str);
	),

	TP_printk("%s", __get_str(str))
);

#endif /* _EARLY_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#include <trace/define_trace.h>
