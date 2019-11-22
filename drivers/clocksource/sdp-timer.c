/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/sched_clock.h>
#include <asm/arch_timer.h>
#include <asm/smp_twd.h>
#include <asm/localtimer.h>

#include <mach/map.h>
#include <mach/soc.h>
#include <mach/regs-timer.h>

#define tmr_debug(fmt, ...)
//#define tmr_debug(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#define SDP_MAX_TIMERS	(16)

struct sdp_timer {
	struct clk	*clk;
	void __iomem	*regs;
	u32		int_stretch;
	int		int_level;
	int		oneshot;
	int		nr_timers;
	int		clocksource_id;
	int		clockevent_id;	
	int		oldtimer;
};

struct sdp_clock_event_dev {
	u32		irq;
	int		irq_affinity;	/* cpu id, -1 for no affinity */
	int		timer_id;
	char		name[16];
	unsigned long	timer_rate;	/* in HZ */
	spinlock_t	lock;
	
	struct clock_event_device	*evt;
	struct irqaction		irqaction;
	enum clock_event_mode		mode;
};

static struct sdp_timer sdp_timer __read_mostly;

extern void sdp_init_clocks(void);

static void sdp_timer_clear_pending(int id)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int val;

	if (!sdp_timer.oldtimer) {
		val = readl(regs + SDP_TMCONE(id));
		writel(val, regs + SDP_TMCONE(id));
	} else {
		val = readl(regs + SDP_TMSTAT);
		writel(val, regs + SDP_TMSTAT);
	}
}

static int sdp_timer_pending(int id)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int val;

	if (!sdp_timer.oldtimer)		
		val = readl(regs + SDP_TMCONE(id)) & SDP_TMCONE_IRQMASK;
	else
		val = readl(regs + SDP_TMSTAT);
	
	return !!val;
}

static void __cpuinit sdp_timer_reset(int id)
{
	void __iomem *regs = sdp_timer.regs;

	writel(SDP_TMCON_STOP, regs + SDP_TMCON(id));
}

static void __cpuinit sdp_timer_setup(int id, unsigned int flags)
{
	void __iomem *regs = sdp_timer.regs;
	u32 val;

	writel(0x0, regs + SDP_TMDATA64L(id));
	writel(0x0, regs + SDP_TMDATA64H(id));
	writel(flags, regs + SDP_TMCON(id));
	/* stretch interrupt line */
	val = sdp_timer.int_stretch;
	val |= sdp_timer.int_level << 5;
	val |= sdp_timer.oneshot << 6;	
	writel(val, regs + SDP_TMCONE(id));
}

/* clockevent */
static void _sdp_set_mode(struct sdp_clock_event_dev *sdp_evt, enum clock_event_mode mode)
{
	unsigned int val;
	void __iomem *regs = sdp_timer.regs;
	
	sdp_evt->mode = mode;

	spin_lock(&sdp_evt->lock);	/* this function always runs in irq mode */

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		
		if (!sdp_timer.oldtimer) {
			val = readl(regs + SDP_TMCONE(sdp_evt->timer_id));
			val &= ~((u32) sdp_timer.oneshot << 6);
			writel(val, regs + SDP_TMCONE(sdp_evt->timer_id));
		}

		writel(sdp_evt->timer_rate / HZ, regs + SDP_TMDATA64L(sdp_evt->timer_id));		
		val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
		val |= SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN;
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		if (!sdp_timer.oldtimer) {
			val = readl(regs + SDP_TMCONE(sdp_evt->timer_id));
			val |= (sdp_timer.oneshot << 6);
			writel(val, regs + SDP_TMCONE(sdp_evt->timer_id));
		}
		
		sdp_timer_clear_pending(sdp_evt->timer_id);
		val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
		val &= ~(SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN);
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		break;
	}
	
	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s: set_mode %d con %x\n", sdp_evt->name, mode, val);
}

static int _sdp_set_next_event(struct sdp_clock_event_dev *sdp_evt, unsigned long cycles)
{
	int id = sdp_evt->timer_id;
	unsigned int val;
	void __iomem *regs = sdp_timer.regs;
	       
	spin_lock(&sdp_evt->lock);

	/* stop timer */
	val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
	writel(val & (~SDP_TMCON_RUN), regs + SDP_TMCON(id));

	/* re-program and enable timer */
	writel(cycles, regs + SDP_TMDATA64L(id));
	writel(val | SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN, regs + SDP_TMCON(id));
	readl(regs + SDP_TMCON(id));	/* flush */

	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s: next_event after %lu cycles (%llu nsec)\n", sdp_evt->name, cycles,
			(unsigned long long)clockevent_delta2ns(cycles, sdp_evt->evt));

	return 0;
}

static irqreturn_t sdp_clock_event_isr(int irq, void *dev_id)
{
	void __iomem *regs = sdp_timer.regs;
	struct sdp_clock_event_dev *sdp_evt = dev_id;
	BUG_ON(sdp_evt->irq != irq);

	if (!sdp_timer_pending(sdp_evt->timer_id))
		return IRQ_NONE;

	spin_lock(&sdp_evt->lock);

	if (sdp_evt->mode != CLOCK_EVT_MODE_PERIODIC) {
		u32 val = readl(regs + SDP_TMCON(sdp_evt->timer_id));
		val &= ~(SDP_TMCON_IRQ_ENABLE | SDP_TMCON_RUN);
		
		writel(val, regs + SDP_TMCON(sdp_evt->timer_id));
		readl(regs + SDP_TMCON(sdp_evt->timer_id));
	}
	
	sdp_timer_clear_pending(sdp_evt->timer_id);
	
	spin_unlock(&sdp_evt->lock);

	tmr_debug("%s isr: handler=%pF\n", sdp_evt->name, sdp_evt->evt->event_handler);

	if(sdp_evt->evt->event_handler)
		sdp_evt->evt->event_handler(sdp_evt->evt);
	
	return IRQ_HANDLED;
}

static void __ref _sdp_clockevent_suspend(struct sdp_clock_event_dev *sdp_evt)
{
	pr_err("%s: clockevent device suspend.\n", sdp_evt->name);
}

static void __ref _sdp_clockevent_resume(struct sdp_clock_event_dev *sdp_evt)
{
	void __iomem *regs = sdp_timer.regs;
	const u32 tmcon = SDP_TMCON_64BIT_DOWN | SDP_TMCON_MUX4;
	
	pr_info("%s: clockevent device resume.\n", sdp_evt->name);

	sdp_timer_clear_pending(sdp_evt->timer_id);
	sdp_timer_reset(sdp_evt->timer_id);

	writel(SDP_TMDATA_PRESCALING(1), regs + SDP_TMDATA(sdp_evt->timer_id));

	{
		/* TODO: check IRQ affinity after resume  */
		struct irq_desc *desc = irq_to_desc(sdp_evt->irq);
		const struct cpumask *mask = desc->irq_data.affinity;
		pr_info("%s: irq affinity = 0x%x\n", sdp_evt->name, *(u32*)mask);
	}

	sdp_timer_setup(sdp_evt->timer_id, tmcon);
}

/* calcuate mult value by given shift value */
static void __cpuinit sdp_clockevent_calc_mult(struct clock_event_device *evt, unsigned long rate)
{
	evt->mult = div_sc(rate, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(-1, evt);
	evt->min_delta_ns = clockevent_delta2ns((sdp_timer.int_stretch + 1) * 2, evt);
}

static void __cpuinit _sdp_clockevent_init(struct sdp_clock_event_dev *sdp_evt)
{
	/* prescaler  = 2 * 4 */
	const unsigned long clock_rate = clk_get_rate(sdp_timer.clk) / 8;
	struct irqaction *irqaction = &sdp_evt->irqaction;
	const u32 tmcon = SDP_TMCON_64BIT_DOWN | SDP_TMCON_MUX4;
	void __iomem *regs = sdp_timer.regs;

	spin_lock_init(&sdp_evt->lock);

	sdp_evt->timer_rate = clock_rate;
	sdp_timer_clear_pending(sdp_evt->timer_id);
	sdp_timer_reset(sdp_evt->timer_id);

	writel(SDP_TMDATA_PRESCALING(1), regs + SDP_TMDATA(sdp_evt->timer_id));

	sdp_clockevent_calc_mult(sdp_evt->evt, clock_rate);

	memset(irqaction, 0, sizeof(*irqaction));	
	irqaction->name = sdp_evt->name;
	irqaction->flags = IRQF_TIMER | IRQF_NOBALANCING;
	if(sdp_timer.int_level)
		irqaction->flags |= IRQF_TRIGGER_HIGH;
	else
		irqaction->flags |= IRQF_TRIGGER_RISING;
	irqaction->handler = sdp_clock_event_isr;
	irqaction->dev_id = sdp_evt;
	
	pr_info("%s: clockevent device using timer%d, irq%d.\n",
			sdp_evt->name, sdp_evt->timer_id, sdp_evt->irq);
	pr_info("\trate=%ldhz mult=%u shift=%u stretch=0x%x min/max_delt=%llu/%llu\n",
			clock_rate, sdp_evt->evt->mult, sdp_evt->evt->shift, sdp_timer.int_stretch,
			(unsigned long long)sdp_evt->evt->min_delta_ns,
			(unsigned long long)sdp_evt->evt->max_delta_ns);
	
	setup_irq(sdp_evt->irq, irqaction);

	if (sdp_evt->irq_affinity >= 0)
		irq_set_affinity(sdp_evt->irq, cpumask_of(sdp_evt->irq_affinity));
	
	sdp_timer_setup(sdp_evt->timer_id, tmcon);

	clockevents_register_device(sdp_evt->evt);
}

/* global clock event device */
static struct sdp_clock_event_dev sdp_clockevent = {
	.name		= "sdp_event_timer",
};

static void sdp_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_set_mode(&sdp_clockevent, mode);
}

static int sdp_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	return _sdp_set_next_event(&sdp_clockevent, cycles);
}

static void sdp_clockevent_suspend(struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_clockevent_suspend(&sdp_clockevent);
}

static void sdp_clockevent_resume(struct clock_event_device *evt)
{
	BUG_ON(sdp_clockevent.evt != evt);
	_sdp_clockevent_resume(&sdp_clockevent);
}

static void __init sdp_clockevent_init(struct device_node *np)
{
	struct clock_event_device *evt;
	u32 timer_id = sdp_timer.clockevent_id;
       
	evt = kzalloc(sizeof(*sdp_clockevent.evt), GFP_ATOMIC);
	BUG_ON(!evt);

	sdp_clockevent.irq = irq_of_parse_and_map(np, timer_id);
	sdp_clockevent.irq_affinity = -1;
	sdp_clockevent.timer_id = timer_id;
	sdp_clockevent.evt = evt;

	evt->set_mode = sdp_set_mode;
	evt->set_next_event = sdp_set_next_event;
	evt->cpumask = cpumask_of(0);
	evt->rating = 300;
	evt->shift = 32;
	evt->name = sdp_clockevent.name;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->suspend = sdp_clockevent_suspend;
	evt->resume = sdp_clockevent_resume;

	_sdp_clockevent_init(&sdp_clockevent);
}

/* clock source */
static inline u32 sdp_clocksource_count(void)
{
	void __iomem *regs = sdp_timer.regs;
	return readl(regs + SDP_TMCNT64L(sdp_timer.clocksource_id));
}

static cycle_t sdp_clocksource_read(struct clocksource *cs)
{
	return sdp_clocksource_count();
}

static u32 notrace sdp_sched_clock_read(void)
{
	return sdp_clocksource_count();
}

static void __ref sdp_clocksource_suspend(struct clocksource *cs)
{
}

static void __ref sdp_clocksource_resume(struct clocksource *cs)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned int flags = SDP_TMCON_64BIT_UP | SDP_TMCON_MUX4 |
				SDP_TMCON_RUN;
	sdp_timer_reset(sdp_timer.clocksource_id);
	writel(SDP_TMDATA_PRESCALING(9), regs + SDP_TMDATA(sdp_timer.clocksource_id));
	sdp_timer_setup(sdp_timer.clocksource_id, flags);
}

struct clocksource sdp_clocksource = {
	.name		= "sdp_clocksource",
	.rating		= 300,
	.read		= sdp_clocksource_read,
	.suspend	= sdp_clocksource_suspend,
	.resume		= sdp_clocksource_resume,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init sdp_clocksource_init(void)
{
	void __iomem *regs = sdp_timer.regs;
	unsigned long clock_rate = clk_get_rate(sdp_timer.clk) / 4;
	unsigned int flags = SDP_TMCON_64BIT_UP | SDP_TMCON_MUX4 |
				SDP_TMCON_RUN;

	sdp_timer_reset(sdp_timer.clocksource_id);

	clock_rate /= 10;	/* div 10 */

	writel(SDP_TMDATA_PRESCALING(9), regs + SDP_TMDATA(sdp_timer.clocksource_id));

	if ( soc_is_sdp1202() ) {
		sdp_clocksource.rating = 500;
	}
		
	clocksource_register_hz(&sdp_clocksource, clock_rate);

	if(!(soc_is_sdp1202() || soc_is_sdp1304()))
		setup_sched_clock(sdp_sched_clock_read, 32, clock_rate);

	sdp_timer_setup(sdp_timer.clocksource_id, flags);
}

#if defined(CONFIG_LOCAL_TIMERS)
/* TODO: hotplug cpu */
static DEFINE_PER_CPU(struct sdp_clock_event_dev, percpu_sdp_tick);

static void sdp_tick_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	_sdp_set_mode(sdp_evt, mode);
}

static int sdp_tick_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	return _sdp_set_next_event(sdp_evt, cycles);
}

static void sdp_localtimer_suspend(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	return _sdp_clockevent_suspend(sdp_evt);
}

static void sdp_localtimer_resume(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	return _sdp_clockevent_resume(sdp_evt);
}

static int __cpuinit sdp_localtimer_setup(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);
	unsigned int cpu = smp_processor_id();

	sdp_evt->evt = evt;

	evt->set_mode = sdp_tick_set_mode;
	evt->set_next_event = sdp_tick_set_next_event;
	evt->cpumask = cpumask_of(cpu);
	evt->rating = 400;
	evt->shift = 32;
	evt->name = sdp_evt->name;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->suspend = sdp_localtimer_suspend;
	evt->resume = sdp_localtimer_resume;

	_sdp_clockevent_init(sdp_evt);
	
	return 0;
}

static void sdp_localtimer_stop(struct clock_event_device *evt)
{
	struct sdp_clock_event_dev *sdp_evt = this_cpu_ptr(&percpu_sdp_tick);

	BUG_ON(sdp_evt->evt != evt);
	sdp_evt->evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	remove_irq(sdp_evt->irq, &sdp_evt->irqaction);
	pr_info ("%s: stopped.\n", sdp_evt->name);
}

static struct local_timer_ops sdp_lt_ops __cpuinitdata = {
	.setup	= sdp_localtimer_setup,
	.stop	= sdp_localtimer_stop,
};

static void __init sdp_localtimer_init(struct device_node *np)
{
	struct property *prop;
	const __be32 *p;
	u32 timer_id;
	int cpu = 0;

	of_property_for_each_u32(np, "localtimer_ids", prop, p, timer_id) {
		struct sdp_clock_event_dev *sdp_evt = &per_cpu(percpu_sdp_tick, cpu);
		u32 irq = irq_of_parse_and_map(np, (int) timer_id);
		
		if (irq <= 0) {
			pr_err("sdp_timer: failed to get timer IRQ for cpu%d\n", cpu);
			return;
		}

		sdp_evt->irq = irq;
		sdp_evt->irq_affinity = cpu;
		sdp_evt->timer_id = (int) timer_id;
		snprintf (sdp_evt->name, 16, "sdp_tick%d", cpu);	
	
		pr_info("%s: localtimer for cpu%d probed: timer%d irq%d\n",
				sdp_evt->name, cpu, sdp_evt->timer_id, irq);
		
		cpu++;
		if (cpu >= NR_CPUS)
			break;
	}
	of_node_put(np);

	local_timer_register(&sdp_lt_ops);
}

#else	/* CONFIG_LOCAL_TIMERS */
static void __init sdp_localtimer_init(struct device_node *np) {}
#endif	/* !CONFIG_LOCAL_TIMERS */

static void __init sdp_timer_of_init(struct device_node *np)
{
	int ret;
	const __be32 *localtimers;
	u32 nr_timers, nr_local_timers;

	ret = of_property_read_u32(np, "nr_timers", &nr_timers);
	BUG_ON(ret || nr_timers > SDP_MAX_TIMERS);
	sdp_timer.nr_timers = (int) nr_timers;
	
	sdp_timer.regs = of_iomap(np, 0);
	BUG_ON(!sdp_timer.regs);

	sdp_timer.clk = of_clk_get(np, 0);
	if (IS_ERR(sdp_timer.clk)) {
		pr_err("Failed to get clock\n");
		sdp_timer.clk = NULL;
	} else
		clk_prepare_enable(sdp_timer.clk);

	if(of_get_property(np, "int_level", NULL)) {
		sdp_timer.int_level = 1;
	}
	if(of_get_property(np, "one-shot", NULL)) {
		sdp_timer.oneshot = 1;
	}
	
	if ( of_get_property(np, "old-timer", NULL)) {
		sdp_timer.oldtimer = 1;
	}

	if (of_property_read_u32(np, "int_stretch", &sdp_timer.int_stretch))
		sdp_timer.int_stretch = 0xf;	/* default max */


	BUG_ON(of_property_read_u32(np, "clocksource_id", &sdp_timer.clocksource_id));
	BUG_ON(of_property_read_u32(np, "clockevent_id", &sdp_timer.clockevent_id));
		
	of_node_put(np);
	
	sdp_clocksource_init();
	sdp_clockevent_init(np);
	
	localtimers = of_get_property(np, "localtimer_ids", &nr_local_timers);
	if (localtimers != NULL) {
		nr_local_timers /= sizeof(*localtimers);
		if (nr_local_timers < NR_CPUS)
			pr_err("sdp_timer: number of hw timers(%u) are not enough to run local timer!.\n",
				nr_local_timers);
		else
			sdp_localtimer_init(np);
	}
}

CLOCKSOURCE_OF_DECLARE(sdp_timer, "samsung,sdp-timer", sdp_timer_of_init);

