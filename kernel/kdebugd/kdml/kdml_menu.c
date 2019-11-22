/*
 * kdml_menu.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/kernel.h>
#include <kdebugd.h>
#include "kdml/kdml_packet.h"
#include "kdml/kdml.h"
#include "kdml/kdml_internal.h"
#include <linux/mm.h>
#include "kdml/kdml_meminfo.h"
#include <linux/kallsyms.h>
#include <linux/slab.h>

static char *kdml_mode_str[KDML_MODE_MAX] = {
	"Not Running",
	"Running"
};

static char *get_kdml_mode_str(void)
{
	int mode = kdml_get_current_mode();
	if ((mode < KDML_MODE_NONE)
			|| (mode >= KDML_MODE_MAX))
		mode = KDML_MODE_NONE;
	return kdml_mode_str[mode];
}

static void kdml_main_menu(void)
{
	PRINT_KD("\n");
	PRINT_KD("Current KDML Mode: %s\n", get_kdml_mode_str());
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("%2d) KDML: Trace Start\n", KDML_MENU_TRACE_START);
	PRINT_KD("%2d) KDML: Trace Stop\n", KDML_MENU_TRACE_STOP);
	PRINT_KD("%2d) KDML: Caller Based Summary Report\n", KDML_MENU_TRACE_CALLER_REP_SUMMARY);
	PRINT_KD("%2d) KDML: Module Based Summary Report\n", KDML_MENU_TRACE_MODULE_REP_SUMMARY);
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("%2d) KDML Counter: Total Allocation Summary\n", KDML_MENU_TRACE_SUMMARY);
	PRINT_KD("%2d) KDML Counter: Kernel Allocation Summary\n", KDML_MENU_KERNEL_TRACE_SUMMARY);
	PRINT_KD("%2d) KDML Counter: User Allocation Summary\n", KDML_MENU_USER_TRACE_SUMMARY);
	PRINT_KD("%2d) KDML: Slab info\n", KDML_MENU_SNAPSHOT_SLABINFO);
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("%2d) KDML: Clear Stats\n", KDML_MENU_CLEAR_STATS);
	PRINT_KD("%2d) KDML: Add Filter for Backtrace\n", KDML_MENU_FILTER_FUNC);
	PRINT_KD("%2d) KDML: Restrict Entries per caller\n", KDML_MENU_RESTRICT);
	PRINT_KD("99) KDML: Exit Menu\n");
	PRINT_KD("--------------------------------------------------\n");
	PRINT_KD("\n");
	PRINT_KD("Select Option==>  ");
}

unsigned long kdml_backtrace_addr;
static unsigned long get_addr_from_buffer(void)
{
	debugd_event_t event;
	unsigned long func_offset;
	unsigned long func_addr;
	char namebuf[KSYM_NAME_LEN];

	unsigned long symbolsize, offset;
	char *modname;

	/* TODO: we can get the address/offset in 1 go, instead,, */
	PRINT_KD("Enter Function Name ==> ");
	debugd_get_event(&event);
	PRINT_KD("\n");
	func_addr = kallsyms_lookup_name(event.input_string);
	if (!func_addr) {
		ddebug_err("Invalid Function Name \"%s\".. Resetting to 0.\n",
				event.input_string);
		return 0;
	}

	/* this shouldn't fail as we calculated address based on func. name */
	kallsyms_lookup(func_addr, &symbolsize, &offset,
			&modname, namebuf);
	ddebug_info("symbol lookup: size: %lx, name: %s, func: %s\n",
			symbolsize, offset, modname, namebuf);
	PRINT_KD("Enter offset (max 0x%lx) ==> ", symbolsize);
	func_offset = (unsigned long)debugd_get_event_as_numeric(&event, NULL);
	PRINT_KD("\n");

	if (func_offset >= symbolsize) {
		ddebug_print("Err: Invalid offset!!\n");
		return 0;
	}

	func_addr += func_offset;

	PRINT_KD("Backtrace Address: %pS\n", (void *)func_addr);
	return func_addr;

}

#ifdef CONFIG_SLABINFO
/* current kernel has 111 caches, this should be sufficient */
#define MAX_CACHE_COUNT 200
static void kdml_print_slabinfo(void)
{
	struct kdml_slab_info_line *slabinfo_arr;
	int cache_count = MAX_CACHE_COUNT;
	int result, ret;
	int i;
	size_t slab_info_mem = (size_t)cache_count * sizeof(struct kdml_slab_info_line);

	slabinfo_arr = kmalloc(slab_info_mem, GFP_KERNEL | __GFP_NOTRACK);
	if (!slabinfo_arr) {
		ddebug_err("Error in allocating memory..\n");
		return;
	}

	ret = kdml_get_cache_list(slabinfo_arr, cache_count, &result);
	if (ret < 0) {
		ddebug_err("Error in getting cache list..\n");
		goto free_slabinfo;
	} else if (cache_count == result) {
		ddebug_err("Too many caches.. only tracking %d\n", cache_count);
	} else {
		/* Normal case: MAX_CACHE_COUNT should be sufficient */
		cache_count = result;
	}

	WARN_ON(!cache_count);


	ddebug_print("# name                     active  objs  objsize "
		 "obj/slab page/slab num_slabs\n");
	for (i = 0; i < cache_count; i++) {
		ddebug_print("%-26s %6u %6u %7u %8u %9d %8u\n",
				slabinfo_arr[i].name,
				slabinfo_arr[i].active_objs,
				slabinfo_arr[i].num_objs,
				slabinfo_arr[i].obj_size,
				slabinfo_arr[i].objs_per_slab,
				(1 << slabinfo_arr[i].cache_order),
				slabinfo_arr[i].num_slabs);
	}

free_slabinfo:
	kfree(slabinfo_arr);
}
#else
static inline void kdml_print_slabinfo(void)
{
	ddebug_print("This feature depends on CONFIG_SLABINFO..\n");
	return;
}
#endif

int kdml_menu(void)
{
	int operation = 0;

	do {
		if (!((operation >= KDML_MENU_TRACE_SUMMARY)
					&& (operation <= KDML_MENU_USER_TRACE_SUMMARY)))
			kdml_main_menu();
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (operation) {

		case KDML_MENU_TRACE_START:
			/* print detailed information */
			kdml_set_current_mode(KDML_MODE_RUNNING);
			PRINT_KD("Detailed Tracing Started...\n");
			break;

		case KDML_MENU_TRACE_STOP:
			/* print detailed information */
			kdml_set_current_mode(KDML_MODE_NONE);
			PRINT_KD("Detailed Tracing Stopped...\n");
			break;

		case KDML_MENU_TRACE_CALLER_REP_SUMMARY:
			/* print detailed information */
			kdml_print_caller_summary_info();
			break;

		case KDML_MENU_TRACE_MODULE_REP_SUMMARY:
			/* not implemented */
			kdml_print_module_summary_info();
			break;

		case KDML_MENU_TRACE_SUMMARY:
			kdml_ctr_meminfo_summary_on_off();
			break;

		case KDML_MENU_KERNEL_TRACE_SUMMARY:
			kdml_ctr_kernel_meminfo_on_off();
			break;

		case KDML_MENU_USER_TRACE_SUMMARY:
			kdml_ctr_user_meminfo_on_off();
			break;

		case KDML_MENU_SNAPSHOT_SLABINFO:
			/* slab information */
			/* print_slab_info(); */
			kdml_print_slabinfo();
			break;

		case KDML_MENU_CLEAR_STATS:
			if (kdml_get_current_mode() == KDML_MODE_NONE) {
				kdml_clear_allocations();
			} else {
				PRINT_KD("!!!Can't clear Stats while running..!!!\n");
				PRINT_KD("  ...Stop the Tracing first\n");
			}
			break;

		case KDML_MENU_FILTER_FUNC:
			kdml_backtrace_addr = get_addr_from_buffer();
			break;

		case KDML_MENU_RESTRICT:
			if (kdml_get_current_mode() == KDML_MODE_NONE) {
				int entries = kdml_global_conf.entries_per_caller;
				PRINT_KD("Enter number of entries (current: %d, 0 for unrestricted) ==> ",
						entries);
				entries = debugd_get_event_as_numeric(NULL, NULL);
				PRINT_KD("\n");
				if (entries < 0) {
					PRINT_KD("Error in config. Keeping default...\n");
				}
				else {
					if (entries == 0)
						PRINT_KD("No restriction per caller\n");

					kdml_global_conf.entries_per_caller = entries;
				}
			} else {
				PRINT_KD("  ...Stop the Tracing first\n");
			}
			break;

		case KDML_MENU_EXIT:
			break;

		default:
			PRINT_KD("\n");
			/* disable the counters print messages */
			kdml_ctr_meminfo_turnoff_messages();
			break;
		}
	} while (operation != KDML_MENU_EXIT);
	PRINT_KD("KDML Debug menu exit....\n");

	/* return value is true - to show the kdebugd menu options */
	return 1;
}

