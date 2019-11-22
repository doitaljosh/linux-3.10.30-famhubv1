/*
 * kdbg_fd_debug.c
 *
 * Copyright (C) 2013 Samsung Electronics
 *
 * Created by rajesh.b1@samsung.com
 *
 * NOTE: Dynamically on/off prints of file fd debugging.
 *
 */

#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <kdebugd/kdebugd.h>

/* debug status: file fd debug status (by deault disabled) */
static atomic_t g_fd_debug_status = ATOMIC_INIT(0);

int kdbg_fd_debug_status(void)
{
	return  atomic_read(&g_fd_debug_status);
}

int kdbg_fd_debug_handler(void)
{

	int operation = 0;

	while (1) {
		PRINT_KD("-----------------------------------\n");
		PRINT_KD("Current FD Debug Status: %s\n",
			atomic_read(&g_fd_debug_status) ? "ENABLE" : "DISABLE");
		PRINT_KD("-------------------------------------\n");
		PRINT_KD("1.  For Toggle Status \n");
		PRINT_KD("99. For Exit\n");
		PRINT_KD("--------------------------------------\n");
		PRINT_KD("Select Option==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);

		if (operation == 1) {
			atomic_set(&g_fd_debug_status, !atomic_read(&g_fd_debug_status));
			break;
		} else if (operation == 99) {
			break;
		} else {
			PRINT_KD("Invalid Option..\n");
		}
	}

	return 1;
}

