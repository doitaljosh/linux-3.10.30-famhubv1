/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#define DEBUG		/* Enable initcall_debug */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/stackprotector.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/tty.h>
#include <linux/percpu.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/kernel_stat.h>
#include <linux/start_kernel.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/cgroup.h>
#include <linux/efi.h>
#include <linux/tick.h>
#include <linux/interrupt.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/unistd.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>
#include <linux/buffer_head.h>
#include <linux/page_cgroup.h>
#include <linux/debug_locks.h>
#include <linux/debugobjects.h>
#include <linux/lockdep.h>
#include <linux/kmemleak.h>
#include <linux/pid_namespace.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/idr.h>
#include <linux/kgdb.h>
#include <linux/ftrace.h>
#include <linux/async.h>
#include <linux/kmemcheck.h>
#include <linux/sfi.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/file.h>
#include <linux/ptrace.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/random.h>
#include <linux/kasan.h>

#include <asm/io.h>
#include <asm/bugs.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

#include <linux/pageowner.h>

#ifdef CONFIG_VDLP_VERSION_INFO
#include <linux/vdlp_version.h>
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/smp.h>
#endif

#include <trace/early.h>

#ifdef CONFIG_EXECUTE_AUTHULD
#include <linux/random.h>
#include "secureboot/include/Secureboot.h"
#include "secureboot/include/hmac_sha1.h"

extern int MicomCtrl(unsigned char ctrl, unsigned char arg);
extern int micom_reboot(void);
char secure_file[1024];
#endif

static int kernel_init(void *);

extern void init_IRQ(void);
extern void fork_init(unsigned long);
extern void mca_init(void);
extern void sbus_init(void);
extern void radix_tree_init(void);
#ifndef CONFIG_DEBUG_RODATA
static inline void mark_rodata_ro(void) { }
#endif

#ifdef CONFIG_TC
extern void tc_init(void);
#endif

/*
 * Debug helper: via this flag we know that we are in 'early bootup code'
 * where only the boot processor is running with IRQ disabled.  This means
 * two things - IRQ must not be enabled before the flag is cleared and some
 * operations which are not allowed with IRQ disabled are allowed while the
 * flag is set.
 */
bool early_boot_irqs_disabled __read_mostly;

enum system_states system_state __read_mostly;
EXPORT_SYMBOL(system_state);

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

#ifdef CONFIG_EXECUTE_AUTHULD

#define AUTH_ING	0x10
#define AUTH_FAIL	0x11
#define AUTH_SUCC	0x12
#define AUTH_BEFORE	0x13

#define RANDOM_LEN 256


static char * auth_argv_init[MAX_INIT_ARGS+2] = {CONFIG_AUTHULD_PATH, NULL, };
extern char rootfs_name[64];
static pid_t authuld_pid=0;
static pid_t authuld_parent_pid=0;
static char auth_random[RANDOM_LEN];
char auth_parent[TASK_COMM_LEN];
static int authuld_result=AUTH_BEFORE;
#endif

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*__initdata late_time_init)(void);
extern void softirq_init(void);

/* Untouched command line saved by arch-specific code. */
char __initdata boot_command_line[COMMAND_LINE_SIZE];
/* Untouched saved command line (eg. for /proc) */
char *saved_command_line;
/* Command line for parameter parsing */
static char *static_command_line;

static char *execute_command;
static char *ramdisk_execute_command;

/*
 * If set, this is an indication to the drivers that reset the underlying
 * device before going ahead with the initialization otherwise driver might
 * rely on the BIOS and skip the reset operation.
 *
 * This is useful if kernel is booting in an unreliable environment.
 * For ex. kdump situaiton where previous kernel has crashed, BIOS has been
 * skipped and devices will be in unknown state.
 */
unsigned int reset_devices;
EXPORT_SYMBOL(reset_devices);

static int __init set_reset_devices(char *str)
{
	reset_devices = 1;
	return 1;
}

__setup("reset_devices", set_reset_devices);

static const char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
const char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern const struct obs_kernel_param __setup_start[], __setup_end[];

static int __init obsolete_checksetup(char *line)
{
	const struct obs_kernel_param *p;
	int had_early_param = 0;

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (parameqn(line, p->str, n)) {
			if (p->early) {
				/* Already done in parse_early_param?
				 * (Needs exact match on param part).
				 * Keep iterating, as we can have early
				 * params and __setups of same names 8( */
				if (line[n] == '\0' || line[n] == '=')
					had_early_param = 1;
			} else if (!p->setup_func) {
				printk(KERN_WARNING "Parameter %s is obsolete,"
				       " ignored\n", p->str);
				return 1;
			} else if (p->setup_func(line + n))
				return 1;
		}
		p++;
	} while (p < __setup_end);

	return had_early_param;
}

/*
 * This should be approx 2 Bo*oMips to start (note initial shift), and will
 * still work even if initially too large, it will just take slightly longer
 */
unsigned long loops_per_jiffy = (1<<12);

EXPORT_SYMBOL(loops_per_jiffy);

static int __init debug_kernel(char *str)
{
	console_loglevel = 10;
	return 0;
}

static int __init quiet_kernel(char *str)
{
	console_loglevel = 4;
	return 0;
}

early_param("debug", debug_kernel);
early_param("quiet", quiet_kernel);

static int __init loglevel(char *str)
{
	int newlevel;

	/*
	 * Only update loglevel value when a correct setting was passed,
	 * to prevent blind crashes (when loglevel being set to 0) that
	 * are quite hard to debug
	 */
	if (get_option(&str, &newlevel)) {
		console_loglevel = newlevel;
		return 0;
	}

	return -EINVAL;
}

early_param("loglevel", loglevel);

/* Change NUL term back to "=", to make "param" the whole string. */
static int __init repair_env_string(char *param, char *val, const char *unused)
{
	if (val) {
		/* param=val or param="val"? */
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
			val--;
		} else
			BUG();
	}
	return 0;
}

/*
 * Unknown boot options get handed to init, unless they look like
 * unused parameters (modprobe will find them in /proc/cmdline).
 */
static int __init unknown_bootoption(char *param, char *val, const char *unused)
{
	repair_env_string(param, val, unused);

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/* Unused module parameter. */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val))
		return 0;

	if (panic_later)
		return 0;

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "Too many boot env vars at `%s'";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], val - param))
				break;
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "Too many boot init vars at `%s'";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/*
	 * In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

static int __init rdinit_setup(char *str)
{
	unsigned int i;

	ramdisk_execute_command = str;
	/* See "auto" comment in init_setup */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("rdinit=", rdinit_setup);

#ifndef CONFIG_SMP
static const unsigned int setup_max_cpus = NR_CPUS;
#ifdef CONFIG_X86_LOCAL_APIC
static void __init smp_init(void)
{
	APIC_init_uniprocessor();
}
#else
#define smp_init()	do { } while (0)
#endif

static inline void setup_nr_cpu_ids(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }
#endif

/*
 * We need to store the untouched command line for future reference.
 * We also need to store the touched command line since the parameter
 * parsing is performed in place, and we should allow a component to
 * store reference of name/value for future reference.
 */
static void __init setup_command_line(char *command_line)
{
	saved_command_line = alloc_bootmem(strlen (boot_command_line)+1);
	static_command_line = alloc_bootmem(strlen (command_line)+1);
	strcpy (saved_command_line, boot_command_line);
	strcpy (static_command_line, command_line);
}

/*
 * We need to finalize in a non-__init function or else race conditions
 * between the root thread and the init thread may cause start_kernel to
 * be reaped by free_initmem before the root thread has proceeded to
 * cpu_idle.
 *
 * gcc-3.4 accidentally inlines this function, so use noinline.
 */

static __initdata DECLARE_COMPLETION(kthreadd_done);

static noinline void __init_refok rest_init(void)
{
	int pid;

	rcu_scheduler_starting();
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	kernel_thread(kernel_init, NULL, CLONE_FS | CLONE_SIGHAND);
	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();
	complete(&kthreadd_done);

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	init_idle_bootup_task(current);
	schedule_preempt_disabled();
	/* Call into cpu_idle with preempt disabled */
	cpu_startup_entry(CPUHP_ONLINE);
}

/* Check for early params. */
static int __init do_early_param(char *param, char *val, const char *unused)
{
	const struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && parameq(param, p->str)) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)
		) {
			if (p->setup_func(val) != 0)
				pr_warn("Malformed early option '%s'\n", param);
		}
	}
	/* We accept everything at this stage. */
	return 0;
}

void __init parse_early_options(char *cmdline)
{
	parse_args("early options", cmdline, NULL, 0, 0, 0, do_early_param);
}

/* Arch code calls this early on, or if not, just before other parsing. */
void __init parse_early_param(void)
{
	static __initdata int done = 0;
	static __initdata char tmp_cmdline[COMMAND_LINE_SIZE];

	if (done)
		return;

	/* All fall through to do_early_param. */
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_early_options(tmp_cmdline);
	done = 1;
}

/*
 *	Activate the first processor.
 */

static void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();
	/* Mark the boot cpu "present", "online" etc for SMP and UP case */
	set_cpu_online(cpu, true);
	set_cpu_active(cpu, true);
	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);
}

void __init __weak smp_setup_processor_id(void)
{
}

# if THREAD_SIZE >= PAGE_SIZE
void __init __weak thread_info_cache_init(void)
{
}
#endif

/*
 * Set up kernel memory allocators
 */
static void __init mm_init(void)
{
	/*
	 * page_cgroup requires contiguous pages,
	 * bigger than MAX_ORDER unless SPARSEMEM.
	 */
	page_cgroup_init_flatmem();
	pageowner_init_flatmem();
	pageowner_init_sparsemem();

	mem_init();
	kmem_cache_init();
	percpu_init_late();
	pgtable_cache_init();
	vmalloc_init();
}
#if defined(CONFIG_ARCH_SDP1106) || defined(CONFIG_ARCH_SDP1202)
void board_dual_init(char *cmdline)
{
	unsigned int part=0;
	char * rootdev;
#if defined(CONFIG_ARCH_SDP1106)
	*(volatile unsigned int*)0xFE090d00 |= (0x2 << 28);     /* SET P0.7 TO INPUT MODE*/
	part=(*(volatile unsigned int*)0xFE090d08)>>7 & 0x1;
	rootdev = strnstr((const char *)cmdline, "mmcblk0p", 0x2000);
	if(part)
	{
		if(rootdev!=0)
		{
			memcpy(rootdev, "mmcblk0p14 ", 11);
		}
	}
	else
	{
		if(rootdev!=0)
		{
			memcpy(rootdev, "mmcblk0p13 ", 11);
		}

	}
#elif defined(CONFIG_ARCH_SDP1202)
	*(volatile unsigned int*)0xFE090CF4 |= (0x2 << 16);     /* SET P0.4 TO INPUT MODE*/
	part=(*(volatile unsigned int*)0xFE090CFC)>>4 & 0x1;
	rootdev = strnstr((const char *)cmdline, "mmcblk0p", 0x2000);
	if(part)
	{
		if(rootdev!=0)
		{
			memcpy(rootdev, "mmcblk0p18 ", 11);
		}
	}
	else
	{
		if(rootdev!=0)
		{
			memcpy(rootdev, "mmcblk0p17 ", 11);
		}

	}
#endif
}
#endif


asmlinkage void __init start_kernel(void)
{
	char * command_line;
	extern const struct kernel_param __start___param[], __stop___param[];

	/* First early event. */
	trace_early_message("start_kernel");

	/*
	 * Need to run as early as possible, to initialize the
	 * lockdep hash:
	 */
	lockdep_init();
	smp_setup_processor_id();
	debug_objects_early_init();

	/*
	 * Set up the the initial canary ASAP:
	 */
	boot_init_stack_canary();

	cgroup_init_early();

	local_irq_disable();
	early_boot_irqs_disabled = true;

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	boot_cpu_init();
	page_address_init();
	pr_notice("%s", linux_banner);
	setup_arch(&command_line);
	kasan_init_shadow();
	mm_init_owner(&init_mm, &init_task);
	mm_init_cpumask(&init_mm);
#if defined(CONFIG_ARCH_SDP1106) || defined(CONFIG_ARCH_SDP1202)
	board_dual_init(command_line);
	board_dual_init(boot_command_line);
#endif
	setup_command_line(command_line);
	setup_nr_cpu_ids();
	setup_per_cpu_areas();
	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */

	build_all_zonelists(NULL, NULL);
	page_alloc_init();

	pr_notice("Kernel command line: %s\n", boot_command_line);
	parse_early_param();
	parse_args("Booting kernel", static_command_line, __start___param,
		   __stop___param - __start___param,
		   -1, -1, &unknown_bootoption);

	jump_label_init();

	/*
	 * These use large bootmem allocations and must precede
	 * kmem_cache_init()
	 */
	setup_log_buf(0);
	pidhash_init();
	vfs_caches_init_early();
	sort_main_extable();
	trap_init();
	mm_init();
	kasan_uar_init();

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	if (WARN(!irqs_disabled(), "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	idr_init_cache();
	perf_event_init();
	rcu_init();
	tick_nohz_init();
	radix_tree_init();
	/* init some links before init_ISA_irqs() */
	early_irq_init();
	init_IRQ();
	tick_init();
	init_timers();
	hrtimers_init();
	softirq_init();
	timekeeping_init();
	time_init();
	profile_init();
	call_function_init();
	WARN(!irqs_disabled(), "Interrupts were enabled early\n");
	early_boot_irqs_disabled = false;
	local_irq_enable();

	kmem_cache_init_late();

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	console_init();
	if (panic_later)
		panic(panic_later, panic_param);

	lockdep_info();

	/*
	 * Need to run this when irqs are enabled, because it wants
	 * to self-test [hard/soft]-irqs on/off lock inversion bugs
	 * too:
	 */
	locking_selftest();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
		    page_to_pfn(virt_to_page((void *)initrd_start)),
		    min_low_pfn);
		initrd_start = 0;
	}
#endif
	page_cgroup_init();
	debug_objects_mem_init();
	kmemleak_init();
	setup_per_cpu_pageset();
	numa_policy_init();
	if (late_time_init)
		late_time_init();
	sched_clock_init();
	calibrate_delay();
	pidmap_init();
	anon_vma_init();
#ifdef CONFIG_X86
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_enter_virtual_mode();
#endif
#ifdef CONFIG_X86_ESPFIX64
	/* Should be run before the first non-init thread is created */
	init_espfix_bsp();
#endif
	thread_info_cache_init();
	cred_init();
	fork_init(totalram_pages);
	proc_caches_init();
	buffer_init();
	key_init();
	security_init();
	dbg_late_init();
	vfs_caches_init(totalram_pages);
	signals_init();
	/* rootfs populating might need page-writeback */
	page_writeback_init();
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	cgroup_init();
	cpuset_init();
	taskstats_init_early();
	delayacct_init();

	check_bugs();

	acpi_early_init(); /* before LAPIC and SMP init */
	sfi_init_late();

	if (efi_enabled(EFI_RUNTIME_SERVICES)) {
		efi_late_init();
		efi_free_boot_services();
	}

	ftrace_init();

	/* Do the rest non-__init'ed, we're now alive */
	rest_init();
}

/* Call all constructor functions linked into the kernel. */
static void __init do_ctors(void)
{
#ifdef CONFIG_CONSTRUCTORS
	ctor_fn_t *fn = (ctor_fn_t *) __ctors_start;

	for (; fn < (ctor_fn_t *) __ctors_end; fn++)
		(*fn)();
#endif
}

bool initcall_debug;
core_param(initcall_debug, initcall_debug, bool, 0644);

static char msgbuf[64];

static int __init_or_module do_one_initcall_debug(initcall_t fn)
{
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	int ret;

	pr_debug("calling  %pF @ %i\n", fn, task_pid_nr(current));
	calltime = ktime_get();
	ret = fn();
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	pr_debug("initcall %pF returned %d after %lld usecs\n",
		 fn, ret, duration);

	return ret;
}

int __init_or_module do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	int ret;

	if (initcall_debug)
		ret = do_one_initcall_debug(fn);
	else
		ret = fn();

	msgbuf[0] = 0;

	if (preempt_count() != count) {
		sprintf(msgbuf, "preemption imbalance ");
		preempt_count() = count;
	}
	if (irqs_disabled()) {
		strlcat(msgbuf, "disabled interrupts ", sizeof(msgbuf));
		local_irq_enable();
	}
	WARN(msgbuf[0], "initcall %pF returned with %s\n", fn, msgbuf);

	return ret;
}


extern initcall_t __initcall_start[];
extern initcall_t __initcall0_start[];
extern initcall_t __initcall1_start[];
extern initcall_t __initcall2_start[];
extern initcall_t __initcall3_start[];
extern initcall_t __initcall4_start[];
extern initcall_t __initcall5_start[];
extern initcall_t __initcall6_start[];
extern initcall_t __initcall7_start[];
extern initcall_t __initcall_end[];

static initcall_t *initcall_levels[] __initdata = {
	__initcall0_start,
	__initcall1_start,
	__initcall2_start,
	__initcall3_start,
	__initcall4_start,
	__initcall5_start,
	__initcall6_start,
	__initcall7_start,
	__initcall_end,
};

/* Keep these in sync with initcalls in include/linux/init.h */
static char *initcall_level_names[] __initdata = {
	"early",
	"core",
	"postcore",
	"arch",
	"subsys",
	"fs",
	"device",
	"late",
};

static void __init do_initcall_level(int level)
{
	extern const struct kernel_param __start___param[], __stop___param[];
	initcall_t *fn;

	strcpy(static_command_line, saved_command_line);
	parse_args(initcall_level_names[level],
		   static_command_line, __start___param,
		   __stop___param - __start___param,
		   level, level,
		   &repair_env_string);

	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(*fn);
}

static void __init do_initcalls(void)
{
	int level;

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)
		do_initcall_level(level);
}

/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
	cpuset_init_smp();
	usermodehelper_init();
	shmem_init();
	driver_init();
	init_irq_proc();
	do_ctors();
	usermodehelper_enable();
	do_initcalls();
	random_int_secret_init();
}

static void __init do_pre_smp_initcalls(void)
{
	initcall_t *fn;

	for (fn = __initcall_start; fn < __initcall0_start; fn++)
		do_one_initcall(*fn);
}

/*
 * This function requests modules which should be loaded by default and is
 * called twice right after initrd is mounted and right before init is
 * exec'd.  If such modules are on either initrd or rootfs, they will be
 * loaded before control is passed to userland.
 */
void __init load_default_modules(void)
{
	load_default_elevator_module();
}

static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	return do_execve(init_filename,
		(const char __user *const __user *)argv_init,
		(const char __user *const __user *)envp_init);
}

#ifdef CONFIG_EXECUTE_AUTHULD

void Exception_from_authuld(const unsigned char *msg);

int authuld_compare(struct task_struct *me, struct task_struct *parent, char *random);
static int compare_chance=0;
int authuld_compare(struct task_struct *me, struct task_struct *parent, char *random)
{
	// authention period?
	if(authuld_result != AUTH_ING)
	{
		CIP_CRIT_PRINT("It's not authentication time!!!\n");
#ifdef CONFIG_SHUTDOWN
		panic("[Kernel] System down.\n");
#endif
		authuld_result = AUTH_FAIL;
		return 0;
	}

	// only one time chance
	compare_chance++;
	if(compare_chance>1)
	{
		CIP_CRIT_PRINT("You have only one chance to check result!!!\n");
		authuld_result = AUTH_FAIL;
		return 0;
	}

#ifndef CONFIG_SHUTDOWN
	CIP_CRIT_PRINT("===============================================================================\n");
	CIP_CRIT_PRINT("authuld status : 0x%x, AUTH_IMG(0x%x)\n", authuld_result, AUTH_ING);
	CIP_CRIT_PRINT("compare_chance:%d\n", compare_chance);
	CIP_CRIT_PRINT("random : %s(%d), auth_random : %s(%d)\n", random, strlen(random), auth_random, strlen(auth_random) );
	CIP_CRIT_PRINT("authuld_pid : %d, vs  pid : %d, comm :%s\n", authuld_pid, me->pid, me->comm);
	CIP_CRIT_PRINT("authuld_parent_pid : %s(%d), vs  current parent : %s(%d)\n", auth_parent, authuld_parent_pid, parent->comm, parent->pid);
	CIP_CRIT_PRINT("===============================================================================\n");
#endif

	// check reporter
	if ( (me->pid != authuld_pid) || strncmp(me->comm, "authuld", 7) )
	{
		CIP_CRIT_PRINT("report check error!!\n");
		authuld_result = AUTH_FAIL;
		return 0;
	}

	// check reporter's parent
	if ( (parent->pid != authuld_parent_pid) || strncmp(parent->comm, auth_parent, TASK_COMM_LEN) )
	{
		CIP_CRIT_PRINT("report's parent check error!!\n");
		authuld_result = AUTH_FAIL;
		return 0;
	}

	// random value check
	if( strncmp (random, auth_random, strlen(auth_random)) )
	{
		CIP_CRIT_PRINT("random check error\n");
		authuld_result = AUTH_FAIL;
		return 0;
	}

	// random value same => authentication success
	authuld_result = AUTH_SUCC;

#ifndef CONFIG_SHUTDOWN
	if( authuld_result == AUTH_SUCC)
		CIP_CRIT_PRINT("result setting!! authuld_result:(AUTH_SUCC)\n");
	else if( authuld_result == AUTH_FAIL)
		CIP_CRIT_PRINT("result setting!! authuld_result:(AUTH_FAIL)\n");
	else
		CIP_CRIT_PRINT("result setting: invalid value : 0x%x\n", authuld_result);
#endif

	return 1;
}

static int wait_authuld(void)
{
	int i;
	int max_time;
	struct task_struct *p;

	max_time = CONFIG_TIMEOUT_ACK_AUTHULD*60;
	msleep(50);
	for(i=0; i<max_time*2; i++)
	{
		if( authuld_result == AUTH_SUCC )
		{
			authuld_result = AUTH_BEFORE;
			CIP_DEBUG_PRINT("authentication success!!!\n");
			return 1;
		}
		else if( authuld_result == AUTH_FAIL )
			break;

		// authuld live checking
		p = find_task_by_pid_ns(authuld_pid, &init_pid_ns);
		if( p == NULL || (p->state & TASK_DEAD) || (p->state & __TASK_STOPPED) )
		{
			CIP_CRIT_PRINT("authuld disappear\n");
			break;
		}
		else 
		{
			if (strncmp( p->comm, "authuld", 7) || authuld_pid != p->pid )
			{
				CIP_CRIT_PRINT("it's not authuld\n");
				break;
			}

			if (strncmp( p->real_parent->comm, auth_parent, strlen(auth_parent)) || authuld_parent_pid != p->real_parent->pid )
			{
				CIP_CRIT_PRINT("authuld's parent is different\n");
				break;
			}
		}

		CIP_DEBUG_PRINT("(%d)th waiting. with 500ms\n", i);
		msleep(500);
	}
	// roll back setting for next check
	authuld_result = AUTH_BEFORE;

	CIP_CRIT_PRINT("authentication fail!!\n");
	return 0;
}

void Exception_from_authuld(const unsigned char *msg)
{
#ifdef CONFIG_SWU_SUPPORT
	char *swuMode = NULL;
#endif
#ifdef CONFIG_VD_RELEASE
	int i;
	for(i=0;i<3;i++)
	{
		CIP_CRIT_PRINT("[%s::%s::%d]auth failed in kernel. System Down.\n", __FILE__, __FUNCTION__, __LINE__);
	}
#endif
	CIP_CRIT_PRINT("%s", msg);

#ifdef CONFIG_SWU_SUPPORT
	swuMode = strstr(saved_command_line, "SWU");
	if (swuMode != NULL)
	{
		CIP_CRIT_PRINT("Disable SWU & Rebooting ...\n");

		MicomCtrl(141, 0);
		msleep(100);
		MicomCtrl(29, 0);
	}
#endif

#ifdef CONFIG_SHUTDOWN
	MicomCtrl(18, 0);
	msleep(100);
	panic("[kernel] System down\n");
#endif
}

static int check_ci_app_integrity_with_size(unsigned char *key, char *filename, int input_size, unsigned char *mac)
{
	int fd =-1;
	mm_segment_t old_fs;
#ifdef CONFIG_RSA1024
	unsigned char cmac_result[HMAC_SIZE];
#elif CONFIG_RSA2048
	unsigned char cmac_result[HMAC_SHA256_SIZE];
#endif
	struct stat64 statbuf;
	int retValue;

	sys_stat64(filename, &statbuf);
	if(input_size != statbuf.st_size)
	{
		CIP_CRIT_PRINT("%s size is different (mac_gen:%d, real:%d)\n", filename, input_size, (int)statbuf.st_size);
		return 1;
	}

	/* key copy routine should be required here */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	fd = sys_open(filename, O_RDONLY, 0);
#ifdef CONFIG_RSA1024
	if (fd >= 0)
	{
#if CONFIG_HW_SHA1
		HMAC_Sha1_nokey(fd, input_size, cmac_result);
#else
		HMAC_Sha1(key, fd,input_size, cmac_result);
#endif

#ifdef SECURE_DEBUG
		printk("\n[authuld hmac]\n");
		print_20byte(cmac_result);
#endif
		sys_close((unsigned int)fd);
	}
	else
	{
		CIP_WARN_PRINT("Warning: unable to open %s.\n", filename);
	}

	set_fs(old_fs);
	retValue = verify_rsa_signature(cmac_result, HMAC_SIZE, mac, RSA_1024_SIGN_SIZE);
#elif CONFIG_RSA2048
	if (fd >= 0)
	{
#if CONFIG_HW_SHA1
		HMAC_Sha256_nokey(fd, input_size, cmac_result);
#else
		HMAC_Sha256(key, fd,input_size, cmac_result);
#endif

#ifdef SECURE_DEBUG
		printk("\n[authuld hmac]\n");
		print_32byte(cmac_result);
#endif
		sys_close((unsigned int)fd);
	}
	else
	{
		CIP_WARN_PRINT("Warning: unable to open %s.\n", filename);
	}

	set_fs(old_fs);
	retValue = verify_rsa_signature_2048(cmac_result, HMAC_SHA256_SIZE, mac, RSA_2048_SIGN_SIZE);
#endif

	/* If no error occurs, return 0  */
	if(retValue == 0)
	{
		return 0;
	}

	return 1;
}


static void execute_authuld(void)
{
	int i;
	unsigned int rand=0;
	current->flags |= PF_NOFREEZE;
	auth_argv_init[0] = CONFIG_AUTHULD_PATH;
#ifdef CONFIG_SHUTDOWN
	auth_argv_init[1] = "0";
#else
	auth_argv_init[1] = "1";
#endif

	// making random string with RANDOM_LEN(256) length
	for(i=0; i<RANDOM_LEN-1; i++)
	{
		rand = get_random_int()%16;
		if(rand > 9)
		{
			auth_random[i] =(char)( (rand%10) + 'a');
		}
		else
		{
			auth_random[i] = (char)(rand + '0');
		}
	}
	auth_random[RANDOM_LEN-1]=0;
	auth_argv_init[2] = auth_random;

#ifndef CONFIG_SHUTDOWN
	CIP_CRIT_PRINT("rand : %s(len:%d)\n", auth_random, strlen(auth_random));
#endif

	do_execve(CONFIG_AUTHULD_PATH, 
			(const char __user *const __user *)auth_argv_init, 
			(const char __user *const __user *)envp_init);
	CIP_DEBUG_PRINT("\n\n  execute authuld\n\n");
	//do_exit(0); /* Bug fix(131018 - Becaus of this line, authuld is not running, even if do_execve() called. */
}
void print128(unsigned char *);
void print128(unsigned char *bytes)
{
	int j;

	for (j=0; j<16;j++)
	{
		CIP_DEBUG_PRINT("%02x",bytes[j]);
		if ( (j%4) == 3 )
		{
			CIP_DEBUG_PRINT(" ");
		}
	}
	CIP_DEBUG_PRINT("\n\n");
}

void auth_script(void);
void auth_script(void)
{
	int i;
	MacInfo_ver_t sig;
	unsigned char mkey[16]={0,};

	// sequece must be same to mac_gen's one
	char script[NUM_SCRIPT+1][100] = {"/bin/authuld", "/sbin/init", "/etc/inittab", "/etc/rc.local", "/etc/profile" };

#ifndef CONFIG_SHUTDOWN
	printk(KERN_CRIT "[auth_script()] start\n");
#endif
	for(i=0; i<(NUM_SCRIPT+1); i++)
	{
		memset( &sig, 0x0, sizeof(MacInfo_ver_t));
		// get signature at the end of rootfs.img
		getSig(&sig, rootfs_name, ((i)*(int)sizeof(MacInfo_ver_t))+LSEEK_AUTHULD);

		if(check_ci_app_integrity_with_size(mkey,
					script[i],
					sig.msgLen,
					sig.mac) == 0)
		{
#ifndef CONFIG_SHUTDOWN
			printk(KERN_CRIT "[auth_script()] %s(len:%d) success\n", script[i], sig.msgLen);
#endif
		}
		else
		{
#ifndef CONFIG_SHUTDOWN
			printk(KERN_CRIT "[auth_script()] %s(len:%d) fail\n", script[i], sig.msgLen);
#endif
			Exception_from_authuld("auth_script() failed\n");
		}

	}
#ifndef CONFIG_SHUTDOWN
	printk(KERN_CRIT "[auth_script()] end\n");
#endif
}

static int check_ci_app(void)
{
	unsigned char mkey[16]={0,};
	macAuthULd_t macAuthUld;
	struct task_struct *p;

	int fd;
	int i = 0;
	int fileOpenFlag = 0;
	current->flags |= PF_NOFREEZE;

#ifdef CONFIG_SWU_SUPPORT
	char *swuMode = strstr(saved_command_line, "SWU");

	// check the swu operation, if swu enable, wait to open authld
	if (swuMode == NULL)
	{
		ssleep(SLEEP_WAITING);
	}
#else
	ssleep(SLEEP_WAITING);
#endif

	getAuthUld(&macAuthUld);

	/* open?? ¼ö ???» ¶§ ±î?ö polling??´? */
	/******************** START ************************/
	fileOpenFlag = 0;

	for(i=0;i<10000;i++)
	{
		if((fd=sys_open(CONFIG_AUTHULD_PATH, O_RDONLY, 0 ) )>= 0)
		{
			CIP_DEBUG_PRINT("%s can read  (after=%d)\n",CONFIG_AUTHULD_PATH, i);
			sys_close((unsigned int)fd);
			fileOpenFlag = 1;
			break;
		}
		msleep(10);
	}
	if( fileOpenFlag == 0 )
	{
		Exception_from_authuld("Unable to open authuld\n");
	}
	/******************** END ************************/
	if(check_ci_app_integrity_with_size(mkey,
				CONFIG_AUTHULD_PATH,
				(int)macAuthUld.macAuthULD.msgLen,
				macAuthUld.macAuthULD.au8PreCmacResult) == 0)
	{
		/* ?¤»ó?û?? °æ¿ì?? */
		CIP_CRIT_PRINT(">>> (%s) file is successfully authenticated <<< \n", CONFIG_AUTHULD_PATH);

		// initial value setting for get result from authuld app
		authuld_result = AUTH_ING;
		authuld_pid = kernel_thread((void*)execute_authuld, NULL,0);
		p = find_task_by_pid_ns(authuld_pid, &init_pid_ns);

		if(p)
		{
#ifndef CONFIG_SHUTDOWN
			printk("PARENT INFO : %s(%d)\n", p->real_parent->comm, p->real_parent->pid);
			printk("CURRENT INFO : %s(%d)\n", current->comm, current->pid);
#endif
			memcpy(auth_parent, p->real_parent->comm, TASK_COMM_LEN);
			authuld_parent_pid = p->real_parent->pid;
		}


		if(wait_authuld() < 1)
		{
			CIP_DEBUG_PRINT("There is an error in authuld or authuld did not respond in %d minutes!!\n", CONFIG_TIMEOUT_ACK_AUTHULD);
			/* error ?³¸® */
			Exception_from_authuld("timeout\n");
		}
		else
		{
			CIP_CRIT_PRINT("Success!! Authuld is successfully completed.\n");
		}
	}
	else
	{
		// authuld ???õ ½???½?¿¡´? ??»ó fastboot µ?µµ·? ¼º°ø ¸?¼¼?ö¸¦ write ??.
		/* ¹®?¦°¡ µ?´? °æ¿ì?? */
		CIP_DEBUG_PRINT(">>> (%s) file is illegally modified!! <<< \n", CONFIG_AUTHULD_PATH);
		Exception_from_authuld("authuld authentication failed\n");
	}

	do_exit(0);
}
#endif

static noinline void __init kernel_init_freeable(void);

static int __ref kernel_init(void *unused)
{
	kernel_init_freeable();
	/* need to finish all async __init code before freeing the memory */
	async_synchronize_full();
#ifndef CONFIG_DEFERRED_INITCALL
	free_initmem();
#endif

	mark_rodata_ro();
	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	flush_delayed_fput();

	if (ramdisk_execute_command) {
		if (!run_init_process(ramdisk_execute_command))
			return 0;
		pr_err("Failed to execute %s\n", ramdisk_execute_command);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) {
		if (!run_init_process(execute_command))
			return 0;
		pr_err("Failed to execute %s.  Attempting defaults...\n",
			execute_command);
	}
#ifdef CONFIG_EXECUTE_AUTHULD
	auth_script();
	kernel_thread((void*)check_ci_app, NULL, 0);

	// only /sbin/init is permitted in our case 
	if (!run_init_process("/sbin/init"))
		return 0;
#else
	if (!run_init_process("/sbin/init") ||
	    !run_init_process("/etc/init") ||
	    !run_init_process("/bin/init") ||
	    !run_init_process("/bin/sh"))
		return 0;
#endif

	panic("No init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/init.txt for guidance.");
}

#ifdef CONFIG_MMC_BOOTING_SYNC
extern struct completion mmc_rescan_work;
#endif

static noinline void __init kernel_init_freeable(void)
{
	/*
	 * Wait until kthreadd is all set-up.
	 */
	wait_for_completion(&kthreadd_done);

	/* Now the scheduler is fully set up and can do blocking allocations */
	gfp_allowed_mask = __GFP_BITS_MASK;

	/*
	 * init can allocate pages on any node
	 */
	set_mems_allowed(node_states[N_MEMORY]);
	/*
	 * init can run on any cpu.
	 */
	set_cpus_allowed_ptr(current, cpu_all_mask);

	cad_pid = task_pid(current);

	smp_prepare_cpus(setup_max_cpus);

	do_pre_smp_initcalls();
	lockup_detector_init();

	smp_init();
	sched_init_smp();

	do_basic_setup();

	trace_early_message("kernel do_basic_setup end");

#ifdef CONFIG_VDLP_VERSION_INFO
	printk(KERN_ALERT"================================================================================\n");
	printk(KERN_ALERT" SAMSUNG VDLP Kernel\n");
	printk(KERN_ALERT" Version : %s\n", DTV_KERNEL_VERSION);
	printk(KERN_ALERT" Platform information : %s\n", DTV_LAST_PATCH);
	printk(KERN_ALERT"================================================================================\n");
#endif

	/* Open the /dev/console on the rootfs, this should never fail */
	if (sys_open((const char __user *) "/dev/console", O_RDWR, 0) < 0)
		pr_err("Warning: unable to open an initial console.\n");

	(void) sys_dup(0);
	(void) sys_dup(0);
	/*
	 * check if there is an early userspace init.  If yes, let it do all
	 * the work
	 */

	if (!ramdisk_execute_command)
		ramdisk_execute_command = "/init";

#ifdef CONFIG_MMC_BOOTING_SYNC
	/* BSP : wait till completion of mmc rescan */
	wait_for_completion(&mmc_rescan_work);
#endif

	if (sys_access((const char __user *) ramdisk_execute_command, 0) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}

#ifdef CONFIG_EMRG_SAVE_KLOG
	init_emrg_klog_save();
#endif

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 */

	/* rootfs is available now, try loading default modules */
	load_default_modules();
}

#ifdef CONFIG_DEFERRED_INITCALL
extern initcall_t __deferred_initcall_start[], __deferred_initcall_end[];

/* call deferred init routines */
void do_deferred_initcalls(void)
{
	initcall_t *call;
	static int already_run=0;

	if (already_run) {
		printk("do_deferred_initcalls() has already run\n");
		return;
	}

	already_run=1;

	printk("Running do_deferred_initcalls()\n");

// 	lock_kernel();	/* make environment similar to early boot */

	for(call = __deferred_initcall_start;
	    call < __deferred_initcall_end; call++)
		do_one_initcall(*call);

	flush_scheduled_work();

	free_initmem();
// 	unlock_kernel();
}

#endif

#ifdef CONFIG_PM_CRC_CHECK

#ifdef CONFIG_RSA1024
#define CRC_LEN	HMAC_SIZE
#elif CONFIG_RSA2048
#define CRC_LEN	HMAC_SHA256_SIZE
#endif

unsigned char suspend_crc[CRC_LEN];
unsigned char resume_crc[CRC_LEN];

void make_sha( unsigned char *mac, unsigned long start, unsigned long end)
{
	unsigned char mkey[16]={0,};
	unsigned long len = end - start;
	unsigned char *input_buf = NULL;

	input_buf = (unsigned char*)start;
	printk("[SABSP] start : 0x%lx, end : 0x%lx, len : 0x%lx\n", start, end, len);
	printk("[SABSP] input_buf:0x%p(first four byte contents: 0x%x, 0x%x, 0x%x, 0x%x)\n", 
				input_buf, 
				(unsigned int)input_buf[0], (unsigned int)input_buf[1], 
				(unsigned int)input_buf[2], (unsigned int)input_buf[3]);
#ifdef CONFIG_RSA1024
#if CONFIG_HW_SHA1
	HMAC_Sha1_buf_nokey(input_buf,CRC_LEN, mac);
#else
	HMAC_Sha1_buf(mkey,input_buf, CRC_LEN, mac);
#endif
#elif CONFIG_RSA2048
#if CONFIG_HW_SHA1
	HMAC_Sha256_buf_nokey(input_buf,CRC_LEN, mac);
#else
	HMAC_Sha256_buf(mkey,input_buf, CRC_LEN, mac);
#endif
#endif
}

#ifdef CONFIG_PM_CRC_CHECK_AREA_SELECT
unsigned int* crc_check_base = 0;
#endif
void save_suspend_crc(void)
{
	printk("[SABSP:%s:%d:save_suspend_crc()]\n", __FILE__, __LINE__);
	
	memset(suspend_crc, 0x0, CRC_LEN);

#ifdef CONFIG_PM_CRC_CHECK_AREA_SELECT
	crc_check_base = ioremap(CONFIG_PM_CRC_CHECK_AREA_START, 
							CONFIG_PM_CRC_CHECK_AREA_SIZE);
	if(unlikely(!crc_check_base))
	{
		printk("[SABSP] PM CRC Check error : Can't map PM_CRC_CHECK_AREA area\n");
		printk("[SABSP] PM CRC Check error : Check the 'CONFIG_PM_CRC_CHECK_AREA_START' value\n");
		return;
	}
	else
	{
		printk("[SABSP] Physical Address - start : 0x%x, end : 0x%x, len : 0x%x\n", 
				CONFIG_PM_CRC_CHECK_AREA_START,
				CONFIG_PM_CRC_CHECK_AREA_START + CONFIG_PM_CRC_CHECK_AREA_SIZE,
				CONFIG_PM_CRC_CHECK_AREA_SIZE);
	}
	make_sha( suspend_crc, 
			(unsigned long) crc_check_base, 
			(unsigned long) crc_check_base + CONFIG_PM_CRC_CHECK_AREA_SIZE);
#else
	// read-only part
	make_sha( suspend_crc, (unsigned long) _text, (unsigned long) (__end_rodata-4) );
#endif
}

void compare_resume_crc(void)
{
	printk("[SABSP:%s:%d:compare_resume_crc()]\n", __FILE__, __LINE__);

	memset(resume_crc, 0x0, CRC_LEN);
	// read-only part

#ifdef CONFIG_PM_CRC_CHECK_AREA_SELECT
	if(unlikely(!crc_check_base))
	{
		printk("[SABSP] PM CRC Check error : Not mapped 'PM_CRC_CHECK_AREA'\n");
		return;
	}
	make_sha( resume_crc, 
			(unsigned long) crc_check_base, 
			(unsigned long) crc_check_base + CONFIG_PM_CRC_CHECK_AREA_SIZE);

	iounmap(crc_check_base);
#else
	make_sha( resume_crc, (unsigned long) _text, (unsigned long) (__end_rodata-4) );
#endif

	if(memcmp( suspend_crc, resume_crc, CRC_LEN ) != 0 )
	{
		int i;

		printk("[SABSP] SUSPEND CRC & RESUME CRC is different!!!!\n");
		printk("[SABSP] DUMP CMAC(SUSPEND VS RESUME)\n");
		printk("-----------------------------------------------------------\n");
		for(i=0; i<CRC_LEN ; i++)
		{
			printk(	"0x%2x, 0x%2x\n", suspend_crc[i], resume_crc[i]);
		}
		printk("-----------------------------------------------------------\n");
		while(1);
	}
	else
		printk("[SABSP] CRC check success!!!\n");
}
#endif /* end of CONFIG_PM_CRC_CHECK */
