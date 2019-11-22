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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/radix-tree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kasan.h>
#include <linux/memcontrol.h>
#include <linux/syscore_ops.h>
#include <linux/debugfs.h>
#include <linux/dcache.h>

#include "kasan.h"

unsigned int __read_mostly kasan_initialized;
u32 kasan_report_allowed = 1;

static unsigned long kasan_shadow_start;
static unsigned long kasan_shadow_end;

unsigned long kasan_vmalloc_shadow_start;
static unsigned long kasan_vmalloc_shadow_end;

unsigned long __read_mostly kasan_shadow_offset;

static struct dentry *kasan_dentry;

#ifdef CONFIG_KASAN_GLOBALS
#define START_VADDR MODULES_VADDR
#else
#define START_VADDR PAGE_OFFSET
#endif

#ifdef CONFIG_KASAN_UAR
static inline void kasan_uar_enable(void)
{
	current->kasan_uar_off--;
}
static inline void kasan_uar_disable(void)
{
	current->kasan_uar_off++;
}
#else
static inline void kasan_uar_enable(void) { }
static inline void kasan_uar_disable(void) { }
#endif

static inline bool addr_is_in_mem(unsigned long addr)
{
	bool ret = false;

#ifdef CONFIG_KASAN_SLAB
	ret = (addr >= START_VADDR && addr < (unsigned long)high_memory);
#endif

#ifdef CONFIG_KASAN_VMALLOC
	ret = ret || is_vmalloc_addr((void *)addr);
#endif

	return ret;
}

void kasan_enable_local(void)
{
	current->kasan_depth--;
}

void kasan_disable_local(void)
{
	current->kasan_depth++;
}

static inline bool kasan_enabled(void)
{
	return likely(kasan_initialized && !current->kasan_depth);
}

/*
 * Poisons the shadow memory for 'size' bytes starting from 'addr'.
 * Memory addresses should be aligned to KASAN_SHADOW_SCALE_SIZE.
 */
static void poison_shadow(const void *address, size_t size, u8 value)
{
	unsigned long shadow_beg, shadow_end;
	unsigned long addr = (unsigned long)address;

	WARN_ON_ONCE(!addr_is_in_mem(addr) || !addr_is_in_mem(addr+size - 1));

	shadow_beg = kasan_mem_to_shadow(addr);
	shadow_end = kasan_mem_to_shadow(addr + size - 1) + 1;

	memset((void *)shadow_beg, value, shadow_end - shadow_beg);
}

static void unpoison_shadow(const void *address, size_t size)
{
	poison_shadow(address, size, 0);

	if (size & KASAN_SHADOW_MASK) {
		u8 *shadow = (u8 *)kasan_mem_to_shadow((unsigned long)address
						+ size);
		*shadow = size & KASAN_SHADOW_MASK;
	}
}

static bool address_is_poisoned(unsigned long addr)
{
	s8 shadow_value = *(s8 *)kasan_mem_to_shadow(addr);

	if (shadow_value != 0) {
		s8 last_accessed_byte = addr & KASAN_SHADOW_MASK;
		return last_accessed_byte >= shadow_value;
	}
	return false;
}

static bool memory_is_zero(const u8 *beg, size_t size)
{
	const u8 *end = beg + size;
	unsigned long beg_addr = (unsigned long)beg;
	unsigned long end_addr = (unsigned long)end;
	unsigned long *aligned_beg =
		(unsigned long *)round_up(beg_addr, sizeof(unsigned long));
	unsigned long *aligned_end =
		(unsigned long *)round_down(end_addr, sizeof(unsigned long));
	unsigned long all = 0;
	const u8 *mem;
	for (mem = beg; mem < (u8 *)aligned_beg && mem < end; mem++)
		all |= *mem;
	for (; aligned_beg < aligned_end; aligned_beg++)
		all |= *aligned_beg;
	if ((u8 *)aligned_end >= beg)
		for (mem = (u8 *)aligned_end; mem < end; mem++)
			all |= *mem;
	return all == 0;
}

/*
 * Returns address of the first poisoned byte if the memory region
 * lies in the physical memory and poisoned, returns 0 otherwise.
 */
static unsigned long memory_is_poisoned(unsigned long addr, size_t size)
{
	unsigned long beg, end;
	unsigned long aligned_beg, aligned_end;
	unsigned long shadow_beg, shadow_end;

	beg = addr;
	end = beg + size;
	if (!addr_is_in_mem(beg) || !addr_is_in_mem(end))
		return 0;

	aligned_beg = round_up(beg, KASAN_SHADOW_SCALE_SIZE);
	aligned_end = round_down(end, KASAN_SHADOW_SCALE_SIZE);
	shadow_beg = kasan_mem_to_shadow(aligned_beg);
	shadow_end = kasan_mem_to_shadow(aligned_end);

	if (shadow_beg == shadow_end)
		return address_is_poisoned(beg) ? beg : 0;

	if (!address_is_poisoned(beg) &&
	    !address_is_poisoned(end - 1) &&
	    (shadow_end <= shadow_beg ||
		    memory_is_zero((const u8 *)shadow_beg,
			    shadow_end - shadow_beg)))
		return 0;
	for (; beg < end; beg++)
		if (address_is_poisoned(beg))
			return beg;

	WARN_ON(1); /* Unreachable. */
	return 0;
}

static __always_inline void check_memory_region(unsigned long addr,
						size_t size, bool write)
{
	unsigned long access_addr;
	struct access_info info;

	if (unlikely(size == 0))
		return;

	if (!kasan_enabled())
		return;

	access_addr = memory_is_poisoned(addr, size);
	if (access_addr == 0)
		return;

	kasan_uar_disable();
	info.access_addr = access_addr;
	info.access_size = size;
	info.shadow_addr = (u8 *)kasan_mem_to_shadow(access_addr);
	info.is_write = write;
	info.thread_id = current->pid;
	info.strip_addr = _RET_IP_;
	kasan_report_error(&info);
	kasan_uar_enable();
}

static inline size_t lowmem_shadow_size(void)
{
#ifdef CONFIG_KASAN_SLAB
	return ((unsigned long)high_memory - START_VADDR)
		>> KASAN_SHADOW_SCALE_SHIFT;
#else
	return 0;
#endif
}

static inline size_t vmalloc_shadow_size(void)
{
#ifdef CONFIG_KASAN_VMALLOC
	return (VMALLOC_END - VMALLOC_START) >> PAGE_SHIFT;
#else
	return 0;
#endif
}

void kasan_suspend(void)
{
	kasan_initialized = 0;
}

void kasan_resume(void)
{
	kasan_initialized = 1;
}

void __init kasan_alloc_shadow(void)
{
	size_t kasan_shadow_size;
	phys_addr_t shadow_phys_addr;

	kasan_shadow_size = lowmem_shadow_size()
		+ vmalloc_shadow_size();

	shadow_phys_addr = memblock_alloc(kasan_shadow_size, PAGE_SIZE);
	if (!shadow_phys_addr) {
		pr_err("Unable to reserve shadow region\n");
		return;
	}

	kasan_shadow_start = (unsigned long)phys_to_virt(shadow_phys_addr);
	kasan_shadow_end = kasan_shadow_start + lowmem_shadow_size();
	kasan_vmalloc_shadow_start = kasan_shadow_end;
	kasan_vmalloc_shadow_end = kasan_vmalloc_shadow_start
		+ vmalloc_shadow_size();

	pr_info("Lowmem shadow start: 0x%lx\n", kasan_shadow_start);
	pr_info("Lowmem shadow end: 0x%lx\n", kasan_shadow_end);
	pr_info("Vmalloc shadow start 0x%lx\n", kasan_vmalloc_shadow_start);
	pr_info("Vmalloc shadow end 0x%lx\n", kasan_vmalloc_shadow_end);
}

void __init kasan_init_shadow(void)
{
	if (kasan_shadow_start) {
		kasan_shadow_offset = kasan_shadow_start
			- (START_VADDR >> KASAN_SHADOW_SCALE_SHIFT);

		unpoison_shadow((void *)START_VADDR,
				(size_t)(kasan_shadow_start - START_VADDR));
		poison_shadow((void *)kasan_shadow_start,
			kasan_vmalloc_shadow_end - kasan_shadow_start,
			KASAN_SHADOW_GAP);
		unpoison_shadow((void *)kasan_vmalloc_shadow_end,
				(size_t)(high_memory - kasan_vmalloc_shadow_end));
#ifdef CONFIG_KASAN_VMALLOC
		unpoison_shadow((void *)VMALLOC_START,
				(size_t)(VMALLOC_END - VMALLOC_START));
#endif
		kasan_shadow_offset = kasan_shadow_start
			- (START_VADDR >> KASAN_SHADOW_SCALE_SHIFT);

		kasan_initialized = 1;
	}
}

#ifdef CONFIG_KASAN_SLAB
void kasan_alloc_slab_pages(struct page *page, int order)
{
	poison_shadow(page_address(page),
		PAGE_SIZE << order, KASAN_SLAB_REDZONE);
}

void kasan_free_slab_pages(struct page *page, int order)
{
	poison_shadow(page_address(page), PAGE_SIZE << order, KASAN_SLAB_FREE);
}

void kasan_slab_alloc(struct kmem_cache *cache, void *object)
{
	if (unlikely(object == NULL))
		return;

	poison_shadow(object, cache->size, KASAN_KMALLOC_REDZONE);
	unpoison_shadow(object, cache->object_size);
}

void kasan_slab_free(struct kmem_cache *cache, void *object)
{
	unsigned long size = cache->size;
	unsigned long rounded_up_size = round_up(size,
						KASAN_SHADOW_SCALE_SIZE);

	if (unlikely(cache->flags & SLAB_DESTROY_BY_RCU))
		return;

	poison_shadow(object, rounded_up_size, KASAN_KMALLOC_FREE);
}

void kasan_kmalloc(struct kmem_cache *cache, const void *object, size_t size)
{
	unsigned long rounded_up_object_size = cache->size;

	if (unlikely(object == NULL))
		return;

	poison_shadow(object, rounded_up_object_size,
		KASAN_KMALLOC_REDZONE);
	unpoison_shadow(object, size);
}
EXPORT_SYMBOL(kasan_kmalloc);

void kasan_kmalloc_large(void *ptr, size_t size)
{
	struct page *page;
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (unlikely(ptr == NULL))
		return;

	page = virt_to_head_page(ptr);
	redzone_start = round_up((unsigned long)(ptr + size),
				KASAN_SHADOW_SCALE_SIZE);
	redzone_end = (unsigned long)ptr + (PAGE_SIZE << compound_order(page));

	unpoison_shadow(ptr, size);
	if (redzone_end > redzone_start)
		poison_shadow((void *)redzone_start,
			redzone_end - redzone_start,
			KASAN_PAGE_REDZONE);
}
EXPORT_SYMBOL(kasan_kmalloc_large);

void kasan_krealloc(const void *object, size_t size)
{
	struct page *page;

	if (unlikely(object == ZERO_SIZE_PTR))
		return;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page)))
		kasan_kmalloc_large(page_address(page), size);
	else
		kasan_kmalloc(page->slab_cache, object, size);
}

void kasan_kfree_large(const void *ptr)
{
	struct page *page;

	page = virt_to_page(ptr);
	poison_shadow(ptr, PAGE_SIZE << compound_order(page), KASAN_FREE_PAGE);
}

void kasan_alloc_pages(struct page *page, unsigned int order)
{
	if (page && !PageHighMem(page))
		unpoison_shadow(page_address(page), PAGE_SIZE << order);
}

void kasan_free_pages(struct page *page, unsigned int order)
{
	if (!PageHighMem(page))
		poison_shadow(page_address(page),
			PAGE_SIZE << order, KASAN_FREE_PAGE);
}

#endif /* CONFIG_KASAN_SLAB */

#ifdef CONFIG_KASAN_VMALLOC

void kasan_vmalloc(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr) ||
		!is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	unpoison_shadow((void *)addr, size - PAGE_SIZE);
	poison_shadow((void *)addr + size - PAGE_SIZE,
		PAGE_SIZE, KASAN_VMALLOC_REDZONE);
}

void kasan_vmalloc_nopageguard(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr)
		|| !is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	unpoison_shadow((void *)addr, size);
}

void kasan_vfree(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr)
		|| !is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	poison_shadow((void *)addr, size, KASAN_VMALLOC_FREE);
}

#endif /* CONFIG_KASAN_VMALLOC */

void *kasan_memcpy(void *dst, const void *src, size_t len)
{
	check_memory_region((unsigned long)src, len, false);
	check_memory_region((unsigned long)dst, len, true);

	return memcpy(dst, src, len);
}
EXPORT_SYMBOL(kasan_memcpy);

void *kasan_memset(void *ptr, int val, size_t len)
{
	check_memory_region((unsigned long)ptr, len, true);

	return memset(ptr, val, len);
}
EXPORT_SYMBOL(kasan_memset);

void *kasan_memmove(void *dst, const void *src, size_t len)
{
	check_memory_region((unsigned long)src, len, false);
	check_memory_region((unsigned long)dst, len, true);

	return memmove(dst, src, len);
}
EXPORT_SYMBOL(kasan_memmove);

void __asan_load1(unsigned long addr)
{
	check_memory_region(addr, 1, false);
}
EXPORT_SYMBOL(__asan_load1);

void __asan_load2(unsigned long addr)
{
	check_memory_region(addr, 2, false);
}
EXPORT_SYMBOL(__asan_load2);

void __asan_load4(unsigned long addr)
{
	check_memory_region(addr, 4, false);
}
EXPORT_SYMBOL(__asan_load4);

void __asan_load8(unsigned long addr)
{
	check_memory_region(addr, 8, false);
}
EXPORT_SYMBOL(__asan_load8);

void __asan_load16(unsigned long addr)
{
	check_memory_region(addr, 16, false);
}
EXPORT_SYMBOL(__asan_load16);

void __asan_loadN(unsigned long addr, size_t size)
{
	check_memory_region(addr, size, false);
}
EXPORT_SYMBOL(__asan_loadN);

void __asan_store1(unsigned long addr)
{
	check_memory_region(addr, 1, true);
}
EXPORT_SYMBOL(__asan_store1);

void __asan_store2(unsigned long addr)
{
	check_memory_region(addr, 2, true);
}
EXPORT_SYMBOL(__asan_store2);


void __asan_store4(unsigned long addr)
{
	check_memory_region(addr, 4, true);
}
EXPORT_SYMBOL(__asan_store4);


void __asan_store8(unsigned long addr)
{
	check_memory_region(addr, 8, true);
}
EXPORT_SYMBOL(__asan_store8);

void __asan_store16(unsigned long addr)
{
	check_memory_region(addr, 16, true);
}
EXPORT_SYMBOL(__asan_store16);

void __asan_storeN(unsigned long addr, size_t size)
{
	check_memory_region(addr, size, true);
}
EXPORT_SYMBOL(__asan_storeN);


#ifdef CONFIG_KASAN_STACK
unsigned long __asan_get_shadow_ptr(void)
{
	return kasan_shadow_offset;
}
EXPORT_SYMBOL(__asan_get_shadow_ptr);
#endif

#ifdef CONFIG_KASAN_GLOBALS

void register_global(struct __asan_global *global, struct radix_tree_root *tree)
{
	size_t aligned_size = round_up(global->size, KASAN_SHADOW_SCALE_SIZE);
	void **pslot;

	pslot = radix_tree_lookup_slot(tree,
				(unsigned long)global->beg);

	if (!pslot)
		WARN_ON(radix_tree_insert(tree, (unsigned long)global->beg,
						&global->size));
	else {
		size_t size = *(size_t *)radix_tree_deref_slot(pslot);
		if (size >= global->size)
			return;
		else
			radix_tree_replace_slot(pslot, &global->size);
	}

	unpoison_shadow(global->beg, global->size);

	poison_shadow(global->beg + aligned_size,
		global->size_with_redzone - aligned_size,
		KASAN_GLOBAL_REDZONE);
}

void __asan_register_globals(struct __asan_global *globals, size_t n)
{
	int i;
	static RADIX_TREE(globals_tree, GFP_KERNEL);

	if (unlikely(!kasan_initialized))
		return;

	for (i = 0; i < n; i++)
		register_global(&globals[i], &globals_tree);

	for (i = 0; i < n; i++)
		radix_tree_delete(&globals_tree,
				(unsigned long)globals[i].beg);
}
EXPORT_SYMBOL(__asan_register_globals);

void __asan_unregister_globals(struct __asan_global *globals, size_t n)
{
}
EXPORT_SYMBOL(__asan_unregister_globals);
#endif

#ifdef CONFIG_KASAN_UAR
#define FAKE_THREAD_SIZE_ORDER 4
void kasan_allocate_fake_stack(struct task_struct *task, gfp_t gfp, int node)
{
	struct page *page;

	if (unlikely(!kasan_initialized))
		return;

	page = alloc_pages_node(node, gfp, FAKE_THREAD_SIZE_ORDER);
	if (page)
		local_set(&task->kasan_fake_sp,
			(unsigned long)page_address(page));
	else {
		local_set(&task->kasan_fake_sp, 0);
		WARN_ON_ONCE(1);
	}
}

void kasan_free_fake_stack(struct task_struct *task)
{
	unsigned long fake_stack_addr = local_read(&task->kasan_fake_sp);

	if (unlikely(!kasan_initialized))
		return;

	if (fake_stack_addr) {
		free_memcg_kmem_pages(fake_stack_addr &
				~((PAGE_SIZE << FAKE_THREAD_SIZE_ORDER) - 1),
				FAKE_THREAD_SIZE_ORDER);
		local_set(&task->kasan_fake_sp, 0);
	}
}

void kasan_uar_init(void)
{
	kasan_allocate_fake_stack(&init_task, GFP_ATOMIC, NUMA_NO_NODE);
}

static __always_inline void *kasan_stack_malloc(size_t size,
						void *real_stack,
						int class_id)
{
	if (likely(kasan_initialized &&
			local_read(&current->kasan_fake_sp) &&
			!current->kasan_uar_off)) {
		size_t aligned_size = 64UL << class_id;
		local_add(aligned_size, &current->kasan_fake_sp);
		unpoison_shadow((void *)local_read(&current->kasan_fake_sp)
				- aligned_size, aligned_size);
		return (void *)local_read(&current->kasan_fake_sp)
			- aligned_size;
	}
	return real_stack;
}

static __always_inline void kasan_stack_free(void *fake_stack,
					unsigned long size,
					void *real_stack, int class_id)
{
	if (likely(local_read(&current->kasan_fake_sp) && fake_stack != real_stack)) {
		local_sub(64UL << class_id, &current->kasan_fake_sp);
		poison_shadow(fake_stack, (64UL << class_id), KASAN_FREE_STACK);
	}
}


#define DEFINE_STACK_MALLOC(id)						\
void *__asan_stack_malloc_##id(size_t size, void *real_stack)		\
{									\
	return kasan_stack_malloc(size, real_stack, id);		\
}									\
EXPORT_SYMBOL(__asan_stack_malloc_##id);				\

#define DEFINE_STACK_FREE(id)                                                \
void __asan_stack_free_##id(void *fake_stack, size_t size, void *real_stack) \
{                                                                            \
	kasan_stack_free(fake_stack, size, real_stack, id);                  \
}                                                                            \
EXPORT_SYMBOL(__asan_stack_free_##id);                                       \


#define DECLARE_STACK_MALLOC_FREE(id) DEFINE_STACK_MALLOC(id) DEFINE_STACK_FREE(id)

DECLARE_STACK_MALLOC_FREE(0)
DECLARE_STACK_MALLOC_FREE(1)
DECLARE_STACK_MALLOC_FREE(2)
DECLARE_STACK_MALLOC_FREE(3)
DECLARE_STACK_MALLOC_FREE(4)
DECLARE_STACK_MALLOC_FREE(5)
DECLARE_STACK_MALLOC_FREE(6)
DECLARE_STACK_MALLOC_FREE(7)
DECLARE_STACK_MALLOC_FREE(8)
DECLARE_STACK_MALLOC_FREE(9)
DECLARE_STACK_MALLOC_FREE(10)

int __asan_option_detect_stack_use_after_return = 1;
EXPORT_SYMBOL(__asan_option_detect_stack_use_after_return);

#endif /* CONFIG_KASAN_UAR */

/* To shut up linker's complains */
void __asan_init_v3(void) {}
EXPORT_SYMBOL(__asan_init_v3);
void __asan_handle_no_return(void) {}
EXPORT_SYMBOL(__asan_handle_no_return);

#ifndef CONFIG_ARM_UNWIND
void __aeabi_unwind_cpp_pr1(void) {}
EXPORT_SYMBOL(__aeabi_unwind_cpp_pr1);
void __aeabi_unwind_cpp_pr0(void) {}
EXPORT_SYMBOL(__aeabi_unwind_cpp_pr0);
#endif

static int __init kasan_init(void)
{
	struct dentry *temp_dentry;
	int err = 0;

	kasan_dentry = debugfs_create_dir("kasan", NULL);
	if (IS_ERR(kasan_dentry)) {
		err = PTR_ERR(kasan_dentry);
		goto out;
	}

	temp_dentry = debugfs_create_u32("report_allowed", S_IRUSR | S_IWUSR,
			kasan_dentry, &kasan_report_allowed);
	if (IS_ERR(temp_dentry)) {
		err = PTR_ERR(temp_dentry);
		goto out_remove;
	}

	return 0;

out_remove:
	debugfs_remove_recursive(kasan_dentry);
out:
	pr_err("Unable to allocate debugfs dentry\n");

	return err;
}

module_init(kasan_init);
