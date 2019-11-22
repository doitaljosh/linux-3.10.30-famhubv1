#ifndef _LINUX_IRQDESC_H
#define _LINUX_IRQDESC_H

/*
 * Core internal functions to deal with irq descriptors
 *
 * This include will move to kernel/irq once we cleaned up the tree.
 * For now it's included from <linux/irq.h>
 */

struct irq_affinity_notify;
struct proc_dir_entry;
struct module;
struct irq_desc;

/**
 * struct irq_desc - interrupt descriptor
 * @irq_data:		per irq and chip data passed down to chip functions
 * @kstat_irqs:		irq stats per cpu
 * @handle_irq:		highlevel irq-events handler
 * @preflow_handler:	handler called before the flow handler (currently used by sparc)
 * @action:		the irq action chain
 * @status:		status information
 * @core_internal_state__do_not_mess_with_it: core internal status information
 * @depth:		disable-depth, for nested irq_disable() calls
 * @wake_depth:		enable depth, for multiple irq_set_irq_wake() callers
 * @irq_count:		stats field to detect stalled irqs
 * @last_unhandled:	aging timer for unhandled count
 * @irqs_unhandled:	stats field for spurious unhandled interrupts
 * @threads_handled:	stats field for deferred spurious detection of threaded handlers
 * @threads_handled_last: comparator field for deferred spurious detection of theraded handlers
 * @lock:		locking for SMP
 * @affinity_hint:	hint to user space for preferred irq affinity
 * @affinity_notify:	context for notification of affinity changes
 * @pending_mask:	pending rebalanced interrupts
 * @threads_oneshot:	bitfield to handle shared oneshot threads
 * @threads_active:	number of irqaction threads currently running
 * @wait_for_threads:	wait queue for sync_irq to wait for threaded handlers
 * @dir:		/proc/irq/ procfs entry
 * @name:		flow handler name for /proc/interrupts output
 */
struct irq_desc {
	struct irq_data		irq_data;
	unsigned int __percpu	*kstat_irqs;
	irq_flow_handler_t	handle_irq;
#ifdef CONFIG_IRQ_PREFLOW_FASTEOI
	irq_preflow_handler_t	preflow_handler;
#endif
	struct irqaction	*action;	/* IRQ action list */
	unsigned int		status_use_accessors;
	unsigned int		core_internal_state__do_not_mess_with_it;
	unsigned int		depth;		/* nested irq disables */
	unsigned int		wake_depth;	/* nested wake enables */
	unsigned int		irq_count;	/* For detecting broken IRQs */
	unsigned long		last_unhandled;	/* Aging timer for unhandled count */
	unsigned int		irqs_unhandled;
	atomic_t		threads_handled;
	int			threads_handled_last;
#ifdef CONFIG_IRQ_TIME
        unsigned int        runtime;
#endif
	raw_spinlock_t		lock;
	struct cpumask		*percpu_enabled;
#ifdef CONFIG_SMP
	const struct cpumask	*affinity_hint;
	struct irq_affinity_notify *affinity_notify;
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_var_t		pending_mask;
#endif
#endif
	unsigned long		threads_oneshot;
	atomic_t		threads_active;
	wait_queue_head_t       wait_for_threads;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*dir;
#endif
	int			parent_irq;
	struct module		*owner;
	const char		*name;
} ____cacheline_internodealigned_in_smp;

#ifndef CONFIG_SPARSE_IRQ
extern struct irq_desc irq_desc[NR_IRQS];
#endif

#ifdef CONFIG_GENERIC_HARDIRQS

static inline struct irq_data *irq_desc_get_irq_data(struct irq_desc *desc)
{
	return &desc->irq_data;
}

static inline struct irq_chip *irq_desc_get_chip(struct irq_desc *desc)
{
	return desc->irq_data.chip;
}

static inline void *irq_desc_get_chip_data(struct irq_desc *desc)
{
	return desc->irq_data.chip_data;
}

static inline void *irq_desc_get_handler_data(struct irq_desc *desc)
{
	return desc->irq_data.handler_data;
}

static inline struct msi_desc *irq_desc_get_msi_desc(struct irq_desc *desc)
{
	return desc->irq_data.msi_desc;
}


#ifdef CONFIG_UNHANDLED_IRQ_TRACE_DEBUGGING
extern void vd_irq_set(int irq, struct irq_desc *desc);
extern void vd_irq_unset(int irq, struct irq_desc *desc);
extern void show_irq(void);
extern int check_timer_irq(struct irq_desc *desc);
#endif

#ifdef CONFIG_IRQ_TIME
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/time.h>
extern void do_gettimeofday(struct timeval *tv);
struct irq_desc_debug{
        long            last_time;
        long            last_time_kth;
        long            last_time_tint;
        const char      *name;
        unsigned int    irq;
        unsigned int    irq_run;
};
extern struct irq_desc_debug irq_desc_last[NR_CPUS];
#endif

/*
 * Architectures call this to let the generic IRQ layer
 * handle an interrupt. If the descriptor is attached to an
 * irqchip-style controller then we call the ->handle_irq() handler,
 * and it calls __do_IRQ() if it's attached to an irqtype-style controller.
 */
static inline void generic_handle_irq_desc(unsigned int irq, struct irq_desc *desc)
{
#ifdef CONFIG_IRQ_TIME
        struct timeval before;
        struct timeval after;
        long time_value;
        unsigned int cpu = smp_processor_id();
        struct irqaction *action;
#endif

#ifdef CONFIG_UNHANDLED_IRQ_TRACE_DEBUGGING
        vd_irq_set(irq, desc);
#endif

#ifdef CONFIG_IRQ_TIME
        do_gettimeofday(&before);
	irq_desc_last[cpu].irq_run=irq;
#endif

	desc->handle_irq(irq, desc);

#ifdef CONFIG_IRQ_TIME
        do_gettimeofday(&after);

        // additional info 
        time_value = ((after.tv_sec  - before.tv_sec)*1000000) + (after.tv_usec  - before.tv_usec);
        if(desc->runtime < time_value)
        {
                desc->runtime = time_value;
        }
        irq_desc_last[cpu].last_time=(before.tv_sec*1000000) + before.tv_usec;

        action=desc->action;

	irq_desc_last[cpu].irq_run=0;
        if(action)
        {
                if(check_timer_irq(desc))
                        irq_desc_last[cpu].last_time_tint=(before.tv_sec*1000000) + before.tv_usec;
                irq_desc_last[cpu].name = action->name;
        }
	else
	{
		irq_desc_last[cpu].name = 0;
	}
        irq_desc_last[cpu].irq = irq;
        // end

	if((after.tv_sec  - before.tv_sec) < 31536000)  // 1 year
        {	
		if(unlikely(time_value > 1000000) && !check_timer_irq(desc) )
        	{
        	        printk(KERN_ALERT "============================================================\n");
        	        printk(KERN_ALERT "before time : %ld.%ld\n", before.tv_sec, before.tv_usec );
        	        printk(KERN_ALERT "after time : %ld.%ld\n", after.tv_sec, after.tv_usec );
        	        printk(KERN_ALERT "[CPU:%d] irq(number:%d, name:%s) handling takes 1 more second!!!\n", smp_processor_id(), irq, desc->name );
        	        show_irq();
        	        printk(KERN_ALERT "============================================================\n");
        	}
	}	
#endif

#ifdef CONFIG_UNHANDLED_IRQ_TRACE_DEBUGGING
        vd_irq_unset(irq, desc);
#endif
}

int generic_handle_irq(unsigned int irq);

/* Test to see if a driver has successfully requested an irq */
static inline int irq_has_action(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	return desc->action != NULL;
}

/* caller has locked the irq_desc and both params are valid */
static inline void __irq_set_handler_locked(unsigned int irq,
					    irq_flow_handler_t handler)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	desc->handle_irq = handler;
}

/* caller has locked the irq_desc and both params are valid */
static inline void
__irq_set_chip_handler_name_locked(unsigned int irq, struct irq_chip *chip,
				   irq_flow_handler_t handler, const char *name)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	irq_desc_get_irq_data(desc)->chip = chip;
	desc->handle_irq = handler;
	desc->name = name;
}

static inline int irq_balancing_disabled(unsigned int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	return desc->status_use_accessors & IRQ_NO_BALANCING_MASK;
}

static inline void
irq_set_lockdep_class(unsigned int irq, struct lock_class_key *class)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (desc)
		lockdep_set_class(&desc->lock, class);
}

#ifdef CONFIG_IRQ_PREFLOW_FASTEOI
static inline void
__irq_set_preflow_handler(unsigned int irq, irq_preflow_handler_t handler)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	desc->preflow_handler = handler;
}
#endif
#endif

#endif
