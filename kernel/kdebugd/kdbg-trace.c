/*
 * kdbg-trace.c (09.04.10)
 *
 * Copyright (C) 2009 Samsung Electronics
 * Created by js.lee (js07.lee@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/mmu_context.h>
#include <linux/syscalls.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <kdebugd.h>
#include "kdbg_util.h"
#include "kdbg-trace.h"
#ifdef CONFIG_ELF_MODULE
#include "kdbg_elf_sym_api.h"
#endif
#include "kdbg-waitmacros.h"

extern int suppress_extra_prints;
void show_user_backtrace_pid(pid_t pid, int use_ptrace, int load_elf);

/*
 * get_user_value
 * Get the address and copy/read the data/instruction from user stack
 */
inline unsigned int get_user_value(struct mm_struct *bt_mm,
					  unsigned long addr)
{
	unsigned int retval = KDEBUGD_BT_INVALID_VAL;
	struct vm_area_struct *vma;

	if (bt_mm == NULL)
		return retval;

	if (!access_ok(VERIFY_READ, addr, sizeof(retval)))
		return retval;

	vma = find_vma(bt_mm, addr);
	if (!vma)
		return retval;

	if (in_atomic()) {
		/* dont allow access to mmap'ed area */
		if (vma->vm_file && vma->vm_file->f_dentry->d_name.name && !strcmp(vma->vm_file->f_dentry->d_name.name, "mem"))
			return retval;

		if (__copy_from_user_inatomic(&retval, (unsigned int *)addr, sizeof(retval)))
			/* in case of fail, it returns non-zero (might modify retval) */
			retval = KDEBUGD_BT_INVALID_VAL;
	} else {
		int ret;
		ret = __get_user(retval, (unsigned long *)addr);
		if (ret)
			retval = KDEBUGD_BT_INVALID_VAL;
	}

	return retval;
}

/*
 * __save_user_trace
 * stores the unsigned long addr val in the array trace
 */
void __save_user_trace(struct kbdg_bt_trace *trace, unsigned long val)
{
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = val;
}

/* Find task struct and increments its usage */
static struct task_struct *bt_get_task_struct(pid_t pid)
{
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task)
		return ERR_PTR(-ESRCH);
	return task;
}

void show_user_backtrace_pid(pid_t pid, int use_ptrace, int load_elf)
{
	struct task_struct *task;
	struct mm_struct *mm, *current_mm;
	long ret = 0;
	unsigned long addr = 0, data = 0;
	int retry_count = 0;

/* Since not tested in Orsay platform not sure about its working,
 * therfore skipping pid 1 (init) for Orsay
 * */
#ifndef CONFIG_PLAT_TIZEN
	/*
	 * skip certain PIDs
	 * - init 1
	 */
	if (pid == 1) {
		PRINT_KD("Skipping PID: %d\n", pid);
		return;
	}
#endif

	/* increments task usage, if found */
	task = bt_get_task_struct(pid);
	if (IS_ERR(task)) {
		PRINT_KD("[ERROR] No Thread\n");
		return;
	}

	/* increment target task's users */
	mm = get_task_mm(task);
	if (!mm) {
		PRINT_KD("[ERROR] %d doesn't have mm, (Kernel Thread ?)\n",
			 pid);
		goto go_to_put_task_struct;
	}

	current_mm = current->mm;
	if (current_mm) {
		printk("backtrace called outside kernel thread\n");
		/* increment mm_count,to avoid __mm_drop
		 * in case if mm_count = 1 of this user task */
		atomic_inc(&current_mm->mm_count);
	}
	/* take the target task's mm */
	use_mm(mm);

	/* check task state and don't attach in uninterruptible state */
	if (task->state == TASK_UNINTERRUPTIBLE)
		use_ptrace = 0;

	if (use_ptrace) {
		/* Refer to the link for values:
			https://lists.nongnu.org/archive/html/libunwind-devel/2012-02/txt9gd2v4S2NM.txt 
		*/
		const int wait_loops = 10;	
		const int wait_time = 100;
		int x = 0;
		/* use ptrace for attaching the thread */
		ret = sys_ptrace(PTRACE_ATTACH, pid, addr, data);
		if (ret != 0) {
			PRINT_KD
			    ("[ERROR] %s: Error in attaching pid: %d, ret: %ld\n",
			     __FUNCTION__, pid, ret);
			goto unuse_mm_put_task_struct;
		}
		/* wait for task to stop */
		x = 0;
		while (x < wait_loops) {
			int status;
			ret = sys_wait4(pid, &status, WNOHANG| WUNTRACED, NULL);
			if (WIFSTOPPED(status))
				break;
			msleep(wait_time);
			x++;
		}
		if (x == wait_loops) {
			/* unable to stop the task, detach it */
			goto goto_ptrace_detach;
		}
	}

	/* thread is not running */
	show_user_backtrace_task(task, load_elf, mm);

goto_ptrace_detach:
	retry_count = 5;
	if (use_ptrace) {
		do {
			ret = sys_ptrace(PTRACE_DETACH, pid, addr, data);
			if (ret != 0) {
				PRINT_KD
					("[ERROR] %s: Error in detaching pid: %d, ret: %ld (state: 0x%lx, exit: 0x%x)\n",
					 __FUNCTION__, pid, ret,
					task->state,
					task->exit_state);
				/* for zombie, we need to clear the resources */
				if (ret == -ESRCH) {
					/* wait for pid to exit */
					int status;
					int ret;
					ret = sys_wait4(pid, &status, WNOHANG | __WALL, NULL);
					if (WIFSIGNALED(status)) {
						PRINT_KD("#### %d terminated by signal: %d ####\n", pid,
								WTERMSIG(status));
						break;
					}
				}
				retry_count--;
				msleep(1000);
			}
		} while (ret && retry_count);
	}

unuse_mm_put_task_struct:
	if (current_mm) {
		use_mm(current_mm);
		/* decrease mm_count, corresponding to
		 * increment above */
		atomic_dec(&current_mm->mm_count);
	}
	else
		unuse_mm(mm);
	mmput(mm);

go_to_put_task_struct:
	put_task_struct(task);

}

/* function to show backtrace for all threads */
static void show_user_backtrace_all_pids(void)
{
	const int MAX_EXTRA_THREADS = 100;	/* allow for variation of 100 threads */
	pid_t *pid_arr = NULL;
	struct task_struct *g, *p;
	int i, count = 0;
	const int MAX_THREADS = nr_threads + MAX_EXTRA_THREADS;

	/*
	 * we shouldn't be holding lock during show_user_backtrace_task()
	 * so we maintain a list of max pids
	 */
	pid_arr = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				       (size_t)MAX_THREADS * sizeof(pid_t), GFP_KERNEL);
	if (!pid_arr) {
		PRINT_KD("[ERROR] Insufficient Memory!!!!\n");
		return;
	}

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (!p || !p->mm || !p->pid) {
			BUG_ON(!p);
			PRINT_KD("Inappropriate Thread - pid: %d, mm: %p\n",
				 p->pid, p->mm);
		} else {
			pid_arr[count++] = p->pid;
			if (count == MAX_THREADS) {
				PRINT_KD
				    ("User threads more than %d, increase MAX_THREADS\n",
				     MAX_THREADS);
				/* this is a double loop, so we have to use goto */
				goto skip_loop;
			}
		}
	} while_each_thread(g, p);	/* end for pid search */
skip_loop:
	read_unlock(&tasklist_lock);

	for (i = 0; i < count; i++) {
		BUG_ON(!pid_arr[i]);
		/*
		 * We loaded ELF already
		 * control Ptracing
		 */
		show_user_backtrace_pid(pid_arr[i], 1, 1);
	}
	KDBG_MEM_DBG_KFREE(pid_arr);
	pid_arr = NULL;
}

/*
 * show_user_stack_backtrace:
 * This fnction received the event of user to start backtrace if it is 0,
 * get the pid of all task and start backtrace for each one else ps per pid.
 * find the task and start backtrace.
 */
static int show_user_stack_backtrace(void)
{
	long event;
	int use_ptrace = 1;
	struct task_struct *g, *p, *input;
	pid_t tgid = 0;
	debugd_event_t core_event;

	PRINT_KD("Enter Pid/Name Of Task (0 for all task)\n");
	PRINT_KD("==>   ");

	/* Enable tab feature of kdebugd */
	kdbg_set_tab_action(KDBG_TAB_SHOW_TASK);
	event = debugd_get_event_as_numeric(&core_event, NULL);
	if (event < 0)
		event = kdbg_tab_process_event(&core_event);
	kdbg_set_tab_action(KDBG_TAB_NO_ACTION);

	PRINT_KD("\n");

	if (event != 0) {
		/* Find task struct */
		rcu_read_lock();
		input = find_task_by_pid_ns(event, &init_pid_ns);
		if (input)
			tgid = input->tgid;

		rcu_read_unlock();

		if (kdbg_get_task_status()) {
			/* Process mode */
			do_each_thread(g, p) {
				if (p->tgid == tgid)
					show_user_backtrace_pid(p->pid, use_ptrace, 1);
			} while_each_thread(g, p);	/* end for pid search */
		} else {
			/* Thread Mode */
			show_user_backtrace_pid(event, 1, 1);
		}
	} else
	show_user_backtrace_all_pids();

	return 1;
}

/*
 * function to display the backtrace.
 * It disables the extra prints that are not relevant
 */
void show_user_bt(struct task_struct *tsk)
{
	pid_t pid;
	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("Invalid Task\n");
		return;
	}
	pid = tsk->pid;
	suppress_extra_prints = 1;
	/*
	 * this function is called from Futex.
	 * when a task is ptraced, its futexes are woken up.
	 * and ELF is already loaded by the caller
	 */
	show_user_backtrace_pid(pid, 0, 0);

	suppress_extra_prints = 0;
}

/*
 * kdbg_trace_init
 * register show_user_stack_backtrace with kdebugd
 */
int kdbg_trace_init(void)
{
	kdbg_register("DEBUG: Dump backtrace(User)", show_user_stack_backtrace,
		      NULL, KDBG_MENU_SHOW_USER_STACK_BACKTRACE);
	return 0;
}
