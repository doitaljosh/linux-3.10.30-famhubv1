/*
 * t2ddebugd.h
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee joon-seok (minsu082.lee@samsung.com)
 *
 * NOTE: this is based on kdebugd.h
 *
 */

#ifndef __T2DDEBUGD_H__
#define __T2DDEBUGD_H__

#include <linux/sched.h>
#include <linux/elf.h>


#define T2DDEBUGD_MAX_EVENTS	(64)
#define T2DDEBUGD_MAX_RAW_CHARS	(128)
#define T2DDEBUGD_MAX_CHARS	(64)
#define T2DDEBUGD_STRING	"t2ddebugd"
#define T2DDBGUGD_MENU_MAX	(64)

/* Input is received as a string. */
struct sec_tdbg_input_event {
    /* null terminated string */
    char input_string[T2DDEBUGD_MAX_CHARS];
};


/*
 * Events (results of Get Kdebugd Event)
 */
struct sec_tdbg_queue {
	unsigned int            	event_head;
	unsigned int            	event_tail;
	struct sec_tdbg_input_event	events[T2DDEBUGD_MAX_EVENTS];
};

extern char t2d_sched_serial[10];
extern unsigned int t2ddebugd_running;
extern struct sec_tdbg_queue t2ddebugd_queue;
extern int t2ddbgd_outside_print;

int t2ddebugd_start(void);

void t2d_queue_add_event(struct sec_tdbg_queue *q, struct sec_tdbg_input_event *event);
long t2d_dbg_get_event_as_numeric(struct sec_tdbg_input_event *event, int *is_number);
long t2d_dbg_get_event_as_hex(struct sec_tdbg_input_event *event, int *is_number);
void t2d_dbg_get_event(struct sec_tdbg_input_event *event);

int t2d_dbg_register(char *name, int num, int (*func)(void), void (*turnoff)(void));
int t2d_dbg_unregister(char *name, int num, int (*func)(void), void (*turnoff)(void));

#define  PRINT_T2D(fmt , args ...)  	 printk(fmt, ##args)

#endif	/* __T2DDEBUGD_H__ */
