/*
 *  linux/arch/arm/kernel/arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>

#include <asm/delay.h>
#include <asm/sched_clock.h>

#include <clocksource/arm_arch_timer.h>

#define ARCH_TIMER_READ_SHIFT 0

static unsigned long arch_timer_read_counter_long(void)
{
	return arch_timer_read_counter();
}

/* 56bit to 32bit */
static u32 arch_timer_read_counter_u32(void)
{
	return (arch_timer_read_counter()>>ARCH_TIMER_READ_SHIFT);
}

static struct delay_timer arch_delay_timer;

static void __init arch_timer_delay_timer_register(void)
{
	/* Use the architected timer for the delay loop. */
	arch_delay_timer.read_current_timer = arch_timer_read_counter_long;
	arch_delay_timer.freq = arch_timer_get_rate();
	register_current_timer_delay(&arch_delay_timer);
}

int __init arch_timer_arch_init(void)
{
        u32 arch_timer_rate = arch_timer_get_rate();

	if (arch_timer_rate == 0)
		return -ENXIO;

	arch_timer_delay_timer_register();

	setup_sched_clock(arch_timer_read_counter_u32, BITS_PER_LONG, arch_timer_rate>>ARCH_TIMER_READ_SHIFT);

	return 0;
}
