#include <linux/fs.h>
#include <linux/hugetlb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/quicklist.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include "internal.h"

void __attribute__((weak)) arch_report_meminfo(struct seq_file *m)
{
}

typedef void (*print_zram_info_t)(struct seq_file*);
typedef size_t (*get_zram_used_t)(void);

struct _pt_zram_struct {
	print_zram_info_t pt_zram_info;
	spinlock_t pt_zram_lock;
	get_zram_used_t get_zram_used;
}pt_zram_struct;
EXPORT_SYMBOL(pt_zram_struct);

void _pt_zram_info(struct seq_file *m)
{
	spin_lock(&pt_zram_struct.pt_zram_lock);
	if(pt_zram_struct.pt_zram_info)
		pt_zram_struct.pt_zram_info(m);
	spin_unlock(&pt_zram_struct.pt_zram_lock);
}

size_t get_zram_info()
{
	size_t zram_used = 0;
	spin_lock(&pt_zram_struct.pt_zram_lock);
	if (pt_zram_struct.get_zram_used)
		zram_used = pt_zram_struct.get_zram_used();
	spin_unlock(&pt_zram_struct.pt_zram_lock);
	return zram_used >> 10;
}

#ifdef CONFIG_HDMA_DEVICE
phys_addr_t  hdma_declared_pages(void);
phys_addr_t  hdma_allocated_pages(void);
#endif

#ifdef CONFIG_VMALLOCUSED_PLUS
	extern void seq_printk_vmalloc_statistics(struct seq_file *m,
		struct vmalloc_info *vm_info, struct vmalloc_usedinfo *vm_used);
#else
	extern void seq_printk_vmalloc_statistics(struct seq_file *m,
		struct vmalloc_info *vm_info, void *vm_used);
#endif

static int meminfo_proc_show(struct seq_file *m, void *v)
{
	struct sysinfo i;
	unsigned long committed;
	unsigned long allowed;
	struct vmalloc_info vmi;
	long cached;
	unsigned long pages[NR_LRU_LISTS];
	int lru;
#ifdef CONFIG_MODULESUSED_PLUS
	extern void seq_printk_module_statistics(struct seq_file *m,
		struct vmalloc_info *vm_info, struct vmalloc_usedinfo *vm_used);
#endif

/*
 * display in kilobytes.
 */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	si_meminfo(&i);
	si_swapinfo(&i);
	committed = percpu_counter_read_positive(&vm_committed_as);
	allowed = ((totalram_pages - hugetlb_total_pages())
		* sysctl_overcommit_ratio / 100) + total_swap_pages;

	cached = global_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	get_vmalloc_info(&vmi);

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_page_state(NR_LRU_BASE + lru);

	/*
	 * Tagged format, for easy grepping and expansion.
	 */
	seq_printf(m,
		"MemTotal:       %8lu kB\n"
		"MemFree:        %8lu kB\n"
		"Buffers:        %8lu kB\n"
		"Cached:         %8lu kB\n"
		"SwapCached:     %8lu kB\n"
		"Active:         %8lu kB\n"
		"Inactive:       %8lu kB\n"
		"Active(anon):   %8lu kB\n"
		"Inactive(anon): %8lu kB\n"
		"Active(file):   %8lu kB\n"
		"Inactive(file): %8lu kB\n"
		"Unevictable:    %8lu kB\n"
		"Mlocked:        %8lu kB\n"
#ifdef CONFIG_HIGHMEM
		"HighTotal:      %8lu kB\n"
		"HighFree:       %8lu kB\n"
		"LowTotal:       %8lu kB\n"
		"LowFree:        %8lu kB\n"
#endif
#ifndef CONFIG_MMU
		"MmapCopy:       %8lu kB\n"
#endif
		"SwapTotal:      %8lu kB\n"
		"SwapFree:       %8lu kB\n"
		"Dirty:          %8lu kB\n"
		"Writeback:      %8lu kB\n"
		"AnonPages:      %8lu kB\n"
		"Mapped:         %8lu kB\n"
		"Shmem:          %8lu kB\n"
		"Slab:           %8lu kB\n"
		"SReclaimable:   %8lu kB\n"
		"SUnreclaim:     %8lu kB\n"
		"KernelStack:    %8lu kB\n"
		"PageTables:     %8lu kB\n"
#ifdef CONFIG_QUICKLIST
		"Quicklists:     %8lu kB\n"
#endif
		"NFS_Unstable:   %8lu kB\n"
		"Bounce:         %8lu kB\n"
		"WritebackTmp:   %8lu kB\n"
		"CommitLimit:    %8lu kB\n"
		"Committed_AS:   %8lu kB\n"
		,
		K(i.totalram),
		K(i.freeram),
		K(i.bufferram),
		K(cached),
		K(total_swapcache_pages()),
		K(pages[LRU_ACTIVE_ANON]   + pages[LRU_ACTIVE_FILE]),
		K(pages[LRU_INACTIVE_ANON] + pages[LRU_INACTIVE_FILE]),
		K(pages[LRU_ACTIVE_ANON]),
		K(pages[LRU_INACTIVE_ANON]),
		K(pages[LRU_ACTIVE_FILE]),
		K(pages[LRU_INACTIVE_FILE]),
		K(pages[LRU_UNEVICTABLE]),
		K(global_page_state(NR_MLOCK)),
#ifdef CONFIG_HIGHMEM
		K(i.totalhigh),
		K(i.freehigh),
		K(i.totalram-i.totalhigh),
		K(i.freeram-i.freehigh),
#endif
#ifndef CONFIG_MMU
		K((unsigned long) atomic_long_read(&mmap_pages_allocated)),
#endif
		K(i.totalswap),
		K(i.freeswap),
		K(global_page_state(NR_FILE_DIRTY)),
		K(global_page_state(NR_WRITEBACK)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		K(global_page_state(NR_ANON_PAGES)
		  + global_page_state(NR_ANON_TRANSPARENT_HUGEPAGES) *
		  HPAGE_PMD_NR),
#else
		K(global_page_state(NR_ANON_PAGES)),
#endif
		K(global_page_state(NR_FILE_MAPPED)),
		K(global_page_state(NR_SHMEM)),
		K(global_page_state(NR_SLAB_RECLAIMABLE) +
				global_page_state(NR_SLAB_UNRECLAIMABLE)),
		K(global_page_state(NR_SLAB_RECLAIMABLE)),
		K(global_page_state(NR_SLAB_UNRECLAIMABLE)),
		global_page_state(NR_KERNEL_STACK) * THREAD_SIZE / 1024,
		K(global_page_state(NR_PAGETABLE)),
#ifdef CONFIG_QUICKLIST
		K(quicklist_total_size()),
#endif
		K(global_page_state(NR_UNSTABLE_NFS)),
		K(global_page_state(NR_BOUNCE)),
		K(global_page_state(NR_WRITEBACK_TEMP)),
		K(allowed),
		K(committed));

	seq_printk_vmalloc_statistics(m, &vmi, NULL);

#ifdef CONFIG_MODULESUSED_PLUS
	seq_printk_module_statistics(m, &vmi, NULL);
#endif

	seq_printf(m,
		""
#ifdef CONFIG_MEMORY_FAILURE
		"HardwareCorrupted: %5lu kB\n"
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		"AnonHugePages:  %8lu kB\n"
#endif
#ifdef CONFIG_CMA
		"FreeCMAPages    %8lu kB\n"
#endif
#ifdef CONFIG_HDMA_DEVICE
		"HDMADeclared:   %8lu kB\n"
		"HDMAAllocated:  %8lu kB\n"
#endif
#ifdef CONFIG_MEMORY_FAILURE
		,atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10)
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		, K(global_page_state(NR_ANON_TRANSPARENT_HUGEPAGES) *
		    HPAGE_PMD_NR)
#endif
#ifdef CONFIG_CMA
		, K(global_page_state(NR_FREE_CMA_PAGES))
#endif
#ifdef CONFIG_HDMA_DEVICE
		, (unsigned long) K(hdma_declared_pages())
		, (unsigned long) K(hdma_allocated_pages())
#endif
		);

	hugetlb_report_meminfo(m);

	_pt_zram_info(m);

	arch_report_meminfo(m);

	return 0;
#undef K
}

static int meminfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, meminfo_proc_show, NULL);
}

static const struct file_operations meminfo_proc_fops = {
	.open		= meminfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_meminfo_init(void)
{
	proc_create("meminfo", 0, NULL, &meminfo_proc_fops);
	spin_lock_init(&pt_zram_struct.pt_zram_lock);
	return 0;
}
module_init(proc_meminfo_init);

#ifdef CONFIG_VD_MEMINFO
#include <linux/shmem_fs.h>
extern void dump_tasks_plus(const struct mem_cgroup *mem, struct seq_file *s);
static void show_kernel_memory(struct seq_file *m)
{
	struct sysinfo i;
	struct vmalloc_usedinfo vmallocused;
	struct vmalloc_info vmi;
	long cached;

	si_meminfo(&i);

	cached = global_page_state(NR_FILE_PAGES) -
		 total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	memset(&vmallocused, 0, sizeof(struct vmalloc_usedinfo));
	get_vmallocused(&vmallocused);

	get_vmalloc_info(&vmi);

#define K(x) ((x) << (PAGE_SHIFT - 10))
	seq_printf(m, "\nKERNEL MEMORY INFO\n");
	seq_printf(m, "=================\n");
	seq_printf(m, "Buffers\t\t\t: %7luK\n", K(i.bufferram));
	seq_printf(m, "Cached\t\t\t: %7luK\n", K(cached));
	seq_printf(m, "SwapCached\t\t: %7luK\n", K(total_swapcache_pages()));
	seq_printf(m, "Anonymous Memory\t: %7luK\n",
		   K(global_page_state(NR_ANON_PAGES)));
	seq_printf(m, "Slab\t\t\t: %7luK (Unreclaimable : %luK)\n",
		   K(global_page_state(NR_SLAB_RECLAIMABLE) +
		     global_page_state(NR_SLAB_UNRECLAIMABLE)),
		   K(global_page_state(NR_SLAB_UNRECLAIMABLE)));
	seq_printf(m, "PageTable\t\t: %7luK\n",
		   K(global_page_state(NR_PAGETABLE)));
	seq_printf(m, "VmallocUsed\t\t: %7luK (ioremap : %luK)\n",
		   vmi.used >> 10, vmallocused.ioremapsize >> 10);
	seq_printf(m, "Kernel Stack\t\t: %7luK\n",
		   global_page_state(NR_KERNEL_STACK)*(THREAD_SIZE/1024));
	seq_printf(m, "kernel Total\t\t: %7luK\n", K(i.bufferram) + K(cached)
		   + K(total_swapcache_pages())
		   + K(global_page_state(NR_ANON_PAGES))
		   + K(global_page_state(NR_SLAB_RECLAIMABLE) +
		       global_page_state(NR_SLAB_UNRECLAIMABLE))
		   + K(global_page_state(NR_PAGETABLE))
		   + ((vmi.used>>10) - (vmallocused.ioremapsize>>10))
		   + global_page_state(NR_KERNEL_STACK)*(THREAD_SIZE/1024));

	seq_printf(m, "MemTotal\t\t: %7luK\n", K(i.totalram));
	seq_printf(m, "MemTotal-MemFree\t: %7luK\n", K(i.totalram-i.freeram));
	seq_printf(m, "Unevictable\t\t: %7luK\n",
		   K(global_page_state(NR_UNEVICTABLE)));
	seq_printf(m, "Unmapped PageCache\t: %7luK\n",
		   K(cached-global_page_state(NR_FILE_MAPPED)));
#undef K
}

static int vd_meminfo_proc_show(struct seq_file *m, void *v)
{
	dump_tasks_plus(NULL, m);
	show_kernel_memory(m);

	return 0;
}

static int vd_meminfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, vd_meminfo_proc_show, NULL);
}

static const struct file_operations vd_meminfo_proc_fops = {
	.open           = vd_meminfo_proc_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init proc_vd_meminfo_init(void)
{
	proc_create("vd_meminfo", 0, NULL, &vd_meminfo_proc_fops);
	return 0;
}
module_init(proc_vd_meminfo_init);

#define BUF_SIZE	256
static int print_proc_file_data(char *path, mode_t mode)
{
	int ret;
	static char *buf;
	mm_segment_t oldfs;
	struct file *fp;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	buf = kzalloc(BUF_SIZE, GFP_KERNEL);

	if (!buf) {
		set_fs(oldfs);
		return -ENOMEM;
	}

	fp = filp_open(path, O_RDONLY , mode);	
	if (IS_ERR(fp)) {
		pr_err("%s : open error \n", path);
		ret = -EINVAL;
		goto error;
	}

	while ((ret = fp->f_op->read(fp, buf, BUF_SIZE, &fp->f_pos)) > 0){
		pr_cont("%s", buf);
		memset(buf, 0, BUF_SIZE);
	}

	filp_close(fp, NULL);
error:
	kfree(buf);
	set_fs(oldfs);

	return ret;
}

/* When oom happen this function show additional infomation
 * for memory usage and memory fragmentation.
 */
void show_additional_info_for_oom(void)
{
	struct sysinfo j;
	struct vmalloc_info vmi;
	long cached;
	s64 total_used = 0;
	struct super_block *sb;
	struct shmem_sb_info *sbinfo;
        
#define K(x) ((x) << (PAGE_SHIFT-10))
	si_meminfo(&j);
	cached = global_page_state(NR_FILE_PAGES) -
		total_swapcache_pages() - j.bufferram;
	if (cached < 0)
		cached = 0;

	get_vmalloc_info(&vmi);

	/* Calculate memory usage from tmpfs */
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if(!strcmp(sb->s_type->name,"tmpfs")){ 
			sbinfo = (struct shmem_sb_info *)(sb->s_fs_info);
			if(sbinfo)
				total_used = total_used + ((percpu_counter_read(&sbinfo->used_blocks)) << (PAGE_CACHE_SHIFT - 10));
		}
	}
	spin_unlock(&sb_lock);
      
	pr_info("tmpfs used memory     : %8lld (kB)\n" 
		"Buffers               : %8lu (kB)\n"
		"Cached                : %8lu (kB)\n"
		"Shmem                 : %8lu (kB)\n",
		total_used,
		K(j.bufferram),
		K(cached),
		K(global_page_state(NR_SHMEM)));

	seq_printk_vmalloc_statistics(NULL, &vmi, NULL);

	pr_info("========================================================\n");

	print_proc_file_data("/proc/mounts", 0600);

	printk(KERN_INFO "\n========================================================\n");

	print_proc_file_data("/proc/pagetypeinfo", 0600);

	printk(KERN_INFO "\n========================================================\n");

}
#endif
