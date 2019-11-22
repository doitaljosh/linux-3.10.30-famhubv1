/*
 * sdp12xx_timer.c
 * original code is from arch/arm/plat-sdp/sdp_hrtimer64.c
 *   Support legacy timer logic prior to 2013.
 *
 * Copyright (C) 2010 Samsung Electronics.co
 * Author : tukho.kim@samsung.com
 *
 */
#include <linux/init.h>
#include <linux/dma-mapping.h>
//#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/clk.h>

/* hrtimer headr file */
#include <linux/clocksource.h>
#include <linux/clockchips.h>
/* hrtimer headr file end */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>


#include <asm/mach/time.h>
#include <mach/irqs.h>
#include <mach/soc.h>

#include <mach/sdp12xx_timer.h>

static SDP_TIMER_REG_T * gp_sdp_timer;

enum clock_event_mode g_clkevt_mode = CLOCK_EVT_MODE_PERIODIC;

// resource 
static spinlock_t sdp_hrtimer_lock;

static void sdp_clkevent_setmode(enum clock_event_mode mode,
				   struct clock_event_device *clk)
{
	g_clkevt_mode = mode;

	switch(mode){
		case(CLOCK_EVT_MODE_PERIODIC):
			R_SYSTMCON = (TMCON_MUX04 | TMCON_INT_DMA_EN | TMCON_RUN);
//			printk("[%s] periodic mode\n", __FUNCTION__);
			break;
		case(CLOCK_EVT_MODE_ONESHOT):
			R_SYSTMCON = (TMCON_MUX04 | TMCON_INT_DMA_EN);
//			printk("[%s] oneshot mode\n", __FUNCTION__);
			break;
        	case (CLOCK_EVT_MODE_UNUSED):
        	case (CLOCK_EVT_MODE_SHUTDOWN):
		default: 
			R_SYSTMCON = 0;
			break;
	}
}

static int sdp_clkevent_nextevent(unsigned long evt,
				 struct clock_event_device *unused)
{
	BUG_ON(!evt);

	R_SYSTMCON = 0;
	R_SYSTMDATA64L = evt;  
	R_SYSTMCON = (TMCON_MUX04 | TMCON_INT_DMA_EN | TMCON_RUN);

//	printk("[%s] evt is %d\n", __FUNCTION__,(u32)evt);

	return 0;
}

struct clock_event_device sdp_clockevent = {
	.name		= "SDP Timer clock event",
	.rating		= 200,
	.shift		= 32,		// nanosecond shift 
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= sdp_clkevent_setmode,
	.set_next_event = sdp_clkevent_nextevent,
};

static irqreturn_t sdp_hrtimer_isr(int irq, void *dev_id)
{
	unsigned int regVal = R_TMSTATUS;
	struct clock_event_device *evt = &sdp_clockevent;

	if(!regVal) return IRQ_NONE;

	if(regVal & SYS_TIMER_BIT){
		if (g_clkevt_mode == CLOCK_EVT_MODE_ONESHOT) 
				R_SYSTMCON = TMCON_MUX04;
		R_TMSTATUS = SYS_TIMER_BIT;  // clock event timer intr pend clear
		evt->event_handler(evt);
	}

	return IRQ_HANDLED;
}

static struct irqaction sdp_hrtimer_event = {
	.name = "SDP Hrtimer interrupt handler",
#ifdef CONFIG_ARM_GIC
	.flags = IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
#else
	.flags = IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER,
#endif
	.handler = sdp_hrtimer_isr,
};

static cycle_t sdp_clksrc_read (struct clocksource *cs)
{
#if 0
	cycle_t retval;
	unsigned long flags;	

	u32 timer64_h, timer64_l;	


	spin_lock_irqsave(&sdp_hrtimer_lock, flags);

	timer64_h = R_CLKSRC_TMCNT64H;
	timer64_l = R_CLKSRC_TMCNT64L;

	if(timer64_h != R_CLKSRC_TMCNT64H)
		timer64_l = R_CLKSRC_TMCNT64L;

	retval = timer64_h;
	retval = (retval << 32) + timer64_l;
	retval = timer64_l;

	spin_unlock_irqrestore(&sdp_hrtimer_lock, flags);

	return retval;
#else 
	return R_CLKSRC_TMCNT64L;
#endif
}

struct clocksource sdp12xx_clocksource = {
	.name 		= "SDP Timer clock source",
#ifdef CONFIG_ARCH_SDP1202
	.rating		= 500,
#else
	.rating 	= 200,
#endif
	.read 		= sdp_clksrc_read,
	.mask 		= CLOCKSOURCE_MASK(32),
	.mult		= 0,
	.shift 		= 19,		
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init sdp_hrtimer_clksrc_init(unsigned long timer_clock)
{
	unsigned long clksrc_clock;

	clksrc_clock = timer_clock >> 2;	// MUX4
	R_CLKSRC_TMDATA = 9 << 16;		// divide 10
	clksrc_clock = clksrc_clock / 10;

// clock source init -> clock source 
	sdp12xx_clocksource.mult = clocksource_khz2mult(clksrc_clock / 1000, sdp12xx_clocksource.shift);
	clocksource_register(&sdp12xx_clocksource);

	R_CLKSRC_TMDATA64L = 0x0;
	R_CLKSRC_TMDATA64H = 0x0;
	R_CLKSRC_TMCON = TMCON_64BIT_UP | TMCON_MUX04 | TMCON_RUN;
//	R_CLKSRC_TMCON = TMCON_MUX04 | TMCON_RUN;

	printk("[%s] HRTIMER Clock-source %d\n", __FUNCTION__,(u32)clksrc_clock);

}

/* TODO: setup sched_clock.
 * NOTE: fox-b 2013 (3.0.20) had no sched_clock config then.  */

static unsigned long __init sdp_get_timer_clkrate(struct device_node *np)
{
	unsigned long ret = 0;
//	struct clk* clk = clk_get(NULL, "sdp12xx_timer");
	struct clk* clk = of_clk_get(np, 0);
	if (clk)
		ret = clk_get_rate(clk);
	return ret;
}

/* Initialize Timer */
void __init sdp12xx_timer_init(struct device_node *np)
{
	unsigned long timer_clock;
	unsigned long clkevt_clock;
	unsigned int interrupt_num = 0;
	
//	unsigned long init_clkevt;

	printk(KERN_INFO "Samsung DTV Linux System HRTimer initialize\n");
	gp_sdp_timer = (SDP_TIMER_REG_T*)of_iomap(np, 0);
	R_TMDMASEL = 0;

// Timer reset & stop
	R_TIMER(0, control) = 0;
	R_TIMER(1, control) = 0;


// get Timer source clock 
	timer_clock = sdp_get_timer_clkrate(np);
	printk(KERN_INFO "HRTIMER: source clock is %u Hz, SYS tick: %d\n", 
			(unsigned int)timer_clock, HZ);

// init lock 
	spin_lock_init(&sdp_hrtimer_lock);

	sdp_hrtimer_clksrc_init(timer_clock);

	clkevt_clock = timer_clock >> 2;		// MUX04
	R_SYSTMDATA = 1 << 16;
	clkevt_clock = (clkevt_clock) >> 1; 	// divide 2
	R_SYSTMDATA64L = clkevt_clock / HZ;

// register timer interrupt service routine
	interrupt_num = irq_of_parse_and_map(np, 0);

	if ( interrupt_num < 0 )
	{
		printk("Invaid Timer irq number %u\n", interrupt_num);
		return ;
	}

	setup_irq(interrupt_num, &sdp_hrtimer_event);

// timer event init
	sdp_clockevent.mult = 
			div_sc(clkevt_clock, NSEC_PER_SEC, (int) sdp_clockevent.shift);
	sdp_clockevent.max_delta_ns =
			clockevent_delta2ns(0xFFFFFFFF, &sdp_clockevent);
	sdp_clockevent.min_delta_ns =
			clockevent_delta2ns(0xf, &sdp_clockevent);

	sdp_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&sdp_clockevent);
}


CLOCKSOURCE_OF_DECLARE(sdp_timer, "samsung,sdp12xx-timer", sdp12xx_timer_init);
