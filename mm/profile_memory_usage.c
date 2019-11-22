#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/memcontrol.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

struct mem_size_stats {
	struct vm_area_struct *vma;
	unsigned long resident;
	unsigned long shared_clean;
	unsigned long shared_dirty;
	unsigned long private_clean;
	unsigned long private_dirty;
	unsigned long referenced;
	unsigned long anonymous;
	unsigned long anonymous_thp;
	unsigned long swap;
	unsigned long nonlinear;
	u64 pss;
};

struct smap_mem {
	unsigned long total;
	unsigned long vmag[VMAG_CNT];
};

extern int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			   struct mm_walk *walk);
#define PSS_SHIFT 12
#define K(x) ((x) << (PAGE_SHIFT-10))

/* A global variable is a bit ugly, but it keeps the code simple */
int sysctl_profile_memory_usage;
static int g_start_flag;
//static struct timer_list my_mod_timer;

unsigned long g_start_pss;
unsigned long g_start_gem;
unsigned long g_start_mali;
unsigned long g_start_kernel_mem;
unsigned long g_start_used;

unsigned long g_stop_pss;
unsigned long g_stop_gem;
unsigned long g_stop_mali;
unsigned long g_stop_kernel_mem;
unsigned long g_stop_used;

unsigned long g_max_pss;
unsigned long g_max_gem;
unsigned long g_max_mali;
unsigned long g_max_kernel_mem;
unsigned long g_max_used;

unsigned long g_min_pss;
unsigned long g_min_gem;
unsigned long g_min_mali;
unsigned long g_min_kernel_mem;
unsigned long g_min_used;

static int get_user_pss_rss(struct task_struct *p,
			    struct smap_mem *pss,
			    struct smap_mem *rss)
{
	int idx = 0;
	struct mm_struct *mm = NULL;
	struct vm_area_struct *vma = NULL;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pmd_entry = smaps_pte_range,
		.private = &mss,
	};

	if (!thread_group_leader(p))
		return 0;

	mm = get_task_mm(p);
	if (!mm) {
		return 0;
	}

	smaps_walk.mm = mm;

	if (!down_read_trylock(&mm->mmap_sem)) {
		pr_warn("Skipping task : %s\n", p->comm);
		mmput(mm);
		return 0;
	}

	vma = mm->mmap;
	if (!vma) {
		up_read(&mm->mmap_sem);
		mmput(mm);
		return 0;
	}

	/* Walk all VMAs to update various counters. */
	while (vma) {
		/* Ignore the huge TLB pages. */
		if (vma->vm_mm && !(vma->vm_flags & VM_HUGETLB)) {
			memset(&mss, 0, sizeof(struct mem_size_stats));
			mss.vma = vma;

			walk_page_range(vma->vm_start, vma->vm_end,
						&smaps_walk);
			pss->total +=
				(unsigned long)(mss.pss >> (10 + PSS_SHIFT));
			rss->total += (unsigned long)(mss.resident >> 10);

			idx = get_group_idx(vma);

			pss->vmag[idx] +=
				(unsigned long)(mss.pss >> (10 + PSS_SHIFT));
			rss->vmag[idx] += (unsigned long)(mss.resident >> 10);
		}
		vma = vma->vm_next;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);

	return 1;
}

static unsigned long get_pss_from_tasks(bool print)
{
	struct task_struct *p = NULL;
	struct smap_mem pss;
	struct smap_mem rss;
	unsigned long sum_of_pss = 0;

	rcu_read_lock();
	for_each_process(p) {
		memset(&pss, 0, sizeof(struct smap_mem));
		memset(&rss, 0, sizeof(struct smap_mem));

		if (get_user_pss_rss(p, &pss, &rss) == 0)
			continue;

		sum_of_pss += pss.total;
	};
	rcu_read_unlock();

	return sum_of_pss;
}

static unsigned long get_gem_mem(void)
{
	return 0;
}

static unsigned long get_mali_mem(void)
{
	return 0;
}

static unsigned long get_kernel_mem(bool print)
{
	struct vmalloc_usedinfo svmallocused;
	struct vmalloc_info vmi;
	unsigned long total;

	memset(&svmallocused, 0, sizeof(struct vmalloc_usedinfo));
	memset(&vmi, 0, sizeof(struct vmalloc_info));

	get_vmallocused(&svmallocused);
	get_vmalloc_info(&vmi);

	/* Caculate kernel memory usage.
	 * Slab(reclaimable+unreclaimable) + PageTable 
	 *  + Vmalloc(except vm_map_ram and ioremap) + Kernel Stack
	 */
	total = K(global_page_state(NR_SLAB_UNRECLAIMABLE)) 
	+ K(global_page_state(NR_SLAB_RECLAIMABLE)) 
	+ K(global_page_state(NR_PAGETABLE)) + ((vmi.used >> 10)
	- (svmallocused.ioremapsize >> 10)
	- (svmallocused.vmapramsize >> 10))
	+ global_page_state(NR_KERNEL_STACK) * (THREAD_SIZE/1024);

	if (print) {
		printk(KERN_INFO " [Kernel]\n");
		printk(KERN_INFO " Unreclaimable slab	:  %5lu  kbyte\n",
				K(global_page_state(NR_SLAB_UNRECLAIMABLE)));
		printk(KERN_INFO " Reclaimable slab	:  %5lu  kbyte\n",
				K(global_page_state(NR_SLAB_RECLAIMABLE)));
		printk(KERN_INFO " PageTable		:  %5lu  kbyte\n",
				K(global_page_state(NR_PAGETABLE)));
		printk(KERN_INFO " Vmalloc(except vm_map_ram and ioremap):  %5lu  kbyte\n",
				((vmi.used >> 10) - (svmallocused.ioremapsize >> 10) 
				- (svmallocused.vmapramsize >> 10)));
		printk(KERN_INFO " PageTable		:  %5lu  kbyte\n",
				K(global_page_state(NR_PAGETABLE)));
		printk(KERN_INFO " Kernel Stack		:  %5lu  kbyte\n",
				global_page_state(NR_KERNEL_STACK)*(THREAD_SIZE/1024));
	}

	return total;
}

static void profile(void)
{
	unsigned long current_pss = 0;
	unsigned long current_gem = 0;
	unsigned long current_mali = 0;
	unsigned long current_kernel_mem = 0;
	unsigned long current_used = 0;

	if (sysctl_profile_memory_usage == 2)
		current_pss = get_pss_from_tasks(true);
	else if (sysctl_profile_memory_usage == 1)
		current_pss = get_pss_from_tasks(false);
	current_gem = get_gem_mem();
	current_mali = get_mali_mem();
	current_kernel_mem = get_kernel_mem(false);
	current_used = (current_pss + current_gem 
			+ current_mali + current_kernel_mem);

	/* Set high,low peak values */
	if (g_max_pss < current_pss)
		g_max_pss = current_pss;
	if (g_min_pss > current_pss)
		g_min_pss = current_pss;

	if (g_max_gem < current_gem)
		g_max_gem = current_gem;
	if (g_min_gem > current_gem)
		g_min_gem = current_gem;

	if (g_max_mali < current_mali)
		g_max_mali = current_mali;
	if (g_min_mali > current_mali)
		g_min_mali = current_mali;

	if (g_max_kernel_mem < current_kernel_mem)
		g_max_kernel_mem = current_kernel_mem;
	if (g_min_kernel_mem > current_kernel_mem)
		g_min_kernel_mem = current_kernel_mem;

	if (g_max_used < current_used)
		g_max_used = current_used;
	if (g_min_used > current_used)
		g_min_used = current_used;
}

#include <linux/kthread.h>

int kthread_stop_flag;
static int kmemory_profiled(void *p)
{
	for ( ; ; ) {
		schedule_timeout(HZ/2);
		if(kthread_stop_flag)
			break;

		profile();
	}
	return 0;
}


static void profiling_start(void)
{
	if (g_start_flag == 1) {
		printk(KERN_INFO "Profiling already started.\n");
		return;
	}
	printk(KERN_INFO "Profiling Started...\n\n");
	g_start_flag = 1;

	/* Initialize global variables */
	g_start_pss = g_max_pss = g_min_pss = get_pss_from_tasks(true);
	g_start_kernel_mem = g_max_kernel_mem = g_min_kernel_mem =
		get_kernel_mem(false);
	g_start_gem = g_max_gem = g_min_gem = get_gem_mem();
	g_start_mali = g_max_mali = g_min_mali = get_mali_mem();
	g_start_used = g_max_used = g_min_used = (g_start_pss +
	g_start_kernel_mem + g_start_gem + g_start_mali);

	kthread_run(kmemory_profiled, NULL, "kmemory_profiled");
	kthread_stop_flag = 0;
}

static void profiling_stop(void)
{
	if (g_start_flag == 0) {
		printk(KERN_INFO "Profiling already stopped.\n");
		printk(KERN_INFO "Please start profiling first.\n");
		return;
	}
	printk(KERN_INFO "Profiling Stopped.\n\n");
	g_start_flag = 0;
	kthread_stop_flag = 1;

	g_stop_pss = get_pss_from_tasks(true);
	g_stop_kernel_mem = get_kernel_mem(false);
	g_stop_gem = get_gem_mem();
	g_stop_mali = get_mali_mem();
	g_stop_used = (g_stop_pss + g_stop_kernel_mem
			+ g_stop_gem + g_stop_mali);

	/* Set high,low peak values */
	if (g_max_pss < g_stop_pss)
		g_max_pss = g_stop_pss;
	if (g_min_pss > g_stop_pss)
		g_min_pss = g_stop_pss;

	if (g_max_gem < g_stop_gem)
		g_max_gem = g_stop_gem;
	if (g_min_gem > g_stop_gem)
		g_min_gem = g_stop_gem;

	if (g_max_mali < g_stop_mali)
		g_max_mali = g_stop_mali;
	if (g_min_mali > g_stop_mali)
		g_min_mali = g_stop_mali;

	if (g_max_kernel_mem < g_stop_kernel_mem)
		g_max_kernel_mem = g_stop_kernel_mem;
	if (g_min_kernel_mem > g_stop_kernel_mem)
		g_min_kernel_mem = g_stop_kernel_mem;

	if (g_max_used < g_stop_used)
		g_max_used = g_stop_used;
	if (g_min_used > g_stop_used)
		g_min_used = g_stop_used;
}

int profile_memory_usage_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (write) {
		if (sysctl_profile_memory_usage == 0)
			profiling_stop();
		else if (sysctl_profile_memory_usage == 1)
			profiling_start();
		else if (sysctl_profile_memory_usage == 2)
			profiling_start();
		else {
			printk(KERN_INFO "\nWrong! Input value should be"
			       " 0 or 1 or 2.\n");
			printk(KERN_INFO " 0 : stop to profile\n");
			printk(KERN_INFO " 1 : start to profile\n");
			printk(KERN_INFO " 2 : start to profile with "
			       "printing info\n");
		}
	}
	return 0;
}

static int memory_profile_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", 
			(g_stop_pss-g_start_pss)+(g_stop_gem-g_start_gem)+
			(g_stop_mali-g_start_mali)+(g_stop_kernel_mem-g_start_kernel_mem),
			g_max_used, 
			g_start_pss, g_start_gem, g_start_mali, g_start_kernel_mem,
			g_stop_pss-g_start_pss, g_stop_gem-g_start_gem, 
			g_stop_mali-g_start_mali, g_stop_kernel_mem-g_start_kernel_mem, 
			g_max_pss, g_max_gem, g_max_mali, g_max_kernel_mem);

	return 0;
}

static int memory_profile_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, memory_profile_proc_show, NULL);
}

static const struct file_operations memory_profile_proc_fops = {
	.open		= memory_profile_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_memory_profile_init(void)
{
	proc_create("memory_profile", 0, NULL, &memory_profile_proc_fops);
	return 0;
}
module_init(proc_memory_profile_init);
