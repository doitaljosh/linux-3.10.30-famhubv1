/*
 * kdbg-func.c
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by lee jung-seung(js07.lee@samsung.com)
 *
 * Created by lee jung-seung (js07.lee@samsung.com)
 *
 * 2009-11-17 : Added, Virt to physical ADDR converter.
 * 2009-11-20 : Modified, show_task_state_backtrace for
 * taking real backtrace on kernel.
 * NOTE:
 *
 */

#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <asm/pgtable.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/list.h>

#include <sec_topthread.h>

#include <kdebugd.h>
#include "kdbg_arch_wrapper.h"
#include "kdbg_util.h"
#include "sec_workq.h"

#include <linux/bootmem.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
#include "sec_perfcounters.h"
#endif

#ifdef CONFIG_ELF_MODULE
#include "elf/kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_ADVANCE_OPROFILE
#include "aop/aop_oprofile.h"
#endif /* CONFIG_ADVANCE_OPROFILE */

#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
#include <linux/err.h>
#include <linux/highmem.h>
#endif

#ifdef CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#include <agent/agent_core.h>
#endif
#ifdef CONFIG_KDML
#include "kdml/kdml_packet.h"
#include "kdml/kdml.h"
#endif

#ifdef CONFIG_KDEBUGD_MISC
#include <linux/nmi.h>
#endif

struct proc_dir_entry *kdebugd_cpu_dir;

#ifdef CONFIG_ELF_MODULE

/* Circular Array for containing Program Counter's and time info*/
struct sec_kdbg_pc_info {
	unsigned int pc;	/* program counter value */
	struct timespec pc_time;	/* time of the program counter */
	unsigned int cpu;
	pid_t pid;      /* pid value of a thread*/
	pid_t tgid;     /* tgid value of a thread*/
};
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#include <linux/nmi.h>
#include <linux/sched.h>
#include <linux/rwlock.h>
#include <agent/agent_packet.h>
#include <agent/kern_ringbuffer.h>
#include <agent/agent_kdbg_struct.h>
#include <agent/tvis_agent_packet.h>
#include <agent/tvis_agent_cmds.h>

#endif

#ifndef OFFSET_MASK
#define OFFSET_MASK ((unsigned long)(PAGE_SIZE) - 1UL)
#endif

#define OFFSET_ALIGN_MASK (OFFSET_MASK & ~(0x3UL))
#define DUMP_SIZE 0x400
#define P2K(x) ((x) << (PAGE_SHIFT - 10))

#if defined(CONFIG_KDEBUGD_MISC)
enum kdbg_print_menu {
	KDBG_PRINT_PROCESS = 0,
	KDBG_PRINT_FIRST_THREAD,
	KDBG_PRINT_LAST_THREAD
};

/* Different states of threads */
static const char kdbg_stat_name[] = TASK_STATE_TO_CHAR_STR;

/* structure to maintain a list of threads */
struct kdbg_thlist {
	pid_t tid;
	pid_t tgid;
};

/* function to swap the tid and tgid of two threads*/
static void swap_threadlist_info(void *va, void *vb)
{
	struct kdbg_thlist *a = va, *b = vb;
	pid_t ttid = a->tid, ttgid = a->tgid;
	a->tid = b->tid;
	a->tgid = b->tgid;
	b->tid = ttid;
	b->tgid = ttgid;
}

/* kdebugd Functions */

static void show_task_prio(struct task_struct *p, enum kdbg_print_menu flag)
{
	unsigned int state;
	unsigned int temp;
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	unsigned long stack_usage = 0;
	unsigned long stack_size = 0;
	struct mm_struct *mm = NULL;

	if (p == NULL)
		return;

	/* Both state and exit_state can't be -ve number so can be converted to unsigned int */
	state = (unsigned int)((p->state & TASK_REPORT) | p->exit_state);
	state =  state ? (temp = __ffs(state)) + 1 : 0;
	switch (flag) {
	case KDBG_PRINT_PROCESS:
		PRINT_KD(KERN_INFO "%-16.16s", p->comm);
		break;
	case KDBG_PRINT_FIRST_THREAD:
		PRINT_KD(KERN_INFO " |--%-16.16s", p->comm);
		break;
	case KDBG_PRINT_LAST_THREAD:
		PRINT_KD(KERN_INFO " `--%-16.16s", p->comm);
		break;
	default:
		break;
	}
#ifdef CONFIG_SMP
	if (flag == KDBG_PRINT_PROCESS)
		PRINT_KD(KERN_CONT "    [%d] ", task_cpu(p));
	else if (flag == KDBG_PRINT_FIRST_THREAD || flag == KDBG_PRINT_LAST_THREAD)
		PRINT_KD(KERN_CONT "[%d] ", task_cpu(p));
#endif
	PRINT_KD(" %c", state < sizeof(kdbg_stat_name) ? kdbg_stat_name[state] : '?');

#if (BITS_PER_LONG == 32)
	PRINT_KD(" %08lX ", KSTK_EIP(p));
#else
	PRINT_KD(" %016lx ", KSTK_EIP(p));
#endif
	mm = get_task_mm(p);
	if (mm) {
		regs = task_pt_regs(p);
		vma = find_vma(p->mm, p->user_ssp);
		if (vma != NULL) {
#if defined(CONFIG_ARM)
			stack_usage =  vma->vm_end - regs->ARM_sp; /* MF?? */
#elif defined(CONFIG_MIPS)
			stack_usage =  vma->vm_end - regs->regs[29];
#else
#error "Architecture Not Supported!!!"
#endif
			stack_size  =  vma->vm_end - vma->vm_start;
			PRINT_KD("%6lu/%06lu    ",
					stack_usage >> 10,
					stack_size  >> 10);
		} else {
			PRINT_KD(" --------  ");
		}
	} else {

		if (p->state == TASK_DEAD || p->state == EXIT_DEAD ||
				p->state ==	EXIT_ZOMBIE) {
			PRINT_KD(KERN_CONT "       [dead]    ");
		} else {
			PRINT_KD("     [kernel]    ");
		}
	}
	if (mm)
		mmput(mm);
	PRINT_KD("%4d %4d  %4d    %u       %u\n",
			p->pid,
			p->prio,
			p->static_prio,
			p->policy,
			p->rt.time_slice);
}

int show_state_prio(void)
{
	struct task_struct *g, *p, *ts;
#ifdef CONFIG_PREEMPT_RT
	int do_unlock = 1;
#endif
	struct kdbg_thlist *th_list = NULL;
	int i = 0, j = 0, k = 0, thread_count = 0;

	PRINT_KD("\n");
#if (BITS_PER_LONG == 32)
	PRINT_KD("                             user(kb/kb)                       \n");
	PRINT_KD("  task         ");

#ifdef CONFIG_SMP
	PRINT_KD("[CPU]      ");
#endif
	PRINT_KD("PC    stack_usage  pid prio sprio policy time_slice[x%dms]\n", 1000/HZ);

#else
	PRINT_KD("                            kb/kb                         \n");
	PRINT_KD("  task         ");
#ifdef CONFIG_SMP
	PRINT_KD("[CPU]      ");
#endif
	PRINT_KD("PC    stack_usage  pid prio sprio policy time_slice[x%dms]\n", 1000/HZ);
#endif
	if (kdbg_get_task_status()) { /* Process mode */
		/* No of threads in system */
		thread_count = nr_threads;

		/* Allocating a memory block to store pids and tgids for all threads */
		th_list = kmalloc(sizeof(struct kdbg_thlist) * (unsigned int)thread_count, GFP_KERNEL);
		/* Check nullity for th_list*/
		if (th_list == NULL) {
			PRINT_KD("unable to allocate memory !!\n");
			return 0;
		}

#ifdef CONFIG_PREEMPT_RT
		if (!read_trylock(&tasklist_lock)) {
			PRINT_KD("hm, tasklist_lock write-locked.\n");
			PRINT_KD("ignoring ...\n");
			do_unlock = 0;
		}
#else
		read_lock(&tasklist_lock);
#endif
		rcu_read_lock();
		i = 0;
		do_each_thread(g, p) {
			if (i >= thread_count)
				break;
			th_list[i].tid = p->pid;
			th_list[i].tgid = p->tgid;
			/* Check for i==thread_count */
			i++;
		} while_each_thread(g, p);
		rcu_read_unlock();

		thread_count = i-1;

#ifdef CONFIG_PREEMPT_RT
		if (do_unlock)
#endif
		read_unlock(&tasklist_lock);
		debug_show_all_locks();
		PRINT_KD("---------------------------------------------------------------------------------------------\n");
		for (j = 0; j < thread_count - 1; j++) {
			for (k = j+1; k < thread_count; k++) {
				if (th_list[j].tgid > th_list[k].tgid)
					swap_threadlist_info(&th_list[j], &th_list[k]);
			}
		}
		for (i = 0; i < thread_count; i++) {
			rcu_read_lock();
			ts = find_task_by_pid_ns(th_list[i].tid, &init_pid_ns);
			if (ts)
				get_task_struct(ts);
			rcu_read_unlock();
			if (ts == NULL)
				continue;

			if (th_list[i].tgid == th_list[i].tid) {
				show_task_prio(ts, KDBG_PRINT_PROCESS);
			} else {
				if (i == thread_count - 1)
					show_task_prio(ts, KDBG_PRINT_LAST_THREAD);
				else {
					if (th_list[i+1].tgid == th_list[i+1].tid)
						show_task_prio(ts, KDBG_PRINT_LAST_THREAD);
					else
						show_task_prio(ts, KDBG_PRINT_FIRST_THREAD);
				}
			}

			put_task_struct(ts);
		}

		/*free the allocated memory block*/
		kfree(th_list);

	} else { /* Thread Mode*/
#ifdef CONFIG_PREEMPT_RT
		if (!read_trylock(&tasklist_lock)) {
			PRINT_KD("hm, tasklist_lock write-locked.\n");
			PRINT_KD("ignoring ...\n");
			do_unlock = 0;
		}
#else
		read_lock(&tasklist_lock);
#endif
		rcu_read_lock();
		do_each_thread(g, p) {
			show_task_prio(p, KDBG_PRINT_PROCESS);
		} while_each_thread(g, p);
		rcu_read_unlock();
#ifdef CONFIG_PREEMPT_RT
		if (do_unlock)
#endif
		read_unlock(&tasklist_lock);
	}

	task_state_help();
	return 1;
}

static inline struct task_struct *eldest_child(struct task_struct *p)
{
	if (list_empty(&p->children))
		return NULL;
	return list_entry(p->children.next, struct task_struct, sibling);
}

/* Info: For functions older_sibling() and younger_sibling():
 * Kernel stores sibling information only for processes and
 * not for threads. In case of threads, task_struct's sibling
 * is an empty double link list. Hence, in case of threads
 * p->sibling link list is empty i.e.
 * (p->sibling.prev == &p->sibling) and we return NULL*/
static inline struct task_struct *older_sibling(struct task_struct *p)
{
	if ((p->sibling.prev == &p->parent->children) ||
			(p->sibling.prev == &p->sibling))
		return NULL;
	return list_entry(p->sibling.prev, struct task_struct, sibling);
}

static inline struct task_struct *younger_sibling(struct task_struct *p)
{
	if ((p->sibling.next == &p->parent->children) ||
			(p->sibling.next == &p->sibling))
		return NULL;
	return list_entry(p->sibling.next, struct task_struct, sibling);
}

static void kdbg_show_task(struct task_struct *p, enum kdbg_print_menu flag)
{
	unsigned long free = 0;
	unsigned int state;
	unsigned int temp;
	struct mm_struct *mm = NULL;

	struct task_struct *relative = NULL;
	/* Both state and exit_state can't be -ve number so can be converted to unsigned int */
	state = (unsigned int)((p->state & TASK_REPORT) | p->exit_state);
	state = state ? (temp = __ffs(state)) + 1 : 0;

	switch (flag) {
	case KDBG_PRINT_PROCESS:
		PRINT_KD(KERN_INFO "%-16.16s", p->comm);
		break;
	case KDBG_PRINT_FIRST_THREAD:
		PRINT_KD(KERN_INFO " |--%-16.16s", p->comm);
		break;
	case KDBG_PRINT_LAST_THREAD:
		PRINT_KD(KERN_INFO " `--%-16.16s", p->comm);
		break;
	default:
		break;
	}
#ifdef CONFIG_SMP
	if (flag == KDBG_PRINT_PROCESS)
		PRINT_KD(KERN_CONT "    [%d]", task_cpu(p));
	else if (flag == KDBG_PRINT_FIRST_THREAD || flag == KDBG_PRINT_LAST_THREAD)
		PRINT_KD(KERN_CONT "[%d]", task_cpu(p));
#endif
	PRINT_KD(KERN_CONT " %c [%p]", state < sizeof(kdbg_stat_name) ? kdbg_stat_name[state] : '?', p);

#if BITS_PER_LONG == 32
	if (state == TASK_RUNNING)
		PRINT_KD(KERN_CONT " running  ");
	else
		PRINT_KD(KERN_CONT " %08lx ", thread_saved_pc(p));
#else
	if (state == TASK_RUNNING)
		PRINT_KD(KERN_CONT "  running task    ");
	else
		PRINT_KD(KERN_CONT " %016lx ", thread_saved_pc(p));
#endif

#ifdef CONFIG_DEBUG_STACK_USAGE
	free = stack_not_used(p);
#endif

	PRINT_KD(KERN_CONT "%5lu %5d 0x%08lx %6d", free,
			task_pid_nr(p),
			(unsigned long)task_thread_info(p)->flags,
			task_pid_nr(p->real_parent));

	relative = eldest_child(p);
	if (relative)
		PRINT_KD("%7d", relative->pid);
	else
		PRINT_KD("       ");

	relative = younger_sibling(p);
	if (relative)
		PRINT_KD("%8d", relative->pid);
	else
		PRINT_KD("        ");

	relative = older_sibling(p);
	if (relative)
		PRINT_KD("%8d", relative->pid);
	else
		PRINT_KD("        ");

	mm = get_task_mm(p);
	if (!mm) {
		if (p->state == TASK_DEAD || p->state == EXIT_DEAD ||
				p->state == EXIT_ZOMBIE) {
			PRINT_KD(KERN_CONT "      (dead)\n");
		} else {
			PRINT_KD(KERN_CONT "      (kernel thread)\n");
		}
	} else
		PRINT_KD(KERN_CONT "      (user thread)\n");

	if (mm)
		mmput(mm);

#if defined(CONFIG_SHOW_TASK_STATE)
	if (!kdebugd_nobacktrace) {
		show_stack(p, NULL);
		PRINT_KD("-------------------------------------------------------------------------------------\n");
	}
#endif
}

static void kdbg_show_state(void)
{
	struct task_struct *g, *p, *ts;
	struct kdbg_thlist *th_list = NULL;
	int i = 0, j = 0, k = 0, thread_count = 0;

#if BITS_PER_LONG == 32
	PRINT_KD(KERN_INFO
			"                                                                                  sibling\n");
	PRINT_KD(KERN_INFO "  task         ");
#ifdef CONFIG_SMP
	PRINT_KD(KERN_CONT "    [CPU]  ");
#else
	PRINT_KD(KERN_CONT "    ");
#endif
	PRINT_KD(KERN_CONT "task_struct   PC    stack  pid  flag         father child   younger older\n");
	PRINT_KD("---------------------------------------------------------------------------------------------------\n");
#else
	PRINT_KD(KERN_INFO
			"                                                                                       sibling\n");
	PRINT_KD(KERN_INFO "  task         ");
#ifdef CONFIG_SMP
	PRINT_KD(KERN_CONT "    [CPU]  ");
#endif
	PRINT_KD(KERN_CONT "task_struct   PC    stack  pid  flag         father child   younger older\n");
	PRINT_KD("---------------------------------------------------------------------------------------------------\n");


#endif
	if (kdbg_get_task_status()) { /* Process mode */

		/* No of threads in system */
		thread_count = nr_threads;

		/* Allocating a memory block to store pids and tgids for all threads */
		th_list = kmalloc(sizeof(struct kdbg_thlist) * (unsigned int)thread_count, GFP_KERNEL);

		/* Check nullity for th_list*/
		if (th_list == NULL) {
			PRINT_KD("unable to allocate memory !!\n");
			return;
		}

		rcu_read_lock();
		i = 0;
		do_each_thread(g, p) {
			if (i >= thread_count)
				break;
			th_list[i].tid = p->pid;
			th_list[i].tgid = p->tgid;
			/* Check for i == thread_count */
			i++;
		} while_each_thread(g, p);
		rcu_read_unlock();

		thread_count = i-1;

		/* sort the list th_list on the basis of tgid */
		for (j = 0; j < thread_count - 1; j++) {
			for (k = j+1; k < thread_count; k++) {
				if (th_list[j].tgid > th_list[k].tgid)
					swap_threadlist_info(&th_list[j], &th_list[k]);
			}
		}

		/* calling the corresponding function based on process/thread */
		for (i = 0; i < thread_count; i++) {
			rcu_read_lock();
			ts = find_task_by_pid_ns(th_list[i].tid, &init_pid_ns);
			if (ts)
				get_task_struct(ts);
			rcu_read_unlock();
			if (ts == NULL)
				continue;

			if (th_list[i].tgid == th_list[i].tid)
				kdbg_show_task(ts, KDBG_PRINT_PROCESS);

			else {
				if (i == thread_count - 1)
					kdbg_show_task(ts, KDBG_PRINT_LAST_THREAD);
				else {
					if (th_list[i+1].tgid == th_list[i+1].tid)
						kdbg_show_task(ts, KDBG_PRINT_LAST_THREAD);
					else
						kdbg_show_task(ts, KDBG_PRINT_FIRST_THREAD);
				}
			}

			put_task_struct(ts);
		}

		/* free the allocated memmory block*/
		kfree(th_list);

	} else { /* Thread Mode */
		rcu_read_lock();
		do_each_thread(g, p) {
			kdbg_show_task(p, KDBG_PRINT_PROCESS);
		} while_each_thread(g, p);
		rcu_read_unlock();
	}
}
#endif


unsigned int kdebugd_nobacktrace;

int get_user_stack(struct task_struct *task,
	unsigned int **us_content, unsigned long *start, unsigned long *end)
{
	struct pt_regs *regs;
	struct vm_area_struct *vma;
	int no_of_us_value = 0;
	struct mm_struct *mm = NULL;

	regs = task_pt_regs(task);

	mm = get_task_mm(task);
	vma = find_vma(task->mm, task->user_ssp);
	if (vma) {

		unsigned long bottom = regs->ARM_sp;
		unsigned long top = task->user_ssp;
		unsigned long p = bottom & ~(31UL);
		mm_segment_t fs;
		unsigned int *tmp_user_stack;

		*start = bottom;
		*end = top;

		*us_content = (unsigned int *)vmalloc(
				(top - bottom) * sizeof (unsigned int));

		if (!*us_content) {
			printk ("%s %d> No memory to build user stack\n",
					__FUNCTION__, __LINE__);
			if (mm)
				mmput(mm);
			return no_of_us_value;
		}

		printk("stack area (0x%08lx ~ 0x%08lx)\n", vma->vm_start, vma->vm_end);
		printk("User stack area (0x%08lx ~ 0x%08lx)\n",
				regs->ARM_sp, vma->vm_end);

		/*
		 * We need to switch to kernel mode so that we can use __get_user
		 * to safely read from kernel space.  Note that we now dump the
		 * code first, just in case the backtrace kills us.
		 */
		fs = get_fs();
		set_fs(KERNEL_DS);

		/* copy the pointer  to tmp, to fill the app cookie value */
		tmp_user_stack = *us_content;

		for (p = bottom & ~(31UL); p < top; p += 4) {
			if (!(p < bottom || p >= top)) {
				unsigned int val;
				__get_user(val, (unsigned long *)p);
				if (val) {
					*(tmp_user_stack+no_of_us_value++) = p & 0xffff;
					*(tmp_user_stack+no_of_us_value++) = val;
					/*printk("<%08x - %08x> ",
					 *(tmp_user_stack+no_of_us_value++), val);*/
				}
			}
		}

		set_fs(fs);

	}
	if (mm)
		mmput(mm);
	return no_of_us_value;
}


/*
* kdbg_dump_mem:
* to dump user stack
*/
void kdbg_dump_mem(const char *str, unsigned long bottom, unsigned long top)
{
	unsigned long p = bottom & ~(31UL);
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);
	PRINT_KD("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	/* We print for 8 addresses (8*4 = 32) in a line.
	 * If address is outside the limit, blank is printed
	 */
	for (p = bottom & ~(31UL); p < top;) {
		PRINT_KD("%04lx: ", p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				PRINT_KD("         ");
			else {
				__get_user(val, (unsigned long *)p);
				PRINT_KD("%08x ", val);
			}
		}
		PRINT_KD("\n");
	}
	set_fs(fs);
}

/* Fill agent thread list structure and write on RB */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#define THLIST_PACKET_DROP_FREQ	10

int kdbg_agent_thread_info(void)
{
	unsigned state;
	struct task_struct *g, *p;
	struct agent_thread_list list;
	size_t write_size = 0;
	struct tool_header t_hdr;
	/* nr_threads can't be negative hence can be converted to uint32_t */
	uint32_t num_of_threads = (uint32_t)nr_threads;
	uint32_t sec;
	int loop_cnt = 0;
	size_t total_write_size = 0;
	size_t available = 0;
	int err_count = 0;
	static int print_count;

	/* This will be the maximum Threadlist data size to be written on RB */
	total_write_size = TOOL_HDR_SIZE + (num_of_threads * sizeof(list)) +
				(2 * sizeof(uint32_t));

	/* Acquire RB write lock, as other threads may try to interfare */
	mutex_lock(&g_agent_rb_lock);

	available = kdbg_ringbuffer_write_space();
	while (available < total_write_size) {
		mutex_unlock(&g_agent_rb_lock);
		err_count++;
		print_count++;
		msleep(20);
		/* Keeping the priority of threadlist twice than
		 * any other data. Check available space for 10
		 * times i.e. 0.2sec, then drop the packet */
		if (err_count == THLIST_PACKET_DROP_FREQ) {
			/* Print every 30 sec */
			if (print_count == AGENT_ERROR_PRINT_FREQ) {
				PRINT_KD("[Agent Threadlist Write Error] ");
				PRINT_KD("Available: %d, need: %d\n", available,
						total_write_size);
				print_count = 0;
			}
			return -1;
		}

		mutex_lock(&g_agent_rb_lock);
		available = kdbg_ringbuffer_write_space();
	}

	kdbg_ringbuffer_reset_adv_writer();

	/* Advance rb writer to write TOOL_HDR later on */
	write_size += TOOL_HDR_SIZE;
	kdbg_ringbuffer_inc_adv_writer(write_size);
	/* Copy second */
	sec  = kdbg_get_uptime();
	agent_adv_write((void *)&sec, sizeof(uint32_t));
	/* Copy num_of_threads */
	agent_adv_write((void *)&num_of_threads, sizeof(uint32_t));
	write_size += 2 * sizeof(uint32_t);

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {

		state = (unsigned)((p->state & TASK_REPORT) | p->exit_state);
		state = state ? (unsigned)((unsigned long)__ffs(state) + 1UL) : 0U;


#ifdef CONFIG_SMP
		list.cpu = (uint8_t)task_cpu(p);
#else
		list.cpu = 0;
#endif /* CONFIG_SMP */

		list.status = (uint16_t)state;
		list.cpu = (uint8_t)task_cpu(p);
		list.policy = (uint8_t)p->policy;
		memcpy(list.info.name, p->comm, THREAD_NAME_MAX_SIZE);
		list.info.pid = (uint32_t)task_pid_nr(p);
		list.info.tgid = (uint32_t)p->tgid;

		/*Write each thread information in RB first*/
		agent_adv_write((void *)&list, sizeof(struct agent_thread_list));
		write_size += sizeof(struct agent_thread_list);

		if (++loop_cnt == nr_threads)
			goto loop_break;

	} while_each_thread(g, p);

loop_break:

	read_unlock(&tasklist_lock);

	/* Now write tool header */
	kdbg_ringbuffer_reset_adv_writer();
	t_hdr.cmd = KDBG_CMD_GET_THREAD_INFO;

	/* At this point write_size can't be less then TOOL_HDR_SIZE
	hence (write_size - TOOL_HDR_SIZE) can be converted to int32_t */
	t_hdr.data_len = (int32_t)(write_size - TOOL_HDR_SIZE);

	agent_adv_write((void *)&t_hdr, TOOL_HDR_SIZE);
	kdbg_ringbuffer_reset_adv_writer();
	kdbg_ringbuffer_write_advance(write_size);
	/* Release lock */
	mutex_unlock(&g_agent_rb_lock);

	return 0;
}
#endif

#ifdef CONFIG_SHOW_TASK_STATE
static int show_task_state(void)
{
	kdebugd_nobacktrace = 1;
	kdbg_show_state();
	task_state_help();

	return 1;
}
#endif

#ifdef CONFIG_TASK_STATE_BACKTRACE
static int show_task_state_backtrace(void)
{
	struct task_struct *tsk, *g, *p;
	debugd_event_t core_event;
	long event;

	kdebugd_nobacktrace = 0;

	PRINT_KD("\n");
	PRINT_KD("Enter pid/name of task (0 for all task)\n");
	PRINT_KD("===>  ");

	/* Enable kdebugd tab feature */
	kdbg_set_tab_action(KDBG_TAB_SHOW_TASK);
	event = debugd_get_event_as_numeric(&core_event, NULL);
	if (event < 0)
		event = kdbg_tab_process_event(&core_event);
	kdbg_set_tab_action(KDBG_TAB_NO_ACTION);

	PRINT_KD("\n");

	if (event) {
		/* If task count already incremented if task exists */
		tsk = get_task_with_given_pid(event);
		if (tsk == NULL) {
			PRINT_KD("\n");
			PRINT_KD("[ALERT] NO Thread\n");
			return 1;	/* turn on the menu */
		}
	} else {
		do_each_thread(g, p) {
			kdbg_show_task(p, KDBG_PRINT_PROCESS);
			PRINT_KD("\n");
		} while_each_thread(g, p);
		return 1;	/* turn on the menu */
	}

	kdbg_show_task(tsk, KDBG_PRINT_PROCESS);

	/* Decrement usage count which is incremented in
	 * get_task_with_given_pid */
	put_task_struct(tsk);

	return 1;
}
#endif

#ifdef CONFIG_TASK_FOR_COREDUMP
static int kill_task_for_coredump(void)
{
	struct task_struct *tsk;

	/* If task count already incremented if task exists */
	tsk = get_task_with_pid();
	if (tsk == NULL) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] NO Thread\n");
		return 1;	/* turn on the menu */
	}

	/* Send Signal for killing the task */
	force_sig(SIGABRT, tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);

	return 1;	/* turn on the menu */
}
#endif

/* 6. Dump task register with pid */
#ifdef CONFIG_SHOW_USER_THREAD_REGS
static int show_user_thread_regs(void)
{
	struct task_struct *tsk;
	struct pt_regs *regs;

	/* If task count already incremented if task exists */
	tsk = find_user_task_with_pid();
	if (tsk == NULL)
		return 1;	/* turn on the menu */

	task_lock(tsk);
	/* Namit 10-Dec-2010 in funciton __show_reg.
	 * cpu showing is current running CPU which
	 * can be mismatched with the task CPU*/
	regs = task_pt_regs(tsk);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);
	show_regs_wr(regs);

	return 1;		/* turn on the menu */
}
#endif

/* 7. Dump task pid maps with pid */
#ifdef CONFIG_SHOW_USER_MAPS_WITH_PID
static void __show_user_maps_with_pid(void)
{
	struct task_struct *tsk;

	/* If task count already incremented if task exists */
	tsk = find_user_task_with_pid();
	if (tsk == NULL)
		return;

	task_lock(tsk);

	PRINT_KD("======= Maps Summary Report =======================\n");
	PRINT_KD("       Total VMA Count : %d\n", tsk->mm->map_count);
	PRINT_KD("       Total VMA Size  : %lu kB\n",
			P2K(tsk->mm->total_vm));
	PRINT_KD("==============================================\n");

	show_pid_maps_wr(tsk);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * find_user_task_with_pid */
	put_task_struct(tsk);
}

static int show_user_maps_with_pid(void)
{
	__show_user_maps_with_pid();

	return 1;
}
#endif

/* 8. Dump user stack with pid */
#ifdef CONFIG_SHOW_USER_STACK_WITH_PID
static void __show_user_stack_with_pid(void)
{
	struct task_struct *tsk = NULL;
	struct pt_regs *regs = NULL;
	struct mm_struct *tsk_mm;

	/* If task count already incremented if task exists */
	tsk = find_user_task_with_pid();

	if (tsk == NULL)
		return;

	task_lock(tsk);

	regs = task_pt_regs(tsk);
	task_unlock(tsk);

	tsk_mm = get_task_mm(tsk);
	if (tsk_mm) {
		use_mm(tsk_mm);
		show_user_stack(tsk, regs);
		unuse_mm(tsk_mm);
		mmput(tsk_mm);
	} else {
		PRINT_KD("No mm\n");
	}

	/* Decrement usage count which is incremented in
	 * find_user_task_with_pid */
	put_task_struct(tsk);

}

static int show_user_stack_with_pid(void)
{
	__show_user_stack_with_pid();

	return 1;
}
#endif

/* 9. Convert Virt Addr(User) to Physical Addr */
#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
static void print_get_physaddr(unsigned long pfn,
			       unsigned long p_addr, unsigned long k_addr)
{

	unsigned long dump_limit = k_addr + DUMP_SIZE;

	if (((unsigned long)PAGE_MASK & k_addr) != ((unsigned long)PAGE_MASK & dump_limit))
		dump_limit = ((unsigned long)PAGE_MASK & k_addr) + OFFSET_MASK;

	/* Inform page table walking procedure */
	PRINT_KD
	    ("\n===============================================================");
	PRINT_KD("\nPhysical Addr:0x%08lx, Kernel Addr:0x%08lx", p_addr,
		 k_addr);
	PRINT_KD
	    ("\n===============================================================");
	PRINT_KD("\n Page Table walking...! [Aligned addresses]");
	PRINT_KD("\n PFN :0x%08lx\n PHYS_ADDR: 0x%08lx ", pfn,
		 (pfn << PAGE_SHIFT));
	PRINT_KD("\n VIRT_ADDR: 0x%08lx + 0x%08lx", (unsigned long)(PAGE_MASK & k_addr),
		 (unsigned long)(OFFSET_ALIGN_MASK & p_addr));
	PRINT_KD("\n VALUE    : 0x%08lx (addr:0x%08lx)",
		 *(unsigned long *)k_addr, k_addr);
	PRINT_KD
	    ("\n==============================================================");
	kdbg_dump_mem("\nKERNEL_ADDR", k_addr, dump_limit);
	PRINT_KD
	    ("\n==============================================================");
}

static unsigned long get_physaddr(struct task_struct *tsk, unsigned long u_addr,
			   int detail)
{
	struct page *page;
	struct vm_area_struct *vma = NULL;
	unsigned long k_addr, k_preaddr, p_addr, pfn, pfn_addr;
	int high_mem = 0;

	/* find vma w/ user address */
	vma = find_vma(tsk->mm, u_addr);
	if (!vma) {
		PRINT_KD("NO VMA\n");
		goto out;
	}

	/* get page struct w/ user address */
	page = follow_page(vma, u_addr, 0);

	/*Aug-17:Added check to see if the returned value is ERROR */
	if (!page || IS_ERR(page)) {
		PRINT_KD("NO PAGE\n");
		goto out;
	}

	if (PageReserved(page))
		PRINT_KD("[Zero Page]\n");

	if (PageHighMem(page)) {
		PRINT_KD("[Higmem Page]\n");
		high_mem = 1;
	}

	/* Calculate pfn, physical address,
	 * kernel mapped address w/ page struct */
	pfn = page_to_pfn(page);

	/*Aug-5-2010:modified comparison operation into macro check */
	/* In MSTAR pfn_valid is expanded as follows
	 * arch/mips/include/asm/page.h:#define pfn_valid(pfn)
	 * ((pfn) >= ARCH_PFN_OFFSET && (pfn) < max_mapnr)
	 * where the value of ARCH_PFN_OFFSET is 0 for MSTAR.
	 * This causes prevent to display warning that
	 * comparison >=0 is always true.
	 * Since this is due to system macro expansion
	 * this warning is acceptable.
	 */
	if (!pfn_valid(pfn)) {
		PRINT_KD("PFN IS NOT VALID\n");
		goto out;
	}

	/*Aug-5-2010:removed custom function to reuse system macro */
	pfn_addr = page_to_phys(page);

	if (!pfn_addr) {
		PRINT_KD("CAN'T CONVERT PFN TO PHYSICAL ADDR\n");
		goto out;
	}

	p_addr = pfn_addr + (unsigned long)(OFFSET_ALIGN_MASK & u_addr);

	/*Aug-6-2010:Map the page into kernel if memory is in HIGHMEM */
	if (high_mem)
		k_preaddr = (unsigned long)kmap(page);
	else
		k_preaddr = (unsigned long)page_address(page);

	if (!k_preaddr) {
		PRINT_KD
		    ("KERNEL ADDRESS CONVERSION FAILED (k_preaddr:0x%08lx)\n",
		     k_preaddr);
		goto out;
	}

	k_addr = k_preaddr + (unsigned long)(OFFSET_ALIGN_MASK & u_addr);
	/* In MSTAR virt_addr_valid is expanded as follows
	 * arch/mips/include/asm/page.h:#define virt_addr_valid(kaddr)   pfn_valid(PFN_DOWN(virt_to_phys(kaddr)))
	 * where the warning for "pfn_valid" system macro is explained above.
	 * Since this system macro depends on pfn_valid, which is another system macro the
	 * warning that comparison >=0 is always true due to macro expansion is acceptable
	 */
	if (!high_mem && (!virt_addr_valid((void *)k_addr)))
		PRINT_KD("INVALID KERNEL ADDRESS\n");

	if (detail == 1)
		print_get_physaddr(pfn, p_addr, k_addr);
	else
		PRINT_KD("Physical Addr:0x%08lx, Kernel Addr:0x%08lx\n", p_addr,
			 k_addr);
	if (high_mem)
		kunmap(page);

	return k_addr;
out:
	return 0;
}

static void __physical_memory_converter(void)
{
	struct task_struct *tsk;
	unsigned long p_addr, u_addr;

	tsk = get_task_with_pid();
	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("\n[ALERT] %s Thread",
			 (tsk == NULL) ? "No" : "Kernel");
		return;
	}

	/* get address */
	PRINT_KD("\nEnter memory address....\n===>  ");
	u_addr = (unsigned long)debugd_get_event_as_numeric(NULL, NULL);

	task_lock(tsk);
	/* Convert User Addr => Kernel mapping Addr */
	p_addr = get_physaddr(tsk, u_addr, 1);
	task_unlock(tsk);
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);

	if (!p_addr) {
		PRINT_KD("\n [KDEBUGD] "
			 "The virtual Address(user:0x%08lx) is not mapped to real memory",
			 u_addr);
	} else {
		PRINT_KD("\n [KDEBUGD] physical address :0x%08lx", p_addr);
	}

	return;
}

static int physical_memory_converter(void)
{
	__physical_memory_converter();
	return 1;
}
#endif /* CONFIG_VIRTUAL_TO_PHYSICAL */

#ifndef CONFIG_SMP
#ifdef CONFIG_MEMORY_VALIDATOR

/*Struct defined in linux/kdebugd.h*/
struct kdbg_mem_watch kdbg_mem_watcher = { 0,};

static void __memory_validator(void)
{
	struct task_struct *tsk;
	struct mm_struct *next_mm;
	mm_segment_t fs;
	unsigned long k_addr;

	/* If task count already incremented if task exists */
	tsk = get_task_with_pid();
	if (tsk == NULL) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] No Thread\n");
		return;
	}

	if (!tsk->mm) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] Kernel Thread\n");
		goto error_exit;
	}

	kdbg_mem_watcher.watching = 0;
	next_mm = tsk->active_mm;

	/* get pid */
	kdbg_mem_watcher.pid = tsk->pid;
	kdbg_mem_watcher.tgid = tsk->tgid;

	/* get address */
	PRINT_KD("\n");
	PRINT_KD("Enter memory address....\n");
	PRINT_KD("===>  ");
	kdbg_mem_watcher.u_addr = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n");

	/* check current active mm */
	if (current->active_mm != next_mm) {
		PRINT_KD("\n");
		PRINT_KD("[ALERT] This is Experimental Function..\n");

		k_addr = get_physaddr(tsk, kdbg_mem_watcher.u_addr, 0);
		if (k_addr == 0) {
			PRINT_KD
				("[ALERT] Page(addr:%08x) is not loaded in memory\n",
				 kdbg_mem_watcher.u_addr);
			goto error_exit;
		}

		kdbg_mem_watcher.buff = *(unsigned long *)k_addr;
	} else {

		/* Get User memory value */
		fs = get_fs();
		set_fs(KERNEL_DS);
		if (!access_ok
				(VERIFY_READ, (unsigned long *)kdbg_mem_watcher.u_addr,
				 sizeof(unsigned long *))) {
			PRINT_KD("[ALERT] Invalid User Address\n");
			set_fs(fs);
			goto error_exit;
		}
		__get_user(kdbg_mem_watcher.buff,
				(unsigned long *)kdbg_mem_watcher.u_addr);
		set_fs(fs);
	}
	/* Print Information */
	PRINT_KD("=====================================================\n");
	PRINT_KD(" Memory Value Watcher....!\n");
	PRINT_KD(" [Trace]  PID:%d  value:0x%08x (addr:0x%08x)\n",
			kdbg_mem_watcher.pid, kdbg_mem_watcher.buff,
			kdbg_mem_watcher.u_addr);
	PRINT_KD("=====================================================\n");
	kdbg_mem_watcher.watching = 1;

error_exit:
	/* Decrement usage count which is incremented in
	 * get_task_with_pid */
	put_task_struct(tsk);
	/* Check whenever invoke schedule().!! */
}

static int memory_validator(void)
{
	__memory_validator();

	return 1;
}
#endif /*CONFIG_MEMORY_VALIDATOR */
#endif /*CONFIG_SMP */

/* 11. Trace thread execution(look at PC) */

struct timespec timespec_interval = {.tv_sec = 0, .tv_nsec = 1000000};

#ifdef CONFIG_TRACE_THREAD_PC

#define SEC_PC_TRACE_MS           ((5 * 1000)/HZ)	/* 5 MS Timer tickes  */
#define SEC_PC_TRACE_MAX_ITEM   100	/* Max no of PC's in circular array */

#ifdef CONFIG_ELF_MODULE
static char *trace_func_name;
static int sec_pc_trace_idx;	/* Index for Currnet Program Counter */
#endif /* CONFIG_ELF_MODULE */

static int pctracing;
struct sec_kdbg_pc_info *sec_pc_trace_info;
struct task_struct *sec_pc_trace_tsk;
pid_t trace_tsk_pid;
struct hrtimer trace_thread_timer;

/* structure instances are used as list nodes */
struct kdbg_pid_list {
	pid_t pid;
	unsigned int prev_pc;
	struct list_head list;
};

/* a global variable to track the status of pc value */
struct kdbg_pid_list kpl = {.prev_pc = 0};

/* initialize the thread entry linklist */
struct kdbg_pid_list trace_alone, trace_head;

static int print_thread_execution(struct kdbg_pid_list *listptr)
{
	unsigned int cur_pc, cpu = 0;
	struct task_struct *trace_tsk = 0;
	ktime_t now;
	struct timespec curr_timespec;
	pid_t trace_pid, tgid = 0;

	if (listptr == NULL)
		return 0;

	trace_pid = listptr->pid;

	/* Check whether timer is still tracing already dead thread or not */
	now = ktime_get();
	/*Take RCU read lock register can be changed */
	rcu_read_lock();
	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */

	trace_tsk = find_task_by_pid_ns(trace_pid, &init_pid_ns);

	if (!trace_tsk) {
		PRINT_KD("\n");
		PRINT_KD("[kdebugd] traced task is killed...\n");
		kpl.prev_pc = 0;
		rcu_read_unlock();
		return -1;
	}

	if (trace_tsk) {
		/*Increment usage count */
		get_task_struct(trace_tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	/* fetching tgid for trace_task */
	tgid = trace_tsk->tgid;

	/* Cant take task lock here may be chances for hang */
	cur_pc = KSTK_EIP(trace_tsk);

	cpu = task_cpu(trace_tsk);

	/* Decrement usage count */
	put_task_struct(trace_tsk);
	kpl.prev_pc = listptr->prev_pc;
	if (kpl.prev_pc != cur_pc) {
		ktime_get_ts(&curr_timespec);
#ifdef CONFIG_ELF_MODULE
		/* Save the PC and time in a array of structure of sec_pc_trace_info
		 * and the symbol can be extracted out from sec_pc_trace_pc_symbol
		 * thread. */

		/* buffer not allocated dont process further */
		if (sec_pc_trace_info) {

			sec_pc_trace_info[sec_pc_trace_idx].pc = cur_pc;
			sec_pc_trace_info[sec_pc_trace_idx].pc_time.tv_sec =
				curr_timespec.tv_sec;
			sec_pc_trace_info[sec_pc_trace_idx].pc_time.tv_nsec =
				curr_timespec.tv_nsec;
			sec_pc_trace_info[sec_pc_trace_idx].cpu = cpu;
			sec_pc_trace_info[sec_pc_trace_idx].pid = trace_pid;
			sec_pc_trace_info[sec_pc_trace_idx].tgid = tgid;
			sec_pc_trace_idx =
				(sec_pc_trace_idx + 1) % SEC_PC_TRACE_MAX_ITEM;
		}
#else
		PRINT_KD
			("[kdebugd] [CPU %d] Pid:%4d  PC:0x%08x\t\t(TIME:%ld.%09ld)\n",
			cpu, trace_pid, cur_pc, curr_timespec.tv_sec, curr_timespec.tv_nsec);
#endif /* CONFIG_ELF_MODULE */
	}

	listptr->prev_pc = cur_pc;
	kpl.prev_pc = listptr->prev_pc;

	hrtimer_forward(&trace_thread_timer, now,
			timespec_to_ktime(timespec_interval));
	return 0;
}

static enum hrtimer_restart show_pc(struct hrtimer *timer)
{
	struct kdbg_pid_list *tmp = NULL;
	struct list_head *pos = NULL, *nx = NULL;

	if (list_empty(&trace_head.list)) {
		/* process with no thread/threads */
		if (print_thread_execution(&trace_alone) == -1)
			return HRTIMER_NORESTART;
	} else{
		/* process with threads */
		list_for_each_safe(pos, nx, &trace_head.list) {
			tmp = list_entry(pos, struct kdbg_pid_list, list);

			if (print_thread_execution(tmp) == -1)
				list_del(pos);
		}
	}
	return HRTIMER_RESTART;
}

static void start_trace_timer(void)
{
	hrtimer_init(&trace_thread_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	trace_thread_timer._softexpires = timespec_to_ktime(timespec_interval);
	trace_thread_timer.function = show_pc;
	hrtimer_start(&trace_thread_timer, trace_thread_timer._softexpires,
		      HRTIMER_MODE_REL);
}

static inline void end_trace_timer(void)
{
	hrtimer_cancel(&trace_thread_timer);
}

static void turnoff_trace_thread_pc(void)
{
	if (pctracing != 0) {
		PRINT_KD("\n");
		PRINT_KD("trace thread pc OFF!!\n");
		end_trace_timer();
		kpl.prev_pc = 0;
		pctracing = 0;

#ifdef CONFIG_ELF_MODULE
		if (sec_pc_trace_tsk)
			kthread_stop(sec_pc_trace_tsk);	/* Stop the symbol resolution thread */

		PRINT_KD(" Kernel Thread for symbol resolution done ...\n");
		sec_pc_trace_tsk = NULL;

		if (trace_func_name) {
			KDBG_MEM_DBG_KFREE(trace_func_name);
			trace_func_name = NULL;
		}
		if (sec_pc_trace_info) {
			KDBG_MEM_DBG_KFREE(sec_pc_trace_info);
			sec_pc_trace_info = NULL;
		}
#endif /* CONFIG_ELF_MODULE */

	}
}

#ifdef CONFIG_ELF_MODULE

/* function takes the program counter from the array which is
 * populated in show_pc function
 * and extract out corresponding symbol from symbol database
 * of application */

static int sec_pc_trace_pc_symbol(void *arg)
{
	static int rd_idx;
	int wr_idx = 0;
	int ii = 0;
	int buffer_count = 0;

	/*  Start from write index  */
	rd_idx = sec_pc_trace_idx;

	/* break the task function, when kthead_stop is called */
	while (!kthread_should_stop()) {
		/* buffer not allocated no need to process further */
		if (!sec_pc_trace_info)
			break;

		wr_idx = sec_pc_trace_idx;	/* take the idx lacally to prevent run time change */

		/* case of empty array or read and write index are in same position
		   can be handle automaticaly (buffer_count = 0) */
		if (rd_idx < wr_idx) {	/*  write ptr is leading  */
			buffer_count = wr_idx - rd_idx;
		} else if (rd_idx > wr_idx) {	/* read ptr rolled back first go to end
										   and then roll the write ptr */
			buffer_count = SEC_PC_TRACE_MAX_ITEM - rd_idx + wr_idx;
		} else {	/* case of empty array or read and write index are in same position
					   (buffer_count = 0) */
			buffer_count = 0;
		}

		for (ii = 0; ii < buffer_count; ++ii, ++rd_idx) {
			rd_idx = rd_idx % SEC_PC_TRACE_MAX_ITEM;

			WARN_ON(!sec_pc_trace_info[rd_idx].pc);

			if (trace_func_name) {
				/* get the symbol for program counter */
				if (!kdbg_elf_get_symbol_by_pid(sec_pc_trace_info[rd_idx].pid,
							sec_pc_trace_info
							[rd_idx].pc,
							trace_func_name)) {
					strncpy(trace_func_name, "???",
							KDBG_ELF_SYM_NAME_LENGTH_MAX -
							1);
					trace_func_name
						[KDBG_ELF_SYM_NAME_LENGTH_MAX - 1] =
						0;
				}
			}

			PRINT_KD
				("[kdebugd] [CPU %d] Pid:%4d tgid:%d PC:0x%08x (TIME:%06ld.%09ld)  %s\n",
				sec_pc_trace_info[rd_idx].cpu,
				sec_pc_trace_info[rd_idx].pid,
				sec_pc_trace_info[rd_idx].tgid,
				 sec_pc_trace_info[rd_idx].pc,
				 sec_pc_trace_info[rd_idx].pc_time.tv_sec,
				 sec_pc_trace_info[rd_idx].pc_time.tv_nsec,
				 (trace_func_name ? trace_func_name : "???"));
		}

		rd_idx = rd_idx % SEC_PC_TRACE_MAX_ITEM;
		msleep(SEC_PC_TRACE_MS);	/* N (default 5) timer ticks sleep */
	}

	return 0;
}

#endif /* CONFIG_ELF_MODULE */

static void __trace_thread_pc(void)
{
	struct task_struct *trace_tsk = NULL;
	struct task_struct *g, *p;
	struct kdbg_pid_list  *tmp;
	pid_t tgid;
	INIT_LIST_HEAD(&trace_head.list);

	if (pctracing == 0) {
		PRINT_KD("trace thread pc ON!!\n");
		trace_tsk = get_task_with_pid();

		if (trace_tsk == NULL || !trace_tsk->mm) {
			PRINT_KD("[ALERT] %s Thread\n",
					trace_tsk == NULL ? "No" : "Kernel");
			return;
		}
		trace_tsk_pid = trace_tsk->pid;

		tgid = trace_tsk->tgid;
		/* initializing the prime object which will always
		 * contain the passed pid by user*/
		trace_alone.pid = trace_tsk_pid;

		/*check if it is in Process mode then only perform this */
		if (kdbg_get_task_status()) {
			/* populate the child link list, if it is a process */
			/* scan child thread list to populate link list */
			do_each_thread(g, p) {
				/* condition will be true only in case of child threads
				 * whose parent and passed pid's parent will be same*/
				if (p->tgid == tgid) {
					/* creating a new memory block to append */
					tmp = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
						sizeof(struct kdbg_pid_list), GFP_ATOMIC);
					if (!tmp) {
						PRINT_KD("kmalloc, len %d failure.\n",
							sizeof(struct kdbg_pid_list));
						return;
					}

					tmp->pid = p->pid;
					list_add(&(tmp->list), &(trace_head.list));
				}
			} while_each_thread(g, p); /* end of pid search */
		}
#endif
		/* Decrement usage count which is incremented in
		 * get_task_with_pid */
		put_task_struct(trace_tsk);

#ifdef CONFIG_ELF_MODULE
		BUG_ON(sec_pc_trace_info);
		sec_pc_trace_info =
			(struct sec_kdbg_pc_info *)
			KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
					SEC_PC_TRACE_MAX_ITEM *
					sizeof(struct sec_kdbg_pc_info),
					GFP_KERNEL);

		BUG_ON(!sec_pc_trace_info);
		if (!sec_pc_trace_info)
			return;
		memset(sec_pc_trace_info, 0,
				(SEC_PC_TRACE_MAX_ITEM *
				 sizeof(struct sec_kdbg_pc_info)));

		BUG_ON(trace_func_name);
		trace_func_name = (char *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_KDEBUGD_MODULE, KDBG_ELF_SYM_NAME_LENGTH_MAX,
			 GFP_KERNEL);
		BUG_ON(!trace_func_name);
		if (!trace_func_name)
			return;

		kdbg_elf_load_elf_db_by_pids(&trace_tsk_pid, 1);

		BUG_ON(sec_pc_trace_tsk);
		/* create thread for symbol resolution */
		sec_pc_trace_tsk = kthread_create(sec_pc_trace_pc_symbol, NULL,
				"sec_pc_trace_tsk");
		if (IS_ERR(sec_pc_trace_tsk)) {
			PRINT_KD
				("%s:Symbol Resolve thread Creation Failed --------\n",
				 __FUNCTION__);
			sec_pc_trace_tsk = NULL;
		}

		sec_pc_trace_idx = 0;
		if (sec_pc_trace_tsk) {
			sec_pc_trace_tsk->flags |= PF_NOFREEZE;
			wake_up_process(sec_pc_trace_tsk);
		}
#endif /* CONFIG_ELF_MODULE */

		start_trace_timer();
		pctracing = 1;
	} else {
		PRINT_KD("trace thread pc OFF!!\n");
		end_trace_timer();
		kpl.prev_pc = 0;
		pctracing = 0;

#ifdef CONFIG_ELF_MODULE
		if (sec_pc_trace_tsk)
			kthread_stop(sec_pc_trace_tsk);	/* Stop the symbol resolution thread */

		PRINT_KD(" Kernel Thread for symbol resolution done ...\n");
		sec_pc_trace_tsk = NULL;

		if (trace_func_name) {
			KDBG_MEM_DBG_KFREE(trace_func_name);
			trace_func_name = NULL;
		}

		if (sec_pc_trace_info) {
			KDBG_MEM_DBG_KFREE(sec_pc_trace_info);
			sec_pc_trace_info = NULL;
		}
#endif /* CONFIG_ELF_MODULE */

	}
}

static int trace_thread_pc(void)
{
	__trace_thread_pc();

	return 0;
}

#if defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY)

/* For kdebugd - schedule history logger */
#define QUEUE_LIMIT 500

struct sched_queue {
	int pid;
	int tgid;
	char comm[16];
	unsigned long long sched_clock;
	int cpu;
#ifdef CONFIG_ELF_MODULE
	unsigned int pc;
#endif				/* CONFIG_ELF_MODULE */
};

struct sched_queue *g_pvd_queue_;
static atomic_t  sched_history_init_flag = ATOMIC_INIT(E_NONE);
unsigned long vd_queue_idx;
int vd_queue_is_full;

static DEFINE_SPINLOCK(sched_history_lock);

bool init_sched_history(void)
{

	WARN_ON(g_pvd_queue_);
	g_pvd_queue_ = (struct sched_queue *)KDBG_MEM_DBG_KMALLOC
	    (KDBG_MEM_KDEBUGD_MODULE, QUEUE_LIMIT * sizeof(struct sched_queue),
	     GFP_KERNEL);
	if (!g_pvd_queue_) {
		PRINT_KD
		    ("Cannot initialize schedule history: Insufficient memory\n");
		atomic_set(&sched_history_init_flag, 0);
		return false;
	}
	atomic_set(&sched_history_init_flag, 1);
	return true;

}

void destroy_sched_history(void)
{
	struct sched_queue *aptr = NULL;

	spin_lock(&sched_history_lock);
	if (g_pvd_queue_) {
		/* This spin lock is going to use from scheduler
		 * keep as light as possible */
		/* Assign the pointer and free later from outside spin lock*/
		aptr = g_pvd_queue_;
		g_pvd_queue_ = NULL;
		atomic_set(&sched_history_init_flag, 0);
	}
	spin_unlock(&sched_history_lock);

	if (aptr) {
		KDBG_MEM_DBG_KFREE(aptr);
		PRINT_KD("Sched History Destroyed Successfuly\n");
	} else {
		PRINT_KD("Not Initialized\n");
	}
}

int status_sched_history(void)
{
	if (atomic_read(&sched_history_init_flag))
		PRINT_KD("Initialized        Running\n");
	else
		PRINT_KD("Not Initialized    Not Running\n");

	return 1;
}

void sched_history_OnOff(void)
{
	if (atomic_read(&sched_history_init_flag))
		destroy_sched_history();
	else
		init_sched_history();
}

int show_sched_history(void)
{
	int idx, j;
	int buffer_count = 0;

	if (!atomic_read(&sched_history_init_flag))
		init_sched_history();

	if (atomic_read(&sched_history_init_flag)) {

#ifdef CONFIG_ELF_MODULE
		char *func_name = NULL;
		func_name = (char *)KDBG_MEM_DBG_KMALLOC
			(KDBG_MEM_KDEBUGD_MODULE, KDBG_ELF_SYM_NAME_LENGTH_MAX,
			 GFP_KERNEL);
#endif /* CONFIG_ELF_MODULE */

		/* BUG:flag is on but no memory allocated !! */
		BUG_ON(!g_pvd_queue_);

		if (vd_queue_is_full) {
			buffer_count = QUEUE_LIMIT;
			idx = (int)vd_queue_idx % QUEUE_LIMIT;
		} else {
			buffer_count = (int)vd_queue_idx % QUEUE_LIMIT;
			idx = 0;
		}

		/*preempt_disable(); resolve the crash at the time of IO read */
		PRINT_KD
			("===========================================================\n");
		PRINT_KD("CONTEXT SWITCH HISTORY LOGGER\n");
		PRINT_KD
			("===========================================================\n");
		PRINT_KD("Current time:%llu Context Cnt:%lu\n", sched_clock(),
				vd_queue_idx);
		PRINT_KD
			("===========================================================\n");

		if (buffer_count) {
			/* print */
			for (j = 0; j < buffer_count; ++idx, idx %= QUEUE_LIMIT) {
				PRINT_KD
					(" INFO:%3d:[CPU %d]%-17s(tid:%-4d,tgid:%-4d)t:%llu\n",
					 j++, g_pvd_queue_[idx].cpu,
					 g_pvd_queue_[idx].comm,
					 g_pvd_queue_[idx].pid,
					 g_pvd_queue_[idx].tgid,
					 g_pvd_queue_[idx].sched_clock);
#ifdef CONFIG_ELF_MODULE
				if (func_name) {
					if (!kdbg_elf_get_symbol_by_pid
							(g_pvd_queue_[idx].pid,
							 g_pvd_queue_[idx].pc, func_name)) {
						strncpy(func_name, "???",
								sizeof("???"));
						func_name[sizeof("???") - 1] =
							'\0';
					}
				}
				PRINT_KD(" PC: (0x%x)  %s\n",
						g_pvd_queue_[idx].pc,
						(func_name ? func_name : "???"));
#endif /* CONFIG_ELF_MODULE */
			}
		} else {
			PRINT_KD
				("No Statistics found [may be system is Idle]\n");
		}

#ifdef CONFIG_ELF_MODULE
		if (func_name)
			KDBG_MEM_DBG_KFREE(func_name);
#endif /* CONFIG_ELF_MODULE */

		PRINT_KD("\n");
		vd_queue_idx = 0;
		/* preempt_enable(); Fix the crash at the time of IO read i.e.,from USB*/
	}

	return 1;
}

void schedule_history(struct task_struct *next, int cpu)
{
	static int prev_idx;
	int idx = (int)vd_queue_idx % QUEUE_LIMIT;

	spin_lock(&sched_history_lock);

	/* If not initialize return */
	if (!atomic_read(&sched_history_init_flag)) {
		spin_unlock(&sched_history_lock);
		return;
	}

	if (next->pid == g_pvd_queue_[prev_idx].pid) {
		spin_unlock(&sched_history_lock);
		return;
	}

	/* pid */
	g_pvd_queue_[idx].pid = (int)next->pid;
	g_pvd_queue_[idx].tgid = (int)next->tgid;

	/* comm */
	g_pvd_queue_[idx].comm[0] = '\0';
	get_task_comm(g_pvd_queue_[idx].comm, next);
	g_pvd_queue_[idx].comm[TASK_COMM_LEN - 1] = '\0';

#ifdef CONFIG_ELF_MODULE
	g_pvd_queue_[idx].pc = KSTK_EIP(next);
#endif /* CONFIG_ELF_MODULE */

	/* sched_clock */
	g_pvd_queue_[idx].sched_clock = sched_clock();
	g_pvd_queue_[idx].cpu = cpu;

	spin_unlock(&sched_history_lock);

	prev_idx = idx;
	vd_queue_idx++;

	if (vd_queue_idx == QUEUE_LIMIT) {
		vd_queue_is_full = 1;
	}
}
#endif /* defined(CONFIG_KDEBUGD_MISC) && defined(CONFIG_SCHED_HISTORY) */

/* Kdebugd task configuration flag - thread/process */
static atomic_t kdbg_task_status = ATOMIC_INIT(0);

/* Get task status */
int kdbg_get_task_status(void)
{
	return atomic_read(&kdbg_task_status);
}

/* Toggle the task status between process and thread */
static void kdbg_toggle_task_status(void)
{
	atomic_set(&kdbg_task_status, !atomic_read(&kdbg_task_status));
}

static int kdbg_configuration(void)
{
	int operation = 1;
	while (operation != 99) {
		PRINT_KD("-----------------------------------------\n");
		PRINT_KD("1.  Toggle task mode (Currently: %s)\n",
				kdbg_get_task_status() ? "PROCESS" : "THREAD");
		PRINT_KD("99. For exit\n");
		PRINT_KD("-----------------------------------------\n");
		PRINT_KD("Select Option ==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");
		if (operation == 1)
			kdbg_toggle_task_status();
		else if (operation != 99)
			PRINT_KD("Invalid Option..\n");
	}
	PRINT_KD("\n");
	return 1;
}

static int __init kdbg_misc_init(void)
{
	int retval = 0;

#ifdef CONFIG_SHOW_TASK_STATE
	kdbg_register("DEBUG: A list of tasks and their relation information",
			show_task_state, NULL, KDBG_MENU_SHOW_TASK_STATE);
#endif

#ifdef CONFIG_SHOW_TASK_PRIORITY
	kdbg_register("DEBUG: A list of tasks and their priority information",
			show_state_prio, NULL, KDBG_MENU_SHOW_TASK_PRIORITY);
#endif

#ifdef CONFIG_TASK_STATE_BACKTRACE
	kdbg_register
		("DEBUG: A list of tasks and their information + backtrace(kernel)",
		 show_task_state_backtrace, NULL, KDBG_MENU_TASK_STATE_BACKTRACE);
#endif

#ifdef CONFIG_TASK_FOR_COREDUMP
	kdbg_register("DEBUG: Kill the task to create coredump",
			kill_task_for_coredump, NULL,
			KDBG_MENU_TASK_FOR_COREDUMP);
#endif

#ifdef CONFIG_VIRTUAL_TO_PHYSICAL
	kdbg_register("DEBUG: Virt(User) to physical ADDR Converter ",
			physical_memory_converter, NULL,
			KDBG_MENU_VIRTUAL_TO_PHYSICAL);
#endif

#ifdef CONFIG_SHOW_USER_THREAD_REGS
	kdbg_register("DEBUG: Dump task register with pid",
			show_user_thread_regs, NULL,
			KDBG_MENU_SHOW_USER_THREAD_REGS);
#endif

#ifdef CONFIG_SHOW_USER_MAPS_WITH_PID
	kdbg_register("DEBUG: Dump task maps with pid", show_user_maps_with_pid,
			NULL, KDBG_MENU_SHOW_USER_MAPS_WITH_PID);
#endif

#ifdef CONFIG_SHOW_USER_STACK_WITH_PID
	kdbg_register("DEBUG: Dump user stack with pid",
			show_user_stack_with_pid, NULL,
			KDBG_MENU_SHOW_USER_STACK_WITH_PID);
#endif

#ifdef CONFIG_KDEBUGD_TRACE
	kdbg_trace_init();
#endif

#ifdef	CONFIG_ELF_MODULE
	kdbg_register("DEBUG: Dump symbol of user stack with pid",
			kdbg_elf_show_symbol_of_user_stack_with_pid, NULL,
			KDBG_MENU_DUMP_SYMBOL_USER_STACK);
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_KDEBUGD_FUTEX
	kdbg_futex_init();
#endif

#ifdef CONFIG_KDEBUGD_FTRACE
	kdbg_ftrace_init();
#endif

#ifndef CONFIG_SMP
#ifdef CONFIG_MEMORY_VALIDATOR
	kdbg_register("TRACE: Memory Value Watcher", memory_validator, NULL,
			KDBG_MENU_MEMORY_VALIDATOR);
#endif
#endif

#ifdef CONFIG_TRACE_THREAD_PC
	kdbg_register("TRACE: Trace thread execution(look at PC)",
			trace_thread_pc, turnoff_trace_thread_pc,
			KDBG_MENU_TRACE_THREAD_PC);
#endif

#ifdef CONFIG_SCHED_HISTORY

#ifdef CONFIG_SCHED_HISTORY_AUTO_START
	init_sched_history();
#endif

	kdbg_register("TRACE: Schedule history logger", show_sched_history,
			NULL, KDBG_MENU_SCHED_HISTORY);
#endif

#ifdef CONFIG_KDEBUGD_COUNTER_MONITOR
	/* Core should be initialize first .. */
	kdbg_work_queue_init();
	kdbg_register("COUNTER MONITOR: Counter monitor status", kdebugd_status,
			NULL, KDBG_MENU_COUNTER_MONITOR);
	kdbg_cpuusage_init();
	kdbg_topthread_init();
	kdbg_diskusage_init();
#ifdef CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS
	kdbg_perfcounters_init();
#endif
	kdbg_memusage_init();
	kdbg_netusage_init();
	kdbg_cpufreq_init();

#endif /*CONFIG_KDEBUGD_COUNTER_MONITOR */

#ifdef CONFIG_ELF_MODULE
	kdbg_elf_init();
#endif /* CONFIG_ELF_MODULE */

#ifdef CONFIG_ADVANCE_OPROFILE
	aop_kdebug_start();
#endif /* CONFIG_ADVANCE_OPROFILE */

#ifdef KDBG_MEM_DBG
	kdbg_mem_init();
#endif /* KDBG_MEM_DBG */

#ifdef CONFIG_KDEBUGD_LIFE_TEST
	kdbg_key_test_player_init();
#endif

#ifdef CONFIG_KDEBUGD_HUB_DTVLOGD
	kdbg_register("HUB: DTVLOGD_LOG PRINTING", dtvlogd_buffer_printf, NULL, KDBG_MENU_DTVLOGD_LOG);
#endif

#ifdef CONFIG_KDEBUGD_PRINT_ONOFF
	kdbg_register("Dynamically Support Printf", kdbg_printf_status, NULL, KDBG_MENU_PRINT_ONOFF);
#endif

#ifdef CONFIG_KDEBUGD_FD_DEBUG
	kdbg_register("DEBUG: FD Debug", kdbg_fd_debug_handler, NULL, KDBG_MENU_FD_DEBUG);
#endif
	/* Control kdebugd menus output - threadwise/processwise */
	kdbg_register("Kdebugd Configuration", kdbg_configuration, NULL, KDBG_MENU_KDBG_CONFIG);
#ifdef CONFIG_KDML
	retval = kdbg_kdml_init();
	if (retval) {
		PRINT_KD("KDebugd: Error in initializing KDML\n");
	}
#endif
	return retval;
}

module_init(kdbg_misc_init)
