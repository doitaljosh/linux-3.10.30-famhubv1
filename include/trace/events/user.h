#undef TRACE_SYSTEM
#define TRACE_SYSTEM user

#if !defined(_TRACE_USER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_USER_H_

#include <linux/tracepoint.h>

TRACE_EVENT(user,

	TP_PROTO(const char *message),

	TP_ARGS(message),

	TP_STRUCT__entry(
		__string(	message, message);
	),

	TP_fast_assign(
		__assign_str(message, message);
	),

	TP_printk("%s", __get_str(message))
);

#endif /* _TRACE_USER_H_ */

/* This part must be outside protection */
#include <trace/define_trace.h>
