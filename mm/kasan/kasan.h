/*
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 *
 * Some of code borrowed from https://github.com/xairy/linux by
 *        Andrey Konovalov <andreyknvl@google.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __MM_KASAN_KASAN_H
#define __MM_KASAN_KASAN_H

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_SCALE_SIZE (1UL << KASAN_SHADOW_SCALE_SHIFT)
#define KASAN_SHADOW_MASK       (KASAN_SHADOW_SCALE_SIZE - 1)

#define KASAN_FREE_PAGE         0xFF  /* page was freed */
#define KASAN_PAGE_REDZONE      0xFE  /* redzone for kmalloc_large allocations */
#define KASAN_SLAB_REDZONE      0xFD  /* Slab page redzone, does not belong to any slub object */
#define KASAN_KMALLOC_REDZONE   0xFC  /* redzone inside slub object */
#define KASAN_KMALLOC_FREE      0xFB  /* object was freed (kmem_cache_free/kfree) */
#define KASAN_SLAB_FREE         0xFA  /* free slab page */
#define KASAN_SHADOW_GAP        0xF9  /* address belongs to shadow memory */
#define KASAN_VMALLOC_REDZONE   0xF8  /* address belongs to vmalloc guard page */
#define KASAN_VMALLOC_FREE      0xF7  /* memory was freed by vfree call */
#define KASAN_GLOBAL_REDZONE    0xF6

/* Stack redzones */
#define KASAN_STACK_LEFT        0xF1
#define KASAN_STACK_MID         0xF2
#define KASAN_STACK_RIGHT       0xF3
#define KASAN_STACK_PARTIAL     0xF4

#define KASAN_FREE_STACK	0xF0

struct __asan_global {
	/* Address of the beginning of the global variable. */
	const void *beg;

	/* Initial size of the global variable. */
	size_t size;

	/* Size of the global variable + size of the red zone.  This
	 * size is 32 bytes aligned.
	 */
	size_t size_with_redzone;

	/* Name of the global variable. */
	const void *name;

	/* Name of the module where the global variable is declared. */
	const void *module_name;

	/* 1 if it has dynamic initialization, 0 otherwise. */
	unsigned long has_dynamic_init;
};


struct access_info {
	unsigned long access_addr;
	size_t access_size;
	u8 *shadow_addr;
	bool is_write;
	pid_t thread_id;
	unsigned long strip_addr;
};

extern unsigned long kasan_vmalloc_shadow_start;

extern unsigned long __read_mostly kasan_shadow_offset;

void kasan_report_error(struct access_info *info);

static inline unsigned long kasan_mem_to_shadow(unsigned long addr)
{
#ifdef CONFIG_KASAN_VMALLOC
	if (is_vmalloc_addr((void *)addr)) {
		return ((addr - VMALLOC_START) >> PAGE_SHIFT) +
			kasan_vmalloc_shadow_start;
	}
#endif

#ifdef CONFIG_KASAN_SLAB
	return (addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ kasan_shadow_offset;
#endif
}

static inline unsigned long kasan_shadow_to_mem(unsigned long shadow_addr)
{
#ifdef CONFIG_KASAN_VMALLOC
	if (shadow_addr >= kasan_vmalloc_shadow_start)
		return ((shadow_addr - kasan_vmalloc_shadow_start)
			<< PAGE_SHIFT) + VMALLOC_START;
#endif

#ifdef CONFIG_KASAN_SLAB
	return (shadow_addr - kasan_shadow_offset)
		<< KASAN_SHADOW_SCALE_SHIFT;
#endif

}

extern u32 kasan_report_allowed;

#endif
