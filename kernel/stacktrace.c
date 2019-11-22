/*
 * kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/stacktrace.h>

void seq_print_stack_trace(struct seq_file *m, struct stack_trace *trace,
		int spaces)
{
	int i;

	if (WARN_ON(!trace->entries))
		return;

	for (i = 0; i < trace->nr_entries; i++) {
		unsigned long ip = trace->entries[i];
		seq_printf(m, "%*c[<%p>] %pS\n", 1 + spaces, ' ',
				(void *) ip, (void *) ip);
	}
}
EXPORT_SYMBOL_GPL(seq_print_stack_trace);

int snprint_stack_trace(char *buf, int buf_len, struct stack_trace *trace,
			int spaces)
{
	int ret = 0;
	int i;

	if (WARN_ON(!trace->entries))
		return 0;

	for (i = 0; i < trace->nr_entries; i++) {
		unsigned long ip = trace->entries[i];
		int printed = snprintf(buf, buf_len, "%*c[<%p>] %pS\n",
				1 + spaces, ' ',
				(void *) ip, (void *) ip);
		/* snprintf() return the number of bytes that would have been
		 * written if n had been sufficiently large, or a negative
		 * value if an encoding error occurred (ISO C99) */
		if (unlikely(printed < 0))
			return 0;
		if (unlikely(printed >= buf_len))
			return 0;
		buf_len -= printed;
		ret += printed;
		buf += printed;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snprint_stack_trace);

void print_stack_trace(struct stack_trace *trace, int spaces)
{
	int i;

	if (WARN_ON(!trace->entries))
		return;

	for (i = 0; i < trace->nr_entries; i++) {
		printk("%*c", 1 + spaces, ' ');
		print_ip_sym(trace->entries[i]);
	}
}
EXPORT_SYMBOL_GPL(print_stack_trace);

/*
 * Architectures that do not implement save_stack_trace_tsk or
 * save_stack_trace_regs get this weak alias and a once-per-bootup warning
 * (whenever this facility is utilized - for example by procfs):
 */
__weak void
save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	WARN_ONCE(1, KERN_INFO "save_stack_trace_tsk() not implemented yet.\n");
}

__weak void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	WARN_ONCE(1, KERN_INFO "save_stack_trace_regs() not implemented yet.\n");
}
