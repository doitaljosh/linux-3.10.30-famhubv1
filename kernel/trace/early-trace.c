/*
 *  early-trace.c
 *
 *  Copyright (C) 2014  Roman Pen
 *
 *  Naive and simple but still working implementation of the early tracing
 *  which accepts messages from the beginning of the execution of the linux
 *  kernel (start_kernel routine) and then dumps everything from a lockless
 *  buffer to the original ftrace ring buffer.
 */

#include <linux/mm.h>
#include <linux/trace_clock.h>
#include <linux/rwlock.h>
#include <linux/atomic.h>

#define CREATE_TRACE_POINTS
#include <trace/events/early-trace.h>

/* Actually without this feature early tracing does not mean much,
 * because we can't inject our clock and all timestamps will be
 * bogus */
#ifdef CONFIG_SDP_HW_CLOCK_TRACE
#include <mach/sdp_hwclock.h>
#endif

struct early_event {
	u64      ns;
	char     str[];
};

struct early_buffer {
	atomic_t size;
	atomic_t real_size;
	/* see EV_SZ comments about alignment */
	char     buffer[1024] __aligned(sizeof(struct early_event));
};

/* This structure is declared as '__initdata' and it will be
 * returned to buddy on 'free_initmem' call, so be very attentive
 * and think twice before adding any members to it. */
struct early_buffer __initdata early_buffer = {
	.size      = ATOMIC_INIT(0),
	.real_size = ATOMIC_INIT(0),
};

static rwlock_t dump_lock = __RW_LOCK_UNLOCKED(dump_lock);
static bool     early_disabled;


/* Yeah, I know that in MMU alignment is turned off, but still I had the
 * 'data alignment fault' in case of access to this buffer using 'strd'
 * instruction. So now everything is aligned on header */
#define EV_SZ(len) (round_up(len + sizeof(struct early_event),	\
			     sizeof(struct early_event)))

/*
 * Lockless implementation of saving new early event to the buffer
 */
static struct early_event *__early_event_create(u64 ns, const char *str)
{
	unsigned int len;
	unsigned int buf_sz;
	struct early_event *ev;

	len = strlen(str) + 1;
	buf_sz = atomic_read(&early_buffer.size);

	/* First check */
	if (buf_sz >= sizeof(early_buffer.buffer) ||
	    sizeof(early_buffer.buffer) - buf_sz < EV_SZ(len))
		return NULL;

	buf_sz = atomic_add_return(EV_SZ(len), &early_buffer.size);
	/* Second check, we lose or win */
	if (buf_sz > sizeof(early_buffer.buffer))
		return NULL;

	/* Advance real size */
	atomic_add(EV_SZ(len), &early_buffer.real_size);

	ev = (void *)early_buffer.buffer + buf_sz - EV_SZ(len);
	ev->ns = ns;
	strcpy(ev->str, str);

	return ev;
}

void trace_early_message(const char *str)
{
	/* Here we protect only from dump, which happens once.
	 * We do not attempt to protect from writes from different
	 * CPUs, we have lockless early buffer. */
	read_lock(&dump_lock);

	/* Early buffer is dead, use original ftrace */
	if (early_disabled)
		trace_early_event(str);
	else {
		u64 ns;

#ifdef CONFIG_SDP_HW_CLOCK_TRACE
		ns = hwclock_ns((uint32_t *)hwclock_get_va());
#else
		ns = trace_clock();
#endif
		__early_event_create(ns, str);
	}

	read_unlock(&dump_lock);
}
EXPORT_SYMBOL(trace_early_message);

void dump_early_events(void)
{
	unsigned buff_sz, ev_iter = 0;

	/* We are doing dump */
	write_lock(&dump_lock);

	early_disabled = true;

	buff_sz = atomic_read(&early_buffer.real_size);

	/* Full fill the ftrace buffer from the early buffer */
	while (ev_iter < buff_sz) {
		struct early_event *ev;

		ev = (void *)early_buffer.buffer + ev_iter;
		ev_iter += EV_SZ(strlen(ev->str) + 1);

#ifdef CONFIG_SDP_HW_CLOCK_TRACE
		/* Hack the clock, trace event will get exactly this value */
		__hwclock_set(ev->ns);
#endif

		trace_early_event(ev->str);

#ifdef CONFIG_SDP_HW_CLOCK_TRACE
		/* Reset hack for this event */
		__hwclock_reset();
#endif
	}

	write_unlock(&dump_lock);
}
