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

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/memcontrol.h> /* for ../slab.h */

#include <asm/sections.h>

#include "kasan.h"
#include "../slab.h"


/* Shadow layout customization. */
#define SHADOW_BYTES_PER_BLOCK 1
#define SHADOW_BLOCKS_PER_ROW 16
#define SHADOW_BYTES_PER_ROW (SHADOW_BLOCKS_PER_ROW * SHADOW_BYTES_PER_BLOCK)
#define SHADOW_ROWS_AROUND_ADDR 5

static inline void *virt_to_obj(struct kmem_cache *s, void *slab_start, void *x)
{
	return x - ((x - slab_start) % s->size);
}

static void print_error_description(struct access_info *info)
{
	const char *bug_type = "unknown crash";

	if (info->access_addr >= MODULES_VADDR
		&& info->access_addr < MODULES_END) {
		bug_type = "global data overflow in module";
		pr_err("AddressSanitizer: %s in %pS\n",
			bug_type, (void *)info->strip_addr);
		pr_err("at addr [%p] %pS\n", (void *)info->access_addr,
			(void *)info->access_addr);
		return;
	}
	if (info->access_addr >= (unsigned long)_stext
		&& info->access_addr <= (unsigned long)_end) {
		bug_type = "global data overflow";
		pr_err("AddressSanitizer: %s in %pS\n",
			bug_type, (void *)info->strip_addr);
		pr_err("at addr [%p] %pS\n", (void *)info->access_addr,
			(void *)info->access_addr);
		return;
	}
	switch (*info->shadow_addr) {
	case KASAN_PAGE_REDZONE:
	case KASAN_SLAB_REDZONE:
	case KASAN_KMALLOC_REDZONE:
	case KASAN_VMALLOC_REDZONE:
	case 0 ... KASAN_SHADOW_SCALE_SIZE - 1:
		bug_type = "buffer overflow";
		break;
	case KASAN_GLOBAL_REDZONE:
		bug_type = "global data overflow";
		break;
	case KASAN_FREE_PAGE:
	case KASAN_SLAB_FREE:
	case KASAN_KMALLOC_FREE:
	case KASAN_VMALLOC_FREE:
		bug_type = "use after free";
		break;
	case KASAN_SHADOW_GAP:
		bug_type = "wild memory access";
		break;
	case KASAN_STACK_LEFT:
	case KASAN_STACK_MID:
	case KASAN_STACK_RIGHT:
	case KASAN_STACK_PARTIAL:
		bug_type = "out-of-bounds on stack";
		break;
	case KASAN_FREE_STACK:
		bug_type = "use after return";
		break;
	}

	pr_err("AddressSanitizer: %s in %pS at addr %p\n",
		bug_type, (void *)info->strip_addr,
		(void *)info->access_addr);
}

static void print_address_description(struct access_info *info)
{
	void *object;
	struct kmem_cache *cache;
	struct page *page;

	if (is_vmalloc_addr((void *)info->access_addr)) {
		pr_err("The buggy address %lx is located in vmalloc area\n",
		       info->access_addr);
		dump_stack();
		goto out;
	} else if ((info->access_addr >= MODULES_VADDR
		&& info->access_addr < MODULES_END) ||
		(info->access_addr >= (unsigned long)_stext
			&& info->access_addr <= (unsigned long)_end)) {
		dump_stack();
		goto out;
	}

	page = virt_to_head_page((void *)info->access_addr);

	switch (*info->shadow_addr) {
	case KASAN_SLAB_REDZONE:
		cache = page->slab_cache;
		slab_err(cache, page, "access to slab redzone");
		dump_stack();
		break;
	case KASAN_KMALLOC_FREE:
	case KASAN_KMALLOC_REDZONE:
	case 1 ... KASAN_SHADOW_SCALE_SIZE - 1:
		if (PageSlab(page)) {
			cache = page->slab_cache;
			object = virt_to_obj(cache, page_address(page), (void *)info->access_addr);
			object_err(cache, page, object, "kasan error");
		}
		break;
	case KASAN_FREE_PAGE:
	case KASAN_SLAB_FREE:
	case KASAN_VMALLOC_FREE:
		dump_page(page);
		dump_stack();
		break;
	case KASAN_SHADOW_GAP:
	case KASAN_VMALLOC_REDZONE:
		pr_err("No metainfo is available for this access.\n");
		dump_stack();
		break;
	case KASAN_STACK_LEFT:
	case KASAN_STACK_MID:
	case KASAN_STACK_RIGHT:
	case KASAN_STACK_PARTIAL:
		pr_err("stack redzone\n");
		dump_stack();
		break;
	case KASAN_FREE_STACK:
		pr_err("use after return\n");
		dump_stack();
		break;
	default:
		WARN_ON(1);
	}
out:
	pr_err("%s of size %zu by thread T%d:\n",
		info->is_write ? "Write" : "Read",
		info->access_size, info->thread_id);
}

static bool row_is_guilty(unsigned long row, unsigned long guilty)
{
	return (row <= guilty) && (guilty < row + SHADOW_BYTES_PER_ROW);
}

static void print_shadow_pointer(unsigned long row, unsigned long shadow,
				 char *output)
{
	/* The length of ">ff00ff00ff00ff00: " is 3 + (BITS_PER_LONG/8)*2 chars. */
	unsigned long space_count = 3 + (BITS_PER_LONG >> 2) + (shadow - row)*2 +
		(shadow - row) / SHADOW_BYTES_PER_BLOCK;
	unsigned long i;

	for (i = 0; i < space_count; i++)
		output[i] = ' ';
	output[space_count] = '^';
	output[space_count + 1] = '\0';
}

static void print_shadow_for_address(unsigned long addr)
{
	int i;
	unsigned long shadow = kasan_mem_to_shadow(addr);
	unsigned long aligned_shadow = round_down(shadow, SHADOW_BYTES_PER_ROW)
		- SHADOW_ROWS_AROUND_ADDR * SHADOW_BYTES_PER_ROW;

	pr_err("Memory state around the buggy address:\n");

	for (i = -SHADOW_ROWS_AROUND_ADDR; i <= SHADOW_ROWS_AROUND_ADDR; i++) {
		unsigned long kaddr = kasan_shadow_to_mem(aligned_shadow);
		char buffer[100];

		snprintf(buffer, sizeof(buffer),
			(i == 0) ? ">%lx: " : " %lx: ", kaddr);

		kasan_disable_local();

		print_hex_dump(KERN_ERR, buffer,
			DUMP_PREFIX_NONE, SHADOW_BYTES_PER_ROW, 1,
			(void *)aligned_shadow, SHADOW_BYTES_PER_ROW, 0);

		kasan_enable_local();

		if (row_is_guilty(aligned_shadow, shadow)) {
			print_shadow_pointer(aligned_shadow, shadow, buffer);
			pr_err("%s\n", buffer);
		}
		aligned_shadow += SHADOW_BYTES_PER_ROW;
	}
}

static DEFINE_SPINLOCK(kasan_lock);

void kasan_report_error(struct access_info *info)
{
	if (!kasan_report_allowed)
		return;

	spin_lock(&kasan_lock);
	kasan_disable_local();
	pr_err("================================="
		"=================================\n");
	print_error_description(info);
	print_address_description(info);

	if (!is_vmalloc_addr((void *)info->access_addr))
		print_shadow_for_address(info->access_addr);
	pr_err("================================="
		"=================================\n");
	kasan_enable_local();
	spin_unlock(&kasan_lock);
}
