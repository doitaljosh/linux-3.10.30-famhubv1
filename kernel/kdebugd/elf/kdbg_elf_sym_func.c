/*
 *  kernel/kdebugd/elf/KDBG_ELF_sym_func.c
 *
 *  Symbol (ELF) module public interface functions
 *  These functions can be used to enhance kdebugd function related to symbol table
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-10-26  Created by karuna.nithy@samsung.com
 *
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include "kdbg_util.h"
#include "kdbg_elf_sym_api.h"

#define SYM_DEBUG_ON  0
#include "kdbg_elf_sym_debug.h"

/* Used to load executables and share libraries of the given PID.
 return 0: success, -1= fail */
static int kdbg_elf_sym_get_exe_n_so_libs(pid_t pid, struct kdbg_elf_fs_lib_paths
					  *p_lib_paths);

/* find the elf file index from the given elf files and pc */
static int find_elf_file_idx(struct kdbg_elf_fs_lib_paths *p_lib_paths,
			     unsigned int addr, unsigned int *pc)
{
	int count = 0;

	if (!p_lib_paths)
		return -1;

	while (count < p_lib_paths->num_paths) {
		if (p_lib_paths->elf_file[count].start_addr <= (unsigned long)addr &&
		    p_lib_paths->elf_file[count].end_addr > (unsigned long)addr) {
			*pc = addr - (unsigned int)(p_lib_paths->elf_file[count].start_addr);
			return count;
		}
		count++;
	}

	return -1;
}

/*
 * The Function find address in task vma,
 * if elf exist for the VMA, load the
 * ELF and resolve the symbol
 * Note: Befor using this function make sure that reference count of task_mm is incremented !!!
 * */
static int get_symbol_and_lib_by_task_mm(struct mm_struct *tsk_mm, unsigned int sym_addr,
				     char *plib_name,
				     struct aop_symbol_info *symbol_info,
				     unsigned int *start_addr)
{

	unsigned long pc;
	int ret = 0;
	struct vm_area_struct *vma;
	static char path_buf[KDBG_ELF_MAX_PATH_LEN];
	char* p=NULL;
	int retval;
	kdbg_elf_usb_elf_list_item *plist=NULL;
	int elf_match = KDBG_ELF_SYMBOLS;

	if (!plib_name || !tsk_mm || !symbol_info->pfunc_name)
		return ret;


	vma = find_vma(tsk_mm, sym_addr);
	if (!vma || !vma->vm_file)
		return ret;

	if (vma->vm_start > sym_addr) {
		return ret;
	}


	if ((vma->vm_flags & VM_READ) && (vma->vm_flags & VM_EXEC) &&
			(!(vma->vm_flags & VM_WRITE)) && (!(vma->vm_flags & VM_MAYSHARE))) {

		p = d_path(&(vma->vm_file->f_path),path_buf, KDBG_ELF_MAX_PATH_LEN);
		if (!IS_ERR(p)) sym_printk("File name is: %s", p);
		else return ret;

		plist = lookup_elf_file_in_db(p);
		if (!plist) {
			plist = load_elf_db_by_elf_file(p, 0, elf_match);
			if (!plist) {
				PRINT_KD("ELF: [ALERT]: %s Fail to load file %s\n",
						__FUNCTION__, p);
				return ret;
			}
		}

		pc = sym_addr - vma->vm_start;

		symbol_info->virt_addr = plist->virtual_addr;
		pc += plist->virtual_addr;

		retval = kdbg_elf_sym_find(pc, plist, symbol_info->pfunc_name,
				KDBG_ELF_SYM_NAME_LENGTH_MAX, &symbol_info->start_addr);
		if (retval == 0) {
			if (symbol_info->pfunc_name[0] != '\0')
				ret = 1;	/* success */
			memcpy(plib_name, plist->elf_name_actual_path, KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN);
			symbol_info->start_addr =
				symbol_info->start_addr - symbol_info->virt_addr;
			*start_addr = vma->vm_start + symbol_info->start_addr;
		} else {
			symbol_info->start_addr = sym_addr - symbol_info->virt_addr;
			*start_addr = 0;
		}
	}

	return ret;
}

/* Get symbol name by pid & pc */
int kdbg_elf_get_symbol_by_pid(pid_t pid, unsigned int sym_addr, char *sym_name)
{
	int ret = 0;
	struct task_struct *task;
	struct mm_struct *mm = NULL;
	static char path_buf[KDBG_ELF_MAX_PATH_LEN];
	char* p=NULL;
	kdbg_elf_usb_elf_list_item *plist=NULL;
	struct vm_area_struct *vma;
	int retval;
	int elf_match = KDBG_ELF_SYMBOLS;

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return 0;
	}
	if (!sym_name) {
		return 0;
	}

	/* find the task struct of the given PID */
	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	rcu_read_lock();
	task = find_task_by_pid_ns(pid, &init_pid_ns);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task) {
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: No Thread\n",
				__FUNCTION__, pid);
		return 0;
	}

	/* Increment task mm */
	mm = get_task_mm(task);
	if (!mm) {
		put_task_struct(task);
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: Kernel Thread\n",
				__FUNCTION__, pid);
		return 0;
	}

	vma = find_vma(mm, sym_addr);
	if (!vma || !vma->vm_file){
		mmput(mm);
		put_task_struct(task);
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: VMA not found\n",
				__FUNCTION__, pid);
		return 0;
	}

	if (vma->vm_start > sym_addr) {
		mmput(mm);
		put_task_struct(task);
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: Invalid VMA!! Symbol(%u) Start Add (%lu)\n",
				__FUNCTION__, pid, sym_addr, vma->vm_start);
		return 0;
	}

	p = d_path(&(vma->vm_file->f_path),path_buf, KDBG_ELF_MAX_PATH_LEN);
	if (!IS_ERR(p)) sym_printk("File name is: %s", p);
	else {
		mmput(mm);
		put_task_struct(task);
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: File name not valid\n",
				__FUNCTION__, pid);
		return 0;
	}

	put_task_struct(task);
	mmput(mm);

	if ((vma->vm_flags & VM_READ) && (vma->vm_flags & VM_EXEC) &&
			(!(vma->vm_flags & VM_WRITE)) && (!(vma->vm_flags & VM_MAYSHARE))) {

		plist = lookup_elf_file_in_db(p);
		if (!plist) {
			plist = load_elf_db_by_elf_file(p, 0, elf_match);
			if (!plist) {
				PRINT_KD("ELF: [ALERT]: %s Fail to load file %s\n",
						__FUNCTION__, p);
				return ret;
			}
		}

		retval = kdbg_elf_sym_find(sym_addr, plist, sym_name,
				KDBG_ELF_SYM_NAME_LENGTH_MAX, NULL);
		if (retval == 0) {
			/* Check for name mangling*/
			if (sym_name[0] == '_' && sym_name[1] == 'Z') {
				char *d_fname = NULL;
				d_fname = kdbg_elf_sym_demangle_wrap(sym_name);
				if (d_fname) {
					strncpy(sym_name, d_fname,
							KDBG_ELF_SYM_NAME_LENGTH_MAX);
					sym_name[KDBG_ELF_SYM_NAME_LENGTH_MAX - 1] =
						'\0';
					KDBG_MEM_DBG_KFREE(d_fname);
				}
			}
			ret = 1;
		}
	}

	return ret;
}


/* Dump symbol of user stack with pid */
int kdbg_elf_show_symbol_of_user_stack_with_pid(void)
{
	unsigned int *user_stack = NULL;
	struct task_struct *tsk;
	unsigned long start, end;
	int no_of_us_values;
	struct kdbg_elf_fs_lib_paths *p_lib_paths = NULL;
	char *func_name = NULL, *d_fname = NULL;
	int stack_index = 0;
	pid_t pid = 0;

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return 1;
	}

	tsk = get_task_with_pid();
	if (tsk == NULL || !tsk->mm) {
		PRINT_KD("\nAOP: [ALERT] %s Thread",
			 (tsk == NULL) ? "No" : "Kernel");
		return 0;
	}
	/*Increment usage count */
	get_task_struct(tsk);

	kdbg_elf_load_elf_db_by_pids(&tsk->pid, 1);
	no_of_us_values = get_user_stack(tsk, &user_stack, &start, &end);
	pid = tsk->pid;

	/* Decrement usage count */
	put_task_struct(tsk);

	if (no_of_us_values && user_stack) {

		/* load elf files */
		p_lib_paths = (struct kdbg_elf_fs_lib_paths *)
		    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_PUBLIC_INTERFACE_MODULE,
					 sizeof(struct kdbg_elf_fs_lib_paths),
					 GFP_KERNEL);
		if (!p_lib_paths) {
			PRINT_KD("p_lib_paths out of memory\n");
			goto __SYM_ERROR_EXIT;
		}

		if (kdbg_elf_sym_get_exe_n_so_libs(pid, p_lib_paths) == -1) {
			goto __SYM_ERROR_EXIT;
		}

		func_name = (char *)
		    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_PUBLIC_INTERFACE_MODULE,
					 KDBG_ELF_SYM_NAME_LENGTH_MAX,
					 GFP_KERNEL);
		if (!func_name) {
			PRINT_KD("func_name: no memory\n");
			goto __SYM_ERROR_EXIT;
		}


		PRINT_KD("-----------------------------------------\n");
		PRINT_KD("* dump symbol of user stack\n");
		PRINT_KD("-----------------------------------------\n");

		PRINT_KD("stack value\tfunction name\n");
		PRINT_KD("-----------------------------------------\n");

		while (stack_index < no_of_us_values) {
			int elf_file_idx;
			unsigned int pc;
			unsigned int stack_addr = *(user_stack + stack_index++);
			unsigned int sym_addr = *(user_stack + stack_index++);

			PRINT_KD("%4x  %08x  ", stack_addr, sym_addr);

			/* find elf file */
			elf_file_idx =
			    find_elf_file_idx(p_lib_paths, sym_addr, &pc);

			if (elf_file_idx == -1)
				PRINT_KD("???\n");
			else {
				struct aop_symbol_info symbol_info;
				char *elf_name = NULL;

				/* get the elf base name from elf path */
				elf_name =
				    kdbg_elf_base_elf_name(p_lib_paths->elf_file
							   [elf_file_idx].name);

				/* validate and get the symbol name */
				if (!elf_name ||
				    elf_name <
				    p_lib_paths->elf_file[elf_file_idx].name) {
					PRINT_KD("???\n");
				} else {
					PRINT_KD("%s::", elf_name);
					func_name[0] = '\0';
					symbol_info.pfunc_name = func_name;
#ifdef CONFIG_DWARF_MODULE
					symbol_info.df_info_flag = 0;
					symbol_info.pdf_info = NULL;
#endif
					if (kdbg_elf_get_symbol
					    (elf_name, pc,
					     KDBG_ELF_SYM_NAME_LENGTH_MAX,
					     &symbol_info) != 0) {
						PRINT_KD("???\n");
					} else {
						if (func_name[0] == '\0')
							PRINT_KD("???\n");
						else {
							/* Check for name mangling*/
							if (func_name[0] == '_' && func_name[1] == 'Z') {
								d_fname = kdbg_elf_sym_demangle_wrap(func_name);
								if (!d_fname) {
									PRINT_KD("d_fname: no memory\n");
									goto __SYM_ERROR_EXIT;
								}
							}
							PRINT_KD("%s\n", d_fname);
						}
					}
				}
			}
		}

		PRINT_KD("-----------------------------------------\n");
	}

__SYM_ERROR_EXIT:

	if (p_lib_paths)
		KDBG_MEM_DBG_KFREE(p_lib_paths);
	if (user_stack)
		vfree(user_stack);	/* this allocation is done @ traps.c at function get_user_stack() */
	if (func_name)
		KDBG_MEM_DBG_KFREE(func_name);
	if (d_fname)
		KDBG_MEM_DBG_KFREE(d_fname);

	return 1;
}

/* load elf database by elf file  */
kdbg_elf_usb_elf_list_item * kdbg_elf_load_elf_db_by_elf_file(char *elf_file)
{

	kdbg_elf_usb_elf_list_item *plist = NULL;
	int elf_status = KDBG_ELF_SYMBOLS;

	if (!elf_file) {
		return NULL;
	}

	plist = lookup_elf_file_in_db(elf_file);
	if (!plist) {
		return load_elf_db_by_elf_file(elf_file, 0, elf_status);
	}
	return plist;
}

/*
 * Load elf database by pid. pid can be a single pid or collection of pid.
 * First it will collect all elf file belongs to the process id and prepare the elf file list
 * which is belongs to the given (collection of) process id and load the elf database
 * */
int kdbg_elf_load_elf_db_by_pids(const pid_t *ppid, int num_pids)
{
	struct kdbg_elf_fs_lib_paths *p_lib_paths = NULL;
	struct kdbg_elf_fs_lib_paths *p_tmp_lib_paths = NULL;
	int count = 0;

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return 1;
	}

	if (!ppid || !num_pids) {
		/* not a valid pid list */
		return 1;
	}

	/* load elf files */
	p_lib_paths = (struct kdbg_elf_fs_lib_paths *)
	    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_PUBLIC_INTERFACE_MODULE,
				 sizeof(struct kdbg_elf_fs_lib_paths),
				 GFP_KERNEL);
	if (!p_lib_paths) {
		PRINT_KD("p_lib_paths out of memory\n");
		return 1;
	}

	p_tmp_lib_paths = (struct kdbg_elf_fs_lib_paths *)
	    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_PUBLIC_INTERFACE_MODULE,
				 sizeof(struct kdbg_elf_fs_lib_paths),
				 GFP_KERNEL);
	if (!p_tmp_lib_paths) {
		PRINT_KD("p_tmp_lib_paths out of memory\n");
		KDBG_MEM_DBG_KFREE(p_lib_paths);
		return 1;
	}

	p_lib_paths->num_paths = 0;

	while (count < num_pids) {
		/* validate the max limit */
		if (p_lib_paths->num_paths >= KDBG_ELF_SYM_MAX_SO_LIBS) {
			PRINT_KD("Lib Array Full %d !!!!!\n",
				 p_lib_paths->num_paths);
			break;
		}

		p_tmp_lib_paths->num_paths = 0;

		/* get exe or shared libraries used for the given process */
		if (kdbg_elf_sym_get_exe_n_so_libs(ppid[count], p_tmp_lib_paths)
		    > 0 && p_tmp_lib_paths->num_paths > 0) {

			int new_elf_file_count = 0;
			BUG_ON(p_lib_paths->num_paths >=
			       KDBG_ELF_SYM_MAX_SO_LIBS);
			BUG_ON(p_tmp_lib_paths->num_paths >
			       KDBG_ELF_SYM_MAX_SO_LIBS);

			/* files found then update it */
			/* verify and update new exe or shared libraries to elf lib path */
			while (new_elf_file_count < p_tmp_lib_paths->num_paths
			       && p_lib_paths->num_paths <
			       KDBG_ELF_SYM_MAX_SO_LIBS) {
				int stored_elf_file_count = 0;
				int elf_file_found = 0;

				BUG_ON(p_lib_paths->num_paths >=
				       KDBG_ELF_SYM_MAX_SO_LIBS);
				BUG_ON(p_tmp_lib_paths->num_paths >
				       KDBG_ELF_SYM_MAX_SO_LIBS);

				/* verify the given elf file is already exist or not */
				while (stored_elf_file_count <
				       p_lib_paths->num_paths) {
					BUG_ON(stored_elf_file_count >=
					       KDBG_ELF_SYM_MAX_SO_LIBS);
					BUG_ON(new_elf_file_count >=
					       KDBG_ELF_SYM_MAX_SO_LIBS);

					if (p_lib_paths->elf_file
					    [stored_elf_file_count].inod_num ==
					    p_tmp_lib_paths->elf_file
					    [new_elf_file_count].inod_num) {
						/* elf file exists */
						sym_printk
						    ("%s: stored_elf_file= %d: File Exists\n",
						     stored_elf_file_count);
						elf_file_found = 1;
						break;
					}
					stored_elf_file_count++;
				}

				/* elf file not found add it to elf lib path */
				if (!elf_file_found) {
					/* copy the executable to given array */
					BUG_ON(new_elf_file_count >=
					       KDBG_ELF_SYM_MAX_SO_LIBS);

					strncpy(p_lib_paths->elf_file
						[p_lib_paths->num_paths].name,
						p_tmp_lib_paths->elf_file
						[new_elf_file_count].name,
						KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN);
					p_lib_paths->elf_file
					    [p_lib_paths->num_paths].name
					    [KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN -
					     1] = '\0';

					p_lib_paths->elf_file
					    [p_lib_paths->num_paths].start_addr
					    =
					    p_tmp_lib_paths->elf_file
					    [new_elf_file_count].start_addr;
					p_lib_paths->elf_file
					    [p_lib_paths->num_paths].end_addr =
					    p_tmp_lib_paths->elf_file
					    [new_elf_file_count].end_addr;
					p_lib_paths->elf_file
					    [p_lib_paths->num_paths].inod_num =
					    p_tmp_lib_paths->elf_file
					    [new_elf_file_count].inod_num;
					p_lib_paths->num_paths++;
					sym_printk
					    ("%s: p_lib_paths->num_path= %d\n",
					     __FUNCTION__,
					     p_lib_paths->num_paths);
					BUG_ON(p_lib_paths->num_paths >
					       KDBG_ELF_SYM_MAX_SO_LIBS);
				}
				new_elf_file_count++;
				sym_printk("%s:  new_elf_file_count= %d\n",
					   __FUNCTION__, new_elf_file_count);
			}
		} else {
			sym_printk("AOP: No libs loaded for process (pid:0x%x):"
				   "p_tmp_lib_paths->num_paths = %d\n",
				   ppid[count], p_tmp_lib_paths->num_paths);
		}

		count++;

	}

	for (count = 0; count < p_lib_paths->num_paths; count++) {
		/* call elf load function to load elf database */
		kdbg_elf_load_elf_db_by_elf_file(p_lib_paths->elf_file[count].
						 name);
	}

	KDBG_MEM_DBG_KFREE(p_lib_paths);
	KDBG_MEM_DBG_KFREE(p_tmp_lib_paths);
	return 0;
}

/* collect all the process id and collect elf files belongs to the process id's
and load the elf database */
int kdbg_elf_load_elf_db_for_all_process(void)
{
	struct task_struct *p;
	int do_unlock = 1;
	int num_pids = 0;
	pid_t *pid_list = NULL;
	int count = 0;		/* lalit */

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return 1;
	}

	/* make an estimate of total number of processes */
	num_pids = nr_processes();
	BUG_ON(num_pids <= 0);
	sym_printk("Num Pids = %d\n", num_pids);
	/* allocate memory to store pid */
	pid_list =
	    (pid_t *) KDBG_MEM_DBG_KMALLOC(KDBG_MEM_PUBLIC_INTERFACE_MODULE,
					   (size_t)num_pids * sizeof(pid_t),
					   GFP_KERNEL);
	if (!pid_list) {
		sym_errk("no memory for loading ELFs\n");
		return 1;
	}
	/* to do task lock */
#ifdef CONFIG_PREEMPT_RT
	if (!read_trylock(&tasklist_lock)) {
		do_unlock = 0;
	}
#else
	read_lock(&tasklist_lock);
#endif
	for_each_process(p) {
		/* store the pid, which is having memory map */
		if (p->mm)
			pid_list[count++] = p->pid;

		/* array out of range validation */
		if (count >= num_pids)
			break;
	}

	/* to do task unlock */
	if (do_unlock)
		read_unlock(&tasklist_lock);

	if (likely(count > 0)) {
		/* note: we may get less process also, in case if any process
		 * exits just before we take the lock */
		if (kdbg_elf_load_elf_db_by_pids(pid_list, count) < 0) {
			sym_errk("Unable to Load elf by pid\n");
		}
	}
	KDBG_MEM_DBG_KFREE(pid_list);
	return 0;
}

/*Get symbol name and library by pid & pc */
int kdbg_elf_get_symbol_and_lib_by_mm(struct mm_struct *tsk_mm, unsigned int sym_addr,
				       char *plib_name,
				       struct aop_symbol_info *symbol_info,
				       unsigned int *start_addr)
{
	int ret = 0;

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return 0;
	}

	if (symbol_info->pfunc_name) {

		if (get_symbol_and_lib_by_task_mm(tsk_mm, sym_addr, plib_name,
					symbol_info, start_addr)) {

			/* Check for name mangling*/
			if (symbol_info->pfunc_name[0] == '_' && symbol_info->pfunc_name[1] == 'Z') {
				char *d_fname = NULL;
				d_fname = kdbg_elf_sym_demangle_wrap(symbol_info->pfunc_name);
				if(d_fname) {
					strncpy(symbol_info->pfunc_name, d_fname,
							KDBG_ELF_SYM_NAME_LENGTH_MAX);
					symbol_info->pfunc_name
						[KDBG_ELF_SYM_NAME_LENGTH_MAX - 1] = '\0';
					KDBG_MEM_DBG_KFREE(d_fname);
				}
			}

			ret = 1;
		}
	}
	return ret;
}

/* Used to load executables and share libraries of the given PID.
 return success: num_paths loaded (>= 0), fail: -1 */
static int kdbg_elf_sym_get_exe_n_so_libs(pid_t pid, struct kdbg_elf_fs_lib_paths
					  *p_lib_paths)
{
	struct vm_area_struct *vma;
	struct file *file;
	static char path_buf[KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN];
	struct task_struct *task;
	struct mm_struct *mm = NULL;
	int intr_cnt = 0;

	if (!p_lib_paths) {
		PRINT_KD("ELF: [ALERT]: %s  null lib path\n", __FUNCTION__);
		return -1;
	}
	p_lib_paths->num_paths = 0;

	/* find the task struct of the given PID */
	/* Refer to kernel/pid.c
	 *  init_pid_ns has bee initialized @ void __init pidmap_init(void)
	 *  PID-map pages start out as NULL, they get allocated upon
	 *  first use and are never deallocated. This way a low pid_max
	 *  value does not cause lots of bitmaps to be allocated, but
	 *  the scheme scales to up to 4 million PIDs, runtime.
	 */
	rcu_read_lock();
	task = find_task_by_pid_ns(pid, &init_pid_ns);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task) {
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: No Thread\n",
			 __FUNCTION__, pid);
		return -1;
	}

	/* Increment task mm */
	mm = get_task_mm(task);
	if (!mm) {
		put_task_struct(task);
		PRINT_KD("ELF: [ALERT]: %s  pid= 0x%x: Kernel Thread\n",
			 __FUNCTION__, pid);
		return -1;
	}

	sym_printk("-----------------------------------------\n");
	sym_printk("* Read exe & all shared lib on pid (%d)\n", task->pid);
	sym_printk("-----------------------------------------\n");

	/* Taking task lock is little bit costly here, avoid task lock */
	/* Try to get the lock after 1 second return */

	while (!down_read_trylock(&mm->mmap_sem)) {
		msleep(50);
		if (++intr_cnt == 20) {
			/* wait 1 Sec */
			PRINT_KD("Failed to get the lock.. returning..\n");
			mmput(mm);
			put_task_struct(task);
			return -1;
		}
	}

	intr_cnt = 0;
	/* take the mmap start address */
	vma = mm->mmap;

	/* lopp thru all the mmap and collect the executables for the give task */
	while (vma) {
		file = vma->vm_file;
		if (file) {
			struct inode *inode = vma->vm_file->f_dentry->d_inode;
			if ((vma->vm_flags & VM_READ) &&
			    (vma->vm_flags & VM_EXEC) &&
			    (!(vma->vm_flags & VM_WRITE)) &&
			    (!(vma->vm_flags & VM_MAYSHARE))) {
				/* check the max limit */
				if (p_lib_paths->num_paths <
				    KDBG_ELF_SYM_MAX_SO_LIBS) {
					/* decode path & executables */

					char *p = d_path(&file->f_path,
							 path_buf,
							 KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN);

					/* validate the return value */
					if (!IS_ERR(p)) {
						sym_printk("%08x %s\n",
							   (unsigned int)
							   vma->vm_flags, p);

						/* copy the executable to given array */
						snprintf(p_lib_paths->elf_file
							 [p_lib_paths->
							  num_paths].name,
							 KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN,
							 "%s", p);
						p_lib_paths->
						    elf_file[p_lib_paths->
							     num_paths].
						    start_addr = vma->vm_start;
						p_lib_paths->
						    elf_file[p_lib_paths->
							     num_paths].
						    end_addr = vma->vm_end;
						p_lib_paths->
						    elf_file[p_lib_paths->
							     num_paths].
						    inod_num = inode->i_ino;
						p_lib_paths->num_paths++;
					}
				} else {
					sym_printk("Lib Array Full %d !!!!!\n",
						   p_lib_paths->num_paths);
				}
			} /* if (vma is READ, EXEC, WRITE, MAYSHARE */
			else {
				/* update the end address */
				int count = 0;
				while (count < p_lib_paths->num_paths) {
					if (p_lib_paths->elf_file[count].
					    inod_num == inode->i_ino) {
						/* update end address */
						p_lib_paths->elf_file[count].
						    end_addr = vma->vm_end;
					}
					count++;
				}
			}
		}
		/* if (file) */
		vma = vma->vm_next;
	}

	/* release the mm sem */
	up_read(&mm->mmap_sem);
	mmput(mm);
	put_task_struct(task);

	sym_printk("--------------------------------------------\n\n");

	return p_lib_paths->num_paths;
}
