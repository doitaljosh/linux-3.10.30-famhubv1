/*
 * kdbg-core.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee jung-seung(js07.lee@samsung.com)
 *
 * NOTE:
 *
 */
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <linux/module.h>

#include <t2ddebugd.h>
#include "t2ddbg-version.h"

static DECLARE_WAIT_QUEUE_HEAD(t2ddebugd_wait);
static DEFINE_SPINLOCK(t2ddebugd_queue_lock);

struct task_struct *tdebugd_tsk;

/* extern variable */
struct sec_tdbg_queue t2ddebugd_queue;
unsigned int t2ddebugd_running;

/*
 * Debugd event queue management.
 */
static inline int queue_empty(struct sec_tdbg_queue *q)
{
	return q->event_head == q->event_tail;
}

static inline void queue_get_event(struct sec_tdbg_queue *q,
				   struct sec_tdbg_input_event *event)
{
	BUG_ON(!event);
	q->event_tail = (q->event_tail + 1) % T2DDEBUGD_MAX_EVENTS;
	*event = q->events[q->event_tail];
}

static void t2d_print_register(unsigned int phy_addr, unsigned int *vir_addr, unsigned int count)
{
	int i;

	PRINT_T2D("\n");
	PRINT_T2D("[ address] [+%07X][+%07X][+%07X][+%07X]\n", 0x0, 0x4, 0x8, 0xC);
	for (i = 0; i < count; i++) {
		if ((i % 4) == 0)
			PRINT_T2D("[%08X]  ", phy_addr + (4 * i));
		PRINT_T2D("%08X  ", *vir_addr);
		if ((i % 4) == 3)
			PRINT_T2D("\n");
		vir_addr++;
	}
	PRINT_T2D("\n");
}

static int t2d_register_test(void)
{
	int i, event;
	unsigned int addr, value, count;
	unsigned int *vir_addr, *cur_addr;

	while(1) {
		PRINT_T2D("\n");
		PRINT_T2D("--- Read/Write register ------------------------\n");
		PRINT_T2D("1) Read.\n");
		PRINT_T2D("2) Write.\n");
		PRINT_T2D("99) Exit.\n");
		PRINT_T2D("Select> ");
		event = t2d_dbg_get_event_as_numeric(NULL, NULL);
		PRINT_T2D("\n");

		if ((event != 1) && (event != 2))
			break;

		PRINT_T2D("Address(hex, skip 0x)> ");
		addr = t2d_dbg_get_event_as_hex(NULL, NULL);
		PRINT_T2D("\n");

		if ((addr % 4) != 0) {
			PRINT_T2D("error!! address unit is 4bytes\n");
			continue;
		}

		PRINT_T2D("Count(x4 bytes)> ");
		count = t2d_dbg_get_event_as_numeric(NULL, NULL);
		PRINT_T2D("\n");

		if ((count < 1) || (count > 32)) {
			PRINT_T2D("error!! range of count : 1~32\n");
			continue;
		}

		vir_addr = (unsigned int *)ioremap(addr, 4 * count);
		cur_addr = vir_addr;

		t2d_print_register(addr, vir_addr, count);

		if (event == 2) 	{
			for (i = 0; i < count; i++) {
				PRINT_T2D("[%08X] hex_value> ", addr + (4 * i));
				value = t2d_dbg_get_event_as_hex(NULL, NULL);
				PRINT_T2D("\n");
				writel(value, cur_addr);
				cur_addr++;
			}

			PRINT_T2D("----- Result ------\n");
			t2d_print_register(addr, vir_addr, count);
 		}

		iounmap(vir_addr);
	}

	return 1;
}

void t2d_queue_add_event(struct sec_tdbg_queue *q, struct sec_tdbg_input_event *event)
{
	unsigned long flags;

	BUG_ON(!event);
	spin_lock_irqsave(&t2ddebugd_queue_lock, flags);
	q->event_head = (q->event_head + 1) % T2DDEBUGD_MAX_EVENTS;
	if (q->event_head == q->event_tail) {
		static int notified;

		if (notified == 0) {
			PRINT_T2D("\n");
			PRINT_T2D("t2ddebugd: an event queue overflowed\n");
			notified = 1;
		}
		q->event_tail = (q->event_tail + 1) % T2DDEBUGD_MAX_EVENTS;
	}
	q->events[q->event_head] = *event;
	spin_unlock_irqrestore(&t2ddebugd_queue_lock, flags);
	wake_up_interruptible(&t2ddebugd_wait);
}

/*
 * t2d_dbg_get_event_as_numeric
 *
 * Description
 * This API returns the input given by user as long value which can be
 * processed by t2ddebugd developers.
 * and If user enters a invalid value i.e. which can not be converted to string,
 * is_number is set to zero and entered value is returned as a string
 * i.e. event->input_string.
 *
 * Usage
 * struct sec_tdbg_input_event event;
 * int is_number;
 * int option;
 *
 * option = t2d_dbg_get_event_as_numeric(&event, &is_number);
 * if (is_number) {
 *	valid long value, process it i.e. option
 * } else {
 * printk("Invalid Value %s\n", event.input_string);
 * }
 */
long t2d_dbg_get_event_as_numeric(struct sec_tdbg_input_event *event, int *is_number)
{
	struct sec_tdbg_input_event temp_event;
	long value = 0;
	char *ptr_end = NULL;
	int is_number_flag = 1;
	int base = 10;

	t2d_dbg_get_event(&temp_event);

	/* convert to numeric */
	if (temp_event.input_string[0] == '0'
	    && temp_event.input_string[1] == 'x') {
		base = 16;	/* hex */
	} else {
		base = 10;	/* decimal */
	}
	value = simple_strtol(temp_event.input_string, &ptr_end, base);

	/* check if pure number */
	if (!ptr_end || *ptr_end || ptr_end == temp_event.input_string) {
		value = -1L;
		is_number_flag = 0;
	}

	/* output parameters */
	if (is_number)
		*is_number = is_number_flag;

	if (event)
		*event = temp_event;

	return value;
}
EXPORT_SYMBOL(t2d_dbg_get_event_as_numeric);

long t2d_dbg_get_event_as_hex(struct sec_tdbg_input_event *event, int *is_number)
{
	struct sec_tdbg_input_event temp_event;
	long value = 0;
	char *ptr_end = NULL;
	int is_number_flag = 1;
	int base = 10;

	t2d_dbg_get_event(&temp_event);

	base = 16;	/* hex */
	value = simple_strtol(temp_event.input_string, &ptr_end, base);

	/* check if pure number */
	if (!ptr_end || *ptr_end || ptr_end == temp_event.input_string) {
		value = -1L;
		is_number_flag = 0;
	}

	/* output parameters */
	if (is_number)
		*is_number = is_number_flag;

	if (event)
		*event = temp_event;

	return value;
}
EXPORT_SYMBOL(t2d_dbg_get_event_as_hex);

/*
 * t2d_dbg_get_event
 *
 * Description
 * This API returns the input given by user as event and no checks are
 * performed on input.
 * It is returned to user as string in i.e. event->input_string.
 *
 * Usage
 * struct sec_tdbg_input_event event;
 * t2d_dbg_get_event(&event);
 * process it event.input_string
 * printk("Value %s\n", event.input_string);
 */
void t2d_dbg_get_event(struct sec_tdbg_input_event *event)
{
	struct sec_tdbg_input_event temp_event;

	wait_event_interruptible(t2ddebugd_wait,
				 !queue_empty(&t2ddebugd_queue)
				 || kthread_should_stop());
	spin_lock_irq(&t2ddebugd_queue_lock);
	if (!queue_empty(&t2ddebugd_queue))
		queue_get_event(&t2ddebugd_queue, &temp_event);
	else
		temp_event.input_string[0] = '\0';

	spin_unlock_irq(&t2ddebugd_queue_lock);

	if (event)
		*event = temp_event;
}

/*
 *    t2ddebugd()
 */

struct tdbg_entry {
	const char *name;
	int (*execute) (void);
	void (*turnoff) (void);
	int reserved;
};

struct tdbg_base {
	unsigned int count;
	struct tdbg_entry entry[T2DDBGUGD_MENU_MAX];
};

static struct tdbg_base t_base;

int t2d_dbg_register(char *name, int num, int (*func)(void), void (*turnoff)(void))
{
	struct tdbg_entry *cur;

	if (!name || !func) {
		PRINT_T2D(KERN_ERR
			 "[ERROR] Invalid params, name %p, func %p !!!\n", name,
			 func);
		return -ENOMEM;
	}

	if ((t_base.count >= T2DDBGUGD_MENU_MAX) || (num >= T2DDBGUGD_MENU_MAX)) {
		PRINT_T2D(KERN_ERR
			 "[ERROR] Can not add debug function, count(%d), menunum(%d)!!!\n",
				 t_base.count, num);
		return -ENOMEM;
	}

	cur = &(t_base.entry[num]);
	if (cur->reserved) {
		PRINT_T2D(KERN_ERR "[ERROR] menu number %d is already reserved.\n", num);
		return -EPERM;
	}

	cur->name = name;
	cur->execute = func;
	cur->turnoff = turnoff;
	cur->reserved = 1;
	t_base.count++;

	return (t_base.count - 1);
}
EXPORT_SYMBOL(t2d_dbg_register);

int t2d_dbg_unregister(char *name, int num, int (*func)(void), void (*turnoff)(void))
{
	struct tdbg_entry *cur;

	if (num >= T2DDBGUGD_MENU_MAX) {
		PRINT_T2D(KERN_ERR
			 "[ERROR] out of range menunum(%d)!!!\n",
				num);
		return -EINVAL;
	}

	cur = &(t_base.entry[num]);
	if(!cur->reserved) {
		PRINT_T2D(KERN_ERR "[ERROR] menu number %d is not reserved.\n", num);
		return -EPERM;
	}

	if(cur->execute != func) {
		PRINT_T2D(KERN_ERR
			 "[ERROR] Invalid params, func %p !!!\n", func);
		return -EINVAL;
	}

	if(cur->turnoff != turnoff) {
		PRINT_T2D(KERN_ERR
			 "[ERROR] Invalid params, turnoff %p !!!\n", func);
		return -EINVAL;
	}

	cur->name = NULL;
	cur->execute = NULL;
	cur->turnoff = NULL;
	cur->reserved = 0;
	t_base.count--;

	return (t_base.count - 1);
}
EXPORT_SYMBOL(t2d_dbg_unregister);



static void t2ddebugd_menu(void)
{
	int i, count = 0;
	PRINT_T2D("\n");
	PRINT_T2D
		(" --- Menu --------------------------------------------------------------\n");
	PRINT_T2D(" Select Kernel Debugging Category. - %s\n", T2DDBG_VERSION);
	for (i = 0; i < T2DDBGUGD_MENU_MAX; i++) {
		if (t_base.entry[i].execute != NULL) {
			PRINT_T2D(" %-2d) %s.\n", i, t_base.entry[i].name);
			count++;
			if (count % 4 == 0)
				PRINT_T2D(" -----------------------------------------------------------------------\n");
		}
	}
	PRINT_T2D
	    (" -----------------------------------------------------------------------\n");
	PRINT_T2D(" 99) exit\n");
	PRINT_T2D
	    (" -----------------------------------------------------------------------\n");
	PRINT_T2D
	    (" -----------------------------------------------------------------------\n");
	PRINT_T2D
	    (" Samsung Electronic - VD Division - System S/W Lab1 - Tizen Part\n");
	PRINT_T2D
	    (" -----------------------------------------------------------------------\n");

}

static int t2ddebugd(void *arg)
{
	long event;
	unsigned int idx;
	int menu_flag = 1;
	int menu_found = 0;

	do {
		if (kthread_should_stop())
			break;

		if (menu_flag) {
			t2ddebugd_menu();
			PRINT_T2D("%s #> ", t2d_sched_serial);
		} else {
			menu_flag = 1;
		}

		event = t2d_dbg_get_event_as_numeric(NULL, NULL);
		PRINT_T2D("\n");

		/* exit */
		if (event == 99)
			break;

		/* execute operations */
		if ((event >= 0) && (event < T2DDBGUGD_MENU_MAX)) {
			if (t_base.entry[event].execute != NULL) {
				PRINT_T2D("[t2ddebugd] %ld. %s\n",
					 event,
					 t_base.entry[event].name);
				menu_flag = (*t_base.entry[event].execute)();
				menu_found = 1;
			}
		}

		if (!menu_found) {
			for (idx = 0; idx < T2DDBGUGD_MENU_MAX; idx++) {
				if (t_base.entry[idx].turnoff != NULL)
					(*t_base.entry[idx].turnoff) ();
			}
		}

		menu_found = 0;
	} while (1);

	PRINT_T2D("\n");
	PRINT_T2D("[t2ddebugd] Kdebugd Exit....");
	t2ddebugd_running = 0;

	return 0;
}

int t2ddebugd_start(void)
{
	int ret = 0;

	tdebugd_tsk = kthread_create(t2ddebugd, NULL, "t2ddebugd");
	if (IS_ERR(tdebugd_tsk)) {
		ret = PTR_ERR(tdebugd_tsk);
		tdebugd_tsk = NULL;
		return ret;
	}

	tdebugd_tsk->flags |= PF_NOFREEZE;
	wake_up_process(tdebugd_tsk);

	return ret;
}

static int __init t2ddebugd_init(void)
{
	t2d_dbg_register("Read/Write register", 0, t2d_register_test, NULL);

	return 0;
}

static void __exit t2ddebugd_cleanup(void)
{
}

module_init(t2ddebugd_init);
module_exit(t2ddebugd_cleanup);
