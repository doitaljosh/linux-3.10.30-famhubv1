/*
 *  linux/arch/arm/kernel/smp_tlb.c
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/preempt.h>
#include <linux/smp.h>

#include <asm/smp_plat.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

/**********************************************************************/

/*
 * TLB operations
 */
struct tlb_args {
	struct vm_area_struct *ta_vma;
	unsigned long ta_start;
	unsigned long ta_end;
};

static inline void ipi_flush_tlb_all(void *ignored)
{
	local_flush_tlb_all();
}

static inline void ipi_flush_tlb_mm(void *arg)
{
	struct mm_struct *mm = (struct mm_struct *)arg;

	local_flush_tlb_mm(mm);
}

static inline void ipi_flush_tlb_page(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_page(ta->ta_vma, ta->ta_start);
}

static inline void ipi_flush_tlb_kernel_page(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_kernel_page(ta->ta_start);
}

static inline void ipi_flush_tlb_range(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_range(ta->ta_vma, ta->ta_start, ta->ta_end);
}

static inline void ipi_flush_tlb_kernel_range(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_kernel_range(ta->ta_start, ta->ta_end);
}

static inline void ipi_flush_bp_all(void *ignored)
{
	local_flush_bp_all();
}

#ifdef CONFIG_ARM_ERRATA_798181
static int erratum_a15_798181(void)
{
#if defined(CONFIG_ARCH_SDP1404) || defined(CONFIG_ARCH_SDP1304) || defined(CONFIG_ARCH_SDP1202)
	return 1;
#else
	unsigned int midr = read_cpuid_id();

	/* Cortex-A15 r0p0..r3p2 affected */
	if ((midr & 0xff0ffff0) != 0x410fc0f0 || midr > 0x413fc0f2)
		return 0;
	return 1;
#endif
}
#else
static int erratum_a15_798181(void)
{
	return 0;
}
#endif

static void ipi_flush_tlb_a15_erratum(void *arg)
{
	dmb();
}

#if defined(CONFIG_ARM_ERRATA_830076)
static void ipi_flush_tlb_a12_erratum(void* arg)
{
	isb();
}

/* piggyback on a15_798181 */
static int hook_a12_830076_all(void)
{
	isb();
	smp_call_function(ipi_flush_tlb_a12_erratum, NULL, 1);
	return 1;
}

static int hook_a12_830076_mm(struct mm_struct *mm)
{
	int this_cpu;
	cpumask_t mask = { CPU_BITS_NONE };

	isb();

	this_cpu = get_cpu();
	a15_erratum_get_cpumask(this_cpu, mm, &mask);
	smp_call_function_many(&mask, ipi_flush_tlb_a12_erratum, NULL, 1);
	put_cpu();

	return 1;
}
#else
static int hook_a12_830076_all(void) { return 0; }
static int hook_a12_830076_mm(struct mm_struct *mm) { return 0; }
#endif

static void broadcast_tlb_a15_erratum(void)
{
	if (hook_a12_830076_all())
		return;
	if (!erratum_a15_798181())
		return;

	dummy_flush_tlb_a15_erratum();

	/* Cortex-A15 r0p0..r3p2 w/o ECO fix affected */	
//	if (!(read_cpuid(CPUID_REVIDR) & 0x10))
		smp_call_function(ipi_flush_tlb_a15_erratum, NULL, 1);
}

static void broadcast_tlb_mm_a15_erratum(struct mm_struct *mm)
{
	int this_cpu;
	cpumask_t mask = { CPU_BITS_NONE };

	if (hook_a12_830076_mm(mm))
		return;
	if (!erratum_a15_798181())
		return;

	dummy_flush_tlb_a15_erratum();
	
	/* Cortex-A15 r0p0..r3p2 w/o ECO fix affected */	
//	if (!(read_cpuid(CPUID_REVIDR) & 0x10)) {
		this_cpu = get_cpu();
		a15_erratum_get_cpumask(this_cpu, mm, &mask);
		smp_call_function_many(&mask, ipi_flush_tlb_a15_erratum, NULL, 1);
		put_cpu();
//	}
}

void flush_tlb_all(void)
{
	if (tlb_ops_need_broadcast())
		on_each_cpu(ipi_flush_tlb_all, NULL, 1);
	else
		local_flush_tlb_all();
	broadcast_tlb_a15_erratum();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	if (tlb_ops_need_broadcast())
		on_each_cpu_mask(mm_cpumask(mm), ipi_flush_tlb_mm, mm, 1);
	else
		local_flush_tlb_mm(mm);
	broadcast_tlb_mm_a15_erratum(mm);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	if (tlb_ops_need_broadcast()) {
		struct tlb_args ta;
		ta.ta_vma = vma;
		ta.ta_start = uaddr;
		on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_page,
					&ta, 1);
	} else
		local_flush_tlb_page(vma, uaddr);
	broadcast_tlb_mm_a15_erratum(vma->vm_mm);
}

void flush_tlb_kernel_page(unsigned long kaddr)
{
	if (tlb_ops_need_broadcast()) {
		struct tlb_args ta;
		ta.ta_start = kaddr;
		on_each_cpu(ipi_flush_tlb_kernel_page, &ta, 1);
	} else
		local_flush_tlb_kernel_page(kaddr);
	broadcast_tlb_a15_erratum();
}

void flush_tlb_range(struct vm_area_struct *vma,
                     unsigned long start, unsigned long end)
{
	if (tlb_ops_need_broadcast()) {
		struct tlb_args ta;
		ta.ta_vma = vma;
		ta.ta_start = start;
		ta.ta_end = end;
		on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_range,
					&ta, 1);
	} else
		local_flush_tlb_range(vma, start, end);
	broadcast_tlb_mm_a15_erratum(vma->vm_mm);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	if (tlb_ops_need_broadcast()) {
		struct tlb_args ta;
		ta.ta_start = start;
		ta.ta_end = end;
		on_each_cpu(ipi_flush_tlb_kernel_range, &ta, 1);
	} else
		local_flush_tlb_kernel_range(start, end);
	broadcast_tlb_a15_erratum();
}

void flush_bp_all(void)
{
	if (tlb_ops_need_broadcast())
		on_each_cpu(ipi_flush_bp_all, NULL, 1);
	else
		local_flush_bp_all();
}
