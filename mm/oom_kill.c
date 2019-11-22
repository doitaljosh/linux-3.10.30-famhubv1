/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *  Copyright (C)  2010  Google, Inc.
 *	Rewritten by David Rientjes
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from __alloc_pages()
 *  in mm/page_alloc.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/cpuset.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/freezer.h>
#include <linux/ftrace.h>
#include <linux/ratelimit.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#define CREATE_TRACE_POINTS
#include <trace/events/oom.h>

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
#include <linux/coredump.h>
#endif

int sysctl_panic_on_oom;
int sysctl_oom_kill_allocating_task;
int sysctl_oom_dump_tasks = 1;
static DEFINE_SPINLOCK(zone_scan_lock);

#define MB(pages) ((K(pages))>>10)
#define K(pages) ((pages) << (PAGE_SHIFT - 10))

static int oom_min_mb=INT_MAX, oom_max_mb= 0;

void dump_tasks(const struct mem_cgroup *mem, const nodemask_t *nodemask, struct seq_file *s);

#ifdef CONFIG_NUMA
/**
 * has_intersects_mems_allowed() - check task eligiblity for kill
 * @start: task struct of which task to consider
 * @mask: nodemask passed to page allocator for mempolicy ooms
 *
 * Task eligibility is determined by whether or not a candidate task, @tsk,
 * shares the same mempolicy nodes as current if it is bound by such a policy
 * and whether or not it has the same set of allowed cpuset nodes.
 */
static bool has_intersects_mems_allowed(struct task_struct *start,
					const nodemask_t *mask)
{
	struct task_struct *tsk;
	bool ret = false;

	rcu_read_lock();
	for_each_thread(start, tsk) {
		if (mask) {
			/*
			 * If this is a mempolicy constrained oom, tsk's
			 * cpuset is irrelevant.  Only return true if its
			 * mempolicy intersects current, otherwise it may be
			 * needlessly killed.
			 */
			ret = mempolicy_nodemask_intersects(tsk, mask);
		} else {
			/*
			 * This is not a mempolicy constrained oom, so only
			 * check the mems of tsk's cpuset.
			 */
			ret = cpuset_mems_allowed_intersects(current, tsk);
		}
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}
#else
static bool has_intersects_mems_allowed(struct task_struct *tsk,
					const nodemask_t *mask)
{
	return true;
}
#endif /* CONFIG_NUMA */

/*
 * The process p may have detached its own ->mm while exiting or through
 * use_mm(), but one or more of its subthreads may still have a valid
 * pointer.  Return p, or any of its subthreads with a valid ->mm, with
 * task_lock() held.
 */
struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t;

	rcu_read_lock();

	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();

	return t;
}
#ifdef CONFIG_VD_LOW_MEMORY_KILLER
EXPORT_SYMBOL(find_lock_task_mm);
#endif

/* return true if the task is not adequate as candidate victim task. */
static bool oom_unkillable_task(struct task_struct *p,
		const struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	if (is_global_init(p))
		return true;
	if (p->flags & PF_KTHREAD)
		return true;

	/* When mem_cgroup_out_of_memory() and p is not member of the group */
	if (memcg && !task_in_mem_cgroup(p, memcg))
		return true;

	/* p may not have freeable memory in nodemask */
	if (!has_intersects_mems_allowed(p, nodemask))
		return true;

	return false;
}

/**
 * oom_badness - heuristic function to determine which candidate task to kill
 * @p: task struct of which task we should calculate
 * @totalpages: total present RAM allowed for page allocation
 *
 * The heuristic for determining which task to kill is made to be as simple and
 * predictable as possible.  The goal is to return the highest value for the
 * task consuming the most memory to avoid subsequent oom failures.
 */
unsigned long oom_badness(struct task_struct *p, struct mem_cgroup *memcg,
			  const nodemask_t *nodemask, unsigned long totalpages, unsigned long *ptask_size)
{
	long points= 0;
	long adj;

	if (oom_unkillable_task(p, memcg, nodemask))
		return 0;

	p = find_lock_task_mm(p);
	if (!p)
		return 0;

	adj = (long)p->signal->oom_score_adj;
	if (adj == OOM_SCORE_ADJ_MIN) {
		task_unlock(p);
		return 0;
	}


	/*
	 * The baseline for the badness score is the proportion of RAM that each
	 * task's rss, pagetable and swap space use.
	 */
#if(0)	 
	points = get_mm_rss(p->mm) + p->mm->nr_ptes +
		 get_mm_counter(p->mm, MM_SWAPENTS);
#else
    *ptask_size= get_mm_rss(p->mm) + p->mm->nr_ptes +
		 get_mm_counter(p->mm, MM_SWAPENTS);
#endif
	task_unlock(p);

	/*
	 * Root processes get 3% bonus, just like the __vm_enough_memory()
	 * implementation used by LSMs.
	 */
#if(0)	 
	if (has_capability_noaudit(p, CAP_SYS_ADMIN))
		points -= (points * 3) / 100;

	/* Normalize to oom_score_adj units */
	adj *= totalpages / 1000;
	points += adj;
#else
    points= adj + abs(OOM_SCORE_ADJ_MIN) + 1;   //not 0
#endif


	/*
	 * Never return 0 for an eligible task regardless of the root bonus and
	 * oom_score_adj (oom_score_adj can't be OOM_SCORE_ADJ_MIN here).
	 */
	return points > 0 ? points : 1;
}

/*
 * Determine the type of allocation constraint.
 */
#ifdef CONFIG_NUMA
static enum oom_constraint constrained_alloc(struct zonelist *zonelist,
				gfp_t gfp_mask, nodemask_t *nodemask,
				unsigned long *totalpages)
{
	struct zone *zone;
	struct zoneref *z;
	enum zone_type high_zoneidx = gfp_zone(gfp_mask);
	bool cpuset_limited = false;
	int nid;

	/* Default to all available memory */
	*totalpages = totalram_pages + total_swap_pages;

	if (!zonelist)
		return CONSTRAINT_NONE;
	/*
	 * Reach here only when __GFP_NOFAIL is used. So, we should avoid
	 * to kill current.We have to random task kill in this case.
	 * Hopefully, CONSTRAINT_THISNODE...but no way to handle it, now.
	 */
	if (gfp_mask & __GFP_THISNODE)
		return CONSTRAINT_NONE;

	/*
	 * This is not a __GFP_THISNODE allocation, so a truncated nodemask in
	 * the page allocator means a mempolicy is in effect.  Cpuset policy
	 * is enforced in get_page_from_freelist().
	 */
	if (nodemask && !nodes_subset(node_states[N_MEMORY], *nodemask)) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, *nodemask)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_MEMORY_POLICY;
	}

	/* Check this allocation failure is caused by cpuset's wall function */
	for_each_zone_zonelist_nodemask(zone, z, zonelist,
			high_zoneidx, nodemask)
		if (!cpuset_zone_allowed_softwall(zone, gfp_mask))
			cpuset_limited = true;

	if (cpuset_limited) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, cpuset_current_mems_allowed)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_CPUSET;
	}
	return CONSTRAINT_NONE;
}
#else
static enum oom_constraint constrained_alloc(struct zonelist *zonelist,
				gfp_t gfp_mask, nodemask_t *nodemask,
				unsigned long *totalpages)
{
	*totalpages = totalram_pages + total_swap_pages;
	return CONSTRAINT_NONE;
}
#endif

enum oom_scan_t oom_scan_process_thread(struct task_struct *task,
		unsigned long totalpages, const nodemask_t *nodemask,
		bool force_kill)
{
	if (task->exit_state)
		return OOM_SCAN_CONTINUE;
	if (oom_unkillable_task(task, NULL, nodemask))
		return OOM_SCAN_CONTINUE;

	/*
	 * This task already has access to memory reserves and is being killed.
	 * Don't allow any other task to have access to the reserves.
	 */
	if (test_tsk_thread_flag(task, TIF_MEMDIE)) {
		if (unlikely(frozen(task)))
			__thaw_task(task);
		if (!force_kill)
			return OOM_SCAN_ABORT;
	}
	if (!task->mm)
		return OOM_SCAN_CONTINUE;

	/*
	 * If task is allocating a lot of memory and has been marked to be
	 * killed first if it triggers an oom, then select it.
	 */
	if (oom_task_origin(task))
		return OOM_SCAN_SELECT;

	if (task->flags & PF_EXITING && !force_kill) {
		/*
		 * If this task is not being ptraced on exit, then wait for it
		 * to finish before killing some other task unnecessarily.
		 */
		if (!(task->group_leader->ptrace & PT_TRACE_EXIT))
			return OOM_SCAN_ABORT;
	}
	return OOM_SCAN_OK;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'.
 *
 * (not docbooked, we don't want this one cluttering up the manual)
 */
static struct task_struct *select_bad_process(unsigned int *ppoints,
		unsigned long totalpages, const nodemask_t *nodemask,
		bool force_kill)
{
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	unsigned long chosen_points = 0;
	unsigned long chosen_task_size= 0, task_size=0;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		unsigned int points;

		switch (oom_scan_process_thread(p, totalpages, nodemask,
						force_kill)) {
		case OOM_SCAN_SELECT:
			chosen = p;
			chosen_points = ULONG_MAX;
			/* fall through */
		case OOM_SCAN_CONTINUE:
			continue;
		case OOM_SCAN_ABORT:
			rcu_read_unlock();
			return ERR_PTR(-1UL);
		case OOM_SCAN_OK:
			break;
		};
		points = oom_badness(p, NULL, nodemask, totalpages, &task_size);
		if (points > chosen_points) {
			chosen = p;
			chosen_points = points;
			chosen_task_size= task_size;
		}
		else if ( points==chosen_points && chosen_task_size<task_size) {
			chosen = p;
			chosen_points = points;
			chosen_task_size= task_size;
		}//else if
	}
	if (chosen)
		get_task_struct(chosen);
	rcu_read_unlock();

	*ppoints = chosen_points * 1000 / totalpages;
	return chosen;
}

#ifdef CONFIG_VD_MEMINFO
#include <linux/seq_file.h>
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

extern int smaps_pte_range(pmd_t *pmd, unsigned long addr,
			   unsigned long end, struct mm_walk *walk);

#define PSS_SHIFT 12


struct mm_sem_struct {
	char task_comm[TASK_COMM_LEN];
	pid_t tgid;
	struct mm_struct *mm;
	struct list_head list;
};


#ifdef CONFIG_VD_MEMINFO
extern int oom_score_to_adj(short oom_score_adj);
#endif

/**
 * dump_tasks - dump current memory state of all system tasks
 * @memcg: current's memory controller, if constrained
 * @nodemask: nodemask passed to page allocator for mempolicy ooms
 *
 * Dumps the current memory state of all eligible tasks.  Tasks not in the same
 * memcg, not in the same cpuset, or bound to a disjoint set of mempolicy nodes
 * are not shown.
 * State information includes task's pid, uid, tgid, vm size, rss, nr_ptes,
 * swapents, oom_score_adj value, and name.
 */
 
static inline unsigned long memnotify_get_reclaimable(void)
{
    unsigned long rec= 0;
    
#if(1)
	rec= global_page_state(NR_FILE_PAGES) -
		global_page_state(NR_SHMEM);
#else
	rec= global_page_state(NR_INACTIVE_FILE) +
	    global_page_state(NR_SLAB_RECLAIMABLE);
#endif	    
        
    return rec;
}

extern long buff_for_perf_kb;
static inline unsigned long memnotify_get_free(void)
{
	int other_free;

	other_free = global_page_state(NR_FREE_PAGES) 
	             -(buff_for_perf_kb/4);
	             
	if (other_free < 0)
		other_free = 0;

	return other_free;
}


void dump_tasks(const struct mem_cgroup *mem,
		       const nodemask_t *nodemask, struct seq_file *s)
{
	struct task_struct *p;
	struct task_struct *task;

#ifndef CONFIG_VD_MEMINFO
	pr_crit("[ pid ]   uid  tgid total_vm      rss nr_ptes swapents oom_score_adj name\n");
#else
	unsigned long cur_cnt[VMAG_CNT], max_cnt[VMAG_CNT];
	unsigned long tot_rss_cnt = 0;
	unsigned long tot_maxrss_cnt = 0;
	unsigned long sum_of_tot_rss_cnt = 0;
	unsigned long sum_of_tot_maxrss_cnt = 0;
	unsigned long sum_of_tot_swap_cnt = 0;
	int i;

	if (s)
		seq_printf(s, "[ pid ]   uid  tgid total_vm      rss  rss_max"
			     "   swap cpu       oom_adj   "
#ifdef CONFIG_LMK_PRELOAD_APP
			     "priority "
#endif
			     "name\n");
	else
		pr_crit("[ pid ]   uid  tgid total_vm      rss  rss_max"
				 "   swap cpu       oom_adj   "
#ifdef CONFIG_LMK_PRELOAD_APP
			         "priority "
#endif
				 "name\n");
#endif

	rcu_read_lock();
	for_each_process(p) {
		if (oom_unkillable_task(p, mem, nodemask))
			continue;

		task = find_lock_task_mm(p);
		if (!task) {
			/*
			 * This is a kthread or all of p's threads have already
			 * detached their mm's.  There's no need to report
			 * them; they can't be oom killed anyway.
			 */
			continue;
		}

#ifndef CONFIG_VD_MEMINFO
		pr_crit("[%5d] %5d %5d %8lu %8lu %7lu %8lu         %5hd %s\n",
			task->pid, from_kuid(&init_user_ns, task_uid(task)),
			task->tgid, task->mm->total_vm, get_mm_rss(task->mm),
			task->mm->nr_ptes,
			get_mm_counter(task->mm, MM_SWAPENTS),
			task->signal->oom_score_adj, task->comm);
#else
		for (i = 0; i < VMAG_CNT; i++) {
			get_rss_cnt(task->mm, i, &cur_cnt[i], &max_cnt[i]);
			tot_rss_cnt += cur_cnt[i];
			tot_maxrss_cnt += max_cnt[i];
		}

		if (s)
			seq_printf(s, "[%5d] %5d %5d %8lu %8lu %8lu %6lu %3d"
				      "     %3d(%4d)"
#ifdef CONFIG_LMK_PRELOAD_APP
				      " %3d"
#endif
				      "  %s\n",
				   p->pid, __task_cred(p)->uid, p->tgid,
				   K(task->mm->total_vm), K(tot_rss_cnt),
				   K(tot_maxrss_cnt),
				   K(get_mm_counter(task->mm, MM_SWAPENTS)),
				   (int)task_cpu(p), oom_score_to_adj(p->signal->oom_score_adj), p->signal->oom_score_adj,
#ifdef CONFIG_LMK_PRELOAD_APP
				   p->signal->lmk_preload_prio,
#endif
				   p->comm);
		else
			pr_crit("[%5d] %5d %5d %8lu %8lu %8lu %6lu %3d"
					 "     %3d(%4d)"
#ifdef CONFIG_LMK_PRELOAD_APP
					 " %3d"
#endif
					 "  %s\n",
			       p->pid, __task_cred(p)->uid, p->tgid,
			       K(task->mm->total_vm), K(tot_rss_cnt),
			       K(tot_maxrss_cnt),
			       K(get_mm_counter(task->mm, MM_SWAPENTS)),
			       (int)task_cpu(p), oom_score_to_adj(p->signal->oom_score_adj), p->signal->oom_score_adj,
#ifdef CONFIG_LMK_PRELOAD_APP
			       p->signal->lmk_preload_prio,
#endif
			       p->comm);

		sum_of_tot_rss_cnt += tot_rss_cnt;
		sum_of_tot_maxrss_cnt += tot_maxrss_cnt;
		sum_of_tot_swap_cnt += get_mm_counter(task->mm, MM_SWAPENTS);
		tot_rss_cnt = 0;
		tot_maxrss_cnt = 0;
#endif

		task_unlock(task);
	}
	rcu_read_unlock();

#ifdef CONFIG_VD_MEMINFO
	if (s) {
		seq_printf(s, "* Sum of rss    : %6lu (kB)\n",
			   K(sum_of_tot_rss_cnt));
		seq_printf(s, "* Sum of maxrss : %6lu (kB)\n",
			   K(sum_of_tot_maxrss_cnt));
		seq_printf(s, "* Sum of swap   : %6lu (kB)\n",
			   K(sum_of_tot_swap_cnt));
        seq_printf(s, "* Free:%luMB, Reclaim:%luMB, Swap:%luMB\n\n",
            MB(memnotify_get_free()),
            MB(memnotify_get_reclaimable()),
            MB(get_nr_swap_pages())
        );
    
	} else {
		pr_crit("* Sum of rss    : %6lu (kB)\n",
		       K(sum_of_tot_rss_cnt));
		pr_crit("* Sum of maxrss : %6lu (kB)\n",
		       K(sum_of_tot_maxrss_cnt));
		pr_crit("* Sum of swap   : %6lu (kB)\n",
		       K(sum_of_tot_swap_cnt));
        pr_crit("* Free:%luMB, Reclaim:%luMB, Swap:%luMB\n"
                "* OOM Min:%dMB, OOM Max:%dMB\n\n",
            MB(memnotify_get_free()),
            MB(memnotify_get_reclaimable()),
            MB(get_nr_swap_pages()),
            oom_min_mb!=INT_MAX?oom_min_mb:0, oom_max_mb
        );
	}
#endif
}


static unsigned long sum_of_pss;

/* Update the counters with the information retrieved from the vma
 * as well as the mem_size_stats as returned by walk_page_range(). */
static inline void update_dtp_counters(struct vm_area_struct *vma ,
				       struct mem_size_stats *mss,
				       struct smap_mem *vmem,
				       struct smap_mem *rss,
				       struct smap_mem *pss,
				       struct smap_mem *shared,
				       struct smap_mem *private,
				       struct smap_mem *swap
				      )
{
	int idx = 0;

	/* Add this to the process's consolidated totals. */
	vmem->total    += (vma->vm_end - vma->vm_start) >> 10;
	rss->total     += mss->resident >> 10;
	pss->total     += (unsigned long)(mss->pss >> (10 + PSS_SHIFT));
	shared->total  += (mss->shared_clean + mss->shared_dirty) >> 10;
	private->total += (mss->private_clean + mss->private_dirty) >> 10;
	swap->total    += mss->swap >> 10;

	/* Add this to the VMA's relevant counters, i.e., Code, Data, LibCode,
	* LibData, Stack or Other. Also classify them according to the following
	* memory types - vmem, rss, pss, shared, private and swap. */
	idx = get_group_idx(vma);
	vmem->vmag[idx]    += (vma->vm_end - vma->vm_start) >> 10;
	rss->vmag[idx]     += mss->resident >> 10;
	pss->vmag[idx]     += (unsigned long)(mss->pss >> (10 + PSS_SHIFT));
	shared->vmag[idx]  += (mss->shared_clean + mss->shared_dirty) >> 10;
	private->vmag[idx] += (mss->private_clean + mss->private_dirty) >> 10;
	swap->vmag[idx]    += mss->swap >> 10;
}

static inline void display_dtp_counters(struct seq_file *s,
					char *comm,
					pid_t tgid,
					struct mm_struct *mm,
					struct smap_mem *vmem,
					struct smap_mem *rss,
					struct smap_mem *pss,
					struct smap_mem *shared,
					struct smap_mem *private,
					struct smap_mem *swap
				       )
{
	if (s) {
		/* Display the consolidated counters for all the VMAs for
		 * this process. */
		seq_printf(s, "\nComm : %s,  Pid : %d\n", comm, tgid);
		seq_printf(s,
			   "=========================================================================\n"
		"                VMSize    Rss  Rss_max  Shared  Private    Pss   Swap\n"
		"  Process Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Code Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Data Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibCode Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibData Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		" Heap-BRK Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Stack Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Other Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n",
		vmem->total, rss->total,
		K(mm->max_rss[0]+mm->max_rss[1]+mm->max_rss[2]+
		mm->max_rss[3]+mm->max_rss[4]+mm->max_rss[5]+mm->max_rss[6]),
		shared->total, private->total, pss->total, swap->total,
		vmem->vmag[0], rss->vmag[0], K(mm->max_rss[0]),
		shared->vmag[0], private->vmag[0], pss->vmag[0], swap->vmag[0],
		vmem->vmag[1], rss->vmag[1], K(mm->max_rss[1]),
		shared->vmag[1], private->vmag[1], pss->vmag[1], swap->vmag[1],
		vmem->vmag[2], rss->vmag[2], K(mm->max_rss[2]),
		shared->vmag[2], private->vmag[2], pss->vmag[2], swap->vmag[2],
		vmem->vmag[3], rss->vmag[3], K(mm->max_rss[3]),
		shared->vmag[3], private->vmag[3], pss->vmag[3], swap->vmag[3],
		vmem->vmag[4], rss->vmag[4], K(mm->max_rss[4]),
		shared->vmag[4], private->vmag[4], pss->vmag[4], swap->vmag[4],
		vmem->vmag[5], rss->vmag[5], K(mm->max_rss[5]),
		shared->vmag[5], private->vmag[5], pss->vmag[5], swap->vmag[5],
		vmem->vmag[6], rss->vmag[6], K(mm->max_rss[6]),
		shared->vmag[6], private->vmag[6], pss->vmag[6], swap->vmag[6]
		);
	} else {
		/* Display the consolidated counters for all the VMAs for
		 * this process. */
		printk(KERN_INFO "\nComm : %s,  Pid : %d\n", comm, tgid);
		printk(KERN_INFO
		"=========================================================================\n"
		"                VMSize    Rss  Rss_max  Shared  Private    Pss   Swap\n"
		"  Process Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Code Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"     Data Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibCode Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"  LibData Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		" Heap-BRK Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Stack Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n"
		"    Other Cur: %7lu %6lu   %6lu  %6lu   %6lu %6lu %6lu (kB)\n",
		vmem->total, rss->total,
		K(mm->max_rss[0]+mm->max_rss[1]+mm->max_rss[2]+
		mm->max_rss[3]+mm->max_rss[4]+mm->max_rss[5]+mm->max_rss[6]),
		shared->total, private->total, pss->total, swap->total,
		vmem->vmag[0], rss->vmag[0], K(mm->max_rss[0]),
		shared->vmag[0], private->vmag[0], pss->vmag[0], swap->vmag[0],
		vmem->vmag[1], rss->vmag[1], K(mm->max_rss[1]),
		shared->vmag[1], private->vmag[1], pss->vmag[1], swap->vmag[1],
		vmem->vmag[2], rss->vmag[2], K(mm->max_rss[2]),
		shared->vmag[2], private->vmag[2], pss->vmag[2], swap->vmag[2],
		vmem->vmag[3], rss->vmag[3], K(mm->max_rss[3]),
		shared->vmag[3], private->vmag[3], pss->vmag[3], swap->vmag[3],
		vmem->vmag[4], rss->vmag[4], K(mm->max_rss[4]),
		shared->vmag[4], private->vmag[4], pss->vmag[4], swap->vmag[4],
		vmem->vmag[5], rss->vmag[5], K(mm->max_rss[5]),
		shared->vmag[5], private->vmag[5], pss->vmag[5], swap->vmag[5],
		vmem->vmag[6], rss->vmag[6], K(mm->max_rss[6]),
		shared->vmag[6], private->vmag[6], pss->vmag[6], swap->vmag[6]
		);
	}
}



/* Walk all VMAs to update various counters.
 * mm->mmap_sem must be read-taken
 */
static void dump_task(struct seq_file *s, struct mm_struct *mm, char *comm,
		      pid_t tgid)
{
	struct vm_area_struct *vma;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pgd_entry = NULL,
		.pud_entry = NULL,
		.pmd_entry = smaps_pte_range,
		.pte_entry = NULL,
		.pte_hole = NULL,
		.private = &mss,
	};
	struct smap_mem vmem;
	struct smap_mem rss;
	struct smap_mem pss;
	struct smap_mem shared;
	struct smap_mem private;
	struct smap_mem swap;

	smaps_walk.mm = mm;

	vma = mm->mmap;

	memset(&vmem    , 0, sizeof(struct smap_mem));
	memset(&rss     , 0, sizeof(struct smap_mem));
	memset(&shared  , 0, sizeof(struct smap_mem));
	memset(&private , 0, sizeof(struct smap_mem));
	memset(&pss     , 0, sizeof(struct smap_mem));
	memset(&swap    , 0, sizeof(struct smap_mem));

	while (vma) {
		/* Ignore the huge TLB pages. */
		if (vma->vm_mm && !(vma->vm_flags & VM_HUGETLB)) {
			memset(&mss, 0, sizeof(struct mem_size_stats));
			mss.vma = vma;

			walk_page_range(vma->vm_start, vma->vm_end,
					&smaps_walk);

			update_dtp_counters(vma, &mss, &vmem, &rss, &pss,
					    &shared, &private, &swap);
		}
		vma = vma->vm_next;
	}

	display_dtp_counters(s, comm, tgid, mm, &vmem, &rss, &pss,
			     &shared, &private, &swap);

	sum_of_pss += pss.total;
}






/* if oom_cond == 1 then OOM condition is considered */
static void dump_tasks_plus_oom(const struct mem_cgroup *mem,
				struct seq_file *s, int oom_cond)
{
#if(1)
	/* call original dump_tasks */
	dump_tasks(NULL, NULL, s);

	sum_of_pss = 0;
#else
	struct mm_struct *mm = NULL;
	struct vm_area_struct *vma = NULL;
	struct task_struct *g = NULL, *p = NULL;
	struct mm_sem_struct *mm_sem;
	struct list_head *pos, *q;
	struct list_head mm_sem_list = LIST_HEAD_INIT(mm_sem_list);


	/* lock current tasklist state */
	rcu_read_lock();

	/* call original dump_tasks */
	dump_tasks(NULL, NULL, s);

	sum_of_pss = 0;
	
	/* dump tasks with additional info */
	do_each_thread(g, p) {
		if (mem && !task_in_mem_cgroup(p, mem))
			continue;

		if (!thread_group_leader(p))
			continue;

		task_lock(p);
		mm = p->mm;
		if (!mm) {
			task_unlock(p);
			continue;
		}

		vma = mm->mmap;
		if (!vma) {
			task_unlock(p);
			continue;
		}

		if (down_read_trylock(&mm->mmap_sem)) {
			/* If we took semaphore then dump info here */
			dump_task(s, mm, p->comm, p->tgid);
			up_read(&mm->mmap_sem);
		} else {
			/* Else add mm to dump later list */
			mm_sem = kmalloc(sizeof(*mm_sem), GFP_ATOMIC);
			if (mm_sem == NULL) {
				printk(KERN_ERR "dump_tasks_plus: can't allocate struct mm_sem!");
				task_unlock(p);
				rcu_read_unlock();
				return;
			} else {
				memcpy(mm_sem->task_comm, p->comm,
					sizeof(p->comm));
				mm_sem->tgid = p->tgid;
				mm_sem->mm = mm;
				INIT_LIST_HEAD(&mm_sem->list);
				list_add(&mm_sem->list, &mm_sem_list);

				/* Increase mm usage counter to ensure mm is
				 * still valid without tasklock */
				atomic_inc(&mm->mm_users);
			}
		}

		task_unlock(p);
	} while_each_thread(g, p);

	/* unlock tasklist to take remaining semaphores */
	rcu_read_unlock();

	/* print remaining tasks info */
	list_for_each(pos, &mm_sem_list) {
		mm_sem = list_entry(pos, struct mm_sem_struct, list);

		mm = mm_sem->mm;

		if (oom_cond) {
			/* Can't wait for semaphore in OOM killer context. */
			if (down_read_trylock(&mm->mmap_sem)) {
				dump_task(s, mm, mm_sem->task_comm,
					  mm_sem->tgid);
				up_read(&mm->mmap_sem);
			} else {
				pr_warn("Skipping task with tgid = %d and comm "
				     "'%s'\n", mm_sem->tgid, mm_sem->task_comm);
			}
		} else {
			down_read(&mm->mmap_sem);
			dump_task(s, mm, mm_sem->task_comm, mm_sem->tgid);
			up_read(&mm->mmap_sem);
		}

		/* release mm */
		mmput(mm);
	}

	/* free mm_sem list */
	list_for_each_safe(pos, q, &mm_sem_list) {
		mm_sem = list_entry(pos, struct mm_sem_struct, list);
		list_del(pos);
		kfree(mm_sem);
	}
#endif

	if (s)
		seq_printf(s, "\n* Sum of pss : %6lu (kB)\n", sum_of_pss);
	else
		printk(KERN_INFO "\n* Sum of pss : %6lu (kB)\n", sum_of_pss);
}

void dump_tasks_plus(const struct mem_cgroup *mem, struct seq_file *s)
{
	dump_tasks_plus_oom(mem, s, 0);
}
#endif



#ifdef CONFIG_VD_MEMINFO
extern void show_additional_info_for_oom(void);
#endif

#ifdef CONFIG_VD_LOW_MEMORY_KILLER
extern void lmk_print_params(void);
#endif /* CONFIG_VD_LOW_MEMORY_KILLER */

#ifdef CONFIG_SHOW_SWPINF_OOM
void print_swaps(void);
#endif

#if defined(CONFIG_KNBD_SUPPORT)
DEFINE_MUTEX(oom_knbd_print_stats_mutex);
EXPORT_SYMBOL(oom_knbd_print_stats_mutex);
void (*oom_knbd_print_stats)(void) = NULL;
EXPORT_SYMBOL(oom_knbd_print_stats);
#endif
void _pt_zram_info(struct seq_file *m);

static void dump_header(struct task_struct *p, gfp_t gfp_mask, int order,
			struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	task_lock(current);
	pr_warning("%s invoked oom-killer: gfp_mask=0x%x, order=%d, "
		"oom_score_adj=%hd\n",
		current->comm, gfp_mask, order,
		current->signal->oom_score_adj);
	cpuset_print_task_mems_allowed(current);
	task_unlock(current);
	dump_stack();
	if (memcg)
		mem_cgroup_print_oom_info(memcg, p);
	else
		show_mem(SHOW_MEM_FILTER_NODES);
#ifdef CONFIG_VD_LOW_MEMORY_KILLER
	lmk_print_params();
#endif

#ifdef CONFIG_VD_MEMINFO
	show_additional_info_for_oom();
#endif
	if (sysctl_oom_dump_tasks)
#ifdef CONFIG_VD_MEMINFO
		dump_tasks_plus_oom(memcg, NULL, 1);
#else
		dump_tasks(memcg, nodemask, NULL);
#endif
	_pt_zram_info(NULL);

#ifdef CONFIG_SHOW_SWPINF_OOM
	print_swaps();
#endif

#if defined(CONFIG_KNBD_SUPPORT)
	mutex_lock(&oom_knbd_print_stats_mutex);
	if (oom_knbd_print_stats)
		oom_knbd_print_stats();
	mutex_unlock(&oom_knbd_print_stats_mutex);
#endif

	print_slabinfo_oom();
}

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
extern void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name);
#endif
#ifdef CONFIG_SAVELOGD
extern ssize_t save_write2(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);
#endif


/*
 * Must be called while holding a reference to p, which will be released upon
 * returning.
 */
void oom_kill_process(struct task_struct *p, gfp_t gfp_mask, int order,
		      unsigned int points, unsigned long totalpages,
		      struct mem_cgroup *memcg, nodemask_t *nodemask,
		      const char *message)
{
	struct task_struct *victim = p;
	struct task_struct *child;
	struct task_struct *t;
	struct mm_struct *mm;
	unsigned int victim_points = 0;
	unsigned long victim_task_size= 0;
	static DEFINE_RATELIMIT_STATE(oom_rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);
#ifdef CONFIG_KPI_SYSTEM_SUPPORT
#ifndef CONFIG_PLAT_TIZEN
#ifndef CONFIG_PROC_VD_TASK_LIST
	int i;
#endif
#endif
#endif

//#ifdef	CONFIG_ENABLE_OOM_DEBUG_LOGS
	int ori_console_loglevel = console_loglevel;
	console_loglevel = 7;
//#endif

	/*
	 * If the task is already exiting, don't alarm the sysadmin or kill
	 * its children or threads, just set TIF_MEMDIE so it can die quickly
	 */
	if (p->flags & PF_EXITING) {
		set_tsk_thread_flag(p, TIF_MEMDIE);
		put_task_struct(p);
		return;
	}

#if(0)
	if (__ratelimit(&oom_rs))
		dump_header(p, gfp_mask, order, memcg, nodemask);
#else
	//if (__ratelimit(&oom_rs))
        //dump_tasks(NULL, NULL, NULL);
        
    //Protect ->comm from prctl()
    task_lock(current);	
	pr_crit("pid:%d(%s) invoked oom_killer.\n",
	    task_pid_nr(current), current->comm
    );
    task_unlock(current);
#endif


//#ifdef	CONFIG_ENABLE_OOM_DEBUG_LOGS
	console_loglevel = ori_console_loglevel;
//#endif

	task_lock(p);
	pr_crit("%s: Kill process %d (%s) score %d or sacrifice child\n",
		message, task_pid_nr(p), p->comm, points);
	task_unlock(p);

#if(1)
	/*
	 * If any of p's children has a different mm and is eligible for kill,
	 * the one with the highest oom_badness() score is sacrificed for its
	 * parent.  This attempts to lose the minimal amount of work done while
	 * still freeing memory.
	 */
	read_lock(&tasklist_lock);
	for_each_thread(p, t) {
		list_for_each_entry(child, &t->children, sibling) {
			unsigned int child_points;
            unsigned long child_task_size= 0;
            
			if (child->mm == p->mm)
				continue;
			/*
			 * oom_badness() returns 0 if the thread is unkillable
			 */
			child_points = oom_badness(child, memcg, nodemask,
								totalpages, &child_task_size);
			if (child_points > victim_points) {
				put_task_struct(victim);
				victim = child;
				victim_points = child_points;
				victim_task_size= child_task_size;
				get_task_struct(victim);
			}
			else if(child_points == victim_points && victim_task_size < child_task_size) {
				put_task_struct(victim);
				victim = child;
				victim_points = child_points;
				victim_task_size= child_task_size;
				get_task_struct(victim);
			}//else if
		}
	}
	read_unlock(&tasklist_lock);
#endif

	p = find_lock_task_mm(victim);
	if (!p) {
		put_task_struct(victim);
		return;
	} else if (victim != p) {
		get_task_struct(p);
		put_task_struct(victim);
		victim = p;
	}


	/* mm cannot safely be dereferenced after task_unlock(victim) */
	mm = victim->mm;
	pr_crit("Kill process %d (%s) total-vm:%lukB, anon-rss:%lukB, file-rss:%lukB\n"
	        "Free:%luMB, Reclaim:%luMB, Swap:%luMB\n"
	        "OOM Min:%dMB, OOM Max:%dMB\n",
		task_pid_nr(victim), victim->comm, K(victim->mm->total_vm),
		K(get_mm_counter(victim->mm, MM_ANONPAGES)),
		K(get_mm_counter(victim->mm, MM_FILEPAGES)),
        MB(memnotify_get_free()),
        MB(memnotify_get_reclaimable()),
        MB(get_nr_swap_pages()),
        oom_min_mb!=INT_MAX?oom_min_mb:0, oom_max_mb
    );
    //dump_stack();
    pr_crit("\n");
    
        
#ifdef CONFIG_KPI_SYSTEM_SUPPORT
    #ifndef CONFIG_PLAT_TIZEN
        #ifdef CONFIG_PROC_VD_TASK_LIST
        	if (vd_policy_allow_task_check(victim->group_leader->comm))
        			set_kpi_fault(0, 0, "OOM", "OOM");
        #else
        	for (i=0 ; i < ALLOWED_TASK_NUM ; i++) {
        		if (!strncmp(victim->group_leader->comm, allowTask[i], TASK_COMM_LEN)){
        			set_kpi_fault(0, 0, "OOM", "OOM");	
        			break;
        		}
        	}
        #endif
    #else  /*ifdef CONFIG_PLAT_TIZEN */
    	set_kpi_fault(0, 0, "OOM", "OOM");
    #endif /*CONFIG_PLAT_TIZEN*/
#endif
	task_unlock(victim);
	
	
#ifdef CONFIG_SAVELOGD	
    save_write2(NULL, "Z r 0 OOM\0", 10, NULL);
#endif

	/*
	 * Kill all user processes sharing victim->mm in other thread groups, if
	 * any.  They don't get access to memory reserves, though, to avoid
	 * depletion of all memory.  This prevents mm->mmap_sem livelock when an
	 * oom killed thread cannot exit because it requires the semaphore and
	 * its contended by another thread trying to allocate memory itself.
	 * That thread will now get access to memory reserves since it has a
	 * pending fatal signal.
	 */
	rcu_read_lock();
	for_each_process(p)
		if (p->mm == mm && !same_thread_group(p, victim) &&
		    !(p->flags & PF_KTHREAD)) {
			if (p->signal->oom_score_adj == OOM_SCORE_ADJ_MIN)
				continue;
			task_lock(p);	/* Protect ->comm from prctl() */
			pr_crit("victim kills process %d (%s) sharing same memory\n",
				task_pid_nr(p), p->comm);
			task_unlock(p);
			do_send_sig_info(SIGKILL, SEND_SIG_FORCED, p, true);
		}
	rcu_read_unlock();

	set_tsk_thread_flag(victim, TIF_MEMDIE);
	do_send_sig_info(SIGKILL, SEND_SIG_FORCED, victim, true);
	put_task_struct(victim);
}


/*
 * Determines whether the kernel must panic because of the panic_on_oom sysctl.
 */
void check_panic_on_oom(enum oom_constraint constraint, gfp_t gfp_mask,
			int order, const nodemask_t *nodemask)
{
	if (likely(!sysctl_panic_on_oom))
		return;
	if (sysctl_panic_on_oom != 2) {
		/*
		 * panic_on_oom == 1 only affects CONSTRAINT_NONE, the kernel
		 * does not panic for cpuset, mempolicy, or memcg allocation
		 * failures.
		 */
		if (constraint != CONSTRAINT_NONE)
			return;
	}
	dump_header(NULL, gfp_mask, order, NULL, nodemask);
	panic("\n\nOut of memory: %s panic_on_oom is enabled\n",
		sysctl_panic_on_oom == 2 ? "compulsory" : "system-wide");
}

static BLOCKING_NOTIFIER_HEAD(oom_notify_list);

int register_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_oom_notifier);

int unregister_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_oom_notifier);

/*
 * Try to acquire the OOM killer lock for the zones in zonelist.  Returns zero
 * if a parallel OOM killing is already taking place that includes a zone in
 * the zonelist.  Otherwise, locks all zones in the zonelist and returns 1.
 */
int try_set_zonelist_oom(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;
	int ret = 1;

	spin_lock(&zone_scan_lock);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		if (zone_is_oom_locked(zone)) {
			ret = 0;
			goto out;
		}
	}

	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		/*
		 * Lock each zone in the zonelist under zone_scan_lock so a
		 * parallel invocation of try_set_zonelist_oom() doesn't succeed
		 * when it shouldn't.
		 */
		zone_set_flag(zone, ZONE_OOM_LOCKED);
	}

out:
	spin_unlock(&zone_scan_lock);
	return ret;
}

/*
 * Clears the ZONE_OOM_LOCKED flag for all zones in the zonelist so that failed
 * allocation attempts with zonelists containing them may now recall the OOM
 * killer, if necessary.
 */
void clear_zonelist_oom(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;

	spin_lock(&zone_scan_lock);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		zone_clear_flag(zone, ZONE_OOM_LOCKED);
	}
	spin_unlock(&zone_scan_lock);
}

/**
 * out_of_memory - kill the "best" process when we run out of memory
 * @zonelist: zonelist pointer
 * @gfp_mask: memory allocation flags
 * @order: amount of memory being requested as a power of 2
 * @nodemask: nodemask passed to page allocator
 * @force_kill: true if a task must be killed, even if others are exiting
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 */
void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask,
		int order, nodemask_t *nodemask, bool force_kill)
{
	const nodemask_t *mpol_mask;
	struct task_struct *p;
	unsigned long totalpages;
	unsigned long freed = 0;
	unsigned int uninitialized_var(points);
	enum oom_constraint constraint = CONSTRAINT_NONE;
	int killed = 0;

    int free_mb= MB(memnotify_get_free()) + MB(memnotify_get_reclaimable());


    if( current->signal->oom_score_adj<0 && free_mb<oom_min_mb ) {
        oom_min_mb= free_mb;
    }//if
    if( current->signal->oom_score_adj<0 && free_mb>oom_max_mb ) {
        #if(0)  //TODO
            char tc_pname[]="memtest";
            if( strncmp(current->comm, tc_pname, sizeof(tc_pname))!=0)
        #endif
        oom_max_mb= free_mb;
    }//if


	blocking_notifier_call_chain(&oom_notify_list, 0, &freed);
	if (freed > 0)
		/* Got some memory back in the last second. */
		return;

	/*
	 * If current has a pending SIGKILL or is exiting, then automatically
	 * select it.  The goal is to allow it to allocate so that it may
	 * quickly exit and free its memory.
	 */
	if (fatal_signal_pending(current) || current->flags & PF_EXITING) {
		set_thread_flag(TIF_MEMDIE);
		return;
	}

	/*
	 * Check if there were limitations on the allocation (only relevant for
	 * NUMA) that may require different handling.
	 */
	constraint = constrained_alloc(zonelist, gfp_mask, nodemask,
						&totalpages);
	mpol_mask = (constraint == CONSTRAINT_MEMORY_POLICY) ? nodemask : NULL;
	check_panic_on_oom(constraint, gfp_mask, order, mpol_mask);

	if (sysctl_oom_kill_allocating_task && current->mm &&
	    !oom_unkillable_task(current, NULL, nodemask) &&
	    current->signal->oom_score_adj != OOM_SCORE_ADJ_MIN) {
		get_task_struct(current);
		oom_kill_process(current, gfp_mask, order, 0, totalpages, NULL,
				 nodemask,
				 "Out of memory (oom_kill_allocating_task)");
		goto out;
	}

	p = select_bad_process(&points, totalpages, mpol_mask, force_kill);
	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (!p) {
        //dump_header(NULL, gfp_mask, order, NULL, mpol_mask);
        dump_tasks(NULL, NULL, NULL);
        
        #if(0)  //TODO
            do {
                DEFINE_SPINLOCK(mspinlock);
                unsigned long flags;
                int i;
                
                spin_lock_irqsave(&mspinlock, flags);
                do {
                    pr_crit("oom panic\n");
                    for(i=0; i<1000; i++) udelay(1000);
                } while(1);
                spin_unlock_irqrestore(&mspinlock, flags);
            } while(0);
        #endif
        
		panic("\n\nOut of memory and no killable processes...\n");
        //cannot reach here
	}
	if (PTR_ERR(p) != -1UL) {
		oom_kill_process(p, gfp_mask, order, points, totalpages, NULL,
				 nodemask, "Out of memory");
		killed = 1;
	}
out:
	/*
	 * Give the killed threads a good chance of exiting before trying to
	 * allocate memory again.
	 */
	if (killed)
		schedule_timeout_killable(1);
}

/*
 * The pagefault handler calls here because it is out of memory, so kill a
 * memory-hogging task.  If any populated zone has ZONE_OOM_LOCKED set, a
 * parallel oom killing is already in progress so do nothing.
 */
void pagefault_out_of_memory(void)
{
	struct zonelist *zonelist = node_zonelist(first_online_node,
						  GFP_KERNEL);

	if (try_set_zonelist_oom(zonelist, GFP_KERNEL)) {
		out_of_memory(NULL, 0, 0, NULL, false);
		clear_zonelist_oom(zonelist, GFP_KERNEL);
	}
}
#ifdef CONFIG_KNBD_SUPPORT
EXPORT_SYMBOL(pagefault_out_of_memory);
#endif
