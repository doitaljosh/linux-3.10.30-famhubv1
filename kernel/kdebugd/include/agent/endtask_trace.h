#if !defined(_ENDTASK_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _ENDTASK_TRACE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM task
#include <linux/tracepoint.h>

TRACE_EVENT(task_endtask,

	TP_PROTO(struct task_struct *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(pid_t,  pid)
		__array(char,   comm, TASK_COMM_LEN)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
	),

	TP_printk("pid=%d comm=%s",
		__entry->pid, __entry->comm)
);


#endif /* _ENDTASK_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../include/agent
#define TRACE_INCLUDE_FILE endtask_trace
#include <trace/define_trace.h>
